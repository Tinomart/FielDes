# Two models to try the viewport's right-click menus and the I key on
from fieldes import *

view.set_bounds((-40, -30, -30), (40, 30, 30))
view.set_resolution(3)
view.set_quality(8)

block = box_exact((-30, -10, -10), (-5, 10, 10))
ball = sphere(9, (15, 0, 0))

block
ball
