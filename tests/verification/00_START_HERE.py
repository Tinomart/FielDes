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
#  1. A SYMMETRY IN THE TREE.  Open examples/29_symmetry.py and click the row of the symmetry (`mirror`) in the model tree -- or take the guided tour (Help > Guided tour) to the step "A mirror" (the symmetry is selected there already) and click it
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
#  5. THE TOUR, IN CHAPTERS (see item 34 for the whole of it).  Help > Guided tour, chapter 3, the step "Your primitives" (after "Make things"): the menu bar's Primitives menu is drawn with "Manage primitives..." framed; "Show me" opens
#     the window on the selected model's kind and closes it again.
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
#  8. THE LATTICE MENUS.  Right-click empty space in the viewport, first with a script that has no model, then with a box selected, and open Engineering.  YOU MUST SEE: two categories in the list, under the
#     domains (Structural, Thermal, Fluid ...) -- Periodic lattices (lattice, lattice_surface_conform and the four families of cells) and Non-periodic lattices (random_points, voronoi_lattice, delaunay_lattice, surface_lattice).  With no model
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
# 14. THE ICONS.  Right-click empty space, open Primitives (and Parametric shapes, Engineering > the lattice categories, Engineering > New material / New support or load ...).  YOU MUST SEE: an icon of its own
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
# 25. PLACEMENT IS ARGUMENTS.  Make a box (right-click empty space > New 3D shape > box) and click it in the model tree.  YOU MUST SEE: the box in the tree starts CLOSED, and its lines in the code editor start FOLDED with it (the gutter's arrow is closed: the tree and the code are one state; an object with a placeholder to fill in starts OPEN in both); opened, its last rows are
#     `position`, `rotation`, `scale` and `alpha`, dimmed, each with its default; hover one: "position is not written in the call: it has its default, (0, 0, 0). Click to write it ...".  Click `position`:
#     the call of the box gets its placement arguments and the number is selected in the code, ready to be typed over.  The gizmo is on the box; drag an arrow: the box moves and the numbers of
#     `position=` change in the code.  There is NO `gizmo(...)` line anywhere in the script.  Type `alpha=0.4` into the call: the box is drawn see-through.  Ctrl+D on the box: the copy has its own placement.
#
# 26. WINDOWS OF THEIR OWN.  Every major element has a small button at its top right: the code editor, the console and output, the model tree, the section, field and result cards, the work plane, the symmetry
#     and the viewport.  Press the one of the model tree.  YOU MUST SEE: the tree in a window of its own (put it on another screen, if you have one); in the viewport the card is gone; its button now says it
#     puts the tree back; press it, or close that window: the tree is where it was.  The same for the code editor (while it is out the viewport takes the editor's room: no empty space is left in its place,
#     and Ctrl+S, Ctrl+Return and the other keys still work in its window) and for the console and output.  THE VIEWPORT: press its button.  YOU MUST SEE: the model drawn in a window of its own, turned by Shift + left
#     drag, zoomed by the wheel, selected by a click, with the cards that float over it; in the main window the editor takes the whole width, with no empty space left where the viewport was.  Put it back: the model is drawn there again, nothing lost.  Quit FielDes with
#     the tree pulled out and start it again: the tree is pulled out again, where you left it.  (If any element of these does not draw, take input, or come back cleanly, that is a defect to report.)
#
# 27. UNITS.  Settings > Units...: choose Length: in, Stress and pressure: psi, Temperature: F.  YOU MUST SEE: the ruler of the viewport in inches, the numbers of a result card and its colour bar in psi (open
#     examples/05_static_analysis.py and run it), the value under the cursor and the work plane's step in inches; the script itself unchanged (a box of 10 is still 10 mm: 0.394 in on the ruler).  File >
#     Export mesh writes the file in inches; The button "Millimetres, newtons, megapascals" puts everything back to mm, N, MPa, kg and C.  The dialog is as readable in the dark theme.
#
# 28. SOLVERS OF YOUR OWN.  Settings > Solver...: a list with the analyses that are built in (the equations are shown), the solvers of the library (`heat`, `elasticity`) and the files of your solvers folder.
#     Select `heat`, press "Make an editable copy", name it `heat2`, change the conductivity in the editor, press "Save and check": "Saved and read: ..." shows.  In the script, `solve('heat2', part, ...)` draws the
#     temperature of the copy like any result (the part as its elements, each coloured by its temperature: the first example of docs/solvers.md gives a plate that is blue at its cold end and red at its hot one, 20 to 80, the card says "temperature - per element") (docs/solvers.md has a first solver to start from).  A syntax error is shown by "Not saved -- ..." with its line and nothing is written; any other mistake leaves the file in the list as "(error)".  THE WAY BACK: change the equations, press "Save and check", then press
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
#     the tube: the outer wall, the bore and each end move on their own; the slot: an end makes it longer and keeps the round end, a long side makes it wider.  `threaded_rod` (Parametric shapes) pulls by its side (the diameter: the pitch stays) and by its ends -- by its side the same wherever you press the thread (a crest, a root, a flank facing up or down, either side): the arrow of the pull points straight away from the axis, and the same drag gives the same new diameter.
#     Each drag changes one number in the `handles(...)` line, never a scale.
#
# 34. THE GUIDED TOUR IN CHAPTERS.  Help > Guided tour (the first time: the welcome card).  YOU MUST SEE: the title of every card whole -- the place in the tour ("Chapter 1 of 13 · 2 / 6") is a small line ABOVE it --
#     and a button "Chapters" on every card but the first and the last: it lists the thirteen chapters (a tick for the done ones, the one you are in in bold) and goes to the one you choose.  Chapter 1 is the idea of
#     FielDes (the script is the model, change a number, every shape is a field -- the section view --, a field instead of a number, what that means for design -- the point moves and the thickness with it --,
#     an analysis as a field); chapter 2 the view (the standard views by a click on a point of the little coordinate axis in the corner, your own navigation, panes pulled out -- click the button of the
#     viewport, it goes out, click it again, the step turns green --, the gizmo, the keys); chapter 3 the model tree, with the step "Snap one body
#     onto another" (Show me: tip, then corner are selected, S: the peg stands on the corner, only its position changes) and "Copy or link"; 4 import and export; 5 work planes; 6 symmetry (one key makes it, Planes 2 fits it to the table, a leg and its images, editing the original or an
#     image, Shift+S on and off); 7 the special features in short (lattices, simulations, flows,
#     manufacturing, apps -- each with a picture of its own); 8 to 13 build a lattice, a static analysis, a flow, a topology optimization, a manufacturing optimization and an app.
#     WHAT TO DO IN 8 TO 13: do each step by hand -- select the models (Ctrl+click), right-click the first, choose the entry the card names, type over a placeholder or a number -- or press Show me, which does it with a
#     pretend hand (a Ctrl badge on the cursor, the menu entries lit one after the other, the typing) and puts everything back.  YOU MUST SEE: the card turns green when the step is done; the next step starts from the
#     model the menus wrote, also when you skipped the step before; a result is drawn (the lattice, the stress colours, the streamlines, the density, the red of a broken rule).  Every chapter ends with a card that says
#     "Next up: chapter N" and what that chapter is about (the chapter that was finished is named in a small line only), with Continue and "Explore on my own"; "Explore on my own" shows the card that says where the tour is found again, and Help > Guided tour then goes on at the next chapter.  The place you were at is
#     kept: quit in the middle of chapter 9, close FielDes, open it and choose Help > Guided tour: it opens in chapter 9 at the step you left.
#     LOOK AT THE RIGHT EDGE OF THE CARD: on a card with all five buttons (Chapters, Quit the tour, Back, Show me, Skip step) the last button must end inside the card's border, also with a larger system font (Windows:
#     Settings > Accessibility > Text size) -- the card is as wide as its row of buttons needs.  And in the build chapters the result is drawn ALONE: the beam, the analysis and the part are hidden (`# hidden: name`) as
#     the next step needs the result seen, not a body drawn over it.
#     IN YOUR LANGUAGE: Settings > Language (or the language of the computer): every card of the tour, the Chapters menu, the chapter-end cards and the buttons are in Spanish, French, German, Japanese,
#     Chinese or Russian; the names of the code (fixed_1, static_analysis, app.number) stay as they are, and no button is cut off in any of them.
#

