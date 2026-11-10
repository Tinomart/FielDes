# CAD operations

The operations a CAD program has that FielDes did not: turning a body about any axis, mirroring it in any plane, patterns, revolving a profile, cutting by a plane, holes (with the sizes of the
screws, tapped holes and real threads), a few more shapes, putting one body against another, and measuring. They are in `python/fieldes/stdlib/cad.py`, they are in the right-click menus
(*Moving*, *Repeating*, *Extruding*, *Combining*, and the parametric shapes), and example 37 uses them on one part.

```python
from fieldes import *

arm = rotate(bar, axis=(1, 1, 0), angle=30, center=(0, 0, 0))            # about any axis through any point
both = mirror(arm, normal=(1, 0, 0), point=(0, 0, 0))                    # the arm and its mirror image
studs = linear_pattern(stud, count=4, step=(15, 0, 0))                   # four studs, 15 mm apart
blades = circular_pattern(blade, count=6, axis=(0, 0, 1))                # six blades round the z axis
wheel = revolve(profile, angle=270, axis_point=(0, 0), axis_direction=(0, 1))
top, bottom = split(block, normal=(0, 0, 1), point=(0, 0, 5))
plate = hole(plate, position=(10, 10, 4), screw='M6', kind='counterbore')        # the hole for an M6 socket head screw
plate = hole(plate, position=(30, 10, 4), screw='M6', kind='tapped', modeled=True)   # an M6 thread in it
```

Every one takes numbers for places, directions, sizes and angles, in mm and **degrees**, and returns a body that is made of the ones it was given, so it can be meshed, offset, shelled,
exported and measured like any other. A number may be a **`var()`**: a gizmo or a handle drags it and the body follows (a count is a whole number, and so is the direction of a hole). What
comes out is a body in the model tree -- a 3D shape with its gizmo; `slot` is a 2D shape. (`rotate_x`, `rotate_y` and `rotate_z` of the library work in radians, about the axes of the coordinates;
`rotate` is the general one.)

## Mirror, reflect, and the Symmetry feature

| | what it is | a link to the original? |
|---|---|---|
| `reflect(body, normal, point)` | the mirror image in the plane through `point` square to `normal`; the body itself is gone | none |
| `mirror(body, normal, point)` | the body **and** its mirror image, fused into one new body | none |
| a **symmetry** (`symmetry_planes` + `symmetry_images`, Shift+S) | mirror planes through a centre; what you make while it is selected is made once and its images are live | the images follow every edit, either way round |

`reflect` and `mirror` are one-off operations on a body: the result is a finished, independent body, and changing the original afterwards changes nothing in it. Use them when you want a
mirrored body as an input of something else (a difference, a lattice, an offset), or in a plane the symmetry feature does not have in the way you want. A symmetry is for the other case: you
keep editing one half and want the other to follow. They do not interfere (they share nothing), but a part that has both gets its copies twice, so choose one for a part. For the planes
of the coordinates the library has had `reflect_x`, `symmetric_x` and friends all along: `reflect` is the same for any plane.

## Turning and mirroring

| call | what it makes |
|---|---|
| `rotate(body, axis=(0, 0, 1), angle=0, center=(0, 0, 0))` | the body turned by `angle` degrees about the line through `center` along `axis` (right-hand rule) |
| `reflect(body, normal=(1, 0, 0), point=(0, 0, 0))` | the mirror image |
| `mirror(body, normal=(1, 0, 0), point=(0, 0, 0))` | the body and its mirror image as one body (they fuse where the body crosses the plane) |

## Patterns

| call | what it makes |
|---|---|
| `linear_pattern(body, count, step)` | `count` copies in a row, the first where the body is, each `step` (x, y, z) from the one before |
| `circular_pattern(body, count, axis=(0, 0, 1), center=(0, 0, 0), angle=360)` | `count` copies round a line: evenly spread over the whole turn (the last is not on the first), or over `angle` degrees with the last copy at `angle` |

