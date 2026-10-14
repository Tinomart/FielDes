'''
Exporting: a body as a mesh file, an analysis as a finite-element model and as results, in the files other programs read.

    export_mesh(part, "part.stl")                   .stl .obj .ply .3mf .glb .gltf .fbx .off .dxf  (see meshfiles.py)
    export_fea(result, "bracket.inp")               the model of a static analysis for another solver: .inp (Abaqus, CalculiX), .bdf
                                                    (Nastran), .msh (Gmsh 2.2)
    export_results(result, "bracket.vtu")           what an analysis found: .vtu (VTK / ParaView: the mesh with every field), .csv (the
                                                    nodes and the elements as tables), .txt (the key numbers: maximum stress, safety
                                                    factor, drag ... as text)
    export_field(field, "stress.vti")               any field sampled on a regular grid: .vti (VTK image data), .csv

Every file is in FielDes's own units: millimetres, newtons, megapascals (so a mass is in tonnes: N, mm, t, s, MPa is a consistent set).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import math
import os
import re
from array import array

from fieldes.ffi import lib, libfive_interval_t, libfive_region_t
from fieldes.i18n import tr as _tr
from fieldes.shape import Shape
from fieldes.stdlib import meshfiles

__all__ = ['export_mesh', 'export_fea', 'export_results', 'export_field', 'result_summary', 'key_results', 'EXPORT_FEA_FORMATS',
           'EXPORT_RESULT_FORMATS']

EXPORT_FEA_FORMATS = ('.inp', '.bdf', '.msh')
EXPORT_RESULT_FORMATS = ('.vtu', '.csv', '.txt')


def _host(name, default=None):
    ''' A setting of the application (the scene's region, resolution, quality), or `default` outside it '''
    try:
        import _fieldes_host as host
        value = getattr(host, name, None)
        return default if value is None else value
    except ImportError:
        return default


def _body_of(x, what):
    ''' The shape an export is about: a shape, the body of an analysis' result, the design an optimisation made '''
    if isinstance(x, Shape):
        return x
    shape = getattr(x, 'shape', None)
    if isinstance(shape, Shape):
        return shape
    if callable(shape):
        made = shape()
        if isinstance(made, Shape):
            return made
    as_field = getattr(type(x), '_as_field', None)
    if as_field is not None:
        made = x._as_field()
        if isinstance(made, Shape):
            return made
    raise TypeError('{}: a body (a shape, or a result that has one) is exported, not {!r}'.format(what, type(x).__name__))


# ---------------------------------------------------------------------------
# Meshes

def _surface_arrays(shape, bounds, resolution):
    from fieldes.stdlib.fields import _shape_bounds, render_mesh
    lo, hi = _shape_bounds(shape, bounds)
    if resolution is None:
        resolution = _host('__resolution', None)
        if resolution is None:
            size = max(h - l for l, h in zip(lo, hi))
            resolution = max(0.5, min(20.0, 200.0 / max(size, 1e-9)))
    region = libfive_region_t(*[libfive_interval_t(a, b) for a, b in zip(lo, hi)])
    mesh_p = render_mesh(shape, region, resolution)
    if not mesh_p:
        raise RuntimeError('nothing to export: the shape has no surface inside the region {} .. {}'.format(tuple(lo), tuple(hi)))
    try:
        mesh = mesh_p[0]
        nv, nt = mesh.vert_count, mesh.tri_count
        if not nt:
            raise RuntimeError('nothing to export: the shape has no surface inside the region {} .. {}'.format(tuple(lo), tuple(hi)))
        xyz = array('f')
        xyz.frombytes(ctypes.string_at(ctypes.cast(mesh.verts, ctypes.c_void_p).value, nv * 12))
        tri = array('I')
        tri.frombytes(ctypes.string_at(ctypes.cast(mesh.tris, ctypes.c_void_p).value, nt * 12))
    finally:
        lib.libfive_mesh_delete(mesh_p)
    return xyz, tri


def export_mesh(part, path, bounds=None, resolution=None, name=None, ascii=False):
    ''' Writes the surface of a body as a triangle mesh file, in the format of the file's extension:

            .stl (binary; ascii=True for text), .obj, .ply, .3mf, .glb, .gltf, .fbx (Autodesk FBX, ASCII 7.4: see fbx.py), .off, .dxf (3DFACE)

        part        a body (a shape, a list of them, the design an optimisation made, the body of an analysis' result)
        path        the file to write (a folder that is not there is made)
        bounds      ((x0, y0, z0), (x1, y1, z1)) the region meshed; the body's own extent when it is not given
        resolution  samples per mm of the mesh (the scene's resolution when it is not given: the one of the viewport)
        name        the object's name inside the file (the file's name)
        ascii       STL as text, not binary

        Millimetres in every format that has no unit; the ones that state their unit (3MF, FBX, DXF) say millimetres (a glTF is in metres, Y up, as
        glTF is).  Returns the path. '''
    path = os.fspath(path)
    ext = os.path.splitext(path)[1].lower()
    if ext not in meshfiles.MESH_WRITE_FORMATS:
        raise ValueError('export_mesh({!r}): the mesh formats are {}'.format(path, ', '.join(meshfiles.MESH_WRITE_FORMATS)))
    parts = list(part) if isinstance(part, (list, tuple)) else [part]
    xyz, tri = array('f'), array('I')
    for p in parts:
        body = _body_of(p, 'export_mesh')
        x, t = _surface_arrays(body, bounds, resolution)
        base = len(xyz) // 3
        xyz.extend(x)
        tri.extend(base + i for i in t)
    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    meshfiles.write_mesh(path, xyz, tri, name=name, ascii=ascii)
    return path


# ---------------------------------------------------------------------------
# A finite-element model, from the tetrahedral mesh of a static analysis

def _tet_data(result, what):
    ''' The mesh a static analysis was solved on, with what it found: nodes (flat x, y, z), tets (flat, 4 nodes each), the boundary triangles, the
        displacement of each node, the value of each field in each tetrahedron '''
    from fieldes.stdlib import fea
    handle = getattr(result, '_handle', None)
    if not isinstance(result, fea.TetResult) or handle is None or getattr(lib, 'libfive_tetfea_counts', None) is None:
        raise TypeError('{}: the finite-element model is that of a static analysis (static_analysis(...)), not {!r}'.format(what, type(result).__name__))
    p = handle.ptr
    counts = (ctypes.c_int64 * 3)()
    lib.libfive_tetfea_counts(p, counts)
    nv, nt, nf = counts[0], counts[1], counts[2]
    verts = (ctypes.c_float * (3 * nv))()
    tets = (ctypes.c_int32 * (4 * nt))()
    faces = (ctypes.c_int32 * (3 * nf))()
    face_tet = (ctypes.c_int32 * nf)()
    lib.libfive_tetfea_mesh(p, verts, tets, faces, face_tet)
    disp = (ctypes.c_float * (3 * nv))()
    lib.libfive_tetfea_node_displacements(p, disp)
    values = {}
    for i, field in enumerate(fea._FIELDS):
        buf = (ctypes.c_float * nt)()
        lib.libfive_tetfea_element_values(p, i, buf, nt)
        values[field] = buf
    # (every tetrahedron right-handed: a solver that wants positive volumes takes it as it is)
    xyz = list(verts)
    tet = list(tets)
    for e in range(nt):
        a, b, c, d = tet[4 * e:4 * e + 4]
        ax, ay, az = xyz[3 * a], xyz[3 * a + 1], xyz[3 * a + 2]
        ux, uy, uz = xyz[3 * b] - ax, xyz[3 * b + 1] - ay, xyz[3 * b + 2] - az
        vx, vy, vz = xyz[3 * c] - ax, xyz[3 * c + 1] - ay, xyz[3 * c + 2] - az
        wx, wy, wz = xyz[3 * d] - ax, xyz[3 * d + 1] - ay, xyz[3 * d + 2] - az
        volume = ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) + uz * (vx * wy - vy * wx)
        if volume < 0:
            tet[4 * e + 1], tet[4 * e + 2] = c, b
    return {'xyz': xyz, 'tets': tet, 'faces': list(faces), 'disp': list(disp), 'values': {k: list(v) for k, v in values.items()},
            'nodes': nv, 'elements': nt, 'handle': p}


