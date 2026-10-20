# 06: your own primitives (the Primitives menu of the menu bar).
#
# WHAT THIS SHOWS.  The menus offer a short list of PRIMITIVES (the most basic shape of each kind) apart from the Parametric shapes.  What is in that list is yours to change, and
# you can put a model of your own in it: it is then in the Primitives menu of every script you open.  The script below makes a bracket (`bracket_1`: a plate with a post on it, made
# of a variable, a function and two shapes) -- the model to turn into a primitive.
#
# WHAT TO DO AND WHAT YOU MUST SEE.
#   1. Click `bracket_1` in the model tree (or in the viewport): it is selected.
#   2. Menu bar > Primitives > Manage primitives...  A window lists "Primitives of: 3D shapes" -- the kind of the model that is selected -- with every shape the menus have of that kind:
#      a tick means it is under Primitives, no tick means it is with the Parametric shapes (a rounded box, a prism ...).  Tick one and look at the right-click menu of empty space: it is
#      under Primitives now.  Untick `torus`: it moves to the Parametric shapes.
#   3. Type a name (`my_bracket`) and press "Add selection".  `my_bracket (yours)` is in the list.  Right-click empty space > Primitives > 3D > my_bracket: the bracket is written into
#      the script AGAIN, as a model of its own (`my_bracket_1`) -- with the statements it is made of (the variable `size`, the function `post`, the plate) under names the script
#      has not got: `size` is `size_2` there, so nothing of the first bracket is overwritten.
#   4. Close FielDes and open it again (or open another script): `my_bracket` is still in the Primitives menu.  It is kept in ONE file of yours (`primitives.json`, in %APPDATA%\FielDes
#      or ~/.local/share/FielDes), not in this script: deleting this script does not take it away.  In the window, select it in the list and press Remove to forget it.

from fieldes import *

view.set_bounds([-12, -12, -4], [52, 32, 30], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

size = 10


def post(r, h):
    return cylinder(r, h, (size * 2, 10, 2))


plate = box((0, 0, 0), (size * 4, 20, 2))
bracket_1 = union(plate, post(4, size))
bracket_1
