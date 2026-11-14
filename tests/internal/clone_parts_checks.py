''' H174 (the user, 2026-11-14, his file StillFucked.py): the copy of a model that is made of other models -- a path extrusion of a profile and a line -- carries its parts: the line is drawn through the
images of its points, and a dot for each of them, wherever the copy is.  The copy FielDes writes is `copy = instance(original, ...)` and then `copy = place(copy, ...)`: both forms are looked at.

Run it with the library only (no window):   python tests/internal/clone_parts_checks.py
'''
import ast
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), 'python'))

from fieldes import *                                   # noqa: E402,F401
from fieldes import made_from                           # noqa: E402
from fieldes.stdlib.curves import value_of              # noqa: E402

FAILED = []


def check(name, ok, detail=''):
    print('%s  %s  %s' % ('PASS' if ok else 'FAIL', name, detail))
    if not ok:
        FAILED.append(name)


def run(text):
    ''' The statements of a script, run as the runner runs them (made_from notes what each model was made from) '''
    ns = {'__name__': '__main__', 'var': (lambda x: x)}
    ns.update({k: v for k, v in globals().items() if not k.startswith('__')})
    for node in ast.parse(text).body:
        before = made_from.reads(node, ns)
        exec(compile(ast.Module([node], []), 'script', 'exec'), ns)
        if before is not None:
            made_from.note(node, ns, before)
    return ns


def centre(shape):
    lo, hi = shape._bounds
    return tuple((lo[i] + hi[i]) / 2.0 for i in range(3))


def line_points(shape):
    shown = shape._display()
    curve = [s for s in shown if hasattr(s, 'points') and hasattr(s, '_remake')]
    return [tuple(float(value_of(c)) for c in (p.xyz if hasattr(p, 'xyz') else p)) for p in curve[0].points] if curve else None


def relation(shape, points):
    ''' where the points of the line are, from the middle of the box of the body '''
    c = centre(shape)
    return [tuple(p[i] - c[i] for i in range(3)) for p in points]


HEAD = '''
profile = rectangle(5)
p1 = point(position=(var(6.15), var(-10.68), var(8.8)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
p2 = point(position=(var(6.9), var(-11.53), var(4.61)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
p3 = point(position=(var(10.1), var(-13.2), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
path = polyline([p1, p2, p3], closed=False)
leg = path_extrude(profile, path, twist=0)
'''

# 1. his form: instance with no place of its own, then place
ns = run(HEAD + '''
clone = instance(leg, position=(var(0), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
clone = place(clone, position=(var(-4.4), var(-6.7), var(0.0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
''')
leg, clone = ns['leg'], ns['clone']
original_line = [tuple(float(value_of(c)) for c in p.xyz) for p in ns['path'].points]
clone_line = line_points(clone)
check('the clone made with instance and place draws its line', clone_line is not None and len(clone_line) == 3)
if clone_line:
    ra, rb = relation(leg, original_line), relation(clone, clone_line)
    worst = max(abs(ra[k][i] - rb[k][i]) for k in range(3) for i in range(3))
    check('the line stands on the clone as the original line stands on the original (worst %.4f mm)' % worst, worst < 0.05)
    dots = [s for s in clone._display() if hasattr(s, 'xyz')]
    check('a dot for each point of the line, as items of what the clone displays (%d)' % len(dots), len(dots) == 3)
    nested = [s for s in clone._display() if getattr(s, '_image_dots', None)]
    check('and no item that answers with a list of its own (the viewport draws no list inside a list)', not nested)

# 2. the same with instance alone, placed by its own numbers
ns = run(HEAD + '''
clone = instance(leg, position=(var(-4.4), var(-6.7), var(0.0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
''')
leg, clone = ns['leg'], ns['clone']
original_line = [tuple(float(value_of(c)) for c in p.xyz) for p in ns['path'].points]
clone_line = line_points(clone)
check('the clone made with instance alone draws its line', clone_line is not None and len(clone_line) == 3)
if clone_line:
    ra, rb = relation(leg, original_line), relation(clone, clone_line)
    worst = max(abs(ra[k][i] - rb[k][i]) for k in range(3) for i in range(3))
    check('and it stands on the clone as the original line stands on the original (worst %.4f mm)' % worst, worst < 0.05)

