# Topology optimization: the stiffest bracket that uses 50 % of the material.
#
# The imported bracket is the design space: the optimizer keeps the material that carries the
# 2 kN pull at its lugs from the bolted plates and removes the rest.  Supports and loads work
# as in 05_static_analysis.py.  About 40 analyses run; an unchanged problem is cached, so running
# the script again is instant.  The result on its own shows the optimized part, and the result card
# steps through the iterations: the part as it was after each one (play to watch it being found).
#
# The pull acts on the round holes of the two lugs, and those must stay as they are: right after the
# import the holes are excluded -- each whole hole with the ring of material round it, as a region of its
# own (exclude(space, holes)).  What is excluded is locked -- inside the region the part is its own exact
# surface from the STEP file, which nothing done to it afterwards changes -- and an optimization reads it
# as a region to KEEP, by itself: the holes and their rings stay intact however much of the rest is taken
# away.  (A region has to enclose the whole feature: one that cuts through the ring would leave it torn open
# there.)  The kept rings use a part of the volume asked for, so the free material has less than the 50 %:
# if the bracket falls into pieces, raise volume_fraction -- the loose scraps show what it would become.
#
from fieldes import *

parts = import_model("step/PivotBearingSupportBracket.STEP")
space, (lo, hi) = parts[0]
# hidden: space

view.set_bounds(*roi(parts, ((-75.01, -85.07, -60.02), (75.01, 35.06, 28))))
view.set_resolution(roi_resolution(parts, ((-75.01, -85.07, -60.02), (75.01, 35.06, 28))))
view.set_quality(8)

plates = union(box_exact((-76, 5, -61), (-45, 36, 29)),
               box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))      # the lug ends: the pull acts here
# hidden: lugs

# the two lug holes, each with its whole ring of material (the holes are 17 mm across, the rings 36 mm, along x)
holes = union(box_exact((-48, -88, -22), (-26, -44, 22)),
              box_exact((26, -88, -22), (48, -44, 22)))
# hidden: holes
design = exclude(space, holes)                        # ... which stay as they are
# hidden: design

conditions = static_boundary_conditions(design, supports=[fixed(plates)],
                                        loads=[force(lugs, (0, -2000, 0))])
# hidden: conditions
opt = topology_optimization(design, conditions, material=aluminium, volume_fraction=0.50, element_size=5)
opt

optimized = opt.shape()                       # where the density is above the level that keeps the 50 % asked for
# hidden: optimized
check = opt.verify()                          # a static analysis of the result
# hidden: check
print(check)
                                          # shown: the optimized part, iteration by iteration (check: its stresses)
