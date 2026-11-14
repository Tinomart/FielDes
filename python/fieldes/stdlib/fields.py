'''
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
'''
import ctypes
import math
import numbers
import os
import sys

from fieldes.ffi import lib, libfive_vec3_t
from fieldes.shape import Shape
from fieldes.stdlib.content_cache import content_cached
from fieldes.stdlib import distance_field as _distance_field
from fieldes.stdlib.fieldargs import (is_field, present, positive, number_or_field, lowest, highest,
                                      returns_field, returns_body)

__all__ = [
    'x_field', 'y_field', 'z_field', 'radial_field', 'angle_field', 'polar_field',
    'distance_to_point', 'distance_to_points', 'distance_to_line',
    'distance_to_segment', 'distance_to_polyline', 'distance_to_plane',
    'distance_to_surface', 'depth_below', 'signed_distance',
    'ramp', 'clamp', 'lerp', 'smoothstep', 'step_field', 'normalize',
    'remap_field', 'attractor', 'wave', 'sum_fields', 'mix',
    'add_fields', 'subtract_fields', 'multiply_fields', 'divide_fields', 'power_field', 'min_fields', 'max_fields',
    'abs_field', 'negate_field', 'sqrt_field', 'square_field', 'field_from_body', 'body_from_field',
    'low_level_field', 'low_level_body', 'maximum', 'minimum',
    'thicken', 'shell_inside', 'shell_outside', 'shell_centered', 'offset_by',
    'smooth_union', 'smooth_intersection',
    'smooth_difference', 'chamfer_union', 'union_all', 'intersection_all',
    'repeat', 'repeat_polar', 'mirror_x', 'mirror_y', 'mirror_z',
    'twist_z', 'bend_z',
    'evaluate', 'sample_grid', 'field_range', 'volume_of', 'mass_properties',
    'exact_distance', 'offset_exact', 'shell_exact', 'round_edges', 'fillet', 'chamfer', 'smooth',
    'gradient_field', 'gradient_magnitude', 'normal_field', 'overhang_angle',
    'overhang_mask', 'wall_thickness', 'curvature_field',
    'field_from_points', 'field_from_csv', 'noise_field', 'texture',
]

X, Y, Z = Shape.X, Shape.Y, Shape.Z


def _s(v):
    ''' A Shape from a number or a Shape '''
    return Shape.wrap(v)


def _dist(v):
    ''' A Shape for the operations that need a DISTANCE (offsets, shells, thickening). The field of a part imported
        from STEP is exact in sign but outside and inside it is only a lower bound of the distance to the part (the
        distance to the infinite surfaces its faces lie on, and the cells it is built from end in places where there
        is no face): an offset or a shell of it grows blocks and fins, and a thickening has stray walls inside. So an
        imported part is replaced by its exact distance (exact_distance(): the distance read from its own field, by walking along the gradient to the surface -- nothing is meshed), which is
        what these operations mean. A part that was moved, turned or scaled (position=/rotation=/scale=, transforms in the
        script) has the same placement on its distance; one whose faces are dragged (handles()) has the distance of
        its numbers when the script was run, and what dragging changes since. Set FIELDES_NO_EXACT_OFFSETS to use
        the part's own field. '''
    s = _s(v)
    get = getattr(s, '_distance_of', None)
    if get is not None and not os.environ.get('FIELDES_NO_EXACT_OFFSETS'):
        try:
            return get()
        except Exception as e:
            from fieldes.stdlib.result_cache import _log
            _log("fieldes: the exact distance of this part could not be made (%s); its own field is used\n" % e)
    return s


_BODY_KINDS = ('solid', 'profile', 'lattice', 'import', 'simulation')


def exact_of(v, resolution=None):
    ''' THE FIELD OF A BODY (H190, the user, 2026-11-12, capitals: "ALL BODIES ARE ALWAYS TRUE DISTANCE FIELDS. THAT IS THE DEFINITION OF THE TYPE ... HAVE IT ENFORCED EVERYWHERE AS A REQUIREMENT as a part
        of the type itself"; 2026-11-13: "MAKE A BODYS FIELD ALWAYS A TRUE SIGNED DISTANCE FIELD"): the signed distance (mm, negative inside) from every point to the surface of the body -- its value, not a
        bound of it.  This is `Body.field` (fieldes.shape.Body), and what every reading of a body is.  The tree a body is made as is not always it: the min and max of a boolean are bounds (inside a
        union, outside an intersection or a difference), the weighted sum of a smoothing, the field of a cylinder outside its rim, a scale that is not the same in all directions, a blend are not -- and
        that tree is what is meshed (the surface of the two is the same) and what the next body is built from.
        In order: the distance the body carries (`_distance_of`: an optimized part, an imported one, a part whose faces are pulled, a combination of those), the body itself when it says it is exact
        (`_exact`), and else the distance to the body's own surface -- the surface of the body meshed finely (200 samples across it, a cell of a half percent of its size: the edges and corners
        are kept) and the exact distance to those triangles, which the kernel makes (a closest-point search, nothing sampled onto a grid), made once for the body and kept.  A field that is not a body
        (a ramp, a noise) is itself.  `resolution` samples per mm for that surface, when it is to be finer '''
    s = _s(v)
    get = getattr(s, '_distance_of', None)
    if get is not None and not os.environ.get('FIELDES_NO_EXACT_OFFSETS'):
        try:
            return get()
        except Exception as e:
            from fieldes.stdlib.result_cache import _log
            _log("fieldes: the exact distance of this part could not be made (%s); its surface is used\n" % e)
    if getattr(s, '_exact', False) or getattr(s, '_tessellated_import', False):
        return s                    # (the distance to its own triangles already)
    from fieldes.kinds import kind_of
    if kind_of(s) not in _BODY_KINDS:
        return s
    from fieldes.stdlib.importing import _tessellate_field
    box = getattr(s, '_bounds', None)
    if not box:
        box = find_extent(s)
    if not box:
        # (a body with no extent -- open on a side, repeated without end: the region of the scene is meshed, as an export of such a body does)
        try:
            from fieldes.stdlib.exporting import _host
            region = _host('__bounds', None)
            if region is not None:
                box = (tuple(float(c) for c in region[0]), tuple(float(c) for c in region[1]))
        except Exception:
            box = None
    if not box:
        return s                    # (no box to mesh it in, and no region of the scene: the body's own tree is all there is)
    lo, hi = tuple(float(c) for c in box[0]), tuple(float(c) for c in box[1])
    dims = sorted(max(hi[i] - lo[i], 1e-9) for i in range(3))
    size = dims[2] or 1.0
    if resolution:
        res = float(resolution)
    else:
        # (200 samples across the longest side, and for a thin body -- a row of small copies, a plate with a thin rim -- fine enough across the thinnest: 40 samples, as far as eight million cells of its box go)
        thin = min(40.0 / dims[0], (8.0e6 / (dims[0] * dims[1] * dims[2])) ** (1.0 / 3.0))
        res = max(200.0 / size, thin)
    oracle = _tessellate_field(s, lo, hi, res, _vars_key())
    # (the SIZE of the distance is the surface's, meshed; the SIDE is the body's own tree's, which a mesh that is too coarse for a thin or a small piece cannot lose: a point inside a copy of a row of copies
    # is inside, whatever the mesh made of the copy)
    out = abs(oracle) * Shape.compare(s, 0)
    out._bounds = oracle._bounds
    out._exact = True
    out._display_field = s          # (what is drawn is the body's own tree: the same surface, a cheaper evaluation)
    return out


def _script_vars():
    ''' The numbers of the script's var()s, inside the application: (trees, values, count) for the library
        calls that take them, or None (outside it, or when the script has none) '''
    try:
        from fieldes.app_support import _var_values
        return _var_values()
    except Exception:
        return None


def _vars_key():
    ''' The script's var() numbers as a cache key (a mesh of a shape with var()s depends on them) '''
    known = _script_vars()
    return None if known is None else tuple(known[1][i] for i in range(known[2]))


def render_mesh(shape, region, res):
    ''' The library's mesh of a shape over a region (a libfive_region_t) at `res` samples per mm: the
        pointer libfive_tree_render_mesh gives (free it with libfive_mesh_delete), or None. A shape with
        var()s is meshed with the numbers they have in the script. '''
    known = _script_vars() if hasattr(lib, 'libfive_tree_render_mesh_vars') else None
    if known is not None:
        return lib.libfive_tree_render_mesh_vars(shape.ptr, region, res, known[0], known[1], known[2]) or None
    return lib.libfive_tree_render_mesh(shape.ptr, region, res) or None


def _coords(p, n=3):
    ''' A point whose coordinates are numbers or fields (a field is kept: the point is then wherever the field says);
        a Point (point(position=(x, y, z))) is its coordinates '''
    if hasattr(p, 'xyz'):
        p = p.xyz
    p = tuple(c if isinstance(c, Shape) else float(c) for c in p)
    if len(p) != n:
        raise ValueError('expected a point with {} coordinates, got {}'.format(n, p))
    return p


def _vec(p, n=3):
    p = tuple(float(c) for c in p)
    if len(p) != n:
        raise ValueError('expected a point with {} coordinates, got {}'.format(n, p))
    return p


def _minimum(items):
    ''' Balanced min over a list of Shapes (shallower trees prune better) '''
    items = list(items)
    if not items:
        raise ValueError('nothing to combine')
    while len(items) > 1:
        items = [items[i].min(items[i + 1]) if i + 1 < len(items) else items[i]
                 for i in range(0, len(items), 2)]
    return items[0]


def _maximum(items):
    items = list(items)
    if not items:
        raise ValueError('nothing to combine')
    while len(items) > 1:
        items = [items[i].max(items[i + 1]) if i + 1 < len(items) else items[i]
                 for i in range(0, len(items), 2)]
    return items[0]


################################################################################
# Coordinates

def x_field():
    ''' The x coordinate as a field (mm) '''
    return X()


def y_field():
    ''' The y coordinate as a field (mm) '''
    return Y()


def z_field():
    ''' The z coordinate as a field (mm) '''
    return Z()


def _point_radius(center, radius):
    ''' The radius a field round a point is measured from: `radius` when it is given, else the influence the point itself says (`point(position=(x, y, z), influence=r)`), else none --
        the plain distance from the point '''
    return getattr(center, '_influence', None) if radius is None else radius


def _radial_distance(center, axis):
    ''' The plain distance from an axis through `center` (cylindrical radius, mm), whatever the influence of the centre is '''
    c = _coords(center)
    x, y, z = X() - c[0], Y() - c[1], Z() - c[2]
    a, b = {'x': (y, z), 'y': (x, z), 'z': (x, y)}[axis]
    return (a.square() + b.square()).sqrt()


def radial_field(center, axis='z'):
    ''' Distance from the axis through the point `center` (cylindrical radius, mm): a field made of a POINT, as `distance_to_point` is -- right-click a point, and radial_field is in its menu.  `center` is
        a `point(position=(x, y, z))` (its coordinates may be fields), the Point class: a vector -- (x, y, z) -- is not a point, and is refused.  The field has no radius of its own:
        the INFLUENCE of the point, `point(position=(x, y, z), influence=r)`, is the radius, as it is for `distance_to_point` -- the field is then the distance from the TUBE of
        that radius round the axis: negative inside it (-r on the axis), 0 on it, positive outside.  A point that says no influence gives the plain distance from the axis '''
    if not hasattr(center, 'xyz'):
        raise TypeError("radial_field takes a point -- `point(position=(x, y, z))`, a model of its own, whose influence is the radius -- and not the vector %r: make the point (Primitives > Points, "
                        "or right-click the place) and give it, `radial_field(point_1, 'z')`" % (center,))
    out = _radial_distance(center, axis)
    radius = getattr(center, '_influence', None)
    return out if radius is None else out - radius


def angle_field(center=(0, 0, 0), axis='z'):
    ''' Angle around an axis through `center`, in radians (-pi..pi) '''
    c = _coords(center)
    x, y, z = X() - c[0], Y() - c[1], Z() - c[2]
    a, b = {'x': (y, z), 'y': (z, x), 'z': (x, y)}[axis]
    return b.atan2(a)


