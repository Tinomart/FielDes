# smooth() on a box with a region excluded, at the default resolution: how long, and is the result still right
# (a flat face stays, the excluded part is untouched)?  Evaluates the result so the timing includes making it.
import time
from fieldes import *

box = box_exact((0, 0, 0), (10, 10, 10))
region = sphere(4, (10, 5, 5))
ex = exclude(box, region)
probe = [(5, 5, 10.0), (5, 5, 9.0), (5, 5, 11.0), (0.0, 0.0, 5.0), (0.3, 0.3, 9.7), (-0.5, 5, 5)]

for label, shape in (('plain box', box), ('box with a region excluded', ex)):
    for r, steps in ((1.0, 3), (0.5, 3), (0.25, 2)):
        t = time.time()
        s = smooth(shape, r, steps)
        v = evaluate(s, probe)
        print('%-28s smooth(r=%.2f, steps=%d): %6.2f s   probes %s' % (label, r, steps, time.time() - t, ['%.3f' % x for x in v]))
# the excluded part is exactly what it was: inside the region the field is the box's
inner = [(10 - 1.0, 5, 5), (10 + 1.0, 5, 5), (10 + 3.0, 5, 5), (10, 5 + 3.5, 5)]
s = smooth(ex, 1.0, 3)
print('inside the region, before:', ['%.3f' % x for x in evaluate(ex, inner)])
print('inside the region, after: ', ['%.3f' % x for x in evaluate(s, inner)])
