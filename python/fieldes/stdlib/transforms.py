"""
Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.transforms
"""

from fieldes.ffi import libfive_tree, tfloat, tvec2, tvec3, stdlib
from fieldes.shape import Shape

import ctypes

stdlib.move.argtypes = [libfive_tree, tvec3]
stdlib.move.restype = libfive_tree
def move(t, offset):
    """ Moves the given shape in 2D or 3D space
    """
    args = [Shape.wrap(t), list([Shape.wrap(i) for i in offset])]
    return Shape(stdlib.move(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]])))

stdlib.reflect_x.argtypes = [libfive_tree, tfloat]
stdlib.reflect_x.restype = libfive_tree
def reflect_x(t, x0=0):
    """ Reflects a shape about the x origin or an optional offset
    """
    args = [Shape.wrap(t), Shape.wrap(x0)]
    return Shape(stdlib.reflect_x(
        args[0].ptr,
        args[1].ptr))

stdlib.reflect_y.argtypes = [libfive_tree, tfloat]
stdlib.reflect_y.restype = libfive_tree
def reflect_y(t, y0=0):
    """ Reflects a shape about the y origin or an optional offset
    """
    args = [Shape.wrap(t), Shape.wrap(y0)]
    return Shape(stdlib.reflect_y(
        args[0].ptr,
        args[1].ptr))

stdlib.reflect_z.argtypes = [libfive_tree, tfloat]
stdlib.reflect_z.restype = libfive_tree
def reflect_z(t, z0=0):
    """ Reflects a shape about the z origin or an optional offset
    """
    args = [Shape.wrap(t), Shape.wrap(z0)]
    return Shape(stdlib.reflect_z(
        args[0].ptr,
        args[1].ptr))

stdlib.reflect_xy.argtypes = [libfive_tree]
stdlib.reflect_xy.restype = libfive_tree
def reflect_xy(t):
    """ Reflects a shape about the plane X=Y
    """
    args = [Shape.wrap(t)]
    return Shape(stdlib.reflect_xy(
        args[0].ptr))

stdlib.reflect_yz.argtypes = [libfive_tree]
stdlib.reflect_yz.restype = libfive_tree
def reflect_yz(t):
    """ Reflects a shape about the plane Y=Z
    """
    args = [Shape.wrap(t)]
    return Shape(stdlib.reflect_yz(
        args[0].ptr))

stdlib.reflect_xz.argtypes = [libfive_tree]
stdlib.reflect_xz.restype = libfive_tree
def reflect_xz(t):
    """ Reflects a shape about the plane X=Z
    """
    args = [Shape.wrap(t)]
    return Shape(stdlib.reflect_xz(
        args[0].ptr))

stdlib.symmetric_x.argtypes = [libfive_tree]
stdlib.symmetric_x.restype = libfive_tree
def symmetric_x(t):
    """ Clips the given shape at the x origin, then duplicates the remaining
        shape reflected on the other side of the origin
    """
    args = [Shape.wrap(t)]
    return Shape(stdlib.symmetric_x(
        args[0].ptr))

stdlib.symmetric_y.argtypes = [libfive_tree]
stdlib.symmetric_y.restype = libfive_tree
def symmetric_y(t):
    """ Clips the given shape at the y origin, then duplicates the remaining
        shape reflected on the other side of the origin
    """
    args = [Shape.wrap(t)]
    return Shape(stdlib.symmetric_y(
        args[0].ptr))

stdlib.symmetric_z.argtypes = [libfive_tree]
stdlib.symmetric_z.restype = libfive_tree
def symmetric_z(t):
    """ Clips the given shape at the z origin, then duplicates the remaining
        shape reflected on the other side of the origin
    """
    args = [Shape.wrap(t)]
    return Shape(stdlib.symmetric_z(
        args[0].ptr))

stdlib.scale_x.argtypes = [libfive_tree, tfloat, tfloat]
stdlib.scale_x.restype = libfive_tree
def scale_x(t, sx, x0=0):
    """ Scales a shape by sx on the x axis about 0 or an optional offset
    """
    args = [Shape.wrap(t), Shape.wrap(sx), Shape.wrap(x0)]
    return Shape(stdlib.scale_x(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr))

stdlib.scale_y.argtypes = [libfive_tree, tfloat, tfloat]
stdlib.scale_y.restype = libfive_tree
def scale_y(t, sy, y0=0):
    """ Scales a shape by sx on the x axis about 0 or an optional offset
    """
    args = [Shape.wrap(t), Shape.wrap(sy), Shape.wrap(y0)]
    return Shape(stdlib.scale_y(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr))

