from fieldes import *
view.set_bounds([-5, -7, -4], [3, 2, 5], auto_scale=True)

# START HERE (for the maintainer): THE FILES THAT SHOW WHETHER THE WORK IS RIGHT.
#
# This folder is not shipped.  What users get to build their projects on is `examples/`.  Here are the files that check the work: each one says WHAT TO DO and WHAT YOU MUST SEE, so that the
# picture, and not a number, tells you whether it is right.  Look at the picture first.
#
#   walkthroughs/       seven small scripts, one for each thing of the interface that is new (the faces of every primitive, Python in the model tree, the insertion point, Ctrl+E and the faces of an
#                       extrusion, the part drawn turned, your own primitives, the night of 2026-10-26).  Start with walkthroughs/00_START_HERE.py.
#   manufacturing/      four showcases -- a 3D printer's job, a mill's, an injection mould's, the topology-optimized bracket -- each with the answers worked out by hand and the checks (one PASS or
#                       FAIL line each) that the helper in `../kit` makes.  Start with manufacturing/00_START_HERE.py.
#   known_answers/     four problems whose answer is known on paper (a cantilever beam, a plate with a hole, water in a channel, heat through a slab), each with the answer worked out in the file.
#   ../internal/        about a hundred cases, each trying to break one thing on a part made for it.
#
# THESE ROUNDS (2026-10-21 to 2026-10-26): what to do, and what you must see.  Open the file named in each line (File > Open, or drag it onto the window).
#
#  1. A SYMMETRY IN THE TREE.  Open examples/29_symmetry.py and click the row of the symmetry (`mirror`) in the model tree -- or take the guided tour (Help > Guided tour) to the step "A mirror" and click it
#     there.  YOU MUST SEE: its planes drawn, the whole model still in the picture and the camera where it was.  (Before, a click on the row took the camera in on the few millimetres round the
#     symmetry's centre and every model left the picture.)
#
#  2. THE WHEEL ON A POINT.  Open examples/26_profiles_and_sweeps.py, select nothing, put the mouse on the dot of one of the five points of the plate and turn the wheel with Shift held -- or Ctrl held.
#     YOU MUST SEE: the first notch selects the point (its gizmo and the gold ring appear, and `position=`, `rotation=` and `scale=` are written into its call); the next notches scale it, 10 % a notch, and the plate's outline follows
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
#  6. THE EXAMPLES.  File > Open example file: the files 00 .. 37 and the folders step and meshes -- no walkthroughs, no manufacturing folder, no verification folder (the known-answer problems are in tests/verification/known_answers).
#     Run examples/33_manufacturing_optimization.py (about five minutes): the bracket as the optimizer made it (right) and its result for the printer (left), grey where it is as it was, red where
#     material was added; the mill's and the mould's results are models of the tree (the eye shows them).
#
#  7. THE PRINTER SHOWCASE, THE THREE PLACES (manufacturing/01_printer.py; Shift + left drag turns the view, the wheel zooms).  Look at each from the side named:
#     a. THE FIN (front, in front of the plate): from the FRONT (View > Front), zoomed on it.  YOU MUST SEE: a bar 2.0 mm wide and 12 mm high, red on both sides of the grey core, flat on top
#        and meeting the plate in a straight line -- nothing lifted above its top -- and the same on the left and on the right.  What is left at the foot, and is known: a small widening on one side and a
#        slight taper on the other, 0.3 mm at the most (the picture is a lattice of nodes 0.34 mm apart and the foot is within a node of the exact result; the numbers and the exports are the exact result).
#        A fillet 0.5 mm wide, or a slit of air along the foot, is the defect this round fixed: if you see one, it is back.
#     b. THE SLOT (between the two blocks): from the TOP (View > Top), zoomed on it.  YOU MUST SEE: one red slab as wide as the slot, from block to block, the tops of the two blocks beside it grey
#        and flat, no red rim on them and nothing sticking out at either end -- the same at the top end and at the bottom end.
#     c. THE GUSSET (behind, on the left, under the ledge): from the RIGHT (View > Right), turned a little with Shift + left drag, zoomed on it.  YOU MUST SEE: a red pyramid under the ledge whose
#        sloped faces meet the wall along straight lines, with no teeth and no white gaps between the red and the wall, and no slab sticking out of its foot onto the plate.
#
#  8. THE LATTICE MENUS.  Right-click empty space in the viewport, first with a script that has no model, then with a box selected, and open Operations.  YOU MUST SEE: two categories at the bottom of
#     the list -- Periodic lattices (lattice, lattice_surface_conform and the four families of cells) and Non-periodic lattices (random_points, voronoi_lattice, delaunay_lattice, surface_lattice).  With no model
#     every operation of the menu is there, none greyed out, and picking one writes `...` where the model goes (only center and bounding_box, which measure a model, are left out).  With the box selected,
#     Non-periodic lattices > surface_lattice writes `surface_lattice(box_1, cell_size=..., ...)` and a net of beams covers the box.
#
#  9. THE LATTICES THAT DO NOT REPEAT.  Open examples/34_non_periodic_lattices.py (about three minutes; the progress bar shows it).  YOU MUST SEE: row 1, a net of beams over the whole surface of a block with
#     a dent, the hollow and the edges included (cells like hexagons), the same net in triangles, and the net inside a ball; row 2, the foam whose beams the surface cuts off (stubs at the surface) beside two foams
#     with a clean surface -- a net of beams on the ball's skin, joined to the foam inside, in Voronoi cells and in triangles.  Change `spacing=6` in a `random_points` call and the tile follows.
#
# 10. THE HEAT SINK.  Open examples/35_thermal_topology_optimization.py (about half a minute).  YOU MUST SEE: the optimized part coloured by its density, red where the metal is solid: thick trunks from
#     the strip on the left edge (the sink) that branch out towards the rest of the plate, the same above and below the middle line; the console says the plate's heat-weighted temperature, the
#     optimiser's own value and the value of the part analysed again (lower: only the metal makes heat in it); the card steps through the iterations.
#
# 11. THE SYMMETRIC MESH.  Open examples/06_modal_analysis.py (about twenty seconds) and look at the first mode from the front (View > Front).  YOU MUST SEE: both lugs moving alike, the same red on
#     both (the first mode, about 1507 Hz); before the mesh was mirrored one lug moved about twice as far as the other.  Then open examples/17_flow_topology_optimization.py (about four minutes): a slender
#     spindle with pointed ends in the stream, the same above and below the middle line; the card says a drag coefficient of about 0.3 and a lift coefficient of a few thousandths (a symmetric body has no lift:
#     that is the noise of the places of the lattice, about 1 % of the drag, and the one flow that is drawn has a lift of 1e-11 of its drag).
#
# 12. THE ELEMENTS OF A RESULT, AND THE CUT THROUGH THEM.  Open examples/05_static_analysis.py, then examples/07_thermal_analysis.py, examples/06_modal_analysis.py and examples/16_fluid_flow.py, one after the
#     other.  On the result card of each: the button "Elements" (and, below the colour bar, the number of nodes and elements).  YOU MUST SEE: pressing it draws the tetrahedra of the part, coloured by the value of
#     each element; then View > Section view (Shift+X), choose an axis and move the plane through the part: with the elements shown the card of the section has "Whole elements", and with it ticked the plane keeps whole
#     elements instead of cutting them smooth.  The thermal result shows the temperature, the flow its speed, a mode the displacement of that mode.  A result with more than 8 000 000 tetrahedra has no such button
#     -- its card says how many there are and that they are too many to draw.  A small solid part has millions at the default element size (a cube 0.4 mm on a side: 2 678 400): its elements are drawn.
#
# 13. THE ENGINEERING MENU.  Right-click empty space in the viewport, open Engineering.  YOU MUST SEE: "topology_optimization" first, on its own, a line under it, and then Structural, Thermal, Fluid and
#     Manufacturing -- no topology_optimization inside Structural -- and a last category, Mesh, with mesh_volume and mesh_surface.
#
# 14. THE ICONS.  Right-click empty space, open Primitives (and Parametric shapes, Operations > the lattice categories, Engineering > New material / New support or load ...).  YOU MUST SEE: an icon of its own
#     beside every entry: a ball for the sphere, a cylinder, a cone, a ring for the torus, a pyramid, a prism; outlines for the circle, the rectangle, the ring, the polygon, the triangle; a sheet, a globe, a tube
#     for the surfaces; rings, layers, noise, waves for the fields; a wall with hatching for fixed, an arrow for a force; an ingot with its element for a metal, a drop for a fluid; and a different picture for
#     each cell of the lattices.  Then open examples/05_static_analysis.py: the rows of the model tree show the same pictures.
#
# THE NIGHT OF 2026-10-25/26 (the build of 2026-10-26 02:48).  Open walkthroughs/07_the_night_of_2026_10_26.py first: it draws snap, the corners and edges of the right-click menu, custom_resolution as an
# argument row, an imposed displacement and a body as the region of a support in five groups, and its first lines say what to do and what you must see in each.  The rest is the interface:
#
# 15. DELETE AND ESC.  Select a model (the viewport or the tree) and press Delete: it goes, as with D.  Make a work plane (Ctrl+Shift+W) and press Esc while a line is being drawn, then again, then again.
#     YOU MUST SEE: one step each time -- the line being drawn is dropped, then the drawing mode is left, then the work plane's view is closed.  Esc also closes the section view (Shift+X), then cancels a
#     render that is running, and in the console (Ctrl+`) it hands the keyboard back to the editor (this last one has not been seen in a window).
#
# 16. A PLANE ON A SURFACE.  Select one flat face of a box (right-click it > Select Surface) and press Ctrl+Shift+W.  YOU MUST SEE: a work plane lying on that face, turned to face you.  (On a bent
#     surface the plane is the one of the projection that covers the largest area: not seen in a window yet.)
#
# 17. A SPLINE IS SELECTED LIKE ANY MODEL.  In walkthroughs/07 click the thin blue line of `curve_1` (above the block): it is selected.  Select the block, then Ctrl+click the line: it joins the selection.
#     Right-click empty space with nothing selected in a script that has only a spline, Parametric shapes > New 2D shape > profile: YOU MUST SEE a rectangle of four points of its own; the spline is not
#     passed to the profile.
#
# 18. THE THEME, THE TERMINAL, THE BLOCKS.  Settings > Theme...: choose Dark (and Light again), change a colour by hand, Reset.  YOU MUST SEE: the editor, the trees, the dialogs and the output change at once, the
#     defaults come back with Reset.  The terminal under the editor is there as soon as a file is open (output above, a `>>>` line at its foot; Ctrl+` puts the cursor in the line; drag the bar above the pane to make it higher or lower):
#     type `1 + 1` and press Enter (2), `print("a")` (a), `undefined_name` (a traceback of your line only); Shift+Enter is a new line, Up and Down go
#     through what you ran.  In the editor every object (its lines, its placement, its handles) is one block with its arrow, and every second block is a shade darker.  Click the arrow of the first line of an object (try one whose definition runs over several lines, one with a handles statement, one that is hidden): YOU MUST SEE every line of that object go,
#     the closing bracket of its definition, its handles(...) / lock(...) lines, the line that shows it (or its `# hidden:` line), and "... N lines" on its first line.  The next object stays where it is.
#
# 19. THE ORTHOGRAPHIC VIEW IS THE DEFAULT.  Start FielDes with no settings file (a new user): the cube of any script has parallel edges, the top face of a cube in the iso view is an exact rhombus.
#
# 20. THE RENDER SETTINGS ARE ARGUMENTS TOO.  Open the render settings row of the model tree and click Resolution, Quality or Region.  YOU MUST SEE: the number is selected in the code, ready to be typed
#     over, as a variable's is.
#
# 21. THE IMPOSED DISPLACEMENT.  In walkthroughs/07 group D: a beam held at one end whose tip is pushed 1 mm and no force at all.  YOU MUST SEE: total load 0 N, support reaction 2344 N, compliance
#     2344 N mm, maximum displacement 1 mm (at 2 mm elements 2063 N; beam theory with shear says about 1940 N, the clamps stiffen it a few percent more).  Change `inf` to 0 and the tip is held in y too.

