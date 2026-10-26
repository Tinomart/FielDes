'''
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
'''
import math
import numbers

from fieldes.shape import Shape
from fieldes.stdlib.csg import union, intersection, difference
from fieldes.stdlib.fields import _shape_bounds, _eval_many, mass_properties
from fieldes.stdlib.shapes import cylinder, _own, _parametric, _bounded
from fieldes.stdlib.transforms import move

__all__ = ['rotate', 'reflect', 'mirror', 'linear_pattern', 'circular_pattern', 'revolve', 'cut_plane', 'split', 'hole_cutter', 'hole', 'screw_spec', 'threaded_rod',
           'capsule', 'tube', 'slot', 'align', 'surface_area', 'section_area', 'interference', 'distance_between']

X, Y, Z = Shape.X, Shape.Y, Shape.Z
_s = Shape.wrap


################################################################################
# Arguments

def _plain(v, what):
    ''' A plain number: a var() has the number it has in this run '''
    if isinstance(v, Shape):
        try:
            from fieldes.stdlib.curves import value_of
            v = value_of(v)
        except Exception:
            raise TypeError('{} is a number here, not a field'.format(what)) from None
    if isinstance(v, bool) or not isinstance(v, numbers.Real):
        raise TypeError('{} is a number, not {!r}'.format(what, v))
    return float(v)


def _number(v, what):
    ''' A number or a var() (a Shape): arithmetic on it makes the body follow the number when it is dragged '''
    if isinstance(v, Shape):
        return v
    if isinstance(v, bool) or not isinstance(v, numbers.Real):
        raise TypeError('{} is a number, not {!r}'.format(what, v))
    return float(v)


def _positive(v, what):
    ''' A number above 0 (a var() is not checked: it is what it is dragged to) '''
    v = _number(v, what)
    if not isinstance(v, Shape) and not v > 0.0:
        raise ValueError('{} must be more than 0 (it is {:g})'.format(what, v))
    return v


def _count(v, what='count'):
    if isinstance(v, bool) or not isinstance(v, numbers.Integral) or v < 1:
        raise ValueError('{} is a whole number, 1 or more (it is {!r})'.format(what, v))
    return int(v)


def _xyz(v, what, plain=False):
    ''' (x, y, z) of three numbers, of two (z is 0) or of a point of the script: each a number or a var() -- plain=True: each the number it is '''
    v = getattr(v, 'xyz', v)
    pick = _plain if plain else _number
    try:
        out = tuple(pick(c, what) for c in v)
    except TypeError:
        raise TypeError('{} is three numbers (x, y, z) or a point, not {!r}'.format(what, v)) from None
    if len(out) == 2:
        out = (out[0], out[1], 0.0)
    if len(out) != 3:
        raise ValueError('{} is three numbers (x, y, z), not {} of them'.format(what, len(out)))
    return out


def _call(name, v):
    ''' sin, cos, tan, sqrt ... of a number or of a field '''
    return getattr(v, name)() if isinstance(v, Shape) else getattr(math, name)(v)


def _unit(v, what, plain=False):
    ''' The direction of a vector, as three numbers (fields when the vector has some) '''
    v = _xyz(v, what, plain)
    squares = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]
    if not isinstance(squares, Shape) and not squares > 0.0:
        raise ValueError('{} has no direction: it is (0, 0, 0)'.format(what))
    length = _call('sqrt', squares)
    return (v[0] / length, v[1] / length, v[2] / length)


def _union_all(items):
    ''' The union of bodies, in pairs (a tree as deep as the logarithm of their number, not as deep as their number) '''
    items = list(items)
    while len(items) > 1:
        items = [union(items[i], items[i + 1]) if i + 1 < len(items) else items[i] for i in range(0, len(items), 2)]
    return items[0]


def _box(shape):
    ''' The box a body fills, ((x0, y0, z0), (x1, y1, z1)) in plain numbers, or None when it cannot be told '''
    try:
        lo, hi = _shape_bounds(shape, None)
        return tuple(float(c) for c in lo), tuple(float(c) for c in hi)
    except Exception:
        return None


################################################################################
# Turning, mirroring, patterns

def _rotation(axis, angle):
    ''' The matrix that turns a point by `angle` degrees about the unit vector `axis` (the right-hand rule): numbers, or fields where the axis or the angle is one '''
    x, y, z = axis
    a = angle * (math.pi / 180.0)
    c, s = _call('cos', a), _call('sin', a)
    t = 1.0 - c
    return ((t * x * x + c, t * x * y - s * z, t * x * z + s * y),
            (t * x * y + s * z, t * y * y + c, t * y * z - s * x),
            (t * x * z - s * y, t * y * z + s * x, t * z * z + c))


def rotate(shape, axis=(0, 0, 1), angle=0.0, center=(0, 0, 0)):
    ''' The body turned by `angle` degrees about the line through `center` with the direction `axis` (the right-hand rule: the thumb along the axis, the fingers the
        way it turns).  rotate_x, rotate_y and rotate_z are this about an axis of the coordinates, in radians.  Each number may be a var() '''
    k = _unit(axis, 'axis')
    c = _xyz(center, 'center')
    r = _rotation(k, _number(angle, 'angle'))
    qx, qy, qz = X() - c[0], Y() - c[1], Z() - c[2]
    # (a point is in the turned body when the point that was turned onto it is in the body: the inverse of a rotation is its transpose)
    return _s(shape).remap(qx * r[0][0] + qy * r[1][0] + qz * r[2][0] + c[0],
                           qx * r[0][1] + qy * r[1][1] + qz * r[2][1] + c[1],
                           qx * r[0][2] + qy * r[1][2] + qz * r[2][2] + c[2])


