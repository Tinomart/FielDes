"""
Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.shapes

Every shape has ONE primitive, with that shape and nothing else: `box`, `cylinder`, `sphere`, ... (no centred or mitered or `_z` variants:
a shape that stands another way is turned with rotate_x / rotate_y / rotate_z, one that sits somewhere else is moved with move).
The 3D shapes stand along z, the 2D shapes lie in the plane z = 0.
"""

from fieldes.ffi import libfive_tree, tfloat, tvec2, tvec3, stdlib, lib
from fieldes.shape import Shape

import ctypes
import math


def _number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _bounded(shape, lo, hi):
    ''' `shape`, told its own extent -- the box it fills, (x, y, z) low and high corners -- when the numbers it is made of are plain numbers.  The gizmo is
        put at the middle of that box, and the region a model is shown in is worked out from it, at once and exactly: without it the extent is searched for
        (and a small shape in a large search is not found in the time there is, which put the gizmo of a new box at the origin) '''
    corners = list(lo) + list(hi)
    if len(corners) == 6 and all(_number(c) and c == c and abs(c) < 1e12 for c in corners):
        shape._bounds = (tuple(float(min(corners[i], corners[3 + i])) for i in range(3)), tuple(float(max(corners[i], corners[3 + i])) for i in range(3)))
    return shape


def _ringed(shape, make):
    ''' `shape`, told how its outline is drawn from numbers: `make()` gives the corners of the outline as (x, y) pairs -- numbers, or expressions of var()s -- round the origin of the
        plane the shape lies in, or None when the numbers it has now are not a shape that an outline can be made of.  `path_extrude` draws a sweep of such a shape natively, from the
        numbers of its path, the way it draws the sweep of a profile, so that a point of the path is followed at once while it is dragged (a curved outline is many short sides) '''
    shape._ring_points = make
    return shape


_RING_STEPS = 72            # (the sides of the outline of a circle: 0.1 % of its radius from the circle)
_RING_ARC = 12              # (the sides of the outline of a quarter of a rounded corner)


# ---------------------------------------------------------------------------------------------------------------------------------------- 2D

stdlib.circle.argtypes = [tfloat, tvec2]
stdlib.circle.restype = libfive_tree
def circle(r, center=(0, 0)):
    """ A 2D circle with the given radius and optional center
    """
    args = [Shape.wrap(r), list([Shape.wrap(i) for i in center])]
    out = Shape(stdlib.circle(
        args[0].ptr,
        tvec2(*[a.ptr for a in args[1]])))
    if _number(r) and len(center) == 2 and all(_number(c) for c in center):
        _bounded(out, (center[0] - r, center[1] - r, 0), (center[0] + r, center[1] + r, 0))
    if len(center) == 2:
        _ringed(out, lambda: _circle_points(args[0], args[1][0], args[1][1]))
        # (what handles() makes the ring of when it puts var()s for the numbers of a circle -- its centre and its radius, in that order -- see handles.py)
        out._ring_params = (3, lambda v: (lambda: _circle_points(v[2], v[0], v[1])), [center[0], center[1], r])
    return out


def _circle_points(R, cx, cy):
    ''' The outline of a circle of radius R round (cx, cy): `_RING_STEPS` corners, as numbers or expressions of them '''
    if _plain(R) <= 0:
        return None
    R, cx, cy = Shape.wrap(R), Shape.wrap(cx), Shape.wrap(cy)
    return [(cx + R * math.cos(2 * math.pi * k / _RING_STEPS), cy + R * math.sin(2 * math.pi * k / _RING_STEPS)) for k in range(_RING_STEPS)]

stdlib.ring.argtypes = [tfloat, tfloat, tvec2]
stdlib.ring.restype = libfive_tree
def ring(ro, ri, center=(0, 0)):
    """ A 2D ring with the given outer/inner radii and optional center
    """
    args = [Shape.wrap(ro), Shape.wrap(ri), list([Shape.wrap(i) for i in center])]
    out = Shape(stdlib.ring(
        args[0].ptr,
        args[1].ptr,
        tvec2(*[a.ptr for a in args[2]])))
    if _number(ro) and len(center) == 2 and all(_number(c) for c in center):
        _bounded(out, (center[0] - ro, center[1] - ro, 0), (center[0] + ro, center[1] + ro, 0))
    return out

