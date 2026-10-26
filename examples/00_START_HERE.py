from fieldes import *
view.set_bounds([-5, -7, -4], [3, 2, 5], auto_scale=True)

# START HERE: FIELDES 1.1.0.
#
# This file does nothing but say where everything is.  Everything below is a script you open in the window (File > Open example file, or drag it onto the window) and look at.  Look at the
# picture first; the numbers are an aid.  The comment at the top of each script says what to look at.
#
# 1.  THE EXAMPLES  --  the files 01 .. 34 next to this one
#       Each one is a small design that opens, runs and draws.  They are there to start a project from: copy one, change it.  The README next to them lists them, with the time each one takes.
#       Importing and inspecting a part: 01, 02, 03.   Handles and the gizmo: 04.   Analyses: 05 (static), 06 (modal), 07 (thermal), 16 (fluid).   Optimization: 08, 17, 32.
#       Lattices: 09, 10, 11, 14, 15, 23, 34.   Fields: 18, 19, 21, 30, 31.   Shapes made of points, copies and symmetry: 26, 27, 28, 29.   Apps: 24, 25.   Exporting: 12, 22.
#
# 2.  MANUFACTURING  --  33_manufacturing_optimization.py
#       A bracket that a topology optimization shaped -- struts of every thickness leaning every way, nothing designed for any process -- changed until a 3D printer, a mill and an injection
#       mould can make it: gussets grown under what leans too far, thin walls thickened, narrow gaps closed, inside corners rounded, draft added.  The load-carrying plates and lugs stay as
#       they are.  About five minutes.
#
# 3.  REAL PARTS  --  the folders `step` and `meshes`
#       A bracket as a STEP file and meshes to import.

print("Open one of the files listed above.")
