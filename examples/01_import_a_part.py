# Import a STEP part and measure it.
#
# File > Import model... writes these lines for you.  The part is rebuilt from the file's
# faces as a field (a signed distance function), so everything else in FielDes works on
# it: sections, analysis, lattices, regions, edits by dragging.
#
from fieldes import *

parts = import_model("step/PivotBearingSupportBracket.STEP")   # one (shape, bounds) per solid
part, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))                   # the region the viewport meshes
view.set_resolution(roi_resolution(parts))     # samples per mm, chosen for the part
view.set_quality(8)

print("size (mm):", [round(h - l, 1) for l, h in zip(lo, hi)])

# Volume, mass (aluminium: 2.7 g/cm3) and centre of mass, from the field itself
properties = mass_properties(part, density=2.7, lower=lo, upper=hi, resolution=1.0)
print("volume %.0f cm3, mass %.0f g" % (properties["volume"] / 1000, properties["mass"]))
print("centre of mass (mm):", [round(c, 1) for c in properties["centroid"]])

part