stdlib.polygon.argtypes = [tfloat, ctypes.c_int, tvec2]
stdlib.polygon.restype = libfive_tree
def _polygon_sides(r, n, center):
    ''' The unit normals and the offsets of the n sides of the regular polygon with center-to-corner distance r round `center` -- an odd polygon has a corner straight below the
        centre, an even one a side, as the kernel's has -- the side k being the line n . (x, y) = c between the corners k and k + 1 '''
    first = -math.pi / 2 + (math.pi / n if n % 2 == 0 else 0.0)
    apothem = r * math.cos(math.pi / n)
    normals, offsets = [], []
    for k in range(n):
        t = first + (k + 0.5) * 2 * math.pi / n
        nx, ny = math.cos(t), math.sin(t)
        normals.append((nx, ny))
        offsets.append(nx * center[0] + ny * center[1] + apothem)
    return normals, offsets


def _rebuilt_polygon(normals, offsets):
    out = _own(_triangle_planes(normals, offsets))
    _ringed(out, lambda: _triangle_corners(normals, offsets))
    return _parametric(out, offsets, lambda v: _rebuilt_polygon(normals, v))


def polygon(r, n, center=(0, 0)):
    """ A polygon with center-to-vertex distance r and n sides.  Its handles pull a side each -- a side moves parallel to itself, the others stay -- and the gizmo's pivot is where the polygon was made, so a side pulled moves that side only, at any scale
    """
    if _number(r) and r > 0 and isinstance(n, int) and n >= 3 and len(center) == 2 and all(_number(c) for c in center):
        normals, offsets = _polygon_sides(r, n, center)
        out = _own(_triangle_planes(normals, offsets))
        _bounded(out, (center[0] - r, center[1] - r, 0), (center[0] + r, center[1] + r, 0))     # (the corners are within r of the centre: a box that holds it)
        _ringed(out, lambda: _triangle_corners(normals, offsets))
        return _parametric(out, offsets, lambda v: _rebuilt_polygon(normals, v))
    args = [Shape.wrap(r), n, list([Shape.wrap(i) for i in center])]
    out = Shape(stdlib.polygon(
        args[0].ptr,
        args[1],
        tvec2(*[a.ptr for a in args[2]])))
    if _number(r) and len(center) == 2 and all(_number(c) for c in center):
        _bounded(out, (center[0] - r, center[1] - r, 0), (center[0] + r, center[1] + r, 0))     # (the corners are within r of the centre: a box that holds it)
    if len(center) == 2 and isinstance(n, int) and n >= 3:
        def ring():
            if _plain(r) <= 0:
                return None
            # (an odd polygon has a corner straight below the centre, an even one a side: the corners follow round from there)
            R, cx, cy = args[0], args[2][0], args[2][1]
            first = -math.pi / 2 + (math.pi / n if n % 2 == 0 else 0.0)
            return [(cx + R * math.cos(first + 2 * math.pi * k / n), cy + R * math.sin(first + 2 * math.pi * k / n)) for k in range(n)]
        _ringed(out, ring)
    return out

def _own(field):
    ''' A shape that owns a copy of the tree of `field` (`field` is held by the caller for the whole call: a pointer read off a temporary would be of a tree that is already
        freed) '''
    return Shape(lib.libfive_tree_copy(field.ptr))


def _plain(v):
    ''' The number a value has now (a number, or a var() / an expression of var()s) '''
    if isinstance(v, Shape):
        return float(v(0.0, 0.0, 0.0))
    return float(v)


def _parametric(out, params, rebuild):
    ''' What a shape made of a few numbers of its own tells `handles()`: `params` the numbers (in the order its handles list them), `rebuild(values)` the same shape made of other
        values (plain numbers or var()s).  A rectangle's handles are its four sides, a rounded rectangle's the four sides and the radius -- a number each, not the numbers a tree of
        constants happens to have.  The pivot of a gizmo on it is where the shape was made (see handles._centre_of): plain numbers that no handle changes, so a side that is pulled
        moves that side only, whatever the gizmo's scale '''
    out._handle_params = [_plain(p) for p in params]
    out._handle_rebuild = rebuild
    return out


