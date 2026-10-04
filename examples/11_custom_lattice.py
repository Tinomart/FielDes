# Custom cells: cell_custom(...) makes a cell of your own, and every lattice operation takes it like a standard one.
#
# Shown inside the same imported part one after the other (change which line is last to see the other):
#   1) a strut cell from nodes and beams (each beam may have its own radius): cell_custom(nodes, beams)
#   2) your own TPMS equation: cell_custom(equation=f)
# A shape modelled in one cell and repeated works too: cell_custom(shape=cell_shape).
#
# The lattice is inside a 1 mm skin: open the section card (Ctrl+Shift+X) to look inside.
#
from fieldes import *

parts = import_step_parts("step/Bracket.step")
body, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(3)
view.set_quality(8)

# 1) A strut cell.  Coordinates run 0..1 across the cell.  Draw one part of a symmetric cell and
#    mirror it: here a corner-to-centre diagonal (thicker, 1.0 mm) and half an edge (the radius
#    of the lattice), mirrored in x, y and z: a body-centred cube with its edges.
star = cell_custom({"o": (0, 0, 0), "c": (0.5, 0.5, 0.5), "e": (0.5, 0, 0)},
                   [("o", "c", 1.0), ("o", "e")],
                   mirror="xyz")
print("strut cell:", star, star.check() or "tiles")      # check() lists what would make it not tile
struts = lattice(body, star, cell_size=8, radius=0.6, skin=1.0)

# 2) Your own TPMS: a function of the three cell phases a, b, c (2 pi per cell), written with
#    + - * / and .sin() .cos() .sqrt() .square().  The result becomes a distance, so thickness=
#    is a real wall in mm, and density= calibrates it like the built-in ones.
my_tpms = cell_custom(equation=lambda a, b, c: a.cos() * b.cos() * c.cos() - a.sin() * b.sin() * c.sin(), name="D-like")
sheets = lattice(body, my_tpms, cell_size=8, thickness=0.8, skin=1.0)

struts          # or: sheets