The copies are one body: a pattern of holes is a pattern of **cutters** (see *Holes*), subtracted once. A grid is a pattern of a pattern, or `array_xy`.

A pattern is the union of its copies, exact everywhere; the union is built in pairs, so that a pattern of hundreds of copies is a tree as deep as the logarithm of their number.

## Revolving

`revolve(profile, angle=360, axis_point=(0, 0), axis_direction=(0, 1))` turns a 2D shape about a line in its own plane (the plane of x and y: a 2D shape is what it is at z = 0). The line goes
through `axis_point` with the direction `axis_direction` -- the y axis by default. For the whole turn the profile may lie on either side of the line, or across it. With less than the whole
turn the part on the **right** of the axis (seen along its direction: x > 0 for the y axis) turns from its plane toward +z, and the part on the left turns the same way round the axis (so toward
-z): a profile may lie on either side, or on both, with any angle. A `var()` angle always stops at the angle (360 is a whole turn as a plain number).

```python
ring = revolve(rectangle((2, 5), position=(3, 2.5)))                       # a tube, 2 to 4 mm from the y axis, 5 long
quarter = revolve(rectangle((2, 5), position=(3, 2.5)), angle=90)           # a quarter of it
left = revolve(rectangle((2, 5), position=(-3, 2.5)), angle=90)            # the same from a profile on the other side
standing = rotate(ring, (1, 0, 0), 90)                           # stood up on z
```

## Cutting by a plane

`cut_plane(body, normal=(0, 0, 1), point=(0, 0, 0), keep='above')` keeps the part of the body on the side the normal points to (`keep='below'`: the other side); `split(body, normal, point)`
returns both: `(above, below)`.

## Holes

`hole(body, position, diameter=None, depth=None, direction=(0, 0, -1), kind='simple', head_diameter=None, head_depth=None, angle=90, screw=None, fit='medium', pitch=None, thread_depth=None, modeled=False, hand='right')`:
a hole that starts on the surface at `position`, goes `depth` into the body along `direction` (through everything when `depth` is None; the default direction is down), `diameter` wide -- or the
size of the screw it is for.

| `kind` | what it makes |
|---|---|
| `'simple'` | a plain hole |
| `'counterbore'` | a flat-bottomed step `head_diameter` across and `head_depth` deep at the mouth |
| `'countersink'` | a cone that opens to `head_diameter` at the surface, `angle` degrees across (90 for a metric screw) |
| `'tapped'` | the hole of a thread: the tap drill; with `modeled=True` the thread itself |

**`screw='M6'`** gives the sizes from the screw (`M2` to `M24`, metric coarse; `pitch=` for a fine thread): the clearance hole it passes through (`fit='medium'`, ISO 273; `'close'`), and for a
counterbore the room of a socket head cap screw (ISO 4762: the head diameter + 1 mm across, the head height + 0.5 mm deep), for a countersink a 90 degree flat head (2.24 times the nominal diameter
+ 0.4 mm), for a tapped hole the tap drill (the nominal diameter less the pitch: 5.0 for M6, 6.8 for M8, 8.5 for M10). Anything you give as well (`diameter=`, `head_diameter=`) is used instead.
`screw_spec('M6')` says the numbers: pitch, tap drill, minor diameter, clearances, head size.

A **tapped hole** is the tap drill alone (what a drawing shows as a cosmetic thread), or, with `modeled=True`, the thread: an ISO profile (60 degrees, the flats of a root and a crest) of
right-hand turns (`hand='left'` for a left-hand one) over `thread_depth` (the depth of the hole, else three diameters). A modelled thread asks for a fine mesh -- a pitch every 8 cells at least --
so for a general model leave it cosmetic and model it where it is made or shown. `threaded_rod(4, length)` (the radius 4 and the pitch 1.25 of an M8) is the external thread, a rod standing along z, that fits it.

`hole_cutter(...)` takes the same arguments (but the body) and returns the shape of the hole: union the cutters of a pattern and subtract them once -- `circular_pattern(hole_cutter(...), 6)`.
A hole in any direction is `direction=(x, y, z)`; `position`, `diameter`, `depth` and the head sizes may be `var()` numbers, so a hole is dragged and resized.

