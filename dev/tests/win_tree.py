# The model tree: types with their icons, nesting (a model under the first operation that takes it), drag and drop
from fieldes import *

base = box_exact((0, 0, 0), (40, 30, 6))
hole = cylinder_z(4, 20, (20, 15, -5))
plate = difference(base, hole)
thick = offset(plate, 1.0)
thick
ball = sphere(8, (60, 15, 10))
ball
disc = circle(6, (20, 50))
disc
anchor = point(60, 15, 25)
anchor
sheet = plane((0, 0, 3), (0, 0, 1))
dist = distance_to_point(anchor)
rounded = offset(ball, 1.0)
rounded
combined = union(thick, ball)
combined
cap = sphere(5, (20, 50, 10))
cap
