# A lattice inside an imported part, graded by a regression.
#
# lattice(body, cell, cell_size, ...) fills any body with a gyroid, a strut lattice, a honeycomb
# ...  Here the wall of the gyroid follows a regression through a few data points over the depth
# below the skin of the part: thick at the skin, thinner inside.  The thickness is a field, so
# the part can be painted by it (hover to read it; the section card shows it inside).
#
from fieldes import *

parts = import_model("step/Bracket.step")
body, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(3)                         # samples per mm: thin walls want a fine mesh
view.set_quality(8)

# 1) data: the wall thickness (mm) wanted at several depths below the skin (mm)
data = [(0, 1.6), (3, 1.2), (6, 0.8), (12, 0.6)]

# 2) a regression through it (pchip never overshoots the data; see fit() for the others) ...
thickness_of_depth = fit(data, model="pchip")

# 3) ... applied to the depth field of the part is a field: the wall thickness wanted at every point
thickness = thickness_of_depth(depth_below(body))

# 4) the field drives the lattice: gyroid walls, 6 mm cells, a 1 mm solid skin
graded = lattice(body, cell_periodic("gyroid"), cell_size=6, thickness=thickness, skin=1.0)
colored(graded, thickness, label="wall thickness (mm)")

# Other things to try:
#   lattice(body, cell_periodic("octet"), cell_size=8, density=0.2)        # struts, by relative density
#   lattice(body, cell_periodic("gyroid"), cell_size=6, density=ramp(z_field(), (-30, 30), (0.1, 0.4)))
#   lattice(body, cell_periodic("kelvin"), cell_size=8, radius=0.7, region="shell", depth=6, skin=1)
#   lattice(body, cell_non_periodic("voronoi"), cell_size=8, radius=0.5)   # a random foam
