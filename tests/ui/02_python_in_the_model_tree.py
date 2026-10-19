# 02: Python in the model tree, and the Python menu.
#
# WHAT THIS SHOWS.  The model tree is a picture of the script.  The script below is plain Python around a few shapes: a loop, an if / elif / else, a function, a class, a
# try / except and a while.  Each block is a row of the tree (a violet tile with the kind of block), with what is in it under it.
#
# WHAT TO DO AND WHAT YOU MUST SEE.
#   1. Look at the tree (open and close a row with its arrow).
#        for i in range(3)        i (the loop variable) and b (assigned in the body)
#        if size > 5              size (the variable the header reads: a hollow dot), a, and the branch `elif size > 2` with size, a and its `else` holding a
#        def make_post(r, h)      r and h (the parameters) and the statement `return cylinder(...)`
#        class Holder             the function `def __init__(self, width)` with self, width and the statement `self.width = width`  (the attribute is a statement, not a variable)
#        try                      c, and the branch `except Exception` holding `print(error)`
#        while size > 8           size (read by the header) and size (assigned in the body)
#      `post` and `last` are shapes: they have an eye, handles and a gizmo as always.  A plain `size = 10` at the top is no model and no block: it has no row.
#   2. Click a name under a block (say `size` under the `if`): the code editor selects that name in the script, ready to be replaced by typing.  Click a statement: it selects the statement.
#   3. What is inside a block is LOCKED in the tree: it has no eye, cannot be dragged, renamed or deleted there.  The code is edited in the code editor.
#   4. Drag a block (the `for` row) above or below another top-level row -- `post`, the class, the `try`: its lines move in the script (with the comment lines above it and the
#      blank lines under it) as one edit, and Ctrl+Z puts it back.  A block goes above or below a row of the top level; held over a row INSIDE a model or a block (a name, a statement, a branch) it is
#      refused: the label turns red and a red note beside it says why.
#   5. Right-click a model in the tree, or empty space in the viewport: the menu has a PYTHON category next to Primitives, Parametric shapes, Operations and Engineering --
#      variable, if, if / else, for loop (a count), for loop (a list), while loop, function (def), class, list, list from a loop, dictionary, try / except, with (a file), import,
#      print.  Pick one: its code is written at the insertion point (see 03) as plain Python -- no `name =` in front, no line that shows it, because it is no model.
#   6. Only what the script shows at the top level is drawn: `post` and `last` (a line with the name alone).  `b` and `a` are made inside blocks and are not shown.

from fieldes import *

view.set_bounds([-30, -30, -5], [80, 40, 40])
view.set_resolution(3)
view.set_quality(8)

size = 10

for i in range(3):
    b = box((i * 20, 0, 0), (i * 20 + 10, 10, 10))

if size > 5:
    a = sphere(5, (0, 20, 5))
elif size > 2:
    a = sphere(3, (0, 20, 3))
else:
    a = sphere(1, (0, 20, 1))

def make_post(r, h):
    return cylinder(r, h, (40, 20, 0))

post = make_post(4, 20)
post

class Holder:
    def __init__(self, width):
        self.width = width

try:
    c = cone(4, 8, (50, 0, 0))
except Exception as error:
    print(error)

while size > 8:
    size = size - 1

last = sphere(4, (60, 20, 4))
last
