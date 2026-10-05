# Changelog

## 0.1.0 — first beta

The first release under the name **FielDes**. It is what libfive Studio became on Windows: a design and
analysis workbench driven by fields, now with its own name, design and documentation.

### Application
- **The model tree fills in while the script runs.** A statement that takes a while (an import, a smoothing, an analysis)
  no longer leaves the tree as it was until the whole script is done: once a statement has been running for a moment, the
  tree lists the variables of the statements that are finished, as soon as they are, and the finished script's tree
  replaces it. A script that stops with an error leaves the variables made before it in the tree too. (The viewport still
  shows the shapes when the script has run.)
- **Opening a file clears what the old one made.** The viewport and the model tree were left showing the previous script until the new one had
  run (and for good when it failed), which looked as if opening it had done nothing. Opening a file, a new script or the
  tutorial now empties both at once; what a run of the old script delivers late is thrown away.
- **The sphere a new file starts with works with the handles at once**, and is moved by the shared gizmo of a
  multi-selection: it is named and its numbers are exposed, as a primitive from the right-click menu is. An expression
  displayed on its own (`sphere(3)`) that is selected together with other models is named first (`sphere_1 = sphere(3)`),
  because the shared gizmo moves variables.
- **Several selected models are in a multi-select state of their own and are moved by one gizmo.** Two or more selected
  models are moved by one gizmo, at the middle of them, with its arrows and its centre dot, at once -- whatever way each is
  edited when selected alone, which is kept and is there again as soon as the selection is one model (a model without
  numbers to move it by gets a gizmo line in the mode it is in). Nothing is pulled by its surface meanwhile. `E`
  changes each model's own gizmo mode (what the tree's buttons show); the viewport shows the shared gizmo (unless every
  selected model is set to *never*).
  The **dot in the middle of every gizmo** drags the model (or all of them) freely, in the plane facing the camera.
  Turning and scaling stay one model at a time.
- **Locked models cannot be multi selected.** Adding one to a selection (Ctrl+click, a Shift range, a rectangle) leaves it
  out and says so, with a *do not show this again* box (Settings → Show hidden messages again). A locked model alone can be
  selected, which is how it is unlocked; `R` on several models locks them and they leave the selection.
- **More keys, and one rule for every toggle.** `V` shows / hides and `C` turns the render cache on / off, beside `E`
  (the gizmo mode), `R` (lock) and `I` (isolate). All of them work on all selected models: if the models are not all
  in the *on* state (shown, locked, cached) they are all put there first, and only when they all are in it are
  they all turned off, so a mixed selection is not flipped model by model. Isolate shows the selected models and hides
  every other one.
- **`D` deletes the selected models** from the script (definition, show / hide, handles, lock and cache lines), one
  undoable step for all of them, with one question if the rest of the script still uses their names. The model's
  right-click menu has **Delete** too.
- **A click on empty space deselects everything** (not with Shift or Ctrl held).
- **Dragging a surface is always there; the gizmo has a mode of its own.** The gizmo/handles toggle is gone. A model's
  surfaces can be dragged whatever else is set, and the gizmo -- which has priority where it is shown -- has three modes,
  set by the tree's gizmo button and the `E` key, which goes round them: **click** (the default: the gizmo shows while the
  model is selected), **never** and **always**. `handles(x, ..., mode='click' | 'never' | 'always')`; `'gizmo'` and
  `'handles'` are refused, and `G` and `H` are gone. Selecting a model makes it ready: it gets the numbers its gizmo moves
  it by (a `handles(...)` line) and, if it has none and they are few (up to 120), the numbers that place its surfaces
  (`expose(...)`); a bigger shape has **Make its surfaces draggable** in its menu. An excluded shape cannot be dragged by
  its surfaces. The shared gizmo of a multi-selection shows unless every selected model is *never*.
- **Selecting several models.** Ctrl+click adds a row of the model tree or a model in the viewport to the selection (or
  takes it out), Shift+click in the tree selects the rows from the last one clicked to this one, as in a file list, and
  Left-dragging in the viewport draws a selection rectangle that selects every model lying wholly inside it (with Ctrl held:
  added to the selection); the view is turned with Shift+left-drag or the middle button instead, and right-drag still pans. The selected models are lit up in the viewport; the order they were selected in is kept.
  `I`, `E`, `R`, `V`, `C` and `D` work on all of them.
- **Operations on a selection.** With several models selected, *union*, *difference* and *intersection* in the right-click
  menu are no longer greyed out: *union* unites all the selected models, *difference* subtracts every one after the first
  selected from the first, *intersection* keeps what they share, written as one call under the model defined last.
- **Selecting in the viewport leaves the keyboard in the viewport.** A click on a model no longer moves the keyboard to
  the code editor (it did after a click on a draggable model, and when another editor tab was showing), so the single keys
  that work on the selection are not typed into the script; a click in the viewport takes the keyboard from the editor.
- **Lock is a switch of its own.** A lock button (key `R`) locks or unlocks, by writing and deleting the line
  `part = lock(part)` under the definition, and the gizmo mode the part had is still there when it is unlocked. The `L` key and the `M` (next mode) key are gone, and so is
  `handles(mode='lock')` (and `show=`): lock with `lock()`.
