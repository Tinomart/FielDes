# Manufacturing 8: second pass, the budget, and a region that is kept.
#
# The ledge of mfg_03 (a wall with a slab sticking out) four times, asking overhang 45.  WHAT EACH SHOULD DO:
#   first   the shell as it is, allowed to change 60 % of the part: a gusset under the slab (5083 mm^3 by hand for the smallest 45 degree one, see mfg_03).
#   second  the RESULT of the first, given to the shell again: a part that already keeps the rule should come out UNCHANGED, about 0 added.
#   small   max_change=0.05: the gusset would change more than 5 % of the part, so it should NOT be made, and the output says why.
#   kept    keep=[a box under the slab]: what is in a kept region is never touched, so NO gusset should grow there, and the overhang is
#           reported as still broken.
# The output pane prints the four results in this order.  The viewport shows the original (back) and the first pass (front).
from fieldes import *

view.set_bounds([-5, -55, -5], [45, 45, 45], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

ledge = union(box((0, 0, 0), (10, 40, 40)), box((10, 0, 20), (30, 40, 25)))
constraint = manufacturing_constraint(overhang=45)

first = manufacturing_optimization(ledge, constraints=[constraint], max_support_height=25, max_change=0.6)
print('=== first'); print(first)
second = manufacturing_optimization(first, constraints=[constraint], max_support_height=25, max_change=0.6)
print('=== second (the result of the first, given to the shell again)'); print(second)
small = manufacturing_optimization(ledge, constraints=[constraint], max_support_height=25, max_change=0.05)
print('=== small (max_change=0.05)'); print(small)
kept = manufacturing_optimization(ledge, constraints=[constraint], max_support_height=25, max_change=0.6, keep=[box((10, 0, 5), (31, 41, 20))])
print('=== kept (nothing may change under the slab)'); print(kept)
original = move(ledge, (0, -50, 0))
original
first
