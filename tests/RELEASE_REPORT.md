# Release report: what a release made from this tree would push

Prepared on the night of 2026-10-16 by Claude, for the maintainer. Nothing here has been pushed, tagged or published.
The release candidate is labelled **0.10.0** in the file names only: the version in `CMakeLists.txt` is still 0.6.1 (you bump it), and the packages were built with a temporary 0.10.0 that was put back afterwards.

The last pushed state is `origin/main` d59e380 (the 0.9.0 beta, 2026-10-14). This tree is 26 local commits ahead of it: 22 from before the night shift, then three from the night shift (the evaluator; manufacturing with the reference and the translations; the examples, the proofs and the changelog) and this report. The packages in section 5 were built from the third of the night shift's commits (0068b3a) with the temporary version number.

How to read the evidence: **tonight** = run again on the final build, in real FielDes windows from empty caches, and the pictures looked at; **earlier** = verified in an earlier session of this project and not changed since except by the evaluator work below.

## 1. Everything that was run tonight, on the final build

| what | result |
|---|---|
| all 33 examples, one real window each (`regress.ps1`; contact sheets looked at) | 33 of 33: picture, run finished, no error line. Four had defects, found and fixed tonight (section 3) and re-run |
| 12 manufacturing proofs (`tests/proof/mfg_*`) | all run; every picture looked at; the numbers against the hand answers are in `tests/proof/README.md` |
| 6 parametric optimization proofs (`tests/proof/param_*`) | all match their known answers (beam theory, calculus, a 125-design grid) |
| 4 known-answer solver checks (`examples/verification/v1..v4`) | cantilever -2.0 % (10-node elements -0.8 %), plate with a hole -3.7 % (+0.4 %), channel flow -0.3 % / +0.2 %, bar conduction 0.14 K: the numbers the documentation states, so the solvers did not change |
| the evaluator's output | the mesh of the heaviest manufacturing body has the same 29,571 vertices as before, each at a distance of 0.000000 from one of the old ones |
| translations | 1,382 of 1,382 program texts in each of es, fr, de, ja, zh, ru; none missing, none with wrong places |
| the library reference | regenerated: 393 entries |
| UI smoke (menus, the Help menu, navigation presets, Report a bug, the guided tour walked step by step) | see section 4 |
| the Linux packages (tarball unpacked, the four known answers run with its own program, the `.run` installer installed, run and uninstalled in a throw-away folder, the `.deb` opened) | all as expected; see section 5 |

## 2. The features of the release

