# Sample custom blocks.  Every function in a file of this folder (whose name does not start with an underscore) is a block:
# it is there in every script, like a function of the library, with its call tip, completion and menus.  Write one with a
# few lines, save the file, and use it:
#
#     part = perforate(box_exact((0, 0, 0), (60, 40, 6)), 2.5, 10)
#
# The whole library is at hand in a block file (no imports).  The first line of the docstring is what the call tip says.
# A block whose first argument is a model and has defaults for the others is in the right-click menu under
# "Add operation > Custom blocks"; a block that needs no argument is under "New custom block".


def perforate(body, hole_radius=2.0, spacing=10.0):
    ''' Drills a grid of round holes right through a body, along z '''
    holes = repeat(cylinder_z(hole_radius, 100000, (0, 0, -50000)), (spacing, spacing, 0))
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
    base = box_exact((0, 0, 0), (width, depth, thickness))
    upright = box_exact((0, 0, 0), (width, thickness, height))
    return union(base, upright, radius=thickness * 0.8)
