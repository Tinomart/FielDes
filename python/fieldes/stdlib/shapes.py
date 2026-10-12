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
    return out

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
def polygon(r, n, center=(0, 0)):
    """ A polygon with center-to-vertex distance r and n sides
    """
    args = [Shape.wrap(r), n, list([Shape.wrap(i) for i in center])]
    out = Shape(stdlib.polygon(
        args[0].ptr,
        args[1],
        tvec2(*[a.ptr for a in args[2]])))
    if _number(r) and len(center) == 2 and all(_number(c) for c in center):
        _bounded(out, (center[0] - r, center[1] - r, 0), (center[0] + r, center[1] + r, 0))     # (the corners are within r of the centre: a box that holds it)
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


def _parametric(out, params, rebuild, centre):
    ''' What a shape made of a few numbers of its own tells `handles()` and `gizmo()`: `params` the numbers (in the order its handles list them), `rebuild(values)` the same
        shape made of other values (plain numbers or var()s), `centre(values)` the middle of its box as an expression of them.  A rectangle's handles are its four sides, a
        rounded rectangle's the four sides and the radius -- a number each, not the numbers a tree of constants happens to have -- and the gizmo stays at the middle of the
        shape whatever side is pulled '''
    out._handle_params = [_plain(p) for p in params]
    out._handle_rebuild = rebuild
    out._handle_centre = centre
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
    return _parametric(out, [a[0], a[1], b[0], b[1]], lambda v: rectangle((v[0], v[1]), (v[2], v[3])),
                       lambda v: ((v[0] + v[2]) * 0.5, (v[1] + v[3]) * 0.5, 0.0))


def rounded_rectangle(a, b, r):
    """ A rectangle with rounded corners of radius r, between the corners a (the low one) and b (the high one) (a true distance outside it).  The corners are tangent to the
        sides whatever the numbers are: its handles pull a side each (the radius with them)
    """
    out = _own(_rect_distance(a[0], a[1], b[0], b[1], r))
    if len(a) == 2 and len(b) == 2:
        _bounded(out, (a[0], a[1], 0), (b[0], b[1], 0))
    return _parametric(out, [a[0], a[1], b[0], b[1], r], lambda v: rounded_rectangle((v[0], v[1]), (v[2], v[3]), v[4]),
                       lambda v: ((v[0] + v[2]) * 0.5, (v[1] + v[3]) * 0.5, 0.0))

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


def _triangle_centre(normals, offsets):
    ''' The middle of the box of the triangle as an expression of the three offsets: its corners are where two sides meet (the normals are numbers, so each corner is a
        linear combination of two offsets) '''
    xs, ys = [], []
    for i in range(3):
        (ax, ay), (bx, by) = normals[i - 1], normals[i]
        det = ax * by - ay * bx
        # the corner between side i-1 and side i: n_{i-1} . p = c_{i-1}, n_i . p = c_i
        c0, c1 = Shape.wrap(offsets[i - 1]), Shape.wrap(offsets[i])
        xs.append((c0 * by - c1 * ay) * (1.0 / det))
        ys.append((c1 * ax - c0 * bx) * (1.0 / det))
    lo_x, hi_x = xs[0].min(xs[1]).min(xs[2]), xs[0].max(xs[1]).max(xs[2])
    lo_y, hi_y = ys[0].min(ys[1]).min(ys[2]), ys[0].max(ys[1]).max(ys[2])
    return ((lo_x + hi_x) * 0.5, (lo_y + hi_y) * 0.5, 0.0)


def triangle(a, b, c):
    """ A 2D triangle between three corners.  Its handles pull a side each: a side moves parallel to itself, and the gizmo stays at the middle of the triangle
    """
    corners = [tuple(a), tuple(b), tuple(c)]
    if all(len(p) == 2 and all(_number(v) for v in p) for p in corners):
        area = sum(corners[i][0] * corners[(i + 1) % 3][1] - corners[(i + 1) % 3][0] * corners[i][1] for i in range(3))
        if abs(area) > 1e-12:
            sign = 1.0 if area > 0 else -1.0
            normals, offsets = [], []
            for i in range(3):
                (px, py), (qx, qy) = corners[i], corners[(i + 1) % 3]
                ex, ey = qx - px, qy - py
                length = math.hypot(ex, ey)
                if length < 1e-12:
                    break
                nx, ny = sign * ey / length, -sign * ex / length            # (out of the triangle)
                normals.append((nx, ny))
                offsets.append(nx * px + ny * py)
            if len(normals) == 3:
                out = _own(_triangle_planes(normals, offsets))
                _bounded(out, (min(a[0], b[0], c[0]), min(a[1], b[1], c[1]), 0), (max(a[0], b[0], c[0]), max(a[1], b[1], c[1]), 0))
                return _parametric(out, offsets, lambda v: _rebuilt_triangle(normals, v), lambda v: _triangle_centre(normals, v))
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b]), list([Shape.wrap(i) for i in c])]
    out = Shape(stdlib.triangle(
        tvec2(*[a.ptr for a in args[0]]),
        tvec2(*[a.ptr for a in args[1]]),
        tvec2(*[a.ptr for a in args[2]])))
    if all(len(p) == 2 and all(_number(v) for v in p) for p in (a, b, c)):
        _bounded(out, (min(a[0], b[0], c[0]), min(a[1], b[1], c[1]), 0), (max(a[0], b[0], c[0]), max(a[1], b[1], c[1]), 0))
    return out


def _rebuilt_triangle(normals, offsets):
    out = _own(_triangle_planes(normals, offsets))
    return _parametric(out, offsets, lambda v: _rebuilt_triangle(normals, v), lambda v: _triangle_centre(normals, v))

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

stdlib.rounded_box.argtypes = [tvec3, tvec3, tfloat]
stdlib.rounded_box.restype = libfive_tree
def rounded_box(a, b, r):
    """ Rounded box with the given bounds and radius (as a 0-1 fraction)
    """
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
    """ A plane which divides the world into inside and outside, defined by its
        normal and a single point on the plane
    """
    args = [list([Shape.wrap(i) for i in norm]), list([Shape.wrap(i) for i in point])]
    return Shape(stdlib.half_space(
        tvec3(*[a.ptr for a in args[0]]),
        tvec3(*[a.ptr for a in args[1]])))

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

stdlib.cone_z.argtypes = [tfloat, tfloat, tvec3]
stdlib.cone_z.restype = libfive_tree
def cone(radius, height, base=(0, 0, 0)):
    """ A cone with the given base radius and height, standing along z on the (optional) base position
    """
    args = [Shape.wrap(radius), Shape.wrap(height), list([Shape.wrap(i) for i in base])]
    out = Shape(stdlib.cone_z(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]])))
    if _number(radius) and _number(height) and len(base) == 3 and all(_number(c) for c in base):
        _bounded(out, (base[0] - radius, base[1] - radius, base[2]), (base[0] + radius, base[1] + radius, base[2] + height))
    return out

stdlib.pyramid_z.argtypes = [tvec2, tvec2, tfloat, tfloat]
stdlib.pyramid_z.restype = libfive_tree
def pyramid(a, b, zmin, height):
    """ A pyramid defined by its base rectangle, lower Z value, and height
    """
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b]), Shape.wrap(zmin), Shape.wrap(height)]
    out = Shape(stdlib.pyramid_z(
        tvec2(*[a.ptr for a in args[0]]),
        tvec2(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))
    if len(a) == 2 and len(b) == 2 and _number(zmin) and _number(height):
        _bounded(out, (a[0], a[1], zmin), (b[0], b[1], zmin + height))
    return out

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
