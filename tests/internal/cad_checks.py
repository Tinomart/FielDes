'''
Checks of the CAD operations (python/fieldes/stdlib/cad.py): every function against a volume, an area or a point that is known exactly --
turning and mirroring (where a point must be inside and where not), the patterns, revolving (the whole turn, a part of it, an oblique axis), cutting by a plane, the three
kinds of hole in any direction, capsule, tube and slot, align, and the measurements (surface area, section area, overlap, distance between two bodies).

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/cad_checks.py
It prints PASS or FAIL for every check, and exits with 1 when one failed (about 30 s).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import sys
import time
import traceback

from fieldes import *

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


def inside(shape, p):
    return evaluate(shape, p) < 0.0


def volume(shape, lo, hi, per_mm=3.0):
    return mass_properties(shape, lower=lo, upper=hi, resolution=per_mm)['volume']


def near(value, exact, tolerance):
    return abs(value - exact) <= tolerance * abs(exact)


def run(label, fn):
    try:
        return fn()
    except Exception:
        check(label + ' (no exception)', False, traceback.format_exc(limit=3).replace('\n', ' | '))
        return None


# ---------------------------------------------------------------- 1. turning
stage('1. rotate')
bar = box((0, 0, 0), (10, 20, 30))
r = rotate(bar, (0, 0, 1), 90)
check('rotate 90 about z: inside where it went', inside(r, (-10, 5, 15)))
check('rotate 90 about z: not where it was', not inside(r, (5, 5, 15)))
v = volume(r, (-25, -5, -5), (15, 25, 35), 2.0)
check('rotate keeps the volume', near(v, 6000.0, 0.02), '{:.0f}'.format(v))
ball = sphere(2, (10, 0, 0))
d = rotate(ball, (1, 1, 0), 180)
check('rotate 180 about (1,1,0): (10,0,0) goes to (0,10,0)', inside(d, (0, 10, 0)) and not inside(d, (10, 0, 0)))
c = rotate(ball, (0, 0, 1), 90, center=(10, 10, 0))
check('rotate about a centre', inside(c, (20, 10, 0)) and not inside(c, (10, 0, 0)))
check('rotate 0 is the body', inside(rotate(bar, (1, 2, 3), 0), (5, 10, 15)) and not inside(rotate(bar, (1, 2, 3), 0), (11, 10, 15)))
try:
    rotate(bar, (0, 0, 0), 30)
    check('rotate: a zero axis is refused', False)
except ValueError as e:
    check('rotate: a zero axis is refused, and says why', 'direction' in str(e), str(e))
print(took())

# ---------------------------------------------------------------- 2. mirroring
stage('2. reflect and mirror')
b = box((2, 0, 0), (4, 5, 6))
check('reflect in x = 0', inside(reflect(b, (1, 0, 0)), (-3, 2, 3)) and not inside(reflect(b, (1, 0, 0)), (3, 2, 3)))
m = mirror(b, (1, 0, 0))
v = volume(m, (-5, -1, -1), (5, 6, 7), 6.0)
check('mirror: twice the volume', near(v, 120.0, 0.03), '{:.1f}'.format(v))
o = reflect(ball, (1, 1, 0))
check('reflect in an oblique plane', inside(o, (0, -10, 0)) and not inside(o, (10, 0, 0)))
check('mirror in a plane through a point', inside(mirror(b, (1, 0, 0), (10, 0, 0)), (17, 2, 3)) and inside(mirror(b, (1, 0, 0), (10, 0, 0)), (3, 2, 3)))
print(took())

# ---------------------------------------------------------------- 3. patterns
stage('3. linear and circular patterns')
row = linear_pattern(sphere(2, (0, 0, 0)), 4, (10, 0, 0))
check('linear pattern: four copies', all(inside(row, (10 * k, 0, 0)) for k in range(4)) and not inside(row, (40, 0, 0)) and not inside(row, (5, 0, 0)))
v = volume(row, (-3, -3, -3), (33, 3, 3), 6.0)
check('linear pattern: four times the volume', near(v, 4 * 4.0 / 3.0 * math.pi * 8.0, 0.03), '{:.1f}'.format(v))
ring = circular_pattern(sphere(1, (10, 0, 0)), 6)
check('circular pattern: six copies 60 degrees apart', all(inside(ring, (10 * math.cos(math.radians(60 * k)), 10 * math.sin(math.radians(60 * k)), 0)) for k in range(6)) and not inside(ring, (0, 10, 0)))
half = circular_pattern(sphere(1, (10, 0, 0)), 3, angle=180)
check('circular pattern over 180 degrees: first and last on the ends', inside(half, (10, 0, 0)) and inside(half, (0, 10, 0)) and inside(half, (-10, 0, 0)) and not inside(half, (0, -10, 0)))
about = circular_pattern(sphere(1, (0, 0, 5)), 2, axis=(1, 0, 0), center=(0, 0, 0), angle=180)
check('circular pattern about another axis', inside(about, (0, 0, 5)) and inside(about, (0, 0, -5)))
try:
    linear_pattern(sphere(1), 0, (1, 0, 0))
    check('pattern: a count of 0 is refused', False)
except ValueError as e:
    check('pattern: a count of 0 is refused, and says why', 'whole number' in str(e), str(e))
print(took())

# ---------------------------------------------------------------- 4. revolving
stage('4. revolve')
flat = rectangle((2, 0), (4, 5))
full = revolve(flat)
check('revolve: a ring', inside(full, (3, 2, 0)) and inside(full, (0, 2, 3)) and inside(full, (-3, 2, 0)) and not inside(full, (0, 2, 0)) and not inside(full, (5, 2, 0)))
v = volume(full, (-5, -1, -5), (5, 6, 5), 6.0)
check('revolve: the volume of a tube', near(v, math.pi * 12.0 * 5.0, 0.03), '{:.1f} (exact {:.1f})'.format(v, math.pi * 60.0))
quarter = revolve(flat, angle=90)
check('revolve 90: only the quarter from the profile toward z', inside(quarter, (1, 2, 2.5)) and not inside(quarter, (-1, 2, 2.5)) and not inside(quarter, (1, 2, -2.5)) and inside(quarter, (3, 2, 0.1)))
v = volume(quarter, (-5, -1, -5), (5, 6, 5), 6.0)
check('revolve 90: a quarter of the volume', near(v, math.pi * 15.0, 0.04), '{:.1f}'.format(v))
v = volume(revolve(flat, angle=270), (-5, -1, -5), (5, 6, 5), 6.0)
check('revolve 270: three quarters', near(v, math.pi * 45.0, 0.04), '{:.1f}'.format(v))
side = revolve(rectangle((0, -4), (5, -2)), axis_direction=(1, 0))
v = volume(side, (-1, -5, -5), (6, 5, 5), 6.0)
check('revolve about the x axis', near(v, math.pi * 60.0, 0.03) and inside(side, (2, 0, 3)), '{:.1f}'.format(v))
off = revolve(rectangle((12, 0), (14, 3)), axis_point=(10, 0))
check('revolve about a line away from the origin', inside(off, (10, 1, 3)) and inside(off, (12.5, 1, 0)) and not inside(off, (0, 1, 0)))
print(took())

# ---------------------------------------------------------------- 5. cutting by a plane
stage('5. cut_plane and split')
block = box((0, 0, 0), (10, 10, 10))
top, bottom = split(block, (0, 0, 1), (0, 0, 4))
check('split: the side the normal points to is the upper', inside(top, (5, 5, 7)) and not inside(top, (5, 5, 2)) and inside(bottom, (5, 5, 2)) and not inside(bottom, (5, 5, 7)))
vt, vb = volume(top, (0, 0, 0), (10, 10, 10), 4.0), volume(bottom, (0, 0, 0), (10, 10, 10), 4.0)
check('split: 600 and 400', near(vt, 600.0, 0.03) and near(vb, 400.0, 0.03), '{:.0f} {:.0f}'.format(vt, vb))
slanted = cut_plane(block, (1, 1, 0), (5, 5, 0))
v = volume(slanted, (0, 0, 0), (10, 10, 10), 4.0)
check('cut_plane by an oblique plane: half', near(v, 500.0, 0.03), '{:.0f}'.format(v))
check('cut_plane keep below is the other side', inside(cut_plane(block, (0, 0, 1), (0, 0, 4), 'below'), (5, 5, 2)))
print(took())

# ---------------------------------------------------------------- 6. holes
stage('6. holes')
plate = box((0, 0, 0), (20, 20, 10))
box_lo, box_hi = (0, 0, 0), (20, 20, 10)
through = hole(plate, (10, 10, 10), 4)
v = volume(through, box_lo, box_hi, 4.0)
check('a hole through the plate', near(v, 4000.0 - math.pi * 4.0 * 10.0, 0.01) and not inside(through, (10, 10, 5)) and inside(through, (14, 10, 5)), '{:.1f}'.format(v))
blind = hole(plate, (10, 10, 10), 4, depth=5)
v = volume(blind, box_lo, box_hi, 4.0)
check('a blind hole', near(v, 4000.0 - math.pi * 4.0 * 5.0, 0.01) and not inside(blind, (10, 10, 7)) and inside(blind, (10, 10, 3)), '{:.1f}'.format(v))
cb = hole(plate, (10, 10, 10), 4, kind='counterbore', head_diameter=8, head_depth=3)
v = volume(cb, box_lo, box_hi, 4.0)
check('a counterbore', near(v, 4000.0 - (math.pi * 16.0 * 3.0 + math.pi * 4.0 * 7.0), 0.01) and not inside(cb, (13, 10, 8)) and inside(cb, (13, 10, 5)), '{:.1f}'.format(v))
cs = hole(plate, (10, 10, 10), 4, kind='countersink', head_diameter=8, angle=90)
v = volume(cs, box_lo, box_hi, 4.0)
check('a countersink', near(v, 4000.0 - (math.pi * 2.0 / 3.0 * 28.0 + math.pi * 4.0 * 8.0), 0.01) and not inside(cs, (13.5, 10, 9.9)) and inside(cs, (13.5, 10, 7)), '{:.1f}'.format(v))
side = hole(plate, (20, 10, 5), 4, depth=10, direction=(-1, 0, 0))
v = volume(side, box_lo, box_hi, 4.0)
check('a hole from the side', near(v, 4000.0 - math.pi * 4.0 * 10.0, 0.01) and not inside(side, (15, 10, 5)) and inside(side, (5, 10, 5)), '{:.1f}'.format(v))
tilted = hole(plate, (10, 10, 10), 2, direction=(1, 0, -1))
check('a hole at an angle', not inside(tilted, (13, 10, 7)) and inside(tilted, (10, 10, 7)))
cutters = union(hole_cutter((5, 5, 10), 3), hole_cutter((15, 15, 10), 3))
two = difference(plate, cutters)
check('a pattern of holes from cutters', not inside(two, (5, 5, 5)) and not inside(two, (15, 15, 5)) and inside(two, (10, 10, 5)))
for label, call in (('a negative diameter', lambda: hole(plate, (10, 10, 10), -1)),
                    ('a countersink with no head', lambda: hole(plate, (10, 10, 10), 4, kind='countersink')),
                    ('a head that is not wider', lambda: hole(plate, (10, 10, 10), 4, kind='counterbore', head_diameter=3, head_depth=1)),
                    ('an unknown kind', lambda: hole(plate, (10, 10, 10), 4, kind='tapped'))):
    try:
        call()
        check(label + ' is refused', False)
    except (ValueError, TypeError) as e:
        check(label + ' is refused, and says why', len(str(e)) > 20, str(e))
print(took())

# ---------------------------------------------------------------- 7. more shapes
stage('7. capsule, tube, slot')
cap = capsule(3, 20)
v = volume(cap, (-4, -4, -1), (4, 4, 21), 5.0)
check('capsule: the volume', near(v, math.pi * 9.0 * 14.0 + 4.0 / 3.0 * math.pi * 27.0, 0.02), '{:.1f}'.format(v))
check('capsule: round ends', inside(cap, (0, 0, 0.5)) and not inside(cap, (2.9, 0, 0.1)) and inside(cap, (2.9, 0, 10)))
tb = tube(8, 6, 12)
v = volume(tb, (-9, -9, -1), (9, 9, 13), 4.0)
check('tube: the volume', near(v, math.pi * 28.0 * 12.0, 0.02), '{:.1f}'.format(v))
check('tube: open ends', not inside(tb, (0, 0, 6)) and inside(tb, (7, 0, 6)) and not inside(tb, (7, 0, 13)))
sl = extrude(slot(20, 6), 0, 4)
v = volume(sl, (-11, -4, -1), (11, 4, 5), 6.0)
check('slot: the volume', near(v, (14.0 * 6.0 + math.pi * 9.0) * 4.0, 0.02), '{:.1f}'.format(v))
check('slot: round ends', inside(sl, (9.5, 0, 2)) and not inside(sl, (9.9, 2.9, 2)) and inside(sl, (0, 2.9, 2)))
try:
    capsule(5, 8)
    check('capsule: a height under twice the radius is refused', False)
except ValueError as e:
    check('capsule: a height under twice the radius is refused, and says why', 'twice' in str(e), str(e))
print(took())

# ---------------------------------------------------------------- 8. align
stage('8. align')
a = box((0, 0, 0), (10, 10, 10))
b = box((20, 5, 3), (24, 9, 7))
left = align(b, a, 'x', 'min')
check('align min', inside(left, (2, 7, 5)) and not inside(left, (6, 7, 5)))
up = align(b, a, 'z', 'above', gap=2)
check('align above, with a gap', inside(up, (22, 7, 14)) and not inside(up, (22, 7, 5)) and not inside(up, (22, 7, 11)))
mid = align(b, a, 'y', 'center')
check('align center', inside(mid, (22, 5, 5)) and not inside(mid, (22, 8.5, 5)) and inside(mid, (22, 3.5, 5)))
print(took())

# ---------------------------------------------------------------- 9. measuring
stage('9. surface area, section area, overlap, distance')
s = surface_area(sphere(10))
check('surface area of a sphere', near(s, 4.0 * math.pi * 100.0, 0.03), '{:.1f} (exact {:.1f})'.format(s, 4.0 * math.pi * 100.0))
s = surface_area(box((0, 0, 0), (10, 20, 30)))
check('surface area of a box', near(s, 2200.0, 0.03), '{:.1f} (exact 2200)'.format(s))
a0 = section_area(sphere(10), (0, 0, 1), (0, 0, 0))
check('section area of a sphere through its middle', near(a0, math.pi * 100.0, 0.015), '{:.1f} (exact {:.1f})'.format(a0, math.pi * 100.0))
a6 = section_area(sphere(10), (0, 0, 1), (0, 0, 6))
check('section area of a sphere off the middle', near(a6, math.pi * 64.0, 0.02), '{:.1f} (exact {:.1f})'.format(a6, math.pi * 64.0))
ao = section_area(box((0, 0, 0), (10, 10, 10)), (1, 1, 0), (5, 5, 0))
check('section area of a cube by an oblique plane', near(ao, 10.0 * 10.0 * math.sqrt(2.0), 0.02), '{:.1f} (exact {:.1f})'.format(ao, 100.0 * math.sqrt(2.0)))
lens = interference(sphere(5), sphere(5, (6, 0, 0)))
check('interference of two spheres', near(lens, math.pi * 26.0 * 16.0 / 12.0, 0.03), '{:.1f} (exact {:.1f})'.format(lens, math.pi * 26.0 * 16.0 / 12.0))
check('interference of bodies that do not meet is 0', interference(sphere(5), sphere(5, (20, 0, 0))) == 0.0)
g = distance_between(sphere(5), sphere(5, (20, 0, 0)))
check('distance between two spheres', abs(g - 10.0) < 0.05, '{:.3f} (exact 10)'.format(g))
g = distance_between(box((0, 0, 0), (10, 10, 10)), box((15, 3, 3), (20, 8, 8)))
check('distance between two boxes', abs(g - 5.0) < 0.05, '{:.3f} (exact 5)'.format(g))
g = distance_between(sphere(5), box((8, -20, -20), (12, 20, 20)))
check('distance between a sphere and a wall', abs(g - 3.0) < 0.05, '{:.3f} (exact 3)'.format(g))
check('distance between overlapping bodies is 0', distance_between(sphere(5), sphere(5, (6, 0, 0))) == 0.0)
print(took())

# ---------------------------------------------------------------- 10. what each function makes is a body in the model tree
stage('10. kinds: a body, with a gizmo; slot a 2D shape')
import random
from fieldes import outline
from fieldes.kinds import kind_of
from fieldes.shape import Shape
import importlib
C = importlib.import_module('fieldes.stdlib.cad')            # ('import fieldes.stdlib.cad as C' reads the attribute `stdlib` of fieldes, which is the kernel's library of that name)

made = {'capsule': capsule(3, 20), 'tube': tube(8, 5, 12), 'rotate': rotate(bar, (1, 1, 0), 30), 'reflect': reflect(bar, (1, 0, 0)), 'mirror': mirror(bar, (1, 0, 0)),
        'linear_pattern': linear_pattern(sphere(2), 4, (10, 0, 0)), 'circular_pattern': circular_pattern(sphere(1, (10, 0, 0)), 6), 'revolve': revolve(flat),
        'cut_plane': cut_plane(block, (0, 0, 1), (0, 0, 4)), 'hole': hole(block, (5, 5, 10), 3), 'hole_cutter': hole_cutter((5, 5, 10), 3),
        'threaded_rod': threaded_rod('M8', 10), 'align': align(bar, block, 'x', 'above')}
for name, body in made.items():
    f = outline.facts_of(body)
    check('{} is a 3D shape in the tree, with a gizmo'.format(name), kind_of(body) == 'solid' and f['type'] == 'solid' and not f['no_gizmo'],
          'kind {} type {} no_gizmo {}'.format(kind_of(body), f['type'], f['no_gizmo']))
sl = slot(20, 6)
check('slot is a 2D shape in the tree', kind_of(sl) == 'profile' and outline.facts_of(sl)['type'] == 'profile', 'kind {}'.format(kind_of(sl)))
for name in ('rotate', 'reflect', 'mirror', 'linear_pattern', 'circular_pattern', 'revolve', 'cut_plane', 'hole', 'hole_cutter', 'threaded_rod', 'capsule', 'tube', 'align'):
    check('{} says it makes a 3D shape before it has run'.format(name), getattr(getattr(C, name), '_makes', None) == 'solid')
check('slot says it makes a 2D shape before it has run', getattr(C.slot, '_makes', None) == 'profile')
print(took())

# ---------------------------------------------------------------- 11. var() numbers
stage('11. numbers that are var()s')
V = Shape.wrap                                   # (a number as a field, what var() hands over: the code that takes it is the same)
check('rotate with a field angle', inside(rotate(bar, (0, 0, 1), V(90)), (-10, 5, 15)) and not inside(rotate(bar, (0, 0, 1), V(90)), (5, 5, 15)))
check('rotate with a field axis and centre', inside(rotate(ball, (V(0), V(0), V(1)), 90, center=(V(10), 10, 0)), (20, 10, 0)))
check('reflect with a field plane', inside(reflect(b, (V(1), 0, 0), (V(0), 0, 0)), (-22, 7, 5)) and not inside(reflect(b, (V(1), 0, 0), (V(0), 0, 0)), (22, 7, 5)))
row2 = linear_pattern(sphere(2), 4, (V(10), 0, 0))
check('linear pattern with a field step', all(inside(row2, (10 * k, 0, 0)) for k in range(4)) and not inside(row2, (5, 0, 0)))
half2 = circular_pattern(sphere(1, (10, 0, 0)), 3, angle=V(180))
check('circular pattern with a field angle', inside(half2, (10, 0, 0)) and inside(half2, (0, 10, 0)) and inside(half2, (-10, 0, 0)) and not inside(half2, (0, -10, 0)))
q2 = revolve(flat, angle=V(90))
check('revolve with a field angle: the quarter', inside(q2, (1, 2, 2.5)) and not inside(q2, (-1, 2, 2.5)) and not inside(q2, (1, 2, -2.5)) and inside(q2, (3, 2, 0.1)))
check('revolve with a field angle of 270', volume(revolve(flat, angle=V(270)), (-5, -1, -5), (5, 6, 5), 6.0) > 0.9 * math.pi * 45.0)
h2 = hole(block, (V(5), V(5), V(10)), V(3))
check('hole with field position and diameter', not inside(h2, (5, 5, 5)) and inside(h2, (8, 8, 5)) and not inside(h2, (6.4, 5, 5)) and inside(h2, (6.8, 5, 5)))
check('capsule, tube and slot with field sizes', inside(capsule(V(3), V(20)), (0, 0, 10)) and not inside(capsule(V(3), V(20)), (0, 0, 21)) and inside(tube(V(8), V(5), V(10)), (6.5, 0, 5)) and not inside(tube(V(8), V(5), V(10)), (0, 0, 5)) and inside(slot(V(20), V(6)), (9, 0, 0)) and not inside(slot(V(20), V(6)), (11, 0, 0)))
check('cut_plane and align with field numbers', inside(cut_plane(block, (0, 0, 1), (0, 0, V(4))), (5, 5, 7)) and not inside(cut_plane(block, (0, 0, 1), (0, 0, V(4))), (5, 5, 2)) and inside(align(bar, block, 'x', 'above', V(5)), (24, 5, 5)))
print(took())

# ---------------------------------------------------------------- 12. revolving a profile on either side
stage('12. revolve: the profile on the left, or across the axis')
leftp = rectangle((-4, 0), (-2, 5))
lq = revolve(leftp, angle=90)
check('a profile on the left turns the same way round the axis (toward -z)', inside(lq, (-1, 2, -2.5)) and not inside(lq, (-1, 2, 2.5)) and not inside(lq, (1, 2, -2.5)) and inside(lq, (-3, 2, -0.1)))
v = volume(lq, (-5, -1, -5), (5, 6, 5), 6.0)
check('revolve 90 of a profile on the left: a quarter of the volume', near(v, math.pi * 15.0, 0.04), '{:.1f}'.format(v))
cross = revolve(rectangle((-4, 0), (4, 5)), angle=90)
check('a profile across the axis: the right turns toward +z, the left toward -z', inside(cross, (2, 2, 1)) and inside(cross, (-2, 2, -1)) and not inside(cross, (-2, 2, 1)) and not inside(cross, (2, 2, -1)))
v = volume(cross, (-5, -1, -5), (5, 6, 5), 6.0)
check('revolve 90 of a profile across the axis: half a cylinder', near(v, math.pi * 16.0 * 5.0 / 2.0, 0.05), '{:.1f} (exact {:.1f})'.format(v, math.pi * 40.0))
print(took())

# ---------------------------------------------------------------- 13. the screws, and threads
stage('13. holes for screws, tapped holes, threads')
spec = screw_spec('M6')
check('screw_spec M6', spec['pitch'] == 1.0 and spec['tap_drill'] == 5.0 and spec['clearance_medium'] == 6.6 and spec['head_diameter'] == 10.0 and abs(spec['minor_diameter'] - 4.9175) < 1e-3, str(spec))
check('screw_spec M8 fine thread', abs(screw_spec('M8', pitch=1.0)['tap_drill'] - 7.0) < 1e-9)
for bad in ('M7', 'x'):
    try:
        screw_spec(bad)
        check('screw_spec({!r}) is refused'.format(bad), False)
    except ValueError as e:
        check('screw_spec({!r}) is refused, with the screws there are'.format(bad), 'M6' in str(e), str(e)[:80])
plate = box((0, 0, 0), (40, 40, 10))
w = hole(plate, (20, 20, 10), screw='M6')
check('hole for M6: 6.6 across, through', not inside(w, (23.2, 20, 5)) and inside(w, (23.5, 20, 5)) and not inside(w, (20, 20, 0.5)))
cb = hole(plate, (20, 20, 10), screw='M6', kind='counterbore')
check('counterbore for an M6 socket head: 11 across, 6.5 deep', not inside(cb, (25, 20, 7)) and inside(cb, (25, 20, 3)) and inside(cb, (26, 20, 7)) and not inside(cb, (23, 20, 2)))
cs = hole(plate, (20, 20, 10), screw='M6', kind='countersink')
check('countersink for an M6 flat head: 13.8 across at the surface, 45 degrees', not inside(cs, (26.5, 20, 9.9)) and inside(cs, (27.2, 20, 9.9)) and not inside(cs, (23.5, 20, 7)) and inside(cs, (25, 20, 7)))
tp = hole(plate, (20, 20, 10), screw='M6', kind='tapped', depth=8)
check('tapped hole: the tap drill, 5 across, 8 deep', not inside(tp, (22.4, 20, 5)) and inside(tp, (22.6, 20, 5)) and inside(tp, (20, 20, 1)) and not inside(tp, (20, 20, 3)))
check('explicit sizes beat the screw', not inside(hole(plate, (20, 20, 10), 8, screw='M6'), (23.9, 20, 5)) and inside(hole(plate, (20, 20, 10), 8, screw='M6'), (24.2, 20, 5)))
for bad, why in ((lambda: hole(plate, (20, 20, 10)), 'diameter'), (lambda: hole(plate, (20, 20, 10), kind='tapped'), 'thread')):
    try:
        bad()
        check('a hole without its size is refused ({})'.format(why), False)
    except ValueError as e:
        check('a hole without its size is refused ({})'.format(why), why in str(e), str(e)[:90])
th = hole(plate, (20, 20, 10), screw='M6', kind='tapped', modeled=True, depth=8)
rng = random.Random(3)
void = total = 0
for _ in range(1500):
    a, z = rng.uniform(0, 2 * math.pi), rng.uniform(3.0, 9.0)
    total += 1
    void += 0 if inside(th, (20 + 2.75 * math.cos(a), 20 + 2.75 * math.sin(a), z)) else 1
frac = void / total
check('a modelled thread: between the minor and the major radius the groove fills about 40 % of the turn', 0.33 < frac < 0.50, '{:.2f}'.format(frac))
check('a modelled thread: the minor radius is always void, the major always solid', all(not inside(th, (20 + 2.4 * math.cos(a), 20 + 2.4 * math.sin(a), 6)) and inside(th, (20 + 3.1 * math.cos(a), 20 + 3.1 * math.sin(a), 6)) for a in [k * 0.5 for k in range(13)]))
tl = hole(plate, (20, 20, 10), screw='M6', kind='tapped', modeled=True, depth=8, hand='left')


def helix_phase(body, quarter):
    ''' Where along the axis the groove is at a quarter turn round (0..1 of a pitch): the mean place of the void points at radius 2.9 '''
    zs = [3.0 + 0.01 * i for i in range(600)]
    px, py = (2.9, 0.0) if quarter == 0 else (0.0, 2.9)
    hits = [z for z in zs if not inside(body, (20 + px, 20 + py, z))]
    s = sum(math.sin(2 * math.pi * z) for z in hits)
    c = sum(math.cos(2 * math.pi * z) for z in hits)
    return math.atan2(s, c) / (2 * math.pi)


for body, name, sign in ((th, 'right', 1), (tl, 'left', -1)):
    shift = (helix_phase(body, 1) - helix_phase(body, 0)) % 1.0
    check('a {}-hand thread climbs by a quarter of the pitch in a quarter turn counterclockwise (seen from above)'.format(name), abs(shift - (0.25 if sign > 0 else 0.75)) < 0.05, 'shift {:.3f}'.format(shift))
rod = threaded_rod('M20', 10)
rv = volume(rod, (-11, -11, -1), (11, 11, 11), 4.0)
root = 20 - 1.2269 * 2.5
mean_sq = sum((root / 2 + (10 - root / 2) * min(1.0, max(0.0, (0.375 - abs(u / 400.0 - 0.5)) / 0.3125))) ** 2 for u in range(400)) / 400.0
check('threaded rod: the volume of its profile', near(rv, math.pi * mean_sq * 10.0, 0.04), '{:.0f} (exact {:.0f})'.format(rv, math.pi * mean_sq * 10.0))
print(took())

# ---------------------------------------------------------------- 14. a pattern of many copies
stage('14. patterns of many copies')
small = sphere(1.5, (0, 0, 0))
many = linear_pattern(small, 200, (4, 0, 0))
check('a row of 200 copies: every copy is there', all(inside(many, (4 * k, 0, 0)) for k in (0, 1, 57, 128, 199)))
check('and between the copies and past the ends there is nothing', not any(inside(many, p) for p in ((2, 0, 0), (-4, 0, 0), (800, 0, 0), (4 * 57 + 2, 0, 0))))
v = volume(many, (-3, -3, -3), (800, 3, 3), 3.0)
v_one = volume(small, (-3, -3, -3), (3, 3, 3), 3.0)               # (the samples round each copy are the same: 3 to a mm and a step of 4 mm -- so 200 copies are 200 times one)
check('and the volume is 200 times the volume of one', near(v, 200 * v_one, 0.01), '{:.0f} (200 times {:.1f})'.format(v, v_one))
tooth = box((8, -0.2, -1), (11, 0.2, 1))
gear = circular_pattern(tooth, 100)
step = 2.0 * math.pi / 100.0
check('a ring of 100 teeth: every tooth is there', all(inside(gear, (9.5 * math.cos(k * step), 9.5 * math.sin(k * step), 0)) for k in (0, 1, 33, 50, 99)))
check('and between the teeth there is nothing', not any(inside(gear, (9.5 * math.cos((k + 0.5) * step), 9.5 * math.sin((k + 0.5) * step), 0)) for k in (0, 1, 33, 50, 99)))
v = volume(gear, (-12, -12, -1.5), (12, 12, 1.5), 8.0)
check('and the volume is that of 100 teeth', near(v, 100 * 3.0 * 0.4 * 2.0, 0.08), '{:.0f} (exact {:.0f})'.format(v, 100 * 3.0 * 0.4 * 2.0))
print(took())

# ---------------------------------------------------------------- 15. distance_between where the bodies come near each other in several places
stage('15. distance_between: several places')
plate_a = box((-40, -10, -5), (40, 10, 0))
near_one = box((0, -5, 1.0), (5, 5, 2.0))
near_two = box((20, -5, 0.6), (25, 5, 1.6))
two = union(near_one, near_two)
d = distance_between(plate_a, two, element_size=2.0)
check('the nearer of two places is the distance', abs(d - 0.6) < 0.05, '{:.3f} (exact 0.6)'.format(d))
sa, sb = sphere(5, (0, 0, 0)), sphere(3, (12, 0, 0))
check('two spheres', abs(distance_between(sa, sb) - 4.0) < 0.05, '{:.3f}'.format(distance_between(sa, sb)))
print(took())

# ---------------------------------------------------------------- 16. the new base shapes are drag adjustable: a face each
stage('16. handles of capsule, tube, slot and the threaded rod')
from fieldes.stdlib.handles import handle_values
cap = capsule(3, 20)
check('capsule: three handles, its radius and the heights of its ends', handle_values(cap) == ['3.0', '0.0', '20.0'], str(handle_values(cap)))
tall = handles(cap, [3, 0, 25])
check('capsule: the top end pulled makes it taller and keeps its radius', inside(tall, (0, 0, 24)) and not inside(tall, (0, 0, 26)) and inside(tall, (2.5, 0, 12)) and not inside(tall, (3.5, 0, 12)) and inside(tall, (0, 0, 1)))
wide = handles(cap, [4, 0, 20])
check('capsule: the side pulled makes it wider and keeps both ends where they are', inside(wide, (3.5, 0, 10)) and not inside(wide, (4.5, 0, 10)) and inside(wide, (0, 0, 19.9)) and not inside(wide, (0, 0, 20.5)) and inside(wide, (0, 0, 0.1)) and not inside(wide, (0, 0, -0.5)))
tb = tube(8, 5, 12)
check('tube: four handles, its outer wall, its bore and the heights of its ends', handle_values(tb) == ['8.0', '5.0', '0.0', '12.0'], str(handle_values(tb)))
check('tube: the outer wall pulled', inside(handles(tb, [9, 5, 0, 12]), (8.5, 0, 6)) and not inside(handles(tb, [9, 5, 0, 12]), (9.5, 0, 6)) and inside(handles(tb, [9, 5, 0, 12]), (5.5, 0, 6)))
check('tube: the bore pulled', not inside(handles(tb, [8, 6, 0, 12]), (5.5, 0, 6)) and inside(handles(tb, [8, 6, 0, 12]), (6.5, 0, 6)) and inside(handles(tb, [8, 6, 0, 12]), (7.5, 0, 6)))
check('tube: an end pulled', inside(handles(tb, [8, 5, 0, 15]), (6.5, 0, 14)) and not inside(handles(tb, [8, 5, 0, 15]), (6.5, 0, 16)) and not inside(handles(tb, [8, 5, 0, 15]), (0, 0, 14)))
sl2 = slot(20, 6)
check('slot: four handles, its two ends and its two long sides', handle_values(sl2) == ['-10.0', '10.0', '-3.0', '3.0'], str(handle_values(sl2)))
longer = handles(sl2, [-12, 10, -3, 3])
check('slot: an end pulled makes it longer and keeps the round end and the width', inside(longer, (-11.5, 0, 0)) and not inside(longer, (-12.5, 0, 0)) and not inside(longer, (-11.8, 2.9, 0)) and inside(longer, (0, 2.9, 0)) and not inside(longer, (0, 3.2, 0)) and not inside(longer, (10.3, 0, 0)))
wider = handles(sl2, [-10, 10, -3, 5])
check('slot: a long side pulled makes it wider, keeps the ends at their places and the ends round', inside(wider, (0, 4.5, 0)) and not inside(wider, (0, 5.3, 0)) and not inside(wider, (0, -3.3, 0)) and inside(wider, (-9.8, 1, 0)) and not inside(wider, (-10.2, 1, 0)) and not inside(wider, (-9.9, 4.9, 0)))
rod = threaded_rod('M8', 10)
check('threaded rod: three handles, its diameter and the heights of its ends', handle_values(rod) == ['8.0', '0.0', '10.0'] and not getattr(rod, '_handles_ends_only', False), str(handle_values(rod)))
longrod = handles(rod, [8, 0, 15])
check('threaded rod: the top end pulled makes it longer', inside(longrod, (0, 0, 14)) and not inside(longrod, (0, 0, 16)) and inside(longrod, (0, 0, 1)))


def crest_reach(body):
    ''' How far the thread reaches from the axis, along x: the most over one pitch (1.25) of heights '''
    best = 0.0
    for k in range(25):
        lo, hi = 0.0, 12.0
        for _ in range(30):
            mid = 0.5 * (lo + hi)
            if inside(body, (mid, 0, 2.0 + 1.25 * k / 25.0)):
                lo = mid
            else:
                hi = mid
        best = max(best, lo)
    return best


wide_rod, thin_rod = handles(rod, [10, 0, 10]), handles(rod, [6, 0, 10])
check('threaded rod: the side pulled makes it wider (and narrower), and the thread keeps its profile', abs(crest_reach(rod) - 4.0) < 0.02 and abs(crest_reach(wide_rod) - 5.0) < 0.02 and abs(crest_reach(thin_rod) - 3.0) < 0.02,
      '{:.3f} {:.3f} {:.3f} (4, 5, 3)'.format(crest_reach(rod), crest_reach(wide_rod), crest_reach(thin_rod)))
check('threaded rod: ... and the ends stay where they were', inside(wide_rod, (0, 0, 9.5)) and not inside(wide_rod, (0, 0, 10.5)) and inside(wide_rod, (0, 0, 0.5)) and not inside(wide_rod, (0, 0, -0.5)))
print(took())

# ---------------------------------------------------------------- 17. a half space square to an axis is a body, and is drawn up to the boundary of the region
stage('17. half_space: a 3D shape, and drawn')
from fieldes.kinds import kind_of
from fieldes.ffi import lib, libfive_region_t, libfive_interval_t
from fieldes.stdlib.fields import render_mesh
for axis, norm, point in (('z', (0, 0, 1), (0, 0, 3.37)), ('x', (1, 0, 0), (3.37, 0, 0)), ('y', (0, -1, 0), (0, 2.5, 0))):
    plane = half_space(norm, point)
    check('half_space square to {} is a 3D shape (a field that does not depend on z is not a 2D shape)'.format(axis), kind_of(plane) == 'solid', str(kind_of(plane)))
    mesh = render_mesh(plane, libfive_region_t(*[libfive_interval_t(-20.0, 20.0) for _ in range(3)]), 2)
    m = mesh.contents if mesh else None
    pts = [(m.verts[i].x, m.verts[i].y, m.verts[i].z) for i in range(m.vert_count)] if m else []
    across = [a for a in range(3) if (0, 1, 2)[a] != 'xyz'.index(axis)]
    reach = min(max(abs(p[a]) for p in pts) for a in across) if len(pts) > 5 else 0.0
    check('half_space square to {} is drawn (a mesh with faces, not one cell)'.format(axis), bool(m) and m.tri_count > 4, '{} vertices'.format(len(pts)))
    check('and reaches the boundary of the region in both directions across it (within the cells of the boundary)', reach > 18.0, '{:.2f} of 20'.format(reach))
    if mesh:
        lib.libfive_mesh_delete(mesh)
print(took())

print('=' * 70)
print(('ALL CHECKS PASSED' if not failures else '{} check(s) failed'.format(len(failures))) + ' {}    total {:.1f} s'.format(failures if failures else '', time.time() - t_all))
sys.exit(1 if failures else 0)
