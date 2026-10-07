# Selecting surfaces

Pick a face of a model, the way a CAD program does with a flood fill, and use it as a **region** — for supports and
loads, for a lattice that follows it, for colouring — without leaving the idea that everything here is a field.

- [In the viewport](#in-the-viewport)
- [In a script](#in-a-script)
- [By other bodies](#by-other-bodies)
- [What a selection is](#what-a-selection-is)
- [Using a selection](#using-a-selection)

## In the viewport

**Right-click a model**, then **Select Surface** (the other entry, **Operation**, writes an operation on the model: see [the interface](interface.md#the-viewport)). A menu offers the variables of the flood fill:

| | |
|---|---|
| **Spread** | *Flat face*: the patch spreads while the surface faces the way it does at your click (a flat face, or a gently curved one). *Round / smooth faces*: it spreads over round faces — a cylinder, a fillet, a rounded skin — until the surface bends tighter than the angle allows, or to a sharp edge |
| **Angle** | flat: how far from the click's direction the surface may face; smooth: how much the surface may turn within 10 mm of the walk — a limit on how tightly it bends (30° per 10 mm: a bend of radius under about 19 mm stops it). Default 10° (flat) and 35° (smooth) |
| **Radius** | stop this far from the click (no limit by default) |

**Select** writes the selection into the script, under the definition of the model you clicked, and shows it:

```python
selection_1 = select_surface(part, seed=(30.685, -9.977, 28.002), angle=10)
selection_1
```

Nothing else is stored: the script is the selection. Change the angle in the text and it runs again; delete
the two lines (or use the bin in the model tree) and it is gone. The new selection is the selected model in the tree, and it
is shown as the **patch lit up in magenta on the model**: the vertices of the model's surface mesh (the viewport's, made to
draw it) that belong to the patch are coloured, every other part of the surface is not drawn at all (the edge of the patch
runs between the vertices, as fine as the viewport's mesh, and a step of the walk). What `fixed()`, `force()` and `lattice_surface_conform()` use is the surface of the patch itself;
the viewport draws it by lighting the model's own surface, because a surface has no thickness to draw at the resolution of the viewport. The row in the model tree has an eye like any other.

## In a script

`select_surface(shape, seed, angle=15, mode='flat', radius=None, resolution=None, bounds=None)`

- `seed`: a point on or near the surface (`(x, y, z)`); the walk starts where the field's gradient carries it onto the
  surface.
- `mode='flat'` or `'smooth'`, `angle`, `radius` as above. There is no thickness: a selection is a surface.
- `resolution`: steps per mm along the surface (default about 150 along the longest side, at least 0.1). A face smaller
  than a few steps needs a higher one; the walk takes one sample for every step of the surface it reaches, so a finer
  one costs more.

**It is made from the field alone.** The surface is where the shape's field is zero and its normal is the field's gradient;
the walk steps over it from the seed — eight steps from every sample, each carried back onto the surface along the
gradient — and keeps a step where the normal there is within the angle (of the seed's, for flat; for smooth, of the sample
about 10 mm back along the walk, the angle being allowed for every 10 mm of the way between them). No mesh of the shape is made or read, so it does the same on anything: a reconstructed or a tessellated
part, a mesh, a CSG model, a lattice, a result. A thin wall is no harder than a thick one: the walk follows the surface,
not the space, so a wall thinner than a step is still one face on each side, and a step past an edge finds no surface to
land on (or the face on the other side of the edge, which turns by the angle of the edge).

## By other bodies

`surface_from_bodies(body, *others, tolerance=None, bounds=None)` picks a surface not by a place but by **other bodies**: the
surface of the **first** body, where it meets the bodies that follow. With no other body it is the whole surface of the body.

```python
holes = surface_from_bodies(plate, bolt_1, bolt_2)    # the plate's surface where the bolts sit in it
skin = surface_from_bodies(plate)                     # all of the plate's surface
```

- A point of the first body's surface is selected when it is **inside** one of the others or within `tolerance` of one: the
  surface of a box *inside* a sphere that crosses it, the wall of a hole a bolt fills, the face a block stands on. A list may
  stand for the bodies (`surface_from_bodies(plate, [a, b])`).
- `tolerance` (mm) is how close counts as meeting: bodies that touch at a face, or sit in a hole with a little play, meet; a gap wider
  than it is a gap. The default is half a percent of the size of the first body. If the bodies do not meet, nothing is selected (a
  note says so when their boxes are apart).
- The selection ends where the other body does -- it follows no face to its edges -- so a block over half of a face selects that half.
- It is a surface like the one `select_surface` makes (below), and used the same way: a region for `fixed()` and `force()`, a surface
  for `lattice_surface_conform()`. It is made from the fields alone, so it follows a body that is dragged or has `var()` numbers.

In the viewport: **select several models** in the model tree (Ctrl or Shift click; or Ctrl-click them in the viewport), right-click one of them, and choose
**Operation → Surfaces → surface_from_bodies**. The first model selected is the body; the others are what meets it. With one
model selected the entry writes `surface_from_bodies(model)`: its whole surface.

The surface lies on the first body, and most often *inside* the others -- the box's face inside the sphere is covered by the sphere -- so
**isolate the selection** (select it in the tree and press `I`) to see it.

## What a selection is

A `SurfaceSelection` is a **surface** (the *Surface* kind in the model tree, like any open surface): the patch of the
part's surface, with no thickness. As a field it is the distance to the walk's samples of the patch, less the thinnest
layer the samples cover (not a setting: what makes it a region at all, as a surface itself is where a field is zero). So it
is the same kind of object as any shape: show it, hide it, combine it (`union`, `intersection`, `offset_by`), colour with it.
It also keeps `.shape` (what it was picked on), `.seed`, `.angle`, `.mode`, `.samples` (how many points the
walk took), `.spacing` (how far apart they are), `.cover` (how far the patch's surface is from its nearest sample), `.patch`
(the distance to the patch's surface: zero on it, to within the cover) and `.whole` (the distance to the whole surface,
the shape's own value |f|, the distance wherever the field is one: `.patch - .whole` is below 0 where the nearest point of the surface is in the patch).
The edge of a patch is where the walk stopped, to within a step: a point within the cover of the patch's last samples,
on the other side of an edge, is counted in.

## Using a selection

```python
foot = select_surface(part, seed=(57.8, 20.4, -60.0), angle=5)
top = select_surface(part, seed=(30.7, -10.0, 28.0), angle=5)

# supports and loads act on it
conditions = static_boundary_conditions(part, [fixed(foot)], [force(top, (0, 0, -500))])
result = static_analysis(part, conditions, material=aluminium, element_size=4)

# a lattice standing on it, following it: the selection is the surface
ribs = lattice_surface_conform(foot, cell_periodic('octet'), cell_thickness=5, cell_size=5, radius=0.5)
```

A selection is a surface, so a `fixed()` or `force()` on it acts on the surface nodes of the analysis that lie within it
(about a hundredth of the model): on a coarse analysis, thicken it by an element or two with `offset_by(selection, mm)`.
See [Analysis](analysis.md#boundary-conditions) and [Lattices](lattices.md#lattices-that-follow-a-surface).
