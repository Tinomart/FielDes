# THE NIGHT OF 2026-10-25/26: snap, corners and edges, the argument row of custom_resolution, an imposed displacement, a body as the region of a support, the points to snap to (centres of circles and
# faces, rounded corners and edges).  About one minute to draw.
# Six groups of models, one under the other (y = -120 to +210).  Open the file (File > Open, or drag it onto the window), turn the view a little (Shift + left drag) and look at each group: WHAT TO DO, WHAT YOU MUST SEE.
#
#  A  SNAP (y = -20 .. 20).  WHAT TO DO: nothing; read `snapped = snap(bracket, foot_face, foot, top_face, hole)`: the bracket, by the face and the point of its foot, onto the plate by the face and the point `hole`.
#     YOU MUST SEE: the grey plate with two brackets on it -- one standing on its foot on the point (0, 0, 0), one lying on its side on the point (10, 10, 0) -- each touching the plate and not
#     entering it, and the bracket of the script, unmoved, at the right of the plate.
#
#  B  CORNERS AND EDGES (y = 60 .. 98).  WHAT TO DO: right-click the block near its upper right corner, then near the middle of one of its top edges, then the top rim of the cylinder (the peg); the sphere and a flat
#     face of the block as well.  YOU MUST SEE: at the corner "Point at the corner"; at the edge "Point on the edge" and "Line along the edge"; at the rim "Point on the edge" and "Point at the centre of the circle" (a circle is
#     not a line); at the sphere and on a flat face only "Point at the centre of this face".  Picking one writes `corner_1 = corner_at(block, (x, y, z))` (or edge_at, edge_point_at) under the block, with the place that was FOUND, and selects it.
#     The three that are in the script already were made so: the dot at the corner, the thin blue line along the block's edge from corner to corner (drawn on the edge, not beside it) and the dot on the rim.
#
#  C  AN ARGUMENT LIKE ANY OTHER (y = 118 .. 142).  WHAT TO DO: open the cone `tower` in the model tree and click the row `custom_resolution = 8`.  YOU MUST SEE: the row looks like the rows of the other
#     arguments (the same little tag, the same text column), the number 8 is selected in the line `tower = custom_resolution(tower, 8)` of the editor and the keyboard is in the editor (type a number and it replaces
#     the 8; run the script again and the cone is drawn at that resolution).  The number is the scene's resolution (as view.set_resolution says it) for this model alone: 8 draws it twice as fine as a scene at 4, and no longer
#     a number that has to be 500 to get the same picture.
#
#  D  AN IMPOSED DISPLACEMENT (y = -60 .. -50).  WHAT TO DO: nothing; read `settle = fixed(tip, body=beam, displacement=(0, inf, -1.0))`.  YOU MUST SEE: a steel beam held at the wall, its tip pushed down
#     1 mm (no force anywhere: total load 0 N), the result card with a support reaction of 2344 N and a compliance of 2344 N mm (the force that pushes times the 1 mm; at 4 mm elements, 2063 N
#     at 2 mm and with 10-node tetrahedra: beam theory with shear gives about 1940 N for this beam, the clamps stiffen it by a few percent more), a largest displacement of 1 mm and a von Mises stress of
#     757 MPa at the wall.  Press "Elements": the tetrahedra; View > Section view: the plane cuts them whole.
#
#  E  A BODY AS THE REGION OF A SUPPORT (y = -122 .. -98).  WHAT TO DO: select `part`, then Ctrl+click `backing` in the tree, right-click empty space > Engineering > Structural > New support or load > fixed.
#     YOU MUST SEE: `fixed_1 = fixed(region=backing, body=part, displacement=(0.0, 0.0, 0.0))` written, and the blue patch of the support on the face of the part where the backing touches it (a label "Fixed").
#     The script ends with the same support written by hand: `support = fixed(backing, body=part)`.
#
#  F  THE POINTS TO SNAP TO (y = 170 .. 210).  WHAT TO DO: right-click the drum (the cylinder at the left) on its top face, then at the front of the rim of its bottom; then the block (rounded edges) at its
#     front upper corner (x = 110, y = 170), then in the middle of its upper front edge.  YOU MUST SEE: on the top face "Point at the centre of this face" and nothing else; at the rim of the bottom also "Point at the
#     centre of the circle"; at the corner "Point at the rounded corner"; on the edge "Point on the rounded edge".  The dots already in the script: `drum_middle` at the middle of the drum's bottom circle,
#     `funnel_middle` at the middle of the cone's base, `face_dot` at the middle of the drum's top, `soft_corner` and `soft_edge` on the SHARP corner and edge that the rounding replaced (just outside the block).
#     The cone `under` hangs below the drum, apex down, on its axis, its base against the drum's bottom: it is the cone at the right (`funnel`) moved by
#     `snap(funnel, funnel_base, funnel_middle, drum_bottom, drum_middle)`.
from fieldes import *

