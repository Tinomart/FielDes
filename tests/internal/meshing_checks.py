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
body = box((20, 10, 6), position=(10, 5, 3))
m = run('mesh_tet4 box', lambda: mesh_tet4(body, 2.0))
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
m10 = run('tet10', lambda: mesh_tet10(body, 2.0))
if m10 is not None:
    print(m10)
    check('tet10 volume', abs(m10.volume() - 1200.0) < 0.5, '{:.4f}'.format(m10.volume()))
    check('tet10 nodes = corners + edges', m10.node_count > m.node_count)
mh20 = None
mh = run('hex', lambda: mesh_hex_quanta(body, 2.0))
if mh is not None:
    print(mh)
    check('hex volume', abs(mh.volume() - 1200.0) < 0.5, '{:.4f}'.format(mh.volume()))
    qh = mh.quality()
    print(qh.report())
    check('no inverted hexahedra', qh.negative == 0, str(qh.negative))
    over, one = conforming(mh)
    check('hex conforming', over == 0, str(over))
    check('hex boundary faces are quads', mh.face_count('quad4') == mh.face_count() and mh.face_count() > 0, str(mh.faces.keys()))
    # (H193, the user, 2026-11-13: "hex is absolutely horrible": the hexahedra are on a grid that is fitted to the body, no longer the tetrahedra cut into four.  A box of 20 x 10 x 6 at 2 mm is
    # 10 x 5 x 3 cubes -- aspect ratio 1, skewness 0, scaled Jacobian 1)
    check('hex: a box is a block of cubes (10 x 5 x 3)', mh.cell_count('hex8') == 150, str(mh.cell_count('hex8')))
    check('hex: scaled Jacobian 1 on a box', qh['scaled_jacobian'][0] > 0.999, '{:.4f}'.format(qh['scaled_jacobian'][0]))
    check('hex: aspect ratio 1 and skewness 0 on a box', qh['aspect_ratio'][2] < 1.001 and qh['skewness'][2] < 0.001, '{:.3f} {:.3f}'.format(qh['aspect_ratio'][2], qh['skewness'][2]))
    mh20 = run('hex20', lambda: mesh_hex20_quanta(body, 2.0))
    if mh20 is not None:
        check('hex20: the same cells, with the middle of every edge', mh20.cell_count('hex20') == 150 and mh20.node_count > mh.node_count, '{} {}'.format(mh20.cell_count('hex20'), mh20.node_count))
        check('hex20 volume', abs(mh20.volume() - 1200.0) < 0.5, '{:.4f}'.format(mh20.volume()))
        over, one = conforming(mh20)
        check('hex20 conforming', over == 0 and one == mh20.face_count(), '{} {}'.format(over, one))
    # H203 (the user, 2026-11-14: "This SHOULD NOT BE IN OUR PROGRAM!!" of the old hexahedra, whose skin was pushed onto the surface and patched): mesh_hex_quanta is Salome's Body Fitting with Quanta -- the cubes of a Cartesian
    # grid, a cube the surface cuts a whole cube when `quanta` (0.5) of it is inside -- so EVERY cell is a perfect cube, whatever the body, and the boundary is a staircase
    # a part with a hole: every cell a perfect cube, the volume within 5 % of the exact one (the staircase, at 2 mm)
    plate = difference(box((40, 20, 6), position=(0, 0, 0)), cylinder(4, 10, position=(0, 0, 0)))
    mp = run('hex plate with hole', lambda: mesh_hex_quanta(plate, 2.0))
    if mp is not None:
        exact = 40 * 20 * 6 - math.pi * 16 * 6
        check('hex plate with hole: volume within 5 %', abs(mp.volume() - exact) / exact < 0.05, '{:.1f} (exact {:.1f})'.format(mp.volume(), exact))
        qp = mp.quality()
        check('hex plate with hole: every cell a perfect cube (scaled Jacobian 1, aspect ratio 1)', qp['scaled_jacobian'][0] > 0.999 and qp['aspect_ratio'][2] < 1.001, '{:.4f} {:.3f}'.format(qp['scaled_jacobian'][0], qp['aspect_ratio'][2]))
        check('hex plate with hole: only hexahedra', mp.cell_count() == mp.cell_count('hex8'), str(mp))
    # round bodies at 1 mm: volume within 3 % (the staircase of whole cubes at quanta 0.5: about +1.5 to +2 %), no cell that is not a cube
    for label, shape, exact_volume in (('cylinder', cylinder(5, 10, position=(0, 0, 0)), math.pi * 25 * 10), ('sphere', sphere(8), 4.0 / 3.0 * math.pi * 512)):
        mr = run('hex ' + label, lambda: mesh_hex_quanta(shape, 1.0))
        if mr is not None:
            qr = mr.quality()
            check('hex {}: every cell a perfect cube'.format(label), qr['scaled_jacobian'][0] > 0.999 and qr['aspect_ratio'][2] < 1.001, '{:.4f} {:.3f}'.format(qr['scaled_jacobian'][0], qr['aspect_ratio'][2]))
            check('hex {}: volume within 3 %'.format(label), abs(mr.volume() - exact_volume) / exact_volume < 0.03, '{:.1f} (exact {:.1f})'.format(mr.volume(), exact_volume))
    # Quanta: the part of a cube that must be inside for the cube to be made -- Salome's 0.01 swells the sphere, 1 leaves out every cut cube, and a value outside (0, 1] is refused
    swell = run('hex quanta 0.01', lambda: mesh_hex_quanta(sphere(8), 1.0, quanta=0.01))
    shrink = run('hex quanta 1', lambda: mesh_hex_quanta(sphere(8), 1.0, quanta=1.0))
    half = run('hex quanta 0.5', lambda: mesh_hex_quanta(sphere(8), 1.0))
    if swell is not None and shrink is not None and half is not None:
        check('hex quanta: 0.01 has more volume than 0.5, which has more than 1 ({:.0f} > {:.0f} > {:.0f})'.format(swell.volume(), half.volume(), shrink.volume()), swell.volume() > half.volume() > shrink.volume())
    refused_q = []
    for bad in (0.0, 1.5):
        try:
            mesh_hex_quanta(sphere(8), 1.0, quanta=bad)
            refused_q.append(bad)
        except ValueError:
            pass
    check('hex quanta: a value outside (0, 1] is refused', not refused_q, str(refused_q))
print(took())

# ---------------------------------------------------------------- 3. a sphere
stage('3. sphere r 10, 1.5 mm, curved boundary')
sph = sphere(10)
ms = run('sphere', lambda: mesh_tet4(sph, 1.5))
if ms is not None:
    print(ms)
    exact = 4.0 / 3.0 * math.pi * 1000.0
    check('sphere volume within 2 %', abs(ms.volume() - exact) / exact < 0.02, '{:.1f} (exact {:.1f})'.format(ms.volume(), exact))
    area_exact = 4.0 * math.pi * 100.0
    check('sphere area within 3 %', abs(ms.surface_area() - area_exact) / area_exact < 0.03, '{:.1f} (exact {:.1f})'.format(ms.surface_area(), area_exact))
    print(took())
    ms10 = run('sphere tet10', lambda: mesh_tet10(sph, 1.5))
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
field_mesh = run('size function', lambda: mesh_tet4(sph, lambda pts: [0.8 + 0.08 * (p[0] + 10.0) for p in pts]))
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
gm = run('graded', lambda: mesh_tet4(body, graded_size(body, wall_size=1.0, growth_rate=1.5, max_size=3.0)))
if gm is not None:
    print(gm)
    # (the kernel rounds an edge that runs between lattice points within one element: 2.4 mm elements on a box of 20 x 10 x 6 lose about 1.4 %)
    check('graded volume within 3 %', abs(gm.volume() - 1200.0) < 36.0, '{:.2f}'.format(gm.volume()))
    check('graded: no inverted', gm.quality().negative == 0)
    print(took())

