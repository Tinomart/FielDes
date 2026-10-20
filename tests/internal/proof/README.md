# Proof scripts

These are live scripts, not logs. Open one in FielDes (File > Open, or drag it onto the window) and it builds its parts from scratch, runs the
feature, and shows the result in the viewport and the output pane. Change a number in the text and run it again.

Every script has the same shape: a header that says what the feature is **supposed** to do and what the right answer is (worked out by hand or by
a formula, printed as plain reference lines starting with `BY HAND` / `KNOWN BEST` / `STARTING`), then the feature run, then the original beside
the result. There are no PASS or FAIL lines: you compare the result with the reference.

**Caches.** The scripts use the caches like every other script: a solve that has been done is read back, so a second run is fast. To run one from
scratch, close FielDes and delete the folders `result-cache` and `field-cache` in `%LOCALAPPDATA%\FielDes\FielDes\cache`.
Scripts that write files put them in a folder `out` next to the script. The two scripts on the bracket read its STEP file from `examples/step`.
Run the scripts one at a time: each is heavy. `param_06` changes the number in `thin = var(...)` when it finishes (`apply=True`): press undo, or
reopen the file, to run it from the start again.

## What the final build gives (measured in real windows, from empty caches, on a 1.3 GHz laptop processor, 2026-10-16)

### Parametric optimization (known best stated in each script's header)

| script | known answer | what the optimizer found |
|---|---|---|
| `param_01_cantilever.py` | lightest beam at 90 MPa and 0.5 mm: width 8 (its bound), height 14.26 (beam theory) | 8 x 14.312 mm in 36 designs, 0.4899 mm, 76.5 MPa |
| `param_02_modal.py` | highest first frequency at a given mass: a square section, 12.17 x 12.17 | 12.215 x 12.19 mm in 40 designs (1010 Hz; the elements read a little above the 994 Hz of theory) |
| `param_03_thermal.py` | coolest slab: the thickest, 25 mm | 25 mm in 14 designs |
| `param_04_interior_optimum.py` | an optimum in the middle of the range: 12.00 mm by calculus | 11.556 mm in 27 designs (the finite elements bend a little more than beam theory) |
| `param_05_three_variables_vs_grid.py` | a grid of 125 designs: lightest that keeps the limits 0.05385 kg | 0.04649 kg (width 20, height 8.70, radius 2.09) in 45 designs: 14 % lighter than the grid's best |
| `param_06_failures_impossible_apply.py` | 9.26 mm by beam theory for 90 MPa; a limit of 1 MPa nothing can keep | 8.82 mm; `apply=True` wrote `thin = var(8.846122)` into the text; the 1 MPa run says *NO FEASIBLE DESIGN* and lists the 16 designs |

### Manufacturing (volumes are measured on points of space: expect a few per cent)

| script | what it shows | on the final build |
|---|---|---|
| `mfg_01_check_finds_each_rule.py` | a part with seven broken rules, and a control that must report nothing | the control: *All rules satisfied*; the broken part: overhang 2.5 %, wall 0.92 %, gap 0.58 %, bridge 2.18 %, inside corner 8.46 %, draft 38.9 %, undercut 6.4 % of the surface (script 9.8 s, drawn in 0.6 s) |
| `mfg_01b_thin_wall_share.py` | does "how much of the surface" match the area worked out by hand | 5.9 % (by hand 6.8 %, plus the 0.4 mm strip along the edges that the rule does not count) |
| `mfg_02_shell_walls_gaps_corners.py` | a thin wall thickened, a slot closed, an inside corner rounded | fin +957.7 mm^3 (by hand 960 to 1008), slot +573.2 (576), corner +151 (154.5) |
| `mfg_03_overhang_gusset_and_teardrop.py` | a 45 degree gusset under a ledge, a teardrop over a hole | gusset +4905 mm^3 (by hand 5083; a straight prism would be 8000), teardrop -110.6 (107.3); the tool takes 2 s and the picture about 180 s at 3 cells/mm (it took 230 s before the entry search of the overhang remedy was given a finite range; at 2 cells/mm about 2.3 times quicker). The shell's own re-check lists two or three places on the flanks of the teardrop (0.04 to 0.3 % of the surface) as still over 45 degrees |
| `mfg_04_undercut_and_draft.py` | a groove filled, a draft put on the walls | groove +959.4 mm^3 (960), draft +2618 (2602) |
| `mfg_05_process_presets.py` | `fff_constraint`, `cnc_constraint`, `molding_constraint` on one part | each asks different things; the CNC preset rounds every inside corner (+14,893 mm^3) |
| `mfg_06_supports_and_print_files.py` | a refused remedy, a support block, and the files | the gusset is refused (25 % against 5 %), a support of 11,503 mm^3 is made, `print.3mf` 402,795 bytes, `layers.dxf` 21,609, `slice.svg` 358 |
| `mfg_07_free_build_direction.py` | the way up of a print found by the tool | (0, 0, -1) as expected; the part turned; `turned.stl` 187,484 bytes |
| `mfg_08_second_pass_and_limits.py` | a second pass, `max_change`, `keep=` | first +5008 mm^3; a second pass on its result +832 and *all rules satisfied*; the small budget refuses it |
| `mfg_09_imported_bracket.py` | a real part: the STEP bracket of the examples | script 54 s, drawn in 22 s; overhang on 9.2 % of the surface; the shell adds 35,609 mm^3 (12.9 %) |
| `mfg_10_topology_optimized_bracket.py` | a part that is hard to print: the bracket as an optimizer shaped it | script 217 s, drawn in 94 s; overhang on 6.6 % (12 regions); the shell adds 6,554 mm^3 (4.6 %) |
| `mfg_11_check_catches_a_wrong_fix.py` | the check is independent of the shell: it must catch a fix that is wrong | fins of 1.5, 3.0 and 3.7 mm are *broken*, 3.9 mm (inside the 5 % allowance) and 4.0 mm are *satisfied*; of the shell's own fin 1.4 % of the surface (a rim 0.1 mm wide) is up to 0.48 mm short, and the check says so |
