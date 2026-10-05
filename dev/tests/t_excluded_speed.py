# How long exclude() takes on STEP parts, and what the locked field costs to mesh compared with the import's own field
import os, sys, time
from fieldes import *
from fieldes.stdlib import excluded as ex

here = os.path.dirname(os.path.abspath(__file__))
for name in ('Frying Pan.STEP', 'ShaftSupportStand.STEP', 'MobileStand.step', 'PivotBearingSupportBracket.STEP'):
    path = os.path.normpath(os.path.join(here, '..', '..', 'examples', 'step', name))
    parts = import_step_parts(path)
    part, (lo, hi) = parts[0]
    size = max(hi[i] - lo[i] for i in range(3))
    mid = tuple(0.5 * (lo[i] + hi[i]) for i in range(3))
    res = 60.0 / size * 2          # about 120 cells across the part
    region = sphere(0.35 * size, mid)
    t = time.time()
    e = exclude(part, region)
    t_ex = time.time() - t
    lock_mesh = ex.locks_of(e)[0][0]
    t = time.time()
    exclude(part, region)
    t_again = time.time() - t
    box = (tuple(l - 1 for l in lo), tuple(h + 1 for h in hi))
    t = time.time(); v0, f0 = part.get_mesh(box[0], box[1], res); t_plain = time.time() - t
    t = time.time(); v1, f1 = e.get_mesh(box[0], box[1], res); t_excl = time.time() - t
    print('%-32s size %5.0f mm: exclude %.2f s (again %.2f s); meshed at %.2f/mm: plain %.2f s (%d tris), excluded %.2f s (%d tris)' %
          (name, size, t_ex, t_again, res, t_plain, len(f0), t_excl, len(f1)))
