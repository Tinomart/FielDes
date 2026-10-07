# A conformal lattice on an OPEN surface, made of a cell of your own.
#
# The surface is just where a field is zero (here an S-shaped sheet, z = 8 sin(0.12 x)): it has no thickness and no STEP file is
# needed.  lattice_surface_conform(surface, cell, ...) lays one layer of cells on one side of it (side='outside': where the field is
# positive), 6 mm deep and 6 mm wide, bending with the surface, and cuts it off at the edge of the patch you give (within=).
#
# The cell is yours: cell_custom_truss(nodes, beams, mirror) -- here a cube frame with a plus through its middle.  A cell made for a
# surface has beams of one radius and is symmetric (mirror='xyz' draws one corner of it and copies the rest); cell.check() says
# whether it tiles.
#
# Try: cell_periodic('octet') in its place, side='inside', a deeper layer, a smaller cell_size.  A closed body: see
# 15_conformal_closed_body.py.
#
from fieldes import *

view.set_bounds((-35, -25, -20), (35, 25, 20))
view.set_resolution(5)                     # samples per mm: a strut 1.2 mm thick wants three or four
view.set_quality(8)

x, y, z = Shape.X(), Shape.Y(), Shape.Z()

# the surface: where this field is zero (negative below it, positive above it), and the patch of it wanted: 60 x 40 mm
wave = z - 8 * (0.12 * x).sin()
patch = box_exact((-30, -20, -14), (30, 20, 14))

# a cell of your own, coordinates 0..1 across it, one corner drawn and mirrored in x, y and z: from the corner "o" beams run along the
# edges to their middles ("ex", "ey", "ez"), from the centre "c" to the middles of the faces at that corner ("fx", "fy", "fz")
my_cell = cell_custom_truss({"o": (0, 0, 0), "ex": (0.5, 0, 0), "ey": (0, 0.5, 0), "ez": (0, 0, 0.5),
                             "c": (0.5, 0.5, 0.5), "fx": (0, 0.5, 0.5), "fy": (0.5, 0, 0.5), "fz": (0.5, 0.5, 0)},
                            [("o", "ex"), ("o", "ey"), ("o", "ez"), ("c", "fx"), ("c", "fy"), ("c", "fz")],
                            mirror="xyz")
print("my cell:", my_cell, my_cell.check() or "tiles")

lattice = lattice_surface_conform(wave, my_cell, within=patch, side='outside', cell_thickness=6, cell_size=6, radius=0.6)

lattice