def _eval_at(shape, xyz, count):
    ''' A field's value at `count` points (flat x, y, z floats) '''
    pts = (ctypes.c_float * (3 * count))(*xyz) if not isinstance(xyz, ctypes.Array) else xyz
    out = (ctypes.c_float * count)()
    known = None
    try:
        from fieldes.app_support import _var_values
        known = _var_values()
    except Exception:
        known = None
    if known is not None and hasattr(lib, 'libfive_tree_eval_points_vars'):
        lib.libfive_tree_eval_points_vars(shape.ptr, pts, count, out, known[0], known[1], known[2])
    else:
        lib.libfive_tree_eval_points(shape.ptr, pts, count, out)
    return list(out)


def _node_sets(data, result):
    ''' The conditions of the analysis as node sets: supports (the nodes inside the region, with the axes that are held), loads (the nodes of the
        surface under the region, each with its share of the force by the area it carries), gravity ((g) for the whole model) '''
    from fieldes.stdlib import fea
    supports, loads, gravity, notes = [], [], None, []
    conditions = getattr(result, '_conditions', None)
    if conditions is None:
        notes.append('the conditions of this analysis were not kept (it was read back from an earlier session): '
                     'run the script again to export them')
        return supports, loads, gravity, notes
    xyz, nv = data['xyz'], data['nodes']
    faces = data['faces']
    nf = len(faces) // 3
    for s in conditions[0]:
        if not isinstance(s, fea._Support):
            continue
        inside = _eval_at(s.region, xyz, nv)
        nodes = [i for i in range(nv) if inside[i] <= 0.0]
        supports.append({'nodes': nodes, 'axes': tuple(s.axes)})
    # centroids of the boundary triangles
    cen = []
    area = []
    for f in range(nf):
        a, b, c = faces[3 * f:3 * f + 3]
        pa, pb, pc = xyz[3 * a:3 * a + 3], xyz[3 * b:3 * b + 3], xyz[3 * c:3 * c + 3]
        cen.extend(((pa[0] + pb[0] + pc[0]) / 3, (pa[1] + pb[1] + pc[1]) / 3, (pa[2] + pb[2] + pc[2]) / 3))
        ux, uy, uz = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
        vx, vy, vz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
        area.append(0.5 * math.sqrt((uy * vz - uz * vy) ** 2 + (uz * vx - ux * vz) ** 2 + (ux * vy - uy * vx) ** 2))
    for l in conditions[1]:
        if isinstance(l, fea._Force):
            if nf == 0:
                continue
            inside = _eval_at(l.region, cen, nf)
            weight = {}
            total = 0.0
            profile = _eval_at(l.profile, cen, nf) if l.profile is not None else None
            for f in range(nf):
                if inside[f] > 0.0:
                    continue
                w = area[f] * (max(profile[f], 0.0) if profile is not None else 1.0)
                total += w
                for n in faces[3 * f:3 * f + 3]:
                    weight[n] = weight.get(n, 0.0) + w / 3.0
            if total <= 0:
                notes.append('a force(...) of the analysis touches no surface of the model: it is left out')
                continue
            loads.append({'vector': tuple(l.vector), 'nodes': [(n, w / total) for n, w in sorted(weight.items())]})
        elif isinstance(l, fea._Gravity):
            gravity = tuple(l.g)
        elif isinstance(l, fea._Thermal):
            notes.append('thermal_expansion(...) is not part of the exported model (the temperature field is not a node table)')
    return supports, loads, gravity, notes