# ---------------------------------------------------------------- 6. zones
stage('6. zones on the box')
zm = run('zones', lambda: mesh_tet4(body, 2.0, zones={'inlet': box((1.01, 12, 8), position=(-0.495, 5, 3)), 'outlet': box((1.01, 12, 8), position=(20.495, 5, 3))}))
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
zs = run('feature angle', lambda: mesh_tet4(body, 2.0, feature_angle=30))
if zs is not None:
    check('feature angle splits the box into six walls', len(zs.zone_names) == 6, str(zs.zone_names))
print(took())

# ---------------------------------------------------------------- 7. boundary layers
stage('7. boundary layers on a sphere in a box of air')
air = difference(box(40), sphere(5))
layers = inflation_layers(sphere(5), first_height=0.2, growth_rate=1.2, layers=4)
bm = run('boundary layers', lambda: mesh_tet4(air, 3.0, boundary_layers=layers, zones={'farfield': difference(box(60), sphere(15))}))
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
stage('8. mesh_surface_tri of the sphere, quads and triangles')
sq = run('surface quad', lambda: mesh_surface_quad(sph, 1.5))
if sq is not None:
    print(sq)
    check('quad surface: all quads', sq.face_count('quad4') == sq.face_count() > 0)
    check('quad surface area', abs(sq.surface_area() - area_exact) / area_exact < 0.04, '{:.1f}'.format(sq.surface_area()))
st = run('surface tri', lambda: mesh_surface_tri(sph, 1.5))
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


if mh20 is not None:
    # (H193: the 20-node hexahedron is written by the formats that have it; the corners alone go to the ones that do not -- Fluent and OpenFOAM)
    base20 = os.path.join(OUT, 'hex20')
    n20 = mh20.cell_count()
    p = run('gmsh hex20', lambda: mh20.export(base20 + '.msh'))
    if p:
        nodes, count, lines, types = read_gmsh(p)
        check('gmsh hex20: element type 17 for every cell', types.get(17) == n20, str(types))
    p = run('vtk hex20', lambda: mh20.export(base20 + '.vtu'))
    if p:
        piece = ET.parse(p).getroot().find('.//Piece')
        check('vtk hex20: cells', int(piece.get('NumberOfCells')) == n20)
        check('vtk hex20: points', int(piece.get('NumberOfPoints')) == mh20.node_count)
    p = run('abaqus hex20', lambda: mh20.export(base20 + '.inp'))
    if p:
        text = open(p).read()
        check('abaqus hex20: C3D20 element block', '*ELEMENT, TYPE=C3D20' in text, 'no C3D20')
        block = text.split('*ELEMENT, TYPE=C3D20')[1].split('\n*')[0].split('\n')[1:]
        rows = [ln for ln in block if ln]
        # (the first line of an element ends with a comma: that is how Abaqus knows the next line continues it)
        firsts = [ln for ln in rows if ln.endswith(',') and len(ln.rstrip(',').split(',')) == 16]
        check('abaqus hex20: two lines for each element (the first with its number and 15 nodes, ending with a comma)', len(firsts) == n20 and len(rows) == 2 * n20, '{} first lines, {} lines, {} cells'.format(len(firsts), len(rows), n20))
    p = run('nastran hex20', lambda: mh20.export(base20 + '.bdf'))
    if p:
        text = open(p).read()
        check('nastran hex20: CHEXA with 20 grids (three lines each)', len(re.findall(r'^CHEXA,', text, re.M)) == n20 and len(re.findall(r'^\+,', text, re.M)) == 2 * n20)
    p = run('fluent hex20', lambda: mh20.export(base20 + '.fluent.msh', format='fluent'))
    check('fluent hex20: written (its corners)', bool(p))

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
dm = run('default size', lambda: mesh_tet4(box((60, 40, 6), position=(30, 20, 3))))
if dm is not None:
    print(dm)
    check('default size: about 40 along the longest side', 1.2 < dm.element_size < 1.8, '{:.3f}'.format(dm.element_size))
