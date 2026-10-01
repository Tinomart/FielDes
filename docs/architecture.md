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
| `src/render/brep` | The meshers: dual contouring, simplex and hybrid, with an octree/xtree that splits regions until a cell is 1/resolution |
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
   Outside the part the value of a plane is its distance to the INFINITE plane, which can be far less than its
   distance to the faces that lie on it. So a plane (and each helper plane) is not allowed to be smaller, outside
   the box of its faces, than the distance to that box: `s' = max(s, min(distance to the box, 1000 s))`. Where
   `s <= 0` this is `s` itself, so a point is inside exactly where it was; the boxes are written
   `square(max(max(lo - x, x - hi), 0))`, which `expose()` does not offer for dragging. Cylinders and the other
   round surfaces are left alone (they gain almost nothing for their cost).
   **Convex edges are round.** A cube is a max of its surfaces' distances, and the max of two distances is a
   mitre: the offset of a convex edge would come out sharp, while cylinders and the face boxes come out round.
   Where a cube has a real convex edge on the outside of the part (the cell diagonally across both of the edge's
   surfaces is outside), the cube is also taken with the exact distance to the box its axis-parallel planes
   bound, `sqrt(sum of the squared outside distances per axis) + min(deepest, 0)` (a cylinder with the planes
   across its axis: `sqrt(axial^2 + radial^2) + ...`). Inside the box this is the largest of the surfaces'
   distances, which the cube already is, so nothing inside changes; outside it is the straight-line distance, the
   offset a ball makes. Edges between tilted planes, or between a plane and a cylinder that is not on an axis, are
   still mitred.
5. **Emit** a tree (an expression of `x, y, z`) per solid. The import cache stores these trees.

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

Known limit: **large torus faces** are not refined (sampled analytically only); they can come out a few
percent small. The field has them exact.

## Files FielDes writes

| File | Where | Holds |
|---|---|---|
| `<name>.step.fieldes-cache.py` and `.fieldes-cache.trees/` | next to each imported STEP file | the import cache; safe to delete |
| autosave | the script's own file, every 5 s once it has a name | your script |
| settings | the usual per-user location (`QSettings`: `FielDes`) | window, splitter, recent files, shortcuts |

Nothing else is written outside the folders you choose (meshes exported by the script, STLs).

## Environment variables

| | |
|---|---|
| `FIELDES_DIR` | folder holding `fieldes.dll` (for the Python library without the application) |
| `FIELDES_AUTOMATION` | path of a command file: run a scripted GUI session (developer facility, `automation.hpp`) |
| `FIELDES_TIMING` | print timing lines to stderr |
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
