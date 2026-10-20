# Manufacturing 7: does it find the best way up for a print, and turn the part that way?
#
# THE PART.  A plate (50 x 50 x 5) sitting on top of a post (10 x 10 x 30).  The constraint says overhang 45 and build_direction=None: the
# way up of the print is not given.
#
# WHAT IT SHOULD DO.  Printed with the post at the bottom, the plate sticks out over nothing on every side, 20 mm each way: a big overhang.
# Printed upside down, with the plate at the bottom, there is none.  So the best way up is (0, 0, -1): the print grows toward -z.
#   1. manufacturing_analysis and manufacturing_optimization should report that way up.
#   2. result.oriented() is the part turned so that the way up is z: the plate at the BOTTOM, the post standing on it (shown in the viewport,
#      in front; the original is at the back).
#   3. export_mesh(result, ...) writes the part turned, which is how it goes to a printer: out/turned.stl should show the plate at the bottom.
# The output pane prints the way up found and the file written.
import os
from fieldes import *

view.set_bounds([-30, -90, -40], [40, 40, 45], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

post = box((0, 0, 0), (10, 10, 30))
plate = box((-20, -20, 30), (30, 30, 35))
part = union(post, plate)

free = manufacturing_constraint(overhang=45, build_direction=None)
report = manufacturing_analysis(part, constraints=[free])
print('the check was made in the way up:', tuple(round(v, 3) for v in report.orientation), ' (expected (0, 0, -1))')
result = manufacturing_optimization(part, constraints=[free], max_change=0.9)
print('the shell chose the way up:      ', tuple(round(v, 3) for v in result.build_direction), ' (expected (0, 0, -1))')

os.makedirs('out', exist_ok=True)
export_mesh(result, 'out/turned.stl', resolution=2)
print('written: out/turned.stl, %d bytes' % os.path.getsize('out/turned.stl'))
original = move(part, (0, -60, 0))
original
result.oriented()
