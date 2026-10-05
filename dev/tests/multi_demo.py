# Three models to try selecting several of them (Ctrl+click, Shift+click, a Shift+drag rectangle), the E and R keys and
# the operations on a selection.  The ball has a var(): clicking it starts a drag of its surface, as a real shape's would
from fieldes import *

view.set_bounds((-40, -30, -30), (40, 30, 30))
view.set_resolution(3)
view.set_quality(8)

block = box_exact((-30, -10, -10), (-5, 10, 10))
ball = sphere(var(9), (15, 0, 0))
top = sphere(7, (-2, 0, 20))

block
ball
top