def _rect_distance(a0, a1, b0, b1, r=None):
    ''' The signed distance to the rectangle between the corners (a0, a1) and (b0, b1) (exact: a true distance outside, negative inside), made of the four numbers each
        standing for ONE side -- so a side pulled moves that side and no other -- and, with `r`, of the corners rounded by r: the box of the corners drawn in by r, grown by r
        again, which is tangent to the sides at any numbers.  (A tree of x - centre and half sizes ties the opposite sides to each other, and a rounded rectangle of
        a rectangle and four circles comes apart at the corners when a side is pulled.) '''
    X, Y = Shape.X(), Shape.Y()
    dx = (Shape.wrap(a0) - X).max(X - Shape.wrap(b0))
    dy = (Shape.wrap(a1) - Y).max(Y - Shape.wrap(b1))
    if r is not None:
        r = Shape.wrap(r)
        dx, dy = dx + r, dy + r
    inside = dx.max(dy).min(0)
    # (a hair under the square root -- 1e-6 mm of distance -- so that the field has a gradient ON the sides: the square root of zero has none, and a side that a press landed on
    # exactly could not be pulled)
    outside = (dx.max(0).square() + dy.max(0).square() + 1e-12).sqrt()
    return inside + outside if r is None else inside + outside - r


def rectangle(a, b):
    """ A rectangle between the corners a (the low one) and b (the high one) (a true distance outside it: an offset of it is a rounded rectangle).  Its four numbers are
        its four sides, so the handles of a rectangle pull one side each
    """
    out = _own(_rect_distance(a[0], a[1], b[0], b[1]))
    if len(a) == 2 and len(b) == 2:
        _bounded(out, (a[0], a[1], 0), (b[0], b[1], 0))
        A0, A1, B0, B1 = (Shape.wrap(v) for v in (a[0], a[1], b[0], b[1]))
        _ringed(out, lambda: [(A0, A1), (B0, A1), (B0, B1), (A0, B1)] if _plain(a[0]) != _plain(b[0]) and _plain(a[1]) != _plain(b[1]) else None)
    return _parametric(out, [a[0], a[1], b[0], b[1]], lambda v: rectangle((v[0], v[1]), (v[2], v[3])))


def rounded_rectangle(a, b, r):
    """ A rectangle with rounded corners of radius r, between the corners a (the low one) and b (the high one) (a true distance outside it).  The corners are tangent to the
        sides whatever the numbers are: its handles pull a side each (the radius with them)
    """
    out = _own(_rect_distance(a[0], a[1], b[0], b[1], r))
    if len(a) == 2 and len(b) == 2:
        _bounded(out, (a[0], a[1], 0), (b[0], b[1], 0))

        def ring():
            wide, high, rv = abs(_plain(b[0]) - _plain(a[0])), abs(_plain(b[1]) - _plain(a[1])), _plain(r)
            if wide < 1e-9 or high < 1e-9 or rv < 0 or 2 * rv > min(wide, high) + 1e-9:
                return None            # (a radius that does not fit the sides is not a corner: the field says what it is)
            A0, A1, B0, B1, R = (Shape.wrap(v) for v in (a[0], a[1], b[0], b[1], r))
            if rv < 1e-9:
                return [(A0, A1), (B0, A1), (B0, B1), (A0, B1)]
            points = []
            # (the corners one after the other, each from the side it comes in by to the side it goes out by)
            for (cx, cy), start in (((B0 - R, A1 + R), -90.0), ((B0 - R, B1 - R), 0.0), ((A0 + R, B1 - R), 90.0), ((A0 + R, A1 + R), 180.0)):
                for k in range(_RING_ARC + 1):
                    t = math.radians(start + 90.0 * k / _RING_ARC)
                    points.append((cx + R * math.cos(t), cy + R * math.sin(t)))
            return points
        _ringed(out, ring)
    return _parametric(out, [a[0], a[1], b[0], b[1], r], lambda v: rounded_rectangle((v[0], v[1]), (v[2], v[3]), v[4]))

stdlib.triangle.argtypes = [tvec2, tvec2, tvec2]
stdlib.triangle.restype = libfive_tree
def _triangle_planes(normals, offsets):
    ''' The triangle of three straight sides, each the line n . (x, y) = c with its normal n (a unit vector, out of the triangle) fixed and its offset c a number of its own: the
        greatest of the three distances to the lines.  A side that is pulled moves along its normal -- parallel to itself -- and no other side moves '''
    X, Y = Shape.X(), Shape.Y()
    out = None
    for (nx, ny), c in zip(normals, offsets):
        side = X * nx + Y * ny - Shape.wrap(c)
        out = side if out is None else out.max(side)
    return out


