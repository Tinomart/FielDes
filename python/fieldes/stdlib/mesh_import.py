'''
Triangle-mesh import (what import_model() does with a mesh file): STL (binary or ASCII), Wavefront OBJ, PLY
(ASCII or binary), 3MF and glTF (.glb / .gltf) files.

    parts = import_model(r"C:\\models\\bracket.stl")
    bracket, bounds = parts[0]
    view.set_bounds(*roi(parts))

The mesh becomes an exact signed distance field (negative inside), so it
works with everything else in fieldes.stdlib: union / difference with CSG
shapes, offset / shell / blend, transforms and so on.  Before that its
triangles are cleaned up (duplicate vertices welded, degenerate triangles
dropped, windings made consistent, inside-out shells flipped).  Meshes with
holes still import: near a hole, inside and outside come from the
generalised winding number, which closes it with a smooth membrane.

STL, OBJ and PLY files carry no units: pass file_units= to say what their
numbers mean (e.g. 'm' or 'in'; millimetres otherwise).  3MF files state
their unit and glTF is always in metres; that is used unless file_units=
overrides it.  glTF's Y-up axes are turned into FielDes's Z-up.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import os
import struct
import sys
from array import array

from fieldes.ffi import lib, libfive_mesh_import_info_t
from fieldes.shape import Shape
from fieldes.stdlib import cad_import as _cad
from fieldes.stdlib.content_cache import cache_for

__all__ = ['mesh_info']


def mesh_info(path):
    ''' The summary of the last import of `path` in this session (triangle
        count, whether it is watertight, ...) as a dict, or None. '''
    entry = _cad._import_log.get(os.path.normcase(os.path.abspath(path)))
    return None if entry is None else entry.get('mesh')


# ---------------------------------------------------------------------------
# PLY

_PLY_TYPES = {'char': 'b', 'int8': 'b', 'uchar': 'B', 'uint8': 'B',
              'short': 'h', 'int16': 'h', 'ushort': 'H', 'uint16': 'H',
              'int': 'i', 'int32': 'i', 'uint': 'I', 'uint32': 'I',
              'float': 'f', 'float32': 'f', 'double': 'd', 'float64': 'd'}


def _fan(poly, out):
    for k in range(1, len(poly) - 1):
        out.extend((poly[0], poly[k], poly[k + 1]))


def _read_ply(path):
    with open(path, 'rb') as f:
        data = f.read()
    end = data.find(b'end_header')
    if not data.startswith(b'ply') or end < 0:
        raise RuntimeError('not a PLY file (no "ply" / "end_header")')
    body = data[data.index(b'\n', end) + 1:]

    fmt = None
    elements = []           # [name, count, props]; prop = (name, type[, count type])
    for line in data[:end].decode('ascii', 'replace').splitlines():
        w = line.split()
        if not w:
            continue
        if w[0] == 'format':
            fmt = w[1]
        elif w[0] == 'element':
            elements.append([w[1], int(w[2]), []])
        elif w[0] == 'property' and elements:
            if w[1] == 'list':
                elements[-1][2].append((w[4], w[3], w[2]))
            else:
                elements[-1][2].append((w[2], w[1]))
    if fmt not in ('ascii', 'binary_little_endian', 'binary_big_endian'):
        raise RuntimeError('unsupported PLY format {!r}'.format(fmt))

    xyz = array('f')
    tris = array('I')
    if fmt == 'ascii':
        tokens = body.split()
        pos = 0
        for name, count, props in elements:
            for _ in range(count):
                values = {}
                for p in props:
                    if len(p) == 3:                                  # list
                        n = int(tokens[pos])
                        values[p[0]] = [int(float(t)) for t in tokens[pos + 1:pos + 1 + n]]
                        pos += 1 + n
                    else:
                        values[p[0]] = float(tokens[pos])
                        pos += 1
                if name == 'vertex':
                    xyz.extend((values['x'], values['y'], values['z']))
                elif name == 'face':
                    idx = values.get('vertex_indices', values.get('vertex_index'))
                    if idx:
                        _fan(idx, tris)
        return xyz, tris

    e = '<' if fmt == 'binary_little_endian' else '>'
    pos = 0
    for name, count, props in elements:
        lists = [p for p in props if len(p) == 3]
        if not lists:
            rec = struct.Struct(e + ''.join(_PLY_TYPES[p[1]] for p in props))
            block = body[pos:pos + rec.size * count]
            if name == 'vertex':
                names = [p[0] for p in props]
                ix, iy, iz = names.index('x'), names.index('y'), names.index('z')
                for r in rec.iter_unpack(block):
                    xyz.extend((r[ix], r[iy], r[iz]))
            pos += rec.size * count
            continue

        # Elements with a list: fast path when every face is a triangle
        before = ''.join(_PLY_TYPES[p[1]] for p in props[:props.index(lists[0])])
        after_props = props[props.index(lists[0]) + 1:]
        simple = len(lists) == 1 and not any(len(p) == 3 for p in after_props)
        if simple:
            lp = lists[0]
            ct, it = _PLY_TYPES[lp[2]], _PLY_TYPES[lp[1]]
            head = struct.calcsize(e + before)
            rec = struct.Struct(e + before + ct + it * 3 +
                                ''.join(_PLY_TYPES[p[1]] for p in after_props))
            cnt = struct.Struct(e + ct)
            size = rec.size * count
            if len(body) >= pos + size and all(
                    cnt.unpack_from(body, pos + k * rec.size + head)[0] == 3
                    for k in range(0, count, max(1, count // 64))):
                block = body[pos:pos + size]
                n_before = len(before)
                ok = True
                faces = array('I')
                for r in rec.iter_unpack(block):
                    if r[n_before] != 3:
                        ok = False
                        break
                    faces.extend(r[n_before + 1:n_before + 4])
                if ok:
                    if name == 'face':
                        tris.extend(faces)
                    pos += size
                    continue
        # General (slow) path: record by record
        for _ in range(count):
            values = {}
            for p in props:
                if len(p) == 3:
                    ct = struct.Struct(e + _PLY_TYPES[p[2]])
                    n = ct.unpack_from(body, pos)[0]
                    pos += ct.size
                    it = struct.Struct(e + _PLY_TYPES[p[1]] * n)
                    values[p[0]] = list(it.unpack_from(body, pos))
                    pos += it.size
                else:
                    t = struct.Struct(e + _PLY_TYPES[p[1]])
                    values[p[0]] = t.unpack_from(body, pos)[0]
                    pos += t.size
            if name == 'vertex':
                xyz.extend((values['x'], values['y'], values['z']))
            elif name == 'face':
                idx = values.get('vertex_indices', values.get('vertex_index'))
                if idx:
                    _fan(idx, tris)
    return xyz, tris


# ---------------------------------------------------------------------------
# 3MF

_3MF_UNITS_MM = {'micron': 0.001, 'millimeter': 1.0, 'centimeter': 10.0,
                 'inch': 25.4, 'foot': 304.8, 'meter': 1000.0}
_IDENTITY = (1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)


def _transform(text):
    ''' 3MF's "m00 m01 m02 m10 m11 m12 m20 m21 m22 m30 m31 m32" (row
        vectors: p' = p * M, the last row being the translation) '''
    if not text:
        return _IDENTITY
    m = tuple(float(v) for v in text.split())
    return m if len(m) == 12 else _IDENTITY


