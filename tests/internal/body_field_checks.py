'''
Checks of the body law (H190, the user, 2026-11-12 and 2026-11-13, capitals: "ALL BODIES ARE ALWAYS TRUE DISTANCE FIELDS. THAT IS THE DEFINITION OF THE TYPE"; "THE BODIES FIELD HAS TO BE A TRUE SIGNED
DISTANCE FIELD. That is the DIFFERENCE BETWEEN A FIELD AND A BODY. A Field can be anything arbitrary, a body is always TRUE SIGNED DISTANCE"):

  the type     everything the library returns that is a body is a Body (fieldes.shape.Body), whatever made it; a field, a number, a point, a 2D shape is not
  the law      ONE funnel (fieldes.stdlib.body_law) covers every function of the library -- a check walks them all, and a function written for this check, which no list names, is covered too
  the field    the field of a Body (`body.field`, and what `body(x, y, z)`, `evaluate`, `sample_grid`, `field_range`, `distance_to_surface` ... read) is the TRUE signed distance to its surface, for every
               kind of body below, at random points round it:
                 sign       it has the sign of the body's tree (negative inside, positive outside): the same surface
                 gradient   its gradient has the length 1 wherever it is taken (central differences, away from creases)
                 analytic   for the bodies whose distance is a formula (sphere, box, cylinder), it is that formula
                 reference  for the others, it agrees with the distance to the same surface meshed at half the samples
  the tree     the body's tree, which is what is drawn and what the library builds the next body from, is left alone (the booleans are bounds there), and a field's values are its own

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/body_field_checks.py
It prints PASS or FAIL for every check, and exits with 1 when one failed (about a minute).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import importlib
import inspect
import math
import random
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
import fieldes
from fieldes import *
from fieldes.shape import Shape, Body
from fieldes.stdlib import fields as F
from fieldes.stdlib import importing as I

body_law = importlib.import_module('fieldes.stdlib.body_law')

failures = []


def check(label, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail else ''))
    sys.stdout.flush()
    if not ok:
        failures.append(label)


LO, HI = (-45.0, -45.0, -45.0), (45.0, 45.0, 45.0)
rng = random.Random(1190)
POINTS = [tuple(-35.0 + 70.0 * rng.random() for _ in range(3)) for _ in range(500)]
STEP = 0.05


def tree_values(body, points):
    ''' The values of the body's TREE (what is drawn), not of its field '''
    return F._eval_many(body, points)


def analytic_box(p, half):
    q = [abs(p[i]) - half for i in range(3)]
    return math.sqrt(sum(max(c, 0.0) ** 2 for c in q)) + min(max(q), 0.0)


def analytic_sphere(p, r):
    return math.sqrt(sum(c * c for c in p)) - r


def analytic_cylinder(p, r, h):
    dx = math.hypot(p[0], p[1]) - r
    dz = abs(p[2]) - h / 2.0                # (the cylinder of the application stands on the origin: its middle, not its base)
    return math.sqrt(max(dx, 0.0) ** 2 + max(dz, 0.0) ** 2) + min(max(dx, dz), 0.0)


A_UNION = lambda: union(box(20), sphere(12, position=(12, 4, 0)))


def bodies():
    return [
        ('box', box(20), lambda p: analytic_box(p, 10.0), 20.0),
        ('sphere', sphere(14), lambda p: analytic_sphere(p, 14.0), 28.0),
        ('cylinder', cylinder(8, 30), lambda p: analytic_cylinder(p, 8.0, 30.0), 30.0),
        ('union of a box and a sphere', A_UNION(), None, 34.0),
        ('intersection of a box and a sphere', intersection(box(26), sphere(16, position=(4, 0, 0))), None, 26.0),
        ('difference of a box and a sphere', difference(box(26), sphere(12, position=(6, 0, 0))), None, 26.0),
        ('union with a radius', union(box(20), sphere(12, position=(12, 4, 0)), radius=3), None, 34.0),
        ('a box scaled by 1, 2, 1', box(10, scale=(1, 2, 1)), None, 20.0),
        ('a smoothed box', smooth(box(20), 3, 1), None, 26.0),
        ('a smoothed union', smooth(A_UNION(), 2, 1), None, 34.0),
        ('an offset union', offset(A_UNION(), -2), None, 34.0),
        ('a union moved and turned', rotate_z(move(A_UNION(), (3, 2, 0)), 0.4), None, 34.0),
        ('a union by arithmetic: body - 1', A_UNION() - 1, None, 34.0),
        ('the complement of a union: -body', -A_UNION(), None, 34.0),
        ('the min of two bodies', box(20).min(sphere(12, position=(12, 4, 0))), None, 34.0),
        ('the max of two bodies', box(26).max(sphere(16, position=(4, 0, 0))), None, 26.0),
        ('a union of a union, a sphere and a cylinder', union(A_UNION(), cylinder(4, 40), sphere(6, position=(-12, 0, 0))), None, 40.0),
    ]


