# A model and a bare expression displayed on its own (it has no name until it joins a selection of several)
from fieldes import *

view.set_bounds((-40, -30, -30), (40, 30, 30))
view.set_resolution(3)
view.set_quality(8)

block = box_exact((-30, -10, -10), (-5, 10, 10))
block
sphere(9, (15, 0, 0))