def _compose(a, b):
    ''' The transform applying a, then b '''
    def row(r):
        x, y, z = r
        return (x * b[0] + y * b[3] + z * b[6],
                x * b[1] + y * b[4] + z * b[7],
                x * b[2] + y * b[5] + z * b[8])
    r0, r1, r2 = row(a[0:3]), row(a[3:6]), row(a[6:9])
    t = row(a[9:12])
    return r0 + r1 + r2 + (t[0] + b[9], t[1] + b[10], t[2] + b[11])


def _read_3mf(path):
    import zipfile
    import xml.etree.ElementTree as ET
    try:
        z = zipfile.ZipFile(path)
    except zipfile.BadZipFile:
        raise RuntimeError('not a 3MF file (not a zip archive)')
    with z:
        name = None
        try:
            for r in ET.fromstring(z.read('_rels/.rels')):
                if r.get('Type', '').endswith('/3dmodel'):
                    name = r.get('Target', '').lstrip('/')
        except KeyError:
            pass
        names = z.namelist()
        if name not in names:
            name = next((n for n in names if n.lower().endswith('.model')), None)
        if name is None:
            raise RuntimeError('the 3MF file has no 3D model part')
        root = ET.fromstring(z.read(name))

    ns = root.tag[:root.tag.index('}') + 1] if root.tag.startswith('{') else ''
    unit_mm = _3MF_UNITS_MM.get(root.get('unit', 'millimeter'), 1.0)

    objects = {}
    resources = root.find(ns + 'resources')
    for obj in (resources.findall(ns + 'object') if resources is not None else []):
        mesh = obj.find(ns + 'mesh')
        if mesh is not None:
            xyz = array('f')
            for v in mesh.iter(ns + 'vertex'):
                xyz.extend((float(v.get('x')), float(v.get('y')), float(v.get('z'))))
            tri = array('I')
            for t in mesh.iter(ns + 'triangle'):
                tri.extend((int(t.get('v1')), int(t.get('v2')), int(t.get('v3'))))
            objects[obj.get('id')] = ('mesh', xyz, tri)
        else:
            comps = obj.find(ns + 'components')
            parts = [] if comps is None else [
                (c.get('objectid'), _transform(c.get('transform')))
                for c in comps.findall(ns + 'component')]
            objects[obj.get('id')] = ('components', parts)

    out_xyz = array('f')
    out_tri = array('I')

    def emit(oid, m, depth):
        o = objects.get(oid)
        if o is None or depth > 32:
            return
        if o[0] == 'components':
            for cid, t in o[1]:
                emit(cid, _compose(t, m), depth + 1)
            return
        base = len(out_xyz) // 3
        xyz = o[1]
        for i in range(0, len(xyz), 3):
            x, y, z = xyz[i], xyz[i + 1], xyz[i + 2]
            out_xyz.extend((x * m[0] + y * m[3] + z * m[6] + m[9],
                            x * m[1] + y * m[4] + z * m[7] + m[10],
                            x * m[2] + y * m[5] + z * m[8] + m[11]))
        out_tri.extend(base + i for i in o[2])

    build = root.find(ns + 'build')
    items = build.findall(ns + 'item') if build is not None else []
    if items:
        for item in items:
            emit(item.get('objectid'), _transform(item.get('transform')), 0)
    else:
        for oid in objects:
            emit(oid, _IDENTITY, 0)
    return out_xyz, out_tri, unit_mm


