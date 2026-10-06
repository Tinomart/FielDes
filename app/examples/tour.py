# The guided tour's model
from fieldes import *

view.set_bounds([-35, -25, -8], [35, 25, 30])
view.set_resolution(4)
view.set_quality(8)

plate = box_exact((-30, -20, 0), (30, 20, var(6)))
hole = cylinder_z(var(5), 40, (0, 0, -10))
drilled = difference(plate, hole)
drilled