stdlib.scale_z.argtypes = [libfive_tree, tfloat, tfloat]
stdlib.scale_z.restype = libfive_tree
def scale_z(t, sz, z0=0):
    """ Scales a shape by sx on the x axis about 0 or an optional offset
    """
    args = [Shape.wrap(t), Shape.wrap(sz), Shape.wrap(z0)]
    return Shape(stdlib.scale_z(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr))

stdlib.scale_xyz.argtypes = [libfive_tree, tvec3, tvec3]
stdlib.scale_xyz.restype = libfive_tree
def scale_xyz(t, s, center=(0, 0, 0)):
    """ Scales a shape on all three axes, about 0 or an optional offset
    """
    args = [Shape.wrap(t), list([Shape.wrap(i) for i in s]), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.scale_xyz(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        tvec3(*[a.ptr for a in args[2]])))

stdlib.rotate_x.argtypes = [libfive_tree, tfloat, tvec3]
stdlib.rotate_x.restype = libfive_tree
def rotate_x(t, angle, center=(0, 0, 0)):
    """ Rotate the given shape by an angle in radians
        The center of rotation is [0 0 0] or specified by the optional argument
    """
    args = [Shape.wrap(t), Shape.wrap(angle), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.rotate_x(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]])))

stdlib.rotate_y.argtypes = [libfive_tree, tfloat, tvec3]
stdlib.rotate_y.restype = libfive_tree
def rotate_y(t, angle, center=(0, 0, 0)):
    """ Rotate the given shape by an angle in radians
        The center of rotation is [0 0 0] or specified by the optional argument
    """
    args = [Shape.wrap(t), Shape.wrap(angle), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.rotate_y(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]])))

stdlib.rotate_z.argtypes = [libfive_tree, tfloat, tvec3]
stdlib.rotate_z.restype = libfive_tree
def rotate_z(t, angle, center=(0, 0, 0)):
    """ Rotate the given shape by an angle in radians
        The center of rotation is [0 0 0] or specified by the optional argument
    """
    args = [Shape.wrap(t), Shape.wrap(angle), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.rotate_z(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]])))

stdlib.taper_x_y.argtypes = [libfive_tree, tvec2, tfloat, tfloat, tfloat]
stdlib.taper_x_y.restype = libfive_tree
def taper_x_y(shape, base, h, scale, base_scale=1):
    """ Tapers a shape along the x axis as a function of y
        width = base-scale at base
        width = scale at base + [0 h]
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in base]), Shape.wrap(h), Shape.wrap(scale), Shape.wrap(base_scale)]
    return Shape(stdlib.taper_x_y(
        args[0].ptr,
        tvec2(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr,
        args[4].ptr))

stdlib.taper_xy_z.argtypes = [libfive_tree, tvec3, tfloat, tfloat, tfloat]
stdlib.taper_xy_z.restype = libfive_tree
def taper_xy_z(shape, base, height, scale, base_scale=1):
    """ Tapers a shape in the xy plane as a function of z
        width = base-scale at base
        width = scale at base + [0 0 height]
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in base]), Shape.wrap(height), Shape.wrap(scale), Shape.wrap(base_scale)]
    return Shape(stdlib.taper_xy_z(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr,
        args[4].ptr))