- **Isolate** (`I`, with the viewport or the model tree focused): only the selected model is shown, every other model
  hidden; `I` again shows what was shown before. It is made of the same script edits as the tree's eyes (undoable),
  and the tree says which model is isolated.
- **A surface selection is shown as the patch lit up on the model**: the vertices of the model's surface mesh that
  belong to the patch are coloured and all the others are not drawn (the field itself, a thin layer across the patch, came
  out as disconnected fragments at the viewport's resolution). The new selection is also the selected model in the tree.
- **Right-click menus in the viewport.** On empty space: *New primitive* (every primitive of the library, placed where the
  cursor is: the point of the ray under it closest to the origin, about a hundred pixels across) and *Add operation*
  (offsets and walls, smoothing and rounding, moving, repeating, combining); hovering shows the functions, choosing one
  writes the call into the script with a line showing it and selects the new model. On a model: *Operation* (the same
  list, with that model passed in) and *Select Surface* (the menu that used to open directly). The list the menus offer is
  one file (`fieldes/menu_catalog.py`) and a test runs every entry.
- **Results are shown by stating them**, as shapes are: `result` on a line of its own displays the part coloured
  by the result (the stress, the temperature, the speed, the density, a mode), with the result card. `show()` is gone.
- **The result card steps through every result** -- a slider with play / pause, the same for all analyses: a
  static result from 5 % to 100 % of the load, a mode through a cycle of its vibration, a flow in time through its
  stored times, a topology optimisation through its iterations (the part as it was after each one).
- **Flow view**: a fluid result is drawn like every other result, with streamlines from the inlets, coloured by the
  speed, and particles moving along them drawn over it (the **Flow** button of the result card hides them); a steady
  flow's steps are its solver iterations, the flow developing from the Stokes start.
- Fixed in the flow solver: the triangles of the rounded edge between two slip planes were adopted by their region
  by comparing with the region's mean normal, which is zero for a region of two opposite planes (both faces of a
  slab, both its sides): one side's edge was then a wall at rest along its whole length and that side dragged (the
  speed along it 15 % low). They are now compared with the region's own triangles at their corners.
- The flow solver's incomplete-LU preconditioner is built and applied in blocks, one per core with a little
  overlap (additive Schwarz): the linear solves, most of a flow's time, now run on every core.
- The result card's play has a mode (round and round, back and forth, once) and a speed (a drop-down, ×⅛ to ×4),
  with one-step back and forward buttons beside it; the step slider has a row of its own, the card's whole width.
- **Every analysis is kept across sessions.** Static, modal, thermal and flow analyses and every optimisation are
  written to the result cache (the mesh, every field, the modes, the iterations, a flow's steps) and read back when
  the same problem is asked again in a later session -- nothing is meshed or solved again, as a field is not built
  again by the field cache. A result read back keeps the keys of its fields, so its renders are found again too.
  `[result cache] read ... back` in the log.
- A result's colours are drawn as finely as the solver's elements: the render's triangles are split (red-green, no
  seams) until no edge is longer than the element size, so a field across a flat face is no longer smeared between
  a few corners.
- New name, icon and look: a light interface around the dark viewport, a dark top dock in the style of the viewport cards, with Open and Import icons at
  the left and a Settings menu (keyboard shortcuts, text size, navigation), a slim editor gutter, a compact model
  tree (300 px), small output text, designed for maximised windows on large monitors.
- The window always starts maximized.
- The meshing options are in the View menu; the Debug menu is gone. Text size is `Ctrl++` / `Ctrl+-`.
- Shorter texts: the result card (a short note, details in a tooltip), the section card, tooltips, the guide and
  the messages in the output pane.
- The shape reference opens at a sensible size.
- Model tree: the region-of-interest button is gone; shorter tooltips; per-part Reimport; handles button
  (gizmo or handles) and lock button, also on displayed expressions: a primitive such as `sphere(3)` is given a name
  and its surfaces become draggable.
- Section card: **Whole elements** appears only when the plane cuts a part shown with analysis elements.
- Legends: closing a legend stops probing.
- Scripts run in their own folder, so relative paths work.
- The guide (`Shift+F1`) rewritten for FielDes.
- Editor: **go to definition across files** (library functions, methods, modules next to the script) opens the
  file in a **tab**. The first tab is the script that is rendered (marked with ▶ and bold); the others are for
  editing only, saved with `Ctrl+S`; a module of your own that is saved runs the script again. Completion
  writes the brackets of a function and puts the cursor inside them.
- The key shortcuts `E` (the gizmo mode) and `R` (lock / unlock) work on the selected models; the gizmo sits at the
  model's centre also for shapes made with `var()` numbers.
- The dark parts of the window and the viewport are a little lighter, so the editor is less of an insert.
- Only Python is supported as the scripting language (the Scheme/Guile binding and the other language
  bindings of libfive are removed).
- **Y points up** in the viewport by default (Settings → Rotation mode switches to Z up).
- **Delete button on every row of the model tree** (a bin; the old reset button on imports is a turn-back arrow
  now): removes the object's statement, the line that shows it, hides it and its `handles()` / `expose()` lines,
  after asking when the rest of the script still uses the name; one undo brings it back.
