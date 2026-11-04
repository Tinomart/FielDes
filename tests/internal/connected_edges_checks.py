'''Library checks of `fieldes.stdlib.features.connected_edges` (H97, the user, 2026-11-03: "A line along connected edges operation ... every edge connected to the edge that would be created is
also created"): every straight edge of a body that is joined to the picked edge through its corners, in every direction.

Run:  $env:FIELDES_DIR='<repo>\\dist\\FielDes'; $env:PYTHONPATH='<repo>\\python'; python tests\\internal\\connected_edges_checks.py
'''
import math
import sys
import time

from fieldes import *
from fieldes.stdlib import features
from fieldes.stdlib.fields import _eval_many, _shape_bounds

bad = []


def check(label, ok, extra=''):
    print(('ok   ' if ok else 'FAIL ') + label + ((' [' + extra + ']') if extra and not ok else ''))
    if not ok:
        bad.append(label)


def edges_of(shape, near):
    lo, hi = _shape_bounds(shape, None)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    began = time.perf_counter()
    found = features.connected_edges(lambda points: _eval_many(shape, points), near, 0.03 * size, size)
    return found, size, time.perf_counter() - began


def middle(e):
    return tuple(0.5 * (e[0][i] + e[1][i]) for i in range(3))


def length(e):
    return math.sqrt(sum((e[0][i] - e[1][i]) ** 2 for i in range(3)))


def near(a, b, tol):
    return all(abs(a[i] - b[i]) <= tol for i in range(3))


# ---- a cube: twelve edges, however it is reached, the picked one first
cube = box(10, position=(5, 5, 5))
found, size, seconds = edges_of(cube, (10, 10, 5))
check('a cube: 12 connected edges (%.1f s) -> %d' % (seconds, len(found)), len(found) == 12, str(len(found)))
check('a cube: the picked edge is the first', bool(found) and near(middle(found[0]), (10, 10, 5), 0.2), str(found[:1]))
check('a cube: every edge is 10 long', bool(found) and all(abs(length(e) - 10.0) < 0.3 for e in found), str([round(length(e), 2) for e in found]))
corners = set()
for e in found:
    for p in e:
        corners.add(tuple(round(c / 10.0) * 10 for c in p))
check('a cube: the ends are its eight corners -> %d' % len(corners), len(corners) == 8, str(sorted(corners)))
distinct = []
for e in found:
    m = middle(e)
    if not any(near(m, d, 0.5) for d in distinct):
        distinct.append(m)
check('a cube: twelve different edges -> %d' % len(distinct), len(distinct) == 12, str(distinct))
other, _, _ = edges_of(cube, (5, 0, 0))
check('a cube: picked at another edge, the same twelve', len(other) == 12, str(len(other)))

# ---- two cubes side by side are one bar: the seam between them is no edge
bar = union(box(10, position=(5, 5, 5)), box(10, position=(15, 5, 5)))
found, size, seconds = edges_of(bar, (0, 0, 5))
check('a bar of two cubes: 12 edges, the seam is none (%.1f s) -> %d' % (seconds, len(found)), len(found) == 12, str(len(found)))

# ---- an L of three cubes: 18 edges (two hexagons and six uprights)
ell = union(union(box(10, position=(5, 5, 5)), box(10, position=(15, 5, 5))), box(10, position=(5, 15, 5)))
found, size, seconds = edges_of(ell, (0, 0, 5))
check('an L of three cubes: 18 edges (%.1f s) -> %d' % (seconds, len(found)), len(found) == 18, str(len(found)))

# ---- no edge there, or an edge that is not straight: nothing
found, _, _ = edges_of(cube, (10, 5, 5))
check('the middle of a face: no edges', found == [], str(found))
tube = cylinder(5, 10, position=(0, 0, 5))
found, _, _ = edges_of(tube, (5, 0, 10))
check('the rim of a cylinder is a circle: no straight edges', found == [], str(found))

print()
print('%d failed' % len(bad) if bad else 'all passed')
sys.exit(1 if bad else 0)