def reflect(shape, normal=(1, 0, 0), point=(0, 0, 0)):
    ''' The mirror image of the body in the plane through `point` that is square to `normal`: the body itself is gone, only its image is left.  (mirror keeps both.)
        It is a plain operation on one body, with no link to it: for an image that follows the original as you edit it, use a symmetry (symmetry_planes, Shift+S) '''
    n = _unit(normal, 'normal')
    c = _xyz(point, 'point')
    d = (X() - c[0]) * n[0] + (Y() - c[1]) * n[1] + (Z() - c[2]) * n[2]
    return _s(shape).remap(X() - d * (2.0 * n[0]), Y() - d * (2.0 * n[1]), Z() - d * (2.0 * n[2]))


def mirror(shape, normal=(1, 0, 0), point=(0, 0, 0)):
    ''' The body and its mirror image in the plane through `point` that is square to `normal`, as one body (a part that is made on one side of its plane of symmetry,
        then mirrored).  Where the body crosses the plane the two halves fuse.  A plain operation, a finished body with no link to the original -- a symmetry
        (symmetry_planes) is the live version, whose images follow every edit '''
    return union(_s(shape), reflect(shape, normal, point))


def linear_pattern(shape, count, step):
    ''' `count` copies of the body in a row, the first where the body is and each one `step` (x, y, z) from the one before, as one body: the union of the copies, which is exact
        everywhere.  `step` may be var() numbers '''
    n = _count(count)
    v = _xyz(step, 'step')
    return _union_all(move(shape, (v[0] * k, v[1] * k, v[2] * k)) for k in range(n))


