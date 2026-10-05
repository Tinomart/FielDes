# Example 08: with the lug ends excluded, the optimised bracket keeps the lug region as the part has it (the round
# holes intact); without the exclusion the optimiser eats into it.  Compares inside / outside at points of the lug box.
import os
import random
import sys
import time

from fieldes import *

here = os.path.dirname(os.path.abspath(__file__))
parts = import_step_parts(os.path.normpath(os.path.join(here, '..', '..', 'examples', 'step', 'PivotBearingSupportBracket.STEP')))
space, (lo, hi) = parts[0]
plates = union(box_exact((-76, 5, -61), (-45, 36, 29)), box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))
holes = union(box_exact((-48, -88, -22), (-26, -44, 22)), box_exact((26, -88, -22), (48, -44, 22)))

random.seed(7)
pts = []
while len(pts) < 4000:
    p = (random.choice((-1, 1)) * random.uniform(26, 48), random.uniform(-88, -44), random.uniform(-22, 22))
    if abs(space(*p)) < 6.0:               # near the part's surface: where holes and lug faces are
        pts.append(p)

for label, shape in (('plain', space), ('lug ends excluded', exclude(space, holes))):
    cond = static_boundary_conditions(shape, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))])
    t = time.time()
    opt = topology_optimization(shape, cond, material=aluminium, volume_fraction=0.45, element_size=5)
    s = opt.shape()
    differ = sum(1 for p in pts if (s(*p) < 0) != (space(*p) < 0))
    print('%-20s %.0f s: of %d points near the lug surfaces, %d (%.1f %%) are not as the part has them' %
          (label, time.time() - t, len(pts), differ, 100.0 * differ / len(pts)))
