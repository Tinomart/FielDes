from fieldes import *
view.set_bounds([-5, -7, -4], [3, 2, 5], auto_scale=True)

# START HERE (for the maintainer): THE FILES THAT SHOW WHETHER THE WORK IS RIGHT.
#
# This folder is not shipped.  What users get to build their projects on is `examples/`.  Here are the files that check the work: each one says WHAT TO DO and WHAT YOU MUST SEE, so that the
# picture, and not a number, tells you whether it is right.  Look at the picture first.
#
#   walkthroughs/       six small scripts, one for each thing of the interface that is new (the faces of every primitive, Python in the model tree, the insertion point, Ctrl+E and the faces of an
#                       extrusion, the part drawn turned, your own primitives).  Start with walkthroughs/00_START_HERE.py.
#   manufacturing/      four showcases -- a 3D printer's job, a mill's, an injection mould's, the topology-optimized bracket -- each with the answers worked out by hand and the checks (one PASS or
#                       FAIL line each) that the helper in `../kit` makes.  Start with manufacturing/00_START_HERE.py.
#   ../internal/        about a hundred cases, each trying to break one thing on a part made for it.
#
# THIS ROUND (2026-10-21): what to do, and what you must see.  Open the file named in each line (File > Open, or drag it onto the window).
#
#  1. A SYMMETRY IN THE TREE.  Open examples/29_symmetry.py and click the row of the symmetry (`mirror`) in the model tree -- or take the guided tour (Help > Guided tour) to the step "A mirror" and click it
#     there.  YOU MUST SEE: its planes drawn, the whole model still in the picture and the camera where it was.  (Before, a click on the row took the camera in on the few millimetres round the
#     symmetry's centre and every model left the picture.)
#
#  2. THE WHEEL ON A POINT.  Open examples/26_profiles_and_sweeps.py, select nothing, put the mouse on the dot of one of the five points of the plate and turn the wheel with Shift held -- or Ctrl held.
#     YOU MUST SEE: the first notch selects the point (its gizmo and the gold ring appear, and `gizmo(` is written under it); the next notches scale it, 10 % a notch, and the plate's outline follows
#     (a spline point is rounder, a corner is not affected).  With the mouse on empty space the wheel zooms, as ever.
#
#  3. THE END FACES OF AN EXTRUSION.  Open walkthroughs/04_extrude_with_ctrl_e.py, click `extrude_2`, then press its TOP face (the flat end) and drag it up or down.  YOU MUST SEE: the length changes
#     and the radius stays; the number moves in the `handles(extrude_2, [...])` line.  The same on the plate of examples/26_profiles_and_sweeps.py (an extrusion of a profile of points): click it, then
#     drag its top face -- the 6 of `extrude(outline, 0, 6)` changes.  Drag the BASE of an extrusion, where its 2D shape lies over it, and the 2D shape's own edge or face is what moves.
#     (If a press on a face pulls nothing, start FielDes with FIELDES_TIMING=1: the log has a line `[drag] nothing to pull ...` that says why.)
#
#  4. PRIMITIVES > MANAGE PRIMITIVES.  Click a profile (a 2D shape) in the tree first, then open the window.  YOU MUST SEE: it opens on "2D shapes"; the list of kinds (3D shapes, 2D shapes, Lines,
#     Surfaces, Fields -- a point and a symmetry are one button in the menus, so they have no section) opens white with grey borders and dark text, readable, the item under the mouse blue; ticking a shape moves it between Primitives and the Parametric
#     shapes.  Select a row and press Rename: its name is edited in place, type another and press Return: the note under the list says what the menus call it now, and the right-click menu
#     Primitives shows the name; an empty name gives the library's own back.  "Add selection" puts a model of your own in the list of its kind, with "(yours)".
#
#  5. THE TOUR.  Help > Guided tour, to the step "Your primitives" (after "Make things"): the menu bar's Primitives menu is drawn with "Manage primitives..." framed; "Show me" opens the window on
#     the selected model's kind and closes it again.
#
#  6. THE EXAMPLES.  File > Open example file: the files 00 .. 33, the folders step, meshes and verification (the four problems with a known answer) -- no walkthroughs, no manufacturing folder.
#     Run examples/33_manufacturing_optimization.py (about five minutes): the bracket as the optimizer made it (right) and its result for the printer (left), grey where it is as it was, red where
#     material was added; the mill's and the mould's results are models of the tree (the eye shows them).
#
#  7. THE PRINTER SHOWCASE, THE THREE PLACES (manufacturing/01_printer.py; Shift + left drag turns the view, the wheel zooms).  Look at each from the side named:
#     a. THE FIN (front, in front of the plate): from the FRONT (View > Front), zoomed on it.  YOU MUST SEE: a bar 2.0 mm wide and 12 mm high, red on both sides of the grey core, flat on top
#        and meeting the plate in a straight line -- nothing lifted above its top, no flare at its foot -- and the same on the left and on the right.
#     b. THE SLOT (between the two blocks): from the TOP (View > Top), zoomed on it.  YOU MUST SEE: one red slab as wide as the slot, from block to block, the tops of the two blocks beside it grey
#        and flat, no red rim on them and nothing sticking out at either end -- the same at the top end and at the bottom end.
#     c. THE GUSSET (behind, on the left, under the ledge): from the RIGHT (View > Right), turned a little with Shift + left drag, zoomed on it.  YOU MUST SEE: a red pyramid under the ledge whose
#        sloped faces meet the wall along straight lines, with no teeth and no white gaps between the red and the wall, and no slab sticking out of its foot onto the plate.

print("Open one of the files listed above.")
