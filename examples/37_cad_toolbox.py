# A flange with a hub, six countersunk bolt holes, six slots and three gussets -- every operation of docs/cad.md in one part: a profile revolved, a hole with a countersink,
# patterns round an axis, a slot, a body cut by a plane, a body turned about an axis -- and the measurements of the part in the output.  Docs: docs/cad.md.
#
# What to look at: the hub is a profile (a rectangle) turned about the y axis and stood up on z; the six bolt holes are ONE hole made by hole_cutter and repeated by
# circular_pattern, and so are the slots and the gussets (a box cut by a slanted plane); change `count` of a pattern, the head of the countersink, the angle of the gussets.
from fieldes import *

view.set_bounds([-45, -45, -3], [45, 45, 30], auto_scale=True)
view.set_resolution(4)
view.set_quality(8)

# The flange: a disc 80 mm across and 6 thick, and a hub on it -- the profile (distance from the axis, height) turned about the y axis, then turned up onto z.
plate = cylinder(40, 6)
hub = rotate(revolve(rectangle((0, 0), (15, 26))), (1, 0, 0), 90)
flange = union(plate, hub)
bore = cylinder(9, 40, (0, 0, -5))

# Six bolt holes 30 mm from the axis: one countersunk hole from the top of the plate, six times round the z axis.
bolt = hole_cutter((30, 0, 6), 5, kind='countersink', head_diameter=9, angle=90)
bolts = circular_pattern(bolt, 6, axis=(0, 0, 1))

# Six radial slots between the bolts, 12 long and 4 wide, 18 mm from the axis (a slot is 2D: extruded through the plate, then turned half a step).
window = rotate(extrude(slot(12, 4, (18, 0)), -1, 7), (0, 0, 1), 30)
windows = circular_pattern(window, 6, axis=(0, 0, 1))

# Three gussets from the hub to the rim: a box cut by a plane that slopes from the top of the hub down to the plate, three times round, turned 15 degrees off the bolts.
rib = box((14, -2.5, 6), (38, 2.5, 24))
gusset = cut_plane(rib, (18, 0, 23), (15, 0, 24), keep='below')
gussets = circular_pattern(rotate(gusset, (0, 0, 1), 15), 3, axis=(0, 0, 1))

part = difference(difference(difference(union(flange, gussets), bore), bolts), windows)

volume = mass_properties(part, lower=(-41, -41, -1), upper=(41, 41, 27), resolution=2)['volume']
print('volume %.0f mm^3, surface %.0f mm^2' % (volume, surface_area(part, element_size=0.6)))
print('section through the plate at z = 3: %.0f mm^2, through the hub at z = 16: %.0f mm^2' % (section_area(part, (0, 0, 1), (0, 0, 3)), section_area(part, (0, 0, 1), (0, 0, 16))))
print('the hub is %.0f mm^3 of the plate (they overlap there), and the closest the gussets come to the bore is %.1f mm' % (interference(plate, hub), distance_between(gussets, bore)))

part