# ---------------------------------------------------------------------------
# glTF 2.0 (.glb binary, .gltf JSON)

_GLTF_COMPONENTS = {5120: ('b', 127.0), 5121: ('B', 255.0), 5122: ('h', 32767.0),
                    5123: ('H', 65535.0), 5125: ('I', None), 5126: ('f', None)}
_GLTF_WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def _gltf_load(path):
    ''' (json dict, list of buffers as bytes) '''
    import base64
    import json
    with open(path, 'rb') as f:
        data = f.read()
    buffers = []
    if data[:4] == b'glTF':
        version, length = struct.unpack_from('<II', data, 4)
        if version != 2:
            raise RuntimeError('glTF version {} is not supported (only 2)'.format(version))
        pos = 12
        doc = None
        binary = None
        while pos + 8 <= min(length, len(data)):
            size, kind = struct.unpack_from('<II', data, pos)
            chunk = data[pos + 8:pos + 8 + size]
            if kind == 0x4E4F534A:          # JSON
                doc = json.loads(chunk.decode('utf-8'))
            elif kind == 0x004E4942 and binary is None:     # BIN
                binary = chunk
            pos += 8 + size
        if doc is None:
            raise RuntimeError('the .glb file has no JSON chunk')
    else:
        doc = json.loads(data.decode('utf-8'))
        binary = None
    base = os.path.dirname(os.path.abspath(path))
    for i, b in enumerate(doc.get('buffers', [])):
        uri = b.get('uri')
        if uri is None:
            if binary is None:
                raise RuntimeError('buffer {} has no data'.format(i))
            buffers.append(binary)
        elif uri.startswith('data:'):
            buffers.append(base64.b64decode(uri.split(',', 1)[1]))
        else:
            from urllib.parse import unquote
            with open(os.path.join(base, unquote(uri)), 'rb') as f:
                buffers.append(f.read())
    return doc, buffers


def _gltf_accessor(doc, buffers, index):
    ''' Values of an accessor as a flat list (floats or ints) and its width '''
    acc = doc['accessors'][index]
    if 'sparse' in acc:
        raise RuntimeError('sparse glTF accessors are not supported')
    code, norm = _GLTF_COMPONENTS[acc['componentType']]
    width = _GLTF_WIDTH[acc['type']]
    count = acc['count']
    size = struct.calcsize('<' + code)
    if 'bufferView' not in acc:
        return [0] * (count * width), width
    view = doc['bufferViews'][acc['bufferView']]
    buf = buffers[view['buffer']]
    start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = view.get('byteStride') or size * width
    if stride == size * width:
        values = array(code)
        values.frombytes(buf[start:start + count * stride])
        if sys.byteorder == 'big':
            values.byteswap()
        values = values.tolist()
    else:
        rec = struct.Struct('<' + code * width)
        values = []
        for k in range(count):
            values.extend(rec.unpack_from(buf, start + k * stride))
    if acc.get('normalized') and norm:
        values = [max(v / norm, -1.0) for v in values]
    return values, width