# 35. THE LAST FIXES.  (a) THE ROWS OF THE MODEL TREE.  Make a box and open its row: `a = (-5, -5, -5)`, `b = (5, 5, 5)`, `position = ...`, `rotation = ...`, `scale = 1`, `alpha = 1` and `Render settings` are whole, to the
#     right edge of the tree, also with a larger system font -- no `a...)`, no blue blank space to their right.  (b) A FLAT OVERHANG OF A SHARP BOX.  `part = union(box((-6, -6, 0), (6, 6, 16)), box((-18, -6, 16), (18, 6, 22)))`,
#     `manufacturing_analysis(part, constraints=[fff_constraint(nozzle=0.4, overhang=45)])`, then View > Bottom.  YOU MUST SEE: both undersides of the cap red, the stem's bottom grey between them, the card saying 12 %
#     for the overhang.  (c) THE GUSSET.  The same T with `rounded_box((-18, -6, 16), (18, 6, 22), 0.3)` for the cap, through `manufacturing_optimization(part, constraints=[fff_constraint(nozzle=0.4, overhang=45)], max_change=0.25)`,
#     then View > Bottom and View > Front.  YOU MUST SEE: the added gussets red under both ends of the cap, each one clean surface -- no ribs, no cream streaks --, a straight 45 degree line in the front view, and "Places still
#     breaking a rule: 0" on the card.
#
# 36. THE TOUR, THE NIGHT'S PASS (open Help > Guided tour and go through what is new; each was done with real clicks in a window and looked at).  (a) A PLACEHOLDER TYPED OVER.  Right-click `drilled`, Operations > Offsets and
#     walls > offset: the row `o = ...` is amber; click it and type `.5`.  YOU MUST SEE the call stay open in the code and in the tree, `o=.5` where the three dots were, and the step "Placeholders" turn green.
#     (b) THE STATIC AND THE FLOW CHAPTERS (9, 10) take the faces from the viewport: right-click the left end of the beam, Select Surface, Select -- the face is lit in magenta and is `selection_1`; the same for the
#     right end; then fixed, force, the material and static_analysis are made from the right-click menus with `beam` and the face selected.  YOU MUST SEE `fixed_1 = fixed(region=selection_1, body=None, ...)`: a face says where
#     the support acts, so the call has no body (the menu writes it so).  (c) NEW STEPS: chapter 3 "A shape from math" (Parametric shapes > New 3D shape > low_level_body, then the radius changed to 8 + (x * 0.8).sin() * 1.2),
#     "Points, edges and faces" (the corner of a body, before the snapping); chapter 6 starts with "Make a symmetry" (click in the viewport, Shift+S: `symmetry_1`, its card says In use) and "Fit it to the table" (press 2 under Planes in its card: planes=2 is written, green); "Copy or link" is in chapter 3 (green only when the copy has been moved and a number of bolt changed after it); chapter
#     8 has "Points for a foam", "The foam" (Engineering > Non-periodic lattices > random_points, then voronoi_lattice: about a hundred points, the foam drawn in 2 seconds), "A cell of your own" (Periodic lattices > Custom
#     cells > custom: the true custom cell), "Fill the block with it" and "Change the cell" (see item 39).  The result steps of chapters 9 to 12 are each ONE step with its isolation: green only once the result is isolated with I, and the arrow goes to the result's row;
#     while the calculation runs only its bar is lit.  The flow's inlet and outlet, and the support and load of the beam, are made from the face alone (click selection_1, right-click it).  "Send a model out" (chapter 4) and "Send it as a file"
#     (chapter 13) write nothing: the save window does not open (the status line says so).  The last card has "Open the examples".
#     In "A skin for the foam" (chapter 8) surface=False is changed to surface=True on the foam: the dots are in the block and on its surface only, none stands outside it, and the foam is Voronoi all through and closed by the surface: nothing is cut off, no beam ends open at a face, and the surface is a net of beams that wraps round the edges of the block (no beam runs along an edge: frame=True is for that).  Right-click a model > Custom resolution: the
#     model's row and its lines open and stay open (no frame of opening and closing again) and the number is selected -- type it.  Rest the mouse on Parametric shapes > threaded_rod: the preview shows its thread (128 cells across, 0.17 mm each, in about 0.2 s): the turns are clear and the small teeth along their crests are the size of that cell, the same as the rod itself shows at that cell, not a coarse blob.  (d) THE CARDS.  The line with the place ("Chapter 1 of 13 · 6 / 6") is a handle: drag the card by it, it stays where you put it until the
#     step changes; text of a card can be marked with the mouse and copied (Ctrl+C, or right-click on the marked text: Copy; the result card and the notes have Copy the text where you right-click a place that
#     nothing answers: one row a line, "maximum von Mises stress 53.36 MPa"; right-click the red error banner of the viewport: Copy the error); a step whose model takes seconds says "FielDes is working on what this step
#     shows. One moment." under its text, also while Show me does it.  (e) REAL CLICKS COUNT.  Chapter 2 "Standard views": click a point of the little axis in the corner -- the step turns green; "Make room for your
#     work": click the viewport's button (top right), it goes out, the card says how to put it back, click the same button in its own window: green.  Chapter 13 "See it as the user will": click File in the menu bar --
#     the arrow goes to the entry -- choose Preview as app: green (close the window that opens).  And chapter 13 "Send it as a file": File > Export as app...; press Save in the window that asks where to save (the tour has put the name of a temporary file in it, deleted when you go on; "Send a model out" in chapter 4 does the same): green, and no
#     question about saving the tour's model when the tour is left.  (f) LATTICES AND MATERIALS.  Right-click empty space > Engineering: Periodic lattices and Non-periodic lattices are there (not under Operations) and
#     Materials has New material and New fluid together.
#
# 37. THE SOLVED PROBLEMS THAT COME WITH THE PROGRAM.  In a folder `seed-cache/result-cache` beside the program's `python` folder (made by scripts/make-seed-cache.ps1, copied by the build scripts): take the tour to chapter 9
#     "Run the analysis" -> Show me, or do it by hand.  YOU MUST SEE the stress drawn at once, and in the console `[result cache] read the static analysis back (4.1 MB, 0.01 s, shipped with the program)`; the same
#     for the flow of chapter 10.  Set FIELDES_NO_SEED=1 for the cold solve (the beam of chapter 9 about three seconds, the flow of chapter 10 about twenty -- the card says "FielDes is working on what this step
#     shows. One moment." under its text, also while Show me does it).  The program carries eight solved problems (21.5 MB).
#     (b) THE MANUFACTURING RESULTS AND THEIR PICTURES: the printer's analysis and optimization of chapter 12 are shipped the same way, in `seed-cache/result-cache/manufacturing/` (one folder to a result: all the fields
#     in one file of the kernel, the checks and numbers as a head; 5 of them, 6 MB: the tour's own model and the model a selected part makes -- a part that is selected has its placement and handles as var()s)
#     and the meshes of their pictures in `seed-cache/render-cache/` (8 meshes, 36.5 MB: the picture of the check of a selected part takes 80 s to make, the others 5 to 12).  Take the tour to "See where it breaks
#     the rule" -> Show me, then "Let the tool fix it" -> Show me.  YOU MUST SEE each result on the screen about seven seconds after the pretend hand has finished (the hand itself takes a few seconds), and in the
#     console `[manufacturing store] read the manufacturing analysis back (0.5 MB, 0.02 s, shipped with the program)`, `read the manufacturing optimization back (2.3 MB, 0.17 s, shipped with the program)` and
#     `[render cache] read the mesh of line ...: 253283 vertices, 13 ms`.  With FIELDES_NO_SEED=1 the tool runs (10 s) and the pictures are made (up to 80 s) and the console says `kept the manufacturing
#     optimization` and `[render cache] kept the mesh`; opening the program again the same call is read back from the user's own copy (no "shipped" then).  Settings > Clear the caches deletes the user's
#     copies, the solved problems and these (not the shipped ones).
#
# 38. THE FOOT OF A PART ON INTEGER COORDINATES.  Paste into a new script: `view.set_bounds([-24, -12, -4], [24, 12, 28])`, `view.set_resolution(3)`, `stem = box((-6, -6, 0), (6, 6, 16))`, `cap = rounded_box((-18, -6, 16), (18, 6, 22), 0.3)`,
#     `part = union(stem, cap)`, `result = manufacturing_optimization(part, constraints=[fff_constraint(nozzle=0.4, overhang=45)], remedies=['gaps'])`, and look at `result._preview` from the front (View > Front).
#     YOU MUST SEE the stem's bottom corners square, as the box is.  (Before the fix of the mesher each bottom corner of the stem had a small wing, 0.75 mm sideways and 0.5 mm below the floor, on the picture only: the exported
#     mesh, the numbers and the checks never had it.  Cause: the mesher placed the vertex of a cell at a corner that lies exactly on its grid from a sample whose derivative was float noise of 1e-8, normalised to a
#     direction that means nothing and a plane 8000 mm away; such a sample no longer makes a plane.)  The same in the tour: chapter 12, "Let the tool fix it", Show me, View > Bottom and Front.
#     `FIELDES_DC_TRACE=x,y,z,r` in the environment prints what the cells of the mesher within r of a point were given (for whoever meets another such place; nothing is printed without it).
#
# 39. THE TOUR AFTER A MORNING OF USE (Help > Guided tour; the chapters as numbered now, thirteen).  (a) Chapter 2 "Make room for your work": press the viewport's button; the card is now IN the viewport's window, near
#     its top right corner, with a frame and an arrow at that window's own button; press that button: the viewport goes back, and the card with it.  (b) Chapter 2 "Hold Shift": hold Shift with the model selected -- the card
#     turns green.  (c) Chapter 3 "Rename" and every step with the model tree: the rows show their names at the narrowest width of the panel.  (d) Chapter 3 "Sections": the frame is on the thin gap above drilled, not on
#     the row.  (e) EVERY STEP ABOUT A MENU (Import model, Export mesh, Save as custom block, Show the blocks folder, Preview as app ...): while the menu is shut the card stands UNDER its button and the arrow comes straight
#     up to the button -- it does not run along the bar.  (f) CHAPTER 4, IMPORT AND EXPORT: File > Import model... opens in the folder of the example parts (choose PivotBearingSupportBracket.STEP: the step turns green once
#     `import_model(` is in the script); File > Export mesh...; File > Save as custom block (the status bar says where: `drilled_block.py`); Settings > Show the blocks folder (the file is in it); right-click empty space >
#     Parametric shapes > New custom block > drilled_block makes `drilled_block_1 = drilled_block()`.  The block is plain Python: the model's lines in a function, `var(5)` written as 5, the shown model returned.  Saving again
#     updates the same file.  (g) EVERY STEP THAT ASKS FOR SEVERAL MODELS frames all of their rows in the tree (snap: tip and corner; sweep: profile and route; lattice: part and walls; the cell: part and the cell; the
#     analysis: beam, fixed_1, force_1, aluminium_1; the flow: slab and the face, then post, slab, inlet_1, outlet_1, water_1; manufacturing: part and fff_constraint_1).  The two face steps of the static analysis ask for the
#     face alone: click selection_1, right-click it, fixed.  (h) EVERY STEP THAT STARTS A CALCULATION (the analysis, the flow, the optimizations, the manufacturing analysis and optimization, and the steps that change what they
#     use) waits for it: the bar above the console -- or, while only the picture is being drawn, the bar at the bottom of the viewport -- is lit and framed while it runs, and the card and the frame turn green together when it has finished.  Each result has a step of its own after it, "See ... alone": click the
#     result's row and press I (only the result is shown; I again brings the others back).  "Change the load" asks for a direction: (0, 0, -100) to (0, 100, 0), and the red moves to the side of the beam.  (i) The flow chapter is
#     in the order of the static one: face, inlet, face, outlet, the fluid, the solve.  (j) Chapter 5 "Sweep a shape along a line": the picture holds the circle, the line and the tube; select profile and route and press Ctrl+E
#     (or use the menu): the same path_extrude line.  (k) Chapter 6 "A mirror": the symmetry you fitted in the step before is still there, selected -- nothing is loaded again --; the table has no legs; with the symmetry in use, make a cylinder (right-click empty space, Primitives > New 3D shape > cylinder): its images are the
#     other legs.  (l) Chapter 7 "Simulations and optimization": the optimization runs in front of you every time (cache=False in its call); "Apps": File > Preview as app opens the example 25_app_beam_load_case as an app.
#     (m) Chapter 8: the lattice and the foam are written as the menu writes them (one argument to a line); the foam is made with the plain points and seen, then, in "A skin for the foam", surface=False is changed to surface=True and the surface closes the cells
#     (nothing is cut off, the net wraps round the edges); "A cell of your own" is the true custom cell (Custom cells > custom: a box and a sphere in it, cell_custom(region, geometry)), the block is filled with it (the lattice call has no thickness), and then "Change the cell": the sphere's radius 2.5 is made 3.5 and the whole block changes.
#
# 40. THE SNAPPING MODE (TAB), THE RADIUS OF A POINT FIELD, THE ICON OF A FIELD.  (a) Click in the viewport and press Tab: a violet frame, a faint see-through lattice, a slice of the whole space from a little in front of the place you look at out into the distance (and a fainter stretch of rows that reaches farther toward you), fading with distance from you (the three axes in their own colours, and every other line in a pale tint of its axis's colour -- red along x, green along y, blue along z --; look at two lines that run side by side: the nearer one is stronger, thicker and lighter),
#     the card "Snapping" in the top right corner, and "Snapping: grid ... mm, turns ...°" at the bottom left.  Tab again, or the card's cross, leaves it.  The card: Grid (the lattice on or off), Grid step (Auto, a number, half, double),
#     Angle step (a number and 1 5 15 45 90), Distance (the viewing distance of the lattice, in per cent of the distance from you to the place you look at: it is seen out to there and fades towards it); and two switches side by side, Show grid and Snap to points.  ZOOM in and out and PAN: the part of the picture that shows the lattice stays the same -- its lines move apart or together, because the grid keeps its size (Auto is a round number from the size of the region, a tenth of it: 2 mm for a region 20 mm across) --, and ZOOM FAR OUT: the lattice does not vanish, it gets coarser -- where its lines would be closer than about 40 pixels only every 2nd, 5th, 10th ... one is drawn and the line at the bottom left says "lines shown every ... mm" (every step is still snapped to).  Raise Distance: more of the depth is seen.  (b) Select a model, drag an arrow of its gizmo: it goes from node to node of the grid -- its position numbers are
#     multiples of the step.  Drag a square of the gizmo: the same, in that plane.  Hold Alt while dragging: it moves freely.  (c) Turn a ring of the gizmo: in the mode every turn is a multiple of the angle step; with the mode off,
#     the Shift gizmo turns in the same step (5 degrees until you change it).  (d) Right-click empty space and make a sphere: it is put on a node.  (e) Open the work plane view (Shift+W): the mode goes off and its card goes; leave the
#     view (Esc): the mode is back.  (f) The tour, chapter 2, "Snap to a grid": Tab turns the step green; Show me presses it for you.  (g) The icon of a field in the viewport: click it, its viewer opens; click the same icon again, it closes
#     (before, only a click on empty space did).  (h) In a script: `p = point(0, 0, 0, influence=10)` and `distance_to_point(p)` is the distance from the sphere of radius 10 round p -- -10 at the point, 0 at 10 mm, positive outside;
#     `radial_field(p, 'z')` is the same round the axis; `distance_to_point((0, 0, 0))` is the plain distance, as it always was.    (i) THE INFLUENCE OF A POINT, APART FROM ITS SCALE.  A point has two numbers that its gold ring can drag: its influence
#     (`point(..., influence=r)`: how strongly a spline leaves it, 1 is the natural curve; the factor of a round corner's radius; the radius of the field round it) and its scale (how thick a sweep along a spline is there).
#     Make a `spline` through four points and sweep a circle along it (`path_extrude`): select a point of the path and drag its ring -- the TUBE gets thicker there and the curve does not move; hold Shift and drag the ring -- the CURVE
#     bulges rounder or tighter there and the tube keeps its thickness.  On a point of a sketch (a corner, a round corner) the ring is its influence.  The two used to be one number.
#     (j) WHERE IT SNAPS TO, AND THE SAME STEP EVERYWHERE.  With the mode on, move the mouse over the viewport: a violet ring with a cross is on the node a click would land on (a square when it is a point that is there), and beside it its x, y, z in the unit.
#     Drag an arrow, a square, a point or a face of a gizmo: the ring jumps from node to node where it will land, and goes when you let go.  Put `view.set_bounds([-2, -0.5, -2], [2, 6, 2], auto_scale=True)` in a script and press Tab: the card (Grid step: Auto),
#     the line at the bottom left and the spacing of the lattice (read it against the scale bar) are the same number, 0.5 mm.  Move the mouse over a model: the ring is on the node nearest the place of its surface that is under the cursor, and stays there as the mouse moves a little.
#     Move it over empty space: the ring is on the node the cursor is nearest to on the screen, one of the rows near you as well as one near the model, and the node that is marked stays marked while the cursor is near it (it does not jump from
#     row to row with every pixel).  Right-click there (empty space or the surface of a model, Primitives > New 3D shape > sphere): the sphere is made on that node, its numbers are the ring's numbers (with auto scale on, the region grows to hold it);
#     the lattice is a readable slice round the place you look at, with a fainter stretch toward you, so that you can tell the grid, the model and the mark apart at a glance; the mark is a small ring with a cross, and while the right-click menu is open it stays where the sphere will be.
#     (k) THE PREVIEW OF A MENU ENTRY.  Right-click empty space, open Primitives > New 3D shape and rest the mouse on `sphere` for a moment: the sphere it would make is shown, see-through and shaded like any body (no wireframe, no edges), where it would be made -- on the snapping mode's node when
#     that is on -- and goes when the mouse leaves the entry or the menu closes.  The same on `box`, `cylinder`, `cone`, `torus` and on the entries of Parametric shapes: any entry whose call needs nothing of the script and renders quickly shows its shape; one that is made of
#     models of the script (an operation) shows nothing, and one that takes longer than a moment is not shown.  Try `rounded_box`, `capsule`, `tube` and `threaded_rod` under Parametric shapes > New 3D shape (`threaded_rod` is shown whole too, at its own size), and
#     a field (`radial_field`), a point, and a cell of the lattice menus (`gyroid`): they have no body to show, so nothing is shown.
#     (l) THE SNAPPING LATTICE AS JUNCTIONS.  `view.set_bounds([-2, -0.5, -2], [2, 6, 2], auto_scale=True)`, `sphere(1, (0, 1.5, 0))`, press Tab: the lattice is a round patch round the middle of the viewport that fades toward its rim, each junction
#     is a small star -- its beams full strength at the junction and fading out toward the middle of the line --, the beams of a near junction are longer than those of a far one, and the lines are whole at the near end (only the distance fades
#     them).  The program starts in an ORTHOGRAPHIC view (View menu): switch to perspective and the stars are bigger toward you as well.  Rest the cursor on any junction, also a faint one in the background: the junction nearest the cursor on the
#     screen, and no other, carries the violet mark (and its numbers) and is the one a click puts a model on, and the junctions within about 80 pixels of the cursor come up with longer beams, the nearest the most; move away and they go back.
#
# 41. SURFACE, FRAME, CLIP AND POINT_CLOUD OF A FOAM (docs/lattices.md).  Paste into a new script: `view.set_bounds([-14, -10, -1], [14, 10, 7], auto_scale=True)`, `view.set_resolution(12)`, `plate = extrude(rounded_rectangle((-12, -8), (12, 8), 4), 0, 6)`,
#     `points = random_points(plate, spacing=2.5, frame=True)`, `foam = voronoi_lattice(points, thickness=0.8)`.  YOU MUST SEE the plain foam, cut by the plate, and a full beam round the outline of the top and of the bottom -- straight along the
#     sides, round at the four rounded corners, no gap -- and NO beam at the four rounded vertical corners (a smooth fillet is no sharp edge); the beams of the frame are cut by the plate with the rest (clip=True, the default).  Make `surface=True`
#     as well: the foam is closed by the surface, a net of beams on the faces that wraps round the edges (no beam runs along an edge by itself), and the frame is on the outline.  With `frame=False` and `surface=True` only the net: no beam along any edge.
#     `voronoi_lattice(points, clip=False)`: the whole lattice is left whole -- the cells of the plain foam reach out past the plate, and the beams of the frame stand half out of it.  A cylinder (`cylinder(8, 20)`, spacing 2.5) with `frame=True`: both rims are full circles.  A sphere cut by a cylinder (`intersection(sphere(8, (0, 0, 0)), cylinder(5, 20, (0, 0, -10)))`, spacing 2.5) with `frame=True`: a full ring of beams on each of the two
#     circles where the two round faces meet (the edge between two round faces is followed along the surface); a sphere with a cylinder through it (`union(sphere(6, (0, 0, 0)), cylinder(3, 20, (0, 0, -10)))`): four rings, the two concave creases and the two flat ends.
#     A block (`box((-10, -10, 0), (10, 10, 14))`, spacing 5): all twelve edges.  A ring (`torus(14, 5)`) with `frame=True`: no sharp edge, the frame adds nothing and nothing breaks.  `random_points(plate, frame=True)` does not change the points or the cells:
#     the plain foam of the plate is the same with and without it.  (b) POINT_CLOUD: make five or more points (right-click empty space > Primitives > Points > point, or `point(x, y, z)` in a script), select them all, right-click > Engineering > Non-periodic
#     lattices > point_cloud: `point_cloud([point_1, point_2, ...])` is written (the points are dragged to the places wanted by their gizmos and the cloud follows); select the cloud and choose voronoi_lattice: a foam around exactly those points, clipped by the box
#     that holds them (`point_cloud(points, body=part)` clips it by a body).  The rim of the tour's foam call has `frame=False` and `voronoi_lattice` has `clip=True`, as the menu writes them.
#
print("Open one of the files listed above.")
