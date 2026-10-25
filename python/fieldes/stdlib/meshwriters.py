'''
The files a mesh (see meshing.py) is written to, for the solvers and viewers that read them.

    format      the file                                        what is in it
    'gmsh'      .msh   Gmsh, version 2.2 (ASCII)                the nodes, the cells and the boundary faces, with the zones as physical groups
    'vtk'       .vtu   VTK / ParaView (XML, ASCII)              the cells (or the faces of a surface mesh) with their zone as a number; boundary=True adds the faces
    'abaqus'    .inp   Abaqus / CalculiX                        *NODE, *ELEMENT (C3D4, C3D10, C3D6, C3D8; S3, S4 for a surface), element sets of the cell zones,
                                                                node sets of the boundary zones
    'nastran'   .bdf   Nastran (free format)                    GRID, CTETRA, CPENTA, CHEXA with a property per cell zone
    'unv'       .unv   I-DEAS universal (Salome, Elmer, Code_Aster)  nodes, elements (tetrahedra, hexahedra, triangles, quadrilaterals), the zones as groups
    'fluent'    .msh   Ansys Fluent                             the mesh as Fluent's face-based text: nodes, cells, the faces of every zone with their boundary type
    'openfoam'  a folder   OpenFOAM's polyMesh                  points, faces, owner, neighbour, boundary (the patches are the zones), cellZones
    'stl'       .stl   the boundary faces, triangles           (a quadrilateral as two triangles), binary
    'obj'       .obj   the boundary faces, one group for every zone

The format is found from the ending of the path ('.msh' is Gmsh: ask for 'fluent' with format='fluent'), and a path ending in 'polyMesh' is OpenFOAM's.  Options:
scale (a factor for the coordinates: 0.001 for a solver that works in metres), zones=False (without the zones), boundary=True (VTK: the faces too),
patch_types={'inlet': 'patch', 'sym': 'symmetryPlane'} (OpenFOAM: the type of a patch; 'wall' for the names that start with wall, 'symmetryPlane' for those that
have symmetry in them, 'patch' for the rest), boundary_types={'inlet': 'velocity-inlet'} (Fluent: the type of a zone: velocity-inlet, pressure-outlet, wall,
symmetry ...; wall for the names that start with wall, pressure-outlet for outlet, velocity-inlet for inlet).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import os
import re
import struct
from array import array

from fieldes.stdlib.meshing import (CORNERS, EDGES, FACES, NODES, SOLIDS, SURFACES, MeshError, _centroid, _cell_volume, _polygon_area_vector)

__all__ = []

FORMATS = ('gmsh', 'vtk', 'abaqus', 'nastran', 'unv', 'fluent', 'openfoam', 'stl', 'obj')
_BY_EXTENSION = {'.msh': 'gmsh', '.vtu': 'vtk', '.inp': 'abaqus', '.bdf': 'nastran', '.nas': 'nastran', '.unv': 'unv', '.stl': 'stl', '.obj': 'obj'}


def write_mesh(mesh, path, format=None, **options):
    ''' Writes `mesh` to `path`; returns the path '''
    path = str(path)
    fmt = format
    if fmt is None:
        if os.path.basename(os.path.normpath(path)) == 'polyMesh':
            fmt = 'openfoam'
        else:
            fmt = _BY_EXTENSION.get(os.path.splitext(path)[1].lower())
    if fmt is None or fmt not in FORMATS:
        raise ValueError('the mesh formats are {} (the ending of the path says which one: .msh .vtu .inp .bdf .unv .stl .obj, or format=): not {!r}'.format(
            ', '.join(FORMATS), format or os.path.splitext(path)[1]))
    if not mesh.node_count:
        raise MeshError('the mesh has no nodes')
    scale = float(options.get('scale', 1.0))
    volume = any(mesh.cell_count(k) for k in SOLIDS)
    if fmt in ('fluent', 'openfoam') and not volume:
        raise MeshError('{} is a mesh of cells: this one is a surface'.format(fmt))
    writer = {'gmsh': _write_gmsh, 'vtk': _write_vtk, 'abaqus': _write_abaqus, 'nastran': _write_nastran, 'unv': _write_unv, 'fluent': _write_fluent,
              'openfoam': _write_openfoam, 'stl': _write_stl, 'obj': _write_obj}[fmt]
    folder = path if fmt == 'openfoam' else os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    writer(mesh, path, scale, options)
    return path


def _name(n):
    ''' A name for a file that wants no spaces '''
    return re.sub(r'[^A-Za-z0-9_.-]', '_', n) or 'zone'


def _coords(mesh, scale):
    P = mesh.nodes
    for i in range(mesh.node_count):
        yield P[3 * i] * scale, P[3 * i + 1] * scale, P[3 * i + 2] * scale


def _chunks(values, per_line, sep=' '):
    for i in range(0, len(values), per_line):
        yield sep.join(str(v) for v in values[i:i + per_line])


def _all_cells(mesh):
    ''' Every cell in the order the writers number them: (kind, corners and the other nodes, zone) '''
    for kind in SOLIDS:
        conn = mesh.cells.get(kind)
        if not conn:
            continue
        n = NODES[kind]
        zones = mesh.cell_zone.get(kind)
        for i in range(len(conn) // n):
            yield kind, tuple(conn[n * i:n * i + n]), (zones[i] if zones is not None else 0)


def _all_faces(mesh):
    for kind in SURFACES:
        conn = mesh.faces.get(kind)
        if not conn:
            continue
        n = NODES[kind]
        zones = mesh.face_zone.get(kind)
        for i in range(len(conn) // n):
            yield kind, tuple(conn[n * i:n * i + n]), (zones[i] if zones is not None else 0)


def _tet10_for_gmsh(nodes):
    # (the corners, then the middle of 0-1, 1-2, 2-0, 0-3, 2-3, 1-3)
    return nodes[:4] + (nodes[4], nodes[5], nodes[6], nodes[7], nodes[9], nodes[8])


################################################################################
# Gmsh

_GMSH_TYPE = {'tri3': 2, 'quad4': 3, 'tet4': 4, 'hex8': 5, 'prism6': 6, 'tet10': 11}


def _write_gmsh(mesh, path, scale, options):
    use_zones = options.get('zones', True)
    out = ['$MeshFormat', '2.2 0 8', '$EndMeshFormat']
    names = []
    if use_zones:
        for i, n in enumerate(mesh.zone_names):
            names.append('2 {} "{}"'.format(i + 1, _name(n)))
        if any(mesh.cell_count(k) for k in SOLIDS):
            for i, n in enumerate(mesh.cell_zone_names):
                names.append('3 {} "{}"'.format(i + 1, _name(n)))
    if names:
        out.append('$PhysicalNames')
        out.append(str(len(names)))
        out.extend(names)
        out.append('$EndPhysicalNames')
    out.append('$Nodes')
    out.append(str(mesh.node_count))
    for i, (x, y, z) in enumerate(_coords(mesh, scale)):
        out.append('{} {!r} {!r} {!r}'.format(i + 1, x, y, z))
    out.append('$EndNodes')
    elements = []
    eid = 1
    for kind, nodes, zone in _all_faces(mesh):
        elements.append('{} {} 2 {} {} {}'.format(eid, _GMSH_TYPE[kind], zone + 1, zone + 1, ' '.join(str(v + 1) for v in nodes)))
        eid += 1
    for kind, nodes, zone in _all_cells(mesh):
        if kind == 'tet10':
            nodes = _tet10_for_gmsh(nodes)
        elements.append('{} {} 2 {} {} {}'.format(eid, _GMSH_TYPE[kind], zone + 1, zone + 1, ' '.join(str(v + 1) for v in nodes)))
        eid += 1
    out.append('$Elements')
    out.append(str(len(elements)))
    out.extend(elements)
    out.append('$EndElements')
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


################################################################################
# VTK

_VTK_TYPE = {'tri3': 5, 'quad4': 9, 'tet4': 10, 'tet10': 24, 'hex8': 12, 'prism6': 13}


def _write_vtk(mesh, path, scale, options):
    cells = list(_all_cells(mesh))
    names = {}
    if not cells or options.get('boundary'):
        faces = list(_all_faces(mesh))
        if not cells:
            cells, faces = faces, []
        else:
            cells = cells + faces
    zone_of = [z for _, _, z in cells]
    cell_dim = [3 if k in SOLIDS else 2 for k, _, _ in cells]
    points = ' '.join('{!r} {!r} {!r}'.format(*p) for p in _coords(mesh, scale))
    conn = []
    offsets = []
    types = []
    total = 0
    for kind, nodes, _ in cells:
        if kind == 'prism6':
            nodes = (nodes[0], nodes[2], nodes[1], nodes[3], nodes[5], nodes[4])       # (VTK's wedge has the normal of its first triangle away from the other)
        conn.extend(nodes)
        total += len(nodes)
        offsets.append(total)
        types.append(_VTK_TYPE[kind])
    out = ['<?xml version="1.0"?>',
           '<VTKFile type="UnstructuredGrid" version="0.1" byte_order="LittleEndian">',
           '<UnstructuredGrid><Piece NumberOfPoints="{}" NumberOfCells="{}">'.format(mesh.node_count, len(cells)),
           '<Points><DataArray type="Float64" NumberOfComponents="3" format="ascii">', points, '</DataArray></Points>',
           '<Cells>',
           '<DataArray type="Int32" Name="connectivity" format="ascii">', ' '.join(map(str, conn)), '</DataArray>',
           '<DataArray type="Int32" Name="offsets" format="ascii">', ' '.join(map(str, offsets)), '</DataArray>',
           '<DataArray type="UInt8" Name="types" format="ascii">', ' '.join(map(str, types)), '</DataArray>',
           '</Cells>']
    if options.get('zones', True):
        out.append('<CellData>')
        out.append('<DataArray type="Int32" Name="zone" format="ascii">')
        out.append(' '.join(map(str, zone_of)))
        out.append('</DataArray>')
        out.append('<DataArray type="Int32" Name="dimension" format="ascii">')
        out.append(' '.join(map(str, cell_dim)))
        out.append('</DataArray>')
        out.append('</CellData>')
    out.append('</Piece></UnstructuredGrid></VTKFile>')
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')
    if options.get('zones', True):
        # (the names of the zones next to it: a .vtu has no place for them)
        with open(os.path.splitext(path)[0] + '.zones.txt', 'w', encoding='utf-8') as f:
            f.write('zone numbers of the cells (dimension 3) and of the boundary faces (dimension 2)\n')
            for i, n in enumerate(mesh.cell_zone_names):
                f.write('cells {} {}\n'.format(i, n))
            for i, n in enumerate(mesh.zone_names):
                f.write('faces {} {}\n'.format(i, n))


################################################################################
# Abaqus

_ABAQUS_TYPE = {'tet4': 'C3D4', 'tet10': 'C3D10', 'hex8': 'C3D8', 'prism6': 'C3D6', 'tri3': 'S3', 'quad4': 'S4'}


def _write_abaqus(mesh, path, scale, options):
    out = ['*HEADING', 'FielDes mesh {} ({})'.format(mesh.name, mesh)[:200].replace('\n', ' '), '*NODE']
    for i, (x, y, z) in enumerate(_coords(mesh, scale)):
        out.append('{}, {!r}, {!r}, {!r}'.format(i + 1, x, y, z))
    eid = 1
    groups = {}
    order = []
    source = list(_all_cells(mesh)) if any(mesh.cell_count(k) for k in SOLIDS) else list(_all_faces(mesh))
    zone_names = mesh.cell_zone_names if any(mesh.cell_count(k) for k in SOLIDS) else mesh.zone_names
    for kind, nodes, zone in source:
        key = (kind, zone)
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append((eid, nodes))
        eid += 1
    for kind, zone in order:
        elset = _name(zone_names[zone] if zone < len(zone_names) else 'zone_{}'.format(zone))
        out.append('*ELEMENT, TYPE={}, ELSET={}'.format(_ABAQUS_TYPE[kind], elset))
        for e, nodes in groups[(kind, zone)]:
            if kind == 'tet10':
                out.append(', '.join([str(e)] + [str(v + 1) for v in nodes[:8]]))
                out.append(', '.join(str(v + 1) for v in nodes[8:]))
            else:
                out.append(', '.join([str(e)] + [str(v + 1) for v in nodes]))
    if options.get('zones', True) and any(mesh.cell_count(k) for k in SOLIDS):
        sets = {}
        for kind, nodes, zone in _all_faces(mesh):
            sets.setdefault(zone, set()).update(nodes[:CORNERS[kind]])
        for zone in sorted(sets):
            out.append('*NSET, NSET={}'.format(_name(mesh.zone_names[zone])))
            out.extend(', '.join(str(v + 1) for v in chunk) for chunk in _slices(sorted(sets[zone]), 16))
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


def _slices(values, n):
    for i in range(0, len(values), n):
        yield values[i:i + n]


################################################################################
# Nastran

def _write_nastran(mesh, path, scale, options):
    if not any(mesh.cell_count(k) for k in SOLIDS):
        raise MeshError('nastran: a surface mesh has no solid elements to write')
    out = ['$ FielDes mesh {}'.format(mesh.name), 'BEGIN BULK']
    for i, (x, y, z) in enumerate(_coords(mesh, scale)):
        out.append('GRID,{},,{!r},{!r},{!r}'.format(i + 1, x, y, z))
    eid = 1
    names = {'tet4': 'CTETRA', 'tet10': 'CTETRA', 'prism6': 'CPENTA', 'hex8': 'CHEXA'}
    for kind, nodes, zone in _all_cells(mesh):
        ids = [str(v + 1) for v in nodes]
        if kind == 'tet10':
            out.append('{},{},{},{}'.format(names[kind], eid, zone + 1, ','.join(ids[:6])))
            out.append('+,{}'.format(','.join(ids[6:])))
        else:
            out.append('{},{},{},{}'.format(names[kind], eid, zone + 1, ','.join(ids)))
        eid += 1
    for i in range(len(mesh.cell_zone_names)):
        out.append('PSOLID,{},1'.format(i + 1))
    out.append('$ the property numbers are the cell zones: ' + ', '.join('{} {}'.format(i + 1, n) for i, n in enumerate(mesh.cell_zone_names)))
    out.append('ENDDATA')
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


################################################################################
# I-DEAS universal

_UNV_TYPE = {'tri3': 91, 'quad4': 94, 'tet4': 111, 'hex8': 115}


def _write_unv(mesh, path, scale, options):
    out = ['    -1', '  2411']
    for i, (x, y, z) in enumerate(_coords(mesh, scale)):
        out.append('{:10d}{:10d}{:10d}{:10d}'.format(i + 1, 1, 1, 11))
        out.append('{:25.16E}{:25.16E}{:25.16E}'.format(x, y, z))
    out.append('    -1')
    out.append('    -1')
    out.append('  2412')
    eid = 1
    solid = any(mesh.cell_count(k) for k in SOLIDS)
    members = []
    for kind, nodes, zone in list(_all_faces(mesh)) + list(_all_cells(mesh)):
        if kind not in _UNV_TYPE:
            raise MeshError('unv: no {} elements (the format is written for tetrahedra, hexahedra, triangles and quadrilaterals)'.format(kind))
        out.append('{:10d}{:10d}{:10d}{:10d}{:10d}{:10d}'.format(eid, _UNV_TYPE[kind], 1, 1, 7, len(nodes)))
        ids = [v + 1 for v in nodes]
        for chunk in _slices(ids, 8):
            out.append(''.join('{:10d}'.format(v) for v in chunk))
        members.append((kind in SURFACES, zone, eid))
        eid += 1
    out.append('    -1')
    if options.get('zones', True):
        groups = {}
        for is_face, zone, e in members:
            groups.setdefault((is_face, zone), []).append(e)
        out.append('    -1')
        out.append('  2467')
        for number, ((is_face, zone), ids) in enumerate(sorted(groups.items()), 1):
            names = mesh.zone_names if is_face else mesh.cell_zone_names
            out.append('{:10d}{:10d}{:10d}{:10d}{:10d}{:10d}{:10d}{:10d}'.format(number, 0, 0, 0, 0, 0, 0, len(ids)))
            out.append(_name(names[zone]) if zone < len(names) else 'zone_{}'.format(zone))
            pairs = ['{:10d}{:10d}{:10d}{:10d}'.format(8, e, 0, 0) for e in ids]
            for i in range(0, len(pairs), 2):
                out.append(''.join(pairs[i:i + 2]))
        out.append('    -1')
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


################################################################################
# The faces of the cells: Fluent and OpenFOAM

def _topology(mesh):
    ''' The cells and the faces between them: (cells: [(kind, corner nodes)], internal: [(nodes, c0, c1)], boundary: [(nodes, c0, zone)]).  The nodes of a face go round
        it with its normal out of c0 (and into c1); c0 is the lower numbered cell '''
    cells = []
    faces = {}
    zone_of = {}
    for kind, conn in mesh.faces.items():
        n = NODES[kind]
        zones = mesh.face_zone.get(kind)
        for i in range(len(conn) // n):
            zone_of[tuple(sorted(conn[n * i:n * i + n]))] = zones[i] if zones is not None else 0
    for kind, nodes, _ in _all_cells(mesh):
        corners = nodes[:CORNERS[kind]]
        c = len(cells)
        cells.append((kind, corners))
        for face in FACES[kind]:
            fn = tuple(corners[j] for j in face)
            key = tuple(sorted(fn))
            entry = faces.get(key)
            if entry is None:
                faces[key] = [fn, c, None]
            else:
                entry[2] = c
    internal = []
    boundary = []
    for key, (fn, c0, c1) in faces.items():
        if c1 is None:
            boundary.append((fn, c0, zone_of.get(key, 0)))
        else:
            internal.append((fn, c0, c1))
    internal.sort(key=lambda f: (f[1], f[2]))
    boundary.sort(key=lambda f: (f[2], f[1]))
    return cells, internal, boundary


_FLUENT_BC = {'wall': 3, 'pressure-inlet': 4, 'pressure-outlet': 5, 'symmetry': 7, 'velocity-inlet': 10, 'mass-flow-inlet': 20, 'interface': 24, 'outflow': 36,
              'axis': 37, 'periodic': 12}
_FLUENT_ELEMENT = {'tri3': 1, 'tet4': 2, 'tet10': 2, 'quad4': 3, 'hex8': 4, 'prism6': 6}


def _fluent_boundary(name, given):
    if name in given:
        t = given[name]
    else:
        low = name.lower()
        t = 'wall' if low.startswith('wall') else 'velocity-inlet' if 'inlet' in low else 'pressure-outlet' if 'outlet' in low else 'symmetry' if 'symm' in low else 'wall'
    if t not in _FLUENT_BC:
        raise ValueError('fluent: the type of a zone is one of {}'.format(', '.join(sorted(_FLUENT_BC))))
    return _FLUENT_BC[t]


def _write_fluent(mesh, path, scale, options):
    cells, internal, boundary = _topology(mesh)
    given = options.get('boundary_types') or {}
    used = sorted(set(k for k, _ in cells))
    nodes_used = mesh.node_count
    nfaces = len(internal) + len(boundary)
    out = ['(0 "FielDes mesh {}")'.format(mesh.name), '(0 "Dimension:")', '(2 3)',
           '(0 "Grid sizes:")',
           '(10 (0 1 {:x} 0 3))'.format(nodes_used), '(12 (0 1 {:x} 0 0))'.format(len(cells)), '(13 (0 1 {:x} 0 0))'.format(nfaces)]
    out.append('(10 (1 1 {:x} 1 3)('.format(nodes_used))
    for x, y, z in _coords(mesh, scale):
        out.append('{!r} {!r} {!r}'.format(x, y, z))
    out.append('))')
    # the cells: one zone, the type of its element, or mixed
    kinds = set(_FLUENT_ELEMENT[k] for k, _ in cells)
    if len(kinds) == 1:
        out.append('(12 (2 1 {:x} 1 {:x}))'.format(len(cells), kinds.pop()))
    else:
        out.append('(12 (2 1 {:x} 1 0)('.format(len(cells)))
        for chunk in _slices([_FLUENT_ELEMENT[k] for k, _ in cells], 40):
            out.append(' '.join(str(v) for v in chunk))
        out.append('))')
    first = 1
    zone_id = 3

    def face_section(zone, bc, rows):
        nonlocal first
        last = first + len(rows) - 1
        sizes = set(len(r[0]) for r in rows)
        if len(sizes) == 1:
            ftype = 3 if sizes.copy().pop() == 3 else 4
            lines = [' '.join('{:x}'.format(v + 1) for v in r[0]) + ' {:x} {:x}'.format(r[1], r[2]) for r in rows]
        else:
            ftype = 0
            lines = ['{:x} '.format(len(r[0])) + ' '.join('{:x}'.format(v + 1) for v in r[0]) + ' {:x} {:x}'.format(r[1], r[2]) for r in rows]
        out.append('(13 ({:x} {:x} {:x} {:x} {:x})('.format(zone, first, last, bc, ftype))
        out.extend(lines)
        out.append('))')
        first = last + 1

    if internal:
        face_section(zone_id, 2, [(fn, c0 + 1, c1 + 1) for fn, c0, c1 in internal])
        zone_id += 1
    by_zone = {}
    for fn, c0, z in boundary:
        by_zone.setdefault(z, []).append((fn, c0 + 1, 0))
    zone_names = []
    for z in sorted(by_zone):
        name = mesh.zone_names[z] if z < len(mesh.zone_names) else 'zone_{}'.format(z)
        face_section(zone_id, _fluent_boundary(name, given), by_zone[z])
        zone_names.append((zone_id, _name(name), _fluent_boundary(name, given)))
        zone_id += 1
    out.append('(0 "Zone sections:")')
    for zid, name, bc in zone_names:
        kind = [k for k, v in _FLUENT_BC.items() if v == bc][0]
        out.append('(45 ({} {} {})())'.format(zid, kind, name))
    out.append('(45 (2 fluid {})())'.format(_name(mesh.cell_zone_names[0]) if mesh.cell_zone_names else 'fluid'))
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')


def _foam_header(cls, obj, note=None):
    lines = ['FoamFile', '{', '    version     2.0;', '    format      ascii;', '    class       {};'.format(cls)]
    if note:
        lines.append('    note        "{}";'.format(note))
    lines.append('    object      {};'.format(obj))
    lines.append('}')
    return '\n'.join(lines) + '\n\n'


def _write_openfoam(mesh, folder, scale, options):
    cells, internal, boundary = _topology(mesh)
    types = options.get('patch_types') or {}
    # the nodes the faces use, numbered again in the order they first appear
    renumber = {}
    faces = []
    owner = []
    neighbour = []
    for fn, c0, c1 in internal:
        faces.append(fn)
        owner.append(c0)
        neighbour.append(c1)
    patches = []
    by_zone = {}
    for fn, c0, z in boundary:
        by_zone.setdefault(z, []).append((fn, c0))
    start = len(faces)
    for z in sorted(by_zone):
        for fn, c0 in by_zone[z]:
            faces.append(fn)
            owner.append(c0)
        name = mesh.zone_names[z] if z < len(mesh.zone_names) else 'zone_{}'.format(z)
        low = name.lower()
        kind = types.get(name) or ('wall' if low.startswith('wall') else 'symmetryPlane' if 'symmetry' in low else 'patch')
        patches.append((_name(name), kind, len(by_zone[z]), start))
        start += len(by_zone[z])
    for fn in faces:
        for v in fn:
            if v not in renumber:
                renumber[v] = len(renumber)
    inverse = sorted(renumber, key=renumber.get)
    note = 'nPoints:{}  nCells:{}  nFaces:{}  nInternalFaces:{}'.format(len(renumber), len(cells), len(faces), len(internal))
    os.makedirs(folder, exist_ok=True)
    P = mesh.nodes
    with open(os.path.join(folder, 'points'), 'w', encoding='ascii') as f:
        f.write(_foam_header('vectorField', 'points'))
        f.write('{}\n(\n'.format(len(inverse)))
        for v in inverse:
            f.write('({!r} {!r} {!r})\n'.format(P[3 * v] * scale, P[3 * v + 1] * scale, P[3 * v + 2] * scale))
        f.write(')\n')
    with open(os.path.join(folder, 'faces'), 'w', encoding='ascii') as f:
        f.write(_foam_header('faceList', 'faces'))
        f.write('{}\n(\n'.format(len(faces)))
        for fn in faces:
            f.write('{}({})\n'.format(len(fn), ' '.join(str(renumber[v]) for v in fn)))
        f.write(')\n')
    with open(os.path.join(folder, 'owner'), 'w', encoding='ascii') as f:
        f.write(_foam_header('labelList', 'owner', note))
        f.write('{}\n(\n'.format(len(owner)))
        f.write('\n'.join(str(v) for v in owner) + '\n)\n')
    with open(os.path.join(folder, 'neighbour'), 'w', encoding='ascii') as f:
        f.write(_foam_header('labelList', 'neighbour', note))
        f.write('{}\n(\n'.format(len(neighbour)))
        f.write('\n'.join(str(v) for v in neighbour) + '\n)\n')
    with open(os.path.join(folder, 'boundary'), 'w', encoding='ascii') as f:
        f.write(_foam_header('polyBoundaryMesh', 'boundary'))
        f.write('{}\n(\n'.format(len(patches)))
        for name, kind, n, s in patches:
            f.write('    {}\n    {{\n        type            {};\n'.format(name, kind))
            if kind == 'wall':
                f.write('        inGroups        List<word> 1(wall);\n')
            f.write('        nFaces          {};\n        startFace       {};\n    }}\n'.format(n, s))
        f.write(')\n')
    # the zones of cells, when there is more than one
    index = {}
    k = 0
    for kind, nodes, zone in _all_cells(mesh):
        index.setdefault(zone, []).append(k)
        k += 1
    if len(index) > 1:
        with open(os.path.join(folder, 'cellZones'), 'w', encoding='ascii') as f:
            f.write(_foam_header('regIOobject', 'cellZones'))
            f.write('{}\n(\n'.format(len(index)))
            for z in sorted(index):
                f.write('{}\n{{\n    type cellZone;\n    cellLabels List<label> {}\n(\n'.format(_name(mesh.cell_zone_names[z]), len(index[z])))
                f.write('\n'.join(str(v) for v in index[z]) + '\n)\n;\n}\n')
            f.write(')\n')


################################################################################
# The surface alone

def _triangles(mesh):
    for kind, nodes, zone in _all_faces(mesh):
        if kind == 'tri3':
            yield nodes, zone
        else:
            yield (nodes[0], nodes[1], nodes[2]), zone
            yield (nodes[0], nodes[2], nodes[3]), zone


def _write_stl(mesh, path, scale, options):
    P = mesh.nodes
    tris = list(_triangles(mesh))
    with open(path, 'wb') as f:
        f.write(('FielDes mesh ' + mesh.name).encode('ascii', 'replace')[:80].ljust(80, b' '))
        f.write(struct.pack('<I', len(tris)))
        for nodes, _ in tris:
            a, b, c = [(P[3 * v] * scale, P[3 * v + 1] * scale, P[3 * v + 2] * scale) for v in nodes]
            ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
            vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
            nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
            length = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
            f.write(struct.pack('<12fH', nx / length, ny / length, nz / length, *a, *b, *c, 0))


def _write_obj(mesh, path, scale, options):
    out = ['# FielDes mesh {}'.format(mesh.name)]
    for x, y, z in _coords(mesh, scale):
        out.append('v {!r} {!r} {!r}'.format(x, y, z))
    by_zone = {}
    for kind, nodes, zone in _all_faces(mesh):
        by_zone.setdefault(zone, []).append(nodes)
    for zone in sorted(by_zone):
        out.append('g {}'.format(_name(mesh.zone_names[zone]) if zone < len(mesh.zone_names) else 'zone_{}'.format(zone)))
        for nodes in by_zone[zone]:
            out.append('f ' + ' '.join(str(v + 1) for v in nodes))
    with open(path, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')