# 3. turned: the line stands on the clone as the original's, turned by the same angle about the middle (90 degrees about z)
ns = run(HEAD + '''
clone = instance(leg, position=(var(0), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
clone = place(clone, position=(var(-4.4), var(-6.7), var(0.0)), rotation=(var(0), var(0), var(90)), scale=(var(1), var(1), var(1)))
''')
leg, clone = ns['leg'], ns['clone']
original_line = [tuple(float(value_of(c)) for c in p.xyz) for p in ns['path'].points]
clone_line = line_points(clone)
check('the turned clone draws its line', clone_line is not None and len(clone_line) == 3)
if clone_line:
    ra, rb = relation(leg, original_line), relation(clone, clone_line)
    # (a turn of 90 degrees about z takes (x, y, z) to (-y, x, z); the middle of the box of a turned body is not the turn of the middle, so the distances along the line are compared too)
    spans = [math.dist(original_line[k], original_line[k + 1]) - math.dist(clone_line[k], clone_line[k + 1]) for k in range(2)]
    check('the distances along the line are those of the original (worst %.5f mm)' % max(abs(s) for s in spans), max(abs(s) for s in spans) < 1e-6)
    check('the height of every point above the middle is the original\'s', all(abs(ra[k][2] - rb[k][2]) < 0.05 for k in range(3)),
          '%s %s' % ([round(r[2], 2) for r in ra], [round(r[2], 2) for r in rb]))

# 3b. a body of points: the copy shows a dot for each of its points, where the copy is (his words: "this goes for every point based object")
POINTS = '''
q1 = point(position=(var(-10), var(-10), var(-10)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
q2 = point(position=(var(10), var(-10), var(-10)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
q3 = point(position=(var(10), var(10), var(-10)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
q4 = point(position=(var(-10), var(10), var(-10)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
q5 = point(position=(var(-10), var(-10), var(10)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
q6 = point(position=(var(10), var(-10), var(10)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
q7 = point(position=(var(10), var(10), var(10)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
q8 = point(position=(var(-10), var(10), var(10)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
blob = morph_body([q1, q2, q3, q4, q5, q6, q7, q8])
twin = instance(blob, position=(var(0), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
twin = place(twin, position=(var(40), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
'''
ns = run(POINTS)
twin = ns['twin']
shown = twin._display() if callable(getattr(twin, '_display', None)) else None
dots = [s for s in shown if hasattr(s, 'xyz')] if isinstance(shown, (list, tuple)) else []
check('the copy of a body of points shows a dot for each of its eight points (%d)' % len(dots), len(dots) == 8)
if len(dots) == 8:
    xs = sorted(float(value_of(d.xyz[0])) for d in dots)
    check('the dots stand where the copy is (x from %.1f to %.1f, the copy is at 40 +- 10)' % (xs[0], xs[-1]), abs(xs[0] - 30) < 0.01 and abs(xs[-1] - 50) < 0.01)

# 3c. H174, his picture of 2026-11-14 ("the points of the object appeared in narnia instead of on the object"): a body that is NOT centred on the origin.  The copy of a body of points is the body in its own frame
# (its field moved by minus the middle of the points); its points have to be drawn in that frame too.  Every point of the copy is compared with where it must be: position + R S (point - middle)
CX, CY, CZ = 3.0, -4.0, 7.5
OFF = '\n'.join('o%d = point(position=(var(%g), var(%g), var(%g)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))' % (
    k + 1, CX + (5 if k & 1 else -5), CY + (5 if k & 2 else -5), CZ + (5 if k & 4 else -5)) for k in range(8)) + '\nblob = morph_body([o1, o2, o3, o4, o5, o6, o7, o8])\n'


