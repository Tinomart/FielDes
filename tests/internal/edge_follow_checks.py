'''The live terms of an edge, headless: a stand-in for the host of the program (its list of the numbers of the script and its retune), so that `edge_at` makes the terms it makes in the window.'''
import math
import sys
import time
import types

host = types.ModuleType('_fieldes_host')
setattr(host, '__vars', [])


def retune(v, value):
    entries = getattr(host, '__vars')
    for i, e in enumerate(entries):
        if e[0] is v:
            entries[i] = (v, float(value), e[2])


setattr(host, '__retune', retune)
sys.modules['_fieldes_host'] = host

from fieldes import *
from fieldes.shape import Shape
from fieldes.stdlib import features as F


def number(value):
    v = Shape.var()
    getattr(host, '__vars').append((v, float(value), (1, 1, 0, 1)))
    return v


def at(expression_point):
    return tuple(evaluate(c, [(0.0, 0.0, 0.0)])[0] for c in expression_point.xyz)


bad = []


def check(label, ok, extra=''):
    print(('ok   ' if ok else 'FAIL ') + label + ((' [' + str(extra) + ']') if extra != '' and not ok else ''))
    if not ok:
        bad.append(label)


# ---- a cylinder whose radius is a number of the script: its rim, a circle
r = number(5.0)
h = number(10.0)
cyl = cylinder(r, h, position=(0, 0, h / 2))          # (a model stands by its position, its middle: from z = 0 to z = h)
t = time.time()
e = edge_at(cyl, (5, 0, 10))
print('edge_at of the rim: %.2f s, %d points, circle %s' % (time.time() - t, len(e.points), getattr(e, '_edge_circle', None) is not None))
radii = lambda: [math.hypot(at(p)[0], at(p)[1]) for p in e.points]
tops = lambda: [at(p)[2] for p in e.points]
check('the points of the rim are on the radius 5 now', all(abs(x - 5.0) < 0.05 for x in radii()), radii())
retune(r, 6.0)
check('the radius number moved to 6: the points of the rim are on the radius 6 (they follow)', all(abs(x - 6.0) < 0.1 for x in radii()), radii())
retune(r, 5.0)
retune(h, 12.0)
check('the height number moved to 12: the points of the rim are at the height 12', all(abs(x - 12.0) < 0.1 for x in tops()), tops())
retune(h, 10.0)

# ---- a straight edge of a box whose side is a number
w = number(10.0)
b = box((w, 10, 10), position=(w / 2, 5, 5))
t = time.time()
line = edge_at(b, (10, 10, 5))
print('edge_at of a box edge: %.2f s' % (time.time() - t))
ends = lambda: [at(p) for p in line.points]
check('the ends of the box edge are at x = 10 now', all(abs(p[0] - 10.0) < 0.05 for p in ends()), ends())
retune(w, 14.0)
check('the side number moved to 14: the ends follow (x = 14)', all(abs(p[0] - 14.0) < 0.1 for p in ends()), ends())
retune(w, 10.0)

# ---- an edge that is neither a line nor a circle: a cylinder minus a sphere, the sphere's radius a number
s = number(6.0)
diff = difference(cylinder(5, 10, position=(0, 0, 5)), sphere(s, position=(3, 0, 8)))
t = time.time()
curve = edge_at(diff, (5, 0, 2.34))
print('edge_at of the curve of a cylinder and a sphere: %.2f s, %d points' % (time.time() - t, len(curve.points)))
sphere_distance = lambda: [math.dist(at(p), (3, 0, 8)) for p in curve.points]
check('the curve is on the sphere of radius 6 now', all(abs(x - 6.0) < 0.15 for x in sphere_distance()), sphere_distance()[:6])
retune(s, 6.5)
moved = sphere_distance()
zs = [at(p)[2] for p in curve.points]
stay = [round(z, 2) for d, z in zip(moved, zs) if abs(d - 6.5) > 0.3]
print('   points that do not follow the sphere (their heights):', stay, 'of', len(moved))
check('the sphere number moved to 6.5: the curve follows it (every point of it that is not at the top of the cylinder, where the sphere does not reach, is on the sphere of radius 6.5 within 0.3)', all(z > 9.0 for z in stay) and len(stay) < 0.3 * len(moved), stay)
retune(s, 6.0)

print('FAILED:' if bad else 'ALL PASSED', bad)
