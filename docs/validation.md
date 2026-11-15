# Validation and accuracy

**FielDes is a design tool, not a certified solver, and it is not validated for safety-critical use.**  This page lists the checks of the solver that anyone can repeat: every line below has a script in the `validation` folder beside the program.
Click the name of a script and it opens in the editor; run it, and it prints the program's number next to the reference and stops with an error when they differ by more than the tolerance written in it.  A claim without a script is not made on this page.
**Compare any result with a hand calculation and, where it matters, a test, before you rely on it.**

- [The standard these checks follow](#the-standard-these-checks-follow)
- [Patch tests: exact answers](#patch-tests-exact-answers)
- [NAFEMS benchmarks](#nafems-benchmarks)
- [Mesh convergence study](#mesh-convergence-study)
- [Closed-form solutions](#closed-form-solutions)
- [Modal analysis](#modal-analysis)
- [Thermal analysis](#thermal-analysis)
- [Thermal stress](#thermal-stress)
- [Fluid flow](#fluid-flow)
- [Topology optimization](#topology-optimization)
- [Parametric optimization](#parametric-optimization)
- [Manufacturing analysis](#manufacturing-analysis)
- [Meshing](#meshing)
- [CAD operations](#cad-operations)
- [Solvers of your own](#solvers-of-your-own)
- [What is not covered](#what-is-not-covered)

## The standard these checks follow

Solvers are compared by a few agreed kinds of check, and this page uses those and no others:

1. **Benchmarks of NAFEMS** (the association that defines the standard problems of linear structural analysis: for example the thick plate LE10).  Its own documents are for members.  The problems themselves, with geometry, supports, loads and the reference value, are published
   with the input of open-source solvers: FeenoX / Fino ([seamplex.com/fino/cases](https://www.seamplex.com/fino/cases/), CC BY 4.0) and the CoFEA initiative ([cofea.readthedocs.io](https://cofea.readthedocs.io/)).  The set-ups here are taken from those pages and say which page.
2. **Patch tests and exact solutions** of the same collections: a problem whose answer is known exactly and that an element must reproduce on any mesh.
3. **A mesh convergence study**, the way the verification guides ask for it: ASME V&V 20 and the grid convergence index of Roache as set out by Celik et al., *Journal of Fluids Engineering* 130 (2008).
4. **Closed-form solutions of the textbooks** (Timoshenko's beam, Peterson's stress concentration, Poiseuille's flow, Fourier's conduction).

Every number below was printed by the script named beside it on 2026-11-15, running as a file in the window of the program (the build date is in the status bar of the window; a script that makes a shape shows it in the viewport).  The numbers of 2026-11-14 were run again after the mesher of the program was reworked, and where they changed the text says so.  A result is only as good as its mesh: each table says which element size or element type it was read with.

## Patch tests: exact answers

A patch test is a problem whose stress is uniform, so that any element that represents a uniform stress must give the exact answer on any mesh.  An element that fails it is wrong.  Both cases are from the Fino case files (CC BY 4.0), non-dimensional (E = 1 MPa, nu = 0.3).

| Case | The exact answer | FielDes, smallest to largest value over a grid of points in the body | Script |
|---|---|---|---|
| A cube of 1 mm under a tensile force of 1 N on one face, the opposite face held in its normal direction and the rigid motions removed at a corner and an edge ([Fino 003](https://www.seamplex.com/fino/cases/003-cube-pure-tension)) | sigma_x = 1, the other five stresses 0 | linear tetrahedra (7,680): sigma_x **0.999999 to 1.000000**, the others **within 1e-6 of 0**; 10-node: sigma_x **0.999993 to 1.000016**, the others within **5e-6** | [fino_003_cube_pure_tension.py](../validation/fino_003_cube_pure_tension.py) |
| A cylinder of radius 0.5 and height 1 mm under a pressure of 1 MPa on its top, the base held in z, a quarter modelled with its two symmetry planes ([Fino 006](https://www.seamplex.com/fino/cases/006-cylinder-pure-compression)) | sigma_z = -1, the other five stresses 0 | linear (2,720): sigma_z **-1.0079 to -0.9983**, the others within **0.0035**; 10-node (2,720): sigma_z **-1.0089 to -0.9956**, the others within **0.0071** | [fino_006_cylinder_pure_compression.py](../validation/fino_006_cylinder_pure_compression.py) |

**What it says.**  The linear and the 10-node tetrahedra pass both.  In the cylinder sigma_z scatters by about 1 % round -1 from one point to the next (0.8 % above at most with linear elements), partly for the reason the Fino page gives: the facets of the curved side make the loaded face a little smaller than the circle, so a force of pressure times the exact area is spread over less
area, and partly because the stress of a linear element is one number for the whole element; both fall as the elements get smaller.  The page's own radial symmetry condition is replaced here by a quarter of the cylinder with symmetry planes (the script says so); the exact solution is the same.

## NAFEMS benchmarks

Structural solvers are compared by the benchmark problems of NAFEMS (for example the thick plate LE10), whose targets are published in the association's documents, which are for its members.  What is public are
the set-ups and targets that the open-source solvers print in their own validation pages (FeenoX / Fino, [seamplex.com/fino/cases](https://www.seamplex.com/fino/cases/), and the CoFEA initiative, [cofea.readthedocs.io](https://cofea.readthedocs.io/)), and those
are the ones this page uses.  Where FielDes stands against the NAFEMS set-ups:

| Benchmark | The target | FielDes |
|---|---|---|
| LE10, thick elliptic plate under pressure: the stress in y at the point D | -5.38 MPa | [nafems_le10_thick_plate.py](../validation/nafems_le10_thick_plate.py), measured 2026-11-15 inside the window at element sizes of 100 / 75 / 60 / 50 mm (44,000 to 328,000 elements, 6 to 12 elements through the 600 mm of the plate): linear **+5.1 / +10.4 / +1.8 / +0.7 %**, 10-node **-5.5 / -4.6 / -5.1 / -5.3 %** |
| LE1, elliptic membrane under a tension on its outer edge (`pressure()`): the stress in y at the point D | 92.7 MPa | [fino_010_nafems_le1_elliptic_membrane.py](../validation/fino_010_nafems_le1_elliptic_membrane.py), measured 2026-11-15 inside the window with 10-node tetrahedra at nine element sizes from 90 to 50 mm (19,000 to 61,000 elements): **-0.5 / -0.6 / -0.5 / -0.6 / -0.6 / -0.4 / -0.2 / -0.5 / -0.4 %** at 90 / 85 / 80 / 75 / 70 / 65 / 60 / 55 / 50 mm; median **0.5 %**, no size more than 1 % off |

**What it says.**  *LE10:* the linear element reads the benchmark's value within 2 % from 60 mm on (ten elements through the plate) and scatters at the coarser sizes (+5.1 and +10.4 % at 100 and 75 mm); the 10-node element reads 4.6 to 5.5 % below it at every size and does not move with the mesh: a steady difference of a stress that is read at a corner of the body from the one element there, where the reference is the value of the converged meshes of 20-node hexahedra of many programs, not an exact solution.  (The hole of the script is a cylinder much longer than the plate, not one that ends in the faces of the plate: a hole that ends exactly in a face puts tetrahedra into the hole on that plane.)
*LE1:* at every one of the nine sizes the stress at D is within 0.6 % of the benchmark, a good result for a stress at a boundary point.  Three sizes (85, 60 and 55 mm) read 14 to 29 % high before the mesher put a node at the corner where the wall of the hole, the plane of symmetry and a face of the plate meet (the corners (2000, 0, 0) and (2000, 0, 100)): the two edges that end there stopped 34 and 38 mm (0.57 and 0.63 of an element) short of it and the faces cut across the corner, which made a bump of half an element at D (121 MPa where 80 mm gave 92).  The corner node is made by the kernel (`captureCorners`; the check in `tests/internal/mesh_edge_checks.py`: the corners at the tip of the plate at a twentieth of its size must have a node within a fifth of an element).  The script prints every size and fails only when the median of the sizes is over 2 % or the finest mesh over 10 %.  At 120 mm the mesh holds a third of the plate (a plate 100 mm thick with elements 120 mm: not resolved) and the sweep starts at 90 mm.

## Mesh convergence study

A mesh convergence study solves one problem on a series of meshes, each finer than the last by the same ratio, and reads what changes.  It is the usual way to put an uncertainty on a number that has no exact answer, and
the one the verification guides of the field ask for (ASME V&V 20; Roache's grid convergence index as set out by Celik et al., *Journal of Fluids Engineering* 130, 2008; the grid convergence tutorial of NASA's Glenn Research Center).  From the
three finest meshes it gives the **observed order of convergence** p, the value the mesh would give at zero element size (Richardson extrapolation) and the **grid convergence index** (GCI), the band that the mesh puts round the finest value.
A quantity that does not converge has no such order; the study says so instead of printing one.

The script [mesh_convergence_study.py](../validation/mesh_convergence_study.py) runs it on a problem with a singularity: an L-shaped plate 40 x 40 mm with arms 20 mm wide and 2 mm thick, held across the end of one arm and pulled across the end
of the other, between two planes that keep it from moving through its thickness (plane strain), in steel.  Its re-entrant corner has an opening of 270 degrees, where the stress is singular and rises as r^-0.4555 (Williams).  The element size falls by 1.4 each time,
2.0 / 1.43 / 1.02 / 0.73 / 0.52 mm (8,800 to 233,000 elements).  Measured 2026-11-15, linear and 10-node tetrahedra (`ELEMENT = 'tet10'` in the script):

| Element size mm | Compliance (linear / 10-node) | Largest movement mm | Stress 1.5 mm from the corner MPa | Stress 3 mm from the corner MPa | Stress 0.05 mm from the corner MPa |
|---|---|---|---|---|---|
| 2.00 | 106.4 / 109.5 | 0.1382 / 0.1420 | 102 / 110 | 71.4 / 80.8 | 144 / 373 |
| 1.43 | 108.0 / 109.7 | 0.1401 / 0.1422 | 114 / 104 | 71.8 / 79.3 | 175 / 442 |
| 1.02 | 108.7 / 109.7 | 0.1410 / 0.1423 | 119 / 104 | 73.9 / 79.3 | 167 / 542 |
| 0.73 | 109.1 / 109.8 | 0.1415 / 0.1424 | 111 / 104 | 76.3 / 79.5 | 228 / 581 |
| 0.52 | 109.4 / 109.9 | 0.1419 / 0.1425 | 106 / 104 | 77.5 / 79.4 | 282 / 745 |

| Quantity (the three finest meshes) | Linear: order p, value at zero size, GCI of the finest | 10-node: order p, value at zero size, GCI of the finest |
|---|---|---|
| Compliance (twice the strain energy) | 1.41, 109.88, **0.53 %** | 1.77, 109.93, **0.07 %** |
| Largest movement | 1.36, 0.14250 mm, **0.53 %** | 1.63, 0.14255 mm, **0.07 %** |
| Stress 1.5 mm from the corner | 1.40, 98.5 MPa, **9.2 %** | no order: the series is flat, 104.4, 104.2, 104.0 and 104.0 MPa |
| Stress 3 mm from the corner | 2.05, 78.7 MPa, **2.0 %** | no order: the series is flat, 79.3, 79.5 and 79.4 MPa |
| Stress 0.05 mm from the corner (the peak the mesh can show) | **does not converge**: grows like h^-0.62 | **does not converge**: grows like h^-0.74 |

**What it says.**
- The compliance and the movements converge in order (p 1.4 to 1.8) and **monotonically over all five sizes with both elements**, to a GCI of 0.5 % (linear) and 0.07 % (10-node); the two element types extrapolate to the same values within 0.05 % (109.88 and 109.93; 0.14250 and 0.14255 mm).
- The stress at a **fixed distance** from the singularity converges, and the 10-node element converges it far better: from 1.4 mm on it changes by 0.2 % or less with each refinement (104.4, 104.2, 104.0, 104.0 MPa at 1.5 mm), too flat for the study to read an order from it, which is why it prints "does not converge in the order sense" for them and the table prints the numbers;
  the linear element falls monotonically over the three finest meshes (119, 111, 106 MPa at 1.5 mm) and its finest value is still 2 % above the 10-node one, a GCI of 9 %.  **Use `element='tet10'` where a stress matters.**
- The **peak stress at the corner does not converge**, with either element: it is the stress of the smallest element there and rises with every refinement, as the theory for a 270-degree corner says it must (h^-0.46 where the elements lie in the singular field; the linear elements follow it, h^-0.62 over the series; the 10-node
  exponent, -0.74, is steeper because its mid-edge nodes at the corner read the singularity more sharply).  A real part has a radius there: model it, or read the stress at a distance from the corner, never at the corner itself.
- On 2026-11-14 the series of the compliance and of the stress at 1.5 mm was **not monotone** (at 1.02 mm both element types overshot, the compliance by 1 % and the stress by 10 % and 5 %), and the orders then read from the three finest meshes were 2.96 (compliance) and 1.95 (stress at 1.5 mm).  With the mesher of 2026-11-15 the compliance and the movement are monotone for both elements and the linear stress at 1.5 mm
  still overshoots at 1.02 mm (119 MPa) before it falls: the mesh is a grid of cut cells laid over the part, each element size lays its lattice differently against the corner, and the table lists the sizes themselves so that the scatter between neighbouring sizes can be read as the honest error bar of a single mesh.

## Closed-form solutions

| Check | The exact answer | FielDes | Script |
|---|---|---|---|
| A steel bar 100 x 10 x 10 mm held at one end, 100 N across the other: the movement of the tip | Timoshenko beam theory, 0.2016 mm | linear tetrahedra (72,192): 0.1975 mm, **-2.0 %**; 10-node (1,312): 0.1999 mm, **-0.8 %**; tolerance 5 % and 2 % | [v1_cantilever_beam.py](../validation/v1_cantilever_beam.py) |
| A plate 80 x 40 x 3 mm with a hole of 8 mm pulled by 100 MPa (one eighth, by symmetry): the stress concentration | 3.14 (Peterson, for the width of the plate) | linear (1,013,364): 3.15, **+0.4 %**; 10-node (66,355): 3.12, **-0.6 %**; tolerance 6 % and 3 % | [v2_plate_with_hole.py](../validation/v2_plate_with_hole.py) |
| Water between two plates 10 mm apart, mean speed 5 mm/s: the speed in the middle and the pressure gradient | 1.5 times the mean speed, 12 mu U / H^2 | 24,000 tetrahedra: speed **+0.0 %**, gradient **+0.0 %**; tolerance 3 % and 6 % | [v3_channel_flow.py](../validation/v3_channel_flow.py) |
| An aluminium bar held at 100 C and 20 C at its ends, the sides insulated: the temperature along it and the heat flux | a straight line, Fourier's flux | 7,744 tetrahedra: **0.00 K** off the line, flux **0.0 %** off; tolerance 0.3 K and 2 % | [v4_slab_conduction.py](../validation/v4_slab_conduction.py) |

**What it says.**  Displacements and the compliance are accurate to a few percent at the usual element sizes with linear tetrahedra and to about 1 % with 10-node ones; a stress at a notch, a corner or a fillet is the number to be most careful with (see the convergence study).

## Modal analysis

The case is the one of an open-source program, taken with its numbers from its page: **"Five natural modes of a cantilevered wire"** of the annotated examples of FeenoX ([seamplex.com/feenox/examples/modal.html](https://seamplex.com/feenox/examples/modal.html#five-natural-modes-of-a-cantilevered-wire)).
An aluminium wire 151.5 mm long and 1.948 mm in diameter (Young's modulus 80.4 GPa, Poisson's ratio 0, the density that gives its measured mass) is held at one end; the page compares its finite-element frequencies with those of Euler and Bernoulli's beam theory.
The script [feenox_cantilevered_wire_modes.py](../validation/feenox_cantilevered_wire_modes.py) finds the roots of `cos(x) cosh(x) = -1` itself, computes the theory (it differs from the page's column by at most 0.04 %), and solves the same wire with 10-node tetrahedra of 0.8 mm (two and a half across the diameter).  Measured 2026-11-15 (the page's own finite-element column, on a mesh of hexahedra, reads 61.91, 387.8, 1,085, 2,124 and 3,506 Hz):

| Mode (each comes twice) | Euler-Bernoulli | FielDes `modal_analysis`, 0.8 mm | Difference |
|---|---|---|---|
| 1 | 61.92 Hz | 61.91 Hz | **-0.01 %** |
| 2 | 388.02 Hz | 387.79 Hz | **-0.06 %** |
| 3 | 1,086.47 Hz | 1,084.99 Hz | **-0.14 %** |
| 4 | 2,129.04 Hz | 2,123.73 Hz | **-0.25 %** |
| 5 | 3,519.46 Hz | 3,505.54 Hz | **-0.40 %** |

**What it says.**  At 0.8 mm the five frequencies are within 0.4 % of the theory and of the page's finite elements.  **The size still matters for a wire this thin**: with 1.0 mm elements (two across the diameter) the mesh holds 441.8 of 451.5 mm^3 (-2.2 %: round a wire two elements across the section is a polygon of a few nodes inscribed in the circle) and the first frequency reads 61.92 Hz against the theory's 61.92 Hz (-0.00 %); at 0.8 mm the volume is 1.6 % short and the first frequency 61.91 Hz (-0.01 %).  The mesher takes a vertex for a crease only where the surface turns sharply, and not where it only bends round a thin part, so the nodes of a thin round part are not pulled in towards its axis.  A part two elements thin is still not resolved well: **halve the size and see whether the answer moves**.  No damping and no nonlinear dynamics.

## Thermal analysis

Three checks, each with an exact answer; the straight line of the bar between two temperatures is in the table of the closed-form solutions above ([v4_slab_conduction.py](../validation/v4_slab_conduction.py), exact to 0.00 K).
The one-dimensional slab with fixed temperatures is also the first case of the thermal examples of FeenoX ([seamplex.com/feenox/examples/thermal.html](https://seamplex.com/feenox/examples/thermal.html#one-dimensional-linear)).  The two below derive their exact answers in the header of the script (no outside source is claimed for them).

| Check | The exact answer | FielDes | Script |
|---|---|---|---|
| An aluminium bar 100 x 10 x 10 mm held at 20 C at both ends with 20 W generated through its volume, the sides insulated: the temperature along it | a parabola, 35.0 C in the middle | 7,744 tetrahedra: 31.21 / 34.97 / 31.21 C at 25 / 50 / 75 mm against 31.23 / 34.97 / 31.23 C, **0.1 %** of the rise of the middle; tolerance 2 % | [v6_slab_heat_generation.py](../validation/v6_slab_heat_generation.py) |
| The same bar held at 100 C at one end and cooled at the other by a fluid at 20 C (h = 0.002 W/(mm^2 K)): the temperature and the heat flux | a straight line, 56.4 C at the cooled end, 0.0728 W/mm^2 | **0.00 K** off the line, flux **0.0 %** off; tolerance 0.5 K and 3 % | [v7_slab_convection.py](../validation/v7_slab_convection.py) |

**What it says.**  Steady conduction with fixed temperatures, heat generation and convection is exact for what linear elements can represent (a straight line) and a hair off for a parabola.  No radiation, no transient, no temperature-dependent properties in the built-in solver (a solver of your own can do them: see [Solvers](solvers.md)).

## Thermal stress

A steel bar 100 x 10 x 10 mm heated evenly by 60 K, solved with `thermal_expansion` ([v8_thermal_expansion.py](../validation/v8_thermal_expansion.py), 10,368 tetrahedra for the held bar, 2026-11-15; the exact answers are derived in the script's header):

| Case | The exact answer | FielDes |
|---|---|---|
| Held at one end only: the movement of the tip | alpha dT L = 0.0720 mm | 0.0729 mm, **+1.3 %** (the held end also keeps the end of the bar from growing sideways); tolerance 2 % |
| The same bar: the stress | 0 | **0.017 MPa** at three places; tolerance 1 MPa |
| Held at both ends: the stress in the middle | E alpha dT = 144.0 MPa | 148.7 MPa, **+3.3 %** (the middle of a bar ten times as long as it is deep is not far enough from the ends for the exact value); tolerance 5 % |

## Fluid flow

The flow solver is laminar and incompressible (linear tetrahedra with stabilisation).  Three standard problems cover the three ways a flow is used: **inside a channel** (Poiseuille's exact solution, in the table of the closed-form solutions: [v3_channel_flow.py](../validation/v3_channel_flow.py)), **around a body**, which is what the program is built for (the cylinder of the DFG benchmark, next), and **in a closed cavity** (below).

**The flow around a body: the cylinder of the DFG benchmark 2D-1** (Schaefer and Turek, 1996; steady, laminar, Reynolds number 20): a circular cylinder 0.1 across, not quite in the middle of a channel 2.2 long and 0.41 wide, a parabolic inflow, no-slip walls and a "do-nothing" outlet.  The set-up and the reference values, computed with spectral methods (Nabh, 1998),
are on the page of the CFD benchmarking project of the Featflow group of TU Dortmund ([the page](https://wwwold.mathematik.tu-dortmund.de/~featflow/en/benchmarks/cfdbenchmarking/flow/dfg_benchmark1_re20.html)).  Every length is scaled by 0.1 here (the cylinder is 10 mm, the channel 220 x 41 mm) and the fluid is water, so the mean speed that gives Re = 20 is 2 mm/s; the coefficients are dimensionless and compared as such.
The flow is two-dimensional (a slab two elements thick with a frictionless face on each side), the inflow is `profile='developed'` (checked: parabolic to 0.5 % across the channel and the same across the slab), and the force is read from a wall item that is the cylinder's own surface.  The script [dfg_2d1_cylinder_in_a_channel.py](../validation/dfg_2d1_cylinder_in_a_channel.py)
solves it at four element sizes, each three quarters of the one before.  Measured 2026-11-15:

| Element size mm | Elements | Drag coefficient C_D (reference 5.5795) | Pressure difference p_diff (reference 0.11752) | Lift coefficient C_L (reference 0.01062) | Momentum balance |
|---|---|---|---|---|---|
| 1.78 | 80,325 | 6.067, **+8.7 %** | 0.1118, **-4.9 %** | -0.046 | 1.021 |
| 1.33 | 143,546 | 5.833, **+4.5 %** | 0.1131, **-3.8 %** | +0.012 | 1.015 |
| 1.00 | 252,386 | 5.740, **+2.9 %** | 0.1144, **-2.7 %** | +0.005 | 1.011 |
| 0.75 | 449,972 | 5.675, **+1.7 %** | 0.1149, **-2.2 %** | +0.019 | 1.007 |

**What it says.**  The drag converges to the benchmark's value from above and monotonically over the four sizes (+8.7, +4.5, +2.9, +1.7 %), with an observed order of 1.18 (read from the three finest meshes), a value at zero element size of 5.51 (1.2 % below the reference) and a grid convergence index of 3.6 % at the finest mesh, a band that contains the reference.
The pressure difference between the two points on the axis in front of and behind the cylinder is 2.2 % low at the finest mesh and converges too.  The lift is a number near zero (the reference is 0.0106) that the mesh scatters by a few hundredths from one size to the next (-0.046 to +0.019), and is read as a noise level, not as a converging series.
The momentum balance (the pressure drop over the channel times its section, against the force on all its walls) reads 1.021 to 1.007: the force the program reports is the force the flow needs.  In the runs that chose the sizes 2.0, 1.5 and 0.6 mm (not in the script) the drag read +10.4, +5.9 and +2.3 %: the 0.6 mm mesh is a little farther than the 0.75 mm one, the place of the lattice against the cylinder scatters the result by a few tenths of a percent.
*A fault of mine that this check caught:* the first version of the script took a wall region a little bigger than the cylinder; a wall region takes every triangle of the fluid's surface inside it, the frictionless faces round the cylinder too, and the drag read 16 to 24 % too high and rose as the mesh got finer.  The script cuts a thin shell now, and `docs/analysis.md` says so beside `wall`.

A second standard problem is the **lid-driven square cavity** of Ghia, Ghia and Shin (1982), set up in the verification manual of the open-source program MFiX ("FLD03", [mfix.netl.doe.gov/doc/vvuq-manual/main/html/fluid/fld-03.html](https://mfix.netl.doe.gov/doc/vvuq-manual/main/html/fluid/fld-03.html)).
The script [mfix_fld03_lid_driven_cavity.py](../validation/mfix_fld03_lid_driven_cavity.py) solves it at a Reynolds number of 100 and compares the speed along the two centre lines at the 17 points of Ghia's table, as it is printed on a page that transcribes it
([sites.google.com/view/sidrg/...](https://sites.google.com/view/sidrg/research-projects/mechanical-engineering/lid-driven-cavity-flow); the original paper was not opened for this script).  Measured 2026-11-15 with 65,408 linear tetrahedra (28 across the cavity):

| Quantity | Ghia et al. | FielDes |
|---|---|---|
| u / (lid speed) at the middle of the cavity, the lowest value on x = 0.5 (at y = 0.45) | -0.2109 | -0.1841, **-12.7 %** |
| v / (lid speed), the highest value on y = 0.5 (at x = 0.23) | 0.1753 | 0.1545, **-11.8 %** |
| v / (lid speed), the lowest value on y = 0.5 (at x = 0.80) | -0.2453 | -0.2194, **-10.6 %** |
| The worst difference over the 34 points | | **0.0335** of the lid's speed; tolerance 0.04 |

**What it says.**  The flow has the right shape (the vortex and its two centre-line profiles) and the peaks come out 10 to 13 % low at 28 cells across: linear elements with stabilisation spread the peaks, and the error falls as the elements get smaller (an earlier measurement at 40 and 64 cells read the peaks -8 % and -4 %).
Turbulence, compressibility, free surfaces, heat carried by the flow and boundary-layer elements are not offered and not validated.

## Topology optimization

An optimizer has a standard that can be checked from outside only where the best design is known.  For a bar that is pulled, and for a bar that conducts heat, it is: through every cross-section the whole force (or the whole heat flow) passes, so for a given volume the stiffest
(or coolest) layout has the same section all along the bar (Jensen's inequality; the two derivations are in the headers of the scripts, no outside source is claimed for them).  **No layout of that volume can beat it**, and an optimizer that does has an error.  The layout is not unique (every layout with the same section along the bar is as good), so the
scripts compare the number the layout reaches and not its shape.  The bound used is the compliance (or the temperature rise) of the **whole bar as the same model computes it**, divided by the share of the bar the cut part really keeps; the textbook formula is printed beside it.  Measured 2026-11-15, 2 mm elements:

| Check | The best a layout of that volume can do | FielDes | Script |
|---|---|---|---|
| An aluminium bar 40 x 10 x 10 mm held across one end and pulled along its length by 1,000 N, the stiffest layout of 50 % of it (`topology_optimization`, 40 iterations) | the whole bar's compliance over the share kept, 11.65 N mm (the formula P^2 L / (E A f) gives 12.03) | the whole bar: 5.613 N mm against the formula's 5.797 (**-3.2 %**: the support holds the whole end face, so the end cannot draw in sideways; tolerance 5 %); the part keeps **49.5 %** and has 12.90 N mm, **+13.7 %** above the bound; tolerance -3 % (it cannot be stiffer) and +30 % | [v13_topology_optimization_bar_in_tension.py](../validation/v13_topology_optimization_bar_in_tension.py) |
| The same bar held at 20 C across one end with 10 W entering across the other, the coolest layout of 50 % of it (80 iterations) | the whole bar's rise over the share kept, 48.52 K (the formula Q L / (k A f) gives 49.91 K) | the whole bar: a rise of 23.29 K against the formula's 23.95 K (**-2.8 %**; tolerance 5 %); the part keeps **48.1 %** and has a rise of 52.16 K, **+7.7 %** above the bound; tolerance -3 % and +30 % | [v15_thermal_topology_optimization_bar.py](../validation/v15_thermal_topology_optimization_bar.py) |

**What it says.**  On the two problems whose best answer is known, the optimizer lands within 15 % (stiffness) and 8 % (heat) of the best layout that exists for the volume it keeps, and never beats it.  The share kept is a little under the 50 % asked for (48 %): the part is cut from the density at the level that gives the design's own volume and the
mesh counts what is left.  The thermal optimizer needs more iterations than its default of 20 on this problem: a run of 40 left the bar in two pieces (the output says so and the part has no analysis), 80 gave one piece.

**The flow topology optimization, in creeping flow.**  No value of the lowest drag is known for a body in a channel, but something about the best body is: in Stokes flow the equations are reversible, so in a domain that is a mirror image of itself, with the place the body may be in a mirror image of itself, the best body is the same in front and behind.
Pironneau (*J. Fluid Mech.* 59, 1973, "On optimum profiles in Stokes flow") showed that the body of the lowest drag for a given volume in Stokes flow is front-back symmetric, with pointed ends (cones of 120 degrees included angle for a body of revolution; read in the abstract of Montenegro-Johnson and Lauga, arXiv 1411.4783).
The script [pironneau_stokes_drag_minimal_body.py](../validation/pironneau_stokes_drag_minimal_body.py) runs the slab of the flow example (`stokes=True`: 80 x 40 x 4 mm of water, a post of 10 mm that may be reshaped inside a box of 44 x 24 mm) with the post and the box in the middle of the slab and the optimizer told not to keep the design symmetric (`symmetry=None`:
its default would impose the symmetry across the flow), minimizes the drag at the post's volume, and measures the final body in its middle plane.  Measured 2026-11-15, 1.5 mm elements:

| Check | Required | FielDes |
|---|---|---|
| The final body is the same in front and behind (overlap with its own mirror image, front to back, over the union) | at least 0.95 | **0.988** (up to down 0.939) |
| It is longer than it is wide | at least 1.4 | **1.80**: 13.5 mm long, 7.5 mm wide (the post is 10 x 10) |
| It keeps the post's volume | within 3 % | **79.5 mm^2** in the plane against the post's 78.5 (+1.3 %) |
| The force of the real flow around it (the body a wall) is lower than the post's | lower | **2.105e-7 N** against **2.515e-7 N** (**-16.3 %**; the same problem on earlier meshes read -4.9 % and +2.0 %) |

**What it says.**  An optimizer that was wrong would not give a body that is the same in front and behind in a flow where nothing tells the front from the back but the design; this one does (0.988), and the lens it finds is longer than wide, as Pironneau's body is.  **The value of the gain is not checked** (the 120 degree tips are the result for a body of revolution in unbounded flow, not for this slab), and the numbers say two things
that are worth knowing: the optimizer's own model of the drag (the body a friction in the flow) reads **-78 %** over its 20 iterations where the real flow around the final body reads **-16.3 %** (on earlier meshes of the same problem -4.9 % and +2.0 %: the size of the real change depends on the mesh, which is the first thing to know: the VALUE of the gain of this optimizer is NOT established by this case; only the shape is, front to back 0.988), and the model is the porous-friction model that the optimization is made on: take the change it reports as the direction of the design, not as the gain, and read the gain from `design.flow`,
and the optimizer ended after 20 iterations (its limit is 80; it stops when the design stops improving).  The flow optimization **with inertia** (the example's Reynolds number of 40, where the best body is not symmetric) has no such known property to check and is **not covered**.

## Parametric optimization

The best design of a cantilever is known from beam theory, so a parametric optimization can be checked against it.  The beam is aluminium, 100 mm long, held at one end and pulled down by 200 N at the other; its width and height are `var()` numbers (the numbers you can drag), the search is allowed width 8 to 30 and height 6 to 24 mm and starts from 20 x 12 mm, a design that is too heavy; the lightest beam that stays under 90 MPa
and bends less than 0.5 mm at its end.  Measured 2026-11-15, 4 mm elements, every design a real analysis:

| Check | The exact answer | FielDes | Script |
|---|---|---|---|
| The lightest beam that bends less than 0.5 mm and stays under 90 MPa | as narrow as the range allows (8 mm) and just deep enough: the end bends by 4 P L^3 / (E b h^3), so h = 14.26 mm and the mass is 0.03080 kg (the stress there, 74 MPa, does not decide) | width **8.00**, height **13.97 mm** (**-2.0 %**), mass **0.03017 kg** (**-2.0 %**), 20 designs tried, every limit kept; tolerance 4 % | [v11_parametric_optimization_beam.py](../validation/v11_parametric_optimization_beam.py) |

**What it says.**  The optimizer finds the width at its bound and the height within 1.5 % of the beam theory, the same few percent by which a mesh of tetrahedra of this size is stiffer than the theory in the cantilever of the closed-form table (a stiffer beam may be a little shallower).  The script uses `var()`, so it runs in the FielDes window (click its name, or File > Open), not in a plain Python;
the output is the five lines it prints and `OK`.  Parametric optimization of a modal, a thermal or a flow analysis is not covered.

## Manufacturing analysis

`manufacturing_analysis` reads a part's own field and says where a rule is broken.  Two of its rules have an exact answer that follows from the part's dimensions alone, and the script checks the number the program reports (its field, the colour the window shows) against it.  A wall counts as broken when it is more than 5 % short of the limit (what a
field can say of a size is not exact), so a rule of 0.8 mm marks a plate under 0.76 mm, and how much it is short by is 0.8 - t - 0.04 mm; a draft of 1 degree marks a wall under 0.95 degrees, short by 1 - a - 0.05 degrees.  Measured 2026-11-15:

| Case | The exact answer | FielDes | Script |
|---|---|---|---|
| Free plates 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick against `min_wall=0.8` | the first three marked, short by 0.460, 0.060 and 0.020 mm; the others not marked | the first three marked, short by **0.460, 0.060 and 0.020 mm** (the same at the top and the bottom face); the others not marked (-0.020, -0.090 and -0.120) | [v14_manufacturing_wall_and_draft.py](../validation/v14_manufacturing_wall_and_draft.py) |
| Walls leaning 0, 0.5, 0.96, 1.2 and 3 degrees off the pull direction against `draft=1.0` | the first two marked, short by 0.950 and 0.450 degrees; the others not marked | the first two marked, short by **0.950 and 0.450 degrees** (both faces); the others not marked (-0.010, -0.250 and -2.050) | the same script |

**What it says.**  The arithmetic of these two rules is exact to the printed digit.  It is a check of the program's number on a plate and a wall, **not of a manufacturing process**: the other rules (overhang, bridging, undercuts, radii, tool reach) have the pictures of `tests/verification/manufacturing` but no exact answer that a script can compare, so no claim is made for them here.
As the report itself says, it is not a substitute for the manufacturer's own check.

## Meshing

A mesh is the body cut into cells, so the cells must add up to the body.  The script [v12_meshes_volume_and_area.py](../validation/v12_meshes_volume_and_area.py) meshes three bodies whose volume and surface follow from their dimensions (the formulas are written beside the numbers in the script) with every function of the Mesh menu and compares the volume the cells add up
to and the area of the boundary, and counts the cells that are inside out.  The tolerances say what kind of mesh it is: tetrahedra that follow the surface 2 % of the volume, a grid cut by the surface 3 % (and 4 % on the area), the hexahedra of `mesh_hex_quanta` and `mesh_hex20_quanta` follow the surface by a staircase of whole cells (10 % on the volume, no check of the area).
Measured 2026-11-15, element size 2 mm (1 mm for the surface meshes and the turned cylinder); no cell is inside out in any of them. The turned cylinder (radius 4, length 14, turned by 30 degrees about x) has its edges off the grid of the cut cells, which is what the nodes put on the edges are for:

| Mesh | Plate 40 x 20 x 6 with a hole of radius 4: volume (exact 4,498.41 mm^3), area (exact 2,370.27 mm^2) | Ball of radius 10: volume (exact 4,188.79 mm^3), area (exact 1,256.64 mm^2) | Cylinder 4 x 14 turned by 30 degrees: volume (exact 703.72 mm^3), area (exact 452.39 mm^2) |
|---|---|---|---|
| `mesh_tet4` (7,856, 6,768 and 8,985 cells), `mesh_tet10` | **+0.14 %**, **+0.08 %** | **-1.01 %**, **-0.54 %** | **-0.62 %**, area **-0.32 %** |
| `mesh_hex_quanta`, `mesh_hex20_quanta` (564 and 504 cells) | **+0.30 %** | **-3.74 %** | |
| `mesh_cartesian` (564, 598 and 780 cells, join threshold 4) | **+0.30 %**, area **+0.12 %** | **-2.16 %** (-0.56 % before the nodes of the skin were put on the surface: the nodes that the snap had left up to a tenth of a cell outside it made up for part of what the flat faces cut off), area **-1.09 %** | **-1.64 %**, area **-2.24 %** (-1.35 % and -1.62 % with the join threshold 10) |
| `mesh_hex_extrusion` (14,004 cells; refuses a ball) | **+0.18 %** | | |
| `mesh_surface_tri`, `mesh_surface_quad` (9,168 triangles or 27,504 quadrilaterals; 3,716 or 11,148) | area **+0.02 %** | area **-0.17 %** | |

**What it says.**  The cells add up to the body: to a fraction of a percent on the plate, and to 1 % (tetrahedra), 2 % (cut cells) and 4 % (a staircase of whole cubes) on the ball, whose curved surface the flat faces of a mesh of 2 mm always cut a little small.  This checks the volume and the area; it does **not** say that the cells are the right shape for a solver:
the quality of the elements (scaled Jacobian, skewness) is in the report of every mesh (`mesh.quality().report()`, see [Meshing](meshing.md)), and a result is only as good as its mesh.

## CAD operations

The volume or the area of a shape made with the program's own operations (a box, a ball, a cylinder, a hole cut in a plate, a turned and moved body, two balls joined) against the exact value that follows from its dimensions;
the formula is written beside each case in the script [v9_cad_volumes_and_areas.py](../validation/v9_cad_volumes_and_areas.py).  Measured 2026-11-15 (volumes by a grid of samples, about 100 along the longest side; areas by a mesh of triangles on the surface):

| Shape | Exact | FielDes | Difference |
|---|---|---|---|
| Box 20 x 10 x 6 mm, volume | 1,200.00 mm^3 | 1,200.00 mm^3 | **0.00 %** |
| Ball of radius 10 mm, volume | 4,188.79 mm^3 | 4,191.87 mm^3 | **+0.07 %** |
| Cylinder of radius 5 and height 20, volume | 1,570.80 mm^3 | 1,580.80 mm^3 | **+0.64 %** |
| Plate 40 x 20 x 6 with a hole of radius 4, volume | 4,498.41 mm^3 | 4,496.64 mm^3 | **-0.04 %** |
| The box turned 37 degrees and moved, volume | 1,200.00 mm^3 | 1,158.78 mm^3 | **-3.44 %** (the slanted edges are cut by up to half a sample; tolerance 4 %) |
| Two balls of radius 10 with their centres 10 apart, volume | 7,068.58 mm^3 | 7,069.66 mm^3 | **+0.02 %** |
| Box 20 x 10 x 6 mm, surface area | 760.00 mm^2 | 760.00 mm^2 | **0.00 %** |
| Ball of radius 10 mm, surface area (mesh of 0.6 mm) | 1,256.64 mm^2 | 1,256.96 mm^2 | **+0.03 %** |

**What it says.**  Volumes from grid sampling are right to a fraction of a percent where the surfaces lie along the grid or are curved, and a slanted flat face costs up to half a sample at its edges: take a finer `resolution` where it matters.

## Solvers of your own

`solve('heat', ...)` and `solve('elasticity', ...)` (see [Solvers](solvers.md)) against exact answers, [v10_solvers_of_your_own.py](../validation/v10_solvers_of_your_own.py), measured 2026-11-15 on a bar 40 x 10 x 10 mm with elements of 5 mm:

| Check | The exact answer | FielDes |
|---|---|---|
| Heat, a bar held at 20 C and 80 C, constant conductivity | a straight line | largest difference of a node **2.7e-6 K** (185 nodes) |
| Heat, a conductivity that grows with the temperature (k = k0 (1 + 0.01 T)), held at 0 C and 100 C | the line of Kirchhoff's transform (derived in the script) | largest difference **0.115 K** (the straight line would be 8.1 K off in the middle) |
| Elasticity, a bar pulled to a strain of 1 % | the stress is E times the strain, in every node: 1,000 MPa | **1,000.00 MPa** in every node; the sides draw in by 0.03000 mm, as Poisson's ratio x strain x width gives |

**What it says.**  A solver that you write is only as good as its equations and its checks: these three are the way to test yours.

## What is not covered

Everything not listed above has no published check, and no claim is made for it:

- **Flow topology optimization**: only in creeping flow, and only what is known of the best body there (it is the same in front and behind); the value of the gain is not checked, and with inertia nothing is.  Of the **manufacturing rules** only the wall and the draft have an exact answer there (overhang, bridging, undercuts, radii and tool reach do not).  Of **meshing** only the volume and the area the cells add up to are checked, not whether the cells are the right shape for a solver (see the quality report of each mesh).
  The **mesh of a part with thin features** is validated only by the wire of the Modal analysis section (a round wire two elements across holds 2.2 % too little of its volume at 1 mm elements, its first mode exact) and by the corner node of the LE1 plate (see NAFEMS benchmarks); other thin parts are not covered.
- Fluid flow beyond the channel, the cylinder and the cavity above (turbulence, compressibility, free surfaces, heat carried by a flow; a flow that sheds vortices, which the program solves in time, has no check here; a sphere or any three-dimensional body has none either).
- The **export** of a mesh or an analysis to another program (the files are written; a solver of another program has not been run on them in this validation).
- Contact, plasticity, buckling, large strains, dynamics beyond natural frequencies (not offered).
- Anything on a mesh that has not been refined once to see whether the answer moves.
- Fatigue and anything where the peak stress at a notch decides: the built-in stress at a corner is the mesh's.
- The safety of a part: **FielDes is not validated for safety-critical use.**