def _material_numbers(result, what):
    m = result.material
    for attr in ('E', 'density'):
        if isinstance(getattr(m, attr, None), Shape):
            raise ValueError('{}: the material {!r} has a field for {}: a model file holds numbers (one material per element would be '
                             'needed); export_results() writes the fields themselves'.format(what, m.name, attr))
    return m


def _lines(values, per_line, sep=', '):
    return '\n'.join(sep.join(str(v) for v in values[i:i + per_line]) for i in range(0, len(values), per_line))


def _write_inp(path, data, result, sets):
    supports, loads, gravity, notes = sets
    m = _material_numbers(result, 'export_fea')
    nv, nt = data['nodes'], data['elements']
    xyz, tets = data['xyz'], data['tets']
    out = ['*HEADING', 'FielDes static analysis: {} tetrahedra, {} nodes (N, mm, MPa, tonne, s)'.format(nt, nv)]
    out += ['** The body of the model, its material, the supports and the loads of the script.  Solve with CalculiX (ccx) or Abaqus.']
    for n in notes:
        out.append('** NOTE: ' + n)
    out.append('*NODE')
    for i in range(nv):
        out.append('%d, %.9g, %.9g, %.9g' % (i + 1, xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]))
    out.append('*ELEMENT, TYPE=C3D4, ELSET=PART')
    for e in range(nt):
        out.append('%d, %d, %d, %d, %d' % (e + 1, tets[4 * e] + 1, tets[4 * e + 1] + 1, tets[4 * e + 2] + 1, tets[4 * e + 3] + 1))
    mat = ''.join(c if c.isalnum() else '_' for c in m.name).upper() or 'MATERIAL'
    out += ['*MATERIAL, NAME=' + mat, '*ELASTIC', '%.9g, %.9g' % (float(m.E), m.nu)]
    if float(m.density) > 0:
        out += ['*DENSITY', '%.9g' % float(m.density)]
    out.append('*SOLID SECTION, ELSET=PART, MATERIAL=' + mat)
    for k, s in enumerate(supports):
        out.append('*NSET, NSET=SUPPORT_%d' % (k + 1))
        out.append(_lines([n + 1 for n in s['nodes']], 12))
    for k, l in enumerate(loads):
        out.append('*NSET, NSET=LOAD_%d' % (k + 1))
        out.append(_lines([n + 1 for n, _ in l['nodes']], 12))
    out += ['*STEP, NAME=STATIC', '*STATIC']
    if supports:
        out.append('*BOUNDARY')
        for k, s in enumerate(supports):
            for axis, held in enumerate(s['axes']):
                if held and s['nodes']:
                    out.append('SUPPORT_%d, %d, %d' % (k + 1, axis + 1, axis + 1))
    if loads:
        out.append('*CLOAD')
        for k, l in enumerate(loads):
            for n, share in l['nodes']:
                for axis in range(3):
                    value = l['vector'][axis] * share
                    if value != 0.0:
                        out.append('%d, %d, %.9g' % (n + 1, axis + 1, value))
    if gravity is not None:
        g = math.sqrt(sum(c * c for c in gravity))
        if g > 0:
            out += ['*DLOAD', 'PART, GRAV, %.9g, %.9g, %.9g, %.9g' % (g, gravity[0] / g, gravity[1] / g, gravity[2] / g)]
    out += ['*NODE FILE', 'U', '*EL FILE', 'S', '*END STEP']
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


