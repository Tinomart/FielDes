# Dragging a field onto an operation: it takes the place of a number (anywhere a number goes, a field goes)
from fieldes import *

view.set_bounds([-10, -10, -10], [80, 50, 30])
view.set_resolution(4)
plate = box_exact((0, 0, 0), (40, 30, 6))
anchor = point(35, 5, 6)
thick = offset(plate, 1.0)
thick
reach = distance_to_point(anchor)
swell = ramp(reach, (0, 40), (3.0, 0.3))
swell
