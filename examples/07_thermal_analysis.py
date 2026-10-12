# Thermal analysis: where does the heat go?
#
# A hot bearing heats the lugs of the bracket (30 W); the back plates are bolted to a cold plate at 25 C and the rest of
# the surface gives heat to the air by natural convection.  Boundary conditions are regions, as supports and loads are in
# static_analysis; each kind has an input of its own (fixed_temperatures, heat_inputs, convections ...).  The temperature
# is a field: colour the part by it, read it under the cursor, or let it drive the geometry.
#
from fieldes import *

parts = import_model("step/PivotBearingSupportBracket.STEP")
bracket, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

plates = union(box((-76, 5, -61), (-45, 36, 29)),
               box((45, 5, -61), (76, 36, 29)))           # bolted to the cold plate
lugs = box((-60, -90, -61), (60, -60, 29))                 # where the bearing heats it
air = difference(box((lo[0] - 5, lo[1] - 5, lo[2] - 5), (hi[0] + 5, hi[1] + 5, hi[2] + 5)), plates)   # every other surface

result = thermal_analysis(bracket,
                          fixed_temperatures=[fixed_temperature(plates, 25.0)],
                          heat_inputs=[heat_input(lugs, 30.0)],                          # watts
                          heat_generations=[],                                           # (none: [] says so)
                          convections=[convection(air, 10e-6, ambient=25.0)],            # W / (mm2 K): natural convection
                          material=aluminium, element_size=3)
print("in a lug: %.1f C; hottest %.1f C, coldest %.1f C" % (result.temperature(-40, -80, -4), result.max_temperature, result.min_temperature))

result                                                      # shown: the part coloured by the temperature
