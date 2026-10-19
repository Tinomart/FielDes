# 03: the insertion point of the model tree.
#
# WHAT THIS SHOWS.  What you add from the tree (a shape from the Primitives menu, a condition, a simulation, a Python block, a paste) no longer always goes to the end of the script:
# it goes to the INSERTION POINT, the line between two top-level rows (where the + that starts a section shows).  The point and the text cursor of the code editor are one.
#
# WHAT TO DO AND WHAT YOU MUST SEE.
#   1. Click on the thin line between `box_1` and `box_2` in the tree (on the line itself, a few pixels each side, away from the small circle: the circle still starts a section).
#      A line with a small arrow is drawn there, and the text cursor of the editor jumps to the line where `box_2` starts.
#   2. Right-click empty space in the viewport > Primitives > New 3D shape > sphere.  The sphere is written ABOVE `box_2`, under `box_1`.  Add a cylinder the same way: it lands under the sphere
#      (the point stays above `box_2`, so what is added next lands under what was added before).
#   3. Click into the script (any line of it, say inside the `sphere_1` lines): the arrow in the tree follows the cursor.  Inside a statement the point is after the statement; on the
#      first column of its first line it is before it; between statements it is above the next one.  Add a torus: it lands where the cursor is.
#   4. With no point set (the arrow faint, at the end of the tree) a new shape goes to the end of the script, as it always did.
#   5. Operations (union, offset ...) still go under the model they work on.

from fieldes import *

view.set_bounds([-10, -10, -5], [80, 30, 30])
view.set_resolution(3)
view.set_quality(8)

box_1 = box((0, 0, 0), (10, 10, 10))
box_1

box_2 = box((20, 0, 0), (30, 10, 10))
box_2

box_3 = box((40, 0, 0), (50, 10, 10))
box_3