def _write_bdf(path, data, result, sets):
    supports, loads, gravity, notes = sets
    m = _material_numbers(result, 'export_fea')
    nv, nt = data['nodes'], data['elements']
    xyz, tets = data['xyz'], data['tets']
    out = ['$ FielDes static analysis: %d tetrahedra, %d nodes (N, mm, MPa, tonne, s)' % (nt, nv)]
    for n in notes:
        out.append('$ NOTE: ' + n)
    out += ['SOL 101', 'CEND', 'TITLE = FielDes static analysis', 'SUBCASE 1']
    if supports:
        out.append('  SPC = 1')
    if loads or gravity is not None:
        out.append('  LOAD = 2')
    out += ['  DISPLACEMENT(PLOT) = ALL', '  STRESS(PLOT) = ALL', 'BEGIN BULK']
    for i in range(nv):
        out.append('GRID,%d,,%.9g,%.9g,%.9g' % (i + 1, xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]))
    for e in range(nt):
        out.append('CTETRA,%d,1,%d,%d,%d,%d' % (e + 1, tets[4 * e] + 1, tets[4 * e + 1] + 1, tets[4 * e + 2] + 1, tets[4 * e + 3] + 1))
    out.append('PSOLID,1,1')
    out.append('MAT1,1,%.9g,,%.9g,%.9g' % (float(m.E), m.nu, float(m.density)))
    for s in supports:
        dofs = ''.join(str(a + 1) for a, held in enumerate(s['axes']) if held)
        if not dofs or not s['nodes']:
            continue
        nodes = [str(n + 1) for n in s['nodes']]
        for i in range(0, len(nodes), 8):
            out.append('SPC1,1,%s,%s' % (dofs, ','.join(nodes[i:i + 8])))
    load_ids = []
    for l in loads:
        magnitude = math.sqrt(sum(c * c for c in l['vector']))
        if magnitude <= 0:
            continue
        direction = [c / magnitude for c in l['vector']]
        for n, share in l['nodes']:
            out.append('FORCE,2,%d,,%.9g,%.9g,%.9g,%.9g' % (n + 1, magnitude * share, direction[0], direction[1], direction[2]))
    if gravity is not None:
        g = math.sqrt(sum(c * c for c in gravity))
        if g > 0:
            out.append('GRAV,2,,%.9g,%.9g,%.9g,%.9g' % (g, gravity[0] / g, gravity[1] / g, gravity[2] / g))
    out.append('ENDDATA')
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


def _nodal_fields(data, result):
    ''' Every field of the result at the nodes (the smooth field: the elements around a node averaged) '''
    from fieldes.stdlib import fea
    nv = data['nodes']
    pts = (ctypes.c_float * (3 * nv))(*data['xyz'])
    return {name: _eval_at(getattr(result, name), pts, nv) for name in fea._FIELDS}


