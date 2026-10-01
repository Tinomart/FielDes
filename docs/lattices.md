# Lattices

A lattice in FielDes is a **field** that repeats through space, trimmed to a body. Every size in it —
wall thickness, strut radius, offset, relative density, cell size — can itself be a field, so a lattice can
follow a regression, a distance, a stress result, a temperature.

- [One call: lattice()](#one-call-lattice)
- [Kinds](#kinds)
- [Sizing: thickness, radius, density](#sizing-thickness-radius-density)
- [Skin, shell and fill()](#skin-shell-and-fill)
- [Cell maps: cells that follow the body](#cell-maps-cells-that-follow-the-body)
- [Driving a lattice from a field](#driving-a-lattice-from-a-field)
- [Building blocks](#building-blocks)
- [Your own cells and equations](#your-own-cells-and-equations)
- [Beams on surfaces, Voronoi and graphs](#beams-on-surfaces-voronoi-and-graphs)
- [Measuring a lattice](#measuring-a-lattice)
- [Tips](#tips)

## One call: lattice()

```python
part = sphere(30)
lat = lattice(part, 'gyroid', cell_size=8, thickness=1.0, skin=1.5)
```

`lattice(body, kind='gyroid', cell_size=10, thickness=None, radius=None, density=None, style='sheet',
offset=None, skin=0, region='volume', depth=None, cell_map=None, node_radius=None, blend=0, skin_blend=0,
wall=None, axis='z')` fills `body` with a lattice and returns a shape.

## Kinds

| Family | `kind` | Member size |
|---|---|---|
| **TPMS** (triply periodic minimal surfaces) | `gyroid`, `schwarz_p`, `diamond`, `neovius`, `lidinoid`, `split_p`, `iwp`, `frd`, `fischer_koch_s` | `thickness` (sheet) or `offset` (network) |
| **Struts** | `cubic`, `bcc`, `bccz`, `fcc`, `fccz`, `octet`, `octahedron`, `kelvin`, `diamond_struts`, `cross`, `tesseract`, `cuboctahedron` | `radius`, optionally `node_radius`, `blend` |
| **Planar** (2.5D, extruded along `axis`) | `hexagon` (honeycomb), `triangle`, `square`, `kagome` | `wall` |
| **Random** | `voronoi`, `stochastic` | `radius` |

TPMS `style`: `'sheet'` is a wall of `thickness` mm centred on the minimal surface ("walled TPMS");
`'network'` is the solid on one side of it, grown by `offset` mm ("skeletal TPMS"; `invert=True` on `tpms()`
takes the other side).

`cell_size` is in mm, or `(sx, sy, sz)` for a stretched cell. For the honeycomb it is the flat-to-flat width.

## Sizing: thickness, radius, density

Give either the member size or a **relative density** (0–1) — the lattice is then calibrated to fill that
fraction of its volume:

```python
lattice(part, 'octet', cell_size=10, density=0.2)
lattice(part, 'gyroid', cell_size=6, density=ramp(z_field(), (-30, 30), (0.1, 0.4)))   # density as a field
```

Any of `thickness`, `radius`, `offset`, `wall`, `node_radius`, `density` may be a number or a **field**,
e.g. a regression applied to the depth below the surface (`examples/09_lattice.py`):

```python
thickness = fit([(0, 1.6), (3, 1.2), (6, 0.8), (12, 0.6)], model='pchip')(depth_below(body))
graded = lattice(body, 'gyroid', cell_size=6, thickness=thickness, skin=1.0)
```

Thin walls need a fine mesh: a 0.6 mm wall wants a resolution of about 3 samples per mm or more
(`view.set_resolution(3)`).

## Skin, shell and fill()

| Option | |
|---|---|
| `skin` | a solid skin of this thickness (mm) on the body's surface; `skin_blend` rounds the joints to the lattice |
| `region='shell'`, `depth` | fill only the outer `depth` mm — a conformal lattice layer under the skin |
| `blend` | rounds the joints of struts |

`fill(body, lattice_field, skin=0, region='volume', depth=None, blend=0)` is what `lattice()` does
internally: it trims an infinite lattice (from `tpms()`, `strut_lattice()`, `planar_lattice()`, `periodic()`) to
a body.

## Cell maps: cells that follow the body

`cell_map=` bends the lattice's cells:

| | |
|---|---|
| `cartesian(origin, rotation)` | straight cells, optionally shifted and turned (degrees) |
| `cylindrical(origin, axis='z', cells_around=None, radius=None, rotation=None)` | cells wrap around an axis: radial, around, along |
| `spherical(origin, cells_around=None, radius=None, rotation=None)` | cells in shells around a point; shrink towards the poles |

Give the number of cells around (`cells_around=12`) or the radius at which cells are `cell_size` wide.

```python
lattice(part, 'gyroid', cell_size=8, thickness=1.0, cell_map=spherical(cells_around=16))
```

## Driving a lattice from a field

The reason the whole program exists. Any field can be a size:

```python
result = static_analysis(bracket, [fixed(plates)], [force(lugs, (0, -2000, 0))], material=aluminium)
peak = result.max_von_mises
density = ramp(result.von_mises, (0, 0.4 * peak), (0.15, 0.5))       # stress -> relative density
graded = lattice(bracket, 'gyroid', cell_size=8, density=density, skin=1.5)
colored(graded, density, label='lattice density (from stress)')
```

— dense where the part works hard, light where it does not (`examples/10_field_driven_design.py`). Other
drivers: `result.displacement`, a thermal result's `temperature`, a topology optimisation's `.density`, a
distance (`depth_below`, `distance_to_point`), a regression over measured data, `field_from_csv`, noise.

## Building blocks

| | |
|---|---|
| `tpms(kind, cell_size, thickness, style, offset, cell_map, invert, fast)` | an infinite TPMS field. `style='surface'` gives the signed distance to the minimal surface itself. `fast=True` is a quicker, slightly less exact distance (walls ≈ 5 % thin) |
| `strut_lattice(cell, cell_size, radius, node_radius, blend, cell_map)` | an infinite strut lattice of round beams along the edges of a unit cell |
| `planar_lattice(kind, cell_size, wall, axis, cell_map)` | honeycomb and grids |
| `periodic(shape, cell_size, cell_map, check=True)` | **any shape as a unit cell**: model it in the box from (0, 0, 0) to `cell_size`, and it repeats. Model it so it tiles — what leaves one face must come in on the opposite one; `check` samples the faces and warns where they do not match |
| `fill(body, lattice, …)` | trim to a body |

## Your own cells and equations

**A strut cell** from nodes and beams, coordinates 0–1 across the cell. Draw one part of a symmetric cell
and mirror it:

```python
star = unit_cell({'o': (0, 0, 0), 'c': (0.5, 0.5, 0.5), 'e': (0.5, 0, 0)},
                 [('o', 'c', 1.0), ('o', 'e')],          # a beam may carry its own radius (mm)
                 mirror='xyz')
print(star.check() or 'tiles')       # lists what would make it fall apart or not tile
lattice(body, star, cell_size=8, radius=0.6, skin=1.0)
```

`mirror` copies the beams across the cell's mid-planes (`'x'`, `'xy'`, `'xyz'`). `check()` reports nodes
outside the cell, zero-length beams, nodes on a face without a partner on the opposite face, and loose ends.

**A TPMS equation** of the three cell phases `a, b, c` (2π per cell), written with `+ - * /` and the
methods `.sin() .cos() .sqrt() .square()`:

```python
my_tpms = tpms_equation(lambda a, b, c: a.cos()*b.cos()*c.cos() - a.sin()*b.sin()*c.sin(), 'D-like')
lattice(body, my_tpms, cell_size=8, thickness=0.8, skin=1.0)
```

Its value is turned into a distance in mm by following its gradient, so `thickness=` is a real wall and
`density=` calibrates it like the built-in ones. See `examples/11_custom_lattice.py`.

## Beams on surfaces, Voronoi and graphs

| | |
|---|---|
| `surface_lattice(body, cell_size, radius, pattern='triangle'\|'voronoi', seed, blend, bounds, with_body)` | round beams on the body's surface (`with_body='inside'` keeps only the inner half; `'union'` adds the body) |
| `voronoi_lattice(body, cell_size, radius, style='voronoi'\|'delaunay', relax=2, seed, skin, blend, skin_blend, bounds, density)` | a random foam (or a stochastic truss); `radius` may be a field, `seed` picks the pattern |
| `graph_lattice(nodes, beams, radius, blend)` | beams along the edges of **any graph** (nodes `[(x,y,z)…]`, beams `[(i,j)…]`); `radius` a number, one per node, or a field (tapers along each beam) |
| `surface_graph`, `voronoi_graph`, `points_graph` | the graphs behind them, e.g. to print or modify |
| `LatticeGraph` | `.thicken(radius, blend)`, `.lengths()` |

The graph lattices are **cached by content**: a rebuilt graph is expensive, so running the script again
does not repeat it.

## Measuring a lattice

| | |
|---|---|
| `relative_density(lattice_field, cell_size, samples=40, origin)` | the share of one unit cell it fills (an untrimmed, Cartesian lattice) |
| `lattice_parameter_for_density(kind, cell_size, density, style, samples)` | the thickness / offset / wall / radius that gives a relative density (found by bisection) |
| `mass_properties(graded, density, lower, upper)` | the real volume and mass of the trimmed lattice |

## Tips

- A lattice is an **implicit field**: it costs nothing until it is meshed, then meshing cost grows with the
  surface area. Start with a coarse resolution to place the body, refine for the final look.
- `view.set_resolution` should resolve the thinnest wall at several samples. A lattice that disappears
  is a mesh that cannot see it: raise the resolution.
- To analyse a lattice, hand it to `static_analysis` like any shape — with a fine `element_size` (a few
  elements across a strut or wall).
- STL export of a lattice: `graded.save_stl('lattice.stl', lo, hi, resolution=4)`.
