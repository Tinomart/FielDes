# A large assembly with free-form (B-spline) surfaces.
#
# Keukencombinatie is 90 parts.  Its flat and round faces are rebuilt exactly; its free-form
# surfaces (the curved handles, the moulded fronts) are FITTED: replaced by the closest plane,
# quadric, extruded, revolved or helical surface the importer finds, and the import prints how far
# the fit is from the CAD face, in mm and as a percentage of the face's size.  The model is shaded
# where the fit is worse than 0.5 % of the face (grey), fully red at 10 %.
#
# Where a fit is not good enough, FielDes can use the STEP file's own surface instead:
#
#   kitchen = exclude(kitchen, region)
#       the region is a FIELD OBJECT, any shape: inside it, in every part of the import it
#       reaches, the part is the STEP file's own surface -- meshed straight from the file and made a
#       field, LOCKED: nothing done to the part afterwards changes it; outside, the fitted field.
#       A region that encloses a badly fitted face whole leaves no seam.
#
#   kitchen = exclude(kitchen)
#       no region: the places where a part's fit is worse than `threshold` percent, found
#       from the fit markers, in every part.  kitchen[19] = exclude(kitchen[19]) does one part.
#
#   import_step_parts(..., auto_exclude=True)
#       the same, in the import.
#
# The first run reads the STEP file (about a minute and a half) and keeps the result next to it;
# the viewport then meshes every part at its own resolution (a few minutes).
#
from fieldes import *

THRESHOLD = 1.0        # percent of the face's size; the marker starts at 0.5

kitchen = import_step_parts("step/Keukencombinatie.stp")

# A field object as the region: the places where the fit of any part is worse than THRESHOLD.
# poor_fit_region(part) is a field, negative where that part's fit is poor and positive away from
# it, made from the part's fit marker (the shading); union() puts them together.
poor = [poor_fit_region(part, THRESHOLD) for part, _ in kitchen]
region = union(*[field for field in poor if field is not None])

kitchen = exclude(kitchen, region)

view.set_bounds(*roi(kitchen))
view.set_resolution(roi_resolution(kitchen))        # every part at its own resolution
view.set_quality(8)

[part for part, _ in kitchen]
