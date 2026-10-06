# Renaming in the model tree, and the render settings as fields (window test)
from fieldes import *

view.set_bounds([-10, -10, -10], [50, 30, 20])
view.set_resolution(2)

base = box_exact((0, 0, 0), (40, 30, 6))
hole = cylinder_z(4, 20, (20, 15, -5))
plate = difference(base, hole)
thick = offset(plate, 1.0)
thick
print("plate", plate)
