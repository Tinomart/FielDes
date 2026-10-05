# Custom cells: a cell of your own, and every lattice operation takes it like a standard one.
#
# Shown inside the same imported part one after the other (change which line is last to see the others):
#   1) ANY geometry: cell_custom(region, geometry) -- a box for the extent of the cell and a field for what is in it
#   2) a strut cell from nodes and beams (each beam may have its own radius): cell_custom_truss(nodes, beams)
#   3) your own TPMS equation: cell_custom_tpms(f)
#
# The lattice is inside a 1 mm skin: open the section card (Ctrl+Shift+X) to look inside.
#
from fieldes import *

parts = import_step_parts("step/Bracket.step")
body, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(3)
view.set_quality(8)

# 1) Any geometry.  The region is the box of the cell (here 10 mm); the geometry is any field -- it may reach out of the
#    box, only what is inside is the cell, and the cell repeats.  Here a hollow ball held by square rods that run through
#    its middle and out through the middle of every face: what leaves one face comes in on the opposite one, so the cells
#    tile.  (cell.solid() is the cell on its own: put it last to look at it.)
region = box((0, 0, 0), (10, 10, 10))
ball = difference(sphere(3.8, (5, 5, 5)), sphere(2.6, (5, 5, 5)))                        # a hollow ball, 1.2 mm wall
rods = union(box((-2, 4, 4), (12, 6, 6)), box((4, -2, 4), (6, 12, 6)), box((4, 4, -2), (6, 6, 12)))
held_ball = cell_custom(region, union(ball, difference(rods, sphere(2.6, (5, 5, 5)))))
print("custom cell:", held_ball)                       # a warning here would say where the faces do not match
ball_lattice = lattice(body, held_ball, skin=1.0)      # cells of 10 mm as modelled; cell_size=7 scales them to 7 mm

# 2) A strut cell.  Coordinates run 0..1 across the cell.  Draw one part of a symmetric cell and
#    mirror it: here a corner-to-centre diagonal (thicker, 1.0 mm) and half an edge (the radius
#    of the lattice), mirrored in x, y and z: a body-centred cube with its edges.
star = cell_custom_truss({"o": (0, 0, 0), "c": (0.5, 0.5, 0.5), "e": (0.5, 0, 0)},
                         [("o", "c", 1.0), ("o", "e")],
                         mirror="xyz")
print("strut cell:", star, star.check() or "tiles")      # check() lists what would make it not tile
struts = lattice(body, star, cell_size=8, radius=0.6, skin=1.0)

# 3) Your own TPMS: a function of the three cell phases a, b, c (2 pi per cell), written with
#    + - * / and .sin() .cos() .sqrt() .square().  The result becomes a distance, so thickness=
#    is a real wall in mm, and density= calibrates it like the built-in ones.
my_tpms = cell_custom_tpms(lambda a, b, c: a.cos() * b.cos() * c.cos() - a.sin() * b.sin() * c.sin(), name="D-like")
sheets = lattice(body, my_tpms, cell_size=8, thickness=0.8, skin=1.0)

ball_lattice          # or: struts, sheets
