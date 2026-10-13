# A symmetry: mirror planes, as a model of the script.
#
# `mirror = symmetry_planes(planes=2)` is two planes at right angles, through a centre.  It is a model like any other: a row in the model tree,
# a gizmo that moves and turns it, and its planes drawn in the viewport when it is selected.  SELECT IT in the model tree and it is the symmetry
# in use: everything you make from then on is made at its images too -- written under the model it is the image of:
#
#     leg_m1 = symmetry_image(leg, mirror, 1)  # shadow: leg, mirror
#
# `1` is the number of the image in the symmetry (two planes at right angles have three: a turn of half a way round, and two mirror images).
#
# Things to try:
#   * Select `mirror` and drag its gizmo (the arrows): all three images of the leg move with it.  Drag a leg's arrow (or the leg's surfaces):
#     the other legs follow, mirrored.  Select an IMAGE and drag it: the leg it is the image of moves -- the other way, as a mirror shows it.
#   * With `mirror` selected, the card in the top right corner changes it: 1 to 8 planes round the axis, the plane across the axis as well
#     (`kind='across'`), or the nine planes of a cube (`kind='cube'`); the axis along x, y or z, the work plane's normal or up in the view.
#   * Shift+S stops using the symmetry, and starts again.  Make a box now, with the symmetry in use: it gets three images.  Make one with Shift+S
#     pressed (not in use): it does not.  A box whose centre is on the axis has no images: it is its own image.
#   * Select the table top: its own planes (it is mirror symmetric: two planes at right angles, and the plane across them) are drawn in teal,
#     and `Take its planes` in the card makes a symmetry of them.
#   * With the work plane open (Shift+W), draw a line or a sketch: it is drawn at its images too, through the images of its points -- and half
#     a shape whose ends are on a plane is closed through its image into one sketch.
from fieldes import *

view.set_bounds([-60, -40, -5], [60, 40, 45], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

# The symmetry: two planes at right angles through the axis z, through the origin.
mirror = symmetry_planes(planes=2, kind='axis', centre=(0, 0, 0), axis=(0, 0, 1), first=(0, 1, 0))
mirror = gizmo(
    mirror,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
)
mirror

# A table top: its centre is on the axis, so it is its own image.
top = box((-48, -30, 30), (48, 30, 36))
top

# One leg, and its three images.
leg = cylinder(5, 30, (40, 22, 0))
leg = gizmo(
    leg,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
leg
leg_m1 = symmetry_image(leg, mirror, 1)  # shadow: leg, mirror
leg_m1
leg_m2 = symmetry_image(leg, mirror, 2)  # shadow: leg, mirror
leg_m2
leg_m3 = symmetry_image(leg, mirror, 3)  # shadow: leg, mirror
leg_m3
