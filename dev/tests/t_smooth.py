# smooth(): a bump and a dent smaller than the radius go, a flat face stays, the body does not grow
import time
from fieldes import *

box = box_exact((0, 0, 0), (30, 30, 20))
bump = cylinder_z(2.0, 3.0, (15, 15, 19.0))           # a 2 mm radius bump, 2 mm above the top face (z 20..22)
dent = cylinder_z(1.5, 3.0, (8, 8, 18.0))             # a 1.5 mm radius, 2 mm deep dent into the top face
part = difference(union(box, bump), dent)
part._bounds = ((0, 0, 0), (30, 30, 22))

def top(s, x, y):
    ''' the height of the top surface at (x, y): the highest z where the field is still negative '''
    zs = [16 + 0.05 * k for k in range(161)]
    vals = evaluate(s, [(x, y, z) for z in zs])
    inside = [z for z, v in zip(zs, vals) if v < 0]
    return max(inside) if inside else float('nan')

print('before: top at the bump %.2f, in the dent %.2f, flat %.2f' % (top(part, 15, 15), top(part, 8, 8), top(part, 25, 25)))
vols = {}
for steps in (1, 3):
    t0 = time.time()
    s = smooth(part, 2.0, steps=steps)
    print('steps %d (%.1f s): top at the bump %.2f, in the dent %.2f, flat %.2f, far side face x=30: %s' % (
        steps, time.time() - t0, top(s, 15, 15), top(s, 8, 8), top(s, 25, 25),
        ['%.3f' % v for v in evaluate(s, [(30, 15, 10), (30, 5, 5), (0, 15, 10)])]))
    # the body's size: its extent along x at mid height, and its volume against the original's
    xs = [-3 + 0.05 * k for k in range(721)]
    ins = [x for x, v in zip(xs, evaluate(s, [(x, 15, 10) for x in xs])) if v < 0]
    print('   extent along x at mid height %.2f .. %.2f (the box: 0 .. 30)' % (min(ins), max(ins)))
    vols[steps] = volume_of(s, (-2, -2, -2), (32, 32, 25), 2.0)
vols[0] = volume_of(part, (-2, -2, -2), (32, 32, 25), 2.0)
print('volume: original %.0f, 1 step %.0f, 3 steps %.0f mm^3' % (vols[0], vols[1], vols[3]))
for bad in ((dict(radius=0)), dict(radius=2, steps=0)):
    try:
        smooth(part, **bad)
        print('NO ERROR for', bad)
    except ValueError as e:
        print('refused', bad, '->', e)
