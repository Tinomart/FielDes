# The kitchen example's exclusion on the whole import (90 parts): time it, and check the locked fields
import os, sys, time
from fieldes import *
from fieldes.stdlib import excluded as ex

here = os.path.dirname(os.path.abspath(__file__))
path = os.path.normpath(os.path.join(here, '..', '..', 'examples', 'step', 'Keukencombinatie.stp'))
t = time.time()
kitchen = import_step_parts(path)
print('import: %.1f s, %d parts' % (time.time() - t, len(kitchen)))
poor = [poor_fit_region(part, 1.0) for part, _ in kitchen]
region = union(*[f for f in poor if f is not None])
t = time.time()
out = exclude(kitchen, region)
print('exclude(kitchen, region): %.1f s' % (time.time() - t))
locked = [i for i, (p, _) in enumerate(out) if getattr(p, '_locks', None)]
print('%d of %d parts have a locked field: %s' % (len(locked), len(out), locked[:30]))
t = time.time()
out2 = exclude(kitchen)
print('exclude(kitchen) (each part its own poor fits): %.1f s' % (time.time() - t))
from fieldes.stdlib import cad_import
print('fields kept:', len(cad_import._surface_fields))
# mesh the first locked part to see the cost
if locked:
    i = locked[0]
    p, (lo, hi) = out[i]
    size = max(hi[k] - lo[k] for k in range(3))
    res = 100.0 / size
    t = time.time(); v, f = kitchen[i][0].get_mesh(lo, hi, res); a = time.time() - t
    t = time.time(); v2, f2 = p.get_mesh(lo, hi, res); b = time.time() - t
    print('part %d (%.0f mm): plain %.2f s (%d tris), excluded %.2f s (%d tris)' % (i, size, a, len(f), b, len(f2)))
