# Known issues

What you should know before you spend time on something. A problem that is not here: **Help ▸ Report a bug** opens a new issue with the version and your system filled in.
What a tool does and does not do (its scope, its accuracy, its cost) is in the guide, next to the tool: [docs/manufacturing.md](docs/manufacturing.md), [docs/analysis.md](docs/analysis.md),
[docs/fields.md](docs/fields.md), [docs/interface.md](docs/interface.md); the README has the design decisions (*Known limitations*).

## Platforms

- **Windows** is the main platform. The setup program and the zip are not signed: Windows SmartScreen may say *unknown publisher* the first time; *More info ▸ Run anyway*
  opens it (the source is public, and the setup is built from it by the script in `scripts/`).
- **Linux** packages (a folder, a `.run` installer, a `.deb`) are built on Ubuntu 22.04 (glibc 2.35), so they open on Ubuntu 22.04, Debian 12 and everything newer. They were run under WSL2
  (Ubuntu 22.04, with WSLg), not on every distribution.
- There is **no macOS** build.

## Modelling

- **A path extrusion that bends tighter than its profile is wide folds on the inside of the bend.** The curl of a horn is the example: the window draws the plain sweep of the profile along the path, and where the rings pile up on the inside the surface folds over itself, so some triangles of the mesh cross each other (565 pairs, in five places, on the horn that the tests use) and a few edges bend through up to 180 degrees. The shape looks right; a simulation of such a part, or a mesh made from it, should be checked at the bend. With `FIELDES_SWEEP_DEBUG=1` the log of the program prints the number of crossing pairs.
- **The end faces of an extrusion of a profile of points cannot be pulled to change its length.** Select the extrusion (the plate of `examples/26_profiles_and_sweeps.py`, for one), press its far end face and pull: the log says `[drag] surface moved ... numbers: 6 0` for every step with the numbers unchanged, and `handles(plate, [var(0), var(6)])` and the picture stay as they were. The circle, and the other 2D shapes that are made of numbers, are pulled as the guide says. The cause: the field of an extrusion of a profile of points is the exact distance field of the mesh that the window draws it from (a mesh oracle, made with `a` and `b` in its triangles), so no number of the field moves that face, and the kernel's Jacobian, which the surface pull is solved with, does not carry a variable through an oracle (`kernel/src/eval/eval_deriv_array.cpp`, the `ORACLE` case). The walls and the points of such an extrusion are pulled as they always were (by the numbers of the points), and the length can be changed by writing the number into the script. The fix is in the program, not in a script: pull an end face by its own number, as a wall is pulled.

## Simulation

- **The heat flux through a conducting bar depends on the element size, by a few percent.** The check `tests/verification/known_answers/v4_slab_conduction.py` compares the flux of a bar held at two temperatures with Fourier's law: at element sizes of 2.5, 4.0 and 5.0 mm it agrees to 0.1 % or better, at 3.0 and 3.5 mm it is 3.6 % and 3.9 % off (the limit of the check is 2 %, so it stops at those sizes). The tetrahedral mesh of the bar is 0.2 to 0.5 % short of its volume there (9980 and 9947 mm3 of 10000) and its cross-section varies along it. Solve a thermal problem with two element sizes and compare them before you trust a flux on a coarse mesh.
