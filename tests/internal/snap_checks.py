'''
Checks of the points to snap to (python/fieldes/stdlib/features.py): the centre of a circular edge (the rim of a cylinder, a hole, the base of a cone, a tilted cylinder), the corner
and the edge that a rounding replaced, the centre of a face, and snap itself with these points: the bottom of a cylinder onto the base of a cone, coaxial.

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/snap_checks.py
It prints PASS or FAIL for every check, and exits with 1 when one failed (about 1 minute).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import sys
import time
import traceback

from fieldes import *
from fieldes.stdlib.cad import _rotation

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


def xyz(p):
    return tuple(float(c) for c in p.xyz)


def far(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def inside(shape, p):
    return evaluate(shape, p) < 0.0


def attempt(label, fn):
    try:
        return fn()
    except Exception as e:
        check(label + ' (no error)', False, str(e)[:200])
        return None


# ---------------------------------------------------------------- 1. the centre of a circular edge
stage('1. circle_center_at')
cyl = cylinder(10, 20)
for label, near, exact in (('top rim of a cylinder', (10, 0, 20), (0, 0, 20)), ('bottom rim of a cylinder', (0, 10, 0), (0, 0, 0)), ('rim from another side', (-7.07, 7.07, 20), (0, 0, 20))):
    c = attempt(label, lambda: circle_center_at(cyl, near))
    if c is not None:
        check('centre of the ' + label, far(xyz(c), exact) < 0.05, '{} (exact {})'.format(tuple(round(v, 3) for v in xyz(c)), exact))
cn = cone(15, 30, (40, 0, 0))
c = attempt('base of a cone', lambda: circle_center_at(cn, (55, 0, 0)))
if c is not None:
    check('centre of the base of a cone', far(xyz(c), (40, 0, 0)) < 0.05, str(tuple(round(v, 3) for v in xyz(c))))
plate = difference(box((0, 0, 0), (60, 60, 10)), cylinder(8, 20, (30, 30, -5)))
c = attempt('edge of a hole', lambda: circle_center_at(plate, (38, 30, 10)))
if c is not None:
    check('centre of the edge of a hole in a plate', far(xyz(c), (30, 30, 10)) < 0.05, str(tuple(round(v, 3) for v in xyz(c))))
c = attempt('edge of the same hole at the bottom', lambda: circle_center_at(plate, (30, 22, 0)))
if c is not None:
    check('centre of the lower edge of the hole', far(xyz(c), (30, 30, 0)) < 0.05, str(tuple(round(v, 3) for v in xyz(c))))
tilt = rotate(cyl, (1, 1, 0), 30, (3, 4, 5))
k = _rotation((1 / math.sqrt(2), 1 / math.sqrt(2), 0.0), 30.0)
top = (0.0, 0.0, 20.0)
q = (top[0] - 3, top[1] - 4, top[2] - 5)
exact = (k[0][0] * q[0] + k[0][1] * q[1] + k[0][2] * q[2] + 3, k[1][0] * q[0] + k[1][1] * q[1] + k[1][2] * q[2] + 4, k[2][0] * q[0] + k[2][1] * q[1] + k[2][2] * q[2] + 5)
rim = (exact[0] + k[0][0] * 10.0, exact[1] + k[1][0] * 10.0, exact[2] + k[2][0] * 10.0)       # (a point of the rim: the centre plus the turned x axis times the radius)
c = attempt('rim of a tilted cylinder', lambda: circle_center_at(tilt, rim))
if c is not None:
    check('centre of the rim of a tilted cylinder', far(xyz(c), exact) < 0.1, '{} (exact {})'.format(tuple(round(v, 3) for v in xyz(c)), tuple(round(v, 3) for v in exact)))
try:
    circle_center_at(box((0, 0, 0), (30, 30, 30)), (15, 0, 30))
    check('a straight edge has no centre', False)
except ValueError as e:
    check('a straight edge has no centre, and it says why', 'no circular edge' in str(e), str(e)[:120])
print(took())

# ---------------------------------------------------------------- 2. rounded corners and edges
stage('2. corner_at and edge_point_at through a rounding')
block = round_edges(box((0, 0, 0), (40, 40, 40)), 5)
try:
    corner_at(block, (39, 39, 39))
    check('a rounded corner is not a corner without rounded=True', False)
except ValueError:
    check('a rounded corner is not a corner without rounded=True', True)
c = attempt('rounded corner', lambda: corner_at(block, (39, 39, 39), rounded=True))
if c is not None:
    check('the corner that the rounding replaced', far(xyz(c), (40, 40, 40)) < 0.3, str(tuple(round(v, 3) for v in xyz(c))))
c = attempt('another rounded corner', lambda: corner_at(block, (1, 1, 1), rounded=True))
if c is not None:
    check('the corner at the origin', far(xyz(c), (0, 0, 0)) < 0.3, str(tuple(round(v, 3) for v in xyz(c))))
e = attempt('rounded edge', lambda: edge_point_at(block, (40, 20, 38.5), rounded=True))
if e is not None:
    p = xyz(e)
    check('a point of the edge that the rounding replaced', abs(p[0] - 40) < 0.3 and abs(p[2] - 40) < 0.3, str(tuple(round(v, 3) for v in p)))
sharp = attempt('sharp corner', lambda: corner_at(box((0, 0, 0), (40, 40, 40)), (39, 39, 39), rounded=True))
if sharp is not None:
    check('rounded=True finds a sharp corner as before', far(xyz(sharp), (40, 40, 40)) < 0.05, str(tuple(round(v, 4) for v in xyz(sharp))))
print(took())

# ---------------------------------------------------------------- 3. the centre of a face
stage('3. face_center')
face = select_surface(cyl, (3, 2, 0))
c = attempt('face of a cylinder', lambda: face_center(face))
if c is not None:
    check('centre of the bottom of a cylinder', far(xyz(c), (0, 0, 0)) < 0.2, str(tuple(round(v, 3) for v in xyz(c))))
ell = union(box((0, 0, 0), (30, 10, 5)), box((0, 10, 0), (10, 30, 5)))
face = select_surface(ell, (5, 5, 5))
c = attempt('L-shaped face', lambda: face_center(face))
if c is not None:
    check('centre of the area of an L-shaped face (11, 11), not the middle of its box (15, 15)', far(xyz(c), (11, 11, 5)) < 0.6, str(tuple(round(v, 3) for v in xyz(c))))
print(took())

# ---------------------------------------------------------------- 4. snap by these points
stage('4. the bottom of a cylinder snapped onto the base of a cone')
cyl_face = select_surface(cyl, (3, 2, 0))
cone_face = select_surface(cn, (42, 1, 0))
cyl_centre = circle_center_at(cyl, (10, 0, 0))
cone_centre = circle_center_at(cn, (55, 0, 0))
moved = snap(cn, cone_face, cone_centre, cyl_face, cyl_centre)
check('the cone stands coaxial under the cylinder, base to base', inside(moved, (0, 0, -29)) and inside(moved, (14, 0, -1)) and not inside(moved, (16, 0, -1)) and not inside(moved, (0, 0, 1)) and not inside(moved, (0, 0, -31)))
moved = snap(cn, cone_face, face_center(cone_face), cyl_face, face_center(cyl_face))
check('the same with the centres of the two faces', inside(moved, (0, 0, -29)) and inside(moved, (14, 0, -1)) and not inside(moved, (16, 0, -1)))
print(took())

print('=' * 70)
print(('ALL CHECKS PASSED' if not failures else '{} check(s) failed'.format(len(failures))) + ' {}    total {:.1f} s'.format(failures if failures else '', time.time() - t_all))
sys.exit(1 if failures else 0)
