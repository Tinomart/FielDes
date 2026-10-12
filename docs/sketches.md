# Sketches, curves and extrusions: shapes made of points

A **point** is a model (a dot with a gizmo). A **curve** is a line through points (a **1D shape**), a **sketch** is the face a closed outline of points
encloses (a **2D shape**), and an **extrusion** pulls a 2D shape into a solid -- straight (`extrude`) or along a curve (`path_extrude`).
All of them are made of the same points, and all of them are edited in the viewport: the script is the truth, and what you drag, click and
double-click is written into it.

```python
a = point(0, 0, 0)
b = point(40, 0, 0)
c = point(40, 20, 0, smooth=True)         # a spline point: the outline goes smoothly through it
d = point(0, 20, 0)

plate = sketch([a, b, c, d])              # a 2D shape: the face the points enclose
path = curve([a, b, c], smooth=False)     # a 1D shape: a line through points
block = extrude(plate, 0, 5)              # a 3D shape: the face pulled 5 along its normal
pipe = path_extrude(circle(2), path)      # a 3D shape: a 2D shape swept along a curve
tube = offset(path, 1.5)                  # a 3D shape: everything within 1.5 of a curve
```

## Making them in the viewport

Right-click empty space:

| Menu | Entry | What it makes |
|---|---|---|
| **New 2D shape** | `sketch` | a sketch. With **three or more points selected** it is made from them, in the order you selected them; with none selected it makes four new points and a sketch of them |
| **New 1D shape** | `curve` | a curve. With **two or more points selected**, from them; with none, three new points |
| **New 3D shape** | `extrude` | a solid from a 2D shape: with one selected, that one is pulled up; with none, a new rectangle |
| **New 3D shape** | `path_extrude` | a 2D shape swept along a curve: select both (in either order) -- or the shape and the **points** of the path, which get a curve of their own; with none, a new circle and a path |

Hold the mouse on an entry for what it takes. A **tube round a curve** is `offset` of the curve (*Operation → Offsets and walls*), and so are the other things a
distance field does with it: a curve is a model like any other, and as a field it is the distance to the curve.

The **selection rectangle** (drag on empty space) selects every kind of model: shapes, points, point sets and fields.

## Editing them

- **Points are dragged** with their gizmo, as any point is. A new sketch is horizontal (in the plane of its points at the place you clicked); drag the points where you want them.
- **Click an edge** of a sketch -- every 2D shape has its edges drawn **blue**, and the blue line brightens under the mouse -- and a point is put there; **keep the button down and move**,
  and the new point follows the mouse. A curve works the same way where you **press the line and drag** (a click on a curve only selects it). The new point is a model of its own, written above the
  sketch and put in its list in the right place: `plate = sketch([a, p5, b, c, d])`.
- **Double-click a point** of a sketch or a curve and it changes between a **corner** (a hard edge: a **square** dot) and a **spline point** (the curve goes smoothly through it: a **round** dot with a ring).
  What is written is `smooth=True` or `smooth=False` on the point; a point that says nothing is what the sketch or the curve says.
- **The default is a hard edge.** `sketch(points, smooth=False)` and `curve(points, smooth=False)`: with `smooth=True` every point is a spline point, except where a point says `smooth=False`.
- **Rotate a point** (the rings of its gizmo): the **rotation of a spline point turns the direction in which the curve leaves it**. A line through the selected point shows that direction. With no
  rotation the direction comes from the neighbouring points (a Catmull-Rom spline). In a `path_extrude` the same rotation **twists** the profile about the path there (between points the twist runs smoothly),
  so a twisted sweep is a rotation of a point about the path.
- **Delete a point** with its trash button in the model tree: it leaves the list of the sketch.

## The plane of a sketch

A sketch lies in **the plane through its first three points** (the first that is not on a line with the first two). Every other point is **projected onto that plane**, so a sketch can be made of any points
and it always works: move one of them off the plane and the outline moves along the plane under it. `extrude(shape, a, b)` goes between the distances `a` and `b` from the plane, along its normal -- for the shapes
that lie in z = 0 (circle, rectangle, polygon ...) that is `zmin` and `zmax`. With fewer than three points, or points on a line, a sketch is empty.

## What they are as fields

A sketch is a 2D shape in every way: the distance to its outline in its plane (negative inside, exact, for any outline -- concave ones too); `offset(sketch, 2)` is the outline grown by 2. A curve is the unsigned distance
to the line. `path_extrude` carries the profile along the curve in pieces (the curve is cut into short straight pieces, 14 to a curved segment) with mitered joints, turning the profile with the curve without twisting about it; the ends are cut square.
The profile's own origin travels along the path -- the origin for a circle or a rectangle, the first point of a sketch. The result is a field like any other shape: offset it, shell it, put a lattice in it, analyse it.
