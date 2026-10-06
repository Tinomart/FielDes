# Sections of a script that fold: a #SECTION comment runs to the next one
from fieldes import *

view.set_bounds([-10, -10, -10], [20, 10, 10])
view.set_resolution(2)

#SECTION Shapes
a = sphere(3, (0, 0, 0))
b = sphere(3, (8, 0, 0))

# SECTION Combine
u = union(a, b)
u

#SECTION Spare
unused = sphere(1, (0, 5, 0))
# a regional note, which is no section