# (a fat body is held by the budget: a 10 mm cube is about 0.49 mm, 107,000 tetrahedra measured -- a mesh the viewer draws, with its quality numbers -- and not the 0.29 mm, half a million, that it was)
from fieldes.stdlib.element_size import default_element_size
cube_size = run('default size of a cube', lambda: default_element_size(box(10, position=(5, 5, 5)), (0, 0, 0), (10, 10, 10), 40.0, meshing.DEFAULT_ELEMENTS))
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
    check('display {}: an element mesh of 9 parts (the 7 of the tetrahedra, the edges the viewer does not draw, the cell of each tetrahedron)'.format(label), fm is not None and len(fm) == 9)
    if fm is None or len(fm) != 9:
        continue
    verts, tets, faces, face_tet, text, vals, disp, hidden_bytes, cell_bytes = fm
    nt = len(tets) // 16
    # H203: the cells are drawn as the cells they are: a hexahedron has its twelve edges and the diagonals of its tetrahedra are not drawn
    hid = _array.array('i')
    hid.frombytes(hidden_bytes)
    cell_of = _array.array('i')
    cell_of.frombytes(cell_bytes)
    check('display {}: the cell of every tetrahedron is given and the edges not drawn are pairs of nodes'.format(label), len(cell_of) == nt and len(hid) % 2 == 0, '{} {} {}'.format(len(cell_of), nt, len(hid)))
    if mesh.cell_count('hex8') and not mesh.cell_count('tet4') and not mesh.cell_count('prism6'):
        # (a mesh of hexahedra only: 7 edges that are not the cell's own in each cell -- six diagonals of its faces and the one through it -- of which those on a shared face are one for two cells)
        hexes = mesh.cell_count('hex8')
        check('display {}: the diagonals of the hexahedra are not drawn ({} pairs for {} hexahedra)'.format(label, len(hid) // 2, hexes), len(hid) // 2 >= 3 * hexes)
        pairs_hid = set((hid[2 * i], hid[2 * i + 1]) for i in range(len(hid) // 2))
        own = set()
        conn_h = mesh.cells['hex8']
        for i in range(hexes):
            ids = conn_h[8 * i:8 * i + 8]
            for a, b in meshing.EDGES['hex8']:
                own.add((min(ids[a], ids[b]), max(ids[a], ids[b])))
        check('display {}: no edge of a hexahedron is among those not drawn'.format(label), not (pairs_hid & own))
    elif mesh.cell_count('tet4') and not mesh.cell_count('hex8') and not mesh.cell_count('prism6'):
        check('display {}: tetrahedra have every edge drawn'.format(label), len(hid) == 0, '{} pairs'.format(len(hid) // 2))
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
check('display of a surface mesh has its faces and a size', sshown is not None and sshown._fea_mesh is not None and len(sshown._color_fields) == 1 and sshown._color_fields[0][0] == 'size')
if sshown is not None and sshown._fea_mesh is not None:
    sva = _array.array('f')
    sva.frombytes(sshown._fea_mesh[0])
    sta = _array.array('i')
    sta.frombytes(sshown._fea_mesh[1])
    sfa = _array.array('i')
    sfa.frombytes(sshown._fea_mesh[2])
    sflat = [i for i in range(len(sta) // 4) if meshing._det6(sva, sta[4 * i], sta[4 * i + 1], sta[4 * i + 2], sta[4 * i + 3]) <= 0.0]
    check('display of a surface mesh: every tetrahedron is right-handed', not sflat, '{} are not'.format(len(sflat)))
    check('display of a surface mesh: one drawn triangle for each triangle of the mesh and two for each quadrilateral',
          len(sfa) // 3 == sq.face_count('tri3') + 2 * sq.face_count('quad4'), '{} drawn, {} tri3, {} quad4'.format(len(sfa) // 3, sq.face_count('tri3'), sq.face_count('quad4')))
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
stage('13. the call the Engineering > Mesh entries write (H201: every argument, like every other function -- the mesh is no exception)')
import ast
import inspect
import json
from fieldes import completion, menu_catalog


def menu_call(name, body='box_1'):
    return menu_catalog.call(json.dumps({'kind': 'operation', 'name': name, 'body': body, 'var': name + '_1', 'indent': 0, 'x': 0, 'y': 0, 'z': 0, 'scale': 10,
                                         'body_info': {'name': body, 'type': 'solid'}}))


def writes_every_argument(text, name):
    ''' The call `text` has the body first and every other parameter of the function named, as a keyword (the rule of the completion for every function: see completion_every_function_checks.py) '''
    node = ast.parse(text, mode='eval').body
    written = {k.arg for k in node.keywords}
    wanted = [p.name for p in inspect.signature(completion._library()[name]).parameters.values() if not p.name.startswith('_') and p.kind != p.VAR_KEYWORD]
    return all(w in written for w in wanted[len(node.args):]) and len(node.keywords) >= 5


for name_ in ('mesh_tet4', 'mesh_tet10', 'mesh_hex_quanta', 'mesh_hex20_quanta', 'mesh_cartesian', 'mesh_hex_extrusion', 'mesh_surface_tri', 'mesh_surface_quad'):
    text = run(name_ + ' from the menu', lambda: menu_call(name_))
    check(name_ + ' from the menu is written with every argument', text is not None and text.startswith(name_ + '(') and writes_every_argument(text, name_), repr(text))
text = run('static_analysis from the menu', lambda: menu_call('static_analysis'))
check('static_analysis from the menu has the body and its inputs as placeholders (and every other argument)',
      text is not None and text.startswith('static_analysis(\n    box_1,\n    supports=...,\n    loads=...,'), repr(text))
text = run('fluid_analysis from the menu', lambda: menu_call('fluid_analysis'))
check('fluid_analysis from the menu has the fluid domain as a placeholder beside the body', text is not None and 'domain=...' in text and 'inlets=...' in text, repr(text))
print(took())

# ---------------------------------------------------------------- 14. the size of the elements is even
stage('14. the size of the elements: even over the whole mesh, the element size fitted to the body (H202)')
# (the user, 2026-11-13, with the picture of an analysis of a cantilever beam: "The element size varies ENOURMOUSLY ... MAXIMUM CONSISTENCY over the entire mesh. If the mesh cannot fit the exact element size, then the
# element size are adjusted TO FIT AS BEST AS POSSIBLE": the lattice of the tetrahedra started at a corner of the region and its faces cut the cells at an arbitrary offset: the largest tetrahedron of the beam at
# element size 4 was 24 times the smallest, 13 % of them below a fifth of the median)
def volume_spread(points, tets):
    vols = []
    for i in range(len(tets) // 4):
        a, b, c, d = tets[4 * i:4 * i + 4]
        ux, uy, uz = [points[3 * b + k] - points[3 * a + k] for k in range(3)]
        vx, vy, vz = [points[3 * c + k] - points[3 * a + k] for k in range(3)]
        wx, wy, wz = [points[3 * d + k] - points[3 * a + k] for k in range(3)]
        vols.append(abs(ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) + uz * (vx * wy - vy * wx)) / 6.0)
    total = sum(vols)
    vols.sort()
    median = vols[len(vols) // 2]
    return vols[-1] / vols[0], vols[len(vols) // 20] / median, sum(1 for v in vols if v < 0.2 * median) / float(len(vols)), total


from array import array as _arr
beam = box((100, 20, 10), position=(50, 0, 5))
for size in (4.0, 3.7, 5.3):
    mb = run('beam tetrahedra {}'.format(size), lambda: mesh_tet4(beam, size))
    if mb is not None:
        sp = mb.quality().spread
        check('beam, element size {}: the largest tetrahedron is at most 3 times the smallest'.format(size), sp is not None and sp[0] <= 3.0, 'largest / smallest {:.2f}'.format(sp[0] if sp else -1))
        check('beam, element size {}: no tetrahedron below a fifth of the median'.format(size), sp is not None and sp[1] > 0.2, '5th percentile {:.2f} of the median'.format(sp[1] if sp else -1))
wall = select_surface(beam, seed=(0, 0, 5), angle=10)
tip = select_surface(beam, seed=(100, 0, 5), angle=10)
res = run('the analysis of the beam', lambda: static_analysis(beam, supports=[fixed(wall)], loads=[force(tip, (0, 0, -200))], material=steel, element_size=4, cache=False))
if res is not None:
    data = res._mesh_data()
    verts = _arr('f')
    verts.frombytes(data[0])
    tets = _arr('i')
    tets.frombytes(data[1])
    largest, low, share, filled = volume_spread(verts, tets)
    check('the analysis of the beam at element size 4: the largest tetrahedron is at most 3 times the smallest', largest <= 3.0, 'largest / smallest {:.2f} ({:,} tetrahedra)'.format(largest, len(tets) // 4))
    check('the analysis of the beam: no tetrahedron below a fifth of the median', share == 0.0, '{:.1f} % of them'.format(100 * share))
    # (his report, 2026-11-13, of the edges of this beam in the window: "edge defects ... on pretty much every edge": the lattice cut a notch of half an element out of an edge that fell between its
    # vertices, at the seams of the mirror planes -- the beam lost 0.67 % of its volume, and none of the four long edges had a vertex on the seam)
    check('the analysis of the beam fills the beam: its edges and corners are not cut (volume within 0.2 % of 20000)', abs(filled - 20000.0) / 20000.0 < 0.002, '{:.1f} ({:+.3f} %)'.format(filled, 100 * (filled - 20000.0) / 20000.0))
    bnd = sorted(set(_arr('i', data[2])))
    seam = [v for v in bnd if abs(verts[3 * v] - 50.0) < 1e-3 and abs(abs(verts[3 * v + 1]) - 10.0) < 1e-3 and (abs(verts[3 * v + 2]) < 1e-3 or abs(verts[3 * v + 2] - 10.0) < 1e-3)]
    check('the analysis of the beam: every long edge has a node where the mirror plane x = 50 crosses it (4)', len(seam) >= 4, str(len(seam)))
hb = run('beam hexahedra 3.7', lambda: mesh_hex_quanta(beam, 3.7))
if hb is not None:
    sp = hb.quality().spread
    check('beam hexahedra at 3.7: all cells the same size (the cells are fitted: 27 x 5 x 3 of 3.7 x 4 x 3.33)', sp is not None and sp[0] < 1.3, 'largest / smallest {:.2f}'.format(sp[0] if sp else -1))
print(took())

# ---------------------------------------------------------------- 15. the size follows the curvature
stage('15. curvature_size: the size asked where the surface is flat, smaller where it curves (H193, after Salome: "limit size by surface curvature")')
hole_plate = difference(box((40, 20, 6), position=(0, 0, 0)), cylinder(2.5, 12, position=(0, 0, 0)))
try:
    cs = curvature_size(hole_plate, element_size=3.0, segments=16)
    at = cs([(2.5, 0.0, 0.0), (2.5, 1.0, 0.0), (6.0, 0.0, 0.0), (12.0, 0.0, 0.0), (18.0, 8.0, 3.0)])
    check('curvature_size: far from the hole the size is the one asked', abs(at[3] - 3.0) < 1e-9 and abs(at[4] - 3.0) < 1e-9, str([round(v, 2) for v in at]))
    check('curvature_size: at the hole it is a third to a half of it (2 pi R / 16 = 0.98 for R = 2.5, the field and the walk bring it to 1.3 at the most)', at[0] < 1.4 and at[1] < 1.4, str([round(v, 2) for v in at]))
    check('curvature_size: between the hole and the far field it grows (hole < 6 mm away < far)', at[0] < at[2] < at[3], str([round(v, 2) for v in at]))
    flat = curvature_size(box((20, 10, 6), position=(0, 0, 0)), element_size=3.0)
    sizes = flat([(0.0, 0.0, 0.0), (9.0, 4.0, 2.0), (10.0, 5.0, 3.0), (10.0, 0.0, 0.0)])
    check('curvature_size: a box has no curve, edges are not curves: the size is the one asked everywhere', all(abs(v - 3.0) < 1e-9 for v in sizes), str(sizes))
except Exception as e:
    check('curvature_size runs', False, repr(e))
    cs = None
if cs is not None:
    m_c = run('plate with a hole, curvature_size', lambda: mesh_tet4(hole_plate, cs))
    m_u = run('plate with a hole, uniform 3.0', lambda: mesh_tet4(hole_plate, 3.0))
    if m_c is not None and m_u is not None:
        exact_c = 40 * 20 * 6 - math.pi * 2.5 ** 2 * 6
        err_c, err_u = abs(m_c.volume() - exact_c) / exact_c, abs(m_u.volume() - exact_c) / exact_c
        check('curvature_size: the mesh has the hole better than the uniform one (volume error {:.3f} % against {:.3f} %)'.format(100 * err_c, 100 * err_u), err_c < err_u, '{:.4f} {:.4f}'.format(err_c, err_u))
        check('curvature_size: more elements round the hole (more tetrahedra than the uniform mesh)', m_c.cell_count('tet4') > 1.5 * m_u.cell_count('tet4'), '{} and {}'.format(m_c.cell_count('tet4'), m_u.cell_count('tet4')))
        over_c, one_c = conforming(m_c)
        check('curvature_size: conforming, no inverted tetrahedron', over_c == 0 and m_c.quality().negative == 0, '{} {}'.format(over_c, m_c.quality().negative))
        P_c = m_c.nodes
        wall_edges = []
        tri = m_c.faces['tri3']
        for i in range(len(tri) // 3):
            ids = [tri[3 * i + k] for k in range(3)]
            if all(abs(math.hypot(P_c[3 * v], P_c[3 * v + 1]) - 2.5) < 0.3 and abs(P_c[3 * v + 2]) < 3.0 - 1e-6 for v in ids):
                for k in range(3):
                    a, b = ids[k], ids[(k + 1) % 3]
                    wall_edges.append(math.dist(P_c[3 * a:3 * a + 3], P_c[3 * b:3 * b + 3]))
        mean_edge = sum(wall_edges) / max(1, len(wall_edges))
        check('curvature_size: the elements on the wall of the hole have about 2 pi R / 16 = 0.98 mm edges (mean {:.2f})'.format(mean_edge), 0.6 < mean_edge < 1.4, '{:.3f} over {} edges'.format(mean_edge, len(wall_edges)))
print(took())

stage("16. mesh_cartesian: a grid cut by the body, the cut cubes as polyhedra (H193, after Salome's Body Fitting)")
plate_h = difference(box((40, 20, 6), position=(20, 10, 3)), cylinder(2.5, 8, position=(20, 10, 3)))
exact_h = 4800.0 - math.pi * 2.5 ** 2 * 6
mc = run('cartesian plate with a hole', lambda: mesh_cartesian(plate_h, 1.0))
if mc is not None:
    check('cartesian: hexahedra inside, polyhedra at the hole', mc.cell_count('hex8') > 4000 and mc.cell_count('poly') > 0, repr(mc)[:140])
    check('cartesian: the volume is within 0.2 % of the exact one ({:.2f} against {:.2f})'.format(mc.volume(), exact_h), abs(mc.volume() - exact_h) / exact_h < 0.002, '')
    qc = mc.quality()
    check('cartesian: no cell inside out, the worst scaled Jacobian of the others above 0.7 (the old hexahedra of this plate: 0.077)', qc.negative == 0 and qc.metrics['scaled_jacobian'][0] > 0.7,
          '{} negative, {:.3f}'.format(qc.negative, qc.metrics['scaled_jacobian'][0]))
    check('cartesian: skewness under 0.7, orthogonal quality over 0.85', qc.metrics['skewness'][2] < 0.7 and qc.metrics['orthogonal_quality'][0] > 0.85,
          'skewness {:.3f}, orthogonal {:.3f}'.format(qc.metrics['skewness'][2], qc.metrics['orthogonal_quality'][0]))
    # the nodes of the boundary lie on the surface, and the boundary is closed (every edge of its faces is shared by two)
    Pc = mc.nodes
    boundary = set()
    edge_use = {}
    for kind in list(mc.faces):
        for ids in meshing._faces_of_kind(mc, kind):
            boundary.update(ids)
            for k in range(len(ids)):
                e = (min(ids[k], ids[(k + 1) % len(ids)]), max(ids[k], ids[(k + 1) % len(ids)]))
                edge_use[e] = edge_use.get(e, 0) + 1
    values = meshing._eval(plate_h, [(Pc[3 * v], Pc[3 * v + 1], Pc[3 * v + 2]) for v in sorted(boundary)])
    check('cartesian: every node of the boundary is within 0.03 mm of the surface', max(abs(v) for v in values) < 0.03, '{:.4f}'.format(max(abs(v) for v in values)))
    check('cartesian: the boundary is closed (every edge of its faces is the edge of two)', all(v == 2 for v in edge_use.values()), '{} edges, {} not twice'.format(len(edge_use), sum(1 for v in edge_use.values() if v != 2)))
    # the writers: the polyhedra are in VTK, Fluent and OpenFOAM; the others refuse them and say how to go on
    sph = run('cartesian sphere', lambda: mesh_cartesian(sphere(8), 1.5))
    if sph is not None:
        check('cartesian sphere: all the kinds of cut cell are there (hexahedra, prisms, polyhedra)', sph.cell_count('hex8') > 0 and sph.cell_count('prism6') > 0 and sph.cell_count('poly') > 0, repr(sph)[:140])
        vtu = os.path.join(OUT, 'cartesian.vtu')
        sph.export(vtu)
        text = open(vtu, encoding='ascii').read()
        check('cartesian: the VTK file has polyhedra (cell type 42 with their faces)', ' 42' in text and 'Name="faceoffsets"' in text, '')
        refused = []
        for fmt in ('gmsh', 'abaqus', 'nastran', 'unv'):
            try:
                sph.export(os.path.join(OUT, 'cartesian.' + fmt), format=fmt)
                refused.append(fmt + ' wrote it')
            except Exception as e:
                if 'polyhedra' not in str(e):
                    refused.append(fmt + ': ' + str(e)[:60])
        check('cartesian: the formats that have no polyhedron refuse it and say why', not refused, str(refused))
        foam = os.path.join(OUT, 'cartesian_polyMesh')
        sph.export(foam, format='openfoam')
        owner_f = [int(l) for l in open(os.path.join(foam, 'owner'), encoding='ascii').read().split('\n') if l.strip().isdigit()][1:]       # (the first number is the count)
        check('cartesian: the OpenFOAM mesh has as many cells as the mesh', max(owner_f) + 1 == sph.cell_count(), '{} against {}'.format(max(owner_f) + 1, sph.cell_count()))
    # a tilted cylinder: its edges do not lie on the grid, so the nodes on edges are what keeps its cells good
    tilt = cylinder(4.0, 14, position=(0, 0, 0), rotation=(30, 0, 0))
    mt = run('cartesian tilted cylinder', lambda: mesh_cartesian(tilt, 1.0, bounds=((-5.0, -9.0, -9.0), (5.0, 9.0, 9.0))))
    if mt is not None:
        exact_t = math.pi * 16 * 14
        qt = mt.quality()
        check('cartesian: a tilted cylinder (edges off the grid): volume within 3 %, no cell inside out, scaled Jacobian above 0.3', abs(mt.volume() - exact_t) / exact_t < 0.03 and qt.negative == 0
              and qt.metrics['scaled_jacobian'][0] > 0.3, '{:+.2f} %, {} negative, {:.3f}'.format(100 * (mt.volume() - exact_t) / exact_t, qt.negative, qt.metrics['scaled_jacobian'][0]))
    # a body is meshed the same wherever it stands (found by the element types walkthrough, 2026-11-14): the volume of a cut cell was taken from the origin of the coordinates, which is the same for a closed cell and not for
    # the flat piece of two quadrilaterals that a face lying on the grid leaves in a cube -- its volume was its distance from the origin: the plate 120 mm along y got 50 such cells (13,388 mm3 for 4,498) and, with mesh_hex_quanta,
    # 50 cubes too many (+4.1 %)
    plate_far = difference(box((40, 20, 6), position=(50, 130, -67)), cylinder(2.5, 8, position=(50, 130, -67)))
    mcf = run('cartesian plate with a hole 140 mm from the origin', lambda: mesh_cartesian(plate_far, 1.0))
    if mcf is not None:
        flat_cells = sum(1 for faces in mcf.polyhedra() if len(faces) < 4)
        check('cartesian: the same plate 140 mm from the origin has the same cells and volume, and no flat cell of two faces', mcf.cell_count() == mc.cell_count() and abs(mcf.volume() - mc.volume()) < 1e-4 * mc.volume() and flat_cells == 0,
              '{} against {} cells, volume {:.2f} against {:.2f}, {} flat'.format(mcf.cell_count(), mc.cell_count(), mcf.volume(), mc.volume(), flat_cells))
    # the field is evaluated in single precision, so a point on a face of a body 70 mm from the origin reads +4e-6 and not 0: the crossing search of the grid found no sign change along the links in a face that lies on a plane of
    # the grid, the cube lost its face on the surface and was half a cube or was left out -- an L of two boxes (found by the fuzz of the hexahedra, 2026-11-14) had 153 cubes there and 171 at the origin
    La, Lb, Lc = 11.553526645110923, 19.66757147830088, 4.465826806177563

    def L_at(ox, oy, oz):
        return union(box((La, 4.0, Lc), position=(ox, oy - Lb / 2, oz)), box((4.0, Lb, Lc), position=(ox - La / 2 + 2.0, oy, oz)))
    mL0 = run('hex L at the origin', lambda: mesh_hex_quanta(L_at(0.0, 0.0, 0.0), 1.5))
    mL1 = run('hex L 70 mm from the origin', lambda: mesh_hex_quanta(L_at(20.90014651707793, 13.279504713665176, -70.10207226621753), 1.5))
    if mL0 is not None and mL1 is not None:
        check('hex: an L of two boxes 70 mm from the origin has the same cubes as at the origin (single precision of the field)', mL1.cell_count() == mL0.cell_count() and abs(mL1.volume() - mL0.volume()) < 1e-6 * mL0.volume(),
              '{} against {} cubes, volume {:.3f} against {:.3f}'.format(mL1.cell_count(), mL0.cell_count(), mL1.volume(), mL0.volume()))
    mhn = run('hex plate with a hole at the origin', lambda: mesh_hex_quanta(plate_h, 1.0))
    mhf = run('hex plate with a hole 140 mm from the origin', lambda: mesh_hex_quanta(plate_far, 1.0))
    if mhn is not None and mhf is not None:
        check('hex: the same plate 140 mm from the origin has the same cubes and volume ({} against {})'.format(mhf.cell_count(), mhn.cell_count()), mhf.cell_count() == mhn.cell_count() and abs(mhf.volume() - mhn.volume()) < 1e-6 * mhn.volume(),
              '{:.2f} against {:.2f}'.format(mhf.volume(), mhn.volume()))
    # a thin wall with the default size of the grids (H193, 2026-11-14): a tube with a wall of 0.6 mm and a length of 40 was meshed at 1 mm and held 22 % of its volume with hexahedra, 61 % with the cartesian cells
    tube_t = difference(cylinder(10, 40, position=(0, 0, 20)), cylinder(9.4, 50, position=(0, 0, 20)))
    exact_tube = math.pi * (100 - 9.4 ** 2) * 40
    mtc = run('cartesian thin tube, the default size', lambda: mesh_cartesian(tube_t))
    mth = run('hex thin tube, the default size', lambda: mesh_hex_quanta(tube_t))
    if mtc is not None:
        check('cartesian: a tube with a wall of 0.6 mm at the default size keeps its wall (volume within 3 %: {:+.1f} %)'.format(100 * (mtc.volume() / exact_tube - 1)), abs(mtc.volume() / exact_tube - 1) < 0.03, '{} cells'.format(mtc.cell_count()))
    if mth is not None:
        check('hex: the same tube keeps its wall too (a staircase: volume within 25 %: {:+.1f} %)'.format(100 * (mth.volume() / exact_tube - 1)), abs(mth.volume() / exact_tube - 1) < 0.25, '{} cells'.format(mth.cell_count()))
print(took())

# ---------------------------------------------------------------- 17. Extrusion 3D
stage("17. mesh_hex_extrusion: the end face meshed with quadrilaterals and swept (Salome's Extrusion 3D, H203)")
plate_e = difference(box((40, 20, 6), position=(20, 10, 3)), cylinder(2.5, 8, position=(20, 10, 3)))
exact_e = 4800.0 - math.pi * 2.5 ** 2 * 6
me = run('extrusion of a plate with a hole', lambda: mesh_hex_extrusion(plate_e, 1.0))
if me is not None:
    qe = me.quality()
    check('extrusion: hexahedra only', me.cell_count() == me.cell_count('hex8') > 1000, repr(me)[:120])
    check('extrusion: the surface is fitted, the volume within 0.5 % ({:+.2f} %)'.format(100 * (me.volume() - exact_e) / exact_e), abs(me.volume() - exact_e) / exact_e < 0.005, '')
    check('extrusion: no cell inside out, the worst scaled Jacobian above 0.6, the mean above 0.9 (the old hexahedra of this plate: 0.077)',
          qe.negative == 0 and qe.metrics['scaled_jacobian'][0] > 0.6 and qe.metrics['scaled_jacobian'][1] > 0.9, '{} negative, {:.3f}, {:.3f}'.format(qe.negative, qe.metrics['scaled_jacobian'][0], qe.metrics['scaled_jacobian'][1]))
    check('extrusion: no cell stretched beyond an aspect ratio of 4', qe.metrics['aspect_ratio'][2] < 4.0, '{:.2f}'.format(qe.metrics['aspect_ratio'][2]))
    # the nodes of the walls of the hole are on it (a grid of cubes cannot do that)
    # (the walls of the hole: the boundary quadrilaterals that stand on two places of the plane only, one above the other, and are within 3 mm of the axis of the hole)
    Pe = me.nodes
    wall_nodes = set()
    for ids in meshing._faces_of_kind(me, 'quad4'):
        places = set((round(Pe[3 * n], 6), round(Pe[3 * n + 1], 6)) for n in ids)
        if len(places) == 2 and all(math.hypot(x - 20.0, y - 10.0) < 3.0 for x, y in places):
            wall_nodes.update(ids)
    off = max(abs(math.hypot(Pe[3 * n] - 20.0, Pe[3 * n + 1] - 10.0) - 2.5) for n in wall_nodes) if wall_nodes else 9.9
    check('extrusion: the nodes on the walls of the hole lie on it ({:,} of them, at most {:.4f} mm off a circle of 2.5)'.format(len(wall_nodes), off), len(wall_nodes) > 20 and off < 0.02, '')
me_tube = run('extrusion of a tube along x', lambda: mesh_hex_extrusion(difference(cylinder(6, 20, position=(0, 0, 0), rotation=(0, 90, 0)), cylinder(3, 30, position=(0, 0, 0), rotation=(0, 90, 0))), 0.8, axis='x'))
if me_tube is not None:
    tube_exact = math.pi * 27.0 * 20.0
    qt_ = me_tube.quality()
    check('extrusion: a tube along x, volume within 2 %, no cell inside out, scaled Jacobian above 0.4 (0.50 to 0.73 measured at 0.8 and 1.0)', abs(me_tube.volume() - tube_exact) / tube_exact < 0.02 and qt_.negative == 0 and qt_.metrics['scaled_jacobian'][0] > 0.4,
          '{:+.2f} %, {} negative, {:.3f}'.format(100 * (me_tube.volume() - tube_exact) / tube_exact, qt_.negative, qt_.metrics['scaled_jacobian'][0]))
refused_e = []
for label, shape_e in (('a sphere', sphere(8)), ('a box with a spherical pocket in its top', difference(box((20, 20, 10), position=(10, 10, 5)), sphere(6, position=(10, 10, 10))))):
    try:
        mesh_hex_extrusion(shape_e, 1.0)
        refused_e.append(label)
    except MeshError:
        pass
check('extrusion: a body that is not a prism is refused', not refused_e, str(refused_e))
mh_layers = run('extrusion with layers', lambda: mesh_hex_extrusion(box((10, 10, 6), position=(5, 5, 3)), 2.0, layers=[1, 2, 3]))
if mh_layers is not None:
    zs = sorted(set(round(mh_layers.nodes[3 * i + 2], 6) for i in range(mh_layers.node_count)))
    check('extrusion: the layers asked for (1, 2, 3 of 6 mm: faces at 0, 1, 3, 6)', len(zs) == 4 and abs(zs[1] - 1.0) < 1e-6 and abs(zs[2] - 3.0) < 1e-6, str(zs))
print(took())

# ---------------------------------------------------------------- 18. the 10-node tetrahedra at a curved boundary (H193: "EVERY type good")
stage('18. tet10 at a curved boundary: no element turned inside out by the nodes put on the surface')


def _tet10_jacobians(mesh10):
    ''' The smallest and the largest determinant of the Jacobian of every 10-node tetrahedron over its four corners and its centre '''
    def shape(xi, eta, zeta):
        l1 = 1.0 - xi - eta - zeta
        l2, l3, l4 = xi, eta, zeta
        return [l1 * (2 * l1 - 1), l2 * (2 * l2 - 1), l3 * (2 * l3 - 1), l4 * (2 * l4 - 1), 4 * l1 * l2, 4 * l2 * l3, 4 * l3 * l1, 4 * l1 * l4, 4 * l2 * l4, 4 * l3 * l4]

    def det(X, xi, eta, zeta):
        h = 1e-6
        cols = []
        for d in range(3):
            a = [xi, eta, zeta]
            b = [xi, eta, zeta]
            a[d] += h
            b[d] -= h
            sa, sb = shape(*a), shape(*b)
            cols.append([sum((sa[i] - sb[i]) * X[i][c] for i in range(10)) / (2 * h) for c in range(3)])
        m = cols
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])

    P = mesh10.nodes
    conn = mesh10.cells['tet10']
    out = []
    for i in range(len(conn) // 10):
        X = [(P[3 * v], P[3 * v + 1], P[3 * v + 2]) for v in conn[10 * i:10 * i + 10]]
        dets = [det(X, *p) for p in ((0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1), (0.25, 0.25, 0.25))]
        out.append((min(dets), max(dets)))
    return out


for label, shape10, size10 in (('sphere r8 at 3 mm', sphere(8), 3.0), ('cylinder r5 h12 at 2.5 mm', cylinder(5, 12), 2.5)):
    m10c = run('tet10 ' + label, lambda: mesh_tet10(shape10, size10))
    if m10c is not None:
        jac = _tet10_jacobians(m10c)
        inverted = sum(1 for lo_, hi_ in jac if lo_ <= 0.0)
        worst_ratio = min(lo_ / hi_ for lo_, hi_ in jac if hi_ > 0.0)
        check('tet10 {}: no element has a Jacobian of zero or less at a corner or its centre; the smallest over the largest above 0.5 ({:.3f})'.format(label, worst_ratio), inverted == 0 and worst_ratio > 0.5,
              '{} of {:,} inside out, {:.3f}'.format(inverted, len(jac), worst_ratio))
print(took())

# ---------------------------------------------------------------- 19. the grid turned (Salome's axes of the Body Fitting)
stage("19. axes: the grid of cubes turned with a body that is turned")
turned_box = rotate_z(box((20, 10, 6), position=(0, 0, 3)), math.radians(30), center=(0, 0, 0))
m_plain = run('turned box, no axes', lambda: mesh_hex_quanta(turned_box, 1.0))
m_axes = run('turned box, axes', lambda: mesh_hex_quanta(turned_box, 1.0, axes=(0, 0, 30)))
if m_plain is not None and m_axes is not None:
    check('axes: the turned box is a block of 20 x 10 x 6 cubes, its volume exact ({:,} cells, {:.1f})'.format(m_axes.cell_count('hex8'), m_axes.volume()), m_axes.cell_count('hex8') == 1200 and abs(m_axes.volume() - 1200.0) < 0.01, '')
    check('axes: without them the same box is a staircase (a different number of cubes, its volume off by {:+.1f} %)'.format(100 * (m_plain.volume() - 1200.0) / 1200.0), m_plain.cell_count('hex8') != 1200 and abs(m_plain.volume() - 1200.0) > 5.0, '')
    xs = sorted(set(round(m_axes.nodes[3 * i] * math.cos(math.radians(30)) + m_axes.nodes[3 * i + 1] * math.sin(math.radians(30)), 4) for i in range(m_axes.node_count)))
    check('axes: the nodes lie on 21 planes along the turned x axis, a mm apart', len(xs) == 21 and abs((xs[-1] - xs[0]) - 20.0) < 1e-6, '{} planes'.format(len(xs)))
placed_box = box((20, 10, 6), position=(0, 0, 3), rotation=(0, 0, 30))
m_placed = run('box turned by rotation=', lambda: mesh_hex_quanta(placed_box, 1.0))
if m_placed is not None:
    check('rotation=: the box of a part turned by its call is not the circumscribing cube (a plate 20 x 10 x 6 turned 30 degrees: the hexahedra keep its volume within 2 %, {:+.1f} %)'.format(100 * (m_placed.volume() - 1200.0) / 1200.0),
          abs(m_placed.volume() - 1200.0) < 24.0, '')
turned_plate = rotate_z(difference(box((30, 14, 6), position=(0, 0, 3)), cylinder(2.0, 10, position=(0, 0, 3))), math.radians(25), center=(0, 0, 0))
turned_end = rotate_z(box((2, 20, 12), position=(14.5, 0, 3)), math.radians(25), center=(0, 0, 0))
m_cz = run('turned plate with a zone', lambda: mesh_cartesian(turned_plate, 1.0, axes=(0, 0, 25), zones={'end': turned_end}))
if m_cz is not None:
    check('axes: a zone is turned with the body: the end face of the turned plate has faces in its zone', 'end' in m_cz.zone_names and sum(1 for z in m_cz.face_zone.get('quad4', []) if z == m_cz.zone_names.index('end')) > 20,
          str(m_cz.zone_names))
try:
    mesh_tet4(turned_box, 2.0, axes=(0, 0, 30))
    refused_axes = False
except TypeError:
    refused_axes = True
check('axes: tetrahedra have no grid to turn: mesh_tet4 has no axes argument', refused_axes, '')
print(took())

# ---------------------------------------------------------------- 21. Netgen's optimization of the tetrahedra, in the kernel
stage("21. the mender of the tetrahedra in the kernel after Netgen's open source (H193): the split of an edge, the smoothing of a node, the mended mesh against the raw lattice, the volume note")
S3 = math.sqrt(3.0)


def netgen_badness(P_, t_):
    ''' Netgen's badness of a tetrahedron, (0.0080187537 (the sum of the squared edges)^1.5 / V)^2: 1 for a regular one '''
    q_ = [(P_[3 * v], P_[3 * v + 1], P_[3 * v + 2]) for v in t_]
    sub_ = lambda u, v: (u[0] - v[0], u[1] - v[1], u[2] - v[2])
    u_, v_, w_ = sub_(q_[1], q_[0]), sub_(q_[2], q_[0]), sub_(q_[3], q_[0])
    d6 = u_[0] * (v_[1] * w_[2] - v_[2] * w_[1]) - u_[1] * (v_[0] * w_[2] - v_[2] * w_[0]) + u_[2] * (v_[0] * w_[1] - v_[1] * w_[0])
    s_ = sum(sum(c * c for c in sub_(q_[i], q_[j])) for i in range(4) for j in range(i + 1, 4))
    return (0.0080187537 * s_ * math.sqrt(s_) * 6.0 / d6) ** 2 if d6 > 0 else 1e24


def volume6(P_, ts):
    out_ = 0.0
    for t_ in ts:
        q_ = [(P_[3 * v], P_[3 * v + 1], P_[3 * v + 2]) for v in t_]
        u_, v_, w_ = [tuple(q_[k][c] - q_[0][c] for c in range(3)) for k in (1, 2, 3)]
        out_ += u_[0] * (v_[1] * w_[2] - v_[2] * w_[1]) - u_[1] * (v_[0] * w_[2] - v_[2] * w_[0]) + u_[2] * (v_[0] * w_[1] - v_[1] * w_[0])
    return out_ / 6.0


def surface_of(ts, P_=None):
    ''' The faces that only one tetrahedron has: by node numbers, or (with P_) by the places of their nodes, which survive the mender numbering the nodes again '''
    count_ = {}
    for t_ in ts:
        for f_ in ((t_[0], t_[1], t_[2]), (t_[0], t_[1], t_[3]), (t_[0], t_[2], t_[3]), (t_[1], t_[2], t_[3])):
            count_[tuple(sorted(f_))] = count_.get(tuple(sorted(f_)), 0) + 1
    faces_ = sorted(k for k, n in count_.items() if n == 1)
    if P_ is None:
        return faces_
    return sorted(tuple(sorted(tuple(round(P_[3 * v + c], 6) for c in range(3)) for v in f_)) for f_ in faces_)


# nine spears round a long edge inside (a ring of more than eight tetrahedra cannot be swapped away: the edge 0-1 is split, in the middle, the node moved to where the badness is least)
axis_mesh = _array.array('d', [0.0, 0.0, -3.0, 0.0, 0.0, 3.0] + [c_ for k_ in range(9) for c_ in (math.cos(2 * math.pi * k_ / 9), math.sin(2 * math.pi * k_ / 9), 0.0)])
ring_ = [(0, 1, 2 + k_, 2 + (k_ + 1) % 9) for k_ in range(9)]
bad_before = sum(netgen_badness(axis_mesh, t_) for t_ in ring_)
volume_before = volume6(axis_mesh, ring_)
faces_before = surface_of(ring_, axis_mesh)
nodes_before = len(axis_mesh) // 3
notes_ = []
mended_ = run('split', lambda: meshing._mend(axis_mesh, ring_, None, 6, notes_, rounds=1, split_badness=10.0))
if mended_ is not None:
    check('split: the long edge inside nine spears is split in two ({} -> {} nodes, {} tetrahedra), the volume and the surface are the same, the badness {:.1f} -> {:.1f}'.format(
        nodes_before, len(axis_mesh) // 3, len(mended_), bad_before, sum(netgen_badness(axis_mesh, t_) for t_ in mended_)),
          len(axis_mesh) // 3 == nodes_before + 1 and abs(volume6(axis_mesh, mended_) - volume_before) < 1e-9 and surface_of(mended_, axis_mesh) == faces_before and sum(netgen_badness(axis_mesh, t_) for t_ in mended_) < bad_before, str(notes_))
# a node inside a regular tetrahedron, put off the middle: the smoothing moves it back to where the sum of the badness is least
corners_ = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.5, S3 / 2, 0.0), (0.5, S3 / 6, math.sqrt(2.0 / 3.0))]
middle_ = tuple(sum(c[k] for c in corners_) / 4.0 for k in range(3))
displaced_ = _array.array('d', [x for c in corners_ for x in c] + [middle_[0] + 0.18, middle_[1] - 0.10, middle_[2] + 0.06])
inner_ = []
for a_, b_, c_ in ((1, 2, 3), (0, 2, 3), (0, 1, 3), (0, 1, 2)):
    t_ = (4, a_, b_, c_)
    inner_.append(t_ if volume6(displaced_, [t_]) > 0 else (4, a_, c_, b_))
bad_inner = sum(netgen_badness(displaced_, t_) for t_ in inner_)
notes_ = []
smoothed_ = run('smoothing', lambda: meshing._mend(displaced_, inner_, None, 12, notes_, rounds=0))
if smoothed_ is not None:
    node_ = next(v for v in set(v for t_ in smoothed_ for v in t_) if all(v not in f_ for f_ in surface_of(smoothed_)))
    off_ = math.dist((displaced_[3 * node_], displaced_[3 * node_ + 1], displaced_[3 * node_ + 2]), middle_)
    check('smoothing: a node inside a regular tetrahedron, put off the middle, goes back to it ({:.4f} off, badness {:.1f} -> {:.1f})'.format(off_, bad_inner, sum(netgen_badness(displaced_, t_) for t_ in smoothed_)),
          len(smoothed_) == 4 and off_ < 0.02 and sum(netgen_badness(displaced_, t_) for t_ in smoothed_) < bad_inner, str(notes_))
# the lattice as it is cut and the mended mesh of a plate with a hole: the mender makes the worst tetrahedra better and turns none inside out
mend_plate = difference(box((40, 20, 6), position=(20, 10, 3)), cylinder(2.5, 8, position=(20, 10, 3)))
raw_ = run('plate raw', lambda: mesh_tet4(mend_plate, 2.6, optimize=False))
mended_plate = run('plate mended', lambda: mesh_tet4(mend_plate, 2.6))
if raw_ is not None and mended_plate is not None:
    qr_, qm_ = raw_.quality(), mended_plate.quality()
    check('mender: the plate with a hole at 2.6 mm: no tetrahedron inside out, the worst scaled Jacobian {:.3f} -> {:.3f}, the cells above skewness 0.85 {} -> {}'.format(
        qr_.metrics['scaled_jacobian'][0], qm_.metrics['scaled_jacobian'][0], qr_.skewed[0], qm_.skewed[0]),
          qm_.negative == 0 and qm_.metrics['scaled_jacobian'][0] >= qr_.metrics['scaled_jacobian'][0] and qm_.skewed[0] <= qr_.skewed[0] and abs(mended_plate.volume() - raw_.volume()) < 0.01 * raw_.volume(), '')
# (the imported mobile stand at 2.8 mm kept 18 % of its volume and said nothing: a thin tilted plate beside a cube is lost the same way)
lost_cube = box((20, 20, 20), position=(10, 10, 10))
lost_both = union(lost_cube, rotate_x(box((30, 30, 0.8), position=(50, 15, 10)), math.radians(45), center=(50, 15, 10)))
volume_notes = {}
for label_, body_ in (('a cube', lost_cube), ('a cube and a tilted plate 0.8 mm thick', lost_both)):
    m_lost = run(label_, lambda: mesh_tet4(body_, 4.0))
    volume_notes[label_] = [n_ for n_ in m_lost.quality().notes if 'holds' in n_] if m_lost is not None else None
check('volume note: a cube meshed whole has no note, a cube with a plate thinner than its elements says the mesh holds less of the body ({})'.format(volume_notes),
      volume_notes['a cube'] == [] and volume_notes['a cube and a tilted plate 0.8 mm thick'] is not None and len(volume_notes['a cube and a tilted plate 0.8 mm thick']) == 1, '')
print(took())

# ---------------------------------------------------------------- 22. thin parts: the default element size holds them
stage("22. a body with thin walls keeps them at its default element size (H193: the lattice loses a wall thinner than about 0.7 of its spacing; the kernel's probe says where, the size follows)")
from fieldes.stdlib.element_size import default_element_size as _default_size
thin_parts = [
    # (the label, the body, its box, its volume, whether it is meshed: the shell box of the examples of the report has 600,000 tetrahedra and is looked at in a window; here its size is looked at)
    ('a shell box 100 x 60 x 40, walls 1.2, open top', difference(box((100, 60, 40), position=(50, 30, 20)), box((97.6, 57.6, 40), position=(50, 30, 21.2))), ((0, 0, 0), (100, 60, 40)), 100 * 60 * 40 - 97.6 * 57.6 * 38.8, False),
    ('a plate 40 x 24 x 0.6 with a hole of radius 5', difference(box((40, 24, 0.6), position=(20, 12, 0.3)), cylinder(5, 5, position=(20, 12, 0.3))), ((0, 0, 0), (40, 24, 0.6)), 40 * 24 * 0.6 - math.pi * 25 * 0.6, True),
    ('a tube of radius 6, wall 0.5, length 20', difference(cylinder(6, 20, position=(0, 0, 10)), cylinder(5.5, 30, position=(0, 0, 10))), ((-6, -6, 0), (6, 6, 20)), math.pi * (36 - 30.25) * 20, True),
]
cube_ = box((10, 10, 10), position=(5, 5, 5))
same_ = (_default_size(cube_, (0, 0, 0), (10, 10, 10), 40.0, meshing.DEFAULT_ELEMENTS, thin=False), _default_size(cube_, (0, 0, 0), (10, 10, 10), 40.0, meshing.DEFAULT_ELEMENTS))
check('thin parts: a solid cube has the default element size it always had ({:.4f} mm with the thin walls looked for, {:.4f} without)'.format(same_[1], same_[0]), abs(same_[0] - same_[1]) < 1e-12, '')
for label_, part_, (lo_, hi_), exact_, meshed_ in thin_parts:
    plain_ = _default_size(part_, lo_, hi_, 40.0, meshing.DEFAULT_ELEMENTS, thin=False)
    notes_ = []
    held_ = run(label_ + ' size', lambda: _default_size(part_, lo_, hi_, 40.0, meshing.DEFAULT_ELEMENTS, notes=notes_))
    check('thin parts: {}: the default size is {:.3g} mm, not the {:.3g} mm of a body this size, and the note says why'.format(label_, held_ or 0.0, plain_), held_ is not None and held_ < 0.95 * plain_ and len(notes_) == 1 and 'walls' in notes_[0], str(notes_)[:240])
    m_thin = run(label_ + ' mesh', lambda: mesh_tet4(part_)) if meshed_ else None
    if m_thin is not None:
        check('thin parts: {}: the mesh at the default size holds {:.1f} % of the body (a mesh of the lattice before held 0 to 48 %)'.format(label_, 100.0 * m_thin.volume() / exact_), 0.85 < m_thin.volume() / exact_ < 1.15,
              '{:,} tetrahedra, {:.1f} s, {}'.format(m_thin.cell_count(), m_thin.seconds, [n_ for n_ in m_thin.notes if 'walls' in n_][:1]))
sheet_ = thin_parts[1][1]
given_ = run('a size that is given', lambda: mesh_tet4(sheet_, 1.0))
check('thin parts: an element size that is given is used as it is ({} mm; the volume note says what the mesh lost)'.format(given_.element_size if given_ is not None else None), given_ is not None and given_.element_size == 1.0 and any('holds' in n_ for n_ in given_.quality().notes), '')
print(took())

# ---------------------------------------------------------------- 24. layers of hexahedra on a wall (Salome's Viscous Layers on Extrusion 3D, H213)
stage("24. boundary_layers on mesh_hex_extrusion: hexahedra swept from the quadrilaterals of the core to the wall (H213)")
pin_ = cylinder(3, 10)
le = run('plate with layers', lambda: mesh_hex_extrusion(difference(box((16, 16, 4)), pin_), 1.0, boundary_layers=inflation_layers(pin_, first_height=0.2, growth_rate=1.3, layers=4)))
if le is not None:
    exact_p = 16.0 * 16.0 * 4.0 - math.pi * 9.0 * 4.0
    check('hex_extrusion layers: hexahedra only, none inside out, the volume of the plate within 1 % ({:+.2f} %), layers on the wall of the hole'.format(100 * (le.volume() / exact_p - 1)),
          set(le.cells) == {'hex8'} and le.quality().negative == 0 and abs(le.volume() / exact_p - 1) < 0.01 and any('hexahedra in 4 layers' in n_ for n_ in le.notes), repr(le)[:100])
no_layers = []
for function_ in (mesh_hex_quanta, mesh_hex20_quanta, mesh_cartesian, mesh_tet10):
    try:
        function_(box((10, 10, 10), position=(5, 5, 5)), 2.0, boundary_layers=inflation_layers(box((10, 10, 10)), first_height=0.2, growth_rate=1.3, layers=2))
        no_layers.append(function_.__name__)
    except TypeError:
        pass
check('layers: the grids of cubes and the 10-node tetrahedra have no boundary_layers argument (a staircase and a quadratic tetrahedron have no layer to follow)', not no_layers, str(no_layers))
print(took())

# ---------------------------------------------------------------- 27. the meshing section of the Engineering menu: Volume and Surface (the user, 2026-11-14)
stage("27. the meshing section of the Engineering menu has two sections, Volume and Surface, and a function of its own for every element type")
import json as json_
from fieldes import menu_catalog as menu_catalog_
cat_ = json_.loads(menu_catalog_.catalog())
entries_ = {o['name']: o for o in cat_['operations'] if o.get('domain') == 'mesh'}
volume_ = ['mesh_tet4', 'mesh_tet10', 'mesh_hex_quanta', 'mesh_hex20_quanta', 'mesh_cartesian', 'mesh_hex_extrusion']
surface_ = ['mesh_surface_tri', 'mesh_surface_quad']
check('menu: the meshing section holds exactly these functions, each an entry of its own ({})'.format(', '.join(sorted(entries_))), sorted(entries_) == sorted(volume_ + surface_), str(sorted(entries_)))
check('menu: the volume meshes are in the section Volume, the surface meshes in the section Surface',
      all(entries_[n_].get('submenu') == 'Volume' for n_ in volume_) and all(entries_[n_].get('submenu') == 'Surface' for n_ in surface_), str({n_: o_.get('submenu') for n_, o_ in entries_.items()}))
check('menu: every entry says in a hint what it makes', all(entries_[n_].get('hint') for n_ in entries_), str([n_ for n_ in entries_ if not entries_[n_].get('hint')]))
wrong_ = []
request_ = {'kind': 'operation', 'x': 0, 'y': 0, 'z': 0, 'scale': 1, 'body': 'part', 'other': '', 'var': 'mesh_1', 'indent': 0, 'body_info': {'type': 'solid', 'role': '', 'cls': 'Shape'}}
for n_ in sorted(entries_):
    text_ = menu_catalog_.call(dict(request_, name=n_))
    if not text_.startswith(n_ + '(') or 'element=' in text_ or 'element =' in text_:
        wrong_.append((n_, text_[:60]))
check('menu: the call of each entry is the call of its own function and has no element= to choose the element by', not wrong_, str(wrong_))
removed_ = [n_ for n_ in ('mesh_hex_radial', 'mesh_hex_blocks') if hasattr(meshing, n_)]
check('mesh_hex_radial and mesh_hex_blocks are gone, with no alias', not removed_ and 'hex_radial' not in meshing.ELEMENTS and 'hex_blocks' not in meshing.ELEMENTS, str(removed_))
print(took())

print('=' * 70)
print('{} check(s) failed'.format(len(failures)) if failures else 'ALL CHECKS PASSED', failures, '   total {:.1f} s'.format(time.time() - t_all))

sys.exit(1 if failures else 0)
