# Static structural analysis (FEA) of an imported part.
#
# The bracket is bolted down at its two side plates and pulled at its lugs with 2 kN.  Supports
# and loads are ordinary shapes: the part is held wherever it lies inside a support region, and
# a force is spread over its surface inside a load region.  The tetrahedra follow the part's
# surface (element="tet", the default).
#
# Open the result card at the bottom right of the viewport: it switches the field (von Mises,
# displacement, each stress component ...), magnifies the deformation, and shows the elements.
#
from fieldes import *

parts = import_model("step/PivotBearingSupportBracket.STEP")
bracket, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

# Regions (mm): the bolted plates at the back, and the lugs at the front
plates = union(box_exact((-76, 5, -61), (-45, 36, 29)),
               box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))

# The problem: every condition is a model of its own, with its own eye in the model tree -- the surfaces where the part is
# held (blue, with pads) and pulled (red, with arrows: 2000 N).  The "boundary conditions" row of the analysis shows or hides them all
support = fixed(plates)
load = force(lugs, (0, -2000, 0))      # N, pulling away from the wall
result = static_analysis(bracket, supports=[support], loads=[load], material=aluminium, element_size=2)
print("safety factor: %.1f" % result.safety_factor)

result                # shown: the stress on the deformed part (the result card steps the load)