def _write_msh(path, data, result, sets):
    nv, nt = data['nodes'], data['elements']
    xyz, tets = data['xyz'], data['tets']
    out = ['$MeshFormat', '2.2 0 8', '$EndMeshFormat', '$Nodes', str(nv)]
    for i in range(nv):
        out.append('%d %.9g %.9g %.9g' % (i + 1, xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]))
    out += ['$EndNodes', '$Elements', str(nt)]
    for e in range(nt):
        out.append('%d 4 2 0 1 %d %d %d %d' % (e + 1, tets[4 * e] + 1, tets[4 * e + 1] + 1, tets[4 * e + 2] + 1, tets[4 * e + 3] + 1))
    out.append('$EndElements')
    supports, loads, gravity, notes = sets
    # (the supports and loads are node data: 1 where the condition acts)
    for k, s in enumerate(supports):
        flags = [0] * nv
        for n in s['nodes']:
            flags[n] = 1
        out += ['$NodeData', '1', '"support_%d"' % (k + 1), '1', '0.0', '3', '0', '1', str(nv)]
        out += ['%d %d' % (i + 1, flags[i]) for i in range(nv)]
        out.append('$EndNodeData')
    disp = data['disp']
    out += ['$NodeData', '1', '"displacement"', '1', '0.0', '3', '0', '3', str(nv)]
    out += ['%d %.9g %.9g %.9g' % (i + 1, disp[3 * i], disp[3 * i + 1], disp[3 * i + 2]) for i in range(nv)]
    out.append('$EndNodeData')
    for name, values in data['values'].items():
        out += ['$ElementData', '1', '"%s"' % name, '1', '0.0', '3', '0', '1', str(nt)]
        out += ['%d %.9g' % (e + 1, values[e]) for e in range(nt)]
        out.append('$EndElementData')
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


def export_fea(result, path, **options):
    ''' Writes the finite-element model of a static analysis -- its tetrahedral mesh, the material, the supports and the loads -- for another
        solver, in the format of the file's extension:

            .inp   Abaqus and CalculiX: C3D4 elements, *MATERIAL, *NSET of each support and load, *BOUNDARY, *CLOAD, *DLOAD for gravity
            .bdf   Nastran: CTETRA, MAT1, SPC1, FORCE (small-field free format), solution 101
            .msh   Gmsh 2.2: the mesh, with the displacement, the supports and every field of the result as data

        The model is the one FielDes solved (the same nodes and tetrahedra).  A force is spread over the nodes of the surface it acts on by the
        area each carries, so that the sum is the force.  Materials with a field (a graded stiffness) and thermal_expansion loads are not model
        file contents: the first is refused, the second said in a note.  Units: mm, N, MPa, tonne.  Returns the path. '''
    path = os.fspath(path)
    ext = os.path.splitext(path)[1].lower()
    if ext not in EXPORT_FEA_FORMATS:
        raise ValueError('export_fea({!r}): the model formats are {}'.format(path, ', '.join(EXPORT_FEA_FORMATS)))
    if getattr(result, 'element', 'tet') == 'tet10':
        raise ValueError("export_fea: the model of a 10-node analysis cannot be written yet -- what FielDes keeps of it is its nodes cut into linear "
                         "tetrahedra (what the result is drawn on), not the 10-node elements it was solved on, and another solver would not give the same "
                         "numbers on it.  Solve with element='tet' to export a model; export_results writes the fields of this result")
    data = _tet_data(result, 'export_fea')
    sets = _node_sets(data, result)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    {'.inp': _write_inp, '.bdf': _write_bdf, '.msh': _write_msh}[ext](path, data, result, sets)
    return path


# ---------------------------------------------------------------------------
# Results

# Numbers of a result that are computed when asked for (a property of its class): drag and lift of a flow
_COMPUTED_NUMBERS = ('drag_force', 'lift_force', 'drag_coefficient', 'lift_coefficient')