- **Right-click a model: select a surface.** A menu with the variables of a flood fill (flat or smooth spread, angle,
  thickness, radius) writes `selection_1 = select_surface(part, seed=(x, y, z), angle=10)` and a line showing it into
  the script, under the model's definition. The patch is lit up in the viewport.
- **Boundary conditions are drawn.** `static_boundary_conditions(part, supports, loads)` is a shape in the model tree
  (eye, delete) that colours the fixed, sliding and loaded places of the part and draws the symbols of structural
  analysis programs over it -- an array of arrows over a loaded face with the total force beside it, an array of flat pads over a
  support with its name, one arrow for gravity with its value -- by the viewport, so that they are never clipped by the render
  region or ragged at a coarse resolution, and has its own legend. It is the **only** way to give supports and loads: `static_analysis(part, conditions, ...)`,
  `modal_analysis(part, conditions, ...)` and `topology_optimization(part, conditions, ...)` take it as the second
  argument, and the `supports=` / `loads=` arguments (and the lists of `fixed()` and `force()` in their place) are
  gone — the old calls stop with an error that says what to write.

### STEP import
- Solids are rebuilt as fields from their faces: planes, cylinders, cones, spheres and tori exactly; B-spline
  faces by fitted closed-form surfaces, with the fit deviation reported and painted on the model.
- Assemblies arrive assembled, each part in its own units, each occurrence named.
- **`exclude(shape, region, ...)` locks a region of any shape or field against everything done to it afterwards.** It
  cuts the shape into two fields that are always united again: the *free field* (the shape outside the region), which
  every later operation reshapes, and the *locked field* (the shape inside it), which none of them touches. Offsets,
  shells, smoothing, lattices, unions, cuts and the like work on the whole shape and then put the locked field back;
  moving, turning, scaling and mirroring carry both along; copying (`array_*`, `symmetric_*`, `repeat`, `mirror_*`) and
  deforming (`twist_z`, `bend_z`, `taper_*`, `attract_*`, `repel_*`, `twirl_*`) would tear them apart and raise an
  `ExcludedError` -- do them before `exclude()`. For a part imported from a STEP file the locked field is **the part's own
  exact surface, meshed directly from the STEP file and made a field** (the mesh importer's distance field), instead of
  the import's fit: no more mesh union at render time, so the exact-region code of the viewport and the kernel is gone
  (`exact_region_mesh()` too: the field is the mesh). The region is any shape; several regions given at once are their
  union; without a region it is `poor_fit_region(part)`, the places where the fit is poor, made from the fit marker.
  Given the whole import it works on every part the region reaches; given one part (or one entry) on that part only.
  `exclude(body, other, ...)` is also in the right-click menu (Combining), with the shape selected first and the regions
  after it. An excluded shape cannot be dragged by its surfaces. `topology_optimization`,
  `thermal_topology_optimization` and `flow_topology_optimization` read the exclusions a shape carries as regions to
  **keep** by themselves, however many operations came in between; example 08 excludes the lug holes with it.
  **Free-form (B-spline) faces now mesh correctly**: per-face (u, v) triangulation with shared edges
  (watertight), exact spline evaluation, adaptive edge sampling, tilt- and Delaunay-aware edge flips and
  refinement, verified against OpenCascade on the test set.
- Free-form faces that close on themselves (a tube, a curved rail) mesh correctly also in mirrored parts:
  the end of an edge that ran past its vertex, and a closing triangle, made the refinement of the face
  look folded and it was discarded.
- `auto_exclude=True` on `import_step_parts` (opt in): excludes every place where the fit deviates more than
  `exclude_threshold` percent of the face's size, with a field region, not boxes.
- Per-part render resolution (`roi_resolution`), import cache keyed to the algorithm's version.
- **An imported part is one continuous solid.** The cells it is rebuilt from used to touch inside the material
  with a field of exactly 0 on the wall between them, so an inward offset, a shell, a skin or a lattice (the
  field-driven design example) showed internal walls, and a mesh exported from such a part had zeros where a
  re-import has to tell inside from outside. The cells are now bridged by their consensus (see the
  architecture notes); the surface is unchanged, handles still drag the surfaces and the part stays solid. The
  field **outside** the part is exactly what it was (verified against the unbridged field on every example part), so
  the section's field view, outward shells, thickening and offsets read the same. Parts imported earlier are
  imported again once (the import cache is versioned).
- **Mesh import reads back what was exported.** Where the nearest feature of a mesh has no clear side (a sliver
  folded back over its neighbour, a knife edge: a dual-contouring mesh has them along sharp edges), the sign of the
  distance came out wrong, so points a few millimetres from a part exported with `save_stl` read as inside it on
  re-import, and an offset of the imported mesh broke. The generalised winding number now decides there.
- `expose()` with plain numbers no longer reads freed memory, and a plane through the origin that several
  unions share gets one number, not one per union.
- **Offsets, shells and thickening of an imported part use its exact distance.** The field of an imported part is
  exact in sign, but the value outside it is only the distance to the infinite surfaces its faces lie on (and
  inside, the depth is as inaccurate), so `thicken`, `shell_inside`, `shell_outside`, `offset_by` and `offset` of a
  part grew fins, slabs, notches and stray walls that have nothing to do with the operation (on the bracket of the
  examples 11.8 % of the vertices of a 5 mm thickening were off by more than half a millimetre, up to 10 mm).
  These functions now take the part's exact distance (the mesh of the part and the distance to it, cached: made
  once per part, 0.3 s for the bracket) where they are given an imported part: 0 % misplaced vertices. The plain
  field of the part, and with it its display, its section and its analyses, is unchanged. The exact
  distance follows `handles()` (a gizmo, with its var() numbers) and `move`, `rotate`, `scale` and `reflect` in the
  script. A part whose faces are dragged (`expose()`) has the exact distance of its numbers when the script was
  run (the part as edited, when the script is run with other numbers), and while a face is being dragged the
  change of the part's own field on top of it, so a thickened part follows the drag and is exact again at the
  next run. `FIELDES_NO_EXACT_OFFSETS=1` uses the part's own field. Other shapes (a union of parts, a lattice)
  keep their own field; `exact_distance()` is what to use on those.
