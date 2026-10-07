# Changelog

## Next — one import function, a field walk for surface selection, materials and conditions as models

### Speed and feel
- **Dragging never writes a number into the wrong place (the "unclosed bracket" error).** A drag writes its numbers into the `var(...)`
  calls of the script at the lines and columns that the last finished run recorded -- and the script can be another text by then: selecting a
  model makes FielDes write an `expose(...)` block above its handles line, a click on the tree puts a line in or takes one out, a key is
  typed. A number written at an old place went into the middle of some other line (`var(31.73047.0)`, then `SyntaxError`). Reproduced
  in a window on the previous build by selecting a part and dragging its gizmo at once, then fixed: the places are now cursors of the
  document, which every edit moves along (the tree's, the keyboard's, undo, the drag's own); a run's places are taken over only when the
  run was of the text that is in the editor; and what stands at a place is checked to be the number before it is written over (a number
  that is not there is not written, and the script is run again). Also correct now for a line with an accent before a number (Python
  counts bytes, the editor characters). The test `dev/tests/run_drag_after_edit.ps1` does the sequence and reads the script after each step;
  on the keuken file (select a part and drag at once, lock another and drag, unlock and drag) every step parses and the numbers land in
  the part's handles line.
- **The viewport follows the tree's lines.** The tree moves to the new lines the moment a click edits the script, but the shapes in the viewport
  knew the lines they were made on only from a run -- so for the seconds the run took, selecting a model in the tree lit up another shape (or
  none, and no gizmo to drag), and a click on a shape selected another row. The tree now tells the viewport where each line went (the same
  for drops), and the shapes' lines move with it.
- **A render goes on when you change something that has nothing to do with it.** A model's numbers (`var()`) were told apart by the
  order they appear in the script, so one new `var()` line above a shape renamed every number below it: the shapes were "new" and
  every render started again. The numbers are found again by the statement they are in (`name = function` of its statement, and
  which one of that name it is) and their place in it, so a new line, a comment, a changed number elsewhere or another statement
  leave the shapes (and their renders and meshes) as they are. Measured with a window: three unrelated edits above two shapes,
  2 of 2 shapes kept each time (the old numbering restarted both at the first edit).
- **A run that a newer edit has already replaced is not made.** Every edit of the script used to queue a run of the script as it was
  then, and the runs went one after another, each to its end. Only the last of a row of edits runs now; the one that is running
  is stopped as before.
- **Delete is instant too.** The bin of a row (and `D`) takes the statements out of the tree at once, with the lines under them moved
  up, and what an operation was made of stays as rows of its own (5-7 ms to write and follow, in a window: three deletes and a lock in a row
  while the script was running). The bin of a `custom_resolution` row goes the same way.
- **The model tree follows its buttons at once.** The eye, the lock and the render cache write their lines and the tree changes with
  them, in the same moment (a few milliseconds), without waiting for the script to run: every row is on the line the edit gave it, the
  clicked model has its new state, and the rows are made again from that. Clicks in a row while the script is running are all made
  at once, from the tree as it is now, and make ONE run of the last text -- six clicks in a row on a script that takes four seconds
  to run were done in 0.3 s, and the script ran once. The gizmo button shows its new mode at once as well (its lines are the run's to
  say: a second click on it waits for the run, as before). A tree that is followed this way is checked against the text: if a line
  that no edit touched is not the same afterwards, or the lines do not fit, the tree is not followed, nothing is guessed, and the
  tree waits for the run as it did.
- **The model tree is there when a file is opened.** The tree of every run that worked is kept (by the md5 of the script's text, and of
  the program and library that made it; sixty are kept, the oldest go) in `scene-cache` next to the render cache
  (`FIELDES_SCENE_CACHE_DIR` moves it). Opening the same text shows that tree at once, correct, and editable, while the script runs
  for the first time -- measured with a script that sleeps 8 s: 2.5 s after the start, the first launch has two of three rows, the second
  all of them. The run's scene replaces it when it is done.
- A scene from a run that was begun before an edit of the tree is not shown over the tree that edit gave (it would take the tree back
  to the text before it for a moment).
- **An edit of the tree never lands on the wrong line.** In `keuken_no-drag_handles.py` a lock statement had been commented out as if it
  were a hidden model (`# hidden: keukencombinatie_1 = lock(keukencombinatie_1)`, and a ghost row for it in the tree): a hide made from rows
  that were of another text than the script's wrote on the line they pointed at, which was the lock line. Now every edit that comments out,
  rewrites, deletes or inserts under a line it found by the rows first checks what is on that line -- the line that shows the model is
  `x`, the one that hides it `# hidden: x`, a lock is `x = lock(x)`, and so on for handles, expose, the render cache, the resolution and
  the definition -- and when it is not, **nothing is written**: the line under the tree says why (what the line is, and what the rows
  said), the script is run again to bring the rows up to date, and the note says again when that is done. Covered: the eye, the
  multi-select eye, isolate, lock and unlock, the render cache, the gizmo mode, delete (the bin and `D`), removing handles, the reimport
  of a part, reset of an import, the resolution row and its menu entry. (Drops and renames work out their own edit from the whole text and
  were not changed.) Tested in a window by moving the rows' lines onto the wrong statements: the script came out unchanged
  for the eye, isolate, delete and unlock, and the same click on right rows worked.
- **A comment that begins `# hidden:` is a hidden model only when what follows is a name or an expression.** `# hidden: x = lock(x)`
  (a statement that was commented out) is a comment now, and no row of the tree.
- **The model tree is built about a hundred times faster in a big file.** The rows were made inside the view, which then laid out
  every row it had at each icon and tooltip that was set: 405 rows took 6.2 s (14 ms a row, 94 % of it the making of the row) --
  and the window was frozen for that long at every run and, with the buttons following clicks, at every click. The rows are made
  apart and put into the tree once: the same 405 rows take 66 ms (12 ms for the rows). Clicks on a file with 400 models: 33-59 ms
  to write and follow, 50-70 ms to make the rows.
- **A model that is selected is made ready to be dragged when the script has run**, not while clicks of the tree are being followed:
  the lines it writes (`expose`, `handles`) would have made the rows wrong again, and the next click wait for a whole run.
- A test window of the harness no longer crashes when a scene arrives while it is looking for a row (`treebutton`, `treeclick`).

### One body at its own resolution
- **`custom_resolution(body, resolution)`**: ONE body is meshed at a resolution of its own (samples per mm, whatever
  `view.set_resolution` says), so a fine part does not make the whole scene fine -- or a plain big one is drawn coarser. In the
  model tree it is a **property row under the variable** (type the number: `part = custom_resolution(part, 3)` is written as you type;
  the bin takes the line away) and **Custom resolution** is in the right-click menu of a model (on a row of the tree, a line of
  the script, or in the viewport). It changes how the body is drawn, not what it is. Documented in the reference and in
  [Caching and performance](docs/caching-and-performance.md#resolution-and-quality).
- **It works on a body that reaches out of the scene's region** (found in `still_broken_lattice.py`: the lattice's box is an
  estimate and went 17 mm past the pump's region, so the viewport silently drew it at the scene's 0.71 samples per mm and the
  number in the tree changed nothing). The body is meshed at its own resolution over the part of its cube that is in the region.
- **A number that is too fine is capped, not an error**: 2000 samples along the body's longest side is the finest there is. Typing `8`
  for a 262 mm lattice raised an error, which took the picture and the row away, so the number could not be corrected in the tree.
  The body is drawn at the finest there is (7.63 here) and the row says so in amber (`→ 7.63`, why in its tooltip).
- **A plus button at the very left of the top bar**, before Open: a new script (File → New, `Ctrl+N`). The plus is white, and it leaves the room of the arrow the other two have, so the icons are evenly spaced.
- **The numbers in the model tree's fields are centred** (the region, the resolution, the quality and the resolution of a model), not
  left-bound; a number too long for its field shows from its first digit, not its last.
- **A resolution given to a PART of an import has its row too.** The parts of an import are rows under the import, not model rows, and
  only model rows got the `custom_resolution` row: the line was written (by the right-click menu of the part's row, or by hand) and the
  tree showed nothing. The row is under the part now, the same one (number, bin, the amber cap note). A part that was imported as a
  mesh (tessellated: drawn from its own triangles) says `(a mesh)` in its row, with the reason in the tooltip: a resolution changes
  nothing for it. Tested in a window on a kitchen part (meshed at its own 2 samples per mm, as the render log says) and a pump part.
- **The bin of the row (or deleting the line) takes the resolution away at once**: a shape that had one kept it until FielDes was
  restarted.
- **The default number of the menu entry is the scene's resolution** (it was always 10 -- the scene's was looked up under the wrong
  name), so that nothing changes until you type over it.

### Import
- **A part whose free-form faces are fitted badly is tessellated, not reconstructed.** The reconstruction measures how far each fitted
  B-spline face is from the exact face; `import_model` tessellates the part when the worst is `fit_tolerance` (1 % of the face's
  size) or more -- not every part with B-spline faces, only those that do not import properly (in the kitchen file, two parts off by
  2.2 % became meshes; the others, off by 0.4 % and less, keep their faces that can be dragged). `fit_tolerance=None` keeps every
  reconstruction.
- **The model tree shows how each part was imported**: a cube for a reconstructed part (its faces can be dragged), a triangle cut
  into triangles for a tessellated one (a mesh: only its gizmo moves it); the tooltips say why, and the gizmo button of a mesh says it
  has no faces to drag.
- **No legend and no reading under the cursor for the shading of a fit that is off**: the parts light up from grey to red, and that is
  all.
- **One function imports a model: `import_model(path)`.** A STEP file or a mesh (STL, OBJ, PLY, 3MF, glTF), a list of
  `(shape, bounds)` back, one per part (one for a mesh). For each part of a STEP file it chooses: **tessellated** (its exact surface
  as triangles, made a distance field) when more than a tenth of its surface is free-form (B-spline) — `threshold=0.10`, measured
  on the test files: the parts of the examples have none to a few percent, the sculpted ones more than half — or when it is a
  surface body; **reconstructed** (a formula from its faces) otherwise. `reconstruct(path)` and `tessellate(source)` make every
  part one way, by name. **`tessellate` works on anything**: a STEP file, a mesh file, a list of parts, and ANY field
  (`tessellate(shape)`: its surface meshed, the exact distance to that mesh). `import_step_parts`, `import_step`,
  `import_step_parts_reconstructed`, `import_step_tessellated(_parts)` and `import_mesh` are gone everywhere (library, docs,
  examples, tests, the application); the model tree's row of an import has **Import as** (automatic / reconstruct / tessellate),
  **Import model...** is in the menu of empty space, and a part's tooltip says how it was imported and how free-form it is.
- **Surface models import.** The sports shoe was a `SHELL_BASED_SURFACE_MODEL` of 34 open shells — no solid in it, which is
  what "does not contain a solid" meant. Each shell is a part now: a **solid** if it closes, a **sheet** (the distance to its
  triangles less half a thickness, `thickness=`, 0.4 % of the file by default) if it does not. `reconstruct` says that a surface body
  cannot be rebuilt. A face with no surface to tessellate is a failed part that says so (pump.stp's last one).
- **Tessellated parts are drawn from their own triangles**, not meshed from their field: a wall as thin as you like needs no
  resolution (the thin-part resolution problem), nothing is meshed, a part renders at once. The triangles follow `move`, `rotate`,
  `scale`, `reflect` and `handles()` by a matrix; while a `var()` of the part is being dragged its field is meshed instead, and the
  triangles are back when the script has run. `roi_resolution` leaves such parts out (they ask for no resolution and cost none of
  the vertex budget).
- **The ghosts of a tessellated part are gone** (the kernel's mesh-to-distance): slivers and duplicate sheets of the tessellation
  no longer make a solid that is not there; ghosts 6 -> 0, holes 0 on the test parts.
- A failed part says why when it is used in an operator too (`part + 1`), and a part that is a model of the script can be selected, dragged
  (onto an operation, between models), copied and pasted like any model; a part that is not in the script is added by a
  double-click or its menu.

### Surface selection
- **`surface_from_bodies(body, *others, tolerance=None)`**: the surface of the first body where it meets the bodies that follow (inside
  them, or within `tolerance` of them); one body alone is its whole surface. A surface like `select_surface` makes -- a region for
  `fixed()`/`force()`, a surface for `lattice_surface_conform()` -- made from the fields alone. In the viewport: select several models,
  right-click, **Operation → Surfaces → surface_from_bodies** (one model: its whole surface). Tested in `dev/tests/t_surface_from_bodies.py`
  (the wall of a hole a bolt fills, a face under a block, a gap and `tolerance`, several bodies and a list, a sphere cut by a box, the menu, a lattice laid on the
  patch) and in the window (`win_surface_bodies*.py`).
- **A selection is drawn exactly, also in the middle of a big flat face.** The viewport showed the lit-up patch by the vertices of the part's
  mesh, and a flat face is a few huge triangles, so a patch lying in the middle of one -- the face of a box inside a sphere -- came out as skewed
  slivers between far-away corners (or not at all). A triangle that can hold the edge of the patch is now cut into four, again and again,
  down to a tenth of a millimetre or so on a part 65 mm across; one wholly in the patch, or too far from it to hold any, is left. The edge is as sharp as
  the mesh is fine, for `select_surface` too (checked: the top face of a plate with a hole is one clean rectangle with a round hole).
- **A selected surface is a surface, with no thickness.** `select_surface(..., thickness=)` and the *Thickness* entry of the
  right-click menu are gone: a surface has none. The selection is of the *Surface* kind in the model tree (the separate
  *Selection* kind and its icon are gone), like any open surface; as a field it keeps the thinnest layer the samples cover,
  which is what makes it a region at all, not a setting (thicken it with `offset_by(selection, mm)` for a coarse analysis).
- **The smooth selection stops where the surface bends tightly.** Smooth mode used to limit the turn from one step of the walk
  to the next -- 30 degrees over 1.7 mm on a shoe, which nothing but a sharp edge ever reaches, so it took the whole outside. It is now a
  limit on how tightly the surface bends: `angle` degrees for every 10 mm of the walk (measured from the sample about 10 mm back).
  On the pump shoe: 15 degrees or more takes the whole outside, 10 about half of it, 5 a patch round the seed. The menu's default is
  10 degrees for both spreads, and the value it remembered from before (a different quantity) is dropped.
- **`select_surface` on a part that was isolated no longer fails.** Isolating a model writes `part = handles(part, move=(var(0)...))`
  into the script; `handles()` lost the part's box when its numbers were `var()`s, so a selection made on it had to search the
  shape's extent, which does not work on a mesh in the application (it did in a headless run): "could not find the extent of the
  shape". `handles()` now carries the box (scaled, rotated, moved by the numbers the `var()`s have).
- **A lattice on a thin shell no longer reaches into the air.** `lattice_surface_conform` (side `inside`, the default) gives each
  layer the depth of the body under it, found by reading the field inwards along the normal; it stepped an eighth of a cell at a
  time, so a wall thinner than that was jumped over and read as no body at all, and the layer got the whole reach (three cells)
  standing out of the surface. On a shoe with a 1.4 mm wall and 20 mm cells that left lattice pieces 15-26 mm above the shoe. The
  field is now read in finer steps first, so the layer is the wall (1.4 mm), and every piece lies within 1.5 mm of the part.
- **`lattice_surface_conform` makes a thin layer by default, and has `cell_thickness` and `stretch_cell`.** The lattice used to be as thick
  as its cells were wide; for riveting and the like it is now a layer `cell_thickness` thick (3 mm by default), measured from the
  surface. `stretch_cell=True` (the default) deforms the cells to fit that thickness: `cell_size` along the surface, `cell_thickness`
  through it. `stretch_cell=False` keeps the cells' own proportions (as deep as they are wide): `cell_thickness` of them stands out of
  the surface and the rest, on the other side of it (the inside of the body), is cut off at the surface -- for strut cells and for a
  periodic surface, outside or inside, on a body, a surface or a selection. The argument `depth` is gone: it is `cell_thickness`
  (`None` = as it was: a body filled as deep as it is, or one cell). The default strut of a thin layer is a quarter of its thickness
  across at most (a thinner one is finer than the viewport can draw).
- **A lattice on a selection is laid out as a surface, by a method of its own.** A selection is a surface: it used to go through the
  closed-body layout (the whole part had to be mapped as one closed surface, and the call was refused for a part that is not closed,
  `the cell map does not cover the whole surface ...`) and then be cut to the patch, which left the tops of cells in the air. The layout is
  now told, not guessed from the field: a body, a surface (cut by its region) or a selection. A selection is laid out over the
  surface the picked patch makes -- the points of the walk with their normals, as a field, `selection.surface` (the height above the
  patch along its normal) -- over the part of it by the patch only, by the sheet method. Nothing of the body behind it is looked at,
  and nothing is cut afterwards. `lattice_surface_conform(selection, ...)` stands on the side the surface faces, one cell deep, unless you
  give `side=` or `depth=` (the default `side` is `'inside'` for a body, `'outside'` for a surface or a selection).
- **`select_surface` is made from the field alone** — its value and gradient, like the conformal lattice: a walk over the surface
  from the seed in steps (eight from every sample, each carried onto the surface along the gradient), kept where the surface
  stays within the angle of the seed's (flat) or of the sample before (smooth). No mesh is made or read: it is the same on a reconstructed part, a
  tessellated one, a mesh, a CSG model, however thin. The selection is the distance to the walked samples (a kd-tree field) less half its
  thickness; `.samples`, `.spacing`, `.cover`, `.patch`, `.whole` (= |field|). `resolution` is steps per mm (default 150 along the
  longest side). Tested on a box, a plate thinner than a step, a sphere, a cylinder, moved shapes, the bracket reconstructed and
  tessellated, and pump_2 (`dev/tests/t_select_walk.py`). The mesh flood (`libfive_mesh_flood`) is removed.

### Model tree and viewport
- **An error leaves no shadow rows.** The `# hidden: name` lines that come after the line that failed (their models were never
  made) were listed as rows of their own, pointing at nothing, next to the real part rows. A run that stopped, or is still running,
  lists only what it got to.
- **The error banner goes when the failing line is rewritten or passed**, not when the whole run ends: the moment the line it
  was on is not in the script any more, or the run has completed a statement after it. A run that takes long (an import, a
  lattice) used to leave the banner up the whole time. If the new text fails again, the banner comes back after the usual moment.
- **Materials are their own kind** (`material`: tan, a hatched block): `Material(...)`, the presets and fluids. They are models in
  the tree, nested under the analysis that uses them, and **dragged onto an analysis** a material becomes its `material=` (a fluid
  its `fluid=`), replacing the one it has. Conditions are dragged the same way: a set (`static_boundary_conditions`) becomes the
  analysis's conditions, a support goes into `supports=[...]`, a load into `loads=[...]`, a thermal or flow condition into the list the
  analysis takes — never a question, and the reason when it cannot be done.
- **The simulation menus list them** (**Add simulation** in the menu of empty space, **Simulation** in a model's menu -- the first
  menu stays short): New material (steel, stainless steel, aluminium, titanium, PLA, PETG, ABS, nylon — written
  out with their numbers — and your own), New fluid (water, air, oil, glycerol), New support or load (fixed, force, gravity,
  thermal expansion), New thermal condition (fixed temperature, heat input, heat generation, convection), New flow condition (inlet,
  outlet, wall, slip); a condition is written with its region as a box model of its own (change it, or drag a selected surface
  onto the condition to replace it), and **Simulation -> static_boundary_conditions** writes the boundary conditions of a model. **Lattices**
  (`lattice`, `lattice_surface_conform`) and **Importing** (`tessellate`) are in the Operation menu.
- **Ctrl+D duplicates, Ctrl+C / Ctrl+V copy and paste models** (viewport or model tree focused; Ctrl+D is the editor's multi-cursor
  with the editor focused): the model's statements with free names, copies referring to each other's copies, one undo step.
- **The part rows have an eye each**; only the **assembly** (the import row the parts are nested under) has none, since it is the
  file and not something drawn. Imported parts are ordinary models: nothing is locked unless you lock it (the lock button, `R`).
  A locked model that is selected says why it has no gizmo (a line under the tree), and unlocking one that was never made ready
  makes it ready (its gizmo line, its draggable surfaces).
- **A part shown with `I` (or `V`) is made ready to be dragged.** A part that was hidden and was shown by isolating it got its gizmo
  (from the `handles(...)` line it had) but never the numbers that place its surfaces, so its faces could not be pulled; only the
  eye prepared a model. What is selected is made ready after every run now (nothing is written for a model that is ready).
- **An orange dot marks every model whose surfaces cannot be pulled.** Selecting a model writes the numbers that place its surfaces
  (`expose(...)`) when they are no more than 120 -- a bigger one would be pages of script -- and the model tree now shows which
  models have no such handles from the start: a small orange dot right after the name (parts of an import included: a tessellated
  part is a mesh with no faces to pull, a reconstructed one with more than 120 numbers, such as the kitchen file's part 3 with 1620,
  is not made draggable). Its tooltip says which and why; the gizmo moves such a model all the same, and selecting it says it under the tree.
- **The disaster file crashed while the mouse moved over it.** Hovering a model asks which numbers it is made of, and the kernel's
  `Tree::walk()` of a tree that has been moved (remapped: a gizmo, `handles()`) returned pointers into a flattened copy that was
  already gone, so the walk read freed memory: heap corruption or a jump to a freed object, a few seconds after opening, whenever a
  moved model was under the cursor (exit code 0xC0000374 or 0xC0000005; reproduced by moving the mouse over the file while it
  loads). Every walk of such a tree keeps the flattened copy alive now (the hover, the field walker, the save check, the axes, the
  serializer). The hover also checks the shape number it reads from the picking picture, as every other place did.
- **A crash leaves a report**: `%LOCALAPPDATA%\FielDes\FielDes\crash\crash-<date>.txt` with the exception and the places the
  program was at; `dev/tools/crash_symbols.py` names them with the build's map files.
- **A body with no surface is not a failure.** `pump.stp` lists a last body that has no surface to import: its row is dashed and dimmed
  with a tooltip, not red and struck through.
- **A locked model can be in a selection of several again** -- to select a handful of parts and unlock them at once (`R`, or the
  lock button of any of the selected rows, which then acts on all of them). A selection with a locked model in it cannot be
  moved or edited (no shared gizmo, nothing written for it), and a popup says so: when a locked object is part of a multi
  select, the multi selection can no longer be moved or edited. A line under the tree says how many are locked.
- **A click on the tree waits for a script that fits.** Eye, lock, gizmo mode, cache, delete and the import buttons make their edit
  from the lines the last run reported; clicked again before that run was done (a big file runs for seconds) the second edit hit
  the lines of the first one's text, and a definition was commented out or deleted -- the script then ended in an error. The
  scene says which text it is of, and an edit made while the text is another waits for the scene that fits (one at a time; a script
  with an error refuses it and says why).
- **The model tree no longer reloads when you click an eye or a lock.** The script runs again after such a click, and once a
  statement (an import of ninety parts) had run for a moment the tree was replaced by the rows of the statements that were
  done -- one row -- and filled up again when the run ended. A tree that is shown now stays as it is until the run is done (only
  an empty tree, a script just opened, fills row by row as the run goes on), and the button that was clicked changes at once.
- **Dragging numbers no longer eats parentheses.** A drag writes the numbers of a line of `var(...)`s into the script as they
  change; the place of the numbers to the right of one that changed was moved along only when they changed in the same step,
  so one that changed in a later step was written one character off (`var5.200222)` in `examples/dragging_causes_parenthesis_error.py`).
  All of them move along now (`dev/tests/win_varsteps.py`).
- **`I` keeps the tree where it was**; **Fix All** puts
  fixes after `from fieldes import *`; a running render keeps going when something else changes (only a shape whose geometry
  changes starts again).
- **An error shows**: after 1.3 s of a script that does not work, the model tree header says "error" and the viewport gets a red frame
  and a banner with the line (click to go there); it clears when the script runs.

### Fields
- **The field viewer is not kept anywhere.** The disc could not be dragged out of the render region (a box round the part, in a
  big file a small one); nothing limits it now, and the Position slider, which spanned that region, is gone: the arrows move the
  disc.
- **A field has "Body from field" at the top of its menu** (its row in the tree, and the field viewer's disc), next to
  Operation > Field math, which has it too.
- **The field viewer follows the hand.** The disc was sampled again only when the mouse rested: every move started its timer
  anew and a pass on its way was abandoned at every tick, so a drag showed one picture, at its end (measured: 1 per drag of
  40 moves; now 20). The same for the section plane. The numbers of a field that a gizmo drags reach the viewer as they move.
- **The analysis fields read the script's `var()` numbers**: `gradient_magnitude`, `normal_field`, `wall_thickness`, `curvature_field` of a shape moved by `handles()` (or any shape with a `var()`) used 0 for every variable (the kernel printed "uninitialized variable" hundreds of times); they are given the numbers the evaluator holds now, and follow a drag.
- **`body_from_field(field, level=0)`**: a body where the field is below `level` (the inverse of `field_from_body`).
- **`low_level_field(logic)` and `low_level_body(logic)`**: your function of the coordinates, called once with x, y and z as fields
  (or x and y; or x), is the field or the body — `maximum(...)` and `minimum(...)` stand for Python's max and min. Right-click ->
  New field -> low_level_field (or New 3D shape -> low_level_body) writes the function, with a first working logic at the cursor,
  and the call. Example 21 uses them.

## Next — fields everywhere, custom blocks, a typed and nested model tree

### Fields everywhere
- **Wherever a number goes, a field goes.** A size, a radius, a thickness, a spacing, a blend, an offset, a scale ... of every
  function of the library can be a field instead of a number (`offset(part, ramp(z_field(), (0, 40), (0.2, 2.0)))`), and is
  evaluated where it is needed, exactly: nothing is sampled onto a grid. `dev/tests/t_field_slots.py` runs a constant field
  through 98 numeric slots and checks it gives what the number gives. Added or fixed: `ramp` / `normalize` ranges, `attractor`
  (the radius; a point or points as the centre), `smooth_union` / `smooth_intersection` / `smooth_difference`, `union_all`,
  `repeat`, `noise_field`, `distance_to_point` / `_plane`, `radial_field`, `angle_field`, `polar_field` (centres; a point works),
  `mass_properties` (a density field), `offset_exact`, `shell_exact`, `round_edges`, `fillet`, `smooth` (the radius),
  `ellipsoid`, `elliptic_cylinder`, and in lattices `skin`, `blend`, `skin_blend` and the struts' blend. What cannot be a field
  (counts, tolerances, resolutions, a Poisson's ratio) says so when given one.
- **Graded lattices.** `lattice(body, cell, cell_size=<field>)`: big cells here, small cells there, by blending lattices a factor
  of two apart (up to a factor of 16), for every periodic cell including `cell_custom`.
- **Fields in the analyses** (tetrahedral elements): `Material(E=field, density=field, conductivity=field, expansion=field)`, a
  load's `profile=field` (the total stays; it is spread over the surface in proportion to the profile), `fixed_temperature`
  with a field, `convection` with a field for the coefficient and the ambient temperature, `heat_input` / `heat_generation`
  with a profile. Checked against beam theory and Fourier's law (`dev/tests/t_solver_fields.py`); topology optimization and
  the modal analysis take them too. The flow solver takes numbers and says so. The kernel evaluates each field at the centre
  of every element (`TetProblem::setStiffnessField`, `TetThermalProblem::setConductivityField`, ...); the result cache keys
  include them.
- **Points and surfaces are models.** `point(x, y, z)` is drawn as a small ball, has a gizmo, and is accepted wherever a
  coordinate goes (`distance_to_point(p)`); `plane`, `sphere_surface`, `cylinder_surface`, `wave_surface` are surfaces drawn as
  thin sheets. A 2D shape is drawn flat. A **field is not drawn**: selecting it in the model tree opens the section viewer on it,
  which paints a plane through the render region with the field's value at every point (the colour scale found once on a coarse 3D
  grid; hover reads the value; move the plane to see it in 3D). A field model has no eye in the tree.
- **Arithmetic on fields.** Field * field, field / field, field ** field and `2 ** field` work like number * number, pointwise
  (`__rpow__` was missing). The same as functions that always make a field, also from bodies, for the tree and its menus:
  `add_fields`, `subtract_fields`, `multiply_fields`, `divide_fields`, `power_field`, `min_fields`, `max_fields`, `abs_field`,
  `negate_field`, `sqrt_field`, `square_field` (right-click: Operation, Field math).
- **`field_from_body(body)`**: the values of a body as a field -- not a body, not drawn, free to be multiplied and still valid.
  (A body itself keeps its true scale.)
- **A point goes wherever a position goes**: `Point` reads as its three coordinates (`move(part, p)`, `sphere(5, p)`,
  `distance_to_line(p, d)`). In the model tree a point can be dropped on an operation: it takes the place of a position written
  as a tuple (`distance_to_point((5, 5, 5))` -> `distance_to_point(anchor)`; a flat shape's centre becomes `(anchor.x, anchor.y)`),
  never of a size, a direction or a scale; and a dropped field never takes the place of a count (`array_x(body, 3, 12)` takes the
  12's place, not the 3's), with the reason given when there is nothing it can take.
- **Ctrl+drag makes a shadow, and the model stays.** A model that **nothing else uses yet** stays a top-level
  row when it is Ctrl-dragged into a call, and the call shows a shadow of
  it: the drop writes a `# shadow: name` comment on the call's statement, which says the call holds a reference and is not the
  model's first user. A plain drag onto that call removes the comment and moves the row in; deleting the reference removes it too.
  A shadow never stands by itself at the top level: dragging such a statement **above** the model's row (which used to do nothing),
  or the model's row **below** the statement (which used to be refused), asks once -- *the reference inside the statement becomes
  the real model*: its definition goes directly above the statement, the comment goes, and the separate top-level row is gone.
- **Recent files**: a small arrow beside the Open and the Import icons lists the scripts opened and the models imported lately.
- **Open example file** is its own entry of the File menu (the file dialog opens in the examples folder next to the program), and the
  guided tour ends by pointing at it.
- **The field viewer is a card of its own**, no longer the section card in a different mode: a coloured **disc** (radius slider)
  that cuts nothing, with the section card's arrow along the way it faces, two arrows in its plane and the dot in the middle for
  moving it freely. It starts where the field is about (the point or body it was made from, found by Python: `_field_origin`,
  inherited by fields made of fields), has no close button (it closes when no field is selected) and, with several fields
  selected, a menu chooses the one shown. The section card steps aside while it is open and comes back as it was.
- **Cards are draggable and resizable**: the model tree, the section card, the field viewer and the result card can be dragged
  anywhere in the viewport (by the header or any empty place) and resized from their edges and corners; they keep their place
  relative to the window and are remembered between sessions (`CardController`).
- **The guided tour has a Ctrl+drag step**: "A reference with Ctrl" adds a sphere and a union and shows a Ctrl+drag making a shadow
  reference (the pretend hand shows a Ctrl key cap).

### Custom blocks
- **Your own functions, in every script.** Every public function of a `.py` file in the blocks folder is a block: no import, no
  registration. They are written with the whole library at hand, may use the blocks of the files before them, and appear with
  call tips, completion, go to definition and in the right-click menus (*Add operation → Custom blocks*, *New custom block*).
  **Settings → Blocks folder…** chooses the folder; it is watched, and a script that uses a block runs again when its file is
  saved. A block file's errors are named in the output. See [Custom blocks](docs/blocks.md); `blocks/sample_blocks.py` has four.

### The model tree
- **The gizmo shows at once.** Selecting a model used to show its gizmo only after the script had run with the new `handles(...)` line
  (a moment, longer for a heavy script). It is drawn immediately now, from where the model is (the same point the real one has), a
  little transparent; the real one takes its place when it is ready. A press on the early gizmo is kept: the drag starts as soon as
  the real one exists, if the button is still down.
- **Types with icons.** Every kind of thing — 3D shape, 2D shape, field, surface, point, simulation, conditions, lattice cell,
  selection, import — has an icon of its own colour and shape in the tree and in the menus that make it; a model made by a custom
  block carries an *f* badge.
- **Nesting.** The models an operation is made of are its children (a model goes under the first statement that uses it; the
  others show it as a dim *uses x* row).
- **Drag and drop.** Drop a row on an operation to make it one of its inputs (replace one, or add one to `union` and the like);
  drop it between rows to move it in the script, with what it is made of; each is one undoable edit, refused with a reason when
  something would be used before it is defined.
- **A field dropped on an operation takes the place of a number**: drag `swell` onto `thick = offset(plate, 1.0)` and the call
  becomes `offset(plate, swell)` (the first plain number of the call). A model dropped on an operation is added to it, or takes
  the place of the only model it works on; no menu is asked.
- **The right-click menu in empty space** makes things by kind: *New 3D shape / 2D shape / point / surface / field / custom
  block*, then *Add operation*.
- Selecting a model no longer writes gizmo lines for models that are not shown, and a model that is made of the one that gets
  numbers loses its now stale `expose()` line instead of failing.

### The guided tour
- **A tour on the program itself.** The first time FielDes starts it offers a guided tour (**Help → Guided tour** after
  that): short cards over the live window. Each step dims everything but what it is about, points at it with an arrow and a
  glowing frame, and lets the user do only that one thing (the rest of the window and the keyboard are held back so that
  nothing confuses); **Show me** does it with a pretend cursor, through the same mouse events a hand makes (typing a number
  into the script, dragging a surface, clicking a row, the right-click menu, dragging a field onto an operation, renaming,
  editing a render setting). Every step can be skipped, and so can the whole tour. The script is shown as half of what the
  step is about: the lines a step concerns stay lit and framed in the editor, and every line that changes or moves -- typed,
  dragged in the viewport, dropped or renamed in the tree -- glows for a moment (a line that only moved down because another was
  put above it does not). It loads a model of its own (a plate with a
  hole), so it never touches the user's script.

### The model tree
- **Rows that are already nested can be dragged and dropped** like the others, and the tree answers **at once**: it is
  updated from the edit before the script has run again (the run confirms it). Dragging no longer writes gizmo lines into
  the script while the mouse is down, which is what made a drop wait for a run.
- **Rename by double-clicking** a variable's name (`F2` too): every use changes, scope-aware (strings, keyword arguments,
  attributes and a function's own variables of the same name are left alone).
- **Render settings are editable** in the tree: the region, the resolution and the quality are number fields.
- **The tree is the structure of the calls.** Nesting, renesting and denesting always edit the arguments of a call: a model
  dropped on an operation becomes an argument, dragged out of one it leaves the arguments, dropped between the children of
  an operation it stands at that place among them (and the children are listed in the order of the arguments). A model used
  by several statements has its row under the first and a **shadow** row (half transparent) under each of the others; a
  shadow can be dragged (only its reference moves), copied, and deleted (button, `D`). **Ctrl+drag moves nothing**: the model
  keeps its row, and a shadow appears in the call it is dropped in. **A shadow cannot go above its original** (the first
  statement that uses the model): a message explains why and offers the easy fix, which is its default -- the first use becomes the
  original place and a reference stays at the old one -- with *Do not show again* (always do that). It is asked once, from the tree
  the edit would give, so it also catches a statement with shadows dragged above the original, or the original dragged below it,
  and lists all the shadows in one message. Ctrl+pressing a row that is
  already selected no longer takes it out of the selection before the drag can begin. **No menu is ever asked on a drop**: a model dropped on an operation is added to it (`union`, ...), or
  takes the place of the only model the operation works on; a field takes the first plain number of the call; anything that
  cannot be done is refused, and a red note at the mouse says **why**. Taking a model out of an operation that cannot do without it deletes the
  operation (and, in turn, what is made of it), after a question whose default is *Delete* (it can be hidden); one undo.
- **`#SECTION` comments fold** the lines up to the next one (an arrow in the gutter, a box that says how many lines are
  hidden), are set apart in blue, and complete from `#sec`; an error inside a folded section opens it.

### Linux
- `scripts/build-linux.sh [--package]` builds FielDes on Linux (Fedora and Ubuntu package lists in the script; made and
  tested under WSL2 + WSLg) and packs a portable folder (`dist/FielDes-linux-x64.tar.gz`, started with `run.sh`).

### Viewers, typing, the tour (round 6)
- **The section view and the field viewer are separate.** The field viewer used to take the section's plane for itself and hide the
  section card while a field was selected. Now each has its own plane in the viewport, its own samples and its own card; the section
  view is switched on by the user (`Ctrl+Shift+X`) and works the same whether or not a field is selected, and the two cards stack
  (the field viewer's below the section card). The disc keeps its three arrows and its dot.
- **Renaming and the render settings write the script as you type.** A name typed in the tree renames the variable in the script
  with every key (a name that cannot be yet waits; `Enter` keeps it, `Esc` gives the old one back, a name that ends up not being one
  gives the old one back too); the fields of the render settings write `view.set_*` with every key, and one number typed is one
  step of undo.
- **The guided tour's "Show me" shows it once and puts everything back.** The script, the selection and the section view are
  restored to how they were when the button was pressed, and the step is not counted as done, so the user does it himself.
  **Every step has a state of its own**: the first time it is what the steps before left, and whenever it is opened again (Back
  included) it is put back as it was, ready for what it asks. A new step, **Cut a section**, shows the section view, and the
  last step draws the File menu with **Open example file** framed, as the blocks step draws its menu.
- **The region has no bin** in the render settings (the resolution and the quality keep theirs: deleting them goes back to the default).
- **Low-level fields in Python**: `examples/21_low_level.py` does what libfive's Scheme `define-shape` / `remap-shape` do -- a cube from
  six plane distances, a twist by `.remap`, a ball and a torus from their formulas joined by a smooth minimum written in the script, a
  gyroid -- with `Shape.X()/Y()/Z()`, the arithmetic and the methods of `Shape`; [Scripting](docs/scripting.md#low-level-your-own-shapes-from-x-y-and-z)
  explains it.

### Documentation
- A [tutorial](docs/tutorial.md) (the written version of the tour), a revised README, new screenshots, and the
  documents of the new features; examples 18 (fields everywhere), 19 (a graded material and a load profile) and 20 (custom
  blocks), none of which needs a STEP file.
- **One sample part comes with FielDes**: `examples/step/PivotBearingSupportBracket.STEP` is in the repository and in the
  portable packages, so examples 01, 05, 08, 10 and 15 run as soon as FielDes is installed. It is a GrabCAD download and is
  distributed at the maintainer's decision, its author's terms being unknown ([NOTICE.md](NOTICE.md) says so, and that it is
  taken out on request). The other sample parts (examples 02, 03, 04, 06, 07, 09, 11, 12, 13) are still not distributed.

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
