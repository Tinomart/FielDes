# Shadows: a model's row is under the first statement that uses it; a shadow can only go BELOW that statement
from fieldes import *

view.set_bounds([-10, -10, -10], [45, 10, 10])
view.set_resolution(2)

a = sphere(3, (0, 0, 0))
b = sphere(3, (10, 0, 0))
c = sphere(3, (20, 0, 0))
d = sphere(3, (30, 0, 0))
u = union(a, b)
u
v = union(c, d)
v
w = union(b, c, d)
w