def _axis_frame(k):
    ''' Two unit vectors u, v square to the unit vector k and to each other, so that (u, v, k) is right-handed '''
    helper = (1.0, 0.0, 0.0) if abs(k[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = (helper[1] * k[2] - helper[2] * k[1], helper[2] * k[0] - helper[0] * k[2], helper[0] * k[1] - helper[1] * k[0])
    lu = math.sqrt(sum(c * c for c in u))
    u = tuple(c / lu for c in u)
    v = (k[1] * u[2] - k[2] * u[1], k[2] * u[0] - k[0] * u[2], k[0] * u[1] - k[1] * u[0])
    return u, v


def circular_pattern(shape, count, axis=(0, 0, 1), center=(0, 0, 0), angle=360.0):
    ''' `count` copies of the body round the line through `center` with the direction `axis`, as one body: the union of the copies, which is exact everywhere.  With the whole
        turn (`angle` 360, the default) the copies are evenly spread and the last one is not on the first; with less (a number, or a var(), which always means this), the first
        is where the body is and the last `angle` degrees from it '''
    n = _count(count)
    a = _number(angle, 'angle')
    whole = not isinstance(a, Shape) and abs(a) >= 360.0 - 1e-9
    step = a / n if whole else (a / (n - 1) if n > 1 else 0.0)
    return _union_all(rotate(shape, axis, step * k, center) for k in range(n))


################################################################################
# Revolving and cutting

def _profile_side(profile, nx, ny, p):
    ''' Which side of the axis (through the plain point p, the right of it is (nx, ny)) a 2D profile lies on: 1 the right, -1 the left, 0 both or not known '''
    box = _box(profile)
    if box is None:
        return 0
    lo, hi = box
    across = [(x - p[0]) * nx + (y - p[1]) * ny for x in (lo[0], hi[0]) for y in (lo[1], hi[1])]
    if min(across) >= -1e-9:
        return 1
    if max(across) <= 1e-9:
        return -1
    return 0


def revolve(profile, angle=360.0, axis_point=(0, 0), axis_direction=(0, 1)):
    ''' The body that a 2D profile makes by turning about a line in its own plane (the plane of x and y: the profile is what a 2D shape is at z = 0).  The line goes through
        `axis_point` (x, y) and has the direction `axis_direction` (x, y); with the default it is the y axis.  `angle` is how far it turns, 360 for a body of revolution; with
        less, the part of the profile on the right of the axis (seen along its direction: x > 0 for the y axis) turns from the plane of the profile toward +z, and the part on
        the left turns the same way round the axis (so toward -z): a profile may lie on either side, or on both.  The angle, the point and the profile's numbers may be var()
        (a var() angle always makes a turn that stops at the angle, 360 included).  revolve_y is this for the whole turn about a line parallel to y '''
    a = _number(angle, 'angle')
    p = _xyz(axis_point, 'axis_point')
    d = _xyz(axis_direction, 'axis_direction', plain=True)
    if not isinstance(a, Shape) and not 0.0 < a <= 360.0:
        raise ValueError('angle is more than 0 and at most 360 degrees (it is {:g})'.format(a))
    length = math.hypot(d[0], d[1])
    if not length > 0.0:
        raise ValueError('axis_direction has no direction in the plane of the profile: it is (0, 0)')
    dx, dy = d[0] / length, d[1] / length
    nx, ny = dy, -dx                                  # (the right of the axis, in the plane of the profile)
    qx, qy = X() - p[0], Y() - p[1]
    t = qx * dx + qy * dy                             # (along the axis)
    s = qx * nx + qy * ny                             # (across it, in the plane of the profile)
    r = (s.square() + Z().square()).sqrt()            # (the distance from the axis)
    flat = _s(profile)
    zero = _s(0.0)

    def sweep(shape2d):
        ''' the body of revolution of a 2D shape that lies on the right of the axis (a point is in it when the point of the plane that it turns onto is in the shape) '''
        return shape2d.remap(t * dx + r * nx + p[0], t * dy + r * ny + p[1], zero)

    if not isinstance(a, Shape) and a >= 360.0 - 1e-9:
        return union(sweep(flat), flat.remap(t * dx - r * nx + p[0], t * dy - r * ny + p[1], zero))
    # The wedge from the profile's plane to `a` degrees.  For a number: two half-spaces, whose intersection is the wedge up to 180 degrees and whose union is it beyond.  For
    # a var(): the polar angle itself, from 0 to a
    if isinstance(a, Shape):
        wedge = (Z().atan2(s) % (2.0 * math.pi)) - a * (math.pi / 180.0)
    else:
        rad = math.radians(a)
        first = -Z()                                                              # (z >= 0: the side the profile turns toward)
        last = Z() * math.cos(rad) - s * math.sin(rad)                            # (the polar angle is at most `a`)
        wedge = intersection(first, last) if a <= 180.0 else union(first, last)
    side = _profile_side(flat, nx, ny, tuple(_plain(c, 'axis_point') for c in p))
    sx = (X() - p[0]) * nx + (Y() - p[1]) * ny                                    # (across the axis, as a function of the plane's x and y)
    tx = (X() - p[0]) * dx + (Y() - p[1]) * dy
    parts = []
    if side >= 0:
        right = flat if side > 0 else intersection(flat, -sx)                     # (what lies on the right)
        parts.append(intersection(sweep(right), wedge))
    if side <= 0:
        left = flat if side < 0 else intersection(flat, sx)                       # (what lies on the left)
        # (mirrored in the axis so that it is on the right, swept like that, and the result turned half a way round the axis: across and z change sign)
        mirrored = left.remap(p[0] + tx * dx - sx * nx, p[1] + tx * dy - sx * ny, Z())
        swept = intersection(sweep(mirrored), wedge)
        parts.append(swept.remap(p[0] + t * dx - s * nx, p[1] + t * dy - s * ny, -Z()))
    return parts[0] if len(parts) == 1 else union(parts[0], parts[1])


def cut_plane(shape, normal=(0, 0, 1), point=(0, 0, 0), keep='above'):
    ''' The body cut by the plane through `point` that is square to `normal`: the part on the side that `normal` points to is kept (`keep='above'`), or the part
        on the other side (`keep='below'`) '''
    n = _unit(normal, 'normal')
    c = _xyz(point, 'point')
    if keep not in ('above', 'below'):
        raise ValueError("keep is 'above' (the side the normal points to) or 'below'")
    d = (X() - c[0]) * n[0] + (Y() - c[1]) * n[1] + (Z() - c[2]) * n[2]       # (positive on the side the normal points to)
    return intersection(_s(shape), -d if keep == 'above' else d)


def split(shape, normal=(0, 0, 1), point=(0, 0, 0)):
    ''' The body cut in two by the plane through `point` that is square to `normal`: (the part the normal points to, the other part) '''
    return cut_plane(shape, normal, point, 'above'), cut_plane(shape, normal, point, 'below')


################################################################################
# Holes

# The metric screws (ISO 261 coarse threads): name -> (pitch, tap drill, clearance hole close / medium (ISO 273), head diameter and head height of a socket head cap screw (ISO 4762)).  mm
_SCREWS = {
    'M2': (0.4, 1.6, 2.2, 2.4, 3.8, 2.0),
    'M2.5': (0.45, 2.05, 2.7, 2.9, 4.5, 2.5),
    'M3': (0.5, 2.5, 3.2, 3.4, 5.5, 3.0),
    'M4': (0.7, 3.3, 4.3, 4.5, 7.0, 4.0),
    'M5': (0.8, 4.2, 5.3, 5.5, 8.5, 5.0),
    'M6': (1.0, 5.0, 6.4, 6.6, 10.0, 6.0),
    'M8': (1.25, 6.8, 8.4, 9.0, 13.0, 8.0),
    'M10': (1.5, 8.5, 10.5, 11.0, 16.0, 10.0),
    'M12': (1.75, 10.2, 13.0, 13.5, 18.0, 12.0),
    'M14': (2.0, 12.0, 15.0, 15.5, 21.0, 14.0),
    'M16': (2.0, 14.0, 17.0, 17.5, 24.0, 16.0),
    'M20': (2.5, 17.5, 21.0, 22.0, 30.0, 20.0),
    'M24': (3.0, 21.0, 25.0, 26.0, 36.0, 24.0),
}


def screw_spec(screw, pitch=None):
    ''' What a metric screw ('M6') asks of the holes for it, in mm, as a dictionary: diameter, pitch, tap_drill (the hole a thread is cut in: the nominal diameter less the pitch),
        minor_diameter (of the thread in the nut: 1.0825 pitches less than the nominal), clearance_close and clearance_medium (the hole the screw passes through, ISO 273),
        head_diameter and head_height (a socket head cap screw, ISO 4762) and countersink_diameter (a 90 degree flat head: 2.24 times the nominal plus 0.4).  `pitch` says a
        fine thread (M8 with pitch 1): the tap drill and the minor diameter follow it '''
    key = str(screw).upper().replace(' ', '')
    if key not in _SCREWS:
        raise ValueError('screw {!r} is not one of {}'.format(screw, ', '.join(_SCREWS)))
    coarse, tap, close, medium, head, height = _SCREWS[key]
    diameter = float(key[1:])
    fine = coarse if pitch is None else _plain(pitch, 'pitch')
    if not fine > 0.0 or not fine < diameter / 2.0:
        raise ValueError('pitch must be more than 0 and less than half the diameter {:g}'.format(diameter))
    return {'name': key, 'diameter': diameter, 'pitch': fine, 'tap_drill': tap if pitch is None else diameter - fine, 'minor_diameter': diameter - 1.0825 * fine,
            'clearance_close': close, 'clearance_medium': medium, 'head_diameter': head, 'head_height': height, 'countersink_diameter': round(2.24 * diameter, 1) + 0.4}


def _thread_phase(pitch, hand='right', center=(0.0, 0.0), anchor=0.0):
    ''' The place along a thread, 0 to 1 over one pitch, as a field: a right-hand thread advances as it turns counterclockwise.  The axis is the vertical line through `center`, and the
        thread starts at the height `anchor` (the cutter of a hole is made on the axis z, from z = 0) '''
    if hand not in ('right', 'left'):
        raise ValueError("hand is 'right' or 'left'")
    turn = (Y() - center[1]).atan2(X() - center[0]) * (1.0 / (2.0 * math.pi))
    return ((Z() - anchor) * (1.0 / pitch) + (turn if hand == 'left' else -turn)) % 1.0


def _thread_flank(phase):
    ''' The ISO thread profile over one pitch: 1 on the flat of the root (an eighth of the pitch), 0 on the flat of the crest (a quarter), the flanks (60 degrees between them)
        between '''
    return ((0.375 - (phase - 0.5).abs()) * (1.0 / 0.3125)).max(0.0).min(1.0)


def hole_cutter(position, diameter=None, depth=None, direction=(0, 0, -1), kind='simple', head_diameter=None, head_depth=None, angle=90.0,
                screw=None, fit='medium', pitch=None, thread_depth=None, modeled=False, hand='right'):
    ''' The shape of a hole, to subtract from a body (hole does that): it starts on the surface at `position` and goes `depth` into the body along `direction` (through
        everything when `depth` is None), `diameter` wide.  `kind` is 'simple', 'counterbore' (a wider, flat-bottomed step at the mouth: `head_diameter` across,
        `head_depth` deep), 'countersink' (a cone at the mouth that opens to `head_diameter` at the surface, `angle` degrees across: 90 for a metric screw) or 'tapped'
        (the hole of a thread).  `screw='M6'` gives the sizes from the screw: the clearance hole for it (`fit` 'medium' or 'close'; for a tapped hole the tap drill), and for a
        counterbore or a countersink the head's room (a socket head and 1 mm, 0.5 mm deeper than it; a flat head) -- anything given as well is used instead.  A tapped hole is the tap
        drill alone (what a drawing shows as a cosmetic thread), or with `modeled=True` the thread itself, an ISO profile of right-hand turns (`hand='left'`) over `thread_depth`
        (default the depth of the hole, else three diameters); a thread wants a fine mesh to draw (a pitch every 8 cells at least).  `position`, `diameter`, `depth` and the head sizes may be
        var() numbers; `direction` is a number.  Use the cutter for a pattern of holes: union the cutters, subtract the union once '''
    if kind not in ('simple', 'counterbore', 'countersink', 'tapped'):
        raise ValueError("kind is 'simple', 'counterbore', 'countersink' or 'tapped'")
    if fit not in ('medium', 'close'):
        raise ValueError("fit is 'medium' or 'close'")
    spec = screw_spec(screw, pitch) if screw is not None else None
    if kind == 'tapped' and spec is None:
        raise ValueError("a tapped hole needs the thread: screw='M6'")
    if spec is not None:
        if diameter is None:
            diameter = spec['tap_drill'] if kind == 'tapped' else spec['clearance_' + fit]
        if kind == 'counterbore':
            head_diameter = spec['head_diameter'] + 1.0 if head_diameter is None else head_diameter
            head_depth = spec['head_height'] + 0.5 if head_depth is None else head_depth
        elif kind == 'countersink' and head_diameter is None:
            head_diameter = spec['countersink_diameter']
    if diameter is None:
        raise ValueError("a hole needs its diameter, or the screw it is for: screw='M6'")
    d = _positive(diameter, 'diameter')
    threaded = kind == 'tapped' and modeled
    radius = (spec['minor_diameter'] / 2.0) if threaded else d / 2.0
    length = 1.0e4 if depth is None else _positive(depth, 'depth')
    margin = d.max(1.0) if isinstance(d, Shape) else max(d, 1.0)             # (the cutter starts above the surface, so that the cut is clean there)
    # (made along +z from the surface z = 0, then put where it goes)
    cutter = cylinder(radius, length + margin, (0, 0, -margin))
    if kind == 'counterbore':
        if head_diameter is None or head_depth is None:
            raise ValueError('a counterbore needs head_diameter and head_depth (or the screw it is for)')
        wide = _positive(head_diameter, 'head_diameter')
        deep = _positive(head_depth, 'head_depth')
        if not isinstance(wide, Shape) and not isinstance(d, Shape) and not wide > d:
            raise ValueError('head_diameter ({:g}) must be more than diameter ({:g})'.format(wide, d))
        shallow = _s(deep).min(length) if isinstance(deep, Shape) or isinstance(length, Shape) else min(deep, length)
        cutter = union(cutter, cylinder(wide / 2.0, shallow + margin, (0, 0, -margin)))
    elif kind == 'countersink':
        if head_diameter is None:
            raise ValueError('a countersink needs head_diameter (or the screw it is for)')
        wide = _positive(head_diameter, 'head_diameter')
        if not isinstance(wide, Shape) and not isinstance(d, Shape) and not wide > d:
            raise ValueError('head_diameter ({:g}) must be more than diameter ({:g})'.format(wide, d))
        a = _plain(angle, 'angle')
        if not 0.0 < a < 180.0:
            raise ValueError('angle (across the cone) is more than 0 and less than 180 degrees')
        half = math.radians(a) / 2.0
        r = (X().square() + Y().square()).sqrt()
        cone = (r - wide / 2.0 + Z() * math.tan(half)) * math.cos(half)       # (the distance to the side of the cone, which is `wide` across at z = 0)
        cone = intersection(intersection(cone, -margin - Z()), Z() - length)
        cutter = union(cutter, cone)
    elif threaded:
        major, minor = spec['diameter'], spec['minor_diameter']
        reach = (length if depth is not None else 3.0 * major) if thread_depth is None else _positive(thread_depth, 'thread_depth')
        r = (X().square() + Y().square()).sqrt()
        groove = r - (minor / 2.0 + _thread_flank(_thread_phase(spec['pitch'], hand)) * ((major - minor) / 2.0))       # (negative in the groove of the thread)
        groove = intersection(intersection(groove, -margin - Z()), Z() - reach)
        cutter = union(cutter, groove)
    return _in_frame(cutter, position, direction)


def hole(body, position, diameter=None, depth=None, direction=(0, 0, -1), kind='simple', head_diameter=None, head_depth=None, angle=90.0,
         screw=None, fit='medium', pitch=None, thread_depth=None, modeled=False, hand='right'):
    ''' The body with a hole in it: it starts on the surface at `position` and goes `depth` into the body along `direction` (the default goes down; through everything
        when `depth` is None), `diameter` wide, or the size of the screw: `hole(plate, (10, 10, 4), screw='M6', kind='counterbore')`.  `kind` is 'simple', 'counterbore',
        'countersink' or 'tapped' (see hole_cutter, which says it all) '''
    return difference(_s(body), hole_cutter(position, diameter, depth, direction, kind, head_diameter, head_depth, angle, screw, fit, pitch, thread_depth, modeled, hand))


def _rod_of(spec, hand, cx, cy, z0, z1, anchor, major=None):
    ''' A rod with the ISO thread of `spec` along the vertical line through (cx, cy), from the height z0 to z1: its numbers are its diameter and the heights of its ends, so the handles pull
        the side of the rod (the diameter starts as the screw's; the pitch stays, and so does the profile of the thread, which is as deep as it was) and an end each.  `anchor` is where the
        thread starts, which an end that is pulled does not move '''
    pitch = spec['pitch']
    if major is None:
        major = spec['diameter']
    depth = 1.2269 * pitch                                           # (from the crest of the thread to its root: the ISO profile)
    r = ((X() - cx).square() + (Y() - cy).square()).sqrt()
    material = r - ((major - depth) * 0.5 + _thread_flank(_thread_phase(pitch, hand, (cx, cy), anchor)) * (depth * 0.5))       # (negative in the thread)
    out = _own(material.max(_s(z0) - Z()).max(Z() - _s(z1)))
    width = _plain(major, 'diameter')
    _bounded(out, (cx - width / 2.0, cy - width / 2.0, z0), (cx + width / 2.0, cy + width / 2.0, z1))
    _parametric(out, [major, z0, z1], lambda v: _rod_of(spec, hand, cx, cy, v[1], v[2], anchor, v[0]))
    return out


def threaded_rod(screw, length, base=(0, 0, 0), pitch=None, hand='right'):
    ''' A rod with a right-hand ISO thread (`hand='left'` for a left-hand one) along its whole length, standing along z on `base`: the screw (`screw='M8'`, `pitch` for a fine one) is the
        nominal diameter, the thread has the ISO profile (the root at the diameter 1.2269 pitches under the nominal).  It fits the hole of hole(..., kind='tapped', modeled=True) of the
        same screw.  Its handles pull its side (the diameter: the pitch stays, and so does the profile of the thread) and its two ends.  Like a thread, it wants a fine mesh to draw '''
    spec = screw_spec(screw, pitch)
    h = _positive(length, 'length')
    b = _xyz(base, 'base')
    return _rod_of(spec, hand, b[0], b[1], b[2], b[2] + h, _plain(b[2], 'base'))


def _in_frame(shape, position, direction):
    ''' A body made along +z from the origin, turned so that +z goes along `direction`, and moved to `position` '''
    p = _xyz(position, 'position')
    k = _unit(direction, 'direction', plain=True)
    if k[2] > 1.0 - 1e-12:
        out = _s(shape)
    elif k[2] < -1.0 + 1e-12:
        out = rotate(shape, (1, 0, 0), 180.0)
    else:
        out = rotate(shape, (-k[1], k[0], 0.0), math.degrees(math.acos(k[2])))       # (about z x direction)
    return move(out, p)


################################################################################
# More shapes.  Each is made of numbers that stand for ONE face, as a rectangle is, so that its handles pull a face each: a capsule's radius is its side and its two heights are its ends; a
# slot's four numbers are its two ends and its two long sides; a tube has its outer wall, its bore and its two ends.  (A shape that is arithmetic on a distance field has no such numbers:
# there is no face to pull, and a pull could only scale it.)  The place a shape stands at is no handle -- the gizmo moves it -- and each builder is also what `handles()` makes the shape
# again with, from the numbers a drag gives it.

def _capsule_field(r, cx, cy, z0, z1):
    ''' The exact distance to a capsule: the segment on the vertical line through (cx, cy) between the heights z0 + r and z1 - r, less r '''
    r, cx, cy, z0, z1 = (_s(v) for v in (r, cx, cy, z0, z1))
    beyond = (z0 + r - Z()).max(Z() - (z1 - r)).max(0.0)                   # (how far a point is past the ends of the axis, along it)
    return ((X() - cx).square() + (Y() - cy).square() + beyond.square() + 1e-12).sqrt() - r


def _capsule_of(r, cx, cy, z0, z1):
    out = _own(_capsule_field(r, cx, cy, z0, z1))
    _bounded(out, (cx - r, cy - r, z0), (cx + r, cy + r, z1))
    return _parametric(out, [r, z0, z1], lambda v: _capsule_of(v[0], cx, cy, v[1], v[2]))


def capsule(radius, height, base=(0, 0, 0)):
    ''' A cylinder with a hemisphere on each end, standing along z on `base`: `height` is the whole length, so it is at least twice the radius.  A body: the distance to its
        surface is exact.  Its handles are its radius (the side) and the heights of its two ends '''
    r = _positive(radius, 'radius')
    h = _positive(height, 'height')
    if not isinstance(r, Shape) and not isinstance(h, Shape) and h < 2.0 * r:
        raise ValueError('a capsule is at least as high as it is wide: height {:g} is less than twice the radius {:g}'.format(h, r))
    b = _xyz(base, 'base')
    return _capsule_of(r, b[0], b[1], b[2], b[2] + h)


def _tube_field(ro, ri, cx, cy, z0, z1, margin):
    ro, ri, z0, z1 = (_s(v) for v in (ro, ri, z0, z1))
    rad = ((X() - cx).square() + (Y() - cy).square() + 1e-12).sqrt()
    outer = (rad - ro).max(z0 - Z()).max(Z() - z1)
    bore = (rad - ri).max(z0 - margin - Z()).max(Z() - z1 - margin)           # (the bore is longer than the tube, so that its ends are not the tube's)
    return outer.max(-bore)


def _tube_of(ro, ri, cx, cy, z0, z1, margin):
    out = _own(_tube_field(ro, ri, cx, cy, z0, z1, margin))
    _bounded(out, (cx - ro, cy - ro, z0), (cx + ro, cy + ro, z1))
    return _parametric(out, [ro, ri, z0, z1], lambda v: _tube_of(v[0], v[1], cx, cy, v[2], v[3], margin))


def tube(outer_radius, inner_radius, height, base=(0, 0, 0)):
    ''' A hollow cylinder (a pipe), standing along z on `base`: `outer_radius` and `inner_radius` are its two radii.  Its handles are its outer wall, its bore and its two ends '''
    ro = _positive(outer_radius, 'outer_radius')
    ri = _positive(inner_radius, 'inner_radius')
    h = _positive(height, 'height')
    if not isinstance(ro, Shape) and not isinstance(ri, Shape) and not ri < ro:
        raise ValueError('inner_radius ({:g}) must be less than outer_radius ({:g})'.format(ri, ro))
    b = _xyz(base, 'base')
    return _tube_of(ro, ri, b[0], b[1], b[2], b[2] + h, 1.0 + 0.1 * _plain(h, 'height'))


def _slot_field(x0, x1, y0, y1):
    ''' The exact distance to an obround whose ends are at x0 and x1 and whose long sides are at y0 and y1: a segment on the middle line between the centres of its two half circles, less the
        radius (half of y1 - y0).  Each number is the position of one face '''
    x0, x1, y0, y1 = (_s(v) for v in (x0, x1, y0, y1))
    rho = (y1 - y0) * 0.5
    yc = (y0 + y1) * 0.5
    along = (x0 + rho - X()).max(X() - (x1 - rho)).max(0.0)
    return (along.square() + (Y() - yc).square() + 1e-12).sqrt() - rho


def _slot_of(x0, x1, y0, y1):
    out = _own(_slot_field(x0, x1, y0, y1))
    out._kind = 'profile'
    _bounded(out, (x0, y0, 0), (x1, y1, 0))
    return _parametric(out, [x0, x1, y0, y1], lambda v: _slot_of(v[0], v[1], v[2], v[3]))


def slot(length, width, center=(0, 0)):
    ''' A 2D slot (an obround: a rectangle with a half circle on each short end), `length` long in x and `width` across, about `center` (x, y); `length` is the whole length,
        so it is at least the width.  Extrude it for a slot in a plate.  Its handles are its four sides: an end pulled makes it longer and keeps the round end, a long side pulled makes it
        wider and keeps both ends round '''
    L = _positive(length, 'length')
    w = _positive(width, 'width')
    if not isinstance(L, Shape) and not isinstance(w, Shape) and L < w:
        raise ValueError('a slot is at least as long as it is wide: length {:g} is less than width {:g}'.format(L, w))
    c = _xyz(center, 'center')
    return _slot_of(c[0] - L * 0.5, c[0] + L * 0.5, c[1] - w * 0.5, c[1] + w * 0.5)


################################################################################
# Placing and measuring

def align(shape, to, axis='z', mode='center', gap=0.0):
    ''' The body moved along one axis ('x', 'y' or 'z') to line up with another body, by the boxes round them: `mode` 'min' puts their low sides on one plane, 'max' their
        high sides, 'center' their middles, 'above' puts the body on the high side of `to` (with `gap` mm between), and 'below' on the low side '''
    from fieldes.stdlib.measure import _extent
    if axis not in ('x', 'y', 'z'):
        raise ValueError("axis is 'x', 'y' or 'z'")
    if mode not in ('min', 'max', 'center', 'above', 'below'):
        raise ValueError("mode is 'min', 'max', 'center', 'above' or 'below'")
    i = 'xyz'.index(axis)
    lo, hi = _extent(shape, 'align')
    tlo, thi = _extent(to, 'align')
    g = _number(gap, 'gap')
    if mode == 'min':
        shift = tlo[i] - lo[i]
    elif mode == 'max':
        shift = thi[i] - hi[i]
    elif mode == 'center':
        shift = 0.5 * (tlo[i] + thi[i]) - 0.5 * (lo[i] + hi[i])
    elif mode == 'above':
        shift = thi[i] + g - lo[i]
    else:
        shift = tlo[i] - g - hi[i]
    offset = [0.0, 0.0, 0.0]
    offset[i] = shift
    return move(shape, tuple(offset))


def _surface(shape, h):
    ''' (vertices, triangles) of the surface of a body, by dual contouring with cells of `h` mm (the kernel's own surface mesher: a mesh of the surface alone) '''
    lo, hi = _shape_bounds(shape, None)
    return _s(shape).get_mesh(tuple(float(lo[i]) - h for i in range(3)), tuple(float(hi[i]) + h for i in range(3)), resolution=1.0 / h)


def _surface_points(shape, h):
    ''' The vertices of the triangles of the surface of a body (the mesher also lists vertices that no triangle uses, and some of them are not on the surface at all), within 4 cells of it.
        A body thinner than a cell has none at that size: the cells are made smaller, up to six times, until it has '''
    for _ in range(6):
        verts, tris = _surface(shape, h)
        used = [verts[i] for i in sorted(set(i for tri in tris for i in tri))]
        # (and a vertex that is far from the surface, in mm, is not one of it: the field of a body is a distance, or near enough to one, where it is not far)
        found = [p for p, value in zip(used, _eval_many(shape, used)) if abs(value) <= 4.0 * h]
        if found:
            return found
        h *= 0.5
    raise ValueError('the surface of the body could not be meshed: it is thinner than the finest cell tried ({:g} mm)'.format(h * 2.0))


def surface_area(shape, element_size=None):
    ''' The area of the surface of a body (mm^2), from a mesh of triangles on its surface: with the default size (a fortieth of the body's longest side) a sphere's is
        about 1 % low, and a finer `element_size` (mm) is closer.  A mesh of this kind cuts the corners of curves, so the area is always a little low '''
    lo, hi = _shape_bounds(shape, None)
    size = max(float(hi[i]) - float(lo[i]) for i in range(3))
    h = size / 40.0 if element_size is None else _plain(_positive(element_size, 'element_size'), 'element_size')
    verts, tris = _surface(shape, h)
    total = 0.0
    for a, b, c in tris:
        ax, ay, az = verts[b][0] - verts[a][0], verts[b][1] - verts[a][1], verts[b][2] - verts[a][2]
        bx, by, bz = verts[c][0] - verts[a][0], verts[c][1] - verts[a][1], verts[c][2] - verts[a][2]
        cx, cy, cz = ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx
        total += 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)
    return total


def section_area(shape, normal=(0, 0, 1), point=(0, 0, 0), resolution=300):
    ''' The area (mm^2) of the cross section of a body with the plane through `point` that is square to `normal`, counted on a grid of `resolution` cells along the larger side
        of the section: about 0.5 % off for 300 '''
    n = _unit(normal, 'normal', plain=True)
    c = _xyz(point, 'point', plain=True)
    cells = _count(resolution, 'resolution')
    u, v = _axis_frame(n)
    lo, hi = _shape_bounds(shape, None)
    spans = [[], []]
    for cx in (lo[0], hi[0]):
        for cy in (lo[1], hi[1]):
            for cz in (lo[2], hi[2]):
                q = (cx - c[0], cy - c[1], cz - c[2])
                spans[0].append(q[0] * u[0] + q[1] * u[1] + q[2] * u[2])
                spans[1].append(q[0] * v[0] + q[1] * v[1] + q[2] * v[2])
    u0, u1, v0, v1 = min(spans[0]), max(spans[0]), min(spans[1]), max(spans[1])
    h = max(u1 - u0, v1 - v0) / cells
    nu, nv = max(1, int(math.ceil((u1 - u0) / h))), max(1, int(math.ceil((v1 - v0) / h)))
    points = []
    for j in range(nv):
        b = v0 + (j + 0.5) * h
        for i in range(nu):
            a = u0 + (i + 0.5) * h
            points.append((c[0] + u[0] * a + v[0] * b, c[1] + u[1] * a + v[1] * b, c[2] + u[2] * a + v[2] * b))
    inside = sum(1 for value in _eval_many(shape, points) if value < 0.0)
    return inside * h * h


def interference(a, b):
    ''' The volume (mm^3) that two bodies have in common, by sampling (about 1 % off for a part that is not thin): 0 when they only touch or do not meet '''
    la, ha = _shape_bounds(a, None)
    lb, hb = _shape_bounds(b, None)
    lo = tuple(max(float(la[i]), float(lb[i])) for i in range(3))
    hi = tuple(min(float(ha[i]), float(hb[i])) for i in range(3))
    if any(hi[i] <= lo[i] for i in range(3)):
        return 0.0
    return mass_properties(intersection(_s(a), _s(b)), lower=lo, upper=hi)['volume']


def _closest(p_points, q_points):
    ''' The closest pair of two lists of points, (distance, p, q): the points of the first that are nearest the box round the second are tried first, and the rest are left out
        as soon as the box alone is farther than what was found '''
    lo = [min(q[i] for q in q_points) for i in range(3)]
    hi = [max(q[i] for q in q_points) for i in range(3)]

    def gap(p):
        total = 0.0
        for i in range(3):
            d = max(lo[i] - p[i], 0.0, p[i] - hi[i])
            total += d * d
        return total

    best, pair = float('inf'), None
    for lower, (px, py, pz) in sorted((gap(p), p) for p in p_points):
        if lower >= best:
            break
        for qx, qy, qz in q_points:
            d = (qx - px) * (qx - px) + (qy - py) * (qy - py) + (qz - pz) * (qz - pz)
            if d < best:
                best, pair = d, ((px, py, pz), (qx, qy, qz))
    return math.sqrt(best), pair[0], pair[1]


def _approaches(p_points, q_points, room, count):
    ''' The closest pairs of two lists of points in up to `count` DIFFERENT places: the closest pair, then the closest pair that leaves out every point within `room` of the first, and
        so on -- each place two bodies come near each other in is tried '''
    found = []
    for _ in range(count):
        if not p_points or not q_points:
            break
        best, p, q = _closest(p_points, q_points)
        found.append((best, p, q))

        def far(points, at):
            return [x for x in points if (x[0] - at[0]) ** 2 + (x[1] - at[1]) ** 2 + (x[2] - at[2]) ** 2 > room * room]

        p_points, q_points = far(p_points, p), far(q_points, q)
    return found


def distance_between(a, b, element_size=None):
    ''' The shortest distance (mm) between the surfaces of two bodies, 0 when they touch or overlap.  The surface of each is meshed (the default size is a fortieth of the longest
        side of the two); the closest pairs of vertices in up to eight different places are found, and each is improved by going from one surface to the other along the gradient of the
        field (the way to the nearest point of a surface that is a distance field, which is exact for a plane and a sphere), until the distance stops getting shorter; the shortest
        of them is the distance.  So a body that comes near the other in several places is measured at each, and not only at the place whose vertices happen to be closest.  It takes
        seconds for two bodies with a hundred thousand vertices between them '''
    from fieldes.stdlib.meshing import _snap_to_surface
    la, ha = _shape_bounds(a, None)
    lb, hb = _shape_bounds(b, None)
    size = max(max(float(h[i]) - float(l[i]) for i in range(3)) for l, h in ((la, ha), (lb, hb)))
    h = size / 40.0 if element_size is None else _plain(_positive(element_size, 'element_size'), 'element_size')
    verts_a = _surface_points(a, h)
    verts_b = _surface_points(b, h)
    if min(_eval_many(_s(b), verts_a)) <= 0.0 or min(_eval_many(_s(a), verts_b)) <= 0.0:
        return 0.0                                    # (a vertex of one is in the other: they overlap)
    shortest = float('inf')
    for best, p, q in _approaches(verts_a, verts_b, 4.0 * h, 8):
        for _ in range(8):
            p = tuple(_snap_to_surface(_s(a), [q], [best], iterations=6)[0])
            q = tuple(_snap_to_surface(_s(b), [p], [best], iterations=6)[0])
            gap = math.sqrt(sum((p[i] - q[i]) ** 2 for i in range(3)))
            if gap >= best - 1e-6 * size:
                break
            best = gap
        shortest = min(shortest, best)
    return float(shortest)


# What the menus offer these for: bodies (a profile for revolve), see fieldes.kinds.takes
from fieldes.kinds import takes as _declare, BODIES as _BODIES

for _fn in (rotate, reflect, mirror, linear_pattern, circular_pattern, cut_plane, hole):
    _fn._takes = _declare(_BODIES, [_BODIES])
revolve._takes = _declare({'profile'}, [{'profile'}])

# What they make, for the model tree before the statement has run (a run says it exactly): a body -- a 3D shape with a gizmo --, or a 2D shape for slot
for _fn in (rotate, reflect, mirror, linear_pattern, circular_pattern, revolve, cut_plane, hole, hole_cutter, threaded_rod, capsule, tube, align):
    _fn._makes = 'solid'
slot._makes = 'profile'
