# Custom blocks: your own functions, kept in a folder, there in every script.
#
# Every function in a .py file of the blocks folder is a block.  This repository's `blocks/sample_blocks.py` has four:
#
#     perforate(body, hole_radius, spacing)    a grid of round holes right through a body
#     rounded(body, radius)                    smooths every edge and bump smaller than the radius
#     light_core(body, wall, cell_size, ...)   a hollow body with a gyroid in its core
#     bracket(width, height, thickness, depth) an L-shaped bracket with a rounded inside corner
#
# There is nothing to import: they are used like sphere() or lattice(), with call tips, completion (Ctrl+Space) and the
# right-click menus (Add operation > Custom blocks).  Ctrl+click a block's name to read it; edit it, save the file, and this
# script runs again.  Settings > Blocks folder... chooses another folder, for blocks of your own.
#
# A block is a few lines:
#
#     def perforate(body, hole_radius=2.0, spacing=10.0):
#         ''' Drills a grid of round holes right through a body, along z '''
#         holes = repeat(cylinder_z(hole_radius, 100000, (0, 0, -50000)), (spacing, spacing, 0))
#         return difference(body, holes)
#
# Every number of a block can be a field, like every number of the library: the holes below get bigger along x.
from fieldes import *

view.set_bounds([-5, -5, -5], [95, 75, 45])
view.set_resolution(5)
view.set_quality(8)

plate = box_exact((0, 0, 0), (90, 50, 6))
drilled = perforate(plate, ramp(x_field(), (0, 90), (1.0, 3.5)), 10)      # the radius of the holes is a field
drilled = expose(drilled, [
    var(45.0), var(45.0), var(25.0), var(25.0), var(3.0), var(3.0), var(5.0), var(5.0),
    var(-50000.0), var(50000.0),
])
drilled = handles(drilled, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
drilled

support = bracket(40, 30, 4, 20)
moved = move(support, (0, 60, 0))
moved = expose(moved, [
    var(20.0), var(20.0), var(60.0), var(10.0), var(10.0), var(2.0), var(2.0), var(10.0),
    var(10.0), var(20.0), var(20.0), var(2.0), var(2.0), var(15.0), var(15.0), var(2.0),
    var(2.0), var(10.0), var(10.0), var(10.0), var(10.0), var(3.2), var(2.0), var(2.0),
    var(2.0), var(2.0), var(3.2),
])
moved = handles(moved, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
moved
