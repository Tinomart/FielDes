# Manufacturing: what a printer cannot make of a part, where, and the part changed so that it can.
#
# The part is a plate with a thin fin (0.6 mm: a printer with a 0.4 mm nozzle needs 0.8) and a ledge that sticks out over nothing.
#
#   fff_constraint(nozzle, overhang=)        what a printer can make: the thinnest wall, the narrowest gap, the longest bridge -- and, when it is
#                                            asked for (overhang=45), the steepest a surface that faces down may lean without a support
#   manufacturing_analysis(part, constraints=[...])
#                                            the CHECK: the part coloured where it breaks a rule (the result card switches the rule), and in the
#                                            output pane how much of the surface breaks each, how badly, and where
#   manufacturing_optimization(part, constraints=[...])
#                                            the SHELL: the part CHANGED until it keeps the rules -- the fin thickened, a gusset grown under the
#                                            ledge at the angle asked -- coloured by what changed (red added, blue cut), with how much of the part
#                                            that was and what is still broken.  It is a body: wherever a body goes it goes
#
# Both read the part's own field: every rule is a field on the part, and nothing is meshed.  The numbers (how much of the surface, where) are measured
# on a few thousand points of the surface.  It is a check of the part, and not a substitute for the check of the one who makes it.
#
# Things to try: nozzle=0.6 asks for 1.2 mm walls; leave overhang off and the ledge is not asked about (a slicer puts a support there); overhang_fix='cut'
# on a constraint with a region= turns a channel's ceiling into a teardrop; max_change=0.05 stops the gusset (it changes more than that of the part), and
# support='block' on the constraint then puts a block under the ledge instead (better.supports, a body of its own; export_print(better, "part.3mf") writes the
# part, the supports and no_support= volumes as separate objects, export_layers(better, "layers.dxf") the contours of each layer).
from fieldes import *

view.set_bounds([-30, -5, -5], [50, 45, 35], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

plate = box((0, 0, 0), (40, 40, 10))
fin = box((15, 15, 10), (15.6, 25, 30))
post = box((25, 15, 10), (30, 25, 30))
wall = box((-1, 0, 0), (0, 40, 40))
ledge = box((-20, 0, 20), (0, 40, 25))
part = union(union(union(plate, fin), post), union(wall, ledge))

printer = fff_constraint(nozzle=0.4, overhang=45)

report = manufacturing_analysis(part, constraints=[printer])
better = manufacturing_optimization(part, constraints=[printer], max_support_height=25)
better