def polar_field(center=(0, 0, 0), axis='z'):
    ''' Angle from an axis through `center`, in radians (0..pi): 0 along the
        axis, pi/2 at its equator (spherical coordinates) '''
    c = _coords(center)
    x, y, z = X() - c[0], Y() - c[1], Z() - c[2]
    a, b, w = {'x': (y, z, x), 'y': (z, x, y), 'z': (x, y, z)}[axis]
    return (a.square() + b.square()).sqrt().atan2(w)


################################################################################
# Distances

def distance_to_point(p, radius=None):
    ''' Euclidean distance to a point (its coordinates may be fields: a point that is where a field says).  With a `radius` -- or the influence of the point that is given,
        `point(position=(x, y, z), influence=r)`, which is the radius in mm -- it is the distance from the SPHERE of that radius round the point: negative inside it (-r at the point),
        0 on it, positive outside: a sphere's value, as a field.  A point that says no influence, and no radius, is the plain distance from it '''
    radius = _point_radius(p, radius)
    p = _coords(p)
    out = ((X() - p[0]).square() + (Y() - p[1]).square() + (Z() - p[2]).square()).sqrt()
    return out if radius is None else out - radius


def distance_to_points(points):
    ''' Distance to the nearest of several points '''
    return _minimum(distance_to_point(p) for p in points)


def distance_to_line(point, direction):
    ''' Distance to an infinite line through `point` along `direction` '''
    p = _vec(point)
    d = _vec(direction)
    n = math.sqrt(sum(c * c for c in d))
    d = tuple(c / n for c in d)
    q = (X() - p[0], Y() - p[1], Z() - p[2])
    t = q[0] * d[0] + q[1] * d[1] + q[2] * d[2]
    return ((q[0] - t * d[0]).square() + (q[1] - t * d[1]).square() +
            (q[2] - t * d[2]).square()).sqrt()


def _segment(a, b, x, y, z):
    ab = tuple(b[i] - a[i] for i in range(3))
    L2 = sum(c * c for c in ab)
    q = (x - a[0], y - a[1], z - a[2])
    if L2 <= 0:
        return (q[0].square() + q[1].square() + q[2].square()).sqrt()
    h = ((q[0] * ab[0] + q[1] * ab[1] + q[2] * ab[2]) / L2).max(0).min(1)
    return ((q[0] - h * ab[0]).square() + (q[1] - h * ab[1]).square() +
            (q[2] - h * ab[2]).square()).sqrt()


def distance_to_segment(a, b):
    ''' Distance to the line segment from a to b '''
    return _segment(_vec(a), _vec(b), X(), Y(), Z())


def distance_to_polyline(points, closed=False):
    ''' Distance to a polyline (a curve given by its points) '''
    pts = [_vec(p) for p in points]
    if len(pts) < 2:
        raise ValueError('a polyline needs at least 2 points')
    pairs = list(zip(pts[:-1], pts[1:]))
    if closed:
        pairs.append((pts[-1], pts[0]))
    x, y, z = X(), Y(), Z()
    return _minimum(_segment(a, b, x, y, z) for a, b in pairs)


def distance_to_plane(point=(0, 0, 0), normal=(0, 0, 1)):
    ''' Signed distance to a plane: positive on the side the normal points to (the point's and the normal's
        coordinates may be fields: a plane that moves or tilts from place to place) '''
    p = _coords(point)
    n = _coords(normal)
    if any(isinstance(c, Shape) for c in n):
        L = sum((Shape.wrap(c).square() for c in n[1:]), Shape.wrap(n[0]).square()).sqrt()
    else:
        L = math.sqrt(sum(c * c for c in n))
    return ((X() - p[0]) * n[0] + (Y() - p[1]) * n[1] + (Z() - p[2]) * n[2]) / L


def distance_to_surface(shape):
    ''' Unsigned distance to a shape's surface (the shape's field is used as
        the distance, so this is exact for exact distance fields such as
        spheres, boxes, imported meshes) '''
    return abs(_s(shape))


def depth_below(shape):
    ''' Depth below a shape's surface: 0 on the surface and outside, growing
        towards its interior (e.g. the thickness of material above a point) '''
    return (-_s(shape)).max(0)


def signed_distance(shape):
    ''' A shape's signed distance field (negative inside) -- the shape
        itself, named for readability '''
    return _s(shape)


################################################################################
# Value maps

def clamp(field, lo, hi):
    ''' Limits a field to [lo, hi] '''
    return _s(field).max(lo).min(hi)


def ramp(field, input_range, output_range, clamped=True):
    ''' Linear map of a field: input_range=(a, b) -> output_range=(va, vb); each end is a number or a field (a ramp
        that starts and ends where other fields say).  Clamped by default (values beyond the input range hold the end
        values), e.g. ramp(z_field(), (0, 50), (2, 0.5)) is 2 at z = 0,
        falling to 0.5 at z = 50 and above. '''
    a, b = (number_or_field(v, 'an end of the input range') for v in input_range)
    va, vb = (number_or_field(v, 'an end of the output range') for v in output_range)
    if not is_field(a) and not is_field(b) and a == b:
        raise ValueError('ramp: the input range is empty')
    t = (_s(field) - a) / (b - a)
    if clamped:
        t = t.max(0).min(1)
    return va + (vb - va) * t


def remap_field(field, input_range, output_range, clamped=True):
    ''' Same as ramp() '''
    return ramp(field, input_range, output_range, clamped)


def normalize(field, lo, hi):
    ''' Maps [lo, hi] to [0, 1] (clamped) '''
    return ramp(field, (lo, hi), (0, 1))


def lerp(a, b, t):
    ''' Linear interpolation between two values or fields: a at t = 0, b at
        t = 1 (t may itself be a field) '''
    return _s(a) + (_s(b) - _s(a)) * _s(t)


def mix(a, b, t):
    ''' Same as lerp() '''
    return lerp(a, b, t)


def smoothstep(field, edge0, edge1):
    ''' 0 below edge0, 1 above edge1, a smooth S-curve between '''
    t = ((_s(field) - edge0) / (edge1 - edge0)).max(0).min(1)
    return t * t * (3 - 2 * t)


def step_field(field, edge):
    ''' 0 below the edge, 1 above (a sharp step; see smoothstep) '''
    return (_s(field).compare(edge) + 1) / 2


def attractor(points, radius, falloff='smooth', strength=1.0):
    ''' (`radius` and `strength` are numbers or fields.)  A field that is `strength` at the given points (or curves: pass a
        distance field instead of points) and falls to 0 at `radius`.
        falloff: 'linear', 'smooth' (smoothstep) or 'gauss'. '''
    if isinstance(points, Shape) and not hasattr(points, 'xyz'):
        d = points
    else:
        pts = [points] if hasattr(points, 'xyz') else list(points)
        if pts and isinstance(pts[0], numbers.Number):
            pts = [pts]
        d = distance_to_points(pts)
    r = number_or_field(radius, 'the radius')
    if falloff == 'linear':
        f = (1 - d / r).max(0)
    elif falloff == 'gauss':
        f = (-(d / (0.4 * r)).square()).exp()
    elif falloff == 'smooth':
        t = (1 - d / r).max(0).min(1)
        f = t * t * (3 - 2 * t)
    else:
        raise ValueError("attractor: falloff is 'linear', 'smooth' or 'gauss'")
    return strength * f


def _sine(axis='x', period=10.0, amplitude=1.0, phase=0.0):
    ''' A sine along an axis (or a field): amplitude * sin(2 pi s / period + phase) '''
    s = {'x': X(), 'y': Y(), 'z': Z()}[axis] if isinstance(axis, str) else _s(axis)
    return amplitude * (s * (2 * math.pi / period) + phase).sin()


def wave(axis='x', period=10.0, amplitude=1.0, phase=0.0):
    ''' A 3D wave: the height above a wavy sheet, z - amplitude * sin(2 pi s / period + phase), s the coordinate along
    the axis.  It is zero on the sheet and grows with z, so the section viewer shows the sheet cut by its disc and the
    colours change as the disc is dragged up and down.  (Given a field instead of an axis name, it is the plain sine
    of that field, amplitude * sin(2 pi field / period + phase).) '''
    if not isinstance(axis, str):
        return _sine(axis, period, amplitude, phase)
    return Z() - _sine(axis, period, amplitude, phase)


def sum_fields(*fields):
    ''' Sum of several fields '''
    out = _s(fields[0])
    for f in fields[1:]:
        out = out + f
    return out


################################################################################
# Arithmetic on fields
#
# A number is a field with the same value everywhere, so every one of these takes numbers and fields alike, and the same
# can be written with the operators (a + b, a - b, a * b, a / b, a ** b, abs(a), -a).  What they make is a FIELD, also of
# bodies: the values of a body (its distance to its surface) are multiplied, divided ... like any other values.

def _fold(fields, name, op):
    if not fields:
        raise TypeError('%s needs at least one field' % name)
    out = _s(fields[0])
    for f in fields[1:]:
        out = op(out, f)
    return out


def add_fields(*fields):
    ''' The sum of fields (and numbers): a + b + ... at every point '''
    return _fold(fields, 'add_fields', lambda a, b: a + b)


def subtract_fields(first, *others):
    ''' The first field minus the others: a - b - ... at every point '''
    return _fold((first,) + others, 'subtract_fields', lambda a, b: a - b)


def multiply_fields(*fields):
    ''' The product of fields (and numbers): a * b * ... at every point '''
    return _fold(fields, 'multiply_fields', lambda a, b: a * b)


def divide_fields(first, *others):
    ''' The first field divided by the others: a / b / ... at every point (NaN where a divisor is zero) '''
    return _fold((first,) + others, 'divide_fields', lambda a, b: a / b)


def power_field(base, *exponents):
    ''' The first field to the power of the next: (a ** b) ** ... at every point (NaN where a negative value meets a
        fractional power) '''
    return _fold((base,) + exponents, 'power_field', lambda a, b: a ** b)


def min_fields(*fields):
    ''' The smallest of the fields (and numbers) at every point '''
    if not fields:
        raise TypeError('min_fields needs at least one field')
    return _minimum([_s(f) for f in fields])


def max_fields(*fields):
    ''' The largest of the fields (and numbers) at every point '''
    if not fields:
        raise TypeError('max_fields needs at least one field')
    return _maximum([_s(f) for f in fields])


def abs_field(field):
    ''' The absolute value of a field at every point '''
    return _s(field).abs()


def negate_field(field):
    ''' A field with its sign changed: -a at every point '''
    return -_s(field)


def sqrt_field(field):
    ''' The square root of a field at every point (NaN where it is negative) '''
    return _s(field).sqrt()


def square_field(field):
    ''' A field times itself at every point '''
    return _s(field).square()


def field_from_body(body):
    ''' The values of a body as a FIELD: the same number at every point as the body (its distance to its surface, negative
        inside), but not a body -- it is not drawn, it is not a part, and it is free to be multiplied, divided or powered
        and still be a valid field.  (A body itself always keeps its true scale: it is a distance, which a factor would
        break.  Selecting the field shows it in the section viewer.) '''
    s = _s(body)
    # (the field of a part imported from a STEP file is exact in sign only -- outside it is the distance to the infinite surfaces of its
    # faces, the boxy mitres of a CSG field -- so the field is its exact distance: what "its distance to its surface" says)
    out = Shape(lib.libfive_tree_copy(_dist(s).ptr))
    out._kind = 'field'
    out._field_origin = s._field_origin if s._field_origin is not None else s       # (the viewer starts at the body)
    return out


def body_from_field(field, level=0.0):
    ''' A BODY from a field: what is inside where the field is below `level` (0 by default), its surface where the field
        equals `level` -- the other way of field_from_body().  The result is drawn and is a part like any other (it can be
        offset, shelled, filled with a lattice, analysed, exported).

            noise = noise_field(12, 3)                                  # a field: not drawn
            lumps = body_from_field(noise, 0.2)                         # the places where it is below 0.2: a body

        The value of the body is the field's less `level`, so it is a true distance to its surface only where the field is
        one (field_from_body of a body, a distance_to_point ...): the surface is exact, an offset of a body made from a
        field that only roughly measures distance is about right, not exact.  `level` may be a field too. '''
    f = _s(field)
    if level is None or (isinstance(level, (int, float)) and level == 0):
        out = Shape(lib.libfive_tree_copy(f.ptr))
    else:
        grown = f - _s(level)                 # (named: a temporary would free its tree before the copy is made)
        out = Shape(lib.libfive_tree_copy(grown.ptr))
    return out


