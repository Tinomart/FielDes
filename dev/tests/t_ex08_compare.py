# Example 08 as a whole, with and without the lug holes excluded: the optimisation itself (compliance, volume, the stress
# check of the result), not just the region
import os, time
from fieldes import *

here = os.path.dirname(os.path.abspath(__file__))
parts = import_step_parts(os.path.normpath(os.path.join(here, '..', '..', 'examples', 'step', 'PivotBearingSupportBracket.STEP')))
space, (lo, hi) = parts[0]
plates = union(box_exact((-76, 5, -61), (-45, 36, 29)), box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))
holes = union(box_exact((-48, -88, -22), (-26, -44, 22)), box_exact((26, -88, -22), (48, -44, 22)))
vol_space = volume_of(space, lo, hi, 1.0)
print('the part: %.0f mm3' % vol_space)
for label, shape in (('plain', space), ('holes excluded', exclude(space, holes))):
    cond = static_boundary_conditions(shape, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))])
    t = time.time()
    opt = topology_optimization(shape, cond, material=aluminium, volume_fraction=0.45, element_size=5)
    dt = time.time() - t
    s = opt.shape()
    v = volume_of(s, lo, hi, 1.0)
    chk = opt.verify()
    print('%-15s %5.1f s  %d iterations  compliance %.4g -> %.4g  volume %.0f mm3 (%.0f %% of the part, asked 45 %%)  [%s]' %
          (label, dt, opt.iterations, opt.compliance[0], opt.compliance[-1], v, 100 * v / vol_space,
           str(chk).replace(chr(10), ' / ')[:160]))
    print('    compliance by iteration:', ' '.join('%.3g' % c for c in opt.compliance[::6]))
