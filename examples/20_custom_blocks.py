# Custom blocks: your own functions, kept in a folder, there in every script.
#
# Every function in a .py file of the blocks folder is a block.  The folder is empty to begin with: Settings > Show the blocks folder opens it, and File > Save as custom block puts the script you
# are working on there as a function.  There is nothing to import: a block is used like sphere() or lattice(), with call tips, completion (Ctrl+Space) and the right-click menus (Operations >
# Custom blocks).  Ctrl+click a block's name to read it; edit it, save the file, and the scripts that use it run again.
#
# The four functions below are what such a file holds.  They are written in this script so that it runs as it is; put them in a file of the blocks folder and they are in every script, and the
# lines that define them can go:
#
#     perforate(body, hole_radius, spacing)    a grid of round holes right through a body
#     rounded(body, radius)                    smooths every edge and bump smaller than the radius
#     light_core(body, wall, cell_size, ...)   a hollow body with a gyroid in its core
#     bracket(width, height, thickness, depth) an L-shaped bracket with a rounded inside corner
#
# Every number of a block can be a field, like every number of the library: the holes below get bigger along x.
from fieldes import *


def perforate(body, hole_radius=2.0, spacing=10.0):
    ''' Drills a grid of round holes right through a body, along z '''
    holes = repeat(cylinder(hole_radius, 100000, (0, 0, -50000)), (spacing, spacing, 0))
    return difference(body, holes)


def rounded(body, radius=1.0):
    ''' Smooths every edge and bump of a body that is smaller than the radius '''
    return smooth(body, radius, 2)


def light_core(body, wall=2.0, cell_size=8.0, thickness=0.9):
    ''' A body hollowed to a wall, with a gyroid lattice filling its core '''
    core = lattice(body, cell_periodic('gyroid'), cell_size=cell_size, thickness=thickness)
    return union(shell_inside(body, wall), core)


def bracket(width=40.0, height=30.0, thickness=4.0, depth=20.0):
    ''' An L-shaped bracket: a base plate and an upright, with a rounded inside corner '''
    base = box((0, 0, 0), (width, depth, thickness))
    upright = box((0, 0, 0), (width, thickness, height))
    return union(base, upright, radius=thickness * 0.8)


view.set_bounds([-5, -5, -5], [95, 95, 45], auto_scale=True)
view.set_resolution(5)
view.set_quality(8)

plate = box((0, 0, 0), (90, 50, 6))
drilled = perforate(plate, ramp(x_field(), (0, 90), (1.0, 3.5)), 10)      # the radius of the holes is a field
drilled = handles(drilled, [var(45.0), var(45.0), var(25.0), var(25.0), var(3.0), var(3.0), var(5.0), var(5.0), var(-50000.0), var(50000.0)])
drilled = place(
    drilled,
    position=(var(0), var(0), var(0)),
    rotation=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
drilled

support = bracket(40, 30, 4, 20)
moved = move(support, (0, 60, 0),
    position=(var(0), var(0), var(0)),
    rotation=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
moved = handles(moved, [var(20.0), var(20.0), var(60.0), var(10.0), var(10.0), var(2.0), var(2.0), var(10.0), var(10.0), var(20.0), var(20.0), var(2.0), var(2.0), var(15.0), var(15.0), var(2.0), var(2.0), var(10.0), var(10.0), var(10.0), var(10.0), var(3.2), var(2.0), var(2.0), var(2.0), var(2.0), var(3.2)])
moved
