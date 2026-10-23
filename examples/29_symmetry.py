# A symmetry as a model of the script -- a round table with three curved legs, one of them drawn.
#
# `mirror = symmetry_planes(copies=3, kind='turn')` is a three-way symmetry: three copies of whatever you make, 120 degrees apart round the axis of the table, nothing
# mirrored.  It is a model like any other: a row in the model tree, a gizmo that moves and turns it, and its disc with a spoke for each copy drawn in the viewport when it is
# selected.  SELECT IT in the model tree and it is the symmetry in use: everything you make from then on is made at its images too.  Here ONE leg was made -- a profile swept
# along a spline, the profile and the spline are children of the sweep in the model tree -- and the other two legs are its images, each with its own copy of the profile and the spline:
#
#     leg = path_extrude(leg_profile, leg_path)
#     leg_images = symmetry_images(leg, mirror, distinct=True)  # shadow: leg, mirror        both other legs, and their profiles and splines: one line, one row in the tree
#
# The sweep, its profile and its spline are mirrored together: the images bring the blue curve, the outline of the profile and their dots along, there is no model for them.
# Press a dot of ANY leg's spline or profile and drag it: it is the point of the leg you made that moves, and every leg follows.  Only that leg has numbers.  (The spline and
# the profile were drawn with the symmetry off -- Shift+S -- since what is swept is mirrored with the sweep.)
#
# Things to try:
#   * Press a dot of the blue curve of a leg and drag it: the leg bends, and so do the others.  Do it on a leg that is an image: it is the original that changes, the others
#     follow it.
#   * Select a point of the spline: its gold ring is how THICK the leg is there (the ankle is 0.65, the knee 1.2); its rings turn the way the curve goes.
#   * Double-click a point of the profile (its dot, on any leg): a corner or a spline point.  Press an edge of the profile's outline, on any leg, and drag to pull a point out
#     of it: a flatter or a rounder leg.  The point is put on `leg_profile`, and every leg shows it while you drag.
#   * Select `mirror` and drag its gizmo (the arrows, the rings): the copies, and with them all the legs, move and turn.  Select `leg_images` and drag its gizmo: the leg moves
#     -- turned the way its copy is.  The symmetry itself stays where it is.
#   * With `mirror` selected, the card in the top right corner changes it.  Turns: any number of copies (3: three legs; 5, 7 ...; up to 24).  Axis: planes through the axis,
#     1 to 8, 180 / n degrees apart -- every plane mirrors, so the same leg gets mirror images as well (3 planes: six legs, in mirrored pairs); Axis + across adds the plane
#     across the axis; Cube has the nine planes of a cube.  The axis goes along x, y or z, the work plane's normal or up in the view.
#   * Shift+S stops using the symmetry, and starts again.  Make a box now, with the symmetry in use: it gets images.  Make one with Shift+S pressed (not in use): it does
#     not.  A box whose centre is on the axis has no images: it is its own image.
#   * Select `leg_images` with no symmetry in use: the mirror it was made in becomes the symmetry in use -- its disc is drawn and the card shows -- so whatever you make next
#     gets its images too.  It is how the model was made, ready to be added to.
#   * With the work plane open (Shift+W), draw a line or a profile: it is drawn at its images too, through the images of its points.

from fieldes import *

view.set_bounds([-50, -50, -6], [50, 50, 46], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

# The symmetry: three copies round the axis z, 120 degrees apart, nothing mirrored.
mirror = symmetry_planes(
    kind='turn',
    centre=(0, 0, 0),
    axis=(0, 0, 1),
    first=(0, 1, 0),
    copies=3,
)
mirror = gizmo(
    mirror,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
)
mirror

# The table top: its centre is on the axis, so it is its own image.
top = cylinder(40, 3, (0, 0, 34.5))
top

# One leg.  Its path is a spline of five points, drawn with the symmetry off: the leg is swept along it, and the images of the leg bring it along.  The scale of a point
# (the gold ring) is how thick the leg is there.
leg_path_p1 = point(14.9, 30.6, 0)
leg_path_p1 = gizmo(
    leg_path_p1,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
leg_path_p1
leg_path_p2 = point(14.8, 23.7, 6)
leg_path_p2 = gizmo(
    leg_path_p2,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(0.65), var(0.65), var(0.65)),
)
leg_path_p2
leg_path_p3 = point(17.9, 22.9, 15)
leg_path_p3 = gizmo(
    leg_path_p3,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(0.9), var(0.9), var(0.9)),
)
leg_path_p3
leg_path_p4 = point(17.4, 18, 27)
leg_path_p4 = gizmo(
    leg_path_p4,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1.2), var(1.2), var(1.2)),
)
leg_path_p4
leg_path_p5 = point(14.3, 12.5, 35.5)
leg_path_p5 = gizmo(
    leg_path_p5,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1.3), var(1.3), var(1.3)),
)
leg_path_p5
leg_path = spline(
    [leg_path_p1, leg_path_p2, leg_path_p3, leg_path_p4, leg_path_p5],
    closed=False,
)
leg_path

# The profile of the leg: a profile of four spline points, an oval round the foot of the path.  The sweep starts at the profile: the first point of the path is put at the
# middle of it, so the profile is where the leg stands.
leg_profile_p1 = point(18.5, 30.6, 0)
leg_profile_p1 = gizmo(
    leg_profile_p1,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
leg_profile_p1
leg_profile_p2 = point(14.9, 33.4, 0)
leg_profile_p2 = gizmo(
    leg_profile_p2,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
leg_profile_p2
leg_profile_p3 = point(11.3, 30.6, 0)
leg_profile_p3 = gizmo(
    leg_profile_p3,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
leg_profile_p3
leg_profile_p4 = point(14.9, 27.8, 0)
leg_profile_p4 = gizmo(
    leg_profile_p4,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
leg_profile_p4
leg_profile = profile(
    [leg_profile_p1, leg_profile_p2, leg_profile_p3, leg_profile_p4],
    smooth=True,
)
leg_profile

# The leg, and its images.
leg = path_extrude(
    leg_profile,
    leg_path,
    twist=0,
)
leg = gizmo(
    leg,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
leg
leg_images = symmetry_images(leg, mirror, distinct=True)  # shadow: leg, mirror
leg_images
