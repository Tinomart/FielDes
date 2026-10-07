# Analysis

FielDes solves linear finite-element problems directly on the shapes of the script — an imported STEP
part, a lattice, anything — and hands the results back as **fields**, so they can colour, offset, thicken or
fill the part they came from.

- [Units and conventions](#units-and-conventions)
- [Regions: supports and loads are shapes](#regions-supports-and-loads-are-shapes)
- [Boundary conditions](#boundary-conditions)
- [Materials](#materials)
- [Fields in analyses](#fields-in-analyses)
- [Static structural analysis](#static-structural-analysis)
- [Reading a result](#reading-a-result)
- [Modal analysis](#modal-analysis)
- [Thermal analysis](#thermal-analysis)
- [Thermal stress](#thermal-stress)
- [Topology optimization](#topology-optimization)
- [Fluid flow](#fluid-flow)
- [Flow topology optimization](#flow-topology-optimization)
- [Elements and accuracy](#elements-and-accuracy)
- [Caching](#caching)
- [Limitations](#limitations)

## Units and conventions

Millimetres, newtons, megapascals: lengths in mm, forces in N, stresses and moduli in MPa, displacements
in mm. Thermal problems use W, mm and °C (or K); conductivity in W/(mm·K) and heat transfer coefficients in
W/(mm²·K). (1 W/(m²·K) = 10⁻⁶ W/(mm²·K).) Density is in t/mm³ (steel = 7.85·10⁻⁹) for gravity loads.

## Regions: supports and loads are shapes

A support or a load is a **region** — an ordinary shape (a box, an imported part, a cylinder around a bolt
hole). Nothing is selected by clicking faces: you describe where.

| Function | |
|---|---|
| `fixed(region, x=True, y=True, z=True)` | The part is held wherever it lies inside `region`. `z=False` leaves that direction free (a sliding support). |
| `force(region, fx, fy=None, fz=None)` | A total force (N) spread evenly over the part's **surface** inside `region`. `force(region, (0, 0, -100))` works too. |
| `gravity(g=(0, 0, -9810))` | The part's own weight (acceleration in mm/s², 1 g down by default) with the material's density. |
| `thermal_expansion(temperature, reference=20)` | See [Thermal stress](#thermal-stress). |

```python
plates = union(box_exact((-76, 5, -61), (-45, 36, 29)),
               box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))
```

View the regions by displaying them together with the part (and hide them afterwards with the eye in the
model tree) to check that they touch the part where you mean them to — or better, as below.

A region can also be a **picked surface**: right-click a face in the viewport (or write `select_surface(part,
seed=(x, y, z))`) and give the selection to `fixed()` or `force()`; see [Selecting surfaces](selecting-surfaces.md).

## Boundary conditions

The supports and loads of an analysis are **boundary conditions**, made once and given to the analysis:
`static_boundary_conditions(part, supports, loads)` is the problem to solve without the solving: the supports
and loads tied to the part they act on. It is a shape — it has a row in the model tree (eye, delete) like any
other — and it draws the conditions **on the part**:

```python
conditions = static_boundary_conditions(bracket,
                                        supports=[fixed(plates)],
                                        loads=[force(lugs, (0, -2000, 0)), gravity((0, -9810, 0))])
conditions                                   # display it; hide it with the eye of the model tree
result = static_analysis(bracket, conditions, material=aluminium, element_size=4)
```

| Drawn | Means |
|---|---|
| blue place on the part, with blue pads and "Fixed" | fixed support (all directions): an array of flat pads lying on the held faces |
| cyan place, with cyan pads and "Sliding (fixed in …)" | sliding support (`fixed(region, y=False)` and the like) |
| red place, with an array of red arrows and the force ("2000 N") | force: identical arrows spread evenly over the loaded faces, all along the force; each touches the surface with its tip when it pushes in and with its tail when it pulls out; the text is the total force |
| one orange arrow and "Gravity 9.81 m/s²" | gravity, from where the line through the middle of the part along it leaves the part |

A legend at the bottom right names the colours (close it with its ×). The arrows, pads and texts are drawn by the
viewport over the model, not meshed with it: they follow the zoom and are never cut off by the render region. The
coloured places are the part's own surface, drawn a hair towards the eye, so the part can stay displayed or not. A place counts as in a region when it is within
about a hundredth of the part's size of it. `conditions.describe()` lists them; the object also holds `.part`,
`.supports` and `.loads`.

`static_analysis`, `modal_analysis` (the supports only) and `topology_optimization` are given the conditions —
that is the only way to give them their supports and loads: they have no `supports=` and `loads=` of their own,
and a list of `fixed(...)` items passed to them is an error that says so. One set of conditions can be given
to several analyses (a static one, a modal one, an optimisation of the same part), and for
`topology_optimization` the loads may be several load cases, `loads=[[force(a, …)], [force(b, …), gravity()]]`.

**Every condition is a model too** (kind `conditions`, orange): `fixed_1 = fixed(fixed_1_region)` is a row of the model tree, with its
region -- a box model of its own -- nested under it. Right-click empty space -> **Add simulation** -> **New support or load** (fixed, force, gravity, thermal
expansion), **New thermal condition** (fixed temperature, heat input, heat generation, convection), **New flow condition** (inlet,
outlet, wall, slip) writes one, with its region a box at the cursor; **Simulation -> static_boundary_conditions** (the menu of a model) writes the whole
`static_boundary_conditions(part, supports=[...], loads=[...])` for a model. In the tree, **drag them onto what takes them**: a
set of conditions onto an analysis becomes its `conditions` (it replaces the one it has), a support onto
`static_boundary_conditions` goes into its `supports=[...]`, a load into its `loads=[...]`, a thermal or flow condition into the
list of its analysis; a selected surface dragged onto a condition takes the place of its region. A drop that cannot be done says why
(a load does not go in the supports, a material does not go in a condition).

## Materials

`Material(name, E, nu, density, yield_strength, conductivity, expansion)` — an isotropic linear-elastic
material: Young's modulus E (MPa), Poisson's ratio, density (t/mm³), yield strength (MPa, for the safety
factor), thermal conductivity (W/(mm·K)) and expansion coefficient (1/K). Predefined:

| | E (MPa) | ν | yield (MPa) | conductivity (W/m·K) |
|---|---|---|---|---|
| `steel` | 200 000 | 0.30 | 250 | 50 |
| `stainless_steel` | 193 000 | 0.29 | 215 | 16 |
| `aluminium` | 69 000 | 0.33 | 95 | 167 |
| `titanium` (Ti-6Al-4V) | 114 000 | 0.34 | 880 | 6.7 |
| `pla` | 3 500 | 0.36 | 50 | 0.13 |
| `petg` | 2 100 | 0.38 | 45 | 0.20 |
| `abs_plastic` | 2 200 | 0.35 | 40 | 0.17 |
| `nylon` (PA12) | 1 700 | 0.39 | 45 | 0.24 |

These are typical handbook values. Check them against the real material before relying on a result.

**A material is a model** (a kind of its own, `material`: tan, a hatched block in the tree and the menus): `steel_1 = Material(...)`
is a row of the model tree, and so is a fluid (`Fluid(...)`, `water`). Right-click empty space -> **Add simulation** -> **New material** writes one of the
presets above with its numbers (steel, stainless steel, aluminium, titanium, PLA, PETG, ABS, nylon) or a material of your own;
**New fluid** the same for water, air, oil, glycerol. An analysis is given its material as `material=steel_1` (a flow, its fluid
as `fluid=`): **drag the material's row onto the analysis** and it becomes that argument (it replaces the one the analysis has),
and the analysis then shows it nested under it in the tree.

## Fields in analyses

The same rule as everywhere in FielDes — **wherever a number goes, a field goes** (see
[Fields everywhere](fields.md#fields-everywhere)) — holds for the properties and the loads of an analysis. A field is a
shape: its value at a point is the number there. It is read **at the centre of every tetrahedron** (or at the nodes or
boundary triangles a condition acts on), exactly: nothing is sampled onto a grid first. They work with the tetrahedral elements,
the default; the voxel elements (`element='hex'`) refuse a field with a message that says so.

| | Field |
|---|---|
| `Material(..., E=field)` | Young's modulus at every point (MPa): a graded material, stiff here and soft there; a lattice's density field can drive it |
| `Material(..., density=field)` | the density (t/mm³): the weight under `gravity()`, the mass in a modal analysis |
| `Material(..., conductivity=field)` | the conductivity (W/(mm·K)) in a thermal analysis |
| `Material(..., expansion=field)` | the thermal expansion coefficient (1/K) |
| `force(region, vector, profile=field)` | the **total** stays `vector`; it is spread over the surface in proportion to the profile (not negative): a pressure that grows along the beam, a load carried mostly on one side |
| `fixed_temperature(region, field)` | the temperature held, at every node inside the region |
| `convection(region, coefficient, ambient)` | the heat transfer coefficient and the ambient temperature, each a number or a field, at every boundary triangle |
| `heat_input(region, watts, profile=field)`, `heat_generation(...)` | the total power spread in proportion to the profile (by area over a surface, by volume through a body) |
| `thermal_expansion(temperature_field, reference)` | always was a field: a thermal analysis's own `.temperature` works |

```python
graded = Material('graded', ramp(x_field(), (0, 100), (aluminium.E, aluminium.E / 10)), aluminium.nu)
conditions = static_boundary_conditions(beam, supports=[fixed(wall)], loads=[force(top, (0, 0, -200), profile=ramp(x_field(), (0, 100), (0, 1)))])
result = static_analysis(beam, conditions, material=graded, element_size=3)
```

`examples/19_graded_material.py` runs it and compares with beam theory: the tip of a cantilever whose stiffness falls to a
tenth along its length deflects as much as the beam-theory integral says; a load spread evenly along the top deflects 0.375 of
the same load at the tip, and one spread in proportion to *x* deflects 0.55 of it — which is what the solver reproduces.
(`dev/tests/t_solver_fields.py` checks a constant field against the number, those theories, topology optimization, modal
analysis and the thermal conditions.)

**Not fields:** Poisson's ratio and the yield strength (a number: the safety factor is one number), the
components of a force (the profile is how a force is spread), and everything of the flow solver — its fluid, inlets, outlets and
walls take numbers, and say so when given a field. A field must be defined, and positive (the moduli, the conductivity), at
the centre of **every element**: where it is not, the analysis stops and says where.

## Static structural analysis

```python
conditions = static_boundary_conditions(bracket,
                                        supports=[fixed(plates)],
                                        loads=[force(lugs, (0, -2000, 0))])
conditions                                         # shown on the part
result = static_analysis(bracket, conditions, material=aluminium, element_size=4)
print("safety factor: %.1f" % result.safety_factor)
result                                             # shown: the stress on the deformed part
```

`static_analysis(shape, conditions, material=steel, element_size=None, bounds=None,
max_iterations=20000, tolerance=1e-6, cache=True, element='tet')` with the
`static_boundary_conditions(...)` of [Boundary conditions](#boundary-conditions). The element size defaults to the part's
longest side over 60. It raises `FeaError` if the problem cannot be solved as given (no support touching the
part, supports that do not hold it in place).

## Reading a result

**Fields** (shapes, usable in any expression): `von_mises`, `displacement`, `ux`, `uy`, `uz`, the stress
components `sxx … szx`, `max_principal`, `min_principal`, `strain_energy`.

**Numbers:** `max_von_mises`, `max_displacement`, `safety_factor` (if the material has a yield strength),
`elements`, `iterations`, `seconds`, `reaction` (the support force), and `range(field)` /
`element_range(field)`.

**Smooth or per element.** The smooth field is coloured by node values: displacements as solved, stresses
averaged over the elements at each node. The stress in a tetrahedron is constant, so the *element* peak is
higher than the smooth peak. `element_range('von_mises')` gives the element peak; the result line and the
safety factor use it (the honest, higher number).

**Showing it.** A result stated on its own (`result` as a statement, like any shape) displays the part
coloured by the von Mises stress and drawn deformed (the largest movement 5 % of the part's size). In the
viewport the [result card](interface.md#the-result-card) switches the field, magnifies the deformation,
shows the **elements**, and **steps the load** from 5 % to 100 % (a slider with play / pause: the stresses
and the deformation grow in proportion, as a linear analysis does). Hover to read the value under the
cursor. `colored(part, result.von_mises)` colours any shape by any field; `result.deformed(scale)` is the
part moved by its displacements, as a shape.

**Results are fields:**

```python
stiffer = bracket - 0.002 * result.von_mises      # grow the part where stress is high
light = offset_by(bracket, 0.02 * result.von_mises)
```

and they feed lattices (see [Field-driven design](lattices.md#driving-a-lattice-from-a-field) and
`examples/10_field_driven_design.py`).

## Modal analysis

```python
conditions = static_boundary_conditions(part, supports=[fixed(base)])
modes = modal_analysis(part, conditions, material=aluminium, modes=6, element_size=3)
print(modes.frequencies)            # Hz, lowest first
modes.modes[0]                      # shown: the first mode shape, deformed (play: it vibrates)
```

`modal_analysis(shape, conditions, material, modes=6, element_size=None, bounds=None, max_iterations=100,
tolerance=1e-6, cache=True, element='tet')`. Only the supports of the conditions are used: no loads, the
material's E and density are. Each
`Mode` has `.frequency` and the shape as fields (`displacement`, `ux`, `uy`, `uz`, scaled so the largest
movement is 1 — a shape, not an amplitude). A mode stated on its own is shown deformed, and the result card
steps the vibration through a cycle (24 phases; play to see it vibrate); the modal result itself shows its
first mode. Use the mode shape as a field: stiffen the part where the first mode moves most.

## Thermal analysis

Steady-state conduction. Boundary conditions are regions, like supports and loads:

| Function | |
|---|---|
| `fixed_temperature(region, T)` | The part is held at T inside the region. |
| `heat_input(region, watts)` | A total power spread over the part's surface inside the region (negative removes heat). |
| `heat_generation(region, watts)` | A total power generated through the part's volume inside the region (a heater, potted electronics). |
| `convection(region, h, ambient=20)` | The exposed surface inside the region exchanges heat with the ambient. `h`: still air ≈ 5–25·10⁻⁶, forced air 25–250·10⁻⁶, water 500–10 000·10⁻⁶ W/(mm²·K). |

At least one fixed temperature or convection is needed (otherwise the temperature is undetermined).

```python
result = thermal_analysis(part, [fixed_temperature(base, 20),
                                 heat_input(chip, 5.0),
                                 convection(fins, 25e-6, ambient=20)],
                          material=aluminium, element_size=2)
result                               # shown: coloured by temperature
```

`ThermalResult` has `temperature`, `heat_flux` (magnitude, W/mm²), `qx`, `qy`, `qz`, and
`max_temperature`, `min_temperature`, `max_heat_flux`, `heat_in`, `heat_out` (W: through fixed temperatures
and by convection — they balance) and `.range(field)`; stated on its own it is shown coloured by the
temperature (the result card switches to the heat flux). The conductivity comes from the material, or
`conductivity=` overrides it.

## Thermal stress

```python
temp = thermal_analysis(part, [...], material=aluminium)
conditions = static_boundary_conditions(part, supports=[fixed(base)],
                                        loads=[thermal_expansion(temp.temperature, reference=20)])
stress = static_analysis(part, conditions, material=aluminium)
```

A part at `temperature` (a field, e.g. a thermal result's, or a number) expands by the material's
coefficient for each degree above `reference` (where it is stress-free). Held parts are stressed; free ones
just grow. Static analysis only.

## Topology optimization

`topology_optimization(part, conditions, material, volume_fraction=0.3, element_size=None,
iterations=60, filter_radius=None, keep=None, avoid=None, extrude=None, penalty=3, move=0.2, bounds=None,
max_iterations=20000, tolerance=1e-5, cache=True, element='tet')` finds the **stiffest** layout that uses
`volume_fraction` of `part` (the design space) for the supports and loads of the conditions.

| Option | |
|---|---|
| `keep` | regions (a shape or list) that must stay solid — bolt bosses, mounting faces. The material around supports and loads always stays. What the part has **excluded** (`exclude()`, [Fields](fields.md#excluded-regions-exclude)) is added by itself, however many operations came after it: those places stay exactly as they are |
| `avoid` | regions that must stay empty |
| `extrude` | `'x'`, `'y'`, `'z'`: the same design along that axis (a profile to extrude, or to cut right through) |
| `filter_radius` | the smallest member size, mm (default 1.5 elements) |
| the conditions' `loads` | a list of loads, **or several load cases** `[[force(a, …)], [force(b, …), gravity()]]`: the part is made stiff for all of them (the sum of the compliances is minimised) |

The optimisation solves the analysis 30–60 times. Returns a `TopologyResult`:

- `.density` — a field, 0 (no material) to 1 (solid);
- `.shape(threshold=None)` — the optimised part. The default threshold keeps the volume fraction you asked
  for (the density is partly grey, so cutting at 0.5 would keep far less). Loose scraps in the result are what the
  part would become with more volume. If the optimised part falls into **separate pieces** (`.pieces`, counted for
  tetrahedral optimisations: pieces joined only by a link thinner than about a millimetre count as separate), the
  output says so and suggests a higher `volume_fraction` — kept regions (`keep=`, `exclude()`) use up part of the
  volume asked for, so the free material has less of it;
- `.compliance` (per iteration — lower is stiffer), `.densities` (the density field after each iteration),
  `.volume_fraction`, `.iterations`, `.seconds`;
- `.verify()` — a static analysis of the optimised part (a list when there are several load cases).

The result stated on its own shows the optimised part coloured by the density, and the result card **steps
through the iterations**: the part as it was after each one (a slider with play / pause; a step is meshed
the first time it is shown, then kept). `element='hex'` optimisations keep only the final density.

**Thermal topology optimization** (`thermal_topology_optimization(part, boundary, material,
volume_fraction, …, element='hex')`) finds the layout that keeps the heat coolest where it enters: the
heat-weighted mean temperature of the heat inputs is minimised. Heat inputs (`heat_input`,
`heat_generation`) are required; fixed-temperature regions are the sinks; convection follows the design
(give the region as the air around the design space and the fins grown into it are cooled). `extrude=` gives
extruded or pin-fin heat sinks — use it for air cooling: with one coefficient everywhere a closed pocket
deep inside would count as cooled like an open face. It designs on the voxel grid only. The result
(`ThermalTopologyResult`) has `.density`, `.shape()`, `.temperature` (per iteration) and `.verify()`.

## Fluid flow

Incompressible, laminar flow of a fluid through a shape -- the inside of a pipe, a duct, a box with a part cut out of
it -- steady or in time, on the same tetrahedra that follow the surface as the other analyses:

```python
slab = box_exact((0, 0, 0), (80, 40, 4))                           # a thin slab of water, open at its sides...
post = cylinder_z(5, 6, (24, 20, -1))                              # ...with a round post standing in it
faces = union(box_exact((-1, -1, -1), (81, 41, 0.01)), box_exact((-1, -1, 3.99), (81, 41, 5)))
sides = union(box_exact((-1, -1, -1), (81, 0.01, 5)), box_exact((-1, 39.99, -1), (81, 41, 5)))
flow = fluid_analysis(difference(slab, post),
                      [slip(faces), slip(sides),                   # a two-dimensional flow
                       inlet(box_exact((-1, -1, -1), (0.01, 41, 5)), speed=4.0),       # Re = U D / nu = 40
                       outlet(box_exact((79.99, -1, -1), (81, 41, 5)), pressure=0)],
                      fluid=water, element_size=1.0)
print(flow)                              # the flows, the pressure drop, the Reynolds number, the drag on the post
flow                                     # shown: the speed on the fluid, streamlines, particles moving along them
```

**The fluid domain is a shape**: the fluid is where its field is negative. The boundary conditions are regions on
its surface, like the supports and loads of a static analysis:

| Function | |
|---|---|
| `inlet(region, velocity=(vx, vy, vz))` / `speed=` / `flow_rate=` | The fluid comes in through the surface inside `region`: a velocity (mm/s; the direction and the mean speed over the inlet), a mean speed along the inward normal, or a flow rate (mm³/s). `profile='uniform'` (a plug with the no-slip rim) or `'developed'` (the fully developed profile of that cross-section, parabolic in a round pipe). The speed or flow rate is matched exactly on the mesh. |
| `outlet(region, pressure=0)` | The fluid leaves at that pressure (MPa). Put it where the flow leaves parallel to the walls (the "do-nothing" condition lets a developed flow out undisturbed). |
| `wall(region, velocity=(0, 0, 0))` | A moving wall, no-slip. Every surface in no region is a wall at rest, so `wall()` is for moving walls (a lid, a belt) and for naming a wall whose force is wanted. |
| `slip(region)` (`symmetry`) | A symmetry plane or frictionless wall: nothing flows through it, the fluid slides along it. |

`Fluid(name, density, viscosity)` in t/mm³ and MPa·s (water: `1.0e-9`, `1.0e-9`; air, oil and glycerol are
predefined). `gravity=(gx, gy, gz)` (mm/s²) is a body force. A closed domain (no outlet: a lid-driven cavity) is
allowed; its pressure is then relative, zero at one point.

`fluid_analysis(domain, conditions, fluid=water, element_size=None, bounds=None, gravity=None, stokes=False,
max_iterations=60, tolerance=1e-5, cache=True, time=None, store_every=1)`. The element size defaults to the longest
side over 40; the passages should be four elements across or more (the result says how many there are). `stokes=True`
leaves the convection out (creeping flow, one linear solve); otherwise the Navier-Stokes equations are solved by Picard
and then Newton iterations from the Stokes solution until the relative residual is below `tolerance`.

**The flow in time.** `time=(duration, step)` (seconds) solves the flow in time instead of the steady flow: from the
Stokes flow at t = 0 (an impulsive start) by steps of `step` seconds (backward Euler, each step a nonlinear solve) to
`duration`. A step of about an element crossing (element size / speed) keeps it accurate. Every `store_every`-th step
is kept: `result.steps` (a `FluidStep` each: `.time`, the fields and the numbers at that time, `.streamlines()`),
`result.times`; the result's own fields are the last step's. A wake that sheds vortices needs this -- it has no
steady state -- and so does anything started, stopped or stirred.

**Reading a result.** Fields (shapes): `speed`, `vx`, `vy`, `vz` (mm/s), `pressure`, `total_pressure` (MPa),
`shear_rate`, `vorticity` (1/s) -- the velocity and the pressure as solved at the nodes, the others from each
element's constant gradient. Numbers: `inlet_flow`, `outlet_flow` (mm³/s; `inlet_flows` / `outlet_flows` per item),
`wall_flow` (net flow in through the walls: a moving wall, the rounded rim of an inlet), `mass_imbalance`
(|in + wall_flow − out| / in), `pressure_drop` (MPa, mean inlet minus mean outlet), `max_speed`, `wall_force` (N, on all walls;
`wall_forces` per `wall(...)` item), `dissipation` (W), `reynolds` (ρUD_h/μ of the inlet), `cell_reynolds`,
`hydraulic_diameter`, `elements_across`, `elements`, `nodes`, `iterations`, `residual`, `converged`, `seconds`,
`warning`. `result.streamlines(seeds=None, count=40, max_time=None, max_points=4000, backward=False, step=None)` gives
the paths of particles carried by the flow (lists of `(x, y, z, speed, time)` points, from the inlets by default). A
pressure field is in MPa, so it can load a solid directly.

**Showing it.** The result stated on its own shows the fluid coloured by the speed, as every other result is shown,
with 40 streamlines from the inlets and particles moving along them drawn over it -- the velocity field around a body
and its wake. The [result card](interface.md#the-result-card) switches the field (the pressure, the vorticity, ...),
**Flow** hides the lines, and the **step** slider with play / pause walks through the solver's iterations (the
Stokes start to the converged flow: `result.steps`, a `FluidStep` each) or, for a flow in time, its stored times
(the fields and the streamlines of that time). Hovering reads the value; the section card cuts the fluid open and
paints the field on the plane.

**How it is solved.** Linear tetrahedra for the velocity and the pressure, stabilised the usual way (SUPG for the
convection, PSPG for the pressure, a grad-div term, a backflow term on the outlets), all stabilisation residual-based
and consistent; the viscous term in the Laplacian form, whose natural outlet condition a developed profile satisfies;
the linear systems by BiCGSTAB with an incomplete LU (a direct sparse LU for small ones and when the iterative solve
fails).

**Verified** (headless, exact bounds, the numbers as measured on 2026-10-05; the error falls with the element size):

| Case | Element size (elements across) | Result |
|---|---|---|
| Plane Poiseuille flow, duct 10 mm high with slip sides, Re 75, developed inlet | 1.25 / 1.0 / 0.8 mm (8 / 10 / 12.5) | pressure gradient +4.1 / +0.9 / +0.8 %, peak speed +0.9 / −0.3 / −0.2 %, flow in = out to 0.1 % |
| Hagen-Poiseuille flow, pipe 10 mm bore, Re 47, developed inlet | 1.0 / 0.8 / 0.6 mm (10 / 12.5 / 17) | pressure gradient −1.9 / −1.5 / −1.05 %, centreline speed −1.7 / −1.6 / −1.1 %, mass imbalance below 1e-4, dissipation = Δp·Q within 0.2 % |
| Stokes drag on a 2 mm sphere on the axis of a 15 mm tube (the sphere at rest, tube and fluid moving: a settling sphere), wall factor 2.098 of Haberman & Sayre | 1.0 / 0.75 / 0.6 mm (4 / 5.3 / 6.7 across the sphere's diameter) | drag −5.3 / −1.7 / −2.3 % (the force is the reaction of the discrete momentum equations at the wall's nodes; integrating the elements' stress over the wall gives −28 / −12 / −10 %) |
| T-junction, one inlet and two outlets of different length, Re 690 (an unseen case) | 1.0 mm (8) | converges (38 iterations), in = sum of the outlets to 2e-6, the shorter branch carries 53 % |
| Lid-driven square cavity, Re 100 (Ghia, Ghia & Shin 1982, J. Comput. Phys. 48) | 40 / 64 cells across | the centreline velocity profiles against the tables: extrema −8 / −4.4 % (u on x = 0.5), −7 / −3.1 % and −5 / −1.6 % (v on y = 0.5); the primary vortex centre within 0.001 L |
| The flow in time: the start-up of the duct flow (slip sides, developed inlet, Re 30) from an impulsive start, `time=(8, 0.05)` | 0.75 mm (8) | settles within 2 s of flow to the steady solver's flow: pressure drop and wall force within 0.03 %, peak speed identical; 0.3 s per step (22,000 elements) |
| Drag on a round post between slip planes (a 2D cylinder), Re 40, the sides 12 diameters apart | 1.0 mm (10 across the post, 400,000 elements) | C_d 2.19 on the given mean speed against 1.5-1.6 for an unbounded cylinder on a fine mesh: high, as a coarse mesh is (the sphere above was 5 % high at 4 across); the trend with a finer mesh is not yet measured |
| The same at Re 400 | 40 / 64 / 96 cells across | extrema −17 / −10.4 / −6.7 %, −19 / −11.3 / −7.3 %, −16 / −9.2 / −5.9 %; vortex centre within 0.005 L: linear elements with SUPG under-predict the peaks on a coarse mesh and converge about linearly with the element size (the 96-cell run: 260,000 elements, 9 iterations, 200 s) |

**Limits, plainly.** Laminar only: no turbulence model; a flow beyond the laminar range (a Reynolds number above
about 2000 in a pipe) is not described by this and the result says so. In time, the stepping is first order (backward
Euler): a step much longer than an element crossing smooths the flow in time. No
boundary-layer (inflation) elements: the mesh must be fine enough across the passages. The pressure is linear in
each element, so a pressure peak at a sharp corner is smeared over an element. No free surfaces, no heat carried by
the flow, no compressibility. The velocity imposed at an inlet is the mean over its cross-section (the inlet's patch of
triangles projected along the flow direction, so the rounded edge the mesher makes between the inlet and the surfaces
around it counts as the sharp corner it replaces); its rim is at rest (no-slip), so a `'uniform'` profile is a plug that
drops to zero over the last element -- use `'developed'` for a pipe or duct. The mass balance (`mass_imbalance`, flow
in against flow out) is exact only as the mesh is refined: the pressure stabilisation (PSPG) trades a little local
mass conservation for a stable pressure, and where the momentum residual is large on a coarse mesh the flows
differ -- 2.4 % between inlet and outlet on a slab two elements thick around a post, 0.13 % at four elements across;
a passage should be four elements across at least.

## Flow topology optimization

`flow_topology_optimization(body, domain, conditions, fluid=water, objective='drag', volume=1.0, region=None,
keep=None, avoid=None, element_size=None, iterations=40, filter_radius=None, move=0.1, darcy=1e-5, extrude=None,
flow_direction=None, lift_direction=None, bounds=None, cache=True)` takes a **body in a stream** and changes its
shape, and its topology, to make it best for the force the flow puts on it. `body` is the solid as it is (a Shape),
`domain` the fluid domain it sits in (a Shape that holds the body's place too), `conditions` the flow's inlets,
outlets, slip planes and walls as for `fluid_analysis`. The flow is the real one -- the Navier-Stokes equations at
the Reynolds number the inlet gives -- with the body a friction that a density per element sets (1 solid, 0 fluid:
Borrvall & Petersson's penalised model), so the drag and the lift are the momentum the flow loses in the body.

| Option | |
|---|---|
| `objective` | `'drag'`: the least force along the flow; `'lift'`: the most force across it; `(w_drag, w_lift)`: w_drag·drag − w_lift·lift is minimised |
| `volume` | what the body may use of its own volume: `1.0` keeps it, `(0.5, 1.5)` bounds it, `None` on a side for no bound there |
| `region` | where material may be at all (a Shape; default the whole domain): the body shrinks, grows, moves and splits inside it |
| `keep` / `avoid` | regions that stay solid (a shaft, a mounting) / regions that stay fluid. What the body has excluded (`exclude()`) is kept by itself |
| `extrude` | `'x'`, `'y'`, `'z'`: the body is the same all along that axis (a 2D shape in a 2D flow) |
| `flow_direction`, `lift_direction` | the drag and the lift directions (default: the inlets' mean direction, and perpendicular to it in the plane of the domain's two long axes) |
| `filter_radius`, `move`, `darcy`, `iterations` | the smoothing radius of the boundary's motion (mm, default 1.5 elements; the shape itself is never smoothed); the most the boundary moves in one iteration (in elements, 0.5 by default; a step that raises the objective is taken back and halved); the solid's permeability relative to the element (its friction is μ / (darcy·h²): the flow penetrates it by about √darcy elements, 0.1 by default); at most how many iterations |

Returns a `FlowTopologyResult`: `.shape(iteration=None)` the optimised body (or the body after iteration k),
`.fluid_shape()` the fluid around it, `.level` and `.levels` the level set (mm, positive inside the body) and the
same after each iteration, `.drag` and `.lift` (N) per iteration as the optimiser's model sees them (the body a
friction in the flow), `.flow` a `FluidResult` of the **real** flow around the final body (the body a wall at rest, the
same conditions: fields, numbers, `streamlines()`) with `.real_drag` and `.real_lift` its force, `.model_flow` the
optimiser's own flow (`.model_flow.steps[k]` the flow around the body of iteration k), `.volume` (mm³).
Stated on its own it shows the body in the flow: the fluid coloured by the speed with streamlines and particles
over it, the body solid; the result card steps through the iterations (the body as it was after each one and the
flow around it).

**How it is driven.** The design is a **level set** at the mesh's nodes: a smooth field (the given body's own
distance to start with) whose zero level is the body's boundary, so the boundary is placed to a fraction of an
element and stays smooth rather than staircasing along the mesh. Each element's share
of the body is the exact fraction of it where the level set is positive (the volume of a tetrahedron above a plane,
in closed form, with its derivative in the four nodal values), and that fraction sets the element's friction. One
flow solve and one adjoint solve per iteration: the Newton Jacobian of the converged flow (convection included) is
transposed and solved for the adjoint of the force objective, the derivative of the residual in each element's
friction is differenced element by element, and the chain runs through the fractions to the nodal values; the
descent direction is that gradient smoothed over `filter_radius` (the shape itself is never smoothed, so a nose or
a tail can sharpen as far as the sensitivities ask). The level set is rescaled to unit slope each iteration (a cheap
reinitialisation), moved down the direction by at most `move` elements, and offset as a whole until the body's
volume is within its bounds (bisection: an offset of the level set is an offset of the boundary); a step that
raises the objective is taken back and halved, and the run stops when the boundary stops moving or no step lowers
the objective any more (the result says which, and how many steps were taken back). The steps shown for the
iterations are the optimiser's own flow, in which the solid is a friction and a little fluid creeps through it;
their streamlines are cut where they would enter the body. The final step is the real flow. The linear solves of
every flow -- most of its time -- use a block incomplete-LU preconditioner, one block per core with a little
overlap, so they run on all cores.
The solid's friction is set by the element, μ / (darcy·h²): a solid far more impermeable than that is a wall already
when a sliver of it lies in an element, so the fraction then changes the flow only when it is nearly nothing and
the sensitivities mislead (the optimiser's own drag is therefore a little below the real body's; `.flow` gives the
real one). The boundary splits and merges as it moves; a hole does not open in the middle of solid.
`FIELDES_FLOW_FDCHECK=1` checks the nodal sensitivities by finite differences.

**Measured (2026-10-05)** on the round post of example 16 (10 mm across in a slab of water at Re 40, 1.5 mm
elements, the drag minimised with the volume kept): the optimiser's own drag falls monotonically from 8.72e-7 to
4.82e-7 N (45 % less) over 24 iterations (two steps taken back, 160 s on 12 threads), the volume is held to all
digits, and the post becomes a slender body 21 mm long and 4 mm wide; the real flow around that body (the body a
wall at rest) gives 4.05e-7 N against the round post's 8.21e-7 N: 51 % less drag. On a smaller post at Re 21 the
optimiser's drag goes 5.55e-7 to 3.28e-7 N (41 % less). The nodal sensitivities agree with finite differences within
7 % at every node checked (ratios 0.93 to 1.07). Limits:
the flow sees the body as a porous solid whose wall lies a fraction of an element inside the design's wall (refine
to tighten); laminar flow only; at a Reynolds number where the wake sheds (above about 50 for a post) there is no
steady flow to optimise, and the optimiser works on the unstable symmetric one.

## Elements and accuracy

| `element=` | |
|---|---|
| `'tet'` (default) | Linear tetrahedra that **follow the part's surface**: an unstructured mesh built on the shape's own field. Boundary vertices lie on the surface, sharp edges are followed to a fraction of an element. `element_size` is the edge length. Stiff in bending: refine (a cantilever four elements thick reads 96 % of beam theory, eight thick 99 %). |
| `'hex'` | A voxel grid of cubes, `element_size` on a side, each a trilinear hexahedron with incompatible modes (bends without spurious shear). Its stiffness follows how much of the cube lies inside the part, so the surface is a staircase. |
| `'hex_basic'` | The same grid with the plain trilinear hexahedron (shear-locks in bending). |

Smaller elements are more accurate and slower. A reasonable workflow: solve coarsely to find the load path,
then halve `element_size` once and see whether the peak stress moved by more than a few percent. The solver
is a preconditioned conjugate gradient (multigrid for large voxel problems).

## Caching

An unchanged analysis (same part, supports, loads, material, element size) is **not solved again**: editing
other parts of the script, re-running, or moving a section through the result costs nothing. The same holds
for modal, thermal, flow and optimisation runs, and it holds **across sessions**: every solved analysis is
written to the result cache (its mesh, every field, the modes, the iterations, a flow's steps) and read back when
the same problem is asked again after FielDes was closed and opened, as a field is from the field cache -- the
log says `[result cache] read ... back`. A result read back keeps the keys of its fields, so what was rendered
of it is found in the render cache too. `cache=False` solves every time. See
[Caching and performance](caching-and-performance.md).

## Limitations

- Linear elasticity: small strains, linear isotropic material, **no contact**, no plasticity, no buckling,
  no dynamics beyond natural frequencies. Always compare the peak stress with the material's yield strength
  *and* with a hand calculation.
- Linear tetrahedra need refinement in bending; check convergence.
- Fields as properties and loads work with the tetrahedral elements only; the flow solver takes numbers.
- A stress singularity (a sharp re-entrant corner or a point load) does not converge with refinement; the
  peak there means nothing. Load over a region.
- The mesh follows the **field** of the part. For an imported part with poorly fitted B-spline faces (see
  [STEP import](step-import.md#free-form-b-spline-faces)) that is the fitted surface, except inside an `exclude()`d region, where the part is its exact surface (a field of the
  STEP file's own mesh) and the analysis mesh follows that.
- Fluid flow: laminar, steady, incompressible, isothermal; no boundary-layer elements (see [Fluid flow](#fluid-flow)).
- FielDes is a design tool, not a certified solver. Do not use it as the sole basis for safety decisions.
