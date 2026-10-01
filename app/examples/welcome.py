# Welcome to FielDes: design with fields.
#
# Everything you see is this script.  Edit it and the model updates;
# drag a handle in the viewport and the numbers here change.
#
#   File > Import model...  (Ctrl+I) brings in a STEP or mesh file.
#   Help > FielDes guide    (Shift+F1) lists every feature and shortcut.
#
from fieldes import *

view.set_bounds([-10, -10, -10], [10, 10, 10])
view.set_resolution(10)
view.set_quality(8)

# var(...) makes a number draggable: grab the sphere's surface in the viewport
r = var(3)
ball = sphere(r)

# Shapes combine like numbers: the last expression is what is shown.
# The examples folder starts from real STEP files: File > Open and pick
# 01_import_a_part.py.
hole = cylinder_z(1.2, 12, (0, 0, -6))
difference(ball, hole)
