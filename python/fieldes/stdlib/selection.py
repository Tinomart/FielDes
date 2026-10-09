'''
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
'''
import ctypes

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib.content_cache import content_cached
from fieldes.stdlib.fields import _s, _shape_bounds, _script_vars, _vars_key

__all__ = ['select_surface', 'surface_from_bodies', 'SurfaceSelection']

MODES = {'flat': 0, 'smooth': 1}
SELECTION_CATEGORY = 6          # (its colour in the application's colour map "bc", which has this many categories:)
BC_CATEGORIES = 13

# How far from a sample of the walk the surface of the patch can be, as a share of the spacing of the samples (the walk
# steps one spacing, and takes no sample within 0.7 of another)
COVER = 0.85


class SurfaceSelection(Shape):
    ''' A patch of a surface (see select_surface): a surface.
        .shape (what it was picked on), .surface (the patch as a field: the height above it, zero on it), .seed, .angle, .mode, .samples (how many points of the surface the
        walk took), .spacing (how far apart they are), .patch (the distance to the patch's surface, a field: zero on it,
        to within `.cover`), .whole (the distance to the whole surface, a field: the shape's own value, |f|, which is the
        distance where the shape's field is one -- as the field of an imported part or a mesh is; it equals .patch where the
        nearest point of the surface is in the patch), .cover (how far the surface of the patch can be from a sample: the
        patch and the whole differ by less than that where the nearest surface is in the patch) '''

    _no_handles = True             # (not something to drag: FielDes hides the handles button)

    def _display(self):
        ''' What FielDes shows for the selection stated on its own: the part it was picked on, drawn only where the
            patch is, in the selection's colour -- the vertices of the part's surface mesh that belong to the patch
            are coloured, all the others are not drawn.  (The field itself is a layer a hundredth of the part thick:
            drawn as it is, it comes out as a few disconnected fragments at the resolution of the viewport.)  A
            vertex belongs to the patch when the nearest point of the surface is in it, that is, when its distance
            to the patch equals its distance to the whole surface (to within the cover of the samples). '''
        from fieldes.stdlib.fea import colored
        value = self.patch - self.whole                 # (<= 0 on the patch)
        shown = colored(self.shape, value, range=(0.0, float(BC_CATEGORIES)),
                        label='bc:{}'.format(SELECTION_CATEGORY), colormap='bc')
        shown._color_cutoff = 0.0              # (where the value is above this, the surface is not drawn)
        return shown

    def __repr__(self):
        return 'select_surface(seed={}, angle={:g}, mode={!r}): {} samples {:.3g} apart'.format(
            tuple(round(c, 3) for c in self.seed), self.angle, self.mode, self.samples, self.spacing)


@content_cached('select_surface', limit=16)
def _patch(shape, seed, angle, mode, radius, spacing, lo, hi, script_vars):
    ''' (distance to the samples of the patch, the surface they make as a field, how many, distance of the seed from the surface,
        the patch's box, the spacing, whether the walk was cut short) '''
    pad = 0.03 * max(hi[i] - lo[i] for i in range(3))
    lo3 = (ctypes.c_double * 3)(*[float(a) - pad for a in lo])
    hi3 = (ctypes.c_double * 3)(*[float(b) + pad for b in hi])
    seed3 = (ctypes.c_double * 3)(*seed)
    known = _script_vars()
    if known is not None:
        patch = lib.libfive_surface_select(shape.ptr, known[0], known[1], known[2], lo3, hi3, seed3, angle,
                                           MODES[mode], radius, spacing)
    else:
        patch = lib.libfive_surface_select(shape.ptr, None, None, 0, lo3, hi3, seed3, angle, MODES[mode], radius, spacing)
    if not patch:
        raise ValueError('select_surface: ' + lib.libfive_lattice_last_error().decode('utf-8', 'replace'))
    try:
        p = patch.contents
        count = int(p.count)
        xyz = [p.points[i] for i in range(3 * count)]
        box = (tuple(min(xyz[3 * i + k] for i in range(count)) for k in range(3)),
               tuple(max(xyz[3 * i + k] for i in range(count)) for k in range(3)))
        field = lib.libfive_points_distance(p.points, count)
        # (the surface the samples make, with their normals: the field a lattice on the selection is laid out on)
        surface = lib.libfive_points_surface(p.points, p.normals, count, float(p.spacing))
        distance, used, stopped = float(p.seed_distance), float(p.spacing), bool(p.stopped)
    finally:
        lib.libfive_surface_patch_delete(patch)
    if not field or not surface:
        raise ValueError('select_surface: ' + lib.libfive_lattice_last_error().decode('utf-8', 'replace'))
    return Shape(field), Shape(surface), count, distance, box, used, stopped


