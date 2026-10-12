# Sketches, lines and extrusions: shapes made of points

A **point** is a model (a dot with a gizmo). A **polyline** (a set of lines) and a **spline** (a smooth curve) go through points: they are **1D shapes**. A **sketch** is the face a closed outline of points
encloses: a **2D shape**. An **extrusion** pulls a 2D shape into a solid -- straight (`extrude`) or along a line (`path_extrude`).
All of them are made of the same points, and all of them are edited in the viewport: the script is the truth, and what you drag, pull and double-click is written into it.

```python
a = point(0, 0, 0)
b = point(40, 0, 0)
c = point(40, 20, 0, smooth=True)         # a spline point: the outline goes smoothly through it
d = point(0, 20, 0)

plate = sketch([a, b, c, d])              # a 2D shape: the face the points enclose
lines = polyline([a, b, c])               # a 1D shape: straight lines through points
curve = spline([a, b, c])                 # a 1D shape: a smooth curve through points
block = extrude(plate, 0, 5)              # a 3D shape: the face pulled 5 along its normal
pipe = path_extrude(circle(2), curve)     # a 3D shape: a 2D shape swept along a line
tube = offset(curve, 1.5)                 # a 3D shape: everything within 1.5 of a line
```

## Making them in the viewport

Right-click empty space:

| Menu | Entry | What it makes |
|---|---|---|
| **New 2D shape** | `sketch` | a sketch. With **three or more points selected** it is made from them, in the order you selected them; with none selected it makes four new points and a sketch of them |
| **New 1D shape** | `polyline` | a set of lines. With **two or more points selected**, from them; with none, three new points |
| **New 1D shape** | `spline` | a smooth curve, the same way |

These are **shapes** (nouns), so they are under **New ... shape**. `extrude` and `path_extrude` are **operations** (verbs): they work on what is selected, so they are
under **Operation → Extruding**, and are offered only when what is selected fits:

| Operation | Select | What it makes |
|---|---|---|
| **Extruding → extrude** | a 2D shape | a solid: the face pulled up |
| **Extruding → path_extrude** | a 2D shape and a line (in either order) -- or the shape and the **points** of the path, which get a spline of their own | the shape swept along the path |

Hold the mouse on an entry for what it takes. A **tube round a line** is `offset` of it (*Operation → Offsets and walls*), and so are the other things a distance field does: a line is a model
like any other, and as a field it is the distance to it.

**A sketch made of points that are not in one plane** puts them there: the plane through the first three points that are not on a line stays as it is, and every other point is **moved onto it** -- its
numbers in the script are changed, in the same step that makes the sketch, so one undo takes both back.

The **selection rectangle** (drag on empty space) selects every kind of model: shapes, points, point sets and fields.

## Editing them

- **A point of a sketch is dragged by its dot, in the plane of the sketch.** Press the dot and drag: it moves in the plane and nowhere else (it has no arrows: there is nothing to pull out of the plane). A point of a
  sketch has three handles: the **dot** (move), **one rotation ring** (in the plane) and the **scale squares** (all three scale it). A new sketch is horizontal (in the plane of its points at the place you clicked).
  The picture follows the mouse as it moves: it is drawn from the numbers the points have, not from a run of the script. (A point of a line in space has the arrows and the three rings of any point.)
- **Drag the face of a sketch** and **all its points move together, in the plane of the sketch**. A click on the face only selects it.
- **Pull a point out of an edge.** Press on an edge of a sketch (every 2D shape has its edges drawn **blue**, and the blue line brightens under the mouse) and drag: a new point comes out of the edge and follows the
  mouse, drawn at once. Let go and it is written: a model of its own above the sketch, put in its list in the right place (`plate = sketch([a, p5, b, c, d])`). On a line **press the line and drag**; a click on a
  line only selects it.
- **Double-click a point** of a sketch or a line and it changes between a **corner** (a **square** dot: a hard edge) and a **spline point** (a **round** dot with a ring: the curve goes smoothly through it). What is
  written is `smooth=True` or `smooth=False` on the point; a point that says nothing is what its sketch or line says. The dot always shows what the point is **in its sketch or line**; a point that is in neither is a plain round dot.
- **Double-click a sketch** to frame it: the view also **turns to face the plane of the sketch**.
- **The default is a hard edge** for a sketch and a polyline, a smooth curve for a spline: `sketch(points, smooth=False)`. With `smooth=True` every point is a spline point, except where a point says `smooth=False`.
- **Delete a point** with its trash button in the model tree: it leaves the list of the sketch.

## Rotating and scaling a point

A point has the gizmo of any model, and two of its handles mean something to the lines and sketches it belongs to:

- **Rotate a spline point** and the direction in which the curve leaves it turns (a line through the selected point shows it). With no rotation the direction comes from the neighbouring points (a Catmull-Rom spline).
  **A point of a sketch has one rotation ring, in the plane of the sketch**: it turns the direction about the plane's normal. A point of a line in space has the three rings of any gizmo. In a `path_extrude` the rotation
  of a point **twists** the profile about the path there (between points the twist runs smoothly).
- **Scale a point** (its scale knobs: a point has **one** scale, all three knobs move it) and a spline **leaves it more strongly or less**: `1` is the Catmull-Rom curve, more is a rounder bulge, less is tighter (`0` makes
  it as straight as a corner). In a `path_extrude`, **the scale of a point is how thick the profile is there**, tapering smoothly between points: a point with scale 2 makes the tube twice as thick at that point.
- **Shift + the mouse wheel** scales what is selected: a point by its scale, any other model by its gizmo's scale numbers (a notch is 10 %). The numbers are written when the wheel has rested a moment. See [Handles](handles.md).

## The plane of a sketch

A sketch lies in **the plane through its first three points** (the first that is not on a line with the first two). A point that is not on that plane is **projected onto it** wherever the sketch is built, so a sketch always works:
move one of them off the plane and the outline moves along the plane under it. `extrude(shape, a, b)` goes between the distances `a` and `b` from the plane, along its normal -- for the shapes that lie in z = 0
(circle, rectangle, polygon ...) that is `zmin` and `zmax`. With fewer than three points, or points on a line, a sketch is empty.

## What they are as fields

Everything here is **plain math**: expressions of x, y and z, like any other shape of the library. There is no black box in a sketch, a line or an extrusion, so the renderer can do with them what it does with
every other shape: look at smaller and smaller pieces of space and drop what is far away.

- A **sketch** is a 2D shape in every way: the signed distance to its outline in its plane (negative inside, exact, for any outline -- concave ones too); `offset(sketch, 2)` is the outline grown by 2.
  The distance is the least of the distances to the edges; whether a point is inside is decided by **cutting the outline into triangles** when the script runs (a point is inside when it is inside one of them), so a
  hundred edges cost about what the few near the point cost. An outline that crosses itself cannot be cut and uses the winding of the outline round the point instead (slower, the same answer).
- A **line** is the unsigned distance to it: the least of the distances to its straight pieces.
- `path_extrude` carries the profile along the line in pieces (the line is cut into short straight pieces, 14 to a curved segment) with mitered joints, turning the profile with the line without twisting about it;
  the ends are cut square. The profile's own origin travels along the path -- the origin for a circle or a rectangle, the first point of a sketch.
  The result is a field like any other shape: offset it, shell it, put a lattice in it, analyse it.

What you see while you drag a point is not the field: the viewport draws the outline and the face of a sketch, and the line, straight from the numbers of the points, so it follows the mouse at once. The solid made from it
(an extrusion, a sweep) is made again when you let go.
