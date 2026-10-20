from fieldes import *
view.set_bounds([-5, -7, -4], [3, 2, 5], auto_scale=True)

# START HERE: FIELDES 1.0.0.
#
# This file does nothing but say where everything is.  Everything below is a script you open in the window (File > Open example file, or drag it onto the window) and look at.  Look at the
# picture first; the numbers are an aid.
#
# 1.  WHAT YOU DO WITH THE MOUSE  --  the folder `walkthroughs`
#       Six small scripts, one for each thing of the interface that is new: pulling the faces of every primitive, Python in the model tree (and a move that would break the script asks first), the
#       insertion point between two rows of the tree, Ctrl+E (extrude, path extrude) and pulling an extrusion by its faces, the part drawn turned, your own primitives.  Each one starts with
#       WHAT TO DO and WHAT YOU MUST SEE.  Start with walkthroughs/00_START_HERE.py.
#
# 2.  MANUFACTURING  --  the folder `manufacturing`
#       The check of a part against what a printer, a mill or a mould asks of it, and the tool that changes the part until it keeps the rules: ONE EXAMPLE FOR EACH THING IT DOES, each one a
#       part, the rules, the call, the answers worked out by hand and the checks (one PASS or FAIL line each).  Start with manufacturing/00_START_HERE.py.
#
# 3.  THE KNOWN ANSWERS  --  the folder `verification`
#       Four problems with a solution on paper (a cantilever, a plate with a hole, a channel flow, conduction through a bar): the script stops with an error when FielDes is farther from the
#       answer than a stated tolerance.
#
# 4.  THE EXAMPLES  --  the files 01 .. 33 next to this one
#       Each one is a small design that opens, runs and draws.
#
# 5.  REAL PARTS  --  the folders `step` and `meshes`
#       A bracket as a STEP file and meshes to import.

print("Open one of the files listed above.")