def maximum(*terms):
    ''' The largest of several fields, point by point (Python's own max cannot compare fields; `.max` of one takes one
        other): maximum(x - 6, -6 - x, y - 6) is the intersection of three half-spaces.  See minimum(). '''
    if not terms:
        raise ValueError('maximum() needs at least one field')
    out = _s(terms[0])
    for t in terms[1:]:
        out = out.max(_s(t))
    return out


def minimum(*terms):
    ''' The smallest of several fields, point by point: minimum(a, b, c) is the union of three bodies.  See maximum(). '''
    if not terms:
        raise ValueError('minimum() needs at least one field')
    out = _s(terms[0])
    for t in terms[1:]:
        out = out.min(_s(t))
    return out


def _from_logic(logic, who, kind):
    ''' What low_level_field and low_level_body do: `logic` called once with the coordinates, its answer made a shape '''
    import inspect
    if not callable(logic):
        raise TypeError('{}(logic): logic is a function of the coordinates -- def logic(x, y, z): return ... -- or a '
                        'lambda: lambda x, y, z: x * x + y * y + z * z - 100'.format(who))
    # (as many coordinates as the function takes, one to three: a function of x and y is a 2D shape)
    try:
        count = len([p for p in inspect.signature(logic).parameters.values()
                     if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD) and p.default is p.empty])
    except (TypeError, ValueError):
        count = 3
    if not 1 <= count <= 3:
        raise TypeError('{}: logic takes the coordinates -- (x), (x, y) or (x, y, z) -- and yours takes {} arguments'
                        .format(who, count))
    coordinates = (Shape.X(), Shape.Y(), Shape.Z())[:count]
    value = logic(*coordinates)
    if isinstance(value, (int, float)):
        value = Shape.wrap(float(value))
    if not isinstance(value, Shape):
        raise TypeError('{}: your logic returned {} -- it must return a field: a number, or an expression of the coordinates '
                        '(+ - * / **, .sqrt() .square() .sin() .cos() .abs(), a.max(b) a.min(b), maximum(...) minimum(...))'
                        .format(who, type(value).__name__))
    out = Shape(lib.libfive_tree_copy(value.ptr))
    if kind == 'field':
        out._kind = 'field'
    return out


def low_level_field(logic):
    ''' A FIELD from your own logic, in one line or a few: `logic` is a function of the coordinates -- it is called ONCE, with
        x, y and z as fields (a function of x and y alone is a 2D field) -- and returns the value you want at that point:

            def ripple(x, y, z):
                distance = (x.square() + y.square()).sqrt()          # the distance from the z axis
                return (distance * 0.6).sin() * 3                    # a ring pattern, up to 3

            waves = low_level_field(ripple)

        Write the math with + - * / **, the methods .sqrt() .square() .abs() .sin() .cos() .tan() .exp() .log(), a.max(b) and
        a.min(b) (or maximum(a, b, c) and minimum(a, b, c)); a number is a constant field.  What you get is a field like any
        other: it is not drawn (display it: its icon opens the field viewer), it goes wherever a number goes (`offset(part,
        waves)`), a lattice's cell size, a load's profile.  low_level_body() is the same for a body; body_from_field() makes a
        body of a field.  In FielDes: right-click, Parametric shapes, New field, low_level_field, writes the function and the call for you. '''
    return _from_logic(logic, 'low_level_field', 'field')


def low_level_body(logic):
    ''' A BODY from your own logic: `logic(x, y, z)` returns a number that is NEGATIVE inside the body, zero on its surface and
        positive outside -- ideally the distance to the surface, so that offsets and shells come out right:

            def ball(x, y, z):
                return (x.square() + y.square() + z.square()).sqrt() - 20          # a ball of radius 20

            part = low_level_body(ball)

        or `low_level_body(lambda x, y, z: maximum(x - 6, -6 - x, y - 6, -6 - y, z - 6, -6 - z))` for a cube of 12.  The body is
        drawn and is a part like any other.  See low_level_field() for the operations; a field that is not a body is
        low_level_field() (and body_from_field() makes a body of it). '''
    return _from_logic(logic, 'low_level_body', 'solid')


################################################################################
# Geometry from fields

def thicken(field, thickness):
    ''' A solid wall of the given thickness centred on a field's zero
        surface (e.g. a plane, a TPMS surface, a shape's skin); the
        thickness may be a field '''
    return abs(_dist(field)) - _s(thickness) / 2


def shell_inside(shape, thickness):
    ''' A hollow shell: the part of `shape` within `thickness` of its surface '''
    s = _dist(shape)
    return s.max(-(s + thickness))


def shell_outside(shape, thickness):
    ''' A skin grown outwards from the surface by `thickness` '''
    s = _dist(shape)
    return (s - thickness).max(-s)


def shell_centered(shape, thickness):
    ''' A shell straddling the surface (half inside, half outside) '''
    return thicken(shape, thickness)


def offset_by(shape, distance):
    ''' Offsets a shape by a distance -- which may be a field, e.g. grow a
        part by 0.02 mm per MPa of stress: offset_by(part, 0.02 * stress) '''
    return _dist(shape) - distance


def smooth_union(a, b, radius):
    ''' Union with a rounded blend of the given radius where the shapes meet (a number, or a field: a blend that
        is round here and sharp there) '''
    a, b, k = _s(a), _s(b), number_or_field(radius, 'the radius')
    if not is_field(k):
        if k <= 0:
            return a.min(b)
    else:
        k = k.max(1e-9)                 # (where the field is 0 the union is sharp)
    h = (k - abs(a - b)).max(0) / k
    return a.min(b) - h * h * k / 4


def smooth_intersection(a, b, radius):
    ''' Intersection with a rounded blend '''
    return -smooth_union(-_s(a), -_s(b), radius)


def smooth_difference(a, b, radius):
    ''' a minus b, with a rounded blend along the cut '''
    return smooth_intersection(a, -_s(b), radius)


def chamfer_union(a, b, size):
    ''' Union with a 45-degree chamfer of the given size where the shapes meet '''
    a, b = _s(a), _s(b)
    return a.min(b).min((a + b - size) * math.sqrt(0.5))


def union_all(shapes, blend=0):
    ''' Union of many shapes (optionally blended) '''
    shapes = [_s(s) for s in shapes]
    if present(blend):
        out = shapes[0]
        for s in shapes[1:]:
            out = smooth_union(out, s, blend)
        return out
    return _minimum(shapes)


def intersection_all(shapes, blend=0):
    ''' Intersection of many shapes (optionally blended) '''
    shapes = [_s(s) for s in shapes]
    if present(blend):
        out = shapes[0]
        for s in shapes[1:]:
            out = smooth_intersection(out, s, blend)
        return out
    return _maximum(shapes)


def repeat(shape, spacing, center=(0, 0, 0)):
    ''' Repeats a shape infinitely on a grid.  spacing: a number or (sx, sy,
        sz), each a number or a field (the repeat is closer here and wider there); 0 along an axis leaves that axis alone.  The shape should fit
        inside one grid cell centred on `center`. '''
    if isinstance(spacing, (numbers.Number, Shape)):
        spacing = (spacing,) * 3
    c = _coords(center)
    coords = []
    for axis, (v, s, o) in enumerate(zip((X(), Y(), Z()), spacing, c)):
        if present(s):
            coords.append(((v - o + s / 2) % s) - s / 2 + o)
        else:
            coords.append(v)
    return _s(shape).remap(*coords)


def repeat_polar(shape, count, center=(0, 0), axis='z'):
    ''' Repeats a shape `count` times around the z axis; the shape should
        lie in the wedge around the +x direction '''
    if axis != 'z':
        raise ValueError('repeat_polar: only the z axis is supported')
    cx, cy = float(center[0]), float(center[1])
    x, y = X() - cx, Y() - cy
    r = (x.square() + y.square()).sqrt()
    step = 2 * math.pi / count
    a = ((y.atan2(x) + step / 2) % step) - step / 2
    return _s(shape).remap(r * a.cos() + cx, r * a.sin() + cy, Z())


def mirror_x(shape, x=0.0):
    ''' Mirror-symmetric copy: the half at x > x0 reflected onto the other '''
    return _s(shape).remap(abs(X() - x) + x, Y(), Z())


def mirror_y(shape, y=0.0):
    ''' Mirror-symmetric copy: the half at y > y0 reflected onto the other '''
    return _s(shape).remap(X(), abs(Y() - y) + y, Z())


def mirror_z(shape, z=0.0):
    ''' Mirror-symmetric copy: the half at z > z0 reflected onto the other '''
    return _s(shape).remap(X(), Y(), abs(Z() - z) + z)


def twist_z(shape, degrees_per_mm, center=(0, 0)):
    ''' Twists a shape about the z axis; the rate may be a field '''
    cx, cy = float(center[0]), float(center[1])
    a = _s(degrees_per_mm) * (math.pi / 180) * Z()
    x, y = X() - cx, Y() - cy
    return _s(shape).remap(x * a.cos() + y * a.sin() + cx, -x * a.sin() + y * a.cos() + cy, Z())


def bend_z(shape, radius, center=(0, 0)):
    ''' Bends a shape lying along +x around the z axis: x becomes arc length
        on a circle of the given radius '''
    cx, cy = float(center[0]), float(center[1])
    x, y = X() - cx, Y() - cy
    r = (x.square() + (y - radius).square()).sqrt()
    theta = x.atan2(radius - y)
    return _s(shape).remap(theta * radius + cx, radius - r + cy, Z())


################################################################################
# Evaluation

def _eval_many(shape, points):
    ''' Values of a shape at many points (uses the batch evaluator when the
        library has it) '''
    shape = _s(shape)
    pts = [tuple(float(c) for c in p) for p in points]
    n = len(pts)
    if n == 0:
        return []
    f = getattr(lib, 'libfive_tree_eval_points', None)
    if f is not None:
        xyz = (ctypes.c_float * (3 * n))(*[c for p in pts for c in p])
        out = (ctypes.c_float * n)()
        # (a script's var() numbers, when it runs in the application: without them every one reads as 0)
        known = None
        if hasattr(lib, 'libfive_tree_eval_points_vars'):
            try:
                from fieldes.app_support import _var_values
                known = _var_values()
            except Exception:
                known = None
        if known is not None:
            lib.libfive_tree_eval_points_vars(shape.ptr, xyz, n, out, known[0], known[1], known[2])
        else:
            f(shape.ptr, xyz, n, out)
        return list(out)
    return [lib.libfive_tree_eval_f(shape.ptr, libfive_vec3_t(*p)) for p in pts]


def evaluate(field, point_or_points):
    ''' A field's value at a point, or a list of values at many points '''
    p = list(point_or_points)
    if p and isinstance(p[0], numbers.Number):
        return _eval_many(field, [p])[0]
    return _eval_many(field, p)


def sample_grid(field, lower, upper, n=20):
    ''' Values on an n x n x n grid of cell centres over a box (a flat list,
        x fastest) together with the points '''
    lo, hi = _vec(lower), _vec(upper)
    if isinstance(n, numbers.Number):
        n = (int(n),) * 3
    pts = []
    for k in range(n[2]):
        z = lo[2] + (k + 0.5) * (hi[2] - lo[2]) / n[2]
        for j in range(n[1]):
            y = lo[1] + (j + 0.5) * (hi[1] - lo[1]) / n[1]
            for i in range(n[0]):
                pts.append((lo[0] + (i + 0.5) * (hi[0] - lo[0]) / n[0], y, z))
    return pts, _eval_many(field, pts)


def _bounds_of(shape, lower, upper):
    if lower is not None and upper is not None:
        return _vec(lower), _vec(upper)
    b = getattr(shape, '_bounds', None)
    if b:
        return tuple(b[0]), tuple(b[1])
    raise ValueError('give the bounds: lower=(x, y, z), upper=(x, y, z)')


def field_range(field, body=None, lower=None, upper=None, n=24):
    ''' (min, max) of a field, sampled on a grid over a box (and only inside
        `body` if given) '''
    lo, hi = _bounds_of(body if body is not None else field, lower, upper)
    pts, vals = sample_grid(field, lo, hi, n)
    if body is not None:
        inside = _eval_many(body, pts)
        vals = [v for v, s in zip(vals, inside) if s < 0]
    vals = [v for v in vals if v == v and abs(v) != float('inf')]
    if not vals:
        return (0.0, 0.0)
    return (min(vals), max(vals))


