# Manufacturing 4: does the shell fill an undercut and put a draft on the walls?
#
# One constraint: nothing out of reach along z (undercut_along), and every wall needs a draft of 2 degrees (it must slope away from z).
# The part (back) and the result (front, coloured by what changed: red added):
#
# THE BLOCK (left).  A 30 mm cube with a groove cut into its side: x 0..8, y 5..25, z 12..18.  A tool or a mould half coming along z cannot
# reach inside it.  The shell should FILL the groove.
#   BY HAND: the groove is 8 x 20 x 6 = 960 mm^3.
#
# THE UPRIGHT (right).  A box 10 x 10 x 30 with plain vertical walls.  A wall with a draft of 2 degrees is wider at the foot than at the
# top by 30 x tan 2 degrees = 1.05 mm on each side (the top stays as it is).
#   BY HAND: the material that adds is the integral over the height of (10 + 2g)^2 - 10^2 with g growing from 0 at the top to 1.05 at the foot
#   = about 672 mm^3.   (The cube's own walls get the same draft: 30 x tan 2 = 1.05 mm at the foot; the shell adds that too.)
#
# The output pane prints what each remedy added and cut, and what the shell's own check still finds.
import math
from fieldes import *

view.set_bounds([-5, -50, -5], [75, 35, 35], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

block = difference(box((0, 0, 0), (30, 30, 30)), box((-1, 5, 12), (8, 25, 18)))
upright = box((45, 10, 0), (55, 20, 30))
part = union(block, upright)

t = math.tan(math.radians(2))
def draft_growth(side, height):
    return 4 * side * t * height ** 2 / 2 + 4 * t ** 2 * height ** 3 / 3
print('BY HAND: groove filled +%.0f mm^3; draft on the upright +%.0f mm^3, on the cube +%.0f mm^3' % (8 * 20 * 6, draft_growth(10, 30) + 0, draft_growth(30, 30) + 0))

constraint = manufacturing_constraint(undercut_along=(0, 0, 1), draft=2)
better = manufacturing_optimization(part, constraints=[constraint])
original = move(part, (0, -45, 0))
original
better