def select_surface(shape, seed, angle=15.0, mode='flat', radius=None, resolution=None, bounds=None):
    ''' The patch of the surface of `shape` around the point `seed`, found by a flood fill over the surface made from the
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
        fixed(selection), force(selection, ...), lattice_surface_conform(selection, ...). '''
    shape = _s(shape)
    if mode not in MODES:
        raise ValueError("select_surface: mode is 'flat' or 'smooth'")
    seed = tuple(float(c) for c in seed)
    if len(seed) != 3:
        raise ValueError('select_surface: seed is a point (x, y, z)')
    if not 0.0 < float(angle) <= 90.0:
        raise ValueError('select_surface: angle is between 0 and 90 degrees')
    lo, hi = _shape_bounds(shape, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    res = float(resolution) if resolution else max(0.1, 150.0 / size)
    spacing = 1.0 / res
    field, surface, count, distance, box, used, stopped = _patch(
        shape, seed, float(angle), mode, float(radius or 0.0), spacing, tuple(lo), tuple(hi), _vars_key())
    cover = COVER * used
    # (the field is negative in a layer across the patch, the thinnest one the samples cover: what makes it a region at all, as the
    # surface itself is where the field is zero.  Not a setting: a surface has no thickness)
    t = 2.0 * cover
    if stopped:
        print('select_surface: the walk was cut short at {} samples: raise resolution (fewer steps per mm) or give a '
              'radius'.format(count))
    if distance > 0.05 * size:
        print('select_surface: the seed is {:.3g} mm from the surface (the patch is the nearest one)'.format(distance))
    band = field - t / 2.0            # (named: a temporary would free its tree before the copy is made)
    out = SurfaceSelection(lib.libfive_tree_copy(band.ptr))
    out.shape, out.seed, out.angle, out.mode = shape, seed, float(angle), mode
    out.samples, out.spacing, out.cover = count, used, cover
    out.patch = field - cover
    # the surface itself, as a field: the height above the patch along its normal (zero on it, positive on the side it faces) -- what a
    # lattice laid on the selection is laid out on.  Nothing of the shape it was picked on is in it
    out.surface = surface
    out.whole = shape.abs()
    out._bounds = (tuple(box[0][i] - t for i in range(3)), tuple(box[1][i] + t for i in range(3)))
    _light_up(out)
    return out


def _light_up(selection):
    ''' A selection is shown lit up (the colour of the "selected" category of the application's boundary-condition colours) '''
    selection._color_field = Shape.wrap(float(SELECTION_CATEGORY))
    selection._color_range = (0.0, float(BC_CATEGORIES))
    selection._color_label = 'bc:{}'.format(SELECTION_CATEGORY)
    selection._color_map = 'bc'


class BodiesSurface(SurfaceSelection):
    ''' The surface of a body where it meets other bodies (see surface_from_bodies): a surface, with the attributes of a
        SurfaceSelection (.shape the first body, .patch, .whole, .surface, .cover, .spacing) and .others (the bodies that
        select it, a list: empty for the whole surface), .tolerance '''

    def __repr__(self):
        if not self.others:
            return 'surface_from_bodies: the whole surface of the body'
        return 'surface_from_bodies: the surface of the first body within {:.3g} mm of {} other bod{}'.format(
            self.tolerance, len(self.others), 'y' if len(self.others) == 1 else 'ies')


def _flat(bodies):
    ''' The bodies as a flat list of shapes: a list or tuple among them (surface_from_bodies(a, [b, c])) is its members '''
    out = []
    for b in bodies:
        if isinstance(b, (list, tuple)):
            out.extend(_flat(b))
        elif b is None:
            raise ValueError('surface_from_bodies: a body is None (is a model of the script missing?)')
        else:
            out.append(_s(b))
    return out


def surface_from_bodies(body, *others, tolerance=None, bounds=None):
    ''' The surface of `body` where it meets `others`, the bodies that follow: the parts of its surface that intersect them (that
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
        even touch). '''
    first = _s(body)
    rest = _flat(others)
    if tolerance is not None:
        if not isinstance(tolerance, (int, float)) or isinstance(tolerance, bool) or not float(tolerance) >= 0.0:
            raise ValueError('surface_from_bodies: tolerance is a distance in mm (0 or more), or None')
    lo, hi = _shape_bounds(first, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    # (how far from the surface the layer reaches and how close counts as meeting: the same share of the size as the
    # selection of a walk takes, 0.85 of the spacing of its samples, 150 samples along the longest side)
    cover = COVER * size / 150.0
    tol = cover if tolerance is None else float(tolerance)
    whole = first.abs()
    patch = whole - cover                         # (negative in a layer along the whole surface)
    if rest:
        near = rest[0]
        for other in rest[1:]:
            near = near.min(other)                # (the union of the others: negative inside, a distance outside)
        patch = patch.max(near - tol)             # ... and only where one of them is within `tolerance`
    out = BodiesSurface(lib.libfive_tree_copy(patch.ptr))
    out.shape, out.others, out.tolerance = first, rest, tol
    out.seed, out.angle, out.mode, out.samples = None, 0.0, 'bodies', 0
    out.spacing, out.cover = size / 150.0, cover
    out.patch = patch
    out.surface = first                           # (the field a lattice on the selection is laid out on: zero on the surface, positive outside)
    out.whole = whole
    # the box the selection lies in: the first body's, cut down to where the others are (and a layer round it)
    box_lo, box_hi = list(lo), list(hi)
    if rest:
        others_box = []
        for other in rest:
            try:
                others_box.append(_shape_bounds(other, None))
            except ValueError:
                others_box = None                  # (a body whose extent cannot be found: the first body's box is what there is)
                break
        if others_box:
            for i in range(3):
                box_lo[i] = max(box_lo[i], min(b[0][i] for b in others_box) - tol)
                box_hi[i] = min(box_hi[i], max(b[1][i] for b in others_box) + tol)
            if any(box_hi[i] < box_lo[i] for i in range(3)):
                print('surface_from_bodies: the others do not reach the body (their boxes are apart, by more than {:.3g} mm): '
                      'nothing is selected'.format(tol))
                box_lo, box_hi = list(lo), list(hi)
    layer = 2.0 * cover
    out._bounds = (tuple(box_lo[i] - layer for i in range(3)), tuple(box_hi[i] + layer for i in range(3)))
    _light_up(out)
    return out


# (what these make is not something to drag: the model tree knows it before they have run, from the function that makes it)
select_surface._makes_no_handles = True
surface_from_bodies._makes_no_handles = True
