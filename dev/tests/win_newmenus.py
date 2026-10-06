# Window test of the 2026-10-07 round: a field selected in the tree is shown by the section viewer; the viewport's menu opens on a
# row of the model tree and on a line of the script; a point dropped on distance_to_point
from fieldes import *

view.set_bounds([-40, -30, -10], [40, 30, 30])
view.set_resolution(3)
view.set_quality(8)

plate = box_exact((-30, -20, 0), (30, 20, 6))
anchor = point(10, 0, 3)
other = point(-10, 5, 3)
reach = distance_to_point((5, 5, 5))
swell = ramp(distance_to_point(anchor), (0, 40), (4.0, 0.3))
plate