# What a number of a result is called, and in what unit: (label, unit).  The order is the order they are shown in
_RESULT_NUMBERS = [
    ('max_von_mises', _tr('maximum von Mises stress'), 'MPa'),
    ('max_displacement', _tr('maximum displacement'), 'mm'),
    ('safety_factor', _tr('safety factor'), ''),
    ('reaction', _tr('support reaction (x, y, z)'), 'N'),
    ('total_load', _tr('total load (x, y, z)'), 'N'),
    ('compliance', _tr('compliance'), 'N mm'),
    ('volume', _tr('volume'), 'mm^3'),
    ('drag_force', _tr('drag force'), 'N'),
    ('lift_force', _tr('lift force'), 'N'),
    ('drag_coefficient', _tr('drag coefficient'), ''),
    ('lift_coefficient', _tr('lift coefficient'), ''),
    ('wall_force', _tr('force on the walls (x, y, z)'), 'N'),
    ('pressure_drop', _tr('pressure drop'), 'MPa'),
    ('inlet_flow', _tr('flow in'), 'mm^3/s'),
    ('outlet_flow', _tr('flow out'), 'mm^3/s'),
    ('max_speed', _tr('maximum speed'), 'mm/s'),
    ('reynolds', _tr('Reynolds number'), ''),
    ('dissipation', _tr('dissipated power'), 'W'),
    ('max_pressure', _tr('maximum pressure'), 'MPa'),
    ('min_pressure', _tr('minimum pressure'), 'MPa'),
    ('mass_imbalance', _tr('mass imbalance'), ''),
    ('max_temperature', _tr('maximum temperature'), 'C'),
    ('min_temperature', _tr('minimum temperature'), 'C'),
    ('frequencies', _tr('natural frequencies'), 'Hz'),
    ('elements', _tr('elements'), ''),
    ('nodes', _tr('nodes'), ''),
    ('iterations', _tr('iterations'), ''),
    ('seconds', _tr('solver time'), 's'),
]


def key_results(result, count=6):
    ''' The few numbers the result card shows on the screen: [(label, text, unit)] -- what result_summary lists, without the counts and times '''
    rows = []
    for label, text, unit in result_summary(result):
        if label in (_tr('elements'), _tr('nodes'), _tr('iterations'), _tr('solver time')):
            continue
        # (a vector is its size on the card: the three components are in the text file)
        if text.startswith('('):
            try:
                parts = [float(v) for v in text.strip('()').split(',')]
                text, label = '%.4g' % math.sqrt(sum(v * v for v in parts)), re.sub(r'\s*\(.*\)\s*$', '', label)
            except ValueError:
                pass
        rows.append((label, text, unit))
    return rows[:count]


def _install_key_results():
    ''' Every result's `_display()` carries its key numbers (`_key_results`), which the result card prints under the colour bar '''
    from fieldes.stdlib import fea, fluid, thermal
    classes = [fea.Result, fea.ModalResult, fea.TopologyResult, fluid.FluidResult, fluid.FlowTopologyResult, thermal.ThermalResult]
    for cls in classes:
        original = cls.__dict__.get('_display')
        if original is None or getattr(original, '_with_key_results', False):
            continue

        def make(original):
            def display(self, *args, **kwargs):
                out = original(self, *args, **kwargs)
                try:
                    rows = key_results(self)
                    for shape in (out if isinstance(out, (list, tuple)) else [out]):
                        if rows and hasattr(shape, '__dict__'):
                            shape._key_results = rows
                except Exception:
                    pass
                return out
            display._with_key_results = True
            return display
        cls._display = make(original)


def _text_of(v):
    if isinstance(v, bool):
        return 'yes' if v else 'no'
    if isinstance(v, int):
        return '{:,}'.format(v)
    if isinstance(v, float):
        return '%.4g' % v
    if isinstance(v, (tuple, list)):
        return '(' + ', '.join(_text_of(x) for x in v) + ')'
    return str(v)


def result_summary(result):
    ''' The numbers of a result that matter, as [(label, text, unit)] in the order they are shown -- what the result card prints on the screen
        and what export_results(..., '.txt') writes.  Only what the result has: a static analysis its stress and safety factor, a flow its
        forces and flows, a thermal analysis its temperatures ... '''
    out = []
    seen = set()
    values = {}
    for name in dir(result):
        if name.startswith('_'):
            continue
        try:
            raw = getattr(type(result), name, None)
            if isinstance(raw, property) or callable(raw):
                continue
            v = vars(result).get(name)
        except Exception:
            continue
        if v is not None:
            values[name] = v
    for name in _COMPUTED_NUMBERS:
        try:
            v = getattr(result, name, None)
        except Exception:
            v = None
        if v is not None:
            values[name] = v
    # (temperatures and frequencies are read from what the result keeps)
    ranges = getattr(result, '_ranges', None) or {}
    if 'temperature' in ranges:
        values['min_temperature'], values['max_temperature'] = ranges['temperature']
    if 'frequencies' not in values and hasattr(result, 'frequencies'):
        try:
            values['frequencies'] = list(result.frequencies)
        except Exception:
            pass
    for name, label, unit in _RESULT_NUMBERS:
        if name in values and name not in seen:
            v = values[name]
            if isinstance(v, (int, float, tuple, list)) and not (isinstance(v, (tuple, list)) and not v):
                if isinstance(v, (list, tuple)) and len(v) > 12:
                    continue
                out.append((label, _text_of(v), unit))
                seen.add(name)
    return out


