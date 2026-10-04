# Lattices

A lattice in FielDes is a **field** that repeats through space, trimmed to a body. Every size in it —
wall thickness, strut radius, offset, relative density, cell size — can itself be a field, so a lattice can
follow a regression, a distance, a stress result, a temperature.

- [One call: lattice()](#one-call-lattice)
- [Cells: cell_periodic, cell_non_periodic, cell_custom](#cells)
- [Sizing: thickness, radius, density](#sizing-thickness-radius-density)
- [Skin, shell and fill()](#skin-shell-and-fill)
- [Cell maps: cells that follow the body](#cell-maps-cells-that-follow-the-body)
- [Driving a lattice from a field](#driving-a-lattice-from-a-field)
- [Building blocks](#building-blocks)
- [Your own cells and equations](#your-own-cells-and-equations)
- [Lattices that follow a surface](#lattices-that-follow-a-surface)
- [Beams on surfaces, Voronoi and graphs](#beams-on-surfaces-voronoi-and-graphs)
- [Measuring a lattice](#measuring-a-lattice)
- [Tips](#tips)

## One call: lattice()

```python
part = sphere(30)
lat = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=1.0, skin=1.5)
```

`lattice(body, cell='gyroid', cell_size=10, thickness=None, radius=None, density=None, style='sheet',
offset=None, skin=0, region='volume', depth=None, cell_map=None, node_radius=None, blend=0, skin_blend=0,
wall=None, axis='z')` fills `body` with a lattice and returns a shape. `cell` is what the lattice is made of: see
the next section.

## Cells

Every lattice operation — `lattice()`, `lattice_surface_conform()`, `strut_lattice()`, `tpms()`, `planar_lattice()`,
`lattice_parameter_for_density()` — takes a **cell**: the thing the lattice is made of. Three functions make one, and
they all make the same kind of object, so any of them goes wherever a cell is taken — and nothing else does: a name
such as `'gyroid'` is not a cell, `cell_periodic('gyroid')` is.

| Function | Makes | Examples |
|---|---|---|
| `cell_periodic(kind)` | a standard cell that repeats on a grid | `'octet'`, `'gyroid'`, `'hexagon'` |
| `cell_non_periodic(kind, relax=2, seed=1)` | cells that do not repeat: a random graph | `'voronoi'`, `'delaunay'` |
| `cell_custom(…)` | a cell of your own | nodes and beams, a TPMS equation, a shape that tiles |

```python
lattice(part, cell_periodic('octet'), cell_size=10, radius=0.6)
lattice(part, cell_non_periodic('voronoi', seed=4), cell_size=8, radius=0.5)
lattice(part, cell_custom(equation=my_equation), cell_size=8, thickness=0.8)
lattice_surface_conform(part, cell_periodic('truncated_octahedron'), depth=2, cell_size=5)
```

The cell is only *what* the lattice is made of. How thick it is (`thickness`, `radius`, `wall`, `offset`, `density`),
how big (`cell_size`) and where it goes (a body, a surface, a `cell_map`) belong to the operation.

**`cell_periodic(kind)`**

| Family | `kind` | Member size |
|---|---|---|
| **TPMS** (triply periodic minimal surfaces) | `gyroid`, `schwarz_p`, `diamond`, `neovius`, `lidinoid`, `split_p`, `iwp`, `frd`, `fischer_koch_s` | `thickness` (sheet) or `offset` (network) |
| **Struts** | `cubic`, `bcc`, `bccz`, `fcc`, `fccz`, `octet`, `octahedron`, `kelvin` (= `truncated_octahedron`), `diamond_struts`, `cross`, `tesseract`, `cuboctahedron` | `radius`, optionally `node_radius`, `blend` |
| **Planar** (2.5D, extruded along `axis`) | `hexagon` (honeycomb), `triangle`, `square`, `kagome` | `wall` |

**`cell_non_periodic(kind)`** — `'voronoi'` (the edges of Voronoi cells: a foam) or `'delaunay'` (the Delaunay edges: a
stochastic truss), about `cell_size` apart; `relax` makes the cells more even, `seed` picks another random pattern.
`thickness` is the beam thickness (diameter; `radius` is the same thing, half of it). In `lattice()` it fills a body; in `lattice_surface_conform()` it lays the graph on the
surface (its beams centred on it).

**`cell_custom(…)`** — see [Your own cells and equations](#your-own-cells-and-equations).

TPMS `style`: `'sheet'` is a wall of `thickness` mm centred on the minimal surface ("walled TPMS");
`'network'` is the solid on one side of it, grown by `offset` mm ("skeletal TPMS"; `invert=True` on `tpms()`
takes the other side).

`cell_size` is in mm, or `(sx, sy, sz)` for a stretched cell. For the honeycomb it is the flat-to-flat width.

## Sizing: thickness, radius, density

**One word for every cell: `thickness`.** It is the wall of a sheet TPMS and the diameter of the beams of a strut cell or a
non-periodic cell, in `lattice()`, `strut_lattice()` and `lattice_surface_conform()` alike (`radius=` is the same for beams, half of it;
giving both is an error). A number sets it everywhere, a field varies it.

```python
lattice(part, cell_periodic('octet'), cell_size=10, thickness=1.2)                        # beams 1.2 mm across
lattice(part, cell_non_periodic('voronoi'), cell_size=8, thickness=1.0)                   # a foam of 1 mm beams
lattice_surface_conform(part, cell_periodic('bcc'), 2, cell_size=5, thickness=1.1)         # beams 1.1 mm across on a surface
```

Give either the member size or a **relative density** (0–1) — the lattice is then calibrated to fill that
fraction of its volume:

```python
lattice(part, cell_periodic('octet'), cell_size=10, density=0.2)
lattice(part, cell_periodic('gyroid'), cell_size=6, density=ramp(z_field(), (-30, 30), (0.1, 0.4)))   # density as a field
```

Any of `thickness`, `radius`, `offset`, `wall`, `node_radius`, `density` may be a number or a **field**,
e.g. a regression applied to the depth below the surface (`examples/09_lattice.py`):

```python
thickness = fit([(0, 1.6), (3, 1.2), (6, 0.8), (12, 0.6)], model='pchip')(depth_below(body))
graded = lattice(body, cell_periodic('gyroid'), cell_size=6, thickness=thickness, skin=1.0)
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
internally: it trims an infinite lattice (from `tpms()`, `strut_lattice()`, `planar_lattice()`) to a body.

**Joining a lattice to a part.** `union(part, lat)` is a sharp union. `union(part, lat, radius=1.5)` blends the
surfaces where they meet with a smooth transition of that radius (default 0: sharp), the way a boolean's blend radius
does in other tools — the struts or walls grow into the part with a fillet instead of a crease. It works on any
field, so it joins custom lattices and conformal lattices too.

## Cell maps: cells that follow the body

`cell_map=` bends the lattice's cells:

| | |
|---|---|
| `cartesian(origin, rotation)` | straight cells, optionally shifted and turned (degrees) |
| `cylindrical(origin, axis='z', cells_around=None, radius=None, rotation=None)` | cells wrap around an axis: radial, around, along |
| `spherical(origin, cells_around=None, radius=None, rotation=None)` | cells in shells around a point; shrink towards the poles |

Give the number of cells around (`cells_around=12`) or the radius at which cells are `cell_size` wide.

```python
lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=1.0, cell_map=spherical(cells_around=16))
```

## Driving a lattice from a field

The reason the whole program exists. Any field can be a size:

```python
conditions = static_boundary_conditions(bracket, [fixed(plates)], [force(lugs, (0, -2000, 0))])
result = static_analysis(bracket, conditions, material=aluminium)
peak = result.max_von_mises
density = ramp(result.von_mises, (0, 0.4 * peak), (0.15, 0.5))       # stress -> relative density
graded = lattice(bracket, cell_periodic('gyroid'), cell_size=8, density=density, skin=1.5)
colored(graded, density, label='lattice density (from stress)')
```

— dense where the part works hard, light where it does not (`examples/10_field_driven_design.py`). Other
drivers: `result.displacement`, a thermal result's `temperature`, a topology optimisation's `.density`, a
distance (`depth_below`, `distance_to_point`), a regression over measured data, `field_from_csv`, noise.

## Building blocks

| | |
|---|---|
| `tpms(cell, cell_size, thickness, style, offset, cell_map, invert, fast)` | an infinite TPMS field (`cell`: a TPMS cell or its name). `style='surface'` gives the signed distance to the minimal surface itself. `fast=True` is a quicker, slightly less exact distance (walls ≈ 5 % thin) |
| `strut_lattice(cell, cell_size, radius, node_radius, blend, cell_map, thickness)` | an infinite strut lattice of round beams along the edges of a unit cell |
| `planar_lattice(cell, cell_size, wall, axis, cell_map)` | honeycomb and grids (`cell`: a planar cell or its name) |
| `fill(body, lattice, …)` | trim to a body |

## Your own cells and equations

`cell_custom` makes a cell of your own, of three kinds, and every lattice operation takes it like a standard cell.

**A strut cell** — `cell_custom(nodes, beams, mirror='')` — from nodes and beams, coordinates 0–1 across the cell.
Draw one part of a symmetric cell and mirror it:

```python
star = cell_custom({'o': (0, 0, 0), 'c': (0.5, 0.5, 0.5), 'e': (0.5, 0, 0)},
                   [('o', 'c', 1.0), ('o', 'e')],          # a beam may carry its own radius (mm)
                   mirror='xyz')
print(star.check() or 'tiles')       # lists what would make it fall apart or not tile
lattice(body, star, cell_size=8, radius=0.6, skin=1.0)
```

`mirror` copies the beams across the cell's mid-planes (`'x'`, `'xy'`, `'xyz'`). `check()` reports nodes
outside the cell, zero-length beams, nodes on a face without a partner on the opposite face, and loose ends.

**A TPMS equation** — `cell_custom(equation=f)` — of the three cell phases `a, b, c` (2π per cell), written with
`+ - * /` and the methods `.sin() .cos() .sqrt() .square()`:

```python
my_tpms = cell_custom(equation=lambda a, b, c: a.cos()*b.cos()*c.cos() - a.sin()*b.sin()*c.sin(), name='D-like')
lattice(body, my_tpms, cell_size=8, thickness=0.8, skin=1.0)
```

Its value is turned into a distance in mm by following its gradient, so `thickness=` is a real wall and
`density=` calibrates it like the built-in ones. See `examples/11_custom_lattice.py`.

**A shape that tiles** — `cell_custom(shape=s)` — the shape you model in one cell, the box from (0, 0, 0) to the
`cell_size` the lattice is made with. It is repeated like a standard cell (and follows a `cell_map`); `check=True` warns
where its faces do not match. Model it so it tiles: what leaves one face must come in on the opposite one.

## Lattices that follow a surface

`lattice()` fills a body with a straight grid that the body's surface cuts off. `lattice_surface_conform()` makes a
**conformal lattice**: its cells lie *on* the surface, as big as you ask all along it (the cell size is measured
along the surface), and every cell has the same face towards the surface normal. Its first argument, `surface_field`,
is the surface, whatever it is — one argument, and which kind it is is found out: give it a body and the lattice
follows the body's surface — round a cylinder, over a fillet, along an S-shaped sheet, round the lip of a rim; give it
a `select_surface()` selection and it is laid on that patch of it; give it **a field that is only a surface** (no
thickness, no inside: its zero set, like `z - 8*sin(0.12*x)`) and it lays a one-sided layer on that. `within=` keeps the
lattice inside any other shape as well. By default (`side='inside'`) the
lattice **fills the body**: the cells run through its thickness, one layer for a thin shell, more where it is thicker.
With `side='outside'` the layers stand out of the surface instead (ribbing on the part). It adds no skin and no body
and cuts nothing out of the part — you add it with `union()`.

```python
# a thin shell of the part, filled with strut cells 15 mm wide (one layer: the shell is 4 mm thick)
skin = lattice_surface_conform(shell_outside(part, 4), cell_size=15)

# strut cells 6 mm wide standing 6 mm out of the whole surface of the part
ribs = lattice_surface_conform(part, cell_periodic('octet'), side='outside', depth=6, cell_size=6, radius=0.6)

# only on a face you picked (right-click it, or select_surface): the selection is the one argument
top = select_surface(part, seed=(12.5, 40.0, -3.0), angle=10)
ribs = lattice_surface_conform(top, cell_periodic('bcc'), side='outside', depth=6, cell_size=6, radius=0.6)
part_with_ribs = union(part, ribs, radius=1.0)       # blended into the part with a 1 mm fillet

# an open surface of no thickness: where this field is zero (an S-shaped sheet); the lattice is one layer on one
# side of it -- side='outside' is the side where the field is positive, 'inside' the side where it is negative --
# and it is cut off at the edge of the region `patch`: nothing goes round an edge, there is no other face
wave = z_field() - 8 * (0.12 * x_field()).sin()
patch = box_exact((-30, -20, -14), (30, 20, 14))
layer = lattice_surface_conform(wave, cell_periodic('octet'), within=patch, side='outside', depth=6, cell_size=6, radius=0.6)
```

**A surface, and a body.** A body has an inside, and a lattice with `side='inside'` fills it (a thin sheet of material
has two faces and a rim, and each gets cells). A *surface* is a field that is zero where the surface is and has no
thickness: there is nothing on its other side to wrap round, so the layer is on one side only, the side `side` names,
and it ends at the edge of the `within=` shape — the lattice is cut off there. The field's sign says the side:
positive is `'outside'`. The extent of a field with no end is taken from the region (or from `bounds=`). See
`examples/14_conformal_lattice.py` (a surface) and `examples/15_conformal_closed_body.py` (a closed body).

`lattice_surface_conform(surface_field, cell='octet', depth=None, cell_size=5, radius=None, layers=None,
side='inside', blend=0, direction=None, bounds=None, thickness=None, style='sheet', offset=0,
invert=False, skin=0, within=None)`:

| | |
|---|---|
| `surface_field` | the surface, as one argument: a body, a surface (a field that is zero on it), or a `select_surface()` selection (the lattice is laid on that patch only) |
| `within` | where, besides: any shape; the lattice is kept inside it. Default: the whole surface |
| `depth` | how deep the layers are together (mm). Default: as deep as the body is under each cell (a thin shell: its thickness; at most three cells) for `side='inside'`, one cell for `'outside'` |
| `cell` | a **strut cell** (`cell_periodic('octet')`, `'bcc'`, `'cubic'`, `'kelvin'`, …; `radius` is the strut radius, by default 12 % of the smaller of `cell_size` and the layer's depth), a **periodic surface** (`cell_periodic('gyroid')`, `'schwarz_p'`, `'diamond'`, `'neovius'`, `'lidinoid'`, `'split_p'`, `'iwp'`, `'frd'`, `'fischer_koch_s'`; see below), `cell_custom(nodes, beams)`, or `cell_non_periodic(…)` (a random graph on the surface). Default `cell_periodic('octet')` |
| `thickness`, `style`, `offset`, `invert`, `skin` | for a periodic surface: the wall of a `'sheet'` in mm (default 15 % of `cell_size`), or `style='network'` (the solid on one side of the surface, grown by `offset` mm; `invert=True` takes the other side); `skin` mm of solid against the faces of the layers |
| `cell_size`, `layers` | the cell along the surface; the number of cells through the depth (default: as many as fit, at least 1) |
| `side` | `'inside'` (the lattice fills the body) or `'outside'` (it stands out of the surface) |
| `direction` | the way the rows of cells run where the surface gives them no way (a flat or smoothly curved part with no edge to follow). Default: along x |
| `bounds` | `((x0, y0, z0), (x1, y1, z1))` of the body, if its extent cannot be found (a field with no end): the box the cells are laid out in |

**A periodic surface that follows the surface.** Give `cell` a TPMS and what lies on the surface is not a truss but the
**periodic surface itself**: its sheets run through the layers of cells and bend with the surface, one period to a cell
on the surface and to a layer through the depth, on the same cells as the struts. This is nTop's conformal TPMS.

```python
# a gyroid skin round the part: 6 mm periods, walls 1 mm thick, filling a 4 mm skin
texture = lattice_surface_conform(shell_outside(part, 4), cell_periodic('gyroid'), cell_size=6, thickness=1.0)

# the solid on one side of a diamond surface, standing 8 mm out of the part
net = lattice_surface_conform(part, cell_periodic('diamond'), side='outside', depth=8, cell_size=8, style='network')
```

The periodic function is evaluated in the coordinates of the cell a point is in: `s` and `t` along the surface and `w`
through the layer, found from the four edges of the cell (a Coons patch) and the depth of the layer — so it is made from
the body's field alone, like the struts, and no mesh of the body is made. 
- **Which one.** The ones made of cosines only (`schwarz_p`, `neovius`, `iwp`, `frd`) look the same when a cell is turned a
  quarter, so where rows of cells meet they join up with no break. The others (`gyroid`, `diamond`, `lidinoid`, …) have a
  seam there, a few cells wide, where the sheets do not join.
- **Where three or five cells meet** (a node of the quad mesh that has not four cells round it, as in any quad mesh of a
  surface: about one node in five) the periodic function is rougher. Where the surface is flat, or nearly so, or where one
  orientation of the texture on every face is fine, a plain periodic lattice trimmed to a thin shell has none of this and
  renders in seconds (example 18):

  ```python
  shell = shell_outside(part, 1.5)
  texture = union(part, lattice(shell, cell_periodic('gyroid'), cell_size=5, thickness=0.9))
  ```

  It keeps one orientation in space, so on a face that looks another way the same gyroid is cut across at another angle,
  and the cells are not aligned to the surface; `lattice_surface_conform` keeps the size and the alignment on every face.
- **Rendering takes longer than for struts**, because the sheets fill the volume of the layers (a pan, 6 mm periods,
  about a minute at 4.5 samples per mm). Look at a part closely with a small `view.set_bounds()` and a high resolution.

**How the cells are laid out.** Before any cell is made, the surface is covered by ONE MESH OF QUADS, one quad to a cell:
its rows follow the surface's own directions — along a sharp edge, round a hole, along a handle — and its quads are
`cell_size` wide where the surface lets them be. There are two layouts, because there are two kinds of surface; which is
used is found out from the field (does the surface go on past the region it is wanted in?).

*A body* (a closed solid, or a selection of one): the mesh is **closed and covers all of the surface**. It is made from a
cloud of points of the surface: two points are neighbours when the *surface* joins them, not when they are close in space,
so a thin wall or a gap between two points is never crossed. Points next to a sharp edge or a corner are moved ONTO it first
(the planes of the neighbouring points are fitted, as dual contouring finds its vertices, and the result is kept only if the
field says it is on the surface), so an edge is a line of the graph and a corner of the part is always a vertex of the layout.
A direction field with four-fold symmetry and a lattice position field are solved over the points (a sharp edge is a line the
field follows, a corner is a point the lattice is pinned to); the lattice vertices are where the position field puts them, made
fine enough that each vertex's part of the surface is a disc; a face is made for every three of those parts that meet, as long
as the faces keep making a surface; a face of k corners becomes k quads; and the nodes are then moved over the surface until the
cells are even (a node on an edge or at a corner stays on it, those on a straight edge are spread evenly along it, and the rows
next to an edge are drawn straight), never so far that a cell folds — the cells stay the same cells. The map is checked for what must not be in it: a cell folded over a neighbour or lying
on top of another, an edge through the air, two corners at one place. Where the surface is finer than the cells (a slot
narrower than a cell, a thin rim, a tiny hole) some are left at that size; their number is in the warning the call prints, as is the
share of the surface that lies on walls, gaps or holes thinner than the cell, and a smaller `cell_size` removes them.

*A surface with no body* (a field that is zero on a sheet, cut off by `within=`): a scaffold of the sheet (a triangle mesh
by marching tetrahedra, with its vertices on the surface and on its sharp edges), the same kind of direction and position
fields over it, a region for every square of the lattice, k quads for a region of k corners, and the nodes moved until the
cells are even. The sheet is laid out over a margin of two cells round the region you give, so the region lies well inside
it; the layout has an edge where the region cuts it, and only there.

**It is made from the body's field and nothing else.** The surface is where the field is zero, its normal the field's
gradient; no mesh of the body is made and no distance is taken to one (a mesh is only what is drawn at the end). The
lattice is a graph of straight beams, and the mesher measures the distance to the beams near a point: it renders about as
quickly as a graph lattice.

**Nothing is left out.** Every separate piece of the surface is mapped on its own and checked on its own: every cell has four
different corners, every edge is on two cells, one fan of cells goes round every node, and the cells of a piece make one surface. A piece too small for cells of `cell_size` — a cavity inside a
boss, a speck of material — is sampled finer, by itself, until it has a closed map: its cells are then smaller than asked.
Two surfaces that lie too close together for the points to tell apart (two walls a fraction of a cell apart) are cut apart
and laid out again. A speck of the field that only comes near zero and never crosses it is not a surface; it is given up only
after the field has been read on a fine grid round it. The layout is made **once**, from a grid of sample points, and the same call gives the same layout every time.
Where the surface has detail about as small as the spacing of the points — a hole or a gap narrower than a cell, a wall
thinner than one — the grid decides which points are joined, so one grid may close the part and another not, or give another
number of holes. The grid is yours to choose: **`grid_offset`** (0, 1, 2 or 3: four fixed grids, 0 by default). The layout does
not try the others behind your back; if a part gives an error or a warning that its topology may be off, look at what the part
has at the place it names and try another `grid_offset`, or a smaller `cell_size`. A piece that is not a good map at the size
asked is sampled once finer on its own (cells half as big, for that piece only), and the warning says so. If a piece of the
surface cannot be given a closed map, **nothing is made and an error says which piece, and where**: a map that does not cover
the whole surface is never used. The error also says what to try (another `grid_offset`, or a cell size about half as big:
the surface has a feature — a hole, a thin wall — too fine for cells of this size).

**Cells are distorted, never deleted for it.** A cell is as stretched, squeezed or bent as the surface makes it — over
a fillet, round a rim, across a dome — and its struts follow: the points inside a cell are found from its four edges, which
run along the surface, so a cell bends round a lip with the surface instead of cutting through the metal. **The material is
filled once.** A layer fills the column from a point of the surface inward to where the body ends. On a thin body (a shell
whose skin is thinner than the layers) the cell map has both faces of the slab, an inner and an outer, and the column of a
cell on one is the column of a cell on the other. A cell is left out only when **nothing is lost by it**: when every one of
a grid of 27 points of its column that lies in the material also lies in the column of another cell that stays (and a cell
that covers a point for one that was left out stays for good). Of two cells that cover each other, the one on the smaller
piece of the surface is the first to go (on a surface that is one piece, the one that faces away from a fixed direction).
A body thicker than its layers is not touched: the columns end inside the material, where there is no cell, and what a
cell covers nobody else does. Measured on plates 1, 2 and 3 mm thick, a plate with a hole and a hollow sphere (cells 4 and
6 mm, layers as deep as the cell), 40 to 60 % of the cells go and the share of the material inside a column falls by
0.4 to 2 points (by 9 points with the first rule, which matched the middles of the columns and so left the rims bare); the
share of the material that is in two columns or more falls from 73–99 % to 12–38 %. The message `[conform] … cells whose
layers are all filled by other cells …` says how many were left out.

**Limits.**
- Layers are stretched where the surface curves too: a layer `h` above a surface with a radius `R` is `R / (R + h)` as
  dense on a convex surface and `R / (R − h)` on a concave one. Round the lip of a thin rim the far side of a layer is
  squeezed a lot, and the struts bunch up there.
- A quad mesh of a surface has nodes that are not on four cells (a node on three or five): about one in five here. Cells
  are less regular round those, round a narrow fillet or a small step (narrower than a cell) and where the rows of cells
  have to turn a corner; the nodes are evened out afterwards, but a node on a sharp edge can only slide along it.
- Struts and the nine periodic surfaces (and a graph of random cells); your own TPMS equation (`cell_custom(equation=…)`),
  a planar pattern, a shape that tiles and the trusses made on the nodes of a surface mesh (`prism`, `xbrace`, `zigzag`)
  are not available here (they need the surface's coordinates as a field, or a
  mesh of the surface, and a mesh is only the output).
- Cells much bigger than the thickness of a thin shell (15 mm cells in a 4 mm skin) are flat slabs: struts lie at shallow
  angles and cross, and a 15 mm cell cannot follow a lip a few mm across. The cells should be about as wide as the layer
  is deep, or smaller where the part has small features (a pan with a 4 mm skin: 5 to 6 mm).
- A feature smaller than a cell — a hole narrower than `cell_size`, a wall thinner than a fraction of it — cannot be
  followed by cells of that size: the layout is refused (or, for a speck of material, mapped by cells smaller than asked)
  and the error names the place.
- A selection is cut out with the selection's own field.
- Render a lattice at a resolution that gives its struts at least three or four samples across (a strut 1 mm thick
  wants about 4 samples per mm): thinner than that is where dual contouring has no surface to find.

## Graphs

The random cells are made through `cell_non_periodic` (`lattice()` for a foam or truss in a body,
`lattice_surface_conform()` for beams on a surface); these build and thicken graphs directly:

| | |
|---|---|
| `graph_lattice(nodes, beams, radius, blend)` | beams along the edges of **any graph** (nodes `[(x,y,z)…]`, beams `[(i,j)…]`); `radius` a number, one per node, or a field (tapers along each beam) |
| `surface_graph`, `voronoi_graph`, `points_graph` | the graphs behind them, e.g. to print or modify |
| `LatticeGraph` | `.thicken(radius, blend)`, `.lengths()` |

The graph lattices are **cached by content**: a rebuilt graph is expensive, so running the script again
does not repeat it.

## Measuring a lattice

| | |
|---|---|
| `relative_density(lattice_field, cell_size, samples=40, origin)` | the share of one unit cell it fills (an untrimmed, Cartesian lattice) |
| `lattice_parameter_for_density(cell, cell_size, density, style, samples)` | the thickness / offset / wall / radius that gives a relative density (found by bisection) |
| `mass_properties(graded, density, lower, upper)` | the real volume and mass of the trimmed lattice |

## Tips

- A lattice is an **implicit field**: it costs nothing until it is meshed, then meshing cost grows with the
  surface area. Start with a coarse resolution to place the body, refine for the final look.
- `view.set_resolution` should resolve the thinnest wall at several samples. A lattice that disappears
  is a mesh that cannot see it: raise the resolution.
- To analyse a lattice, hand it to `static_analysis` like any shape — with a fine `element_size` (a few
  elements across a strut or wall).
- STL export of a lattice: `graded.save_stl('lattice.stl', lo, hi, resolution=4)`.