view.set_bounds([-45, -135, -25], [115, 215, 45], auto_scale=False)
view.set_resolution(3)
view.set_quality(8)

# A -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
plate = box((-20, -20, -5), (20, 20, 0))
plate
bracket = box((30, -5, -5), (40, 5, 10))
bracket = custom_resolution(bracket, 8)
bracket
top_face = select_surface(plate, seed=(0, 0, 0), angle=10)
foot_face = select_surface(bracket, seed=(35, 0, -5), angle=10)
side_face = select_surface(bracket, seed=(40, 0, 2), angle=10)
hole = point(0, 0, 0)
hole_2 = point(10, 10, 0)
foot = point(35, 0, -5)
side_point = point(40, 0, 2)
snapped = snap(bracket, foot_face, foot, top_face, hole)
snapped
snapped_2 = snap(bracket, side_face, side_point, top_face, hole_2)
snapped_2

# B -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
block = box((0, 60, 0), (20, 80, 20))
block
peg = cylinder(6, 14, (40, 60, 0))
peg
ball = sphere(8, (40, 90, 10))
ball
corner = corner_at(block, (20, 80, 20))
corner
edge = edge_at(block, (20, 69.8047, 20))
edge
rim = edge_point_at(peg, (45.869, 58.759, 14))
rim
a = point(-30, 80, 0)
b = point(-20, 90, 10)
c = point(-10, 80, 20)
curve_1 = spline([a, b, c])
curve_1

# C -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
tower = cone(10, 25, (0, 130, 0))
tower = custom_resolution(tower, 8)
tower

# D -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
steel = Material('steel', 200e3, 0.30, 7.85e-9, 250, 0.050, 12e-6)
beam = box((0, -60, 0), (100, -50, 10))
wall = box((-6, -66, -6), (0, -44, 16))
tip = box((99, -62, -2), (101, -48, 12))
hold = fixed(wall, body=beam)
settle = fixed(tip, body=beam, displacement=(0, inf, -1.0))
result = static_analysis(beam, supports=[hold, settle], loads=[], material=steel, element_size=4)
result

# E -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
part = box((0, -120, 0), (40, -100, 10))
part
backing = box((-4, -122, -2), (0, -98, 12))
backing
support = fixed(backing, body=part)
support

# F -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
drum = cylinder(10, 20, (0, 190, 10))
drum
funnel = cone(15, 30, (40, 190, 0))
funnel
drum_bottom = select_surface(drum, seed=(3, 192, 10), angle=10)
funnel_base = select_surface(funnel, seed=(42, 191, 0), angle=10)
drum_middle = circle_center_at(drum, (10, 190, 10))
drum_middle
funnel_middle = circle_center_at(funnel, (55, 190, 0))
funnel_middle
face_dot = face_center(select_surface(drum, seed=(0, 190, 30), angle=10))
face_dot
under = snap(funnel, funnel_base, funnel_middle, drum_bottom, drum_middle)
under
soft = rounded_box((70, 170, 0), (110, 210, 40), 0.25)
soft
soft_corner = corner_at(soft, (108, 172, 38), rounded=True)
soft_corner
soft_edge = edge_point_at(soft, (109, 190, 38.5), rounded=True)
soft_edge
