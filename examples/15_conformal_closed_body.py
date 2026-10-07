# A conformal lattice on a CLOSED body: strut cells that fill the wall of a bracket, 2 mm deep, all the way round its faces, fillets
# and bores, with a row of cells along every sharp edge.
#
# lattice_surface_conform(body, cell, cell_thickness, ...) lays ONE closed mesh of quads, 5 mm wide, on the body's surface (made from the body's
# field alone) and puts a cell on each.  The layout takes under a minute; the render a few minutes more, because struts this thin (1.1 mm)
# want view.set_resolution(4.8) (the finest before the mesher's depth steps up) and radius=0.55.
#
# Try: another cell (cell_periodic('bcc') / 'kelvin'), a smaller cell_size, depth=4, side='outside'.  The layout is made once, from
# one grid of sample points: if a part comes out with a warning or an error, grid_offset=1, 2 or 3, or a smaller cell_size, may close it.
#
# (The STEP file is the bracket that comes with FielDes: see step/README.md.)
#
from fieldes import *

parts = import_model("step/PivotBearingSupportBracket.STEP")
bracket = parts[0][0]

view.set_bounds(*roi(parts))
view.set_resolution(4.8)                   # samples per mm: a strut 1.1 mm thick wants about five
view.set_quality(8)

lattice = lattice_surface_conform(bracket, cell_periodic('octet'), 2, cell_size=5, radius=0.55)

lattice
