'''
A triangle mesh as a body of points you can edit.

    from fieldes import *

    body = editable_polyhedron('part.stl')          # a morph_polyhedron: a point at each vertex of the mesh, a face for each triangle

`import_model` of an .stl, .obj, .ply, .off or .3mf file gives the exact distance to the triangles of the file: a body you can cut, round and measure, but not pull by its points.  `editable_polyhedron` makes of the
same file a `morph_polyhedron` -- a point at each vertex of the mesh, joined by the faces of the mesh -- so that the points can be dragged, and double-clicked into spline points or round corners, as the points of any
body made of points.  A triangle mesh stays a triangle mesh: every triangle is a face of its own, so that a box is its twelve triangles and no dragged point bends a face of four.  The mesh has to be closed (every edge is the edge of two
faces) and small enough to be edited by hand: `max_points` (5000) says how many points are made at most -- a mesh of fifty thousand triangles has no use for twenty-five thousand points.  In FielDes: File > Import model of a mesh file does this itself (a mesh that is not closed, or has more points, is imported by `import_model` as before):
the points and the body are written into the script, each point a model of its own with its gizmo.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import os
import re
import struct

__all__ = ['editable_polyhedron']

_MAX_POINTS = 5000          # (the user, 2026-11-02: "Meshes with under 2000-5000 vertices often still need manual adjustment ... BUT 300 AS A CUTOFF???": a body of corners with flat faces is built in a fraction of a second, see morph._flat_tessellation)


def _read_stl(path):
    ''' The triangles of an .stl file, binary or text: [(a, b, c)], each a place of three numbers '''
    data = open(path, 'rb').read()
    if len(data) >= 84:
        count = struct.unpack('<I', data[80:84])[0]
        if 84 + 50 * count == len(data):
            out = []
            for k in range(count):
                v = struct.unpack('<9f', data[84 + 50 * k + 12:84 + 50 * k + 48])
                out.append((v[0:3], v[3:6], v[6:9]))
            return out
    text = data.decode('utf-8', 'replace')
    pts = [tuple(float(x) for x in m.groups()) for m in re.finditer(r'vertex\s+(\S+)\s+(\S+)\s+(\S+)', text)]
    if not pts or len(pts) % 3:
        raise ValueError('%s: not an STL file whose triangles can be read' % os.path.basename(path))
    return [(pts[i], pts[i + 1], pts[i + 2]) for i in range(0, len(pts), 3)]


def _read_obj(path):
    verts, out = [], []
    for line in open(path, encoding='utf-8', errors='replace'):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'v' and len(parts) >= 4:
            verts.append(tuple(float(x) for x in parts[1:4]))
        elif parts[0] == 'f' and len(parts) >= 4:
            idx = []
            for p in parts[1:]:
                i = int(p.split('/')[0])
                idx.append(i - 1 if i > 0 else len(verts) + i)
            for k in range(1, len(idx) - 1):
                out.append((verts[idx[0]], verts[idx[k]], verts[idx[k + 1]]))
    return out


def _from_arrays(xyz, tri):
    v = [(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]) for i in range(len(xyz) // 3)]
    return [(v[tri[3 * k]], v[tri[3 * k + 1]], v[tri[3 * k + 2]]) for k in range(len(tri) // 3)]


def _read_triangles(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == '.stl':
        return _read_stl(path), None
    if ext == '.obj':
        return _read_obj(path), None
    from fieldes.stdlib import mesh_import as mi
    if ext == '.ply':
        xyz, tri = mi._read_ply(path)
        return _from_arrays(xyz, tri), None
    if ext == '.off':
        xyz, tri = mi._meshfiles.read_off(path)
        return _from_arrays(xyz, tri), None
    if ext == '.3mf':
        xyz, tri, declared_mm = mi._read_3mf(path)
        return _from_arrays(xyz, tri), declared_mm
    if ext in ('.glb', '.gltf'):
        xyz, tri, declared_mm = mi._read_gltf(path)
        return _from_arrays(xyz, tri), declared_mm
    raise ValueError('editable_polyhedron: %r is not a mesh file it reads (.stl, .obj, .ply, .off, .3mf, .glb, .gltf)' % path)


def _scale(units, file_units, declared_mm):
    ''' How many of the script's units one unit of the file is: as import_model takes it '''
    from fieldes.stdlib import mesh_import as mi
    file_mm = mi._cad._target_mm(file_units) if file_units is not None else (declared_mm if declared_mm is not None else mi._units.import_mm())
    target_mm = mi._cad._target_mm(units)
    return 1.0 if (file_mm is None or target_mm is None) else file_mm / target_mm


