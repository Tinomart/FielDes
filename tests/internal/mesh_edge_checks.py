'''
Checks of the edges and corners of a volume mesh (the user, 2026-11-03: "I have no idea how you made those weird slops near the edges that has nothing to do with the geometry it came
from").  The lattice of the mesher does not lie on the edges of a body unless the element size fits it, and the nodes stood on the two faces beside an edge with a slope between them: a
box meshed at 4 mm lost 1.8 % of its volume and 7 % of its surface was slope.  the kernel mesher puts the nodes on the edges and corners now (snapFeatures in kernel/src/fea/tetmesh.cpp, for an analysis and for mesh_tet4 alike).  Library checks, no
window.  Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import math
import sys

from fieldes import *

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def boundary(mesh):
    ''' The triangles that only one tetrahedron has, as node triples '''
    T = mesh.cells['tet4']
    count = {}
    for k in range(len(T) // 4):
        t = (T[4 * k], T[4 * k + 1], T[4 * k + 2], T[4 * k + 3])
        for f in ((t[0], t[1], t[2]), (t[0], t[1], t[3]), (t[0], t[2], t[3]), (t[1], t[2], t[3])):
            key = tuple(sorted(f))
            count[key] = count.get(key, 0) + 1
    return [key for key, c in count.items() if c == 1]


def sloped_area(mesh):
    ''' The area of the boundary triangles of a box mesh whose normal is none of the three axes: slopes that the box does not have '''
    P = mesh.nodes
    total = 0.0
    for a, b, c in boundary(mesh):
        u = [P[3 * b + i] - P[3 * a + i] for i in range(3)]
        w = [P[3 * c + i] - P[3 * a + i] for i in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        if length > 0 and max(abs(n[0]), abs(n[1]), abs(n[2])) / length < 0.995:
            total += 0.5 * length
    return total


def nearest_corner_gap(mesh, half):
    ''' How far the nearest node is from the worst of the eight corners of a box '''
    P, n = mesh.nodes, mesh.node_count
    worst = 0.0
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                corner = (sx * half[0], sy * half[1], sz * half[2])
                worst = max(worst, min(math.dist(corner, (P[3 * i], P[3 * i + 1], P[3 * i + 2])) for i in range(n)))
    return worst


def worst_tetrahedron(mesh):
    from fieldes.stdlib import meshing
    P, T = mesh.nodes, mesh.cells['tet4']
    return min(meshing._tet_ratio(P, (T[4 * k], T[4 * k + 1], T[4 * k + 2], T[4 * k + 3])) for k in range(len(T) // 4))


def snapped(mesh):
    return any('edges and corners' in note for note in mesh.notes)


def rim_chords(mesh, radius, z):
    ''' The distances between neighbouring nodes along the circle of `radius` about the z axis in the plane z, the nodes of the boundary within 2 % of the radius of it and 0.01 mm of the plane, in the order of their angle '''
    P = mesh.nodes
    ids = set()
    for tri in boundary(mesh):
        ids.update(tri)
    ring = []
    for i in ids:
        x, y, zz = P[3 * i], P[3 * i + 1], P[3 * i + 2]
        if abs(zz - z) < 0.01 and abs(math.hypot(x, y) - radius) < 0.02 * radius:
            ring.append((math.atan2(y, x), (x, y)))
    ring.sort()
    return [math.dist(ring[k][1], ring[(k + 1) % len(ring)][1]) for k in range(len(ring))] if len(ring) >= 3 else []


def turned_axes(rz, rx):
    ''' The three face normals of a box that is turned by rz degrees about z and then by rx degrees about x '''
    cz, sz, cx, sx = math.cos(math.radians(rz)), math.sin(math.radians(rz)), math.cos(math.radians(rx)), math.sin(math.radians(rx))

    def turn(v):
        x, y, z = v
        x, y = cz * x - sz * y, sz * x + cz * y
        y, z = cx * y - sx * z, sx * y + cx * z
        return (x, y, z)
    return [turn((1.0, 0.0, 0.0)), turn((0.0, 1.0, 0.0)), turn((0.0, 0.0, 1.0))]


def leaning_share(mesh, axes, limit=12.0):
    ''' The share of the boundary triangles of a mesh of a box whose normal is more than `limit` degrees from every one of the box's three face directions '''
    P = mesh.nodes
    cs = math.cos(math.radians(limit))
    lean = total = 0
    for a, b, c in boundary(mesh):
        u = [P[3 * b + i] - P[3 * a + i] for i in range(3)]
        w = [P[3 * c + i] - P[3 * a + i] for i in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        if length <= 0:
            continue
        total += 1
        if max(abs(n[0] * ax[0] + n[1] * ax[1] + n[2] * ax[2]) for ax in axes) / length < cs:
            lean += 1
    return lean / total if total else 1.0


def main():
    size = (30, 20, 10)
    exact = 30.0 * 20.0 * 10.0
    surface = 2.0 * (30 * 20 + 20 * 10 + 30 * 10)
    box_body = box(size, position=(0, 0, 0))

    # --- an element size the lattice does not fit: the edges and corners were cut
    for h, volume_limit, slope_limit in ((4.0, 0.002, 1.0), (3.0, 0.01, 0.03 * surface)):
        mesh = mesh_tet4(box_body, h)
        error = abs(mesh.volume() - exact) / exact
        check('a box meshed at %.1f mm has its volume (%.3f %% off)' % (h, 100 * error), error < volume_limit, error)
        slope = sloped_area(mesh)
        check('... and its surface has no slope the box does not have (%.1f of %.0f mm2)' % (slope, surface), slope < slope_limit, slope)
        # (the element size is fitted to the body now -- H202 -- so that the lattice lies on the faces and edges of the box at every size and there is nothing cut across to put back: the mesher
        # says so when it had to find edges, and when it did not the surface has no slope at all.  The slope check above is the requirement; this is the way the mesher tells)
        check('... the edges were found by the mesher and said so, or the lattice lay on them already and the surface has no slope', snapped(mesh) or slope < 1e-6, mesh.notes)
        check('... no tetrahedron is a sliver (worst %.2f)' % worst_tetrahedron(mesh), worst_tetrahedron(mesh) >= 0.15)
    mesh = mesh_tet4(box_body, 4.0)
    gap = nearest_corner_gap(mesh, (15.0, 10.0, 5.0))
    check('a box meshed at 4 mm has a node on each of its corners (the worst is %.4f mm away)' % gap, gap < 0.01, gap)
    lo, hi = mesh.bounds
    check('... and no node outside it', max(abs(lo[0] + 15), abs(lo[1] + 10), abs(lo[2] + 5), abs(hi[0] - 15), abs(hi[1] - 10), abs(hi[2] - 5)) < 1e-3, (lo, hi))

    # --- an element size it fits: nothing to put back
    fit = mesh_tet4(box_body, 2.5)
    check('a box meshed at 2.5 mm, where the lattice lies on its edges, is left as it is', not snapped(fit) and sloped_area(fit) < 1e-6, fit.notes)

    # --- a plate with a hole: the rim of the hole and the edges of the plate
    plate = difference(box((80, 40, 4), position=(0, 0, 0)), cylinder(5, 12, position=(0, 0, 0)))
    plate_exact = 80 * 40 * 4 - math.pi * 25 * 4
    meshed = mesh_tet4(plate, 3.0)
    error = abs(meshed.volume() - plate_exact) / plate_exact
    check('a plate with a hole meshed at 3 mm has its volume (%.2f %% off; it was 2.1 %% before)' % (100 * error), error < 0.005, error)
    check('... no tetrahedron is a sliver (worst %.2f)' % worst_tetrahedron(meshed), worst_tetrahedron(meshed) >= 0.15)

    # --- H223: the nodes of the rim of the hole are spaced evenly along it (the worst triangles of the skin all touch a crease node and 15 % of the crease nodes had two chain edges differing by more than 1.5)
    # (the bottom rim had a gap of 4.5 mm at 3 mm where the corner was cut off by two triangles, a node standing 1.3 mm up the wall and another on the face outside the hole: the edge was not sharp there)
    for h in (3.0, 2.0):
        meshed = mesh_tet4(plate, h)
        for z, name in ((2.0, 'top'), (-2.0, 'bottom')):
            gaps = rim_chords(meshed, 5.0, z)
            if len(gaps) >= 6:
                # (measured 2026-11-15 on the final build: 1.22 and 1.22 at 3 mm, 1.52 and 1.86 at 2 mm -- the chain pass spaces the nodes along the rim as far as the lattice lets it, and 17 to 19 nodes on a circle of 31 mm
                # cannot all be spaced evenly; the bound is the measured worst with room, and a rim that is worse than 2 against an even spacing would be a regression)
                check('the %s rim of the hole at %.1f mm: %d nodes, the chords between neighbours differ by %.2f at most (the longest over the shortest)' % (name, h, len(gaps), max(gaps) / min(gaps)), max(gaps) / min(gaps) < (1.6 if h >= 3.0 else 2.0), max(gaps) / min(gaps))
            else:
                check('the %s rim of the hole at %.1f mm has nodes on it (%d found)' % (name, h, len(gaps)), False, len(gaps))

    # --- H223 (his words 2026-11-15, of the edges of the real bracket: "ONE SIDE WAS NOT GOOD ENOUGH", and a picture of a long straight edge with notches, single nodes of it standing off the line): a box turned to a
    # position the lattice does not fit, so that its edges run slanted across the lattice at every angle.  The edge pass used to refuse a row of nodes along such an edge all together (a tetrahedron under one of them
    # turned inside out and took back every node of it): the faces of the skin that lean more than 12 degrees from the faces of the box are counted
    turned = rotate(rotate(box(size, position=(0, 0, 0)), axis=(0, 0, 1), angle=17.0), axis=(1, 0, 0), angle=9.0)
    axes = turned_axes(17.0, 9.0)
    for h in (3.0, 2.0):
        meshed = mesh_tet4(turned, h)
        share = leaning_share(meshed, axes)
        error = abs(meshed.volume() - exact) / exact
        # (measured on the build before the one-at-a-time moves: 0.72 % at 3 mm and 0.71 % at 2 mm; the limit is set below that, the check fails there and has to pass after the change)
        check('a box turned by 17 and 9 degrees meshed at %.1f mm: %.2f %% of its surface faces lean more than 12 degrees from its faces (limit 0.5 %%)' % (h, 100 * share), share < 0.005, share)
        check('... its volume (%.2f %% off) and no sliver (worst %.2f)' % (100 * error, worst_tetrahedron(meshed)), error < 0.01 and worst_tetrahedron(meshed) >= 0.15, error)

    # --- H223 (found by the NAFEMS LE1 benchmark, 2026-11-15, validation/fino_010_nafems_le1_elliptic_membrane.py): the plate between two ellipses, a quarter of it, at a twentieth of its size (the element sizes 4.25, 3.0 and 2.75 mm
    # are its 85, 60 and 55 mm, where the stress at the tip of the hole read 14 to 29 % high): the mesh had no node at the corner where the wall of the hole, the plane y = 0 and a face of the plate meet, and the two edges that end
    # there stopped half an element short of it.  Every one of the two corners at the tip of the hole has a node within a fifth of an element (measured on the build of 2026-11-15 14:51: 0.00 mm at 3.0, 2.75 and 4.0 mm, 0.68 mm =
    # 0.16 of an element at 4.25 mm, where the corner was 0.45 of an element from the nearest node before)
    a_in, b_in, a_out, b_out, thick = 100.0, 50.0, 162.5, 137.5, 5.0

    def elliptic(a, b):
        return cylinder(1.0, 500.0).remap(Shape.X() / a, Shape.Y() / b, Shape.Z()) * ((a + b) / 2.0)
    quarter = box((a_out + 5, b_out + 5, thick), position=((a_out + 5) / 2, (b_out + 5) / 2, thick / 2))
    plate = intersection(difference(elliptic(a_out, b_out), elliptic(a_in, b_in)), quarter)
    for h in (4.25, 3.0, 2.75, 4.0):
        meshed = mesh_tet4(plate, h)
        Q, count = meshed.nodes, meshed.node_count
        gaps = [min(math.dist((a_in, 0.0, z), (Q[3 * i], Q[3 * i + 1], Q[3 * i + 2])) for i in range(count)) for z in (0.0, thick)]
        check('the plate of the NAFEMS LE1 case at %.2f mm has a node at each of the two corners at the tip of the hole (%.2f and %.2f mm away, limit %.2f)' % (h, gaps[0], gaps[1], h / 5.0), max(gaps) < h / 5.0, gaps)

    # --- a smooth body has no edge: nothing is moved
    ball = mesh_tet4(sphere(8), 3.0)
    check('a sphere has no edge: no node is moved', not snapped(ball), ball.notes)
    check('... its worst tetrahedron stays good (%.2f)' % worst_tetrahedron(ball), worst_tetrahedron(ball) > 0.5)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        bad.append('an error')
    print('ALL CHECKS PASSED' if not bad else 'FAILED: ' + '; '.join(bad))
    sys.exit(1 if bad else 0)
