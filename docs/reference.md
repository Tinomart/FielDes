# Library reference

Every public function and class of the FielDes library, generated from its docstrings
(`python scripts/gen_reference.py`).  `from fieldes import *` brings all of them in.

Contents: [Primitive shapes](#primitive-shapes) | [Combining shapes (CSG)](#combining-shapes-csg) | [Moving, rotating, scaling, deforming](#moving-rotating-scaling-deforming) | [Placement: where an object is, how it is turned, how big and how see-through](#placement-where-an-object-is-how-it-is-turned-how-big-and-how-see-through) | [Text](#text) | [Importing models](#importing-models) | [The region and resolution of imported models](#the-region-and-resolution-of-imported-models) | [Triangle meshes](#triangle-meshes) | [Exporting files](#exporting-files) | [Handles: editing shapes by dragging](#handles-editing-shapes-by-dragging) | [Linked copies](#linked-copies) | [Symmetry](#symmetry) | [Points and surfaces](#points-and-surfaces) | [Lines and curves: 1D shapes made of points](#lines-and-curves-1d-shapes-made-of-points) | [Profiles and extrusions](#profiles-and-extrusions) | [Measuring a body](#measuring-a-body) | [Fields](#fields) | [Regressions and data](#regressions-and-data) | [Surfaces and offsets](#surfaces-and-offsets) | [Lattices](#lattices) | [Lattices that follow a surface](#lattices-that-follow-a-surface) | [Selecting surfaces](#selecting-surfaces) | [Corners and edges of a body](#corners-and-edges-of-a-body) | [Structural analysis and topology optimization](#structural-analysis-and-topology-optimization) | [Seeing the boundary conditions](#seeing-the-boundary-conditions) | [Thermal analysis and thermal topology optimization](#thermal-analysis-and-thermal-topology-optimization) | [Fluid flow analysis](#fluid-flow-analysis) | [Meshing for the solver of another program](#meshing-for-the-solver-of-another-program) | [CAD operations: turning, mirroring, patterns, revolving, holes, measuring](#cad-operations-turning-mirroring-patterns-revolving-holes-measuring) | [Solvers of your own](#solvers-of-your-own) | [Units](#units) | [Parametric optimization](#parametric-optimization) | [Manufacturing: the constraints](#manufacturing-the-constraints) | [Manufacturing: the check](#manufacturing-the-check) | [Manufacturing: the shell that changes the part](#manufacturing-the-shell-that-changes-the-part) | [Excluding regions from an analysis](#excluding-regions-from-an-analysis) | [Telling fields from numbers](#telling-fields-from-numbers) | [The shape class](#the-shape-class) | [Progress of a long step](#progress-of-a-long-step) | [Caching](#caching) | [Keeping rendered meshes (render cache)](#keeping-rendered-meshes-render-cache) | [A resolution of its own for one body](#a-resolution-of-its-own-for-one-body) | [FielDes apps: the inputs of a script](#fieldes-apps-the-inputs-of-a-script)

## Primitive shapes

Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.shapes

Every shape has ONE primitive, with that shape and nothing else: `box`, `cylinder`, `sphere`, ... (no centred or mitered or `_z` variants:
a shape that stands another way is turned with rotate_x / rotate_y / rotate_z, one that sits somewhere else is moved with move).
The 3D shapes stand along z, the 2D shapes lie in the plane z = 0.

### `array_polar(shape, n, center=(0, 0))`

Iterates a shape about an optional center position

### `array_polar_z(shape, n, center=(0, 0))`

Iterates a shape about an optional center position

### `array_x(shape, nx, dx)`

Iterates a part in a 1D array

### `array_xy(shape, nx, ny, delta)`

Iterates a part in a 2D array

### `array_xyz(shape, nx, ny, nz, delta)`

Iterates a part in a 3D array

### `box(a, b)`

A box between two opposite corners (a true distance outside it: an offset of it has rounded edges)

### `circle(r, center=(0, 0))`

A 2D circle with the given radius and optional center

### `cone(radius, height, base=(0, 0, 0))`

A cone with the given base radius and height, standing along z on the (optional) base position: the base is a disc of that radius at the base height and the apex stands
`height` above its centre.  It has two faces, the base and the side: its handles pull the base (it moves up or down) and the side (the cone grows or shrinks at the same slope, round
its base), and the axis stays where it is.  The apex is rounded by half a percent of the base radius: a perfect apex is a point the surface comes to from every side at once, and a
mesh comes out ragged there at any resolution

### `cylinder(r, h, base=(0, 0, 0))`

A cylinder with the given radius and height, standing along z on the (optional) base position

### `emptiness()`

A value which is empty everywhere

### `half_space(norm, point=(0, 0, 0))`

The solid on one side of a plane, with no end: everything the normal `norm` points away from, the plane through `point` its only face (the field is the distance along `norm` from
the plane, negative inside -- a true distance when `norm` is a unit vector).  It is no shape to look at on its own, and it is not in the menus: it is what `intersection(body, half_space(...))`
and `difference` cut a body by, and a region -- of the `zones` of a mesh, of a support or a load -- that is everything on one side of a plane.  To cut a body by a plane from the menus use
Operations > Combining > cut_plane

### `polygon(r, n, center=(0, 0))`

A polygon with center-to-vertex distance r and n sides.  Its handles pull a side each -- a side moves parallel to itself, the others stay -- and the gizmo's pivot is where the polygon was made, so a side pulled moves that side only, at any scale

### `prism(a, b, c, z0, z1)`

A triangular prism: the triangle of the three corners a, b, c (x, y) between the heights z0 and z1.  Its handles pull a face each -- the three sides and the two ends -- parallel to itself,
and the gizmo's pivot is where the prism was made, so a face pulled moves that face only, at any scale

### `pyramid(a, b, zmin, height)`

A pyramid defined by its base rectangle (the corners a and b), lower Z value, and height: its tip stands above the middle of the base.  It has five faces -- the base and four
slanted ones -- and its handles pull one each, parallel to itself (the slopes stay what they were made with)

### `rectangle(a, b)`

A rectangle between the corners a (the low one) and b (the high one) (a true distance outside it: an offset of it is a rounded rectangle).  Its four numbers are
its four sides, so the handles of a rectangle pull one side each

### `ring(ro, ri, center=(0, 0))`

A 2D ring with the given outer/inner radii and optional center

### `rounded_box(a, b, r)`

Rounded box with the given bounds.  The radius `r` is a fraction, 0 to 1, of half the shortest side of the box (r = 0.4 on a box 15 mm thick rounds its edges with a radius of 3 mm; 1 makes the
shortest side a half circle).  Its handles pull a side each -- a side moves, the others and the rounding stay -- and the gizmo's pivot is where the box was made, so a side pulled moves that side only, at any scale

### `rounded_rectangle(a, b, r)`

A rectangle with rounded corners of radius r, between the corners a (the low one) and b (the high one) (a true distance outside it).  The corners are tangent to the
sides whatever the numbers are: its handles pull a side each (the radius with them)

### `sphere(radius, center=(0, 0, 0))`

A sphere with the given radius and (optional) center

### `torus(ro, ri, center=(0, 0, 0))`

A torus lying round the z axis, with the given outer radius, inner radius, and (optional) center

### `triangle(a, b, c)`

A 2D triangle between three corners.  Its handles pull a side each: a side moves parallel to itself, and the gizmo's pivot is where the triangle was made, so a side pulled moves that side only, at any scale

## Combining shapes (CSG)

Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.csg

### `blend(a, b, m)`

Blends two shapes by the given amount using exponents,
with the blend term adjusted to produce results approximately
resembling blend_rough for values between 0 and 1.

### `blend_difference(a, b, m, o=0)`

Blends the subtraction of b, with optional offset o,
from a, with smoothness m

### `blend_expt(a, b, m)`

Blends two shapes by the given amount using exponents

### `blend_expt_unit(a, b, m)`

Blends two shapes by the given amount using exponents,
with the blend term adjusted to produce results approximately
resembling blend_rough for values between 0 and 1.

### `blend_rough(a, b, m)`

Blends two shapes by the given amount, using a fast-but-rough
CSG approximation that may not preserve gradients

### `clearance(a, b, offset)`

Expands shape b by the given offset then subtracts it from shape a

### `difference(a, b, *rest)`

Subtracts any number of shapes from the first argument

### `intersection(a, *args)`

Returns the intersection of two shapes

### `inverse(a)`

Returns a shape that's the inverse of the input shape

### `loft(a, b, zmin, zmax)`

Produces a blended loft between a (at zmin) and b (at zmax)
a and b should be 2D shapes (i.e. invariant along the z axis)

### `loft_between(a, b, lower, upper)`

Produces a blended loft between a (at lower.z) and b (at upper.z),
with XY coordinates remapped to slide between lower.xy and upper.xy.
a and b should be 2D shapes (i.e. invariant along the z axis)

### `morph(a, b, m)`

Morphs between two shapes.
m = 0 produces a, m = 1 produces b

### `offset(a, o)`

Expand or contract a given shape by an offset
Positive offsets expand the shape; negative offsets shrink it

### `shell(a, offset)`

Returns a shell of a shape with the given offset

### `union(a, *args, radius=0)`

The union of any number of shapes.  radius (mm, default 0: a sharp union) blends the surfaces where they meet
with a smooth transition of that radius, so parts that do not quite fit together are joined by a fillet --
a lattice and the body it is added to, a rib and a plate.  It works on any field, lattices included.
(A radius that is a field makes a blend that varies.)

## Moving, rotating, scaling, deforming

Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.transforms

### `attract(shape, locus, radius, exaggerate=1)`

Attracts the shape away from a point based upon a radius r,
with optional exaggeration

### `attract_x(shape, locus, radius, exaggerate=1)`

Attracts the shape away from a YZ plane based upon a radius r,
with optional exaggeration

### `attract_xy(shape, locus, radius, exaggerate=1)`

Attracts the shape away from line parallel to the Z axis,
with a particular radius and optional exaggeration

### `attract_xz(shape, locus, radius, exaggerate=1)`

Attracts the shape away from line parallel to the Y axis,
with a particular radius and optional exaggeration

### `attract_y(shape, locus, radius, exaggerate=1)`

Attracts the shape away from a XZ plane based upon a radius r,
with optional exaggeration

### `attract_yz(shape, locus, radius, exaggerate=1)`

Attracts the shape away from line parallel to the X axis,
with a particular radius and optional exaggeration

### `attract_z(shape, locus, radius, exaggerate=1)`

Attracts the shape away from a XY plane based upon a radius r,
with optional exaggeration

### `move(shape, v)`

Moves the given shape in 2D or 3D space

### `reflect_x(t, x0=0)`

Reflects a shape about the x origin or an optional offset

### `reflect_xy(t)`

Reflects a shape about the plane X=Y

### `reflect_xz(t)`

Reflects a shape about the plane X=Z

### `reflect_y(t, y0=0)`

Reflects a shape about the y origin or an optional offset

### `reflect_yz(t)`

Reflects a shape about the plane Y=Z

### `reflect_z(t, z0=0)`

Reflects a shape about the z origin or an optional offset

### `repel(shape, locus, radius, exaggerate=1)`

Repels the shape away from a point based upon a radius r,
with optional exaggeration

### `repel_x(shape, locus, radius, exaggerate=1)`

Repels the shape away from a YZ plane based upon a radius r,
with optional exaggeration

### `repel_xy(shape, locus, radius, exaggerate=1)`

Repels the shape away from line parallel to the Z axis,
with a particular radius and optional exaggeration

### `repel_xz(shape, locus, radius, exaggerate=1)`

Repels the shape away from line parallel to the Y axis,
with a particular radius and optional exaggeration

### `repel_y(shape, locus, radius, exaggerate=1)`

Repels the shape away from a XZ plane based upon a radius r,
with optional exaggeration

### `repel_yz(shape, locus, radius, exaggerate=1)`

Repels the shape away from line parallel to the X axis,
with a particular radius and optional exaggeration

### `repel_z(shape, locus, radius, exaggerate=1)`

Repels the shape away from a XY plane based upon a radius r,
with optional exaggeration

### `revolve_y(shape, x0=0)`

Revolves a 2D (XY) shape about a line parallel to the Y axis with the
given x value

### `rotate(t, angle, center=(0, 0, 0))`

Rotate the given shape by an angle in radians
The center of rotation is [0 0 0] or specified by the optional argument

### `rotate_x(t, angle, center=(0, 0, 0))`

Rotate the given shape by an angle in radians
The center of rotation is [0 0 0] or specified by the optional argument

### `rotate_y(t, angle, center=(0, 0, 0))`

Rotate the given shape by an angle in radians
The center of rotation is [0 0 0] or specified by the optional argument

### `rotate_z(t, angle, center=(0, 0, 0))`

Rotate the given shape by an angle in radians
The center of rotation is [0 0 0] or specified by the optional argument

### `scale_x(t, sx, x0=0)`

Scales a shape by sx on the x axis about 0 or an optional offset

### `scale_xyz(t, s, center=(0, 0, 0))`

Scales a shape on all three axes, about 0 or an optional offset

### `scale_y(t, sy, y0=0)`

Scales a shape by sx on the x axis about 0 or an optional offset

### `scale_z(t, sz, z0=0)`

Scales a shape by sx on the x axis about 0 or an optional offset

### `shear_x_y(t, base, height, offset, base_offset=0)`

Shears a shape on the x axis as a function of y
offset = base-offset at base.y
offset = offset = base.y + h

### `symmetric_x(t)`

Clips the given shape at the x origin, then duplicates the remaining
shape reflected on the other side of the origin

### `symmetric_y(t)`

Clips the given shape at the y origin, then duplicates the remaining
shape reflected on the other side of the origin

### `symmetric_z(t)`

Clips the given shape at the z origin, then duplicates the remaining
shape reflected on the other side of the origin

### `taper_x_y(shape, base, h, scale, base_scale=1)`

Tapers a shape along the x axis as a function of y
width = base-scale at base
width = scale at base + [0 h]

### `taper_xy_z(shape, base, height, scale, base_scale=1)`

Tapers a shape in the xy plane as a function of z
width = base-scale at base
width = scale at base + [0 0 height]

### `twirl_axis_x(shape, amount, radius, center=(0, 0, 0))`

Twirls the shape in the x axis about the line extending from the
(optional) center point

### `twirl_axis_y(shape, amount, radius, center=(0, 0, 0))`

Twirls the shape in the y axis about the line extending from the
(optional) center point

### `twirl_axis_z(shape, amount, radius, center=(0, 0, 0))`

Twirls the shape in the z axis about the line extending from the
(optional) center point

### `twirl_x(shape, amount, radius, center=(0, 0, 0))`

Twirls the shape in the x axis about the (optional) center point

### `twirl_y(shape, amount, radius, center=(0, 0, 0))`

Twirls the shape in the y axis about the (optional) center point

### `twirl_z(shape, amount, radius, center=(0, 0, 0))`

Twirls the shape in the z axis about the (optional) center point

## Placement: where an object is, how it is turned, how big and how see-through

Placement: where an object is, how it is turned, how big it is and how see-through.

Every object that is made by a call -- a shape, the result of an operation, a copy, a lattice -- takes four arguments after its own, named and in this order:

    position   (x, y, z) in mm: how far the object is moved from where its definition puts it (0 is where it is made)
    rotation   (x, y, z) in degrees: how it is turned about the middle of its box, x first, then y, then z
    scale      (x, y, z) factors, or one number: how big it is, about the middle of its box
    alpha      0 to 1: how solid it is drawn (1 is solid, 0 is a ghost, 5 % solid at the least): the object is the same in every other way

They are ordinary arguments of the call.  FielDes's model tree shows them inside every object, a click on one selects its number in the code, and the gizmo in the
viewport drags the first three: when the object is selected, FielDes writes them with `var()` numbers (`position=(var(40), var(0), var(0))`) and a drag changes the
numbers in the text.  None of them is needed: an object without them stands where its definition puts it.

    part = box((0, 0, 0), (20, 10, 5), position=(40, 0, 0), rotation=(0, 0, 30), alpha=0.5)

What is not made by a call -- a part taken out of an imported model, a name given to another, a sum -- is placed by `place`, which takes the same arguments:

    part = place(part, position=(40, 0, 0))

A shape that is made of points (a profile, a polyline, a spline) is placed through its points, which are what the gizmo drags.

The arguments are given to every function that makes a model by `install`: a function that has an argument of that name of its own (`hole(position=...)`, `lattice(scale=...)`)
keeps it and has no placement arguments -- a model it makes is placed with `place`.

### `install(namespace)`

Gives the functions of PLACEABLE the placement arguments, in `namespace` (what `from fieldes import *` brings in)

### `place(value, position=None, rotation=None, scale=None, alpha=None)`

`value` placed: moved by `position` (x, y, z), turned by `rotation` (x, y, z degrees) and scaled by `scale` (x, y, z factors, or one number), all about the middle
of its box, and drawn as solid as `alpha` (0 to 1) says.

This is what the placement arguments of every model's call do: `box(a, b, position=(10, 0, 0))` is `place(box(a, b), position=(10, 0, 0))`.  It is written for what is
not made by a call -- a part taken out of an imported model, `part = place(part, position=(10, 0, 0))`.  FielDes's gizmo drags the numbers of either:
write them as `var(...)` (the model tree does that when the model is selected).

### `takes_placement(function)`

Whether a function's calls take the placement arguments (what the model tree asks to know whether a model's placement is written in its call)

### `unplaced(shape)`

(the object as it was before the placement arguments placed it, the arguments), or (shape, None) for an object that was not placed by them

## Text

Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.text

### `text(txt, pos=(0, 0))`

Returns the given text, rendered in a custom f-rep font
(with a character height of 1)

## Importing models

Importing a model: import_model(), reconstruct() and tessellate().

    parts = import_model("bracket.step")           # STEP or mesh: each part the way that suits it
    part, bounds = parts[0]
    view.set_bounds(*roi(parts))

import_model() is the one function to know.  It reads a STEP file (.step, .stp) or a triangle mesh (.stl, .obj, .ply,
.3mf, .glb, .gltf, .fbx, .off, .dxf) and returns a list of (shape, (lower corner, upper corner)), one entry per part -- one for a mesh --
each part placed where the file has it, in the units you ask for.  What is a field here is chosen by the file:

    a mesh                  the exact signed distance to its triangles
    a part of a STEP file   reconstructed (reconstruct()) -- planes, cylinders, cones, spheres and tori as exact
                            formulas, free-form faces fitted: fast, light, and the faces can be dragged -- unless
                            more than `threshold` (10 % by default) of its surface is free-form (B-spline): a
                            sculpted body, a gear, a thread, which a fit does not follow.  That part is tessellated
                            (tessellate()): its exact surface as triangles, made a distance field.  A surface body
                            (an open sheet of faces, which has no inside) is always tessellated
    the choice is made part by part (an assembly can have both); the model tree says which each part is

The other two are the same import, made one way by name when you want that:

    reconstruct(path)       every part rebuilt as CSG from its faces (STEP files only)
    tessellate(source)      the exact surface as a distance field -- of a STEP file's parts, of a mesh file, of ANY
                            field (tessellate(shape): the field's surface made a mesh and the exact distance to it,
                            which makes offsets, shells and lattices uniform), of a list of parts

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `import_model(path, units='mm', file_units=None, rev=None, cache=True, threshold=0.1, quality=64, thickness=None, auto_exclude=False, exclude_threshold=1.0, exclude_quality=64, fit_tolerance=1.0)`

Imports a STEP file (.step, .stp) or a triangle mesh (.stl, .obj, .ply, .3mf, .glb, .gltf, .fbx, .off, .dxf) as a list of
(shape, (lower corner, upper corner)): one entry per part of a STEP file (an assembly comes out assembled: every
part is where the file puts it, a part used several times has an entry for each place), one for a mesh.  Nothing
else to say: the way each part is made a field is chosen for it -- see the module's text:

    parts = import_model("bracket.step")
    part, bounds = parts[2]
    view.set_bounds(*roi(parts))

A part with more than `threshold` of its surface free-form (B-spline) is tessellated, any other
reconstructed -- and so is a reconstructed part whose free-form faces were fitted badly (`fit_tolerance`); a surface
body (an open sheet) is always tessellated.  reconstruct() and tessellate() make every
part one way.  A part that cannot be imported is a FailedPart that says why the moment it is used; the others
import normally.

units       the units of your script ('mm', 'cm', 'm', 'in'...): a STEP file declares its own (each part
            its own; the numbers are converted), 'file' gives the first declared one
file_units  what the numbers of a mesh file mean (the length unit of Settings > Units -- 'mm' by default -- if it does not say: STL, OBJ, PLY and OFF do not; 3MF, FBX and DXF
            with $INSUNITS do, glTF is metres) -- for mesh files only
rev         a number that is part of what an import is kept by: another one imports again ("Reimport" in FielDes)
cache       an import is kept in files next to the STEP file (<file>.fieldes-cache.py, ...-tessellation):
            False: not
threshold   the free-form share of a part's surface from which it is tessellated (0.10: a tenth).  0 tessellates
            every part, 1 reconstructs them all (a surface body is tessellated anyway)
quality     points per full turn of a circle of a tessellated part (64: 0.05 % of a radius off the surface)
thickness   of a surface body that does not close, which has no inside: it is made a sheet this thick (default
            0.4 % of the size of the file), in `units`
fit_tolerance
            a part that is reconstructed has its free-form faces fitted by closed-form surfaces; when the worst of them is
            off by this many percent of the face's size (1.0 by default) the fit does not look like the part, and the part is
            tessellated instead.  None keeps every reconstruction
auto_exclude, exclude_threshold, exclude_quality
            for the reconstructed parts: see reconstruct()

### `reconstruct(path, units='mm', cache=True, rev=None, auto_exclude=False, exclude_threshold=1.0, exclude_quality=64)`

Imports a STEP file (.step, .stp) with every part rebuilt as CSG from its faces, using FielDes's built-in reader:
planes, cylinders, cones, spheres and tori as exact formulas, free-form (B-spline) faces as closed-form surfaces
fitted to them (listed after the import; where a fit is off by more than 0.5 % of its face's size the model is
shaded, turning fully red at 10 %).  Fast, light, and the faces of the result can be dragged -- but a gear, a
thread or a sculpted body does not survive a fit: import_model() tessellates such parts instead (and
tessellate() does it for all).

Returns [(shape, (lower corner, upper corner))], one per solid -- see import_model() for what the list holds,
and for units and rev.  A solid that cannot be rebuilt (a surface body, a face of a kind not supported) is a
FailedPart that says why the moment it is used; the other solids import normally.

cache       the result is kept in <file>.fieldes-cache.py (and a folder of trees) next to the STEP file until the
            file or the import algorithm changes -- a rebuilt library keeps it; False: not
auto_exclude  True excludes every poorly fitted place by itself: where the fit is off by more than
            `exclude_threshold` (percent of the face's size, 1.0 by default; 0.5 is the lowest) the part is its
            exact surface, meshed from the STEP file and made a field, locked against every later operation
            (see exclude(); `exclude_quality` is its quality).  Off by default: the exact surfaces cost
            tessellation time

### `tessellate(source, units='mm', file_units=None, rev=None, cache=True, quality=64, thickness=None, bounds=None, resolution=None)`

The exact surface, as a distance field.  `source` is

a file          a STEP file: every part as the triangles of its faces (free-form faces refined inside their
                outlines until no triangle turns the surface by more than 2 pi over `quality`), made the exact signed
                distance field -- nothing is reconstructed or fitted, so a gear, a thread, a sculpted body or a
                thin wall is what the file says.  A mesh file: what import_model() makes of it.
                Returns [(shape, bounds)] as import_model() does.  A surface body that does not close is made a
                sheet `thickness` thick
a field         any shape: its surface is meshed (`resolution` samples per unit, default about 200 along its
                longest side; `bounds` = ((x0, y0, z0), (x1, y1, z1)) if they cannot be found) and the result is
                the exact signed distance to that mesh.  Booleans, blends and warps have fields that are only
                roughly a distance; after this offsets and shells are uniform.  Returns a shape.  (A feature
                thinner than the mesh cells falls between its samples: raise `resolution`)
a list          of (shape, bounds) -- what import_model() returns -- or of shapes: each tessellated

What it costs: tessellating a part with many free-form faces takes seconds (done once, on all the processor's
threads, kept in <file>.fieldes-tessellation); the distance field a fraction of a second; meshing it 0.5 to 2.8
times as long as a reconstructed part.  What it is not: a rebuilt solid -- the planes of the part are triangles
here, and its faces cannot be dragged.  units, rev, cache: see import_model()

## The region and resolution of imported models

Python library of FielDes, built on the libfive CAD kernel

Hand-written (not code-generated): imports CAD files using FielDes's own
small, dependency-free STEP reader (see kernel/src/step/).

The entry points are import_model(), reconstruct() and tessellate() (fieldes.stdlib.importing); this
module is reconstruct()'s: every
solid is rebuilt as CSG by the reconstruction algorithm (see
step_reconstruct.hpp): analytic faces (plane / cylinder / cone / sphere /
torus) as exact expressions.  A solid that can't be rebuilt doesn't stop the
rest of the file: its slot is a FailedPart (a normal tuple entry with real
bounds) that raises a specific RuntimeError the moment it's actually used.
The reconstruction takes a few seconds on a large file, so the result is
cached in a plain Python file next to the STEP file and only regenerated
when the STEP file changes or a new version of the import algorithm gives
different results (rebuilding the library does not).
B-spline faces are imported as fitted closed-form surfaces (planes,
quadrics, extruded / revolved / helical curves): an approximation, reported
after the import and marked in red where it is poor.

### `FailedPart`

Stands in for a solid that could not be reconstructed as native CSG
(no Oracle fallback exists any more -- see cad_import.py's module
docstring). Its bounds are still real, so the tuple can still be
unpacked and inspected, but ANY attempt to actually use it as a
Shape (meshing it, combining it with other shapes, even just
printing its value) raises immediately with the specific reason,
instead of silently substituting Oracle or approximate geometry.

### `exact_field(sources, quality=64)`

The exact surface of the parts `sources` (_ExactSource) as a field: meshed straight from the STEP file, made
a signed distance field (negative inside the solid), the parts united

### `poor_fit_region(part, threshold=1.0)`

The places where an imported part's B-spline fit is worse than `threshold`
percent of the face's size, as a region: a field that is negative there.
It is made from the part's fit marker (the field that shades the poor fits
grey to red), nothing is sampled; where it is negative, the exact surface of
the STEP file is the better one.  None if nothing of the part was fitted.

    kitchen = exclude(kitchen, poor_fit_region(kitchen[19][0], 2.0))

exclude() uses it when it is given no region.

### `roi(*items, pad=0.1)`

Region of interest covering the given imported models, for
view.set_bounds(*roi(...)).  Accepts whatever the importers
return: a parts list, one (shape, bounds) part, a bounds pair, or
an imported Shape.  Each side is padded by `pad` times the box size.
Returns (xyz_min, xyz_max).

### `roi_resolution(*items, cells=150, part_cells=64, detail_cells=4.0, max_vertices=25000000.0, scene_vertices=2000000.0)`

A render resolution (voxels per unit) for the given models,
view.set_resolution(roi_resolution(...)) -- and, for each imported
part among them, its OWN resolution.

