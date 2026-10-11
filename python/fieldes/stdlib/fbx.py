'''
Autodesk FBX: a triangle-mesh reader (binary and ASCII) and writer (ASCII 7.4), written here with no Autodesk code behind it.

FBX is a proprietary format: Autodesk publishes its SDK, under its own licence, but no specification of the file.  What this module knows of the files
is their structure as it is documented by the community (the node tree: a name, properties and nested nodes; the property types; the objects and
the connections between them) -- the Autodesk FBX SDK is not used, and nothing of it is included.  Because the format is not specified, other
programs can read less of what this module writes, or more of what it reads, than it promises: it handles meshes (geometry, the transforms of
the models that hold it, the unit and the up axis of the scene) and nothing else -- no materials, animation, skins or cameras.

    write_fbx(path, xyz, tri, name)         ASCII FBX 7.4, millimetres written as the unit's scale (0.1 cm), Z up
    read_fbx(path)                          (xyz, tri, declared_mm): the triangles of every mesh in the file, in the scene's own coordinates
                                            turned to Z up, and the length of the file's unit in millimetres

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import re
import struct
import zlib
from array import array

__all__ = ['write_fbx', 'read_fbx']


# ---------------------------------------------------------------------------
# Writing

def write_fbx(path, xyz, tri, name='mesh'):
    ''' An ASCII FBX 7.4 file with one mesh: Z up, a unit of one millimetre (the scene's unit scale is 0.1 centimetre) '''
    nv = len(xyz)
    verts = ','.join('%.9g' % v for v in xyz)
    # (a polygon's last index is stored negated, minus one: the end of the polygon)
    poly = []
    for t in range(0, len(tri), 3):
        poly.append('%d,%d,%d' % (tri[t], tri[t + 1], -tri[t + 2] - 1))
    indices = ','.join(poly)
    geometry_id, model_id = 2000000001, 2000000002
    text = '''; FBX 7.4.0 project file
; written by FielDes
; ------------------------------------------------------------------
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
	Creator: "FielDes"
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",2
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",1
		P: "FrontAxisSign", "int", "Integer", "",-1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "OriginalUpAxis", "int", "Integer", "",2
		P: "OriginalUpAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",0.1
		P: "OriginalUnitScaleFactor", "double", "Number", "",0.1
	}
}
Documents:  {
	Count: 1
	Document: 1000000001, "Scene", "Scene" {
		Properties70:  {
			P: "SourceObject", "object", "", ""
			P: "ActiveAnimStackName", "KString", "", "", ""
		}
		RootNode: 0
	}
}
References:  {
}
Definitions:  {
	Version: 100
	Count: 3
	ObjectType: "GlobalSettings" {
		Count: 1
	}
	ObjectType: "Model" {
		Count: 1
		PropertyTemplate: "FbxNode" {
			Properties70:  {
				P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
				P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
				P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
			}
		}
	}
	ObjectType: "Geometry" {
		Count: 1
		PropertyTemplate: "FbxMesh" {
			Properties70:  {
				P: "Color", "ColorRGB", "Color", "",0.8,0.8,0.8
			}
		}
	}
}
Objects:  {
	Geometry: %(g)d, "Geometry::%(name)s", "Mesh" {
		Vertices: *%(nv)d {
			a: %(verts)s
		}
		PolygonVertexIndex: *%(ni)d {
			a: %(indices)s
		}
		GeometryVersion: 124
	}
	Model: %(m)d, "Model::%(name)s", "Mesh" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
			P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
			P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
		}
		Shading: T
		Culling: "CullingOff"
	}
}
Connections:  {
	C: "OO",%(m)d,0
	C: "OO",%(g)d,%(m)d
}
''' % {'g': geometry_id, 'm': model_id, 'name': name, 'nv': nv, 'ni': len(tri), 'verts': verts, 'indices': indices}
    with open(path, 'w', encoding='ascii') as f:
        f.write(text)


# ---------------------------------------------------------------------------
# Reading: both flavours become the same tree of nodes

class _Node:
    __slots__ = ('name', 'props', 'children')

    def __init__(self, name):
        self.name = name
        self.props = []
        self.children = []

    def find(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def findall(self, name):
        return [c for c in self.children if c.name == name]


_BINARY_MAGIC = b'Kaydara FBX Binary  \x00'


def _parse_binary(data):
    version = struct.unpack_from('<I', data, 23)[0]
    wide = version >= 7500
    head = struct.Struct('<QQQB') if wide else struct.Struct('<IIIB')
    null = 25 if wide else 13

    def read_props(pos, count):
        props = []
        for _ in range(count):
            t = chr(data[pos])
            pos += 1
            if t == 'Y':
                props.append(struct.unpack_from('<h', data, pos)[0]); pos += 2
            elif t == 'C':
                props.append(bool(data[pos])); pos += 1
            elif t == 'I':
                props.append(struct.unpack_from('<i', data, pos)[0]); pos += 4
            elif t == 'F':
                props.append(struct.unpack_from('<f', data, pos)[0]); pos += 4
            elif t == 'D':
                props.append(struct.unpack_from('<d', data, pos)[0]); pos += 8
            elif t == 'L':
                props.append(struct.unpack_from('<q', data, pos)[0]); pos += 8
            elif t in 'fdlib':
                length, encoding, clen = struct.unpack_from('<III', data, pos)
                pos += 12
                raw = data[pos:pos + (clen if encoding else length * {'f': 4, 'd': 8, 'l': 8, 'i': 4, 'b': 1}[t])]
                pos += len(raw)
                if encoding == 1:
                    raw = zlib.decompress(raw)
                code = {'f': 'f', 'd': 'd', 'l': 'q', 'i': 'i', 'b': 'b'}[t]
                props.append(list(struct.unpack('<%d%s' % (length, code), raw[:length * struct.calcsize(code)])))
            elif t in 'SR':
                n = struct.unpack_from('<I', data, pos)[0]
                pos += 4
                raw = data[pos:pos + n]
                pos += n
                props.append(raw.decode('utf-8', 'replace') if t == 'S' else raw)
            else:
                raise RuntimeError('unknown FBX property type %r' % t)
        return props, pos

    def read_node(pos):
        end, count, plen, nlen = head.unpack_from(data, pos)
        pos += head.size
        if end == 0:
            return None, pos
        node = _Node(data[pos:pos + nlen].decode('utf-8', 'replace'))
        pos += nlen
        node.props, _ = read_props(pos, count)
        pos += plen
        while pos < end:
            child, pos = read_node(pos)
            if child is None:
                break
            node.children.append(child)
        return node, end

    root = _Node('')
    pos = 27
    size = len(data)
    while pos + head.size <= size:
        node, pos = read_node(pos)
        if node is None:
            break
        root.children.append(node)
    return root


_ASCII_NUMBER = re.compile(r'[-+]?(?:\d+\.?\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?)')


def _ascii_value(token):
    token = token.strip()
    if not token:
        return None
    if token[0] == '"':
        return token[1:-1] if token.endswith('"') else token[1:]
    if _ASCII_NUMBER.fullmatch(token):
        try:
            return int(token)
        except ValueError:
            return float(token)
    return token


def _split_props(text):
    ''' The comma separated values of a node's line: quoted strings stay whole '''
    out, cur, quoted = [], [], False
    for ch in text:
        if ch == '"':
            quoted = not quoted
            cur.append(ch)
        elif ch == ',' and not quoted:
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append(''.join(cur))
    return [_ascii_value(t) for t in out if t.strip()]


def _parse_ascii(text):
    root = _Node('')
    stack = [root]
    array_node = None                    # the node of an array being read (`Name: *N {` then `a: ...`)
    array_text = []
    for raw in text.splitlines():
        line = raw.split(';', 1)[0] if '"' not in raw else raw
        line = line.strip()
        if not line:
            continue
        if array_node is not None:
            if line.startswith('}'):
                body = ' '.join(array_text)
                body = body[body.index('a:') + 2:] if 'a:' in body else body
                values = [float(v) if ('.' in v or 'e' in v.lower()) else int(v) for v in re.split(r'[,\s]+', body.strip()) if v]
                array_node.props.append(values)
                array_node = None
                array_text = []
            else:
                array_text.append(line)
            continue
        if line.startswith('}'):
            if len(stack) > 1:
                stack.pop()
            continue
        colon = line.find(':')
        if colon < 0:
            continue
        name = line[:colon].strip()
        rest = line[colon + 1:].strip()
        node = _Node(name)
        stack[-1].children.append(node)
        opens = rest.endswith('{')
        if opens:
            rest = rest[:-1].strip()
        if rest.startswith('*'):
            # (an array: its count, then the numbers inside the braces)
            if opens:
                array_node = node
                array_text = []
            continue
        node.props = _split_props(rest)
        if opens:
            stack.append(node)
    return root


# ---------------------------------------------------------------------------
# From the node tree to triangles

def _matrix_mul(a, b):
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


def _local_matrix(model):
    ''' T * R * S of a model, from its Lcl properties (Euler angles in degrees, XYZ order), row-vector convention: p' = p * M '''
    t, r, s = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)
    pre = (0.0, 0.0, 0.0)
    props = model.find('Properties70')
    for p in (props.findall('P') if props else []):
        if len(p.props) < 5:
            continue
        key = p.props[0]
        v = tuple(float(x) for x in p.props[4:7]) if len(p.props) >= 7 else None
        if v is None:
            continue
        if key == 'Lcl Translation':
            t = v
        elif key == 'Lcl Rotation':
            r = v
        elif key == 'Lcl Scaling':
            s = v
        elif key == 'PreRotation':
            pre = v

    def rot(angles):
        ax, ay, az = (math.radians(a) for a in angles)
        cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
        # (x first, then y, then z: R = Rz * Ry * Rx applied to column vectors; the transpose for row vectors)
        rx = [1, 0, 0, 0, cx, -sx, 0, sx, cx]
        ry = [cy, 0, sy, 0, 1, 0, -sy, 0, cy]
        rz = [cz, -sz, 0, sz, cz, 0, 0, 0, 1]

        def mul3(a, b):
            return [sum(a[i * 3 + k] * b[k * 3 + j] for k in range(3)) for i in range(3) for j in range(3)]
        m = mul3(rz, mul3(ry, rx))
        return [m[0], m[3], m[6], 0, m[1], m[4], m[7], 0, m[2], m[5], m[8], 0, 0, 0, 0, 1]

    def scale(v):
        return [v[0], 0, 0, 0, 0, v[1], 0, 0, 0, 0, v[2], 0, 0, 0, 0, 1]

    def move(v):
        return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, v[0], v[1], v[2], 1]

    m = scale(s)                                           # (row vectors: scale, then rotate, then translate)
    m = _matrix_mul(m, rot(r))
    m = _matrix_mul(m, rot(pre)) if any(pre) else m
    return _matrix_mul(m, move(t))