def _weld(triangles, tol):
    ''' (vertices, faces): the places of the triangles' corners with those that coincide made one, and the triangles by those numbers -- a triangle with two corners at one place is dropped '''
    index, verts, faces = {}, [], []

    def vid(p):
        key = tuple(int(round(c / tol)) for c in p)
        i = index.get(key)
        if i is None:
            i = index[key] = len(verts)
            verts.append(tuple(float(c) for c in p))
        return i
    for a, b, c in triangles:
        ia, ib, ic = vid(a), vid(b), vid(c)
        if ia != ib and ib != ic and ic != ia:
            faces.append((ia, ib, ic))
    return verts, faces


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _unit(a):
    n = math.sqrt(_dot(a, a))
    return None if n < 1e-300 else (a[0] / n, a[1] / n, a[2] / n)


def _open_edges(faces):
    ''' The edges that are not the edge of exactly two faces that run the other way along it (the mesh is closed and consistent when there are none) '''
    edges = {}
    for f in faces:
        for k in range(len(f)):
            e = (f[k], f[(k + 1) % len(f)])
            edges[e] = edges.get(e, 0) + 1
    return [e for e, c in edges.items() if c != 1 or (e[1], e[0]) not in edges]


def _volume(verts, faces):
    ''' Six times the volume the faces enclose (negative: they face inward) '''
    total = 0.0
    for f in faces:
        for k in range(1, len(f) - 1):
            total += _dot(verts[f[0]], _cross(verts[f[k]], verts[f[k + 1]]))
    return total


def mesh_faces(path, units='mm', file_units=None, max_points=_MAX_POINTS):
    ''' (vertices, faces) of the mesh file `path`, in the units of the script: the vertices as places of three numbers, the faces as the numbers of three of them (the triangles of the mesh), counterclockwise seen from outside.  Raises
        ValueError for a mesh that is not closed, and for one of more than `max_points` faces '''
    path = os.fspath(path)
    if not os.path.exists(path):
        raise ValueError('editable_polyhedron: the file %r is not there' % path)
    triangles, declared_mm = _read_triangles(path)
    if not triangles:
        raise ValueError('editable_polyhedron: %s has no triangles' % os.path.basename(path))
    if len(triangles) > 8 * max_points:
        # (a closed mesh of at most max_points vertices has about twice as many triangles: more than eight times is far past the limit, and welding that many in Python would only keep the window waiting)
        raise ValueError('editable_polyhedron: %s has %d triangles, far more than a body of at most max_points=%d points is made of: a body of points is edited by hand, so make the mesh coarser first (or give max_points=)'
                         % (os.path.basename(path), len(triangles), max_points))
    k = _scale(units, file_units, declared_mm)
    triangles = [tuple(tuple(k * c for c in p) for p in t) for t in triangles]
    lo = [min(p[a] for t in triangles for p in t) for a in range(3)]
    hi = [max(p[a] for t in triangles for p in t) for a in range(3)]
    size = max(hi[a] - lo[a] for a in range(3))
    if size <= 0:
        raise ValueError('editable_polyhedron: %s is all at one place' % os.path.basename(path))
    verts, faces = _weld(triangles, 1e-6 * size)
    open_ = _open_edges(faces)
    if open_:
        raise ValueError('editable_polyhedron: %s is not a closed mesh (%d edges are not the edge of two triangles): a body of points needs a closed surface' % (os.path.basename(path), len(open_)))
    if _volume(verts, faces) < 0:
        faces = [tuple(reversed(f)) for f in faces]
    # (a triangle mesh stays a triangle mesh -- the user, 2026-11-02, H72: "A tri mesh should become a tri mesh, no faces with anything but triangle surfaces ... then it always stays a polyhedron": two triangles that lie in one plane are NOT merged into a face of four, which a dragged point would bend)
    if len(verts) > max_points:
        raise ValueError('editable_polyhedron: %s makes %d points and %d faces, more than max_points=%d: a body of points is edited by hand, so make the mesh coarser first (or give max_points=)'
                         % (os.path.basename(path), len(verts), len(faces), max_points))
    return verts, faces


def editable_polyhedron(source, units='mm', file_units=None, max_points=_MAX_POINTS, smooth=False):
    ''' The mesh of a file -- or of a model that was imported from one -- as a `morph_polyhedron`: a point at each vertex, a face for each triangle (a triangle mesh stays a triangle mesh: no face of four).  `smooth=True`
        makes the points spline points (a `morph_body`: the surface goes through them and is smooth).  See the module '''
    from fieldes import point
    from fieldes.stdlib.morph import morph_body, morph_polyhedron
    if not isinstance(source, (str, os.PathLike)):
        found = getattr(source, '_mesh_source', None)
        if not found:
            raise ValueError('editable_polyhedron: give the path of a mesh file, or a model that was imported from one with import_model')
        source, units, file_units = found
    verts, faces = mesh_faces(source, units, file_units, max_points)
    points = [point(position=tuple(p)) for p in verts]
    return (morph_body if smooth else morph_polyhedron)(points, _faces=faces)


editable_polyhedron._makes = 'solid'