- **Meshing a field that holds a mesh's distance is 2.5 to 3.5 times faster** (an imported STL, and the exact
  distance above): the dual contouring's search for the surface along an edge took 64 points per edge, each of them
  a search of the mesh's triangles, where a bisection of 10 points finds the same crossing (to a 1024th of the
  cell, past what a float holds at the coordinates of a part); the search of the triangles starts from the one
  the last point was nearest to, and does each box distance once. The vertices are where they were (checked on a
  mesh against its own surface). Fields without such a part are not touched.
- **Dragging a surface of a shape that holds an oracle** (a mesh's distance, so the exact distance above, or a mesh
  import) pulled nothing: the evaluator that works out how the field changes with each `var()` gave the oracle the
  point of an earlier call, so the field was read at the wrong place, and let the oracle's gradient in space into
  the lanes that hold the derivatives with respect to the variables (an oracle only reads the point, so it does not
  change with them). Both are fixed; a drag of a thickened part now moves its surface.
- **A mesh (or any oracle) moved with `var()` numbers follows them.** The oracle that holds a transformed copy of
  another (`move(mesh, (var(5), 0, 0))`, a gizmo on an imported mesh, the exact distance of a part with a gizmo) took
  its numbers as 0 whatever the script said, so it stayed at the origin while the shapes made of arithmetic moved.
  The evaluators now pass every variable to their oracles (`Oracle::setVar`), and the application counts the
  variables inside an oracle among the ones a shape depends on, so such a shape renders again when one of them is
  dragged.