def _gltf_node_matrix(node):
    ''' A node's local transform as a 4x4 column-major list '''
    if 'matrix' in node:
        return list(node['matrix'])
    tx, ty, tz = node.get('translation', (0, 0, 0))
    qx, qy, qz, qw = node.get('rotation', (0, 0, 0, 1))
    sx, sy, sz = node.get('scale', (1, 1, 1))
    r = [1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw),
         2 * (qx * qy - qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw),
         2 * (qx * qz + qy * qw), 2 * (qy * qz - qx * qw), 1 - 2 * (qx * qx + qy * qy)]
    return [r[0] * sx, r[1] * sx, r[2] * sx, 0,
            r[3] * sy, r[4] * sy, r[5] * sy, 0,
            r[6] * sz, r[7] * sz, r[8] * sz, 0,
            tx, ty, tz, 1]


def _mat_mul(a, b):
    ''' a * b for column-major 4x4 matrices '''
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
            for c in range(4) for r in range(4)]


def _read_gltf(path):
    doc, buffers = _gltf_load(path)
    required = doc.get('extensionsRequired', [])
    if 'KHR_draco_mesh_compression' in required or 'EXT_meshopt_compression' in required:
        raise RuntimeError('compressed glTF meshes (Draco / meshopt) are not supported: '
                           'export the file without mesh compression')
    xyz = array('f')
    tri = array('I')

    def emit_mesh(mesh_index, m):
        for prim in doc['meshes'][mesh_index].get('primitives', []):
            mode = prim.get('mode', 4)
            if mode not in (4, 5, 6) or 'POSITION' not in prim.get('attributes', {}):
                continue                    # points / lines: nothing to fill
            pos, _ = _gltf_accessor(doc, buffers, prim['attributes']['POSITION'])
            n = len(pos) // 3
            if 'indices' in prim:
                idx, _ = _gltf_accessor(doc, buffers, prim['indices'])
                idx = [int(i) for i in idx]
            else:
                idx = list(range(n))
            if mode == 5:                   # triangle strip
                idx = [v for k in range(len(idx) - 2) for v in
                       ((idx[k], idx[k + 1], idx[k + 2]) if k % 2 == 0 else
                        (idx[k + 1], idx[k], idx[k + 2]))]
            elif mode == 6:                 # triangle fan
                idx = [v for k in range(1, len(idx) - 1) for v in (idx[0], idx[k], idx[k + 1])]
            base = len(xyz) // 3
            for k in range(n):
                x, y, z = pos[3 * k], pos[3 * k + 1], pos[3 * k + 2]
                wx = m[0] * x + m[4] * y + m[8] * z + m[12]
                wy = m[1] * x + m[5] * y + m[9] * z + m[13]
                wz = m[2] * x + m[6] * y + m[10] * z + m[14]
                # glTF is Y-up; FielDes is Z-up
                xyz.extend((wx, -wz, wy))
            tri.extend(base + i for i in idx[:len(idx) - len(idx) % 3])

    nodes = doc.get('nodes', [])

    def walk(i, parent, depth):
        if depth > 64:
            return
        node = nodes[i]
        m = _mat_mul(parent, _gltf_node_matrix(node))
        if 'mesh' in node:
            emit_mesh(node['mesh'], m)
        for c in node.get('children', []):
            walk(c, m, depth + 1)

    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    scenes = doc.get('scenes', [])
    if scenes:
        for root in scenes[doc.get('scene', 0)].get('nodes', []):
            walk(root, identity, 0)
    elif nodes:
        children = {c for n in nodes for c in n.get('children', [])}
        for i in range(len(nodes)):
            if i not in children:
                walk(i, identity, 0)
    else:
        for i in range(len(doc.get('meshes', []))):
            emit_mesh(i, identity)
    return xyz, tri, 1000.0                 # glTF lengths are metres


# ---------------------------------------------------------------------------

