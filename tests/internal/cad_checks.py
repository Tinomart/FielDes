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

print('=' * 70)
print(('ALL CHECKS PASSED' if not failures else '{} check(s) failed'.format(len(failures))) + ' {}    total {:.1f} s'.format(failures if failures else '', time.time() - t_all))
sys.exit(1 if failures else 0)
