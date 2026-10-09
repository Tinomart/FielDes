# Library reference

Every public function and class of the FielDes library, generated from its docstrings
(`python scripts/gen_reference.py`).  `from fieldes import *` brings all of them in.

Contents: [Primitive shapes](#primitive-shapes) | [Combining shapes (CSG)](#combining-shapes-csg) | [Moving, rotating, scaling, deforming](#moving-rotating-scaling-deforming) | [Text](#text) | [Importing models](#importing-models) | [The region and resolution of imported models](#the-region-and-resolution-of-imported-models) | [Triangle meshes](#triangle-meshes) | [Handles: editing shapes by dragging](#handles-editing-shapes-by-dragging) | [Points and surfaces](#points-and-surfaces) | [Measuring a body](#measuring-a-body) | [Fields](#fields) | [Regressions and data](#regressions-and-data) | [Surfaces and offsets](#surfaces-and-offsets) | [Lattices](#lattices) | [Lattices that follow a surface](#lattices-that-follow-a-surface) | [Selecting surfaces](#selecting-surfaces) | [Structural analysis and topology optimization](#structural-analysis-and-topology-optimization) | [Seeing the boundary conditions](#seeing-the-boundary-conditions) | [Thermal analysis and thermal topology optimization](#thermal-analysis-and-thermal-topology-optimization) | [Fluid flow analysis](#fluid-flow-analysis) | [Caching](#caching) | [Keeping rendered meshes (render cache)](#keeping-rendered-meshes-render-cache) | [A resolution of its own for one body](#a-resolution-of-its-own-for-one-body)

## Primitive shapes

Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.shapes

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

A box with the given bounds, which will stay creased if offset

### `box_centered(size, center=(0, 0, 0))`

A box with the given size and (optional) center, with edges that
will stay sharp if offset.

### `box_exact(a, b)`

A box with the given bounds with a Euclidean distance metric

### `box_exact_centered(size, center=(0, 0, 0))`

A box with the given size, centered around the given point,
with a Euclidean distance metric

### `box_mitered(a, b)`

A box with the given bounds, which will stay creased if offset

### `box_mitered_centered(size, center=(0, 0, 0))`

A box with the given size and (optional) center, with edges that
will stay sharp if offset.

### `circle(r, center=(0, 0))`

A 2D circle with the given radius and optional center

### `cone(radius, height, base=(0, 0, 0))`

A cone defined by its radius, height, and (optional) base location

### `cone_ang(angle, height, base=(0, 0, 0))`

A cone defined by its slope angle, height, and (optional) base location

### `cone_ang_z(angle, height, base=(0, 0, 0))`

A cone defined by its slope angle, height, and (optional) base location

### `cone_z(radius, height, base=(0, 0, 0))`

A cone defined by its radius, height, and (optional) base location

### `cube(a, b)`

A box with the given bounds, which will stay creased if offset

### `cube_centered(size, center=(0, 0, 0))`

A box with the given size and (optional) center, with edges that
will stay sharp if offset.

### `cylinder(r, h, base=(0, 0, 0))`

A cylinder with the given radius and height, extruded from the
(optional) base position.

### `cylinder_z(r, h, base=(0, 0, 0))`

A cylinder with the given radius and height, extruded from the
(optional) base position.

### `emptiness()`

A value which is empty everywhere

### `extrude_z(t, zmin, zmax)`

Extrudes a 2D shape between zmin and zmax

### `gyroid(period, thickness)`

A volume-filling gyroid with the given periods and thickness

### `half_space(norm, point=(0, 0, 0))`

A plane which divides the world into inside and outside, defined by its
normal and a single point on the plane

### `polygon(r, n, center=(0, 0))`

A polygon with center-to-vertex distance r and n sides

### `pyramid_z(a, b, zmin, height)`

A pyramid defined by its base rectangle, lower Z value, and height

### `rectangle(a, b)`

A rectangle with the given bounding corners

### `rectangle_centered_exact(size, center=(0, 0))`

An exact-field rectangle at the (optional) center

### `rectangle_exact(a, b)`

A rectangle from an exact distance field

### `ring(ro, ri, center=(0, 0))`

A 2D ring with the given outer/inner radii and optional center

### `rounded_box(a, b, r)`

Rounded box with the given bounds and radius (as a 0-1 fraction)

### `rounded_cube(a, b, r)`

Rounded box with the given bounds and radius (as a 0-1 fraction)

### `rounded_rectangle(a, b, r)`

A rectangle with rounded corners

### `sphere(radius, center=(0, 0, 0))`

A sphere with the given radius and (optional) center

### `torus(ro, ri, center=(0, 0, 0))`

A torus with the given outer radius, inner radius, and (optional) center

### `torus_z(ro, ri, center=(0, 0, 0))`

A torus with the given outer radius, inner radius, and (optional) center

### `triangle(a, b, c)`

A 2D triangle

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
.3mf, .glb, .gltf) and returns a list of (shape, (lower corner, upper corner)), one entry per part -- one for a mesh --
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

Imports a STEP file (.step, .stp) or a triangle mesh (.stl, .obj, .ply, .3mf, .glb, .gltf) as a list of
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
file_units  what the numbers of a mesh file mean ('mm' if it does not say: STL, OBJ and PLY do not; 3MF does,
            glTF is metres) -- for mesh files only
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
(ASCII or binary), 3MF and glTF (.glb / .gltf) files.

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

## Handles: editing shapes by dragging

Handles: edit a shape -- an imported part too -- by dragging in FielDes's viewport.

