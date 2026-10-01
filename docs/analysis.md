# Analysis

FielDes solves linear finite-element problems directly on the shapes of the script — an imported STEP
part, a lattice, anything — and hands the results back as **fields**, so they can colour, offset, thicken or
fill the part they came from.

- [Units and conventions](#units-and-conventions)
- [Regions: supports and loads are shapes](#regions-supports-and-loads-are-shapes)
- [Materials](#materials)
- [Static structural analysis](#static-structural-analysis)
- [Reading a result](#reading-a-result)
- [Modal analysis](#modal-analysis)
- [Thermal analysis](#thermal-analysis)
- [Thermal stress](#thermal-stress)
- [Topology optimization](#topology-optimization)
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
model tree) to check that they touch the part where you mean them to.

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

## Static structural analysis

```python
result = static_analysis(bracket,
                         supports=[fixed(plates)],
                         loads=[force(lugs, (0, -2000, 0))],
                         material=aluminium,
                         element_size=4)
print(result)
print("safety factor: %.1f" % result.safety_factor)
result.show("von_mises")
```

`static_analysis(shape, supports, loads, material=steel, element_size=None, bounds=None,
max_iterations=20000, tolerance=1e-6, cache=True, element='tet')`. The element size defaults to the part's
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

**Showing it.** `result.show(field='von_mises', range=None, deformation='auto')` displays the part coloured by
a field and drawn deformed (`'auto'`: the largest movement is 5 % of the part's size; a number is a scale
factor, `1` true size; `0` undeformed). In the viewport the [result card](interface.md#the-result-card)
switches the field, magnifies the deformation, and shows the **elements**. Hover to read the value
under the cursor. `result.deformed(scale)` is the part moved by its displacements, as a shape.

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
modes.modes[0].show()               # the first mode shape, deformed
```

`modal_analysis(shape, supports, material, modes=6, element_size=None, bounds=None, max_iterations=100,
tolerance=1e-6, cache=True, element='tet')`. No loads: the material's E and density are used. Each
`Mode` has `.frequency`, the shape as fields (`displacement`, `ux`, `uy`, `uz`, scaled so the largest
movement is 1 — a shape, not an amplitude) and `.show(field, deformation)`. Use the mode shape as a field:
stiffen the part where the first mode moves most.

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
result.show()                        # coloured by temperature
```

`ThermalResult` has `temperature`, `heat_flux` (magnitude, W/mm²), `qx`, `qy`, `qz`, and
`max_temperature`, `min_temperature`, `max_heat_flux`, `heat_in`, `heat_out` (W: through fixed temperatures
and by convection — they balance), `.range(field)`, `.show(field, range)`. The conductivity comes from the
material, or `conductivity=` overrides it.

## Thermal stress

```python
temp = thermal_analysis(part, [...], material=aluminium)
stress = static_analysis(part, supports=[fixed(base)],
                         loads=[thermal_expansion(temp.temperature, reference=20)],
                         material=aluminium)
```

A part at `temperature` (a field, e.g. a thermal result's, or a number) expands by the material's
coefficient for each degree above `reference` (where it is stress-free). Held parts are stressed; free ones
just grow. Static analysis only.

## Topology optimization

`topology_optimization(part, supports, loads, material, volume_fraction=0.3, element_size=None,
iterations=60, filter_radius=None, keep=None, avoid=None, extrude=None, penalty=3, move=0.2, bounds=None,
max_iterations=20000, tolerance=1e-5, cache=True, element='tet')` finds the **stiffest** layout that uses
`volume_fraction` of `part` (the design space) for the given supports and loads.

| Option | |
|---|---|
| `keep` | regions (a shape or list) that must stay solid — bolt bosses, mounting faces. The material around supports and loads always stays |
| `avoid` | regions that must stay empty |
| `extrude` | `'x'`, `'y'`, `'z'`: the same design along that axis (a profile to extrude, or to cut right through) |
| `filter_radius` | the smallest member size, mm (default 1.5 elements) |
| `loads` | a list of loads, **or several load cases** `[[force(a, …)], [force(b, …), gravity()]]`: the part is made stiff for all of them (the sum of the compliances is minimised) |

The optimisation solves the analysis 30–60 times. Returns a `TopologyResult`:

- `.density` — a field, 0 (no material) to 1 (solid);
- `.shape(threshold=None)` — the optimised part. The default threshold keeps the volume fraction you asked
  for (the density is partly grey, so cutting at 0.5 would keep far less);
- `.compliance` (per iteration — lower is stiffer), `.volume_fraction`, `.iterations`, `.seconds`;
- `.verify()` — a static analysis of the optimised part (a list when there are several load cases).

**Thermal topology optimization** (`thermal_topology_optimization(part, boundary, material,
volume_fraction, …, element='hex')`) finds the layout that keeps the heat coolest where it enters: the
heat-weighted mean temperature of the heat inputs is minimised. Heat inputs (`heat_input`,
`heat_generation`) are required; fixed-temperature regions are the sinks; convection follows the design
(give the region as the air around the design space and the fins grown into it are cooled). `extrude=` gives
extruded or pin-fin heat sinks — use it for air cooling: with one coefficient everywhere a closed pocket
deep inside would count as cooled like an open face. It designs on the voxel grid only. The result
(`ThermalTopologyResult`) has `.density`, `.shape()`, `.temperature` (per iteration) and `.verify()`.

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
for modal, thermal and optimisation runs. `cache=False` solves every time. See
[Caching and performance](caching-and-performance.md).

## Limitations

- Linear elasticity: small strains, linear isotropic material, **no contact**, no plasticity, no buckling,
  no dynamics beyond natural frequencies. Always compare the peak stress with the material's yield strength
  *and* with a hand calculation.
- Linear tetrahedra need refinement in bending; check convergence.
- A stress singularity (a sharp re-entrant corner or a point load) does not converge with refinement; the
  peak there means nothing. Load over a region.
- The mesh follows the **field** of the part. For an imported part with poorly fitted B-spline faces (see
  [STEP import](step-import.md#free-form-b-spline-faces)) that is the fitted surface; `exclude()` only
  changes the display and the STL.
- FielDes is a design tool, not a certified solver. Do not use it as the sole basis for safety decisions.
