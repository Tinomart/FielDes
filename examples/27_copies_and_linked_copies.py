# Two kinds of duplicate: a COPY and a LINKED copy.
#
# With a model selected and the viewport (or the model tree) focused:
#
#   Shift+D   a COPY: the model's statements are written again under it, with free names. The copy is a model of its own: change either and
#             the other stays as it was.  (Below: lug_2, a second lug that is a copy of lug and was then made thicker.)
#   Ctrl+D    a LINKED copy: `bolt_2 = instance(bolt)`. It IS the bolt -- the same shape, the same size, every property -- and follows it: change the
#             bolt and all its linked copies change with it. All a linked copy has of its own is WHERE it is: it can only be moved and rotated, its
#             gizmo has no scale and its surfaces cannot be dragged. The bolt is held by a reference (`# shadow: bolt`): it keeps its own row in the
#             model tree and each copy shows a faded "shadow" of it, whether the bolt is locked or not.
#
# Things to try:
#   * Drag a face of the bolt (its top, its side): the three linked copies change with it. Do the same to lug_2: lug is not affected.
#   * Select bolt_3, press Shift and look at the gizmo: the arrows become flat squares that move it along a plane, and the three scale squares
#     are replaced by ONE gold ring that scales everything evenly -- which a linked copy does not have: it is not scaled.
#   * Select the bolt and press Ctrl+D for a fourth one; Operation > Repeating > instance does the same.
from fieldes import *

view.set_bounds([-15, -15, -5], [95, 55, 40], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

# A plate with a lug, and a copy of the lug that was made thicker.
base = box((-10, -10, 0), (90, 40, 4))
base
lug = box((0, 10, 4), (14, 20, 24))
lug
lug_2 = box((60, 10, 4), (74, 20, 24))
lug_2 = handles(lug_2, [var(67.0), var(7.0), var(18.0), var(8.0), var(14.0), var(10.0)])
lug_2

# One bolt, drawn with its numbers as handles, and three linked copies of it set out along the plate.
bolt = cylinder(3, 12, (20, 30, 4))
bolt = handles(bolt, [var(20.0), var(30.0), var(3.0), var(4.0), var(16.0)])
bolt
bolt_2 = instance(bolt,
    position=(var(15), var(0), var(0)),
    rotation=(var(0), var(0), var(0)),
)  # shadow: bolt
bolt_2
bolt_3 = instance(bolt,
    position=(var(30), var(0), var(0)),
    rotation=(var(0), var(0), var(0)),
)  # shadow: bolt
bolt_3
bolt_4 = instance(bolt,
    position=(var(45), var(0), var(0)),
    rotation=(var(0), var(0), var(0)),
)  # shadow: bolt
bolt_4