def copy_points(twin):
    shown = twin._display() if callable(getattr(twin, '_display', None)) else None
    return sorted(tuple(round(float(value_of(c)), 6) for c in s.xyz) for s in (shown if isinstance(shown, (list, tuple)) else []) if hasattr(s, 'xyz'))


def expected_points(ns, position, angle_z=0.0, factor=1.0):
    orig = [tuple(float(value_of(c)) for c in ns['o%d' % (k + 1)].xyz) for k in range(8)]
    mid = tuple((min(p[i] for p in orig) + max(p[i] for p in orig)) / 2.0 for i in range(3))
    a = math.radians(angle_z)
    out = []
    for p in orig:
        x, y, z = [(p[i] - mid[i]) * factor for i in range(3)]
        out.append(tuple(round(v, 6) for v in (position[0] + x * math.cos(a) - y * math.sin(a), position[1] + x * math.sin(a) + y * math.cos(a), position[2] + z)))
    return sorted(out)


for label, call, position, angle, factor in (
    ('moved up by instance alone', 'twin = instance(blob, position=(var(3), var(-4), var(21.5)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))', (3, -4, 21.5), 0.0, 1.0),
    ('instance on the spot, then place', 'twin = instance(blob, position=(var(3), var(-4), var(7.5)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))\n'
     'twin = place(twin, position=(var(3), var(-4), var(35.5)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))', (3, -4, 35.5), 0.0, 1.0),
    ('turned 90 degrees about z and scaled by 2', 'twin = instance(blob, position=(var(10), var(5), var(0)), rotation=(var(0), var(0), var(90)), scale=(var(2), var(2), var(2)))', (10, 5, 0), 90.0, 2.0),
):
    ns = run(OFF + call + '\n')
    got, want = copy_points(ns['twin']), expected_points(ns, position, angle, factor)
    worst = max((abs(got[k][i] - want[k][i]) for k in range(len(got)) for i in range(3)), default=1e9) if len(got) == 8 else 1e9
    check('an off-centre body, %s: its eight points are where they must be (worst %.4f mm)' % (label, worst), len(got) == 8 and worst < 1e-4)
    # (his words, 2026-11-14: "The duplicate also doesnt sweep in real time, like the mirrors do and meshes differently ... Objects that dont mesh directly from the field should obviously clone the sweep": what the copy of
    # a body of points shows first is the body drawn NATIVELY -- the original's control points through the map of the copy -- as the image of it in a symmetry is, not a mesh of the field)
    items = ns['twin']._display()
    first = items[0] if isinstance(items, (list, tuple)) and items else None
    native = first is not None and hasattr(first, '_morph_vstart')
    check('an off-centre body, %s: the copy is drawn natively from the control points, as a mirror image is' % label, native)
    if native and getattr(first, '_morph_controls', None):
        pts = [tuple(float(value_of(c)) for c in p) for p in first._morph_controls]
        mid = tuple((min(p[i] for p in pts) + max(p[i] for p in pts)) / 2.0 for i in range(3))
        mids = tuple((min(q[i] for q in want) + max(q[i] for q in want)) / 2.0 for i in range(3))
        check('and its control points stand where the points of the copy do (middle %s, the points %s)' % (tuple(round(v, 2) for v in mid), tuple(round(v, 2) for v in mids)),
              all(abs(mid[i] - mids[i]) < 0.05 for i in range(3)))

# 4. a copy of a plain box has no parts: nothing is added to what it draws
ns = run('''
slab = box((10, 5, 2), position=(var(0), var(0), var(0)))
twin = instance(slab, position=(var(20), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
twin = place(twin, position=(var(30), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
''')
twin = ns['twin']
shown = twin._display() if callable(getattr(twin, '_display', None)) else None
check('the copy of a box draws nothing but itself', shown is None or not isinstance(shown, (list, tuple)) or len(shown) == 1)

print('FAILED: %s' % FAILED if FAILED else 'ALL CHECKS PASSED')
sys.exit(1 if FAILED else 0)
