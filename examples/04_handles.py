# Edit an imported part by dragging.
#
# The model tree has a handles button on every shape and part; each click goes to the next of
# three ways of editing it in the viewport:
#   gizmo    arrows (move), rings (rotate) and squares (scale) at the part; the numbers you drag
#            are written into the var(...) calls of the line below
#   handles  hover a surface of the part and drag it: the numbers that place that surface change
#            (for a part imported from STEP, a line `x = expose(x, [var(...), ...])` is written
#            with the numbers of its surfaces)
#   lock     nothing can be dragged
#
# The part here starts with a gizmo.  Drag its blue arrow and the third number moves.
#
from fieldes import *

parts = import_step_parts("step/MobileStand.step")
stand = parts[0][0]
view.set_bounds((-60, -80, -20), (140, 80, 90))
view.set_resolution(2)
view.set_quality(8)

stand = handles(stand, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)),
                scale=(var(1), var(1), var(1)), mode="gizmo")
stand