def _triangle_corners(normals, offsets):
    ''' The corners of the triangle (or of a convex polygon, its sides in turn) as expressions of the offsets: a corner is where two sides meet (the normals are numbers, so each
        corner is a linear combination of two offsets) '''
    corners = []
    for i in range(len(normals)):
        (ax, ay), (bx, by) = normals[i - 1], normals[i]
        det = ax * by - ay * bx
        # the corner between side i-1 and side i: n_{i-1} . p = c_{i-1}, n_i . p = c_i
        c0, c1 = Shape.wrap(offsets[i - 1]), Shape.wrap(offsets[i])
        corners.append(((c0 * by - c1 * ay) * (1.0 / det), (c1 * ax - c0 * bx) * (1.0 / det)))
    return corners


def _triangle_sides(corners):
    ''' The unit normals (out of the triangle) and the offsets of the three sides of the triangle of three corners that are plain numbers -- the side i is the line n . (x, y) = c --, or None
        when the corners are on one line '''
    area = sum(corners[i][0] * corners[(i + 1) % 3][1] - corners[(i + 1) % 3][0] * corners[i][1] for i in range(3))
    if abs(area) <= 1e-12:
        return None
    sign = 1.0 if area > 0 else -1.0
    normals, offsets = [], []
    for i in range(3):
        (px, py), (qx, qy) = corners[i], corners[(i + 1) % 3]
        ex, ey = qx - px, qy - py
        length = math.hypot(ex, ey)
        if length < 1e-12:
            return None
        nx, ny = sign * ey / length, -sign * ex / length            # (out of the triangle)
        normals.append((nx, ny))
        offsets.append(nx * px + ny * py)
    return normals, offsets


def triangle(a, b, c):
    """ A 2D triangle between three corners.  Its handles pull a side each: a side moves parallel to itself, and the gizmo's pivot is where the triangle was made, so a side pulled moves that side only, at any scale
    """
    corners = [tuple(a), tuple(b), tuple(c)]
    if all(len(p) == 2 and all(_number(v) for v in p) for p in corners):
        sides = _triangle_sides(corners)
        if sides is not None:
            normals, offsets = sides
            out = _own(_triangle_planes(normals, offsets))
            _bounded(out, (min(a[0], b[0], c[0]), min(a[1], b[1], c[1]), 0), (max(a[0], b[0], c[0]), max(a[1], b[1], c[1]), 0))
            _ringed(out, lambda: _triangle_corners(normals, offsets))
            return _parametric(out, offsets, lambda v: _rebuilt_triangle(normals, v))
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b]), list([Shape.wrap(i) for i in c])]
    out = Shape(stdlib.triangle(
        tvec2(*[a.ptr for a in args[0]]),
        tvec2(*[a.ptr for a in args[1]]),
        tvec2(*[a.ptr for a in args[2]])))
    if all(len(p) == 2 and all(_number(v) for v in p) for p in (a, b, c)):
        _bounded(out, (min(a[0], b[0], c[0]), min(a[1], b[1], c[1]), 0), (max(a[0], b[0], c[0]), max(a[1], b[1], c[1]), 0))
    if all(len(p) == 2 for p in (a, b, c)):
        _ringed(out, lambda: [(args[0][0], args[0][1]), (args[1][0], args[1][1]), (args[2][0], args[2][1])])
    return out


def _rebuilt_triangle(normals, offsets):
    out = _own(_triangle_planes(normals, offsets))
    _ringed(out, lambda: _triangle_corners(normals, offsets))
    return _parametric(out, offsets, lambda v: _rebuilt_triangle(normals, v))


def _prism_planes(normals, offsets, z0, z1):
    ''' The triangular prism: the greatest of the distances to the three side planes of the triangle (`_triangle_planes`) and to the two end planes z0 - z and z - z1.  Every face has a number of its own,
        so a face that is pulled moves along its normal, parallel to itself, and no other face moves '''
    return _triangle_planes(normals, offsets).max(Shape.wrap(z0) - Shape.Z()).max(Shape.Z() - Shape.wrap(z1))


def _rebuilt_prism(normals, v):
    out = _own(_prism_planes(normals, v[0:3], v[3], v[4]))
    return _parametric(out, v, lambda w: _rebuilt_prism(normals, w))


