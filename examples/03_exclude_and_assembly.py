# Assemblies, and the places where the STEP file's own surface is wanted.
#
# import_model() gives one (shape, bounds) per solid of a file -- a bracket is one, an assembly is as many as it has parts -- and
# every one is a shape like any other: move it, copy it, put them together.  Here a plate carries three copies of the bracket,
# each one turned, and the middle one has the file's own surface where it matters.
#
# What is rebuilt from a STEP file is a formula: its flat and round faces exactly, its free-form (B-spline) faces FITTED -- replaced
# by the closest plane, quadric, extruded, revolved or helical surface the importer finds (the import prints how far the fit is
# from the CAD face, and shades the part where it is worse than 0.5 % of the face, red at 10 %).  Where a fit is not good enough,
# or where the file's own surface is simply wanted, FielDes can use it instead:
#
#   bracket = exclude(bracket, region)
#       the region is a FIELD OBJECT, any shape: inside it, in every part of the import it reaches, the part is the STEP
#       file's own surface -- meshed straight from the file and made a field, LOCKED: nothing done to the part afterwards
#       changes it; outside, the fitted field.  A region that encloses a face whole leaves no seam.
#
#   bracket = exclude(bracket)
#       no region: the places where a part's fit is worse than `threshold` percent, found from the fit markers.
#
#   import_model(..., auto_exclude=True)
#       the same, in the import.
#
# (The bracket's faces are all flat and round, so its fit is exact and exclude() has little to change; a part with free-form
# surfaces -- a moulded front, a curved handle -- is where it earns its keep.)
#
from fieldes import *

parts = import_model("step/PivotBearingSupportBracket.STEP")
bracket, (lo, hi) = parts[0]

# The file's own surface in the lugs (the region is a box: any shape would do)
lugs = box_exact((-62, -88, -62), (62, -58, 30))
exact_bracket = exclude(bracket, lugs)

# An assembly of three: the bracket as it is, and two turned about the vertical axis and set beside it
width = hi[0] - lo[0]
left = move(rotate_z(bracket, math.radians(180)), (-1.3 * width, 0, 0))
right = move(rotate_z(bracket, math.radians(180)), (1.3 * width, 0, 0))
base = box_exact((lo[0] - 1.3 * width - 20, lo[1] - 20, lo[2] - 12), (hi[0] + 1.3 * width + 20, hi[1] + 20, lo[2] - 2))

view.set_bounds((lo[0] - 1.3 * width - 30, lo[1] - 30, lo[2] - 20), (hi[0] + 1.3 * width + 30, hi[1] + 30, hi[2] + 10))
view.set_resolution(2)
view.set_quality(8)

base
left
exact_bracket
right