stdlib.shear_x_y.argtypes = [libfive_tree, tvec2, tfloat, tfloat, tfloat]
stdlib.shear_x_y.restype = libfive_tree
def shear_x_y(t, base, height, offset, base_offset=0):
    """ Shears a shape on the x axis as a function of y
        offset = base-offset at base.y
        offset = offset = base.y + h
    """
    args = [Shape.wrap(t), list([Shape.wrap(i) for i in base]), Shape.wrap(height), Shape.wrap(offset), Shape.wrap(base_offset)]
    return Shape(stdlib.shear_x_y(
        args[0].ptr,
        tvec2(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr,
        args[4].ptr))

stdlib.repel.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.repel.restype = libfive_tree
def repel(shape, locus, radius, exaggerate=1):
    """ Repels the shape away from a point based upon a radius r,
        with optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.repel(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.repel_x.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.repel_x.restype = libfive_tree
def repel_x(shape, locus, radius, exaggerate=1):
    """ Repels the shape away from a YZ plane based upon a radius r,
        with optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.repel_x(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.repel_y.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.repel_y.restype = libfive_tree
def repel_y(shape, locus, radius, exaggerate=1):
    """ Repels the shape away from a XZ plane based upon a radius r,
        with optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.repel_y(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.repel_z.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.repel_z.restype = libfive_tree
def repel_z(shape, locus, radius, exaggerate=1):
    """ Repels the shape away from a XY plane based upon a radius r,
        with optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.repel_z(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.repel_xy.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.repel_xy.restype = libfive_tree
def repel_xy(shape, locus, radius, exaggerate=1):
    """ Repels the shape away from line parallel to the Z axis,
        with a particular radius and optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.repel_xy(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.repel_yz.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.repel_yz.restype = libfive_tree
def repel_yz(shape, locus, radius, exaggerate=1):
    """ Repels the shape away from line parallel to the X axis,
        with a particular radius and optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.repel_yz(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.repel_xz.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.repel_xz.restype = libfive_tree
def repel_xz(shape, locus, radius, exaggerate=1):
    """ Repels the shape away from line parallel to the Y axis,
        with a particular radius and optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.repel_xz(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.attract.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.attract.restype = libfive_tree
def attract(shape, locus, radius, exaggerate=1):
    """ Attracts the shape away from a point based upon a radius r,
        with optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.attract(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.attract_x.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.attract_x.restype = libfive_tree
def attract_x(shape, locus, radius, exaggerate=1):
    """ Attracts the shape away from a YZ plane based upon a radius r,
        with optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.attract_x(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.attract_y.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.attract_y.restype = libfive_tree
def attract_y(shape, locus, radius, exaggerate=1):
    """ Attracts the shape away from a XZ plane based upon a radius r,
        with optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.attract_y(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.attract_z.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.attract_z.restype = libfive_tree
def attract_z(shape, locus, radius, exaggerate=1):
    """ Attracts the shape away from a XY plane based upon a radius r,
        with optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.attract_z(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.attract_xy.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.attract_xy.restype = libfive_tree
def attract_xy(shape, locus, radius, exaggerate=1):
    """ Attracts the shape away from line parallel to the Z axis,
        with a particular radius and optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.attract_xy(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.attract_yz.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.attract_yz.restype = libfive_tree
def attract_yz(shape, locus, radius, exaggerate=1):
    """ Attracts the shape away from line parallel to the X axis,
        with a particular radius and optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.attract_yz(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.attract_xz.argtypes = [libfive_tree, tvec3, tfloat, tfloat]
stdlib.attract_xz.restype = libfive_tree
def attract_xz(shape, locus, radius, exaggerate=1):
    """ Attracts the shape away from line parallel to the Y axis,
        with a particular radius and optional exaggeration
    """
    args = [Shape.wrap(shape), list([Shape.wrap(i) for i in locus]), Shape.wrap(radius), Shape.wrap(exaggerate)]
    return Shape(stdlib.attract_xz(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

stdlib.revolve_y.argtypes = [libfive_tree, tfloat]
stdlib.revolve_y.restype = libfive_tree
def revolve_y(shape, x0=0):
    """ Revolves a 2D (XY) shape about a line parallel to the Y axis with the
        given x value
    """
    args = [Shape.wrap(shape), Shape.wrap(x0)]
    return Shape(stdlib.revolve_y(
        args[0].ptr,
        args[1].ptr))

stdlib.twirl_x.argtypes = [libfive_tree, tfloat, tfloat, tvec3]
stdlib.twirl_x.restype = libfive_tree
def twirl_x(shape, amount, radius, center=(0, 0, 0)):
    """ Twirls the shape in the x axis about the (optional) center point
    """
    args = [Shape.wrap(shape), Shape.wrap(amount), Shape.wrap(radius), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.twirl_x(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr,
        tvec3(*[a.ptr for a in args[3]])))

stdlib.twirl_axis_x.argtypes = [libfive_tree, tfloat, tfloat, tvec3]
stdlib.twirl_axis_x.restype = libfive_tree
def twirl_axis_x(shape, amount, radius, center=(0, 0, 0)):
    """ Twirls the shape in the x axis about the line extending from the
        (optional) center point
    """
    args = [Shape.wrap(shape), Shape.wrap(amount), Shape.wrap(radius), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.twirl_axis_x(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr,
        tvec3(*[a.ptr for a in args[3]])))

stdlib.twirl_y.argtypes = [libfive_tree, tfloat, tfloat, tvec3]
stdlib.twirl_y.restype = libfive_tree
def twirl_y(shape, amount, radius, center=(0, 0, 0)):
    """ Twirls the shape in the y axis about the (optional) center point
    """
    args = [Shape.wrap(shape), Shape.wrap(amount), Shape.wrap(radius), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.twirl_y(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr,
        tvec3(*[a.ptr for a in args[3]])))

stdlib.twirl_axis_y.argtypes = [libfive_tree, tfloat, tfloat, tvec3]
stdlib.twirl_axis_y.restype = libfive_tree
def twirl_axis_y(shape, amount, radius, center=(0, 0, 0)):
    """ Twirls the shape in the y axis about the line extending from the
        (optional) center point
    """
    args = [Shape.wrap(shape), Shape.wrap(amount), Shape.wrap(radius), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.twirl_axis_y(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr,
        tvec3(*[a.ptr for a in args[3]])))

stdlib.twirl_z.argtypes = [libfive_tree, tfloat, tfloat, tvec3]
stdlib.twirl_z.restype = libfive_tree
def twirl_z(shape, amount, radius, center=(0, 0, 0)):
    """ Twirls the shape in the z axis about the (optional) center point
    """
    args = [Shape.wrap(shape), Shape.wrap(amount), Shape.wrap(radius), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.twirl_z(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr,
        tvec3(*[a.ptr for a in args[3]])))

stdlib.twirl_axis_z.argtypes = [libfive_tree, tfloat, tfloat, tvec3]
stdlib.twirl_axis_z.restype = libfive_tree
def twirl_axis_z(shape, amount, radius, center=(0, 0, 0)):
    """ Twirls the shape in the z axis about the line extending from the
        (optional) center point
    """
    args = [Shape.wrap(shape), Shape.wrap(amount), Shape.wrap(radius), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.twirl_axis_z(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr,
        tvec3(*[a.ptr for a in args[3]])))

rotate = rotate_z
################################################################################
# Hand-written override to let move work in 2D
_move_prev = move
def move(shape, v):
    if len(v) == 2:
        v = [v[0], v[1], 0]
    return _move_prev(shape, v)
move.__doc__ = _move_prev.__doc__

################################################################################
# Hand-written: an imported STEP part's exact-geometry information (see
# fieldes.stdlib.cad_import.exclude) follows it through the rigid and
# scaling transforms, as the 4x4 matrix each one applies
def _exact_follows(name, matrix):
    prev = globals()[name]
    def f(t, *args, **kwargs):
        out = prev(t, *args, **kwargs)
        if isinstance(t, Shape) and (hasattr(t, '_exact') or hasattr(t, '_exact_source') or
                                     hasattr(t, '_exact_sources')):
            from fieldes.stdlib import cad_import
            cad_import._carry_exact(out, t, matrix(cad_import, *args, **kwargs))
        return out
    f.__doc__ = prev.__doc__
    f.__name__ = name
    globals()[name] = f


def _cos(v):
    import math
    return math.cos(v) if isinstance(v, (int, float)) else Shape.wrap(v).cos()


def _sin(v):
    import math
    return math.sin(v) if isinstance(v, (int, float)) else Shape.wrap(v).sin()


def _m_move(ci, v):
    return ci._translation([v[0], v[1], v[2] if len(v) > 2 else 0.0])


def _m_rotate(axis):
    def m(ci, angle, center=(0, 0, 0)):
        c, s = _cos(angle), _sin(angle)
        r = ci._identity()
        i, j = [(1, 2), (0, 2), (0, 1)][axis]   # rotation in the (i, j) plane
        r[i][i], r[i][j], r[j][i], r[j][j] = c, ci._neg(s), s, c
        return ci._about(center, r)
    return m


def _m_scale(axis):
    def m(ci, s, origin=0):
        r = ci._identity()
        r[axis][axis] = s
        center = [0.0, 0.0, 0.0]
        center[axis] = origin
        return ci._about(center, r)
    return m


def _m_scale_xyz(ci, s, center=(0, 0, 0)):
    r = ci._identity()
    for a in range(3):
        r[a][a] = s[a]
    return ci._about(center, r)


def _m_reflect(axis):
    def m(ci, origin=0):
        r = ci._identity()
        r[axis][axis] = -1.0
        center = [0.0, 0.0, 0.0]
        center[axis] = origin
        return ci._about(center, r)
    return m


def _m_swap(i, j):
    def m(ci):
        r = ci._identity()
        r[i][i] = r[j][j] = 0.0
        r[i][j] = r[j][i] = 1.0
        return r
    return m


for _name, _matrix in [('move', _m_move),
                       ('rotate_x', _m_rotate(0)), ('rotate_y', _m_rotate(1)),
                       ('rotate_z', _m_rotate(2)),
                       ('scale_x', _m_scale(0)), ('scale_y', _m_scale(1)),
                       ('scale_z', _m_scale(2)), ('scale_xyz', _m_scale_xyz),
                       ('reflect_x', _m_reflect(0)), ('reflect_y', _m_reflect(1)),
                       ('reflect_z', _m_reflect(2)),
                       ('reflect_xy', _m_swap(0, 1)), ('reflect_yz', _m_swap(1, 2)),
                       ('reflect_xz', _m_swap(0, 2))]:
    _exact_follows(_name, _matrix)
rotate = rotate_z
