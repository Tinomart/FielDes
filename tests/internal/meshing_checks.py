'''
Checks of the meshing suite (python/fieldes/stdlib/meshing.py and meshwriters.py): the meshes of a box, a sphere and the air round a sphere against their exact volumes and areas, no inverted element, conforming faces, zones, a size field,
boundary layers, every file read back -- and every cell of the Fluent and OpenFOAM meshes closed (the outward area vectors of its faces add up to zero) -- the element mesh handed to the viewer, and example 36.

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/meshing_checks.py
It prints PASS or FAIL for every check, and exits with 1 when one failed (about 20 s).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import tempfile
import math
import os
import re
import sys
import time
import traceback
import xml.etree.ElementTree as ET

from fieldes import *
from fieldes.stdlib import meshing, meshwriters

OUT = tempfile.mkdtemp(prefix='fieldes-mesh-')
os.makedirs(OUT, exist_ok=True)
failures = []
t_all = time.time()


def check(label, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + detail) if detail else ''))
    sys.stdout.flush()
    if not ok:
        failures.append(label)


def stage(name):
    global t_stage
    t_stage = time.time()
    print('--- ' + name)
    sys.stdout.flush()


def took():
    return '{:.1f} s'.format(time.time() - t_stage)


def conforming(mesh):
    ''' every face of the cells is shared by at most two cells, and the faces that one cell has are the boundary faces '''
    seen = {}
    for kind in meshing.SOLIDS:
        conn = mesh.cells.get(kind)
        if not conn:
            continue
        n, k = meshing.NODES[kind], meshing.CORNERS[kind]
        for i in range(len(conn) // n):
            ids = conn[n * i:n * i + k]
            for f in meshing.FACES[kind]:
                key = tuple(sorted(ids[j] for j in f))
                seen[key] = seen.get(key, 0) + 1
    over = sum(1 for v in seen.values() if v > 2)
    one = sum(1 for v in seen.values() if v == 1)
    return over, one


def run(label, fn):
    try:
        return fn()
    except Exception:
        print('FAIL  {} raised:'.format(label))
        traceback.print_exc()
        sys.stdout.flush()
        failures.append(label + ' (exception)')
        return None


# ---------------------------------------------------------------- 1. a box of tetrahedra
stage('1. box 20 x 10 x 6, tetrahedra 2 mm')
body = box((0, 0, 0), (20, 10, 6))
m = run('mesh_volume box', lambda: mesh_volume(body, 2.0))
if m is not None:
    print(m)
    vol = m.volume()
    check('box volume', abs(vol - 1200.0) < 0.5, '{:.4f} (exact 1200)'.format(vol))
    area = m.surface_area()
    check('box surface area', abs(area - 760.0) < 8.0, '{:.2f} (exact 760)'.format(area))
    over, one = conforming(m)
    check('conforming: no face of more than two cells', over == 0, str(over))
    check('boundary faces = faces of one cell', one == m.face_count(), '{} {}'.format(one, m.face_count()))
    q = m.quality()
    print(q.report())
    check('no inverted tetrahedra', q.negative == 0, str(q.negative))
    check('skewness max below 0.95', q['skewness'][2] < 0.95, '{:.3f}'.format(q['skewness'][2]))
    check('zones: a wall', m.zone_names == ['wall'], str(m.zone_names))
    print(took())

# ---------------------------------------------------------------- 2. tet10 and hex
stage('2. the same box as 10-node tetrahedra and hexahedra')
m10 = run('tet10', lambda: mesh_volume(body, 2.0, element='tet10'))
if m10 is not None:
    print(m10)
    check('tet10 volume', abs(m10.volume() - 1200.0) < 0.5, '{:.4f}'.format(m10.volume()))
    check('tet10 nodes = corners + edges', m10.node_count > m.node_count)
mh = run('hex', lambda: mesh_volume(body, 2.0, element='hex'))
if mh is not None:
    print(mh)
    check('hex volume', abs(mh.volume() - 1200.0) < 0.5, '{:.4f}'.format(mh.volume()))
    qh = mh.quality()
    print(qh.report())
    check('no inverted hexahedra', qh.negative == 0, str(qh.negative))
    over, one = conforming(mh)
    check('hex conforming', over == 0, str(over))
    check('hex boundary faces are quads', mh.face_count('quad4') == mh.face_count() and mh.face_count() > 0, str(mh.faces.keys()))
    check('hex count = 4 x tet count', mh.cell_count('hex8') == 4 * m.cell_count('tet4'), '{} {}'.format(mh.cell_count('hex8'), m.cell_count('tet4')))
print(took())

# ---------------------------------------------------------------- 3. a sphere
stage('3. sphere r 10, 1.5 mm, curved boundary')
sph = sphere(10, (0, 0, 0))
ms = run('sphere', lambda: mesh_volume(sph, 1.5))
if ms is not None:
    print(ms)
    exact = 4.0 / 3.0 * math.pi * 1000.0
    check('sphere volume within 2 %', abs(ms.volume() - exact) / exact < 0.02, '{:.1f} (exact {:.1f})'.format(ms.volume(), exact))
    area_exact = 4.0 * math.pi * 100.0
    check('sphere area within 3 %', abs(ms.surface_area() - area_exact) / area_exact < 0.03, '{:.1f} (exact {:.1f})'.format(ms.surface_area(), area_exact))
    print(took())
    ms10 = run('sphere tet10', lambda: mesh_volume(sph, 1.5, element='tet10'))
    if ms10 is not None:
        # (the midside nodes of the boundary are on the surface)
        P = ms10.nodes
        conn = ms10.cells['tet10']
        mid_of = {}
        for i in range(len(conn) // 10):
            row = conn[10 * i:10 * i + 10]
            for k, (a, b) in enumerate(((0, 1), (1, 2), (2, 0), (0, 3), (1, 3), (2, 3))):
                e = (row[a], row[b]) if row[a] < row[b] else (row[b], row[a])
                mid_of[e] = row[4 + k]
        radii = []
        tris = ms10.faces['tri3']
        for j in range(ms10.face_count('tri3')):
            f = tris[3 * j:3 * j + 3]
            for a, b in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
                m_ = mid_of[(a, b) if a < b else (b, a)]
                radii.append(math.sqrt(P[3 * m_] ** 2 + P[3 * m_ + 1] ** 2 + P[3 * m_ + 2] ** 2))
        worst = max(abs(r - 10.0) for r in radii)
        check('tet10 boundary midnodes on the sphere', worst < 0.02, 'worst {:.4f} mm off the surface, {} nodes'.format(worst, len(radii)))

# ---------------------------------------------------------------- 4. a size field
stage('4. sphere with a size field: 0.8 mm at x = -10 growing to 2.4 mm at x = +10')
field_mesh = run('size function', lambda: mesh_volume(sph, lambda pts: [0.8 + 0.08 * (p[0] + 10.0) for p in pts], element='tet'))
if field_mesh is not None:
    print(field_mesh)
    for note in field_mesh.notes:
        print('   note: ' + note)
    P = field_mesh.nodes
    conn = field_mesh.cells['tet4']
    left, right = [], []
    for i in range(len(conn) // 4):
        ids = conn[4 * i:4 * i + 4]
        cx = sum(P[3 * v] for v in ids) / 4.0
        edge = max(math.sqrt(sum((P[3 * a + k] - P[3 * b + k]) ** 2 for k in range(3))) for a, b in meshing._PAIRS for a, b in [(ids[a], ids[b])])
        (left if cx < -6.0 else right if cx > 6.0 else []).append(edge)
    ml, mr = sum(left) / max(1, len(left)), sum(right) / max(1, len(right))
    check('edges grow along x', mr > 1.5 * ml, 'mean longest edge {:.2f} on the left, {:.2f} on the right'.format(ml, mr))
    check('size field volume within 3 %', abs(field_mesh.volume() - exact) / exact < 0.03, '{:.1f}'.format(field_mesh.volume()))
    over, one = conforming(field_mesh)
    check('refined mesh conforming', over == 0, str(over))
    qf = field_mesh.quality()
    print(qf.report())
    check('refined mesh: no inverted tetrahedra', qf.negative == 0, str(qf.negative))
    print(took())

# ---------------------------------------------------------------- 5. graded_size
stage('5. graded_size on a box')
gm = run('graded', lambda: mesh_volume(body, graded_size(body, wall_size=1.0, growth_rate=1.5, max_size=3.0)))
if gm is not None:
    print(gm)
    # (the kernel rounds an edge that runs between lattice points within one element: 2.4 mm elements on a box of 20 x 10 x 6 lose about 1.4 %)
    check('graded volume within 3 %', abs(gm.volume() - 1200.0) < 36.0, '{:.2f}'.format(gm.volume()))
    check('graded: no inverted', gm.quality().negative == 0)
    print(took())

# ---------------------------------------------------------------- 6. zones
stage('6. zones on the box')
zm = run('zones', lambda: mesh_volume(body, 2.0, zones={'inlet': box((-1, -1, -1), (0.01, 11, 7)), 'outlet': box((19.99, -1, -1), (21, 11, 7))}))
if zm is not None:
    print(zm)
    names = zm.zone_names
    check('zones are inlet, outlet, wall', set(names) == {'inlet', 'outlet', 'wall'}, str(names))

    def zone_area(mesh, name):
        z = mesh.zone_names.index(name)
        total = 0.0
        for kind in meshing.SURFACES:
            conn = mesh.faces.get(kind)
            if not conn:
                continue
            n = meshing.NODES[kind]
            for i in range(len(conn) // n):
                if mesh.face_zone[kind][i] == z:
                    ax, ay, az = meshing._polygon_area_vector(mesh.nodes, conn[n * i:n * i + n])
                    total += math.sqrt(ax * ax + ay * ay + az * az)
        return total

    check('inlet area 60', abs(zone_area(zm, 'inlet') - 60.0) < 1.0, '{:.2f}'.format(zone_area(zm, 'inlet')))
    check('outlet area 60', abs(zone_area(zm, 'outlet') - 60.0) < 1.0, '{:.2f}'.format(zone_area(zm, 'outlet')))
    check('wall area 640', abs(zone_area(zm, 'wall') - 640.0) < 7.0, '{:.2f}'.format(zone_area(zm, 'wall')))
zs = run('feature angle', lambda: mesh_volume(body, 2.0, feature_angle=30))
if zs is not None:
    check('feature angle splits the box into six walls', len(zs.zone_names) == 6, str(zs.zone_names))
print(took())

# ---------------------------------------------------------------- 7. boundary layers
stage('7. boundary layers on a sphere in a box of air')
air = difference(box((-20, -20, -20), (20, 20, 20)), sphere(5, (0, 0, 0)))
layers = inflation_layers(sphere(5, (0, 0, 0)), first_height=0.2, growth_rate=1.2, layers=4)
bm = run('boundary layers', lambda: mesh_volume(air, 3.0, boundary_layers=layers, zones={'farfield': difference(box((-30, -30, -30), (30, 30, 30)), sphere(15, (0, 0, 0)))}))
if bm is not None:
    print(bm)
    for note in bm.notes:
        print('   note: ' + note)
    air_volume = 64000.0 - 4.0 / 3.0 * math.pi * 125.0
    check('air volume within 3 %', abs(bm.volume() - air_volume) / air_volume < 0.03, '{:.1f} (exact {:.1f})'.format(bm.volume(), air_volume))
    check('prisms exist', bm.cell_count('prism6') > 0, str(bm.cell_count('prism6')))
    qb = bm.quality()
    print(qb.report())
    check('layers: no inverted element', qb.negative == 0, str(qb.negative))
    over, one = conforming(bm)
    check('layered mesh conforming', over == 0, str(over))
    check('boundary faces = faces of one cell', one == bm.face_count(), '{} {}'.format(one, bm.face_count()))
    wall = bm.zone_names.index('wall') if 'wall' in bm.zone_names else -1
    wall_area = 0.0
    if wall >= 0:
        for kind in meshing.SURFACES:
            conn = bm.faces.get(kind)
            if not conn:
                continue
            n = meshing.NODES[kind]
            for i in range(len(conn) // n):
                if bm.face_zone[kind][i] == wall:
                    ax, ay, az = meshing._polygon_area_vector(bm.nodes, conn[n * i:n * i + n])
                    wall_area += math.sqrt(ax * ax + ay * ay + az * az)
    sphere_area = 4.0 * math.pi * 25.0
    check('wall area is the sphere', abs(wall_area - sphere_area) / sphere_area < 0.05, '{:.1f} (exact {:.1f})'.format(wall_area, sphere_area))
    # the first prism layer is as thick as asked
    P = bm.nodes
    pr = bm.cells['prism6']
    heights = []
    for i in range(min(200, len(pr) // 6)):
        ids = pr[6 * i:6 * i + 6]
        top = sum(math.sqrt(P[3 * ids[3 + k]] ** 2 + P[3 * ids[3 + k] + 1] ** 2 + P[3 * ids[3 + k] + 2] ** 2) for k in range(3)) / 3.0
        bottom = sum(math.sqrt(P[3 * ids[k]] ** 2 + P[3 * ids[k] + 1] ** 2 + P[3 * ids[k] + 2] ** 2) for k in range(3)) / 3.0
        heights.append(abs(top - bottom))
    print('   prism heights (first 200): min {:.4f} max {:.4f}'.format(min(heights), max(heights)))
    check('prism heights are the layers', 0.15 < min(heights) and max(heights) < 0.4, '{:.3f} .. {:.3f}'.format(min(heights), max(heights)))
    print(took())

# ---------------------------------------------------------------- 8. the surface alone
stage('8. mesh_surface of the sphere, quads and triangles')
sq = run('surface quad', lambda: mesh_surface(sph, 1.5, element='quad'))
if sq is not None:
    print(sq)
    check('quad surface: all quads', sq.face_count('quad4') == sq.face_count() > 0)
    check('quad surface area', abs(sq.surface_area() - area_exact) / area_exact < 0.04, '{:.1f}'.format(sq.surface_area()))
st = run('surface tri', lambda: mesh_surface(sph, 1.5))
check('tri surface', st is not None and st.face_count('tri3') > 0)
print(took())

# ---------------------------------------------------------------- 9. writers
stage('9. every writer, read back')


def read_gmsh(path):
    text = open(path).read()
    nodes = int(re.search(r'\$Nodes\n(\d+)', text).group(1))
    elems = re.search(r'\$Elements\n(\d+)\n(.*?)\$EndElements', text, re.S)
    count = int(elems.group(1))
    lines = elems.group(2).strip().split('\n')
    types = {}
    for ln in lines:
        t = int(ln.split()[1])
        types[t] = types.get(t, 0) + 1
    return nodes, count, len(lines), types


def closed_cells(points, faces, owner, neighbour, ncells):
    ''' the sum of the outward area vectors of every cell: zero when the cell is closed and its faces point out '''
    sums = [[0.0, 0.0, 0.0] for _ in range(ncells)]
    for fi, fn in enumerate(faces):
        x0, y0, z0 = points[fn[0]]
        ax = ay = az = 0.0
        for k in range(1, len(fn) - 1):
            ux, uy, uz = [points[fn[k]][a] - (x0, y0, z0)[a] for a in range(3)]
            vx, vy, vz = [points[fn[k + 1]][a] - (x0, y0, z0)[a] for a in range(3)]
            ax += 0.5 * (uy * vz - uz * vy)
            ay += 0.5 * (uz * vx - ux * vz)
            az += 0.5 * (ux * vy - uy * vx)
        o = owner[fi]
        for a, v in enumerate((ax, ay, az)):
            sums[o][a] += v
        if fi < len(neighbour):
            for a, v in enumerate((ax, ay, az)):
                sums[neighbour[fi]][a] -= v
    return max(math.sqrt(s[0] ** 2 + s[1] ** 2 + s[2] ** 2) for s in sums)


for label, mesh in (('tet', zm), ('hex', mh), ('layers', bm)):
    if mesh is None:
        continue
    ncell = mesh.cell_count()
    nnode = mesh.node_count
    base = os.path.join(OUT, label)
    # gmsh
    p = run('gmsh ' + label, lambda: mesh.export(base + '.msh'))
    if p:
        nodes, count, lines, types = read_gmsh(p)
        check('gmsh {}: nodes'.format(label), nodes == nnode, '{} {}'.format(nodes, nnode))
        check('gmsh {}: elements = cells + faces'.format(label), count == lines == ncell + mesh.face_count(), '{} {} {}'.format(count, lines, ncell + mesh.face_count()))
    # vtk
    p = run('vtk ' + label, lambda: mesh.export(base + '.vtu'))
    if p:
        root = ET.parse(p).getroot()
        piece = root.find('.//Piece')
        check('vtk {}: cells'.format(label), int(piece.get('NumberOfCells')) == ncell, piece.get('NumberOfCells'))
        check('vtk {}: points'.format(label), int(piece.get('NumberOfPoints')) == nnode)
    # abaqus
    p = run('abaqus ' + label, lambda: mesh.export(base + '.inp'))
    if p:
        text = open(p).read()
        lines = text.split('\n')
        # (elements: lines of *ELEMENT blocks that start with a number and are not continuation lines of a tet10)
        in_el = False
        rows = 0
        for ln in lines:
            if ln.startswith('*'):
                in_el = ln.startswith('*ELEMENT')
                continue
            if in_el and ln.strip():
                rows += 1
        check('abaqus {}: element rows'.format(label), rows == ncell, '{} {}'.format(rows, ncell))
    # nastran
    p = run('nastran ' + label, lambda: mesh.export(base + '.bdf'))
    if p:
        text = open(p).read()
        n_el = len(re.findall(r'^(CTETRA|CPENTA|CHEXA),', text, re.M))
        check('nastran {}: elements'.format(label), n_el == ncell, '{} {}'.format(n_el, ncell))
    # unv (not for prisms)
    if label != 'layers':
        p = run('unv ' + label, lambda: mesh.export(base + '.unv'))
        if p:
            text = open(p).read()
            parts = text.split('  2412\n')
            check('unv {}: has nodes and elements'.format(label), '  2411' in text and len(parts) == 2)
    # fluent
    p = run('fluent ' + label, lambda: mesh.export(base + '.fluent.msh', format='fluent'))
    if p:
        text = open(p).read()
        faces = []
        owner = []
        nb = []
        for m_ in re.finditer(r'\(13 \(([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+)\)\(\n(.*?)\n\)\)', text, re.S):
            ftype = int(m_.group(5), 16)
            for ln in m_.group(6).split('\n'):
                v = [int(x, 16) for x in ln.split()]
                if ftype == 0:
                    n = v[0]
                    fn = [x - 1 for x in v[1:1 + n]]
                    c0, c1 = v[1 + n], v[2 + n]
                else:
                    fn = [x - 1 for x in v[:-2]]
                    c0, c1 = v[-2], v[-1]
                faces.append((fn, c0 - 1, c1 - 1 if c1 else None))
        pts = []
        pm = re.search(r'\(10 \(1 1 [0-9a-f]+ 1 3\)\(\n(.*?)\n\)\)', text, re.S)
        for ln in pm.group(1).split('\n'):
            pts.append(tuple(float(x) for x in ln.split()))
        internal = [f for f in faces if f[2] is not None]
        owner = [f[1] for f in faces]
        order = sorted(range(len(faces)), key=lambda i: (faces[i][2] is None))
        faces_o = [faces[i] for i in order]
        worst = closed_cells(pts, [f[0] for f in faces_o], [f[1] for f in faces_o], [f[2] for f in faces_o if f[2] is not None], ncell)
        check('fluent {}: nodes'.format(label), len(pts) == nnode)
        check('fluent {}: cells are closed (outward faces sum to zero)'.format(label), worst < 1e-6, 'worst {:.2e}'.format(worst))
        check('fluent {}: boundary faces'.format(label), len(faces) - len(internal) == mesh.face_count(), '{} {}'.format(len(faces) - len(internal), mesh.face_count()))
    # openfoam
    folder = os.path.join(OUT, label + '_foam', 'constant', 'polyMesh')
    p = run('openfoam ' + label, lambda: mesh.export(folder))
    if p:
        def numbers(name):
            text = open(os.path.join(folder, name)).read()
            body_ = text.split('\n(\n', 1)[1].rsplit('\n)', 1)[0]
            return body_.strip().split('\n')

        points = [tuple(float(x) for x in ln.strip('()').split()) for ln in numbers('points')]
        faces_ = []
        for ln in numbers('faces'):
            inside = ln[ln.index('(') + 1:ln.rindex(')')]
            faces_.append([int(x) for x in inside.split()])
        owner_ = [int(x) for x in numbers('owner')]
        nb_ = [int(x) for x in numbers('neighbour')]
        check('openfoam {}: owner < neighbour'.format(label), all(o < n for o, n in zip(owner_, nb_)))
        check('openfoam {}: upper triangular order'.format(label), all((owner_[i], nb_[i]) <= (owner_[i + 1], nb_[i + 1]) for i in range(len(nb_) - 1)))
        worst = closed_cells(points, faces_, owner_, nb_, ncell)
        check('openfoam {}: cells are closed'.format(label), worst < 1e-6, 'worst {:.2e}'.format(worst))
        check('openfoam {}: faces = internal + boundary'.format(label), len(faces_) == len(nb_) + mesh.face_count(), '{} {} {}'.format(len(faces_), len(nb_), mesh.face_count()))
        btext = open(os.path.join(folder, 'boundary')).read()
        counts = [int(x) for x in re.findall(r'nFaces\s+(\d+);', btext)]
        check('openfoam {}: patch faces add up'.format(label), sum(counts) == mesh.face_count(), '{} {}'.format(sum(counts), mesh.face_count()))
    # stl / obj
    p = run('stl ' + label, lambda: mesh.export(base + '.stl'))
    if p:
        tris = (os.path.getsize(p) - 84) // 50
        expect = mesh.face_count('tri3') + 2 * mesh.face_count('quad4')
        check('stl {}: triangles'.format(label), tris == expect, '{} {}'.format(tris, expect))
    p = run('obj ' + label, lambda: mesh.export(base + '.obj'))
    if p:
        text = open(p).read()
        check('obj {}: faces'.format(label), len(re.findall(r'^f ', text, re.M)) == mesh.face_count())
print(took())

# ---------------------------------------------------------------- 10. export_mesh and the default size
stage('10. export_mesh of a Mesh, the default element size')
p = run('export_mesh', lambda: export_mesh(zm, os.path.join(OUT, 'via_export_mesh.vtu')))
check('export_mesh writes a Mesh', p is not None and os.path.exists(p))
dm = run('default size', lambda: mesh_volume(box((0, 0, 0), (60, 40, 6))))
if dm is not None:
    print(dm)
    check('default size: about 40 along the longest side', 1.2 < dm.element_size < 1.8, '{:.3f}'.format(dm.element_size))
# (a fat body is held by the budget: a 10 mm cube is about 0.49 mm, 107,000 tetrahedra measured -- a mesh the viewer draws, with its quality numbers -- and not the 0.29 mm, half a million, that it was)
from fieldes.stdlib.element_size import default_element_size
cube_size = run('default size of a cube', lambda: default_element_size(box((0, 0, 0), (10, 10, 10)), (0, 0, 0), (10, 10, 10), 40.0, meshing.DEFAULT_ELEMENTS))
check('default size: a cube is held by the budget of 100,000 tetrahedra (0.4 to 0.6 mm)', cube_size is not None and 0.4 < cube_size < 0.6 and meshing.DEFAULT_ELEMENTS <= meshing.MAX_DISPLAYED,
      '{} mm, budget {}, drawn up to {}'.format(cube_size, meshing.DEFAULT_ELEMENTS, meshing.MAX_DISPLAYED))
print(took())

# ---------------------------------------------------------------- 11. what FielDes draws
stage('11. what FielDes draws: the element mesh handed to the viewer')
import array as _array

for label, mesh in (('tet', zm), ('tet10', m10), ('hex', mh), ('layers', bm), ('refined', field_mesh)):
    if mesh is None:
        continue
    shown = run('display ' + label, mesh._display)
    if shown is None:
        continue
    fm = shown._fea_mesh
    nv = mesh.node_count
    check('display {}: an element mesh of 7 parts'.format(label), fm is not None and len(fm) == 7)
    if fm is None or len(fm) != 7:
        continue
    verts, tets, faces, face_tet, text, vals, disp = fm
    nt = len(tets) // 16
    check('display {}: vertices are 3 float32 each'.format(label), len(verts) == 12 * nv, '{} {}'.format(len(verts), 12 * nv))
    check('display {}: tetrahedra'.format(label), len(tets) % 16 == 0 and nt >= mesh.cell_count(), '{} tets for {} cells'.format(nt, mesh.cell_count()))
    check('display {}: faces and their tetrahedra agree'.format(label), len(faces) // 12 == len(face_tet) // 4 > 0, '{} {}'.format(len(faces) // 12, len(face_tet) // 4))
    check('display {}: a value per tetrahedron for every channel'.format(label), len(vals) == len(shown._color_fields) >= 3 and all(len(v) == 4 * nt for v in vals),
          '{} channels'.format(len(vals)))
    check('display {}: a displacement per vertex'.format(label), len(disp) == 12 * nv)
    ta = _array.array('i')
    ta.frombytes(tets)
    check('display {}: tetrahedra use real nodes'.format(label), 0 <= min(ta) and max(ta) < nv)
    flat = [i for i in range(nt) if meshing._det6(mesh.nodes, ta[4 * i], ta[4 * i + 1], ta[4 * i + 2], ta[4 * i + 3]) <= 0.0]
    check('display {}: every tetrahedron is right-handed'.format(label), not flat, '{} are not'.format(len(flat)))
    fa = _array.array('i')
    fa.frombytes(faces)
    ft = _array.array('i')
    ft.frombytes(face_tet)
    ok = True
    for k in range(min(len(ft), 2000)):
        tet_nodes = set(ta[4 * ft[k]:4 * ft[k] + 4])
        if sum(1 for v in fa[3 * k:3 * k + 3] if v in tet_nodes) < 2:
            ok = False
            break
    check('display {}: a boundary face shares nodes with its tetrahedron'.format(label), ok)
    print('   ' + text + '; channels: ' + ', '.join(c[0] for c in shown._color_fields))
sshown = run('display of a surface mesh', lambda: sq._display() if sq is not None else None)
check('display of a surface mesh has the body and a size', sshown is not None and sshown._fea_mesh is None and len(sshown._color_fields) == 1)
print(took())

# ---------------------------------------------------------------- 12. example 36
stage('12. example 36 (the channel round a post)')
import runpy

example = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'examples', '36_meshing_for_cfd.py')
ns = run('example 36', lambda: runpy.run_path(example))
if ns is not None and 'mesh' in ns:
    em = ns['mesh']
    check('example: a Mesh', type(em).__name__ == 'Mesh')
    check('example: prisms and tetrahedra', em.cell_count('prism6') > 0 and em.cell_count('tet4') > 0, '{} prisms {} tets'.format(em.cell_count('prism6'), em.cell_count('tet4')))
    check('example: zones inlet and outlet', 'inlet' in em.zone_names and 'outlet' in em.zone_names, str(em.zone_names))
    check('example: the walls are a few pieces, not splinters', 3 <= sum(1 for n in em.zone_names if n.startswith('wall')) <= 8, str(em.zone_names))
    eq = em.quality()
    check('example: no element inside out', eq.negative == 0, str(eq.negative))
    check('example: skewness below 0.95', eq['skewness'][2] < 0.95, '{:.3f}'.format(eq['skewness'][2]))
    check('example: at most 1 % of the cells above skewness 0.85', eq.skewed[0] <= 0.01 * eq.skewed[2], '{} of {}'.format(eq.skewed[0], eq.skewed[2]))
    air_volume = 60 * 30 * 10 - math.pi * 16 * 10
    check('example: volume within 3 %', abs(em.volume() - air_volume) / air_volume < 0.03, '{:.1f} (exact {:.1f})'.format(em.volume(), air_volume))
    shown = run('example display', em._display)
    check('example: drawable', shown is not None and shown._fea_mesh is not None)
print(took())

# ---------------------------------------------------------------- 13. what the right-click menu writes for a mesh
stage('13. the call the Engineering > Mesh entries write (the body and nothing else: everything else has its default)')
import json
from fieldes import menu_catalog


def menu_call(name, body='box_1'):
    return menu_catalog.call(json.dumps({'kind': 'operation', 'name': name, 'body': body, 'var': name + '_1', 'indent': 0, 'x': 0, 'y': 0, 'z': 0, 'scale': 10,
                                         'body_info': {'name': body, 'type': 'solid'}}))


text = run('mesh_volume from the menu', lambda: menu_call('mesh_volume'))
check('mesh_volume from the menu is written with the body alone', text == 'mesh_volume(box_1)', repr(text))
text = run('mesh_surface from the menu', lambda: menu_call('mesh_surface'))
check('mesh_surface from the menu is written with the body alone', text == 'mesh_surface(box_1)', repr(text))
text = run('static_analysis from the menu', lambda: menu_call('static_analysis'))
check('static_analysis from the menu still has the body and its inputs as placeholders (an analysis, unlike a mesh, is written with every argument)',
      text is not None and text.startswith('static_analysis(\n    box_1,\n    supports=...,\n    loads=...,'), repr(text))
text = run('fluid_analysis from the menu', lambda: menu_call('fluid_analysis'))
check('fluid_analysis from the menu has the fluid domain as a placeholder beside the body', text is not None and 'domain=...' in text and 'inlets=...' in text, repr(text))
print(took())

print('=' * 70)
print('{} check(s) failed'.format(len(failures)) if failures else 'ALL CHECKS PASSED', failures, '   total {:.1f} s'.format(time.time() - t_all))

sys.exit(1 if failures else 0)
