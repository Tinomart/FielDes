# 04: extrude with Ctrl+E, and the faces of an extrusion.
#
# WHAT THIS SHOWS.  `circle_1` is a 2D shape and `box_1` a solid.  `circle_2` and `extrude_2` are a circle and its extrusion standing one over the other.
#
# WHAT TO DO AND WHAT YOU MUST SEE.
#   1. Click `circle_1` in the tree (or in the viewport) and press Ctrl+E (the viewport or the tree has the keyboard): `extrude_1 = extrude(circle_1, 0, 20)` is written under the
#      circle -- the length a round number of the shape's size -- and the solid is drawn.  It is the same as Operation > Extruding > extrude on the selection.
#   2. Click `box_1` and press Ctrl+E: nothing is written.  The tree says why: "Extrude pulls a 2D shape into a solid: select a circle, a rectangle, a triangle, a polygon or a
#      profile first."
#   3. Select `extrude_2` and drag its TOP face (the flat end): its end moves, `r` stays.  Drag its WALL: the radius changes and the ends stay.  The numbers are in the
#      `handles(extrude_2, [...])` line the tree writes: centre x, centre y, the radius, the two ends -- one number moves per face.
#   4. Select `circle_2` (the flat circle lies under the extrusion's base) and drag its edge where the extrusion is drawn over it: the CIRCLE's radius changes, not the extrusion's.
#      A 2D shape always takes the press before a face of a solid, where the two overlap.
#   (Export mesh moved to F7 and Ctrl+Alt+E: Ctrl+E is extruding.)

from fieldes import *

view.set_bounds([-30, -60, -5], [60, 30, 40])
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
