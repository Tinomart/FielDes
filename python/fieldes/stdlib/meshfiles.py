'''
Triangle-mesh files: the writers behind export_mesh() and the readers of the formats the library does not read itself.

    write_mesh(path, xyz, tri)      the format is the file's extension: .stl .obj .ply .3mf .glb .gltf .fbx .off .dxf

A mesh is two flat sequences: `xyz` (x0, y0, z0, x1, ...) in millimetres and `tri` (a0, b0, c0, a1, ...), the triangles' vertices as indices
into it, counter-clockwise seen from outside.  Each format is written as its own specification says, with no library behind it:

    .stl   binary (ascii=True: text)        millimetres by convention
    .obj   Wavefront                         millimetres by convention
    .ply   binary little endian              millimetres by convention
    .3mf   3D Manufacturing Format           states its unit (millimetre)
    .glb   glTF 2.0 binary, .gltf with its buffer inside  metres, Y up: the numbers are turned into them
    .fbx   Autodesk FBX 7.4 ASCII (fbx.py)  states its unit (centimetre scale 0.1) and its axes (Z up)
    .off   Object File Format               millimetres by convention
    .dxf   AutoCAD DXF, 3DFACE entities     states its unit ($INSUNITS 4: millimetres)

The coordinates are written in the length unit chosen in Settings > Units (write_mesh(..., unit='in') says another; millimetres by default); the formats that state their
unit say it, and a glTF is in metres whatever is chosen.

and read back by the importers: STL, OBJ, PLY, 3MF and glTF by mesh_import.py, OFF, DXF (3DFACE and polyface meshes) and FBX here and in fbx.py.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import base64
import json
import math
import os
import struct
import zipfile
import zlib
from array import array

from fieldes import units as _units

__all__ = ['write_mesh', 'MESH_WRITE_FORMATS', 'MESH_READ_FORMATS']

# What can be written, and read (by extension)
MESH_WRITE_FORMATS = ('.stl', '.obj', '.ply', '.3mf', '.glb', '.gltf', '.fbx', '.off', '.dxf')
MESH_READ_FORMATS = ('.stl', '.obj', '.ply', '.3mf', '.glb', '.gltf', '.fbx', '.off', '.dxf')


def _clean_name(name):
    name = ''.join(c if (c.isalnum() or c in '_-') else '_' for c in str(name or 'fielDes'))
    return name or 'mesh'


def _bounds(xyz):
    n = len(xyz) // 3
    if not n:
        return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
    lo = [min(xyz[i::3]) for i in range(3)]
    hi = [max(xyz[i::3]) for i in range(3)]
    return lo, hi


def _num(v):
    ''' A number as short text that reads back as the same float32 '''
    return '%.9g' % v


# ---------------------------------------------------------------------------
# STL

def write_stl(path, xyz, tri, name='fieldes', ascii=False):
    n = len(tri) // 3
    if ascii:
        out = ['solid %s\n' % _clean_name(name)]
        for t in range(n):
            a, b, c = tri[3 * t] * 3, tri[3 * t + 1] * 3, tri[3 * t + 2] * 3
            p = (xyz[a], xyz[a + 1], xyz[a + 2]), (xyz[b], xyz[b + 1], xyz[b + 2]), (xyz[c], xyz[c + 1], xyz[c + 2])
            nx, ny, nz = _normal(*p)
            out.append(' facet normal %s %s %s\n  outer loop\n' % (_num(nx), _num(ny), _num(nz)))
            for q in p:
                out.append('   vertex %s %s %s\n' % (_num(q[0]), _num(q[1]), _num(q[2])))
            out.append('  endloop\n endfacet\n')
        out.append('endsolid %s\n' % _clean_name(name))
        with open(path, 'w', encoding='ascii') as f:
            f.write(''.join(out))
        return
    buf = bytearray(84 + 50 * n)
    struct.pack_into('<80sI', buf, 0, (b'FielDes ' + _clean_name(name).encode('ascii', 'replace'))[:80].ljust(80, b' '), n)
    pack = struct.Struct('<12fH').pack_into
    pos = 84
    for t in range(n):
        a, b, c = tri[3 * t] * 3, tri[3 * t + 1] * 3, tri[3 * t + 2] * 3
        p0, p1, p2 = (xyz[a], xyz[a + 1], xyz[a + 2]), (xyz[b], xyz[b + 1], xyz[b + 2]), (xyz[c], xyz[c + 1], xyz[c + 2])
        nx, ny, nz = _normal(p0, p1, p2)
        pack(buf, pos, nx, ny, nz, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], 0)
        pos += 50
    with open(path, 'wb') as f:
        f.write(buf)


def _normal(p0, p1, p2):
    ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    vx, vy, vz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length < 1e-30:
        return 0.0, 0.0, 0.0
    return nx / length, ny / length, nz / length


def read_stl(path):
    ''' A binary or ASCII STL: (xyz, tri) with the vertices that share a place welded '''
    with open(path, 'rb') as f:
        data = f.read()
    corners = []
    if len(data) >= 84 and 84 + 50 * struct.unpack_from('<I', data, 80)[0] == len(data):
        n = struct.unpack_from('<I', data, 80)[0]
        unpack = struct.Struct('<12fH').unpack_from
        for t in range(n):
            v = unpack(data, 84 + 50 * t)
            corners.append((v[3], v[4], v[5]))
            corners.append((v[6], v[7], v[8]))
            corners.append((v[9], v[10], v[11]))
    else:
        for line in data.decode('ascii', 'replace').splitlines():
            w = line.split()
            if len(w) == 4 and w[0] == 'vertex':
                corners.append((float(w[1]), float(w[2]), float(w[3])))
        if len(corners) % 3:
            raise RuntimeError('not an STL file')
    xyz, tri, seen = array('f'), array('I'), {}
    for p in corners:
        k = seen.get(p)
        if k is None:
            k = seen[p] = len(xyz) // 3
            xyz.extend(p)
        tri.append(k)
    return xyz, tri


def convert_mesh(source, target):
    ''' A mesh file written in another format: `source` (an STL) read, `target` written in the format of its extension '''
    xyz, tri = read_stl(source)
    write_mesh(target, xyz, tri)
    return len(tri) // 3


# ---------------------------------------------------------------------------
# OBJ

def write_obj(path, xyz, tri, name='fieldes'):
    out = ['# FielDes (millimetres)\n', 'o %s\n' % _clean_name(name)]
    for i in range(0, len(xyz), 3):
        out.append('v %s %s %s\n' % (_num(xyz[i]), _num(xyz[i + 1]), _num(xyz[i + 2])))
    for t in range(0, len(tri), 3):
        out.append('f %d %d %d\n' % (tri[t] + 1, tri[t + 1] + 1, tri[t + 2] + 1))
    with open(path, 'w', encoding='ascii') as f:
        f.write(''.join(out))


# ---------------------------------------------------------------------------
# PLY

def write_ply(path, xyz, tri, name='fieldes'):
    nv, nt = len(xyz) // 3, len(tri) // 3
    head = ('ply\nformat binary_little_endian 1.0\ncomment FielDes (millimetres) %s\n'
            'element vertex %d\nproperty float x\nproperty float y\nproperty float z\n'
            'element face %d\nproperty list uchar int vertex_indices\nend_header\n'
            % (_clean_name(name), nv, nt)).encode('ascii')
    verts = array('f', xyz)
    if struct.pack('=I', 1) != struct.pack('<I', 1):
        verts.byteswap()
    pack = struct.Struct('<B3i').pack
    faces = b''.join(pack(3, tri[t], tri[t + 1], tri[t + 2]) for t in range(0, len(tri), 3))
    with open(path, 'wb') as f:
        f.write(head)
        f.write(verts.tobytes())
        f.write(faces)


# ---------------------------------------------------------------------------
# 3MF

def write_3mf(path, xyz, tri, name='fieldes', unit='mm'):
    verts = ''.join('<vertex x="%s" y="%s" z="%s"/>' % (_num(xyz[i]), _num(xyz[i + 1]), _num(xyz[i + 2])) for i in range(0, len(xyz), 3))
    tris = ''.join('<triangle v1="%d" v2="%d" v3="%d"/>' % (tri[t], tri[t + 1], tri[t + 2]) for t in range(0, len(tri), 3))
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="' + _units.threemf_unit(unit) + '" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<metadata name="Application">FielDes</metadata>'
             '<resources><object id="1" type="model" name="%s"><mesh><vertices>%s</vertices><triangles>%s</triangles></mesh></object></resources>'
             '<build><item objectid="1"/></build></model>' % (_clean_name(name), verts, tris))
    types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
             '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
             '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', types)
        z.writestr('_rels/.rels', rels)
        z.writestr('3D/3dmodel.model', model)


def write_3mf_objects(path, objects):
    ''' A 3MF with several objects: [(name, xyz, tri, kind)] -- kind 'model' (a part), 'support' (a support that is not part of the print's shape) or 'other' (a volume that is not
        printed: a no-support region).  Each is an object of its own in the file and an item of the build, so that a slicer lists them apart '''
    resources, items = [], []
    for k, (name, xyz, tri, kind) in enumerate(objects, start=1):
        verts = ''.join('<vertex x="%s" y="%s" z="%s"/>' % (_num(xyz[i]), _num(xyz[i + 1]), _num(xyz[i + 2])) for i in range(0, len(xyz), 3))
        tris = ''.join('<triangle v1="%d" v2="%d" v3="%d"/>' % (tri[t], tri[t + 1], tri[t + 2]) for t in range(0, len(tri), 3))
        resources.append('<object id="%d" type="%s" name="%s"><mesh><vertices>%s</vertices><triangles>%s</triangles></mesh></object>' % (
            k, kind if kind in ('model', 'support', 'solidsupport', 'surface', 'other') else 'model', _clean_name(name), verts, tris))
        items.append('<item objectid="%d"/>' % k)
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<metadata name="Application">FielDes</metadata><resources>%s</resources><build>%s</build></model>' % (''.join(resources), ''.join(items)))
    types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
             '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
             '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', types)
        z.writestr('_rels/.rels', rels)
        z.writestr('3D/3dmodel.model', model)


# ---------------------------------------------------------------------------
# glTF 2.0 (.glb, .gltf): metres, Y up

def _gltf_arrays(xyz, tri):
    ''' The positions in metres with Y up (x, z, -y: FielDes's Z up turned the way mesh_import turns glTF back), as float32 bytes, and the indices '''
    pos = array('f')
    for i in range(0, len(xyz), 3):
        pos.extend((xyz[i] * 0.001, xyz[i + 2] * 0.001, -xyz[i + 1] * 0.001))
    idx = array('I', tri)
    if struct.pack('=I', 1) != struct.pack('<I', 1):
        pos.byteswap()
        idx.byteswap()
    lo = [min(pos[i::3]) for i in range(3)] if len(pos) else [0.0] * 3
    hi = [max(pos[i::3]) for i in range(3)] if len(pos) else [0.0] * 3
    return pos.tobytes(), idx.tobytes(), lo, hi


def _gltf_document(nbytes_pos, nbytes_idx, nv, nt, lo, hi, name, uri=None):
    buffer = {'byteLength': nbytes_pos + nbytes_idx}
    if uri is not None:
        buffer['uri'] = uri
    return {
        'asset': {'version': '2.0', 'generator': 'FielDes'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [{'mesh': 0, 'name': _clean_name(name)}],
        'meshes': [{'name': _clean_name(name), 'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1, 'mode': 4}]}],
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': nv, 'type': 'VEC3', 'min': lo, 'max': hi},
            {'bufferView': 1, 'componentType': 5125, 'count': nt * 3, 'type': 'SCALAR'},
        ],
        'bufferViews': [
            {'buffer': 0, 'byteOffset': 0, 'byteLength': nbytes_pos, 'target': 34962},
            {'buffer': 0, 'byteOffset': nbytes_pos, 'byteLength': nbytes_idx, 'target': 34963},
        ],
        'buffers': [buffer],
    }


def write_glb(path, xyz, tri, name='fieldes'):
    pos, idx, lo, hi = _gltf_arrays(xyz, tri)
    doc = _gltf_document(len(pos), len(idx), len(xyz) // 3, len(tri) // 3, lo, hi, name)
    text = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    text += b' ' * (-len(text) % 4)
    binary = pos + idx
    binary += b'\0' * (-len(binary) % 4)
    total = 12 + 8 + len(text) + 8 + len(binary)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, total))
        f.write(struct.pack('<I4s', len(text), b'JSON'))
        f.write(text)
        f.write(struct.pack('<I4s', len(binary), b'BIN\0'))
        f.write(binary)


def write_gltf(path, xyz, tri, name='fieldes'):
    pos, idx, lo, hi = _gltf_arrays(xyz, tri)
    uri = 'data:application/octet-stream;base64,' + base64.b64encode(pos + idx).decode('ascii')
    doc = _gltf_document(len(pos), len(idx), len(xyz) // 3, len(tri) // 3, lo, hi, name, uri)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(doc, f, separators=(',', ':'))


# ---------------------------------------------------------------------------
# OFF

def write_off(path, xyz, tri, name='fieldes'):
    out = ['OFF\n', '%d %d 0\n' % (len(xyz) // 3, len(tri) // 3)]
    for i in range(0, len(xyz), 3):
        out.append('%s %s %s\n' % (_num(xyz[i]), _num(xyz[i + 1]), _num(xyz[i + 2])))
    for t in range(0, len(tri), 3):
        out.append('3 %d %d %d\n' % (tri[t], tri[t + 1], tri[t + 2]))
    with open(path, 'w', encoding='ascii') as f:
        f.write(''.join(out))


def read_off(path):
    ''' An Object File Format mesh (OFF; the counts on the header line or the next; a face line may carry colours after its indices): (xyz, tri) --
        polygons are fanned '''
    with open(path, 'r', encoding='ascii', errors='replace') as f:
        lines = [ln.split('#', 1)[0].strip() for ln in f]
    lines = [ln for ln in lines if ln]
    if not lines or not lines[0].upper().startswith(('OFF', 'COFF', 'NOFF')):
        raise RuntimeError('not an OFF file (it does not start with "OFF")')
    head = lines[0].split()
    if len(head) >= 3 and head[0].upper() == 'OFF':
        counts, at = head[1:3], 1
    else:
        counts, at = lines[1].split()[:2], 2
    nv, nf = int(counts[0]), int(counts[1])
    xyz = array('f')
    for ln in lines[at:at + nv]:
        w = ln.split()
        xyz.extend((float(w[0]), float(w[1]), float(w[2])))
    tri = array('I')
    for ln in lines[at + nv:at + nv + nf]:
        w = ln.split()
        k = int(w[0])
        poly = [int(x) for x in w[1:1 + k]]
        for j in range(1, k - 1):
            tri.extend((poly[0], poly[j], poly[j + 1]))
    return xyz, tri


# ---------------------------------------------------------------------------
# DXF

def write_dxf(path, xyz, tri, name='fieldes', unit='mm'):
    out = ['0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n%d\n0\nENDSEC\n' % _units.dxf_unit(unit),
           '0\nSECTION\n2\nENTITIES\n']
    layer = _clean_name(name)
    for t in range(0, len(tri), 3):
        corners = []
        for k in range(3):
            i = tri[t + k] * 3
            corners.append((xyz[i], xyz[i + 1], xyz[i + 2]))
        out.append('0\n3DFACE\n8\n%s\n' % layer)
        for k, (x, y, z) in enumerate(corners + [corners[2]]):
            out.append('%d\n%s\n%d\n%s\n%d\n%s\n' % (10 + k, _num(x), 20 + k, _num(y), 30 + k, _num(z)))
    out.append('0\nENDSEC\n0\nEOF\n')
    with open(path, 'w', encoding='ascii') as f:
        f.write(''.join(out))


_DXF_UNITS_MM = {1: 25.4, 2: 304.8, 3: 1609344.0, 4: 1.0, 5: 10.0, 6: 1000.0, 7: 1e6, 8: 25.4e-6, 9: 0.0254, 10: 914.4,
                 13: 0.001, 14: 100.0, 15: 1e4, 16: 1e5}


def read_dxf(path):
    ''' A DXF mesh: its 3DFACE entities (a quad is two triangles) and polyface meshes (POLYLINE with VERTEX records, flags 64 and 128).
        Returns (xyz, tri, declared_mm): the unit the file says ($INSUNITS) in millimetres, or None '''
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        raw = f.read().splitlines()
    if len(raw) < 2:
        raise RuntimeError('not a DXF file')
    pairs = []
    for i in range(0, len(raw) - 1, 2):
        try:
            pairs.append((int(raw[i].strip()), raw[i + 1].strip()))
        except ValueError:
            raise RuntimeError('not an ASCII DXF file (a group code is not a number: binary DXF is not read)')
    xyz = array('f')
    tri = array('I')
    declared = None
    section = None
    entity = None
    data = {}
    poly = None                      # the polyface being read: [vertices, faces]

    def corner(k):
        return (float(data.get(10 + k, 0)), float(data.get(20 + k, 0)), float(data.get(30 + k, 0)))

    def finish(kind):
        nonlocal poly
        if kind == '3DFACE':
            c = [corner(k) for k in range(4)]
            base = len(xyz) // 3
            for p in c:
                xyz.extend(p)
            tri.extend((base, base + 1, base + 2))
            if c[3] != c[2]:
                tri.extend((base, base + 2, base + 3))
        elif kind == 'VERTEX' and poly is not None:
            flags = int(data.get(70, 0))
            if flags & 128 and not flags & 64:                       # a face: the corners are the numbers of its vertices
                idx = [int(data.get(c, 0)) for c in (71, 72, 73, 74)]
                idx = [abs(i) - 1 for i in idx if i != 0]
                poly[1].append(idx)
            else:
                poly[0].append(corner(0))
        elif kind == 'SEQEND' and poly is not None:
            base = len(xyz) // 3
            for p in poly[0]:
                xyz.extend(p)
            for idx in poly[1]:
                for j in range(1, len(idx) - 1):
                    tri.extend((base + idx[0], base + idx[j], base + idx[j + 1]))
            poly = None

    var = None
    for code, value in pairs:
        if code == 0:
            if entity is not None and section == 'ENTITIES':
                finish(entity)
            data = {}
            if value == 'SECTION':
                entity = None
                section = '?'
            elif value == 'ENDSEC':
                entity = None
                section = None
            elif value == 'POLYLINE' and section == 'ENTITIES':
                entity = 'POLYLINE'
                poly = [[], []]
            else:
                entity = value
            continue
        if code == 2 and section == '?':
            section = value
            continue
        if section == 'HEADER':
            if code == 9:
                var = value
            elif var == '$INSUNITS' and code == 70:
                declared = _DXF_UNITS_MM.get(int(value))
            continue
        if entity is not None:
            data[code] = value
    if entity is not None and section == 'ENTITIES':
        finish(entity)
    if not len(tri):
        raise RuntimeError('the DXF file has no 3DFACE or polyface mesh entities (only meshes made of faces are read)')
    return xyz, tri, declared


# ---------------------------------------------------------------------------

def write_mesh(path, xyz, tri, name=None, ascii=False, unit=None):
    ''' Writes a mesh in the format of the file's extension (see the top of this file), in the length unit `unit` ('mm', 'cm', 'm', 'in', 'ft' or 'um'): the one
        chosen in Settings > Units when it is not given.  `xyz` is in millimetres '''
    path = os.fspath(path)
    ext = os.path.splitext(path)[1].lower()
    name = name or os.path.splitext(os.path.basename(path))[0]
    if len(tri) % 3 or len(xyz) % 3:
        raise ValueError('a mesh is flat lists of x, y, z and of triangle corners')
    unit = _units.export_unit() if unit is None else _units.check_length(unit)
    if unit != 'mm' and ext not in ('.glb', '.gltf'):         # (a glTF is in metres whatever is chosen: that is what glTF is)
        factor = 1.0 / _units.LENGTH[unit]
        xyz = array('f', (v * factor for v in xyz))
    if ext == '.stl':
        write_stl(path, xyz, tri, name, ascii)
    elif ext == '.obj':
        write_obj(path, xyz, tri, name)
    elif ext == '.ply':
        write_ply(path, xyz, tri, name)
    elif ext == '.3mf':
        write_3mf(path, xyz, tri, name, unit)
    elif ext == '.glb':
        write_glb(path, xyz, tri, name)
    elif ext == '.gltf':
        write_gltf(path, xyz, tri, name)
    elif ext == '.off':
        write_off(path, xyz, tri, name)
    elif ext == '.dxf':
        write_dxf(path, xyz, tri, name, unit)
    elif ext == '.fbx':
        from fieldes.stdlib import fbx
        fbx.write_fbx(path, xyz, tri, _clean_name(name), _units.LENGTH[unit])
    else:
        raise ValueError('cannot write {!r}: the mesh formats are {}'.format(ext, ', '.join(MESH_WRITE_FORMATS)))
    return path
