# Validation and accuracy

**FielDes is a design tool, not a certified solver, and it is not validated for safety-critical use.** This page is every check that has been made of every simulation and every analysis-like
tool in it, what it was checked against, how far off it came out, and what the check does *not* say.  Every number here was measured on this program's own problems on the date given;
the scripts that measure them are named, so that they can be run again after a change (`python scripts/run_example.py <script>`).  A result is only as good as its mesh: in every
table the error is read with the element size it was measured at, and falls as the elements get smaller.  **Compare any result with a hand calculation and, where it matters, a test, before
you rely on it.**

- [Static analysis](#static-analysis-linear-elasticity)
- [Modal analysis](#modal-analysis)
- [Thermal analysis](#thermal-analysis)
- [Thermal stress](#thermal-stress)
- [Fluid flow](#fluid-flow)
- [Topology optimization](#topology-optimization)
- [Flow topology optimization](#flow-topology-optimization)
- [Thermal topology optimization](#thermal-topology-optimization)
- [Parametric optimization](#parametric-optimization)
- [Manufacturing analysis](#manufacturing-analysis)
- [Meshing for another program](#meshing-for-another-program)
- [CAD operations and snapping](#cad-operations-and-snapping)
- [Solvers of your own](#solvers-of-your-own)
- [What is not validated](#what-is-not-validated)

## Static analysis (linear elasticity)

The solver: linear tetrahedra (or 10-node tetrahedra with `element='tet10'`), conjugate gradients.  Checked against the exact answers of problems that have one:

| Check | The exact answer | FielDes | Date | Script |
|---|---|---|---|---|
| A steel bar 100 x 10 x 10 mm held at one end, 100 N across the other: tip movement | Timoshenko beam theory, 0.2016 mm | 0.1975 mm, **-2.0 %** (67,000 tetrahedra, 1 s); 10-node **-0.8 %** (1,300 elements, 0.1 s); tolerance 5 % and 2 % | 2026-10-14 | `tests/verification/known_answers/v1_cantilever_beam.py` |
| A plate 80 x 40 x 3 mm with a hole of 8 mm pulled by 100 MPa (one eighth, by symmetry): the stress concentration | 3.14 (Peterson, for the width of the plate) | 3.02, **-3.7 %** (960,000 tetrahedra, 18 s); 10-node **+0.4 %** (45,000 elements, 13 s); tolerance 6 % and 3 % | 2026-10-14 | `v2_plate_with_hole.py` |
| The same beam, how the error falls with the elements across its depth | the same | **-5.6 %** with 4, **-1.0 %** with 16 | 2026-10-14 | |
| The same plate, how the error falls with the element | the same | **-9 %** with 1 mm, **-8 %** with 0.5 mm, **-4 %** with 0.25 mm | 2026-10-14 | |
| A round bar bent by a tip load (a curved surface) | beam theory | **-1.0 %** with 469,000 linear elements (12 s); **+0.1 %** with 4,456 10-node elements (1.8 s) | 2026-10-14 | |
| The plate with a hole with 10-node elements of 1 mm (four to the hole's radius) | Peterson | **+20 %** (too high): a mesh too coarse for the feature is not saved by the extra nodes; 0.7 mm and 0.5 mm read it right | 2026-10-14 | |
| A patch of uniform stress, a bar hanging by its weight, a bar held between its ends and heated | exact (linear elements represent them) | reproduced exactly | | the checks of the element |
| A cantilever whose stiffness falls to a tenth along its length, loaded evenly or in proportion to x (graded material) | the beam-theory integrals: tip 0.375 and 0.55 of the load | reproduced | | `examples/19_graded_material.py` |
| The bracket of example 05 (445,000 tetrahedra of 2 mm, a pull of 2 kN): the peak von Mises stress | none: a corner's peak is the mesh's | **41, 58, 62, 67, 67 MPa** with the lattice of the mesh at five places; the largest displacement 0.0498 to 0.0501 mm (**0.6 %**) | 2026-10-24 | `examples/05_static_analysis.py` |

**What it says.** Displacements and the compliance are accurate to a few percent at the usual element sizes; the stress at a notch, a corner or a fillet is the number to be most careful with: with
linear elements it comes out **low** and moves by tens of percent with where the mesh lattice falls (the safety factor of example 05 reads between 1.4 and 1.6).  **Halve the element size and see
whether the peak moves** (or use `element='tet10'`).  A stress singularity (a sharp re-entrant corner, a point load) does not converge with refinement.

## Modal analysis

| Check | The reference | FielDes | Date |
|---|---|---|---|
| The first bending mode of the beam of the check above | converged: about **812 Hz** | linear: **818 Hz** (66,816 elements, 23 s); 10-node: **813 Hz** (1,296 elements, 3.6 s) | 2026-10-14 |
| The first natural frequency of a bar of a given mass, maximised over its section (26 designs, 14 s) | the square section, **12.17 mm** by beam theory | **12.2 x 12.2 mm** | 2026-10-15 |

**What it says.** Frequencies of bending modes come out within about 1 % of the converged value at the usual sizes.  No damping, no nonlinear dynamics.

## Thermal analysis

| Check | The exact answer | FielDes | Date | Script |
|---|---|---|---|---|
| An aluminium bar held at 100 C and 20 C at its ends, the sides insulated: the temperature along it, and the heat flux | a straight line, Fourier's flux | **0.14 K** off the line, flux **-0.5 %** (5,000 tetrahedra); tolerance 0.3 K and 2 % | 2026-10-14 | `v4_slab_conduction.py` |
| A constant conductivity field against the same number | the number | identical | | the checks of the thermal conditions |

**What it says.** Steady conduction with fixed temperatures, heat inputs, generation and convection is exact for what linear elements can represent (a linear temperature) and converges with the elements elsewhere.
No radiation, no transient, no temperature-dependent properties in the built-in solver (a solver of your own can do them: see [Solvers of your own](#solvers-of-your-own)).

## Thermal stress

A bar held between its ends and heated reproduces the exact thermal stress (E times alpha times the temperature rise); a free part grows without stress.  Checked with the element checks of the static analysis (2026-10-14).

## Fluid flow

Laminar, steady or in time, incompressible, isothermal; linear tetrahedra with SUPG and PSPG stabilisation.  Measured 2026-10-05 (headless, exact bounds), the error falling with the element size:

| Case | Element size (elements across) | Result |
|---|---|---|
| Plane Poiseuille flow, duct 10 mm high with slip sides, Re 75, developed inlet | 1.25 / 1.0 / 0.8 mm (8 / 10 / 12.5) | pressure gradient **+4.1 / +0.9 / +0.8 %**, peak speed **+0.9 / -0.3 / -0.2 %**, flow in = out to 0.1 % |
| Hagen-Poiseuille flow, pipe 10 mm bore, Re 47, developed inlet | 1.0 / 0.8 / 0.6 mm (10 / 12.5 / 17) | pressure gradient **-1.9 / -1.5 / -1.05 %**, centreline speed **-1.7 / -1.6 / -1.1 %**, mass imbalance below 1e-4, dissipation = dp Q within 0.2 % |
| Water between two plates 10 mm apart, mean speed 5 mm/s (`v3_channel_flow.py`, 2026-10-14) | 22,000 tetrahedra | middle speed **-0.3 %** of 1.5 x the mean, pressure gradient **+0.2 %** of 12 mu U / H^2 (tolerance 3 % and 6 %) |
| Stokes drag on a 2 mm sphere on the axis of a 15 mm tube, wall factor 2.098 (Haberman & Sayre) | 1.0 / 0.75 / 0.6 mm (4 / 5.3 / 6.7 across) | drag **-5.3 / -1.7 / -2.3 %** (the reaction of the discrete momentum equations at the wall's nodes) |
| T-junction, one inlet and two outlets, Re 690 (an unseen case) | 1.0 mm (8) | converges in 38 iterations, in = outlets to 2e-6, the shorter branch carries 53 % |
| Lid-driven square cavity, Re 100 (Ghia, Ghia & Shin 1982) | 40 / 64 cells across | centreline velocity extrema **-8 / -4.4 %**, **-7 / -3.1 %** and **-5 / -1.6 %**; the primary vortex centre within 0.001 L |
| The flow in time: the start-up of the duct flow from an impulsive start, Re 30, `time=(8, 0.05)` | 0.75 mm (8) | settles to the steady solver's flow: pressure drop and wall force within **0.03 %**, peak speed identical |
| Drag on a round post between slip planes, Re 40, the sides 12 diameters apart | 1.0 mm (10 across the post) | C_d 2.19 on the given mean speed against 1.5-1.6 for an unbounded cylinder on a fine mesh: **high**, as a coarse mesh is |
| The same at Re 400 | 40 / 64 / 96 cells across | extrema **-17 / -10.4 / -6.7 %**, **-19 / -11.3 / -7.3 %**, **-16 / -9.2 / -5.9 %**; linear elements with SUPG under-predict the peaks on a coarse mesh and converge about linearly with the element size |

**What it says.** Smooth internal flows at laminar Reynolds numbers: within a few percent from 8-10 elements across a passage.  **Not validated, and not offered:** turbulence (no model: above a Reynolds number of
about 2000 in a pipe the result says it is out of range), compressibility, heat carried by the flow, free surfaces, boundary-layer elements.  A passage should be four elements across at least; the mass balance is exact only as the mesh is refined
(2.4 % between inlet and outlet on a slab two elements thick around a post, 0.13 % at four across).

## Topology optimization

The optimiser (density per element, method of moving asymptotes, the derivatives of every measure from the solution and one adjoint solve each) is checked in three ways:

| Check | Result |
|---|---|
| The derivative of every measure to every element's density against two evaluations of the measure (`FIELDES_TOPO_FDCHECK=<iteration>`) | agree; run after every change of the optimiser |
| The solver inside it against the theories of the static analysis, a constant field against the number | agree (the same checks as the static analysis) |
| A stress limit on a cut bracket at three element sizes (5 / 3.5 / 2.5 mm) | the same cut part reads **483 / 404 / 73 MPa**: a member that is one or two elements across is not resolved |

**What it says.** A compliance or volume design is as good as the analysis under it.  **A stress limit is only as good as the mesh**: members should be three elements across or more, and the optimised part can be checked on a finer mesh
(`best.verify(element_size=...)`).  A thin wall is cut at the density that keeps the volume, so a part can keep less material than the design (the angle bracket asked for half and kept 41 %).

## Flow topology optimization

The real drag of the optimised body (`.flow`) is what the result reports, so a design is judged by the flow solver itself.  Measured 2026-10-24 on a lens of the post's area (28 mm long, 4.3 mm wide, in example 17's slab): the drag of one
lens moved along the flow in steps of 0.2 mm ranges from 4.06e-7 to 4.75e-7 N on elements of 2 mm (**15 % of the mean, repeating with a period of 1 mm**): that is the mesh's noise, and it is the error bar of a drag read at that size.
The accepted reference is the least-drag shape: a design whose drag is lower than the reference body's would mean the solver is wrong.  Laminar only; a post that sheds a wake (above about Re 50) has no steady flow to optimise.

## Thermal topology optimization

The same optimiser over the thermal analysis; the sensitivities (one adjoint solve per measure) were checked against finite differences, and the result reports the thermal analysis of the optimised part (`best.max_temperature` is its true number).
Example 35 (a 60 x 60 x 3 mm aluminium plate making 6 W through its volume, a 12 mm strip held at 20 C, 30 % kept): the plate alone reads **28.3 C** where the heat is made, the design keeps 27 % and reads **29.4 C** (the optimiser, which makes heat in the void too, 41.4 C: its numbers are not the part's).
Convection acts on the surface of the design space, not on the holes the design makes inside it.

## Parametric optimization

Measured 2026-10-15 on a beam: 36 designs in 4.3 s give 8 x 14.3 mm, 0.0307 kg, bending 0.490 mm (the limit is 0.5) under 76.5 MPa (the limit is 90); beam theory for the tip load gives 14.26 mm, **0.4 %** less.  A grid of 156 designs finds 0.0323 kg, 5 % more, in 63 s.
It finds a good design in a few tens of analyses, not a proven best one; the peak stress of a mesh jumps by a few percent from one design to the next, so a limit on it is kept within that.

## Manufacturing analysis

The analyses (printability, machining reach, mould draft and undercuts) are checked against published rules of the processes (minimum wall, overhang angle, draft, tool radius) on the parts of `tests/manufacturing`, the cases and their judge in
`tests/verification`.  They say what a rule says about the geometry; they do not simulate the process (no warping, no tool deflection, no flow of the plastic).

## Meshing for another program

`tests/internal/meshing_checks.py`, 145 checks, 60 s: the volume and area of a box, a sphere and the air round a sphere against their exact values; no inverted element; faces that conform; the zones; every file read back; every cell of the Fluent and
OpenFOAM meshes closed.  The CFD example (example 36, 2026-10-26): 29,253 tetrahedra and 3,021 prisms, 114 of 32,274 cells (0.35 %) above 0.85 skewness, none above 0.95 (the worst is 0.919), the smallest orthogonal quality 0.36, volume ratio 4.6, smallest dihedral 16.8 degrees.
It is a mesh a solver accepts and the usual CFD guidelines call usable; it is not the polished mesh of a dedicated mesher: where a result depends on the mesh, refine it and compare.

## CAD operations and snapping

`tests/internal/cad_checks.py`, 60 checks: the volume of rotated, mirrored and patterned bodies, of revolved shapes (a tube, its quarter, half and three quarters), of a plate with each kind of hole in each direction (to 1 %), the area of a sphere (1,257 against 1,256.6) and a box,
a section by an oblique plane, the overlap of two spheres (109.1 against 108.9), the distance between two spheres (10.000), two boxes (5.000) and a sphere and a wall (3.002).
`tests/internal/snap_checks.py`: the centres of circular edges and the corners of a rounded block to 0.001-0.04 mm, and a cone snapped under a cylinder coaxially, base to base, by its own `position=` and `rotation=` (the angles written make the same turn again to 4e-14 over 500 random turns and the quarter-turn cases; the faces and points made on the cone go with it; a second snap of the cone, placed already, puts it on a box).

## Solvers of your own

`solve('heat', ...)` (see [Solvers](solvers.md)) is checked by `tests/internal/solver_checks.py` against the exact temperature of a bar held at two temperatures (a straight line, exact for linear elements) and against the exact solution of a bar whose
conductivity depends on the temperature (the Kirchhoff transform makes it a straight line).  A solver that you write is only as good as its equations and its checks: the same two checks are the way to test yours.

## What is not validated

- Contact, plasticity, buckling, large strains, dynamics beyond natural frequencies (not offered).
- Turbulence, compressibility, free surfaces, heat carried by a flow, flow with boundary layers (not offered).
- Anything on a mesh that has not been refined once to see whether the answer moves.
- Fatigue and anything where the peak stress at a notch decides: the built-in stress at a corner is the mesh's.
- The safety of a part: **FielDes is not validated for safety-critical use.**