def prism(a, b, c, z0, z1):
    """ A triangular prism: the triangle of the three corners a, b, c (x, y) between the heights z0 and z1.  Its handles pull a face each -- the three sides and the two ends -- parallel to itself,
        and the gizmo's pivot is where the prism was made, so a face pulled moves that face only, at any scale
    """
    corners = [tuple(a), tuple(b), tuple(c)]
    if all(len(p) == 2 and all(_number(v) for v in p) for p in corners) and _number(z0) and _number(z1):
        sides = _triangle_sides(corners)
        if sides is not None:
            normals, offsets = sides
            out = _own(_prism_planes(normals, offsets, z0, z1))
            _bounded(out, (min(p[0] for p in corners), min(p[1] for p in corners), min(z0, z1)), (max(p[0] for p in corners), max(p[1] for p in corners), max(z0, z1)))
            return _parametric(out, list(offsets) + [z0, z1], lambda v: _rebuilt_prism(normals, v))
    # (corners that are not plain numbers, or on one line: the triangle of them, pulled between the heights)
    return _own(triangle(a, b, c).max(Shape.wrap(z0) - Shape.Z()).max(Shape.Z() - Shape.wrap(z1)))

# ---------------------------------------------------------------------------------------------------------------------------------------- 3D

stdlib.box_exact.argtypes = [tvec3, tvec3]
stdlib.box_exact.restype = libfive_tree
def box(a, b):
    """ A box between two opposite corners (a true distance outside it: an offset of it has rounded edges)
    """
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b])]
    out = Shape(stdlib.box_exact(
        tvec3(*[a.ptr for a in args[0]]),
        tvec3(*[a.ptr for a in args[1]])))
    if len(a) == 3 and len(b) == 3:
        _bounded(out, a, b)
    return out

def _box_distance(a, b, r=None):
    ''' The signed distance to the box between the corners a (the low one) and b (the high one), exact (a true distance outside, negative inside), made of the six numbers each
        standing for ONE side -- so a side that is pulled moves that side and no other -- and, with `r`, of the edges and corners rounded by r: the box of the corners drawn in by r,
        grown by r again.  (The kernel's rounded box is a box of centre and half sizes drawn in and grown by one radius: its numbers tie a side to the opposite one and to the radius.) '''
    X, Y, Z = Shape.X(), Shape.Y(), Shape.Z()
    dx = (Shape.wrap(a[0]) - X).max(X - Shape.wrap(b[0]))
    dy = (Shape.wrap(a[1]) - Y).max(Y - Shape.wrap(b[1]))
    dz = (Shape.wrap(a[2]) - Z).max(Z - Shape.wrap(b[2]))
    if r is not None:
        r = Shape.wrap(r)
        dx, dy, dz = dx + r, dy + r, dz + r
    inside = dx.max(dy).max(dz).min(0)
    # (a hair under the square root, as in the rectangle's: a side that a press landed on exactly can be pulled)
    outside = (dx.max(0).square() + dy.max(0).square() + dz.max(0).square() + 1e-12).sqrt()
    return inside + outside if r is None else inside + outside - r


def _rounded_box_radius(a, b, r):
    ''' The radius of the rounding in length: the fraction r of half the shortest side, as an expression of the six sides '''
    sides = [Shape.wrap(b[i]) - Shape.wrap(a[i]) for i in range(3)]
    return Shape.wrap(r) * sides[0].min(sides[1]).min(sides[2]) * 0.5


def _rebuilt_rounded_box(v):
    out = _own(_box_distance(v[0:3], v[3:6], _rounded_box_radius(v[0:3], v[3:6], v[6])))
    return _parametric(out, v, _rebuilt_rounded_box)


stdlib.rounded_box.argtypes = [tvec3, tvec3, tfloat]
stdlib.rounded_box.restype = libfive_tree
def rounded_box(a, b, r):
    """ Rounded box with the given bounds.  The radius `r` is a fraction, 0 to 1, of half the shortest side of the box (r = 0.4 on a box 15 mm thick rounds its edges with a radius of 3 mm; 1 makes the
        shortest side a half circle).  Its handles pull a side each -- a side moves, the others and the rounding stay -- and the gizmo's pivot is where the box was made, so a side pulled moves that side only, at any scale
    """
    if len(a) == 3 and len(b) == 3:
        out = _own(_box_distance(a, b, _rounded_box_radius(a, b, r)))
        if all(_number(v) for v in list(a) + list(b)):
            _bounded(out, a, b)
        return _parametric(out, list(a) + list(b) + [r], _rebuilt_rounded_box)
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b]), Shape.wrap(r)]
    out = Shape(stdlib.rounded_box(
        tvec3(*[a.ptr for a in args[0]]),
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr))
    if len(a) == 3 and len(b) == 3:
        _bounded(out, a, b)
    return out