Each imported part knows its smallest feature (a sheet's thickness, a
hollow tube's wall, a pin's or a fillet's radius) and its size.  It is
meshed over a cube around itself, with a voxel of `detail_cells` cells
across that feature, but no coarser than `part_cells` across its own
size, and never to more than about a million and a half vertices by
itself.  So a very thin part, or one with tiny features, renders at its
own, higher resolution; nothing else has to follow it.  Together the
parts stay under about `max_vertices` vertices (about 6 s of meshing
per million): if they would not, the costliest parts are coarsened
first -- down to 1.5 cells across their smallest feature, then 1, and
only then below.  A part left thinner than about a cell is named.

The number returned is the scene's resolution, for the shapes you make
from them (a lattice, a cut): the voxel the typical part asks for,
limited to about `scene_vertices` vertices and never coarser than
`cells` voxels across the models.  Changing it scales every part's
resolution with it.  A part still too fine to render properly is named.

The mesher splits a region in halves until a cell is no larger than
1 / resolution, so a mesh only changes size in steps of four to six
times the vertices: each resolution here is the one that gives a whole
number of halvings.

### `step_length_unit_mm(path)`

Millimetres per length unit of a STEP file, read from the unit its
(first) representation context declares (SI_UNIT(.MILLI.,.METRE.)
-> 1, SI_UNIT($,.METRE.) -> 1000, CONVERSION_BASED_UNIT('INCH', ...)
-> 25.4, ...).  Files that declare nothing are taken to be in mm.
(The importers themselves read every part's own unit: a file can
mix them.)

## Triangle meshes

Triangle-mesh import (what import_model() does with a mesh file): STL (binary or ASCII), Wavefront OBJ, PLY
(ASCII or binary), 3MF, glTF (.glb / .gltf), Autodesk FBX (binary or ASCII: see fbx.py for what that means), OFF and DXF (3DFACE
and polyface meshes) files.

    parts = import_model(r"C:\models\bracket.stl")
    bracket, bounds = parts[0]
    view.set_bounds(*roi(parts))

The mesh becomes an exact signed distance field (negative inside), so it
works with everything else in fieldes.stdlib: union / difference with CSG
shapes, offset / shell / blend, transforms and so on.  Before that its
triangles are cleaned up (duplicate vertices welded, degenerate triangles
dropped, windings made consistent, inside-out shells flipped).  Meshes with
holes still import: near a hole, inside and outside come from the
generalised winding number, which closes it with a smooth membrane.

STL, OBJ and PLY files carry no units: pass file_units= to say what their
numbers mean (e.g. 'm' or 'in'; millimetres otherwise).  3MF files state
their unit and glTF is always in metres; that is used unless file_units=
overrides it.  glTF's Y-up axes are turned into FielDes's Z-up.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `mesh_info(path)`

The summary of the last import of `path` in this session (triangle
count, whether it is watertight, ...) as a dict, or None.

## Exporting files

Exporting: a body as a mesh file, an analysis as a finite-element model and as results, in the files other programs read.

    export_mesh(part, "part.stl")                   .stl .obj .ply .3mf .glb .gltf .fbx .off .dxf  (see meshfiles.py)
    export_fea(result, "bracket.inp")               the model of a static analysis for another solver: .inp (Abaqus, CalculiX), .bdf
                                                    (Nastran), .msh (Gmsh 2.2)
    export_results(result, "bracket.vtu")           what an analysis found: .vtu (VTK / ParaView: the mesh with every field), .csv (the
                                                    nodes and the elements as tables), .txt (the key numbers: maximum stress, safety
                                                    factor, drag ... as text)
    export_field(field, "stress.vti")               any field sampled on a regular grid: .vti (VTK image data), .csv

The files of an analysis are in FielDes's own units: millimetres, newtons, megapascals (so a mass is in tonnes: N, mm, t, s, MPa is a consistent set); only the
key numbers of export_results(..., '.txt') are in the units of Settings > Units, each with its unit.  A mesh file (export_mesh) is in the length unit chosen there.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `export_fea(result, path, **options)`

Writes the finite-element model of a static analysis -- its tetrahedral mesh, the material, the supports and the loads -- for another
solver, in the format of the file's extension:

    .inp   Abaqus and CalculiX: C3D4 elements, *MATERIAL, *NSET of each support and load, *BOUNDARY, *CLOAD, *DLOAD for gravity
    .bdf   Nastran: CTETRA, MAT1, SPC1, FORCE (small-field free format), solution 101
    .msh   Gmsh 2.2: the mesh, with the displacement, the supports and every field of the result as data

The model is the one FielDes solved (the same nodes and tetrahedra).  A force is spread over the nodes of the surface it acts on by the
area each carries, so that the sum is the force.  Materials with a field (a graded stiffness) and thermal_expansion loads are not model
file contents: the first is refused, the second said in a note.  Units: mm, N, MPa, tonne.  Returns the path.

### `export_field(field, path, bounds=None, resolution=None, name='field')`

Samples a field -- anything that is a Shape, a result's field (`result.von_mises`) -- on a regular grid and writes it:

    .vti   VTK image data (ParaView ...): the values as point data
    .csv   x, y, z, value for every grid point

bounds      ((x0, y0, z0), (x1, y1, z1)): the box sampled (the extent of the shape that has one: field.bounds, else this is needed)
resolution  samples per mm (the scene's resolution by default, limited to 2 million points)

A field is not a mesh: this is the sampling of it that other programs work with, at the resolution asked for.  Returns the path.

### `export_layers(part, path, layer_height=0.2, bounds=None, resolution=None, first=None, name=None)`

Writes the layers of a body -- its contours at heights, one layer every `layer_height` mm -- as a DXF: closed 3D polylines, the outer contours and the holes, on a DXF layer
of their own for each (L0001, L0002 ...), at their height.  The contours are read from the body's field by the kernel's 2D contouring: nothing is meshed.  Another program
reads them for a laser or a router, or to see the stack.  It is not G-code and there are no paths, no infill and no perimeters.

    part          a body (a part that manufacturing_optimization made is written turned into the way up of its print, as export_mesh does)
    layer_height  mm between layers (default 0.2); each layer is cut through the middle of its layer (at first + 0.5 layer, ...)
    first         the height of the bottom of the first layer (default: the bottom of the body)
    resolution    samples per mm of the contours (the scene's resolution when not given)

Millimetres.  Returns the path and writes nothing it could not read from the field.

### `export_mesh(part, path, bounds=None, resolution=None, name=None, ascii=False, unit=None, **options)`

Writes the surface of a body as a triangle mesh file, in the format of the file's extension:

    .stl (binary; ascii=True for text), .obj, .ply, .3mf, .glb, .gltf, .fbx (Autodesk FBX, ASCII 7.4: see fbx.py), .off, .dxf (3DFACE)

A mesh of elements -- what mesh_volume and mesh_surface make -- is written as it is, with its zones: .msh (Gmsh), .vtu, .inp (Abaqus), .bdf (Nastran), .unv, .stl,
.obj, or format='fluent' / 'openfoam' (a folder); the options (scale=0.001 for metres, ...) are those of Mesh.export (see meshwriters.py)

part        a body (a shape, a list of them, the design an optimisation made, the body of an analysis' result)
path        the file to write (a folder that is not there is made)
bounds      ((x0, y0, z0), (x1, y1, z1)) the region meshed; the body's own extent when it is not given
resolution  samples per mm of the mesh (the scene's resolution when it is not given: the one of the viewport)
name        the object's name inside the file (the file's name)
ascii       STL as text, not binary
unit        the length unit of the file: 'mm', 'cm', 'm', 'in', 'ft' or 'um'; the one chosen in Settings > Units when it is not given (millimetres by default)

The coordinates are in that unit, and the formats that state their unit (3MF, FBX, DXF) say it (a glTF is in metres, Y up, as glTF is).  Returns the path.

### `export_print(result, path, bounds=None, resolution=None)`

Writes what a print is made of as a 3MF with an object for each: the part (what manufacturing_optimization made, turned so that the way up of the print is z), its supports
(an object of type support) and the volumes where no support may be made (objects of type other, named support_blocker_N), all turned the same way, in millimetres.  A
plain body is the one object.  There is no G-code here and no slicing: the file is for a slicer to read; how a slicer treats the objects of type support and other is its own.
Returns the path.

### `export_results(result, path, **options)`

Writes what an analysis found, in the format of the file's extension:

    .vtu   VTK XML (ParaView, VisIt...): the tetrahedral mesh with the displacement and every field at the nodes, and every field
           in each element (static analyses)
    .csv   the nodes (position, displacement, every field) as a table, and the elements next to it (<name>_elements.csv)
           (static analyses)
    .txt   the key numbers as text, one per line, with their units: maximum stress, safety factor, reactions, drag and lift of a
           flow, temperatures, natural frequencies ... (every kind of result)

Units: mm, N, MPa.  Returns the path.

### `export_slice(part, path, z, bounds=None, resolution=None)`

Writes one layer of a body -- its contours at the height `z` -- as an SVG picture in millimetres (y up as in the model; the outer contours and the holes are one path, filled
with the even-odd rule, so that a hole is a hole).  Read from the body's field, not from a mesh.  Returns the path.

### `key_results(result, count=6)`

The few numbers the result card shows on the screen: [(label, text, unit)] -- what result_summary lists, without the counts and times

### `result_summary(result, native=False)`

The numbers of a result that matter, as [(label, text, unit)] in the order they are shown -- what the result card prints on the screen
and what export_results(..., '.txt') writes.  Only what the result has: a static analysis its stress and safety factor, a flow its
forces and flows, a thermal analysis its temperatures ...  The numbers are in the units chosen in Settings > Units (fieldes.units), each with its
unit; native=True: in millimetres, newtons and megapascals (the card converts them itself when it draws, so that a change of the units needs no new run)

## Handles: editing shapes by dragging

Handles and the gizmo: edit a shape -- an imported part too -- by dragging in FielDes's viewport.

Two ways of editing by dragging work together, and the first has priority where they meet:

  the gizmo    the part's own move arrows, rotation rings and scale knobs.  They drag the placement arguments of the shape's
               call (see fieldes.stdlib.placement), which FielDes writes in the call when the shape is selected,

                   part = box(a, b, position=(var(0), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))

               so what the gizmo does is ordinary script text, like any dragged var().  The shape is scaled about its
               centre, rotated (x, then y, then z; degrees) and moved.  When the gizmo is shown is the state of the model
               tree's gizmo button (key E), which is not in the script:

                   'click'   (the default) the gizmo is shown while the shape is selected, i.e. after you click it
                   'never'   it is never shown, so that it cannot get in the way
                   'always'  it is shown on the shape whether it is selected or not

  handles      the handles on the shape's own surfaces: hover a surface of the shape and drag it.  They are always
               there, whatever the mode of the gizmo.  Any shape with `var()` numbers has them; for a shape written
               with plain numbers -- a primitive, or a part imported from a STEP file, which is made of primitives --
               `handles()` makes the numbers that place its surfaces variables:

                   part = handles(part, [var(6), var(40)])

               One line, however many numbers (nobody edits them by hand: they are dragged).  Dragging a face changes
               the number of that face (a plane's position, a radius) in the script text; a round feature keeps its centre,
               a box keeps the face opposite the one dragged.  FielDes writes the call itself when you select the shape.

A lock button beside it (key R) is a separate switch: a locked shape cannot be dragged at all, neither by the
gizmo nor by its handles, and keeps its gizmo mode, which comes back when it is unlocked:

               part = lock(part)

Nothing is stored anywhere but in the script: delete the `handles(...)` call of a part and the placement arguments of its
definition (FielDes's Reimport does) and it is the file's version again; the `lock(...)` call only keeps it from being
dragged. 

### `forget_literals()`

A script was opened: the numbers the last run saw in the calls of the old one say nothing about this one

### `handle_values(shape)`

The numbers handles() makes variables, in its order, as the text to write them as

### `handles(shape, values=None, **removed)`

A shape whose surfaces have handles: the numbers that place its
surfaces -- a plane's position, a radius, the faces of a box -- are
replaced by `values`, in the order of their appearance in the shape
(FielDes writes them as var(...) with the shape's own numbers, which
makes them draggable: hover a surface, drag it).  Orientations and the
placement of the whole part are not exposed, so a dragged face
moves and neither turns nor takes the rest of the part along.

With the shape's own numbers the result is the very same shape.  If the
shape has a different number of them (the part was changed in the STEP
file) it raises ValueError: reimport the part.

(This is what expose() was called.  The gizmo -- the arrows, rings and knobs
that move, turn and scale a shape -- drags its position=, rotation= and scale=.)

A shape that is placed by the arguments of its call (see fieldes.stdlib.placement) has its surfaces pulled where it is made, and is placed again by the same
arguments: the numbers are those of the shape as it is made.

### `lock(shape)`

`shape`, locked: FielDes does not let it be dragged -- neither by the gizmo nor by its surfaces -- until the
line is deleted.  It is the shape itself in every other way (its bounds, handles, colours and exact regions
stay), and the way of editing it had (gizmo or handles) comes back when it is unlocked.

Write it under a shape's definition, after its handles() call:

    part = lock(part)

(FielDes's model tree has a lock button on every model -- shapes, results, conditions, materials, points -- and the key R toggles it.)

## Linked copies

Linked copies: `instance(shape)`.

A linked copy IS another shape -- the same field, so the same surfaces, sizes, colours and every other property -- and it follows it: change the original
(its numbers, its definition, anything it is made of) and the copy changes with it.  What a copy has of its own is only WHERE it is: its POSITION and its ROTATION are
its own, not the original's -- moving or turning the original does not move or turn the copy, and the other way round -- with its gizmo, which drags the `position=` and `rotation=` of the copy's call.
Nothing else is its own: no scale knobs (a scale the original has is part of that shape, and the copy has it too).  Its surfaces CAN be dragged, as the original's can,
and they are linked: both are pulled by the numbers of the original's `handles(...)`, so dragging a surface of the copy moves the original's and every other copy's, and the
other way round.  (A copy that has a shape of its own is another thing: copy the model's statements instead -- Shift+D in FielDes.)

FielDes writes one with Ctrl+D (or Operations > Repeating > instance):

    plate_2 = instance(
        plate,
        position=(var(0), var(0), var(0)),
        rotation=(var(0), var(0), var(0)),
    )  # shadow: plate

The original is only REFERRED to: the mark `# shadow: plate` keeps it where it is in the model tree (a shadow of it is shown under the copy), whether it is
locked or not -- taking the original into the copy would move its row away and make the copy look like what the original is made of.
Every model is cloned the same way, `copy = instance(model)`: a model that is not a shape -- a material, the conditions or the result of an analysis, a lattice cell -- is its own value
(no place to give it, nothing is solved again).  What is made of points -- a profile, a polyline, a spline -- is cloned like any shape: its copy is placed by its own placement arguments, as the
same outline through the images of the original's points, so a point dragged, a number typed or a point added in the original changes every copy of it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `instance(shape)`

A linked copy of `shape`: the very same shape -- its surfaces, sizes, colours and properties are the original's, and change with it -- that can only be
moved and rotated, with its gizmo (the `position=` and `rotation=` of its call).  It cannot be scaled: its shape is the original's -- and so are the numbers its
surfaces are pulled by, so dragging a surface of the copy or of the original moves both.  FielDes writes it with Ctrl+D, holding the original as a reference (`# shadow: original`: the original keeps its own row in the model tree,
locked or not).  A copy that has a shape of its own is a copy of the statements instead (Shift+D)

## Symmetry

Symmetry: `symmetry_planes(...)`, a model that is a set of mirror planes, and `symmetry_images(shape, symmetry)`, ALL the images of a shape in it, as one model.

    mirror = symmetry_planes()                        # one plane, through the origin, the plane x = 0: a mirror
    mirror = symmetry_planes(position=(var(0), var(0), var(0)), rotation=(var(0), var(0), var(0)))        # moved and turned with its gizmo, like any model
    leg = cylinder(5, 30, (40, 22, 0))                # the one you edit
    leg_images = symmetry_images(leg, mirror)         # all its images, one model: the leg as it looks in the mirror, left to right

The leg is the only model with numbers: the images are not models of their own, they are what the symmetry makes of the leg, so a symmetry of two planes at right angles makes three
images (a turn of half a way round and two mirror images) in ONE model, and a cube's nine planes make forty-seven in one -- not forty-seven rows and forty-seven lines.

A symmetry is made of planes through one centre.  `kind` says how they are arranged round the axis (the line through the centre that all of them contain, or that is across the
plane of the last):

    'axis'      `planes` planes that all contain the axis, equally spaced (1: a mirror; 2: two at right angles; 3: 60 degrees apart ...)
    'across'    the same, and the plane across the axis (a part is mirrored top to bottom as well)
    'cube'      the nine planes of a cube whose axes are the three lines of the symmetry: three across them, six through its edges
    'turn'      no planes: `copies` copies of what is made, equally spaced round the axis, each the one before it turned by 360 / copies degrees (3: three legs 120 degrees
                apart; 5, 7 ...: any number, 1 to 24) -- the rotational symmetry of a three-legged table, a fan, a gear

Every plane makes an image of what is made while the symmetry is on, and so does every turn between them (two planes at right angles make a turn of half a way round); a 'turn'
symmetry has the turns only: the
images of a symmetry are numbered 1 .. symmetry_count(symmetry).  `symmetry_images(x, mirror)` is all of them (`only=(1, 3)` some of them), `symmetry_image(x, mirror, 2)` one.
The images FOLLOW what they are the images of and the symmetry: move the original and they move the way a mirror shows; move the symmetry (its gizmo) and every image of it moves
with it.  They have no numbers of their own: pulling one pulls the original.  FielDes writes the images of what you make while a symmetry is selected (Shift+S turns that on and off):
one model, `leg_images`, for a body, a 2D shape or a point (a body or a 2D shape: the union of its images; a point: a set of points); what is
made of other models -- a path_extrude, a union -- gets its images the same way (`distinct=True` leaves out those that would be the model itself or another image); a surface or a field
gets one model for each image, `symmetry_image`; so does a profile, a line or a spline: its image is the same outline through the images of its points
(`symmetry_image(stroke, mirror, 1)`), each point of it a dot that is pressed, dragged, selected and deleted -- it is the original's point that changes -- and a press on its outline
puts a point on the original; a stroke whose images meet it (a half drawn on one side of a plane) is made of the points it shares with them (`symmetry_image(point, mirror, 3)`).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `SymmetryPlanes`

A symmetry: mirror planes through a centre (see the top of this file).  In the model tree it is a model of its own kind; selected it has a gizmo that moves and turns it,
and the symmetry card; nothing is drawn of it but its planes

### `base_elements(definition)`

The images of a symmetry as plain matrices (rows), in the order they are numbered: every turn and mirror image but the identity, in the symmetry's own place -- before its
gizmo moved and turned it.  The same list, in the same order, that FielDes's viewport makes

### `symmetry_count(symmetry)`

How many images a symmetry makes of what is made with it: they are numbered 1 .. symmetry_count(symmetry) (see symmetry_image)

### `symmetry_image(shape, symmetry, index=1)`

The image of `shape` in the symmetry `symmetry`: `index` is the number of the image (1 .. symmetry_count(symmetry)).  It follows both: change the shape, or move the
symmetry, and the image changes with it.  It has no gizmo and no numbers of its own: pulling it pulls the shape.  FielDes writes it for everything that is made while the
symmetry is selected (Shift+S)

### `symmetry_images(shape, symmetry, only=None, distinct=False)`

ALL the images of `shape` in the symmetry `symmetry`, as ONE model -- or those numbered in `only`, `only=(1, 3)`; with `distinct=True` those that are neither the shape
itself nor another image (a tube along a path that lies in a plane of the symmetry has no image in that plane) -- so that what a symmetry of several planes makes of a leg
is one row of the model tree and one line of the script, not one for each image.  It is the union of the images for a body or a 2D shape (the shape
itself is not in it: it is its own model, the one you edit), and a set of points for a point.  The viewport draws each image as a shape of its own, meshed and moved on its
own (so that dragging the shape moves its images as it is dragged, with nothing meshed again), all of them one model of the tree: select it and all of them are selected.
It follows the shape and the symmetry as each image does (see symmetry_image) and has no numbers of its own: its gizmo is the shape's, drawn at the first image, and pulling
a surface of any of them pulls the shape.

What a shape was made from is mirrored TOGETHER with it: the profiles, the lines and the points of the models it was made from -- the profile and the path of a
`path_extrude`, the profile of an `extrude`, whatever the operation -- are drawn with each image, as a copy of the line or the outline and a dot for each point.  They are not
models -- there is no statement and no row of the model tree for them -- and the ones of the shape you made stay the only ones with numbers: press a dot of any image's and drag
it, and it is that point of the original that moves.

### `symmetry_planes(planes=1, kind='axis', centre=(0, 0, 0), axis=(0, 0, 1), first=None, copies=None)`

A symmetry: mirror planes through `centre`.  `kind` is 'axis' (`planes` planes that all contain the axis `axis`, equally spaced: one is a mirror), 'across' (and the plane
across the axis) or 'cube' (the nine planes of a cube).  `first` is a line in the first plane (across the axis; any when it is left out): with one plane the plane is the one
that holds the axis and `first`.  The default is the mirror x = 0.

`kind='turn'` has no planes: it is the rotational symmetry of `copies` copies equally spaced round the axis (`copies=3`: three legs 120 degrees apart; 5, 7 ... up to 24),
each the one before turned by 360 / copies degrees -- nothing is mirrored.

It is a model: selected in the model tree it has a gizmo -- moved, turned -- and everything made while it is selected is made with its images (see symmetry_image)

## Points and surfaces

Points and surfaces: models of their own kind, and how every kind is drawn.

    p = point(10, 0, 5)                    a point: drawn as a dot (the middle point of a gizmo); click it and its gizmo shows, and moves it
    s = plane((0, 0, 8), (0, 0, 1))        a surface: an open sheet, the zero set of a field, with no body behind it
    wavy = wave_surface(2, 10)             z = 2 sin(2 pi x / 10)

A point is usable where a field function takes a position (distance_to_point(p), attractor(p, ...)) and gives its
coordinates as p.xyz.  A surface is a field like any other (negative on one side, positive on the other), so it works
wherever a field does: thicken(s, 1) makes it a wall, lattice_surface_conform(s, ...) lays a lattice on it.

What is drawn for each kind of model (Shape._display calls displayed() below):
    a 3D shape and a simulation   as they are
    a 2D shape (no z)             its face in the z = 0 plane: flat, a sheet with no thickness (it has no height)
    a surface                     the sheet itself, with no thickness (the zero set of its field, meshed as it is)
    a point                       a dot, the middle point of a gizmo (click it to select the point: its gizmo shows)
    a field                       nothing in the viewport: selected in the model tree it is shown by the section viewer,
                                  which colours a plane through the render region by the field (move the plane to see
                                  the field in 3D)

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `Point`

A point: p.xyz is its coordinates (numbers or fields).  The viewport draws it as **a dot** -- the middle point of a gizmo -- wherever it is;
click the dot to select it, and its full gizmo shows.  (As a field it is a tiny ball, which gives it a place for its gizmo and for the
bounds of what is made of it: nothing of the ball is painted.)

### `Surface`

An open surface: the zero set of a field, with no body behind it (negative on one side, positive on the other)

### `cylinder_surface(radius=5.0, axis='z', center=(0, 0, 0))`

The surface of an infinite cylinder along an axis: a surface, negative inside

### `displayed(shape)`

What FielDes draws for a shape (see the top of this file)

### `image_dot(source, xyz, rows, about, shift=(0.0, 0.0, 0.0), frozen=())`

The dot of a point that is drawn somewhere else than the point is: the IMAGE of `source` under the map that takes a place to `about + rows * (place - about) + shift` (a turn or a
mirror image, then a move), at `xyz` -- expressions of the numbers of `source`.  It is the point in every way that is seen: it is pressed, dragged, double clicked, and it is
`source` that all of those change -- the numbers of `source`'s gizmo are what it is dragged by, taken back through the map (the viewport does it: `_symmetry`) --, so that the
original and everything that is an image of it change together.  `frozen` are the numbers of the map itself (a gizmo's): a drag of the dot never changes them.  The dots of the
images of a profile or a line in a symmetry, and of a placed copy of one (see handles.gizmo), are these

### `plane(point=(0, 0, 0), normal=(0, 0, 1))`

A plane through `point`, positive on the side `normal` points to: a surface

### `point(x=0.0, y=0.0, z=0.0, smooth=None, radius=None, influence=None)`

`influence` is how far the point reaches, in mm -- a number or a `var()`: the RADIUS of the field round it, when the point is given to `distance_to_point` or `radial_field` (the distance from the sphere, or the tube, of that radius: negative inside it, 0 on it, positive outside).  A point that does not say its influence is the point alone, and those fields are the plain distance from it.  Select a point and the ring of its gizmo is its influence: `influence=var(...)` is written into the call when it has none (a point that has its position, rotation and scale written already gets it too), and the field viewer of a field made of the point stays open until its icon is clicked, and shows the gizmo of the point while it is open, so the radius is seen as the ring is dragged.  The scale is only for the points of a spline or a path: the ring of such a point is its scale, and Shift and the ring its influence.

A point at (x, y, z).  Its coordinates are numbers or fields.  It is drawn as a dot (the middle point of a gizmo): click it, and its gizmo shows.
In a `profile` or the path of a `path_extrude` it is a spline point when `smooth` is True (the curve goes smoothly through it, in the direction
its gizmo's rotation gives) and a corner when False; None (the default): what the profile says.  In a `profile` it can also be a ROUND corner, `radius=r`: an exact arc
of that radius, tangent to both edges, takes the place of the corner (the scale of its gizmo multiplies the radius; a spline point has no radius).  A double click on its
dot goes from a corner to a spline point to a round corner and back to a corner

### `sphere_surface(radius=10.0, center=(0, 0, 0))`

The surface of a sphere (without its inside): a surface, negative inside

### `surface(field)`

An open surface from any field: where the field is 0.  (A body's field as a surface is its skin.)

### `wave_surface(amplitude=2.0, period=10.0, axis='x', height=0.0)`

A wavy sheet z = height + amplitude sin(2 pi s / period) along an axis (s = x or y): a surface, positive above it

### `weld_shift_of(shape)`

The shift a point or a curve is DRAWN with: its own (path_extrude sets it on the path of a sweep and on its points), or -- for the image of such a point in a symmetry -- the
original's, taken through the turn or mirror the image is made by.  The image of a path is drawn where the image of its sweep is, as the path is where its sweep is.
None when it is drawn where it is

## Lines and curves: 1D shapes made of points

1D shapes: a line or a spline through points.

    a = point(0, 0, 0)
    b = point(20, 10, 0)
    c = point(40, 0, 5)
    lines = polyline([a, b, c])                 # straight pieces between the points: a hard corner at each
    smooth = spline([a, b, c])                  # a smooth curve through the points
    loop = spline([a, b, c], closed=True)       # joined from the last point back to the first

A curve is a model of its own kind (a 1D shape), made of points and drawn as a line.  It stands on its own, and anything that takes a curve takes it as it is:

    face = profile(loop)                         # the 2D shape a closed curve encloses (see profile)
    tube = offset(smooth, 1.5)                  # a 3D shape: the points at most 1.5 from the curve (a tube round it)
    beam = path_extrude(face, smooth)           # a 2D shape swept along it

As a field a curve is the distance to it (zero on it, positive everywhere else), so `offset`, `thicken` and the field functions work on it.

A polyline is made of corners and a spline of spline points -- but each POINT can say otherwise (`point(..., smooth=True)` or `smooth=False`; FielDes toggles
it when you double-click the point), so a spline can have a corner and a polyline a smooth stretch.  The ROTATION of a spline point -- the rotate numbers of
its gizmo, which a point has no other use for -- turns the direction in which the curve leaves it.  With no rotation the direction comes from the
neighbouring points (a Catmull-Rom spline); the rotation turns it about x, then y, then z (degrees), the way a gizmo turns a shape.  The same
numbers twist what `path_extrude` sweeps along the curve.

The line is made by the kernel from the numbers the points have when the script runs (and drawn from the numbers they have while you drag them).  Its field
is plain math: the least of the distances to its straight pieces, so the renderer drops the pieces that are far from where it looks.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `Control`

One point of a curve or profile: where it is (three numbers; the expressions the script gave are kept for the viewport, which draws the line from them
while the point is dragged), what it says of itself (-1: what the shape says, 0: a corner, 1: a spline point) and its rotation

#### `Control.smooth(self, default)`

*(no description yet)*

### `Curve`

A 1D shape (see `polyline`, `spline`).  .points are the points it was made from, .smooth the default of its points, .closed whether it is joined at its
ends, .vertices the line it is drawn as (x, y, z)

### `add(a, b)`

*(no description yet)*

### `controls(points, rounds=False, radius_default=0.0, smooth_default=False)`

The controls of a curve (`rounds` False: a point that says it is round is a corner there) or of a profile (`rounds` True)

### `cross(a, b)`

*(no description yet)*

### `dot(a, b)`

*(no description yet)*

### `fillets(ctrls, default, closed, normal=None)`

The round corners of the line or face through the controls, made by the kernel: a list of (control, a, b, centre, radius) -- the arc runs from `a` to `b` (the short
way round) about `centre`, in the plane of the face; the radius is what the arc has been made with (smaller than the one asked for where it would run into the next
arc or past the next point)

### `mark(ctrls, default, kind)`

Tell each point what the curve or profile made of it: a corner or a spline point (the viewport draws the two differently, and the double click
changes it), and the default it follows

### `mul(a, k)`

*(no description yet)*

### `owned(shape, cls=<class 'fieldes.shape.Shape'>)`

A new `cls` that owns a copy of the tree of `shape`.  (`shape` is held for the whole call: a copy of `X(...).ptr` made inline would be made from a tree that
is already freed, the temporary having died as soon as its pointer was read)

### `plane_of(ctrls)`

(origin, u, v) of the plane through the first three points that are not on a line (numbers), or None

### `polygon_field(loop, frame, arcs=None)`

The signed distance to the polygon whose corners are `loop` (s, t) -- negative inside, in the plane of `frame` (origin, u, v: s and t are the
coordinates along u and v), or in x and y when there is none.  An exact distance, concave outlines too.

`arcs`: round corners, (a, b, centre, radius) in the same coordinates, each the short way round from a to b; `loop` then has the chord from a to b as an edge
in place of each of them (see `fillets`).  The distance is to the arc, exact (the chord is not an edge of the outline), and the side of the outline is that of the
polygon of the chords turned over inside every circular segment between a chord and its arc: a segment that bulges out of the polygon is added to it, one that bulges
into it is cut out.

Made the way a vector renderer measures a polygon, of two parts, both a plain `min` over small pieces so that the renderer drops the pieces that are far
from where it looks (a polygon of a hundred edges costs what the few pieces near the point cost):
- how far: the least of the distances to the edges;
- which side: the polygon is cut into triangles when the script runs, and a point is inside when it is inside one of them (a `min` over triangles of
  the `max` of three straight lines).  Across a cut a triangle goes a hair over, so that the cut itself is inside; the edges of the polygon are exact.
An outline that crosses itself cannot be cut: its side is then the winding of the outline round the point (the sum of the angles each edge is seen
under: a full turn inside, none outside)

### `polyline(points, closed=False)`

A set of lines: straight pieces through the points, in the order given, a corner at each (a point can say it is a spline point: `point(..., smooth=True)`),
joined from the last point back to the first when `closed`.  As a field it is the distance to the lines

### `polyline_field(vertices, closed)`

The distance to the straight pieces between the vertices (x, y, z): the least of the distances to the pieces, then one square root

### `rotated(t, rotation)`

The vector t turned the way the gizmo turns a shape: about x, then y, then z (degrees)

### `spline(points, closed=False)`

A smooth curve through the points, in the order given (a point can say it is a corner: `point(..., smooth=False)`), joined from the last point back to the first
when `closed`.  It leaves each point in the direction the rotation of its gizmo says (no rotation: from the neighbouring points).  As a field it is the
distance to the curve

### `sub(a, b)`

*(no description yet)*

### `trace(ctrls, default, closed, pieces=14, normal=None, arc_pieces=32)`

(the vertices of the line through the controls, as (x, y, z); for each the number of the control it is after and how far along towards the next it is)
-- made by the kernel.  `normal`: the normal of the plane of a face the points belong to -- the rotation of a point is then one angle about it, in
the plane (the z number of its rotation) -- or None for a line in space.  A round corner is its arc, `arc_pieces` to a quarter turn (0: only the two ends of
the arc: the chord, which is what the exact field of a face is made on, see `fillets`)

### `unit(a)`

*(no description yet)*

### `value_of(c)`

The number a coordinate has now: a number, or a field that is the same everywhere (a var(), an expression of var()s)

### `vec(v)`

*(no description yet)*

### `viewport_data(out, ctrls, default, closed)`

What the viewport draws the line or the face from while a point is dragged: the expressions of each point's place and rotation (their numbers move with
the script's var()s), and what each says of itself.  The line is made again from them at every frame, natively: no script is run

### `with_image_dots(model, shown)`

What is drawn for a profile or a line: `shown`, and -- when it is a placed copy of another (see handles.gizmo) -- the dots of its points, which are images of the original's
points: each is pressed, dragged and double clicked, and it is the original's point that changes (see points.image_dot)

## Profiles and extrusions

A 2D shape from points or from a closed curve: `profile`.

    a = point(0, 0, 0)
    b = point(40, 0, 0)
    c = point(40, 20, 0)
    d = point(0, 20, 0, smooth=True)
    plate = profile([a, b, c, d])                # the face the points enclose; hard edges unless a point says smooth=True
    round = profile([a, b, c, d], smooth=True)   # ...or every point a spline, but where a point says smooth=False
    outline = spline([a, b, c, d], closed=True)
    plate = profile(outline)                     # the same, from a curve
    body = extrude(plate, 0, 5)                 # a solid: the face pulled 5 along its normal

The points are ordinary points (`point(x, y, z)`, dragged with their gizmo): any number of them, joined in the order they are given and closed (the last to
the first).  The shape lies in the PLANE through the first three points (the first that is not on a line with the first two); every other point is
projected onto that plane, so a profile is made whatever the points are.  A tuple of three numbers stands for a point too.

Each point is a corner (a hard edge: straight to its neighbours) or a spline point (the outline is a smooth curve through it) -- see `spline`, which makes the
outline.  `smooth=` of the profile says which a point is that does not say itself (False, a hard edge, by default); `point(..., smooth=True)` says it for
that point -- FielDes toggles it when you double-click the point.  The ROTATION of a spline point -- its gizmo's rotate numbers, which a point has no other use
for -- turns the direction in which the outline leaves it.

A profile is drawn from the numbers its points have, natively, while you drag them.  Its field (for `extrude`, `offset`, analyses ...) is plain math, made
when the script runs: the signed distance to the outline -- the distance to the nearest edge, with the side taken from that nearest edge or corner alone, so
the renderer drops the edges that are far from where it looks.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `Profile`

A 2D shape made of points (see `profile`).  .points are the points it was made from, .plane the plane it lies in -- (origin, u, v), three numbers each --
.smooth the default of its points, .profile2d the same field in the coordinates of its plane (x along u, y along v)

### `SurfaceProfile`

A 2D shape made from a surface (see `profile`): the outline of the surface in its plane, holes included.  .plane the plane it lies in -- (origin, u, v), three
numbers each, u x v the way the surface faces --, .profile2d the same field in the coordinates of that plane, .flat whether the surface is flat (it is projected
onto the plane when it is not), .away how far the surface is from the plane at most, .loops the outline as [(polygon, hole)] with the points as (s, t)

### `extrude(shape, a, b)`

A 2D shape pulled into a solid: the part of space between the distances `a` and `b` from its plane, along the plane's normal (the plane is
z = 0 for the shapes that lie there -- a circle, a rectangle -- so that is zmin and zmax; the plane of a profile is the plane of its points,
and the normal is the direction u x v of that plane).  `extrude(circle(5), 0, 10)` is a cylinder 10 tall

### `path_extrude(profile, path, twist=0)`

A 2D shape swept along a curve: the `profile` -- its own axes across the curve (x and y for a shape in the plane z = 0, the two axes of the plane of a profile) -- carried
along `path`, turning with it, without twisting about it unless the points of the curve say so.
THE 2D SHAPE IS WHERE THE SWEEP STARTS: the curve says how it travels, not where from -- the whole sweep is moved so that the first point of the curve is the middle of
the shape (the middle of the box of its outline), and its first ring is the shape itself, in its own plane at whatever angle the curve leaves it (the sweep is slanted
when it is oblique; a shape whose plane holds the direction of the curve is turned across it), so that the shape you edit is the base of what it is swept into.  A circle
or a rectangle is such a shape as a profile is.  The path is DRAWN where the sweep goes -- through the middle of its cross-section, from the middle of the shape, its
points with it -- whatever its own numbers say; only its shape matters, and the first point of it is the middle of the shape:
the ROTATION of a point (its gizmo's rotate numbers) twists the profile about the curve there, and the twist runs between the points as a
smooth change.  `twist` (degrees) turns the whole profile about the curve besides.  The ends are cut square.  A curve with corners is swept
with mitered joints.  `path_extrude(circle(2), spline([a, b, c]))` is a bent tube; `extrude` is the straight case.  `path` may be a list of points:
the spline through them

### `profile(source, smooth=False, radius=0)`

The 2D shape the points of `source` enclose -- a list of points, or a curve -- as a closed outline through them in the order given, in the plane
through the first three of them (the others are projected onto it).  `smooth` is what a point is that does not say (point(..., smooth=True) or
smooth=False): False, a corner (a hard edge: the default), or True, a spline point.  A spline point leaves in the direction the rotation of its gizmo
says (no rotation: from its neighbours).  A point can also be a ROUND corner, `point(..., radius=r)`: an exact arc of radius r, tangent to both edges, takes the place
of the corner (the point stays where the two edges would meet; the scale of its gizmo multiplies the radius; where the arc would run into the next one or past the next
point the radius is made smaller, and a square whose four corners ask for half its side is a circle).  `radius` is what a corner is that does not say, 0 (a hard
corner) by default.  With fewer than three points, or all on a line, it is empty.

`source` can also be a SURFACE picked on a part (`select_surface`): the 2D shape of its outline -- the outline of the face, holes included, in the plane
of the face (exact for a flat face), or, for a surface that is not flat, of its projection onto the plane through its middle, along the way it faces on
average.  It is made from the surface and moves with it: it has no points of its own to drag

## Measuring a body

Measuring a body: its box and its middle.

    box = bounding_box(part)          # the smallest box, aligned with the axes, that holds the part: a body of its own
    middle = center(part)             # the middle of that box: a point

Both are what a script needs all the time -- to put something in the middle of a part, to give a region to a condition, to size
a cell to a part, to place the next part against this one -- and both are in the right-click menu of a body.  They are made when
the script runs, from the extent the body has then (a body that is dragged, or has var() numbers, is measured again by the run it
causes), and the extent is the one the body knows exactly -- a box, an imported part -- or else the one found by searching its
field, which can be a little roomy for a body with soft edges.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `bounding_box(body)`

The smallest box, aligned with the axes, that holds `body`: a body of its own (`box` between the body's lowest and highest
corner).  Use it as a region, as the space to lay something out in, or to read the size of a part:

    box = bounding_box(part)
    base = fixed(box)

It is measured when the script runs, from the extent the body knows exactly (a box, an imported part) or the one found by
searching its field.

### `center(body)`

The middle of `body`'s bounding box, as a point:

    middle = center(part)
    part = move(part, (-middle.x, -middle.y, -middle.z))        # the part about the origin

It is measured when the script runs, like bounding_box.

## Fields

Field-driven design tools: scalar fields (Shapes used as values rather than
as solids) and the operations that turn them into geometry.

Every Shape is a field -- a number at every point in space.  A
solid is the region where its field is negative; any other field (a
distance, a ramp, an analysis result, a regression of measured data) can
drive a parameter such as a lattice thickness or an offset:

    from fieldes import *

    part  = sphere(30)
    depth = depth_below(part)                        # 0 at the surface, 30 at the centre
    t     = ramp(depth, (0, 30), (2.0, 0.6))         # 2 mm walls at the skin -> 0.6 mm inside
    lat   = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=t, skin=1.5)

Groups:
    coordinates        x_field(), y_field(), z_field(), radial_field(),
                       angle_field(), polar_field()
    distances          distance_to_point / _points / _line / _segment /
                       _polyline / _plane, distance_to_surface, depth_below,
                       signed_distance
    value maps         ramp, clamp, lerp, smoothstep, step_field, normalize,
                       remap_field, attractor, wave, sum_fields, mix
    geometry           thicken, shell_inside / _outside / _centered,
                       offset_by, smooth_union,
                       smooth_intersection, smooth_difference, chamfer_union,
                       union_all, intersection_all, repeat, repeat_polar,
                       mirror_x / _y / _z, twist_z, bend_z
    evaluation         evaluate, sample_grid, field_range, volume_of,
                       mass_properties
    exact distances    exact_distance, offset_exact, shell_exact,
                       round_edges, fillet
    analysis           normal_field, gradient_field, gradient_magnitude,
                       overhang_angle, overhang_mask, wall_thickness,
                       curvature_field
    data               field_from_points, field_from_csv, noise_field
    surface texture    texture

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `abs_field(field)`

The absolute value of a field at every point

### `add_fields(*fields)`

The sum of fields (and numbers): a + b + ... at every point

### `angle_field(center=(0, 0, 0), axis='z')`

Angle around an axis through `center`, in radians (-pi..pi)

### `attractor(points, radius, falloff='smooth', strength=1.0)`

(`radius` and `strength` are numbers or fields.)  A field that is `strength` at the given points (or curves: pass a
distance field instead of points) and falls to 0 at `radius`.
falloff: 'linear', 'smooth' (smoothstep) or 'gauss'.

### `bend_z(shape, radius, center=(0, 0))`

Bends a shape lying along +x around the z axis: x becomes arc length
on a circle of the given radius

### `body_from_field(field, level=0.0)`

A BODY from a field: what is inside where the field is below `level` (0 by default), its surface where the field
equals `level` -- the other way of field_from_body().  The result is drawn and is a part like any other (it can be
offset, shelled, filled with a lattice, analysed, exported).

    noise = noise_field(12, 3)                                  # a field: not drawn
    lumps = body_from_field(noise, 0.2)                         # the places where it is below 0.2: a body

The value of the body is the field's less `level`, so it is a true distance to its surface only where the field is
one (field_from_body of a body, a distance_to_point ...): the surface is exact, an offset of a body made from a
field that only roughly measures distance is about right, not exact.  `level` may be a field too.

### `chamfer_union(a, b, size)`

Union with a 45-degree chamfer of the given size where the shapes meet

### `clamp(field, lo, hi)`

Limits a field to [lo, hi]

### `curvature_field(shape, step=None)`

Mean curvature (1/mm; positive where convex, e.g. 1/r on a sphere of
radius r).  step: finite-difference step in mm (default 0.05)

### `depth_below(shape)`

Depth below a shape's surface: 0 on the surface and outside, growing
towards its interior (e.g. the thickness of material above a point)

### `distance_to_line(point, direction)`

Distance to an infinite line through `point` along `direction`

### `distance_to_plane(point=(0, 0, 0), normal=(0, 0, 1))`

Signed distance to a plane: positive on the side the normal points to (the point's and the normal's
coordinates may be fields: a plane that moves or tilts from place to place)

### `distance_to_point(p, radius=None)`

Euclidean distance to a point (its coordinates may be fields: a point that is where a field says).  With a `radius` -- or the influence of the point that is given, `point(x, y, z, influence=r)`, which is the radius in mm -- it is the distance from the SPHERE of that radius round the point: negative inside it (-r at the point), 0 on it, positive outside: a sphere's value, as a field.  A point that says no influence, and no radius, is the plain distance from it

### `distance_to_points(points)`

Distance to the nearest of several points

### `distance_to_polyline(points, closed=False)`

Distance to a polyline (a curve given by its points)

### `distance_to_segment(a, b)`

Distance to the line segment from a to b

### `distance_to_surface(shape)`

Unsigned distance to a shape's surface (the shape's field is used as
the distance, so this is exact for exact distance fields such as
spheres, boxes, imported meshes)

### `divide_fields(first, *others)`

The first field divided by the others: a / b / ... at every point (NaN where a divisor is zero)

### `evaluate(field, point_or_points)`

A field's value at a point, or a list of values at many points

### `exact_distance(shape, reach=None, bounds=None)`

The signed distance field of a shape (mm: negative inside, 0 on the surface, the distance to the surface outside), read from the shape's own field: nothing is
meshed.  Many shapes built with booleans, blends, imports or warps have fields that are only roughly a distance (the field of a box is the largest of the distances to
its planes: boxy mitres outside its edges), and offsets, shells, rounded edges and the depth below a surface mean a true one.  From each point the surface is found
by walking along the field's gradient (a few Newton steps, each a handful of calls of the field), and the distance is the length of that walk -- see distance_field.py.
It is exact for a field that is a distance already, for a sphere, a cylinder, a plane, and for the edge and the corner of a box; at an edge where faces meet at another
angle it is a little off (the walk goes from face to face and ends beside the nearest point).
reach: how far from the surface the distance is made, mm (default a quarter of the shape's size): beyond it the shape's own field stands, so that the walk is not made
(and not drawn) over the whole of space; an offset, a shell or a thickness asks for as much as it needs.  bounds: the box round the shape, when it cannot be found --
only its size is used (the step of the gradient).

### `field_from_body(body)`

The values of a body as a FIELD: the same number at every point as the body (its distance to its surface, negative
inside), but not a body -- it is not drawn, it is not a part, and it is free to be multiplied, divided or powered
and still be a valid field.  (A body itself always keeps its true scale: it is a distance, which a factor would
break.  Selecting the field shows it in the section viewer.)

### `field_from_csv(path, x='x', y='y', z='z', value='value', neighbours=8, power=2.0, delimiter=None, scale=1.0)`

A field from a CSV file of samples (e.g. exported stresses,
temperatures or pressures): columns by header name or index.
scale multiplies the coordinates (e.g. 1000 for metres -> mm).

### `field_from_points(points, values=None, neighbours=8, power=2.0)`

A field through scattered samples -- measured data, or results
exported from another simulation: at every point, the
inverse-distance-weighted average (weights 1 / distance^power) of the
nearest `neighbours` samples.  Handles hundreds of thousands of
samples.  points: [(x, y, z), ...] with values [v, ...], or
[(x, y, z, v), ...].  The field stays within the samples' range.

### `field_range(field, body=None, lower=None, upper=None, n=24)`

(min, max) of a field, sampled on a grid over a box (and only inside
`body` if given)

### `fillet(shape, radius, bounds=None)`

Fills every concave (inside) edge and corner of a shape with a
fillet of the given radius (a number or a field), and fills what is narrower than the ball: a ball of that radius rolls in the air, and the
shape is everything it cannot reach.  It is made from the shape's own field (see exact_distance and distance_field.py): the centres of the ball are where the field is
`radius`, and the surface of the result is `radius` from them.  Exact for the inside edge of two flat faces at any angle and for the corner of three
(the ball's centre is where the planes of the faces meet); a slot narrower than the ball is filled flush with its mouth, not left with the sag of the ball resting on its two edges.

### `find_extent(shape, budget=200000, resolution=2.0, half=1000000.0)`

The box ((x0, y0, z0), (x1, y1, z1)) round the inside of a shape, found by searching it with interval
arithmetic -- or None when it has no extent that can be found (it is empty, or open on a side, or the search
ran out of cells).  A shape with var() numbers is searched with the numbers they have in the script: inside
the application a var() is held by the program, and a search that did not know its number would read it as 0
(a box of size var(2) is no box at all).  The search takes a third of a second or more, and every select_surface of the same part
asks for it again on every run of the script: it is remembered by what the shape is (and the numbers it has)

### `gradient_field(shape)`

The gradient of a shape's field, as three fields (gx, gy, gz)

### `gradient_magnitude(shape)`

The length of a field's gradient: 1 for an exact distance field; far
from 1 where a field only roughly measures distance

### `intersection_all(shapes, blend=0)`

Intersection of many shapes (optionally blended)

### `lerp(a, b, t)`

Linear interpolation between two values or fields: a at t = 0, b at
t = 1 (t may itself be a field)

### `low_level_body(logic)`

A BODY from your own logic: `logic(x, y, z)` returns a number that is NEGATIVE inside the body, zero on its surface and
positive outside -- ideally the distance to the surface, so that offsets and shells come out right:

    def ball(x, y, z):
        return (x.square() + y.square() + z.square()).sqrt() - 20          # a ball of radius 20

    part = low_level_body(ball)

or `low_level_body(lambda x, y, z: maximum(x - 6, -6 - x, y - 6, -6 - y, z - 6, -6 - z))` for a cube of 12.  The body is
drawn and is a part like any other.  See low_level_field() for the operations; a field that is not a body is
low_level_field() (and body_from_field() makes a body of it).

### `low_level_field(logic)`

A FIELD from your own logic, in one line or a few: `logic` is a function of the coordinates -- it is called ONCE, with
x, y and z as fields (a function of x and y alone is a 2D field) -- and returns the value you want at that point:

    def ripple(x, y, z):
        distance = (x.square() + y.square()).sqrt()          # the distance from the z axis
        return (distance * 0.6).sin() * 3                    # a ring pattern, up to 3

    waves = low_level_field(ripple)

Write the math with + - * / **, the methods .sqrt() .square() .abs() .sin() .cos() .tan() .exp() .log(), a.max(b) and
a.min(b) (or maximum(a, b, c) and minimum(a, b, c)); a number is a constant field.  What you get is a field like any
other: it is not drawn (display it: its icon opens the field viewer), it goes wherever a number goes (`offset(part,
waves)`), a lattice's cell size, a load's profile.  low_level_body() is the same for a body; body_from_field() makes a
body of a field.  In FielDes: right-click, Parametric shapes, New field, low_level_field, writes the function and the call for you.

### `mass_properties(shape, density=1.0, lower=None, upper=None, resolution=None)`

Volume, mass (density in g/cm^3 -> grams), centroid and bounding box
of a solid, by grid sampling.  Returns a dict.  The density is a number or a field (a material that is
denser here than there: a lattice graded by its density, a result): the mass adds it up over the solid, and the
dict has the centre of mass as well as the centroid.

### `max_fields(*fields)`

The largest of the fields (and numbers) at every point

### `maximum(*terms)`

The largest of several fields, point by point (Python's own max cannot compare fields; `.max` of one takes one
other): maximum(x - 6, -6 - x, y - 6) is the intersection of three half-spaces.  See minimum().

### `min_fields(*fields)`

The smallest of the fields (and numbers) at every point

### `minimum(*terms)`

The smallest of several fields, point by point: minimum(a, b, c) is the union of three bodies.  See maximum().

### `mirror_x(shape, x=0.0)`

Mirror-symmetric copy: the half at x > x0 reflected onto the other

### `mirror_y(shape, y=0.0)`

Mirror-symmetric copy: the half at y > y0 reflected onto the other

### `mirror_z(shape, z=0.0)`

Mirror-symmetric copy: the half at z > z0 reflected onto the other

### `mix(a, b, t)`

Same as lerp()

### `multiply_fields(*fields)`

The product of fields (and numbers): a * b * ... at every point

### `negate_field(field)`

A field with its sign changed: -a at every point

### `noise_field(scale=10.0, octaves=4, seed=1, gain=0.5, lacunarity=2.0, amplitude=1.0, kind='perlin', warp=0.0)`

Smooth random variation, a field of values around zero (`scale`, `amplitude` and `warp` are numbers or fields).
`scale` is the size (mm) of the largest features -- or three sizes, `(x, y, z)`, to stretch the pattern along an axis: `(40, 4, 4)` is streaks along x --, each
octave adds detail `lacunarity` times finer and `gain` as strong; a different `seed` gives a different pattern.  `kind`:
  'perlin'    the classic: soft hills and hollows (Perlin's improved noise).  Four octaves stay mostly inside +-0.3 and rarely go past +-0.6, so
              `amplitude` is what sets how strong it is: 3 or so uses the range of -1 .. 1
  'billow'    puffy, rounded humps with creases where the noise passes through zero
  'ridged'    sharp ridges where the noise passes through zero (mountains, veins)
  'cellular'  Worley noise: the distance to the nearest of random points, low in the middle of a cell and high at its walls (bubbles, pits, scales)
  'cracks'    the distance from the walls between those cells, low along them (a web, a foam's walls, cracks): about -1 .. 1
`warp` pushes the coordinates about by three other noises, so the pattern bends (marble, flowing organic shapes); it is in feature sizes, 1 bends it a lot.
The billow, ridged and cellular kinds are laid over about -1 .. 1.  E.g. an organic surface texture:
part - 0.3 * noise_field(4)

### `normal_field(shape)`

The unit surface normal of a shape (pointing out of it), as three
fields (nx, ny, nz); defined everywhere, meaningful near the surface

### `normalize(field, lo, hi)`

Maps [lo, hi] to [0, 1] (clamped)

### `offset_by(shape, distance)`

Offsets a shape by a distance -- which may be a field, e.g. grow a
part by 0.02 mm per MPa of stress: offset_by(part, 0.02 * stress)

### `offset_exact(shape, distance, bounds=None)`

A uniform offset of any shape: grows it by `distance` mm everywhere
(shrinks it for a negative distance), measured along true normals,
so edges and corners get round, not stretched.  The distance is a number or a field (an offset that
is more here than there).  The distance to the shape is read from its field (see exact_distance) as far out as the offset reaches.

### `overhang_angle(shape, build_direction=(0, 0, 1))`

For additive manufacturing: the angle (degrees) between each surface
and the horizontal build plate, for surfaces facing down -- 0 for a
ceiling facing straight down (the worst overhang), 90 for a vertical
wall; surfaces facing up read 90 too.  Colour a part by it to see
where supports are needed (typically below 45 degrees).

### `overhang_mask(shape, limit=45.0, build_direction=(0, 0, 1))`

1 where a surface overhangs more than `limit` degrees (needs support),
0 elsewhere

### `polar_field(center=(0, 0, 0), axis='z')`

Angle from an axis through `center`, in radians (0..pi): 0 along the
axis, pi/2 at its equator (spherical coordinates)

### `power_field(base, *exponents)`

The first field to the power of the next: (a ** b) ** ... at every point (NaN where a negative value meets a
fractional power)

### `radial_field(center, axis='z')`

Distance from the axis through the point `center` (cylindrical radius, mm): a field made of a POINT, as `distance_to_point` is -- right-click a point, and `radial_field` is in its menu.  `center` is a `point(x, y, z)` (its coordinates may be fields), the Point class; a vector -- `(x, y, z)` -- is not a point, and `radial_field` refuses it with a message.  *Primitives > Fields > radial_field* writes the point (a model of its own, with its gizmo) and the field made of it.  The field has no radius of its own: the INFLUENCE of the point, `point(x, y, z, influence=r)`, is the radius, as it is for `distance_to_point` -- the field is then the distance from the TUBE of that radius round the axis: negative inside it (-r on the axis), 0 on it, positive outside

### `ramp(field, input_range, output_range, clamped=True)`

Linear map of a field: input_range=(a, b) -> output_range=(va, vb); each end is a number or a field (a ramp
that starts and ends where other fields say).  Clamped by default (values beyond the input range hold the end
values), e.g. ramp(z_field(), (0, 50), (2, 0.5)) is 2 at z = 0,
falling to 0.5 at z = 50 and above.

### `remap_field(field, input_range, output_range, clamped=True)`

Same as ramp()

### `render_mesh(shape, region, res)`

The library's mesh of a shape over a region (a libfive_region_t) at `res` samples per mm: the
pointer libfive_tree_render_mesh gives (free it with libfive_mesh_delete), or None. A shape with
var()s is meshed with the numbers they have in the script.

### `repeat(shape, spacing, center=(0, 0, 0))`

Repeats a shape infinitely on a grid.  spacing: a number or (sx, sy,
sz), each a number or a field (the repeat is closer here and wider there); 0 along an axis leaves that axis alone.  The shape should fit
inside one grid cell centred on `center`.

### `repeat_polar(shape, count, center=(0, 0), axis='z')`

Repeats a shape `count` times around the z axis; the shape should
lie in the wedge around the +x direction

### `round_edges(shape, radius, bounds=None)`

Rounds every convex (outside) edge and corner of a shape with the
given radius (a number or a field), and removes what is thinner than the ball: a ball of that radius rolls in the material, and the shape is everything
it reaches.  It is made from the shape's own field (see exact_distance and distance_field.py): the centres of the ball are where the field is
`-radius`, and the surface of the result is `radius` from them.  Exact for the outside edge of two flat faces at any angle and for the corner of three
(the ball's centre is where the planes of the faces meet), for a cylinder and a sphere.

### `sample_grid(field, lower, upper, n=20)`

Values on an n x n x n grid of cell centres over a box (a flat list,
x fastest) together with the points

### `shell_centered(shape, thickness)`

A shell straddling the surface (half inside, half outside)

### `shell_exact(shape, thickness, side='inside', bounds=None)`

A hollow shell of uniform thickness (true distance), side = 'inside',
'outside' or 'center'.  The thickness is a number or a field.

### `shell_inside(shape, thickness)`

A hollow shell: the part of `shape` within `thickness` of its surface

### `shell_outside(shape, thickness)`

A skin grown outwards from the surface by `thickness`

### `signed_distance(shape)`

A shape's signed distance field (negative inside) -- the shape
itself, named for readability

### `smooth(shape, radius, steps=1)`

Smooths the surface of a body without thickening it: bumps, dents, ridges and stair-steps smaller than
about `radius` mm are smoothed away, edges and corners are eased, and a flat or gently curved face stays
where it is (unlike offset() or thicken(), which move or grow the surface).  The field of the body is
averaged over the points a `radius` away on all six sides -- the smoothing of a mesh's Laplacian, done on the
field -- and `steps` of those make the field of the smoothed body: more steps smooth further (the reach
grows as the square root of the number of steps).  The radius is a number or a field (smoothed more where
the field is large, not at all where it is 0).  Like any smoothing it eases convex features slightly
inwards and concave ones slightly outwards; the body as a whole does not grow.

The result is a field like any other, a weighted sum of copies of the body's own field moved by whole
radii (6 copies for one step, 19 for two, 44 for three, 85 for four; more steps than four are four at a
larger radius, which smooths as far), so nothing is measured or meshed here: the surface is found when it
is drawn, and the cost is that of the field times the copies.  The body's field should be about a
distance (the primitives and what is made of them are; a part imported from STEP is replaced by its exact
distance, as offset() does), because a field that rises faster smooths by as much more.

### `smooth_difference(a, b, radius)`

a minus b, with a rounded blend along the cut

### `smooth_intersection(a, b, radius)`

Intersection with a rounded blend

### `smooth_union(a, b, radius)`

Union with a rounded blend of the given radius where the shapes meet (a number, or a field: a blend that
is round here and sharp there)

### `smoothstep(field, edge0, edge1)`

0 below edge0, 1 above edge1, a smooth S-curve between

### `sqrt_field(field)`

The square root of a field at every point (NaN where it is negative)

### `square_field(field)`

A field times itself at every point

### `step_field(field, edge)`

0 below the edge, 1 above (a sharp step; see smoothstep)

### `subtract_fields(first, *others)`

The first field minus the others: a - b - ... at every point

### `sum_fields(*fields)`

Sum of several fields

### `texture(body, pattern='grit', depth=0.1, size=1.0, region=None, fade=None, seed=1, axis='z', radius=None, side='both')`

A texture on the surface of `body`: a pattern of small bumps and pits laid over it (sandpaper, a grip, a leather grain ...), on all of it or only where `region` says.
`pattern` is one of 'grit' (sandpaper, sandblast), 'stipple' (round pits), 'leather' (grooves between cells, with a fine grain), 'ribs' (grooves across `axis`),
'dimples' (a 3D egg carton) and 'knurl' (diamonds round `axis`; give the `radius` it goes round), or a field of your own, about -1 .. 1.
`size` is the grain or the pitch (mm) and `depth` the height from a pit to a bump (mm); `side` 'both' puts the bumps outside and the pits inside the surface,
'in' only cuts into it (the part stays inside its size), 'out' only adds to it.  `region`: a surface picked on the body (select_surface), or a body that holds the
places to texture; the texture fades out over `fade` mm (the size, by default) so that no step is left at its edge.  A texture is made of the field of the body: the
render has to be fine enough to show it -- a resolution of about four samples to the size.  E.g. a rough grip on the top face:
top = select_surface(part, seed=(0, 0, 10)); gripped = texture(part, 'grit', depth=0.2, size=0.6, region=top)

### `thicken(field, thickness)`

A solid wall of the given thickness centred on a field's zero
surface (e.g. a plane, a TPMS surface, a shape's skin); the
thickness may be a field

### `twist_z(shape, degrees_per_mm, center=(0, 0))`

Twists a shape about the z axis; the rate may be a field

### `union_all(shapes, blend=0)`

Union of many shapes (optionally blended)

### `volume_of(shape, lower=None, upper=None, resolution=None)`

Volume of a solid (mm^3), by sampling a grid over its bounds
(resolution: samples per mm; by default ~100 along the longest side)

### `wall_thickness(shape, max_thickness=20.0)`

Local wall thickness (mm): the length of the chord through the
material along the surface normal.  Colour a part by it to find walls
that are too thin (or too thick) to make.  Capped at max_thickness.

### `wave(axis='x', period=10.0, amplitude=1.0, phase=0.0)`

A sine wave along an axis (or a field): amplitude * sin(2 pi s / period + phase)

### `x_field()`

The x coordinate as a field (mm)

### `y_field()`

The y coordinate as a field (mm)

### `z_field()`

The z coordinate as a field (mm)

## Regressions and data

Regressions: turn measured data into fields.

A regression is fitted to (x, y) data and can then be applied to numbers or
to any field -- typically a distance field -- to get a field that drives an
operation (a lattice thickness, an offset, a blend):

    from fieldes import *

    ball = sphere(30)
    # wall thickness measured / required at several depths below the skin
    t_of_depth = fit([(0, 2.0), (5, 1.4), (15, 0.9), (30, 0.6)], model='poly', degree=2)
    print(t_of_depth)                  # equation and goodness of fit (R^2)
    thickness = t_of_depth(depth_below(ball))      # a field
    lat = lattice(ball, cell_periodic('gyroid'), cell_size=8, thickness=thickness, skin=1.5)

Models (fit(..., model=...)):
    'linear'        a + b x
    'poly'          polynomial of the given degree (default 2)
    'exp'           a e^(b x)                 (y > 0)
    'exp_offset'    a e^(b x) + c
    'power'         a x^b                     (x > 0, y > 0)
    'log'           a + b ln x                (x > 0)
    'logistic'      c + L / (1 + e^(-k (x - x0)))
    'interp'        straight lines through the points
    'spline'        smooth natural cubic spline through the points
    'pchip'         smooth, shape-preserving cubic through the points
                    (no overshoot between points -- good for thicknesses)
    'auto'          the best of the parametric models (by corrected AIC)

By default a regression holds its end values outside the data range
(clamp=True), so a field never runs off to unphysical values; pass
clamp=False to extrapolate.

Also:
    fit_csv(path, x_column, y_column, ...)      fit data from a CSV file
    fit_field(points, values, degree)           3D polynomial field from
                                                scattered samples (x, y, z) -> v
    interpolate_field(points, values)           a field through scattered
                                                samples (radial basis functions)

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `FieldFit`

A fitted 3D field: .field (the Shape), .r2, .rmse, and call it with a
point for its value.

### `Regression`

A fitted regression.  Call it on a number (-> number) or on a field
(-> field): reg(3.0), reg(depth_below(part)).

Attributes: model, params, r2 (coefficient of determination), rmse,
equation (text), domain (min and max x of the data), clamp.

#### `Regression.on(self, field)`

The regression applied to a field (same as calling it)

#### `Regression.residuals(self)`

The data's y minus the fitted curve at each x (what the fit leaves unexplained)

#### `Regression.table(self, n=11)`

(x, y) samples of the fitted curve across the data range

### `fit(x, y=None, model='poly', degree=2, clamp=True)`

Fits a regression to data and returns a Regression, which can be
called on numbers or fields.

Data: fit([(x0, y0), (x1, y1), ...]) or fit(xs, ys) or fit({x: y}).
model: 'linear', 'poly' (with degree), 'exp', 'exp_offset', 'power',
'log', 'logistic', 'interp', 'spline', 'pchip' or 'auto'.

### `fit_csv(path, x_column=0, y_column=1, model='poly', degree=2, clamp=True, delimiter=None)`

Fits a regression to two columns of a CSV file.  Columns are given
by index or by header name; rows that aren't numbers are skipped.

### `fit_field(points, values=None, degree=2)`

Least-squares polynomial field in x, y, z through scattered samples,
e.g. measured temperatures or loads at points -> a smooth field.
points: [(x, y, z), ...] with values [v, ...], or [(x, y, z, v), ...].
Returns a FieldFit; its .field is the Shape.

### `interpolate_field(points, values=None, smoothing=0.0, method='rbf', power=2.0)`

A field passing through scattered samples (x, y, z) -> v.
method='rbf': polyharmonic radial basis functions (smooth, exact at
the samples unless smoothing > 0; up to a few hundred points).
method='idw': inverse-distance weighting with the given power
(no solve; bounded by the sample values).

## Surfaces and offsets

Closed-form surfaces: cheap primitives beyond planes, spheres and cylinders.

Each is a handful of arithmetic operations, so it meshes as fast as any
other shape.  The general forms are what the STEP importer fits to B-spline
faces (see reconstruct); the named ones are for modeling:

    quadric(coefficients, center, scale)        any surface of degree 2
    extruded_curve(coefficients, direction, origin, scale)
                                                a cubic curve swept straight
    revolved_curve(coefficients, axis, origin, scale)
                                                a cubic curve spun around an axis
    helical_curve(coefficients, lead, axis, origin, offset, scale)
                                                a cubic curve screwed along an axis
    helical_sweep(profile, lead, center)        a cross-section screwed along z:
                                                helical gears, twisted flutes
    helical_revolve(profile, lead, center)      an axial profile screwed around z:
                                                threads, springs, auger flights
    coil_spring(radius, wire_radius, lead, turns, center)
    ellipsoid(radii, center)
    elliptic_cylinder(a, b, center)             along z
    paraboloid(k, center)                       z >= k (x^2 + y^2), opening up
    hyperboloid(a, c, center)                   one sheet, along z
    wave_block(size, amplitude, period, center) a block with a wavy top

Coefficient orders (in coordinates w = (p - origin) / scale):
    quadric:  x^2 y^2 z^2 xy xz yz x y z 1
    curves:   u^3 u^2v uv^2 v^3 u^2 uv v^2 u v 1
      extruded_curve: u, v across the direction (see _frame)
      revolved_curve: u = distance from the axis, v = along the axis
      helical_curve: u, v across the axis, turning with the height
All three return scale * f(w): inside where negative.

### `coil_spring(radius, wire_radius, lead, turns, center=(0, 0, 0))`

A coil spring along z: `turns` turns of round wire (radius
`wire_radius`) around a coil of radius `radius`, rising `lead` per
turn, standing on `center`, its ends cut flat.

### `ellipsoid(radii, center=(0, 0, 0))`

An ellipsoid with semi-axes radii = (a, b, c) along x, y, z, numbers or fields
(distance-like near the surface; exact for a sphere)

### `elliptic_cylinder(a, b, center=(0, 0, 0))`

An infinite cylinder along z with an elliptic section (semi-axes a, b)

### `extruded_curve(coefficients, direction=(0, 0, 1), origin=(0, 0, 0), scale=1)`

A cubic curve g(u, v) = 0 in the plane across `direction`, swept
straight along it (u, v: along the two axes _frame picks)

### `helical_curve(coefficients, lead, axis=(0, 0, 1), origin=(0, 0, 0), offset=(0, 0), scale=1)`

A cubic curve g(u, v) = 0 in the plane across the axis (through
`origin`), turning as it advances along the axis -- one full turn per
`lead` (a negative lead turns the other way): the flank of a helical
gear or a worm, a thread, a coil.  The form the STEP importer fits to
such faces; u, v are measured from `offset` in the turned plane.

### `helical_revolve(profile, lead, center=(0, 0, 0))`

Like revolving a profile around the z axis (through `center`), but
advancing `lead` along it per turn (negative: left-handed): a
thread, a coil spring, an auger's flight.  The profile is a 2D shape
in the xy plane: x = distance from the axis, y = height; it must fit
within one lead of height (|y| < lead / 2), since the result repeats
every lead.  Unbounded along z -- cut it to length.

### `helical_sweep(profile, lead, center=(0, 0, 0))`

`profile` (a shape; its cross-section in the xy plane is what counts)
screwed along the z axis through `center`: turned by one full turn
per `lead` of height (negative: the other way).  A gear outline gives
a helical gear, a drill's outline its twisted flutes (for threads and
springs, whose profile is drawn through the axis: helical_revolve).
Unbounded along z -- cut it to length, e.g. with extrude.  Kept
close to a distance by dividing out the twist's stretching.

### `hyperboloid(a, c, center=(0, 0, 0))`

One-sheet hyperboloid along z: (x^2 + y^2) / a^2 - z^2 / c^2 = 1,
waist radius a (the inside is the part around the axis)

### `paraboloid(k, center=(0, 0, 0))`

The inside of z = k (x^2 + y^2) (opening up from its vertex at center)

### `quadric(coefficients, center=(0, 0, 0), scale=1)`

Any surface of degree 2: ellipsoids, elliptic / parabolic /
hyperbolic cylinders, cones, paraboloids, hyperboloids.
coefficients: x^2 y^2 z^2 xy xz yz x y z 1, in w = (p - center) / scale

### `revolved_curve(coefficients, axis=(0, 0, 1), origin=(0, 0, 0), scale=1)`

A cubic curve g(r, h) = 0 (r: distance from the axis, h: along it,
from `origin`) spun around the axis

### `wave_block(size, amplitude, period, center=(0, 0, 0))`

A block (size = (sx, sy, sz)) whose top is a wave:
z_top = sz / 2 + amplitude sin(2 pi x / period) sin(2 pi y / period)

## Lattices

Lattices: periodic TPMS and strut lattices, field-driven thickness, cell
maps (Cartesian, cylindrical, spherical), and filling bodies.

A lattice that repeats is made of a CELL: the one cell it is made of.  Four functions make one --

    cell_periodic(kind)              a standard cell: a strut cell (octet, bcc, kelvin ...), a TPMS
                                     (gyroid, schwarz_p ...) or a planar pattern (hexagon ...)
    cell_custom(region, geometry)    a custom cell of ANY geometry: a box for the extent of the cell and a field for
                                     what is in it -- the cell is their intersection, and repeats on any cell map
    cell_custom_truss(nodes, beams)  a custom strut cell: beams between nodes in the unit cube
    cell_custom_tpms(equation)       a custom triply periodic surface, from its equation

-- and lattice(), lattice_surface_conform(), strut_lattice(), tpms() ... take it as their `cell`, and nothing else: a
name such as 'gyroid' is not a cell, cell_periodic('gyroid') is.  The cell is only WHAT the lattice is made of; how thick
it is, how big, and where it goes (a body, a surface, a cell map) are the operation's.

A foam does not repeat, so it is not made of a cell: it is made around POINTS.  Three steps, each what its name says --

    random_points(body, ...)         points in a body: how far apart they are on average, how random, the nearest and the farthest
                                     two may be.  FielDes draws them, each as the dot of a gizmo
    voronoi_lattice(points, ...)     the edges of the Voronoi cells of the points, as beams: a foam
    delaunay_lattice(points, ...)    the Delaunay edges between the points, as beams: a stochastic truss

-- a lattice made of points fills the body the points were made in (so it takes no body of its own).

The surface of the body cuts a foam off.  random_points(body, surface=True) puts points ON the surface as well, that are cells of their own: a voronoi_lattice then is closed by the surface, with a net of beams over it that wraps
round the sharp edges and nothing cut off (a delaunay_lattice joins them along the surface); random_points(body, frame=True) gives the lattice a full beam along every sharp edge of the body, found from the body itself
(the outline of each flat face; a smooth fillet is no sharp edge), whatever the lattice is; and clip=True (the default) lets the body cut the lattice, the frame with it, clip=False leaves it whole.
point_cloud(points, body=None) is a set of points of your own, for the same lattices.  surface_lattice(body, cell_size) is a net on its own: a random lattice of one layer, that does not repeat, over the surface of any body.

The one-call version:

    from fieldes import *

    part = sphere(30)
    lat = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=1.0, skin=1.5)

    # graded: thicker walls near the skin, by a ramp or a regression
    t = ramp(depth_below(part), (0, 30), (1.6, 0.5))
    lat = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=t, skin=1.5)

    # by relative density instead of thickness (calibrated automatically;
    # the density may itself be a field)
    lat = lattice(part, cell_periodic('octet'), cell_size=10, density=0.2)

    # a foam: points in the body, and the Voronoi cells around them
    points = random_points(part, spacing=8, randomness=0.7)
    foam = voronoi_lattice(points, thickness=1.0)

    # cells that follow a sphere / cylinder
    lat = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=1.0,
                  cell_map=spherical(cells_around=16))

Building blocks (infinite lattices -- fields; fill() or lattice() trims them):
    tpms(cell, cell_size, thickness=..., style='sheet' | 'network', ...)
    strut_lattice(cell, cell_size, radius=... | thickness=..., node_radius=None, blend=0, ...)
    planar_lattice(cell, cell_size, wall, axis='z')      honeycombs, grids
    fill(body, lattice, skin=0, region='volume' | 'shell', depth=...)
    relative_density(lattice, cell_size)                 volume fraction
    lattice_parameter_for_density(cell, cell_size, density)

TPMS kinds: gyroid, schwarz_p, diamond (Schwarz D), neovius, lidinoid,
    split_p, iwp, frd, fischer_koch_s.  style='sheet' is a wall of the given
    thickness on both sides of the minimal surface ("walled TPMS");
    style='network' is the solid on one side of it, grown by `offset`
    ("skeletal TPMS").
Strut cells: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin,
    diamond, cross, tesseract, cuboctahedron.
Planar (2.5D) patterns: hexagon (honeycomb), triangle, square, kagome.
(All of them are names for cell_periodic(); the kinds are listed there.)

Thickness, radius, offset, density, node radius -- any of them can be a
field (a Shape), e.g. a regression of test data applied to a distance field.

Units are mm like everything else; cell_size may be (sx, sy, sz).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `CellMap`

Maps space to lattice coordinates (u, v, w), in mm, in which the
lattice repeats every cell size

#### `CellMap.describe(self, cell)`

A short description of the mapping, for the output pane

#### `CellMap.map(self, p, cell)`

The point p as lattice coordinates (u, v, w) in mm, in which the lattice repeats every `cell`

### `LatticeCell`

What a lattice is made of (see the module): made by cell_periodic(), cell_custom(),
cell_custom_truss() or cell_custom_tpms(), taken by every lattice operation as its `cell`.
.family      'strut' (beams between nodes), 'tpms' (a periodic surface), 'planar' (a 2.5D pattern) or 'shape'
             (any geometry in a box, see cell_custom)
.periodic    whether the cell repeats on a grid (so can follow a cell map or a surface)

### `LatticeGraph`

A graph of nodes and beams: nodes [(x, y, z), ...], beams [(i, j), ...].
.thicken(radius, blend) makes it solid; radius may be a number, a
list (one per node) or a field.

#### `LatticeGraph.lengths(self)`

The length of every beam (mm)

#### `LatticeGraph.thicken(self, radius=0.5, blend=0.0)`

The graph as round beams of `radius` mm: see graph_lattice()

### `PlanarCell`

A standard planar (2.5D) pattern (see cell_periodic)

### `PointSet`

A set of points: what random_points() makes, and voronoi_lattice() and delaunay_lattice() are made around.  FielDes draws
them, each as the dot of a gizmo (without the gizmo's handles: they are data, not something to drag).
.points     the points, (x, y, z) in mm
.spacing    the average distance between a point and its nearest neighbour that it was made for
.body       the body they were made in (None when they were not made in one): a lattice made of them fills it
len(points), iterating it, points[i] work as for a list; points.stats() says how the points really are

#### `PointSet.stats(self)`

How the points are: {count, mean, min, max} -- the distance from a point to its nearest neighbour (mm), on average, the
smallest and the largest of them

### `ShapeCell`

A cell of any geometry (see cell_custom): a box and a field, the cell is their intersection.
.region, .geometry   what it was made from
.lo, .hi, .size      the box the region is, per axis (mm)
.solid()             the cell on its own, as a shape you can look at

#### `ShapeCell.solid(self)`

The cell on its own: the geometry cut off by the region (their intersection)

### `StrutCell`

A standard strut cell that repeats (see cell_periodic)

### `TPMSCell`

A standard triply periodic minimal surface (see cell_periodic)

### `TPMSEquation`

Your own TPMS (or any triply periodic) equation, for tpms() and
lattice(): f(a, b, c) -> value, where a, b, c are the position in the
cell as phases (2 pi per cell).  Write it with + - * / and the
methods .sin() .cos() .sqrt() .square() of a, b, c, e.g. a gyroid:
    cell_custom_tpms(lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos() + c.sin() * a.cos())
Its value is turned into a distance in mm (its gradient is followed),
so thickness= is a real wall thickness.

### `UnitCell`

A custom strut unit cell: beams between nodes in the unit cube
(coordinates 0..1 across the cell, scaled by cell_size when used).
What cell_custom_truss(nodes, beams, mirror) makes; use it wherever a cell is
taken: lattice(body, cell), strut_lattice(cell), lattice_surface_conform(...).

nodes: {name: (x, y, z)} or a list of (x, y, z) (then names are the
    indices)
beams: pairs of node names (or indices); a third entry sets that
    beam's own radius in mm, e.g. ('c', 'v0', 1.2) -- beams without
    one take the lattice's radius
mirror: 'x', 'xy', 'xyz' ... copies the beams mirrored across the
    cell's mid-planes (x -> 1 - x ...), so you only draw one part of a
    symmetric cell

UnitCell.check() lists what would make the lattice fall apart or not
tile: nodes outside the cell, beams of zero length, nodes on a cell
face without a partner on the opposite face (the neighbouring cell
has nothing to connect to there), and loose ends inside the cell.

#### `UnitCell.check(self)`

What would make the lattice fall apart or not tile (a list of texts; empty when the cell is fine)

#### `UnitCell.radii(self)`

The beams that carry their own radius: {beam: radius in mm}

### `cartesian(origin=(0, 0, 0), rotation=None)`

Straight cells, optionally shifted (origin) and turned (rotation =
(rx, ry, rz) degrees)

### `cell_custom(region=None, geometry=None, check=True, **removed)`

A cell of ANY geometry of your own -- a box for the extent of the cell and a field for what is in it.  The cell
is the intersection of the two, and it repeats like any periodic cell: on a straight grid, or on a
cylindrical or spherical cell map.

region      the box the cell is: box(...) -- any box, its faces are the cell's
            faces and its size the cell's size
geometry    the field in it: any shape -- spheres, rods, a TPMS, a boolean of those ... -- that may reach
            out of the box (only what is inside is the cell)
check       warn when the cell does not tile: what the geometry does on a face of the region has to be what
            it does on the opposite face (a rod that leaves through one face must come in through the other,
            a sphere on a corner must be on all eight)

    cell = cell_custom(box((0, 0, 0), (10, 10, 10)),
                       union(sphere(3, (5, 5, 5)), cylinder(1.0, 20, (5, 5, -5))))
    lattice(part, cell)                                    # cells of 10 mm, as modelled
    lattice(part, cell, cell_size=6)                       # the cell scaled to 6 mm
    lattice(part, cell, cell_map=cylindrical(cells_around=12))

The cell is yours as modelled: thickness=, radius= or density= do not apply (put the thickness in the
geometry).  cell.solid() is the cell on its own, to look at it.  (Beams between nodes: cell_custom_truss().
A triply periodic surface from its equation: cell_custom_tpms().  The arguments nodes, beams, mirror, equation
and shape of the old cell_custom are gone: `**removed` is only there to say so, and what to write instead.)

### `cell_custom_tpms(equation, name='custom')`

A custom triply periodic surface, from its equation f(a, b, c): a, b, c are the position in the cell as
phases (2 pi per cell).  Write it with + - * / and the methods .sin() .cos() .sqrt() .square() of a, b, c, e.g.
a gyroid:

    cell = cell_custom_tpms(lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos() + c.sin() * a.cos())
    lattice(part, cell, cell_size=8, thickness=0.8)

Its value is turned into a distance in mm (its gradient is followed), so thickness= is a real wall thickness.
(Any other geometry as a cell: cell_custom(region, geometry).)

### `cell_custom_truss(nodes, beams, mirror='')`

A custom strut cell: beams between nodes in the unit cube (coordinates 0..1 across the cell, scaled by
cell_size when used).

nodes   {name: (x, y, z)} or a list of (x, y, z) (then the names are the indices)
beams   pairs of node names (or indices); a third entry gives that beam a radius of its own in mm, e.g.
        ('c', 'v0', 1.2) -- beams without one take the lattice's radius
mirror  'x', 'xy', 'xyz' ... copies the beams mirrored across the cell's mid-planes (x -> 1 - x ...), so you
        only draw one part of a symmetric cell

    cell = cell_custom_truss({'c': (0.5, 0.5, 0.5), 'o': (0, 0, 0)}, [('c', 'o')], mirror='xyz')
    lattice(part, cell, cell_size=8, radius=0.6)

(Any other geometry as a cell: cell_custom(region, geometry).)  See UnitCell: .check() lists what would make
the lattice fall apart or not tile.

### `cell_periodic(kind='octet')`

A standard cell that repeats on a grid -- what a lattice, a conformal lattice or a lattice on a cell map is
made of.

kind    a strut cell: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin (= truncated_octahedron),
        diamond_struts, cross, tesseract, cuboctahedron;
        a TPMS: gyroid, schwarz_p, diamond (Schwarz D), neovius, lidinoid, split_p, iwp, frd, fischer_koch_s;
        a planar pattern (2.5D walls): hexagon, triangle, square, kagome

Only the cell: its size, its thickness (radius, wall, offset) and where it goes are the lattice operation's.
    lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=1.0)
    lattice_surface_conform(part, cell_periodic('truncated_octahedron'), cell_thickness=2, cell_size=5)
(Custom cells: cell_custom(region, geometry), cell_custom_truss(), cell_custom_tpms().  A foam is made around points,
not of a cell: voronoi_lattice(random_points(body)).)

### `cylindrical(origin=(0, 0, 0), axis='z', cells_around=None, radius=None, rotation=None)`

Cells that wrap around an axis: radial, around, and along the axis.
Give the number of cells around (cells_around=) or the radius at
which cells are cell_size wide (radius=).

### `delaunay_lattice(points, thickness=None, radius=None, density=None, skin=0.0, blend=0.0, skin_blend=0.0, body=None)`

A stochastic truss: beams along the Delaunay edges between a set of points (every point joined to its neighbours), filling the
body the points were made in.  The arguments are those of voronoi_lattice(): the points, the beams' size, the skin ...

    points = random_points(part, spacing=8)
    truss = delaunay_lattice(points, thickness=1.0)

### `fill(body, lattice_field, skin=0.0, region='volume', depth=None, blend=0.0)`

Trims a lattice to a body.
skin: a solid skin of this thickness (mm) on the body's surface
region='volume' fills the whole body; region='shell' only the
    outer `depth` mm (a conformal lattice layer under the skin)
blend: rounds the joints between lattice and skin

### `graph_lattice(nodes, beams, radius=0.5, blend=0.0)`

Round beams along the edges of any graph: nodes [(x, y, z), ...],
beams [(i, j), ...] (node indices).  radius: a number, one per node,
or a field (evaluated at the nodes; each beam tapers linearly
between its ends).  blend rounds the joints.

### `lattice(body, cell=None, cell_size=None, thickness=None, radius=None, density=None, style='sheet', offset=None, skin=0.0, region='volume', depth=None, cell_map=None, node_radius=None, blend=0.0, skin_blend=0.0, wall=None, axis='z')`

A body filled with a lattice, in one call.

cell: what it is made of -- cell_periodic(kind) (a TPMS: gyroid, schwarz_p, diamond, neovius, lidinoid, split_p,
    iwp, frd, fischer_koch_s; a strut cell: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin,
    diamond_struts, cross, tesseract, cuboctahedron; a planar pattern: hexagon, triangle, square, kagome),
    cell_custom(region, geometry), cell_custom_truss(nodes, beams) or cell_custom_tpms(equation).
    Default: cell_periodic('gyroid').  (A foam is not a cell: voronoi_lattice(random_points(body)))
cell_size: mm, or (sx, sy, sz), or a FIELD (cells bigger here and smaller there).  (Default 10 mm; for a
    cell_custom(region, geometry) cell the size of its region, so that it comes out as you modelled it -- a
    larger or smaller size scales the cell.)  A cell size field is blended from lattices of cells a factor 2
    apart (up to a factor of 16 in all): the cells are exact where the field is one of those sizes and a blend of
    the two next to it between them
thickness: the member size of every cell -- the wall of a sheet TPMS, the diameter of the beams of a strut
    cell (radius= is the same thing for beams, half of it); offset (network TPMS), wall
    (planar).  Numbers or fields
density: instead of the member size, a relative density 0..1 (a
    number or a field); calibrated automatically
style: 'sheet' or 'network' (TPMS)
skin: solid skin thickness on the body's surface (0 = none)
region: 'volume' or 'shell' (only the outer `depth` mm)
cell_map: cartesian(...), cylindrical(...) or spherical(...)
node_radius, blend: joint spheres and joint rounding (struts)
skin_blend: rounds the lattice-to-skin joints

### `lattice_parameter_for_density(cell, cell_size, density, style='sheet', samples=40)`

The thickness (sheet TPMS), offset (network TPMS), wall (planar) or
radius (struts) that gives a lattice of this cell the requested relative density
(0..1), found by bisection on a sampled unit cell

### `planar_lattice(cell, cell_size=10.0, wall=0.8, axis='z', cell_map=None)`

A 2.5D pattern of walls, extruded along an axis: a honeycomb
(cell_periodic('hexagon'); cell_size = flat-to-flat width), or a
triangle, square or kagome grid.  wall is the wall thickness (mm,
may be a field).

### `points_graph(points, style='delaunay')`

The Delaunay (or Voronoi) graph of your own points

### `random_points(body=None, spacing=8.0, randomness=0.5, min_distance=None, max_distance=None, seed=1, bounds=None, surface=False)`

Points in a body -- with the say over how random they are.  A foam is made around them: voronoi_lattice(points).

body          where the points are made: inside it, and a layer of one `spacing` outside, so that the cells at its surface are
              closed.  Without a body, give bounds=((x0, y0, z0), (x1, y1, z1))
spacing       how far a point is from its nearest neighbour, on average (mm): the points are made so that they come out that far
              apart whatever the randomness (points.stats() says how far they really are)
randomness    0 to 1: how much the points are moved off a regular grid -- 0 is a regular body-centred grid (every cell the same),
              1 is as random as the distances below allow
min_distance  no two points are closer than this (mm).  Default: `spacing` less 60 % of the randomness -- a low number lets the
              points clump, `spacing` itself makes them even
max_distance  no point is farther than this from its nearest neighbour (mm): a point that is more alone than that is given a
              neighbour.  Default: none.  It cannot be less than min_distance.  (min_distance and max_distance are the range the
              distances to the nearest neighbour are in: the closer together they are, the more regular the points)
seed          another number is another pattern (the same number is the same points again)
surface       True adds a set of points ON the surface of the body, about `spacing` apart (the nodes of a random net on the surface, as surface_lattice() makes it),
              so that a lattice made of the points has a clean net of beams all over the surface of the body, and not beams that the surface cuts off:
              delaunay_lattice() has its beams along the surface between them, voronoi_lattice() a net of beams one beam radius inside the surface.  The other
              points keep their distance to them.  Needs a body

Returns a PointSet, which FielDes draws in the viewport (each point as the dot of a gizmo): len(points), points.stats() (the
distances the points really have), points.body (the body they were made in), and

    points = random_points(part, spacing=8, randomness=0.7, min_distance=5, max_distance=11)
    foam = voronoi_lattice(points, thickness=1.0)
    truss = delaunay_lattice(points, thickness=1.0)

make a foam and a stochastic truss around them, in the same body.

### `relative_density(lattice_field, cell_size, samples=40, origin=(0, 0, 0))`

Volume fraction of an (untrimmed, Cartesian) lattice: the share of
one unit cell it fills, 0..1

### `spherical(origin=(0, 0, 0), cells_around=None, radius=None, rotation=None)`

Cells in shells around a point: radial, around (longitude) and pole
to pole (latitude).  Give cells_around= or radius= (where the cells
should be cell_size wide).  Cells shrink towards the poles.

### `strut_lattice(cell, cell_size=10.0, radius=None, node_radius=None, blend=0.0, cell_map=None, thickness=None)`

An infinite strut (beam) lattice: round beams of `thickness` mm (their diameter;
or `radius` mm, which is half of it: give one of the two, default radius 0.8)
along the edges of a unit cell, repeated every cell_size.
cell: a strut cell -- cell_periodic('cubic' | 'bcc' | 'bccz' | 'fcc' | 'fccz' | 'octet' | 'octahedron' |
    'kelvin' | 'diamond_struts' | 'cross' | 'tesseract' | 'cuboctahedron'), or your own:
    cell_custom_truss(nodes, beams) (beams may have their own radius)
node_radius: spheres at the joints (defaults to none)
blend: rounds the joints with a smooth blend of this radius
thickness / radius / node_radius may be fields.

### `surface_graph(body, cell_size=8.0, pattern='triangle', seed=1, bounds=None)`

A graph on a body's surface: points about cell_size apart joined into
triangles (pattern='triangle') or the dual cells (pattern='voronoi',
mostly hexagons)

### `surface_lattice(body, cell_size=8.0, pattern='voronoi', thickness=None, radius=None, seed=1, blend=0.0, bounds=None)`

A lattice that does not repeat, over the surface of a body: a random net of round beams, one layer thick, the axes of the beams lying ON the surface.

body       any body (a solid, a lattice ...): the whole of its surface is covered, hollows and edges too
cell_size  how far apart the nodes of the net are on average (mm)
pattern    'voronoi': cells that are mostly hexagons, three beams at every node -- or 'delaunay': triangles, six beams at a node on average
thickness  the diameter of the beams, mm: a number, or a field.  radius= is the same thing, half of it.  Default: 16 % of the cell size
seed       another number is another net (the same number is the same net again)
blend      rounds the joints
bounds     ((x0, y0, z0), (x1, y1, z1)): where to look for the surface, when FielDes cannot find the extent of the body

    net = surface_lattice(part, cell_size=6, thickness=1.0)

Half of each beam is outside the body: intersect the net with the body to have it inside.

### `tpms(cell, cell_size=10.0, thickness=1.0, style='sheet', offset=0.0, cell_map=None, invert=False, fast=False)`

An infinite TPMS lattice (a field; trim it with fill() or use
lattice()).
cell: a TPMS cell -- cell_periodic('gyroid' | 'schwarz_p' | 'diamond' | 'neovius' | 'lidinoid' |
    'split_p' | 'iwp' | 'frd' | 'fischer_koch_s'), or your own: cell_custom_tpms(f)
style='sheet': walls of `thickness` mm centred on the surface
style='network': the solid on one side of the surface, grown by
    `offset` mm (0 = half the volume for gyroid / diamond / P);
    invert=True takes the other side
style='surface': the signed distance to the surface itself
thickness / offset may be fields.
fast=True: a quicker, slightly less exact distance (walls ~5 % thin)

### `unit_cell_beams(cell)`

The beams of a strut cell (in unit-cube coordinates), made
periodic: every beam of the infinite lattice that passes near the
cell, clipped to it (with a margin for cells that aren't
mirror-symmetric).  Returns (beams, margin).

### `voronoi_graph(body, cell_size=8.0, style='voronoi', relax=2, seed=1, bounds=None)`

A random graph filling a body: Poisson-disk points about cell_size
apart, joined by the edges of their Voronoi cells (style='voronoi',
a foam) or by their Delaunay edges (style='delaunay', a stochastic
truss).  relax: iterations that make the cells more even.

### `voronoi_lattice(points, thickness=None, radius=None, density=None, skin=0.0, blend=0.0, skin_blend=0.0, body=None)`

A foam: the edges of the Voronoi cells around a set of points, as round beams, filling the body the points were made in.

points     random_points(body, ...) -- how far apart they are, how random, the nearest and the farthest two may be -- or a list of
           (x, y, z), which does not know a body: then say it with body=
thickness  the diameter of the beams, mm: a number, or a field.  radius= is the same thing, half of it.  Default: 16 % of the
           spacing of the points
density    instead of the beams' size: the share of the body that is material (0..1), calibrated
skin       the thickness of a solid skin on the surface of the body (0: none)
blend      rounds the joints; skin_blend rounds the joints with the skin
body       the body to fill -- only for a list of points.  Points from random_points() fill the body they were made in

    points = random_points(part, spacing=8, randomness=0.7, min_distance=5, max_distance=11)
    foam = voronoi_lattice(points, thickness=1.0)

## Lattices that follow a surface

A lattice that follows a surface: its cells lie on the surface, face its normal and are as big as asked all
along it -- the "conformal" lattice of nTop.

    from fieldes import *

    # a thin shell of the part filled with strut cells 6 mm wide: the cells run through its thickness
    skin = lattice_surface_conform(shell_outside(part, 4), cell_periodic('octet'), cell_thickness=None, cell_size=6)

    # a thin layer (3 mm, the default) of strut cells 6 mm wide on the surface of the part, standing out of it: the cells are
    # flattened to fit (stretch_cell=True, the default)
    layer = lattice_surface_conform(part, cell_periodic('octet'), side='outside', cell_size=6, radius=0.4)

    # the same, but the cells keep their own proportions (as deep as they are wide): 3 mm of each stands out of the surface, and the
    # rest of it, on the inside of the body, is cut off at the surface
    rivets = lattice_surface_conform(part, cell_periodic('octet'), side='outside', stretch_cell=False, cell_size=6, radius=0.6)

    # strut cells standing 6 mm out of the surface of the part
    ribs = lattice_surface_conform(part, cell_periodic('octet'), side='outside', cell_thickness=6, cell_size=6, radius=0.6)

    # ... or only over a face you picked (right-click it in FielDes, or select_surface())
    top = select_surface(part, seed=(12.5, 40.0, -3.0), angle=10)
    ribs = lattice_surface_conform(top, cell_periodic('bcc'), cell_thickness=6, cell_size=6, radius=0.6)

    part_with_ribs = union(part, ribs)

    # an open surface of no thickness -- a field that is zero on it (negative below, positive above) -- with a patch
    # of it: one layer of cells on its positive side, cut off at the edge of the patch
    wave = Shape.Z() - 8 * (0.12 * Shape.X()).sin()
    layer = lattice_surface_conform(wave, cell_periodic('bcc'), within=box((-30, -20, -14), (30, 20, 14)),
                                    side='outside', cell_thickness=6)

    # a periodic surface instead of struts: a gyroid skin that follows the part, 6 mm periods, 1 mm walls
    texture = lattice_surface_conform(shell_outside(part, 4), cell_periodic('gyroid'), cell_thickness=None, cell_size=6, thickness=1.0)

A body's surface is what the cells are laid on.  Where a plain lattice() cuts a straight grid off at the surface,
here the grid is drawn on the surface itself: a row of cells runs along it and bends with it, round a cylinder,
over a fillet, along an S-shaped surface, and every cell has its top and bottom face parallel to the surface and its
sides along the surface normal -- the same face towards the normal everywhere.  The layer is `cell_thickness` thick (3 mm by
default: a thin layer, for riveting and the like), measured from the surface: with side='inside' (the default for a body) it goes into
the body, with side='outside' the cells stand out of the surface (ribbing standing on the part).  Its cells are flattened to fit
(`stretch_cell=True`); with `stretch_cell=False` they keep their own proportions, and only `cell_thickness` of them stands out of the
surface, the rest -- on the other side of it, the inside of the body -- being cut off there.  `cell_thickness=None` fills the body
instead: the cells run through its thickness, one layer for a thin shell or sheet, more where it is thicker (at most three cells
deep).  A *surface* -- a field that is only a surface, with no thickness (and so no other face, no rim) -- gets one layer, on the side
`side` names (`'outside'` is the side the field is positive on), cut off at the edge of the region given as `within=`.

It is made from the body's field and nothing else: the surface is where the field is zero, its normal the field's
gradient.  No mesh of the body is made and no distance is taken to one; the mesh is only what is drawn at the end.

How the cells are laid out
    The surface is first covered by ONE MESH OF QUADS, one quad to a cell, before any cell is made: its rows follow the surface's own
    directions -- along a sharp edge, round a hole, along a handle -- and its quads are `cell_size` wide where the surface lets them be.
    There are three layouts, for three kinds of surface, and which one is used is TOLD, never guessed from the field: a closed BODY, a
    SURFACE that is only a surface (cut by the region you give it), and a SELECTION (a patch picked on a surface with select_surface()).

    A BODY (a closed solid): the surface ends inside its box, and the mesh is CLOSED and covers ALL of it.  It is made from a cloud of
    points of the surface, in five steps, all from the field:

    1. points of the surface a third of a spacing apart (the centres of the cubes the surface passes through, put onto the surface along the
       field's gradient); two points are neighbours when the SURFACE joins them, not when they are close in space -- a hop is accepted if its
       middle lies on the surface, or if the chord put onto the surface is a connected curve of about its length -- so a thin wall or a gap
       between two points is never crossed; at a sharp edge a point is put ON the edge;
    2. a direction field with four-fold symmetry and a lattice position field over the points; sharp edges are lines the field follows, so
       the rows of cells run along them;
    3. lattice vertices where the position field puts them, made fine enough that the part of the surface nearest to each vertex is a disc
       and two vertices that are neighbours touch along ONE arc;
    4. a face for every three of those parts that meet at a place (one for four when four meet), taken in the order of how many points see
       them, as long as the faces keep making a surface: every edge on two faces, the faces round a vertex one fan, all going round the
       same way;
    5. a face with k corners is made into k quads -- a corner, the middle of the side after it, the middle of the face, the middle of the
       side before it -- the middle of a side being shared by the two faces that have it, so the quads fit edge to edge.  Finally the nodes
       are moved over the surface towards the middle of their neighbours and put back on it, a little at a time (a node on a sharp edge
       stays; no move may fold a cell or make a corner worse): the cells stay the same cells.

    NOTHING IS LEFT OUT.  Every separate piece of the surface is mapped on its own and checked on its own (four different corners to every
    cell, every edge on two cells, one fan round every vertex, one connected surface).  A piece too small for cells of `cell_size` -- a
    cavity inside a boss, a small island of the field -- is sampled finer, by itself, until it can have a closed map: its cells are then
    smaller than asked.  Two surfaces that the points join by a few hops (two walls closer together than the points can tell apart) are
    cut apart and laid out again.  A speck that the points found where the field comes near zero but never crosses it is not a surface: it
    is given up only after the field has been read on a fine grid round it and found to keep one sign.  The layout is made
    from several placements of the grid the samples are taken on, and the topology of what comes out is VOTED on: a map is used when
    two placements agree on how many separate surfaces there are and on their Euler number; a surface that the cells can resolve gives
    the same answer from every placement, one they cannot (a hole or a gap narrower than a cell, a wall thinner than one) gives answers
    that differ with where the samples fall, and then nothing is made and the error says so.  If every placement is refused, a piece
    that can be judged but is not a good map may be sampled once finer (cells half as big).  An error says which piece of the surface
    could not be mapped and where, and what to try (a cell size about half as big), and nothing is returned: a map that does not cover
    the whole surface, or that another map of the same surface contradicts, is never used.

    A SURFACE THAT IS ONLY A SURFACE -- a field that is zero on a sheet with no body behind it, which goes on past the region you give with
    within=: the layout has an edge where the region cuts it, and only there.  A scaffold of it is made (a triangle mesh by marching
    tetrahedra over the cubes the surface passes through, its vertices moved onto the surface and onto its sharp edges), a direction field
    and a lattice position field are solved over the scaffold, the triangles that lie in one square of the lattice make a region, and a
    region with k corners is made into k quads as above; the nodes are moved over the surface until the cells are even.  The sheet is laid
    out over a margin of two cells round the region, so that the region lies well inside it.

    A SELECTION is a surface, and is laid out as one, by the same method as a sheet: from the surface the picked patch makes -- the points
    the walk over it took, with their normals, as a field (the height above the patch along its normal) -- over the part of it that is by the
    patch, and only there.  The body it was picked on is not looked at, nor its other faces, nor how thick it is: a shell, a plate and a solid
    are the same to it, and the cells cover the patch and nothing else (their edge follows the patch's, to within a third of a cell).  The
    lattice stands on the side the surface faces (side='outside', `cell_thickness` thick).

    Every node is on the surface, and every point of a cell is put back on it too, so no strut lies in a hole or outside the body.  Cells
    are distorted wherever the surface cannot be flattened -- over a fillet, round the lip of a rim, across a dome -- and none is left out
    for that: a cell is as stretched, squeezed or bent as the surface makes it, and the beams of the unit cell follow.  Where a narrow
    fillet or a small step is narrower than a cell, or the rows of cells have to turn a corner, some cells are less regular.

cell
    a strut cell ('octet', 'bcc', 'cubic', 'kelvin', ...: see lattice()) -- `radius` is the strut radius.  The cell's
    beams are carried over to every cell of the grid: the lattice is a graph of straight beams, and the mesher
    measures the distance to the beams near a point, so it is as quick as a graph lattice.

    or a periodic surface ('gyroid', 'schwarz_p', 'diamond', 'neovius', 'lidinoid', 'split_p', 'iwp', 'frd',
    'fischer_koch_s') -- `thickness` is the wall of a sheet, or style='network' for the solid on one side of it.  The
    periodic function is evaluated in the coordinates of the cell a point is in (s, t along the surface, w through the
    layer, found from the four edges of the cell and the depth of the layer), one period to each cell on the surface and
    to each layer, so its sheets run through the layers and bend with the surface.  The ones made of cosines only
    (schwarz_p, neovius, iwp, frd) look the same after a quarter turn, so they join up where rows of cells meet; the
    others (gyroid, diamond, ...) have a seam there.  Rendering costs more than for struts: the sheets fill the layers.
    Where three or five cells meet the pattern is rougher; where the surface is flat enough, or one orientation of the
    texture on every face is fine, a plain union(part, lattice(shell_outside(part, depth), cell_periodic(...),
    cell_size=...)) has none of that.

The lattice reaches a little into the part (by a strut's radius) so that it fuses with it when you add the two.  It
makes no skin, adds no body and cuts nothing of the part: combine it with the part yourself.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `lattice_surface_conform(surface_field, cell=None, cell_thickness=3.0, stretch_cell=True, cell_size=5.0, radius=None, layers=None, side=None, blend=0.0, direction=None, bounds=None, thickness=None, style='sheet', offset=0.0, invert=False, skin=0.0, within=None, grid_offset=0)`

A lattice that follows a surface (see the module): its cells lie on it, as big as asked all along it, each
with a face towards the surface normal -- filling the body the surface bounds (side='inside', the default) or
standing out of it (side='outside').

surface_field   the surface, as ONE argument whatever it is: a body (a closed solid: its surface), a surface
                (a field that is zero on it, with no body behind it), or a select_surface(...) selection (the
                patch picked on a body: a SURFACE, laid out as one -- a method of its own, that has nothing to do
                with what is behind it or how thick that is -- the lattice is laid on that patch only)
cell            what it is made of: cell_periodic('octet') (a strut cell: octet, bcc, cubic, kelvin ...; a
                TPMS: gyroid, schwarz_p ...) or cell_custom_truss(nodes, beams).  Default: cell_periodic('octet')
cell_thickness  how thick the layer of cells is (mm), measured from the surface: how far the cells stand out of it
                (side='outside') or go into the body (side='inside').  Default 3 mm: the lattice is a thin layer,
                for riveting and the like.  None: for a body filled from inside, as deep as the body is under each
                cell (a thin shell: its thickness; at most three cells); for a surface, a selection or side='outside',
                one cell
stretch_cell    True (the default): the cells are deformed to fit the thickness -- `cell_size` along the surface,
                `cell_thickness` through it, so a thin layer has flat cells.  False: the cells keep their own
                proportions (as deep as they are wide), and `cell_thickness` of them stands out of the surface;
                the rest of each cell, on the other side of the surface (the inside of the body), is cut off there.
                (`layers` then counts whole cells through the depth; a thickness over one cell makes more of them)
within          where, besides: any shape, or a list of them, the lattice is kept inside it (default: everywhere on the surface)
cell_size       mm along the surface
thickness   the thickness of the cell's members, mm: the diameter of the beams of a strut cell,
            the wall of a TPMS sheet (below).  Default: beams 24 % of cell_size across (not more than half of the layer's
            thickness), a sheet 15 % of cell_size
radius      the same for beams, as a radius (half of thickness; give one of the two): a number, or a field (the
            struts taper between the nodes).  Struts inside a body keep inside it.  Default: 12 % of cell_size, but not
            more than a quarter of the layer's thickness (a thin layer has thin struts)
layers      the number of cells through the depth (default: as many as fit, at least 1)
side        'inside' (the default for a body: the lattice fills it) or 'outside' (it stands out of the surface: the
            default for a selection or a surface, on the side its normal faces -- the side the field is positive on)
blend       rounds the joints of struts
direction   the way the rows of cells run where the surface gives them no way (a flat or smoothly curved part with
            no edge to follow) (default: along x)
bounds      ((x0, y0, z0), (x1, y1, z1)) of the surface, if its extent cannot be found (a field with no end: an
            open surface).  Default: its extent, and if it has none, that of `within`
grid_offset 0 (the default), 1, 2 or 3: which of four fixed grids of sample points the surface is laid out from.
            The layout is made ONCE, from that grid, and is the same every time.  Where the surface has detail
            about as small as the spacing of the points (a narrow neck, a thin wall, a tiny gap) the grid decides
            which points are joined, so a part that does not close with one value may close with another, or give a
            different count of holes: if the call says no closed map could be made, or the warning says the
            topology may be off, try the other values (or a smaller cell_size), looking at what the part has there

For a TPMS kind (gyroid, schwarz_p, diamond, neovius, lidinoid, split_p, iwp, frd, fischer_koch_s) the periodic
surface follows the surface, one period to each cell on it and to each layer:
thickness   the wall of a sheet, mm (default 15 % of cell_size)
style       'sheet' (walls `thickness` thick), or 'network' (the solid on one side of the surface, grown by
            `offset` mm; `invert=True` takes the other side)
skin        a solid skin this deep (mm) against the faces of the layers

Returns the lattice alone, as a shape: add it to the part with union().

## Selecting surfaces

Selecting a surface: the flood fill of a CAD program, as a field.

    from fieldes import *

    top = select_surface(part, seed=(12.5, 40.0, -3.0), angle=10)
    top                                       # displayed: the part's surface, only the patch of it, lit up

`select_surface` picks the patch of the part's surface around `seed` (a point on or near it) by walking over the
surface: from the seed, in small steps along it, as long as the surface stays within `angle` degrees -- with
mode='flat' (the default) of the way it faced at the seed (a flat face, or a gently curved one); with mode='smooth' the
surface may turn at most `angle` degrees within 10 mm of the walk -- a limit on how tightly it bends: a cylinder, a fillet or
a gently rounded skin is followed, a tight bend (the toe of a shoe, a small round) or a sharp edge stops it.  `radius` stops
it that far from the seed.

It is made from the part's FIELD alone -- its value and its gradient, which is the surface normal -- the way the
conformal lattice is: a step is carried onto the surface where the field is zero, and the angle is the angle of the
gradients.  No mesh of the part is made or read, so it does the same on any shape however it was made (a
reconstructed STEP part, a tessellated one, a mesh, a CSG model), and a thin wall is no harder than a thick one: the walk
follows the surface, not the space, and finds no surface to step onto past an edge.

What comes back is a SURFACE -- the patch of the part's surface, nothing thicker -- as a field, like everything else here.
It is a region you can give to `fixed()` and `force()`, show on the part, combine with other shapes, or hand to
`lattice_surface_conform()` as the surface to put a lattice on:

    base = fixed(select_surface(part, (0, 0, 0)))
    push = force(top, (0, -100, 0))
    result = static_analysis(part, supports=[base], loads=[push], material=aluminium)

In FielDes, right-click a surface in the viewport: the menu holds the mode, the angle and the radius, and
writes the `select_surface(...)` line into the script under the part, like everything else the program does.

`surface_from_bodies` picks a surface by other bodies instead of by a place: the surface of the FIRST body, where it meets the
bodies that follow.

    plate_holes = surface_from_bodies(plate, bolt_1, bolt_2)      # the walls of the holes the two bolts sit in
    skin = surface_from_bodies(plate)                             # no other body: the whole surface of the plate

It is a surface like the one `select_surface` makes -- a region for `fixed()` and `force()`, a surface to lay a lattice on --
and it too is made from the fields alone: the first body's surface is where its field is zero, and "meets" is a number, the
distance of that surface from the other bodies (their field), so no mesh is made and a body that is dragged or has var()
numbers moves the selection with it.  In FielDes: select several models in the model tree (Ctrl or Shift click), right-click
one, Operations > Surfaces > surface_from_bodies: the first selected is the body, the others are what it is met by.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `BodiesSurface`

The surface of a body where it meets other bodies (see surface_from_bodies): a surface, with the attributes of a
SurfaceSelection (.shape the first body, .patch, .whole, .surface, .cover, .spacing) and .others (the bodies that
select it, a list: empty for the whole surface), .tolerance

### `SurfaceSelection`

A patch of a surface (see select_surface): a surface.
.shape (what it was picked on), .surface (the patch as a field: the height above it, zero on it), .seed, .angle, .mode, .samples (how many points of the surface the
walk took), .spacing (how far apart they are), .patch (the distance to the patch's surface, a field: zero on it,
to within `.cover`), .whole (the distance to the whole surface, a field: the shape's own value, |f|, which is the
distance where the shape's field is one -- as the field of an imported part or a mesh is; it equals .patch where the
nearest point of the surface is in the patch), .cover (how far the surface of the patch can be from a sample: the
patch and the whole differ by less than that where the nearest surface is in the patch)

### `select_surface(shape, seed, angle=15.0, mode='flat', radius=None, resolution=None, bounds=None)`

The patch of the surface of `shape` around the point `seed`, found by a flood fill over the surface made from the
shape's field (see the module): a surface.

seed        a point on (or near) the surface: (x, y, z)
angle       degrees: with mode='flat' how far from the way the seed's surface faces it may face, with
            mode='smooth' how much it may turn within 10 mm of the surface (how tightly it may bend)
mode        'flat' (a face, flat or gently curved) or 'smooth' (round faces: a bend tighter than `angle` per 10 mm
            stops it, and so does a sharp edge)
radius     mm: stop this far from the seed (default: no limit)
resolution  steps per mm along the surface (default: about 150 along the longest side, at least 0.1): a finer one
            follows smaller faces and places the edge of the patch more exactly, and costs more (the walk takes a
            sample for every step of the surface it reaches)
bounds      ((x0, y0, z0), (x1, y1, z1)) of the shape, if it cannot be found

Returns a SurfaceSelection: a shape (so it is shown, hidden, deleted like any), usable as a region:
fixed(selection), force(selection, ...), lattice_surface_conform(selection, ...).

### `surface_from_bodies(body, *others, tolerance=None, bounds=None)`

The surface of `body` where it meets `others`, the bodies that follow: the parts of its surface that intersect them (that
touch them or run through them), as a surface.  With no other body, the whole surface of `body`.

    holes = surface_from_bodies(plate, bolt_1, bolt_2)   # the plate's surface where the bolts are
    skin = surface_from_bodies(plate)                    # all of it

body        the body whose surface is selected (a part, an imported part, any shape)
others      the bodies that choose where: a point of the surface of `body` is selected when it is inside one of
            them or within `tolerance` of one (several can be given, or a list)
tolerance   mm: how close a body must come to the surface to count as meeting it (default: half a percent of the
            size of `body`) -- bodies that touch at a face, or that sit in a hole with a little play, meet; a gap
            wider than this is a gap
bounds      ((x0, y0, z0), (x1, y1, z1)) of `body`, if it cannot be found

It is made from the fields alone, like everything: the surface of `body` is where its field is zero, and the distance of
a point from the others is their field.  No mesh is made, and a body that moves (a var() number, a drag) moves
the selection.  The selection is a thin layer along the surface, a hundredth of the body thick at most -- not a setting: a
surface has no thickness -- the way select_surface makes it.  The patch is cut by the others the way a box cuts
it: where one ends, the selection ends (it follows no face to its edges).  What comes back is a surface, shown
lit up on the body, and a region for `fixed()` and `force()` (and the other conditions), or
`lattice_surface_conform()`.  If the bodies do not meet, nothing is selected (a note says so when their boxes do not
even touch).

## Corners and edges of a body

Corners and edges of a body, found from its field: the points and lines a CAD program lets you snap to.

    from fieldes import *

    corner = corner_at(part, (10.2, 9.9, 20.1))            # displayed: the point where three faces of the part meet, nearest that place
    edge = edge_at(part, (10.2, 0.1, 19.8))                # displayed: the straight line of the edge nearest that place, from corner to corner
    on_edge = edge_point_at(part, (10.2, 0.1, 19.8))       # displayed: the point of the nearest edge that is nearest that place (any edge, a circle too)

`near` is a place on (or near) the surface -- where you right-clicked: in FielDes, right-click a body close to an edge or a corner and the menu offers
the call that fits (it is offered only where there is one), written under the body like `select_surface` is.

They are made from the part's FIELD alone -- its value and its gradient, which is the surface normal --, as `select_surface` is, and no mesh of the part is made or
read: around `near` the surface is sampled and its normals are sorted into faces; where two faces meet at a sharp angle there is an edge, where three do there is
a corner, and its place is where the planes of the faces meet (worked out again from smaller and smaller neighbourhoods, so that a curved face is followed).
What counts is a *jump* of the normal that stays a jump however closely it is looked at: a smooth surface -- a sphere, the side of a cylinder, a fillet --
turns by less and less the closer it is looked at, so it has neither edges nor corners; the rim of a cylinder, where the side meets the flat end, is an edge
(`edge_point_at` gives a point of it; `edge_at` needs a straight one, and says so when the edge curves).

Three more points to snap to:

    middle = circle_center_at(part, (10.2, 0.1, 19.8))     # displayed: the centre of the circular edge nearest that place (the rim of a cylinder or a hole, the base of a cone)
    corner = corner_at(rounded_part, (10, 10, 20), rounded=True)    # the corner that the rounding of three edges replaced: where the planes of the flat faces round it meet
    on_edge = edge_point_at(rounded_part, (10, 0, 19.8), rounded=True)   # a point of the sharp edge that a rounding replaced
    centre = face_center(select_surface(part, (5, 5, 0)))  # the middle of a face (a selection of surface): the mean of the places it was sampled at

A rounded edge has no sharp edge: it turns by a little all along the rounding.  With `rounded=True` the faces on both sides are looked at from further and further away -- past
the rounding, onto the flat faces -- and the point is where their planes meet, once two looks at different distances agree on it.

What comes back are the ordinary things: a `point` (so it is a corner of a profile, the end of a `polyline`, a place for a gizmo) and a `polyline`
of two points.  They are computed when the script runs, from `near` and the body: the body can be changed and they follow, as long as the edge or
corner is still near that place.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `circle_center_at(shape, near, scale=None, bounds=None, influence=None)`

The centre of the circular edge of `shape` nearest `near`: a point on the axis of a cylinder's rim, of a hole's edge, of the base of a cone.  The circle is read from the
edge itself at several places round it, so it is the circle that the edge is, not one fitted to a mesh.  Raises ValueError when the edge nearest `near` is not a circle
(a straight edge, an oval, a corner).  See the module and `corner_at`.

### `corner_at(shape, near, scale=None, bounds=None, rounded=False, influence=None)`

The corner of `shape` nearest `near`: a point, where three faces of the surface meet at sharp angles.

near    a place on or near the surface (x, y, z) -- where the corner is looked for
influence  how far the point reaches, in mm -- a number or a `var()`: the radius of a field made of it (`radial_field`, `distance_to_point`), as for `point(x, y, z, influence=r)`.  Select the point and the ring of its gizmo drags it (the point is locked to its body, so the ring is all its gizmo has); the same for `edge_point_at` and `circle_center_at`
scale   how far round `near` is looked, in mm (default: 3 % of the body)
bounds  ((x0, y0, z0), (x1, y1, z1)) of the shape, if it cannot be found
rounded True: a corner that the rounding of three edges replaced is found too -- the point where the planes of the flat faces round it meet

Raises ValueError when there is no corner round `near`.  See the module.

### `edge_at(shape, near, scale=None, bounds=None)`

The straight edge of `shape` nearest `near`: a polyline of two points, from the corner at one end of the edge to the corner at the other.  Raises ValueError
when there is no edge round `near`, or when it curves (a circle is not a line: `edge_point_at` gives a point of it).  See the module and `corner_at`.

### `edge_point_at(shape, near, scale=None, bounds=None, rounded=False, influence=None)`

The point of an edge of `shape` nearest `near`: a point on the line where two faces meet at a sharp angle -- any edge, straight or curved (the rim of a
cylinder too).  Where a corner is nearer, the corner.  With rounded=True an edge that a rounding replaced is found too: the point of the line where the planes of the
flat faces on both sides meet.  Raises ValueError when there is no edge round `near`.  See the module and `corner_at`.

### `face_center(face)`

The middle of a face -- a selection of surface (`select_surface`): the mean of the places it was sampled at, which are spread evenly over it, so it is the centre of its
area (for a circular face the centre of the circle).  `center(face)` is the middle of the box round it, which is the same for a disc and not for an L

### `features_near(values, near, scale, size)`

What the right-click menu offers at a place: {'corner': point or None, 'edge': {'point', 'straight': (p0, p1) or None} or None} -- the nearest corner and the nearest
edge.  A place with neither (a face, a smooth surface: nearly every click) costs a look round the place and no more

### `find_feature(values, near, scale, size, kind=None)`

The corner or edge nearest `near` that can be found (see the module), or None: {'kind': 'corner' | 'edge', 'point': (x, y, z), 'direction': unit vector or None,
'straight': the two ends of the edge when it is a straight line, or None}.  `values(points)` is the field at many points (a list of floats), `scale` how many mm
about thirty pixels span there (the neighbourhood that is looked in), `size` the size of the body.  `kind` ('corner' / 'edge') asks for that kind only

## Structural analysis and topology optimization

Static finite element analysis (linear elasticity) of FielDes shapes.

    from fieldes import *

    bracket = ...                                        # any Shape, in mm
    base = fixed(box((0, 0, 0), (5, 40, 20)))            # clamp the left end
    push = force(box((95, 0, 0), (100, 40, 20)), (0, 0, -200))   # 200 N down
    result = static_analysis(bracket, supports=[base], loads=[push], material=aluminium, element_size=1.0)
    base, push                                           # each is drawn on the part the analysis was given: held (blue), pushed (red)
    result                                   # the stress on the deformed part (FielDes: the result card)
    stiffer = bracket - 0.002 * result.von_mises   # results are fields like any other

Supports and loads are regions -- ordinary shapes: a support fixes the part
wherever it lies inside the region, a load spreads a total force over the
part's surface inside the region.  Units: mm, N and MPa (E in MPa), so
stresses come out in MPa and displacements in mm.

The elements are the `element` option of static_analysis, modal_analysis and
topology_optimization (thermal_analysis has it too):
    'tet'        linear tetrahedra that follow the part's surface (the
                 default): an unstructured mesh made for the part by isosurface
                 stuffing on the shape's own field -- its boundary vertices lie
                 on the surface, sharp edges are followed to a fraction of an
                 element -- with `element_size` the edge length, a stiffness
                 matrix assembled from each element's own geometry and a
                 preconditioned conjugate-gradient solve.  The stress in a
                 tetrahedron is constant in it; linear tetrahedra are stiff in
                 bending, so refine (a cantilever four elements thick reads 96 %
                 of beam theory, eight thick 99 %)
    'tet10'      the same tetrahedra with a node in the middle of every edge: 10-node
                 (quadratic) elements, a displacement that is a quadratic in each
                 element and a stress that is linear in it, with the edges on the
                 part's surface curved to follow it.  For static_analysis and
                 modal_analysis.  They do not bend stiff, and for a smooth part about
                 three times the element size of 'tet' reads the same stress -- but
                 on the same mesh they cost about 15 times the memory and the time,
                 so use them where the stress matters (a notch, a hole, a fillet)
                 and the part's thin features do not set the element size
    'hex'        a voxel grid of cubes, `element_size` on a side, each one
                 trilinear hexahedron with incompatible modes: bends without
                 spurious shear; its stiffness follows how much of the cube lies
                 inside the part, so the surface is a staircase
    'hex_basic'  the same grid with the plain trilinear hexahedron (shear-locks
                 in bending)
Topology optimization designs one density per element, on the tetrahedra or on
the grid; thermal topology optimization is on the grid only.  Smaller elements
are more accurate and slower; results are cached in memory, so re-running an
unchanged analysis (e.g. while editing other parts of a FielDes script) is
instant.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `FeaError`

The analysis could not be set up or solved (the message says why)

### `FieldResult`

What every result is -- of an analysis or of an optimisation: a FIELD, like any shape.  Wherever a shape goes (`offset(result, 1)`,
`difference(part, result)`, `result * 2`, the part of the next analysis) the result goes, and it is its own field there:

    an optimisation       the final design (the shape it made)
    static_analysis       the von Mises stress          modal_analysis      the first mode's displacement
    thermal_analysis      the temperature               fluid_analysis      the speed

and `result.fields` lists every field it calculated -- `{name: field}` -- the others being attributes by those names (`result.pressure`).
Whatever the result is made visible as -- the optimisation's iterations, the flow's streamlines -- is how it is shown when it is
stated on its own: the field it is passed as does not change that.

### `Material`

An isotropic linear-elastic material: Young's modulus E (MPa),
Poisson's ratio nu, density (t/mm^3, for gravity loads and the mass of a modal analysis),
thermal conductivity (W / (mm K), for thermal_analysis), thermal expansion
coefficient (1/K, for thermal_expansion).

E, density, conductivity and expansion can each be a FIELD instead of a number -- a Shape, its value at every
point of the part is the property there: `Material('graded', ramp(z_field(), (0, 50), (70e3, 3e3)), 0.33, 2.7e-9)`
is stiff at the bottom and soft at the top; a lattice's density field can drive it as well.  (Fields in a material
and in loads work with the tetrahedral elements, the default.)  nu and yield_strength are numbers.

### `ModalResult`

The result of modal_analysis(): .frequencies (Hz, lowest first),
.modes (a Mode each: its shape as fields), .seconds.  Stated on its
own it shows the first mode (result.modes[1] the second, ...).

### `Mode`

One natural mode of vibration: .frequency (Hz), and its shape as
fields (Shapes usable in any expression): displacement, ux, uy, uz
-- scaled so the largest movement is 1 (a shape, not an amplitude).
Stated on its own, FielDes shows the part coloured by it and deformed by it, and the
result card plays the vibration (the deformation through a cycle).

### `Result`

The solved analysis.  Fields (Shapes whose value is the result at
each point, usable in any expression): von_mises, displacement,
ux, uy, uz, sxx .. szx, max_principal, min_principal,
strain_energy.  Summary: max_von_mises, max_displacement,
safety_factor (if the material has a yield strength), elements,
iterations, seconds, reaction (support force), ...

#### `Result.deformed(self, scale=None, field_shape=None)`

The part moved by the computed displacements, magnified by
`scale` (default: so the largest movement is 5% of the part's
size).  Pass field_shape to deform another shape the same way.

#### `Result.element_range(self, field='von_mises')`

(min, max) of a field over the elements, each with its own value (a tetrahedron's
stress is constant in it; a hexahedron's is taken at its centre).  A stress peak
is higher here than in range(), which averages the elements at each node.

#### `Result.element_text(self)`

What the analysis is made of, and how many of them

#### `Result.range(self, field='von_mises')`

(min, max) of a field over the part: of the smooth field (the nodal values;
stresses are averages of the elements at each node)

### `TetMode`

One natural mode of a part on a body-fitted tetrahedral mesh (see Mode)

### `TetResult`

A static analysis on a body-fitted tetrahedral mesh: its elements are tetrahedra
that follow the part's surface (not a voxelization), the stress in each is its
own (constant in it; with element='tet10' linear in it), and the fields are read
anywhere in the mesh.  Everything of Result: fields as shapes, .range(),
.element_range(), ...  With the 10-node element the fields are on its nodes, and
are drawn on the element cut into eight linear tetrahedra.

#### `TetResult.element_text(self)`

What the analysis is made of, and how many of them

### `TopologyResult`

The result of topology_optimization():
.density     a field, 0 (no material) .. 1 (solid), in the part
.shape()     the optimized part, keeping the volume fraction asked
             for (shape(threshold=0.5): where density > 0.5)
.history     the objective at each iteration (what was made best: the compliance, the mass ...)
.densities   the density field after each iteration (tetrahedral
             optimizations)
.volume_fraction, .iterations, .seconds
.pieces      how many separate pieces the optimized part is in (tetrahedral optimizations; more than
             one is warned about when the result is made: try a higher volume_fraction)
.symmetry    the planes the design was kept symmetric about: {'z': 0.0} (see the symmetry argument), {} if none
.analysis    the analysis of the optimized part, made as the one the optimisation was given was (a list with several): its numbers
             -- `best.max_von_mises`, `best.mass` -- are the true ones; .values what was asked, as that part has it; .feasible
.verify()    the analyses of the optimized part again (a list, one per analysis, when there are several)
Stated on its own, FielDes shows the optimized part coloured by the
density, and the result card steps through the iterations: the part
as it was after each one (slider, play / pause).

#### `TopologyResult.keep_threshold(self)`

The density above which the part has the volume the design has.  (The density field is partly grey -- between 0 and 1 -- so cutting at
0.5 kept only 9-15 % of a block asked to keep 25 %: a much weaker part than the optimised one.)  The volume above a level is counted in
the kernel, exactly: the field is linear in each tetrahedron.  A grid of samples cannot see a wall thinner than its spacing, and cut a
thin bracket that was to keep half of its material at 38 %

#### `TopologyResult.shape(self, threshold=None)`

The optimized part: the original part where the density is above
`threshold` (a smooth surface through the density field).  By
default the threshold that keeps the volume fraction asked for
(see keep_threshold); 0.5 cuts the density field in the middle.

#### `TopologyResult.table(self)`

What was asked, at the first iteration, at the last, and as the optimized part has it, as text

#### `TopologyResult.verify(self, threshold=None, element_size=None)`

The analyses of the optimized part again, each made as the one the optimisation was given was (a different element size if asked) --
a list, one per analysis, when there are several

### `colored(shape, field, range=None, label=None, colormap='turbo')`

`shape`, displayed in FielDes coloured by `field` (another Shape,
e.g. an FEA result) with a colour map; hovering the model shows the
value.  range=(lo, hi) fixes the colour scale (default: the field's
range on the part).  Anywhere else it is simply `shape`.

### `fixed(*regions, region=None, body=None, displacement=(0.0, 0.0, 0.0), **removed)`

A support: the part is held wherever it lies inside `region` (a Shape: a body, a field, a selected surface).  Any number
of regions can be given, one argument after the other -- fixed(a, b) holds the part where either is.

region        where the part is held: a body, a field or a selected surface (the same as the first argument)
body          the body the support is on, when the regions are bodies that choose WHERE on it: `fixed(plate, body=bracket)` holds the bracket on its surface where it
              meets the plate (what `surface_from_bodies(bracket, plate)` is, made by the call)
displacement  (dx, dy, dz) in mm, where the part is held in each direction -- also written after the regions, fixed(region, (0, 0, inf)).
              0 (the default, (0, 0, 0)) lets the part not move there: it is fixed.  `inf` (`math.inf`, `float('inf')`: `from fieldes import *` has it as
              `inf`) leaves that direction free, so `displacement=(0, 0, inf)` is a support the part slides along z on.  Any other number is a
              displacement the support imposes: `(0, 0, -0.5)` pushes the part 0.5 mm down where it is held (a static analysis on tetrahedra
              solves for the rest of it; a modal analysis holds it still, a topology optimization refuses it).

### `force(*args, region=None, body=None, vector=None, profile=None)`

A load: the total force (fx, fy, fz) in N, spread evenly over the
part's surface inside `region` (a Shape).  force(region, (0, 0, -100)) and
force(region, 0, 0, -100) and force(region=region, vector=(0, 0, -100)) are the same.  Any number of regions can be given, one
argument after the other, before the force: force(a, b, (0, 0, -100)) loads the part where either is.

region   where the part is loaded: a body, a field or a selected surface (the same as the first argument)
body     the body the load is on, when the regions are bodies that choose WHERE on it: `force(pad, (0, 0, -100), body=bracket)` loads the bracket's surface where it meets the pad
vector   the total force (fx, fy, fz) in newtons: its direction and its size
profile  a field: the total is spread over the surface in proportion to it (not negative) instead of evenly --
         a pressure that is not the same everywhere, e.g. `profile=ramp(x_field(), (0, 80), (0.2, 1.0))` loads
         the end of a beam five times harder at x = 80 than at x = 0, with the same total.  (Tetrahedral
         elements.)

### `gravity(g=(0.0, 0.0, -9810.0))`

The part's own weight: acceleration in mm/s^2 (default: 1 g down
along -Z), with the material's density

### `modal_analysis(shape, supports, material=Material('steel', E=200000 MPa, nu=0.3), modes=6, element_size=None, bounds=None, tolerance=1e-06, cache=True, element='tet')`

The natural frequencies and mode shapes of `shape` (a Shape, in
mm), held by `supports` (fixed(...) items, one or a list) -- how it
vibrates.  No loads are needed; the
material's E and density are used.

modes        how many (the lowest first)
element_size mm (default: about 40 elements along the longest side;
             20 with element='tet10'; larger for a solid part that would have
             more than half a million tetrahedra (125 000 with 'tet10'))
element      'tet' (default), 'tet10' (10-node elements: the frequencies of a
             part that bends are far better on the same mesh), 'hex' or
             'hex_basic': see the module documentation

Returns a ModalResult: .frequencies (Hz), .modes[i] (the shape as
fields -- displacement, ux, uy, uz).  The shapes are fields like any
other: e.g. stiffen the part where the first mode moves most.  An
unchanged problem is cached.

### `static_analysis(shape, supports, loads, material=Material('steel', E=200000 MPa, nu=0.3), element_size=None, bounds=None, tolerance=1e-06, cache=True, element='tet')`

Linear static analysis of `shape` (a Shape, in mm).

supports   what holds the part: fixed(...) items (at least one), one or a list
loads      what pushes it: force(...) / gravity(...) / thermal_expansion(...) items, one or a list ([] for none).
           The conditions have no body in them (this function is given it); each is a model of its own and is drawn on the part
material   a Material (default steel); E in MPa
element_size   the element edge in mm (default: the part's longest
           side over 60; over 30 with element='tet10' -- or larger, for a
           solid part that would have more than a million tetrahedra
           (250 000 with 'tet10'): the size at which it has that many.
           A size that is given is used as it is)
element    'tet' (default: tetrahedra that follow the part's surface),
           'tet10' (the same with 10 nodes: quadratic elements, about
           three times the element size for the same accuracy),
           'hex' or 'hex_basic' (a voxel grid): see the module
           documentation
bounds     region to analyse, ((x0, y0, z0), (x1, y1, z1)); found
           automatically if not given

Returns a Result (see its fields).  Raises FeaError if the problem
can't be solved as given (no support touching the part, supports
that don't hold it in place, ...).

### `thermal_expansion(temperature, reference=20.0)`

A load from heat: the part at `temperature` -- a field (e.g. a
thermal analysis' .temperature) or a number -- expands by the
material's expansion coefficient per degree above `reference`
(where it is stress-free).  Held parts are stressed; free ones grow.
Static analysis only.

### `topology_optimization(analysis, constraints=(), iterations=None, filter_radius=None, keep=None, avoid=None, grow=False, grow_limit=None, extrude=None, penalty=3.0, sharpness=16.0, symmetry='auto', tolerance=1e-05, cache=True)`

Topology optimization: the layout of material, within the part of `analysis`, that is best for what is asked of it.

    stress = static_analysis(part, [fixed(base)], [force(lug, (0, 0, -300))], steel)
    best = topology_optimization(stress, constraints=[minimize('compliance'), 'volume_fraction <= 0.3'])
    lighter = topology_optimization(stress, constraints=[minimize('mass'), 'max_von_mises <= 120', 'max_displacement <= 0.4'])

analysis      the result of an analysis of this script -- static_analysis, modal_analysis, thermal_analysis -- or a list of them: the part is the design
              space, and its supports, loads and material are the problem it is designed for.  A list is several load cases (the
              same part under other loads and supports), or a stress case and a vibration case: [stress, vibration].  The result of a
              fluid_analysis is a BODY IN A FLOW: the body is the start of the design, the fluid domain its space, and the numbers
              are drag_force and lift_force (the force of the flow on the body along it and across it, N) and volume_fraction (of
              the body's own volume) -- `topology_optimization(wing, constraints=[maximize('lift_force / drag_force'),
              'volume_fraction <= 0.4'])`.  The result of a thermal_analysis (tetrahedral elements) is a HEAT CONDUCTION:
              the part is the design space, the held temperatures, heat inputs and convecting surfaces are the problem, and the numbers
              are max_temperature, min_temperature, max_heat_flux, input_temperature (the temperature the heat comes in at: the
              heat-weighted mean of the heat inputs), temperature(x, y, z), volume, mass and volume_fraction --
              `topology_optimization(sink, constraints=[minimize('max_temperature'), 'volume_fraction <= 0.3'])`
constraints   what the design is for, a list.  Comparisons it keeps: 'volume_fraction <= 0.3', 'max_von_mises <= 90', 'frequencies[0] >= 80',
              an exact value 'frequencies[0] == 100' (kept to within a thousandth), a range '80 <= frequencies[0] <= 120' -- or a
              function of the result that is 0 or less when kept.  And at most one goal: minimize(x) makes x as small as it can be,
              maximize(x) as large -- x a number of the analyses' results ('compliance', 'mass', 'max_von_mises', 'frequencies[0]'
              ...), a formula of them ('mass + 0.01 * compliance', 'lift_force / drag_force') or a function of the result
              (`lambda r: r.max_von_mises / r.mass`).  Without a goal the design only has to keep the comparisons.  The numbers of
              the design that the formulas can read: volume_fraction (the share of the part's volume it uses), volume, mass, and
              from a static analysis compliance, max_von_mises, max_displacement, safety_factor and ux(x, y, z), uy, uz, displacement
              (the displacement at a place), from a modal analysis frequencies[i].  With several analyses a number that more than one
              offers is, in a goal, the SUM of theirs, and a comparison on it is kept in each of them
keep:         regions (Shapes, or a list) that must stay solid -- e.g. bolt bosses, mounting faces; the material around supports
              and loads always stays
avoid:        regions that must stay empty
grow:         False (default: the design stays inside the part) or True: the design is NOT limited to the part -- it starts as the part
              and puts material anywhere, and takes it away anywhere, where that makes it best.  volume_fraction is then of the
              part's own volume (1 spends the same material, 1.2 spends 20 % more).  The design space follows the design: the
              moment material reaches the edge of the space the run stops, the space is made again round the material, and the
              same run goes on in it -- the same iteration, sharpening and step (so the space costs what the material costs, not
              what a shell of some thickness would, and the design is never held against a wall while it runs); nothing is added
              behind the face of the part that a support or a load acts on -- over the face, and past the plane of the face a few
              elements round its outline: what holds the part, or pushes on it, is there -- and nothing else is forbidden: the supports
              hold that face only, the load case is the one that was given, and on the part's side of the plane the design is as the
              stresses ask, also round the plate.  A place that has no flat face (a bolt hole that carries a load) is kept clear as
              the place itself.
              Only the iterations stop the growth: if they run out with the design
              still at the edge of its space, the output says so and the part is cut there
grow_limit:   mm, with grow=True: the most the design may reach out from the part.  A restriction on top of grow, never the base
              of it: left out, nothing limits how far the design grows
extrude:      'x', 'y' or 'z' -- the design is the same all along that axis (outside the keep / avoid regions): a profile to
              extrude, or to cut right through from one side
iterations:   at most this many design updates (default 100; a body in a flow 80: it is sharpened a little at every iteration): it stops sooner when the design has settled (from the 15th on,
              once the sharpness has been reached, when the design hardly changes or the objective stays flat for 5 iterations
              -- and every constraint is kept).  It also sets the pace of the sharpening and the size of the steps
filter_radius: the smallest member size scale, mm (default 1.5 elements)
penalty:      the power the density is raised to in the stiffness (default 3)
sharpness:    how crisp the design is (default 16): the density is pushed towards 0 and 1 more and more as the iterations go
              on, up to this much.  1 leaves the density as the filter makes it (soft edges)
symmetry:     'auto' (default), None, 'x' / 'y' / 'z' or several ('xz'), or {'z': 0.0} with the plane's position.  A design is
              kept symmetric about a plane when the part is, and so are its supports, its loads and its keep / avoid regions

The numbers are the kernel's own: it calculates every measure with its derivative to the density of every element (the
compliance and the frequencies from the solution, the displacement and the stress with one more solve each), differentiates the
formulas, and moves the design with the method of moving asymptotes on the objective and every constraint.  The stress is the
element's von Mises stress, relaxed at low density, and its peak is the p-norm of them brought to the peak.

Returns a TopologyResult: .density (a field), .shape() (the optimized part), .analysis (the analysis of that part: its numbers
-- `best.max_von_mises` -- are the true ones; a list with several), .values (what was asked, as the optimized part has it),
.feasible, .history (the objective per iteration), .verify().  An unchanged problem is cached, so re-running a script is instant.

## Seeing the boundary conditions

Boundary conditions you can see.

    from fieldes import *

    part = ...
    base = fixed(base_face)                                       # held here
    push = force(lug_face, (0, -2000, 0))                         # pushed here
    result = static_analysis(part, supports=[base], loads=[push, gravity()], material=aluminium, element_size=4)
    base                                                          # displayed: the held surface, with pads
    push                                                          # displayed: the loaded surface, with arrows

Every boundary condition -- a support, a force, a temperature, an inlet ... -- is a model of its own: a row of the model tree with an eye, and
it is drawn the way structural and flow analysis programs draw it, on the body of the simulation it was given to (the body itself is drawn by
its own row: show it with its eye to see both):

    * the surface it acts on is tinted, in a colour that says what it is (blue: fixed support, cyan: sliding support, red: force, orange:
      gravity, yellow: heat in, green: inlet, violet: outlet, grey: wall, light blue: slip, ...)
    * a force, an inlet, an outlet or a heat input is an ARRAY of identical arrows over the surface, each touching it with its tip when it pushes
      in and with its tail when it points out, with what it says written beside it (the total force, the speed, the pressure, the power)
    * a support, a wall, a slip, a temperature or a convection is an array of flat pads lying on the surface, with its name beside it
    * gravity is one arrow beside the part along the acceleration, with its value

A simulation has the toggle `boundary conditions` in the model tree: it shows or hides all of its conditions at once.

The arrows, pads and texts are not part of the meshed model: the viewport draws them over it, with a size that follows
the zoom, so they are never cut off by the render region or made ragged by the render's resolution.

The tint is the body's own surface, coloured and drawn alone (a hair towards the eye, so that it wins over the body where both are shown); a
place of the body counts as in a region when it is within about 1 % of the body's size of it.  A condition that no simulation has been given
yet is drawn on a body all the same -- the one its surface was picked on (select_surface, surface_from_bodies), else the biggest solid of the
script that its region reaches -- so it looks the same while you are setting a simulation up; only when the script has no body at all is it
drawn as the region itself (the place it acts).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `describe(item)`

One line that says what a condition is (its text beside it in the picture)

### `display_condition(item)`

What a condition shows for itself (the way its model is displayed): on the body of the simulation it was given to, or on the body its
surface was picked on, drawn once for each body

## Thermal analysis and thermal topology optimization

Steady-state thermal analysis (heat conduction) of FielDes shapes.

    from fieldes import *

    part = ...                                          # any Shape, in mm
    result = thermal_analysis(
        part,
        fixed_temperatures=[fixed_temperature(base_region, 20)],       # held at 20 degrees C
        heat_inputs=[heat_input(chip_region, 5.0)],                    # 5 W into the part here
        heat_generations=[],                                           # (none)
        convections=[convection(fins_region, 25e-6, ambient=20)],      # air cooling
        material=aluminium, element_size=1.0)
    result                               # the part coloured by temperature (FielDes)
    thicker = part - 0.2 * result.heat_flux   # results are fields like any other

    # the material layout (30 % of the part) that keeps the part coolest, for the conditions of the analysis
    design = topology_optimization(result, constraints=[minimize('max_temperature'), 'volume_fraction <= 0.3'])
    design.shape()

Every kind of condition is an input of its own, needed -- written with a placeholder -- and takes one item or a list ([] says there is none).
Each condition is a model of its own, with an eye: it is drawn on the part the analysis was given.

Boundary conditions are regions -- ordinary shapes, as for static_analysis:
  fixed_temperature(region, T)   the part is held at T inside the region
  heat_input(region, watts)      a total power spread over the part's surface
                                 inside the region
  heat_generation(region, watts) a total power generated in the part's volume
                                 inside the region (e.g. a resistive heater,
                                 electronics potted in the part)
  convection(region, h, ambient) the part's exposed surface inside the region
                                 exchanges heat with an ambient temperature;
                                 h in W / (mm^2 K): still air ~5e-6 - 25e-6,
                                 forced air ~25e-6 - 250e-6, water ~500e-6 -
                                 10000e-6 (1 W / (m^2 K) = 1e-6 W / (mm^2 K))
At least one fixed temperature or convection is needed (else the temperature
isn't determined).  Units: mm, W, degrees C (or K); the material's
conductivity in W / (mm K) (aluminium ~0.167 = 167 W / (m K)).

Uses the same elements as static_analysis: tetrahedra that follow the part's
surface (element='tet', the default: a temperature is linear in each and the
heat flux constant in it) or, with element='hex', a voxel grid of cubes (partly
filled at the surface), and a preconditioned conjugate-gradient solve.  Topology
optimization of a thermal analysis (topology_optimization) designs on the tetrahedral mesh.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `ThermalResult`

The solved thermal analysis.  Fields (Shapes whose value is the
result at each point, usable in any expression): temperature,
heat_flux (magnitude, W/mm^2), qx, qy, qz.  Summary: max_temperature,
min_temperature, max_heat_flux, heat_in, heat_out (W: through the
fixed temperatures and by convection -- they balance),
input_temperature (tetrahedral elements: the temperature the heat comes in at, weighted by how much comes in at each place),
volume (mm^3, of the mesh) and mass (when the material has a density), elements, iterations, seconds.

#### `ThermalResult.range(self, field='temperature')`

(min, max) of a field over the part

### `ThermalTopologyResult`

The result of topology_optimization() for a thermal analysis (see there): the part made best for what is asked of its temperature.
.density      a field, 0 (no material) .. 1 (solid), in the part
.shape()      the optimized part, keeping the volume the design has (shape(threshold=0.5): where density > 0.5)
.analysis     the thermal analysis of the optimized part, made as the one the optimisation was given was: its numbers -- `best.max_temperature`
              -- are the true ones; .values what was asked, as that part has it; .feasible, .history (the objective per iteration), .table()
.verify()     the thermal analysis of the optimized part again
Stated on its own, FielDes shows the optimized part coloured by the density, and the result card steps through the iterations.

#### `ThermalTopologyResult.keep_threshold(self)`

The density above which the part has the volume the design has, counted in the kernel (see TopologyResult.keep_threshold)

### `convection(*args, region=None, body=None, coefficient=None, ambient=None)`

The part's exposed surface inside `region` (any number of regions, one argument after the other, then the coefficient and the
ambient temperature) exchanges heat with an ambient temperature.

region       where the part exchanges heat: a body, a field or a selected surface (the same as the first argument)
body         the body that exchanges heat, when the regions are bodies that choose WHERE on it: its surface where they are
coefficient  h in W / (mm^2 K): still air ~5e-6 - 25e-6, forced air ~25e-6 - 250e-6, water ~500e-6 - 1e-2.  A number, or a field --
             its value at each point of the surface (tetrahedral elements)
ambient      the ambient temperature (degrees C; 20 when it is not given): a number, or a field

### `fixed_temperature(*args, region=None, body=None, value=None)`

The part held at temperature `value` wherever it lies inside
`region` (a Shape; any number of regions can be given, one argument after the other, then the value:
fixed_temperature(a, b, 20)).

region  where the part is held: a body, a field or a selected surface (the same as the first argument)
body    the body it is held on, when the regions are bodies that choose WHERE on it: `fixed_temperature(plate, 20, body=part)` holds the part's surface where it meets the plate
value   the temperature (degrees C): a number, or a field -- the temperature at each point (tetrahedral elements)

### `heat_generation(*args, region=None, watts=None, profile=None)`

A total power (W) generated inside the part, spread evenly through
its volume inside `region` (a Shape; any number of regions, one argument after the other, then the power) -- e.g. a resistive heater, or
electronics potted in the part.

region   where the heat is generated: a body or a field (the same as the first argument)
watts    the total power in watts
profile  a field -- spread in proportion to it instead of evenly

### `heat_input(*args, region=None, body=None, watts=None, profile=None)`

A total power (W) put into the part, spread evenly over its surface
inside `region` (a Shape; any number of regions, one argument after the other, then the power); negative takes heat out.

region   where the heat goes in: a body, a field or a selected surface (the same as the first argument)
body     the body the heat goes into, when the regions are bodies that choose WHERE on it: `heat_input(chip, 8, body=part)` heats the part's surface where it meets the chip
watts    the total power in watts (negative takes heat out)
profile  a field -- the power is spread in proportion to it (not negative) instead of evenly

### `thermal_analysis(shape, fixed_temperatures, heat_inputs, heat_generations, convections, material=Material('aluminium', E=69000 MPa, nu=0.33), element_size=None, bounds=None, conductivity=None, tolerance=1e-07, element='tet', cache=True)`

Steady-state heat conduction in `shape` with the given boundary
conditions, one input for each kind (each one item or a list, [] for none):
fixed_temperatures  fixed_temperature(...) items (at least one of these or of the convections is needed)
heat_inputs         heat_input(...) items
heat_generations    heat_generation(...) items
convections         convection(...) items
(see the module's description).  Each condition is a model of its own, drawn on `shape`.

Everything else is as in the analysis below.

### `topology_optimization_thermal(analysis, constraints, iterations=None, filter_radius=None, keep=None, avoid=None, grow=False, grow_limit=None, extrude=None, penalty=3.0, sharpness=16.0, symmetry='auto', tolerance=1e-05, cache=True)`

topology_optimization() of a thermal analysis: the layout of material, within the part, that is best for what is asked of its temperature.
The held temperatures, the heat inputs and generations and the convection of the analysis are the problem (the elements that hold a temperature
or bring heat in stay solid, and material that nothing joins to a held temperature or a convecting surface is taken out); the conductivity of
an element is the material's times a power of its density (penalty).  The numbers it can be asked for: max_temperature, min_temperature (the
peaks of the temperature, as p-norms), max_heat_flux, input_temperature (the heat-weighted mean temperature of the heat inputs: the usual
goal of a heat sink), temperature(x, y, z), volume, mass and volume_fraction; their sensitivities come from one adjoint solve each

## Fluid flow analysis

Fluid flow analysis of FielDes shapes: incompressible laminar flow, steady or in time, around a body.

    from fieldes import *

    duct = box((0, 0, 0), (80, 40, 4))             # the DOMAIN: the fluid, and the place the body is in
    post = cylinder(5, 6, (24, 20, -1))                # the BODY the flow goes around
    result = fluid_analysis(
        post,
        duct,
        inlets=[inlet(box((-1, -1, -1), (0.01, 41, 5)), speed=4)],                             # mm/s, in
        outlets=[outlet(box((79.99, -1, -1), (81, 41, 5)), pressure=0)],                       # MPa
        boundaries=[],                                                                               # (none: the rest of the surface is wall)
        fluid=water, element_size=1.0)
    result                                               # the flow (FielDes: the fluid coloured by the speed,
                                                         # streamlines with moving particles; the result card)
    print(result.wall_force[0], 'N')                     # the force on the walls, the body's: its drag
    thicker = part + 0.02 * result.pressure              # results are fields like any other

It takes the body, the domain, the three kinds of boundary conditions and the fluid -- what topology_optimization designs the body for -- and
solves the flow once, around the body as it is (the way static_analysis is to topology_optimization).  The domain is a shape, the fluid
where its field is negative, with the body's place in it: the fluid is the domain with the body cut out (difference(domain, body)).
The boundary conditions are regions (shapes), on the surface of the fluid.  Each kind is an input of its own -- inlets, outlets, boundaries (walls and slips) -- that is needed (the call is written
with a placeholder for each) and takes one item or a list ([] says there is none); each condition is a model of its own, with an eye, drawn on
the fluid domain:
  inlet(region, velocity= | speed= | flow_rate=, profile='uniform' | 'developed')
                            the fluid comes in: a velocity vector (mm/s), or a mean speed along the
                            inward normal, or a flow rate (mm^3/s).  The speed is the MEAN over the
                            inlet, matched exactly on the mesh.  'developed': the fully developed
                            profile of that cross-section (parabolic in a pipe); 'uniform': a plug
                            with the no-slip rim
  outlet(region, pressure)  the fluid leaves at that pressure (MPa); the "do-nothing" condition,
                            so put an outlet where the flow leaves parallel to the walls
  wall(region, velocity)    a moving wall (no-slip at that velocity); every surface that is in no
                            region is a wall at rest
  slip(region)              a symmetry plane or a frictionless wall: no flow through it
The fluid: Fluid(name, density, viscosity) in t/mm^3 and MPa s (= N s / mm^2); water, air and oil are
predefined.  Gravity (mm/s^2) is a body force.

The equations are the steady Navier-Stokes equations (rho (u.grad) u - mu laplace u + grad p = rho g,
div u = 0) on the same tetrahedra that follow the fluid's surface as the structural analyses use,
linear in the velocity and the pressure and stabilised (SUPG / PSPG); the nonlinearity is solved by
Picard then Newton iterations from the Stokes solution; stokes=True leaves the convection out
(creeping flow: Reynolds numbers well below 1).  The results are fields: speed, vx, vy, vz,
pressure, total_pressure, shear_rate, vorticity; and numbers: the flows, the pressure drop, the
force on the walls, the dissipation, the Reynolds number.

Limits, plainly: laminar and steady only -- no turbulence model, nothing time-dependent; a flow
whose Reynolds number is beyond the laminar range (about 2000 in a pipe) is not described by this;
no boundary-layer (inflation) elements: the mesh must be fine enough across the passages (the
result says how many elements lie across); the pressure is linear in each element, so its peak at
a corner is smeared over an element; no free surfaces, no heat transfer with the flow (yet).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `FlowTopologyResult`

The result of topology_optimization() for a body in a flow (the result of fluid_analysis): the body made best in the flow.
.level          a field (mm, positive inside the body, its boundary the zero level: a smooth
                level set on the flow's mesh); .levels the same after each iteration (the first
                the body as given, the last the final body)
.shape(iteration=None)  the optimised body, or the body after an iteration
.fluid_shape(iteration=None)  the fluid around it (the domain with the body taken out)
.drag, .lift    N, per iteration, as the optimiser's model sees them (the body a friction in the
                flow), the last the final body's; .flow_direction, .lift_direction
.flow           the REAL flow around the final body (a fluid_analysis of fluid_shape() with the
                same conditions, the body a wall at rest): fields, numbers, streamlines();
                .real_drag, .real_lift its force on the walls along the two directions: the MEAN over several places of the mesh lattice (a mesh meets a body at
                some place and the force depends on it, by 5 to 25 % for a body a few elements thick), .real_drag_sd the standard deviation of one place,
                .real_drags the forces of the places, `best.drag_force` the mean as well
.model_flow     the optimiser's own flow (the body as a friction), .model_flow.steps[k] the flow
                around the body of iteration k
.body, .domain, .volume (mm^3 of the final body), .iterations (accepted), .seconds,
.steps_back (steps undone because they raised the objective), .stopped (why the run ended)
.values         each thing the constraints asked, as the final body has it in the real flow; .feasible, .goal, .table(), .history (the
                objective per iteration, as the optimiser's model had it), .volume_fraction (the share of the body's own volume the design
                uses), .body_volume (mm^3 of the body it started as), .pieces (in how many pieces the body is); the numbers of the real flow -- `best.drag_force`, `best.pressure_drop`,
                `best.speed` -- are the result's own
Stated on its own, FielDes shows the body in the flow: the fluid coloured by the speed with
streamlines and particles, the body solid; the result card steps through the iterations (the
body as it was after each one, and the flow around it).

#### `FlowTopologyResult.fluid_shape(self, iteration=None)`

The fluid around the body: the domain with the body taken out

#### `FlowTopologyResult.shape(self, iteration=None)`

The body: where the level set is positive (its boundary the zero level, a fraction of an element
exact); iteration=k the body after iteration k (0 the body as given)

### `Fluid`

A Newtonian fluid: density (t/mm^3) and dynamic viscosity (MPa s = N s / mm^2).
Water: 1.0e-9 t/mm^3, 1.0e-9 MPa s (kinematic viscosity 1 mm^2/s).

### `FluidResult`

The solved flow.  Fields (Shapes whose value is the result at each point, usable in any
expression): speed, vx, vy, vz (mm/s), pressure, total_pressure (MPa), shear_rate,
vorticity (1/s).  Numbers: inlet_flow, outlet_flow (mm^3/s; inlet_flows / outlet_flows per
item), wall_flow (net flow in through the walls: a moving wall, the rounded rim of an inlet),
mass_imbalance (|in + wall_flow - out| / in), pressure_drop (MPa, mean inlet minus mean outlet), max_speed,
wall_force (N, on all the walls; wall_forces per wall(...) item), dissipation (W),
reynolds (rho U D_h / mu of the inlet), cell_reynolds, hydraulic_diameter,
elements_across (how many elements lie across the passages), elements, nodes, iterations,
residual, converged, seconds, warning.  .steps (a FluidStep each): the flow after every solver
iteration of a steady solve (the Stokes start first, the converged flow last), or every stored
time of a flow in time (fluid_analysis(..., time=); .times); the result's own fields are the
last step's.  streamlines() gives the paths of particles through the flow.
Stated on its own, FielDes shows the fluid coloured by the speed, as the other analyses are shown,
with streamlines from the inlets and particles moving along them drawn over it; the result card
switches the field and steps through the iterations or the times (slider, play / pause).

#### `FluidResult.range(self, field='speed')`

(min, max) of a field over the fluid

#### `FluidResult.streamlines(self, seeds=None, count=40, max_time=None, max_points=4000, backward=False, step=None)`

The paths of particles carried by the flow: a list of lines, each a list of (x, y, z, speed, time)
points, from `seeds` (a list of (x, y, z); by default `count` points spread over the inlets), by
Runge-Kutta steps of half an element, for up to `max_time` seconds (by default the time it takes to
cross the fluid three times) or `max_points` points, until the particle leaves the fluid.  backward=True
follows the flow upstream; step= picks a step of a flow in time (the last one by default).

### `FluidStep`

One step of a flow: after a solver iteration of a steady solve (.iteration, 0 the Stokes start; .time
None), or at a stored time of a flow in time (.time in s; .iteration None).  The fields (speed, vx, vy,
vz, pressure, total_pressure, shear_rate, vorticity) and the numbers (inlet_flow, outlet_flow,
pressure_drop, max_speed, wall_force, dissipation, residual, converged) at that step; streamlines().
Stated on its own, FielDes shows it as it shows the result.

#### `FluidStep.range(self, field='speed')`

*(no description yet)*

#### `FluidStep.streamlines(self, seeds=None, count=40, max_time=None, max_points=4000, backward=False)`

The streamlines of the flow at this time (see FluidResult.streamlines)

### `fluid_analysis(body, domain, inlets, outlets, boundaries, fluid=Fluid('water', density=1e-09 t/mm^3, viscosity=1e-09 MPa s), element_size=None, bounds=None, gravity=None, stokes=False, tolerance=1e-05, relaxation=1.0, cache=True, time=None, store_every=1)`

Laminar flow of `fluid` around `body` (a Shape, the solid the flow goes around) in `domain` (a Shape whose inside is the
fluid, the body's place in it too: the fluid is the domain with the body cut out) with the boundary conditions, the same
inputs of topology_optimization for a body in a flow, one for each kind (each one item or a list, [] for none):
inlets    inlet(...) items -- where the fluid comes in (as many as the flow has)
outlets   outlet(...) items -- where it leaves (one is needed, or a moving wall)
boundaries  wall(...) items -- moving walls (every surface in no region is a wall at rest) -- and slip(...) items -- symmetry planes
            and frictionless walls: one or both, in one list
(see the module's description) -- the steady flow, or the flow in time (time=).  Each condition
is a model of its own, drawn on `domain`.  Every surface in no region is a wall at rest: the body's is, unless a wall(...) moves it.

element_size   mm (default: 40 elements along the longest side, or larger when that would make more than 300 000 tetrahedra); the passages should be
               four elements across or more
gravity        (gx, gy, gz) in mm/s^2, a body force on the fluid (none by default)
stokes         True: creeping flow, the convection left out (linear: one solve)
time           (duration, step) in seconds: the flow in TIME instead of the steady flow -- from
               the Stokes flow at t = 0 (an impulsive start) by steps of `step` seconds to
               `duration` (backward Euler; a step of about an element crossing, element_size /
               speed, keeps it accurate).  The result's .steps hold every store_every-th step
               (its time, fields and numbers) instead of the steady solve's iterations, the
               result's own fields are the last step's, and the result card steps through them.
               A wake that sheds vortices needs this: it has no steady state
tolerance      the relative residual of the discrete equations at which to stop
bounds         ((x0, y0, z0), (x1, y1, z1)) of the domain (found if not given)

Returns a FluidResult: the fields (speed, pressure, ...), the flows and the pressure drop,
the wall force (on every wall, the body's: its drag is the part along the flow), streamlines(), .body and .domain.  An unchanged problem is cached (as a static analysis is).
Raises FeaError when the problem cannot be solved as given (a region that touches no
surface, no outlet and no moving wall, a flow that does not converge).

### `inlet(*regions, region=None, body=None, velocity=None, speed=None, flow_rate=None, profile='uniform')`

The fluid comes in through the surface inside `region` (a Shape; any number of regions, one argument after the other).  `body=` is the body the surface is of (the domain),
when the regions are bodies that choose WHERE on it: its surface where they are.  One of:
velocity   a vector (mm/s): the direction, and the mean speed over the inlet
speed      a mean speed (mm/s) along the inlet's inward normal
flow_rate  a flow rate (mm^3/s) along the inward normal
profile    'uniform' (a plug, zero on the no-slip rim) or 'developed' (the fully developed
           profile of the inlet's cross-section: parabolic in a round pipe)
The speed or flow rate is matched exactly on the mesh (the flux through the inlet's triangles).

### `outlet(*regions, region=None, body=None, pressure=0.0)`

The fluid leaves through the surface inside `region` (any number of regions, one argument after the other) at `pressure` (MPa, 0 by default; the
pressure field is relative to it).  Put it where the flow leaves parallel to the walls.  `body=` is the body the surface is of, when the regions are bodies that choose
WHERE on it.

### `slip(*regions, region=None, body=None)`

A symmetry plane or frictionless wall inside `region` (any number of regions, one argument after the other): nothing flows through it,
the fluid slides along it.  `body=` is the body the wall is of, when the regions are bodies that choose WHERE on it.

### `symmetry(*regions, region=None, body=None)`

A symmetry plane or frictionless wall inside `region` (any number of regions, one argument after the other): nothing flows through it,
the fluid slides along it.  `body=` is the body the wall is of, when the regions are bodies that choose WHERE on it.

### `topology_optimization_flow(analysis, constraints=(), iterations=None, filter_radius=None, keep=None, avoid=None, grow=False, grow_limit=None, extrude=None, penalty=3.0, sharpness=16.0, symmetry='auto', tolerance=1e-05, cache=True)`

topology_optimization() for a body in a flow (see fea.py): `analysis` is the result of fluid_analysis(body, domain, inlets, outlets, boundaries).
The body is the design it starts from, the fluid domain is its space; the numbers the constraints can read are drag_force and lift_force (the
force of the flow on the body along the flow and across it, N), volume_fraction (of the body's own volume) and volume.  The flow of every
iteration is solved once, with one adjoint solve for each force the constraints read; the density of each tetrahedron is the friction the fluid
feels there (1 solid, 0 fluid)

### `wall(*regions, region=None, body=None, velocity=(0.0, 0.0, 0.0))`

A wall moving with `velocity` (mm/s; no-slip) inside `region` (any number of regions, one argument after the other).  Surfaces in no region are walls at rest, so
this is for moving walls, and for naming a wall whose force is wanted (wall_forces).  `body=` is the body the wall is of, when the regions are bodies that choose
WHERE on it.

## Meshing for the solver of another program

Meshing: a body as a mesh of elements, for the solver of another program.

    from fieldes import *

    part = ...                                                   # any body, in mm
    mesh = mesh_volume(part, element_size=2.0)                   # tetrahedra of about 2 mm that follow the surface
    mesh = mesh_volume(part, 2.0, element='hex')                 # hexahedra (each tetrahedron cut into four)
    mesh = mesh_volume(part, 2.0, element='tet10')               # 10-node tetrahedra, the edges on the surface curved
    mesh = mesh_volume(part, graded_size(part, wall_size=0.5, growth_rate=1.2, max_size=4.0))      # 0.5 mm at the surface, growing inwards
    mesh = mesh_volume(air, 2.0, boundary_layers=inflation_layers(body, first_height=0.05, growth_rate=1.2, layers=6),
                       zones={'inlet': inlet, 'outlet': outlet})                # a flow: prism layers on the wall, named patches
    surface = mesh_surface(part, 1.0, element='quad')            # the surface alone, triangles or quadrilaterals

    print(mesh)                                                  # what it has
    print(mesh.quality().report())                               # skewness, orthogonal quality, aspect ratio ... and what they mean for a solver
    mesh.export('part.msh')                                      # Gmsh;  .vtu .inp .bdf .unv .stl ...;  format='fluent' | 'openfoam'

The mesh is made by isosurface stuffing on the body's own field (a conforming tetrahedral mesh whose boundary vertices lie on the surface: see the kernel's
tetmesh.hpp), and what is asked of it afterwards is done to that mesh:

    element_size    a number (mm), or a field -- a body whose value at a point is the element size there, a function of a list of points that returns their sizes,
                    or graded_size(...).  The mesh starts at the largest size the field has and is refined where it asks for less: the longest edge of a tetrahedron
                    is bisected (with the tetrahedra round it, so the mesh stays conforming) until its edges are no longer than the size asked for there, and the new
                    vertices of the boundary are put on the surface.  Without one: about forty elements along the longest side, no more than about a hundred thousand tetrahedra.
    element         'tet' (linear tetrahedra), 'tet10' (10-node tetrahedra), 'hex' (hexahedra: every tetrahedron cut into four -- the elements have the shape the
                    tetrahedra give them, which is not a hex-dominant mesh's, but they are hexahedra for a solver that needs them)
    boundary_layers inflation_layers(...): prism layers on the surface of a solid, growing into the fluid (the first height, the growth rate, the number of layers)
    zones           the names of the patches of the boundary: {'inlet': region, 'outlet': region}: a boundary face lies in the first region that holds its middle,
                    and the rest are `default_zone` ('wall'); feature_angle splits that one into walls at the creases
    cell_zones      the same for the cells: {'solid': region}; the rest are 'fluid'
    optimize        the poor tetrahedra are mended after the meshing (on by default): the nodes of poor tetrahedra are moved (those on the surface along it, `smooth`
                    rounds in each pass), edges and faces swapped and interior nodes collapsed, wherever that makes the worst tetrahedron round them better

Every length is in mm, as everywhere in FielDes.  A mesh is a description of the elements: it has no material, no conditions and no solver settings.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `Mesh`

A mesh: nodes, the cells (solids: tetrahedra, 10-node tetrahedra, hexahedra, prisms) and the faces of its boundary (triangles, quadrilaterals), each of
them in a zone with a name.  Everything is in flat arrays:

    nodes                   array('d'): x y z of every node
    cells[kind]             array('i'): the nodes of every cell of that kind, in the order the kind has them (see meshing.py)  -- 'tet4' 'tet10' 'hex8' 'prism6'
    faces[kind]             the boundary: 'tri3' 'quad4', the corner nodes in order, normal out
    cell_zone[kind], face_zone[kind]   array('i'): the zone of every cell / face (an index into cell_zone_names / zone_names)

quality() measures it, export(path) writes it for another program

#### `Mesh.cell_count(self, kind=None)`

*(no description yet)*

#### `Mesh.cell_ids(self)`

Every cell as (kind, index in its kind): the order the writers number them in

#### `Mesh.corners(self, kind, i, faces=False)`

The corner nodes of cell i (or of face i) of that kind

#### `Mesh.export(self, path, format=None, **options)`

Writes the mesh: format 'gmsh' (.msh, version 2.2), 'vtk' (.vtu), 'abaqus' (.inp), 'nastran' (.bdf), 'unv' (.unv), 'fluent' (.msh: Ansys Fluent's mesh),
'openfoam' (a polyMesh folder: `path` is the folder), 'stl' / 'obj' (the surface).  The format is found from the ending of the path when it is not given.
options: scale (a factor for the coordinates: 0.001 for metres), zones=False (without the zones), and, for the formats that have them, the ones they list

#### `Mesh.face_count(self, kind=None, zone=None)`

*(no description yet)*

#### `Mesh.quality(self, full=False, max_cells=150000)`

How good the elements are (MeshQuality).  The numbers that look at the neighbours of an element are made for meshes of up to `max_cells` cells,
and for a larger one -- they take a dictionary of its faces -- when full=True is asked; the others are made for every `k`-th cell of a larger mesh

#### `Mesh.surface_area(self)`

The area of the boundary faces (mm^2)

#### `Mesh.surface_mesh(self)`

The boundary alone, as a mesh

#### `Mesh.volume(self)`

The volume of the cells (mm^3)

### `MeshError`

A mesh that cannot be made, and why

### `MeshQuality`

How good the elements are, in the numbers a solver is told about: skewness (0 is a regular element, 1 degenerate), orthogonal quality (1 is best, 0 worst),
the ratio of the longest edge to the shortest, the volume of an element beside the ones it touches, and the smallest dihedral angle of the tetrahedra.
`metrics[name]` is (minimum, mean, maximum); report() says what they mean

#### `MeshQuality.report(self)`

*(no description yet)*

### `graded_size(wall, wall_size, growth_rate=1.2, max_size=None)`

An element size that grows with the distance from a surface: `wall_size` at the surface of `wall`, and `growth_rate - 1` more for every mm from it (1.2: the
size has grown by 0.2 mm after one mm), up to `max_size`.  It is what mesh_volume's element_size takes for a mesh that is fine where the flow or the stress is
and coarse elsewhere:

    mesh = mesh_volume(air, graded_size(body, wall_size=0.5, growth_rate=1.2, max_size=8))

`wall` is a body: the distance is that from the surface of it, on either side (a solid in a fluid, or the fluid's own boundary)

### `inflation_layers(on, first_height, growth_rate=1.2, layers=5)`

Layers of prisms on the surface of a solid, for a flow: `layers` of them, the one at the wall `first_height` thick and each one `growth_rate` times as thick
as the one before it (the first height is what sets y+: about y+ times the viscosity over the friction velocity).  mesh_volume's `boundary_layers` takes it:

    air = difference(box_of_air, body)
    mesh = mesh_volume(air, 2.0, boundary_layers=inflation_layers(body, first_height=0.05, growth_rate=1.2, layers=6))

`on` is the solid whose surface the layers grow from, into the mesh.  The core of the mesh is the body with that much taken off the surface, the layers are
what is between it and the surface: they are made of its boundary, moved to the surface in steps, so the layers follow it -- and at a sharp convex corner,
where the direction of the surface is not defined, they are skewed: round the corners of a body that needs good layers.  The total thickness must be less than
half of the thinnest passage of the fluid

### `mesh_surface(body, element_size=None, element='tri', zones=None, default_zone='wall', feature_angle=None, bounds=None, name=None)`

The surface of `body` as a mesh of triangles ('tri') or quadrilaterals ('quad': every triangle cut into three, so the quadrilaterals are about half the
size of the triangles).  It is the boundary of the tetrahedra that mesh_volume makes, so it follows the body's surface and is conforming, and it costs the
volume mesh; its arguments are mesh_volume's

### `mesh_volume(body, element_size=None, element='tet', boundary_layers=None, zones=None, default_zone='wall', feature_angle=None, cell_zones=None, curved=True, smooth=6, optimize=True, bounds=None, name=None)`

The inside of `body` as a mesh of elements (see the description of this module).

    mesh = mesh_volume(part, element_size=2.0)

element_size    the element edge in mm, or a field (see graded_size); default: about 40 elements along the longest side, no more than about a hundred thousand tetrahedra
element         'tet', 'tet10' or 'hex'
boundary_layers inflation_layers(...): prisms on the surface of a solid
zones           {name: region}: the patches of the boundary; the rest are `default_zone`; feature_angle (degrees) splits those at the creases
cell_zones      {name: region}: zones of cells; the rest are 'fluid'
curved          the nodes the elements have in the middle of edges and faces on the boundary (tet10, hex) lie on the surface
smooth          rounds of moving the nodes of poor tetrahedra toward the middle of their neighbours (along the surface for the nodes on it), where that makes the
                worst of their tetrahedra better; 0 for none
optimize        mend the poor tetrahedra: twice the nodes are moved as `smooth` says and edges and faces are swapped (False: the mesh as it comes, the fastest)
bounds         ((x0, y0, z0), (x1, y1, z1)): the box to mesh in, when the body's cannot be found

## CAD operations: turning, mirroring, patterns, revolving, holes, measuring

The operations of a CAD program, on bodies that are fields: turning, mirroring, patterns, revolving, cutting by a plane, holes (simple, counterbored, countersunk,
tapped, with the sizes of the screws), a few more shapes, and measuring.

    from fieldes import *

    arm = rotate(bar, axis=(1, 1, 0), angle=30, center=(0, 0, 0))           # about any axis through any point (degrees)
    both = mirror(arm, normal=(1, 0, 0), point=(0, 0, 0))                   # the arm and its mirror image in a plane
    studs = linear_pattern(stud, count=4, step=(15, 0, 0))                  # the stud and three more, 15 mm apart
    ring = circular_pattern(blade, count=6, axis=(0, 0, 1), center=(0, 0, 0))     # six blades round an axis
    wheel = revolve(profile, angle=270, axis_point=(0, 0), axis_direction=(0, 1))   # a 2D profile revolved about a line of its plane
    top, bottom = split(block, normal=(0, 0, 1), point=(0, 0, 5))           # a body cut by a plane into its two sides
    plate = hole(plate, position=(10, 10, 4), screw='M6', kind='counterbore')      # a hole for an M6 socket head screw, from the top face down
    tapped = hole(plate, position=(30, 10, 4), screw='M6', kind='tapped', modeled=True)       # an M6 thread, modelled
    part = union(capsule(3, 20), tube(8, 6, 12))                            # a capsule, a tube
    print(surface_area(part), section_area(part, (0, 0, 1), (0, 0, 5)), distance_between(a, b), interference(a, b))

Every function takes numbers for its places, directions, sizes and angles -- plain numbers, or var() numbers, which a gizmo or a handle drags -- (a point of the script, which
has .x .y .z, is taken for a position) and returns a body that is made of the ones it was given by the fields' own arithmetic, so what comes out is a body like any other: it
can be meshed, offset, shelled, exported and measured, and it is a BODY in the model tree (a 3D shape, with a gizmo; `slot` is a 2D shape).  The shapes made here -- capsule, tube,
slot, threaded_rod -- are drag adjustable like the primitives: each is made of numbers that stand for ONE face, so the handles pull a face each (see "More shapes").  Lengths are in mm and angles in degrees.
A count is a whole number, and so is a direction that a hole is drilled in (a number, not a var()).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `align(shape, to, axis='z', mode='center', gap=0.0)`

The body moved along one axis ('x', 'y' or 'z') to line up with another body, by the boxes round them: `mode` 'min' puts their low sides on one plane, 'max' their
high sides, 'center' their middles, 'above' puts the body on the high side of `to` (with `gap` mm between), and 'below' on the low side

### `capsule(radius, height, base=(0, 0, 0))`

A cylinder with a hemisphere on each end, standing along z on `base`: `height` is the whole length, so it is at least twice the radius.  A body: the distance to its
surface is exact.  Its handles are its radius (the side) and the heights of its two ends

### `circular_pattern(shape, count, axis=(0, 0, 1), center=(0, 0, 0), angle=360.0)`

`count` copies of the body round the line through `center` with the direction `axis`, as one body: the union of the copies, which is exact everywhere.  With the whole
turn (`angle` 360, the default) the copies are evenly spread and the last one is not on the first; with less (a number, or a var(), which always means this), the first
is where the body is and the last `angle` degrees from it

### `cut_plane(shape, normal=(0, 0, 1), point=(0, 0, 0), keep='above')`

The body cut by the plane through `point` that is square to `normal`: the part on the side that `normal` points to is kept (`keep='above'`), or the part
on the other side (`keep='below'`)

### `distance_between(a, b, element_size=None)`

The shortest distance (mm) between the surfaces of two bodies, 0 when they touch or overlap.  The surface of each is meshed (the default size is a fortieth of the longest
side of the two); the closest pairs of vertices in up to eight different places are found, and each is improved by going from one surface to the other along the gradient of the
field (the way to the nearest point of a surface that is a distance field, which is exact for a plane and a sphere), until the distance stops getting shorter; the shortest
of them is the distance.  So a body that comes near the other in several places is measured at each, and not only at the place whose vertices happen to be closest.  It takes
seconds for two bodies with a hundred thousand vertices between them

### `hole(body, position, diameter=None, depth=None, direction=(0, 0, -1), kind='simple', head_diameter=None, head_depth=None, angle=90.0, screw=None, fit='medium', pitch=None, thread_depth=None, modeled=False, hand='right')`

The body with a hole in it: it starts on the surface at `position` and goes `depth` into the body along `direction` (the default goes down; through everything
when `depth` is None), `diameter` wide, or the size of the screw: `hole(plate, (10, 10, 4), screw='M6', kind='counterbore')`.  `kind` is 'simple', 'counterbore',
'countersink' or 'tapped' (see hole_cutter, which says it all)

### `hole_cutter(position, diameter=None, depth=None, direction=(0, 0, -1), kind='simple', head_diameter=None, head_depth=None, angle=90.0, screw=None, fit='medium', pitch=None, thread_depth=None, modeled=False, hand='right')`

The shape of a hole, to subtract from a body (hole does that): it starts on the surface at `position` and goes `depth` into the body along `direction` (through
everything when `depth` is None), `diameter` wide.  `kind` is 'simple', 'counterbore' (a wider, flat-bottomed step at the mouth: `head_diameter` across,
`head_depth` deep), 'countersink' (a cone at the mouth that opens to `head_diameter` at the surface, `angle` degrees across: 90 for a metric screw) or 'tapped'
(the hole of a thread).  `screw='M6'` gives the sizes from the screw: the clearance hole for it (`fit` 'medium' or 'close'; for a tapped hole the tap drill), and for a
counterbore or a countersink the head's room (a socket head and 1 mm, 0.5 mm deeper than it; a flat head) -- anything given as well is used instead.  A tapped hole is the tap
drill alone (what a drawing shows as a cosmetic thread), or with `modeled=True` the thread itself, an ISO profile of right-hand turns (`hand='left'`) over `thread_depth`
(default the depth of the hole, else three diameters); a thread wants a fine mesh to draw (a pitch every 8 cells at least).  `position`, `diameter`, `depth` and the head sizes may be
var() numbers; `direction` is a number.  Use the cutter for a pattern of holes: union the cutters, subtract the union once

### `interference(a, b)`

The volume (mm^3) that two bodies have in common, by sampling (about 1 % off for a part that is not thin): 0 when they only touch or do not meet

### `linear_pattern(shape, count, step)`

`count` copies of the body in a row, the first where the body is and each one `step` (x, y, z) from the one before, as one body: the union of the copies, which is exact
everywhere.  `step` may be var() numbers

### `mirror(shape, normal=(1, 0, 0), point=(0, 0, 0))`

The body and its mirror image in the plane through `point` that is square to `normal`, as one body (a part that is made on one side of its plane of symmetry,
then mirrored).  Where the body crosses the plane the two halves fuse.  A plain operation, a finished body with no link to the original -- a symmetry
(symmetry_planes) is the live version, whose images follow every edit

### `reflect(shape, normal=(1, 0, 0), point=(0, 0, 0))`

The mirror image of the body in the plane through `point` that is square to `normal`: the body itself is gone, only its image is left.  (mirror keeps both.)
It is a plain operation on one body, with no link to it: for an image that follows the original as you edit it, use a symmetry (symmetry_planes, Shift+S)

### `revolve(profile, angle=360.0, axis_point=(0, 0), axis_direction=(0, 1))`

The body that a 2D profile makes by turning about a line in its own plane (the plane of x and y: the profile is what a 2D shape is at z = 0).  The line goes through
`axis_point` (x, y) and has the direction `axis_direction` (x, y); with the default it is the y axis.  `angle` is how far it turns, 360 for a body of revolution; with
less, the part of the profile on the right of the axis (seen along its direction: x > 0 for the y axis) turns from the plane of the profile toward +z, and the part on
the left turns the same way round the axis (so toward -z): a profile may lie on either side, or on both.  The angle, the point and the profile's numbers may be var()
(a var() angle always makes a turn that stops at the angle, 360 included).  revolve_y is this for the whole turn about a line parallel to y

### `rotate(shape, axis=(0, 0, 1), angle=0.0, center=(0, 0, 0))`

The body turned by `angle` degrees about the line through `center` with the direction `axis` (the right-hand rule: the thumb along the axis, the fingers the
way it turns).  rotate_x, rotate_y and rotate_z are this about an axis of the coordinates, in radians.  Each number may be a var()

### `screw_spec(screw, pitch=None)`

What a metric screw ('M6') asks of the holes for it, in mm, as a dictionary: diameter, pitch, tap_drill (the hole a thread is cut in: the nominal diameter less the pitch),
minor_diameter (of the thread in the nut: 1.0825 pitches less than the nominal), clearance_close and clearance_medium (the hole the screw passes through, ISO 273),
head_diameter and head_height (a socket head cap screw, ISO 4762) and countersink_diameter (a 90 degree flat head: 2.24 times the nominal plus 0.4).  `pitch` says a
fine thread (M8 with pitch 1): the tap drill and the minor diameter follow it

### `section_area(shape, normal=(0, 0, 1), point=(0, 0, 0), resolution=300)`

The area (mm^2) of the cross section of a body with the plane through `point` that is square to `normal`, counted on a grid of `resolution` cells along the larger side
of the section: about 0.5 % off for 300

### `slot(length, width, center=(0, 0))`

A 2D slot (an obround: a rectangle with a half circle on each short end), `length` long in x and `width` across, about `center` (x, y); `length` is the whole length,
so it is at least the width.  Extrude it for a slot in a plate.  Its handles are its four sides: an end pulled makes it longer and keeps the round end, a long side pulled makes it
wider and keeps both ends round

### `split(shape, normal=(0, 0, 1), point=(0, 0, 0))`

The body cut in two by the plane through `point` that is square to `normal`: (the part the normal points to, the other part)

### `surface_area(shape, element_size=None)`

The area of the surface of a body (mm^2), from a mesh of triangles on its surface: with the default size (a fortieth of the body's longest side) a sphere's is
about 1 % low, and a finer `element_size` (mm) is closer.  A mesh of this kind cuts the corners of curves, so the area is always a little low

### `threaded_rod(screw, length, base=(0, 0, 0), pitch=None, hand='right')`

A rod with a right-hand ISO thread (`hand='left'` for a left-hand one) along its whole length, standing along z on `base`: the screw (`screw='M8'`, `pitch` for a fine one) is the
nominal diameter, the thread has the ISO profile (the root at the diameter 1.2269 pitches under the nominal).  It fits the hole of hole(..., kind='tapped', modeled=True) of the
same screw.  Its handles pull its side (the diameter: the pitch stays, and so does the profile of the thread) and its two ends.  Like a thread, it wants a fine mesh to draw

### `tube(outer_radius, inner_radius, height, base=(0, 0, 0))`

A hollow cylinder (a pipe), standing along z on `base`: `outer_radius` and `inner_radius` are its two radii.  Its handles are its outer wall, its bore and its two ends

## Solvers of your own

Your own solvers: write the equations of a problem in Python, and FielDes meshes the part, runs them, and draws the result.

    from fieldes import *

    plate = box((0, 0, 0), (60, 20, 3))
    cold = box((-1, -1, -1), (1, 21, 4))                    # where the plate is held at 20 degrees
    hot = box((59, -1, -1), (61, 21, 4))                    # and where it is at 80

    result = solve('heat', plate, fixed=[(cold, 20), (hot, 80)], conductivity=0.2)
    result                                                  # displayed: the temperature of the plate, with the elements behind it (the Elements button)
    print(result.max_temperature)

`solve(name, part, element_size=None, **parameters)` meshes `part` with tetrahedra (`mesh_volume`, `element_size` as there; or give it a mesh you made), makes a `Problem` of the
mesh and calls the solver `name` with the problem and the parameters.  The solver gives back the fields it found -- one number at every node of the mesh -- and, if it likes,
some numbers.  The result is displayed like the result of any analysis: coloured by the field, with the mesh behind it.

A solver is a function with a decorator, in a file of the solvers folder (Settings > Solver... opens it, and edits the files):

    from fieldes.stdlib.solvers import solver, Sparse, tet_gradients

    @solver('heat', description='Steady heat conduction',
            equation='div(conductivity * grad(T)) + source = 0')
    def heat(problem, fixed=(), conductivity=1.0, source=0.0):
        K = Sparse(len(problem.nodes))
        load = [0.0] * len(problem.nodes)
        for tet in problem.tets:
            volume, g = tet_gradients(*[problem.nodes[i] for i in tet])
            for a in range(4):
                load[tet[a]] += source * volume / 4.0
                for b in range(4):
                    K.add(tet[a], tet[b], conductivity * volume * (g[a][0] * g[b][0] + g[a][1] * g[b][1] + g[a][2] * g[b][2]))
        values = {}
        for region, value in fixed:
            for i in problem.nodes_in(region):
                values[i] = value
        return {'temperature': K.solve(load, values)}

What a solver is given:

    problem.nodes           [(x, y, z), ...]: every node of the mesh
    problem.tets            [(a, b, c, d), ...]: the nodes of every tetrahedron
    problem.nodes_in(region)    the nodes that lie in a body, on a surface selection or on a field that is at most zero there (`tolerance=` mm beyond it)
    problem.boundary_nodes()    the nodes on the surface of the part
    problem.element_size    about how large an element is, mm
    problem.mesh, problem.body, problem.parameters

The tools it can use (they are plain Python: nothing here needs a library that is not there):

    tet_gradients(p0, p1, p2, p3)       the volume of a tetrahedron and the gradients of its four linear shape functions
    Sparse(n)                           a matrix of n rows: .add(i, j, value), .multiply(x), .solve(b, fixed={node: value}) -- conjugate gradients, with the fixed nodes taken out

What it gives back: a dictionary of fields `{'temperature': [one number for each node]}`, or `(fields, numbers)` with `numbers = {'max_temperature': 80.0}`.  Both are the result's: a field is
`result.fields['temperature']` and `result.temperature`; a number is `result.max_temperature`.  The solver `heat` here is the one the example is written for: copy it, change its
equation -- the conductivity as a function of the temperature, a source that depends on where it is -- and it is yours: `conductivity` and `source` may be functions
`f(T, x, y, z)`, and the temperature is then found by iterating (see below).  The solver `elasticity` is the equations of the static analysis in Python
(`solve('elasticity', part, fixed=[(face, (0, 0, 0))], forces=[(end, (0, 0, -500))], young=210000)`): `young` may be a function of the strain, `gravity` a body force; copy it
to change the law of the material, the loads, or the equations themselves.

It is Python, so it is as slow as Python is: a mesh of a few thousand nodes is a second or two, a hundred thousand is not for this.  The analyses FielDes has built in -- static, modal,
thermal, fluid -- are made in C++ and are for that.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `Problem`

What a solver is given (see the module): the mesh as lists, and the ways to find the nodes of a region

#### `Problem.boundary_nodes(self)`

The nodes on the surface of the part

#### `Problem.nodes_in(self, region, tolerance=None)`

The nodes that lie in `region` -- a body, a surface selection, or a field that is at most zero there -- or within `tolerance` mm of it (default a twentieth of an element)

#### `Problem.per_tet(self, values)`

The mean of a nodal field over each tetrahedron

### `SolverResult`

What `solve` gives back: `fields` (a number for each node, by name) and `numbers`, each also an attribute by its name (`result.temperature`, `result.max_temperature`)

### `Sparse`

A square matrix of n rows that is mostly zeros: .add(i, j, value) puts a number in (it adds to what is there), .multiply(x) is the matrix times a list, and .solve(b, fixed) finds
x with A x = b for a symmetric matrix, with some of the x given -- `fixed` is {index: value} -- by conjugate gradients

#### `Sparse.add(self, i, j, value)`

*(no description yet)*

#### `Sparse.multiply(self, x)`

*(no description yet)*

#### `Sparse.solve(self, b, fixed=None, tolerance=1e-09, iterations=None)`

x with A x = b, and x[i] = value for every i in `fixed`: the unknowns are the others, which conjugate gradients with a diagonal preconditioner finds (the matrix is symmetric
and positive definite once enough nodes are fixed)

### `elasticity(problem, fixed=(), forces=(), young=210000.0, poisson=0.3, gravity=None, density=7.85e-09, iterations=20)`

Static elasticity with small strains, linear tetrahedra.  fixed=[(region, (ux, uy, uz)), ...]: where the displacement is given (mm; None for a direction that is free, a single
number for all three).  forces=[(region, (fx, fy, fz)), ...]: the total force (N) on a region, shared by the area of the surface it is on.  young (MPa): a number, or a function
f(equivalent_strain, x, y, z) -- the stiffness of the material where the strain is that, found by iterating (the secant modulus: the stiffness of the last iteration is the
next one's).  poisson.  gravity=(gx, gy, gz) in mm/s2 with density in t/mm3 (steel 7.85e-9).  Gives the fields von_mises (MPa), displacement (mm) and ux, uy, uz, and the numbers
max_von_mises and max_displacement

### `heat(problem, fixed=(), conductivity=1.0, source=0.0, iterations=25)`

Steady heat conduction.  fixed=[(region, temperature), ...]: where the temperature is given.  conductivity (W/mm/K) and source (W/mm3): a number, or a function f(T, x, y, z) of the
temperature and the place -- then the temperature is found by iterating: the conductivity of the last one is the next one's, until the temperature stops changing

### `solve(name, part, element_size=None, **parameters)`

Runs the solver `name` on `part` (see the module): the part is meshed with tetrahedra -- `element_size` as in mesh_volume, or give `part` as a mesh you made -- and the
solver is called with the problem and `parameters`.  Returns a SolverResult

### `solver(name, description='', equation='')`

A decorator: the function is the solver `name` -- `solve(name, part, ...)` runs it.  `description` is a line about it, `equation` the equations it solves, in a line of Python, for
the list of solvers (Settings > Solver...)

### `solver_names()`

The names of the solvers `solve` can run: the ones in this module and the ones of the solvers folder

### `tet_gradients(p0, p1, p2, p3)`

(volume, [g0, g1, g2, g3]) of the tetrahedron with these corners: the gradients of the four linear shape functions (each is 1 at its corner and 0 at the others)

## Units

Units: what the program shows its numbers in, and what the files it writes are in.

A script and the solvers have ONE set of units and Settings > Units does not change it: lengths in millimetres, forces in newtons, stresses and pressures in
megapascals, masses in kilograms, temperatures in degrees Celsius (so that a script means the same on every machine).  The choice made there is about the
other side of the numbers:

    shown      the ruler, the key numbers of a result and its colour bars, the value under the cursor, the work plane and the radius of a surface selection
    written    export_mesh() and Mesh.export() write their coordinates in the length unit (`scale=` of Mesh.export and `unit=` of export_mesh override it);
               the formats that state their unit (3MF, DXF, FBX) say it
    read       a mesh file that does not say what its numbers mean (STL, OBJ, PLY, OFF) is read in the length unit, unless import_model(..., file_units=...)
               says otherwise

Files of the analyses (export_fea, export_results, export_field) stay in millimetres, newtons and megapascals: a solver reads a consistent set, and the
file says nothing about its units.  The key numbers of export_results(..., '.txt') are in the shown units, each with its unit written next to it.

    from fieldes import units
    units.configure({'length': 'in', 'stress': 'psi'})     # the rest stay at their defaults
    units.convert(25.4, 'mm')                              # (1.0, 'in')
    units.convert(6.894757, 'MPa')                         # (1000.0, 'psi')

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `check_length(unit)`

A name of a length unit; raises ValueError when it is not one

### `configure(choice=None)`

Chooses the units numbers are shown and files are written in.  Whatever `choice` leaves out is its default (so configure() resets).
Raises ValueError for a quantity or a unit that is not known.  Returns the choice

### `conversion(native)`

How a number in a unit the program works in is shown now: (scale, offset, label) -- shown = number * scale + offset -- or None for a unit that is
not converted (a count, a time, a frequency).  The units that are converted are those of NATIVE: lengths (mm, mm^2, mm^3, mm/s, mm^3/s), stress and
pressure (MPa), force (N), the product of force and length (N mm), mass (kg) and temperature (C)

### `convert(value, native)`

(the number as it is shown, the unit as it is shown) for a number in a unit the program works in; the number itself for a unit that is not converted

### `convert_text(text, native)`

The same for a number written as text -- '12.5', or '(1, 2, 3)' for a vector --: (text, unit) as they are shown.  A text that is no number stays as it is,
with its own unit

### `current()`

The units chosen: {'length': 'mm', 'force': 'N', 'stress': 'MPa', 'mass': 'kg', 'temperature': 'C'}

### `dxf_unit(unit)`

The $INSUNITS code of a DXF file for a length unit (4 millimetres, 1 inches ...)

### `export_scale()`

The factor the coordinates of a file are multiplied by to be in the length unit chosen: 1 for millimetres, 0.001 for metres

### `export_unit()`

The name of the length unit files are written in (the one chosen)

### `import_mm()`

Millimetres in the unit a mesh file is read in when it does not say (the length unit chosen)

### `length_mm()`

Millimetres in one unit of the length chosen

### `options()`

What can be chosen: {quantity: [(name, how it is shown)]} -- the order is the order of the menus

### `shown_name(unit)`

A unit as it is written on a screen: um is the micrometre sign, C and F carry a degree sign

### `table()`

conversion() of every unit of NATIVE, as {unit: {'scale', 'offset', 'label'}}: what the window is given to convert what it draws

### `threemf_unit(unit)`

What a 3MF file calls a length unit ('millimeter', 'inch' ...)

## Parametric optimization

parametric_optimization: the best numbers for the var()s of a model, over an analysis.

    from fieldes import *

    width = var(20)
    height = var(12)
    beam = box((0, 0, 0), (100, width, height))
    wall = fixed(box((-1, -1, -1), (1, 25, 25)))
    pull = force(box((99, -1, -1), (101, 25, 25)), (0, 0, -200))
    stress = static_analysis(beam, supports=[wall], loads=[pull], material=aluminium)

    best = parametric_optimization(stress, [(width, 8, 30), (height, 6, 24)],
                                   constraints=[minimize('mass'), 'max_von_mises <= 90', 'max_displacement <= 0.5'])

The analysis is solved again with each set of numbers the search tries -- the model is made again from the numbers, the mesh, the
solve, every measure of the result -- and the numbers that give the best design stay in the var()s: the model, and everything after
the call, is the best design.  The search needs no derivatives: it samples the ranges, then closes in on the best design with a
search over surrogates of the objective and of every constraint that puts a design that meets the constraints before one that does not.  It tries a few tens of designs; every
one is an ordinary analysis that is kept by its content, so a run again (or another search over the same designs) finds what it
tried in the cache.  Everything it tried is listed in the result.

What it can be asked for -- everything is in the list `constraints=`, as in topology_optimization -- is a measure of the analysis' result:
the name of one of its numbers ('mass', 'max_von_mises', 'max_displacement', 'compliance', 'safety_factor', 'volume', 'max_temperature',
'pressure_drop', 'frequencies[0]' ...), an expression of them ('mass + 0.01 * compliance'), or a function of the result
(`lambda r: r.max_von_mises / r.mass`).  A goal is minimize(x) or maximize(x), at most one.  A constraint is a comparison of measures in a
text ('max_von_mises <= 90', 'frequencies[0] >= 80', an exact value 'frequencies[0] == 100', a range '80 <= frequencies[0] <= 120'), or a
function of the result that gives how much it is broken (0 or less when it is kept).  Without a goal the search only looks for a design that
keeps the constraints.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `ParametricError`

The optimisation cannot be made as asked

### `ParametricResult`

What parametric_optimization found.  It is the analysis of the best design (its fields, its numbers: `best.max_von_mises`, `best.von_mises`)
and what the search did:

    values       the best numbers, {name of the var: number}
    objective    the best design's value of what was minimised or maximised
    feasible     whether the best design keeps every constraint (when no design does, it is the one that breaks them the least)
    history      every design tried, in order: {values, objective, violation, feasible, measures, seconds, error}
    result       the analysis of the best design
    evaluations  how many designs were tried
    apply()      writes the best numbers into the var()s of the script (apply=True in the call does it at the run)

#### `ParametricResult.apply(self)`

Writes the best numbers into the var()s of the script, in the text, when the run is over

#### `ParametricResult.table(self)`

Every design tried, as text

### `parametric_optimization(analysis, variables, constraints=(), max_evaluations=None, tolerance=0.002, apply=False)`

The best numbers for the var()s of a model, over an analysis: the designs the search tries are solved with `analysis` again, each with
other numbers for the variables, and the numbers of the best one stay in the var()s.

analysis      the result of an analysis of this script (static_analysis, modal_analysis, thermal_analysis, fluid_analysis ...), made of the
              var()s
variables     what is changed: (var, low, high) -- the var() and the range it is searched in -- or a list of them
constraints   what the design is for, a list -- as in topology_optimization.  The comparisons the best design keeps, in a text
              ('max_von_mises <= 90', 'frequencies[0] >= 80', an exact value 'frequencies[0] == 100', a range '80 <= frequencies[0] <= 120'),
              or functions of the result that are 0 or less when kept.  And at most one goal: minimize(x) makes x as small as it can be,
              maximize(x) as large -- x a measure of the result: the name of one of its numbers ('mass', 'max_von_mises', ...), an expression
              of them ('mass + 0.01 * compliance'), or a function of the result.  Without a goal the search stops at the first design that
              keeps the comparisons
max_evaluations  how many designs it tries at most (default 20 + 10 for each variable)
tolerance     it stops when the designs it is looking at are this close, of the ranges (default 0.2 %)
apply         True writes the best numbers into the var()s of the text when the run is over (otherwise they hold for this run only)

Returns a ParametricResult: the analysis of the best design, `.values`, `.objective`, `.feasible`, `.history`.  When no design keeps the
constraints it says so and is the one that breaks them least.

## Manufacturing: the constraints

Manufacturing, the rules: what a part has to keep to be made, as constraints and the presets of a process (fff_constraint, cnc_constraint, molding_constraint).

This is the SPECIFICATION and nothing is measured here: the check (manufacturing_check.py) and the optimization (manufacturing.py) both read what is asked from these
objects, and each does its own measuring.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `ManufacturingConstraint`

What a part has to keep to be made: a set of rules -- `overhang`, `min_wall`, `min_gap`, `max_bridge`, `min_radius`, `draft`, `undercut_along`,
each optional -- with the directions they are about and, if given, the region they hold in.  Made by `manufacturing_constraint` and by
the presets `fff_constraint`, `cnc_constraint` and `molding_constraint`; it is given to `manufacturing_analysis` and
`manufacturing_optimization` in `constraints=`.

### `ManufacturingError`

A manufacturing check or remedy cannot be made as asked

### `cnc_constraint(tool_diameter=6.0, axis=(0, 0, 1), min_wall=0.8, region=None)`

What a 3-axis milling machine can make with a cutter of `tool_diameter` mm coming along `axis`: an inside corner cannot be sharper than
the cutter's radius (min_radius) and a slot cannot be narrower than the cutter (min_gap), no feature can be out of reach of the cutter
from the top and from the bottom (undercut_along), and a wall of metal is not thinner than min_wall (0.8 mm for metal, 1.5 or more for
plastic: thin walls chatter).  Guides advise a radius a little more than the tool's, and a corner radius of a third of the
depth of the pocket.

### `fff_constraint(nozzle=0.4, build_direction=(0, 0, 1), overhang=None, max_bridge=20.0, region=None)`

What a fused-filament (FFF) printer can make, for a nozzle of `nozzle` mm.  min_wall is two nozzle widths (the least that prints as a
wall: published guides give 0.8 to 1.2 mm for 0.4 mm, 1.2 and more for strength), min_gap one nozzle width (a narrower gap
prints closed), max_bridge 20 mm (guides give up to about 30 mm for a clean bridge, less with a soft material).  The overhang rule is off:
a slicer puts supports where it is needed -- give overhang=45 to ask for none (a printer without supports, or a region they cannot reach
-- scope it with region=).  Guides give 45 degrees as the limit; it depends on the material and the printer: calibrate with a test print.

### `manufacturing_constraint(overhang=None, min_wall=None, min_gap=None, max_bridge=None, min_radius=None, draft=None, undercut_along=None, build_direction=(0, 0, 1), pull_direction=(0, 0, 1), region=None, overhang_fix='fill', gap_fix='fill', support=None, support_gap=0.3, no_support=None, name='manufacturing_constraint')`

A constraint a part has to keep to be made, every rule optional (only what is written is asked):

    overhang      the steepest a surface that faces down may be without a support under it, in degrees from the vertical (45: a
                  surface that leans out more than 45 degrees needs one).  Off unless given
    min_wall      the thinnest wall, mm
    min_gap       the narrowest gap between two walls, a hole or a slot, mm
    max_bridge    the longest flat span with nothing under it, mm
    min_radius    the smallest radius of an inside corner, mm (a milling cutter cannot cut a sharper one than its own radius)
    draft         the least slope of a wall away from `pull_direction`, degrees (a moulded part has to come out of its mould)
    undercut_along  a direction: nothing that a tool or a mould half coming along it, from either side, cannot reach
    build_direction  the way up of a print, a direction (default (0, 0, 1)); None leaves it to be found, the way up that needs the least
    pull_direction   the way a mould opens, for `draft` (default (0, 0, 1))
    region        a shape: the constraint holds only inside it (default: everywhere).  A part can be given a constraint for each region
    overhang_fix  what the shell does to an overhang: 'fill' grows the body under it at the angle asked (a gusset), 'cut' cuts the
                  air above it into a teardrop (for a channel or a bore); a region of the other kind is a constraint of its own
    gap_fix       what the shell does to a gap that is too narrow: 'fill' closes it, 'widen' makes it as wide as asked
    support       the kind of support the shell may add under an overhang it cannot fix by changing the part: 'block' (a block straight under the overhang, down to the plate or
                  the part below); None (the default) makes none -- an overhang the part's own changes do not fix is reported.  Supports are a separate body of the result
                  (`result.supports`), exported as an object of their own
    support_gap   the clearance between a support and the part, mm (default 0.3: what lets it be broken off)
    no_support    a shape, or a list of them: where no support may be made (kept clear for the surface that has to be clean, or a slicer's own supports)

The presets `fff_constraint`, `cnc_constraint` and `molding_constraint` write the numbers of a process.  Several constraints can be
given together: a rule is kept when it is kept by each of them.

### `molding_constraint(min_wall=1.0, draft=1.0, pull_direction=(0, 0, 1), region=None)`

What an injection-moulded part needs: walls not thinner than min_wall (1 to 4 mm by the plastic, uniform), a draft of `draft` degrees
(1 is the baseline, 2 and more for a texture or a tall wall) on every wall, no undercut along the way the mould opens, and inside corners
of half the wall's thickness (the usual interior fillet).  Published guides, not a standard: ask your plastic's maker.

## Manufacturing: the check

Manufacturing, the check: where a part breaks the rules of its constraints (manufacturing_analysis).

THE CHECK IS INDEPENDENT OF THE OPTIMIZATION (manufacturing_optimization, in manufacturing.py), so that it can verify it.  This file imports the rules
(manufacturing_rules.py) and the tools that read a part's field (manufacturing_field.py), and nothing of the optimization: the remedies do not appear here, and what
makes the remedies find a thin wall or a leaning face is not what finds it here.  The optimization calls `_analyse` (below) to be told what its result still breaks, and
that is the only way it reaches into this file.  Where a rule is measured here, it is measured by a way of its own:

    wall thickness, gap width   the thinnest straight line through the material (the air, for a gap) from the surface point, in the cone of 20 degrees round the normal
                                (the optimization measures along the normal alone)
    overhang, draft             the normals of the surface against the way up, and the pull direction
    inside corner radius        whether a ball of the radius reaches into the corner (the optimization closes the part with the library's fillet)
    undercut, bridge            lines along the direction from the surface point, against the part

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `ManufacturingAnalysis`

The result of manufacturing_analysis(): where the part breaks the rules of its constraints.

    fields        {rule: field}: for each rule, a field that is above 0 on the surface where the rule is broken and by how much (in the
                  unit of the rule: mm, degrees), and `'all'`, the worst of them as a share of its limit -- what the part is coloured by;
                  each is also an attribute: `result.min_wall`
    report        {rule: {...}}: for each rule, `share` of the surface that breaks it, `worst`, the `regions` it is broken in
                  (`at`, the number of `samples` and the `box` of each) and its `limit`
    compliant     whether no rule is broken anywhere that was measured
    samples       how many points of the surface were measured (a measurement: the numbers of `report`)
    orientation   the way up the part was checked in, found when the constraint left it free

Stated on its own the part is shown red where a rule is broken and in its own colour where they are kept (the card switches to one rule).  It is a check of
the part's own field, not a substitute for the check of the one who makes it.  Nothing of the optimization is used to make it.

### `manufacturing_analysis(part, constraints, samples=5000, keep=None)`

Where a part breaks the rules of its constraints.  The part is checked on its own field, with other means than the shell that changes it
(manufacturing_optimization): the normals, and what a ball of the size of each rule can reach from each point of the surface -- every
rule a field on the part, drawn as colours on it, nothing meshed.  The numbers (how much of the surface breaks each rule, and where)
are measured on `samples` points of the surface.

    part         a body
    constraints  manufacturing_constraint(...), fff_constraint(...), cnc_constraint(...), molding_constraint(...): one, or a list;
                 a rule is broken where it is broken for any of them
    samples      how many points of the surface are measured (default 5000)
    keep         a shape, or a list of them: regions that are not judged -- the ones that are kept as they are, as `keep=` of manufacturing_optimization says -- so that
                 the shares are of the surface outside them and their colours are those of a part that keeps its rules

Returns a ManufacturingAnalysis: `.compliant`, `.report`, `.fields`.  It is a check of the part's own field and not a substitute for the check
of the one who makes it.

## Manufacturing: the shell that changes the part

Manufacturing: constraints a part has to keep to be made, a check of them, and a shell that changes the part until it keeps them.

    from fieldes import *

    printer = fff_constraint(nozzle=0.4)                         # what a printer with a 0.4 mm nozzle can make
    report = manufacturing_analysis(part, constraints=[printer])   # where the part breaks them (coloured on the part), and how much
    better = manufacturing_optimization(part, constraints=[printer])   # the part, changed until it keeps them, and what was changed

The code is in four files, and the check does not share with the shell what finds a fault:

    manufacturing_rules.py   the constraints and the presets: what is asked (nothing is measured)
    manufacturing_field.py   how a part's field is read: moved lookups, normals, values at points (no rule)
    manufacturing_check.py   manufacturing_analysis: finds where the rules are broken, with a measurement of its own for each rule
    manufacturing.py         this file: manufacturing_optimization and its remedies, each finding what it fixes with code of its own, and its result

The check never imports this file, and the remedies call nothing of the check but `_analyse`, to be told what their result still breaks -- so that the check
of a result, made by `manufacturing_analysis`, is a verification of the remedies and not their own reading of themselves.

A CONSTRAINT is an input, like a support or a load (the Engineering menu > Manufacturing makes them).  `manufacturing_constraint` has
every rule, each optional -- only what is written is asked -- and `fff_constraint`, `cnc_constraint` and `molding_constraint` are
presets of it with the numbers of a process:

    overhang=45        the steepest a surface that faces down may be, in degrees from the vertical, without a support under it (off unless
                       asked: supports are normal in printing; it is for what supports cannot reach -- channels, bores, cosmetic faces,
                       lattices -- or a printer without them: scope it with region=)
    min_wall=0.8       the thinnest wall (mm)
    min_gap=0.4        the narrowest gap between walls, a hole or a slot (mm)
    max_bridge=20      the longest flat span with nothing under it (mm)
    min_radius=3       the smallest radius of an inside corner (mm): a milling cutter's radius
    draft=1.0          the least slope of a wall away from the pull direction (degrees: a moulded part has to come out)
    undercut_along=(0,0,1)   no feature that a tool or a mould half coming along this direction (either way) cannot reach
    build_direction    the way up of a print (default z); None leaves it to be found, the way that needs the least
    region=            a shape: the constraint holds only inside it (a part can have a constraint for each of its regions)

The SHELL makes the field of the part changed by remedies, each a field made of shifted copies of the part's own: a gusset that makes an overhang stand
at the angle asked (the body under it grown at that slope), a teardrop cut for a channel, an offset that follows how thin a wall is, a fill of the gap that is
too narrow, a rounding of the inside corners (the library's fillet), a draft, a fill of an undercut.  They are all made from the part as it is -- shifted copies
of shifted copies would multiply -- and united, what they add and then what they cut; what each changed is measured, and one that would change more than
`max_change` of the part is not made.  Then the check (manufacturing_check.py) is made on what it made, and what it could not fix is said, with where.  A region
that is excluded (exclude) or kept is never touched.  Give the result to the shell again for a second pass.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `ManufacturingResult`

The part the shell made, and what it did.  It is a field like the part is -- wherever a body goes it goes (`offset(result, 1)`, `difference`,
the part of an analysis, an export).  Stated on its own it is shown coloured by the geometry change: red where material was added, blue where it was
removed, the part's colour where it is unchanged.

    part          the part as it was
    changes       what each remedy did: {remedy, rule, added, removed (mm^3), kept, why}
    added, removed   mm^3 of material added and cut in all (measured on points of space: about, to a few per cent)
    share         the part's volume changed, as a share
    before        the check of the part as it was (a ManufacturingAnalysis)
    after         the check of what the shell made, when it was made (a ManufacturingAnalysis), or None
    fail_states   what is still broken, where: {rule, where, share, why} -- the five biggest places of each rule
    fail_totals   all the places the re-check found, of each rule: {rule: {places, share}} (what the card counts; the shares are shares of the surface)
    orientation   the way up of the print, when the constraint left it free (the part is not turned: oriented() is the part turned to stand that way)
    change_map    the field the part is coloured by: 1 added, -1 cut, 0 as it was

What it made is the part's field changed, not a mesh: run `manufacturing_analysis` on it (or `.after`) to see what the check says of it -- it is the
shell's own check, with the rules it was asked.  Not a substitute for the check of the one who makes it.

#### `ManufacturingResult.oriented(self, shape=None)`

The part turned so that the way up it was made for is z: what a print of it is made of (the part in the model stays as it is).  Another shape -- the supports,
a no-support region -- is turned the same way

### `manufacturing_optimization(part, constraints, keep=None, remedies=None, max_change=0.25, max_support_height=None, verify=False, samples=2500, show_turned=True)`

The part, changed until it keeps the rules of its constraints, and what was changed.  Each remedy is a field made from the part's own, in the
region of its constraint: thin walls thickened (an offset that follows how thin the wall is), a narrow gap closed (`gap_fix='widen'` of the
constraint: made as wide as asked), an inside corner rounded to the radius, a draft put on the walls, an undercut filled, an overhang made to stand
by a gusset at the angle asked (`overhang_fix='cut'`: a teardrop cut above a channel).  They are made from the part as it is and united;
what they changed is measured, and a remedy that would change more of the part than `max_change` allows is not made.  A region that is excluded
or in `keep` is never touched.

    part         a body
    constraints  manufacturing_constraint(...), fff_constraint(...) ...: one, or a list.  A constraint with a region changes the part only there
    keep         shapes (regions) that stay as they are, besides the regions the part was excluded with
    remedies     which remedies may be made, a list of 'undercut', 'draft', 'gaps', 'corners', 'walls', 'overhang' (default: those the rules ask for)
    max_change   the most of the part's volume that the shell may change, as a share (default 0.25); a remedy that would go past it is not made
    max_support_height  how deep a gusset under an overhang may be, mm (default a fifth of the part's size)
    verify       False (the default): the shell does not check what it made -- run manufacturing_analysis on the result, which is the check.  True: the shell
                 checks its result with the same rules and says what is still broken and where (`fail_states`, and the card's "places still breaking a
                 rule"); that is the whole of the check run again on the result, and a part whose field is slow to evaluate pays for it many times over
                 (on a topology-optimized bracket it was ten of the thirteen minutes)
    samples      how many points of the surface it checks
    show_turned  True (the default): when the constraint left the way up of the print free and the tool found it, the result is DRAWN standing that way (z up) -- the
                 part turns on the screen, the card says "Way up of the print ... part shown turned"; False: it is drawn where it is.  The model is not turned either
                 way: `.oriented()` is the part turned to stand that way, for a print

Returns a ManufacturingResult: a body that is the part changed, coloured by the geometry change, with `.changes`, `.added`, `.removed`, `.fail_states`.
Run `manufacturing_analysis` on the result: the shell's own check is not a substitute for the maker's.

## Excluding regions from an analysis

exclude(): a region of a shape that stays exactly as it is

    part = import_model('bracket.step')[0][0]
    part = exclude(part, sphere(12, (0, 0, 6)))        # inside the sphere the part is locked
    light = shell(part, 2)                              # ... and nothing done to it afterwards touches that

exclude() cuts the shape into two fields that are always united again:

    the free field    the shape outside the region (the shape with the region cut out of it): what every later
                      operation reshapes
    the locked field  the shape inside the region, put there once and never reshaped by anything done to the shape
                      afterwards.  For a part imported from a STEP file it is the part's own exact surface, meshed
                      straight from the file (not what the import fitted) and made into a field; for any other
                      shape it is the shape itself inside the region.

An operation on an excluded shape (offset, shell, smooth, a lattice, a union, a cut, an analysis ...) works on the
whole shape, and then the locked field is put back: wherever the region is, the shape is the locked one, whatever the
operation did there.  (smooth() works on the field the shape was made from instead of the united one, whose values are
cut off at the distance to the region and would make the smoothed surface ripple round it.)  Moving, turning, scaling
and mirroring carry both fields along together.  Operations that would
tear the two apart -- copying the shape (arrays, symmetric_*, repeat, mirror_*) or deforming it (twist, bend, taper,
attract, repel ...) -- raise an ExcludedError: do them before exclude().  The optimisation (topology_optimization, of a
structure, a heat conduction or a body in a flow) reads the exclusions a shape carries as regions to keep: the
locked field stays solid, as exact as it is, and the optimised shape is excluded the same way.  The tables below say, for every function of the
library, what it does with an excluded shape; none is left out.

The region is any shape (a field that is negative inside it), as it always was.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `ExcludedError`

An operation that would separate an excluded shape's locked field from the rest of it

### `carry_locks(out, *shapes)`

`out`, made from `shapes` by something that cannot be guarded (an optimised body), is excluded the way they
are: its locked regions are the optimisation's kept ones, so it is the shape there

### `exclude(shape, *regions, source=None, quality=64, threshold=1.0)`

`shape`, with the places inside `regions` locked: the shape there stays exactly as it is, whatever is done to
the shape afterwards.  (See the top of this file for what that means; in the model tree's right-click menu it
is Combining > exclude, with the shape first and the regions after it.)

    part = import_model('bracket.step')[0][0]
    part = exclude(part, sphere(12, (0, 0, 6)), box((0, 0, 0), (5, 5, 5)))
    light = shell(part, 2)           # shelled everywhere but inside the sphere and the box

A region is any shape -- a sphere, a box, a part, a part offset by some distance, a union of those: the
places where its field is negative.  Several regions are one region, their union.

For a part imported from a STEP file the locked part is the part's EXACT surface, meshed straight from the STEP
file and made into a field (not the import's fit, which is only approximate where B-spline faces were fitted
by simple surfaces -- the red places -- or where there are threads).  For any other shape the locked part is
the shape itself, as it is now.  Either way it is a field of its own, united with the rest after every
operation; an operation that would separate them (copying or deforming the shape) raises an ExcludedError.

Without a region, a STEP part's poorly fitted places are the region (poor_fit_region), in every part that has
any:

    kitchen = import_model('step/kitchen.stp')
    kitchen = exclude(kitchen)

What you give it decides how much it works on.  The WHOLE IMPORT (what import_model returns): the region
goes into every part it touches, and the same list comes back, the parts in the same places.  One entry of the
import, kitchen[19], or one part, kitchen[19][0]: that part only.

    kitchen[19] = exclude(kitchen[19], region)

For a shape made from an imported part (cut, filled with a lattice ...) say which part its locked geometry
comes from, and exclude last, to the finished shape:

    final = exclude(light, region, source=part)

The part may have been moved, rotated, scaled or mirrored with the fieldes.stdlib transforms since the
import (the locked part follows).

quality: points per full turn of a circle in the exact STEP mesh.
threshold: for the default region, in percent of the face's size.

### `install()`

Puts the guards round the functions of the library (when the library is loaded): the functions the tables above
name are replaced by versions that look at their arguments, in the package (what `from fieldes import *`
brings into a script) and in the methods of Shape.  The library's own calls of one another, inside its modules,
are not guarded: a difference is built from an inverse and an intersection, and only the difference is asked
about the shape it is given.  A call that is given no excluded shape goes straight to the function.

### `install_methods()`

The methods of Shape that are functions of the library (shape.py binds them as it is loaded: union, offset, the
transforms ...) guarded.  Done again, it changes nothing.

### `keep_regions(*shapes)`

What an optimisation must keep solid for these shapes: the locked field of every exclusion they carry (the shape
inside its region, as it is) -- topology_optimization (of a structure, a heat conduction or a body in a flow)
adds these to its `keep` by itself, however many operations came between
exclude() and the optimisation (the locks travel with the shape).

### `locks_of(shape)`

*(no description yet)*

### `moved(prev, shape, args, kwargs)`

The locked fields and regions of `shape`, moved the way `prev` (a space transform) has just moved the shape

## Telling fields from numbers

Numbers and fields in the same place.

Anything the library takes as a number takes a FIELD as well (a Shape: a value at every point of space -- a distance, a ramp
of one, a stress result, noise ...), as in nTop.  A function that can do arithmetic with what it is given needs nothing;
the few places that must ask a question of a number (is it positive? is it zero?) use these.  A number stays a number, so
nothing is slower for a plain value; a field is worked with as the tree it is.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `highest(*values)`

The largest of numbers and fields: a number when all are numbers, else a field

### `is_field(v)`

Whether v is a field (a Shape) and not a plain number

### `lowest(*values)`

The smallest of numbers and fields: a number when all are numbers, else a field

### `nonzero(v)`

v itself when it is a field or a nonzero number, else None -- for a size that "0" turns off

### `number_or_field(v, what='a value')`

v as a float, or kept as the field it is; anything else is a TypeError that says what was expected

### `positive(v)`

A number above zero -- or a field, which is not known to be anything else

### `present(v)`

A value was given and it is not zero (a field is taken to be something)

### `returns_body(fn)`

A function that makes a body out of what it is given, which may be a field: what it returns is not of the kind
'field' even when it was made from one (a wall round a sheet, a shell)

### `returns_field(fn)`

A function that makes a field (not a body): what it returns is of the kind 'field', which the model tree shows
with a field's icon, and displayed it has an icon in the viewport that opens the field viewer (a plane coloured by it)

### `returns_lattice(fn)`

A function that makes a LATTICE (a body filled with cells): what it returns is of the kind 'lattice', which the model tree shows with
a lattice's icon.  Used in another operation it is a body like any other (the kind is not kept by arithmetic on it)

## The shape class

Python library of FielDes, built on the libfive CAD kernel
Copyright (C) 2021  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `Shape`

*(no description yet)*

#### `Shape.Shape(ptr)`

*(no description yet)*

#### `Shape.X()`

*(no description yet)*

#### `Shape.Y()`

*(no description yet)*

#### `Shape.Z()`

*(no description yet)*

#### `Shape.abs(self)`

*(no description yet)*

#### `Shape.acos(self)`

*(no description yet)*

#### `Shape.asin(self)`

*(no description yet)*

#### `Shape.atan(self)`

*(no description yet)*

#### `Shape.call(self, fn, *args, **kwargs)`

Calls the given function with self as the first argument.
This is convenient to chain transforms without deep call trees.

#### `Shape.cos(self)`

*(no description yet)*

#### `Shape.exp(self)`

*(no description yet)*

#### `Shape.get_mesh(self, xyz_min=(-10, -10, -10), xyz_max=(10, 10, 10), resolution=10, algorithm='dc')`

Converts this Shape into a mesh, and returns that mesh in
    the form of a face-vertex mesh.

Options are the same as in save_stl(). `algorithm` selects the
meshing algorithm: 'dc' (dual contouring, default, fastest),
'simplex', or 'hybrid'. The latter two are slower but more
robust for STEP-imported oracle shapes, where dual contouring
has been found to fragment meshes even when the underlying
field is provably correct at every queried point.

Returns:
verts - List of (x, y, z) vertices
tris - List of (a, b, c) indices into verts

#### `Shape.lock(self)`

*(no description yet)*

#### `Shape.log(self)`

*(no description yet)*

#### `Shape.new(op, *args)`

*(no description yet)*

#### `Shape.optimized(self)`

*(no description yet)*

#### `Shape.partial(self, axis, oracle_step=0.0, max_call_depth=1)`

The partial derivative of this field with respect to x (axis 0), y (1) or z (2), as a field of its own: the symbolic derivative, by
the rules of calculus -- not a difference of two calls, whose intervals over a cell of the renderer's are as many times too wide as
the cell is wider than the step (a field built from such a gradient makes the renderer refine a whole band round the surface).
A native field inside (a mesh's distance, noise) has no formula: with `oracle_step` above 0 its partial is the central difference over
that step; without it, None is returned.  None too for a field whose calls are nested deeper than `max_call_depth` (default 1: calls of plain
fields): the derivative of a call is three calls of partials, and costs several times what the field does for every level of nesting -- a
field made from the gradient of another, a Newton walk, the result of a remedy that was made from remedies.  Its gradient is read by differences.

#### `Shape.remap(self, x_, y_, z_)`

Performs the remapping f(x_, y_, z_)

#### `Shape.save_stl(self, filename, xyz_min=(-10, -10, -10), xyz_max=(10, 10, 10), resolution=10, quality=8)`

Converts this Shape into a mesh and saves it as an STL file.

xyz_min/max are three-element lists of corner positions
resolution is the reciprocal of minimum feature size
    (larger values = higher resolution = slower)
quality is the negative order of magnitude of maximum error

#### `Shape.sin(self)`

*(no description yet)*

#### `Shape.sqrt(self)`

*(no description yet)*

#### `Shape.square(self)`

*(no description yet)*

#### `Shape.tan(self)`

*(no description yet)*

#### `Shape.var()`

*(no description yet)*

#### `Shape.with_constant_vars(self)`

*(no description yet)*

#### `Shape.wrap(t)`

Ensures that the input argument is a Shape, converting constants

### `field_shape(ptr)`

A field a solver calculated (a stress, a temperature, a speed, a density ...), made from the tree `ptr`: a FIELD, not a body -- a value at
every point of space, which is not drawn, is not a part, and has an icon (that opens the field viewer) when it is displayed

### `shape(f)`

Decorator which converts a target function into a Shape

## Progress of a long step

A script run's progress, for FielDes's progress bar (see run_progress.hpp).

The runner (runner.py) says which top-level statement runs; the operations
inside one count how far they are: FEA solves and optimisations (in C++),
STEP imports (their own count), top-level loops (the runner counts their
items), loading a cached import (its tree files), and progress() for your
own long computations.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `begin(name, shown=None)`

shown: the name the bar shows (default: name)

### `counted(items, name='item')`

Yields items, counting them as the progress of the current step (or
of the operation it runs in); what runs for one item counts within
that item's share.  Items with no len(): counted, but how far
along isn't known.

### `end()`

*(no description yet)*

### `end_to(depth)`

Ends the operations begun deeper than depth (left open by a
progress() call, or by an error)

### `progress(fraction, text='')`

Says how far a long computation of yours is, 0..1, for the progress
bar (text: what it is doing).  Call it as often as you like, e.g.
progress(i / n, 'smoothing') in a loop; it counts within whatever
runs it (a loop's item, a step of the script).

### `report(fraction, detail='')`

*(no description yet)*

### `span(a, b)`

*(no description yet)*

### `task(name)`

with task('meshing'): ... report(f) ...  -- an operation that counts
how far it is, 0..1 (report(f)); one begun inside it counts within the share
span(a, b) gave it

## Caching

Content-keyed caches for the constructions that cost real time.

A script runs again on every edit.  Most of what it builds is a lazy expression
that costs nothing until it is evaluated, but a few constructions do real work
when they are called: meshing a shape and building the search structure of an
exact distance field, importing a mesh file, building a graph lattice, measuring
a volume.  Their results are remembered by *what they were built from* -- the
shape's expression (not its identity), the numbers, the file's path, size and
modification time -- so running the script again, or moving a slider that does
not touch them, asks nothing twice.  Change the shape, a number or the file and
the key differs: the new result is built and the old one stays until it is the
one used least.

    cache_info()      what is held: {name: (entries, hits, misses)}
    clear_caches()    forget everything (to measure, or to free the memory)

Nothing here approximates: a hit returns the very object a miss built.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `Uncacheable`

A value with no content key

### `cache_for(name, limit=8)`

The named in-memory cache (made on first use) holding at most `limit` entries, least recently used out first

### `cache_info()`

What the content caches hold: {name: (entries, hits, misses)}

### `clear_caches()`

Forgets everything the content caches hold

### `content_cached(name, limit=8, copy_result=False)`

A decorator: the function's results are remembered by the content of its arguments.
copy_result: hand out a copy of a mutable result (a dict) each time

### `is_local(key)`

Whether a problem key holds a part that only this session can recognise (nothing is kept under it for another)

### `problem_key(kind, **parts)`

A key of a whole problem -- a static analysis, a thermal analysis: what it is made of, each part by its
content -- or None when some part has no content key (then nothing is remembered)

### `shape_key(shape)`

An exact key of a shape's expression: the same expression built again -- a script run again -- has
the same key, and a different one does not (a constant differing in the seventh digit, another
imported mesh, another data field: all different).  It is the library's structural hash of the
tree, not its printed text.  A var() of the script is its number there (the key is then the same in every
session, and another when the number is another); a tree with anything else no run could recognise -- a
variable of its own, an oracle with no key of its own -- gets a key that is good in this session only
('local': nothing is kept under it for another session).  (Raises Uncacheable when the library has no
such key.)

### `value_key(v)`

A hashable key for an argument, by its content: numbers (every bit), text, sequences, shapes (by
their expression), and the library's own small objects (a material, a support, a load: by their
fields).  Raises Uncacheable for anything else

## Keeping rendered meshes (render cache)

The render cache: keep what FielDes meshes, so a shape that has not changed is on screen at once.

    part = difference(box((0, 0, 0), (40, 30, 10)), sphere(8))
    part                            # its mesh is kept: nothing to write
    part = render_cache(part, False)    # ... unless you opt out (the model tree's cache button writes this line)

Meshing is the slow part of showing a shape.  Every shape's finished mesh is saved (in FielDes's cache
folder, outside your project) once it has taken a while to make, and read back when the same shape is
shown again -- the next time you open the script, or after you changed the script and changed it back --
instead of being meshed again.  When anything about the math changes -- an operation, a number, a
var() you dragged, an imported file, the render region, the resolution -- the shape is not the same
one any more: it is meshed again, shown as usual, and the new mesh is kept.

It is ON unless you turn it off: click the cache button on a row of the model tree (or write
`part = render_cache(part, False)`) to turn it off for that shape; click again (or delete the line) for
the default.  `part = render_cache(part)` says the default aloud and keeps the mesh however quickly it was
made.  The line is the whole setting: it is saved with the script and nothing else about the script
changes.  The shape itself is not touched.

A shape is kept when everything it is made of can be recognised from one run to the next: nodes,
numbers, var()s, imported meshes and parts, lattices, fields.  A shape that is shown with the fields of
an analysis (a result coloured by stress) is not -- the analysis has caches of its own.

The kept meshes are deleted oldest first when they fill more than 4 GB (the environment variable
FIELDES_RENDER_CACHE_MB sets that); Settings > Clear the caches deletes them all (and the files of the field cache).

Every field a script builds with `name = <expression>` is kept the same way -- by the statement's text, the exact
content of what it reads, the var() numbers and the code that builds it -- so a lattice laid out on a part is not laid
out again when the script is run again, also after FielDes was closed (when every part of the field can be saved).
FIELDES_FIELD_CACHE_DIR moves the files; the details are in caching-and-performance.md.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `render_cache(shape, on=True)`

`shape`, with its finished mesh kept on disk: shown from there when the same shape is rendered
again (see the module).  Every shape is kept by default; `render_cache(shape, False)` opts it out, and
`render_cache(shape)` keeps it however quickly it was meshed.  Returns the shape itself in every
other way: it keeps its bounds, handles, colours and exact regions.

Write it as the last line of a shape's definition, after its placement and handles():

    part = render_cache(part)

(FielDes's model tree has a button for it on every shape.)

### `render_cache_key(shape)`

The key the render cache keeps `shape`'s mesh by, as text -- the same in every run of the program for the
same math, another one when anything about the math changes (an operation, a number, a var() with the
value it has now, an imported file) -- or None when the shape cannot be kept: it depends on something no
other run could recognise (a solved analysis).  (The region, resolution and quality it is meshed at are
part of the key the application uses, which is made of this and them.)

## A resolution of its own for one body

A resolution of its own for one body.

The viewport meshes the scene at one resolution (view.set_resolution): fine enough for the finest thing in it, which is
more than the rest needs.  `custom_resolution(body, resolution)` gives ONE body the resolution you say -- the number the scene's resolution
is, for this body alone: a part that is intricate is drawn finer and the rest of the scene stays coarse and quick, or a part that is big
and plain is drawn coarser than the scene.

    from fieldes import *

    view.set_resolution(2)                        # the scene: resolution 2
    gear = custom_resolution(gear, 8)             # ... except the gear, drawn as if the scene's were 8
    gear                                          # (it is shown by the line that names it, as always)

It is the SAME number as the scene's, so it is read the same way: `custom_resolution(gear, 2)` in a scene at 2 is the gear as it was, and
twice as much is twice as fine.  And it scales as the scene's does: with auto scale on (the default) a small body in a big region is
drawn finer than the number says, by how small it is, whatever it is given -- so there is no need to write a number far from the scene's
to get a picture as fine as the scene's own.  The body is meshed over the region of the scene (view.set_bounds), as every shape is.  It
changes how the body is DRAWN, not what it is: an analysis, a boolean or an export of it is the same.

The finest a body can be drawn is 2000 samples along its longest side (a longer one would be hundreds of millions of cells): a resolution
above that is not an error, the body is drawn at the finest there is.

In FielDes, right-click a body and choose "Custom resolution": the line is written under its definition, and its number is a row under
the body in the model tree like any other argument -- click it and the number is selected in the code, ready to be typed over (the bin
takes the line away).

### `custom_resolution(shape, resolution)`

`shape` drawn at a resolution of its own: `resolution` is the number the scene's resolution is (see the module), for this body alone.

shape       a body
resolution  the resolution (a number above 0), as view.set_resolution says it: the scene's own number gives the body as it was drawn,
            twice as much draws it twice as fine

Returns the body itself in every other way; only the way it is meshed for the viewport is changed.

## FielDes apps: the inputs of a script

FielDes apps: a script that people who never open the editor can run.

An engineer writes the script once and declares what the person who runs it may choose -- a radius, a model file, where to save the
result.  FielDes shows those inputs as a form (a window of its own, with a Run button, a log and the files that were made), so an
artist or a designer opens the app, fills in the form and presses Run.  The script is an ordinary FielDes script:

    from fieldes import *

    app.title('Bracket maker', 'Makes the bracket for a pipe and saves it as a mesh')
    radius = app.number('Radius', 5, min=1, max=20, unit='mm')
    material = app.choice('Material', ['steel', 'aluminium'])
    model = app.file('Model', filter='STEP files (*.step *.stp)')
    target = app.save_as('Export path', 'bracket.stl')

    part = import_model(model)
    ...
    export_mesh(part, target)

In the editor the inputs are just their defaults: `app.number('Radius', 5)` is 5, so the script runs as it always did, and the script is
tried and changed there.  File > Export as app... bundles it with the custom blocks it uses and the files it needs into one
`.fieldesapp` file; opening that file (a double click) shows the form.  The same file runs without a window:

    FielDes run bracket.fieldesapp --Radius 7 --Material aluminium

Every input is written with a plain value -- a number, a text, a list -- at the top of the script, because the form is read from the
text of the script (it is there before the script has run).  The name of an input is what the form shows; `--Radius`, `--radius` and
`--RADIUS` are the same option, and so are `Model path`, `ModelPath` and `model_path`.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `app.asset(path)`

A file that is bundled into the app: `model = import_model(app.asset('step/bracket.step'))`.  A relative path is read from
the folder of the script (and is the same inside the app); File > Export as app... copies the file in.  Returns the full path.

### `app.choice(name, options, default=None, help='')`

One of a few texts: `material = app.choice('Material', ['steel', 'aluminium'])`.  `default` is the first one when it is left out.

### `app.file(name, default='', filter='', help='')`

A file the person picks (it must exist when the app runs), returned as its full path:

    model = app.file('Model', filter='STEP files (*.step *.stp);;All files (*)')

`default` can be a file that comes with the app, `app.asset('step/bracket.step')`.  The runner can run the app again by itself
when this file changes (Watch).

### `app.flag(name, default=False, help='')`

A yes / no choice (a check box).  On the command line: `--Hollow`, `--no-Hollow` or `--Hollow yes`.

### `app.folder(name, default='', help='')`

A folder the person picks (a relative path is made under the Documents folder), returned as its full path

### `app.in_app()`

True when the script is run as an app (by the app window or `FielDes run`), False in the editor.  Write files only when it is: an
edit of the script runs it again and again in the editor, and it should only show the result there:

    if app.in_app():
        export_mesh(part, target)

### `app.number(name, default=0, min=None, max=None, step=None, unit='', help='')`

A number the person chooses: a whole number when `default` is one (and min, max, step are), else a decimal.

    radius = app.number('Radius', 5, min=1, max=20, unit='mm')

`unit` is shown after the field, `help` under it.  A value outside min..max is refused before the script runs.

### `app.output(path, label='')`

A file the script makes that the runner lists under *Outputs* when it is there afterwards (a report, a drawing):

    report = app.output('report.txt')

A relative path means the Documents folder (or the folder the command was started in).  Returns the full path.

### `app.running()`

True when the person pressed Run (or the app runs from the command line): the work is to be done.  False while the script only makes the
picture of the app's viewport -- when the app is opened, an input changes, a model was dragged -- and in the editor.  Put what takes
long, and what writes files, under it:

    if app.running():
        result = static_analysis(part, ...)
        export_results(result, target)

### `app.save_as(name, default='result', filter='', help='')`

Where the result is saved: a path the person can change, returned as a full path (a relative one means the Documents folder, or
the folder the command was started in).  The runner lists it under *Outputs* once the script has written it:

    export_mesh(part, app.save_as('Export path', 'bracket.stl'))

### `app.text(name, default='', help='')`

A text the person types (a name, a label to engrave).  Returns it as it is.

### `app.title(text, description='', version='')`

The name of the app, what it does, and its version: shown at the top of its window and in the list of recent apps.
Written once, with plain texts, at the top of the script.

### `app.viewport(models=None, *more, labels=True)`

The app window shows a viewport: a picture of what you give it, that the person can look around in and -- the models that are not
locked -- drag by their gizmo and their handles.  Write it as a statement of its own:

    app.viewport({'Bracket': part, 'Load point': pull}, support, load)

`models` is a dict of name -> model (the name is drawn over the model, in the style of the labels of boundary conditions), or a list of
models (no names); the models after it (`more`) are shown without a name.  A condition (`force(...)`, `fixed(...)`) is a model: it is
drawn on the body as in the editor.  Whatever a model has numbers to drag -- the `position`, `rotation` and `scale` of its call, `handles()`, `var()` -- the person can drag,
unless it is `lock()`ed: click a model to select it and its gizmo shows; there is no model tree and no menu.  A model that is dragged
writes its new numbers into the script the app runs, which runs again for the picture (with app.running() False), and the numbers are
there when Run is pressed.  A point is its dot: always drawn, click it to select it.  A **field** among them has an icon (with its name over it) at the place of the object it
was made from; click the icon and the field is shown on a disc, with a slider for its radius and arrows to move it.

It is a setting of the app: the editor draws nothing for it and the model tree has no row for it -- the models are in the tree as they are,
shown by their eyes; File > Preview as app shows the viewport.  `labels=False` leaves the names out.
