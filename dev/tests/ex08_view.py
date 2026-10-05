# Example 08's optimised bracket, close up at the lug ends (the exclusion region), to look at the holes
from fieldes import *

parts = import_step_parts("../../examples/step/PivotBearingSupportBracket.STEP")
space, (lo, hi) = parts[0]
plates = union(box_exact((-76, 5, -61), (-45, 36, 29)), box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))
holes = union(box_exact((-48, -88, -22), (-26, -44, 22)), box_exact((26, -88, -22), (48, -44, 22)))
design = exclude(space, holes)
conditions = static_boundary_conditions(design, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))])
opt = topology_optimization(design, conditions, material=aluminium, volume_fraction=0.45, element_size=5)
optimized = opt.shape()

view.set_bounds((-52, -92, -26), (52, -40, 26))
view.set_resolution(3)
view.set_quality(8)
optimized
