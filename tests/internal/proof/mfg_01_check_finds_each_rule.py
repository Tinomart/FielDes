# Manufacturing 1: does the check find every broken rule, and only the broken ones?
#
# manufacturing_analysis should colour a part where it breaks a rule and nowhere else, and say where.  Two parts, side by side:
#
# THE BROKEN PART (back).  A base plate with seven features, each made to break ONE rule of the constraint below.  Where each break
# should be reported (x, y, z in mm):
#   min_wall 0.8       the fin, 0.6 thick, at about (10.3, 16, 15)
#   min_gap 0.8        the 0.3 mm slot between the two blocks, at about (35.15, 16, 12)
#   min_radius 3       every INSIDE corner: the L wall's corner at (64, 14) and every edge where a wall stands on the plate
#   overhang 45        the underside of the shelf, z = 25, x 14..34, y 45..75
#   undercut along z   the groove cut into the side of the big block, x 45..51, y 52..68, z 17..23
#   max_bridge 15      the underside of the roof, z = 25, between the piers, y 55..75 (a span of 20)
#   draft 2            every vertical wall (all of them are exactly vertical)
#
# THE CONTROL (front).  A part that keeps the wall, gap, overhang, bridge and undercut rules (thick walls, a wide gap, nothing sticking out
# over nothing).  The check should find NOTHING broken in it ("every rule is kept").  Its inside edges at the base are left out of the
# constraint (min_radius and draft are not asked of it).
#
# The output pane prints both reports.  The result card (right) switches the rule that colours the part.
from fieldes import *

view.set_bounds([-5, -105, -5], [110, 95, 50], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

plate = box((105, 90, 5), position=(52.5, 45, 2.5))
fin = box((0.6, 12, 20), position=(10.3, 16, 15))
gap_a = box((10, 12, 15), position=(30, 16, 12.5))
gap_b = box((10, 12, 15), position=(40.3, 16, 12.5))
l_long = box((30, 4, 20), position=(75, 12, 15))
l_short = box((4, 30, 20), position=(62, 25, 15))
wall = box((4, 30, 35), position=(12, 60, 22.5))
shelf = box((20, 30, 3), position=(24, 60, 26.5))
block = difference(box(30, position=(60, 60, 20)), box((7, 16, 6), position=(47.5, 60, 20)))
pier_a = box((10, 10, 20), position=(85, 50, 15))
pier_b = box((10, 10, 20), position=(85, 80, 15))
roof = box((10, 40, 4), position=(85, 65, 27))
broken = union(plate, fin, gap_a, gap_b, l_long, l_short, wall, shelf, block, pier_a, pier_b, roof)

control = move(union(box((60, 40, 5), position=(30, 20, 2.5)), box((15, 20, 20), position=(17.5, 20, 15)), box((15, 20, 20), position=(42.5, 20, 15))), (0, -70, 0))

everything = manufacturing_constraint(overhang=45, min_wall=0.8, min_gap=0.8, max_bridge=15, min_radius=3, draft=2, undercut_along=(0, 0, 1))
control_rules = manufacturing_constraint(overhang=45, min_wall=0.8, min_gap=0.8, max_bridge=15, undercut_along=(0, 0, 1))

print('=== THE CONTROL (nothing should be broken):')
print(manufacturing_analysis(control, constraints=[control_rules]))
print('=== THE BROKEN PART (the places listed at the top):')
report = manufacturing_analysis(broken, constraints=[everything])
print(report)
control
report
