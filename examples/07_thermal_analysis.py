# Thermal analysis: where does the heat go?
#
# A hot bearing heats the clamp boss of the shaft support (60 W); the base plate is bolted to a
# cold plate at 25 C and the rest of the surface gives heat to the air by natural convection.
# Boundary conditions are regions, as supports and loads are in static_analysis.  The temperature
# is a field: colour the part by it, read it under the cursor, or let it drive the geometry.
#
from fieldes import *

parts = import_model("step/ShaftSupportStand.STEP")
stand, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

bearing = cylinder_z(16, 34, (0, 92, 10))                   # the material around the bore
cold_plate = box_exact((-51, -1, -1), (51, 20, 1.5))         # the base plate's underside
air = box_exact((lo[0] - 5, lo[1] - 5, 2), (hi[0] + 5, hi[1] + 5, hi[2] + 5))   # every surface above it

result = thermal_analysis(stand,
                          [heat_input(bearing, 60.0),                        # watts
                           fixed_temperature(cold_plate, 25.0),
                           convection(air, 10e-6, ambient=25.0)],            # W / (mm2 K): natural convection
                          material=aluminium, element_size=3)
print("bearing: %.1f C" % result.temperature(0, 92, 44))

result                                                      # shown: the part coloured by the temperature