## More shapes

| call | what it makes | its handles (the faces you drag) |
|---|---|---|
| `capsule(radius, height)` | a cylinder with a hemisphere on each end, standing along z; `height` is the whole length (at least twice the radius) -- its field is the exact distance to its surface | the side (the radius) and the two ends |
| `tube(outer_radius, inner_radius, height)` | a hollow cylinder along z | the outer wall, the bore, the two ends |
| `slot(length, width)` | a 2D obround (a rectangle with a half circle on each short end), `length` along x; extrude it for a slot in a plate | the four sides: an end pulled makes it longer (the round end stays), a long side pulled makes it wider (both ends stay round) |
| `threaded_rod_normed(screw, length, None, 'right')` | the rod of a standard screw (`'M8'`, M2 to M24; `pitch=` for a fine thread): the diameter, the pitch and the ISO profile are the screw's, along z; it fits `hole(..., kind='tapped', modeled=True)` of the same screw | none: the size of a screw is not a number to pull |
| `threaded_rod(radius, length, None)` | a threaded cylinder along z, as big as a `cylinder` of the same two numbers; the thread is in proportion to the radius (the pitch is 0.3125 radii, the proportion of an M8, unless `pitch` says another) and 0.6134 pitches deep | the side (the radius: the pitch and the depth of the thread follow it, so a thinner rod has a thinner thread) and the two ends |

### Bodies made of points: `morph_polyhedron` and `morph_body`