Two ways of editing by dragging work together, and the first has priority where they meet:

  the gizmo    the part's own move arrows, rotation rings and scale knobs: `handles()` writes, under the shape's
               definition,

                   part = handles(part, move=(var(0), var(0), var(0)),
                                  rotate=(var(0), var(0), var(0)),
                                  scale=(var(1), var(1), var(1)))

               so what the gizmo does is ordinary script text, like any dragged var().  The shape is `part`
               scaled about its centre, rotated (x, then y, then z; degrees) and moved.  When the gizmo is shown
               is the `mode` of that line (the model tree's gizmo button, key E, goes through them):

                   'click'   (the default) the gizmo is shown while the shape is selected, i.e. after you click it
                   'never'   it is never shown, so that it cannot get in the way
                   'always'  it is shown on the shape whether it is selected or not

  handles      FielDes's own handles: hover a surface of the shape and drag it.  This is always there, whatever
               the mode of the gizmo.  Any shape with `var()` numbers has them; for a shape written with plain
               numbers -- a primitive, or a part imported from a STEP file, which is made of primitives --
               `expose()` makes the numbers that place its surfaces variables:

                   part = expose(part, [var(6), var(40), ...])

               Dragging a face changes the number of that face (a plane's position, a radius) in the script
               text.  FielDes writes the line itself when you select the shape.

A lock button beside it (key R) is a separate switch: a locked shape cannot be dragged at all, neither by the
gizmo nor by its surfaces, and keeps its gizmo mode, which comes back when it is unlocked:

               part = lock(part)

Nothing is stored anywhere but in the script: delete the `expose(...)` and `handles(...)` lines of a part
(FielDes's Reimport does) and it is the file's version again; the `lock(...)` line only keeps it from being
dragged. 

### `expose(shape, values)`

A shape whose surfaces can be dragged: the numbers that place its
surfaces -- a plane's position, a radius, the faces of a box -- are
replaced by `values`, in the order of their appearance in the shape
(FielDes writes them as var(...) with the shape's own numbers, which
makes them draggable: hover a surface, drag it).  Orientations and the
placement of the whole part are not exposed, so a dragged face
moves and neither turns nor takes the rest of the part along.

With the shape's own numbers the result is the very same shape.  If the
shape has a different number of them (the part was changed in the STEP
file) it raises ValueError: reimport the part.

### `exposed_values(shape)`

The numbers expose() makes variables, in its order, as the text to write them as

### `forget_literals()`

A script was opened: the numbers the last run saw in the calls of the old one say nothing about this one

### `handles(shape, move=(0, 0, 0), rotate=(0, 0, 0), scale=(1, 1, 1), about=None, mode=None)`

Scales, rotates and moves a shape, with the gizmo FielDes shows on it.

scale: (x, y, z) factors about the shape's centre, `rotate`: (x, y, z)
rotations in degrees about the same centre (x first, then y, then z),
`move`: (x, y, z) translation.  Write the numbers as var(...) to drag
them: FielDes's model tree does that for you.
about: the centre (default: the middle of the shape's box)
mode: when FielDes shows the gizmo: 'click' (the default) while the shape is selected, 'never', or
'always'.  The shape's own surfaces (see expose()) can be dragged whatever the mode, and the gizmo has
priority where it is shown.
To make a shape undraggable, lock it: `shape = lock(shape)`.

### `lock(shape)`

`shape`, locked: FielDes does not let it be dragged -- neither by the gizmo nor by its surfaces -- until the
line is deleted.  It is the shape itself in every other way (its bounds, handles, colours and exact regions
stay), and the way of editing it had (gizmo or handles) comes back when it is unlocked.

Write it under a shape's definition, with its handles() and expose() lines:

    part = lock(part)

(FielDes's model tree has a lock button on every shape, and the key R toggles it.)

## Points and surfaces

Points and surfaces: models of their own kind, and how every kind is drawn.

    p = point(10, 0, 5)                    a point: not drawn, its gizmo shows where it is and moves it
    s = plane((0, 0, 8), (0, 0, 1))        a surface: an open sheet, the zero set of a field, with no body behind it
    wavy = wave_surface(2, 10)             z = 2 sin(2 pi x / 10)

A point is usable where a field function takes a position (distance_to_point(p), attractor(p, ...)) and gives its
coordinates as p.xyz.  A surface is a field like any other (negative on one side, positive on the other), so it works
wherever a field does: thicken(s, 1) makes it a wall, lattice_surface_conform(s, ...) lays a lattice on it.

What is drawn for each kind of model (Shape._display calls displayed() below):
    a 3D shape and a simulation   as they are
    a 2D shape (no z)             flat, in the z = 0 plane (a thin slab: it has no height)
    a surface                     a thin sheet
    a point                       nothing: a point is not drawn, its gizmo is all there is of it
    a field                       nothing in the viewport: selected in the model tree it is shown by the section viewer,
                                  which colours a plane through the render region by the field (move the plane to see
                                  the field in 3D)

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `Point`

A point: p.xyz is its coordinates (numbers or fields).  It is **not drawn**: select it and its gizmo shows where it is.  (As a field
it is a tiny ball, which gives it a place for its gizmo and for the bounds of what is made of it: nothing of it is painted.)

### `Surface`

An open surface: the zero set of a field, with no body behind it (negative on one side, positive on the other)

### `cylinder_surface(radius=5.0, axis='z', center=(0, 0, 0))`

The surface of an infinite cylinder along an axis: a surface, negative inside

### `displayed(shape)`

What FielDes draws for a shape (see the top of this file)

### `plane(point=(0, 0, 0), normal=(0, 0, 1))`

A plane through `point`, positive on the side `normal` points to: a surface

### `point(x=0.0, y=0.0, z=0.0)`

A point at (x, y, z).  Its coordinates are numbers or fields.  It is not drawn: select it, and its gizmo shows where it is

### `sphere_surface(radius=10.0, center=(0, 0, 0))`

The surface of a sphere (without its inside): a surface, negative inside

### `surface(field)`

An open surface from any field: where the field is 0.  (A body's field as a surface is its skin.)

### `wave_surface(amplitude=2.0, period=10.0, axis='x', height=0.0)`

A wavy sheet z = height + amplitude sin(2 pi s / period) along an axis (s = x or y): a surface, positive above it

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

The smallest box, aligned with the axes, that holds `body`: a body of its own (`box_exact` between the body's lowest and highest
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

### `distance_to_point(p)`

Euclidean distance to a point (its coordinates may be fields: a point that is where a field says)

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

### `exact_distance(shape, bounds=None, resolution=None, margin=0.0)`

The exact signed distance field of a shape (mm): the shape is meshed
and the distance to that mesh is used.  Many shapes built with
booleans, blends or warps have fields that are only roughly a
distance (e.g. flat, square-cornered outside a box's edges); after
this, offsets and shells are uniform and depth_below() is a true
depth.  resolution: mesh samples per mm (default ~150 along the
longest side); margin: extra room around the bounds for offsets.
(A shape with var() numbers is meshed with the numbers they have in
the script, and the result is remembered by them.)

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

### `fillet(shape, radius, bounds=None, resolution=None)`

Fills every concave (inside) edge and corner of a shape with a
fillet of the given radius (a number or a field): the shape is grown by the radius and
shrunk back, with exact distances both times

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
other: it is not drawn (select it and the field viewer shows it), it goes wherever a number goes (`offset(part,
waves)`), a lattice's cell size, a load's profile.  low_level_body() is the same for a body; body_from_field() makes a
body of a field.  In FielDes: right-click, New field, low_level_field, writes the function and the call for you.

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

### `noise_field(scale=10.0, octaves=4, seed=1, gain=0.5, lacunarity=2.0, amplitude=1.0)`

Smooth random variation (Perlin noise), about -amplitude..amplitude (`scale` and `amplitude` are numbers or fields):
`scale` is the size (mm) of the largest features, each octave adds
detail half as large and `gain` as strong.  A different seed gives a
different pattern.  E.g. an organic surface texture:
part - 0.3 * noise_field(4)

### `normal_field(shape)`

The unit surface normal of a shape (pointing out of it), as three
fields (nx, ny, nz); defined everywhere, meaningful near the surface

### `normalize(field, lo, hi)`

Maps [lo, hi] to [0, 1] (clamped)

### `offset_by(shape, distance)`

Offsets a shape by a distance -- which may be a field, e.g. grow a
part by 0.02 mm per MPa of stress: offset_by(part, 0.02 * stress)

### `offset_exact(shape, distance, bounds=None, resolution=None)`

A uniform offset of any shape: grows it by `distance` mm everywhere
(shrinks it for a negative distance), measured along true normals,
so edges and corners get round, not stretched.  The distance is a number or a field (an offset that
is more here than there)

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

### `radial_field(center=(0, 0, 0), axis='z')`

Distance from an axis through `center` (cylindrical radius, mm); the centre's coordinates may be fields

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

### `round_edges(shape, radius, bounds=None, resolution=None)`

Rounds every convex (outside) edge and corner of a shape with the
given radius (a number or a field): the shape is shrunk by the radius and grown back, with
exact distances both times.  resolution (samples per mm) sets how
finely the surfaces are followed; about 4 / radius or finer.

### `sample_grid(field, lower, upper, n=20)`

Values on an n x n x n grid of cell centres over a box (a flat list,
x fastest) together with the points

### `shell_centered(shape, thickness)`

A shell straddling the surface (half inside, half outside)

### `shell_exact(shape, thickness, side='inside', bounds=None, resolution=None)`

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
Unbounded along z -- cut it to length, e.g. with extrude_z.  Kept
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

Every lattice operation takes a CELL: the thing the lattice is made of.  Five functions make one --

    cell_periodic(kind)              a standard cell that repeats: a strut cell (octet, bcc, kelvin ...), a TPMS
                                     (gyroid, schwarz_p ...) or a planar pattern (hexagon ...)
    cell_non_periodic(kind)          cells that do not repeat: 'voronoi' (a foam) or 'delaunay' (a stochastic truss)
    cell_custom(region, geometry)    your own cell of ANY geometry: a box for the extent of the cell and a field for
                                     what is in it -- the cell is their intersection, and repeats on any cell map
    cell_custom_truss(nodes, beams)  your own strut cell: beams between nodes in the unit cube
    cell_custom_tpms(equation)       your own triply periodic surface, from its equation

-- and lattice(), lattice_surface_conform(), strut_lattice(), tpms() ... take it as their `cell`, and nothing else: a
name such as 'gyroid' is not a cell, cell_periodic('gyroid') is.  The cell is only WHAT the lattice is made of; how thick
it is, how big, and where it goes (a body, a surface, a cell map) are the operation's.

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

    # a foam that does not repeat
    foam = lattice(part, cell_non_periodic('voronoi'), cell_size=8, radius=0.5)

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

### `FoamCell`

Cells that do not repeat (see cell_non_periodic)

### `LatticeCell`

What a lattice is made of (see the module): made by cell_periodic(), cell_non_periodic(), cell_custom(),
cell_custom_truss() or cell_custom_tpms(), taken by every lattice operation as its `cell`.
.family      'strut' (beams between nodes), 'tpms' (a periodic surface), 'planar' (a 2.5D pattern), 'shape'
             (any geometry in a box, see cell_custom) or 'foam' (cells that do not repeat)
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

A strut unit cell of your own: beams between nodes in the unit cube
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

region      the box the cell is: box(...), box_exact(...), cube(...) -- any box, its faces are the cell's
            faces and its size the cell's size
geometry    the field in it: any shape -- spheres, rods, a TPMS, a boolean of those ... -- that may reach
            out of the box (only what is inside is the cell)
check       warn when the cell does not tile: what the geometry does on a face of the region has to be what
            it does on the opposite face (a rod that leaves through one face must come in through the other,
            a sphere on a corner must be on all eight)

    cell = cell_custom(box((0, 0, 0), (10, 10, 10)),
                       union(sphere(3, (5, 5, 5)), cylinder_z(1.0, 20, (5, 5, -5))))
    lattice(part, cell)                                    # cells of 10 mm, as modelled
    lattice(part, cell, cell_size=6)                       # the cell scaled to 6 mm
    lattice(part, cell, cell_map=cylindrical(cells_around=12))

The cell is yours as modelled: thickness=, radius= or density= do not apply (put the thickness in the
geometry).  cell.solid() is the cell on its own, to look at it.  (Beams between nodes: cell_custom_truss().
A triply periodic surface from its equation: cell_custom_tpms().  The arguments nodes, beams, mirror, equation
and shape of the old cell_custom are gone: `**removed` is only there to say so, and what to write instead.)

### `cell_custom_tpms(equation, name='custom')`

A triply periodic surface of your own, from its equation f(a, b, c): a, b, c are the position in the cell as
phases (2 pi per cell).  Write it with + - * / and the methods .sin() .cos() .sqrt() .square() of a, b, c, e.g.
a gyroid:

    cell = cell_custom_tpms(lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos() + c.sin() * a.cos())
    lattice(part, cell, cell_size=8, thickness=0.8)

Its value is turned into a distance in mm (its gradient is followed), so thickness= is a real wall thickness.
(Any other geometry as a cell: cell_custom(region, geometry).)

### `cell_custom_truss(nodes, beams, mirror='')`

A strut cell of your own: beams between nodes in the unit cube (coordinates 0..1 across the cell, scaled by
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

### `cell_non_periodic(kind='voronoi', relax=2, seed=1)`

Cells that do not repeat: points at random about a cell size apart, joined into a graph that fills the body
(or lies on a surface).

kind    'voronoi' (the edges of the Voronoi cells: a foam) or 'delaunay' (the Delaunay edges: a stochastic truss)
relax   iterations that make the cells more even
seed    another number is another random pattern

    lattice(part, cell_non_periodic('voronoi'), cell_size=8, radius=0.5)

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
(Your own cell: cell_custom(region, geometry), cell_custom_truss(), cell_custom_tpms().  Cells that do not
repeat: cell_non_periodic().)

### `cylindrical(origin=(0, 0, 0), axis='z', cells_around=None, radius=None, rotation=None)`

Cells that wrap around an axis: radial, around, and along the axis.
Give the number of cells around (cells_around=) or the radius at
which cells are cell_size wide (radius=).

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
    cell_non_periodic('voronoi' | 'delaunay'), cell_custom(region, geometry), cell_custom_truss(nodes, beams)
    or cell_custom_tpms(equation).  Default: cell_periodic('gyroid')
cell_size: mm, or (sx, sy, sz), or a FIELD (cells bigger here and smaller there).  (Default 10 mm; for a
    cell_custom(region, geometry) cell the size of its region, so that it comes out as you modelled it -- a
    larger or smaller size scales the cell.)  A cell size field is blended from lattices of cells a factor 2
    apart (up to a factor of 16 in all): the cells are exact where the field is one of those sizes and a blend of
    the two next to it between them
thickness: the member size of every cell -- the wall of a sheet TPMS, the diameter of the beams of a strut
    or non-periodic cell (radius= is the same thing for beams, half of it); offset (network TPMS), wall
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
    layer = lattice_surface_conform(wave, cell_periodic('bcc'), within=box_exact((-30, -20, -14), (30, 20, 14)),
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
                TPMS: gyroid, schwarz_p ...), cell_non_periodic(...) for a graph of random cells laid on the
                surface, or cell_custom_truss(nodes, beams).  Default: cell_periodic('octet')
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
thickness   the thickness of the cell's members, mm: the diameter of the beams of a strut cell or a non-periodic cell,
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
one, Operation > Surfaces > surface_from_bodies: the first selected is the body, the others are what it is met by.

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
own (constant in it), and the fields are read anywhere in the mesh.  Everything
of Result: fields as shapes, .range(), .element_range(), ...

#### `TetResult.element_text(self)`

What the analysis is made of, and how many of them

### `TopologyResult`

The result of topology_optimization():
.density     a field, 0 (no material) .. 1 (solid), in the part
.shape()     the optimized part, keeping the volume fraction asked
             for (shape(threshold=0.5): where density > 0.5)
.compliance  compliance (N mm) at each iteration -- lower is stiffer
.densities   the density field after each iteration (tetrahedral
             optimizations)
.volume_fraction, .iterations, .seconds
.pieces      how many separate pieces the optimized part is in (tetrahedral optimizations; more than
             one is warned about when the result is made: try a higher volume_fraction)
.symmetry    the planes the design was kept symmetric about: {'z': 0.0} (see the symmetry argument), {} if none
.verify()    a static analysis of the optimized part (stresses) --
             a list, one per load case, when there are several
Stated on its own, FielDes shows the optimized part coloured by the
density, and the result card steps through the iterations: the part
as it was after each one (slider, play / pause).

#### `TopologyResult.keep_threshold(self, samples=40)`

The density above which the part keeps the volume fraction that
was asked for.  (The density field is partly grey -- between 0
and 1 -- so cutting at 0.5 kept only 9-15 % of a block asked to
keep 25 %: a much weaker part than the optimised one.)

#### `TopologyResult.shape(self, threshold=None)`

The optimized part: the original part where the density is above
`threshold` (a smooth surface through the density field).  By
default the threshold that keeps the volume fraction asked for
(see keep_threshold); 0.5 cuts the density field in the middle.

#### `TopologyResult.verify(self, threshold=None, element_size=None)`

A static analysis of the optimized part with the same supports,
loads and material -- with several load cases, a list of them,
one per case

### `colored(shape, field, range=None, label=None, colormap='turbo')`

`shape`, displayed in FielDes coloured by `field` (another Shape,
e.g. an FEA result) with a colour map; hovering the model shows the
value.  range=(lo, hi) fixes the colour scale (default: the field's
range on the part).  Anywhere else it is simply `shape`.

### `fixed(*regions, region=None, x=True, y=True, z=True)`

A support: the part is held in place wherever it lies inside `region` (a Shape: a body, a field, a selected surface).  Any number
of regions can be given, one argument after the other -- fixed(a, b) holds the part where either is.

region  where the part is held: a body, a field or a selected surface (the same as the first argument)
x, y, z  False leaves that direction free (a sliding support)

### `force(*args, region=None, vector=None, profile=None)`

A load: the total force (fx, fy, fz) in N, spread evenly over the
part's surface inside `region` (a Shape).  force(region, (0, 0, -100)) and
force(region, 0, 0, -100) and force(region=region, vector=(0, 0, -100)) are the same.  Any number of regions can be given, one
argument after the other, before the force: force(a, b, (0, 0, -100)) loads the part where either is.

region   where the part is loaded: a body, a field or a selected surface (the same as the first argument)
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
element_size mm (default: about 40 elements along the longest side)
element      'tet' (default), 'hex' or 'hex_basic': see the module
             documentation

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
           side over 60)
element    'tet' (default: tetrahedra that follow the part's surface),
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

### `topology_optimization(part, supports, loads, material=Material('steel', E=200000 MPa, nu=0.3), volume_fraction=0.3, element_size=None, iterations=100, filter_radius=None, keep=None, avoid=None, grow=0.0, extrude=None, penalty=3.0, sharpness=16.0, symmetry='auto', bounds=None, tolerance=1e-05, cache=True, element='tet')`

Topology optimization: the stiffest part that uses `volume_fraction`
of the material of `part` (the design space), for the `supports` and
`loads` (as in static_analysis).

loads: force(...) / gravity(...) items, one or a list -- or several LOAD CASES, a list of such lists:
        loads=[[push], [pull, gravity()]].  Each case acts on its own and the part is made stiff for all of them at once
        (the sum of their compliances is minimised): a part that is pushed in use and pulled in assembly.
supports: fixed(...) items, one or a list -- the same for every load case; or a list of lists, one for each load case
        (a part that is held here in one use and there in another): topology_optimization(part,
        supports=[[base_a], [base_b]], loads=[[push], [pull]]).
symmetry: 'auto' (default), None, 'x' / 'y' / 'z' or several ('xz'), or {'z': 0.0} with the plane's position.  A
        design is kept symmetric about a plane when the part is, and so are its supports, its loads and its keep /
        avoid regions: left to itself a symmetric problem does not stay symmetric -- the mesh of a symmetric part is
        never exactly symmetric, the small difference grows, and one of two members that do the same job takes the
        other's material.  'auto' finds the planes through the middle of the part (x, y, z) that the whole problem is
        symmetric about and keeps the design symmetric about them (the output says which); None leaves it alone; 'z' asks
        for the plane z = the middle of the part, a dict for a plane of your own.  (Tetrahedral optimizations.)

keep:   regions (Shapes, or a list) that must stay solid -- e.g. bolt
        bosses, mounting faces; the material around supports and
        loads always stays
avoid:  regions that must stay empty
grow:   mm (default 0: the design stays inside the part).  The part may
        also thicken OUTWARDS by up to this much, wherever that makes it
        stiffer -- sections that are too thin grow, material that carries
        nothing goes.  The design then starts as the part, and the
        volume_fraction is of the part's own volume (1 spends the same
        material, 1.2 spends 20 % more; the part is not grown round the
        supports and loads, which stay where they are)
extrude: 'x', 'y' or 'z' -- the design is the same all along that
        axis (outside the keep / avoid regions): a profile to
        extrude, or to cut right through from one side
iterations: at most this many design updates (default 100): it stops sooner when the
        design has settled (from the 15th on, once the sharpness has been reached, when the design hardly
        changes or the compliance stays flat for 5 iterations).  It also sets the pace of the sharpening
        (it reaches its full steepness at three quarters of the iterations, however many there are, so even a
        short run ends with a crisp design).  It sets the size of the steps too (there is no step to
        choose): how far a density may move in one iteration starts large -- the fewer the iterations, the larger -- and is
        smaller as they go, and within that it grows while the compliance falls as the sensitivities predicted and
        shrinks when it does not
tolerance: how exactly each solve is made (default 1e-5)
sharpness: how crisp the design is (default 16; tetrahedral
        optimizations).  The density is pushed towards 0 and 1 more and
        more as the iterations go on, up to this much: the part ends up
        solid or empty, not grey, and nothing wanders in at the end.
        1 leaves the density as the filter makes it (soft edges, a
        third of the part grey)
element_size:  mm (default: 40 elements along the longest side; the
        optimization solves the analysis ~30-100 times)
filter_radius: the smallest member size scale, mm (default 1.5
        elements)
element: 'tet' (default: tetrahedra that follow the part's surface, a density in each),
        'hex' or 'hex_basic' (a regular grid of hexahedra)

Returns a TopologyResult: .density (a field), .shape() (the
optimized part), .compliance, .verify().  An unchanged problem is
cached, so re-running a script is instant.

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

    # the material layout (30 % of the part) that keeps the heat input coolest: the same four inputs
    design = thermal_topology_optimization(part, [...], [...], [...], [...], volume_fraction=0.3)
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
filled at the surface), and a preconditioned conjugate-gradient solve.  Thermal
topology optimization designs on the voxel grid only.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.

### `ThermalResult`

The solved thermal analysis.  Fields (Shapes whose value is the
result at each point, usable in any expression): temperature,
heat_flux (magnitude, W/mm^2), qx, qy, qz.  Summary: max_temperature,
min_temperature, max_heat_flux, heat_in, heat_out (W: through the
fixed temperatures and by convection -- they balance), elements,
iterations, seconds.

#### `ThermalResult.range(self, field='temperature')`

(min, max) of a field over the part

### `ThermalTopologyResult`

The result of thermal_topology_optimization():
.density      a field, 0 (no material) .. 1 (solid), in the part
.shape()      the optimized part, keeping the volume fraction asked
              for (shape(threshold=0.5): where density > 0.5)
.temperature  the heat-weighted mean temperature of the heat inputs
              at each iteration -- lower is better
.volume_fraction, .iterations, .seconds
.verify()     a thermal analysis of the optimized part

#### `ThermalTopologyResult.verify(self, threshold=None, element_size=None)`

A thermal analysis of the optimized part with the same boundary
conditions and material

### `convection(*args, region=None, coefficient=None, ambient=None)`

The part's exposed surface inside `region` (any number of regions, one argument after the other, then the coefficient and the
ambient temperature) exchanges heat with an ambient temperature.

region       where the part exchanges heat: a body, a field or a selected surface (the same as the first argument)
coefficient  h in W / (mm^2 K): still air ~5e-6 - 25e-6, forced air ~25e-6 - 250e-6, water ~500e-6 - 1e-2.  A number, or a field --
             its value at each point of the surface (tetrahedral elements)
ambient      the ambient temperature (degrees C; 20 when it is not given): a number, or a field

### `fixed_temperature(*args, region=None, value=None)`

The part held at temperature `value` wherever it lies inside
`region` (a Shape; any number of regions can be given, one argument after the other, then the value:
fixed_temperature(a, b, 20)).

region  where the part is held: a body, a field or a selected surface (the same as the first argument)
value   the temperature (degrees C): a number, or a field -- the temperature at each point (tetrahedral elements)

### `heat_generation(*args, region=None, watts=None, profile=None)`

A total power (W) generated inside the part, spread evenly through
its volume inside `region` (a Shape; any number of regions, one argument after the other, then the power) -- e.g. a resistive heater, or
electronics potted in the part.

region   where the heat is generated: a body or a field (the same as the first argument)
watts    the total power in watts
profile  a field -- spread in proportion to it instead of evenly

### `heat_input(*args, region=None, watts=None, profile=None)`

A total power (W) put into the part, spread evenly over its surface
inside `region` (a Shape; any number of regions, one argument after the other, then the power); negative takes heat out.

region   where the heat goes in: a body, a field or a selected surface (the same as the first argument)
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

### `thermal_topology_optimization(part, fixed_temperatures, heat_inputs, heat_generations, convections, material=Material('aluminium', E=69000 MPa, nu=0.33), volume_fraction=0.3, element_size=None, iterations=60, filter_radius=None, keep=None, avoid=None, extrude=None, penalty=3.0, move=0.2, bounds=None, conductivity=None, tolerance=1e-06, cache=True, element='hex')`

Thermal topology optimization: the material layout, using
`volume_fraction` of `part` (the design space), that keeps the heat
coolest where it goes in -- the heat-weighted mean temperature of
the heat inputs is minimised -- for the given boundary conditions
(as in thermal_analysis).  The rest of the design space is taken as
empty (a thousandth of the material's conductivity).

Heat inputs (heat_input, heat_generation) are needed; the
fixed_temperature regions are the heat sinks.  Convection follows
the design: it cools the optimized part's surface wherever it lies
inside a convection region -- give the region as the air around
and inside the design space, and fins grown into it are cooled.
The classic "volume-to-point" problem -- heat generated all through
a part, one small sink -- grows branching conductor trees.

extrude: 'x', 'y' or 'z' -- the design is constant along that axis
        (outside the keep regions), every hole a channel right
        through: an extruded or pin-fin heat sink.  For air cooling
        use it: with one heat transfer coefficient everywhere, a
        closed pocket deep in the part would count as cooled like an
        open face.

keep:   regions (Shapes, or a list) that must stay solid; the
        material at fixed temperatures and heat inputs always stays
avoid:  regions that must stay empty
element_size:  mm (default: 40 elements along the longest side)
filter_radius: the smallest member size scale, mm (default 1.5
        elements)

Returns a ThermalTopologyResult: .density (a field), .shape() (the
optimized part), .temperature (per iteration), .verify().  An
unchanged problem is cached, so re-running a script is instant.

## Fluid flow analysis

Fluid flow analysis of FielDes shapes: incompressible laminar flow, steady or in time, around a body.

    from fieldes import *

    duct = box_exact((0, 0, 0), (80, 40, 4))             # the DOMAIN: the fluid, and the place the body is in
    post = cylinder_z(5, 6, (24, 20, -1))                # the BODY the flow goes around
    result = fluid_analysis(
        post,
        duct,
        inlets=[inlet(box_exact((-1, -1, -1), (0.01, 41, 5)), speed=4)],                             # mm/s, in
        outlets=[outlet(box_exact((79.99, -1, -1), (81, 41, 5)), pressure=0)],                       # MPa
        boundaries=[],                                                                               # (none: the rest of the surface is wall)
        fluid=water, element_size=1.0)
    result                                               # the flow (FielDes: the fluid coloured by the speed,
                                                         # streamlines with moving particles; the result card)
    print(result.wall_force[0], 'N')                     # the force on the walls, the body's: its drag
    thicker = part + 0.02 * result.pressure              # results are fields like any other

It takes what flow_topology_optimization takes -- the body, the domain, the three kinds of boundary conditions, the fluid -- and
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

The result of flow_topology_optimization(): the body made best in the flow.
.level          a field (mm, positive inside the body, its boundary the zero level: a smooth
                level set on the flow's mesh); .levels the same after each iteration (the first
                the body as given, the last the final body)
.shape(iteration=None)  the optimised body, or the body after an iteration
.fluid_shape(iteration=None)  the fluid around it (the domain with the body taken out)
.drag, .lift    N, per iteration, as the optimiser's model sees them (the body a friction in the
                flow), the last the final body's; .flow_direction, .lift_direction
.flow           the REAL flow around the final body (a fluid_analysis of fluid_shape() with the
                same conditions, the body a wall at rest): fields, numbers, streamlines();
                .real_drag, .real_lift its force on the walls along the two directions
.model_flow     the optimiser's own flow (the body as a friction), .model_flow.steps[k] the flow
                around the body of iteration k
.body, .domain, .region, .volume (mm^3 of the final body), .iterations (accepted), .seconds,
.steps_back (steps undone because they raised the objective), .stopped (why the run ended)
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

### `flow_topology_optimization(body, domain, inlets, outlets, boundaries, fluid=Fluid('water', density=1e-09 t/mm^3, viscosity=1e-09 MPa s), objective='drag', volume=1.0, region=None, keep=None, avoid=None, element_size=None, iterations=40, filter_radius=None, darcy=0.1, extrude=None, symmetry='auto', flow_direction=None, lift_direction=None, bounds=None, cache=True)`

Shape optimisation of a body in a flow: `body` (a Shape, the solid) sits in `domain` (the fluid domain
it is in, a Shape that holds the body's place too) with the flow's `inlets`, `outlets` and
`boundaries` (inlet(...), outlet(...), wall(...) and slip(...) items, as for fluid_analysis); the optimiser changes the body's shape, and
topology, to make it best in the stream.

objective   'drag' (the least force along the flow), 'lift' (the most force across it), or
            (w_drag, w_lift): w_drag * drag - w_lift * lift is minimised
volume      what the body may use of its own volume: a number keeps it (1.0: the same volume), a pair
            (least, most) bounds it (0.5, 1.5); None for no bound on that side
region      where material may be (a Shape; default: anywhere in the domain); the body only shrinks,
            grows and moves inside it
keep        regions that stay solid (a Shape, or a list) -- a mounting, a shaft
avoid       regions that stay fluid
element_size  mm (default 40 elements along the longest side); the flow is solved once per iteration,
            with its adjoint, so it costs about two flow analyses per iteration
iterations  at most.  It stops sooner when the objective has stopped improving (over the last five designs that were kept it
            fell by less than half a percent).  It sets the size of the steps too (there is no step to choose): how far the
            boundary may move in one iteration is one element at most -- half an element when 48 iterations or more are
            allowed -- and grows while the objective falls as the sensitivities predicted; a step that raises the objective
            is taken back and halved, so the objective never rises
filter_radius  the level set's smoothing radius, mm (default 1.5 elements): the smallest feature
darcy       the solid's permeability relative to the element: its friction is mu / (darcy h^2), the
            flow penetrates it by about sqrt(darcy) elements (0.1 by default: a third of an element)
extrude     'x', 'y' or 'z': the body is the same all along that axis (a 2D shape through a slab)
symmetry    'auto' (default), None, 'x' / 'y' / 'z' or several ('xz'), or {'y': 20.0} with the plane's position.  A design is kept
            symmetric about a plane when the whole problem is -- the fluid domain, the body, the regions and the boundary conditions,
            with the flow along the plane: left to itself a symmetric problem does not stay symmetric (the mesh of a symmetric domain
            is never exactly symmetric, and the small difference grows into a crooked nose).  'auto' tries the planes through the
            middle of the domain
flow_direction, lift_direction  (dx, dy, dz): the drag and lift directions (default: the inlets' mean
            direction, and perpendicular to it in the plane of the domain's two long axes)

The body is a level set at the mesh's nodes (a smooth field whose zero level is the boundary, so
the body's edge is placed to a fraction of an element and stays smooth); each element's share of
the body is the exact fraction of it inside that boundary and sets its friction in the
Navier-Stokes flow (Borrvall & Petersson's penalised solid); the force on the body is the momentum
the flow loses in it; the sensitivities are the exact discrete adjoint's; the boundary moves down
them, and the volume is held by offsetting the whole level.  The boundary splits and merges as it
moves (the topology changes that way); a hole does not open in the middle of solid.  Returns a
FlowTopologyResult (the body, the drag and lift per iteration, the flow around it).  An unchanged
problem is cached.

### `fluid_analysis(body, domain, inlets, outlets, boundaries, fluid=Fluid('water', density=1e-09 t/mm^3, viscosity=1e-09 MPa s), element_size=None, bounds=None, gravity=None, stokes=False, tolerance=1e-05, relaxation=1.0, cache=True, time=None, store_every=1)`

Laminar flow of `fluid` around `body` (a Shape, the solid the flow goes around) in `domain` (a Shape whose inside is the
fluid, the body's place in it too: the fluid is the domain with the body cut out) with the boundary conditions, the same
inputs as flow_topology_optimization, one for each kind (each one item or a list, [] for none):
inlets    inlet(...) items -- where the fluid comes in (as many as the flow has)
outlets   outlet(...) items -- where it leaves (one is needed, or a moving wall)
boundaries  wall(...) items -- moving walls (every surface in no region is a wall at rest) -- and slip(...) items -- symmetry planes
            and frictionless walls: one or both, in one list
(see the module's description) -- the steady flow, or the flow in time (time=).  Each condition
is a model of its own, drawn on `domain`.  Every surface in no region is a wall at rest: the body's is, unless a wall(...) moves it.

element_size   mm (default: 40 elements along the longest side); the passages should be
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

### `inlet(*regions, region=None, velocity=None, speed=None, flow_rate=None, profile='uniform')`

The fluid comes in through the surface inside `region` (a Shape; any number of regions, one argument after the other).  One of:
velocity   a vector (mm/s): the direction, and the mean speed over the inlet
speed      a mean speed (mm/s) along the inlet's inward normal
flow_rate  a flow rate (mm^3/s) along the inward normal
profile    'uniform' (a plug, zero on the no-slip rim) or 'developed' (the fully developed
           profile of the inlet's cross-section: parabolic in a round pipe)
The speed or flow rate is matched exactly on the mesh (the flux through the inlet's triangles).

### `outlet(*regions, region=None, pressure=0.0)`

The fluid leaves through the surface inside `region` (any number of regions, one argument after the other) at `pressure` (MPa, 0 by default; the
pressure field is relative to it).  Put it where the flow leaves parallel to the walls.

### `slip(*regions, region=None)`

A symmetry plane or frictionless wall inside `region` (any number of regions, one argument after the other): nothing flows through it,
the fluid slides along it

### `symmetry(*regions, region=None)`

A symmetry plane or frictionless wall inside `region` (any number of regions, one argument after the other): nothing flows through it,
the fluid slides along it

### `wall(*regions, region=None, velocity=(0.0, 0.0, 0.0))`

A wall moving with `velocity` (mm/s; no-slip) inside `region` (any number of regions, one argument after the other).  Surfaces in no region are walls at rest, so
this is for moving walls, and for naming a wall whose force is wanted (wall_forces).

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

    part = difference(box_exact((0, 0, 0), (40, 30, 10)), sphere(8))
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

Write it as the last line of a shape's definition, after handles() and expose():

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

The viewport meshes the whole scene at one resolution (view.set_resolution): fine enough for the finest thing in it, which is
more than the rest needs.  `custom_resolution(body, resolution)` gives ONE body a resolution of its own, in samples per mm,
whatever the scene's is: a part that is intricate is drawn fine and the rest of the scene stays coarse and quick -- or a part
that is big and plain is drawn coarser than the scene.

    from fieldes import *

    view.set_resolution(2)                        # the scene: 2 samples per mm
    gear = custom_resolution(gear, 8)             # ... except the gear, drawn at 8
    gear                                          # (it is shown by the line that names it, as always)

The body is meshed on its own, over a cube round it, at that resolution; the number is the resolution, not a scale of the scene's,
so it stays what it is when the scene's resolution is changed.  Nothing is drawn outside the region of the scene (view.set_bounds), as
for every shape: a body that reaches out of it is drawn at its own resolution inside it.  It changes how the body is DRAWN, not what it
is: an analysis, a boolean or an export of it is the same.

The finest a body can be drawn is 2000 samples along its longest side (a longer one would be hundreds of millions of cells): a
resolution above that is not an error, the body is drawn at the finest there is, and the model tree's row says what is used
(`resolution 8 -> 7.6`).

In FielDes, right-click a body and choose "Custom resolution": the line is written under its definition, and the number is a field
under the body in the model tree (type in it; the bin takes the line away).

### `custom_resolution(shape, resolution, bounds=None)`

`shape` drawn at a resolution of its own: `resolution` samples per mm, whatever the scene's resolution is (see the module).

shape       a body
resolution  samples per mm (a number above 0); 2 draws features of half a millimetre, 10 of a tenth
bounds      ((x0, y0, z0), (x1, y1, z1)) of the body, if its extent cannot be found

Returns the body itself in every other way; only the way it is meshed for the viewport is changed.
