# Library reference

Every public function and class of the FielDes library, generated from its docstrings
(`python scripts/gen_reference.py`).  `from fieldes import *` brings all of them in.

Contents: [Primitive shapes](#primitive-shapes) | [Combining shapes (CSG)](#combining-shapes-csg) | [Moving, rotating, scaling, deforming](#moving-rotating-scaling-deforming) | [Text](#text) | [Importing STEP models](#importing-step-models) | [Importing triangle meshes](#importing-triangle-meshes) | [Handles: editing shapes by dragging](#handles-editing-shapes-by-dragging) | [Fields](#fields) | [Regressions and data](#regressions-and-data) | [Surfaces and offsets](#surfaces-and-offsets) | [Lattices](#lattices) | [Structural analysis and topology optimization](#structural-analysis-and-topology-optimization) | [Thermal analysis and thermal topology optimization](#thermal-analysis-and-thermal-topology-optimization) | [Caching](#caching)

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

### `union(a, *args)`

Returns the union of two shapes

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

### `move(t, *args, **kwargs)`

Moves the given shape in 2D or 3D space

### `reflect_x(t, *args, **kwargs)`

Reflects a shape about the x origin or an optional offset

### `reflect_xy(t, *args, **kwargs)`

Reflects a shape about the plane X=Y

### `reflect_xz(t, *args, **kwargs)`

Reflects a shape about the plane X=Z

### `reflect_y(t, *args, **kwargs)`

Reflects a shape about the y origin or an optional offset

### `reflect_yz(t, *args, **kwargs)`

Reflects a shape about the plane Y=Z

### `reflect_z(t, *args, **kwargs)`

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

### `rotate(t, *args, **kwargs)`

Rotate the given shape by an angle in radians
The center of rotation is [0 0 0] or specified by the optional argument

### `rotate_x(t, *args, **kwargs)`

Rotate the given shape by an angle in radians
The center of rotation is [0 0 0] or specified by the optional argument

### `rotate_y(t, *args, **kwargs)`

Rotate the given shape by an angle in radians
The center of rotation is [0 0 0] or specified by the optional argument

### `rotate_z(t, *args, **kwargs)`

Rotate the given shape by an angle in radians
The center of rotation is [0 0 0] or specified by the optional argument

### `scale_x(t, *args, **kwargs)`

Scales a shape by sx on the x axis about 0 or an optional offset

### `scale_xyz(t, *args, **kwargs)`

Scales a shape on all three axes, about 0 or an optional offset

### `scale_y(t, *args, **kwargs)`

Scales a shape by sx on the x axis about 0 or an optional offset

### `scale_z(t, *args, **kwargs)`

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

## Importing STEP models

Python library of FielDes, built on the libfive CAD kernel

Hand-written (not code-generated): imports CAD files using FielDes's own
small, dependency-free STEP reader (see kernel/src/step/).

The main entry points are import_step_parts() and import_step(). Every
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

### `exact_region_mesh(shape, cell=1.0)`

The exact pieces of the regions excluded from `shape` (see exclude)
as (vertices, triangles) lists -- outside FielDes, e.g. to check
them: the STEP file's surface inside each region, the triangles the
region's surface crosses cut down to `cell` long.  Every number must
be a plain number (no FielDes variables), and no region may have been
moved after exclude().

### `exclude(shape, region=None, source=None, quality=64, threshold=1.0)`

Draws the EXACT surface of the imported part(s) `shape`, meshed straight
from the STEP file, inside a region -- for places the import only
approximates (B-spline faces fitted by simple surfaces, which the import
marks in red; threads; ...) that have to stay exact.

The region is a FIELD OBJECT: any shape -- a sphere, a box, a part, a
part offset by some distance, a union of those -- is a region, the
inside of the shape (where its field is negative):

    rails = union(kitchen[59][0], kitchen[60][0])   # the parts' own fields
    kitchen = exclude(kitchen, offset(rails, 90))   # everything within 90 mm of them

Without a region, it is the places where the fit is worse than
`threshold` percent of the face's size (poor_fit_region), in every part
that has any:

    kitchen = import_step_parts('step/kitchen.stp')
    kitchen = exclude(kitchen)

What you give it decides how much it works on.  The WHOLE IMPORT (what
import_step_parts returns): the region goes into every part it
touches, and the same list comes back, the parts in the same places.
One entry of the import, kitchen[19], or one part, kitchen[19][0]:
that part only.

    kitchen[19] = exclude(kitchen[19], region)

Inside the region FielDes draws the exact surface instead of the shape's
own (in the view and in the exported STL): the shape's mesh loses its
triangles there and the STEP file's surface is cut by the region's field
and put in, the edge between them jagged by one cell.  The field itself
-- what further modelling, FEA or lattices see -- is not changed.  The
region's surface should cross no badly fitted face (the red one): where
the fit is off, the exact surface and the fitted one do not meet at the
region's surface and the seam shows as a step.  A region that encloses
the whole bad face has no such seam.

For a shape made from a part (moved, mirrored, cut, filled with a
lattice ...) say which imported part its exact geometry comes from,
and apply exclude() last, to the finished shape:

    part = import_step_parts('bracket.step')[0][0]
    light = ...                  # anything made from part
    final = exclude(light, region, source=part)

The part may have been moved, rotated, scaled or mirrored with the
fieldes.stdlib transforms since the import (the exact piece follows);
several regions can be excluded one after the other.

quality: points per full turn of a circle in the exact mesh.
threshold: for the default region, in percent of the face's size.

### `import_step(path, cache=True, units='mm', rev=None)`

Imports a STEP (.step/.stp) file as ONE Shape combining every solid
(the union of import_step_parts()), so it can be used with the rest
of fieldes.stdlib (union/difference/intersection with anything else,
transforms, etc). Cached exactly as import_step_parts() is.

Meshing a multi-part assembly as a single Shape forces one
resolution across the whole assembly's bounding box, driven by
whatever feature is smallest anywhere in the file; for a large
assembly, mesh each part from import_step_parts() over its own
bounds instead.

Raises RuntimeError (with a message describing why) if the file
can't be read, contains no importable solids, or ANY solid could
not be reconstructed as native CSG (unlike import_step_parts(),
which lets you still use the other, good parts of the same file by
index -- combining every part into one Shape here means one
FailedPart necessarily fails the whole union).

### `import_step_parts(path, cache=True, units='mm', rev=None, auto_exclude=False, exclude_threshold=1.0, exclude_quality=64)`

Imports a STEP (.step/.stp) file as a separate Shape PER SOLID, using
FielDes's built-in reader -- no external CAD-kernel dependency.

Every solid is rebuilt as CSG by the reconstruction algorithm:
analytic faces (plane, cylinder, cone, sphere, torus) as exact
expressions, free-form B-spline faces as fitted closed-form surfaces
(an approximation: the faces that fit poorly are listed after the
import and shown in red).  The results are ordinary Shapes.
A solid the reconstruction can't resolve does not stop the rest of
the file from importing -- its list entry is a FailedPart instead,
which raises a specific RuntimeError describing why the moment it's
actually used (meshed, combined with other shapes, etc).

The reconstruction can take several seconds on a large file, so the
result is saved to a Python file next to the STEP file
("<file>.fieldes-cache.py") and reused on later calls -- for example
every time a FielDes script is re-run -- until the STEP file changes or
the import algorithm does (a rebuilt library keeps it). cache=False
disables it; cache='some/path.py'
stores it elsewhere.

Meshing each part returned here separately, over its own `bounds`,
lets you pick a resolution matched to that part's own scale:

    for i, (shape, bounds) in enumerate(import_step_parts(path)):
        shape.save_stl(f'part_{i}.stl', bounds[0], bounds[1],
                        resolution=...)

Returns a list of (Shape, (xyz_min, xyz_max)) tuples, one per solid,
where xyz_min/xyz_max are that solid's own tight bounding box
(three-element tuples, ready to pass straight into
save_stl()/get_mesh()). Raises RuntimeError if the file can't be
read or contains no importable solids.

Assemblies come out assembled: every part is placed where the
file's assembly structure puts it.  A component used several
times (four identical screws) gives one entry per occurrence --
the first at its solid's index, the others appended after the last
solid, so part indices stay the same as for a single occurrence.
Each part Shape's `_part_name` is its occurrence in the assembly
(e.g. 'Drive:1/Motor:1/M3x10-Screw:2').

units: the model is converted from the unit each part of the STEP
file declares (millimetres, metres, inches, ...; one file can mix
them) into these units ('mm' by default; 'cm', 'm', 'in' also
work), so a file exported in metres isn't a thousand times too
small.  units='file' gives the numbers in the file's first declared
unit.

rev: an arbitrary revision number that is part of the cache key --
changing it (FielDes's "Reimport" does exactly that) forces a fresh
reconstruction instead of reusing the cached result.

B-spline faces are imported as fitted simple surfaces (planes,
quadrics, extruded / revolved / helical curves), listed after the import; where
a fit is off by more than 0.5 % of its face's size, the model is
shaded there, turning fully red at 10 %.
Faces that must be exact (threads, gear teeth): exclude() a region
around them -- that uses the STEP geometry itself.

auto_exclude=True does that for every poorly fitted place by itself: each
part's fit marker is read, the places where the fit is off by more than
`exclude_threshold` (percent of the face's size, 1.0 by default; 0.5 is the
lowest) are excluded from the part (the region is the field poor_fit_region),
so the FielDes viewport draws the STEP file's own surface there and the fitted
field everywhere else.  The region is cut out of the part's field, which is
what further modelling sees.  `exclude_quality` is exclude()'s `quality`.
Off by default: the exact pieces cost meshing time.

### `import_step_parts_reconstructed(path, cache=True, units='mm', rev=None, auto_exclude=False, exclude_threshold=1.0, exclude_quality=64)`

Imports a STEP (.step/.stp) file as a separate Shape PER SOLID, using
FielDes's built-in reader -- no external CAD-kernel dependency.

Every solid is rebuilt as CSG by the reconstruction algorithm:
analytic faces (plane, cylinder, cone, sphere, torus) as exact
expressions, free-form B-spline faces as fitted closed-form surfaces
(an approximation: the faces that fit poorly are listed after the
import and shown in red).  The results are ordinary Shapes.
A solid the reconstruction can't resolve does not stop the rest of
the file from importing -- its list entry is a FailedPart instead,
which raises a specific RuntimeError describing why the moment it's
actually used (meshed, combined with other shapes, etc).

The reconstruction can take several seconds on a large file, so the
result is saved to a Python file next to the STEP file
("<file>.fieldes-cache.py") and reused on later calls -- for example
every time a FielDes script is re-run -- until the STEP file changes or
the import algorithm does (a rebuilt library keeps it). cache=False
disables it; cache='some/path.py'
stores it elsewhere.

Meshing each part returned here separately, over its own `bounds`,
lets you pick a resolution matched to that part's own scale:

    for i, (shape, bounds) in enumerate(import_step_parts(path)):
        shape.save_stl(f'part_{i}.stl', bounds[0], bounds[1],
                        resolution=...)

Returns a list of (Shape, (xyz_min, xyz_max)) tuples, one per solid,
where xyz_min/xyz_max are that solid's own tight bounding box
(three-element tuples, ready to pass straight into
save_stl()/get_mesh()). Raises RuntimeError if the file can't be
read or contains no importable solids.

Assemblies come out assembled: every part is placed where the
file's assembly structure puts it.  A component used several
times (four identical screws) gives one entry per occurrence --
the first at its solid's index, the others appended after the last
solid, so part indices stay the same as for a single occurrence.
Each part Shape's `_part_name` is its occurrence in the assembly
(e.g. 'Drive:1/Motor:1/M3x10-Screw:2').

units: the model is converted from the unit each part of the STEP
file declares (millimetres, metres, inches, ...; one file can mix
them) into these units ('mm' by default; 'cm', 'm', 'in' also
work), so a file exported in metres isn't a thousand times too
small.  units='file' gives the numbers in the file's first declared
unit.

rev: an arbitrary revision number that is part of the cache key --
changing it (FielDes's "Reimport" does exactly that) forces a fresh
reconstruction instead of reusing the cached result.

B-spline faces are imported as fitted simple surfaces (planes,
quadrics, extruded / revolved / helical curves), listed after the import; where
a fit is off by more than 0.5 % of its face's size, the model is
shaded there, turning fully red at 10 %.
Faces that must be exact (threads, gear teeth): exclude() a region
around them -- that uses the STEP geometry itself.

auto_exclude=True does that for every poorly fitted place by itself: each
part's fit marker is read, the places where the fit is off by more than
`exclude_threshold` (percent of the face's size, 1.0 by default; 0.5 is the
lowest) are excluded from the part (the region is the field poor_fit_region),
so the FielDes viewport draws the STEP file's own surface there and the fitted
field everywhere else.  The region is cut out of the part's field, which is
what further modelling sees.  `exclude_quality` is exclude()'s `quality`.
Off by default: the exact pieces cost meshing time.

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

## Importing triangle meshes

Triangle-mesh import: STL (binary or ASCII), Wavefront OBJ, PLY (ASCII or
binary), 3MF and glTF (.glb / .gltf) files.

    bracket, bounds = import_mesh(r"C:\models\bracket.stl")
    view.set_bounds(*roi(bounds))

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

### `import_mesh(path, units='mm', file_units=None, rev=None)`

Imports a triangle mesh (.stl, .obj, .ply, .3mf, .glb or .gltf) as a Shape whose
value is the exact signed distance to its triangles (negative
inside).

units       the units of your script ('mm', 'cm', 'm', 'in', ...)
file_units  what the numbers in the file mean.  STL, OBJ and PLY
            files don't say (millimetres are assumed); 3MF files do
            and glTF is in metres, which is used unless this
            overrides it
rev         not used by the import itself: changing it makes FielDes
            run the script (and read the file) again, e.g. after
            the file changed on disk

Returns (shape, (xyz_min, xyz_max)): the shape and its bounding box,
ready for view.set_bounds(*roi(...)).  Raises RuntimeError if the
file can't be read or has no usable triangles.

### `mesh_info(path)`

The summary of the last import of `path` in this session (triangle
count, whether it is watertight, ...) as a dict, or None.

## Handles: editing shapes by dragging

Handles: edit a shape -- an imported part too -- by dragging in FielDes's viewport.

FielDes's model tree has a handles button on every shape and part.  It cycles
three modes, so the ways of editing never get in each other's way:

  gizmo    the part's own move arrows, rotation rings and scale knobs:
           `handles()` writes, under the shape's definition,

               part = handles(part, move=(var(0), var(0), var(0)),
                              rotate=(var(0), var(0), var(0)),
                              scale=(var(1), var(1), var(1)), mode='gizmo')

           so what the gizmo does is ordinary script text, like any dragged
           var().  The shape is `part` scaled about its centre, rotated (x,
           then y, then z; degrees) and moved.
  handles  FielDes's own handles: hover a surface of the shape and drag it.
           Any shape with `var()` numbers has them; for a shape written with
           plain numbers -- a primitive, or a part imported from a STEP file,
           which is made of primitives -- `expose()` makes the numbers that
           place its surfaces variables:

               part = expose(part, [var(6), var(40), ...])

           Dragging a face changes the number of that face (a plane's
           position, a radius) in the script text.
  lock     nothing is draggable: neither the gizmo nor the surface

Nothing is stored anywhere but in the script: delete the `expose(...)` and
`handles(...)` lines of a part (FielDes's Reimport does) and it is the file's
version again. 

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

### `handles(shape, move=(0, 0, 0), rotate=(0, 0, 0), scale=(1, 1, 1), about=None, mode=None, show=None)`

Scales, rotates and moves a shape, with the gizmo FielDes shows on it.

scale: (x, y, z) factors about the shape's centre, `rotate`: (x, y, z)
rotations in degrees about the same centre (x first, then y, then z),
`move`: (x, y, z) translation.  Write the numbers as var(...) to drag
them: FielDes's model tree does that for you.
about: the centre (default: the middle of the shape's box)
mode: 'gizmo' (the default) shows the gizmo, 'handles' lets the shape's own
surfaces be dragged instead (see expose()), 'lock' makes it undraggable;
the placement stays in every mode.  (show=True / False, as older scripts
wrote it, is 'gizmo' / 'lock'; mode= wins.)

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
    lat   = lattice(part, 'gyroid', cell_size=8, thickness=t, skin=1.5)

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

### `angle_field(center=(0, 0, 0), axis='z')`

Angle around an axis through `center`, in radians (-pi..pi)

### `attractor(points, radius, falloff='smooth', strength=1.0)`

A field that is `strength` at the given points (or curves: pass a
distance field instead of points) and falls to 0 at `radius`.
falloff: 'linear', 'smooth' (smoothstep) or 'gauss'.

### `bend_z(shape, radius, center=(0, 0))`

Bends a shape lying along +x around the z axis: x becomes arc length
on a circle of the given radius

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

Signed distance to a plane: positive on the side the normal points to

### `distance_to_point(p)`

Euclidean distance to a point

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
fillet of the given radius: the shape is grown by the radius and
shrunk back, with exact distances both times

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

### `mass_properties(shape, density=1.0, lower=None, upper=None, resolution=None)`

Volume, mass (density in g/cm^3 -> grams), centroid and bounding box
of a solid, by grid sampling.  Returns a dict.

### `mirror_x(shape, x=0.0)`

Mirror-symmetric copy: the half at x > x0 reflected onto the other

### `mirror_y(shape, y=0.0)`

Mirror-symmetric copy: the half at y > y0 reflected onto the other

### `mirror_z(shape, z=0.0)`

Mirror-symmetric copy: the half at z > z0 reflected onto the other

### `mix(a, b, t)`

Same as lerp()

### `noise_field(scale=10.0, octaves=4, seed=1, gain=0.5, lacunarity=2.0, amplitude=1.0)`

Smooth random variation (Perlin noise), about -amplitude..amplitude:
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
so edges and corners get round, not stretched

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

### `radial_field(center=(0, 0, 0), axis='z')`

Distance from an axis through `center` (cylindrical radius, mm)

### `ramp(field, input_range, output_range, clamped=True)`

Linear map of a field: input_range=(a, b) -> output_range=(va, vb).
Clamped by default (values beyond the input range hold the end
values), e.g. ramp(z_field(), (0, 50), (2, 0.5)) is 2 at z = 0,
falling to 0.5 at z = 50 and above.

### `remap_field(field, input_range, output_range, clamped=True)`

Same as ramp()

### `repeat(shape, spacing, center=(0, 0, 0))`

Repeats a shape infinitely on a grid.  spacing: a number or (sx, sy,
sz); 0 along an axis leaves that axis alone.  The shape should fit
inside one grid cell centred on `center`.

### `repeat_polar(shape, count, center=(0, 0), axis='z')`

Repeats a shape `count` times around the z axis; the shape should
lie in the wedge around the +x direction

### `round_edges(shape, radius, bounds=None, resolution=None)`

Rounds every convex (outside) edge and corner of a shape with the
given radius: the shape is shrunk by the radius and grown back, with
exact distances both times.  resolution (samples per mm) sets how
finely the surfaces are followed; about 4 / radius or finer.

### `sample_grid(field, lower, upper, n=20)`

Values on an n x n x n grid of cell centres over a box (a flat list,
x fastest) together with the points

### `shell_centered(shape, thickness)`

A shell straddling the surface (half inside, half outside)

### `shell_exact(shape, thickness, side='inside', bounds=None, resolution=None)`

A hollow shell of uniform thickness (true distance), side = 'inside',
'outside' or 'center'

### `shell_inside(shape, thickness)`

A hollow shell: the part of `shape` within `thickness` of its surface

### `shell_outside(shape, thickness)`

A skin grown outwards from the surface by `thickness`

### `signed_distance(shape)`

A shape's signed distance field (negative inside) -- the shape
itself, named for readability

### `smooth_difference(a, b, radius)`

a minus b, with a rounded blend along the cut

### `smooth_intersection(a, b, radius)`

Intersection with a rounded blend

### `smooth_union(a, b, radius)`

Union with a rounded blend of the given radius where the shapes meet

### `smoothstep(field, edge0, edge1)`

0 below edge0, 1 above edge1, a smooth S-curve between

### `step_field(field, edge)`

0 below the edge, 1 above (a sharp step; see smoothstep)

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
    lat = lattice(ball, 'gyroid', cell_size=8, thickness=thickness, skin=1.5)

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
faces (see import_step_parts); the named ones are for modeling:

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

An ellipsoid with semi-axes radii = (a, b, c) along x, y, z
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

The one-call version:

    from fieldes import *

    part = sphere(30)
    lat = lattice(part, 'gyroid', cell_size=8, thickness=1.0, skin=1.5)

    # graded: thicker walls near the skin, by a ramp or a regression
    t = ramp(depth_below(part), (0, 30), (1.6, 0.5))
    lat = lattice(part, 'gyroid', cell_size=8, thickness=t, skin=1.5)

    # by relative density instead of thickness (calibrated automatically;
    # the density may itself be a field)
    lat = lattice(part, 'octet', cell_size=10, density=0.2)

    # cells that follow a sphere / cylinder
    lat = lattice(part, 'gyroid', cell_size=8, thickness=1.0,
                  cell_map=spherical(cells_around=16))

Building blocks (infinite lattices -- fields; fill() or lattice() trims them):
    tpms(kind, cell_size, thickness=..., style='sheet' | 'network', ...)
    strut_lattice(cell, cell_size, radius, node_radius=None, blend=0, ...)
    planar_lattice(kind, cell_size, wall, axis='z')      honeycombs, grids
    fill(body, lattice, skin=0, region='volume' | 'shell', depth=...)
    relative_density(lattice, cell_size)                 volume fraction
    lattice_parameter_for_density(kind, cell_size, density)

TPMS kinds: gyroid, schwarz_p, diamond (Schwarz D), neovius, lidinoid,
    split_p, iwp, frd, fischer_koch_s.  style='sheet' is a wall of the given
    thickness on both sides of the minimal surface ("walled TPMS");
    style='network' is the solid on one side of it, grown by `offset`
    ("skeletal TPMS").
Strut cells: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin,
    diamond, cross, tesseract, cuboctahedron.
Planar (2.5D) patterns: hexagon (honeycomb), triangle, square, kagome.

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

### `LatticeGraph`

A graph of nodes and beams: nodes [(x, y, z), ...], beams [(i, j), ...].
.thicken(radius, blend) makes it solid; radius may be a number, a
list (one per node) or a field.

#### `LatticeGraph.lengths(self)`

The length of every beam (mm)

#### `LatticeGraph.thicken(self, radius=0.5, blend=0.0)`

The graph as round beams of `radius` mm: see graph_lattice()

### `TPMSEquation`

Your own TPMS (or any triply periodic) equation, for tpms() and
lattice(): f(a, b, c) -> value, where a, b, c are the position in the
cell as phases (2 pi per cell).  Write it with + - * / and the
methods .sin() .cos() .sqrt() .square() of a, b, c, e.g. a gyroid:
    tpms_equation(lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos()
                                  + c.sin() * a.cos())
Its value is turned into a distance in mm (its gradient is followed),
so thickness= is a real wall thickness.

### `UnitCell`

A strut unit cell of your own: beams between nodes in the unit cube
(coordinates 0..1 across the cell, scaled by cell_size when used).
Use it wherever a cell is taken: strut_lattice(cell=...),
lattice(..., cell=...).

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

### `lattice(body, kind='gyroid', cell_size=10.0, thickness=None, radius=None, density=None, style='sheet', offset=None, skin=0.0, region='volume', depth=None, cell_map=None, node_radius=None, blend=0.0, skin_blend=0.0, wall=None, axis='z')`

A body filled with a lattice, in one call.

kind: a TPMS (gyroid, schwarz_p, diamond, neovius, lidinoid, split_p,
    iwp, frd, fischer_koch_s), a strut cell (cubic, bcc, bccz, fcc,
    fccz, octet, octahedron, kelvin, diamond_struts, cross,
    tesseract, cuboctahedron), a planar pattern (hexagon,
    triangle, square, kagome) or a random one (voronoi, stochastic)
cell_size: mm, or (sx, sy, sz)
thickness (sheet TPMS), offset (network TPMS), radius (struts), wall
    (planar): the member size -- numbers or fields
density: instead of the member size, a relative density 0..1 (a
    number or a field); calibrated automatically
style: 'sheet' or 'network' (TPMS)
skin: solid skin thickness on the body's surface (0 = none)
region: 'volume' or 'shell' (only the outer `depth` mm)
cell_map: cartesian(...), cylindrical(...) or spherical(...)
node_radius, blend: joint spheres and joint rounding (struts)
skin_blend: rounds the lattice-to-skin joints

### `lattice_parameter_for_density(kind, cell_size, density, style='sheet', samples=40)`

The thickness (sheet TPMS), offset (network TPMS), wall (planar) or
radius (struts) that gives a lattice the requested relative density
(0..1), found by bisection on a sampled unit cell

### `periodic(shape, cell_size=10.0, cell_map=None, check=True)`

Any shape as a unit cell: the shape you model in one cell -- the box
from (0, 0, 0) to cell_size -- repeated through space (along a cell
map too: cylindrical, spherical ...).  Model the cell so that it
tiles: what leaves one face must come in on the opposite face (a
solid that touches a face must touch it the same way on the other
side).  check=True samples the faces and warns where they don't
match (the lattice would have steps or holes at every cell
boundary).

### `planar_lattice(kind='hexagon', cell_size=10.0, wall=0.8, axis='z', cell_map=None)`

A 2.5D pattern of walls, extruded along an axis: a honeycomb
(kind='hexagon'; cell_size = flat-to-flat width), or a triangle,
square or kagome grid.  wall is the wall thickness (mm, may be a
field).

### `points_graph(points, style='delaunay')`

The Delaunay (or Voronoi) graph of your own points

### `relative_density(lattice_field, cell_size, samples=40, origin=(0, 0, 0))`

Volume fraction of an (untrimmed, Cartesian) lattice: the share of
one unit cell it fills, 0..1

### `spherical(origin=(0, 0, 0), cells_around=None, radius=None, rotation=None)`

Cells in shells around a point: radial, around (longitude) and pole
to pole (latitude).  Give cells_around= or radius= (where the cells
should be cell_size wide).  Cells shrink towards the poles.

### `strut_lattice(cell='octet', cell_size=10.0, radius=0.8, node_radius=None, blend=0.0, cell_map=None)`

An infinite strut (beam) lattice: round beams of `radius` mm along
the edges of a unit cell, repeated every cell_size.
cell: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin,
    diamond, cross, tesseract, cuboctahedron -- or your own cell:
    unit_cell(nodes, beams) (beams may have their own radius), or a
    plain list of beams ((x0, y0, z0), (x1, y1, z1)) in unit-cube
    coordinates
node_radius: spheres at the joints (defaults to none)
blend: rounds the joints with a smooth blend of this radius
radius / node_radius may be fields.

### `surface_graph(body, cell_size=8.0, pattern='triangle', seed=1, bounds=None)`

A graph on a body's surface: points about cell_size apart joined into
triangles (pattern='triangle') or the dual cells (pattern='voronoi',
mostly hexagons)

### `surface_lattice(body, cell_size=8.0, radius=0.6, pattern='triangle', seed=1, blend=0.0, bounds=None, with_body=None)`

Round beams on a body's surface: a triangle lattice
(pattern='triangle') or its dual, a Voronoi / hexagon-like pattern
(pattern='voronoi').  The beams are centred on the surface; pass
with_body='inside' to keep only their part inside the body, or
'union' to add them to the body.

### `tpms(kind='gyroid', cell_size=10.0, thickness=1.0, style='sheet', offset=0.0, cell_map=None, invert=False, fast=False)`

An infinite TPMS lattice (a field; trim it with fill() or use
lattice()).
kind: gyroid, schwarz_p, diamond, neovius, lidinoid, split_p, iwp,
    frd, fischer_koch_s -- or your own: tpms_equation(f)
style='sheet': walls of `thickness` mm centred on the surface
style='network': the solid on one side of the surface, grown by
    `offset` mm (0 = half the volume for gyroid / diamond / P);
    invert=True takes the other side
style='surface': the signed distance to the surface itself
thickness / offset may be fields.
fast=True: a quicker, slightly less exact distance (walls ~5 % thin)

### `tpms_equation(func, name='custom')`

Your own TPMS equation: see TPMSEquation

### `unit_cell(nodes, beams, mirror='')`

Your own strut unit cell: see UnitCell.  Example (a body-centred cell
with thicker diagonals, drawn once and mirrored):
    cell = unit_cell({'c': (0.5, 0.5, 0.5), 'o': (0, 0, 0)},
                     [('c', 'o', 1.2)], mirror='xyz')
    strut_lattice(cell, cell_size=8, radius=0.6)

### `unit_cell_beams(cell)`

The beams of a strut unit cell (in unit-cube coordinates), made
periodic: every beam of the infinite lattice that passes near the
cell, clipped to it (with a margin for cells that aren't
mirror-symmetric).  Returns (beams, margin).

### `voronoi_graph(body, cell_size=8.0, style='voronoi', relax=2, seed=1, bounds=None)`

A random graph filling a body: Poisson-disk points about cell_size
apart, joined by the edges of their Voronoi cells (style='voronoi',
a foam) or by their Delaunay edges (style='delaunay', a stochastic
truss).  relax: iterations that make the cells more even.

### `voronoi_lattice(body, cell_size=8.0, radius=0.6, style='voronoi', relax=2, seed=1, skin=0.0, blend=0.0, skin_blend=0.0, bounds=None, density=None)`

A body filled with a random Voronoi foam (or, style='delaunay', a
stochastic truss) of round beams.  radius may be a field (e.g. from
a regression); seed picks a different random pattern.
density: a relative density instead of the radius (a number).

## Structural analysis and topology optimization

Static finite element analysis (linear elasticity) of FielDes shapes.

    from fieldes import *

    bracket = ...                                        # any Shape, in mm
    result = static_analysis(
        bracket,
        supports=[fixed(box((0, 0, 0), (5, 40, 20)))],   # clamp the left end
        loads=[force(box((95, 0, 0), (100, 40, 20)), (0, 0, -200))],   # 200 N down
        material=aluminium, element_size=1.0)
    colored(bracket, result.von_mises)       # show the stress on the part (FielDes)
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
Poisson's ratio nu, density (t/mm^3, for gravity loads), thermal
conductivity (W / (mm K), for thermal_analysis), thermal expansion
coefficient (1/K, for thermal_expansion)

### `ModalResult`

The result of modal_analysis(): .frequencies (Hz, lowest first),
.modes (a Mode each: its shape as fields, .show()), .seconds

### `Mode`

One natural mode of vibration: .frequency (Hz), and its shape as
fields (Shapes usable in any expression): displacement, ux, uy, uz
-- scaled so the largest movement is 1 (a shape, not an amplitude).
.show() colours the part by it, deformed by it in FielDes.

#### `Mode.show(self, field='displacement', deformation='auto')`

The part coloured by the mode shape, shown deformed by it in
FielDes ('auto': the largest movement 5 % of the part's size)

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

#### `Result.show(self, field='von_mises', range=None, deformation='auto')`

The part coloured by a result field, for display in FielDes.
FielDes shows the part deformed and lets you switch the field
and scale the deformation next to the colour bar (and show
the elements).  deformation: 'auto' (the largest movement
shown as 5 % of the part's size), a number (scale factor, 1 =
true size) or 0 (undeformed).

### `TetMode`

One natural mode of a part on a body-fitted tetrahedral mesh (see Mode)

### `TetResult`

A static analysis on a body-fitted tetrahedral mesh: its elements are tetrahedra
that follow the part's surface (not a voxelization), the stress in each is its
own (constant in it), and the fields are read anywhere in the mesh.  Everything
of Result: fields as shapes, .show(), .range(), .element_range(), ...

#### `TetResult.element_text(self)`

What the analysis is made of, and how many of them

#### `TetResult.show(self, field='von_mises', range=None, deformation='auto')`

The part coloured by a result field, for display in FielDes.
FielDes shows the part deformed and lets you switch the field
and scale the deformation next to the colour bar (and show
the elements).  deformation: 'auto' (the largest movement
shown as 5 % of the part's size), a number (scale factor, 1 =
true size) or 0 (undeformed).

### `TopologyResult`

The result of topology_optimization():
.density     a field, 0 (no material) .. 1 (solid), in the part
.shape()     the optimized part, keeping the volume fraction asked
             for (shape(threshold=0.5): where density > 0.5)
.compliance  compliance (N mm) at each iteration -- lower is stiffer
.volume_fraction, .iterations, .seconds
.verify()    a static analysis of the optimized part (stresses) --
             a list, one per load case, when there are several

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

### `fixed(region, x=True, y=True, z=True)`

A support: the part is held in place wherever it lies inside
`region` (a Shape).  x / y / z = False leave that direction free
(a sliding support).

### `force(region, fx, fy=None, fz=None)`

A load: the total force (fx, fy, fz) in N, spread evenly over the
part's surface inside `region` (a Shape).  force(region, (0, 0, -100))
works too.

### `gravity(g=(0.0, 0.0, -9810.0))`

The part's own weight: acceleration in mm/s^2 (default: 1 g down
along -Z), with the material's density

### `modal_analysis(shape, supports, material=Material('steel', E=200000 MPa, nu=0.3), modes=6, element_size=None, bounds=None, max_iterations=100, tolerance=1e-06, cache=True, element='tet')`

The natural frequencies and mode shapes of `shape` (a Shape, in
mm), held by `supports` (fixed(...) items) -- how it vibrates.
No loads are needed; the material's E and density are used.

modes        how many (the lowest first)
element_size mm (default: about 40 elements along the longest side)
element      'tet' (default), 'hex' or 'hex_basic': see the module
             documentation

Returns a ModalResult: .frequencies (Hz), .modes[i] (the shape as
fields -- displacement, ux, uy, uz -- and .show()).  The shapes are
fields like any other: e.g. stiffen the part where the first mode
moves most.  An unchanged problem is cached.

### `static_analysis(shape, supports, loads, material=Material('steel', E=200000 MPa, nu=0.3), element_size=None, bounds=None, max_iterations=20000, tolerance=1e-06, cache=True, element='tet')`

Linear static analysis of `shape` (a Shape, in mm).

supports   fixed(...) items (at least one)
loads      force(...) / gravity(...) / thermal_expansion(...) items
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

### `topology_optimization(part, supports, loads, material=Material('steel', E=200000 MPa, nu=0.3), volume_fraction=0.3, element_size=None, iterations=60, filter_radius=None, keep=None, avoid=None, extrude=None, penalty=3.0, move=0.2, bounds=None, max_iterations=20000, tolerance=1e-05, cache=True, element='tet')`

Topology optimization: the stiffest part that uses `volume_fraction`
of the material of `part` (the design space), for the given supports
and loads (as in static_analysis).

keep:   regions (Shapes, or a list) that must stay solid -- e.g. bolt
        bosses, mounting faces; the material around supports and
        loads always stays
avoid:  regions that must stay empty
extrude: 'x', 'y' or 'z' -- the design is the same all along that
        axis (outside the keep / avoid regions): a profile to
        extrude, or to cut right through from one side
element_size:  mm (default: 40 elements along the longest side; the
        optimization solves the analysis ~30-60 times)
filter_radius: the smallest member size scale, mm (default 1.5
        elements)
element: 'tet' (default: tetrahedra that follow the part's surface, a density in each),
        'hex' or 'hex_basic' (a regular grid of hexahedra)

loads:  a list of loads -- or several load cases, a list of such
        lists: [[force(a, ...)], [force(b, ...), gravity()]].  Each
        case acts on its own and the part is made stiff for all of
        them (the sum of their compliances is minimised) -- e.g. a
        bracket pushed down in use and sideways in assembly.

Returns a TopologyResult: .density (a field), .shape() (the
optimized part), .compliance, .verify().  An unchanged problem is
cached, so re-running a script is instant.

## Thermal analysis and thermal topology optimization

Steady-state thermal analysis (heat conduction) of FielDes shapes.

    from fieldes import *

    part = ...                                          # any Shape, in mm
    result = thermal_analysis(part, [
        fixed_temperature(base_region, 20),             # held at 20 degrees C
        heat_input(chip_region, 5.0),                   # 5 W into the part here
        convection(fins_region, 25e-6, ambient=20)],    # air cooling
        material=aluminium, element_size=1.0)
    result.show()                        # the part coloured by temperature (FielDes)
    thicker = part - 0.2 * result.heat_flux   # results are fields like any other

    # the material layout (30 % of the part) that keeps the heat input coolest
    design = thermal_topology_optimization(part, [...], volume_fraction=0.3)
    design.shape()

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

#### `ThermalResult.show(self, field='temperature', range=None)`

The part coloured by a result field, for display in FielDes (the
card next to the colour bar switches between the fields)

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

### `convection(region, coefficient, ambient=20.0)`

The part's exposed surface inside `region` exchanges heat with an
ambient temperature: coefficient h in W / (mm^2 K) (still air
~5e-6 - 25e-6, forced air ~25e-6 - 250e-6, water ~500e-6 - 1e-2)

### `fixed_temperature(region, value)`

The part held at temperature `value` wherever it lies inside
`region` (a Shape)

### `heat_generation(region, watts)`

A total power (W) generated inside the part, spread evenly through
its volume inside `region` (a Shape) -- e.g. a resistive heater, or
electronics potted in the part

### `heat_input(region, watts)`

A total power (W) put into the part, spread evenly over its surface
inside `region` (a Shape); negative takes heat out

### `thermal_analysis(shape, boundary, material=Material('aluminium', E=69000 MPa, nu=0.33), element_size=None, bounds=None, conductivity=None, max_iterations=50000, tolerance=1e-07, element='tet', cache=True)`

Steady-state heat conduction in `shape` with the given boundary
conditions: fixed_temperature(...), heat_input(...),
heat_generation(...) and convection(...) items (see the module's
description).

material: its conductivity is used (W / (mm K)); conductivity=
          overrides it
element_size: mm (default: 40 elements along the longest side)
element:  'tet' (default: tetrahedra that follow the part's surface) or
          'hex' (voxel hexahedra)

Returns a ThermalResult: .temperature and .heat_flux fields, .show(),
the heat balance.  An unchanged problem is cached (as a static analysis
is): running the script again, or a section moving over its fields, does
not solve it again; a change to the part, the boundary conditions, the
material or the settings solves the new problem.  cache=False solves
every time.

### `thermal_topology_optimization(part, boundary, material=Material('aluminium', E=69000 MPa, nu=0.33), volume_fraction=0.3, element_size=None, iterations=60, filter_radius=None, keep=None, avoid=None, extrude=None, penalty=3.0, move=0.2, bounds=None, conductivity=None, max_iterations=20000, tolerance=1e-06, cache=True, element='hex')`

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

### `problem_key(kind, **parts)`

A key of a whole problem -- a static analysis, a thermal analysis: what it is made of, each part by its
content -- or None when some part has no content key (then nothing is remembered)

### `shape_key(shape)`

An exact key of a shape's expression: the same expression built again -- a script run again -- has
the same key, and a different one does not (a constant differing in the seventh digit, another
imported mesh, another data field: all different).  It is the library's structural hash of the
tree, not its printed text.  (Raises Uncacheable when the library has no such key.)

### `value_key(v)`

A hashable key for an argument, by its content: numbers (every bit), text, sequences, shapes (by
their expression), and the library's own small objects (a material, a support, a load: by their
fields).  Raises Uncacheable for anything else
