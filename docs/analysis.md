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

Every condition is a **model of its own** -- a variable with a row in the model tree, with its eye and its bin -- and every **kind** of
condition is an **input of its own** of the analysis that takes it. A simulation is given the body (its first argument) and one list for
each kind of condition it takes; a kind it has none of is `[]`:

| Simulation | Its inputs for conditions (each a list, all of them required) |
|---|---|
| `static_analysis(part, supports, loads, material=steel, ...)` | `supports` (`fixed(...)`), `loads` (`force(...)`, `gravity(...)`, `thermal_expansion(...)`) |
| `modal_analysis(part, supports, material=steel, ...)` | `supports` |
| `topology_optimization(part, supports, loads, material=steel, ...)` | `supports`, `loads` (or several load cases, see [Topology optimization](#topology-optimization)) |
| `thermal_analysis(part, fixed_temperatures, heat_inputs, heat_generations, convections, material=aluminium, ...)` | those four |
| `thermal_topology_optimization(part, fixed_temperatures, heat_inputs, heat_generations, convections, ...)` | the same four |
| `fluid_analysis(body, domain, inlets, outlets, boundaries, fluid=water, ...)` | those three (`boundaries` takes `wall(...)` and `slip(...)` items, one kind or both); the flow goes around the `body`, inside the `domain` |
| `flow_topology_optimization(body, domain, inlets, outlets, boundaries, fluid=water, ...)` | the same three |

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
| cyan place, with cyan pads and "Sliding (fixed in ...)" | sliding support (`fixed(region, y=False)` and the like) |
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

**Making them.** Right-click empty space -> **Add simulation** -> **New support or load** (fixed, force, gravity, thermal
expansion), **New thermal condition** (fixed temperature, heat input, heat generation, convection), **New flow condition** (inlet,
outlet, wall, slip) writes one, **with a placeholder where it acts** (`force_1 = force(region=..., vector=..., profile=None)`: like any argument that is missing, that statement waits -- the rest of the script runs -- and the placeholder row of the
model tree takes a body, a field or a surface dropped on it -- the menu makes no box of its own for a region). **Select the regions first** -- surfaces, fields or bodies -- and the same entries write the condition for what is selected:
`fixed_1 = fixed(selection_1)`, or `force(a, b, (0, -100, 0))` for several. **Every condition takes any number of regions, one argument after the other**, and in the model tree a body, a surface or a field dropped on a
condition is one more of them. **A force is written with its `vector`** (0, -100, 0 newtons to begin with), where it is changed. **Simulation -> static_analysis** (and modal, topology optimization, thermal, fluid) writes the simulation of the body selected, **with the conditions selected written in the input of their kind** and a placeholder
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

`topology_optimization(part, supports, loads, material, volume_fraction=0.3, element_size=None,
iterations=100, filter_radius=None, keep=None, avoid=None, grow=0.0, extrude=None, penalty=3,
sharpness=16, symmetry='auto', bounds=None, tolerance=1e-5, cache=True, element='tet')` finds the **stiffest**
layout that uses `volume_fraction` of `part` (the design space) for the supports and loads.

**Several load cases.** `loads` can be a list of lists, each a **load case with its own loads**: the part is made stiff for all
of them at once (the compliances of the cases are added up). That is how a part that is pushed *and* pulled is optimised. Each case may be held differently too -- `supports`
is then a list of lists, one for each case (tetrahedral elements):

```python
push = force(lug, (0, 0, -300))
pull = force(lug, (0, 0, +300))
design = topology_optimization(part, supports=[fixed(base)], loads=[[push], [pull]], volume_fraction=0.3, element_size=2)
```

The conditions have no body in them, so they cannot disagree about it; the body is the first argument. Each case needs a support that touches the part.

| Option | |
|---|---|
| `keep` | regions (a shape or list) that must stay solid — bolt bosses, mounting faces. The material around supports and loads always stays. What the part has **excluded** (`exclude()`, [Fields](fields.md#excluded-regions-exclude)) is added by itself, however many operations came after it: those places stay exactly as they are |
| `avoid` | regions that must stay empty |
| `grow` | mm, default 0 (the design stays inside the part). With `grow=5` the part may also **thicken outwards** by up to 5 mm wherever that makes it stiffer: sections that are too thin grow, material that carries nothing goes. The design starts as the part and `volume_fraction` is of the part's own volume (`1` spends the same material, `1.2` spends 20 % more); the part is not grown round the supports and loads, which stay where they are. Use it when the part you have is not a good enough design space |
| `extrude` | `'x'`, `'y'`, `'z'`: the same design along that axis (a profile to extrude, or to cut right through) |
| `filter_radius` | the smallest member size, mm (default 1.5 elements) |
| `iterations` | at most this many (default 100); it stops sooner when the design has settled. If it stops at the limit while still improving, the output says so: raise `iterations`. **It sets the size of the steps too** -- there is no step to choose: how far a density may move in one iteration starts large (the fewer the iterations, the larger, so a short run still goes the whole way), is smaller as they go (so the design has settled by the last), and within that grows while the compliance falls as the sensitivities predicted and shrinks when it does not |
| `symmetry` | `'auto'` (default), `None`, `'x'` / `'y'` / `'z'` or several (`'xz'`), or `{'z': 0.0}` with the plane's position. **A symmetric problem gets a symmetric design.** Left to itself an optimiser does not keep a symmetric problem symmetric: the mesh of a symmetric part is never exactly symmetric, the small difference grows, and one of two members that do the same job takes the other's material -- a strut on one side and none on the other. With `'auto'` the planes through the middle of the part (x, y, z) are tried: when the part, its supports, its loads (what acts on one side is what acts on the other, mirrored) and the `keep` / `avoid` regions are all symmetric about one, the design is kept symmetric about it (the output says which). `None` leaves the design alone; `'z'` asks for the plane through the middle of the part whatever the loads; a dict gives a plane of your own. `result.symmetry` says what was used |
| `sharpness` | how crisp the design is (default 16; tetrahedral optimisations). The density is pushed towards 0 and 1 more and more as the iterations go on, up to this much -- reached at three quarters of the iterations, however many there are (a Heaviside projection): the part ends up solid or empty, not grey, and nothing wanders in at the end. `1` leaves the density as the filter makes it: soft edges, a third of the part grey, and the shape then depends on where the threshold is cut |
| `loads` | a list of loads, **or several load cases** `[[force(a, …)], [force(b, …), gravity()]]`: the part is made stiff for all of them (the sum of the compliances is minimised) |

**Loose material is taken out.** From the tenth iteration on, material (density above 0.3) that no path of material joins to a support or a load (two elements that
share a face are joined) carries nothing and is removed as the optimisation goes. Left alone, an island of material grows from the stress at the corner of a support into the empty
space beside the part -- material that is of no use, using the volume of a member that would be.

The optimisation solves the analysis 30–100 times. Returns a `TopologyResult`:

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
through the iterations**: the part as it was after each one (a slider with play / pause). Each iteration's surface is
made from the optimisation's own densities -- a few tens of milliseconds, in the background as soon as the result is there, and
the same after the result was read back from the cache -- so stepping does not mesh a field for every iteration; the
last step is the real mesh. `element='hex'` optimisations keep only the final density.

**Watching it go.** While a tetrahedral optimisation runs, the viewport shows **the design after every iteration** (in place of the
pictures of the run before): the part where the density is above the level that keeps the volume asked for, so the first
iterations show the whole part and the structure appears as the material is taken away. It is **the result's own picture**:
the same shapes the result is made of, rendered by the same code -- the part cut from the density, coloured by the density, with the
colour bar and the result card -- so what you watch is what you get. A card at the top says how it is going: the
iteration, the **compliance** (with the change since the first iteration) and a small graph of it, the **material** the design has
against what you asked for, and the **biggest change** of any element's density in the iteration. How to read it: the compliance
should fall quickly and then level off; the material should close in on what you asked for; the change should shrink towards 0
(the optimisation stops by itself once it has, after the sharpening has finished). If the compliance does not fall, or the part
stays whole or goes empty, the input is off: check the supports and the loads (a load on a support, a region that holds no part
of the model), the volume fraction, and the element size. The picture is only a picture -- nothing is computed from it -- and it
goes when the optimisation ends and the result takes its place. (The voxel `element='hex'` optimisation does not draw itself yet; the flow
topology optimisation does, below.)

**Thermal topology optimization** (`thermal_topology_optimization(part, fixed_temperatures, heat_inputs, heat_generations,
convections, material, volume_fraction, …, element='hex')`) finds the layout that keeps the heat coolest where it enters: the
heat-weighted mean temperature of the heat inputs is minimised. Heat inputs (`heat_input`,
`heat_generation`) are required; fixed-temperature regions are the sinks; convection follows the design
(give the region as the air around the design space and the fins grown into it are cooled). `extrude=` gives
extruded or pin-fin heat sinks — use it for air cooling: with one coefficient everywhere a closed pocket
deep inside would count as cooled like an open face. It designs on the voxel grid only. The result
(`ThermalTopologyResult`) has `.density`, `.shape()`, `.temperature` (per iteration) and `.verify()`.

## Fluid flow

Incompressible, laminar flow of a fluid **around a body**, inside a domain -- steady or in time, on the same tetrahedra that
follow the surface as the other analyses. `fluid_analysis` takes what `flow_topology_optimization` takes (the body, the domain, the
three kinds of boundary conditions, the fluid) and solves the flow once, around the body as it is: it is to
`flow_topology_optimization` what `static_analysis` is to `topology_optimization`:

```python
slab = box_exact((0, 0, 0), (80, 40, 4))                           # a thin slab of water, open at its sides...
post = cylinder_z(5, 6, (24, 20, -1))                              # ...with a round post standing in it
faces = union(box_exact((-1, -1, -1), (81, 41, 0.01)), box_exact((-1, -1, 3.99), (81, 41, 5)))
sides = union(box_exact((-1, -1, -1), (81, 0.01, 5)), box_exact((-1, 39.99, -1), (81, 41, 5)))
flow = fluid_analysis(post, slab,
                      inlets=[inlet(box_exact((-1, -1, -1), (0.01, 41, 5)), speed=4.0)],       # Re = U D / nu = 40
                      outlets=[outlet(box_exact((79.99, -1, -1), (81, 41, 5)), pressure=0)],
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

`flow_topology_optimization(body, domain, inlets, outlets, boundaries, fluid=water, objective='drag', volume=1.0, region=None,
keep=None, avoid=None, element_size=None, iterations=40, filter_radius=None, darcy=1e-5, extrude=None,
flow_direction=None, lift_direction=None, bounds=None, cache=True)` takes a **body in a stream** and changes its
shape, and its topology, to make it best for the force the flow puts on it. `body` is the solid as it is (a Shape),
`domain` the fluid domain it sits in (a Shape that holds the body's place too), `inlets`, `outlets`,
and `boundaries` the flow's conditions as for `fluid_analysis`. The flow is the real one -- the Navier-Stokes equations at
the Reynolds number the inlet gives -- with the body a friction that a density per element sets (1 solid, 0 fluid:
Borrvall & Petersson's penalised model), so the drag and the lift are the momentum the flow loses in the body.

| Option | |
|---|---|
| `objective` | `'drag'`: the least force along the flow; `'lift'`: the most force across it; `(w_drag, w_lift)`: w_drag·drag − w_lift·lift is minimised |
| `volume` | what the body may use of its own volume: `1.0` keeps it, `(0.5, 1.5)` bounds it, `None` on a side for no bound there |
| `region` | where material may be at all (a Shape; default the whole domain): the body shrinks, grows, moves and splits inside it |
| `keep` / `avoid` | regions that stay solid (a shaft, a mounting) / regions that stay fluid. What the body has excluded (`exclude()`) is kept by itself |
| `extrude` | `'x'`, `'y'`, `'z'`: the body is the same all along that axis (a 2D shape in a 2D flow) |
| `symmetry` | `'auto'` (default), `None`, `'x'` / `'y'` / `'z'` or several (`'xz'`), or `{'y': 20.0}` with the plane's position. **A symmetric problem gets a symmetric body.** Left to itself the optimiser does not keep it so: the mesh of a symmetric domain is never exactly symmetric, and the small difference grows into a crooked nose or tail. With `'auto'` the planes through the middle of the domain are tried: when the domain, the body, the `region` / `keep` / `avoid` regions and the boundary conditions are all symmetric about one (the flow along the plane), the body is kept symmetric about it -- the sensitivities and the level set are averaged with their mirror images at every iteration -- and the output says which. `None` leaves the body alone; a letter asks for the plane through the middle of the domain whatever the rest; a dict gives a plane of your own. `result.symmetry` says what was used |
| `flow_direction`, `lift_direction` | the drag and the lift directions (default: the inlets' mean direction, and perpendicular to it in the plane of the domain's two long axes) |
| `filter_radius`, `darcy`, `iterations` | the smoothing radius of the boundary's motion (mm, default 1.5 elements; the shape itself is never smoothed); the solid's permeability relative to the element (its friction is μ / (darcy·h²): the flow penetrates it by about √darcy elements, 0.1 by default); at most how many iterations (default 40): **the run stops sooner when the objective has stopped improving** (over the last five designs that were kept it fell by less than half a percent), and says so. It also sets the size of the boundary's steps: one element at most per iteration, half an element when 48 iterations or more are allowed, the same all the way (a step cap that shrank as the run went left the nose and the tail too little to move: the body ended round and stubby, and the last iterations changed nothing you could see); a step grows while the objective falls as the sensitivities predicted, and one that raises the objective is taken back and halved, so the objective never rises |

**Watching it go.** While it runs, the viewport shows the flow of the optimiser's model **after every iteration, as the result shows it**:
the fluid around the body (the domain with the body taken out) coloured by the speed, with the streamlines from the inlets and the particles
moving along them, the colour bar and the result card, and the body solid -- the same shapes, rendered by the same code. A card says how it is going: the iteration, the
**drag** (or the objective, when lift counts) with its change since the first iteration and a graph of it, the body's **volume** against what
is allowed, and how far the boundary moved in the iteration. The drag should fall and level off, the volume stay in its bounds, and the
boundary's moves shrink; steps taken back (the objective got worse) are not drawn. The picture goes when the optimisation ends and the result
takes its place.

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
reinitialisation), moved down the direction by the step (see `iterations`), and offset as a whole until the body's
volume is within its bounds (bisection: an offset of the level set is an offset of the boundary); a step that
raises the objective is taken back and halved, and the run stops when the objective stops improving or no step lowers
it any more (the result says which, and how many steps were taken back). The steps shown for the
iterations are the optimiser's own flow, in which the solid is a friction and a little fluid creeps through it;
their streamlines are cut where they would enter the body. The final step is the real flow. The linear solves of
every flow -- most of its time -- use a block incomplete-LU preconditioner, one block per core with a little
overlap, so they run on all cores.
The solid's friction is set by the element, μ / (darcy·h²): a solid far more impermeable than that is a wall already
when a sliver of it lies in an element, so the fraction then changes the flow only when it is nearly nothing and
the sensitivities mislead (the optimiser's own drag is therefore a little below the real body's; `.flow` gives the
real one). The boundary splits and merges as it moves; a hole does not open in the middle of solid.
`FIELDES_FLOW_FDCHECK=1` checks the nodal sensitivities by finite differences.

**Measured (2026-10-09)** on the round post of example 16 (10 mm across in a slab of water at Re 40, 1.5 mm
elements, the drag minimised with the volume kept, 24 iterations allowed): the optimiser's own drag falls from 8.72e-7 to
4.73e-7 N (46 % less); it stops after 22 iterations (the last five kept designs improved it by less than half a percent; two steps were
taken back), the volume is held to all digits, and the post becomes a slender body 22.5 mm long and 4.25 mm at its thickest, its
ends coming to a point (half a millimetre thick at the nose, in elements of 1.5); the real flow around that body (the body a
wall at rest) gives 4.03e-7 N against the round post's 8.18e-7 N: 51 % less drag. For comparison, in the same real flow a lens (two circular arcs, pointed ends) of the same
volume gives 4.58e-7 N at 19 mm long, 4.16e-7 at 24 mm, 4.43e-7 at 30 mm and 5.0e-7 at 40 mm: the optimiser finds the length by itself. With 40 iterations
allowed it stops after 32 (4.19e-7 real). A square post that nothing was tuned for: 7.9e-7 to 4.76e-7 N in the model, 4.10e-7 N in the real flow, stopped after 22 of 30. (The real
drag of a body differs by a few percent with how the mesh happens to cut it: differences smaller than that are noise.) The earlier schedule, whose step cap shrank as the run went, ended the same
runs at 4.5e-7 (a round-ended body 19 mm long) and ran to the limit. The nodal sensitivities agree with finite differences within
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
