# a script that stops with an error: the model tree lists what the statements before the error made
from fieldes import *

view.set_bounds((-5, -5, -5), (15, 15, 15))
view.set_quality(8)
view.set_resolution(5)

a = sphere(3, (5, 5, 5))
b = box_exact((0, 0, 0), (10, 10, 10))
c = difference(b, a)
d = no_such_function(c)
c