`morph_polyhedron([p1 ... pn])` and `morph_body([p1 ... pn])` are the 3D equivalent of a `profile`: a body made of four points or more (not all in one plane). With
`morph_polyhedron` every point is a corner by default (a flat face between the points, a sharp corner at each point: a polyhedron); with `morph_body` every point is a spline point (the body is smooth, its surface goes through
its points, as a spline goes through its points; it is not a polyhedron). The faces are made of the points at every run and follow them, and no list of faces locks a point: they are the faces of the points that are near each other (the convex hull where the body is convex, a point pressed into the body joined to the face over it, a long thin triangle with a corner over 120 degrees cut across its long edge instead, and two nearly flat triangles of which one is a needle made one quad of four points, so that no groove runs down a thin face), which say which points are joined; for a `morph_body` the
surface between them is the smooth surface described below, not flat faces. A flat piece with four corners is one face of four (the eight points of a cube, in the order x changes fastest, then y, then z, have its six squares), any other face is a triangle; points that are all in
one plane, or all in a line, are an error that says so. A part of the body far from an edit keeps its faces. Each point says what it is, as the points of a profile do: `point(position=(..., 0, 0), smooth=True)` a spline point (double click toggles it; the gold ring of its
gizmo, its influence, is how strongly the surface leaves it -- 1 is the spline of a profile, more is rounder, less tighter -- and the surface goes THROUGH the point, exactly, and a point that moves changes only the surface near it; its rotation turns the plane the surface leaves it in by the angle set), `point(position=(..., 0, 0), smooth=False)` a corner (the faces meet in a sharp corner: the edge between two corners is a straight sharp edge, and an edge from a corner to a spline point is sharp along the half of it next to the corner and ends in the smooth surface at its middle; the influence of a corner, the ring of its gizmo, is how far those sharp edges run -- 1 is half the edge, more is a longer and sharper run, less a shorter and softer one), `point(position=(..., 0, 0), radius=r)` a sphere point (the corner rounded by a sphere of radius r: on a box it is cut r x 0.73 deep along the diagonal, as a sphere tangent to the three faces cuts it, and the surface is round about the axis of the point, of about the curvature of a sphere of that radius half a radius from its tip -- not an exact ball, which stood out of the surface as a pimple with a dimple round its foot --, falling steadily away from it, so that there is no edge or neck round it: a smaller radius is a tighter tip; the scale of its gizmo and its influence multiply the radius, as they do the radius of a round corner of a profile -- the radius grows up to half the shortest edge at the point and no further, and a cube whose eight points are sphere points of half an edge is a sphere, to within 3 per cent). A point's gizmo has the parts of its kind: a corner point only the move arrows, a sphere point the move arrows and the scale, a spline point the whole gizmo (move, turn, scale and the gold ring of its influence). The body is drawn from the points' numbers at every step of a drag, so a point that is dragged is followed at once, and a press on a surface pulls the points near it. What is drawn follows the resolution of the view (`view.set_resolution`) the way every other body does: the number of pieces to an edge is an argument of the mesher and any even number, 32 for a cube at the resolution 10, half as many at 5, twice as many at 20 (10 at 3, 38 at 12), up to what keeps a drawing within about sixty thousand triangles (64 pieces for a cube, fewer for a body of many faces). A body of eight points whose squares would pass through each other -- two corners dragged down through its bottom -- has the faces its points make instead, so that dragging points about does not fold the surface through itself.
From the menu (3D shapes) they are made of eight new points to drag (the corners of a cube), or of four or more selected points. **Press the surface of the body and drag, as you press the outline of a profile: a new point is put where
you pressed -- the face of the surface that was there is split round it -- and goes on being dragged: out of the surface for a bump, into it for a dent.** The body has faces whose
corners are the points; they are made of the points at every run, a function of the points and of nothing that happened before (the same in every window, and the same after the file is closed and opened; a square becomes two triangles when a corner leaves its plane by more than about a thousandth of the edge); a script of an earlier version that says them in a list (`body_faces = [...]`) has it ignored. The surface is the limit of a Catmull-Clark subdivision -- C2, and C1 with one tangent plane at a point where a number of edges other than four meet, so the two faces beside an edge have one tangent plane all along it and a ridge along an edge cannot arise -- of control points that are fixed linear combinations of the points: the control points whose limit is the points (the minimal solution, over the points within four edges of each control point, so that a point that moves changes the surface to about five edges around it and not beyond: on a cube of 10 with one corner moved by 3.5 the surface at the opposite corner changes by under 0.3), and the control point at each spline point is solved last, so that its limit is exactly the point: the surface goes through every spline point exactly (to 1e-15 on a cube, also where the points alternate or a corner is pulled far out). The influence of a spline point and its rotation are exact: the plane the surface leaves it in (the first harmonic of the points round it) is turned by the rotation that takes its normal to the turned normal, and its tangent vectors are scaled by the influence. An edge with a corner at an end is a crease for the half of it next to the corner. Faces of three and four that are not a closed surface are an error that says so (a point where two sheets of the surface touch is split into one point for each sheet). The control points are made into expressions of the points' numbers once, in the library;
the viewport evaluates the control points at every render and every step of a drag (each once) and draws the triangles between sums of them directly, so a dragged point is followed at once and nothing is meshed from a field to draw the body. The viewport shades every triangle flat, so the surface is drawn in small triangles (thirty-two pieces to an edge of a cube) to look smooth. The field that operations, exports and the selection of edges and surfaces use is the
exact distance to a tessellation of the same surface (16 pieces to an edge for a body of up to 12 faces, fewer for more), made by the native structure that a mesh import has, with the numbers the points have when the script runs: on a box of 40 x 30 x 20 it lies within 0.1 of the drawn surface, and round edges, offsets, shells and unions of the body cost what they cost any mesh. An operation made of the body follows a dragged point when the point is let go (the body itself follows the hand). A double click on a point makes it a corner, a spline point, a round corner (a radius of a quarter of the shortest edge at the point) and a corner again.
Deleting the body deletes its points.

