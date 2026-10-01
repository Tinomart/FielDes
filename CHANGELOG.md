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
  architecture notes); the surface is unchanged, handles still drag the surfaces and the part stays solid. Parts
  imported earlier are imported again once (the import cache is versioned).
- **Mesh import reads back what was exported.** Where the nearest feature of a mesh has no clear side (a sliver
  folded back over its neighbour, a knife edge: a dual-contouring mesh has them along sharp edges), the sign of the
  distance came out wrong, so points a few millimetres from a part exported with `save_stl` read as inside it on
  re-import, and an offset of the imported mesh broke. The generalised winding number now decides there.
- `expose()` with plain numbers no longer reads freed memory, and a plane through the origin that several
  unions share gets one number, not one per union.

### Analysis and design
- Static, modal, thermal and thermal-stress analysis on body-fitted tetrahedra (or voxel hexahedra),
  structural and thermal topology optimisation, several load cases.
- Fields and regressions: distances, maps, `fit()`, data fields, `colored()`; all results are fields.
- Lattices: nine TPMS families, a dozen strut lattices, planar patterns, Voronoi foams, surface and graph
  lattices, custom unit cells and equations, conformal cell maps, field-driven sizes.
- Mesh import (STL, OBJ, PLY, 3MF, glTF) as exact distance fields.
- Content-addressed caches for imports, analyses, exact distances and graphs.

### Known limitations
See the README: fitted (not exact) B-spline faces in the field; tori not refined in the exact mesh;
Windows is the only tested platform; linear analyses without contact.
