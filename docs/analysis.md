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
- [Parametric optimization](#parametric-optimization)
- [Elements and accuracy](#elements-and-accuracy)
- [Caching](#caching)
- [Limitations](#limitations)
- [Verification: four known answers](#verification-four-known-answers)

## Units and conventions

Millimetres, newtons, megapascals: lengths in mm, forces in N, stresses and moduli in MPa, displacements
in mm. Thermal problems use W, mm and °C (or K); conductivity in W/(mm·K) and heat transfer coefficients in
W/(mm²·K). (1 W/(m²·K) = 10⁻⁶ W/(mm²·K).) Density is in t/mm³ (steel = 7.85·10⁻⁹) for gravity loads.

## Regions: supports and loads are shapes

A support or a load is a **region** — an ordinary shape (a box, an imported part, a cylinder around a bolt
hole). Nothing is selected by clicking faces: you describe where.

| Function | |
|---|---|
| `fixed(region, displacement=(0, 0, 0))` | The part is held wherever it lies inside `region`. `displacement` is where it is held, in each direction (mm): **0** (the default) lets the part not move there, **`inf`** leaves that direction free -- `displacement=(0, 0, inf)` is a support the part slides along z on (`inf` is `math.inf`, `float('inf')`; `from fieldes import *` has it as `inf`) -- and **any other number is a displacement the support imposes**: `(0, 0, -0.5)` pushes the part 0.5 mm down where it is held, and a static analysis on tetrahedra solves for the rest of it (its reaction forces are the result). A modal analysis holds the part still there, a topology optimization refuses an imposed displacement (hold the part and load it with a force). |
| `force(region, fx, fy=None, fz=None)` | A total force (N) spread evenly over the part's **surface** inside `region`. `force(region, (0, 0, -100))` works too. |
| `gravity(g=(0, 0, -9810))` | The part's own weight (acceleration in mm/s², 1 g down by default) with the material's density. |
| `thermal_expansion(temperature, reference=20)` | See [Thermal stress](#thermal-stress). |

```python
plates = union(box((-76, 5, -61), (-45, 36, 29)),
               box((45, 5, -61), (76, 36, 29)))
lugs = box((-60, -90, -61), (60, -60, 29))
```

View the regions by displaying them together with the part (and hide them afterwards with the eye in the
model tree) to check that they touch the part where you mean them to — or better, as below.

A region can also be a **picked surface**: right-click a face in the viewport (or write `select_surface(part,
seed=(x, y, z))`) and give the selection to `fixed()` or `force()`; see [Selecting surfaces](selecting-surfaces.md).

## Boundary conditions

Every condition is a **model of its own** -- a variable with a row in the model tree, with its eye and its bin -- and every **kind** of
condition is an **input of its own** of the analysis that takes it. A simulation is given the body (its first argument) and one list for
each kind of condition it takes; a kind it has none of is `[]`:

| Simulation | Its inputs for conditions (each a list, all of them required) |
|---|---|
| `static_analysis(part, supports, loads, material=steel, ...)` | `supports` (`fixed(...)`), `loads` (`force(...)`, `gravity(...)`, `thermal_expansion(...)`) |
| `modal_analysis(part, supports, material=steel, ...)` | `supports` |
| `topology_optimization(analysis, constraints, ...)` | none of its own: the problem is the **analysis** it is given -- its part, supports, loads and material (a list of analyses is several load cases, see [Topology optimization](#topology-optimization)); of a `thermal_analysis`: its part, conditions and material |
| `thermal_analysis(part, fixed_temperatures, heat_inputs, heat_generations, convections, material=aluminium, ...)` | those four |
| `fluid_analysis(body, domain, inlets, outlets, boundaries, fluid=water, ...)` | those three (`boundaries` takes `wall(...)` and `slip(...)` items, one kind or both); the flow goes around the `body`, inside the `domain` |

```python
support = fixed(plates)                                          # held here
push = force(lugs, (0, -2000, 0))                                # pulled here
weight = gravity((0, -9810, 0))
result = static_analysis(bracket, supports=[support], loads=[push, weight], material=aluminium, element_size=4)
support                                                          # display it: the surface and its pads, on the bracket
```

A condition in the wrong input is an error that says where it goes: `supports=[force(...)]` stops with "supports are fixed(...)
items, and a force(...) is not one -- it goes in loads=". One condition may stand for an input without a list (`supports=support`).
The body is the analysis's first argument and is not part of any condition, so one condition can be given to several simulations
(a static one, a modal one, an optimisation of the same part), and an optimisation can be given several load cases.

**Every condition draws itself, on its own.** Display it (its eye in the model tree, or its name as a statement) and the viewport shows
**only the surface it acts on, flat in its colour, with its symbols on it**; the body has its own row, so you see the condition on the
body or by itself. The simulation's row in the model tree has a **boundary conditions** row under it -- a placeholder for every
kind of condition the simulation still waits for, and the conditions it has under them, in the order the simulation takes them. **The eye of that row
shows all of them at once, or hides all of them**; each condition keeps its own eye, so you can also show them one by one. Which
surface of which body is drawn is found the same way before a simulation has been given the condition: the body its surface was picked
on (`select_surface`, `surface_from_bodies`), else the biggest solid of the script that its region reaches -- a region can be a body or
a field as well as a surface. A region that touches no surface of that body gets a note ("a force acts where the part is not") and no
symbol. Only a script with no body at all draws the regions themselves.

| Drawn | Means |
|---|---|
| blue place on the part, with blue pads and "Fixed" | fixed support (all directions): an array of flat pads lying on the held faces |
| cyan place, with cyan pads and "Sliding (fixed in ...)" | sliding support (`fixed(region, displacement=(0, inf, 0))` and the like; a held direction that imposes a displacement says it: "Fixed, displaced z -0.5 mm") |
| red place, with an array of red arrows and the force ("2000 N") | force: identical arrows spread evenly over the loaded faces, all along the force; each touches the surface with its tip when it pushes in and with its tail when it pulls out; the text is the total force |
| one orange arrow and "Gravity 9.81 m/s²" | gravity, from where the line through the middle of the part along it leaves the part |
| bright orange place with pads and "20 °C" | fixed temperature |
| yellow place with arrows going in and "5 W" | heat input (arrows going out for a negative power) |
| gold place and "5 W generated" | heat generated through the volume |
| purple place with pads and "Convection h = ..." | convection |
| green place with arrows going in and "Inlet 10 mm/s" | inlet |
| violet place with arrows going out and "Outlet" | outlet |
| grey place with pads and "Wall" ("Moving wall") | wall |
| light-blue place with pads and "Slip" | slip |

A legend at the bottom right names the colours (close it with its ×). The arrows, pads and texts are drawn by the
viewport over the model, not meshed with it: they follow the zoom and are never cut off by the render region. The
coloured places are the part's own surface, drawn a hair towards the eye -- only those: the part is not drawn with them, so it can stay displayed or not. A place counts as in a region when it is within
about a hundredth of the part's size of it. `describe(condition)` says one line about a condition.

**Making them.** Right-click empty space -> **Engineering** -> **Structural** -> **New support or load** (fixed, force, gravity, thermal
expansion), **New thermal condition** (fixed temperature, heat input, heat generation, convection), **New flow condition** (inlet,
outlet, wall, slip) writes one, **with a placeholder where it acts** (`force_1 = force(region=..., vector=..., profile=None)`: like any argument that is missing, that statement waits -- the rest of the script runs -- and the placeholder row of the
model tree takes a body, a field or a surface dropped on it -- the menu makes no box of its own for a region). **Select the regions first** -- surfaces, fields or bodies -- and the same entries write the condition for what is selected:
`fixed_1 = fixed(selection_1)`, or `force(a, b, (0, -100, 0))` for several. **Select a body first and the regions after it, and the first is the BODY the condition is on, the others where on it**:
`fixed(region=plate, body=bracket)` holds the bracket on its surface where it meets the plate -- the call makes that surface itself (`surface_from_bodies(bracket, plate)`), so no surface or intersection is made by hand to
give a condition a place (`body=` is an argument of every condition that acts on a surface: `fixed`, `force`, `fixed_temperature`, `heat_input`, `convection`, `inlet`, `outlet`, `wall`, `slip`); a single surface
stays valid as it is. **Every condition takes any number of regions, one argument after the other**, and in the model tree a body, a surface or a field dropped on a
condition is one more of them. **A force is written with its `vector`** (0, -100, 0 newtons to begin with), where it is changed. **Engineering -> Structural -> static_analysis** (and modal; thermal and fluid in their own categories; **topology_optimization** is the first entry of the Engineering menu itself, above the categories: it works for an analysis of any of them) writes the simulation of the body selected, **with the conditions selected written in the input of their kind** and a placeholder
for every other input (`fluid_analysis(body, domain, inlets=[inlet_1], outlets=..., boundaries=...)`): nothing is built for you, the run stops before the line until the placeholders are filled. A condition has **no body**: the simulation is given it.

**Dragging them in.** In the model tree, drag a condition -- or several, selected with Ctrl or Shift -- onto the simulation, onto its **boundary conditions** row or onto the placeholder of its kind: they go into
the input of their kind **as a list** (`boundaries=[wall_1, slip_1, slip_2]`), a placeholder is replaced by it and a list already there gets them as more
items. **Dropped on the simulation or on its boundary conditions row, conditions of different kinds each go into the input of their kind** -- select the whole problem and drop it at
once. Dropped on the placeholder (or the list) of one kind, only that kind is taken. All of them go in, or none does and the reason is said: a load does not go in the supports, a material does not go in a condition.
A condition that is defined below the simulation is moved above it.
Taking one out of the call (its shadow's bin, or `D`) leaves the others, and the last one leaves the placeholder.

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
is a row of the model tree, and so is a fluid (`Fluid(...)`, `water`). Right-click empty space -> **Engineering** -> **New material** writes one of the
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
result = static_analysis(beam, supports=[fixed(wall)], loads=[force(top, (0, 0, -200), profile=ramp(x_field(), (0, 100), (0, 1)))],
                         material=graded, element_size=3)
```

`examples/19_graded_material.py` runs it and compares with beam theory: the tip of a cantilever whose stiffness falls to a
tenth along its length deflects as much as the beam-theory integral says; a load spread evenly along the top deflects 0.375 of
the same load at the tip, and one spread in proportion to *x* deflects 0.55 of it — which is what the solver reproduces.
(The solver is checked against these theories, and a constant field against the number, also in topology optimization,
modal analysis and the thermal conditions.)

**Not fields:** Poisson's ratio and the yield strength (a number: the safety factor is one number), the
components of a force (the profile is how a force is spread), and everything of the flow solver — its fluid, inlets, outlets and
walls take numbers, and say so when given a field. A field must be defined, and positive (the moduli, the conductivity), at
the centre of **every element**: where it is not, the analysis stops and says where.

## Static structural analysis

```python
support = fixed(plates)
load = force(lugs, (0, -2000, 0))
result = static_analysis(bracket, supports=[support], loads=[load], material=aluminium, element_size=4)
support                                            # shown on the part the analysis was given (the load the same way)
print("safety factor: %.1f" % result.safety_factor)
result                                             # shown: the stress on the deformed part
```

`static_analysis(shape, supports, loads, material=steel, element_size=None, bounds=None,
tolerance=1e-6, cache=True, element='tet')` with the
conditions of [Boundary conditions](#boundary-conditions) (`loads=[]` for none). The element size defaults to the part's
longest side over 60 (over 30 with `element='tet10'`, the 10-node element: see [Elements and accuracy](#elements-and-accuracy)); a solid part that this would give more than a million tetrahedra (250 000 with `'tet10'`) gets the larger element at which it has that many -- a cube 0.4 mm on a side has 1 063 776 and solves in 5.7 s, where it had 2 678 400 and 18.6 s (the number of tetrahedra is estimated, and is right to a few percent) -- and a size that is given is used as it is, with no limit. It raises `FeaError` if the problem cannot be solved as given (no support touching the
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

**Every result is a field.** A result of an analysis or of an optimisation goes wherever a shape goes (`offset(result, 1)`,
`difference(part, result)`, `result * 2`, the part of the next analysis), and it is its own field there:

| Result of | is the field |
|---|---|
| `topology_optimization` (of a structure, or of a body in a flow) | the final design (the shape it made) |
| `static_analysis` | the von Mises stress |
| `modal_analysis` | the displacement of the first mode |
| `thermal_analysis` | the temperature |
| `fluid_analysis` | the speed |

`result.fields` lists every field the result calculated, as `{name: field}` (`shape` and `density` for a topology optimisation,
`mode_1`, `mode_2`, … for the modes, `von_mises`, `displacement`, `ux` … for a static analysis): keep it in a variable
(`stress = result.fields['von_mises']`) or use the attributes by those names (`result.pressure`). What the result is made visible
as -- the optimisation's iterations that can be played back, the flow's streamlines -- is how it is shown when it is stated on its own;
passing it on does not change that, and it can still be shown whole.

## Modal analysis

```python
modes = modal_analysis(part, supports=[fixed(base)], material=aluminium, modes=6, element_size=3)
print(modes.frequencies)            # Hz, lowest first
modes.modes[0]                      # shown: the first mode shape, deformed (play: it vibrates)
```

`modal_analysis(shape, supports, material, modes=6, element_size=None, bounds=None,
tolerance=1e-6, cache=True, element='tet')`. Only the supports: no loads, the
material's E and density are used. Each
`Mode` has `.frequency` and the shape as fields (`displacement`, `ux`, `uy`, `uz`, scaled so the largest
movement is 1 — a shape, not an amplitude). A mode stated on its own is shown deformed, and the result card
steps the vibration through a cycle (24 phases; play to see it vibrate); the modal result itself shows its
first mode. Use the mode shape as a field: stiffen the part where the first mode moves most.

## Thermal analysis

Steady-state conduction. Boundary conditions are regions, like supports and loads, an input for each kind:

| Function | Input | |
|---|---|---|
| `fixed_temperature(region, T)` | `fixed_temperatures` | The part is held at T inside the region. |
| `heat_input(region, watts)` | `heat_inputs` | A total power spread over the part's surface inside the region (negative removes heat). |
| `heat_generation(region, watts)` | `heat_generations` | A total power generated through the part's volume inside the region (a heater, potted electronics). |
| `convection(region, h, ambient=20)` | `convections` | The exposed surface inside the region exchanges heat with the ambient. `h`: still air ≈ 5–25·10⁻⁶, forced air 25–250·10⁻⁶, water 500–10 000·10⁻⁶ W/(mm²·K). |

At least one fixed temperature or convection is needed (otherwise the temperature is undetermined).

```python
result = thermal_analysis(part,
                          fixed_temperatures=[fixed_temperature(base, 20)],
                          heat_inputs=[heat_input(chip, 5.0)],
                          heat_generations=[],
                          convections=[convection(fins, 25e-6, ambient=20)],
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
temp = thermal_analysis(part, [...], [...], [], [...], material=aluminium)
stress = static_analysis(part, supports=[fixed(base)],
                         loads=[thermal_expansion(temp.temperature, reference=20)], material=aluminium)
```

A part at `temperature` (a field, e.g. a thermal result's, or a number) expands by the material's
coefficient for each degree above `reference` (where it is stress-free). Held parts are stressed; free ones
just grow. Static analysis only.

## Topology optimization

`topology_optimization(analysis, constraints, iterations=100, filter_radius=None, keep=None, avoid=None, grow=0.0, extrude=None,
penalty=3, sharpness=16, symmetry='auto', tolerance=1e-5, cache=True)` finds the layout of material, inside the part of an analysis,
that is best for what you ask of it. **The analysis is the problem**: its part is the design space, and its supports, loads and
material are what the design is made for. Anything the analysis measures can be the goal or a condition, as in
[parametric_optimization](#parametric-optimization):

```python
stress = static_analysis(part, supports=[fixed(base)], loads=[force(lug, (0, 0, -300))], material=steel, element_size=2)

stiffest = topology_optimization(stress, constraints=[minimize('compliance'), 'volume_fraction <= 0.3'])
lightest = topology_optimization(stress, constraints=[minimize('mass'), 'max_von_mises <= 120', 'max_displacement <= 0.4'])
```

**`constraints`** is one list. `minimize(x)` makes x as small as it can be and `maximize(x)` as large (at most one of them; without
one the design only has to keep the conditions). The conditions are comparisons in a text -- `'volume_fraction <= 0.3'`,
`'max_von_mises <= 90'`, an exact value `'frequencies[0] == 100'` (kept to within a thousandth), a range `'80 <= frequencies[0] <= 120'`
-- or functions of the result that are 0 or less when kept. x is a **measure** of the design: the name of one of its numbers, a formula of
them (`'mass + 0.01 * compliance'`) or a function of the result (`lambda r: r.max_von_mises / r.mass`). What a design measures:

| Measure | |
|---|---|
| `volume_fraction`, `volume`, `mass` | the share of the part's volume the design uses (`1` is all of it), its volume, its mass |
| `compliance` | from a static analysis: the work of the loads (lower is stiffer) |
| `max_von_mises`, `safety_factor` | from a static analysis: the peak stress (see below), and the material's yield over it |
| `max_displacement`, `ux(x, y, z)`, `uy(...)`, `uz(...)`, `displacement(...)` | from a static analysis: the largest displacement, and the displacement at a place |
| `frequencies[i]` | from a modal analysis: the natural frequency (Hz) |

A name the design does not have is refused with the list of those it has. Because the problem is the analysis, a stress limit, a
frequency or a displacement need no switch: `[stress, vibration]` makes the design for both, and `'frequencies[0] >= 80'` is one more
line in `constraints`.

**Several load cases.** Several loads in **one** analysis act together (and cancel where they oppose). To make a part for **several cases**
-- pushed *and* pulled, held here in one use and there in another -- give a **list of analyses** of the same part: each is simulated completely
separately, and the design is made for all of them (the compliances of the cases are added up, a limit is kept in each one):

```python
push = static_analysis(part, supports=[fixed(base)], loads=[force(lug, (0, 0, -300))], material=steel)
pull = static_analysis(part, supports=[fixed(base)], loads=[force(lug, (0, 0, +300))], material=steel)
design = topology_optimization([push, pull], constraints=[minimize('compliance'), 'volume_fraction <= 0.3'])
```

| Option | |
|---|---|
| `keep` | regions (a shape or list) that must stay solid — bolt bosses, mounting faces. The material around supports and loads always stays. What the part has **excluded** (`exclude()`, [Fields](fields.md#excluded-regions-exclude)) is added by itself, however many operations came after it: those places stay exactly as they are |
| `avoid` | regions that must stay empty |
| `grow` | `False` (default: the design stays inside the part) or `True`: **the design is not limited to the part at all** -- it starts as the part, puts material anywhere and takes it away anywhere, wherever that makes it best. `volume_fraction` is then of the part's own volume (`1` spends the same material, `1.2` spends 20 % more); nothing is added behind the face of the part that a support or a load acts on -- over the face, and past the plane of the face a few elements round its outline, where the wall (or the body that pushes) is -- and nothing else is forbidden: the supports hold that face only (the nodes on the surface of the mesh, not the slab of the part behind it that the region of the support takes in), so the load case is the one that was given, and on the part's side of the plane the design is as the stresses ask, also round the plate (a place that has no flat face, such as a bolt hole that carries a load, is kept clear as the place itself). The supports and loads act on the part itself -- its surface and what lies inside it -- and never on the walls that the design space has round those places. Every design space of a run is meshed on the lattice of the first, so that the thin hole a place has beside the part is resolved in all of them (a mesh that bridged it with tetrahedra gave material held to the part that is not the part's, and drew as a comb of triangles in front of a support). **The design space follows the design**: it starts as the part and a thin skin round it, and the moment material reaches the edge of it the run stops, the space is made again -- the part, its skin and everything within about four elements of the material -- and **the same run** goes on in it, with the design, the sharpening, the step size and the iteration it had, so the design is never held against a wall while it runs. Only the iterations stop the growth (if they run out with the design still at the edge of its space, the output says so and the part is cut there). A design that does not reach the edge is done in the first space. What it costs is what its material costs, not what a shell of some thickness round the whole part would |
| `grow_limit` | mm, with `grow=True`: the most the design may reach out from the part -- **a restriction on top of `grow`, never the base of it**; left out, nothing limits how far the design grows |
| `extrude` | `'x'`, `'y'`, `'z'`: the same design along that axis (a profile to extrude, or to cut right through) |
| `filter_radius` | the smallest member size, mm (default 1.5 elements) |
| `iterations` | at most this many (default 100); it stops sooner when the design has settled. **It sets the size of the steps too** -- there is no step to choose: how far a density may move in one iteration starts large (the fewer the iterations, the larger, so a short run still goes the whole way), is smaller as they go (so the design has settled by the last), grows while the objective improves as the sensitivities predicted, and a step that makes the design worse is taken back and halved |
| `symmetry` | `'auto'` (default), `None`, `'x'` / `'y'` / `'z'` or several (`'xz'`), or `{'z': 0.0}` with the plane's position. **A symmetric problem gets a symmetric design.** Left to itself an optimiser does not keep a symmetric problem symmetric: the mesh of a symmetric part is never exactly symmetric, the small difference grows, and one of two members that do the same job takes the other's material -- a strut on one side and none on the other. With `'auto'` the planes through the middle of the part (x, y, z) are tried: when the part, its supports, its loads (what acts on one side is what acts on the other, mirrored) and the `keep` / `avoid` regions are all symmetric about one, the design is kept symmetric about it (the output says which). `None` leaves the design alone; `'z'` asks for the plane through the middle of the part whatever the loads; a dict gives a plane of your own. `result.symmetry` says what was used |
| `sharpness` | how crisp the design is (default 16). The density is pushed towards 0 and 1 more and more as the iterations go on, up to this much -- reached at three quarters of the iterations, however many there are (a Heaviside projection; it waits while a condition is broken by more than a hundredth, so a design is not made crisp while it is still being moved): the part ends up solid or empty, not grey, and nothing wanders in at the end. `1` leaves the density as the filter makes it: soft edges, a third of the part grey, and the shape then depends on where the threshold is cut |
| `penalty` | the power the density is raised to in the stiffness (default 3) |

**The filter smooths the design only.** An element that is held -- solid (the material round a support or a load, a `keep` region) or empty (an `avoid` region) -- is not a variable of the design, and it does not lend its density to the elements round it: counted in the filter, a support's solid elements put density into every element within the filter radius whatever the stress there said, and material that no stress reached stayed beside the held region. Beside a held region the design is what the stress asks for.

**How it works.** The kernel calculates every measure with its derivative to the density of every element (the compliance and the
frequencies from the solution, the displacement and the stress with one more solve each), differentiates the formulas, and moves the
design with the method of moving asymptotes on the goal and every condition. A design that keeps the conditions is better than one
that does not; the design given back is the best one of the last, crisp stage. **The stress** is each element's von Mises stress, relaxed at low density -- an element
that is `x` of solid reports its stress times `x` to the power `penalty - 2.5` (0.5 for the default), so a foam of grey elements, which would
meet a limit with a layout that cannot be made, costs more material than a crisp one -- and the peak is the p-norm of them brought to the peak.

**A stress limit is only as good as the mesh.** The stress is calculated on the elements of the design (`element_size`): a member that is only one or two elements across is not resolved, and its stress -- and the stress of the cut part analysed on the same mesh -- can be several times what a finer mesh gives (one cut bracket gave 483, 404 and 73 MPa at element sizes 5, 3.5 and 2.5). A stress limit wants a mesh in which the members are three elements across or more, and the part can be checked on a finer one: `best.verify(element_size=2.5)`. When no layout can keep the limits -- or when the layout the optimiser finds breaks one on the finer mesh -- the result is the best design it found, and the output says which limit the part breaks: that is an answer, not a failure.

**Thin parts.** The filter radius (default 1.5 elements) is the smallest feature the design can make: a wall a few elements thick has its density graded across it, and the part is cut at the density that keeps the design's volume, so a wall that is grey through its thickness comes out thinner than it was. Where the density of a wall lies in a plateau near the level of the cut, a mesh at the element size cannot follow the surface, and the part analysed again can have noticeably less material than the design (the angle bracket `Bracket.step`, walls about 2.5 elements thick, asked for half of its volume: the part came out with 41 %). Use an `element_size` of a third of the thinnest wall, or less.

**Loose material is taken out.** From the tenth iteration on, material (density above 0.3) that no path of material joins to an element that holds a support
(two elements that share a face are joined) carries nothing and is removed as the optimisation goes. Left alone, an island of material grows from the stress at the corner
of a support into the empty space beside the part -- material that is of no use, using the volume of a member that would be.

**The optimised part is cut from the design** at the density that keeps the volume of the design (the density is partly grey, so cutting at 0.5
would keep far less). If the part cut there is in several pieces, or its members are thinner than the mesh of the analysis can follow (the supports do not hold it, a load
does not touch it), **the cut is lowered, a little at a time, to the first density at which it is one piece that can be analysed**, and the output says which cut it
used and how much of the part it keeps, next to the design's own share. The analysis of that part (`.analysis`: its numbers, `best.max_von_mises`, are the true ones) is made for you, and the table
says what was asked, at the first iteration, at the last, and **as the optimised part has it** -- the design's own numbers are the optimiser's, those of the part are what you
get. When a limit is broken by the part, the output says so. A design whose members are about as thin as the filter (three elements or so: a truss found on a coarse mesh) is
in pieces at the cut that keeps its volume, and is cut lower to be one piece: the part then keeps more than the volume asked, and the output says how much -- a smaller `element_size`
(or a larger `filter_radius`) gives members the mesh can follow.

The optimisation solves the analyses 30–100 times. Returns a `TopologyResult`:

- `.density` — a field, 0 (no material) to 1 (solid);
- `.shape(threshold=None)` — the optimised part (the cut described above; `threshold=0.5` cuts the density in the middle). Loose scraps in the result are what the
  part would become with more volume. If it cannot be made one piece (`.pieces`: pieces joined only by a link thinner than about a millimetre count as separate), the
  output says so and suggests more material -- kept regions (`keep=`, `exclude()`) use up part of the volume asked for, so the free material has less of it;
- `.analysis` (the analysis of the optimised part, a list for several), `.values` (what was asked, as the part has it), `.feasible`, `.table()`;
- `.history` (the goal per iteration), `.densities` (the density field after each iteration), `.volume_fraction`, `.iterations`, `.seconds`;
- `.verify(threshold=None, element_size=None)` — the analyses of the optimised part again (a smaller `element_size` for a finer check).

A design that **grew** was found in several design spaces, one after the other: its history (`.history`, the table) is all of them joined, and its density steps are those of all of them while the script runs. Read back from the cache, the result has the whole history but the steps of the last design space only.

The result stated on its own shows the optimised part coloured by the density, and the result card **steps
through the iterations**: the part as it was after each one (a slider with play / pause). Each iteration's surface is
made from the optimisation's own densities -- a few tens of milliseconds, in the background as soon as the result is there, and
the same after the result was read back from the cache -- so stepping does not mesh a field for every iteration; the
last step is the real mesh.

**Watching it go.** While an optimisation runs, the viewport shows **the design after every iteration** (in place of the
pictures of the run before): the part where the density is above the level that keeps the volume asked for, so the first
iterations show the whole part and the structure appears as the material is taken away. It is **the result's own picture**:
the same shapes the result is made of, rendered by the same code -- the part cut from the density, coloured by the density, with the
colour bar and the result card -- so what you watch is what you get. A card at the top says how it is going: the
iteration, **the goal** (with the change since the first iteration) and a small graph of it, the **material** the design has
against what you asked for, and the **biggest change** of any element's density in the iteration. How to read it: the goal
should improve quickly and then level off; the material should close in on what you asked for; the change should shrink towards 0
(the optimisation stops by itself once it has, after the sharpening has finished). If the goal does not improve, or the part
stays whole or goes empty, the input is off: check the supports and the loads (a load on a support, a region that holds no part
of the model), the limits, and the element size. The picture is only a picture -- nothing is computed from it -- and it
goes when the optimisation ends and the result takes its place. (The flow topology optimisation does too, below.)

**Topology optimization of a thermal analysis** (`topology_optimization(sink, constraints=[...])`, `sink` the result of a `thermal_analysis` with tetrahedral elements) designs the material of the part
for what is asked of its temperature, with the same optimiser, the same list of constraints and the same options as a structure (`keep`, `avoid`, `extrude`, `symmetry`, `iterations`, `filter_radius`, `sharpness`):

```python
sink = thermal_analysis(plate, [fixed_temperature(base, 20)], [heat_input(chip, 10.0)], [], [], aluminium, element_size=1.0)
best = topology_optimization(sink, constraints=[minimize('input_temperature'), 'volume_fraction <= 0.3'])
best.shape()                           # the layout of 30 % of the plate that keeps the chip coolest
```

The numbers the formulas can read: `input_temperature` (the temperature the heat comes in at -- the mean temperature of the heat inputs, weighted by how much comes in at each place: the usual goal of a heat sink), `max_temperature` and
`min_temperature` (the peaks of the temperature of the nodes, as p-norms brought to the peak), `max_heat_flux`, `temperature(x, y, z)`, and the design's `volume_fraction`, `volume` and `mass`.
The held temperatures are the sinks, the heat inputs and generations the sources: the elements that hold a temperature or carry a heat input (over a surface, at a point) stay solid, a heat generation is
a condition of the design space, as in the classic volume-to-point problem (its heat is made where its region is, material or not, and the conductors have to reach it; the analysis of the optimized part makes it
in the part only), and material that nothing joins to a held temperature or a convecting surface is taken out. The conductivity of an element is the material's times its density to the power `penalty` (3 by default, a thousandth for an empty element). Convection acts on the surface of the part
as it is meshed (the faces of the design space), as in the analysis -- not on the surface of the holes the design makes inside it. `extrude=` gives extruded or pin-fin sinks. Every number has its sensitivity to the density of
every element from one adjoint solve. The result (`ThermalTopologyResult`) has `.density`, `.shape()`, `.analysis` (the thermal analysis of the optimized part: `best.max_temperature` is its true number), `.values`, `.history`
and `.verify()`.

## Fluid flow

Incompressible, laminar flow of a fluid **around a body**, inside a domain -- steady or in time, on the same tetrahedra that
follow the surface as the other analyses. `fluid_analysis` takes the body, the domain, the three kinds of boundary conditions and the fluid, and solves the flow once, around the body as it is: it is to a body in a flow what
`static_analysis` is to a structure -- the analysis that `topology_optimization` designs it for:

```python
slab = box((0, 0, 0), (80, 40, 4))                           # a thin slab of water, open at its sides...
post = cylinder(5, 6, (24, 20, -1))                              # ...with a round post standing in it
faces = union(box((-1, -1, -1), (81, 41, 0.01)), box((-1, -1, 3.99), (81, 41, 5)))
sides = union(box((-1, -1, -1), (81, 0.01, 5)), box((-1, 39.99, -1), (81, 41, 5)))
flow = fluid_analysis(post, slab,
                      inlets=[inlet(box((-1, -1, -1), (0.01, 41, 5)), speed=4.0)],       # Re = U D / nu = 40
                      outlets=[outlet(box((79.99, -1, -1), (81, 41, 5)), pressure=0)],
                      boundaries=[slip(faces), slip(sides)],          # a two-dimensional flow (the post is a wall at rest: so is everything not named)
                      fluid=water, element_size=1.0)
print(flow)                              # the flows, the pressure drop, the Reynolds number, the drag on the post
flow                                     # shown: the speed on the fluid, streamlines, particles moving along them
```

**The domain is a shape**: the fluid is where its field is negative, with the body's place in it -- the fluid is the domain with
the body cut out (`difference(domain, body)`). The body is a wall at rest, like every surface in no region, unless a `wall(...)` moves
it; the force on it is the force on the walls, `flow.wall_force`. The boundary conditions are regions on the fluid's surface, like
the supports and loads of a static analysis, an input for each kind (`inlets`, `outlets`, `boundaries` for walls and slips):

| Function | |
|---|---|
| `inlet(region, velocity=(vx, vy, vz))` / `speed=` / `flow_rate=` | The fluid comes in through the surface inside `region`: a velocity (mm/s; the direction and the mean speed over the inlet), a mean speed along the inward normal, or a flow rate (mm³/s). `profile='uniform'` (a plug with the no-slip rim) or `'developed'` (the fully developed profile of that cross-section, parabolic in a round pipe). The speed or flow rate is matched exactly on the mesh. |
| `outlet(region, pressure=0)` | The fluid leaves at that pressure (MPa). Put it where the flow leaves parallel to the walls (the "do-nothing" condition lets a developed flow out undisturbed). |
| `wall(region, velocity=(0, 0, 0))` | A moving wall, no-slip. Every surface in no region is a wall at rest, so `wall()` is for moving walls (a lid, a belt) and for naming a wall whose force is wanted. |
| `slip(region)` (`symmetry`) | A symmetry plane or frictionless wall: nothing flows through it, the fluid slides along it. |

`Fluid(name, density, viscosity)` in t/mm³ and MPa·s (water: `1.0e-9`, `1.0e-9`; air, oil and glycerol are
predefined). `gravity=(gx, gy, gz)` (mm/s²) is a body force. A closed domain (no outlet: a lid-driven cavity) is
allowed; its pressure is then relative, zero at one point.

`fluid_analysis(body, domain, inlets, outlets, boundaries, fluid=water, element_size=None, bounds=None, gravity=None, stokes=False,
tolerance=1e-5, cache=True, time=None, store_every=1)`. The element size defaults to the longest
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

`topology_optimization` designs a **body in a stream** when it is given the result of a `fluid_analysis`: the body of the analysis is the design it starts from, its
fluid domain is the space the body may fill, and what is asked is asked as for every other analysis -- a list of comparisons and at most one goal:

```python
flow = fluid_analysis(post, slab, inlets=[...], outlets=[...], boundaries=[...], element_size=1.5)
best = topology_optimization(flow, constraints=[minimize('drag_force'), 'volume_fraction == 1.0'], extrude='z')
wing = topology_optimization(flow, constraints=[maximize('lift_force / drag_force'), 'volume_fraction <= 0.4'])
```

| The numbers the constraints read | |
|---|---|
| `drag_force`, `lift_force` | the force of the flow on the body along the flow and across it, N. The flow's direction is that of the inlets' `velocity=`, or from the middle of the inlets to the middle of the outlets; the lift direction is the one across it that is most nearly Z (Y for a flow along Z) |
| `volume_fraction`, `volume` | the share of the body's own volume the design uses, and its volume (mm³) |

A ratio, a sum or any formula of them is read the same way, and `==` keeps a number to within a thousandth from both sides (`'volume_fraction == 1.0'` keeps the
body's volume). `keep` / `avoid` give regions that stay solid (a shaft, a mounting) and fluid (everything but a box round the post, so that the body is reshaped only
where it is), what the body has excluded (`exclude()`) is kept by itself; `extrude='x'`, `'y'`, `'z'` makes the body the same all along that axis (a 2D shape in a 2D
flow); `filter_radius` (mm, default 1.5 elements) is the smallest feature; `sharpness` how crisp the design is. **`symmetry='auto'`** (default), `None`, `'x'` /
`'y'` / `'z'` or several (`'xz'`), or `{'y': 20.0}` with the plane's position: a symmetric problem gets a symmetric body -- when the domain, the body, the regions and the
boundary conditions are all symmetric about a plane the flow runs along, the body is kept symmetric about it and the output says which; `None` leaves the body alone.
**`iterations`** is at most how many design updates (default **80**): the design is sharpened a little at every iteration over the first three quarters of the run (and
waits while a condition is broken), so a flow wants about that many -- a run stops sooner only when the design has stopped changing after the sharpening.

**Watching it go.** While it runs, the viewport shows the flow of the optimiser's model **after every iteration, as the result shows it**: the fluid around the body
coloured by the speed, with the streamlines from the inlets and the particles moving along them, the colour bar and the result card, and the body solid. A card says how it is
going: the iteration, the objective with its change since the first iteration and a graph of it, the body's volume, and how far the design moved in the iteration.

Returns a `FlowTopologyResult`: `.shape(iteration=None)` the optimised body (or the body after iteration k), `.fluid_shape()` the fluid around it, `.level` and `.levels` the
body as a level set (mm, positive inside it) and the same after each iteration, `.drag` and `.lift` (N) per iteration as the optimiser's model sees them (the body a friction
in the flow), `.flow` a `FluidResult` of the **real** flow around the final body (the body a wall at rest, the same conditions: fields, numbers, `streamlines()`) with
`.real_drag` and `.real_lift` (the mean over several places of the mesh lattice: a mesh meets a body at some place and the force depends on it, so one mesh is good to the spread of the places, `.real_drag_sd` is the standard deviation of one place, `.real_drags` the forces), `.model_flow` the optimiser's own flow (`.model_flow.steps[k]` the flow around the body of iteration k), `.volume` (mm³ of the final body),
`.values` what was asked as the final body has it in the real flow, `.feasible`, `.history` (the objective per iteration), `.table()`, `.pieces` (in how many pieces the body is), and what the real flow has --
`best.drag_force`, `best.pressure_drop`, `best.speed`. Stated on its own it shows the body in the flow: the fluid coloured by the speed with streamlines and particles over it,
the body solid; the result card steps through the iterations.

**How it is driven.** The design is **one density for each tetrahedron** (1 solid, 0 fluid) on the optimiser of the structural designs: filtered over `filter_radius`
(the held elements do not lend their density), sharpened towards 0 and 1 as the run goes, averaged over the planes of symmetry and the lines of an extrusion, and moved by the method
of moving asymptotes on the sensitivities of every number the constraints read. An element's **friction** follows its density linearly, `alpha = alpha_max rho` with `alpha_max = mu / (0.1 h^2)`. (Borrvall and Petersson's convex interpolation was tried: a grey
element is then mostly fluid, so the optimiser parks volume as a cloud of grey that costs no drag, and each sharpening step takes the cloud away at once; the sharpening goes up a
little at every iteration instead, and waits while a condition is broken.) The flow is
the real one -- the Navier-Stokes equations at the Reynolds number the inlet gives -- and the force on the body is the momentum the flow loses in the friction. Each iteration
solves the flow once and, for each force the constraints read, one adjoint: the transposed Newton Jacobian of the converged flow (convection included), on the factorisation
that the flow's own solve made, and the derivative of the residual in each element's friction, differenced element by element. The body is cut from the density at the level that keeps
the design's own volume (counted exactly: the density is linear in each tetrahedron). `FIELDES_TOPO_FDCHECK=<iteration>` checks the derivatives against two evaluations
a little off the design. The steps shown for the iterations are the optimiser's own flow, in which the solid is a friction and a little fluid creeps through it; their
streamlines are cut where they would enter the body. The final step is the real flow.

**How far the numbers can be trusted is the mesh's.** The optimiser's flow and the real flow around the final body are both solved on a mesh of tetrahedra of `element_size`, and a
body that is only a few elements thick is not resolved by it. Measured (2026-10-24) on a lens of the post's area (28 mm long, 4.3 mm at its widest, in example 17's slab): the real drag
of that one lens, moved along the flow in steps of 0.2 mm, ranges from 4.06e-7 to 4.75e-7 N on elements of 2 mm (15 % of the mean, repeating with a period of 1 mm) and, in steps of
0.15 mm, from 4.03e-7 to 4.26e-7 N on elements of 1.5 mm (5.5 %); at one place it is 4.08e-7 N on 2 mm elements, 4.15e-7 on 1.5 mm and 4.53e-7 on 1 mm. A difference between two bodies
that is smaller than that is not a difference of the bodies: look at the real flow around the final body (`.flow`), with an `element_size` that puts four elements or more across the
thinnest part of the body, and compare bodies only on a mesh that resolves them. `fluid_analysis` says when the passages are under four elements across.
The lens is the body of least drag of the family a post can be turned into (two circular arcs of the post's area, pointed ends, the length scanned from 20 to 36 mm), so a body with a lower real drag than the best lens would mean the solver is wrong. Measured (2026-10-24, five places along the flow on 2 mm elements, three places on the finer ones): the optimiser's body against the best lens, -6 % (1.4 sigma of the two means) on 2 mm elements, +2.1 % (0.8 sigma) on 1.5 mm, +1.3 % (0.6 sigma) on 1.0 mm -- on 2 mm elements the position error hides the answer, on the finer ones the body is not below the lens. The drag of one body depends on where the lattice meets it in y and z as well as along the flow: the same lens on 1.2 mm elements gives 4.34e-7 N on the lattice of the domain and 4.07e-7 N on one with planes through y = 20 and z = 2 (4.07e-7 and 4.42e-7 N on 1.5 mm), so a single mesh is not an answer to better than about 8 %.

**Symmetric and repeatable.** A design that is kept symmetric is paired element by element with its mirror image (exact pairs; the lines of an `extrude=` counted from the plane outward) and put back on the symmetric side after every step, whatever the mesh is: the optimiser's body is 0.0 % asymmetric in area, on 2 mm and on 1.5 mm elements (before: 14-20 %). The mesh of a problem whose shape is symmetric about a plane through the middle of its region is made on one side of the plane and mirrored where the result passes a check (every volume positive, every boundary vertex on the shape's surface); where a plane meets the surface the half mesh has a corner, and its vertices are moved onto the surface before the mirror is made; the mirror holds about both planes of the slab with a post or a lens, and about all three of the optimiser's, on 1.5 mm elements (the lift force of the flow that example 17 draws, `.flow`, is 1e-11 of its drag), and a plane that fails the check is dropped and the plain lattice is used for it (`FIELDES_TET_DEBUG=1` says which planes held) -- so the flow around a symmetric body is as symmetric as its mesh: exactly where the mirror holds, to a few percent of the force across the flow where it does not. Nothing in the flow solver reads a clock or the number of cores: the same problem is solved with the same iterations, and the optimiser run twice on the same input gives the same bits.

Limits: the volume a design is asked to keep can be parked where the optimiser's model charges little drag for it -- a piece of an element or two lets the flow through its friction,
and the real flow around it costs more -- and is then a piece of its own (`.pieces` counts them and the output says so; a finer `element_size` holds the volume in one body);
the flow sees the body as a porous solid whose wall lies a fraction of an element inside the design's wall (refine to tighten), so the optimiser's own drag differs from the real
body's (`.flow` gives the real one); laminar flow only; at a Reynolds number where the wake sheds (above about 50 for a post) there is no steady flow to optimise, and the
optimiser works on the unstable symmetric one.

## Parametric optimization

`parametric_optimization` finds the best numbers for the `var()`s of a model, over an analysis of it. Write the numbers that
may change as `var(...)` (they are the numbers you can drag), make the analysis from them as usual, and give the result of the
analysis to the optimisation with the range of each variable and what is to be made best:

```python
width = var(20)
height = var(12)
beam = box((0, 0, 0), (100, width, height))
wall = fixed(box((-1, -1, -1), (1, 60, 60)))
pull = force(box((99, -1, -1), (101, 60, 60)), (0, 0, -200))
stress = static_analysis(beam, supports=[wall], loads=[pull], material=aluminium, element_size=4)

best = parametric_optimization(stress, [(width, 8, 30), (height, 6, 24)],
                               constraints=[minimize('mass'), 'max_von_mises <= 90', 'max_displacement <= 0.5'])
```

It is in the **Engineering** menu, directly (it works over an analysis of any domain, so it is in none of the categories): select a
result and choose it, and the call is written with `variables=...` and `constraints=...` for you to fill in.

| Argument | |
|---|---|
| `analysis` | the result of an analysis made in this script and made of the `var()`s -- a static, modal, thermal or flow analysis. |
| `variables` | `(var, low, high)`: the `var()` that is changed and the range it is looked for in; a list of them for several. |
| `constraints` | what the design is for -- **one list**, as in [topology_optimization](#topology-optimization). **The goal**: `minimize(x)` makes x as small as it can be, `maximize(x)` as large (at most one) -- x is **a measure** of the result: the name of one of its numbers (`'mass'`, `'max_von_mises'`, `'max_displacement'`, `'compliance'`, `'safety_factor'`, `'volume'`, `'max_temperature'`, `'pressure_drop'`, `'frequencies[0]'` ...), an expression of them (`'mass + 0.01 * compliance'`: numbers, `+ - * / **`, `[index]`, `abs`, `min`, `max`, `sqrt`, `log`, `exp`), or a function of the result (`lambda r: r.max_von_mises / r.mass`); a name the result does not have is refused with the list of those it has. **What the best design keeps**: comparisons in a text (`'max_von_mises <= 90'`, `'frequencies[0] >= 80'`, an exact value `'frequencies[0] == 100'`, a range `'80 <= frequencies[0] <= 120'`), or functions of the result that give how much the limit is broken (0 or less when it is kept). A limit is kept when it is broken by less than a millionth of its size (an exact value: within a thousandth). Without a goal the search stops at the first design that keeps them. |
| `max_evaluations` | how many designs it tries at most (default 20, and 10 for each variable). |
| `tolerance` | it stops when it has closed in to this fraction of the ranges and nothing better comes (default 0.2 %). |
| `apply` | `True` writes the best numbers into the `var()`s of the text when the run is over, the way dragging does. Otherwise they hold for the run only: the model, and everything after the call, is the best design, and the numbers in the text are as you wrote them. |

**What it does.** Each design the search tries is an ordinary analysis -- the model is made again from the numbers, meshed and
solved -- so every kind of analysis works, with its own element and mesh, and its result is kept like any other: running the
script again, or another search over the same designs, finds them in the cache. The search needs **no derivatives**: it solves a
spread of designs over the ranges, learns from all of them how the objective and each constraint move with the variables
(radial basis functions through the designs), and solves the design that those surrogates say is best and keeps the constraints,
and again, closing in on the best design near the best one found, and on a bound or a limit when that is where it is; a design that
keeps the constraints always comes before one that does not. A design the mesh cannot hold (a wall thinner than the elements, a
part that falls apart) is listed as failed, and the search goes on without it.

**The result** is the analysis of the best design: its fields and its numbers are those of `best` (`best.von_mises`,
`best.max_displacement`), the result card shows it, and it is exported as that analysis is. It also says what the search did:

| | |
|---|---|
| `best.values` | the best numbers, by the name of the variable in the script |
| `best.objective` | the best value of what was minimised or maximised |
| `best.feasible` | whether the best design keeps every constraint. When **no design does**, the result says so and is the one that breaks them least |
| `best.history` | every design tried, in order: its numbers, its objective, how far it breaks the constraints, what each constraint measured, and why it failed if it did |
| `best.result` | the analysis of the best design |
| `best.apply()` | writes the best numbers into the text (what `apply=True` does) |

The output pane lists every design (the ones that broke a limit are marked), so the whole search can be read. If **none of the numbers it looked at changed
with the variables**, it says so: the analysis is not made of them.

**Measured** (the beam above, 2026-10-15): 36 designs in 4.3 s give 8 x 14.3 mm, 0.0307 kg, bending 0.490 mm (the limit is 0.5) under 76.5 MPa (the limit is
90): the displacement limit decides, and the width is at its lower bound. Beam theory for a tip load gives 14.26 mm for 0.5 mm at 8 mm wide (4 F L^3 / E b h^3), 0.4 % less. A
grid of 156 designs over the same ranges (a design every 2 mm and 1.5 mm) finds 0.0323 kg, 5 % more, in 63 s. Maximising the first natural frequency of a bar of a given mass (a modal analysis, 26 designs, 14 s) finds 12.2 x 12.2 mm: the
square section, 12.17 mm by beam theory.

**Limits.** It changes `var()` numbers of the script, nothing else (a `var()` made inside a function of yours cannot be changed from outside it); the model must be made of them -- it says when it is not.
It finds a good design in a few tens of analyses, not a proven best one: a problem with several equally good designs ends in one of them, and the answer is as good as the analysis: the peak
stress of a mesh jumps by a few percent from one design to the next (a corner that has a singularity), so a limit on it is kept within that, and an optimum that sits on such a limit
is found within it. Refine `element_size` (or use `element='tet10'`) where it matters. A search is as long as its analyses: forty designs of a modal analysis take forty modal analyses.

## Elements and accuracy

| `element=` | |
|---|---|
| `'tet'` (default) | Linear tetrahedra that **follow the part's surface**: an unstructured mesh built on the shape's own field. Boundary vertices lie on the surface, sharp edges are followed to a fraction of an element. `element_size` is the edge length. Stiff in bending: refine (a cantilever four elements thick reads 96 % of beam theory, eight thick 99 %). |
| `'tet10'` | **10-node (quadratic) tetrahedra** on the same mesh: a node in the middle of every edge, so the displacement is a quadratic in each element and the stress is linear in it, and the edges on the surface are bent to follow it. `element_size` is the edge of the tetrahedra they are made on. **Much more accurate for the same element size** -- about three times the size of `'tet'` reads the same stress on a smooth part -- but **dearer on the same mesh**: 6 to 7 times as many unknowns, 14 times as many entries in the stiffness matrix (and the memory for it), and a solver that needs up to twice the iterations -- 15 to 45 times the time. Use it where the stress matters (a hole, a notch, a fillet) and the part's thin features do not set the element size (a lattice's struts do: there `'tet'` is the one). For `static_analysis` (loads, fields and conditions as with `'tet'`) and `modal_analysis`; topology optimisation, thermal and flow analysis have no 10-node version. The fields are on the nodes (the corners and the middles of the edges) and are drawn on each element cut into eight linear tetrahedra; the result's `elements` counts the 10-node elements. `export_fea` has no model of it yet. |
| `'hex'` | A voxel grid of cubes, `element_size` on a side, each a trilinear hexahedron with incompatible modes (bends without spurious shear). Its stiffness follows how much of the cube lies inside the part, so the surface is a staircase. |
| `'hex_basic'` | The same grid with the plain trilinear hexahedron (shear-locks in bending). |

**What the 10-node element costs and what it gives** (measured on this program's own problems, 2026-10-14; the *time* is the solver's):

| Problem | Linear (`'tet'`) | 10-node (`'tet10'`) |
|---|---|---|
| the cantilever beam: tip movement against beam theory | -2.0 % with 66,816 elements (14,000 nodes), 0.8 s | **-0.8 %** with **1,296** elements (2,500 nodes), **0.1 s** |
| the plate with a hole: stress concentration against Peterson (one eighth by symmetry) | -3.7 % with 963,000 elements, 22 s | **+0.4 %** with **45,000** elements, 13 s; -0.3 % with 127,000 elements, 41 s |
| a round bar bent by a tip load (a curved surface) | -1.0 % with 469,000 elements, 12 s | **+0.1 %** with **4,456** elements, **1.8 s** |
| the first bending mode of the beam, `modal_analysis` (converged: about 812 Hz) | 818 Hz with 66,816 elements, 23 s | **813 Hz** with 1,296 elements, 3.6 s |
| **the same mesh** (the beam, 66,816 elements) | 0.8 s | 35 s: 101,000 nodes instead of 14,000, 2,900 iterations instead of 1,200 |

A mesh that is too coarse for the feature is not saved by the extra nodes: 10-node elements of 1 mm on the plate's hole (four to its radius) read the stress 20 % **too high**, 0.7 mm
and 0.5 mm read it right. As with linear elements, **halve the size and see whether the peak moves**. A patch of uniform stress, a bar hanging by its weight and a bar held between its ends and heated
are reproduced exactly (the checks that were run on the element); a very fine 10-node mesh is refused with the memory it would want and how much is free, rather than run out of it.

Smaller elements are more accurate and slower. A reasonable workflow: solve coarsely to find the load path,
then halve `element_size` once and see whether the peak stress moved by more than a few percent. The solver
is a preconditioned conjugate gradient (multigrid for large voxel problems).

**Measured on the bracket of example 05** (linear elements of 2 mm, 445,000 tetrahedra, a pull of 2 kN on the lugs): the peak von Mises stress was 58, 62, 67 and 67 MPa with the mesh lattice at four places (a quarter of an element apart in x), and 41 MPa on one more, while the largest displacement was 0.0498 to 0.0501 mm (0.6 %). The stress at a corner or a fillet is the mesh's, the displacement and the compliance are not: the safety factor of the example reads between 1.4 and 1.6 (it read 2.3 on the mesh of an earlier version), and a peak is to be trusted to no better than that before the element size is halved or `element='tet10'` is asked for.

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
- Linear tetrahedra need refinement in bending; check convergence (the 10-node element, `element='tet10'`, needs far less -- and is dearer on the same mesh).
- The 10-node element is for static and modal analysis. Its result card peak is read at the nodes, where the elements round a node are averaged; the finite-element model for another solver is not exported.
- Fields as properties and loads work with the tetrahedral elements only; the flow solver takes numbers.
- A stress singularity (a sharp re-entrant corner or a point load) does not converge with refinement; the
  peak there means nothing. Load over a region.
- The mesh follows the **field** of the part. For an imported part with poorly fitted B-spline faces (see
  [STEP import](step-import.md#free-form-b-spline-faces)) that is the fitted surface, except inside an `exclude()`d region, where the part is its exact surface (a field of the
  STEP file's own mesh) and the analysis mesh follows that.
- Fluid flow: laminar, steady, incompressible, isothermal; no boundary-layer elements (see [Fluid flow](#fluid-flow)).
- FielDes is a design tool, not a certified solver. Do not use it as the sole basis for safety decisions.

## Verification: four known answers

**FielDes is not validated for safety-critical use.** It is a design tool: the result card says so, and the limits are above. What is checked is the program, against four
problems whose answers are known, in `tests/verification/known_answers/`. Each script solves its problem, prints the program's number next to the exact one, and **stops with an error
when they differ by more than its tolerance** -- run them after a change to the solvers, the elements or the caches (`python scripts/run_example.py
tests/verification/known_answers/v1_cantilever_beam.py`). They do not use the result cache, so they always solve.

| Script | The problem | The exact answer | FielDes (as measured 2026-10-14) | Tolerance |
|---|---|---|---|---|
| `v1_cantilever_beam.py` | a steel bar 100 x 10 x 10 mm held at one end, 100 N across the other | tip movement by Timoshenko beam theory, 0.2016 mm | 0.1975 mm, **−2.0 %** (67,000 tetrahedra, 1 s); 10-node: **−0.8 %** (1,300 elements, 0.1 s) | 5 % and 2 % |
| `v2_plate_with_hole.py` | a plate 80 x 40 x 3 mm with a hole of 8 mm, pulled with 100 MPa (one eighth of it is solved, by symmetry) | stress concentration 3.14 (Peterson, for the width of the plate) | 3.02, **−3.7 %** (960,000 tetrahedra, 18 s); 10-node: **+0.4 %** (45,000 elements, 13 s) | 6 % and 3 % |
| `v3_channel_flow.py` | water between two plates 10 mm apart, mean speed 5 mm/s, developed inlet | speed at the middle 1.5 x the mean; pressure gradient 12 mu U / H^2 | **−0.3 %** and **+0.2 %** (22,000 tetrahedra, 1 s) | 3 % and 6 % |
| `v4_slab_conduction.py` | an aluminium bar held at 100 C and 20 C at its ends, the sides insulated | a straight line in temperature, Fourier's heat flux | 0.14 K and **−0.5 %** (5,000 tetrahedra) | 0.3 K and 2 % |

Linear tetrahedra are stiff in bending and average a stress over each element, so the first two come out a little low and come nearer as the elements get smaller -- slowly (the beam: -5.6 % with 4 elements across its depth, -1.0 % with 16; the plate: -9 % with 1 mm elements, -8 % with 0.5 mm, -4 % with 0.25 mm), so the stress at a notch is the number to be most careful with: it comes out **low**. The 10-node elements (`element='tet10'`) do much better at the same size, and the first two scripts check them too (the beam -0.8 %, the plate +0.4 %). The two others are exact for what linear elements can represent. Four cases do not make a validation: **compare any result with a hand calculation and, where it matters, a test,
before you rely on it.**
