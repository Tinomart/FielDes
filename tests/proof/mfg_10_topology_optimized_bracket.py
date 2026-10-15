# Manufacturing 10: a part that is hard to print -- the topology-optimized bracket of example 08.
#
# The other scripts use parts whose faults can be seen at a glance.  This one is the kind of part where they cannot: a bracket whose shape was
# found by an optimizer (topology_optimization, 50 % of the material, a 2 kN pull at the lugs), which makes struts of every thickness, leaning
# every way, with voids between them.  Nobody drew it, and nobody can say by eye which strut is thinner than a nozzle makes, which one leans too
# far to print, or whether a gap is narrower than the nozzle.  That is what the tools are for.
#
# What the script does:
#   1. imports the bracket and finds the stiffest shape that keeps half of its material (the same call as example 08; about 40 analyses --
#      the first run takes minutes, the same script again reads them back);
#   2. takes the exact distance of that shape (the density it comes from is not a distance, and the walls and gaps are measured with one);
#   3. checks it for a printer with a 0.4 mm nozzle that also asks for no overhang steeper than 45 degrees (manufacturing_analysis): the report says
#      how much of the surface breaks each rule, how badly, and where;
#   4. changes it (manufacturing_optimization): thin struts thickened, narrow gaps closed, overhangs filled by a gusset, up to 25 % of the part;
#      the viewport shows the geometry change (grey as it was, red material added, blue material cut) on the part itself, in front, and the
#      optimized bracket as it came out of the optimizer behind it.
#
# There is NO answer worked out by hand: judge it.  Is what the report flags a strut that is thin, a place that leans out?  Is anything flagged
# that is not a problem, or missing that is one?  How much material did the shell add, and where?  Run manufacturing_analysis on `better` to check
# it again.  (A part of this kind is slow to evaluate: expect minutes.)
from fieldes import *

parts = import_model("../../examples/step/PivotBearingSupportBracket.STEP")
space, (lo, hi) = parts[0]
# hidden: space

view.set_bounds(*roi(parts, ((-75.01, -85.07, -60.02), (75.01, 35.06, 28))), auto_scale=True)
view.set_resolution(roi_resolution(parts, ((-75.01, -85.07, -60.02), (75.01, 35.06, 28))))
view.set_quality(8)

plates = union(box((-76, 5, -61), (-45, 36, 29)),
               box((45, 5, -61), (76, 36, 29)))
lugs = box((-60, -90, -61), (60, -60, 29))      # the lug ends: the pull acts here
# hidden: lugs
holes = union(box((-48, -88, -22), (-26, -44, 22)),
              box((26, -88, -22), (48, -44, 22)))
# hidden: holes
design = exclude(space, holes)                    # the lug holes and their rings stay as they are
# hidden: design

support = fixed(plates)
load = force(lugs, (0, -2000, 0))
opt = topology_optimization(design, supports=[support], loads=[load], material=aluminium, volume_fraction=0.50, element_size=5)
# hidden: opt
optimized = opt.shape()
# hidden: optimized

solid = exact_distance(optimized)
# hidden: solid

printer = fff_constraint(nozzle=0.4, overhang=45)
report = manufacturing_analysis(solid, constraints=[printer])
print(report)
better = manufacturing_optimization(solid, constraints=[printer], max_change=0.25, max_support_height=10)
print(better)
better
