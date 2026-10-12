"""
Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.shapes

Every shape has ONE primitive, with that shape and nothing else: `box`, `cylinder`, `sphere`, ... (no centred or mitered or `_z` variants:
a shape that stands another way is turned with rotate_x / rotate_y / rotate_z, one that sits somewhere else is moved with move).
The 3D shapes stand along z, the 2D shapes lie in the plane z = 0.
"""

from fieldes.ffi import libfive_tree, tfloat, tvec2, tvec3, stdlib
from fieldes.shape import Shape

import ctypes

# ---------------------------------------------------------------------------------------------------------------------------------------- 2D

stdlib.circle.argtypes = [tfloat, tvec2]
stdlib.circle.restype = libfive_tree
def circle(r, center=(0, 0)):
    """ A 2D circle with the given radius and optional center
    """
    args = [Shape.wrap(r), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.circle(
        args[0].ptr,
        tvec2(*[a.ptr for a in args[1]])))

stdlib.ring.argtypes = [tfloat, tfloat, tvec2]
stdlib.ring.restype = libfive_tree
def ring(ro, ri, center=(0, 0)):
    """ A 2D ring with the given outer/inner radii and optional center
    """
    args = [Shape.wrap(ro), Shape.wrap(ri), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.ring(
        args[0].ptr,
        args[1].ptr,
        tvec2(*[a.ptr for a in args[2]])))

stdlib.polygon.argtypes = [tfloat, ctypes.c_int, tvec2]
stdlib.polygon.restype = libfive_tree
def polygon(r, n, center=(0, 0)):
    """ A polygon with center-to-vertex distance r and n sides
    """
    args = [Shape.wrap(r), n, list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.polygon(
        args[0].ptr,
        args[1],
        tvec2(*[a.ptr for a in args[2]])))

stdlib.rectangle_exact.argtypes = [tvec2, tvec2]
stdlib.rectangle_exact.restype = libfive_tree
def rectangle(a, b):
    """ A rectangle with the given bounding corners (a true distance outside it: an offset of it is a rounded rectangle)
    """
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b])]
    return Shape(stdlib.rectangle_exact(
        tvec2(*[a.ptr for a in args[0]]),
        tvec2(*[a.ptr for a in args[1]])))

stdlib.rounded_rectangle.argtypes = [tvec2, tvec2, tfloat]
stdlib.rounded_rectangle.restype = libfive_tree
def rounded_rectangle(a, b, r):
    """ A rectangle with rounded corners
    """
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b]), Shape.wrap(r)]
    return Shape(stdlib.rounded_rectangle(
        tvec2(*[a.ptr for a in args[0]]),
        tvec2(*[a.ptr for a in args[1]]),
        args[2].ptr))

stdlib.triangle.argtypes = [tvec2, tvec2, tvec2]
stdlib.triangle.restype = libfive_tree
def triangle(a, b, c):
    """ A 2D triangle
    """
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b]), list([Shape.wrap(i) for i in c])]
    return Shape(stdlib.triangle(
        tvec2(*[a.ptr for a in args[0]]),
        tvec2(*[a.ptr for a in args[1]]),
        tvec2(*[a.ptr for a in args[2]])))

# ---------------------------------------------------------------------------------------------------------------------------------------- 3D

stdlib.box_exact.argtypes = [tvec3, tvec3]
stdlib.box_exact.restype = libfive_tree
def box(a, b):
    """ A box between two opposite corners (a true distance outside it: an offset of it has rounded edges)
    """
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b])]
    return Shape(stdlib.box_exact(
        tvec3(*[a.ptr for a in args[0]]),
        tvec3(*[a.ptr for a in args[1]])))

stdlib.rounded_box.argtypes = [tvec3, tvec3, tfloat]
stdlib.rounded_box.restype = libfive_tree
def rounded_box(a, b, r):
    """ Rounded box with the given bounds and radius (as a 0-1 fraction)
    """
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b]), Shape.wrap(r)]
    return Shape(stdlib.rounded_box(
        tvec3(*[a.ptr for a in args[0]]),
        tvec3(*[a.ptr for a in args[1]]),
        args[2].ptr))

stdlib.sphere.argtypes = [tfloat, tvec3]
stdlib.sphere.restype = libfive_tree
def sphere(radius, center=(0, 0, 0)):
    """ A sphere with the given radius and (optional) center
    """
    args = [Shape.wrap(radius), list([Shape.wrap(i) for i in center])]
    return Shape(stdlib.sphere(
        args[0].ptr,
        tvec3(*[a.ptr for a in args[1]])))

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
    return Shape(stdlib.cylinder_z(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]])))

stdlib.cone_z.argtypes = [tfloat, tfloat, tvec3]
stdlib.cone_z.restype = libfive_tree
def cone(radius, height, base=(0, 0, 0)):
    """ A cone with the given base radius and height, standing along z on the (optional) base position
    """
    args = [Shape.wrap(radius), Shape.wrap(height), list([Shape.wrap(i) for i in base])]
    return Shape(stdlib.cone_z(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]])))

stdlib.pyramid_z.argtypes = [tvec2, tvec2, tfloat, tfloat]
stdlib.pyramid_z.restype = libfive_tree
def pyramid(a, b, zmin, height):
    """ A pyramid defined by its base rectangle, lower Z value, and height
    """
    args = [list([Shape.wrap(i) for i in a]), list([Shape.wrap(i) for i in b]), Shape.wrap(zmin), Shape.wrap(height)]
    return Shape(stdlib.pyramid_z(
        tvec2(*[a.ptr for a in args[0]]),
        tvec2(*[a.ptr for a in args[1]]),
        args[2].ptr,
        args[3].ptr))

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