1. **A call: a field asked at a computed point, without a copy of it** (kernel, Python `s(x', y', z')`). Values, derivatives (so dragging a handle), sharp-edge features, interval pruning and saving agree with the substitution it replaces to 2.4e-7.
   The copies of `smooth`, the images of a symmetry, the placement of STEP and mesh imports and the lookups of manufacturing are calls now (the bracket's optimization: 29 s and 307 MB where it was 97 s and 3.9 GB). A crash of the mesher on eight threads (a neighbour with no leaf) is fixed.
   *Evidence:* earlier (the equality tests, example 29's three legs); tonight (all examples that place copies draw, example 27 and 29 included).
2. **Imported meshes are drawn in seconds, not a minute.** Example 12 (232,764 triangles): 48.8 s to about 1.7 s in the window. *Evidence:* earlier, measured; tonight example 12 draws with the rest.
3. **The evaluator.** Passes of 8 to 192 points run with block sizes the compiler knows (both interpreters), the row of the value array is 17 cache lines, and the "is this corner ambiguous" check runs only for a corner whose value is exactly 0.
   Same mesh, vertex for vertex. The gusset example (`mfg_03`, 3 cells/mm) is drawn in **226.7 s where it took 280.6 s**; the heaviest body's finest level 134.5 s where it was 175.3 s. *Tonight.* Tried and not kept: a 4-point edge search for every field that calls others (23 % more vertices, vertices up to 0.25 mm off the surface).
4. **Manufacturing:** constraints (`manufacturing_constraint`, `fff_constraint`, `cnc_constraint`, `molding_constraint`), the check (`manufacturing_analysis`: every rule is a field drawn on the part, and a report), and the shell (`manufacturing_optimization`: thickens thin walls, closes or widens gaps, rounds inside corners, puts a draft on, fills undercuts, makes overhangs stand with a gusset or a teardrop), supports (`support='block'`), `export_print` (3MF), `export_layers` (DXF), `export_slice` (SVG), free build direction, `.oriented()`, `max_change`, `keep=`, a second pass.
   **The check is independent of the shell** (four files; the check imports nothing of the optimization, and measures a wall or a gap with its own method) and `mfg_11` shows it catching a fix that is wrong. Fixed tonight: the cut and the added material were combined so that a cut could erase a gusset; the check reported gaps at the feet of walls that are no gaps (a line now counts only if the face it ends on faces back); the report's volumes were off by a third for small features (now measured twice: 110.6 against 107.3 by hand).
   *Evidence:* tonight, section 1 and `tests/proof/README.md`; the complicated parts: the STEP bracket (script 54 s, drawn in 22 s) and the topology-optimized bracket (217 s, drawn in 94 s).
5. **Parametric optimization** (`parametric_optimization`): the best numbers for the `var()`s of a model over any analysis; `apply=True` writes them into the text. `Result.mass` (kg) and `ModalResult.volume` / `.mass` are new for it. *Tonight:* six proofs against known answers.
6. **Engineering:** a right-click menu of its own with a category and colour for each domain (structural, thermal, fluid). *Earlier;* the menu is dumped and pictured in the UI smoke tonight.
7. **10-node tetrahedra** (`element='tet10'`) for static and modal analysis: the cantilever -0.8 % with 1,296 elements where 66,816 linear ones read -2.0 %; the plate with a hole +0.4 % with 45,000 elements (one eighth of the plate, by symmetry). *Tonight* (verification v1 and v2).
8. **Four known answers and a notice:** `examples/verification/` stops with an error when FielDes is farther from the answer than a tolerance; the result card says *Not validated for safety-critical use*. *Tonight.*
9. **Settings > Navigation:** Blender style and Touchpad as options beside the default (Shift + left drag stays). *Earlier;* the presets are switched in the UI smoke tonight.
10. **Help > Report a bug** opens a prefilled GitHub issue (nothing is sent from the program); `KNOWN_ISSUES.md`. *Earlier;* the dialog is opened in the UI smoke tonight.
11. **The pane that has the keyboard has a frame, and the status bar says *Keys: viewport* or *Keys: editor*.** *Earlier;* Turning the view now says that it closed the work plane view (the grid and the drawing used to vanish without a word). `focusstate` is read in the UI smoke tonight.
12. **The guided tour is one walk of 32 steps** after the welcome card, with a lattice step; a skipped tour says where it is found. *Earlier;* walked step by step in the UI smoke tonight.
13. **`sketch` is now `profile`** (no alias; a beta change), and **round corners** (`point(x, y, z, radius=4)`: an exact arc, the arc is the handle of the radius). *Earlier;* the blue outline of a profile is drawn from its curves (the arc itself), not from pieces. Examples 26, 28 and 29 draw tonight.
14. **Cold against warm:** all 31 examples agreed when run with empty caches and again with warm ones (2026-10-14); the examples README says how long each takes.
15. **Packaging and documents:** packaging from the one build tree (`scripts/build-windows.ps1 -Package`), the rules of symmetry written down (`docs/interface.md`), a render strip in the README, the known-issues list, 50 stale translations removed on 10-14. *Earlier;* the packages of section 5 were made with that script tonight.

## 3. Found and fixed tonight (so that nothing is left)

- **Example 27 drew one big block.** Its `handles(...)` number lists were in the old order (a box's handles are now centre and half-size on each axis, a cylinder's x, y, radius, z from, z to): the second lug was a distorted box and the bolts huge. Fixed; the plate, the thicker lug and the row of bolts are drawn.
- **Examples 03, 14 and 20 rewrote their own `set_bounds` line** when they ran (their models reached 20, 7 and 8 mm outside the region they asked for). Their regions hold the models now.
- **The cut and the added material of the manufacturing shell** were combined wrongly (a cut erased a gusset); **the independent check had a false positive** at the foot of a wall; **the report's volumes were estimated on a handful of points** (above).
- **`analyse`** was a public name of the manufacturing check that every script saw; it is private (`_analyse`).
- **`docs/reference.md` lacked the manufacturing check and the constraints** (they moved to other modules), `exclude`, the shape class and three more; **71 program texts of manufacturing and parametric optimization had no translation** (49 old entries of rephrased texts were removed).

## 4. The window itself: UI smoke on the packaged build

One real window on example 33, from the portable folder that went into the installer and the zip, driven with the program's own automation commands (the same ones a person's hands would trigger). Pictures: `ui_*.png` on the report page.

| what | what was seen |
|---|---|
| keyboard cue | with the script editor in front: *Keys: editor*, frame round the editor; after the viewport is given the keys: *Keys: viewport*. Each carries a tooltip naming the keys of that pane |
| right-click on a model row | the menu: Primitives, Parametric shapes, Operations, **Engineering** (Structural, Thermal, Fluid, Manufacturing, New material, parametric_optimization), center, bounding_box, Import model, Custom resolution, Export, Select Surface, Delete. Entries that do not fit the selection are greyed with the reason beside them (support/load entries, Select Surface, the VTK and CSV exports that exist only for a static analysis). The full text dump is kept in the repository: `tests/ui_menu_model.txt` |
| Help menu | FielDes guide (Shift+F1), About, Report a bug, Guided tour, Shape reference (F1) |
| Settings > Navigation | default, Blender, touchpad and back to default switched without an error; the choice is not written to the settings during a test |
| Help > Report a bug | the dialog opened; Return on its default button made the program print the address it would have opened: the issue form of the project with the version (0.10.0), the kernel revision, the system (Windows 11), Qt (5.15.19) and the graphics driver; no script copied; a scripted run opens no browser |
| guided tour | started, then walked with *Show me* and *Next* through the welcome card and steps 1 to 32 (the last is "That is the tour"), then left |

One line was seen in the log of one of the two smoke runs (not in the other): `Exception ignored in: <function Shape.__del__> ... libfive_tree_delete`. It comes from stopping a run: when the tour edits the script, the running script is cancelled by raising an exception inside its worker thread (`interpreter.cpp`, `PyThreadState_SetAsyncExc`); if that exception lands while Python is deleting a shape, Python prints this line instead of stopping. The run was being cancelled and is started again, so nothing is lost and nothing shows in the window.

## 5. The packages (`dist\`, not in git, nothing uploaded)

| file | bytes | SHA256 |
|---|---|---|
| `FielDes-0.10.0-windows-x64-setup.exe` | 37,224,960 | `DDD0E521304E2CC47335CEAE17A74638D1FD1C228E6CAEC11D3F51F8B4D418CF` |
| `FielDes-0.10.0-windows-x64.zip` | 37,086,610 | `D36CC07B85B846C5809744C9848467BC49C3161A4BB884E13C4BAF493F690A86` |
| `FielDes-0.10.0-linux-x64.tar.gz` | 61,260,167 | `F4D103BC22DDC87CCC2CFFEC9B336DC97EB5DE37A19084F62BBA1B8F8DAF4614` |
| `FielDes-0.10.0-linux-x64-installer.run` | 61,266,041 | `F6C90D4703413223B32C8907BF333C098ADAD79F9BA36B80A683891C9A087828` |
| `FielDes_0.10.0_amd64.deb` | 61,275,686 | `7D8708A34B69AAF5F08D3718BDEE961F0A3EABCC5C614D721E6B600FC239304E` |

- **Windows:** the installer's payload and the zip each hold the same 794 files as the portable folder (checked by name and size); no `__pycache__`, no map file, no cache of mine.
- **Linux** (built in WSL Fedora 44 from the same sources): the tarball unpacks to `FielDes-linux/` (986 entries: the program, its libraries, Qt, Python 3.14, the library, the blocks, the examples, the licences); `ldd` with the bundle's own library folder reports nothing missing. The 66 `.pyc` files in it are the encodings of the bundled Python's standard library, which is how that runtime is shipped.
  **The Linux program computes what the Windows one does:** the four known-answer scripts, run with the extracted bundle's own `FielDes run` (offscreen), print the same numbers as the Windows run: cantilever -2.0 % (10-node elements -0.8 %), plate with a hole -3.7 % (+0.4 %), channel flow -0.3 % / +0.2 %, slab 0.14 K, each ending in `ok`.
- **What the Linux package needs:** a C library as new as the one it was built with (glibc 2.43, the same as the 0.9.0 package). It opens only on a system that new. For one that opens on nearly everything, build on Ubuntu 22.04: `.github/workflows/release.yml` does that (it runs when a tag is pushed, so it was not run).
- **The Linux installer was run for real, in a throw-away folder** (WSL, `--prefix`, `--no-menu`): it installed 913 files, the installed copy answered the slab known answer as above, and `--uninstall` removed it. **The `.deb`** was opened and checked (control file: package `fieldes`, version 0.10.0, amd64; 1,001 entries) but not installed: that needs root.
- **Not run here: the Windows installer.** It writes the registry (the language) and the Start Menu of the machine it runs on, which is the maintainer's own; it was checked by its payload (794 files), not by running it.

## 6. Honest notes

- **Speed.** What was asked for, an order of magnitude on the manufacturing render, was not reached. The gusset example draws in 227 s at 3 cells/mm on this machine (a 1.3 GHz laptop processor, no spare core): a point of that field is about 118 lookups of the part. A resolution of 2 cells/mm draws it about four times faster. The two complicated parts draw in 22 s and 94 s. Further gains need another formulation of the remedy's field, not another loop.
- **Example 10** (field-driven design) takes 174 s in the window where an earlier run took 147 s (its render is 135 s); the cause was not found. Its picture is right.
- **The shell's own re-check** can list places "still violated after the remedy": the independent check agrees (a rim 0.1 mm wide on a thickened fin, 0.8 % of the surface at the edge of a filled groove); they are reported, not hidden.
- **The kernel revision in a bug report** (Help > Report a bug) is the one the build was configured at (`a948ccd+`: the last commit before the night shift, with uncommitted changes on top). A release built after the version bump and configured at the release commit shows that commit.
- **16 old translation entries** are left (single common words that also occur elsewhere in the sources).
- **The installer and the zip are unsigned** (Windows SmartScreen says "unknown publisher" once).
