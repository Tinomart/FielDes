# Scripting

FielDes scripts are ordinary Python. A script begins with

```python
from fieldes import *
```

which brings in the whole library (shapes, CSG, transforms, STEP and mesh import, fields, regressions,
lattices, analysis, caching helpers) and `view`, the viewport settings. The full list of functions with
their documentation is in the [library reference](reference.md); this page is the language around them.

- [A shape is a field](#a-shape-is-a-field)
- [What is displayed](#what-is-displayed)
- [Draggable numbers: var()](#draggable-numbers-var)
- [Viewport settings](#viewport-settings)
- [Primitives](#primitives)
- [Combining shapes](#combining-shapes)
- [Moving, rotating, scaling, deforming](#moving-rotating-scaling-deforming)
- [Arithmetic on fields](#arithmetic-on-fields)
- [Low level: your own shapes from x, y and z](#low-level-your-own-shapes-from-x-y-and-z)
- [Your own functions: custom blocks](#your-own-functions-custom-blocks)
- [Units](#units)
- [Errors, printing, long computations](#errors-printing-long-computations)
- [Using the library outside the application](#using-the-library-outside-the-application)

## A shape is a field

A `Shape` is a function of position: a number for every point `(x, y, z)`, **negative inside** the
solid, zero on its surface and positive outside — usually (not always) the distance to the surface.
`sphere(5)` is `sqrt(x² + y² + z²) − 5`. Because shapes are functions they can be added, scaled, clamped
and composed like numbers; the surface is wherever the value is zero.

```python
x, y, z = Shape.X(), Shape.Y(), Shape.Z()     # the coordinate fields
ball = sphere(5)
ball(1, 2, 3)                                 # evaluate: a number (≈ -1.26)
```

Nothing is computed until something asks: the viewport meshes the shape, an analysis samples it, a
regression reads it. That is why an expression such as `ramp(depth_below(part), (0, 30), (1.6, 0.5))`
costs nothing to write and can be used anywhere a number is wanted.

## What is displayed

After the script runs the viewport shows:

- every **top-level expression statement** that evaluates to a shape (or a list of shapes). The last
  line of most scripts is just the name of the shape, or a call that makes it;
- nothing else: assignments (`ball = sphere(5)`) define a name without displaying it. They appear in the
  **model tree**, where the eye adds a line naming the variable (which displays it) or turns it into a
  `# hidden: ball` comment line (which stops displaying it). The editor and the tree edit each other.

```python
ball = sphere(5)
hole = cylinder_z(1, 12, (0, 0, -6))
# hidden: hole
difference(ball, hole)          # displayed
ball                            # also displayed: a second model
```

A list of shapes is shown as all of them: `[part for part, _ in parts]`.

## Draggable numbers: var()

Wrap a literal number in `var(...)` and it becomes a **handle**: hover the shape it affects and drag,
and the number in the script is rewritten live.

```python
r = var(3)
sphere(r)
```

The argument must be a constant (`var(3)`, `var(-2.5)`). Shapes made from `var()`s are dragged natively:
the viewport finds the `var` numbers that place the surface under the cursor. For everything else —
a part imported from STEP, a shape written with plain numbers — use [handles](handles.md):
`handles(part, …)` (a gizmo) and `expose(part, [var(..), …])` (the surfaces of the part as draggable
numbers), which the model tree writes for you; `lock(part)` makes a shape undraggable again.

## Viewport settings

```python
view.set_bounds([-10, -10, -10], [10, 10, 10])    # the region meshed (mm)
view.set_resolution(10)                           # samples per mm
view.set_quality(8)                               # mesh accuracy, 1 (coarse) to 10
```

They are statements like any other and appear under *Render settings* in the tree. For imports,
`view.set_bounds(*roi(parts))` sets the region to enclose them and
`view.set_resolution(roi_resolution(parts))` picks a resolution for the parts (each part is meshed at
its own); see [Region and resolution](step-import.md#region-and-resolution).

## Primitives

3D: `sphere`, `box`, `box_exact`, `box_centered`, `box_exact_centered`, `box_mitered`, `rounded_box`,
`cube`, `cylinder`, `cylinder_z`, `cone`, `cone_z`, `cone_ang`, `cone_ang_z`, `pyramid_z`, `torus`,
`torus_z`, `half_space`, `gyroid`; 2D: `circle`, `ring`, `rectangle`, `rounded_rectangle`, `triangle`,
`polygon`; `extrude_z` turns a 2D shape into a solid; `text` makes lettering; `emptiness` is the empty
shape. Free-form and analytic surfaces: `ellipsoid`, `elliptic_cylinder`, `hyperboloid`, `paraboloid`,
`quadric`, `extruded_curve`, `revolved_curve`, `helical_curve`, `helical_sweep`, `helical_revolve`,
`coil_spring`, `wave_block`.

`box(a, b)` is the box between two corners; the `_exact` variants have a Euclidean distance metric
(a true distance outside the box, which matters for offsets and analyses), `_mitered` ones keep sharp
edges when offset.

## Combining shapes

```python
union(a, b, c)            difference(a, b)           intersection(a, b)
blend(a, b, 0.5)          blend_difference(a, b, 0.5)        # smooth versions
offset(a, 1.5)            shell(a, 2.0)              clearance(a, b, 0.3)
loft(a, b, z0, z1)        morph(a, b, t)
```

and the field-based ones in [Fields](fields.md): `smooth_union`, `smooth_difference`,
`smooth_intersection`, `chamfer_union`, `offset_by(part, field)` (an offset that varies in space),
`thicken`, `shell_inside`, `round_edges`, `fillet`, `smooth`, `repeat`, `twist_z`, `bend_z`, `mirror_x/y/z`.

## Moving, rotating, scaling, deforming

`move`, `rotate`, `rotate_x/y/z`, `scale_x/y/z`, `scale_xyz`, `reflect_*`, `symmetric_*`, `shear_x_y`,
`taper_x_y`, `taper_xy_z`, `twirl_*`, `attract_*`, `repel_*`, `revolve_y`; array helpers `array_x`,
`array_xy`, `array_xyz`, `array_polar`, `array_polar_z`. A part imported from STEP can be moved, rotated,
scaled or mirrored with them and remembers where it came from, so `exclude()` (the exact STEP surface)
follows it.

## Arithmetic on fields

Shapes support `+ - * / **`, unary minus, `abs`, and the methods `.min(other)`, `.max(other)`,
`.sqrt()`, `.square()`, `.sin()`, `.cos()` and more. Union is `min`, intersection is `max`,
`difference(a, b)` is `a.max(-b)`:

```python
thick = part - 0.02 * result.von_mises      # grow the part where it is highly stressed
```

Numbers and shapes mix freely, and sequences of three numbers are points
(`cylinder_z(1, 12, (0, 0, -6))`, each coordinate may be a `var()` or a field).

**Field × field works like number × number**, pointwise -- a number is only a field with the same value everywhere:

```python
a = distance_to_point((0, 0, 0))
b = x_field() + 10
c = a * b                       # at every point: the distance times (x + 10)
d = 2 ** b                      # 2 to the power of the field
e = (a - 3) / (b + 1)           # NaN only where the divisor is zero
```

Nothing about it is unpredictable: the result at a point is the result of the same operation on the two numbers there. What
can surprise is the maths itself -- a divisor that reaches 0 or a negative value under a fractional power gives NaN at those
points, and a negative factor turns the sign (inside and outside swap where a *body's* values are multiplied by one).

The same is written as functions, which work in the model tree (drag, drop, the right-click menu: **Operation → Field math**) and
always make a **field**, also from bodies: `add_fields(a, b, ...)`, `subtract_fields(a, b, ...)`, `multiply_fields(a, b, ...)`,
`divide_fields(a, b, ...)`, `power_field(a, b, ...)`, `min_fields(...)`, `max_fields(...)`, `abs_field(a)`, `negate_field(a)`,
`sqrt_field(a)`, `square_field(a)`.

**A body keeps its true scale.** The values of a body are a distance to its surface, which a factor would stretch: `2 * sphere(5)`
is not a sphere of any size. To compute with the values of a body, make a field of it: **`field_from_body(part)`** has the same
number at every point as the body, but it is a *field* -- it is not drawn, it is not a part, and it can be multiplied, divided or
powered and still be a valid field to give to a lattice, an offset or an analysis. (Select it in the model tree and the
[field viewer](interface.md#the-field-viewer) shows its values.)

**A field goes wherever a number goes** — that is the rule of the library, not a feature of a few functions:
`offset(part, 1.0)` and `offset(part, ramp(z_field(), (0, 40), (0.2, 2.0)))` are written alike. See
[Fields everywhere](fields.md#fields-everywhere) for where it holds and for the few numbers that must stay numbers (counts,
tolerances, resolutions). A **point** (`point(x, y, z)`) is a model of its own and is accepted where a coordinate goes:
`distance_to_point(anchor)`.

## Low level: your own shapes from x, y and z

Everything above is built on one thing: a shape is a tree of arithmetic over the three coordinates, and the library is only a
collection of ready-made trees. The kernel's low-level interface (libfive documents it in Scheme: `define-shape`, `remap-shape`)
is there in Python, and you can write a shape from nothing but `x`, `y` and `z`:

```python
from fieldes import *
from fieldes.shape import shape      # shape(f) calls f(x, y, z) with the three coordinates as trees, and gives the tree

def maximum(*terms):                 # (Python's max cannot compare trees: .max is the method)
    out = terms[0]
    for t in terms[1:]:
        out = out.max(t)
    return out

cube = shape(lambda x, y, z: maximum(x - 6, -6 - x, y - 6, -6 - y, z - 6, -6 - z))      # (define-shape (cube x y z) ...)

x, y, z = Shape.X(), Shape.Y(), Shape.Z()
turn = z * 0.14
twisted = cube.remap(turn.cos() * x + turn.sin() * y,        # (remap-shape (cube x y z) ...): ask the cube about
                     turn.cos() * y - turn.sin() * x,        # other coordinates, here turned about z by an angle
                     z)                                      # that grows with z
twisted
```

- **The coordinates** are `Shape.X()`, `Shape.Y()`, `Shape.Z()` (the same as `x_field()`, `y_field()`, `z_field()`, which the model tree
  lists as fields); numbers go where a tree goes.
- **The operations** are the operators `+ - * / ** %`, unary minus, and the methods `.min`, `.max`, `.abs`, `.sqrt`, `.square`,
  `.pow`, `.sin`, `.cos`, `.tan`, `.asin`, `.acos`, `.atan`, `.atan2`, `.exp`, `.log`, `.nth_root`, `.nanfill` and `.compare`.
- **`s.remap(x', y', z')`** gives the shape that, at `(x, y, z)`, has the value `s` has at `(x'(x, y, z), y'(...), z'(...))`.
  Every transform is a remap: `s.remap(x - 28, y, z)` is `s` moved 28 mm along x.
- **A shape is a function and a tree you can look at**: `cube(10, 0, 0)` is a number (4.0), `print(cube)` the tree, `.optimized()` a
  simplified copy, `Shape.var()` a free number (the numbers you can drag are `var(...)`).
- What comes out is an **ordinary body** (or a field, if you use it as one): `union`, `difference`, `move`, a lattice, an analysis take it
  as they take a box. A shape made from a `max` is not an exact distance, so the operations that expect a distance (`offset`,
  `shell_inside`) give a size that is about right, not exact.

`examples/21_low_level.py` does this for a cube, a twist, a ball and a torus joined by a smooth minimum of its own, and a gyroid lattice.

## Your own functions: custom blocks

A function of your own that is there in every script — without importing it — is a **block**: put it in a `.py` file of the blocks
folder (Settings → Blocks folder…). See [Custom blocks](blocks.md). (A function you need in one script only is an ordinary
function of that script, or a module next to it: `from bracket import make_bracket`.)

## Units

Everything is millimetres, except where the function says otherwise. A STEP file's own units are
converted on import (`units='mm'` by default); STL/OBJ/PLY files are assumed to be millimetres
(`file_units=`); analyses use N, mm and MPa, and thermal analyses W, mm and °C.

## Errors, printing, long computations

- `print(...)` goes to the [output pane](interface.md#the-output-pane). So does the value of a last
  expression that is not a shape.
- An error shows its traceback there and underlines the line in the editor; what ran before it is
  still displayed.
- Operations that take time report progress: imports, analyses and optimisations show a bar, and
  `progress(fraction, "text")` does the same for your own loops.
- Most expensive constructions are [cached](caching-and-performance.md), so editing the last line of a
  script does not redo the import or the analysis above it.
- **Breakpoints** (`F9`) stop the run before a statement so you can look at what has been built.

## Using the library outside the application

```python
import os
os.add_dll_directory(r"C:\path\to\FielDes")       # the folder holding fieldes.dll
from fieldes import *

part, (lo, hi) = import_step_parts("bracket.step")[0]
print(mass_properties(part, density=2.7, lower=lo, upper=hi)["mass"], "g")
part.save_stl("bracket.stl", lo, hi, resolution=4)
```

`view.set_*` are remembered in `view.settings` and used by nothing. `python scripts/run_example.py
script.py` runs a whole script this way.
