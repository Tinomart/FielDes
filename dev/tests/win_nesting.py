# The tree is the structure of the calls: nesting, renesting, denesting and shadows are edits of the arguments
from fieldes import *

view.set_bounds([-10, -10, -10], [45, 10, 10])
view.set_resolution(2)

a = sphere(3, (0, 0, 0))
b = sphere(3, (10, 0, 0))
c = sphere(3, (20, 0, 0))
d = sphere(3, (30, 0, 0))
u = union(a, b, c)
u
v = union(c, d)
v
t = offset(a, 1.0)
t