def _scene_settings(root):
    ''' (unit in millimetres, up axis, sign) of the scene: FBX's own default unit is the centimetre '''
    unit_mm, up, sign = 10.0, 1, 1
    gs = root.find('GlobalSettings')
    props = gs.find('Properties70') if gs else None
    for p in (props.findall('P') if props else []):
        if len(p.props) < 5:
            continue
        key = p.props[0]
        if key == 'UnitScaleFactor':
            unit_mm = float(p.props[4]) * 10.0
        elif key == 'UpAxis':
            up = int(p.props[4])
        elif key == 'UpAxisSign':
            sign = int(p.props[4])
    return unit_mm, up, sign


def read_fbx(path):
    ''' (xyz, tri, declared_mm) of every mesh in an FBX file (binary or ASCII): each model's transform and those of its parents are applied, polygons
        are fanned, and a scene with Y up is turned to Z up (x, -z, y); `declared_mm` is the length of the scene's unit in millimetres '''
    with open(path, 'rb') as f:
        data = f.read()
    if data.startswith(_BINARY_MAGIC):
        root = _parse_binary(data)
    else:
        text = data.decode('utf-8', 'replace')
        if 'FBX' not in text[:400] and 'Objects:' not in text:
            raise RuntimeError('not an FBX file')
        root = _parse_ascii(text)
    unit_mm, up, sign = _scene_settings(root)
    objects = root.find('Objects')
    if objects is None:
        raise RuntimeError('the FBX file has no Objects')
    geometries, models = {}, {}
    for node in objects.children:
        if node.name == 'Geometry' and node.props:
            verts, idx = node.find('Vertices'), node.find('PolygonVertexIndex')
            if verts is not None and idx is not None and verts.props and idx.props:
                geometries[node.props[0]] = (verts.props[0], idx.props[0])
        elif node.name == 'Model' and node.props:
            models[node.props[0]] = node
    parent, owner = {}, {}
    connections = root.find('Connections')
    for c in (connections.findall('C') if connections else []):
        if len(c.props) >= 3 and c.props[0] in ('OO', 'OP'):
            child, par = c.props[1], c.props[2]
            if child in geometries and par in models:
                owner.setdefault(child, par)
            elif child in models:
                parent[child] = par
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]

    def world(model_id):
        m, depth = identity, 0
        while model_id in models and depth < 64:
            m = _matrix_mul(m, _local_matrix(models[model_id]))
            model_id = parent.get(model_id)
            depth += 1
        return m

    out_xyz = array('f')
    out_tri = array('I')
    for gid, (verts, idx) in geometries.items():
        m = world(owner.get(gid))
        base = len(out_xyz) // 3
        for i in range(0, len(verts) - 2, 3):
            x, y, z = verts[i], verts[i + 1], verts[i + 2]
            px = x * m[0] + y * m[4] + z * m[8] + m[12]
            py = x * m[1] + y * m[5] + z * m[9] + m[13]
            pz = x * m[2] + y * m[6] + z * m[10] + m[14]
            if up == 1:
                px, py, pz = px, -pz * sign, py * sign         # Y up -> Z up
            elif up == 0:
                px, py, pz = py * sign, pz, px * sign          # X up -> Z up (a quarter turn: the rest is kept)
            out_xyz.extend((px, py, pz))
        poly = []
        for v in idx:
            if v < 0:
                poly.append(-v - 1)
                for j in range(1, len(poly) - 1):
                    out_tri.extend((base + poly[0], base + poly[j], base + poly[j + 1]))
                poly = []
            else:
                poly.append(v)
    if not len(out_tri):
        raise RuntimeError('the FBX file has no polygon meshes')
    return out_xyz, out_tri, unit_mm
