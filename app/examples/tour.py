# The guided tour's model
from fieldes import *

view.set_bounds([-35, -25, -8], [35, 25, 30], auto_scale=True)
view.set_resolution(4)
view.set_quality(8)

plate = box((60, 40, var(6)), position=(0, 0, 3))
hole = cylinder(var(5), 40, position=(0, 0, 10))
drilled = difference(plate, hole)
drilled
