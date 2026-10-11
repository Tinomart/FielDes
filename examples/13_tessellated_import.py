# The bracket, imported EXACTLY.
#
# reconstruct() rebuilds every solid as a formula and FITS its free-form (B-spline) faces with simple
# surfaces: fast and light, and an approximation (example 03 shows how to ask for the file's own surface
# where it matters).  tessellate() is for parts that are almost all free-form, where that does not work: it
# fits nothing.  Every solid is tessellated straight from its trimmed faces, on all the processor's threads, and
# the triangles are made the exact signed distance field of the part.
# (import_model(), which the other examples use, chooses between the two for each part: the one with more than a tenth
# of its surface free-form is tessellated.  Here the part is, by name.)
#
# Look at the output: how many triangles, how many of the faces are free-form, and how long the tessellation
# took (the first run; it is kept in step/PivotBearingSupportBracket.STEP.fieldes-tessellation and read from there next
# time).  The part is exact, and a shell, an offset or a lattice of it is made from its true distance:
# `hollow` is the bracket with 3 mm walls, `bracket_fine` an offset of it.
#
# What you give up is the formula: the faces of this import cannot be dragged (the gizmo of the model tree
# works), and a part that is mostly flat faces and cylinders -- like this one -- is lighter reconstructed.
from fieldes import *

parts = tessellate("step/PivotBearingSupportBracket.STEP")
bracket, (lo, hi) = parts[0]
bracket

view.set_bounds(*roi(parts))
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

hollow = shell(bracket, 3)           # 3 mm walls, from the exact distance of the tessellation
# hidden: hollow
bracket_fine = offset(bracket, 1.5)  # the part grown by 1.5 mm, round at the edges
# hidden: bracket_fine
