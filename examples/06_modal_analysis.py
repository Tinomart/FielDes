# Modal analysis: how a part vibrates.
#
# The bracket is bolted down at its two back plates.  Its natural frequencies and mode shapes
# are computed; every mode is a field like any other (displacement, ux, uy, uz), and a mode stated
# on its own shows the part deformed by it -- play on the result card to see it vibrate.  Stiffen
# it (a gusset, a thicker web) and run again to see how far each frequency moves.
#
from fieldes import *

parts = import_model("step/PivotBearingSupportBracket.STEP")
bracket, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

plates = union(box_exact((-76, 5, -61), (-45, 36, 29)),
               box_exact((45, 5, -61), (76, 36, 29)))         # held by their bolts

support = fixed(plates)
support                           # shown: where the part is held (the eye of the "boundary conditions" row of the analysis shows them all)
result = modal_analysis(bracket, supports=[support], material=steel, modes=6, element_size=4)
print(result)
for i, f in enumerate(result.frequencies):
    print("mode %d: %.0f Hz" % (i + 1, f))

result.modes[0]                   # the first mode (result.modes[1] for the second ...); play: it vibrates
