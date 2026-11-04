'''
Checks of the edges and corners of a volume mesh (the user, 2026-11-03: "I have no idea how you made those weird slops near the edges that has nothing to do with the geometry it came
from").  The lattice of the mesher does not lie on the edges of a body unless the element size fits it, and the nodes stood on the two faces beside an edge with a slope between them: a
box meshed at 4 mm lost 1.8 % of its volume and 7 % of its surface was slope.  mesh_volume puts the nodes on the edges and corners now (meshing._snap_features).  Library checks, no
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


def main():
    size = (30, 20, 10)
    exact = 30.0 * 20.0 * 10.0
    surface = 2.0 * (30 * 20 + 20 * 10 + 30 * 10)
    box_body = box(size, position=(0, 0, 0))

    # --- an element size the lattice does not fit: the edges and corners were cut
    for h, volume_limit, slope_limit in ((4.0, 0.002, 1.0), (3.0, 0.01, 0.03 * surface)):
        mesh = mesh_volume(box_body, h)
        error = abs(mesh.volume() - exact) / exact
        check('a box meshed at %.1f mm has its volume (%.3f %% off)' % (h, 100 * error), error < volume_limit, error)
        slope = sloped_area(mesh)
        check('... and its surface has no slope the box does not have (%.1f of %.0f mm2)' % (slope, surface), slope < slope_limit, slope)
        check('... the edges were found by the mesher, and said so', snapped(mesh), mesh.notes)
        check('... no tetrahedron is a sliver (worst %.2f)' % worst_tetrahedron(mesh), worst_tetrahedron(mesh) >= 0.15)
    mesh = mesh_volume(box_body, 4.0)
    gap = nearest_corner_gap(mesh, (15.0, 10.0, 5.0))
    check('a box meshed at 4 mm has a node on each of its corners (the worst is %.4f mm away)' % gap, gap < 0.01, gap)
    lo, hi = mesh.bounds
    check('... and no node outside it', max(abs(lo[0] + 15), abs(lo[1] + 10), abs(lo[2] + 5), abs(hi[0] - 15), abs(hi[1] - 10), abs(hi[2] - 5)) < 1e-3, (lo, hi))

    # --- an element size it fits: nothing to put back
    fit = mesh_volume(box_body, 2.5)
    check('a box meshed at 2.5 mm, where the lattice lies on its edges, is left as it is', not snapped(fit) and sloped_area(fit) < 1e-6, fit.notes)

    # --- a plate with a hole: the rim of the hole and the edges of the plate
    plate = difference(box((80, 40, 4), position=(0, 0, 0)), cylinder(5, 12, position=(0, 0, -6)))
    plate_exact = 80 * 40 * 4 - math.pi * 25 * 4
    meshed = mesh_volume(plate, 3.0)
    error = abs(meshed.volume() - plate_exact) / plate_exact
    check('a plate with a hole meshed at 3 mm has its volume (%.2f %% off; it was 2.1 %% before)' % (100 * error), error < 0.005, error)
    check('... no tetrahedron is a sliver (worst %.2f)' % worst_tetrahedron(meshed), worst_tetrahedron(meshed) >= 0.15)

    # --- a smooth body has no edge: nothing is moved
    ball = mesh_volume(sphere(8), 3.0)
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
