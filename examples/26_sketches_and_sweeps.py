# Shapes made of points: a sketch, an extrusion, a line and a sweep.
#
# A POINT is a model of its own: a dot in the viewport. Click it to select it, or just press it and drag it -- it moves in the plane of the
# sketch it belongs to. A SKETCH is the 2D shape a closed outline through points encloses; extrude() pulls it into a solid. A SPLINE is a smooth
# line through points, and path_extrude() sweeps a profile along it.
#
# Things to try, with the viewport focused:
#   * Press an EDGE of the blue outline and drag: a new point comes out of it (written into the list of the sketch).
#   * Double-click a point: a CORNER (a square dot, a hard edge) becomes a SPLINE POINT (a round dot with a ring: the outline goes smoothly through it),
#     and back.  The corner at the top right of the plate is a spline point already: smooth=True.
#   * Select a spline point: its ring turns the direction the curve leaves it in; the gold ring round it scales it -- for a sketch, how strongly the
#     curve leaves it; for the tube below, how THICK the tube is there (the middle point of the path is drawn at 1.8 times the thickness).
#   * Select the plate and its points are the dots round it. Select several points together (Ctrl+click) and one dot moves them all, in the plane.
#   * Shift+D copies a model, Ctrl+D links it (see example 27).
from fieldes import *

view.set_bounds([-10, -10, -10], [115, 70, 55], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

# The corners of a plate. The fourth one says smooth=True: a spline point.
a = point(0, 0, 0)
a
b = point(60, 0, 0)
b
c = point(60, 30, 0, smooth=True)
c
d = point(30, 50, 0)
d
e = point(0, 30, 0)
e

# The face they enclose (smooth=False: a point that says nothing is a corner), and the plate it makes.
outline = sketch(
    [a, b, c, d, e],
    smooth=False,
)
outline
plate = extrude(
    outline,
    0,
    6,
)
plate

# A path of four points, and the tube along it. The scale of a point is how thick the tube is there: the gizmo of q2 has a scale of 1.8.
q1 = point(70, 5, 10)
q1
q2 = point(88, 24, 24)
q2 = gizmo(
    q2,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1.8), var(1.8), var(1.8)),
)
q2
q3 = point(102, 44, 16)
q3
q4 = point(106, 62, 36)
q4
path = spline(
    [q1, q2, q3, q4],
)
path
# The profile of the tube is a sketch as well: four spline points round the origin, the face they enclose. Move one of them, or pull a point out of its outline, and the tube follows.
s1 = point(3, 0, 0)
s1
s2 = point(0, 3, 0)
s2
s3 = point(-3, 0, 0)
s3
s4 = point(0, -3, 0)
s4
profile = sketch(
    [s1, s2, s3, s4],
    smooth=True,
)
profile
tube = path_extrude(
    profile,
    path,
)
tube