stdlib.sphere.argtypes = [tfloat, tvec3]
stdlib.sphere.restype = libfive_tree
def sphere(radius, center=(0, 0, 0)):
    """ A sphere with the given radius and (optional) center
    """
    args = [Shape.wrap(radius), list([Shape.wrap(i) for i in center])]
    out = Shape(stdlib.sphere(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]])))
    if _number(radius) and len(center) == 3 and all(_number(c) for c in center):
        _bounded(out, [c - radius for c in center], [c + radius for c in center])
    return out

stdlib.half_space.argtypes = [tvec3, tvec3]
stdlib.half_space.restype = libfive_tree
def half_space(norm, point=(0, 0, 0)):
    """ The solid on one side of a plane, with no end: everything the normal `norm` points away from, the plane through `point` its only face (the field is the distance along `norm` from
        the plane, negative inside -- a true distance when `norm` is a unit vector).  It is no shape to look at on its own, and it is not in the menus: it is what `intersection(body, half_space(...))`
        and `difference` cut a body by, and a region -- of the `zones` of a mesh, of a support or a load -- that is everything on one side of a plane.  To cut a body by a plane from the menus use
        Operations > Combining > cut_plane
    """
    args = [list([Shape.wrap(i) for i in norm]), list([Shape.wrap(i) for i in point])]
    out = Shape(stdlib.half_space(
        tvec3(*[a.ptr for a in args[0]]),
        tvec3(*[a.ptr for a in args[1]])))
    out._kind = 'solid'                 # (a body, whatever axes its field depends on: a half space square to x or y is no 2D shape)
    return out

stdlib.cylinder_z.argtypes = [tfloat, tfloat, tvec3]
stdlib.cylinder_z.restype = libfive_tree
def cylinder(r, h, base=(0, 0, 0)):
    """ A cylinder with the given radius and height, standing along z on the (optional) base position
    """
    args = [Shape.wrap(r), Shape.wrap(h), list([Shape.wrap(i) for i in base])]
    out = Shape(stdlib.cylinder_z(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]])))
    if _number(r) and _number(h) and len(base) == 3 and all(_number(c) for c in base):
        _bounded(out, (base[0] - r, base[1] - r, base[2]), (base[0] + r, base[1] + r, base[2] + h))
    return out

_APEX = 0.005       # (the share of a cone's base radius that its apex is rounded by: see _cone_planes)


def _cone_planes(slope, axis, offsets, eps=1e-6):
    ''' The cone as two surfaces: the base plane z = zb and the side, a cone of a FIXED slope (the unit normal of its profile (nr, nz) in (r, z)) at an offset e of its own: the side is
        nr r + nz z - e = 0.  Each has ONE number and neither depends on the other's -- the side is measured from the ground, not from the base --, so a face that is pulled moves along
        its normal (the side: the cone grows or shrinks at its slope, round its base) and nothing else changes.  `axis` is where the cone stands (the gizmo moves it: it is no handle);
        `offsets` are e, zb.  r is the root of r^2 + eps^2 (not of r^2 alone): the field has a gradient ON the axis, and the apex is a rounded tip of the size `eps`.  The apex of a perfect
        cone is a point the surface comes to from every side at once: a mesh that has to place a vertex for each cell round it comes out ragged there, whatever the resolution -- the
        cone looks the same near its tip at every scale.  A tip rounded by a small share of the radius is the same at a scale of cells that are finer than it, and so is smooth there '''
    nr, nz = slope
    e, zb = (Shape.wrap(v) for v in offsets)
    X, Y, Z = Shape.X(), Shape.Y(), Shape.Z()
    rad = ((X - axis[0]).square() + (Y - axis[1]).square() + eps * eps).sqrt()
    return (zb - Z).max(rad * nr + Z * nz - e)


def _rebuilt_cone(slope, axis, eps, v):
    out = _own(_cone_planes(slope, axis, v, eps))
    return _parametric(out, v, lambda w: _rebuilt_cone(slope, axis, eps, w))


