# The section view and the field viewer together (window test): a body, a point, and a field made of it
from fieldes import *

view.set_bounds([-30, -25, -10], [60, 25, 30])
view.set_resolution(3)

plate = box_exact((-20, -15, 0), (30, 15, 12))
anchor = point(25, 0, 12)
reach = ramp(distance_to_point(anchor), (0, 40), (1.0, 0.0))
plate
