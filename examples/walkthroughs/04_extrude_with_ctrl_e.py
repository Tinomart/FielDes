# 04: extrude with Ctrl+E, path extrude, and pulling an extrusion by its faces.
#
# WHAT THIS SHOWS.  `circle_1` is a 2D shape and `box_1` a solid.  `circle_2` / `extrude_2` and `rectangle_2` / `extrude_3` are 2D shapes with their extrusions standing over them.
# `circle_3` is a 2D shape and `guide` a curve: together they make a path extrusion.
#
# WHAT TO DO AND WHAT YOU MUST SEE.
#   1. Click `circle_1` in the tree (or in the viewport) and press Ctrl+E (the viewport or the tree has the keyboard): `extrude_1 = extrude(circle_1, 0, 20)` is written under the
#      circle -- the length a round number of the shape's size -- and the solid is drawn.  It is the same as Operation > Extruding > extrude on the selection.
#   2. Click `box_1` and press Ctrl+E: nothing is written.  The tree says why: "Extrude pulls a 2D shape into a solid: select a circle, a rectangle, a triangle, a polygon or a
#      profile first."
#   3. Click `circle_3`, then Ctrl+click `guide` (a 2D shape and a curve) and press Ctrl+E: `path_extrude(circle_3, guide)` is written -- the circle swept along the curve.
#   4. Select `extrude_2` and drag its TOP face (the flat end, the face that is parallel to the circle): the length changes, the radius stays.  Drag its WALL: the radius changes and the
#      ends stay.  The numbers are in the `handles(extrude_2, [...])` line the tree writes: centre x, centre y, the radius, then the two ends -- one number moves per face.
#   5. The same for `extrude_3`, the extrusion of a rectangle: its top face is its length, each of its four walls is a side of the rectangle (the numbers of the rectangle, then the two ends).
#      This holds for every 2D shape: circle, rectangle, rounded rectangle, triangle, polygon, ring, profile.
#   6. Select `circle_2` (the flat circle lies under the extrusion's base) and drag its edge where the extrusion is drawn over it: the CIRCLE's radius changes, not the extrusion's.
#      A 2D shape always takes the press before a face of a solid, where the two overlap.
#   (Export mesh is on F7 and Ctrl+Alt+E: Ctrl+E is extruding.)

from fieldes import *

view.set_bounds([-30, -60, -17], [90, 45, 60], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

circle_1 = circle(10, (40, 20))
circle_1

box_1 = box((15, 15, 0), (25, 25, 10))
box_1

circle_2 = circle(10, (0, -30))
circle_2

extrude_2 = extrude(circle_2, 0, 20)
extrude_2

rectangle_2 = rectangle((30, -40), (55, -20))
rectangle_2

extrude_3 = extrude(rectangle_2, 0, 15)
extrude_3

circle_3 = circle(4, (70, 0))
circle_3

guide = spline([point(70, 0, 0), point(70, 0, 18), point(80, 0, 32)])
guide
