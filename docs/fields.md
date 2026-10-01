# Fields and regressions

Every shape in FielDes is a **field**: a number at every point in space. A solid is where its field is
negative. But any other field — a distance, a ramp, the stress of an analysis, a regression of measured
data — is a shape too, and can be used anywhere a number or a shape is used. That is the point of the
program: *anything can drive anything*.

```python
part  = sphere(30)
depth = depth_below(part)                      # 0 at the surface, 30 at the centre
t     = ramp(depth, (0, 30), (2.0, 0.6))       # 2 mm walls at the skin -> 0.6 mm inside
lat   = lattice(part, 'gyroid', cell_size=8, thickness=t, skin=1.5)
```

Fields are evaluated lazily and exactly: nothing is sampled onto a grid (except where a function says it
is — `sample_grid`, `mass_properties`, `exact_distance`). Every function here is in the
[library reference](reference.md#fields).

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

and the arithmetic of [Scripting](scripting.md#arithmetic-on-fields) (`+ - * /`, `abs`, `min`, `max`, …).

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
