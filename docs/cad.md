# CAD operations

The operations a CAD program has that FielDes did not: turning a body about any axis, mirroring it in any plane, patterns, revolving a profile, cutting by a plane, holes, a few more
shapes, putting one body against another, and measuring. They are in `python/fieldes/stdlib/cad.py`, they are in the right-click menus (*Moving*, *Repeating*, *Extruding*, *Combining*, and the
parametric shapes), and example 37 uses all of them on one part.

```python
from fieldes import *

arm = rotate(bar, axis=(1, 1, 0), angle=30, center=(0, 0, 0))            # about any axis through any point
both = mirror(arm, normal=(1, 0, 0), point=(0, 0, 0))                    # the arm and its mirror image
studs = linear_pattern(stud, count=4, step=(15, 0, 0))                   # four studs, 15 mm apart
blades = circular_pattern(blade, count=6, axis=(0, 0, 1))                # six blades round the z axis
wheel = revolve(profile, angle=270, axis_point=(0, 0), axis_direction=(0, 1))
top, bottom = split(block, normal=(0, 0, 1), point=(0, 0, 5))
plate = hole(plate, position=(10, 10, 4), diameter=5, kind='countersink', head_diameter=10)
```

Every one takes plain numbers for places, directions, angles and counts (a point of the script is taken for a place), lengths in mm and angles in **degrees**, and returns a body that
is made of the ones it was given, so it can be meshed, offset, shelled, exported and measured like any other. (`rotate_x`, `rotate_y` and `rotate_z` of the library work in radians,
about the axes of the coordinates; `rotate` is the general one.)

## Turning and mirroring

| call | what it makes |
|---|---|
| `rotate(body, axis=(0, 0, 1), angle=0, center=(0, 0, 0))` | the body turned by `angle` degrees about the line through `center` along `axis` (right-hand rule) |
| `reflect(body, normal=(1, 0, 0), point=(0, 0, 0))` | the mirror image in the plane through `point` square to `normal`; the body itself is gone |
| `mirror(body, normal=(1, 0, 0), point=(0, 0, 0))` | the body **and** its mirror image, as one body (they fuse where the body crosses the plane) |

`reflect_x`, `symmetric_x` and friends of the library are the same for the planes of the coordinates (`symmetric_x` keeps one half and mirrors it onto the other).

## Patterns

| call | what it makes |
|---|---|
| `linear_pattern(body, count, step)` | `count` copies in a row, the first where the body is, each `step` (x, y, z) from the one before |
| `circular_pattern(body, count, axis=(0, 0, 1), center=(0, 0, 0), angle=360)` | `count` copies round a line: evenly spread over the whole turn (the last is not on the first), or over `angle` degrees with the last copy at `angle` |

The copies are one body: a pattern of holes is a pattern of **cutters** (see *Holes*), subtracted once. A grid is a pattern of a pattern, or `array_xy`.

## Revolving

`revolve(profile, angle=360, axis_point=(0, 0), axis_direction=(0, 1))` turns a 2D shape about a line in its own plane (the plane of x and y: a 2D shape is what it is at z = 0). The line
goes through `axis_point` with the direction `axis_direction` -- the y axis by default -- and the profile may lie on either side of it (or both) for the whole turn. With `angle` less than 360 the
profile must lie on the **right** of the axis, seen along its direction (x > 0 for the y axis), and it turns from its plane toward +z.

```python
ring = revolve(rectangle((2, 0), (4, 5)))                       # a tube, 2 to 4 mm from the y axis, 5 long
quarter = revolve(rectangle((2, 0), (4, 5)), angle=90)           # a quarter of it
standing = rotate(ring, (1, 0, 0), 90)                           # stood up on z
```

## Cutting by a plane

`cut_plane(body, normal=(0, 0, 1), point=(0, 0, 0), keep='above')` keeps the part of the body on the side the normal points to (`keep='below'`: the other side); `split(body, normal, point)`
returns both: `(above, below)`.

## Holes

`hole(body, position, diameter, depth=None, direction=(0, 0, -1), kind='simple', head_diameter=None, head_depth=None, angle=90)`: a hole that starts on the surface at `position`, goes `depth` into the
body along `direction` (through everything when `depth` is None; the default direction is down), `diameter` wide.

| `kind` | what it adds at the mouth |
|---|---|
| `'simple'` | nothing |
| `'counterbore'` | a flat-bottomed step `head_diameter` across and `head_depth` deep |
| `'countersink'` | a cone that opens to `head_diameter` at the surface, `angle` degrees across (90 for a metric screw) |

`hole_cutter(...)` takes the same arguments (but the body) and returns the shape of the hole: union the cutters of a pattern and subtract them once -- `circular_pattern(hole_cutter(...), 6)`.
The hole is the shape of the hole: it makes no thread. A hole in any direction is `direction=(x, y, z)`.

## More shapes

| call | what it makes |
|---|---|
| `capsule(radius, height, base=(0, 0, 0))` | a cylinder with a hemisphere on each end, standing along z; `height` is the whole length (at least twice the radius) |
| `tube(outer_radius, inner_radius, height, base=(0, 0, 0))` | a hollow cylinder along z |
| `slot(length, width, center=(0, 0))` | a 2D obround (a rectangle with a half circle on each short end), `length` along x; extrude it for a slot in a plate |

## Putting one body against another

`align(body, to, axis='z', mode='center', gap=0)` moves the body along one axis so that its box lines up with the box of another body: `mode` `'min'` (low sides on one plane), `'max'`,
`'center'`, `'above'` (on the high side of `to`, `gap` mm between) or `'below'`. For two faces that are not parallel to an axis, or two points, use `snap`.

## Measuring

| call | what it measures |
|---|---|
| `surface_area(body, element_size=None)` | the area of the surface, mm^2, from a mesh of triangles on it (the default size is a fortieth of the longest side; the area is a little low for curves) |
| `section_area(body, normal=(0, 0, 1), point=(0, 0, 0), resolution=300)` | the area of the cross section by a plane, mm^2, counted on a grid of `resolution` cells along its longer side |
| `interference(a, b)` | the volume the two bodies have in common, mm^3 (0 when they only touch) |
| `distance_between(a, b, element_size=None)` | the shortest distance between the surfaces, mm; 0 when they touch or overlap: the closest pair of vertices of a mesh of each surface, improved by going from one surface to the other along the field's gradient (exact for planes and spheres; the checks demand 0.05 mm) |
| `mass_properties(body, density)`, `volume_of(body)` | volume, mass, centroid (already in the library) |
| `bounding_box(body)`, `center(body)` | the box round a body and its middle (already in the library) |

The areas, the overlap and the distance are measured on samples, and say how far they can be off (the checks in `tests/internal/cad_checks.py` demand 1 to 3 %).

## Limits

* The functions take numbers, not `var()` numbers: a number that is dragged by a gizmo or a handle is not one of them (the body that comes out can be moved by its own gizmo as any body).
* A partial `revolve` needs the profile on the right of the axis; a profile that crosses the axis is only meant for the whole turn.
* `hole` makes the shape of a hole, not a thread; `distance_between` starts from the vertices of a mesh of the surface and improves the closest pair by projection, which finds the nearest points
  of smooth and flat surfaces and may stop at a local nearest pair for a body with several close approaches (give it `element_size` finer than the features).
* A body made by these operations is a field like any other: the sharp edges of a pattern of patterns are as sharp as the fields make them, and a very large pattern is slow to draw
  (every copy is another call of the field: 100 copies of a part cost 100 times as much).