def volume_of(shape, lower=None, upper=None, resolution=None):
    ''' Volume of a solid (mm^3), by sampling a grid over its bounds
        (resolution: samples per mm; by default ~100 along the longest side) '''
    return mass_properties(shape, lower=lower, upper=upper, resolution=resolution)['volume']


@content_cached('mass_properties', limit=16, copy_result=True)
def mass_properties(shape, density=1.0, lower=None, upper=None, resolution=None):
    ''' Volume, mass (density in g/cm^3 -> grams), centroid and bounding box
        of a solid, by grid sampling.  Returns a dict.  The density is a number or a field (a material that is
        denser here than there: a lattice graded by its density, a result): the mass adds it up over the solid, and the
        dict has the centre of mass as well as the centroid. '''
    lo, hi = _bounds_of(shape, lower, upper)
    size = [hi[i] - lo[i] for i in range(3)]
    if resolution is None:
        h = max(size) / 100.0
    else:
        h = 1.0 / float(resolution)
    n = [max(1, int(math.ceil(s / h))) for s in size]
    while n[0] * n[1] * n[2] > 4e6:
        n = [max(1, v // 2) for v in n]
    pts, vals = sample_grid(shape, lo, hi, n)
    cell = (size[0] / n[0]) * (size[1] / n[1]) * (size[2] / n[2])
    inside = [p for p, v in zip(pts, vals) if v < 0]
    count = len(inside)
    sx = sum(p[0] for p in inside)
    sy = sum(p[1] for p in inside)
    sz = sum(p[2] for p in inside)
    vol = count * cell
    centroid = (sx / count, sy / count, sz / count) if count else (float('nan'),) * 3
    out = {'volume': vol, 'mass': vol * float(density) / 1000.0 if not is_field(density) else None,
           'centroid': centroid, 'fill_fraction': count / float(len(pts)), 'samples': n}
    if is_field(density):
        d = _eval_many(density, inside) if inside else []
        total = sum(d)
        out['mass'] = total * cell / 1000.0
        out['center_of_mass'] = (tuple(sum(di * p[a] for di, p in zip(d, inside)) / total for a in range(3))
                                 if count and total else (float('nan'),) * 3)
    return out


################################################################################
# Exact distance fields (re-distancing) and what they make possible

def find_extent(shape, budget=200000, resolution=2.0, half=1.0e6):
    ''' The box ((x0, y0, z0), (x1, y1, z1)) round the inside of a shape, found by searching it with interval
        arithmetic -- or None when it has no extent that can be found (it is empty, or open on a side, or the search
        ran out of cells).  A shape with var() numbers is searched with the numbers they have in the script: inside
        the application a var() is held by the program, and a search that did not know its number would read it as 0
        (a box of size var(2) is no box at all).  The search takes a third of a second or more, and every select_surface of the same part
        asks for it again on every run of the script: it is remembered by what the shape is (and the numbers it has) '''
    from fieldes.stdlib.content_cache import cache_for, shape_key, Uncacheable
    memory = cache_for('extent', 256)
    try:
        key = (shape_key(shape), budget, resolution, half)
    except Uncacheable:
        key = None
    if key is not None:
        hit, found = memory.get(key)
        if hit:
            return found
    found = _search_extent(shape, budget, resolution, half)
    if key is not None:
        memory.put(key, found)
    return found


def tight_extent(shape):
    ''' The box round a shape that a MESH is fitted to (H202, the user, 2026-11-13: the element size is adjusted to fit the body, which a lattice or a grid can only do against the true faces of the
        body).  find_extent's box comes out up to a tenth of a millimetre larger than the body on each side (interval arithmetic stops at a tolerance); here every face of the box that is a flat face
        of the body, square to an axis -- a box, a plate, a prism, the end of a bar -- is put on the face itself: along a grid of 15 x 15 lines across the face the place where the body begins
        is found within the margin, and the face is taken only when the lines that find it agree to a hundredth of the margin and no point of the grid just outside it is inside the body.  A face
        that is curved (the pole of a sphere), tilted, or that the lines do not find stays where find_extent had it: the box never cuts the body.  None when find_extent finds no box '''
    from fieldes.stdlib.content_cache import cache_for, shape_key, Uncacheable
    memory = cache_for('tight_extent', 256)
    try:
        key = shape_key(shape)
    except Uncacheable:
        key = None
    if key is not None:
        hit, found = memory.get(key)
        if hit:
            return found
    found = _tightened(shape, find_extent(shape))
    if key is not None:
        memory.put(key, found)
    return found


def _tightened(shape, found):
    if found is None:
        return None
    lo = [float(c) for c in found[0]]
    hi = [float(c) for c in found[1]]
    ext = [hi[a] - lo[a] for a in range(3)]
    if not all(e > 0 for e in ext):
        return found
    margin = 0.002 * max(ext) + 1e-3
    n, steps = 15, 16
    new_lo, new_hi = list(lo), list(hi)
    for a in range(3):
        u, v = [i for i in range(3) if i != a]
        grid = [(lo[u] + (i + 0.5) / n * ext[u], lo[v] + (j + 0.5) / n * ext[v]) for i in range(n) for j in range(n)]
        for side in (0, 1):
            bound = lo[a] if side == 0 else hi[a]
            inward = 1.0 if side == 0 else -1.0

            def at(g, t):
                p = [0.0, 0.0, 0.0]
                p[a] = bound + inward * t
                p[u], p[v] = g
                return tuple(p)

            values = _eval_many(shape, [at(g, margin * (k + 1) / steps) for g in grid for k in range(steps)])
            below, above, rays = [], [], []
            for i in range(len(grid)):
                row = values[steps * i:steps * i + steps]
                first = next((k for k, f in enumerate(row) if f <= 0.0), None)
                if first is not None:
                    below.append(margin * first / steps)
                    above.append(margin * (first + 1) / steps)
                    rays.append(i)
            if len(rays) < 0.2 * len(grid):
                continue
            for _ in range(14):
                mids = [0.5 * (below[r] + above[r]) for r in range(len(rays))]
                for r, f in enumerate(_eval_many(shape, [at(grid[i], mids[r]) for r, i in enumerate(rays)])):
                    if f <= 0.0:
                        above[r] = mids[r]
                    else:
                        below[r] = mids[r]
            found_at = sorted(0.5 * (below[r] + above[r]) for r in range(len(rays)))
            if found_at[-1] - found_at[0] > 0.01 * margin:
                continue
            e = found_at[len(found_at) // 2]
            # (nothing of the body just outside the face anywhere on the grid)
            outside = _eval_many(shape, [at(g, e - 4.0 * margin / steps) for g in grid])
            if any(f <= 0.0 for f in outside):
                continue
            if side == 0:
                new_lo[a] = bound + e
            else:
                new_hi[a] = bound - e
    return (tuple(new_lo), tuple(new_hi))


def _search_extent(shape, budget, resolution, half):
    from fieldes.ffi import libfive_region_t
    search = libfive_region_t()
    for axis in (search.X, search.Y, search.Z):
        axis.lower, axis.upper = -half, half
    out = libfive_region_t()
    open_sides = ctypes.c_int(0)
    known = _script_vars() if hasattr(lib, 'libfive_tree_bounds_vars') else None
    if known is not None:
        found = lib.libfive_tree_bounds_vars(shape.ptr, search, budget, resolution, ctypes.byref(out),
                                             ctypes.byref(open_sides), known[0], known[1], known[2])
    else:
        found = lib.libfive_tree_bounds(shape.ptr, search, budget, resolution, ctypes.byref(out),
                                        ctypes.byref(open_sides))
    if not found or open_sides.value:
        return None
    return ((out.X.lower, out.Y.lower, out.Z.lower), (out.X.upper, out.Y.upper, out.Z.upper))


def _shape_bounds(shape, bounds):
    if bounds is not None:
        lo, hi = bounds
        return _vec(lo), _vec(hi)
    b = getattr(shape, '_bounds', None)
    if b:
        return tuple(b[0]), tuple(b[1])
    found = find_extent(shape)
    if found is None:
        raise ValueError('could not find the extent of the shape: pass '
                         'bounds=((x0, y0, z0), (x1, y1, z1))')
    return found


def exact_distance(shape, reach=None, bounds=None):
    ''' The signed distance field of a shape (mm: negative inside, 0 on the surface, the distance to the surface outside), read from the shape's own field: nothing is
        meshed.  Many shapes built with booleans, blends, imports or warps have fields that are only roughly a distance (the field of a box is the largest of the distances to
        its planes: boxy mitres outside its edges), and offsets, shells, rounded edges and the depth below a surface mean a true one.  From each point the surface is found
        by walking along the field's gradient (a few Newton steps, each a handful of calls of the field), and the distance is the length of that walk -- see distance_field.py.
        It is exact for a field that is a distance already, for a sphere, a cylinder, a plane, and for the edge and the corner of a box; at an edge where faces meet at another
        angle it is a little off (the walk goes from face to face and ends beside the nearest point).
        reach: how far from the surface the distance is made, mm (default a quarter of the shape's size): beyond it the shape's own field stands, so that the walk is not made
        (and not drawn) over the whole of space; an offset, a shell or a thickness asks for as much as it needs.  bounds: the box round the shape, when it cannot be found --
        only its size is used (the step of the gradient). '''
    f = _s(shape)
    lo, hi = _shape_bounds(f, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    out = _distance_field.distance(f, size, _DISTANCE_REACH * size if reach is None else _positive_number(reach, 'the reach'))
    out._bounds = (tuple(lo), tuple(hi))
    # (the surface is the shape's own, so near it the gradient is the shape's: a tool that reads normals from this field takes them from the shape's derivative -- see distance_field.partials)
    out._partials_hint = _distance_field.partials(f)
    # (what the viewport draws is the surface, and the surface of the distance is the shape's own: the walk has the sign of the field everywhere, so it is zero where the field is and nowhere else.
    # The viewport meshes the shape's own field, which is one evaluation per point instead of a dozen calls of it at every point of the drawing -- minutes of drawing for a part that draws in seconds;
    # the distances themselves -- what a tool or a field viewer reads -- are the walk's, as they always were: `_distance_of` is the walk itself, see app_support.exact_view_tree)
    out._display_field = f
    out._distance_of = lambda: out
    return out


_DISTANCE_REACH = 0.25     # (how far from the surface exact_distance makes the distance when it is not told, as a share of the shape's size)


def _reach(v, lo, hi):
    ''' (smallest, largest) that a number or a field takes inside the box lo..hi: the number itself, or the field
        sampled on a grid -- how far an operation with a field for its size can reach '''
    if not is_field(v):
        return float(v), float(v)
    return field_range(v, lower=lo, upper=hi, n=16)


def _positive_number(v, what):
    ''' `v` as a number above 0, or a ValueError that names what it is '''
    if not isinstance(v, numbers.Real) or not v > 0:
        raise ValueError('%s must be a number above 0 (mm)' % what)
    return float(v)


def offset_exact(shape, distance, bounds=None):
    ''' A uniform offset of any shape: grows it by `distance` mm everywhere
        (shrinks it for a negative distance), measured along true normals,
        so edges and corners get round, not stretched.  The distance is a number or a field (an offset that
        is more here than there).  The distance to the shape is read from its field (see exact_distance) as far out as the offset reaches. '''
    d = number_or_field(distance, 'the distance')
    f = _s(shape)
    lo0, hi0 = _shape_bounds(f, bounds)
    size = max(hi0[i] - lo0[i] for i in range(3)) or 1.0
    least, most = _reach(d, lo0, hi0)
    grow = max(most, 0.0)
    e = exact_distance(f, reach=max(1.25 * max(abs(least), abs(most)), 0.01 * size), bounds=(lo0, hi0))
    out = e - d
    out._bounds = (tuple(v - grow for v in lo0), tuple(v + grow for v in hi0))
    return out


def shell_exact(shape, thickness, side='inside', bounds=None):
    ''' A hollow shell of uniform thickness (true distance), side = 'inside',
        'outside' or 'center'.  The thickness is a number or a field. '''
    t = number_or_field(thickness, 'the thickness')
    f = _s(shape)
    lo0, hi0 = _shape_bounds(f, bounds)
    size = max(hi0[i] - lo0[i] for i in range(3)) or 1.0
    reach = max(_reach(t, lo0, hi0)[1], 0.0)
    e = exact_distance(f, reach=max(1.25 * reach, 0.01 * size), bounds=(lo0, hi0))
    if side == 'inside':
        out = e.max(-(e + t))
    elif side == 'outside':
        out = (e - t).max(-e)
    elif side == 'center':
        out = abs(e) - t / 2
    else:
        raise ValueError("shell_exact: side is 'inside', 'outside' or 'center'")
    out._bounds = (tuple(lo0), tuple(hi0))
    return out


def round_edges(shape, radius, bounds=None):
    ''' Rounds every convex (outside) edge and corner of a shape with the
        given radius (a number or a field), and removes what is thinner than the ball: a ball of that radius rolls in the material, and the shape is everything
        it reaches.  It is made from the shape's own field (see exact_distance and distance_field.py): the centres of the ball are where the field is
        `-radius`, and the surface of the result is `radius` from them.  Exact for the outside edge of two flat faces at any angle and for the corner of three
        (the ball's centre is where the planes of the faces meet), for a cylinder and a sphere. '''
    r = number_or_field(radius, 'the radius')
    f = _s(shape)
    lo, hi = _shape_bounds(f, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    rmax = _reach(r, lo, hi)[1]
    if not rmax > 0:
        raise ValueError('round_edges: the radius must be above 0 (mm)')
    out = _distance_field.opening(f, r, size, rmax)
    out._bounds = (tuple(lo), tuple(hi))
    return out


def _fill_all(shape, radius, bounds=None):
    ''' Fills every concave (inside) edge and corner of a shape with a
        fillet of the given radius (a number or a field), and fills what is narrower than the ball: a ball of that radius rolls in the air, and the
        shape is everything it cannot reach.  It is made from the shape's own field (see exact_distance and distance_field.py): the centres of the ball are where the field is
        `radius`, and the surface of the result is `radius` from them.  Exact for the inside edge of two flat faces at any angle and for the corner of three
        (the ball's centre is where the planes of the faces meet); a slot narrower than the ball is filled flush with its mouth, not left with the sag of the ball resting on its two edges. '''
    r = number_or_field(radius, 'the radius')
    f = _s(shape)
    lo, hi = _shape_bounds(f, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    rmax = _reach(r, lo, hi)[1]
    if not rmax > 0:
        raise ValueError('fillet: the radius must be above 0 (mm)')
    out = _distance_field.closing(f, r, size, rmax)
    out._bounds = (tuple(lo), tuple(hi))
    return out


def _edge_vertices(edge, what, most):
    ''' The numbers of an edge that was selected -- `edge_at(body, (x, y, z))`: a line for a straight edge, a closed curve for a circular one --: its vertices (thinned to `most` at the most) and whether it is closed '''
    from fieldes.stdlib.curves import Curve
    if not isinstance(edge, Curve):
        raise TypeError('%s(body, edge, ...): the edge is a curve -- the one `edge_at(body, (x, y, z))` makes of an edge of the body (select the edge, and the body, and use the menu) -- not %r' % (what, edge))
    vertices = [tuple(float(c) for c in v) for v in edge.vertices]
    closed = bool(edge.closed)
    if len(vertices) < 2:
        raise ValueError('%s: the edge has no length' % what)
    if closed and vertices[0] == vertices[-1]:
        vertices = vertices[:-1]
    if len(vertices) > most:
        every = -(-len(vertices) // most)
        vertices = vertices[::every]
    return vertices, closed


def _edge_call(what, given):
    ''' The arguments of `fillet(edge, radius)` and `chamfer(edge, size)` -- and of the older `fillet(body, edge, radius)` -- in whatever order they stand: (body, the edges, the radius or size).  Any number of
        edges may be given (`fillet(edge_1, edge_2, edge_3, 2)`: the user, 2026-10-29, "if they can only take one, I want them to be able to use multiple").  The body is left out when the edges are all there is:
        an edge you selected knows the body it was made on (`edge_at(body, ...)`: `_on`), so the edges alone are enough for the menu (the user, 2026-10-28: "you just take the edge and that's that") '''
    from fieldes.stdlib.curves import Curve
    from fieldes.kinds import kind_of, BODIES
    amount_name = 'radius' if what == 'fillet' else 'size'
    edges = [x for x in given if isinstance(x, Curve)]
    if not edges:
        raise TypeError('%s(edge, %s): the edge is a curve -- the one `edge_at(body, (x, y, z))` makes of an edge of the body (select the edge and use the menu) -- not %r' % (what, amount_name, given[0]))
    body, amounts = None, []
    for x in given:
        if isinstance(x, Curve):
            continue
        if isinstance(x, Shape) and kind_of(x) in BODIES and body is None:
            body = x
        else:
            amounts.append(x)
    if len(amounts) > 1:
        raise TypeError('%s(edge, %s): more than one %s was given' % (what, amount_name, amount_name))
    if not amounts:
        raise ValueError('%s: a %s is needed: %s(edge, %s)' % (what, amount_name, what, amount_name))
    if body is None:
        body = next((getattr(e, '_on', None) for e in edges if getattr(e, '_on', None) is not None), None)
    if body is None:
        raise ValueError('%s: the edge does not say which body it is on: %s(body, edge, %s)' % (what, what, amount_name))
    return body, edges, amounts[0]


def _along_edge(vertices, closed, reach):
    ''' (the pieces of the line through `vertices`, as (a, b, direction, length, the room the piece has before it, and after it)) -- a piece goes a little way into the next one, so that nothing between two pieces is
        left out, and an end of an open line does not go past its end '''
    n = len(vertices)
    out = []
    pieces = n if closed else n - 1
    for i in range(pieces):
        a, b = vertices[i], vertices[(i + 1) % n]
        d = tuple(b[k] - a[k] for k in range(3))
        length = math.sqrt(sum(c * c for c in d))
        if length < 1e-12:
            continue
        before = 0.25 * reach if (closed or i > 0) else 0.0
        after = 0.25 * reach if (closed or i < pieces - 1) else 0.0
        out.append((a, b, tuple(c / length for c in d), length, before, after))
    return out


def _edge_zone(pieces, rhos, trim=0.0):
    ''' A solid, negative inside: the tube round the edge -- of radius `rhos[i]` round piece i, the room that edge's own rounding needs and no more --, cut off square at the ends of an open edge, a little
        inside them (`trim`): the next edge that leaves the corner at its end lies IN the end plane, and is not this edge's to round.  Where a rounding or a chamfer of that edge is, and nowhere else.
        A piece with no radius (None) is left out '''
    zone = None
    for (a, b, t, length, before, after), rho in zip(pieces, rhos):
        if rho is None:
            continue
        dx, dy, dz = X() - a[0], Y() - a[1], Z() - a[2]
        s = dx * t[0] + dy * t[1] + dz * t[2]
        dist = (dx * dx + dy * dy + dz * dz - s * s).max(0.0).sqrt()
        tube = (dist - rho).max(-s - before + (trim if before == 0.0 else 0.0)).max(s - length - after + (trim if after == 0.0 else 0.0))
        zone = tube if zone is None else zone.min(tube)
    return zone


def _piece_frames(pieces, h, rays):
    ''' For each piece of an edge: (its middle, two directions square to the piece and to each other), and -- for all the pieces in a list -- the places on a ring of radius `h` round each middle, `rays` of them, in
        the plane square to the piece: the rings are read to see the two faces that meet at the edge '''
    frames, places = [], []
    for a, b, t, length, before, after in pieces:
        middle = tuple(0.5 * (a[k] + b[k]) for k in range(3))
        ref = (1.0, 0.0, 0.0) if abs(t[0]) < 0.9 else (0.0, 1.0, 0.0)
        u = (t[1] * ref[2] - t[2] * ref[1], t[2] * ref[0] - t[0] * ref[2], t[0] * ref[1] - t[1] * ref[0])
        un = math.sqrt(sum(x * x for x in u))
        u = tuple(x / un for x in u)
        w = (t[1] * u[2] - t[2] * u[1], t[2] * u[0] - t[0] * u[2], t[0] * u[1] - t[1] * u[0])
        frames.append((middle, u, w))
        for k in range(rays):
            ph = 2.0 * math.pi * k / rays
            places.append(tuple(middle[j] + h * (math.cos(ph) * u[j] + math.sin(ph) * w[j]) for j in range(3)))
    return frames, places


def _face_traces(f, stations, h, rays=24, steps=16):
    ''' For each station -- (a place on a straight edge, two directions square to the edge and to each other) -- the two angles, in the plane square to the edge, at which the faces that meet there cross a ring
        of radius `h` round the place: where the body begins as the angle grows, and where it ends.  The ring is read at `rays` places and each crossing is found to a few millionths of a turn by halving,
        for all the stations at once.  None where a ring does not cross the surface of the body exactly twice '''
    def at(station, phi):
        m, u, w = station
        return tuple(m[j] + h * (math.cos(phi) * u[j] + math.sin(phi) * w[j]) for j in range(3))
    step = 2.0 * math.pi / rays
    values = evaluate(f, [at(s, step * k) for s in stations for k in range(rays)])
    lows, highs, low_inside = [], [], []
    for n in range(len(stations)):
        v = values[n * rays:(n + 1) * rays]
        enter = [k for k in range(rays) if v[k] >= 0 > v[(k + 1) % rays]]
        leave = [k for k in range(rays) if v[k] < 0 <= v[(k + 1) % rays]]
        if len(enter) != 1 or len(leave) != 1:
            return None
        for k, inside in ((enter[0], False), (leave[0], True)):
            lows.append(step * k)
            highs.append(step * (k + 1))
            low_inside.append(inside)
    places = [(s, c) for s in range(len(stations)) for c in range(2)]
    for _ in range(steps):
        mids = [0.5 * (lows[i] + highs[i]) for i in range(len(lows))]
        got = evaluate(f, [at(stations[places[i][0]], mids[i]) for i in range(len(mids))])
        for i in range(len(mids)):
            if (got[i] < 0) == low_inside[i]:
                lows[i] = mids[i]
            else:
                highs[i] = mids[i]
    return [(0.5 * (lows[2 * n] + highs[2 * n]), 0.5 * (lows[2 * n + 1] + highs[2 * n + 1])) for n in range(len(stations))]


def _straight_cutter(f, piece, frame, r, rmax, rays=24):
    ''' What a fillet takes off a STRAIGHT convex edge between two flat faces, made exactly: a field that is above 0 where material goes -- the room between the two faces' tangent lines and the ball of radius r
        that rolls along the edge, along the whole edge and a hair past its two ends, where there is air.  The body is cut with it (`body.max(cutter)`), so the rounding ends flush in the end faces and touches
        nothing else: the edges that leave the corner at its ends are not its to round (the user's picture, 2026-11-02, a fillet along the top edge of a box with a dome and a stub at each end: task C18, "a fillet does
        not spill over neighbouring edges").  The faces are read at three places along the edge, as the angles at which they cross a ring round it; the cutter is made only when they are the same at all three --
        two flat faces --, else None (a curved face has the rolling ball of the body's field instead).  The radius r is what it was given: a number, a var() or a field, and the cutter follows it '''
    a, b, t, length, before, after = piece
    middle, u, w = frame
    stations = [(tuple(a[k] + t[k] * length * frac for k in range(3)), u, w) for frac in (0.2, 0.5, 0.8)]
    traces = _face_traces(f, stations, 0.2 * rmax, rays)
    if traces is None:
        return None
    turn = 2.0 * math.pi
    first, last = traces[1]
    wedge = (last - first) % turn
    for p, q in traces:
        if abs((p - first + math.pi) % turn - math.pi) > 0.004 or abs((q - last + math.pi) % turn - math.pi) > 0.004:
            return None
    if not 0.2 < wedge < math.pi - 0.02:
        return None
    half = 0.5 * wedge
    d1, d2 = (math.cos(first), math.sin(first)), (math.cos(last), math.sin(last))
    bis = (math.cos(first + half), math.sin(first + half))

    def toward(d):
        # (from the middle of the ball to where it touches the face on that side: the face's normal, out of the body)
        x, y = d[0] / math.tan(half) - bis[0] / math.sin(half), d[1] / math.tan(half) - bis[1] / math.sin(half)
        n = math.hypot(x, y)
        return x / n, y / n
    n1, n2 = toward(d1), toward(d2)
    c = n1[0] * n2[0] + n1[1] * n2[1]
    dx, dy, dz = X() - middle[0], Y() - middle[1], Z() - middle[2]
    pu = dx * u[0] + dy * u[1] + dz * u[2]
    pw = dx * w[0] + dy * w[1] + dz * w[2]
    along = dx * t[0] + dy * t[1] + dz * t[2]
    k = r / math.sin(half)                       # (how far the middle of the ball is from the edge)
    qu, qw = pu - k * bis[0], pw - k * bis[1]
    s1 = qu * n1[0] + qw * n1[1]
    s2 = qu * n2[0] + qw * n2[1]
    left = (s1 - s2 * c) * (1.0 / (1.0 - c * c))
    right = (s2 - s1 * c) * (1.0 / (1.0 - c * c))
    outside_ball = (qu * qu + qw * qw).sqrt() - r
    over = 0.02 * rmax
    along_edge = (0.5 * length + over) - along.abs()
    return left.min(right).min(outside_ball).min(along_edge)


def fillet(edge, *more, radius=None, bounds=None):
    ''' Rounds edges of a body: `fillet(edge, radius)` -- the edge you selected (`edge_at(body, (x, y, z))`: a line for a straight edge, a closed curve for a circular one, a cylinder's rim; it knows its body), with a
        fillet of the given radius (a number, a `var()` its handle drags, or a field), exactly that edge and no other: `fillet(edge_1, edge_2, edge_3, radius)` rounds all of them.  A convex edge is rounded off and a
        concave one is filled, as the ball of that radius rolls along it (see round_edges and distance_field.py: exact for two flat faces at any angle): made from the body's own field and limited to the room that
        edge's own rounding needs -- a tube round it as wide as the ball touches the two faces from it, which is the radius for a right angle, more for a sharper edge, less for a blunter one --, so it follows the
        body when a handle of the body is dragged and leaves the other edges near it as they are (the tube was four radii wide, and rounded every edge in it).
        The body may be named as well, in any place: `fillet(body, edge, radius)`.
        `fillet(body, radius)` -- with no edge -- fills every concave edge of the body '''
    from fieldes.stdlib.curves import Curve
    # (the edges stand in a row, any number of them, and the radius is a keyword: it does not stand in a place that an edge could take.  A number written among the edges is told by what it is -- see _edge_call)
    given = (edge,) + more + (() if radius is None else (radius,))
    if not any(isinstance(x, Curve) for x in given):
        if len(given) != 2:
            raise ValueError('fillet: %s: fillet(body, radius)' % ('a radius is needed' if len(given) < 2 else 'a body takes one radius'))
        return _fill_all(edge, given[1], bounds)
    shape, edges, radius = _edge_call('fillet', given)
    r = number_or_field(radius, 'the radius')
    f = _s(shape)
    lo, hi = _shape_bounds(f, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    rmax = _reach(r, lo, hi)[1]
    if not rmax > 0:
        raise ValueError('fillet: the radius must be above 0 (mm)')
    rays = 24
    zone_round, zone_fill = None, None
    cutters = []
    for e in edges:
        vertices, closed = _edge_vertices(e, 'fillet', 96)
        pieces = _along_edge(vertices, closed, 4.0 * rmax)
        if not pieces:
            raise ValueError('fillet: the edge has no length')
        # (a ring round the middle of each piece, a fifth of the radius from it: ON an edge or a corner the field has no direction.  The body takes up part of the ring: a convex edge, less than half of it, which a
        # ball rolls over; a concave edge, more than half, which the ball cannot reach into.  Where it is neither -- a flat place, a corner, no edge -- the piece is left alone)
        frames, places = _piece_frames(pieces, 0.2 * rmax, rays)
        values = evaluate(f, places)
        rho_round, rho_fill = [], []
        for index in range(len(pieces)):
            v = values[index * rays:(index + 1) * rays]
            inside = sum(1 for x in v if x < 0)
            changes = sum(1 for k in range(rays) if (v[k] < 0) != (v[(k + 1) % rays] < 0))
            if inside == 0 or inside == rays or changes != 2:
                rho_round.append(None)
                rho_fill.append(None)
                continue
            wedge = inside * 2.0 * math.pi / rays
            convex = wedge < math.pi
            if convex and not closed and len(pieces) == 1:
                # (a straight edge between two flat faces: cut off exactly, to its ends and no farther -- see _straight_cutter)
                cutter = _straight_cutter(f, pieces[0], frames[0], r, rmax, rays)
                if cutter is not None:
                    cutters.append(cutter)
                    rho_round.append(None)
                    rho_fill.append(None)
                    continue
            sharper = wedge if convex else 2.0 * math.pi - wedge          # (the angle of the wedge the ball is in: the body's for a convex edge, the air's for a concave one)
            sharper = max(sharper - 2.0 * math.pi / rays, 0.2)             # (the rays are a spacing apart: the wedge is that much less than they say, at the most)
            # The ball touches each face a distance `radius / tan(wedge / 2)` from the edge -- the radius itself for a right angle --: nothing the rounding changes is farther from the edge
            rho = min(1.1 * rmax / math.tan(0.5 * sharper) + 0.1 * rmax, 8.0 * rmax)
            (rho_round if convex else rho_fill).append(rho)
            (rho_fill if convex else rho_round).append(None)
        trim = 0.02 * rmax
        z = _edge_zone(pieces, rho_round, trim)
        if z is not None:
            zone_round = z if zone_round is None else zone_round.min(z)
        z = _edge_zone(pieces, rho_fill, trim)
        if z is not None:
            zone_fill = z if zone_fill is None else zone_fill.min(z)
    if zone_round is None and zone_fill is None and not cutters:
        print('fillet: the edge is not an edge that a ball of radius %.4g rounds (a flat place, or no sharp edge there): the body is left as it is' % rmax)
        return f * 1.0
    out = f
    for cutter in cutters:
        out = out.max(cutter)
    if zone_round is not None:
        # (on the edge itself the body is rounded off where it is convex: the ball rolls in the material -- the opened body --)
        opened = _distance_field.opening(f, r, size, rmax)
        out = out.max(-zone_round).min(opened.max(zone_round))
    if zone_fill is not None:
        # (and filled where it is concave: the ball rolls in the air -- the closed body --)
        filled = _distance_field.closing(f, r, size, rmax)
        out = out.max(-zone_fill).min(filled.max(zone_fill))
    out._bounds = (tuple(lo), tuple(hi))
    return out


def chamfer(edge, *more, size=None, bounds=None):
    ''' Chamfers edges of a body: `chamfer(edge, size)` -- the edge you selected (`edge_at(body, (x, y, z))`: a line, or a closed curve for a circular edge; it knows its body), with a flat cut that goes `size`
        along each of the two faces from the edge (a convex edge is cut off, a concave one is filled), exactly that edge and no other: `chamfer(edge_1, edge_2, edge_3, size)` cuts all of them.  The body may be named
        as well, in any place: `chamfer(body, edge, size)`.
        Each piece of the edge has the cut through the two faces as they are there, so the cut of a circular edge is a cone.  The cut is made from the numbers of the edge as it was when the script last ran: a handle
        of the body that is dragged moves the cut with it when the script runs again, at the end of the drag '''
    from fieldes.stdlib.curves import value_of
    shape, edges, size = _edge_call('chamfer', (edge,) + more + (() if size is None else (size,)))
    c = float(value_of(size) if isinstance(size, Shape) else size)
    if not c > 0:
        raise ValueError('chamfer: the size must be above 0 (mm)')
    f = _s(shape)
    pieces = []
    for e in edges:
        vertices, closed = _edge_vertices(e, 'chamfer', 48)
        found = _along_edge(vertices, closed, 2.0 * c)
        if not found:
            raise ValueError('chamfer: the edge has no length')
        pieces += found
    rays = 24
    h = 0.25 * c
    places = []
    frames = []
    for a, b, t, length, before, after in pieces:
        middle = tuple(0.5 * (a[k] + b[k]) for k in range(3))
        ref = (1.0, 0.0, 0.0) if abs(t[0]) < 0.9 else (0.0, 1.0, 0.0)
        u = (t[1] * ref[2] - t[2] * ref[1], t[2] * ref[0] - t[0] * ref[2], t[0] * ref[1] - t[1] * ref[0])
        un = math.sqrt(sum(x * x for x in u))
        u = tuple(x / un for x in u)
        w = (t[1] * u[2] - t[2] * u[1], t[2] * u[0] - t[0] * u[2], t[0] * u[1] - t[1] * u[0])
        frames.append((middle, u, w))
        for k in range(rays):
            ph = 2.0 * math.pi * k / rays
            places.append(tuple(middle[j] + h * (math.cos(ph) * u[j] + math.sin(ph) * w[j]) for j in range(3)))
    values = evaluate(f, places)
    cuts, fills = None, None
    for index, (a, b, t, length, before, after) in enumerate(pieces):
        middle, u, w = frames[index]
        v = values[index * rays:(index + 1) * rays]
        inside = sum(1 for x in v if x < 0)
        if inside == 0 or inside == rays:
            continue
        # (the two faces seen along the edge: where the field changes its sign on the ring round it, a ray from the edge each; the way through the inside says whether the edge is convex)
        angles = []
        for k in range(rays):
            k2 = (k + 1) % rays
            if (v[k] < 0) != (v[k2] < 0):
                angles.append(2.0 * math.pi * (k + v[k] / (v[k] - v[k2])) / rays)
        if len(angles) != 2:
            continue
        d1 = tuple(math.cos(angles[0]) * u[j] + math.sin(angles[0]) * w[j] for j in range(3))
        d2 = tuple(math.cos(angles[1]) * u[j] + math.sin(angles[1]) * w[j] for j in range(3))
        p1 = tuple(middle[j] + c * d1[j] for j in range(3))
        p2 = tuple(middle[j] + c * d2[j] for j in range(3))
        chord = tuple(p2[j] - p1[j] for j in range(3))
        m = (t[1] * chord[2] - t[2] * chord[1], t[2] * chord[0] - t[0] * chord[2], t[0] * chord[1] - t[1] * chord[0])
        mn = math.sqrt(sum(x * x for x in m))
        if mn < 1e-12:
            continue
        m = tuple(x / mn for x in m)
        if sum(m[j] * (middle[j] - p1[j]) for j in range(3)) < 0:
            m = tuple(-x for x in m)                    # (the normal points to the side the edge is on)
        g = (X() - p1[0]) * m[0] + (Y() - p1[1]) * m[1] + (Z() - p1[2]) * m[2]
        dx, dy, dz = X() - a[0], Y() - a[1], Z() - a[2]
        s = dx * t[0] + dy * t[1] + dz * t[2]
        dist = (dx * dx + dy * dy + dz * dz - s * s).max(0.0).sqrt()
        piece = (-g).max((dist - 2.0 * c)).max(-s - before).max(s - length - after)
        if inside * 2.0 * math.pi / rays < math.pi:
            cuts = piece if cuts is None else cuts.min(piece)                 # (a convex edge: the corner of the body is cut off)
        else:
            fills = piece if fills is None else fills.min(piece)              # (a concave one: the corner of the air is filled)
    if cuts is None and fills is None:
        print('chamfer: no edge of the body at the line it was given: the body is left as it is')
        return f * 1.0
    out = f
    if cuts is not None:
        out = out.max(-cuts)
    if fills is not None:
        out = out.min(fills)
    box = getattr(f, '_bounds', None)
    if box:
        out._bounds = box
    return out


_SIX_NEIGHBOURS = ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))
def smooth(shape, radius, steps=1):
    ''' Smooths the surface of a body without taking material off it (H178, the user, 2026-11-12: "the smooth operaitons took off material that made smoothing the topology optimization ... become a
        disconnected mesh, instead of getting cleanly smoother"): bumps, dents, ridges and stair-steps smaller than about `radius` mm are smoothed away, edges and corners are eased, and a flat or
        gently curved face stays where it is (unlike offset() or thicken(), which move or grow the surface).  The field of the body is averaged over the points a `radius` away on all six sides --
        the smoothing of a mesh's Laplacian, done on the field -- and `steps` of those are one smoothing: more steps smooth further (the reach grows as the square root of the number of steps).
        What took the material off the optimized part was the field the average was taken of -- a density, which rises 0 to 11 times as fast as a distance and saturates a few millimetres from
        the surface -- and not the average: of the DISTANCE of the part (fields._dist: the primitives and what is made of them are one; a part imported from STEP or an optimized part is replaced by
        its exact distance, as offset() does) the average keeps the volume (measured on his bowl: +0.1 %, one piece, where the average of the density took 4.5 %).  What is thinner than about the radius
        is smoothed away.  The radius is a number or a field (smoothed more where the field is large, not at all where it is 0).

        The result is a field like any other, the average of SIX copies of the body's distance moved by the radius (H188, the user: "nothing over a marginal performance loss (below 10%) ... it would be better
        if it ran even faster": the average of two steps took 19 copies, a shrink-free version of it 25 and 12, and a smoothing inside a smoothing multiplies the copies: his bowl nests one in another, 19 times 6
        copies and then 12 times 12; more steps are one smoothing at a larger radius, which smooths as far -- the reach of n steps of a radius is that of one step of the radius times the square root of n --,
        so six copies whatever the steps), so nothing is measured or meshed here: the surface is found when it is drawn, and the cost is that of the field times the copies.

        The optimized part of a topology optimization is a SURFACE already, and it is smoothed as one (H178, the user, 2026-11-14: "The smoothing still separated different parts at even small radii like
        3 mm"): an average of copies of its distance eats what is thinner than the radius (on his bowl at 2 mm smooth(3, 2) kept half of the volume, in four pieces) and only copies the bumps round.  Its
        surface is smoothed by a balanced pair of steps -- a step towards the average of the neighbours and a step back by the same weight, which does not grow it and shrinks it only slowly --, the vertices on
        a border are held, and the result is the exact distance of the smoothed surface, moved out by the volume the smoothing took: no copies, so it costs what the part itself costs.  The number of
        pairs follows the radius as the FOURTH power of (radius over the length of an edge of the surface), because that is how such a pair takes a bump away (H178, the user, 2026-11-14: "I input an extreme radius of 10
        mm here and I see barely ANY difference to 1 mm": the number of pairs went as the square, and radius 10 got 42 of them on his bowl): on his bowl the angle between neighbouring triangles falls from 6.8 degrees
        (mean) to 6.1 at radius 1, 5.1 at 3 and 3.9 at 10, in one piece, with 0.4 %, 1 % and 6 % less volume (the surface cannot grow past what the design may fill). '''
    r = number_or_field(radius, 'the radius')
    n = int(steps)
    if not positive(r):
        raise ValueError('smooth: the radius must be positive (mm)')
    if n < 1:
        raise ValueError('smooth: steps is at least 1')
    # A part that is a SURFACE already -- the optimized part of a topology optimization -- is smoothed as a surface (H178, the user, 2026-11-14: "The smoothing still separated different parts at even small
    # radii like 3 mm and DID NOT ACTUALLY SMOOTH IT IN GENERAL and made the topology optimizations edges SUPER JAGGED, POINT AND FUCKED UP. But at least it runs very fast now."): an average of copies of
    # its distance is a diffusion in space that eats what is thinner than the radius (measured on his bowl at 2 mm: smooth(3, 2) kept half of the volume, in four pieces) and, with six copies, only copies
    # the bumps smaller than the radius round.  The surface is smoothed by Taubin's filter and its exact distance is the result: no copies, so as fast as the part itself
    hook = getattr(_s(shape), '_smooth_distance', None)
    if hook is not None and isinstance(r, (int, float)) and not isinstance(r, bool):
        try:
            made = hook(float(r), n)
        except Exception:
            made = None
        if made is not None:
            return made
    if n > 1:
        r *= math.sqrt(n)
    field = _dist(shape)
    x, y, z = X(), Y(), Z()
    terms = []
    for index, (a, b, c) in enumerate(_SIX_NEIGHBOURS, 1):
        # Each lookup sits a ten-thousandth of the radius off its whole-radius point, differently for every one: a field
        # made of max() and min() (a box) ties with itself along its diagonals, and the mesher's grid of sample points
        # lands on those ties -- the very points it takes the slow way round.  (The weights are symmetric: the surface
        # does not move by more than that, a tenth of a micron for a radius of a millimetre.)
        jitter = [1e-4 * r * ((index * k) % 1.0 - 0.5) for k in (0.6180339887, 0.7548776662, 0.5698402910)]
        # (the body's field asked at the point moved by the offset -- a call: it is one field however many points it is asked at)
        terms.append(field(x - (a * r + jitter[0]), y - (b * r + jitter[1]), z - (c * r + jitter[2])) * (1.0 / 6.0))
    while len(terms) > 1:           # (a balanced sum: a shallow tree)
        terms = [terms[a] + terms[a + 1] if a + 1 < len(terms) else terms[a] for a in range(0, len(terms), 2)]
    out = terms[0]
    b = getattr(_s(shape), '_bounds', None)
    if b:
        out._bounds = (tuple(b[0]), tuple(b[1]))
    return out


################################################################################
# Analysis fields: normals, overhangs, wall thickness, curvature

def _need_fields():
    if getattr(lib, 'libfive_field_gradient', None) is None:
        raise RuntimeError('this FielDes library is too old for analysis fields')


def gradient_field(shape):
    ''' The gradient of a shape's field, as three fields (gx, gy, gz) '''
    _need_fields()
    s = _s(shape)
    return tuple(Shape(lib.libfive_field_gradient(s.ptr, 4 + a)) for a in range(3))


def gradient_magnitude(shape):
    ''' The length of a field's gradient: 1 for an exact distance field; far
        from 1 where a field only roughly measures distance '''
    _need_fields()
    return Shape(lib.libfive_field_gradient(_s(shape).ptr, 3))


def normal_field(shape):
    ''' The unit surface normal of a shape (pointing out of it), as three
        fields (nx, ny, nz); defined everywhere, meaningful near the surface '''
    _need_fields()
    s = _s(shape)
    return tuple(Shape(lib.libfive_field_gradient(s.ptr, a)) for a in range(3))


def overhang_angle(shape, build_direction=(0, 0, 1)):
    ''' For additive manufacturing: the angle (degrees) between each surface
        and the horizontal build plate, for surfaces facing down -- 0 for a
        ceiling facing straight down (the worst overhang), 90 for a vertical
        wall; surfaces facing up read 90 too.  Colour a part by it to see
        where supports are needed (typically below 45 degrees). '''
    b = _vec(build_direction)
    L = math.sqrt(sum(c * c for c in b))
    b = tuple(c / L for c in b)
    nx, ny, nz = normal_field(shape)
    down = -(nx * b[0] + ny * b[1] + nz * b[2])       # 1 facing straight down
    # a surface is tilted from horizontal by the angle between its normal
    # and straight down (upward-facing surfaces clamp to 90: no overhang)
    ang = down.max(-1).min(1).acos() * (180.0 / math.pi)
    return ang.max(0).min(90)


def overhang_mask(shape, limit=45.0, build_direction=(0, 0, 1)):
    ''' 1 where a surface overhangs more than `limit` degrees (needs support),
        0 elsewhere '''
    return (Shape.wrap(float(limit)).compare(overhang_angle(shape, build_direction)) + 1) / 2


def wall_thickness(shape, max_thickness=20.0):
    ''' Local wall thickness (mm): the length of the chord through the
        material along the surface normal.  Colour a part by it to find walls
        that are too thin (or too thick) to make.  Capped at max_thickness. '''
    _need_fields()
    return Shape(lib.libfive_field_thickness(_s(shape).ptr, float(max_thickness)))


def curvature_field(shape, step=None):
    ''' Mean curvature (1/mm; positive where convex, e.g. 1/r on a sphere of
        radius r).  step: finite-difference step in mm (default 0.05) '''
    _need_fields()
    return Shape(lib.libfive_field_curvature(_s(shape).ptr, float(step or 0.05)))


################################################################################
# Fields from data and noise

def field_from_points(points, values=None, neighbours=8, power=2.0):
    ''' A field through scattered samples -- measured data, or results
        exported from another simulation: at every point, the
        inverse-distance-weighted average (weights 1 / distance^power) of the
        nearest `neighbours` samples.  Handles hundreds of thousands of
        samples.  points: [(x, y, z), ...] with values [v, ...], or
        [(x, y, z, v), ...].  The field stays within the samples' range. '''
    if getattr(lib, 'libfive_field_points', None) is None:
        raise RuntimeError('this FielDes library is too old for field_from_points')
    if values is None:
        pts = [tuple(float(c) for c in p[:3]) for p in points]
        vals = [float(p[3]) for p in points]
    else:
        pts = [tuple(float(c) for c in p) for p in points]
        vals = [float(v) for v in values]
    n = len(pts)
    if n == 0 or n != len(vals):
        raise ValueError('field_from_points: need matching points and values')
    xyz = (ctypes.c_float * (3 * n))(*[c for p in pts for c in p])
    cv = (ctypes.c_float * n)(*vals)
    return Shape(lib.libfive_field_points(xyz, cv, n, int(neighbours), float(power)))


def field_from_csv(path, x='x', y='y', z='z', value='value', neighbours=8, power=2.0,
                   delimiter=None, scale=1.0):
    ''' A field from a CSV file of samples (e.g. exported stresses,
        temperatures or pressures): columns by header name or index.
        scale multiplies the coordinates (e.g. 1000 for metres -> mm). '''
    import csv
    with open(path, newline='', encoding='utf-8-sig') as f:
        text = f.read()
    if delimiter is None:
        try:
            delimiter = csv.Sniffer().sniff(text[:4096], delimiters=',;\t ').delimiter
        except csv.Error:
            delimiter = ','
    rows = list(csv.reader(text.splitlines(), delimiter=delimiter))
    header = [h.strip().lower() for h in rows[0]] if rows else []

    def col(c):
        if isinstance(c, int):
            return c
        if c.strip().lower() not in header:
            raise ValueError('field_from_csv: no column {!r} (columns: {})'.format(c, rows[0]))
        return header.index(c.strip().lower())
    ix, iy, iz, iv = col(x), col(y), col(z), col(value)
    pts, vals = [], []
    for r in rows:
        try:
            p = (float(r[ix]) * scale, float(r[iy]) * scale, float(r[iz]) * scale)
            v = float(r[iv])
        except (ValueError, IndexError):
            continue
        pts.append(p)
        vals.append(v)
    return field_from_points(pts, vals, neighbours, power)


NOISE_KINDS = ('perlin', 'billow', 'ridged', 'cellular', 'cracks')
_CELL_MODES = {'cellular': 0, 'cracks': 1}


def _noise_unit(kind, seed):
    ''' One layer of the noise of a kind at a feature size of 1 mm (the kernel makes it; the layers, the stretch and the warp are made of fields) '''
    seed = int(seed) & 0xffffffff
    if kind in _CELL_MODES:
        if getattr(lib, 'libfive_field_cellular', None) is None:
            raise RuntimeError('this FielDes library is too old for the cellular noises')
        return Shape(lib.libfive_field_cellular(1.0, seed, _CELL_MODES[kind], 1.0))
    return Shape(lib.libfive_field_noise(1.0, 1, seed, 1.0, 2.0))


def _noise_layer(kind, n):
    ''' What a kind does to one layer of Perlin noise (the cellular kinds come made).  A layer is mostly inside +-0.55, which is laid over 0 .. 1 '''
    if kind in ('billow', 'ridged'):
        m = (n.abs() * (1.0 / 0.55)).min(1.0)
        if kind == 'billow':
            return m * 2.0 - 1.0                    # rounded humps, with creases where the noise is zero
        r = 1.0 - m
        return r * r * 2.0 - 1.0                    # sharp ridges where the noise is zero
    return n


def _noise_general(scales, octaves, seed, gain, lacunarity, kind, warp):
    ''' Noise of any kind, stretched by an axis of its own, bent by a warp: layers of unit noise looked at through coordinates divided by the scales (numbers or fields), each
        layer finer by the lacunarity and weaker by the gain, added and laid over -1 .. 1 by what they add up to '''
    u = [X() / scales[0], Y() / scales[1], Z() / scales[2]]
    if is_field(warp) or warp:
        # (the coordinates pushed about by three other noises: the pattern bends, as marble and flowing organic shapes do)
        w = [_noise_unit('perlin', int(seed) + 1009 * (axis + 1)).remap(u[0], u[1], u[2]) for axis in range(3)]
        u = [u[axis] + w[axis] * warp for axis in range(3)]
    total, norm, a, f = None, 0.0, 1.0, 1.0
    for o in range(max(1, min(12, int(octaves)))):
        layer = _noise_layer(kind, _noise_unit(kind, int(seed) + 7919 * o).remap(u[0] * f, u[1] * f, u[2] * f))
        total = layer * a if total is None else total + layer * a
        norm += a
        a *= gain
        f *= lacunarity
    return total / norm


def noise_field(scale=10.0, octaves=4, seed=1, gain=0.5, lacunarity=2.0, amplitude=1.0, kind='perlin', warp=0.0):
    ''' Smooth random variation, a field of values around zero (`scale`, `amplitude` and `warp` are numbers or fields).
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
        part - 0.3 * noise_field(4)  '''
    if getattr(lib, 'libfive_field_noise', None) is None:
        raise RuntimeError('this FielDes library is too old for noise_field')
    if kind not in NOISE_KINDS:
        raise ValueError("noise_field: kind is one of %s, not %r" % (', '.join(repr(k) for k in NOISE_KINDS), kind))
    stretched = isinstance(scale, (tuple, list))
    if stretched and len(scale) != 3:
        raise ValueError('noise_field: scale is one number (or field), or three: (x, y, z)')
    for one in (scale if stretched else [scale]):
        if not is_field(one) and not float(one) > 0:
            raise ValueError('noise_field: the scale must be positive (mm)')
    if kind == 'perlin' and not stretched and not (is_field(warp) or warp):
        # (the plain noise: the kernel's own, with its layers added in it)
        if is_field(scale):
            # (the noise made at a scale of 1 mm, looked at through coordinates divided by the scale at every point: a
            # pattern that is finer here and coarser there)
            unit = noise_field(1.0, octaves, seed, gain, lacunarity)
            n = unit.remap(X() / scale, Y() / scale, Z() / scale)
        else:
            n = Shape(lib.libfive_field_noise(float(scale), int(octaves), int(seed) & 0xffffffff,
                                              float(gain), float(lacunarity)))
    else:
        n = _noise_general(tuple(scale) if stretched else (scale,) * 3, octaves, seed, gain, lacunarity, kind, warp)
    if is_field(amplitude):
        return n * amplitude
    return n if amplitude == 1.0 else n * float(amplitude)


TEXTURES = ('grit', 'stipple', 'leather', 'ribs', 'dimples', 'knurl')


def _texture_height(pattern, size, seed, axis, radius):
    ''' The height of a pattern, about -1 .. 1, over the surface it is laid on: patterns of noise (made of space itself, so they need no mapping on a face and have no seam), and
        periodic ones made of coordinates '''
    if is_field(pattern):
        return pattern
    if pattern not in TEXTURES:
        raise ValueError("texture: pattern is one of %s, or a field of your own (about -1 .. 1), not %r" % (', '.join(repr(t) for t in TEXTURES), pattern))
    tau = 2.0 * math.pi
    if pattern == 'grit':
        # sandpaper, sandblasting: small sharp grains of every size round the one asked for
        return (noise_field(size, 3, seed, 0.65, 2.0) * 3.0).max(-1.0).min(1.0)
    if pattern == 'stipple':
        # the orange peel of a moulded part: round pits, each about `size` across
        return noise_field(size, 1, seed, kind='cellular')
    if pattern == 'leather':
        # grooves between cells of about `size`, with a fine grain on them
        return (noise_field(size, 1, seed, kind='cracks') * 0.8 + noise_field(size / 4.0, 2, seed + 1) * 1.2).max(-1.0).min(1.0)
    if pattern == 'ribs':
        # grooves of a pitch of `size` along the axis `axis` (they run across it)
        coordinate = {'x': X(), 'y': Y(), 'z': Z()}[axis]
        return (coordinate * (tau / size)).sin()
    if pattern == 'dimples':
        # a three-dimensional egg carton of a pitch of `size`: on any face, with no mapping
        return ((X() * (tau / size)).cos() + (Y() * (tau / size)).cos() + (Z() * (tau / size)).cos()) * (1.0 / 3.0)
    # 'knurl': the diamonds of a knurled grip round the axis `axis` -- two helical waves crossed, with a whole number of teeth round `radius`
    if radius is None or not float(radius) > 0:
        raise ValueError("texture: a knurl goes round something: give its radius=, which sets how many teeth there are round it")
    teeth = max(3, int(round(tau * float(radius) / size)))
    angle = angle_field((0, 0, 0), axis)
    along = {'x': X(), 'y': Y(), 'z': Z()}[axis]
    return (angle * teeth + along * (tau / size)).sin() * (angle * teeth - along * (tau / size)).sin()


def texture(body, pattern='grit', depth=0.1, size=1.0, region=None, fade=None, seed=1, axis='z', radius=None, side='both'):
    ''' A texture on the surface of `body`: a pattern of small bumps and pits laid over it (sandpaper, a grip, a leather grain ...), on all of it or only where `region` says.
        `pattern` is one of 'grit' (sandpaper, sandblast), 'stipple' (round pits), 'leather' (grooves between cells, with a fine grain), 'ribs' (grooves across `axis`),
        'dimples' (a 3D egg carton) and 'knurl' (diamonds round `axis`; give the `radius` it goes round), or a field of your own, about -1 .. 1.
        `size` is the grain or the pitch (mm) and `depth` the height from a pit to a bump (mm); `side` 'both' puts the bumps outside and the pits inside the surface,
        'in' only cuts into it (the part stays inside its size), 'out' only adds to it.  `region`: a surface picked on the body (select_surface), or a body that holds the
        places to texture; the texture fades out over `fade` mm (the size, by default) so that no step is left at its edge.  A texture is made of the field of the body: the
        render has to be fine enough to show it -- a resolution of about four samples to the size.  E.g. a rough grip on the top face:
        top = select_surface(part, seed=(0, 0, 10)); gripped = texture(part, 'grit', depth=0.2, size=0.6, region=top)  '''
    if side not in ('both', 'in', 'out'):
        raise ValueError("texture: side is 'both', 'in' or 'out'")
    if not is_field(depth) and not float(depth) >= 0:
        raise ValueError('texture: depth is a height in mm, not negative')
    if not is_field(size) and not float(size) > 0:
        raise ValueError('texture: size is a length in mm, above zero')
    height = _texture_height(pattern, float(size) if not is_field(size) else 1.0, seed, axis, radius)
    # (what the pattern does to the surface: both ways, or in or out only)
    if side == 'in':
        height = (height - 1.0) * 0.5
    elif side == 'out':
        height = (height + 1.0) * 0.5
    else:
        height = height * 0.5
    shift = height * depth
    if region is not None:
        reach = float(fade) if fade is not None else (float(size) if not is_field(size) else 1.0)
        if hasattr(region, 'patch'):
            near = (region.patch * (1.0 / reach)).min(1.0)                   # (a patch of a surface: how far from it, 0 at it and 1 at `reach`)
        else:
            near = (-region * (1.0 / reach)).max(0.0).min(1.0)               # (a body: how far inside it, 1 at `reach` deep)
            near = 1.0 - near
        weight = 1.0 - near * near * (3.0 - 2.0 * near)                       # (1 on the region, falling smoothly to 0)
        shift = shift * weight
    return body - shift


# What makes a field (and not a body) is of the kind 'field' (see fieldes.kinds): the model tree gives it a field's icon, the
# section viewer shows it (a plane coloured by it) when it is selected.  A wall made round one is a body again.
for _name in ('x_field', 'y_field', 'z_field', 'radial_field', 'angle_field', 'polar_field', 'distance_to_point',
              'distance_to_points', 'distance_to_line', 'distance_to_segment', 'distance_to_polyline',
              'distance_to_plane', 'distance_to_surface', 'depth_below', 'clamp', 'ramp', 'remap_field', 'normalize',
              'lerp', 'mix', 'smoothstep', 'step_field', 'attractor', 'wave', 'sum_fields', 'noise_field',
              'gradient_field', 'gradient_magnitude', 'normal_field', 'curvature_field', 'overhang_angle',
              'overhang_mask', 'wall_thickness', 'field_from_points', 'field_from_csv',
              'add_fields', 'subtract_fields', 'multiply_fields', 'divide_fields', 'power_field', 'min_fields', 'max_fields',
              'abs_field', 'negate_field', 'sqrt_field', 'square_field'):
    globals()[_name] = returns_field(globals()[_name])
for _name in ('thicken', 'shell_centered'):
    globals()[_name] = returns_body(globals()[_name])

# What these take, for the menus and the model tree (see fieldes.kinds.takes): the inside of a body and its edges, and a field selected with it is the number.  (Set here, after the
# wrappers above: they are the functions the script calls.)
from fieldes.kinds import takes as _declare, slot as _slot, BODIES as _BODIES, SHAPES as _SHAPES
for _name in ('smooth', 'round_edges', 'shell_inside', 'shell_outside', 'shell_exact'):
    globals()[_name]._takes = _declare(_BODIES | {'field'}, [_BODIES])
# (one edge of a body: the body and the edge -- a curve made by edge_at -- selected together; a field selected with them is the radius of a fillet)
# (an edge alone is enough -- it knows its body --, and a field selected with it is the radius; a body alone is the fillet of every concave edge)
globals()['fillet']._takes = _declare(_BODIES | {'curve', 'field'}, [{'curve'}]) + _declare(_BODIES | {'field'}, [_BODIES])
globals()['chamfer']._takes = _declare(_BODIES | {'curve'}, [{'curve'}])
# (what the call cannot do without though it has a default: the completion writes it as a placeholder -- `radius=...` -- after however many edges stand before it; the edges are told by what they are, not by their place)
globals()['fillet']._required = ('radius',)
globals()['chamfer']._required = ('size',)
# (and no argument that stands in a place takes it: the edges before it, any number of them, are not the radius -- `_positional` lists what the arguments that are given by their place stand for)
globals()['fillet']._positional = ()
globals()['chamfer']._positional = ()
# (what goes into each input, for the menus that write the call from the models selected -- see fieldes.kinds.slot: the edges that were selected, any number of them, in a row, else the body; a field is the radius)
globals()['fillet']._slots = [_slot('edge', 'curve', _BODIES, many='curve', needed=True), _slot('radius', 'field')]
globals()['chamfer']._slots = [_slot('edge', 'curve', many='curve', needed=True)]
# (the body is the first model selected, whatever it is, and a field selected with it is the number -- the thickness, the radius, the distance)
import inspect as _inspect
for _name in ('offset_exact', 'round_edges', 'shell_centered', 'shell_exact', 'shell_inside', 'shell_outside', 'smooth', 'thicken'):
    _names = list(_inspect.signature(globals()[_name]).parameters)
    globals()[_name]._slots = [_slot(_names[0], _SHAPES, needed=True), _slot(_names[1], 'field')]
# (a surface, a field or a body that is selected with the body is where the texture goes)
globals()['texture']._slots = [_slot('body', _BODIES, needed=True), _slot('region', ('surface', 'field', 'solid', 'profile', 'lattice'), keyword=True)]
globals()['field_from_body']._takes = _declare(_BODIES)
globals()['evaluate']._takes = _declare(_BODIES | {'field'}, [{'point'}])               # (a field or a body, and the point its value is read at, selected with it: the entry of Operations > Field math)
# What READS the field of a body is given the field of the body, its true signed distance (see fieldes.shape.Body and fieldes.stdlib.body_law); a function that makes a field from a body is (it is tagged
# a field-maker above).  What does not -- a union, an offset, a transform -- is given its tree, which is what a boolean and a drawing need.
for _name in ('evaluate', 'sample_grid', 'field_range', 'signed_distance'):
    globals()[_name]._reads_field = True
globals()['distance_to_point']._takes = _declare({'point'})            # (the distance from a point: a point is what it is made of)
globals()['radial_field']._takes = _declare({'point'})                 # (the distance from the axis through a point: made of a point as well -- the user, 2026-10-28: "it takes a point, like I said it should")
globals()['texture']._takes = _declare(_BODIES | {'surface', 'field'}, [_BODIES])      # (a body, and a surface or a field selected with it is where the texture goes)
