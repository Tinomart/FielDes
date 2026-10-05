# exclude() in the real window: a STEP part, a region in the middle of it (a sphere), then an offset of the excluded part.
# Inside the sphere the part is its exact surface (a field made from the STEP file's own mesh) and the offset leaves it alone.
from fieldes import *

parts = import_step_parts("../../examples/step/PivotBearingSupportBracket.STEP")
part, (lo, hi) = parts[0]
size = max(hi[i] - lo[i] for i in range(3))
mid = tuple(0.5 * (lo[i] + hi[i]) for i in range(3))

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

region_1 = sphere(0.22 * size, mid)
excluded_1 = exclude(part, region_1)
offset_1 = offset(excluded_1, 0.06 * size)
# hidden: excluded_1
offset_1
# hidden: region_1
