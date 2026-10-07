# Fields and regressions

Every shape in FielDes is a **field**: a number at every point in space. A solid is where its field is
negative. But any other field — a distance, a ramp, the stress of an analysis, a regression of measured
data — is a shape too, and can be used anywhere a number or a shape is used. That is the point of the
program: *anything can drive anything*.

```python
part  = sphere(30)
depth = depth_below(part)                      # 0 at the surface, 30 at the centre
t     = ramp(depth, (0, 30), (2.0, 0.6))       # 2 mm walls at the skin -> 0.6 mm inside
lat   = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=t, skin=1.5)
```

Fields are evaluated lazily and exactly: nothing is sampled onto a grid (except where a function says it
is — `sample_grid`, `mass_properties`, `exact_distance`). Every function here is in the
[library reference](reference.md#fields).

- [Fields everywhere](#fields-everywhere)
- [Points and surfaces](#points-and-surfaces)
- [Coordinates](#coordinates)
- [Distances](#distances)
- [Value maps](#value-maps)
- [Geometry from fields](#geometry-from-fields)
- [Exact distances](#exact-distances)
- [Measuring a part](#measuring-a-part)
- [Data as fields](#data-as-fields)
- [Regressions](#regressions)
- [Colouring by a field](#colouring-by-a-field)
- [Evaluating fields in a script](#evaluating-fields-in-a-script)

## Fields everywhere

**Wherever a number goes, a field goes.** This is the rule of the library: a size, a radius, a thickness, a spacing, a
blend, an offset, a scale — anything that you would write as `2.5` — can be a field instead, and the function evaluates it
where it needs it. `offset(part, 1.0)` grows a part by a millimetre everywhere;
`offset(part, ramp(z_field(), (0, 40), (0.2, 2.0)))` grows it by 0.2 mm at the bottom and 2 mm at the top. The two are
written alike; the second is not sampled onto a grid, not meshed first, and not slower to write.

```python
anchor = point(52, 8, 6)                         # a point: a model, and a value wherever a coordinate goes
swell  = ramp(distance_to_point(anchor), (0, 60), (4.0, 0.3))
grown  = offset(plate, swell)                    # 4 mm near the anchor, 0.3 mm far from it
joined = smooth_union(post, block, ramp(x_field(), (80, 140), (0.5, 8.0)))   # a blend that is sharp here and round there
foam   = lattice(core, cell_periodic('gyroid'), cell_size=ramp(x_field(), (160, 220), (16, 6)), thickness=1.0)
```

(`examples/18_fields_everywhere.py` runs these three.) In the [model tree](interface.md#the-model-tree) you do not have to type it:
**drag a field's row onto an operation** and it takes the place of one of the operation's numbers. Where it works, in short:

| | A field is accepted for |
|---|---|
| **Geometry** | every dimension of every primitive and every transform (`sphere`, `box`, `cylinder`, `move`, `rotate*`, `scale_*`, ...: they are tree arithmetic, a field is a tree); `offset`, `thicken`, `shell_*`, `offset_exact`, `shell_exact`, `round_edges`, `fillet` (the size), `smooth` (the radius), `smooth_union` / `smooth_intersection` / `smooth_difference` and `union_all` / `intersection_all` (the radius or blend), `repeat` (the spacing), `twist_z`, `bend_z` |
| **Fields** | `ramp`, `normalize` (the ranges), `attractor` (the radius, and a point or several points as the centre), `noise_field` (the scale and the amplitude), `distance_to_point`, `distance_to_plane`, `radial_field`, `angle_field`, `polar_field` (the centres, a point works too), `mass_properties` (the density) |
| **Lattices** | `thickness`, `radius`, `density`, `skin`, `blend`, `skin_blend`, and **`cell_size`**: a graded cell size blends lattices a factor of two apart (it may change by a factor of up to 16 over the part), for any periodic cell including `cell_custom` |
| **Analyses** | `Material(E=..., density=..., conductivity=..., expansion=...)` (the property at every point of the part), `force(region, ..., profile=field)` (how the total is spread over the surface), `fixed_temperature(region, field)`, `convection(region, h_field, ambient_field)`, `heat_input` / `heat_generation(..., profile=field)`: tetrahedral elements, see [Analysis](analysis.md#fields-in-analyses) |

**What can never be a field** — a number that is not a *length-like value at a place*: counts (`array_x(shape, 3, ...)`, the
number of modes, iterations, steps), resolutions and element sizes, tolerances, seeds, a Poisson's ratio or a yield
strength, the numbers of a flow (its fluid, inlets, outlets and walls: the flow solver takes numbers). A function says so
when it is given a field there (`TypeError: ... is a number`), never silently.
A **constant field through every numeric slot of the library gives exactly what the number gives** (checked on 98 slots).

The kind of thing a value is — a 3D shape, a 2D shape, a field, a surface, a point — is shown by its colour and icon in
the [model tree](interface.md#the-model-tree). A field is not a body, so **nothing of it is drawn in the viewport**: select it in
the model tree and the [field viewer](interface.md#the-field-viewer) opens on it by itself, painting a disc through the render
region with the field's value at every point -- move the disc to see the field in 3D.

## Points and surfaces

A **point** is a model: `anchor = point(52, 8, 6)` shows as a small ball, has a gizmo, and goes wherever a coordinate
goes — `distance_to_point(anchor)`, `attractor(anchor, 30)`, `radial_field(anchor, 'z')` — so a design can be driven from
a point you drag. `anchor.xyz` is its coordinates (they follow the gizmo). A point reads as its three coordinates **wherever a
position goes** -- `sphere(5, anchor)`, `move(part, anchor)`, `distance_to_line(anchor, (0, 0, 1))` -- and in the model tree you
can **drag a point onto an operation** to put it in the place of a position written in the call
(`distance_to_point((5, 5, 5))` → `distance_to_point(anchor)`).

A **surface** is the zero set of a field with no body behind it: `plane(point, normal)`, `sphere_surface(radius, center)`,
`cylinder_surface(radius, axis, center)`, `wave_surface(amplitude, period, axis, height)`. It is drawn as a thin sheet,
and the right-click menu makes them (*New surface*). A surface is a field like any other: `distance_to_surface(sheet)`,
or `lattice_surface_conform(sheet, ...)` for a lattice that follows it.

Both are in the right-click menu of the viewport: *New point* and *New surface* write the call where you clicked.

## Coordinates

| | |
|---|---|
| `x_field()`, `y_field()`, `z_field()` | the coordinates (mm) |
| `radial_field(center, axis)` | distance from an axis (cylindrical radius) |
| `angle_field(center, axis)` | angle around an axis, −π..π |
| `polar_field(center, axis)` | angle from an axis, 0..π (spherical coordinates) |

`ramp(z_field(), (0, 50), (2, 0.5))` is 2 at z = 0, falling to 0.5 at z = 50.

## Distances

| | |
|---|---|
| `distance_to_point(p)`, `distance_to_points(points)` | Euclidean distance to a point / the nearest of several |
| `distance_to_line(point, direction)`, `distance_to_segment(a, b)`, `distance_to_polyline(points, closed)` | to lines and curves |
| `distance_to_plane(point, normal)` | signed distance to a plane |
| `distance_to_surface(shape)` | unsigned distance to a shape's surface |
| `depth_below(shape)` | depth below the surface: 0 on it and outside, growing inward |
| `signed_distance(shape)` | the shape's field, by name |
| `attractor(points, radius, falloff='smooth', strength=1)` | `strength` at the points (or at a curve's distance field) falling to 0 at `radius` (`'linear'`, `'smooth'`, `'gauss'`) |

**How good a distance is a shape?** Primitives are exact. Booleans, blends and warps give fields that are
only *roughly* a distance (flat or square-cornered outside a box's edges). `gradient_magnitude(shape)` is
1 for an exact distance and far from 1 where it is not. Where it matters, `exact_distance(shape)` turns it
into a true one (see [below](#exact-distances)). Imported STEP parts are accurate near their faces.

## Value maps

| | |
|---|---|
| `ramp(field, (a, b), (va, vb), clamped=True)` (`remap_field`) | linear map, clamped by default |
| `clamp(field, lo, hi)` | limit |
| `normalize(field, lo, hi)` | [lo, hi] → [0, 1] |
| `lerp(a, b, t)` (`mix`) | interpolate; `t` may be a field |
| `smoothstep(field, e0, e1)`, `step_field(field, edge)` | smooth and sharp steps |
| `wave(axis, period, amplitude, phase)` | a sine along an axis |
| `noise_field(scale, octaves, seed, gain, lacunarity, amplitude)` | Perlin noise — `part - 0.3 * noise_field(4)` is an organic texture |
| `sum_fields(*fields)` | sum |
| `add_fields`, `subtract_fields`, `multiply_fields`, `divide_fields`, `power_field`, `min_fields`, `max_fields` | arithmetic on any number of fields (and numbers) at every point: `a + b`, `a - b`, `a * b`, `a / b`, `a ** b` as functions, in the model tree's **Operation → Field math** |
| `abs_field`, `negate_field`, `sqrt_field`, `square_field` | the same on one field |
| `field_from_body(body)` | the values of a body as a **field**: not a body, not drawn, free to be multiplied (a body itself always keeps its true scale) |
| `body_from_field(field, level=0)` | the other way: a **body** where the field is below `level` (its surface where it equals `level`): drawn, a part like any other |
| `low_level_field(logic)`, `low_level_body(logic)` | a field or a body from your own function of the coordinates, `logic(x, y, z)`, called once with x, y and z as fields: see [Low level](scripting.md#low-level-your-own-shapes-from-x-y-and-z). Right-click → New field → low_level_field writes the function and the call for you |
| `maximum(a, b, ...)`, `minimum(a, b, ...)` | the largest or smallest of several fields at every point (Python's `max` and `min` cannot compare fields) |

and the arithmetic of [Scripting](scripting.md#arithmetic-on-fields) (`+ - * / **`, `abs`, `min`, `max`, …): field × field and
2 ** field work like number × number, at every point.

## Geometry from fields

| | |
|---|---|
| `offset_by(shape, distance)` | offset by a distance that **may be a field**: `offset_by(part, 0.02 * stress)` |
| `thicken(field, thickness)` | a wall centred on the zero surface of a field (a plane, a TPMS, a skin); thickness may be a field |
| `shell_inside / shell_outside / shell_centered(shape, t)` | hollow shell inward, skin outward, or straddling the surface |
| `smooth_union / smooth_intersection / smooth_difference(a, b, radius)` | rounded booleans |
| `chamfer_union(a, b, size)` | 45° chamfered union |
| `union_all(shapes, blend=0)`, `intersection_all(shapes, blend=0)` | many shapes at once |
| `repeat(shape, spacing, center)`, `repeat_polar(shape, count, center, axis)` | infinite grid / polar repeat |
| `mirror_x / _y / _z(shape, x)` | mirror-symmetric copy |
| `twist_z(shape, degrees_per_mm, center)`, `bend_z(shape, radius, center)` | deformations (the rate may be a field) |

## Excluded regions: exclude()

`exclude(shape, region, ...)` locks the places of a shape that lie inside the regions (any shapes: where their field is
negative). It cuts the shape into **two fields that are always united again**: the *free field*, the shape outside the
region, which every later operation reshapes, and the *locked field*, the shape inside the region, which none of them
touches. It works on **every shape and every field** — a box, a lattice, a result of other operations. For a part
imported from a STEP file the locked field is the part's own exact surface, meshed directly from the file and made a
field, instead of the import's fit; see [STEP import](step-import.md#putting-the-exact-surface-back-exclude).

```python
part = exclude(part, sphere(12, (0, 0, 6)))       # inside the sphere the part is locked
light = shell(part, 2)                            # shelled everywhere else; the sphere's part is as it was
final = union(light, bracket)                     # the bracket is not added inside the sphere either
```

What an excluded shape does with the rest of the library:

| Operation | What happens |
|---|---|
| `offset`, `shell`, `thicken`, `shell_*`, `offset_by`, `offset_exact`, `shell_exact`, `round_edges`, `fillet`, `smooth`, `inverse`, `lattice`, `fill`, `topology_optimization` and the other topology optimisations | works on the whole shape, then the locked field is put back: inside the region the shape is as it was |
| `union`, `intersection`, `blend*`, `morph`, `smooth_*`, `chamfer_union`, `union_all`, `intersection_all` | the same, for the locked fields of every excluded shape given |
| `difference(a, b)`, `clearance`, `blend_difference`, `smooth_difference` | the locked fields of `a` (a tool that is excluded is only subtracted) |
| `move`, `rotate*`, `scale_*`, `reflect_*`, `handles()` | the whole thing moves: the locked field and its region go with the shape |
| `array_*`, `symmetric_*`, `repeat*`, `mirror_*`, `loft*`, `extrude_z`, `twist_z`, `bend_z`, `taper_*`, `shear_x_y`, `attract_*`, `repel_*`, `twirl_*`, `revolve_y`, `expose` | would separate the two fields (copy or deform them differently), so they raise an `ExcludedError` — do them first and `exclude()` the result |
| analyses, `colored`, `render_cache`, `lock`, queries (`volume_of`, `evaluate`, ...) | see the whole shape; `colored` and `lock` keep it excluded |

Raw field arithmetic (`a + b`, `a.min(b)`, `a.max(b)`) is not an operation of the library: it works on the whole field
and gives a plain shape. The table lives in `fieldes/stdlib/excluded.py`, and every
function of the library is in it.

## Exact distances

Offsets, shells and fillets of a shape with only a *rough* field (booleans, blends, deformations) come
out stretched at edges. The `_exact` versions mesh the shape and use the **true** distance to that
surface:

| | |
|---|---|
| `exact_distance(shape, bounds, resolution, margin)` | the shape with an exact signed distance (cached) |
| `offset_exact(shape, distance)` | uniform offset along true normals, round at edges |
| `shell_exact(shape, thickness, side='inside'\|'outside'\|'center')` | uniform hollow shell |
| `round_edges(shape, radius)` | rounds every convex edge and corner |
| `fillet(shape, radius)` | fills every concave edge and corner |
| `smooth(shape, radius, steps=1)` | smooths the surface without thickening the body: features smaller than about `radius` go, flat faces stay; more `steps` smooth further. A field like any other (a weighted sum of the body's field moved by whole radii: 6 copies for one step, 19 for two, 44 for three, 85 for four), so nothing is measured or meshed in it |

`resolution` (samples per mm; about 4 / radius or finer for `round_edges`) sets how finely the surface
is followed. The expensive part (the mesh and its distance structure) is [cached](caching-and-performance.md).

## Measuring a part

| | |
|---|---|
| `mass_properties(shape, density, lower, upper, resolution)` | a dict: `volume` (mm³), `mass` (density in g/cm³ → g), `centroid`, `fill_fraction`, `samples` |
| `volume_of(shape, lower, upper, resolution)` | volume in mm³ by grid sampling |
| `wall_thickness(shape, max_thickness=20)` | local wall thickness: the chord through the material along the normal |
| `overhang_angle(shape, build_direction)` | angle (degrees) between each downward-facing surface and the build plate: 0 = worst overhang, 90 = vertical or up |
| `overhang_mask(shape, limit=45, build_direction)` | 1 where a surface overhangs more than `limit`, else 0 |
| `curvature_field(shape, step)` | mean curvature (1/mm; positive where convex) |
| `normal_field(shape)`, `gradient_field(shape)`, `gradient_magnitude(shape)` | the unit normal and gradient as three fields / the gradient's length |
| `field_range(field, body, lower, upper, n=24)` | (min, max) of a field over a box (only inside `body` if given) |
| `evaluate(field, point_or_points)`, `sample_grid(field, lower, upper, n)` | numbers |

Colour a part by `wall_thickness` or `overhang_angle` and read values with the mouse
([Legends and probing](interface.md#legends-and-probing)); `examples/02_inspect_a_part.py` does exactly
that.

## Data as fields

Measured or simulated data become fields so they can drive geometry:

| | |
|---|---|
| `field_from_points(points, values, neighbours=8, power=2)` | inverse-distance-weighted average of the nearest `neighbours` samples; handles hundreds of thousands of samples; stays within the samples' range |
| `field_from_csv(path, x, y, z, value, neighbours, power, delimiter, scale)` | the same from a CSV (columns by name or index; `scale=1000` for metres → mm) |
| `interpolate_field(points, values, smoothing=0, method='rbf'\|'idw')` | passes through the samples (radial basis functions, up to a few hundred points, or IDW) |
| `fit_field(points, values, degree=2)` | least-squares polynomial in x, y, z → `FieldFit` with `.field`, `.r2`, `.rmse` |

Exported stresses or temperatures from another simulator, a pressure map, a CT scan's density: all are a
CSV away from being a field.

## Regressions

A regression is fitted to (x, y) data and then applied to numbers **or to fields** — typically a distance
field — giving a field that drives an operation.

```python
t_of_depth = fit([(0, 2.0), (5, 1.4), (15, 0.9), (30, 0.6)], model='pchip')
print(t_of_depth)                          # equation and goodness of fit
thickness = t_of_depth(depth_below(part))  # a field
```

`fit(x, y=None, model='poly', degree=2, clamp=True)` accepts `[(x, y), …]`, `(xs, ys)` or `{x: y}`;
`fit_csv(path, x_column, y_column, …)` reads a CSV. Models:

| `model` | |
|---|---|
| `'linear'`, `'poly'` (with `degree`) | a + bx, polynomial |
| `'exp'`, `'exp_offset'`, `'power'`, `'log'`, `'logistic'` | parametric models |
| `'interp'`, `'spline'`, `'pchip'` | straight lines, natural cubic spline, shape-preserving cubic (no overshoot between points — good for thicknesses) |
| `'auto'` | the best of the parametric models by corrected AIC |

A regression **holds its end values** outside the data range (`clamp=True`), so a field never runs off
to unphysical values. The result has `model`, `params`, `r2`, `rmse`, `equation`, `domain`, and `.table(n)`,
`.residuals()`.

## Colouring by a field

```python
colored(part, wall_thickness(part), range=(0.8, 3.0), label="wall thickness, mm")
```

`colored(shape, field, range=None, label=None, colormap='turbo')` makes the viewport paint the shape by the
field — geometry is unchanged. The colour bar appears as a legend bottom right; hover the model to read
the field's value. Analysis results and the STEP fit deviation use it.

## Evaluating fields in a script

A shape is callable: `part(x, y, z)` returns its field at a point. `evaluate(field, points)` does many at
once, and `sample_grid` gives a grid. See [Scripting](scripting.md#a-shape-is-a-field).
