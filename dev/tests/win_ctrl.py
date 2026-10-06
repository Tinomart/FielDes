# Ctrl+drag of a variable: a reference (shadow) at the dropped place, the variable itself where it was
from fieldes import *

view.set_bounds([-10, -10, -10], [55, 10, 10])
view.set_resolution(2)

a = sphere(3, (0, 0, 0))
b = sphere(3, (10, 0, 0))
c = sphere(3, (20, 0, 0))
d = sphere(3, (30, 0, 0))
e = sphere(3, (40, 0, 0))
u = union(a, b)
u
v = union(c, d)
v
e