def cone(radius, height, base=(0, 0, 0)):
    """ A cone with the given base radius and height, standing along z on the (optional) base position: the base is a disc of that radius at the base height and the apex stands
        `height` above its centre.  It has two faces, the base and the side: its handles pull the base (it moves up or down) and the side (the cone grows or shrinks at the same slope, round
        its base), and the axis stays where it is.  The apex is rounded by half a percent of the base radius: a perfect apex is a point the surface comes to from every side at once, and a
        mesh comes out ragged there at any resolution
    """
    if _number(radius) and _number(height) and len(base) == 3 and all(_number(c) for c in base) and radius > 0 and height > 0:
        length = math.hypot(radius, height)
        slope = (height / length, radius / length)                  # (the unit normal of the side in the plane of (r, z))
        axis = (base[0], base[1])
        offsets = [radius * height / length + slope[1] * base[2], base[2]]             # (the side's own number, measured from the ground; the base's)
        eps = _APEX * radius                                                           # (the apex is rounded by this much: see _cone_planes)
        out = _own(_cone_planes(slope, axis, offsets, eps))
        _bounded(out, (base[0] - radius, base[1] - radius, base[2]), (base[0] + radius, base[1] + radius, base[2] + height))
        return _parametric(out, offsets, lambda v: _rebuilt_cone(slope, axis, eps, v))
    # (a size or a place that is not a plain number -- a var(), a field -- or a cone of no size: the plane through the rim and the apex, made of the numbers themselves)
    R, H = Shape.wrap(radius), Shape.wrap(height)
    cx, cy, cz = (Shape.wrap(c) for c in base)
    X, Y, Z = Shape.X(), Shape.Y(), Shape.Z()
    rad = ((X - cx).square() + (Y - cy).square() + (R * _APEX).square()).sqrt()
    side = (H * rad + R * (Z - cz) - R * H) / (R.square() + H.square()).sqrt()
    return _own((cz - Z).max(side))

def _pyramid_field(a0, a1, b0, b1, zmin, height):
    ''' The pyramid on the base rectangle (a0, a1) - (b0, b1) from the height zmin up, its tip `height` above the middle of the base: the greatest of the distances to its five planes, the four
        slanted ones the planes through a base edge and the tip (the plane through the edge x = b0 and the tip: (height (x - cx) + dx (z - zmin) - dx height) / sqrt(dx^2 + height^2), cx the
        middle of the base and dx half its width).  Made of the six numbers themselves -- the kernel's pyramid folded its slopes into constants that are no one's number, so a press on
        a face could only move the pyramid as a whole '''
    X, Y, Z = Shape.X(), Shape.Y(), Shape.Z()
    a0, a1, b0, b1, zmin, h = (Shape.wrap(v) for v in (a0, a1, b0, b1, zmin, height))
    cx, cy = (a0 + b0) * 0.5, (a1 + b1) * 0.5
    dx, dy = (b0 - a0) * 0.5, (b1 - a1) * 0.5
    hx, hy = (dx.square() + h.square()).sqrt(), (dy.square() + h.square()).sqrt()
    up_x, up_y = dx * (Z - zmin) - dx * h, dy * (Z - zmin) - dy * h
    return ((zmin - Z).max((h * (X - cx) + up_x) / hx).max((h * (cx - X) + up_x) / hx)
                      .max((h * (Y - cy) + up_y) / hy).max((h * (cy - Y) + up_y) / hy))


def _pyramid_planes(slopes, offsets):
    ''' The pyramid as five planes, each with a number of its own: the base z = zmin and four slanted faces of a FIXED slope -- the face on the +x side is qx x + sx z = c, the one on the -x side
        sx z - qx x = c, and the same in y -- with an offset c each.  A face that is pulled moves along its normal, parallel to itself, and no other face moves (the tip is where they
        meet: it stays over the middle of the base while the four are as they were made, and follows the faces when they are not) '''
    qx, sx, qy, sy = slopes
    cxp, cxm, cyp, cym, zmin = (Shape.wrap(v) for v in offsets)
    X, Y, Z = Shape.X(), Shape.Y(), Shape.Z()
    return ((zmin - Z).max(X * qx + Z * sx - cxp).max(Z * sx - X * qx - cxm)
                      .max(Y * qy + Z * sy - cyp).max(Z * sy - Y * qy - cym))


