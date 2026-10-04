# Architecture

FielDes has three layers, in three folders. Each can be used without the ones above it.

```
   app/        Qt application  (FielDes.exe)        GPL-2.0-or-later
     │  embeds Python, draws the viewport, edits the script
   python/     Python library  (import fieldes)     MPL-2.0
     │  Shape, the standard library, STEP/mesh import, fields, analysis front ends
   kernel/     C++ kernel      (fieldes.dll)        MPL-2.0
        expression trees, evaluators, meshing, STEP reconstruction, FEM solvers
```

- [The kernel](#the-kernel)
- [The Python library](#the-python-library)
- [The application](#the-application)
- [How a script becomes a picture](#how-a-script-becomes-a-picture)
- [How the STEP importer works](#how-the-step-importer-works)
- [The exact tessellator](#the-exact-tessellator)
- [Files FielDes writes](#files-fieldes-writes)
- [Environment variables](#environment-variables)
- [Origins and licences](#origins-and-licences)

## The kernel

`kernel/` is C++17, built as the shared library `fieldes.dll` (and a small `fieldes-stdlib.dll` of
C++-defined shapes). It descends from **libfive**, Matt Keeter's kernel for implicit modelling.

| Folder | Holds |
|---|---|
| `src/tree`, `include/libfive/tree` | The expression **tree**: immutable, hash-consed expressions of `x, y, z` and constants/variables; serialisation; the structural **content key** used by every cache; `expose` (turns the numbers that place surfaces into variables) |
| `src/eval` | Evaluators for trees: point, array (SIMD), interval, derivative, and feature-aware evaluation |
| `src/render/brep` | The meshers: dual contouring, simplex and hybrid, with an octree/xtree that splits regions until a cell is 1/resolution. Where an oracle is on the tape (a mesh's distance costs about a microsecond a point) the dual contouring finds the surface along an edge by bisection (10 points) instead of 4 stages of 16 |
| `src/render/discrete` | Heightmaps and voxels |
| `src/oracle` | *Oracles*: nodes of a tree that are evaluated by code instead of by arithmetic (fitted surfaces, meshes, FEA results, data fields), with per-point memoisation |
| `src/solve` | The small solver that turns a drag in the viewport into changes of `var()` numbers |
| `src/step` | STEP: parser, assembly structure, geometry, per-face reconstruction, B-spline fitting, **reconstruction to CSG**, units, and the exact **tessellator** (see below) |
| `src/mesh_import` | STL/OBJ/PLY/3MF/glTF → cleaned triangles → exact signed-distance oracle (spatial search structure, generalised winding numbers) |
| `src/fields` | Field oracles: distance to curves, wall thickness, curvature, scattered data, noise |
| `src/lattice` | Graph lattices (beams along a graph) |
| `src/fea` | The finite-element solvers: voxel hexahedra, body-fitted **tetrahedral meshing** (isosurface stuffing), static, modal and thermal analysis, topology optimisation, multigrid and PCG |
| `stdlib` | Shapes defined in C++ |

The C API is `kernel/include/libfive.h`; the Python library calls it through `ctypes`.

## The Python library

`python/fieldes/` — `from fieldes import *`.

| File | Holds |
|---|---|
| `ffi.py` | Loads `fieldes.dll` (found through `FIELDES_DIR`, next to the package, or on the path) and declares the C API |
| `shape.py` | `Shape`: the wrapper of a tree, operators, evaluation, `save_stl`, `get_mesh` |
| `view.py` | `view.set_bounds / set_resolution / set_quality` |
| `stdlib/shapes.py`, `csg.py`, `transforms.py`, `text.py`, `surfaces.py` | Primitives, booleans, transforms, lettering, closed-form surfaces |
| `stdlib/cad_import.py` | STEP import, `roi`, `roi_resolution`, `exclude`, `auto_exclude`, the import cache |
| `stdlib/mesh_import.py` | Mesh import |
| `stdlib/fields.py`, `regression.py` | Fields, regressions |
| `stdlib/lattices.py` | Lattices |
| `stdlib/fea.py`, `thermal.py` | Analysis front ends |
| `stdlib/handles.py` | `handles()`, `expose()` |
| `stdlib/content_cache.py` | Content-keyed caches |
| `app_support.py`, `run_progress.py`, `runner.py` | What the application needs from Python: running a script statement by statement, evaluating displays, reporting progress |

## The application

`app/` — Qt 5 (Fusion style with a custom stylesheet, `theme.cpp`), OpenGL for the viewport.

| File | Holds |
|---|---|
| `main.cpp`, `app.cpp`, `args.cpp` | Start-up, command line |
| `window.cpp` | The main window: the top dock and its menus, import code generation, guide and dialogs, autosave, file watching |
| `editor.cpp`, `script.cpp`, `syntax.cpp`, `findbar.cpp`, `documentation.cpp` | The code editor and its helpers |
| `python/interpreter.cpp` | The embedded Python interpreter (host module `_fieldes_host`: `var()`, `view.set_*`, display callbacks) |
| `view.cpp`, `shape.cpp`, `camera.cpp`, `shader.cpp`, `gl/` | The viewport: meshes, picking, dragging, drawing |
| `scenetree.cpp` | The model tree |
| `section.cpp` | The section card: the plane evaluated exactly on all cores |
| `result_panel.cpp` | The result card |
| `theme.cpp`, `icons.cpp` | Colours and the drawn icons |
| `automation.cpp` | A scripted-GUI facility for testing (see below) |

The application finds its Python runtime in `runtime/python3` (the portable folder) or in a vcpkg
checkout, and the `fieldes` package by walking up from its own folder for `python/fieldes`.

## How a script becomes a picture

1. The editor's text is run **statement by statement** in the embedded interpreter. The interpreter records
   every top-level expression that evaluates to a shape (or a list of shapes), every assignment (for the model
   tree), the `view.set_*` calls, and the `var()` numbers with their positions in the text.
2. For each displayed shape the application builds a render job: the region, the resolution, the quality,
   the colouring field, the analysis display.
3. The viewport compares the new shapes with the ones it already has by **structural identity** of their
   expressions. Unchanged shapes keep their mesh; new ones are meshed on worker threads (progress is shown),
   each import part in its own cube at its own resolution.
4. Shapes that have exact STEP regions (`exclude()`) are additionally tessellated from the B-rep: the shape's
   mesh loses its triangles inside each region (a field) and the exact surface, cut by evaluating that field on
   its triangles, is put in.
5. Hover, drag and probing evaluate the **field itself** (not a mesh) at the point under the cursor; a drag
   is turned into changes of `var()` numbers by the solver and written back into the text.

## How the STEP importer works

1. **Parse** the file (`step_parser`), resolve the product/assembly structure and units (`step_assembly`),
   and collect every solid with its placement.
2. **Per face**: turn each face into a surface description and a boundary (`step_geometry`, `step_face`):
   planes, cylinders, cones, spheres, tori, and B-splines.
3. **Fit** each B-spline face with a closed-form surface (`step_fit`), measuring its deviation from the true
   face.
4. **Reconstruct** each solid as CSG (`step_reconstruct`): the faces' surfaces are combined by half-space
   logic — intersection over convex regions, union and difference over concave ones — following the edges,
   so that the result has the faces' exact boundaries. Symmetric or repeated solids are detected and shared.
   The cells the surfaces cut space into that lie inside the part are united (`min`) as cubes of the cell
   code; where two cubes meet inside the material their wall would be 0 in both, an internal wall that an
   offset, a shell or a lattice skin would keep, so the **consensus** of those two cubes (their surfaces but the one
   between them) is added: it lies inside their union whatever the surfaces do, so a dragged surface (`expose`,
   handles) keeps the part one continuous solid. It also leaves the field **outside** the part untouched (where the
   field is positive the consensus is never below the lower of the two cubes), which a section's field view, an
   outward shell or an offset read. A cube grown further than the consensus has fewer surfaces and lower values,
   and would lower the outside. Only the bridges a wall needs are added.
   Outside the part the value of a cube is a lower bound of the distance to the part: the distance to the
   INFINITE surfaces its faces lie on (a plane's face may end well before the plane does), and the largest of two
   distances is a mitre at a convex edge. An offset, a shell or a thickening of the part can therefore grow fins
   where a face ends early, and its plane-to-plane edges are sharp. Every term tried to tighten that (a box around
   each plane's faces, the exact distance to a cube's box, the distance to the faces, a box around each cube's
   material, a hole's wall as a piece of its own) made the render several times slower or did not remove the
   fins, so the field is the plain union of cubes. Offsets, shells and thickening do not read it: see below.
5. **Emit** a tree (an expression of `x, y, z`) per solid. The import cache stores these trees.

### Offsets, shells and thickening of an imported part

The part's field (above) is not a distance, so `thicken`, `shell_inside`, `shell_outside`, `offset_by` and
`offset` do not read it: `fields._dist()` replaces an imported part by its **exact distance**, the part meshed
(`exact_distance()`: `libfive_tree_render_mesh`, then the mesh as a signed-distance oracle; cached by content) and
the distance to that mesh. What it knows about the part is the attribute `_distance_of`, a function that makes the
distance on the first call:

- `import_step_parts` gives it to every part (`cad_import._distance_getter`; fresh or from the cache).
- The transforms (`move`, `rotate_*`, `scale_*`, `reflect_*`: `transforms._exact_follows`) give a copy the same
  transform of that distance with the same numbers, so `handles()` (a gizmo and its `var()`s) and placement in a
  script keep it. (A transformed oracle holds its coordinate expressions itself: the evaluators hand it the
  `var()` values with `Oracle::setVar`, and `Shape::buildDeps` in the application looks inside oracles for the
  variables a shape depends on.) Other operations (union, difference, lattices, ...) give a shape without it: its
  own field is used.
- `expose()` (`handles._exposed_distance`): the exact distance of the part with the numbers the script has *plus*
  `exposed shape - the part with those numbers as constants`. With the part's own numbers the second term is
  zero, so it is the exact distance. When a face is dragged the variable changes only the second term (the change of the
  part's own field, which is right at the face that moved and zero where it did not), so the thickened part follows
  the drag; the script is not run again for a drag, so it is exact again at the next run (with other numbers in the
  script: the part with them as plain numbers is meshed). This is what keeps `thicken(part, ...)` draggable: its
  tree still holds the part's `var()`s.

Measured on the bracket of the examples (`thicken(part, 5)`, resolution 1): the part's own field gave 11.8 %
of the vertices more than half a millimetre from where the skin belongs (worst 10 mm), the exact distance 0.00 %;
0.4 s against 0.9 to 1.1 s to mesh. `FIELDES_NO_EXACT_OFFSETS=1` turns it off.

## Selecting a surface and lattices that follow it

`select_surface()` (`stdlib/selection.py`) meshes the shape (`render_mesh`, the viewport's own meshing with the
script's `var()`s) and hands the triangles to the kernel (`libfive_mesh_flood`, `mesh_import.cpp`): a breadth-first
fill over the triangles that share an edge, with the angle test of mode `flat` (against the seed triangle's normal)
or `smooth` (against the neighbour it came from) and a radius. `libfive_mesh_patch` renumbers the vertices of the
chosen triangles and returns a `MeshOracle` in **unsigned mode** — the unsigned distance to the patch, with a lower
bound of 0 for the interval evaluator — which is the field of the selection (less half its thickness). The same
call over all triangles gives `.whole`; `patch - whole` is 0 exactly where the nearest point of the surface is in
the patch, which is how a ribbing gets sides that run straight along the normal.

`lattice_surface_conform()` (`stdlib/conformal.py`, `kernel/src/lattice/surface_cells.cpp` with `surface_quads.inl` and `quad_layout.inl`,
`libfive_surface_cells`) lays a strut lattice on the surface of a body **from its field alone** — a rule of the project: the mesh is the
final output only, and nothing is computed on a mesh. `Field` wraps a `DerivArrayEvaluator` (value and gradient at many points at once,
the script's `var()`s bound). Everything is a Newton step on that: a point is moved onto the surface along the gradient (`Surf::project`,
batched; the gradient is taken by central differences of the value, so that a point exactly on an edge of a box gives the same answer in
every run).

The cells are first laid out as ONE MESH OF QUADS, one quad to a cell, and everything else (thickness, beams, the periodic surface) is
carried on those cells. There are two layouts, and they have little in common but the field. `surfaceContinuesBeyond` chooses: the cubes
the surface may pass through are found in a box 1.5 cells larger than the region asked for, their centres are put onto the surface, and
if one of the points that get there lies more than half a cell beyond the region, the surface goes on past it and is a **sheet**;
otherwise it ends inside the region and is a **body**. (The centres have to be put onto the surface first: a field that is not a
distance lets cubes well away from the surface through the cube test, and only the projection says where the surface is.)

**The body layout** (`surface_quads.inl`, `body::growQuads`): a CLOSED mesh that covers ALL of the surface, made from a graph of points of it.

- *Points* (`nearCubes`, `fineSamples`): a coarse grid is refined towards the surface (a cube is kept when `|f| ≤ 1.08·size·|∇f|` at its
  centre), the centres of the finest cubes are projected onto the surface and thinned to one point per 0.45 of a spacing
  (`delta = cell·2/7.5`). The grid's offset has four fixed placements (`shift`), chosen by the user with `grid_offset`; the layout is made
  once, from that one grid.
- *Cost* (`Field`, `Surf`): the layout is nearly all field evaluations (the gradient is taken by central differences, 7 evaluations a
  point; the evaluator's own derivative is not used because it takes one branch where a min or a max has a tie). They are made by
  several threads (`Field::evalRaw`): each has an evaluator of its own (a `Deck` holds its oracles), a batch is cut into runs of 256
  points as one evaluator would take it, and the threads take the runs one after another, so that every point gets exactly the value and
  gradient it would get from one thread: the layout is the same whatever the number of threads (`FIELDES_SC_THREADS=n`). A Newton step
  shorter than 0.05 of the step the gradient is taken with is not made (the point is settled, and not evaluated again; the points and
  normals come out the same to a micron). Measured on an imported bracket of 150 x 120 x 88 mm at 5 mm cells (2650 cells, 12 threads):
  154 s with two layouts on one thread, 60 s with one layout, 28 s threaded (about 50 s in the application window with the statistics on). `FIELDES_SC_STATS=1` prints the time and the number of
  field evaluations at every stage.
- *Sharp edges and corners* (`snapToFeatures`): next to a sharp edge a sample lands on one face or the other, up to a step from the
  edge, so nothing of the graph is ON the edge and a lattice line laid along it finds only nodes beside it (the outline of a part came
  out ragged by a fraction of a cell, and the rows of cells did not follow the edge). The samples within 1.5 steps of each other are
  read as a set of planes (a point and a normal each); the directions in which the normals span nothing are left out (a flat face: no
  move), and the rest is solved in the least-squares sense, the way dual contouring finds its vertices: a sample by an edge goes to the
  nearest point of the edge line, one by a corner (three directions) to the corner. A move is kept only if the field says the point is
  ON the surface (a fillet, whose planes meet in the air, is not an edge) and at most 0.8 steps long; samples that land within 0.45
  steps of each other are one node (corners first). The edge nodes carry the middle of their faces' normals and the direction of the
  edge; the carriers by an edge take it as their fixed direction and the edge as a lattice line, as before, but now the line is exact.
  A sample is NOT moved when a normal pointing against its faces lies in the neighbourhood (a wall, a rib or a gap thinner than the look
  radius: the planes of the two sheets cannot be told apart from an edge's, and the fit lands in the middle of the top of the rib;
  without this rule two mechanical test parts could not be given a closed map at all). The nodes that `addBridges` put on an edge
  are marked as edge nodes too. (`FIELDES_SC_NOSNAP=1` turns snapping off.)
- *The graph* (`SGraph`): two points are neighbours if the SURFACE joins them: a hop of at most 1.6 `delta` between points that face
  within 107° of each other, whose middle lies on the surface or whose chord, put onto the surface in three points, is a connected curve
  of about its length (`connected`); a wall or a gap between two points is never crossed. `addBridges` adds the points the samples miss:
  ON a sharp edge (where the tangent planes of two nodes meet, with the bisector as the normal) and in a void of one sheet (the
  projected middle of two nodes); every hop stays within the reach, so a node has neighbours all round it wherever the surface is.
  `unfold` walks a way over the graph with a frame carried by the least rotation at each hop, which puts a neighbour's tangent plane onto
  ours round any edge.
- *The field*: carriers (a Poisson choice, then Lloyd steps), a direction field with four-fold symmetry over them (`orientationField`:
  sharp edges are lines it must follow; where the surface bends one way more than the other -- a cylinder, a torus, a fillet -- the
  directions are pulled towards the principal directions, read from the Hessian of the field divided by its gradient,
  `curvatureDirections`, so that the cells run round a tube and along it, `FIELDES_SC_NOPRIOR=1`), and a lattice position field
  (`positionField`). The position field is grown from a seed: the carriers by an edge first (their lattice line is known), then
  outwards, each taking its lattice from the neighbours already placed, the nearest first (a neighbour counts the more the nearer it
  is, `1/(0.25+(t/h)²)`); the sweeps that follow move the seams where the ways round a loop do not fit to where they cost least. A
  carrier within a carrier spacing of a CORNER of the part has its lattice point AT the corner (a lattice cannot go round a cone
  point unless the point is one of its own: with the corner a lattice point, the lattices on its faces agree about where the lines
  across each edge go).
- *The lattice* (`latticeVertices`, `refineVertices`, `partition`): vertices are carriers that share a lattice point. A corner of the
  part is always a vertex (put first; the vertices within half a spacing of it give way), and a cluster whose seed is by a sharp edge
  takes the nearest node ON the edge as its vertex (the lattice line is straight, a rim that bends away from it is not). Every point of
  the graph belongs to the vertex nearest to it over the surface (a multi-source Dijkstra), and vertices are added until every such region
  is a disc, every two regions touch along one arc, and every piece of the surface has four.
- *Faces* (`tripleFaces`, `fillHoles`, `mergeTriangles`): a face for every three regions that meet at a node (a polygon triangulated for
  four to eight), taken in the order of how many nodes see them, under exact checks (an edge on at most two faces, the faces round a
  vertex one fan, all going round the same way — a union–find of the orientation parities). Candidates turned away at a hole are tried
  again with a higher priority (repair rounds), a hole that is a plain cycle of up to six corners is filled with one face, and two
  triangles whose shared edge touches less than the four sides are one quad -- or, where the four corners are nearly a square (within
  45 degrees and an aspect of 2.2; 60 degrees and 3 when one end of the shared edge already has six faces round it), whatever the touch:
  a triangle is a three-valent vertex at the middle of its cells, and a lattice has none where it is regular (`FIELDES_SC_MERGE=0|1|2|3`;
  measured on ten bodies: a quarter fewer triangles, three per cent fewer cells, no more cells that are forbidden). Loops of open edges that remain are
  closed with one face each (`closeBoundaries`, up to 16 corners; the closing is audited and undone if it does not leave fewer open
  edges). A vertex whose faces are two or more fans that meet only at it is a neck (the surface is narrower than the vertex spacing
  there), and it is bridged with a tube, one polygon face between the two fans (`bridgeNecks`; its Euler number is one lower, as a
  handle's is). (Splitting the vertex, one copy to each fan, was tried: it cuts the neck and caps both sides, which closes a handle
  that is really there.) A face of six corners or more (the
  faces of the repairs, mostly) puts six cells or more round its middle: it is cut by chords between its own corners into faces of
  four and five corners (`splitBigFaces`: the chord that is shortest against the three sides it cuts off, and only one that lies on
  the surface and is not an edge already).
- *Quads*: a face of k corners is k quads (a corner, the middle of the side after it, the middle of the face, the middle of the side
  before it); the middle of a side is half way along the way over the surface, the middle of a face the node nearest the mean of its
  corners unfolded about a node (`surfaceCentre`).  That node is CHECKED: every corner of every quad must turn the right way and be near
  a right angle, and no spoke from the middle of a side to the middle of the face may run through the air.  If it fails, the 40 nodes
  nearest to it are scored by the shapes of the quads they would give (no reading of the field), and the best four by that and by the
  spokes; the best of them is the middle if it is better than the nearest node (`FIELDES_SC_NOCENTRE=1` turns this off).  A face
  whose nearest node passes is not touched.  The middle of a face is never a node ON a sharp edge (an edge is where cells end; the band
  beside an edge holds only edge nodes, and a face whose middle lay on one was squeezed flat against it).
- *Evening out* (`relaxCells`): every node is tried with several moves and takes the one that makes the cells round it best (the energy
  `cellEnergy`: corners further than 15° from a right angle — measured as a signed angle, so a dented corner over 180° costs the most —
  the longest side against the shortest, and sides far from the cell size), if it makes them better at all: towards the middle of its
  neighbours, to where each cell round it becomes a parallelogram, away from a neighbour that is too near or towards one that is too far.
  Each is put back on the surface (within a hundredth of its own edge length). A node ON a sharp edge or at a corner stays where the
  layout put it: it is read from the field -- the gradient at eight points round the node, 0.05 of a cell away in its tangent plane,
  turns by more than 30° -- not from the normals (at an edge the normal is one face's, and a node that looks as if it sat on one face is
  moved into it). A node next to the edge moves over its own face. A corner of a cell is judged along its node's normal when that agrees
  with the cell and along the cell's own normal (the cross of its diagonals) when it does not: at a node on an edge the normal belongs to the
  other face, and a cell on this one would be seen from the side. A cell round a thin rim (its corners' normals differ by more than 100°)
  is judged by its sides only. A move that would fold a cell is not made. Nothing but the places of the nodes change.
- *Straightening* (the second phase of `relaxCells`): the energy above has a dead zone (corners within 15° of a right angle and sides
  within 1.35 of each other cost nothing), so a row of cells that is nearly straight but rocks back and forth costs nothing and the moves
  above leave it alone. Every node that is not on an edge or a corner (and not in a cell round a thin rim) is drawn towards the mean of
  its neighbours and put back on the surface: rows beside a straight row (an edge, or a row already straight) relax into straight
  lines, elsewhere into smooth ones. A move is made if it dents no cell, makes none worse by more than a hair, and keeps the node on
  its own face; the neighbours of a node that moved wake up again. Edge nodes on a STRAIGHT stretch (their two neighbours on the edge
  in line with them) slide to the middle of the two, which spreads them evenly along the edge (the middle of two nodes of a straight
  edge is on the edge; the field and the eight probes say whether it still is: a bent edge fails both). Before the rounds, a lattice
  vertex that lies within half a cell of an exact edge node (the layout hands the edge nodes of the graph on with the map) and is not
  on an edge itself moves onto that node when the chord to it lies on the surface and the move is a valid one: a straight lattice line
  cannot follow the rim of a hole, and a vertex a few tenths of a cell beside the rim made a ragged outline there.
  `FIELDES_SC_NOSTRAIGHT=1`, `FIELDES_SC_NOSLIDE=1`, `FIELDES_SC_NOVSNAP=1`.
- *What is forbidden* (`mapDefects`): a cell that is dented or turned over, one that lies on top of another, an edge whose middle is more
  than half a cell from the surface (it runs through the air), two corners at one place. They are counted exactly on the map that is
  delivered, and what is left is said in the warning (`libfive_lattice_last_warning`) with its number. `thinFeatureShare` follows the
  way into the material and the way out from up to 600 nodes of the map until the field changes sign (the thickness of the wall, the
  width of the gap or hole there), and when 1 % or more lie on features under 0.8 of the cell the warning says so, with the smallest
  size: a hole or a wall thinner than a cell can be missing from the map even when all the placements agree. (Edges much shorter or
  longer than the rest, and edges that cut a corner of the surface -- a cell that straddles a sharp edge with no vertex on it -- are
  counted as ugly, not forbidden.)

*Nothing is left out* is the rule of this layout, and the code that keeps it is `mapPass`, `growAttempt` and `growQuads`. The pieces of
the surface graph (`SGraph::pieces`) are each mapped and each checked on its own (`checkPiece`: every edge on two faces, one fan round
every vertex, one orientation, one connected surface; then four different corners to every quad, every edge on two quads, every node on
the surface). What does not pass is not dropped:
a piece with fewer nodes than four regions of the lattice's own size hold (`4·(s/delta)²`, 225) cannot be judged at this cell size and is
sampled finer by itself (`FailedPiece`, `mapPass` with `seeds`: the box shrinks to the piece as it is learned, only the cubes near its
nodes are looked at, up to twelve halvings), until it is mapped, is found to be joined to a map that
exists (a node of it within 0.75 step of a mapped node on the same sheet), or is found not to be a surface at all (nothing at the finer
sampling, and the field on a grid round it keeps one sign: a speck within the placement tolerance of zero that never crosses it); a
piece of all-coincident samples (extent under a tenth of a step: a corner that samples from several sides landed on) has no area and
no cells. A piece whose faces make several separate surfaces is two surfaces that a few hops joined: the hops between them are cut
(`cutOut`) and the surface is laid out again. `growQuads` makes the whole layout ONCE, from the grid of sample points at the offset the user
asked for (`grid_offset`, 0 to 3: four fixed grids; `growAttempt`, `shift`), and the same call gives the same layout every time. It does
not try the other grids behind the user's back (an earlier version made two to four layouts and voted on their topology, which doubled
or quadrupled the run time of a real part and hid from the user that the answer depended on where the grid fell). Where the surface has
detail about as small as the spacing of the points (a hole, a wall or a neck about as thin as a cell) the grid decides which points are
joined, so one offset may close and another not, or give another number of holes. A map that is closed and covers the whole surface is
delivered whatever its topology, with the warning that says where the doubt is (the holes that were closed with a face, a piece that
was mapped at half the cell, the share of the surface on features thinner than a cell, the number of forbidden cells) and names the
remedy: another `grid_offset`, or a smaller `cell_size`. Where the grid gave no closed map, the error names the piece and where it is,
and gives the same two remedies; nothing is returned. The box a piece is sampled in may reach 1.5 cells beyond the region (a body's
region is its own extent, so that a piece that fills it -- a tube, a plate -- is not always "cut by the box"; a test with a margin
round the body had hidden that). (`FIELDES_SC_SHIFT=n` overrides `grid_offset` for the developer.)

**The sheet layout** (`quad_layout.inl`, `sheet::growQuads`): for a field that is zero on a sheet with no body, cut by the region. A
scaffold is made of marching tetrahedra over the cubes the surface passes through, in a box two cells larger than the region (the
sheet's edge is ragged, and the region must lie well inside it); its vertices are projected onto the surface and, where it turns
sharply, onto the edge. A direction field and a lattice position field are solved over the scaffold on a hierarchy of coarser vertex
graphs, the triangles that lie in one square of the lattice are a region (a sliver is joined to a neighbour, a region with six corners
or more is cut in two), a region of k corners is k quads, and the nodes are moved over the surface until the cells are even. The
scaffold is closed except where the box cuts the surface, so the layout has an edge there and only there; it is checked like the other
(four different corners, every edge on two cells, one only on the edge).

No cell is left out for its shape: a cell is as stretched or bent as the surface makes it (earlier versions threw cells away at every tight bend, by a stretch test, then by tests on columns and rings; none is left). Each node has a
second normal, `nl`, the mean of the normals of the nodes it shares a cell side with (`liftNormals`), which is what a layer
is lifted along, so that the cells over a crease share the plane they meet in. Thickness (`measureThickness`) is the
deepest the body goes along `nl`, twice the distance of the deepest point of the tent formula (a normal that runs along a
face of a thin part would otherwise leave the body far off), smoothed over the nodes that share a cell side (the middle
value). **The material is filled once** (`dropRedundantLayers`, after the thickness is known, only for `side='inside'`): the
map of a shell has both faces of the slab, and the column of a cell on one face is the column of a cell on the other. The
column of a cell is the hexahedron of its four corners and the four points a layer's depth below them along `nl`; a cell
(deeper than 0.3 of a cell) is left out only if each of 27 points of its column (3 x 3 x 3) that lies in the material (the
field is negative there) is inside the column of another cell that is not left out (a Newton inversion of the trilinear
map, found through a hash of the columns); the cells that cover those points are marked and can no longer be left out
themselves, so the coverage of the material can never shrink by a chain of leaving-outs. The order in which cells are tried:
the smallest surface piece first, and on equal pieces the cell that faces away from a fixed direction. (A first version
matched the middles of columns of cells that face opposite ways: it left the rims of plates bare, 9 points of the material
inside no column; the test of covering costs one inversion per sample and leaves 0.4 to 2.) On a frying-pan skin (the one of example 17)
290 of 1120 cells are left out. `FIELDES_SC_NODEDUP` switches it off, for measuring the whole map. `beamsFromCells` carries each
beam of the unit cell over by a **Coons patch** of the cell's four edge paths (`coons`): on a flat cell the bilinear point
between the corners, on a cell that wraps a lip a point that stays by the surface; each point is then projected onto the
surface, and lifted along the normal by the layer's depth (bilinear between the corners). The nodes of the whole graph are
keyed by the node ids (a corner column by its node, a point on a side by the two nodes and the position along it, a point
inside by the cell), so shared faces give the same node to both cells, and the output order depends on nothing but the
cells. The graph goes to `libfive_beam_lattice` (`BeamOracle`: a BVH of beams, a distance with a Lipschitz interval, the
gradient of the nearest beam's axis for beams of one radius), which is what the mesher sees. A selection is cut out with its
own `patch - whole` field.

*A periodic surface on the cells* (`fieldCellTpms`, `CellMapData`, `CellMapOracle`). The same cells carry a TPMS too. Each
cell is a map `X(s, t, w) = C(s, t) + h·w·T(s, t)·N(s, t)` of the unit cube: `C` the Coons patch of the cell's four edges (each a
quadratic Bézier through the start, the middle by arclength and the end, so that two cells that share a side give the same
curve), `T` the thickness (bilinear) and `N` the normalised bilinear mean of the node normals `nl`. The oracle finds the
cell a point is in by a BVH over the cells' boxes (`w` from −0.6 to 1.6, so that a point a little outside a cell still
finds it) and inverts the map by Newton's method with the analytic Jacobian (start: the middle, then the corners), and takes
the cell whose coordinates are most central; the field must be a pure function of position (a "cell I was last in" hint
made it depend on the order of the queries, and the mesher's threads then saw overlapping cells and crashed in the
evaluator — it is not used). The periodic function is evaluated at `(2π s, 2π t, 2π·layers·w)` with its analytic gradient
(`tpmsGrad`: gyroid, Schwarz P, diamond, Neovius, Lidinoid, split P, IWP, FRD, Fischer–Koch S); the gradient is divided by
its length times a scale, taken from the edge lengths of the nodes interpolated over the cell (continuous across a
cell side, where the physical gradient is not), to make `d`, an approximate distance; the sheet is `|d| − thickness/2`, a
network `±d − offset`, trimmed to the layer by `−min(w, 1−w)·T` (and a skin `skin` thick along the faces). The interval of
the oracle is `value ± 3·r` (a Lipschitz bound of 3 on `d`), the gradient is that of `d` by the map's Jacobian. The cosine-only
functions look the same when the axes of a cell are turned a quarter, so they have no seam where rows of cells meet; the others
do. It is a TPMS on a surface that is not smooth in the corners, with cells that are not exactly rectangles, so the
walls in a few cells at a crease or where rows of cells meet are rougher than the rest.

What was tried, and went: coordinates from a mesh of the surface (`u` the arclength of the base curve, `v` a geodesic found by fast
marching, four oracles that read the nearest triangle), evaluated by the mesher at every point it samples — about a hundred
nearest-triangle searches per output triangle, and unsound or loose intervals so that the whole layer's volume was sampled, not its beams.
It also depended on the mesh being watertight, and a mesh of the exact-distance field of an imported part (what `shell_outside`,
`thicken` and `offset` use) is not: its zero set has cracks along the first planes the mesher splits its region at (14,000 open edges for
a frying pan), the surface fell in two pieces and the coordinates were garbage (the cracks are the exact-distance field's own, `_dist`,
which is itself the distance to a mesh of the part, and still have to be fixed there). Then a walk of columns from a base curve, a square
grid grown as a front over a shared set of nodes, and rings grown from one point (a ring is a line and the strip between two lines closes
by construction; what was left was where fronts meet: a seam across every body, triangles and pinched pattern cells along a line, a start
point that mattered): every one of them left cells that did not close — isolated tilted boxes on a wall, holes on a rim — which a spring
relaxation and a string of thresholds could not make clean. A scaffold of marching tetrahedra with a global field (`quad_layout.inl`)
cannot have a hole and gives cells that look good, but its regions of triangles do not make a closed map of a body of arbitrary
topology; it is kept as the layout of a sheet, where its edge is the region's. The body layout reads the map off a graph of points
instead and CHECKS it, every piece, every time, because a layout that is closed can still cover too little: a cavity left unmapped, a piece dropped for
being small. What no check of the map can know is a feature thinner than its cells: a handle through a hole narrower than a cell, or a wall thinner than
a cell, can be lost in a map that is closed and consistent (edges through the gap are not distinguishable from the sag of an edge round a thin rim by
any measure on the field that was tried), so the cells have to be smaller than the features, and the documentation says so. Each of those was found by measuring on bodies it had not been built
on (random solids and the example parts, a voxel count of every body as the reference, and a count of the separate surfaces of each
piece); the voxel count itself cannot see a wall or a gap thinner than its voxel, which is why a body with a wall 0.2 mm thick can look
wrong when it is right.

## The render cache

The cache is **on by default**: a shape the script does not mention keeps its finished mesh when meshing took 0.4 s or more
(`Shape::m_cache_on` starts true; `m_cache_forced` is set by an explicit `render_cache(x)`, which keeps it however
quick). `render_cache(shape, False)` (`stdlib/render_cache.py`) puts the attribute `_render_cache = False` on a copy of the
shape, the opt-out; the interpreter (`recordShape`) hands it to `Shape::setRenderCache`. A shape with the cache on keeps its finished mesh
(`Shape::writeRenderCache`, `<cache dir>/render-cache/<sha1 of the key>.fdmesh`: header, box, the key itself, float
vertices, triangles and the colour values) and `Shape::renderMesh` looks the key up before it meshes, at the start of a
render and again when the key changes; a hit is the finished mesh whatever level asked for it. The key is
`libfive::treePersistentKey` of the tree (a free variable counts by its value in a snapshot of what the evaluators
hold, an oracle by `OracleClause::persistentKey()`, a hash of its data — an oracle with none, such as a solved
analysis' field, makes the shape un-keepable and the row's button amber) plus the colour field, the exact regions
(file size and time, the placement trees), the region, the resolution, the quality and the algorithm, and
`kRenderCacheVersion` (bumped when the mesher's output changes — not on every build). The tree's key is made of the tree
as the script built it (`Shape::built_tree`), not of the optimized one: the optimizer's output differs from run to
run in the order of operands. A mesh an oracle holds is keyed by the surface, not by the order the mesher wrote it
in (`meshContentKey`: each triangle from its smallest corner, the triangles sorted). `render_cache_key()` shows the key. When anything about the math
changes the key is another one: a miss, the usual progressive render, and the new mesh is kept. The folder is
limited to 4 GB (`FIELDES_RENDER_CACHE_MB`), oldest files first; `Settings → Clear the caches` empties it (and the field cache).
The model tree's cache button writes and deletes the opt-out line `x = render_cache(x, False)` under the shape's definition
(under its `expose()` and `handles()` lines) like every other button of the tree (`ScenePanel::toggleCache`; the script
parser records the line as `cache_off`, an explicit `render_cache(x)` as `cache`).

The right-click in the viewport (`View::showSelectMenu`) emits `surfaceSelectRequested`; `ScenePanel::addSurfaceSelection`
writes the call into the script as an edit, so the selection is code like everything else. The boundary conditions
(`stdlib/boundary_conditions.py`) are the part itself painted (colour field with the categories of the
`bc` colour map in `colormap.hpp`) plus **symbols the viewport draws over it**: Python finds an even array of points on
the surface inside each region (a grid of samples near the surface, one to a square of the spacing, projected onto the
surface by Newton steps on the field) and hands `_bc_glyphs` (kind, tip, point, direction, size) and `_bc_labels` to the
`Shape` (`setBoundarySymbols`); `View` draws arrows and pads (`Glyphs` in `arrow.cpp`, lit by the `basic` shader, depth
tested) and the texts (`drawBoundaryLabels`). They are not in the mesh, so the render region does not clip them and the
render's resolution does not make them ragged. The symbols are part of `Shape::colorKey`, so a shape is only reused when
they are the same.

## The field cache

`python/fieldes/field_cache.py`, called by the runner (`_continue`) for every statement `name = <expression>` that is not a
call of a file reader (`import_*`, `load*`, `read*`, `open`, `save*`, `write*`, `export*`, `input`, `var`, `expose`, `handles`,
`render_cache`, `progress`). `lookup` makes the key — sha1 of the statement text, the content key of every name it reads
(`content_cache.value_key` / `shape_key`; a function or module without content makes the statement uncacheable), the
current `var()` numbers and the code identity (the kernel library's size and time, the library's Python files' sizes and
times) — and returns the kept `(Shape, printed text)`; `store` keeps a statement that took 0.25 s or more. The memory layer
holds the Shape itself; the disk layer holds `<key>.fdtree` (`libfive_tree_save`) and `<key>.fdfield` (JSON: the plain
Python attributes of the Shape and the text it printed) and is written only when `libfive_tree_can_save` says every oracle
in the tree installs a serializer (`OracleClause::install<T>(name)`; `libfive_tree_save` refuses an unsavable tree instead of
writing a file that cannot be read). Oracles that serialize today: transformed oracles, `BeamLattice` (blend and beams).
A new oracle type gets persistence by installing its serializer — nothing else changes. The folder is `field-cache/` beside
the render cache (`FIELDES_FIELD_CACHE_DIR`), 3 GB, oldest first.

A call of the conformal layout is ONE `libfive::run_progress::Task` (`stagePlan` in `surface_cells.cpp`, created in
`makeCells` for the layout it chooses): `kBodySteps` / `kSheetSteps` list the steps in the order they run, each with its
share of a typical call (percent, measured with `FIELDES_SC_STAGES=1`, which prints each step's seconds and its last
words). `stageBegin(name)` puts the bar in that step (a step already passed, or one that is not in the list, only changes
the text); `stageSet(fraction)` / `stageTick(evaluations)` move it inside the step's share by what the step has counted
(levels, rounds, sweeps as point updates, field evaluations against the number the step is known to need). The task never
goes back. A second pass of the body layout (the first map was cut apart and mapped again) is recognised by the first step
beginning again; it spends the rest of the bar from where the first stopped. Not predicted per body: the shares are one
table, so a step can end earlier or later than its share.

## The exact tessellator

`step_tessellate.cpp`, used by `exclude()`: each face is meshed in its own (u, v) parameter space and its
boundary edges are shared between neighbouring faces through an edge cache, so the pieces join into a
watertight surface without a global weld.

- Planar, cylindrical, conical, spherical and toroidal faces are sampled analytically.
- B-spline faces (`step_bspline`) are evaluated from the spline exactly (de Boor), with a foot-point search
  for projecting onto the surface and adaptive edge sampling that follows curvature.
- The boundary polygon of each face is triangulated in (u, v) with Manifold's polygon triangulation, then
  improved: edge flips (Delaunay and tilt aware), refinement where the surface turns faster than the
  triangles, splits where a facet is tilted from the true surface.
- The chord error is bounded by an absolute tolerance scaled to the solid's size, so micro-features do not
  over-tessellate.

- The free-form faces of a solid are refined **in parallel** after the others (each depends on its own outline
  only: `Deferred` in `tessellateSolid`), and the result is the same on any number of threads.
- A cylinder or cone whose holes are windows that wind round it (a worm's grooves cover every u, so no cut along
  the axis misses them) is cut open along a **helix**: the (u, v) are sheared by the windows' slope, the widest gap
  found there, and the cut's points are put on both sides, one turn's share apart. Holes of a face with a partial
  outline are moved by whole turns to where the outline is. A sphere's cap, bounded by one curve round its pole,
  gets curves between the outline and the pole. Tori are refined by the turn of the normal and the new point is
  the surface point nearest the edge's middle; a refinement is kept only if the mesh's area comes closer to the
  patch's own (`A = r |∮ (R v + r sin v) du|`), except at the apex of a spindle dome.
- The tessellating importer (`import_step_tessellated_parts`) is made of this tessellation: `step::brepParts` tessellates every
  solid (a few at a time, each on all threads) and describes the placed occurrences; the Python side makes
  the triangles a distance field (`libfive_mesh_from_arrays`), places it with `remap`, and keeps the tessellation
  in `<file>.fieldes-tessellation` (keyed by the file, `quality` and `kTessellationVersion`).

Verified against OpenCascade (gmsh) face by face and by volume, and against the main importer by inside/outside
sampling (see the changelog for the numbers); two recognisers of rational arcs (cylinder, cone) now check that an
arc is circular at two more points, as the quarter of an ellipse has a circle's weights.

## Files FielDes writes

| File | Where | Holds |
|---|---|---|
| `<name>.step.fieldes-cache.py` and `.fieldes-cache.trees/` | next to each imported STEP file | the import cache; safe to delete |
| `field-cache/<key>.fdtree`, `.fdfield` | FielDes's cache folder (beside the render cache) | the field cache; safe to delete (Settings → Clear the caches) |
| `<name>.step.fieldes-tessellation/` | next to a STEP file imported with `import_step_tessellated_parts` | its tessellation (`meta.json`, one `.mesh` per solid); safe to delete |
| autosave | the script's own file, every 5 s once it has a name | your script |
| settings | the usual per-user location (`QSettings`: `FielDes`) | window, splitter, recent files, shortcuts |

Nothing else is written outside the folders you choose (meshes exported by the script, STLs).

## Environment variables

| | |
|---|---|
| `FIELDES_DIR` | folder holding `fieldes.dll` (for the Python library without the application) |
| `FIELDES_AUTOMATION` | path of a command file: run a scripted GUI session (developer facility, `automation.hpp`) |
| `FIELDES_TIMING` | print timing lines to stderr |
| `FIELDES_FIELD_CACHE_DIR` | folder of the field cache (default: `field-cache` beside the render cache) |
| `FIELDES_NO_EXACT_OFFSETS` | thicken, shell and offset an imported part with its own field instead of its exact distance |
| `FIELDES_TESS_DEBUG`, `_TRACE`, `_QUALITY`, `_DUMP` | tessellator diagnostics (developer) |
| `FIELDES_STEP_*`, `FIELDES_FEA_*`, `FIELDES_TET_*` | importer and solver diagnostics and tuning knobs (developer; see the `getenv` calls in `kernel/src`) |

## Origins and licences

FielDes stands on **libfive** (Matt Keeter, [github.com/libfive/libfive](https://github.com/libfive/libfive)).
From libfive come: the expression tree and evaluators, the dual-contouring/simplex mesher, the Python
bindings' shape and the GUI ("Studio") from which `app/` derives. Everything FielDes adds — the STEP
importer and exact tessellator, mesh import, fields, lattices, FEA and thermal solvers, optimisation, the
model tree, section and result cards, handles, the new theme — is in the same files and folders under
the same licences: `kernel/` and `python/` are MPL-2.0; `app/` is GPL-2.0-or-later. See
[NOTICE](../NOTICE.md) for third-party components.
