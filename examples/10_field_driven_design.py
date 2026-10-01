# Field-driven design: the analysis drives the geometry.
#
# The bracket is analysed (FEA); its stress field then sets the density of the lattice that
# fills it: dense where the part works hard, light where it does not, inside a solid skin.
# Everything is a field, so any result can drive any parameter (thickness, cell size, blend
# radius ...).
#
# The lattice is inside a 1.5 mm skin: open the section card (Ctrl+Shift+X) to look inside.
#
from fieldes import *

parts = import_step_parts("step/PivotBearingSupportBracket.STEP")
bracket, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(1.5)
view.set_quality(8)

plates = union(box_exact((-76, 5, -61), (-45, 36, 29)),
               box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))

# 1) analysis: bolted at the plates, pulled at the lugs
result = static_analysis(bracket, [fixed(plates)], [force(lugs, (0, -2000, 0))],
                         material=aluminium, element_size=4)
print(result)

# 2) the stress field -> a relative density field: 0.15 where the stress is low, rising to 0.5
#    at 40 % of the peak stress and above (ramp is a clamped linear map; fit() can shape any curve)
peak = result.max_von_mises
density = ramp(result.von_mises, (0, 0.4 * peak), (0.15, 0.5))

# 3) the density field drives the lattice (calibrated per point), inside a 1.5 mm skin;
#    shown coloured by the density it follows
graded = lattice(bracket, "gyroid", cell_size=8, density=density, skin=1.5)
colored(graded, density, label="lattice density (from stress)")

# Other ways to drive it:
#   density = ramp(result.displacement, (0, result.max_displacement), (0.5, 0.15))
#   opt = topology_optimization(bracket, [fixed(plates)], [force(lugs, (0, -2000, 0))], volume_fraction=0.4)
#   graded = lattice(bracket, "octet", cell_size=8, density=ramp(opt.density, (0, 1), (0.1, 0.5)), skin=1.5)
