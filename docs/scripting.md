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
numbers), which the model tree writes for you.

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
`thicken`, `shell_inside`, `round_edges`, `fillet`, `repeat`, `twist_z`, `bend_z`, `mirror_x/y/z`.

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
