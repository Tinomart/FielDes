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

# ---------------------------------------------------------------- 4. snap by these points: the move is the body's own placement (position= and rotation=)
import json
import random

from fieldes import app_support, runner
from fieldes.stdlib import snapping
from fieldes.stdlib.symmetries import _turn_matrix

stage('4. the bottom of a cylinder snapped onto the base of a cone: the cone is placed, not remade')
cyl_face = select_surface(cyl, (3, 2, 0))
cone_face = select_surface(cn, (42, 1, 0))
cyl_centre = circle_center_at(cyl, (10, 0, 0))
cone_centre = circle_center_at(cn, (55, 0, 0))
found = snapping._placement([cone_face, cone_centre, cyl_face, cyl_centre])
moved = place(cn, position=found['position'], rotation=found['rotation'])
check('the cone stands coaxial under the cylinder, base to base', inside(moved, (0, 0, -29)) and inside(moved, (14, 0, -1)) and not inside(moved, (16, 0, -1)) and not inside(moved, (0, 0, 1)) and not inside(moved, (0, 0, -31)),
      'position {} rotation {}'.format(found['position'], tuple(round(v, 4) for v in found['rotation'])))
found = snapping._placement([cone_face, face_center(cone_face), cyl_face, face_center(cyl_face)])
moved = place(cn, position=found['position'], rotation=found['rotation'])
check('the same with the centres of the two faces', inside(moved, (0, 0, -29)) and inside(moved, (14, 0, -1)) and not inside(moved, (16, 0, -1)))
print(took())

# ---------------------------------------------------------------- 5. the angles of the rotation
stage('5. the rotation= angles make the turn of the snap, whatever it is')
worst = 0.0
angles = [(0, 0, 0), (90, 0, 0), (0, 90, 0), (0, -90, 30), (10, 90, 0), (170, 45, -120)]
rng = random.Random(7)
angles += [(rng.uniform(-180, 180), rng.uniform(-90, 90), rng.uniform(-180, 180)) for _ in range(500)]
for a in angles:
    R = [[float(v) for v in row] for row in _turn_matrix(a)]
    R2 = [[float(v) for v in row] for row in _turn_matrix(snapping._euler(R))]
    worst = max(worst, max(abs(R[i][j] - R2[i][j]) for i in range(3) for j in range(3)))
check('the angles written for any turn (also a quarter turn about y) make the same turn again', worst < 1e-9, 'worst difference {:.2e} over {} turns'.format(worst, len(angles)))
print(took())

# ---------------------------------------------------------------- 6. what S writes: the placement of the body, and the features made on it go with it
stage('6. the script as S rewrites it: a snap, then a second snap of the body that is placed already')
PLAIN = 'cn = cone(15, 30, (40, 0, 0))\n'
PLACED = 'cn = cone(15, 30, (40, 0, 0), position=%s, rotation=%s)\n'
SOURCE = ('from fieldes import *\n'
          + PLAIN +
          'cyl = cylinder(10, 20)\n'
          'slab = box((-30, -30, 40), (30, 30, 50))\n'
          'near_place = (55, 0, 0)\n'
          'cone_face = select_surface(cn, seed=(42, 1, 0))\n'
          'cone_centre = circle_center_at(cn, (var(55), var(0), 0))\n'
          'cone_rim = edge_point_at(cn, near_place)\n'
          'cyl_face = select_surface(cyl, (3, 2, 0))\n'
          'cyl_centre = circle_center_at(cyl, (10, 0, 0))\n'
          'slab_top = select_surface(slab, (0, 0, 50), angle=5)\n'
          'slab_centre = face_center(slab_top)\n')


def apply_edits(text, edits):
    lines = text.split('\n')
    for l0, c0, l1, c1, new in sorted(edits, reverse=True):
        lines[l0] = lines[l0][:c0] + new + lines[l1][c1:]
    return '\n'.join(lines)


