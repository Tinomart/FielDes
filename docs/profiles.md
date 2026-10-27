# Profiles, lines and extrusions: shapes made of points

A **point** is a model (a dot with a gizmo). A **polyline** (a set of lines) and a **spline** (a smooth curve) go through points: they are **1D shapes**. A **profile** is the face a closed outline of points
encloses: a **2D shape**. 2D shapes are drawn **over** the 3D ones, in a very light blue, and are picked before them: a shape drawn on a body or inside it is always seen whole, and
a click on it selects it, not the body beneath. An **extrusion** pulls a 2D shape into a solid -- straight (`extrude`) or along a line (`path_extrude`).
All of them are made of the same points, and all of them are edited in the viewport: the script is the truth, and what you drag, pull and double-click is written into it.

```python
a = point(0, 0, 0)
b = point(40, 0, 0)
c = point(40, 20, 0, smooth=True)         # a spline point: the outline goes smoothly through it
d = point(0, 20, 0)

plate = profile([a, b, c, d])              # a 2D shape: the face the points enclose
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
| **Primitives → New 2D shape** | `profile` | a profile. With **three or more points selected** it is made from them, in the order you selected them; with **a surface selected** it is the outline of that surface (see below); with none selected it makes four new points and a profile of them |
| **Primitives → New 1D shape** | `polyline` | a set of lines. With **two or more points selected**, from them; with none, three new points |
| **Primitives → New 1D shape** | `spline` | a smooth curve, the same way |

These are **shapes** (nouns), so they are under **New ... shape**. `extrude` and `path_extrude` are **operations** (verbs): they work on what is selected, so they are
under **Operation → Extruding**, and are offered only when what is selected fits:

| Operation | Select | What it makes |
|---|---|---|
| **Extruding → extrude** | a 2D shape | a solid: the face pulled up |
| **Extruding → path_extrude** | a 2D shape and a line (in either order) -- or the shape and the **points** of the path, which get a spline of their own (the points the shape is made of are **not** part of the path, so selecting the profile together with its path's points is fine) | the shape swept along the path |

Hold the mouse on an entry for what it takes. A **tube round a line** is `offset` of it (*Operation → Offsets and walls*), and so are the other things a distance field does: a line is a model
like any other, and as a field it is the distance to it.

**A profile made of points that are not in one plane** puts them there: the plane through the first three points that are not on a line stays as it is, and every other point is **moved onto it** -- its
numbers in the script are changed, in the same step that makes the profile, so one undo takes both back.

The **selection rectangle** (drag on empty space) selects every kind of model: shapes, points, point sets and fields.

## Drawing in the work plane

Open a **work plane** (`Shift+W`, or `Ctrl+Shift+W` for a new one -- the plane of the selected 2D shape or surface, or parallel to the view) and click the plane: in **line** mode every click is the next point of a line, **a click on the first point
closes it and makes a profile of it**, `Enter` ends it open (a `polyline`); in **spline** mode the same with a smooth curve; in **point** mode every click is a point. `E` goes round the three drawing modes; **a right click in the viewport (or the button that is on, clicked again) stops drawing**, and `E` brings back the mode that was on: nothing is put down and things are selected, moved, turned and scaled as usual, and no marker shows on the plane. While drawing a marker shows the point a click puts down -- a square for a line, a round point with a ring for a spline, a dot for a point. The grid and snapping keep the points
exact, and the points land on the plane, written as ordinary numbers. When not drawing, a profile (or any 2D shape) in the plane is moved about in it by pressing it. See [The work plane](interface.md#the-work-plane).

## A profile from a surface

Pick a surface on a part (right-click it, **Select Surface**: a `select_surface(...)` line), select it in the model tree and make **Primitives → New 2D shape → profile** -- from the right-click menu of the surface in the viewport, of
its row in the tree or of its line in the script, all the same menu: the line written is `profile(top)`, and what you get is a
**2D shape of that surface's outline**.

- **A flat face is exact.** The profile lies in the plane of the face and has the outline of the face -- its corners where its edges meet, a hole in it as a hole in the profile. Its edges are found against the part's own field
  (where it stops being zero on the face), so they are right to about a ten-thousandth of the size of the face, not to a step of the selection.
- **A surface that is not flat is projected.** The plane is the one through the middle of the surface, facing the way the surface faces on average, and the profile is the outline of the surface seen from there -- along that
  direction. A note in the output says so, and how far from the plane the surface reaches. The outline then follows the selection to within a step of its walk.
- It is made from the surface and **moves with it** (drag the part, or change a number it is made from, and it is made again); it has no points of its own to drag. Like any 2D shape it goes into `extrude`, `path_extrude`, `offset` and the field
  functions: `extrude(profile(top), 0, 5)` is a plate on the face. `profile(surface)` also works on a surface a script made with `select_surface`; the whole-surface selections of `surface_from_bodies` have no points to take an
  outline from.

## Editing them

- **A point is dragged by its dot, selected or not.** Press the dot and drag: a point of a profile moves in the plane of the profile and nowhere else (it has no arrows: there is nothing to pull out of the plane), a point of a line in the plane that faces the camera. A click that does not move selects it. A point of a
  profile has three handles: the **dot** (move), **one rotation ring** (in the plane) and the **scale squares** (all three scale it). A new profile is horizontal (in the plane of its points at the place you clicked).
  The picture follows the mouse as it moves: it is drawn from the numbers the points have, not from a run of the script. (A point of a line in space has the arrows and the three rings of any point.)
- **Drag the face of a profile** and **all its points move together, in the plane of the profile**. A click on the face only selects it.
- **A profile, a polyline or a spline selected as a whole can show a gizmo -- it starts off.** Turn it on with the gizmo button on the outline's row in the model tree (*click*: shown while it is selected, or *always*; the mode is the state of the button, which is not in the script). Then select the outline itself (its row in the model tree, or a click on it) and its points share one gizmo at the middle of them: it is the gizmo of any shape: its **arrows** and **dot** move all of it, its three **knobs** stretch it along an axis and its three **rings** turn it about the axes (with Shift, the plane squares and the one gold ring), about the middle of the points. What is written is the points' own `position`, `rotation` and `scale` numbers. See [Handles](handles.md).
- **Pull a point out of an edge.** Press on an edge of a profile (every 2D shape has its edges drawn **blue**, and the blue line brightens under the mouse) and drag: a new point comes out of the edge and follows the
  mouse, drawn at once. Let go and it is written: a model of its own above the profile, put in its list in the right place (`plate = profile([a, p5, b, c, d])`). On a line **press the line and drag**; a click on a
  line only selects it.
- **Double-click a point** of a profile and it goes from a **corner** (a **square** dot: a hard edge) to a **spline point** (a **round** dot with a ring: the curve goes smoothly through it) to a **round corner**
  (a **square with round corners**: an arc takes the place of the corner, see below) and back to a corner; on a line it changes between corner and spline point. What is written is `smooth=True` / `smooth=False`
  or `radius=` on the point; a point that says nothing is what its profile or line says. The dot always shows what the point is **in its profile or line**; a point that is in neither is a plain round dot.
- **Double-click a profile** to frame it: the view also **turns to face the plane of the profile**.
- **The default is a hard edge** for a profile and a polyline, a smooth curve for a spline: `profile(points, smooth=False)`. With `smooth=True` every point is a spline point, except where a point says `smooth=False`.
  `profile(points, radius=5)` rounds every corner that does not say otherwise with an arc of radius 5 (`radius=0`, the default, is a hard corner).
- **Delete a point** with its trash button in the model tree: it leaves the list of the profile.
- **Delete a profile** (or a line) and the points it was made from go with it, in the same undo step. A point that something else also uses -- another profile, a `move`, a field -- stays.

## Round corners

A point of a profile can be a **round corner**: `point(20, 12, 0, radius=4)`. An **exact circular arc** of radius 4, tangent to both edges, takes the place of the corner; the point stays where the two edges would meet,
so it is still the one you drag. It is a corner with a radius -- there is no separate kind to learn -- and `radius=0` is a hard corner again. A spline point has no radius (the curve goes smoothly through it: there is no corner
to round), and `point(..., smooth=True, radius=4)` says so in an error.

```python
a = point(0, 0, 0, radius=4)
b = point(40, 0, 0, radius=4)
c = point(40, 20, 0, radius=4)
d = point(0, 20, 0, radius=4)
slot = profile([a, b, c, d])                 # a rectangle with round corners
disc = profile([a, b, c, d], radius=10)      # ...with corners that ask for more than the sides allow: a stadium (the long sides keep their length)
```

- **The arc is the handle of the radius.** Hover the arc of a round corner (the pointer changes, and the bar at the bottom says so) and drag: pulled in, along the way the corner looks, it is rounder; pushed out
  towards the corner it is sharper. The number the point says is written as you drag (`radius=var(5)`; a plain `radius=5` becomes `var(5)` the first time you press the arc), and it stops where the geometry stops it. An arc is never
  an edge to pull a point out of: that would break the corner. (A corner that takes its radius from `profile(points, radius=...)` has no number of its own to drag: give the point `radius=`.)
- **The scale ring of a round corner multiplies the radius** as well: the radius in the call times the scale of the point's gizmo, as the scale of a sphere multiplies its radius. Its rotation ring is not shown (an arc has no direction).
- **The radius has a limit, and it is the geometry's own.** The arc takes a length `t = radius / tan(half the angle between the edges)` from each of its two edges, and the two arcs on one edge may take no more than the
  edge is long. Ask for more and the radius is made smaller, both arcs by the same share: a square with a round corner of half its side (or more) at each corner is a **circle**, and a rectangle asked for more than half its short
  side is a stadium. Nothing is left over and nothing runs past a point.
- **Exact.** The arc is an arc in the field: the distance to the shape is measured to the circle, not to a polygon that approximates it, so an `extrude`, an `offset`, a `shell` or a simulation of it sees a true circle at any
  resolution. (The distance to a spline is measured to straight pieces by the kernel, 14 to a segment -- a cubic has no closed form for its distance; an arc does.) It is drawn from the numbers of its points while you drag, like any profile, and its blue
  outline is the arc itself, cut on the screen to a twentieth of a pixel at the zoom you have -- so it stays a circle however far you zoom in (a spline's too). The filled face is a mesh of triangles.
- **A round corner next to a spline point**: the arc is tangent to the straight line to that point, and the spline leaves the end of the arc along that line. The ends of an open line cannot be round, and a round corner
  on a `polyline` or `spline` is a hard corner there: the profile is where a corner has a radius.

## Rotating and scaling a point

A point has the gizmo of any model, and two of its handles mean something to the lines and profiles it belongs to:

- **Rotate a spline point** and the direction in which the curve leaves it turns (a line through the selected point shows it). With no rotation the direction comes from the neighbouring points (a Catmull-Rom spline).
  **A point of a profile has one rotation ring, in the plane of the profile, and only a spline point has it**: it turns the direction about the plane's normal, and a corner does not leave in a direction, so its ring is not shown.
  A point of a line in space has the three rings of any gizmo. In a `path_extrude` the rotation of a point **twists** the profile about the path there (between points the twist runs smoothly).
- **The gold ring of a point** (the uniform handle of Blender's scale gizmo): drag it outward to grow, inward to shrink, and the factor shows beside it. A point has two numbers that the ring can drag, both uniform: its **influence**
  (`point(..., influence=r)`) and its **scale** (the three numbers of its `scale=`, moved together). The **influence** is how strongly a spline leaves the point -- `1` is the Catmull-Rom curve, more is a rounder bulge, less is tighter (`0` makes
  it as straight as a corner) --, the factor of the radius of a **round corner** (see above), and the radius of the field round the point. The **scale** is how thick a `path_extrude` makes the tube at the point, tapering smoothly between points:
  a point with scale 2 makes the tube twice as thick there. **The ring drags the influence on every point -- except a spline point, where it drags the scale; Shift and the ring drag the other number**, so the shape of a spline and the
  thickness of its tube are two numbers that no longer move each other (they used to be one).
- **Shift + the mouse wheel** (or **Ctrl + the mouse wheel**) scales the point the mouse is on, selected or not, else what is selected: a point by its scale, any other model by its gizmo's scale numbers (a notch is 10 %). The numbers are written when the wheel has rested a moment. See [Handles](handles.md).

## How `extrude` works

`extrude(shape, a, b)` pulls a 2D shape into a solid: the part of space between the distances **`a` and `b` from the plane of the shape, along its normal**. `a` is where the solid starts and `b` where it ends, both measured
from the plane: `extrude(plate, 0, 5)` is a plate 5 thick that sits on the profile's plane and grows to **one side** of it; `extrude(plate, -2.5, 2.5)` is 5 thick with the profile in the middle; a negative `b` grows the other way. (For a
shape that lies in z = 0 -- a circle, a rectangle -- `a` and `b` are `zmin` and `zmax`.) **Which side is "up" is decided by the order of the first three points of the profile**: the normal is `u x v` of the plane, which
points **towards you when the points go counter-clockwise** as you look at the profile, and away from you when they go clockwise -- so a profile drawn clockwise is extruded *down* by `extrude(plate, 0, 5)` (use `-5`, or draw it the other way).
In the model tree the call has its `a` and `b` as dim rows: click one to change it in the code. A profile that is outside the render region does not stop its extrusion from being seen: **the region grows to hold it**
(see *Auto scale* in [the interface](interface.md)).

**Dragging an extrusion, whatever 2D shape it is made of.** The two **end faces** -- the faces parallel to the 2D shape -- are the length of the extrusion: select the extrusion, press one of them and pull, and `a` (the face on the
shape's side) or `b` (the far face) follows the mouse; `handles(extrude_1, [a, b])` is written once you let go. That holds for a circle, a rectangle, a rounded rectangle, a triangle, a polygon, a ring and a profile of points alike.
The **walls** are the sides of the 2D shape, and are pulled as the 2D shape's sides are: the wall of a circle or a rectangle changes its radius or its side, the wall of a profile moves the two points of the outline it is
between, together, in the plane of the profile (the points' own gizmo numbers change, the profile and the extrusion follow). A press on the 2D shape itself -- drawn over its extrusion -- always pulls the 2D shape, not the extrusion.
**`Ctrl+E`** extrudes the selected 2D shape; with a 2D shape **and a curve** selected it makes `path_extrude(shape, curve)` instead.

## The plane of a profile

A profile lies in **the plane through its first three points** (the first that is not on a line with the first two). A point that is not on that plane is **projected onto it** wherever the profile is built, so a profile always works:
move one of them off the plane and the outline moves along the plane under it. `extrude(shape, a, b)` goes between the distances `a` and `b` from the plane, along its normal -- for the shapes that lie in z = 0
(circle, rectangle, polygon ...) that is `zmin` and `zmax`. With fewer than three points, or points on a line, a profile is empty.

## What they are as fields

Everything here is **plain math**: expressions of x, y and z, like any other shape of the library. There is no black box in a profile, a line or an extrusion, so the renderer can do with them what it does with
every other shape: look at smaller and smaller pieces of space and drop what is far away.

- A **profile** is a 2D shape in every way: the signed distance to its outline in its plane (negative inside, exact, for any outline -- concave ones too); `offset(profile, 2)` is the outline grown by 2.
  The distance is the least of the distances to the edges; whether a point is inside is decided by **cutting the outline into triangles** when the script runs (a point is inside when it is inside one of them), so a
  hundred edges cost about what the few near the point cost. An outline that crosses itself cannot be cut and uses the winding of the outline round the point instead (slower, the same answer).
- A **line** is the unsigned distance to it: the least of the distances to its straight pieces.
- `path_extrude` carries the profile along the line in pieces (the line is cut into short straight pieces, 14 to a curved segment) with mitered joints, turning the profile with the line without twisting about it;
  the ends are cut square. The profile's own origin travels along the path -- the origin for a circle or a rectangle, **the middle of the box of its outline for a profile** (the path runs through the middle of what is swept, whichever of its points is first; the thickness of a point scales the profile about that middle).
  **A profile is where the sweep starts**: the line says how the sweep travels, not where from. The whole sweep is moved so that the first point of the line is the middle of the profile, and **the first ring is the profile itself, in its
  own plane, at whatever angle the line leaves it** (the sweep is then slanted, like an extrusion pushed sideways, and the end is the same profile carried to the end of the line; a profile whose plane lies along the line has nothing to sweep and is
  turned across it), so editing the profile is editing the base of what it is swept into. A line that already starts at the middle of the profile is not moved. (A circle or a rectangle has no place of its own, so it is carried along the line where the line is.)
  **The path is drawn where the sweep goes, at all times**: from the middle of the profile, through the middle of the cross-section, its points with it -- whatever numbers its own points have in the script (only the shape of the path matters,
  not where it was drawn: the first point of it is the middle of the profile). A point of the path is dragged where you see it, and the first point stays at the middle of the profile. A path that is used by no sweep of a profile is drawn where its numbers are.
  The result is a field like any other shape: offset it, shell it, put a lattice in it, analyse it.

What you see while you drag a point is not the field: the viewport draws the outline and the face of a profile, and the line, straight from the numbers of the points, so it follows the mouse at once. **A straight
`extrude` of a profile of points follows it too**: it is a *prism* -- the outline pulled along its normal -- and the viewport makes it from the numbers of the points (and of `a` and `b`) at every frame, the way it makes the face, with no
script run and no field meshed: drag a point and the solid follows it, and nothing is calculated when you let go. It stays a prism while its numbers are what it was made with (its gizmo has not placed it, no face of it has been pulled);
with another number, or an outline that crosses itself, its field is meshed as any shape's is. **A sweep (`path_extrude`) of a profile along a line of points is drawn the same way**: the viewport carries the outline along
the path, a ring at every piece, in the frame the field of the sweep has (turned by the rotation of the points, scaled by their scale, cut square at the ends or by the plane that bisects a corner), and closes the ends with the face of the profile.
Nothing of the field is meshed -- and its tree is not even optimised, compiled or searched until something reads it (a difference, an offset, an analysis) -- so changing a number of a point of the profile, of the path, of the twist -- by dragging,
or by typing in the script -- shows the new sweep at once (a smooth profile of 15 points swept along a path of 3 points: the script runs in half a second and the sweep is on the screen 0.1 s later; preparing the field of the same sweep took 6 s and
a search through it 15 s of a window that did not answer, and meshing it at resolution 1 takes minutes). **A sweep of a primitive -- a `circle`, a `rectangle`, a `rounded_rectangle`, a `triangle`, a `polygon` -- is drawn the same way**, and starts at the 2D shape as a sweep of a profile does: its outline (a circle is 72 short sides, a rounded corner 12 to a quarter) is carried along the path, also when the shape has the `handles(...)` and the placement arguments (`position=`, `rotation=`, `scale=`) the right-click menu puts into its call (the numbers of a circle or a rectangle, and a placement that moves, turns about z or scales it in its plane, are followed too). A profile that is anything else (an `offset` of a circle, a union), a sweep that has a colour field, a channel or a result, and what is made *of*
an extrusion or a sweep (a difference, an offset ...) are made from the field, again when you let go. Set the environment variable `FIELDES_NO_NATIVE=1` to see every prism and sweep drawn from its field, to compare.
