# cell_custom(region, geometry) in the window: one cell on a straight grid in a block, and the same cell on a
# cylindrical cell map in a tube
from fieldes import *

view.set_bounds((-5, -5, -5), (75, 35, 35))
view.set_quality(8)
view.set_resolution(4)

region = box((0, 0, 0), (10, 10, 10))
ball = difference(sphere(3.8, (5, 5, 5)), sphere(2.6, (5, 5, 5)))
rods = union(box((-2, 4, 4), (12, 6, 6)), box((4, -2, 4), (6, 12, 6)), box((4, 4, -2), (6, 6, 12)))
held_ball = cell_custom(region, union(ball, difference(rods, sphere(2.6, (5, 5, 5)))))

block = lattice(box_exact((0, 0, 0), (30, 30, 30)), held_ball)
tube_body = difference(cylinder_z(18, 30, (55, 15, 0)), cylinder_z(6, 30, (55, 15, 0)))
tube = lattice(tube_body, held_ball, cell_map=cylindrical(origin=(55, 15, 0), cells_around=12))
block
tube