# 22. THE MESH FOR ANOTHER PROGRAM.  Open examples/36_meshing_for_cfd.py (about ten seconds to mesh).  YOU MUST SEE: a green channel with a round hole where the post stands (the post is taller than the channel), the
#     card "Element size (mm) - per element, press Elements" at the lower right; press Elements: the cells, small and blue at the post (0.4 mm) and large and orange away from it (3 mm), the three layers of prisms
#     in the hole; the output says "made in 5-6 s", 29,000 tetrahedra and 3,000 prisms, zones inlet, outlet and five walls, and a quality report: no element inside out, the worst skewness 0.92, 0.35 % of the
#     cells above 0.85, none above 0.95, orthogonal quality 0.36 at worst.  The four files (Gmsh, Fluent, OpenFOAM, ParaView) are in the folder the last line names.  In the dark theme the card and the cells
#     are as readable.  Right-click empty space > Engineering: Mesh > mesh_volume, mesh_surface.
#
# 23. THE CAD OPERATIONS.  Open examples/37_cad_toolbox.py (a few seconds).  YOU MUST SEE: a flange with a hub, a bore through its middle, six countersunk bolt holes round it, six radial slots between the
#     bolts and three gussets from the hub to the rim (a slanted face each); from above (View > Top) the holes, the slots and the three gussets are evenly spread, the gussets 15 degrees off the bolts.  The output:
#     "volume 38048 mm^3", a section at z = 3 of 4392 mm^2, at z = 16 of 607 mm^2, the hub overlapping the plate by 4247 mm^3 and the gussets 5.0 mm from the bore.  Right-click a model: Moving has rotate,
#     reflect and mirror, Repeating has linear_pattern and circular_pattern, Extruding has revolve, Combining has cut_plane and hole; Parametric shapes has capsule, tube and slot.
#
# 24. THE POINTS TO SNAP TO.  In walkthroughs/07 group F (y = 170 .. 210): a drum (a cylinder), a cone snapped under it on its axis, and a block with rounded edges.  WHAT TO DO: right-click the drum on its top face
#     far from the rim; then right-click the front rim of its bottom; then the front upper corner of the block (the rounded one at x = 110, y = 170); then the middle of its upper front edge.  YOU MUST SEE: on the
#     face only "Point at the centre of this face"; at the rim also "Point at the centre of the circle" and "Point on the edge"; at the rounded corner "Point at the rounded corner"; on the rounded edge "Point on
#     the rounded edge" -- and picking one writes `circle_center_1 = circle_center_at(drum, (x, y, z))` (or `face_center(select_surface(...))`, or `corner_at(..., rounded=True)`) with a dot where it belongs:
#     at the middle of the circle, at the middle of the face, and on the SHARP corner and edge the rounding replaced, just outside the rounded block.  The cone `under` stands apex down below the drum, coaxial
#     with it, its base touching the drum's bottom: it is the cone `funnel` placed (`position=`, `rotation=`) as S places it when the faces and the two centres of the circles are selected (item 31).
#
# 25. PLACEMENT IS ARGUMENTS.  Make a box (right-click empty space > New 3D shape > box) and click it in the model tree.  YOU MUST SEE: the box in the tree starts CLOSED; opened, its last rows are
#     `position`, `rotation`, `scale` and `alpha`, dimmed, each with its default; hover one: "position is not written in the call: it has its default, (0, 0, 0). Click to write it ...".  Click `position`:
#     the call of the box gets its placement arguments and the number is selected in the code, ready to be typed over.  The gizmo is on the box; drag an arrow: the box moves and the numbers of
#     `position=` change in the code.  There is NO `gizmo(...)` line anywhere in the script.  Type `alpha=0.4` into the call: the box is drawn see-through.  Ctrl+D on the box: the copy has its own placement.
#
# 26. WINDOWS OF THEIR OWN.  Every major element has a small button at its top right: the code editor, the console and output, the model tree, the section, field and result cards, the work plane, the symmetry
#     and the viewport.  Press the one of the model tree.  YOU MUST SEE: the tree in a window of its own (put it on another screen, if you have one); in the viewport the card is gone; its button now says it
#     puts the tree back; press it, or close that window: the tree is where it was.  The same for the code editor (the editor's place in the window holds a placeholder with "Put it back" while it is out,
#     and Ctrl+S, Ctrl+Return and the other keys still work in its window) and for the console and output.  THE VIEWPORT: press its button.  YOU MUST SEE: the model drawn in a window of its own, turned by Shift + left
#     drag, zoomed by the wheel, selected by a click, with the cards that float over it; the main window shows the placeholder.  Put it back: the model is drawn there again, nothing lost.  Quit FielDes with
#     the tree pulled out and start it again: the tree is pulled out again, where you left it.  (If any element of these does not draw, take input, or come back cleanly, that is a defect to report.)
#
# 27. UNITS.  Settings > Units...: choose Length: in, Stress and pressure: psi, Temperature: F.  YOU MUST SEE: the ruler of the viewport in inches, the numbers of a result card and its colour bar in psi (open
#     examples/05_static_analysis.py and run it), the value under the cursor and the work plane's step in inches; the script itself unchanged (a box of 10 is still 10 mm: 0.394 in on the ruler).  File >
#     Export mesh writes the file in inches; The button "Millimetres, newtons, megapascals" puts everything back to mm, N, MPa, kg and C.  The dialog is as readable in the dark theme.
#
# 28. SOLVERS OF YOUR OWN.  Settings > Solver...: a list with the analyses that are built in (the equations are shown), the solvers of the library (`heat`, `elasticity`) and the files of your solvers folder.
#     Select `heat`, press "Make an editable copy", name it `heat2`, change the conductivity in the editor, press "Save and check": "Saved and read: ..." shows.  In the script, `solve('heat2', part, ...)` draws the
#     temperature of the copy like any result (docs/solvers.md has a first solver to start from).  A syntax error is shown by "Not saved -- ..." with its line and nothing is written; any other mistake leaves the file in the list as "(error)".  THE WAY BACK: change the equations, press "Save and check", then press
#     "Reset to default": a question, then the text the copy started with is in the editor (not yet saved: Ctrl+Z undoes it) and "Save and check" keeps it; "Previous version" puts back the text before the last save.
#     "Reset to default" is grey only for a file that was put into the solvers folder by hand (it has no default of its own); a copy, a new solver and a file added with "Add from a file..." have one.  "Delete" asks first.
#
# 29. VALIDATION AND ACCURACY.  Help > Validation and accuracy.  YOU MUST SEE: a window with the list of sections at the left (Static analysis, Modal analysis, Thermal, Fluid flow, the optimisations, ...,
#     CAD operations and snapping, Solvers of your own, What is not validated) and the page at the right; a click on a section scrolls the page to it; the page is readable in both themes.
#
# 30. NAVIGATION.  Settings > Navigation > "Left drag turns": drag the left button on empty space.  YOU MUST SEE: the view turns (turntable); Alt + left drag pans; a click on a model still selects it; Ctrl + left
#     drag draws the selection rectangle.  "Blender style": Shift + middle drag pans.  "Custom...": a window with a drop-down for turning, panning and zooming (a mouse button with Shift, Ctrl or Alt held);
#     choose, press OK: the gestures you chose work, the ones you left on "None" stay as they are for everyone.  Back on "FielDes": the original gestures.
#
# 31. THE S KEY.  In walkthroughs/07 group F: select `funnel_base`, `funnel_middle` (on the cone), then `drum_bottom`, `drum_middle` (on the drum) in the model tree, in this order, and press S -- or right-click the
#     bodies' faces (Select Surface) and the centres of the circles yourself and select those.  YOU MUST SEE: NOTHING NEW IS MADE -- no `snap_1`, no hidden cone: the call of `funnel` gets `position=(var(...), ...)` and
#     `rotation=(var(...), ...)`, the cone is carried under the drum at once (the same mesh, displaced: no new calculation, no waiting), apex down, on the drum's axis, and the model tree selects `funnel` with its
#     gizmo, whose arrows drag the numbers.  The faces and points you made on the cone moved with it: they are still on it.  Ctrl+Z puts the cone back in one step.  With only features of one body selected, a message
#     says why nothing was done.  The same in walkthroughs/07 group A, where the bracket is snapped twice (the second on top of the first).
#
# 32. THE TREE AFTER THE LAST ROUND.  (a) Type `a = union(box_1, ...)` into the editor: the tree shows the statement with a placeholder row, as every other statement does; the Delete key and the bin of a row
#     delete a statement that has a placeholder.  (b) Right-click a model > Operation > offset: the call is written with the model as a SHADOW (`# shadow: name`), and the model keeps its row; drag the model onto a
#     placeholder instead and it moves inside.  (c) Right-click a model > Engineering > Mesh > mesh_volume: only the body is written, every other argument has its default.  (d) Select a flat face and press
#     Ctrl+Shift+W: the work plane is made on it and the view turns to look straight at it.
#
# 33. THE NEW BASE SHAPES ARE DRAGGABLE, FACE BY FACE.  Right-click empty space > Parametric shapes: capsule, tube, slot.  YOU MUST SEE: the capsule and the tube with the cube icon of a 3D shape and the slot with
#     the square of a 2D shape, each with a gizmo (a capsule is a body, not a field).  Press the side of the capsule and drag: its radius changes and the ends stay round; press the tip of an end: its length changes (press it off the tip, on the round shoulder, and the pull goes along the surface's normal, as it does for every surface of the program: the capsule grows longer and wider together);
#     the tube: the outer wall, the bore and each end move on their own; the slot: an end makes it longer and keeps the round end, a long side makes it wider.  `threaded_rod` (Primitives > Parametric shapes) pulls by its ends.
#     Each drag changes one number in the `handles(...)` line, never a scale.
#

print("Open one of the files listed above.")