_KIND_NAMES = {'TetResult': _tr('static analysis'), 'Result': _tr('static analysis'), 'FluidResult': _tr('flow analysis'), 'ThermalResult': _tr('thermal analysis'),
               'ModalResult': _tr('modal analysis'), 'TopologyResult': _tr('topology optimization'), 'FlowTopologyResult': _tr('flow topology optimization'),
               'ThermalTopologyResult': _tr('thermal topology optimization')}


def _write_txt(path, result):
    rows = result_summary(result)
    name = _KIND_NAMES.get(type(result).__name__, type(result).__name__)
    mat = getattr(getattr(result, 'material', None), 'name', None)
    lines = [_tr('FielDes results: %s%s') % (name, ' (%s)' % mat if mat else ''), '']
    width = max((len(r[0]) for r in rows), default=10)
    for label, text, unit in rows:
        lines.append('%s  %s%s' % (label.ljust(width), text, (' ' + unit) if unit else ''))
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


def _write_vtu(path, data, result):
    from fieldes.stdlib import fea
    nv, nt = data['nodes'], data['elements']
    xyz, tets = data['xyz'], data['tets']
    nodal = _nodal_fields(data, result)
    out = ['<?xml version="1.0"?>', '<VTKFile type="UnstructuredGrid" version="0.1" byte_order="LittleEndian">', '<UnstructuredGrid>',
           '<Piece NumberOfPoints="%d" NumberOfCells="%d">' % (nv, nt), '<PointData Vectors="displacement">',
           '<DataArray type="Float32" Name="displacement" NumberOfComponents="3" format="ascii">']
    disp = data['disp']
    out.append(' '.join('%.7g' % v for v in disp))
    out.append('</DataArray>')
    for name, values in nodal.items():
        out.append('<DataArray type="Float32" Name="%s (nodal)" format="ascii">' % name)
        out.append(' '.join('%.7g' % v for v in values))
        out.append('</DataArray>')
    out.append('</PointData>')
    out.append('<CellData Scalars="von_mises">')
    for name in fea._FIELDS:
        out.append('<DataArray type="Float32" Name="%s" format="ascii">' % name)
        out.append(' '.join('%.7g' % v for v in data['values'][name]))
        out.append('</DataArray>')
    out.append('</CellData>')
    out.append('<Points><DataArray type="Float32" NumberOfComponents="3" format="ascii">')
    out.append(' '.join('%.9g' % v for v in xyz))
    out.append('</DataArray></Points>')
    out.append('<Cells><DataArray type="Int32" Name="connectivity" format="ascii">')
    out.append(' '.join(str(v) for v in tets))
    out.append('</DataArray><DataArray type="Int32" Name="offsets" format="ascii">')
    out.append(' '.join(str(4 * (e + 1)) for e in range(nt)))
    out.append('</DataArray><DataArray type="UInt8" Name="types" format="ascii">')
    out.append(' '.join('10' for _ in range(nt)))
    out.append('</DataArray></Cells>')
    out += ['</Piece>', '</UnstructuredGrid>', '</VTKFile>']
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


def _write_csv(path, data, result):
    from fieldes.stdlib import fea
    nv, nt = data['nodes'], data['elements']
    xyz, tets = data['xyz'], data['tets']
    nodal = _nodal_fields(data, result)
    disp = data['disp']
    base, _ = os.path.splitext(path)
    names = list(nodal)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write('node,x,y,z,ux,uy,uz,' + ','.join(names) + '\n')
        for i in range(nv):
            f.write('%d,%.9g,%.9g,%.9g,%.7g,%.7g,%.7g,%s\n' % (
                i + 1, xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2], disp[3 * i], disp[3 * i + 1], disp[3 * i + 2],
                ','.join('%.7g' % nodal[n][i] for n in names)))
    elements = base + '_elements.csv'
    with open(elements, 'w', encoding='utf-8', newline='') as f:
        f.write('element,n1,n2,n3,n4,' + ','.join(fea._FIELDS) + '\n')
        for e in range(nt):
            f.write('%d,%d,%d,%d,%d,%s\n' % (e + 1, tets[4 * e] + 1, tets[4 * e + 1] + 1, tets[4 * e + 2] + 1, tets[4 * e + 3] + 1,
                                              ','.join('%.7g' % data['values'][n][e] for n in fea._FIELDS)))


