'''
The operations of a CAD program, on bodies that are fields: turning, mirroring, patterns, revolving, cutting by a plane, holes, a few more shapes, and measuring.

    from fieldes import *

    arm = rotate(bar, axis=(1, 1, 0), angle=30, center=(0, 0, 0))           # about any axis through any point (degrees)
    both = mirror(arm, normal=(1, 0, 0), point=(0, 0, 0))                   # the arm and its mirror image in a plane
    studs = linear_pattern(stud, count=4, step=(15, 0, 0))                  # the stud and three more, 15 mm apart
    ring = circular_pattern(blade, count=6, axis=(0, 0, 1), center=(0, 0, 0))     # six blades round an axis
    wheel = revolve(profile, angle=270, axis_point=(0, 0), axis_direction=(0, 1))   # a 2D profile revolved about a line of its plane
    top, bottom = split(block, normal=(0, 0, 1), point=(0, 0, 5))           # a body cut by a plane into its two sides
    plate = hole(plate, position=(10, 10, 4), diameter=5, kind='countersink', head_diameter=10)     # a hole from the top face down
    part = union(capsule(3, 20), tube(8, 6, 12))                            # a capsule, a tube
    print(surface_area(part), section_area(part, (0, 0, 1), (0, 0, 5)), distance_between(a, b), interference(a, b))

Every function takes plain numbers for its places, directions and angles (a point of the script, which has .x .y .z, is taken for a position) and returns a body
that is made of the ones it was given by the fields' own arithmetic, so what comes out is a body like any other: it can be meshed, offset, shelled, exported and measured.
Lengths are in mm and angles in degrees.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import numbers

from fieldes.shape import Shape
from fieldes.stdlib.csg import union, intersection, difference
from fieldes.stdlib.fields import _shape_bounds, _eval_many, distance_to_segment, mass_properties
from fieldes.stdlib.shapes import cylinder
from fieldes.stdlib.transforms import move

__all__ = ['rotate', 'reflect', 'mirror', 'linear_pattern', 'circular_pattern', 'revolve', 'cut_plane', 'split', 'hole_cutter', 'hole', 'capsule', 'tube', 'slot',
           'align', 'surface_area', 'section_area', 'interference', 'distance_between']

X, Y, Z = Shape.X, Shape.Y, Shape.Z
_s = Shape.wrap


################################################################################
# Arguments

def _number(v, what):
    if isinstance(v, bool) or not isinstance(v, numbers.Real):
        raise TypeError('{} is a number, not {!r} (a var() is not taken here: give the number it has)'.format(what, v))
    return float(v)


def _positive(v, what):
    v = _number(v, what)
    if not v > 0.0:
        raise ValueError('{} must be more than 0 (it is {:g})'.format(what, v))
    return v


def _count(v, what='count'):
    if isinstance(v, bool) or not isinstance(v, numbers.Integral) or v < 1:
        raise ValueError('{} is a whole number, 1 or more (it is {!r})'.format(what, v))
    return int(v)


def _xyz(v, what):
    ''' (x, y, z) of three numbers, of two (z is 0) or of a point of the script '''
    v = getattr(v, 'xyz', v)
    try:
        out = tuple(_number(c, what) for c in v)
    except TypeError:
        raise TypeError('{} is three numbers (x, y, z) or a point, not {!r}'.format(what, v)) from None
    if len(out) == 2:
        out = (out[0], out[1], 0.0)
    if len(out) != 3:
        raise ValueError('{} is three numbers (x, y, z), not {} of them'.format(what, len(out)))
    return out


def _unit(v, what):
    v = _xyz(v, what)
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if not length > 0.0:
        raise ValueError('{} has no direction: it is (0, 0, 0)'.format(what))
    return (v[0] / length, v[1] / length, v[2] / length)


def _union_all(items):
    ''' The union of bodies, in pairs (a tree as deep as the logarithm of their number, not as deep as their number) '''
    items = list(items)
    while len(items) > 1:
        items = [union(items[i], items[i + 1]) if i + 1 < len(items) else items[i] for i in range(0, len(items), 2)]
    return items[0]


################################################################################
# Turning, mirroring, patterns

def _rotation(axis, angle):
    ''' The matrix that turns a point by `angle` degrees about the unit vector `axis` (the right-hand rule) '''
    x, y, z = axis
    a = math.radians(angle)
    c, s = math.cos(a), math.sin(a)
    t = 1.0 - c
    return ((t * x * x + c, t * x * y - s * z, t * x * z + s * y),
            (t * x * y + s * z, t * y * y + c, t * y * z - s * x),
            (t * x * z - s * y, t * y * z + s * x, t * z * z + c))


def rotate(shape, axis=(0, 0, 1), angle=0.0, center=(0, 0, 0)):
    ''' The body turned by `angle` degrees about the line through `center` with the direction `axis` (the right-hand rule: the thumb along the axis, the fingers the
        way it turns).  rotate_x, rotate_y and rotate_z are this about an axis of the coordinates, in radians '''
    k = _unit(axis, 'axis')
    c = _xyz(center, 'center')
    r = _rotation(k, _number(angle, 'angle'))
    qx, qy, qz = X() - c[0], Y() - c[1], Z() - c[2]
    # (a point is in the turned body when the point that was turned onto it is in the body: the inverse of a rotation is its transpose)
    return _s(shape).remap(qx * r[0][0] + qy * r[1][0] + qz * r[2][0] + c[0],
                           qx * r[0][1] + qy * r[1][1] + qz * r[2][1] + c[1],
                           qx * r[0][2] + qy * r[1][2] + qz * r[2][2] + c[2])


def reflect(shape, normal=(1, 0, 0), point=(0, 0, 0)):
    ''' The mirror image of the body in the plane through `point` that is square to `normal`: the body itself is gone, only its image is left.  (mirror keeps both) '''
    n = _unit(normal, 'normal')
    c = _xyz(point, 'point')
    d = (X() - c[0]) * n[0] + (Y() - c[1]) * n[1] + (Z() - c[2]) * n[2]
    return _s(shape).remap(X() - d * (2.0 * n[0]), Y() - d * (2.0 * n[1]), Z() - d * (2.0 * n[2]))


def mirror(shape, normal=(1, 0, 0), point=(0, 0, 0)):
    ''' The body and its mirror image in the plane through `point` that is square to `normal`, as one body (a part that is made on one side of its plane of symmetry,
        then mirrored).  Where the body crosses the plane the two halves fuse '''
    return union(_s(shape), reflect(shape, normal, point))


def linear_pattern(shape, count, step):
    ''' `count` copies of the body in a row, the first where the body is and each one `step` (x, y, z) from the one before, as one body '''
    n = _count(count)
    v = _xyz(step, 'step')
    return _union_all(move(shape, (v[0] * k, v[1] * k, v[2] * k)) for k in range(n))


def circular_pattern(shape, count, axis=(0, 0, 1), center=(0, 0, 0), angle=360.0):
    ''' `count` copies of the body round the line through `center` with the direction `axis`, as one body.  With the whole turn (`angle` 360, the default) the copies
        are evenly spread and the last one is not on the first; with less, the first is where the body is and the last `angle` degrees from it '''
    n = _count(count)
    a = _number(angle, 'angle')
    step = a / n if abs(a) >= 360.0 - 1e-9 else (a / (n - 1) if n > 1 else 0.0)
    return _union_all(rotate(shape, axis, step * k, center) for k in range(n))


################################################################################
# Revolving and cutting

def revolve(profile, angle=360.0, axis_point=(0, 0), axis_direction=(0, 1)):
    ''' The body that a 2D profile makes by turning about a line in its own plane (the plane of x and y: the profile is what a 2D shape is at z = 0).  The line goes through
        `axis_point` (x, y) and has the direction `axis_direction` (x, y); with the default it is the y axis.  `angle` is how far it turns, 360 for a body of revolution.
        With less than the whole turn the profile must lie on the right of the axis (seen along its direction: x > 0 for the y axis), and it turns from the plane of the
        profile toward z.  revolve_y is this for the whole turn about a line parallel to y '''
    a = _number(angle, 'angle')
    if not 0.0 < a <= 360.0:
        raise ValueError('angle is more than 0 and at most 360 degrees (it is {:g})'.format(a))
    p = _xyz(axis_point, 'axis_point')
    d = _xyz(axis_direction, 'axis_direction')
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
    on_right = flat.remap(t * dx + r * nx + p[0], t * dy + r * ny + p[1], zero)
    if a >= 360.0 - 1e-9:
        on_left = flat.remap(t * dx - r * nx + p[0], t * dy - r * ny + p[1], zero)
        return union(on_right, on_left)
    # (the wedge from the profile's plane to `a` degrees: two half-spaces, whose intersection is the wedge up to 180 degrees and whose union is it beyond)
    rad = math.radians(a)
    first = -Z()                                                              # (z >= 0: the side the profile turns toward)
    last = Z() * math.cos(rad) - s * math.sin(rad)                            # (the polar angle is at most `a`)
    wedge = intersection(first, last) if a <= 180.0 else union(first, last)
    return intersection(on_right, wedge)


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

def hole_cutter(position, diameter, depth=None, direction=(0, 0, -1), kind='simple', head_diameter=None, head_depth=None, angle=90.0):
    ''' The shape of a hole, to subtract from a body (hole does that): it starts on the surface at `position` and goes `depth` into the body along `direction` (through
        everything when `depth` is None), `diameter` wide.  `kind` is 'simple', 'counterbore' (a wider, flat-bottomed step at the mouth: `head_diameter` across,
        `head_depth` deep) or 'countersink' (a cone at the mouth that opens to `head_diameter` at the surface, `angle` degrees across: 90 for a metric screw).  Use it for
        a pattern of holes: union the cutters, subtract the union once '''
    if kind not in ('simple', 'counterbore', 'countersink'):
        raise ValueError("kind is 'simple', 'counterbore' or 'countersink'")
    d = _positive(diameter, 'diameter')
    radius = d / 2.0
    length = 1.0e4 if depth is None else _positive(depth, 'depth')
    margin = max(d, 1.0)                              # (the cutter starts above the surface, so that the cut is clean there)
    # (made along +z from the surface z = 0, then put where it goes)
    cutter = cylinder(radius, length + margin, (0, 0, -margin))
    if kind == 'counterbore':
        if head_diameter is None or head_depth is None:
            raise ValueError('a counterbore needs head_diameter and head_depth')
        wide = _positive(head_diameter, 'head_diameter')
        deep = _positive(head_depth, 'head_depth')
        if not wide > d:
            raise ValueError('head_diameter ({:g}) must be more than diameter ({:g})'.format(wide, d))
        cutter = union(cutter, cylinder(wide / 2.0, min(deep, length) + margin, (0, 0, -margin)))
    elif kind == 'countersink':
        if head_diameter is None:
            raise ValueError('a countersink needs head_diameter')
        wide = _positive(head_diameter, 'head_diameter')
        if not wide > d:
            raise ValueError('head_diameter ({:g}) must be more than diameter ({:g})'.format(wide, d))
        a = _number(angle, 'angle')
        if not 0.0 < a < 180.0:
            raise ValueError('angle (across the cone) is more than 0 and less than 180 degrees')
        half = math.radians(a) / 2.0
        r = (X().square() + Y().square()).sqrt()
        cone = (r - wide / 2.0 + Z() * math.tan(half)) * math.cos(half)       # (the distance to the side of the cone, which is `wide` across at z = 0)
        cone = intersection(intersection(cone, -margin - Z()), Z() - length)
        cutter = union(cutter, cone)
    return _in_frame(cutter, position, direction)


def hole(body, position, diameter, depth=None, direction=(0, 0, -1), kind='simple', head_diameter=None, head_depth=None, angle=90.0):
    ''' The body with a hole in it: it starts on the surface at `position` and goes `depth` into the body along `direction` (the default goes down; through everything
        when `depth` is None), `diameter` wide; `kind` 'counterbore' or 'countersink' adds the step or the cone at the mouth (see hole_cutter) '''
    return difference(_s(body), hole_cutter(position, diameter, depth, direction, kind, head_diameter, head_depth, angle))


def _in_frame(shape, position, direction):
    ''' A body made along +z from the origin, turned so that +z goes along `direction`, and moved to `position` '''
    p = _xyz(position, 'position')
    k = _unit(direction, 'direction')
    if k[2] > 1.0 - 1e-12:
        out = _s(shape)
    elif k[2] < -1.0 + 1e-12:
        out = rotate(shape, (1, 0, 0), 180.0)
    else:
        out = rotate(shape, (-k[1], k[0], 0.0), math.degrees(math.acos(k[2])))       # (about z x direction)
    return move(out, p)


################################################################################
# More shapes

def capsule(radius, height, base=(0, 0, 0)):
    ''' A cylinder with a hemisphere on each end, standing along z on `base`: `height` is the whole length, so it is at least twice the radius '''
    r = _positive(radius, 'radius')
    h = _positive(height, 'height')
    if h < 2.0 * r:
        raise ValueError('a capsule is at least as high as it is wide: height {:g} is less than twice the radius {:g}'.format(h, r))
    b = _xyz(base, 'base')
    return distance_to_segment((b[0], b[1], b[2] + r), (b[0], b[1], b[2] + h - r)) - r


def tube(outer_radius, inner_radius, height, base=(0, 0, 0)):
    ''' A hollow cylinder (a pipe), standing along z on `base`: `outer_radius` and `inner_radius` are its two radii '''
    ro = _positive(outer_radius, 'outer_radius')
    ri = _positive(inner_radius, 'inner_radius')
    h = _positive(height, 'height')
    if not ri < ro:
        raise ValueError('inner_radius ({:g}) must be less than outer_radius ({:g})'.format(ri, ro))
    b = _xyz(base, 'base')
    margin = 1.0 + 0.1 * h                            # (the bore is longer than the tube, so that its ends are not the tube's)
    return difference(cylinder(ro, h, b), cylinder(ri, h + 2.0 * margin, (b[0], b[1], b[2] - margin)))


def slot(length, width, center=(0, 0)):
    ''' A 2D slot (an obround: a rectangle with a half circle on each short end), `length` long in x and `width` across, about `center` (x, y); `length` is the whole length,
        so it is at least the width.  Extrude it for a slot in a plate '''
    L = _positive(length, 'length')
    w = _positive(width, 'width')
    if L < w:
        raise ValueError('a slot is at least as long as it is wide: length {:g} is less than width {:g}'.format(L, w))
    c = _xyz(center, 'center')
    along = ((X() - c[0]).abs() - (L - w) / 2.0).max(0.0)
    return (along.square() + (Y() - c[1]).square()).sqrt() - w / 2.0


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
    ''' The vertices of the triangles of the surface of a body (the mesher also lists vertices that no triangle uses, and some of them are not on the surface at all), within 4 cells of it '''
    verts, tris = _surface(shape, h)
    used = [verts[i] for i in sorted(set(i for tri in tris for i in tri))]
    # (and a vertex that is far from the surface, in mm, is not one of it: the field of a body is a distance, or near enough to one, where it is not far)
    return [p for p, value in zip(used, _eval_many(shape, used)) if abs(value) <= 4.0 * h]


def surface_area(shape, element_size=None):
    ''' The area of the surface of a body (mm^2), from a mesh of triangles on its surface: with the default size (a fortieth of the body's longest side) a sphere's is
        about 1 % low, and a finer `element_size` (mm) is closer.  A mesh of this kind cuts the corners of curves, so the area is always a little low '''
    lo, hi = _shape_bounds(shape, None)
    size = max(float(hi[i]) - float(lo[i]) for i in range(3))
    h = size / 40.0 if element_size is None else _positive(element_size, 'element_size')
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
    n = _unit(normal, 'normal')
    c = _xyz(point, 'point')
    cells = _count(resolution, 'resolution')
    helper = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = (n[1] * helper[2] - n[2] * helper[1], n[2] * helper[0] - n[0] * helper[2], n[0] * helper[1] - n[1] * helper[0])
    lu = math.sqrt(sum(x * x for x in u))
    u = tuple(x / lu for x in u)
    v = (n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
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


def distance_between(a, b, element_size=None):
    ''' The shortest distance (mm) between the surfaces of two bodies, 0 when they touch or overlap.  The surface of each is meshed (the default size is a fortieth of the longest
        side of the two) and the closest pair of vertices is found; then that pair is improved by going from one surface to the other along the gradient of the field (the way to the nearest
        point of a surface that is a distance field, which is exact for a plane and a sphere), until the distance stops getting shorter.  It takes seconds for two bodies with
        a hundred thousand vertices between them '''
    from fieldes.stdlib.meshing import _snap_to_surface
    la, ha = _shape_bounds(a, None)
    lb, hb = _shape_bounds(b, None)
    size = max(max(float(h[i]) - float(l[i]) for i in range(3)) for l, h in ((la, ha), (lb, hb)))
    h = size / 40.0 if element_size is None else _positive(element_size, 'element_size')
    verts_a = _surface_points(a, h)
    verts_b = _surface_points(b, h)
    if min(_eval_many(_s(b), verts_a)) <= 0.0 or min(_eval_many(_s(a), verts_b)) <= 0.0:
        return 0.0                                    # (a vertex of one is in the other: they overlap)
    best, p, q = _closest(verts_a, verts_b)
    for _ in range(8):
        p = tuple(_snap_to_surface(_s(a), [q], [best], iterations=6)[0])
        q = tuple(_snap_to_surface(_s(b), [p], [best], iterations=6)[0])
        gap = math.sqrt(sum((p[i] - q[i]) ** 2 for i in range(3)))
        if gap >= best - 1e-6 * size:
            break
        best = gap
    return float(best)


# What the menus offer these for: bodies (a profile for revolve), see fieldes.kinds.takes
from fieldes.kinds import takes as _declare, BODIES as _BODIES

for _fn in (rotate, reflect, mirror, linear_pattern, circular_pattern, cut_plane, hole):
    _fn._takes = _declare(_BODIES, [_BODIES])
revolve._takes = _declare({'profile'}, [{'profile'}])
