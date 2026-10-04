# Selecting surfaces

Pick a face of a model, the way a CAD program does with a flood fill, and use it as a **region** — for supports and
loads, for a lattice that follows it, for colouring — without leaving the idea that everything here is a field.

- [In the viewport](#in-the-viewport)
- [In a script](#in-a-script)
- [What a selection is](#what-a-selection-is)
- [Using a selection](#using-a-selection)

## In the viewport

**Right-click a model.** A menu offers the variables of the flood fill:

| | |
|---|---|
| **Spread** | *Flat face*: the patch spreads while the surface faces the way it does at your click (a flat face, or a gently curved one). *Round / smooth faces*: it spreads over round faces — a cylinder, a fillet, a whole rounded skin — up to a sharp edge |
| **Angle** | flat: how far from the click's direction the surface may face; smooth: how much it may turn from one triangle to the next. Default 10° (flat) and 30° (smooth) |
| **Thickness** | how thick the field across the patch is (automatic: a hundredth of the model, at least two cells of the surface mesh) |
| **Radius** | stop this far from the click (no limit by default) |

**Select** writes the selection into the script, under the definition of the model you clicked, and shows it:

```python
selection_1 = select_surface(part, seed=(30.685, -9.977, 28.002), angle=10)
selection_1
```

Nothing else is stored: the script is the selection. Change the angle in the text and it runs again; delete
the two lines (or use the bin in the model tree) and it is gone. The patch is lit up in magenta on the model; the
row in the model tree has an eye like any other.

## In a script

`select_surface(shape, seed, angle=15, mode='flat', thickness=None, radius=None, resolution=None, bounds=None)`

- `seed`: a point on or near the surface (`(x, y, z)`); the patch is found from the nearest triangle of the
  surface mesh.
- `mode='flat'` or `'smooth'`, `angle`, `thickness`, `radius` as above.
- `resolution`: samples per mm of the surface mesh the flood fill runs over (default about 200 along the longest
  side, at least 1). A face smaller than a few cells of that mesh needs a higher one.

The mesh is that of the shape's own field, so it works on anything: an imported part, a lattice, a result.

## What a selection is

A `SurfaceSelection` is a **shape**: a field that is negative in a thin layer (`thickness`) across the patch and
positive elsewhere — the unsigned distance to the patch's triangles, less half the thickness. So it is the same kind
of object as a box or a part: show it, hide it, combine it (`union`, `intersection`, `offset_by`), colour with it.
It also keeps `.shape` (what it was picked on), `.seed`, `.angle`, `.mode`, `.thickness`, `.triangles` and `.patch`
(the unsigned distance to the patch itself) and `.whole` (the same to the whole surface mesh: it equals `.patch` exactly where the nearest point of the surface is in the patch).

## Using a selection

```python
foot = select_surface(part, seed=(57.8, 20.4, -60.0), angle=5)
top = select_surface(part, seed=(30.7, -10.0, 28.0), angle=5)

# supports and loads act on it
conditions = static_boundary_conditions(part, [fixed(foot)], [force(top, (0, 0, -500))])
result = static_analysis(part, conditions, material=aluminium, element_size=4)

# a lattice standing on it, following it: the selection is the surface
ribs = lattice_surface_conform(foot, cell_periodic('octet'), depth=5, cell_size=5, radius=0.5)
```

A selection is thin, so a `fixed()` or `force()` on it acts on the surface nodes of the analysis that lie within it
(about a hundredth of the model): choose a `thickness` of at least an element or two on a coarse analysis.
See [Analysis](analysis.md#boundary-conditions) and [Lattices](lattices.md#lattices-that-follow-a-surface).
