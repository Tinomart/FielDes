# Selecting a model that another one is made of, after that one was selected (the lines made for dragging)
from fieldes import *

view.set_bounds([-10, -10, -10], [80, 40, 40])
view.set_resolution(4)
ball = sphere(8, (60, 15, 10))
ball
rounded = offset(ball, 1.0)
rounded