def run(text):
    ns = {'var': lambda v: float(v)}            # (var() is the number itself outside the application, where nothing drags it)
    exec(text, ns)
    runner.last_globals = ns
    return ns


def ask(text, names, written):
    return json.loads(app_support.snap_placement(json.dumps({'features': names, 'body': 'cn', 'written': written, 'source': text})))


ns0 = run(SOURCE)
first = ask(SOURCE, ['cone_face', 'cone_centre', 'cyl_face', 'cyl_centre'], [])
check('the snap is worked out from the script (no error)', 'error' not in first, str(first.get('error', ''))[:160])
check('the feature whose place is a name is reported, not guessed', first.get('left') == ['cone_rim'], str(first.get('left')))
check('the places of the two features that are on the cone are rewritten (six numbers), no other', len(first.get('edits', [])) == 6 and all(e[0] in (5, 6) for e in first['edits']), str(first.get('edits'))[:200])
if 'error' not in first:
    # (the feature whose place is a name stayed where it was, as the answer says: on the moved cone it finds nothing, so the line is taken out of the script that is run)
    B = apply_edits(SOURCE, first['edits']).replace(PLAIN, PLACED % (first['position'], first['rotation'])).replace('cone_rim = edge_point_at(cn, near_place)\n', '')
    check('the var() form of a place is kept', 'circle_center_at(cn, (var(' in B, [l for l in B.split('\n') if l.startswith('cone_centre')][0])
    ns1 = run(B)
    check('the cone of the rewritten script stands under the cylinder, base to base', inside(ns1['cn'], (0, 0, -29)) and inside(ns1['cn'], (14, 0, -1)) and not inside(ns1['cn'], (16, 0, -1)) and not inside(ns1['cn'], (0, 0, 1)))
    motion = snapping._motion([ns0['cone_face'], ns0['cone_centre'], ns0['cyl_face'], ns0['cyl_centre']])
    want = snapping._moved(motion, xyz(face_center(ns0['cone_face'])))
    got = xyz(face_center(ns1['cone_face']))
    check('the face made on the cone is the same face of the moved cone (its centre went where the cone went)', far(got, want) < 0.6, '{} (expected {})'.format(tuple(round(v, 3) for v in got), tuple(round(v, 3) for v in want)))
    check('the centre of the cone\'s base is on the centre of the cylinder\'s bottom', far(xyz(ns1['cone_centre']), (0, 0, 0)) < 0.2, str(tuple(round(v, 3) for v in xyz(ns1['cone_centre']))))
    # the body is placed already: its numbers are written, the new ones are worked out on top of them
    second = ask(B, ['cone_face', 'cone_centre', 'slab_top', 'slab_centre'], ['position', 'rotation'])
    check('a snap of a body that is placed already is worked out (no error)', 'error' not in second, str(second.get('error', ''))[:160])
    if 'error' not in second:
        C = apply_edits(B, second['edits']).replace(PLACED % (first['position'], first['rotation']), PLACED % (second['position'], second['rotation']))
        ns2 = run(C)
        check('the cone, snapped a second time, lies on the top of the box (z 50 up to 80), apex up, coaxial',
              inside(ns2['cn'], (0, 0, 79)) and inside(ns2['cn'], (14, 0, 51)) and not inside(ns2['cn'], (16, 0, 51)) and not inside(ns2['cn'], (0, 0, 49)) and not inside(ns2['cn'], (0, 0, 81)),
              'position {} rotation {}'.format(second['position'], second['rotation']))
        check('the centre of the cone\'s base is on the centre of the box\'s top', far(xyz(ns2['cone_centre']), (0, 0, 50)) < 0.2, str(tuple(round(v, 3) for v in xyz(ns2['cone_centre']))))
print(took())

print('=' * 70)
print(('ALL CHECKS PASSED' if not failures else '{} check(s) failed'.format(len(failures))) + ' {}    total {:.1f} s'.format(failures if failures else '', time.time() - t_all))
sys.exit(1 if failures else 0)
