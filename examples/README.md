# Examples

Open one with **File → Open example file**, or run it without the application:
`python scripts/run_example.py examples/05_static_analysis.py`. **Every example runs as it is**: the ones that start from a part use
the pivot-bearing support bracket that comes with FielDes (`step/PivotBearingSupportBracket.STEP`), the others make their
own geometry. The scripts use relative paths (`step/PivotBearingSupportBracket.STEP`), so they work from wherever the examples folder is;
the application switches to a script's folder when it opens it.

The first import of a file takes a few seconds and is cached next to the STEP file in `<file>.fieldes-cache.py` and
`.fieldes-cache.trees/`. These caches can be deleted at any time.

## How long they take

Where to start, and what to expect. These are the seconds the **script itself** takes with every cache empty (`python scripts/cold_vs_warm.py` measures them, as of 2026-10-14; a second run is
faster, see [caching and performance](../docs/caching-and-performance.md)). The first *picture* adds the meshing of what is shown: seconds for most, up to a minute for a lattice that fills a whole part (the
progress bar shows it).

| Time | Examples |
|---|---|
| **Start here -- they open at once** (under 2 s) | `26` to `31` (profiles and sweeps, copies and linked copies, a profile on a plane, the symmetry, noise, texture), `04` (handles), `09` (a lattice), `11` (custom cells), `14` (a conformal lattice), `18` (fields everywhere), `19`, `20`, `21`, `22`, `24`, `25` (apps), `02`, `03` |
| A few seconds | `01` (import a part, 4 s), `07` (thermal analysis, 3 s), `10` (field-driven design, 5 s), `12` (mesh export and import, 3 s) |
| Up to half a minute | `05` (static analysis, 29 s), `06` (modal analysis, 22 s), `23` (the lattice gallery, 23 s) |
| About a minute | `15` (a conformal lattice in the walls of a whole bracket, 48 s) |
| **Minutes** -- start them and read the script meanwhile | `16` (water past a post, 2 min), `08` (topology optimisation, 2 min), `17` (flow topology optimisation, 3 min) |

The ones that start from a part (`01` to `08`, `10`, `12`, `13`, `15`) read the bracket that comes with FielDes.

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
| `26_profiles_and_sweeps.py` | none | points, a **profile** (corners and a spline point), `extrude`, a `spline` and `path_extrude` of a profile whose thickness follows the scale of a point: pull a point out of an edge, double-click a point, scale it with its ring |
| `27_copies_and_linked_copies.py` | none (a plate with lugs and bolts) | `Shift+D` (a copy) against `Ctrl+D` (a linked copy, `instance`): three bolts that follow the first one, a lug copied and changed on its own; shadows in the model tree |
| `28_profile_on_a_plane.py` | none | a profile in a tilted plane, `extrude` along the normal of its plane, a smooth badge on it, and how to draw on the plane with the **work plane** (`Shift+W`) |
| `24_app_bracket_maker.py` | the bracket (it comes along inside the app) | a FielDes app: `app.title`, `app.file`, `app.choice`, `app.number`, `app.save_as`, `app.asset` -- a form for someone who never opens the editor; File > Preview as app shows it, File > Export as app makes the `.fieldesapp` |
| `23_lattice_gallery.py` | none (a simple body for each tile) | one lattice of every kind: strut cells and a custom cell, a TPMS sheet and network, a honeycomb, foams around `random_points` (`voronoi_lattice`, `delaunay_lattice`, by density), a wall graded by a field, a cell map and a conformal layer |

| `29_symmetry.py` | none (a round table: a top, and one leg -- a profile swept along a spline) | a three-way **symmetry** (`symmetry_planes(kind='turn', copies=3)`: three turned copies, nothing mirrored -- any number up to 24, or planes): select it and everything you make is made at its images too; `symmetry_images(leg, mirror)` is the other legs **and their splines** as one model (the sweep and its path are mirrored together); dragging a dot of any leg's spline drags the original's point, changing the copies on the card redraws the legs; `Shift+S`, selecting the images brings back the mirror they were made in |
| `30_noise_kinds.py` | none (ten balls) | `noise_field`: the five kinds (`perlin`, `billow`, `ridged`, `cellular`, `cracks`), a stretch per axis, a `warp`, one octave against six, and a scale that is a field -- each ball is `ball - depth * noise`, so the relief is the noise |
| `31_texture.py` | none (twelve blocks) | `texture`: the six patterns (`grit`, `stipple`, `leather`, `ribs`, `dimples`, `knurl`), on the top face only (`select_surface`) or inside a body (`region`), `side='in'` / `'out'`, a pattern of your own, and a depth that is a field |
| `32_parametric_optimization.py` | none (a cantilever beam) | `parametric_optimization`: the lightest beam that stays under a stress and bends less than a limit -- the width and the height are `var()`s, the analysis is solved again with other numbers, and the best design is the result (every design tried is listed in the output pane) |
| `33_manufacturing_optimization.py` | the bracket | `fff_constraint`, `cnc_constraint`, `molding_constraint` and `manufacturing_optimization`: the topology-optimized bracket of example 08 -- struts of every thickness leaning every way, nothing designed for a process -- changed until a 3D printer, a mill and an injection mould can make it (gussets under what leans too far, thin walls thickened, inside corners rounded, draft), the plates and lugs that carry the load kept as they are; the result is coloured by what changed (about five minutes) |

(Open each script: the comment at its top says what to look at.)

## About the sample part

`step/PivotBearingSupportBracket.STEP` (187 kB) is a pivot-bearing support bracket, about 150 x 120 x 88 mm: two bolting plates at the back and
two lugs with round holes at the front. It is the one sample part that is distributed with FielDes (at the maintainer's decision: it is a
GrabCAD download whose author's terms are not known; see [`../NOTICE.md`](../NOTICE.md)). The scripts place their regions (supports, loads,
boxes) for this part. To try them on a part of your own, put its STEP file in `step/`, change the path in the script, and adjust those numbers
(the importing, the fields and the lattices work with any STEP file).

## Verification

`verification/` holds four scripts that solve a problem with a known answer and **stop with an error when FielDes is farther from it than a tolerance**: a cantilever beam, a plate with a hole,
the flow between two plates and the conduction through a bar (see [Verification](../docs/analysis.md#verification-four-known-answers)). They check the program, not your design; run them after
a change: `python scripts/run_example.py examples/verification/v1_cantilever_beam.py`. They take a second each, except the plate (about 30 s: the linear and the 10-node solve).
