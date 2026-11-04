# Morph shapes: a body made of points with a symmetry INSIDE it -- and a second symmetry that makes a mirrored clone of the whole body.
#
# A morph shape is the 3D equivalent of a profile: a surface through points, each a corner (sharp), a spline point (the surface goes through it and is smooth there, in all
# directions) or a sphere point (a rounded corner).  `morph_body` makes every point a spline point unless it says `smooth=False`; `morph_polyhedron` the other way round.  Press the surface
# and drag to put a point on it; double-click a point to change what it is.
#
# This script has two symmetries, each one plane:
#
#   mirror    (x = 0) is INSIDE the body: the body is CUT IN HALF by the plane and has two identical sides.  Its left points were made, and its right points are their images --
#               body_p2 = symmetry_image(body_p1, mirror, 1)     # shadow: body_p1, mirror
#             -- so there is no second point on top of a point, and pulling a point of one side pulls its partner on the other.  The point on top (body_p9) is ON the plane, so it is its own
#             image: it has none, and it stays on the plane while you drag it along it.
#   mirror_2  (x = 48) makes a MIRRORED CLONE of the whole body: `body_clone = symmetry_images(body, mirror_2)` is the body -- with the symmetry inside it -- mirrored across the second
#             plane.  It is made of the images of the body's points, so it follows the body: change a point of the body and the clone changes with it.
#
# Things to try:
#   * Drag a point of the left side of `body`: the right side follows, mirrored -- and the clone follows both.  Double-click a point: a corner or a spline point -- its partner follows.
#   * Press the top of `body`, near the bump, and drag: a point is put there.
#   * Select `mirror_2` and drag its gizmo: the plane moves, and the clone with it.
#   * Select `mirror` and drag its gizmo: the plane moves; `body` is no longer cut in half by it, and its right side moves with the plane.
#
from fieldes import *

view.set_bounds([-45, -45, -25], [112, 45, 40], auto_scale=True)
view.set_resolution(10)
view.set_quality(8)

# The symmetry: one plane, x = 0 -- two sides.
mirror = symmetry_planes(
    1,
    'axis',
    (0, 0, 1),
    (0, 1, 0),
    position=(var(0), var(0), var(0)),
    rotation=(var(0), var(0), var(0)),
)
mirror

# The body: eight points, the corners of a box in the order x changes fastest, then y, then z -- the left four are its own points, the right four are their images --
# and a ninth on the plane at the top, pulled up into a bump.  The top face is split in four triangles round that point.
body_p1 = point(
    position=(var(-20), var(-14), var(-10)),
    rotation=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
body_p1
body_p2 = symmetry_image(body_p1, mirror, 1)  # shadow: body_p1, mirror
body_p2
body_p3 = point(
    position=(var(-20), var(14), var(-10)),
    rotation=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
body_p3
body_p4 = symmetry_image(body_p3, mirror, 1)  # shadow: body_p3, mirror
body_p4
body_p5 = point(
    position=(var(-16), var(-9), var(10)),
    rotation=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
body_p5
body_p6 = symmetry_image(body_p5, mirror, 1)  # shadow: body_p5, mirror
body_p6
body_p7 = point(
    position=(var(-16), var(9), var(10)),
    rotation=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
body_p7
body_p8 = symmetry_image(body_p7, mirror, 1)  # shadow: body_p7, mirror
body_p8
body_p9 = point(
    position=(var(0), var(0), var(19)),
    rotation=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
body_p9
# fieldes-internal: begin (the faces of the body below: the program writes and reads them, they are no part of the model)
body_faces = [
    (0, 2, 3, 1),
    (0, 1, 5, 4),
    (2, 6, 7, 3),
    (0, 4, 6, 2),
    (1, 3, 7, 5),
    (4, 5, 8),
    (5, 7, 8),
    (7, 6, 8),
    (6, 4, 8),
]
# fieldes-internal: end
body = morph_body(
    [body_p1, body_p2, body_p3, body_p4, body_p5, body_p6, body_p7, body_p8, body_p9],
)
body

# The second symmetry: one plane, x = 48.  It makes a mirrored clone of the whole body.
mirror_2 = symmetry_planes(
    1,
    'axis',
    (0, 0, 1),
    (0, 1, 0),
    position=(var(48), var(0), var(0)),
    rotation=(var(0), var(0), var(0)),
)
mirror_2

# The mirrored clone of the whole body: the body, with the symmetry inside it, mirrored across the second plane.
body_clone = symmetry_images(body, mirror_2)  # shadow: body, mirror_2
body_clone
