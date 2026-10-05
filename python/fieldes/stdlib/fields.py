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

__all__ = [
    'x_field', 'y_field', 'z_field', 'radial_field', 'angle_field', 'polar_field',
    'distance_to_point', 'distance_to_points', 'distance_to_line',
    'distance_to_segment', 'distance_to_polyline', 'distance_to_plane',
    'distance_to_surface', 'depth_below', 'signed_distance',
    'ramp', 'clamp', 'lerp', 'smoothstep', 'step_field', 'normalize',
    'remap_field', 'attractor', 'wave', 'sum_fields', 'mix',
    'thicken', 'shell_inside', 'shell_outside', 'shell_centered', 'offset_by',
    'smooth_union', 'smooth_intersection',
    'smooth_difference', 'chamfer_union', 'union_all', 'intersection_all',
    'repeat', 'repeat_polar', 'mirror_x', 'mirror_y', 'mirror_z',
    'twist_z', 'bend_z',
    'evaluate', 'sample_grid', 'field_range', 'volume_of', 'mass_properties',
    'exact_distance', 'offset_exact', 'shell_exact', 'round_edges', 'fillet', 'smooth',
    'gradient_field', 'gradient_magnitude', 'normal_field', 'overhang_angle',
    'overhang_mask', 'wall_thickness', 'curvature_field',
    'field_from_points', 'field_from_csv', 'noise_field',
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
        imported part is replaced by its exact distance (exact_distance(): its mesh and the distance to it), which is
        what these operations mean. A part that was moved, turned or scaled (handles(), a gizmo, transforms in the
        script) has the same placement on its distance; one whose faces are dragged (expose()) has the distance of
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


def radial_field(center=(0, 0, 0), axis='z'):
    ''' Distance from an axis through `center` (cylindrical radius, mm) '''
    c = _vec(center)
    x, y, z = X() - c[0], Y() - c[1], Z() - c[2]
    a, b = {'x': (y, z), 'y': (x, z), 'z': (x, y)}[axis]
    return (a.square() + b.square()).sqrt()


def angle_field(center=(0, 0, 0), axis='z'):
    ''' Angle around an axis through `center`, in radians (-pi..pi) '''
    c = _vec(center)
    x, y, z = X() - c[0], Y() - c[1], Z() - c[2]
    a, b = {'x': (y, z), 'y': (z, x), 'z': (x, y)}[axis]
    return b.atan2(a)


def polar_field(center=(0, 0, 0), axis='z'):
    ''' Angle from an axis through `center`, in radians (0..pi): 0 along the
        axis, pi/2 at its equator (spherical coordinates) '''
    c = _vec(center)
    x, y, z = X() - c[0], Y() - c[1], Z() - c[2]
    a, b, w = {'x': (y, z, x), 'y': (z, x, y), 'z': (x, y, z)}[axis]
    return (a.square() + b.square()).sqrt().atan2(w)


################################################################################
# Distances

def distance_to_point(p):
    ''' Euclidean distance to a point '''
    p = _vec(p)
    return ((X() - p[0]).square() + (Y() - p[1]).square() + (Z() - p[2]).square()).sqrt()


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
    ''' Signed distance to a plane: positive on the side the normal points to '''
    p = _vec(point)
    n = _vec(normal)
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
    ''' Linear map of a field: input_range=(a, b) -> output_range=(va, vb).
        Clamped by default (values beyond the input range hold the end
        values), e.g. ramp(z_field(), (0, 50), (2, 0.5)) is 2 at z = 0,
        falling to 0.5 at z = 50 and above. '''
    a, b = (float(v) for v in input_range)
    va, vb = (float(v) for v in output_range)
    if a == b:
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
    ''' A field that is `strength` at the given points (or curves: pass a
        distance field instead of points) and falls to 0 at `radius`.
        falloff: 'linear', 'smooth' (smoothstep) or 'gauss'. '''
    if isinstance(points, Shape):
        d = points
    else:
        pts = list(points)
        if pts and isinstance(pts[0], numbers.Number):
            pts = [pts]
        d = distance_to_points(pts)
    r = float(radius)
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


def wave(axis='x', period=10.0, amplitude=1.0, phase=0.0):
    ''' A sine wave along an axis (or a field): amplitude * sin(2 pi s / period + phase) '''
    s = {'x': X(), 'y': Y(), 'z': Z()}[axis] if isinstance(axis, str) else _s(axis)
    return amplitude * (s * (2 * math.pi / period) + phase).sin()


def sum_fields(*fields):
    ''' Sum of several fields '''
    out = _s(fields[0])
    for f in fields[1:]:
        out = out + f
    return out


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
    ''' Union with a rounded blend of the given radius where the shapes meet '''
    a, b, k = _s(a), _s(b), float(radius)
    if k <= 0:
        return a.min(b)
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
    if blend:
        out = shapes[0]
        for s in shapes[1:]:
            out = smooth_union(out, s, blend)
        return out
    return _minimum(shapes)


def intersection_all(shapes, blend=0):
    ''' Intersection of many shapes (optionally blended) '''
    shapes = [_s(s) for s in shapes]
    if blend:
        out = shapes[0]
        for s in shapes[1:]:
            out = smooth_intersection(out, s, blend)
        return out
    return _maximum(shapes)


def repeat(shape, spacing, center=(0, 0, 0)):
    ''' Repeats a shape infinitely on a grid.  spacing: a number or (sx, sy,
        sz); 0 along an axis leaves that axis alone.  The shape should fit
        inside one grid cell centred on `center`. '''
    if isinstance(spacing, numbers.Number):
        spacing = (spacing,) * 3
    c = _vec(center)
    coords = []
    for axis, (v, s, o) in enumerate(zip((X(), Y(), Z()), spacing, c)):
        if s:
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
        of a solid, by grid sampling.  Returns a dict. '''
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
    count = 0
    sx = sy = sz = 0.0
    for p, v in zip(pts, vals):
        if v < 0:
            count += 1
            sx += p[0]
            sy += p[1]
            sz += p[2]
    vol = count * cell
    centroid = (sx / count, sy / count, sz / count) if count else (float('nan'),) * 3
    return {'volume': vol, 'mass': vol * float(density) / 1000.0,
            'centroid': centroid, 'fill_fraction': count / float(len(pts)),
            'samples': n}


################################################################################
# Exact distance fields (re-distancing) and what they make possible

def find_extent(shape, budget=200000, resolution=2.0, half=1.0e6):
    ''' The box ((x0, y0, z0), (x1, y1, z1)) round the inside of a shape, found by searching it with interval
        arithmetic -- or None when it has no extent that can be found (it is empty, or open on a side, or the search
        ran out of cells).  A shape with var() numbers is searched with the numbers they have in the script: inside
        the application a var() is held by the program, and a search that did not know its number would read it as 0
        (a box of size var(2) is no box at all) '''
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


def exact_distance(shape, bounds=None, resolution=None, margin=0.0):
    ''' The exact signed distance field of a shape (mm): the shape is meshed
        and the distance to that mesh is used.  Many shapes built with
        booleans, blends or warps have fields that are only roughly a
        distance (e.g. flat, square-cornered outside a box's edges); after
        this, offsets and shells are uniform and depth_below() is a true
        depth.  resolution: mesh samples per mm (default ~150 along the
        longest side); margin: extra room around the bounds for offsets.
        (A shape with var() numbers is meshed with the numbers they have in
        the script, and the result is remembered by them.) '''
    return _exact_distance(shape, bounds, resolution, margin, _vars_key())


_EXACT_PAD = 0.03       # (the room exact_distance leaves round the bounds, as a share of their longest side)


@content_cached('exact_distance', limit=8)
def _exact_distance(shape, bounds=None, resolution=None, margin=0.0, script_vars=None):
    ''' The exact signed distance field of a shape (mm): the shape is meshed
        and the distance to that mesh is used.  Many shapes built with
        booleans, blends or warps have fields that are only roughly a
        distance (e.g. flat, square-cornered outside a box's edges); after
        this, offsets and shells are uniform and depth_below() is a true
        depth.  resolution: mesh samples per mm (default ~150 along the
        longest side); margin: extra room around the bounds for offsets. '''
    shape = _s(shape)
    lo, hi = _shape_bounds(shape, bounds)
    size = max(hi[i] - lo[i] for i in range(3))
    pad = _EXACT_PAD * size + float(margin)
    res = float(resolution) if resolution else 150.0 / size
    from fieldes.ffi import libfive_region_t, libfive_interval_t, libfive_mesh_import_info_t
    region = libfive_region_t(*[libfive_interval_t(a - pad, b + pad) for a, b in zip(lo, hi)])
    mesh_p = render_mesh(shape, region, res)
    if not mesh_p:
        raise ValueError('exact_distance: the shape could not be meshed')
    try:
        mesh = mesh_p[0]
        if mesh.tri_count == 0:
            raise ValueError('exact_distance: the shape has no surface in its bounds')
        verts = ctypes.cast(mesh.verts, ctypes.POINTER(ctypes.c_float))
        tris = ctypes.cast(mesh.tris, ctypes.POINTER(ctypes.c_uint32))
        info = libfive_mesh_import_info_t()
        ptr = lib.libfive_mesh_from_arrays(verts, mesh.vert_count, tris, mesh.tri_count,
                                           1.0, ctypes.byref(info))
    finally:
        lib.libfive_mesh_delete(mesh_p)
    if not ptr:
        raise ValueError('exact_distance: ' + lib.libfive_import_mesh_last_message().decode(
            'utf-8', 'replace'))
    out = Shape(ptr)
    out._bounds = (tuple(lo), tuple(hi))
    return out


def offset_exact(shape, distance, bounds=None, resolution=None):
    ''' A uniform offset of any shape: grows it by `distance` mm everywhere
        (shrinks it for a negative distance), measured along true normals,
        so edges and corners get round, not stretched '''
    d = float(distance)
    e = exact_distance(shape, bounds, resolution, margin=max(d, 0.0) * 1.2)
    out = e - d
    lo, hi = e._bounds
    out._bounds = (tuple(v - max(d, 0.0) for v in lo), tuple(v + max(d, 0.0) for v in hi))
    return out


def shell_exact(shape, thickness, side='inside', bounds=None, resolution=None):
    ''' A hollow shell of uniform thickness (true distance), side = 'inside',
        'outside' or 'center' '''
    t = float(thickness)
    e = exact_distance(shape, bounds, resolution, margin=t * 1.2 if side != 'inside' else 0.0)
    if side == 'inside':
        out = e.max(-(e + t))
    elif side == 'outside':
        out = (e - t).max(-e)
    elif side == 'center':
        out = abs(e) - t / 2
    else:
        raise ValueError("shell_exact: side is 'inside', 'outside' or 'center'")
    out._bounds = e._bounds
    return out


def round_edges(shape, radius, bounds=None, resolution=None):
    ''' Rounds every convex (outside) edge and corner of a shape with the
        given radius: the shape is shrunk by the radius and grown back, with
        exact distances both times.  resolution (samples per mm) sets how
        finely the surfaces are followed; about 4 / radius or finer. '''
    r = float(radius)
    lo, hi = _shape_bounds(_s(shape), bounds)
    size = max(hi[i] - lo[i] for i in range(3))
    res = resolution or max(150.0 / size, 4.0 / r)
    e0 = exact_distance(shape, (lo, hi), res)
    e1 = exact_distance(e0 + r, (lo, hi), res)
    out = e1 - r
    out._bounds = (tuple(lo), tuple(hi))
    return out


def fillet(shape, radius, bounds=None, resolution=None):
    ''' Fills every concave (inside) edge and corner of a shape with a
        fillet of the given radius: the shape is grown by the radius and
        shrunk back, with exact distances both times '''
    r = float(radius)
    lo, hi = _shape_bounds(_s(shape), bounds)
    size = max(hi[i] - lo[i] for i in range(3))
    res = resolution or max(150.0 / size, 4.0 / r)
    e0 = exact_distance(shape, (lo, hi), res, margin=1.3 * r)
    e1 = exact_distance(e0 - r, (tuple(v - r for v in lo), tuple(v + r for v in hi)), res)
    out = e1 + r
    out._bounds = (tuple(lo), tuple(hi))
    return out


_SIX_NEIGHBOURS = ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))
_SMOOTH_MAX_STEPS = 4       # (more steps are one of these at a larger radius: the sum has a term for every point reached)


def _stencil(steps):
    ''' The points (in units of the radius) and weights that `steps` averages over the six neighbours make together:
        the first step's six points weigh 1/6 each, the second's average those again, and so on.  Every step
        makes the same moves in any order, so the terms add up: 6 points for one step, 19 for two, 44 for three. '''
    weights = {(0, 0, 0): 1.0}
    for _ in range(steps):
        out = {}
        for (i, j, k), w in weights.items():
            for a, b, c in _SIX_NEIGHBOURS:
                key = (i + a, j + b, k + c)
                out[key] = out.get(key, 0.0) + w / 6.0
        weights = out
    return weights


def smooth(shape, radius, steps=1):
    ''' Smooths the surface of a body without thickening it: bumps, dents, ridges and stair-steps smaller than
        about `radius` mm are smoothed away, edges and corners are eased, and a flat or gently curved face stays
        where it is (unlike offset() or thicken(), which move or grow the surface).  The field of the body is
        averaged over the points a `radius` away on all six sides -- the smoothing of a mesh's Laplacian, done on the
        field -- and `steps` of those make the field of the smoothed body: more steps smooth further (the reach
        grows as the square root of the number of steps).  Like any smoothing it eases convex features slightly
        inwards and concave ones slightly outwards; the body as a whole does not grow.

        The result is a field like any other, a weighted sum of copies of the body's own field moved by whole
        radii (6 copies for one step, 19 for two, 44 for three, 85 for four; more steps than four are four at a
        larger radius, which smooths as far), so nothing is measured or meshed here: the surface is found when it
        is drawn, and the cost is that of the field times the copies.  The body's field should be about a
        distance (the primitives and what is made of them are; a part imported from STEP is replaced by its exact
        distance, as offset() does), because a field that rises faster smooths by as much more. '''
    r = float(radius)
    n = int(steps)
    if not r > 0:
        raise ValueError('smooth: the radius must be positive (mm)')
    if n < 1:
        raise ValueError('smooth: steps is at least 1')
    if n > _SMOOTH_MAX_STEPS:
        r *= math.sqrt(n / float(_SMOOTH_MAX_STEPS))
        n = _SMOOTH_MAX_STEPS
    from fieldes.stdlib.transforms import move
    field = _dist(shape)
    terms = []
    for index, ((i, j, k), w) in enumerate(sorted(_stencil(n).items())):
        # Each copy sits a ten-thousandth of the radius off its whole-radius point, differently for every copy: a field
        # made of max() and min() (a box) ties with itself along its diagonals, and the mesher's grid of sample points
        # lands on those ties -- the very points it takes the slow way round.  (The weights are symmetric: the surface
        # does not move by more than that, a tenth of a micron for a radius of a millimetre.)
        jitter = [1e-4 * r * (((index + 1) * c) % 1.0 - 0.5) for c in (0.6180339887, 0.7548776662, 0.5698402910)]
        moved = move(field, (i * r + jitter[0], j * r + jitter[1], k * r + jitter[2]))
        terms.append(moved * w)
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


def noise_field(scale=10.0, octaves=4, seed=1, gain=0.5, lacunarity=2.0, amplitude=1.0):
    ''' Smooth random variation (Perlin noise), about -amplitude..amplitude:
        `scale` is the size (mm) of the largest features, each octave adds
        detail half as large and `gain` as strong.  A different seed gives a
        different pattern.  E.g. an organic surface texture:
        part - 0.3 * noise_field(4)  '''
    if getattr(lib, 'libfive_field_noise', None) is None:
        raise RuntimeError('this FielDes library is too old for noise_field')
    n = Shape(lib.libfive_field_noise(float(scale), int(octaves), int(seed) & 0xffffffff,
                                      float(gain), float(lacunarity)))
    return n if amplitude == 1.0 else n * float(amplitude)
