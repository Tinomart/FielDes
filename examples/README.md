# Examples

Every example starts from an **imported STEP file** (in `step/`). Open one with **File → Open example file**, or run it
without the application: `python scripts/run_example.py examples/05_static_analysis.py`. The scripts use
relative paths (`step/Bracket.step`), so they work from wherever the examples folder is; the application
switches to a script's folder when it opens it.

The first import of a file takes a few seconds (the kitchen assembly about a minute and a half) and is
cached next to the STEP file in `<file>.fieldes-cache.py` and `.fieldes-cache.trees/`. These caches
can be deleted at any time.

| Script | STEP file | Shows |
|---|---|---|
| `01_import_a_part.py` | `PivotBearingSupportBracket.STEP` | `import_step_parts`, `roi`, `roi_resolution`, the view settings, `mass_properties` |
| `02_inspect_a_part.py` | `ShaftSupportStand.STEP` | wall thickness, overhang, curvature and depth fields painted on the part; hover probing; the section card |
| `03_kitchen_assembly.py` | `Keukencombinatie.stp` | a 90-part assembly with free-form surfaces; the fit-deviation shading; `exclude()` with a field object as the region, on the whole import |
| `04_handles.py` | `MobileStand.step` | the gizmo (click / never / always), dragging surfaces, and the lock, of the model tree |
| `05_static_analysis.py` | `PivotBearingSupportBracket.STEP` | static FEA: supports, loads, result card, safety factor |
| `06_modal_analysis.py` | `ShaftSupportStand.STEP` | natural frequencies and mode shapes |
| `07_thermal_analysis.py` | `ShaftSupportStand.STEP` | conduction and convection, heat balance |
| `08_topology_optimization.py` | `PivotBearingSupportBracket.STEP` | the stiffest part in 50 % of the material, the lug holes kept by `exclude()` |
| `09_lattice.py` | `Bracket.step` | a gyroid whose wall follows a regression over the depth below the skin |
| `10_field_driven_design.py` | `PivotBearingSupportBracket.STEP` | stress field → lattice density |
| `11_custom_lattice.py` | `Bracket.step` | your own cells: any geometry in a box (`cell_custom`), a strut cell (`cell_custom_truss`) and a TPMS equation (`cell_custom_tpms`) |
| `12_mesh_export_and_import.py` | `MobileStand.step` | STL out and back in |
| `13_tessellated_import.py` | `Keukencombinatie.stp` | `import_step_tessellated_parts`: the kitchen of example 03 imported exactly, nothing fitted; an exact shell of a part |
| `14_conformal_lattice.py` | none (an S-shaped surface, the zero of a field) | `lattice_surface_conform` on an open surface of no thickness with a cell of your own (`cell_custom_truss`): one layer on one side of it, cut off at the edge of a patch |
| `15_conformal_closed_body.py` | `PivotBearingSupportBracket.STEP` | `lattice_surface_conform` on a closed body (a whole bracket): the cells fill its wall all the way round, over the faces, fillets and bores, with a row of nodes on every sharp edge |

| `16_fluid_flow.py` | none (a slab of water with a round post cut out) | `fluid_analysis`: water past a post at Re 40, slip planes for a two-dimensional flow; the speed on the fluid, streamlines with moving particles, the wake behind the post, the drag; `time=` for the flow in time with the step slider |

| `17_flow_topology_optimization.py` | none (the post of example 16 in its slab of water) | `flow_topology_optimization`: the post reshaped for the least drag with its volume kept; the body in the flow with its wake, iteration by iteration |

| `18_fields_everywhere.py` | none | wherever a number goes, a field goes: a plate grown by a field (`offset`), a blend radius that varies (`smooth_union`), a lattice of graded cell size, driven from a `point` |

| `19_graded_material.py` | none (a cantilever beam) | fields in an analysis: a material whose stiffness varies, a load spread by a profile, compared with beam theory |

| `20_custom_blocks.py` | none | custom blocks (`blocks/sample_blocks.py`): `perforate` with a field radius, `bracket` |
| `21_low_level.py` | none | low-level fields, as in libfive's Scheme `define-shape` / `remap-shape`: a cube from six plane distances (`.max`), a twist by `.remap`, a ball and a torus from their formulas joined by a smooth minimum of your own, a gyroid cut to a cube; evaluating and printing a tree |

(Open each script: the comment at its top says what to look at.)

## About the sample STEP files

| File | Size | What it is | In FielDes |
|---|---|---|---|
| `PivotBearingSupportBracket.STEP` | 187 kB | a pivot-bearing support bracket | **yes**: examples 01, 05, 08, 10 and 15 run as they are |
| `Bracket.step` | 56 kB | a small bracket | no |
| `MobileStand.step` | 85 kB | a phone stand | no |
| `ShaftSupportStand.STEP` | 99 kB | a shaft support stand | no |
| `Keukencombinatie.stp` | 6.4 MB | a kitchen unit assembly (90 parts, free-form surfaces) | no |

The bracket is the one sample part that is distributed with FielDes (at the maintainer's decision: it is a GrabCAD
download whose author's terms are not known; see [`../NOTICE.md`](../NOTICE.md)). **The other files are not included.**
To run the examples that use them, put your own STEP files in `step/` under these names (see
[`step/README.md`](step/README.md)). The scripts place their regions (supports, loads, boxes) for these particular
parts, so a different part needs those numbers adjusted; the importing, the fields and the lattices work with any
STEP file. To use a file under another name, change the path in the script that uses it.
