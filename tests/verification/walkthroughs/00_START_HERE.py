from fieldes import *
view.set_bounds([-5, -7, -4], [3, 2, 5], auto_scale=True)

# START HERE: WALKTHROUGHS OF WHAT YOU DO WITH THE MOUSE.
#
# Seven small scripts, one for each thing of the interface that is new in 1.0.0.  Open one (File > Open, then this folder; or drag it onto the window): its first lines say WHAT TO DO and WHAT YOU
# MUST SEE, step by step, and its script is the thing to do it on.  Look at the picture first; the numbers are an aid.
#
#   01_primitives_and_their_handles.py     every primitive of the Primitives menu (eight solids, six flat shapes): press a face and drag, and only that face's number changes
#   02_python_in_the_model_tree.py         a script of plain Python (loop, if / elif / else, def, class, try, while): the tree shows it, its parts are locked, a block moves by drag -- and a move that would
#                                          break the script asks first -- , and the Python menu writes plain code
#   03_the_insertion_point.py              click the line between two rows of the tree (it turns white): what you add lands there; the point and the text cursor of the editor follow each other
#   04_extrude_with_ctrl_e.py              Ctrl+E extrudes the selected 2D shape (with a curve selected too: a path extrusion); an extrusion's end faces set its length, its walls are the 2D shape's sides
#   05_the_part_turned.py                  a manufacturing result whose way up was left free is drawn standing the way it is printed
#   06_your_own_primitives.py              Primitives > Manage primitives: make anything you built a primitive of your own, available in every file
#   07_the_night_of_2026_10_26.py          snap, the corners and edges of the right-click menu, custom_resolution as an argument row, an imposed displacement, a body as the region of a support
#
# The manufacturing showcases are in the folder next to this one (manufacturing, start with its 00_START_HERE.py); the clean manufacturing example for users is examples/33_manufacturing_optimization.py.

print("Open one of the files listed above.")
