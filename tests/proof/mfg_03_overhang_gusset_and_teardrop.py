# Manufacturing 3: does the shell support an overhang at the angle asked, and cut a teardrop over a hole?
#
# Two overhangs in one part, each with its own constraint and region= box.  The part (back) and the result (front, coloured by what changed):
#
# THE LEDGE (left).  A wall 10 thick and 40 high with a slab sticking out of it, 20 long (x 10..30), 5 thick, 20 up.  Its underside leans out
# 90 degrees from the vertical; the rule says overhang 45.  overhang_fix='fill' should grow a GUSSET under the slab, covering the whole
# underside, with every face of it leaning at most 45 degrees from the vertical.  The smallest such body is a hip shape: it slopes up at 45
# degrees from the tip of the slab and ALSO from its two side edges (y = 0 and y = 40), and is deepest in the middle.
#   BY HAND: at a point (x, y) of the underside the gusset is as deep as the nearest edge allows: min(x, 30 - x, y, 40 - y) (x counted from the
#   wall's far side, 0 to 30).  Integrated over the slab's footprint that is 5083 mm^3.  (A straight prism, the 45 degree triangle 20 x 20 / 2
#   along the full 40 mm, would be 8000 mm^3: an upper bound, more than needed.)
#
# THE HOLE (right).  A block 40 x 20 x 30 with a round hole of radius 5 lying on its side, 20 mm long.  The top of a horizontal hole is an
# overhang too.  overhang_fix='cut' should cut the air above the hole into a TEARDROP: two 45 degree faces meeting in a point above it.
#   BY HAND: the area between the circle and the two tangent faces is (1 - pi/4) r^2 = 5.37 mm^2 per mm of length; 20 mm long = 107 mm^3 cut.
#
# The output pane prints what each remedy added and cut, and what the shell's own check still finds broken.
import math
from fieldes import *

view.set_bounds([-5, -55, -5], [95, 45, 45], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

ledge = union(box((0, 0, 0), (10, 40, 40)), box((10, 0, 20), (30, 40, 25)))
block = box((50, 0, 0), (90, 20, 30))
hole = rotate_x(cylinder(5, 30, (70, 15, -25)), math.pi / 2)
part = union(ledge, difference(block, hole))

print('BY HAND: gusset +5083 mm^3 (a straight prism would be 8000), teardrop -%.1f mm^3' % ((1 - math.pi / 4) * 25 * 20))

on_the_ledge = manufacturing_constraint(overhang=45, overhang_fix='fill', region=box((-1, -1, -1), (31, 41, 41)))
over_the_hole = manufacturing_constraint(overhang=45, overhang_fix='cut', region=box((49, -1, -1), (91, 21, 31)))
better = manufacturing_optimization(part, constraints=[on_the_ledge, over_the_hole], max_support_height=25, max_change=0.6)
original = move(part, (0, -50, 0))
original
better