def _import_mesh(path, units='mm', file_units=None, rev=None):
    ''' import_model() of a mesh file (.stl, .obj, .ply, .3mf, .glb or .gltf): a Shape whose
        value is the exact signed distance to its triangles (negative
        inside).

        units       the units of your script ('mm', 'cm', 'm', 'in', ...)
        file_units  what the numbers in the file mean.  STL, OBJ and PLY
                    files don't say (millimetres are assumed); 3MF files do
                    and glTF is in metres, which is used unless this
                    overrides it
        rev         not used by the import itself: changing it makes FielDes
                    run the script (and read the file) again, e.g. after
                    the file changed on disk

        Returns (shape, (xyz_min, xyz_max)): the shape and its bounding box,
        ready for view.set_bounds(*roi(...)).  Raises RuntimeError if the
        file can't be read or has no usable triangles.
    '''
    path = os.fspath(path)
    ext = os.path.splitext(path)[1].lower()
    if ext not in _NATIVE + _PYTHON:
        raise ValueError('import_model({!r}): unsupported file type {!r}; use a STEP file or '
                         'an .stl, .obj, .ply, .3mf, .glb or .gltf mesh'.format(path, ext))

    # The same file (by its path, size and modification time), read with the same units, is the same shape:
    # reading, cleaning and building the distance structure of a big mesh takes seconds, and a script runs
    # again on every edit.  (A new rev, or a changed file, reads it again.)
    cache = cache_for('import_mesh', 6)
    key = None
    try:
        st = os.stat(path)
        key = (os.path.normcase(os.path.abspath(path)), st.st_size, st.st_mtime_ns, units, file_units, rev)
    except OSError:
        pass
    if key is not None:
        hit, rec = cache.get(key)
        if hit:
            shape, bounds, file_mm, entry_note, entry_mesh = rec
            _cad._log_import(path, 'mesh', [(shape, bounds)], file_mm, units)
            entry = _cad._import_log[os.path.normcase(os.path.abspath(path))]
            entry['note'] = entry_note
            entry['mesh'] = dict(entry_mesh)
            return shape, bounds

    xyz = tri = None
    declared_mm = None                  # the unit stated by the file, if any
    if ext in _PYTHON:
        if not os.path.exists(path):
            raise RuntimeError('import_model({!r}): could not open the file'.format(path))
        try:
            if ext == '.ply':
                xyz, tri = _read_ply(path)
            elif ext == '.3mf':
                xyz, tri, declared_mm = _read_3mf(path)
            else:
                xyz, tri, declared_mm = _read_gltf(path)
        except (RuntimeError, ValueError, KeyError, IndexError, TypeError, OSError, struct.error) as e:
            raise RuntimeError('import_model({!r}): {}'.format(path, e))

    if file_units is not None:
        file_mm = _cad._target_mm(file_units)
    else:
        file_mm = declared_mm if declared_mm is not None else 1.0
    target_mm = _cad._target_mm(units)
    scale = 1.0 if (file_mm is None or target_mm is None) else file_mm / target_mm

    info = libfive_mesh_import_info_t()
    if xyz is None:
        ptr = lib.libfive_import_mesh(path.encode('utf-8'), scale, ctypes.byref(info))
    else:
        if not len(tri):
            raise RuntimeError('import_model({!r}): the file contains no triangles'.format(path))
        cx = (ctypes.c_float * len(xyz)).from_buffer(xyz)
        ct = (ctypes.c_uint32 * len(tri)).from_buffer(tri)
        ptr = lib.libfive_mesh_from_arrays(cx, len(xyz) // 3, ct, len(tri) // 3,
                                           scale, ctypes.byref(info))
    message = lib.libfive_import_mesh_last_message().decode('utf-8', 'replace')
    if not ptr:
        raise RuntimeError('import_model({!r}): {}'.format(path, message))
    shape = Shape(ptr)

    b = info.bounds
    lo = (b.X.lower, b.Y.lower, b.Z.lower)
    hi = (b.X.upper, b.Y.upper, b.Z.upper)

    note = message
    if declared_mm is not None and file_units is None:
        note += '; units from the file ({:g} mm per unit)'.format(declared_mm)
    size = max(hi[i] - lo[i] for i in range(3))
    unit_name = units if units not in (None, 'file') else 'units'
    if target_mm is not None and file_units in (None, 'mm') and declared_mm is None \
            and size * target_mm < 2.0:
        note += ('; only {:.3g} {} across: metres or inches? '
                 'Pass file_units="m" or "in"').format(size, unit_name)

    _cad._log_import(path, 'mesh', [(shape, (lo, hi))], file_mm, units)
    entry = _cad._import_log[os.path.normcase(os.path.abspath(path))]
    entry['note'] = note
    entry['mesh'] = {
        'triangles': info.triangles, 'vertices': info.vertices,
        'shells': info.components, 'watertight': bool(info.watertight),
        'open_edges': info.boundary_edges,
        'nonmanifold_edges': info.nonmanifold_edges,
        'reoriented': info.reoriented, 'dropped': info.dropped,
        'winding_sign': bool(info.winding_sign), 'scale': scale,
    }
    if key is not None:
        cache.put(key, (shape, (lo, hi), file_mm, note, dict(entry['mesh'])))
    return shape, (lo, hi)
