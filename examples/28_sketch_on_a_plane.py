# A sketch in a plane of its own: the work plane.
#
# A sketch lies in the plane through its first three points, whatever that plane is: this one is tilted 30 degrees about the x axis. extrude()
# pulls it along the normal of ITS plane, so the slab stands on the tilted plane and the badge on top of it is built the same way.
#
# To draw on such a plane yourself:
#   * Shift+W opens the work plane view (Ctrl+Shift+W makes a new plane: with a 2D shape selected it is that shape's plane, otherwise it is parallel to
#     the view). W turns the camera to look straight at the plane. The grid snaps what you put down, and the points you drag.
#   * The card has three kinds of drawing: LINE (the corners of a shape), SPLINE (a smooth curve) and POINT -- E goes round them. A click puts a point
#     down; a click on the FIRST point closes the line into a sketch; Enter ends it open; Backspace takes a point back; Q (or the button that is on,
#     clicked again) stops drawing, and then a click selects and moves things as usual. A double click on a point still changes it between a
#     corner and a spline point.
#   * Select the slab's sketch, press Ctrl+Shift+W, and draw a second shape on the same plane: it is made of points in that plane, written as
#     ordinary numbers.
# The work plane is a tool of the viewport: it is not saved in the script, the points and the sketches are.
from fieldes import *

view.set_bounds([-15, -15, -10], [70, 70, 60], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

# The corners of a slab, in a plane tilted 30 degrees about x: (s, t) in the plane is (s, 0.866 t, 0.5 t) in the world.
s1 = point(0, 0, 0)
s1
s2 = point(40, 0, 0)
s2
s3 = point(40, 21.65, 12.5)
s3
s4 = point(20, 34.64, 20)
s4
s5 = point(0, 21.65, 12.5)
s5
slab_outline = sketch(
    [s1, s2, s3, s4, s5],
    smooth=False,
)
slab_outline
slab = extrude(
    slab_outline,
    0,
    5,
)
slab

# A round badge on it, in the same plane (the same rule: (s, 0.866 t, 0.5 t)): every point is a spline point, and it is raised from the top of the slab.
t1 = point(14, 6.93, 4)
t1
t2 = point(26, 6.93, 4)
t2
t3 = point(26, 19.05, 11)
t3
t4 = point(14, 19.05, 11)
t4
badge_outline = sketch(
    [t1, t2, t3, t4],
    smooth=True,
)
badge_outline
badge = extrude(
    badge_outline,
    5,
    8,
)
badge