def export_results(result, path, **options):
    ''' Writes what an analysis found, in the format of the file's extension:

            .vtu   VTK XML (ParaView, VisIt...): the tetrahedral mesh with the displacement and every field at the nodes, and every field
                   in each element (static analyses)
            .csv   the nodes (position, displacement, every field) as a table, and the elements next to it (<name>_elements.csv)
                   (static analyses)
            .txt   the key numbers as text, one per line, with their units: maximum stress, safety factor, reactions, drag and lift of a
                   flow, temperatures, natural frequencies ... (every kind of result)

        Units: mm, N, MPa.  Returns the path. '''
    path = os.fspath(path)
    ext = os.path.splitext(path)[1].lower()
    if ext not in EXPORT_RESULT_FORMATS:
        raise ValueError('export_results({!r}): the result formats are {}'.format(path, ', '.join(EXPORT_RESULT_FORMATS)))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    if ext == '.txt':
        _write_txt(path, result)
        return path
    data = _tet_data(result, 'export_results')
    (_write_vtu if ext == '.vtu' else _write_csv)(path, data, result)
    return path


# ---------------------------------------------------------------------------
# Any field, sampled on a grid

def export_field(field, path, bounds=None, resolution=None, name='field'):
    ''' Samples a field -- anything that is a Shape, a result's field (`result.von_mises`) -- on a regular grid and writes it:

            .vti   VTK image data (ParaView ...): the values as point data
            .csv   x, y, z, value for every grid point

        bounds      ((x0, y0, z0), (x1, y1, z1)): the box sampled (the extent of the shape that has one: field.bounds, else this is needed)
        resolution  samples per mm (the scene's resolution by default, limited to 2 million points)

        A field is not a mesh: this is the sampling of it that other programs work with, at the resolution asked for.  Returns the path. '''
    from fieldes.stdlib.fields import _shape_bounds
    path = os.fspath(path)
    ext = os.path.splitext(path)[1].lower()
    if ext not in ('.vti', '.csv'):
        raise ValueError('export_field({!r}): the field formats are .vti and .csv'.format(path))
    field = _body_of(field, 'export_field') if not isinstance(field, Shape) else field
    lo, hi = _shape_bounds(field, bounds)
    if resolution is None:
        resolution = _host('__resolution', None) or 2.0
    dims = [max(2, int(math.ceil((hi[i] - lo[i]) * resolution)) + 1) for i in range(3)]
    while dims[0] * dims[1] * dims[2] > 2000000:
        dims = [max(2, int(d * 0.9)) for d in dims]
    step = [(hi[i] - lo[i]) / (dims[i] - 1) for i in range(3)]
    n = dims[0] * dims[1] * dims[2]
    pts = (ctypes.c_float * (3 * n))()
    k = 0
    for iz in range(dims[2]):
        for iy in range(dims[1]):
            for ix in range(dims[0]):
                pts[k], pts[k + 1], pts[k + 2] = lo[0] + ix * step[0], lo[1] + iy * step[1], lo[2] + iz * step[2]
                k += 3
    values = _eval_at(field, pts, n)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    if ext == '.csv':
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write('x,y,z,%s\n' % name)
            for i in range(n):
                f.write('%.7g,%.7g,%.7g,%.7g\n' % (pts[3 * i], pts[3 * i + 1], pts[3 * i + 2], values[i]))
    else:
        with open(path, 'w', encoding='ascii') as f:
            f.write('<?xml version="1.0"?>\n<VTKFile type="ImageData" version="0.1" byte_order="LittleEndian">\n'
                    '<ImageData WholeExtent="0 %d 0 %d 0 %d" Origin="%.9g %.9g %.9g" Spacing="%.9g %.9g %.9g">\n'
                    '<Piece Extent="0 %d 0 %d 0 %d">\n<PointData Scalars="%s">\n<DataArray type="Float32" Name="%s" format="ascii">\n'
                    % (dims[0] - 1, dims[1] - 1, dims[2] - 1, lo[0], lo[1], lo[2], step[0], step[1], step[2],
                       dims[0] - 1, dims[1] - 1, dims[2] - 1, name, name))
            f.write(' '.join('%.7g' % v for v in values))
            f.write('\n</DataArray>\n</PointData>\n</Piece>\n</ImageData>\n</VTKFile>\n')
    return path