- **`import_step_tessellated_parts()` / `import_step_tessellated()`: a separate importer for parts that are almost all
  free-form (B-spline) faces** — sculpted bodies, gears, worms, threads, impellers — where the fitted closed-form
  surfaces of the main importer do not follow. Nothing is reconstructed or fitted: every solid is tessellated
  straight from its trimmed faces and the triangles are made the exact signed distance field of the part (the way
  nTop makes an implicit body of a B-rep). The parts, their order, names, units, bounds and assembly placements are
  those of `import_step_parts()`. The tessellation is kept in `<file>.fieldes-tessellation` next to the STEP file. Measured
  against OpenCascade on the test files (more than 260 parts): face areas agree to 0.05 to 0.1 %, volumes to 0.5 %; against
  the main importer the inside and outside of every part agree except four engine pistons (1 to 2 % of the points,
  where it matches OpenCascade). The whole of Keukencombinatie (90 parts, 412 free-form faces) imports in 22 s,
  Cribadora (111 parts) in 16 s, PT.stp in 7 s, the Bandextruder with its worm gear in 7 s, where the main importer
  takes minutes or fails on the free-form parts. Meshing a part takes 0.5 to 2.8 times as long as the main
  importer's (about as long for the worm gear).
  See [Importing STEP files](docs/step-import.md#parts-that-are-almost-all-free-form-import_step_tessellated_parts).
- **The tessellator of the exact STEP surface** (`exclude()` and the tessellating importer) is faster and more exact:
  the free-form faces of a solid are refined on all the threads there are (a worm gear: 31 s to 4 s); a cylinder or
  cone with windows that wind round it (helical grooves) is cut open along a helix (the face came out 79 % too big);
  holes of a face round a periodic surface are put where the outline is (a cross hole came out 76 % too big); a
  sphere's cap has circles of latitude (a ball of two hemispheres was two cones: half its volume); tori are refined,
  kept only where the mesh's area gets closer to the patch's own (fillets were 8 to 11 % too small); the holes of a
  free-form face that closes on itself are moved a period to where its outline is (two round holes of a 750 mm panel
  were not cut out: 5 % too much area and volume); the two circles of a zipped band start at the same place (a
  sphere's zone came out a twisted strip, 13 % too big).
- **A rational B-spline arc that is not circular** (a quarter ellipse of 250 x 100 mm has the weights of a circle's
  arc) was read as an exact circular cylinder or cone, in the main importer too. It is now checked at two more points
  and an ellipse becomes the free-form face it is. (Files with such faces are imported correctly after
  **↺ Reset** of the import; the import cache is not invalidated.)

### Analysis and design
- **Topology optimization warns when the optimised part falls into pieces.** A tetrahedral `topology_optimization` counts
  the separate pieces of its result (`.pieces`, specks under 2 % not counted) and, if there is more than one, prints
  "the optimised part is in N separate pieces ... try a higher volume_fraction". Kept regions (`keep=`, an excluded
  region) use up part of the volume asked for, so the free material has less; the loose scraps stay in the result on
  purpose, as they show what the part would become with more volume.
- **`smooth(shape, radius, steps=1)`**: smooths the surface of a body without thickening it. The body's field is averaged over
  the points a `radius` away on all six sides, `steps` times, so bumps, dents and stair-steps smaller than about the radius go,
  edges and corners are eased, and flat faces stay where they are (`offset` and `thicken` move or grow the surface); more
  `steps` smooth further. Beside `round_edges` (convex edges only) and `fillet` (concave only). **It is a field and nothing
  else**: a weighted sum of copies of the body's own field moved by whole radii (6 copies for one step, 19 for two, 44 for
  three, 85 for four; more steps are four at a larger radius), so no surface is measured or meshed inside it -- the mesher
  reads the field when it draws it, as for any other. It measured the distance with a mesh at every step before: a 20 mm
  box with a region excluded, `smooth(r=1, steps=3)`, took 75 s (4 s of script, 71 s drawing it). The arguments `bounds` and
  `resolution` are gone with the meshing. On an excluded shape it smooths the field the shape was made from, not the united
  one (whose values are cut off at the distance to the region), so the smoothed surface no longer ripples round the
  region; the locked part is put back as it was.
- Static, modal, thermal and thermal-stress analysis on body-fitted tetrahedra (or voxel hexahedra),
  structural and thermal topology optimisation, several load cases.
- **Fluid flow.** `fluid_analysis(domain, conditions, fluid, element_size, time=None)` solves the incompressible laminar
  Navier-Stokes equations through or around any shape (the fluid is where the field is negative) on the same
  body-fitted tetrahedra as the other analyses -- the steady flow, or with `time=(duration, step)` the flow in time
  from an impulsive start (backward Euler, every stored step a `FluidStep` with its fields and numbers, the result
  card stepping through them). `result.streamlines()` gives the paths of particles through the flow. The conditions are regions: `inlet(region, velocity= | speed= | flow_rate=,
  profile='uniform' | 'developed')`, `outlet(region, pressure)`, `wall(region, velocity)` (every other surface is a
  wall at rest), `slip(region)`; `Fluid(name, density, viscosity)` with `water`, `air`, `oil`, `glycerol`. The results
  are fields (`speed`, `vx`, `vy`, `vz`, `pressure`, `total_pressure`, `shear_rate`, `vorticity`) and numbers (the
  flows, the pressure drop, the force on the walls, the dissipation, the Reynolds number, how many elements lie
  across the passages). Linear velocity and pressure with SUPG / PSPG stabilisation, Picard then Newton iterations
  from the Stokes solution, BiCGSTAB with an incomplete LU (a direct LU for small systems); wall forces as the
  reactions of the discrete momentum equations. Verified against plane Poiseuille flow (pressure gradient within 1 %
  at 10 elements across), Hagen-Poiseuille pipe flow (within 1 % at 17 across), the Stokes drag on a sphere in a tube
  (within 2 % of Haberman & Sayre's wall factor) and the lid-driven cavity of Ghia, Ghia & Shin (1982) (extrema within
  5 % at Re 100 with 64 cells across, 7 % at Re 400 with 96) -- the numbers are in `docs/analysis.md`. Laminar only,
  first order in time, no boundary-layer elements: see its limits there.
  Example `16_fluid_flow.py`: water past a round post, the velocity field and the wake.
- **Flow topology optimization.** `flow_topology_optimization(body, domain, conditions, fluid, objective, volume,
  region, ...)` takes a body in a stream and changes its shape and topology for the least drag, the most lift, or a
  mix of both, under a volume bound (`volume=1.0` keeps the body's volume, `(0.5, 1.5)` bounds it), inside a
  region`, with `keep` / `avoid` regions and `extrude=`. The body is a level set at the mesh's nodes -- a smooth
  field whose zero level is the boundary, placed to a fraction of an element -- and each element's share of it (the
  exact fraction inside the boundary) sets the friction of a penalised solid (Borrvall & Petersson) in the real
  (Navier-Stokes) flow, solved once per iteration with its exact discrete adjoint. The result is the body
  (`.shape()`, after any iteration too; `.level` the field), the drag and the lift per iteration and the real flow
  around the final body (`.flow`); shown, it is the body in the flow with its wake, and the result card steps
  through the iterations. Measured on the round post of example 16 at
  Re 40: the optimiser's drag 45 % lower over 24 iterations (160 s), the real flow around the slender 21 x 4 mm body
  it makes 51 % below the post's. See `docs/analysis.md`. Example `17_flow_topology_optimization.py`.
- Flow analysis fixes found on the way, for every flow: an inlet or outlet in the middle of a flat face was one
  element wider than its region all round (the flat wall's triangles straddling the rim were taken as the rounded
  edge's); a given inlet speed counted the rounded-edge triangles at full area (+19 % flow on a thin slab) -- the
  inlet's area is now the patch projected along the flow; the rounded edge between two slip planes holds both
  normals; the incomplete LU is kept between solves while it still pays (a flow in time 4× faster per step).
- Fields and regressions: distances, maps, `fit()`, data fields, `colored()`; all results are fields.
- Lattices: nine TPMS families, a dozen strut lattices, planar patterns, Voronoi foams, surface and graph
  lattices, custom unit cells and equations, conformal cell maps, field-driven sizes.
- **A lattice is made of a cell, and every lattice operation takes one.** `cell_periodic(kind)` (a standard strut
  cell, TPMS or planar pattern), `cell_non_periodic(kind)` (`'voronoi'`, `'delaunay'`), `cell_custom(region, geometry)`
  (any geometry, see below), `cell_custom_truss(nodes, beams)` and `cell_custom_tpms(equation)` all make the same kind of object, and
  `lattice()`, `lattice_surface_conform()`, `strut_lattice()`, `tpms()`, `planar_lattice()` and
  `lattice_parameter_for_density()` take it as their `cell` — and only that: **the old forms are gone** (a name string
  such as `'gyroid'` in the cell's place, `kind=`, `surface=`, `unit_cell()`, `tpms_equation()`, `periodic()`,
  `voronoi_lattice()`, `surface_lattice()`); a refused call says what to write instead. `cell_non_periodic` also lays a
  random graph on a surface in `lattice_surface_conform()`.
- **`cell_custom(region, geometry)` is a cell of any geometry.** `region` is a box that gives the extent of the cell and
  `geometry` any field: the cell is their intersection, so the geometry may reach out of the box, and it repeats on any
  cell map (straight, cylindrical, spherical). `lattice(part, cell)` makes cells of the size of the region (`cell_size`
  scales them, `(sx, sy, sz)` stretches them); a region that is not a box is refused; `check=True` warns when the faces of
  the box do not match; `cell.solid()` is the cell on its own; a thickness, radius or density is refused (the geometry is
  the lattice). What `cell_custom` was before is renamed: **`cell_custom_truss(nodes, beams, mirror='')`** for beams between
  nodes and **`cell_custom_tpms(equation)`** for a TPMS equation; `cell_custom(nodes, beams)`, `equation=` and `shape=` say so
  when they are used. `examples/11_custom_lattice.py` shows all three.
- **`lattice_surface_conform(surface_field, cell, depth, cell_size, ...)` takes ONE surface argument**: a body, a
  surface (a field that is only zero on it) or a `select_surface()` selection — which of them it is is found out, not
  told. `within=` keeps the lattice inside any other shape (it replaces the old `surface=`).
- **`union(a, b, ..., radius=0)`** blends the surfaces where they meet with a smooth transition of that radius (mm),
  like the blend radius of a boolean in other tools; the default 0 is the sharp union. It works on any field, so
  lattices (also custom ones) are joined to a part with a fillet.
- **The render cache is on by default.** Every shape keeps its finished mesh on disk once it took 0.4 s or more to
  mesh (`render_cache(x)` keeps it however quick); the model tree's cache button now turns it **off** for a shape
  (it writes `x = render_cache(x, False)`, and deletes the line to turn it on again).
- **`select_surface(shape, seed, angle, mode, thickness, radius)`**: the flood fill of a CAD program as a field —
  the patch of the surface around a point, found over the triangles of the shape's surface mesh, as a thin layer
  that is a region for `fixed()`, `force()`, colouring and lattices. `.patch` and `.whole` are the unsigned
  distances to the patch and to the whole surface.
- **`lattice_surface_conform(surface_field, cell, depth, cell_size, ...)`**: a conformal lattice as in nTop. Its cells
  lie on the surface — a selected face, or the surface of the body — as big as asked along it, each with a face
  towards the surface normal; by default it **fills the body** (one layer for a thin shell), with `side='outside'` the
  layers stand out of it. It is made **from the body's field alone** (the surface is where the field is zero, its
  normal the gradient; no mesh, no distance to a mesh). **The cell map is a set of quads on points of the surface that
  covers the whole of it and is watertight** (every edge on two cells, every vertex one fan, one orientation), made by
  one of **two layouts**, chosen by looking at the surface: a **closed body** gets a layout made from the graph of its
  surface points (a global field of four directions, vertices where the field is singular and on a grid, the surface
  divided into one region round each vertex, a face wherever three regions meet, every face of k corners cut into k
  quads), and a **sheet** (a surface that goes on beyond the region, or has an edge) gets a layout from a scaffold of the
  surface cut by the region. Nothing of the surface is left out: a piece too small for the cells is sampled finer on its own,
  a point has no area and is no surface, and **a closed map of the whole surface is delivered even where the cells cannot
  resolve a part** (a hole or a gap narrower than a cell, a wall thinner than one): the layout is made ONCE, from the grid of sample
  points that `grid_offset` (0 to 3, a setting of `lattice_surface_conform`) picks, and the warning the call prints says where its
  topology may be off. A map is refused only when that grid gave no closed one, and the error says to try another `grid_offset` or a
  smaller `cell_size`: the layout does not try other grids behind your back. The field is evaluated by all the processor's
  threads at once (the same layout whatever their number): an imported bracket of 150 x 120 x 88 mm at 5 mm cells (2650 cells) is laid out
  in 28 s on 12 threads (about 50 s inside the application window), where one thread and two layouts took 154 s. The cells are then
  made regular by moving their nodes along the surface (the cells stay the same cells); a cell is bad when a corner is under 40°
  or over 140° or its longest side more than 2.5 times its shortest: 30-45 % of the cells of a fresh layout are bad, 4-10 % after the
  smoothing. **What must not be in a map is counted exactly:** a cell folded over a neighbour or turned over, one lying on top of
  another, an edge through the air, two corners at one place; the call's warning gives their number, and says when a wall, gap or hole
  thinner than the cell makes up a share of the surface (such a feature can be missing from the map). **Sharp edges and corners are followed**: the samples next to an edge or a corner are moved onto it (the way dual contouring finds its vertices: the planes of the neighbouring samples are fitted, and a move is kept only if the field says the point is on the surface), so the lattice line along an edge is exact and the outline of a part is straight; a corner of the part is always a lattice vertex, with the lattice pinned to it; the face centres are never on an edge; the nodes that sit on an edge or a corner are read from the field and stay there, and the nodes on a straight edge spread evenly along it. The smoothing has a second phase that draws every other node towards the mean of its neighbours (rows next to an edge relax into straight lines). On the test parts with sharp edges 82 % of the vertices next to an edge were off it before, 5 % are now. **Cells are distorted to fit and never deleted for it** (there is no
  `max_stretch`): over a fillet, round the lip of a rim, the cell bends with the surface — its inside is found from its four
  edges, which run along the surface. **The material is filled once**: on a body thinner than its layers (a shell with two
  surfaces, an inner and an outer skin) both faces of a slab are mapped, and a cell is left out only when every point of its
  column that lies in the material is also in the column of another cell that stays (the smaller surface piece goes first), so a
  pan skin has one layer, not two, and nothing it filled is left bare (the material inside no column falls by 0.4 to 2 points). The beams of the unit cell are carried over
  to every cell, so the lattice is a graph of beams that renders about as quickly as one. Strut cells (`octet`, `bcc`,
  `cubic`, `kelvin`, …) with `radius`; or **a periodic surface that follows the surface** (below). No `prism` / `xbrace` /
  `zigzag` trusses here (they needed a mesh of the surface). (The earlier versions — a flat grid across the face, fast marching
  over a mesh of the surface, columns walked from the curve where a plane cuts the surface, a square grid grown as a front, and
  rings grown round one point (with `origin=`) — all left cells that did not close or deleted cells where fronts met, so all are
  gone. The `lattice_surface_confirm` spelling, a typo, is gone.)
  A field that is only a **surface** (zero thickness: where it is zero) gets one layer on one side of it (`side`), cut off at the edge of the shape given as `within=`; `bounds=` gives the extent of a field that has none. Examples `14_conformal_lattice.py` (a surface) and `15_conformal_closed_body.py` (a closed body). **Measured** on 372 runs on bodies the program had not seen (random CSG bodies, mechanical-looking bodies, plates with bosses, holes and pockets, six STEP parts; two cell sizes each; the box a body gets when none is given), against a count of the topology (the Euler number) of the field on a voxel grid taken at three resolutions: of the 229 runs whose count is trustworthy the map has the right topology in 213 (93 %) and a wrong one in 16 (7 %: a hole or a wall about as thin as the cell is missed or invented; two of the 16 are an island of 0.14 mm³ in the pan's field that the map does find and the voxel count cannot see); 46 runs (12 %) were refused with the message above; 97 more gave a closed map of a body whose count is not trustworthy (features thinner than a voxel). Known limits: a feature thinner than a cell can be lost or invented silently when the placements agree on the wrong answer; a thin tube (a 3 mm wall at 6 mm cells) can be refused where half the cell works; about a fifth of the vertices have a valence other than four (a quad layout of a closed surface needs some); a layer that is deeper than a thin plate stands out of it and is taken back into the material, and 1 to 2 % of the material is in no layer's column at the rims.
- **`lattice_surface_conform` with a periodic surface** (`cell_periodic('gyroid')`, `'schwarz_p'`, `'diamond'`, `'neovius'`, `'lidinoid'`,
  `'split_p'`, `'iwp'`, `'frd'`, `'fischer_koch_s'`; `thickness`, `style='sheet'|'network'`, `offset`, `invert`, `skin`): a TPMS
  that follows the surface, one period to each cell on it and to each layer through the depth — nTop's conformal TPMS, and
  a textured periodic surface for a whole part. It is evaluated in the coordinates of the cell a point is in, found from
  the cell's four edges (a Coons patch) and the depth of the layer by Newton's method, behind a bounding-volume hierarchy
  of the cells; the field is a pure function of position and made from the body's field alone, like the struts.
  The cosine-only surfaces (Schwarz P, Neovius, IWP, FRD) join up across the borders of cells; the others may show a seam there.
- The periodic surface on the cells is quicker to evaluate (a cell whose box holds a point but whose map does not is rejected
  after one step, not twelve: 40 → 13 µs a point in a deep layer, a third off the render of the S-surface example), and it is
  kept by the render cache under a hash of the cells rounded to a thousandth of a millimetre: a layout that changes has other
  cells and so another key, the cache never gives the mesh of other cells.
- A point of a flat face has the same gradient in every run: where two parts of a field are equal (a point exactly on a face
  of `box_exact`) the evaluator gave the gradient of whichever it met first, a different one in different runs; a lost gradient
  now comes from differences of the field round the point, and the search grid is shifted off round numbers.
- Automation (developer facility): `waitrender [seconds]` waits for the render to finish before the next command.
- **Two conformal examples**: `14_conformal_lattice.py` (an open surface, with a `cell_custom_truss` cell of your own: a custom cell
  follows a surface when its beams share one radius and it is mirror-symmetric) and `15_conformal_closed_body.py` (a whole bracket).
- **`thickness` sizes every cell**: the wall of a sheet TPMS and the diameter of the beams of a strut or non-periodic cell, in `lattice()`,
  `strut_lattice()` and `lattice_surface_conform()` (before, `lattice_surface_conform` took no thickness for beams, and `strut_lattice` only a
  radius); `radius=` stays as half of it, and both together is an error.
- The conformal lattice is the same in every run (its sample grid is fixed by `grid_offset`).
- **`Shape.save_stl` crashed now and then** (a third of the time on a big mesh): the C function reads trees until
  a null pointer and the Python call did not give it one.
- **Render cache** (keys: the tree as built, meshes by their surface; `render_cache_key(shape)` shows them). A button on every row of the model tree (and `render_cache(shape)` in the script, the line it
  writes) keeps the shape's finished mesh on disk. The same shape — the next time the script is opened, or after
  it was changed and changed back — is shown from there at once; when anything about the math changes (an
  operation, a number, a dragged `var()`, an imported file, the region, the resolution) the shape is meshed again as
  usual and the new mesh is kept. Off unless turned on; shapes that depend on a solved analysis cannot be kept (the
  button turns amber). The folder is limited to 4 GB; Settings → Clear the caches.
- The Python meshing and evaluation functions take the script's `var()` numbers (`libfive_tree_render_mesh_vars`,
  `libfive_tree_eval_points_vars`), so a shape with variables meshes the way the viewport shows it.
- **Dual contouring no longer makes spikes.** A vertex may leave its cell to keep a sharp feature, but a vertex
  more than a cell width away (thin walls only a few cells thick, creases at a nearly flat angle) went hundreds of
  millimetres off; it is now pulled back to the cell widened by its own width.
- Mesh import (STL, OBJ, PLY, 3MF, glTF) as exact distance fields.
- Content-addressed caches for imports, analyses, exact distances and graphs.
- **One cache for every field.** A statement `name = <expression>` whose value is a field (a conformal lattice on a
  bracket, an offset, a thickness field) is remembered by its text, the exact content of what it reads, the `var()`
  numbers and the code that builds it. The same statement is not computed again — in the session, and (when every part
  of the field can be saved) after closing and opening FielDes: the 30 s lattice of the bracket example is read in
  0.15 s with identical values. Lattice struts (`BeamLattice`) now save and load; `libfive_tree_can_save` tells whether
  a tree can be written, and `libfive_tree_save` refuses one that cannot instead of writing a file that fails to load.
  **Settings → Clear the caches** deletes the field cache with the kept meshes.
- **One progress bar for a whole conformal lattice.** A call of `lattice_surface_conform` is one bar from the first step
  to the last (17 steps for a closed body, 10 for an open surface), so the line of the script that makes it shows how
  far it is. The text names the step (`step 6 of 17: joining the samples into a surface`, the round or level inside it);
  every step has its share of the bar (measured on the bracket and the pan), and inside a step the bar counts what has
  really been done. The bar never goes back; a second attempt of the layout takes the rest of it.

### Known limitations
See the README: fitted (not exact) B-spline faces in the field; tori not refined in the exact mesh;
Windows is the only tested platform; linear analyses without contact.

Meshing a field that contains `normal_field()` or `gradient_field()` (the gradient oracle) can crash or hang: its
interval bound is unbounded.

**The mesh of `shell_outside` / `shell_inside` (and of the exact distance itself) of an imported part has cracks, and
is slow.** The exact distance is the distance to a mesh of the part; meshing the shell (a field with that oracle in
it) at 2 samples per mm leaves about 14,000 open edges for a frying pan, along the first planes the mesher splits its
region at, and takes 36 s against 0.6 s for the part's own field; the mesh changes with a sub-voxel shift of the
region and between runs. `thicken` of the same part (surfaces at plus and minus half the thickness only, none on
the distance's own zero set) is clean (0 open edges, 3.6 s). Not fixed: it is the exact distance's, which is made
from a mesh and so breaks the rule that the mesh is only the output. Until then: `thicken`, or `lattice_surface_conform`
on the part itself, not on a `shell_outside` of it.

The field of an imported part is exact in sign (inside or outside) and a lower bound of the distance to the part:
outside the part it is the distance to the infinite surfaces the part's faces lie on, so what reads it as a distance
(the `depth_below` of a lattice skin, a field-driven design, `exclude` regions made from it, a union of parts) sees
that: blocks and fins where a face ends before its surface does, sharp plane-to-plane edges. `thicken`, the shells
and the offsets of a part use its exact distance instead (see above), which costs 2 to 3 times the render of the
part's own field. Making the field itself a true distance (a box around each plane's faces, round corners, the
distance to the faces themselves) was tried several ways and made the render of every part several times slower, so
the field stays the fast one. Offsets and shells of an imported part that was combined with something else are
made from the combination's own field: use `exact_distance()` on it.