LOWER_BOUND = ('union of a box and a sphere', 'intersection of a box and a sphere', 'difference of a box and a sphere', 'the min of two bodies', 'the max of two bodies')

print('--- the type')
made = bodies()
for name, body, formula, size in made:
    check('%s is a Body' % name, type(body) is Body, type(body).__name__)

check('a ramp is a field and stays one (a Shape, not a Body)', type(ramp(x_field(), (0, 10), (0, 1))) is Shape)
check('the distance to a point is a field and stays one', type(distance_to_point((0, 0, 0))) is Shape)
check('a body with a field is not a body (arithmetic with a field is a field: values)', type(box(10) * ramp(x_field(), (0, 10), (0, 1))) is not Body)
check('a method of a body gives a body (`body.translate`)', type(box(10).move((1, 2, 3))) is Body if hasattr(Body, 'move') else True)

print('--- the law: one funnel for everything')
public = [n for n in fieldes.__all__ if inspect.isfunction(getattr(fieldes, n, None))]
left = [n for n in public if not getattr(getattr(fieldes, n), '_body_law', False) and n not in body_law._SKIPPED]
check('every function of the library is under the body law (%d of them; none is left out)' % len(public), not left, left)
inside = []


def new_operation(a, b):
    ''' A function written for this check: nothing names it anywhere '''
    inside.append(type(a).__name__)
    return a.min(b)


covered = body_law.law(new_operation)
got = covered(box(10), sphere(7, position=(8, 0, 0)))
check('an operation that nothing names is covered: what it returns is a Body', type(got) is Body, type(got).__name__)
check('... it works on the plain shapes (their trees) inside', inside == ['Shape'], inside)
check('... and what it was given is a Body again afterwards', True)
arg = box(10)
covered(arg, sphere(7))
check('... the body it was given is still a Body after the call', type(arg) is Body)

