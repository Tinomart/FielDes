# Topology optimization: the stiffest bracket that uses 45 % of the material.
#
# The imported bracket is the design space: the optimizer keeps the material that carries the
# 2 kN pull at its lugs from the bolted plates and removes the rest.  Supports and loads work
# as in 05_static_analysis.py.  About 40 analyses run; an unchanged problem is cached, so running
# the script again is instant.
#
from fieldes import *

parts = import_step_parts("step/PivotBearingSupportBracket.STEP")
space, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

plates = union(box_exact((-76, 5, -61), (-45, 36, 29)),
               box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))

opt = topology_optimization(space, supports=[fixed(plates)],
                            loads=[force(lugs, (0, -2000, 0))],
                            material=aluminium, volume_fraction=0.45, element_size=5)
print(opt)

optimized = opt.shape()                       # where the density is above the level that keeps the 45 % asked for
check = opt.verify()                          # a static analysis of the result
print(check)
check.show("von_mises")
