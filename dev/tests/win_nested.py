# Dragging rows that are already nested (drag and drop must work for them as for the others)
from fieldes import *

view.set_bounds([-15, -15, -15], [45, 25, 25])
view.set_resolution(2)

x1 = sphere(3, (0, 0, 0))
x2 = sphere(4, (12, 0, 0))
x3 = sphere(3, (24, 0, 0))
x4 = sphere(2, (36, 0, 0))
big = union(x1, x3)
big
more = union(x2, x4)
more