Along the edge between a corner point and a spline point or a sphere point the crease of the corner fades out: the faces are sharp at the corner and smooth from the middle of the edge on, where they have one tangent plane. The gizmo of a point of a body shows what its kind uses: a corner its arrows and dot, a spline point also the rings that turn the plane its surface leaves it in and the ring of its influence, a sphere point the ring of its radius. A profile or a body of points that is turned or scaled as a whole turns and moves with it the symmetry planes its points are images through -- the planes that split it in half --, when only this body is made through them; a plane that only makes a mirrored clone of the body (the second plane of example 38) stays where it is.

`editable_polyhedron('part.stl')` makes a triangle mesh -- an .stl, .obj, .ply, .off, .3mf, .glb or .gltf file, or a model imported from one -- a body made of points: a point at each vertex, a face for each triangle (a tri mesh is a tri mesh: no face of four, so a box is its twelve triangles), `smooth=True` for a morph body. The mesh has to be closed and at most `max_points=5000` points (a body of corners with flat faces is built in a fraction of a second: 2500 points in less than one): a body of points is edited by hand. In FielDes: File > Import model of a mesh file (.stl, .obj, .ply, .3mf, .glb, .gltf) writes this itself -- the points, the list of faces and the `morph_polyhedron` -- each point a model of its own with its gizmo; a mesh that is not closed, or has more points, is imported as before by `import_model`, and the status bar says why.

Like the primitives (`box`, `cylinder`, `sphere` ...), each of these is **drag adjustable**: it is made of numbers that each stand for one face, so FielDes writes `handles(part, [...])` when you select
it and a face you pull changes the number of that face -- and nothing else (a shape made of arithmetic on a field has no face numbers, and a pull could only scale it).

## Putting one body against another

`align(body, to, axis='z', mode='center', gap=0)` moves the body along one axis so that its box lines up with the box of another body: `mode` `'min'` (low sides on one plane), `'max'`,
`'center'`, `'above'` (on the high side of `to`, `gap` mm between) or `'below'`. For two faces that are not parallel to an axis, or two points, select them and press `S` (a [snap](scripting.md#snapping-a-body-onto-another), which places the body by its own `position=` and `rotation=`).

## Measuring

| call | what it measures |
|---|---|
| `surface_area(body, element_size=None)` | the area of the surface, mm^2, from a mesh of triangles on it (the default size is a fortieth of the longest side; the area is a little low for curves) |
| `section_area(body, normal=(0, 0, 1), point=(0, 0, 0), resolution=300)` | the area of the cross section by a plane, mm^2, counted on a grid of `resolution` cells along its longer side |
| `interference(a, b)` | the volume the two bodies have in common, mm^3 (0 when they only touch) |
| `distance_between(a, b, element_size=None)` | the shortest distance between the surfaces, mm; 0 when they touch or overlap. The surface of each is meshed; the closest pairs of vertices in up to eight different places are found and each is improved by going from one surface to the other along the field's gradient (exact for planes and spheres); the shortest is the distance. A body that comes near the other in several places is measured at each |
| `mass_properties(body, density)`, `volume_of(body)` | volume, mass, centroid (already in the library) |
| `bounding_box(body)`, `center(body)` | the box round a body and its middle (already in the library) |

The areas, the overlap and the distance are measured on samples and say how far they can be off (the checks in `tests/internal/cad_checks.py` demand 1 to 3 %). The number they return is a plain number:
it is the value at the time the script runs, and does not follow a `var()` that is dragged until the script runs again (which it does as the number changes).

## What is a real limit

These are limits of what the functions can be, not of what was built:

* A count is a whole number, and the direction of a hole is a number: neither can be a `var()` (a count that is dragged would change the number of copies, which is a different body, not a moved one).
* A modelled thread has the straight ISO profile -- no run-out, no chamfer at its start (put a countersink in front of it with `hole_cutter`) -- and draws only on a mesh fine enough for its pitch.
* The areas, the overlap and the distance are measures on samples, and have the tolerance said above; the distance finds each of up to eight places two bodies come near each other in, and a pair
  that come near each other in more places than that is measured at the eight closest.
* A body made by these operations is a field like any other: a very large pattern of copies that meet is slow to draw (every copy is another call of the field: 100 copies of a part cost 100 times as much).
