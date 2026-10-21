# 01: every primitive of the Primitives menu, and the handles of each.
#
# WHAT THIS SHOWS.  Eight solids in the back row (box, sphere, cylinder, cone, pyramid, prism, torus, rounded box) and six flat shapes in the front row (circle, rectangle,
# rounded rectangle, ring, polygon, triangle).  Every one of them can be reshaped by hand: a face (a side, an end, the radius) is pulled, and only that face moves.
#
# WHAT TO DO.  Click the name of one of the shapes in the model tree (it is framed and selected).  Move the mouse over its surface: the face under the mouse lights.  Press on it
# and drag.  Under the shape the tree writes a line `name = handles(name, [var(...), ...])`; the numbers in that list are the faces, in the order of the shape's arguments.
#
# WHAT YOU MUST SEE.  The face you hold moves, and no other face does:
#   box            a number for the centre and one for the half-size of each axis: a pulled face changes the two of its axis together (the face moves, the opposite face stays)
#   sphere         the radius                         cylinder   the radius and the two ends                  torus   the two radii
#   cone           its side and its base (the cone grows or shrinks at its slope)                             rounded box   its six sides, the rounding stays
#   pyramid        the base and the four slanted sides, one number each (a pulled side moves along its normal and the others stay)
#   prism          the three sides of the triangle and the two ends, one number each
#   circle         the radius      ring   the outer and the inner radius      rectangle, rounded rectangle   the four sides      triangle, polygon   a number for each side
# The picture shows the one face moving.  (Before this release the pyramid only slid as a whole, a pulled polygon side moved the centre as well, and a pulled face of the rounded
# box moved the opposite face and made the rounding bigger; they are made of their faces now.)

from fieldes import *

view.set_bounds([-80, -50, -5], [130, 20, 30])
view.set_resolution(3)
view.set_quality(8)

# the solids
box_1 = box((-70, -10, 0), (-50, 10, 20))
box_1

sphere_1 = sphere(10, (-30, 0, 10))
sphere_1

cylinder_1 = cylinder(10, 20, (-10, 0, 0))
cylinder_1

cone_1 = cone(10, 20, (10, 0, 0))
cone_1

pyramid_1 = pyramid((20, -10), (40, 10), 0, 20)
pyramid_1

prism_1 = prism((50, -10), (70, -10), (60, 10), 0, 20)
prism_1

torus_1 = torus(10, 3, (90, 0, 10))
torus_1

rounded_box_1 = rounded_box((100, -10, 0), (120, 10, 20), 0.3)
rounded_box_1

# the flat shapes (drawn in the plane z = 0: look at them from above, View > Top, or the key 7)
circle_1 = circle(8, (-70, -35))
circle_1

rectangle_1 = rectangle((-55, -43), (-40, -27))
rectangle_1

rounded_rectangle_1 = rounded_rectangle((-30, -43), (-10, -27), 3)
rounded_rectangle_1

ring_1 = ring(8, 5, (5, -35))
ring_1

polygon_1 = polygon(8, 6, (30, -35))
polygon_1

triangle_1 = triangle((45, -43), (65, -43), (55, -27))
triangle_1