def _rebuilt_pyramid(slopes, height, v):
    out = _own(_pyramid_planes(slopes, v))
    return _parametric(out, v, lambda w: _rebuilt_pyramid(slopes, height, w))


def pyramid(a, b, zmin, height):
    """ A pyramid defined by its base rectangle (the corners a and b), lower Z value, and height: its tip stands above the middle of the base.  It has five faces -- the base and four
        slanted ones -- and its handles pull one each, parallel to itself (the slopes stay what they were made with)
    """
    if len(a) == 2 and len(b) == 2 and all(_number(v) for v in (a[0], a[1], b[0], b[1], zmin, height)) and b[0] > a[0] and b[1] > a[1] and height > 0:
        dx, dy = (b[0] - a[0]) / 2.0, (b[1] - a[1]) / 2.0
        hx, hy = math.hypot(dx, height), math.hypot(dy, height)
        slopes = (height / hx, dx / hx, height / hy, dy / hy)        # (qx, sx, qy, sy: the unit normals of the slanted faces)
        qx, sx, qy, sy = slopes
        offsets = [qx * b[0] + sx * zmin, sx * zmin - qx * a[0], qy * b[1] + sy * zmin, sy * zmin - qy * a[1], zmin]
        out = _own(_pyramid_planes(slopes, offsets))
        _bounded(out, (a[0], a[1], zmin), (b[0], b[1], zmin + height))
        return _parametric(out, offsets, lambda v: _rebuilt_pyramid(slopes, height, v))
    # (a corner or a height that is not a plain number -- a var(), a field -- or a pyramid of no size: the planes through the edges of the base and the tip, made of the numbers themselves)
    return _own(_pyramid_field(a[0], a[1], b[0], b[1], zmin, height))

stdlib.torus_z.argtypes = [tfloat, tfloat, tvec3]
stdlib.torus_z.restype = libfive_tree
def torus(ro, ri, center=(0, 0, 0)):
    """ A torus lying round the z axis, with the given outer radius, inner radius, and (optional) center
    """
    args = [Shape.wrap(ro), Shape.wrap(ri), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.torus_z(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]])))

stdlib.emptiness.argtypes = []
stdlib.emptiness.restype = libfive_tree
def emptiness():
    """ A value which is empty everywhere
    """
    args = []
    return Shape(stdlib.emptiness(
        ))

# ------------------------------------------------------------------------------------------------------------------------ repeating, extruding

stdlib.array_x.argtypes = [libfive_tree, ctypes.c_int, tfloat]
stdlib.array_x.restype = libfive_tree
def array_x(shape, nx, dx):
    """ Iterates a part in a 1D array
    """
    args = [Shape.wrap(shape), nx, Shape.wrap(dx)]
    return Shape(stdlib.array_x(
        args[0].ptr,
        args[1],
        args[2].ptr))

stdlib.array_xy.argtypes = [libfive_tree, ctypes.c_int, ctypes.c_int, tvec2]
stdlib.array_xy.restype = libfive_tree
def array_xy(shape, nx, ny, delta):
    """ Iterates a part in a 2D array
    """
    args = [Shape.wrap(shape), nx, ny, list([Shape.wrap(i) for i in delta])]
    return Shape(stdlib.array_xy(
        args[0].ptr,
        args[1],
        args[2],
        tvec2(*[a.ptr for a in args[3]])))

stdlib.array_xyz.argtypes = [libfive_tree, ctypes.c_int, ctypes.c_int, ctypes.c_int, tvec3]
stdlib.array_xyz.restype = libfive_tree
def array_xyz(shape, nx, ny, nz, delta):
    """ Iterates a part in a 3D array
    """
    args = [Shape.wrap(shape), nx, ny, nz, list([Shape.wrap(i) for i in delta])]
    return Shape(stdlib.array_xyz(
        args[0].ptr,
        args[1],
        args[2],
        args[3],
        tvec3(*[a.ptr for a in args[4]])))

stdlib.array_polar_z.argtypes = [libfive_tree, ctypes.c_int, tvec2]
stdlib.array_polar_z.restype = libfive_tree
def array_polar_z(shape, n, center=(0, 0)):
    """ Iterates a shape about an optional center position
    """
    args = [Shape.wrap(shape), n, list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.array_polar_z(
        args[0].ptr,
        args[1],
        tvec2(*[a.ptr for a in args[2]])))

array_polar = array_polar_z