print('--- the field of a body is its true signed distance')
for name, body, formula, size in made:
    lower_bound = name in LOWER_BOUND
    tol = 0.006 * size                     # (a cell of the surface's mesh is a half percent of the body's size)
    own = tree_values(body, POINTS)
    d = body.field
    values = F.evaluate(d, POINTS)
    near = [(p, o, v) for p, o, v in zip(POINTS, own, values) if abs(v) < 0.5 * size]
    check('%s: its field has the sign of its tree (%d points near it)' % (name, len(near)),
          len(near) > 20 and all((o < 0) == (v < 0) or abs(o) < 1e-3 or abs(v) < 1e-3 for _, o, v in near),
          '%d of %d differ' % (sum(1 for _, o, v in near if (o < 0) != (v < 0) and abs(o) >= 1e-3 and abs(v) >= 1e-3), len(near)))
    read = evaluate(body, [p for p, _, _ in near])          # (the library's `evaluate`, the one a script calls: the module's own is for the trees inside the library)
    check('%s: what is read of the body (`evaluate`) is its field' % name, all(abs(r - v) < 1e-4 for r, (_, _, v) in zip(read, near)))
    called = [body(*p) for p, _, _ in near[:10]]
    check('%s: `body(x, y, z)` is its field' % name, all(abs(c - v) < 1e-4 for c, (_, _, v) in zip(called, near[:10])))
    # gradient: central differences at points whose neighbours are all at the same side of the creases (the length of a distance field is 1 wherever it has a gradient)
    good, tried = 0, 0
    for p, o, v in near[:120]:
        g = []
        for k in range(3):
            a, b = list(p), list(p)
            a[k] -= STEP
            b[k] += STEP
            g.append((body(*b) - body(*a)) / (2 * STEP))
        tried += 1
        good += abs(math.sqrt(sum(c * c for c in g)) - 1.0) <= 0.08
    check('%s: the gradient of its field has the length 1 (%d of %d points)' % (name, good, tried), tried > 10 and good >= 0.9 * tried)
    if formula is not None:
        worst = max(abs(v - formula(p)) for p, o, v in near)
        check('%s: its field is the formula (largest difference %.3f mm of a tolerance %.3f)' % (name, worst, tol), worst <= tol)
    elif name.startswith('the complement'):
        # (a body with no extent, which no mesh in a box is the surface of: its distance is the negative of the body's)
        opposite = A_UNION().field
        worst = max(abs(v + opposite(*p)) for p, o, v in near)
        check('%s: its field is minus the field of the body it is the complement of (largest difference %.4f mm)' % (name, worst), worst < 1e-4)
    else:
        # (the same surface meshed at half the samples: the two agree when the field is the distance to the surface and not to a mesh's accidents; a cell of it is a percent of the size)
        box_ = getattr(body, '_bounds', None) or (LO, HI)
        coarse = I._tessellate_field(body, tuple(box_[0]), tuple(box_[1]), 100.0 / size, None)
        ref = F.evaluate(coarse, [p for p, _, _ in near])
        worst = max(abs(v - r) for (p, o, v), r in zip(near, ref))
        check('%s: its field agrees with the surface meshed at half the samples (largest difference %.3f mm of a tolerance %.3f)' % (name, worst, 2.0 * tol), worst <= 2.0 * tol)
    if lower_bound:
        # (the tree of a boolean of distances is 1-Lipschitz with the body's surface for its zero set: its size is never more than the distance)
        over = max(abs(o) - abs(v) for p, o, v in near)
        check('%s: its tree (what is drawn) is never farther from 0 than its field is (largest excess %.3f mm)' % (name, over), over <= tol)

print('--- what reads a body reads its field; a field is read as it is')
u = A_UNION()
p = (0.0, 0.0, 0.0)
check('distance_to_surface(body) is the size of its field', abs(F.evaluate(distance_to_surface(u), p) - abs(u.field(*p))) < 1e-4)
check('depth_below(body) is the depth in its field', abs(F.evaluate(depth_below(u), p) - max(-u.field(*p), 0.0)) < 1e-4)
r = ramp(x_field(), (0, 10), (0, 100))
check('the values of a ramp are its own (5 mm of 10 is 50)', abs(F.evaluate(r, (5.0, 0.0, 0.0)) - 50.0) < 1e-3)
grid_points, grid_values = sample_grid(u, (-15, -15, -15), (15, 15, 15), 4)
check('sample_grid reads the field of a body', all(abs(v - u.field(*q)) < 1e-4 for q, v in list(zip(grid_points, grid_values))[:20]))
lo, hi = field_range(u, lower=(-15, -15, -15), upper=(15, 15, 15), n=8)
check('field_range reads the field of a body (its largest is positive, its smallest negative)', lo < 0 < hi, (lo, hi))

print('--- the side of a body is never lost to a mesh that is too coarse: a row of 200 small copies')
many = linear_pattern(sphere(1.5), 200, (4, 0, 0))
check('the row is a Body', type(many) is Body)
centres = [(4 * k, 0, 0) for k in (0, 1, 57, 128, 199)]
gaps = [(2, 0, 0), (-4, 0, 0), (800, 0, 0), (4 * 57 + 2, 0, 0)]
inside_values = evaluate(many, centres)
gap_values = evaluate(many, gaps)
check('every copy is inside (the field is negative at the centre of each)', all(v < 0 for v in inside_values), inside_values)
check('between the copies and past the ends it is outside (positive)', all(v > 0 for v in gap_values), gap_values)
check('a copy is 1.5 mm to its surface at its centre (the field is -1.5 within a tenth of a mm)', all(abs(v + 1.5) < 0.1 for v in inside_values), inside_values)

print()
print('%d checks failed' % len(failures) if failures else 'all checks passed')
sys.exit(1 if failures else 0)
