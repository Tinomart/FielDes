# Modal analysis: how a part vibrates.
#
# The shaft support is bolted down at its base plate.  Its natural frequencies and mode shapes
# are computed; every mode is a field like any other (displacement, ux, uy, uz), and the result
# card shows the part moving in the mode.  Stiffen it (a gusset, a thicker web) and run again
# to see how far each frequency moves.
#
from fieldes import *

parts = import_step_parts("step/ShaftSupportStand.STEP")
stand, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

base_plate = box_exact((-51, -1, -1), (51, 20, 12))        # held by its bolts

result = modal_analysis(stand, [fixed(base_plate)], material=steel, modes=6, element_size=4)
print(result)
for i, f in enumerate(result.frequencies):
    print("mode %d: %.0f Hz" % (i + 1, f))

result.modes[0].show()            # the first mode; .modes[1].show() for the second ...
