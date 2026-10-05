# Edit an imported part by dragging.
#
# Two ways of editing a part by dragging in the viewport work together:
#   handles  hover a surface of the part and drag it: the numbers that place that surface change
#            (for a part imported from STEP, a line `x = expose(x, [var(...), ...])` is written
#            with the numbers of its surfaces).  This is always there.
#   gizmo    arrows (move), rings (rotate) and squares (scale) at the part; the numbers you drag
#            are written into the var(...) calls of the line below.  Where it is shown, it has priority.
# The gizmo button of the model tree row (key E) says when the gizmo is shown: click (the default: while
# the part is selected), never, always -- E goes round them.  Try them.
# The lock button beside it (key R) is a switch of its own: `stand = lock(stand)` under the part, and
# nothing can be dragged until the line is deleted.
#
# The part here has its gizmo line.  Click the part and drag the gizmo's blue arrow: the third number moves.
#
from fieldes import *

parts = import_step_parts("step/MobileStand.step")
stand = parts[0][0]
view.set_bounds((-60, -80, -20), (140, 80, 90))
view.set_resolution(2)
view.set_quality(8)

stand = handles(stand, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)),
                scale=(var(1), var(1), var(1)))
stand
