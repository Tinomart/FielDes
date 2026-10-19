# 05: the part is drawn the way it is printed.
#
# WHAT THIS SHOWS.  A plate on a post, written plate-on-top: printed that way the plate would hang in the air.  The constraint leaves the way up of the print FREE
# (build_direction=None), so the tool looks for the way that needs no support: the plate at the bottom.  Since 1.0.0 the result is DRAWN standing that way -- the part, the colours of
# the change, the supports and the boxes turned together, z up -- and the card says so.
#
# WHAT YOU MUST SEE.
#   - the output pane: `way up found: (0.0, 0.0, -1.0)` (the print stands the part on its plate: up is -z of the script) and the tool's report, `0 mm3 added, 0 mm3 removed`
#     (nothing needs to change once the part is turned: the plate lies on the bed);
#   - the viewport: the plate at the BOTTOM and the post on it, though the script puts the plate at z = 30 .. 35 above the post;
#   - the card (bottom right): "Way up of the print (0, 0, -1)   part shown turned".
# Then change `manufacturing_optimization(...)` below by adding `show_turned=False` and run again: the part is drawn where the script put it (the plate on top), the card has no
# "part shown turned", and the way up is still reported.  `res.oriented()` is the part turned, ready to export, in either case.

from fieldes import *

post = box((0, 0, 0), (10, 10, 30))
plate = box((-20, -20, 30), (30, 30, 35))
part = union(post, plate)
free = manufacturing_constraint(overhang=45, build_direction=None)
res = manufacturing_optimization(part, constraints=[free], max_change=0.9)
view.set_bounds([-30, -30, -40], [40, 40, 40])
view.set_resolution(2)
view.set_quality(8)
print('way up found:', tuple(round(c, 3) for c in res.build_direction))
res
