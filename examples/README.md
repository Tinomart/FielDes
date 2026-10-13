# Examples

Open one with **File → Open example file**, or run it without the application:
`python scripts/run_example.py examples/05_static_analysis.py`. **Every example runs as it is**: the ones that start from a part use
the pivot-bearing support bracket that comes with FielDes (`step/PivotBearingSupportBracket.STEP`), the others make their
own geometry. The scripts use relative paths (`step/PivotBearingSupportBracket.STEP`), so they work from wherever the examples folder is;
the application switches to a script's folder when it opens it.

The first import of a file takes a few seconds and is cached next to the STEP file in `<file>.fieldes-cache.py` and
`.fieldes-cache.trees/`. These caches can be deleted at any time.

| Script | Part | Shows |
|---|---|---|
| `01_import_a_part.py` | the bracket | `import_model`, `roi`, `roi_resolution`, the view settings, `mass_properties` |
| `02_inspect_a_part.py` | the bracket | wall thickness, overhang, curvature and depth fields painted on the part; hover probing; the section card |
| `03_exclude_and_assembly.py` | the bracket | an assembly of three (the parts of an import are shapes like any other: move, turn, copy them); `exclude()` with a field object as the region: the STEP file's own surface where it matters |
| `04_handles.py` | the bracket | the gizmo (`gizmo()`: click / never / always), the handles on the surfaces (`handles()`), and the lock, of the model tree |
| `05_static_analysis.py` | the bracket | static FEA: supports, loads, result card, safety factor |
| `06_modal_analysis.py` | the bracket | natural frequencies and mode shapes |
| `07_thermal_analysis.py` | the bracket | conduction and convection, heat balance |
| `08_topology_optimization.py` | the bracket | the stiffest part in 50 % of the material, the lug holes kept by `exclude()` |
| `09_lattice.py` | the bracket | a gyroid whose wall follows a regression over the depth below the skin |
| `10_field_driven_design.py` | the bracket | stress field → lattice density |
| `11_custom_lattice.py` | the bracket | custom cells: any geometry in a box (`cell_custom`), a strut cell (`cell_custom_truss`) and a TPMS equation (`cell_custom_tpms`) |
| `12_mesh_export_and_import.py` | the bracket | STL out and back in with `import_model` |
| `13_tessellated_import.py` | the bracket | `tessellate`: a STEP part imported exactly, nothing fitted; an exact shell and offset of it |
| `14_conformal_lattice.py` | none (an S-shaped surface, the zero of a field) | `lattice_surface_conform` on an open surface of no thickness with a custom cell (`cell_custom_truss`): one layer on one side of it, cut off at the edge of a patch |
| `15_conformal_closed_body.py` | the bracket | `lattice_surface_conform` on a closed body (a whole bracket): the cells fill its wall all the way round, over the faces, fillets and bores, with a row of nodes on every sharp edge |
| `16_fluid_flow.py` | none (a round post in a slab of water) | `fluid_analysis(body, domain, ...)`: water past a post at Re 40, slip planes for a two-dimensional flow; the speed on the fluid, streamlines with moving particles, the wake behind the post, the drag; `time=` for the flow in time with the step slider |
| `17_flow_topology_optimization.py` | none (the post of example 16 in its slab of water) | `flow_topology_optimization`: the post reshaped for the least drag with its volume kept; the body in the flow with its wake, iteration by iteration |
| `18_fields_everywhere.py` | none | wherever a number goes, a field goes: a plate grown by a field (`offset`), a blend radius that varies (`smooth_union`), a lattice of graded cell size, driven from a `point` |
| `19_graded_material.py` | none (a cantilever beam) | fields in an analysis: a material whose stiffness varies, a load spread by a profile, compared with beam theory |
| `20_custom_blocks.py` | none | custom blocks (`blocks/sample_blocks.py`): `perforate` with a field radius, `bracket` |
| `21_low_level.py` | none | low-level fields, as in libfive's Scheme `define-shape` / `remap-shape`: a cube from six plane distances (`.max`), a twist by `.remap`, a ball and a torus from their formulas joined by a smooth minimum of your own, a gyroid cut to a cube; evaluating and printing a tree |
| `22_export_files.py` | none (a beam with a hole) | exporting: `export_mesh` in every format (STL, OBJ, PLY, 3MF, glTF, FBX, OFF, DXF), `export_fea` (Abaqus / CalculiX, Nastran, Gmsh), `export_results` (VTK, CSV, key numbers as text); sections in the model tree |
| `26_sketches_and_sweeps.py` | none | points, a **sketch** (corners and a spline point), `extrude`, a `spline` and `path_extrude` of a sketch whose thickness follows the scale of a point: pull a point out of an edge, double-click a point, scale it with its ring |
| `27_copies_and_linked_copies.py` | none (a plate with lugs and bolts) | `Shift+D` (a copy) against `Ctrl+D` (a linked copy, `instance`): three bolts that follow the first one, a lug copied and changed on its own; shadows in the model tree |
| `28_sketch_on_a_plane.py` | none | a sketch in a tilted plane, `extrude` along the normal of its plane, a smooth badge on it, and how to draw on the plane with the **work plane** (`Shift+W`) |
| `24_app_bracket_maker.py` | the bracket (it comes along inside the app) | a FielDes app: `app.title`, `app.file`, `app.choice`, `app.number`, `app.save_as`, `app.asset` -- a form for someone who never opens the editor; File > Preview as app shows it, File > Export as app makes the `.fieldesapp` |
| `23_lattice_gallery.py` | none (a simple body for each tile) | one lattice of every kind: strut cells and a custom cell, a TPMS sheet and network, a honeycomb, foams around `random_points` (`voronoi_lattice`, `delaunay_lattice`, by density), a wall graded by a field, a cell map and a conformal layer |

| `29_symmetry.py` | none (a table: a top and one leg) | a **symmetry** (`symmetry_planes`): select it and everything you make is made at its images too; `symmetry_image(leg, mirror, 1)`; moving the symmetry moves its images, dragging an image drags the original; the card, `Shift+S`, a model's own planes in teal |

(Open each script: the comment at its top says what to look at.)

## About the sample part

`step/PivotBearingSupportBracket.STEP` (187 kB) is a pivot-bearing support bracket, about 150 x 120 x 88 mm: two bolting plates at the back and
two lugs with round holes at the front. It is the one sample part that is distributed with FielDes (at the maintainer's decision: it is a
GrabCAD download whose author's terms are not known; see [`../NOTICE.md`](../NOTICE.md)). The scripts place their regions (supports, loads,
boxes) for this part. To try them on a part of your own, put its STEP file in `step/`, change the path in the script, and adjust those numbers
(the importing, the fields and the lattices work with any STEP file).
