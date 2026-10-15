# Manufacturing 1b: does the report's "how much of the surface" match the geometry?
#
# The report says what share of the part's surface breaks each rule.  This part is simple enough to work that share out by hand.
#
# THE PART.  A plate 40 x 40 x 10 with one thin fin standing on it: 0.6 mm thick, 10 mm long, 20 mm high.  The rule: walls of at least 0.8 mm.
#
# BY HAND.
#   area of the plate           2 (40 x 40) + 4 (40 x 10)                    = 4800 mm^2
#   less the fin's footprint     0.6 x 10                                     =   -6
#   plus the fin's own faces     2 (10 x 20) + 2 (0.6 x 20) + 0.6 x 10         =  +430
#   total surface                                                              = 5224 mm^2
#   the two big faces of the fin are the thin wall: 2 (10 x 20) = 400 mm^2  -> 400 / 5224 = 7.7 % of the surface
#   (the rule does not count the 0.4 mm along the edges of a face, where the surface turns: 2 (9.2 x 19.2) = 353 mm^2  -> 6.8 %)
# So the report should say about 7 % of the surface breaks the wall rule, in one place at the fin, worst 0.2 mm short (0.8 - 0.6).
#
# The output pane prints the report (how much of the surface, the worst, where); the numbers it measures come from points of the surface.
from fieldes import *

view.set_bounds([-5, -5, -5], [45, 45, 35], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

plate = box((0, 0, 0), (40, 40, 10))
fin = box((15, 15, 10), (15.6, 25, 30))
part = union(plate, fin)

total = 5224.0
print('BY HAND: total surface %.0f mm^2; the fin faces are %.1f %% of it (%.1f %% without the edge strips)' % (total, 100 * 400 / total, 100 * 353.3 / total))

report = manufacturing_analysis(part, constraints=[manufacturing_constraint(min_wall=0.8)])
print(report)
report
