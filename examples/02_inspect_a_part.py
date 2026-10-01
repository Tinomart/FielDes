# Read a part by its fields: wall thickness, overhang, curvature.
#
# Every shape is a field, so questions about a part are fields too.  colored(shape, field)
# paints the part by one; hover the part to read the value under the cursor, and open the
# section card (Ctrl+Shift+X) to see the field inside.  Close a legend with its x to stop
# the probing.
#
from fieldes import *

parts = import_step_parts("step/ShaftSupportStand.STEP")
part, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

# Thinnest places first: the wall thickness in mm (blue thin, red thick, up to 20 mm)
thickness = wall_thickness(part, max_thickness=20)
colored(part, thickness, label="wall thickness (mm)")

# Try the others, one at a time (the last expression is what is shown):
#   colored(part, overhang_angle(part), label="overhang (deg)")     # for printing upwards (z)
#   colored(part, curvature_field(part), label="curvature (1/mm)")
#   colored(part, depth_below(part), label="depth below the skin (mm)")
