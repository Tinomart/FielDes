# Changelog

## 0.1.0 — first beta

The first release under the name **FielDes**. It is what libfive Studio became on Windows: a design and
analysis workbench driven by fields, now with its own name, design and documentation.

### Application
- New name, icon and look: a light interface around the dark viewport, a dark top dock in the style of the viewport cards, with Open and Import icons at
  the left and a Settings menu (keyboard shortcuts, text size, navigation), a slim editor gutter, a compact model
  tree (300 px), small output text, designed for maximised windows on large monitors.
- The window always starts maximized.
- The meshing options are in the View menu; the Debug menu is gone. Text size is `Ctrl++` / `Ctrl+-`.
- Shorter texts: the result card (a short note, details in a tooltip), the section card, tooltips, the guide and
  the messages in the output pane.
- The shape reference opens at a sensible size.
- Model tree: the region-of-interest button is gone; shorter tooltips; per-part Reimport; three-mode handles
  button (gizmo, handles, lock), also on displayed expressions: a primitive such as `sphere(3)` is given a name
  and its surfaces become draggable.
- Section card: **Whole elements** appears only when the plane cuts a part shown with analysis elements.
- Legends: closing a legend stops probing.
- Scripts run in their own folder, so relative paths work.
- The guide (`Shift+F1`) rewritten for FielDes.
- Editor: **go to definition across files** (library functions, methods, modules next to the script) opens the
  file in a **tab**. The first tab is the script that is rendered (marked with ▶ and bold); the others are for
  editing only, saved with `Ctrl+S`; a module of your own that is saved runs the script again. Completion
  writes the brackets of a function and puts the cursor inside them.
- The key shortcuts `M`, `G`, `H`, `L` switch the selected model's edit mode (next, gizmo, handles, lock);
  the first click on a model's button gives the gizmo, and the gizmo sits at the model's centre also for
  shapes made with `var()` numbers.
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
- `exclude()`: the STEP file's own surface inside a region, meshed from the B-rep and put into the field's
  mesh (its triangles inside the region go, the exact surface's, cut by the region's field, come in; the edge is
  jagged by one cell). **The region is a field object**: any shape. Without a region it is `poor_fit_region(part)`, the places
  where the fit is poor, made from the fit marker. Given the whole import it works on every part the region
  reaches; given one part (or one entry) on that part only.
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
- Static, modal, thermal and thermal-stress analysis on body-fitted tetrahedra (or voxel hexahedra),
  structural and thermal topology optimisation, several load cases.
- Fields and regressions: distances, maps, `fit()`, data fields, `colored()`; all results are fields.
- Lattices: nine TPMS families, a dozen strut lattices, planar patterns, Voronoi foams, surface and graph
  lattices, custom unit cells and equations, conformal cell maps, field-driven sizes.
- **A lattice is made of a cell, and every lattice operation takes one.** `cell_periodic(kind)` (a standard strut
  cell, TPMS or planar pattern), `cell_non_periodic(kind)` (`'voronoi'`, `'delaunay'`) and `cell_custom(...)` (nodes and
  beams, `equation=` for your own TPMS, `shape=` for a shape that tiles) all make the same kind of object, and
  `lattice()`, `lattice_surface_conform()`, `strut_lattice()`, `tpms()`, `planar_lattice()` and
  `lattice_parameter_for_density()` take it as their `cell` — and only that: **the old forms are gone** (a name string
  such as `'gyroid'` in the cell's place, `kind=`, `surface=`, `unit_cell()`, `tpms_equation()`, `periodic()`,
  `voronoi_lattice()`, `surface_lattice()`); a refused call says what to write instead. `cell_non_periodic` also lays a
  random graph on a surface in `lattice_surface_conform()`.
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
- **Two conformal examples**: `14_conformal_lattice.py` (an open surface, with a `cell_custom` cell of your own: a custom cell
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
