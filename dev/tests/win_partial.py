# a script with slow statements: the model tree should list a, b, c while the first sleep runs, and d during the second
from fieldes import *
import time

view.set_bounds((-5, -5, -5), (15, 15, 15))
view.set_quality(8)
view.set_resolution(5)

a = sphere(3, (5, 5, 5))
b = box_exact((0, 0, 0), (10, 10, 10))
c = difference(b, a)
time.sleep(4)
d = move(c, (1, 0, 0))
time.sleep(4)
d
