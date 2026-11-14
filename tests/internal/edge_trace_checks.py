import math, sys, time, traceback
from fieldes import *

bad = []
def check(label, ok, extra=''):
    print(('ok   ' if ok else 'FAIL ') + label + ((' [' + str(extra) + ']') if extra and not ok else ''))
    if not ok:
        bad.append(label)

def verts(e):
    return [tuple(float(c) for c in v) for v in e.vertices]

def worst(vs, fn):
    return max(abs(fn(v)) for v in vs)

try:
    diff = difference(cylinder(5, 10, position=(0, 0, 5)), sphere(6, position=(3, 0, 8)))          # (a model stands by its position, its middle: the cylinder is from z = 0 to z = 10)
    t = time.time()
    e = edge_at(diff, (5, 0, 2.34))
    dt = time.time() - t
    vs = verts(e)
    print('cylinder minus sphere: closed=%s n=%d circle=%s %.2fs' % (e.closed, len(vs), getattr(e, '_edge_circle', None), dt))
    # (the sphere touches the cylinder's wall only where x is at least -1/3: the curve is an arc between its two tangent places, an open curve -- the crease fades out there)
    check('cyl - sphere: an open curve between the two places where the sphere touches the wall', not e.closed)
    check('cyl - sphere: the points lie on the cylinder (r 5)', worst(vs, lambda v: math.hypot(v[0], v[1]) - 5.0) < 0.1, worst(vs, lambda v: math.hypot(v[0], v[1]) - 5.0))
    sph = lambda v: math.dist(v, (3, 0, 8)) - 6.0
    check('cyl - sphere: the points lie on the sphere', worst(vs, sph) < 0.1, worst(vs, sph))
    check('cyl - sphere: it runs from one tangent place to the other (x from about -0.33 to 5)', min(v[0] for v in vs) < -0.2 and max(v[0] for v in vs) > 4.9, (min(v[0] for v in vs), max(v[0] for v in vs)))
    check('cyl - sphere: took under 20 s', dt < 20, dt)
except Exception:
    traceback.print_exc(); bad.append('cyl - sphere raised')

try:
    c1 = cylinder(4, 20, position=(0, 0, 10))
    c2 = cylinder(3, 30, position=(0, 0, 10), rotation=(0, 90, 0))
    cross = union(c1, c2)
    t = time.time()
    e = edge_at(cross, (4, 0, 13.0))
    dt = time.time() - t
    vs = verts(e)
    print('crossing cylinders (concave saddle): closed=%s n=%d %.2fs' % (e.closed, len(vs), dt))
    check('crossing cylinders: a closed curve', bool(e.closed))
    f1 = lambda v: math.hypot(v[0], v[1]) - 4.0
    f2 = lambda v: math.hypot(v[1], v[2] - 10.0) - 3.0
    check('crossing cylinders: on cylinder 1', worst(vs, f1) < 0.1, worst(vs, f1))
    check('crossing cylinders: on cylinder 2', worst(vs, f2) < 0.1, worst(vs, f2))
    check('crossing cylinders: took under 20 s', dt < 20, dt)
except Exception:
    traceback.print_exc(); bad.append('crossing cylinders raised')

try:
    base = box((20, 20, 5), position=(0, 0, -2.5))
    u = union(base, cylinder(4, 10, position=(0, 0, 5)))
    e = edge_at(u, (4, 0, 0))
    check('union bottom rim is still the exact circle', getattr(e, '_edge_circle', None) is not None)
except Exception:
    traceback.print_exc(); bad.append('union raised')

try:
    f = fillet(edge_at(diff, (5, 0, 2.34)), 0.5)
    check('fillet takes the traced edge (no error)', f is not None)
    c = chamfer(edge_at(diff, (5, 0, 2.34)), 0.5)
    check('chamfer takes the traced edge (no error)', c is not None)
except Exception:
    traceback.print_exc(); bad.append('fillet of a traced edge raised')
print('FAILED:' if bad else 'ALL PASSED', bad)
