# Read a part by its fields: wall thickness, overhang, curvature.
#
# Every shape is a field, so questions about a part are fields too.  colored(shape, field)
# paints the part by one; hover the part to read the value under the cursor, and open the
# section card (Ctrl+Shift+X) to see the field inside.  Close a legend with its x to stop
# the probing.
#
# The part is the pivot bearing support bracket that the other examples import (examples/step/),
# made here from boxes and cylinders -- a front plate, two ribs that end in arms with a bore, a hub with
# a bore, a shelf and the bottom flanges with a hole each -- so that its fields are cheap to ask:
# the thickness of every wall at a hundred thousand places of its surface in seconds.  Measured
# against the imported part at 150,000 random points: 99.4 % are inside both or outside both (its
# small fillets and drafted faces are left out, its volume is 0.5 % smaller).
#
from fieldes import *

view.set_bounds((-80, -90, -65), (80, 40, 32), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)


def box_between(low, high):
    ''' the box with its corners at low and high '''
    return box((high[0] - low[0], high[1] - low[1], high[2] - low[2]),
               position=((low[0] + high[0]) / 2, (low[1] + high[1]) / 2, (low[2] + high[2]) / 2))


# One side of the bracket (x >= 0); the other is its mirror image
flange = box_between((35, -10, -60), (75, 35, -50))                                  # the bottom flange, 10 mm thick
front = union(box_between((0, -10, -12), (45, 0, 28)),                               # the front plate, 10 mm thick, with a rounded end
              box_between((0, -10, -36), (57, 0, -12)),
              box_between((35, -10, -50), (57, 0, -36)),
              cylinder(12, 10, position=(57, -5, -24), rotation=(90, 0, 0)),
              box_between((57, -10, -50), (69, 0, -36)))
shelf = box_between((0, 0, -35), (42, 35, -25))                                      # the shelf at the back
wall = box_between((35, 0, -50), (45, 35, -35))                                      # the wall that joins shelf and flange
ribs = union(box_between((32, -56, -25), (42, -25, 24)),                             # the rib, 10 mm thick, that carries the arm
             box_between((32, -25, -25), (42, -10, 28)),
             box_between((32, -20, -35), (42, -10, -25)))
arms = union(cylinder(19, 16, position=(37, -66, 0), rotation=(0, 90, 0)),            # the arm: a round end of 38 mm and its neck
             box_between((29, -66, -19), (45, -47, 19)))
half = union(flange, front, shelf, wall, ribs, arms)
mirror = half.remap(-Shape.X(), Shape.Y(), Shape.Z())

# The hub in the middle, 50 mm across, along y
hub = union(cylinder(25, 35, position=(0, 17.5, 0), rotation=(90, 0, 0)), box_between((-25, 0, -25), (25, 35, 0)))
body = union(half, mirror, hub)

# The bores: the hub's, the two arms' and one hole in each flange
hub_bore = cylinder(16, 40, position=(0, 20, 0), rotation=(90, 0, 0))
arm_bores = union(cylinder(10, 60, position=(37, -66, 0), rotation=(0, 90, 0)),
                  cylinder(10, 60, position=(-37, -66, 0), rotation=(0, 90, 0)))
flange_holes = union(cylinder(5.784, 20, position=(63, 23, -55)), cylinder(5.784, 20, position=(-63, 23, -55)))
part = difference(body, hub_bore, arm_bores, flange_holes)

# Thinnest places first: the wall thickness in mm (blue thin, red thick, up to 20 mm)
thickness = wall_thickness(part, max_thickness=20)
colored(part, thickness, label="wall thickness (mm)")

# Try the others, one at a time (the last expression is what is shown):
#   colored(part, overhang_angle(part), label="overhang (deg)")     # for printing upwards (z)
#   colored(part, curvature_field(part), label="curvature (1/mm)")
#   colored(part, depth_below(part), label="depth below the skin (mm)")
