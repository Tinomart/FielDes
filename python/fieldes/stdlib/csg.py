"""
Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.csg
"""

from fieldes.ffi import libfive_tree, tfloat, tvec2, tvec3, stdlib
from fieldes.shape import Shape

import ctypes

stdlib._union.argtypes = [libfive_tree, libfive_tree]
stdlib._union.restype = libfive_tree
def union(a, b):
    """ Returns the union of two shapes
    """
    args = [Shape.wrap(a), Shape.wrap(b)]
    return Shape(stdlib._union(
        args[0].ptr,
        args[1].ptr))

stdlib.intersection.argtypes = [libfive_tree, libfive_tree]
stdlib.intersection.restype = libfive_tree
def intersection(a, b):
    """ Returns the intersection of two shapes
    """
    args = [Shape.wrap(a), Shape.wrap(b)]
    return Shape(stdlib.intersection(
        args[0].ptr,
        args[1].ptr))

stdlib.inverse.argtypes = [libfive_tree]
stdlib.inverse.restype = libfive_tree
def inverse(a):
    """ Returns a shape that's the inverse of the input shape
    """
    args = [Shape.wrap(a)]
    return Shape(stdlib.inverse(
        args[0].ptr))

stdlib.difference.argtypes = [libfive_tree, libfive_tree]
stdlib.difference.restype = libfive_tree
def difference(a, b):
    """ Subtracts the second shape from the first
    """
    args = [Shape.wrap(a), Shape.wrap(b)]
    return Shape(stdlib.difference(
        args[0].ptr,
        args[1].ptr))

stdlib.offset.argtypes = [libfive_tree, tfloat]
stdlib.offset.restype = libfive_tree
def offset(a, o):
    """ Expand or contract a given shape by an offset
        Positive offsets expand the shape; negative offsets shrink it
    """
    from fieldes.stdlib.fields import _dist      # (an imported part: its exact distance)
    args = [_dist(a), Shape.wrap(o)]
    return Shape(stdlib.offset(
        args[0].ptr,
        args[1].ptr))

stdlib.clearance.argtypes = [libfive_tree, libfive_tree, tfloat]
stdlib.clearance.restype = libfive_tree
def clearance(a, b, offset):
    """ Expands shape b by the given offset then subtracts it from shape a
    """
    args = [Shape.wrap(a), Shape.wrap(b), Shape.wrap(offset)]
    return Shape(stdlib.clearance(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr))

stdlib.shell.argtypes = [libfive_tree, tfloat]
stdlib.shell.restype = libfive_tree
def shell(a, offset):
    """ Returns a shell of a shape with the given offset
    """
    args = [Shape.wrap(a), Shape.wrap(offset)]
    return Shape(stdlib.shell(
        args[0].ptr,
        args[1].ptr))

stdlib.blend_expt.argtypes = [libfive_tree, libfive_tree, tfloat]
stdlib.blend_expt.restype = libfive_tree
def blend_expt(a, b, m):
    """ Blends two shapes by the given amount using exponents
    """
    args = [Shape.wrap(a), Shape.wrap(b), Shape.wrap(m)]
    return Shape(stdlib.blend_expt(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr))

stdlib.blend_expt_unit.argtypes = [libfive_tree, libfive_tree, tfloat]
stdlib.blend_expt_unit.restype = libfive_tree
def blend_expt_unit(a, b, m):
    """ Blends two shapes by the given amount using exponents,
        with the blend term adjusted to produce results approximately
        resembling blend_rough for values between 0 and 1.
    """
    args = [Shape.wrap(a), Shape.wrap(b), Shape.wrap(m)]
    return Shape(stdlib.blend_expt_unit(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr))

stdlib.blend_rough.argtypes = [libfive_tree, libfive_tree, tfloat]
stdlib.blend_rough.restype = libfive_tree
def blend_rough(a, b, m):
    """ Blends two shapes by the given amount, using a fast-but-rough
        CSG approximation that may not preserve gradients
    """
    args = [Shape.wrap(a), Shape.wrap(b), Shape.wrap(m)]
    return Shape(stdlib.blend_rough(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr))

stdlib.blend_difference.argtypes = [libfive_tree, libfive_tree, tfloat, tfloat]
stdlib.blend_difference.restype = libfive_tree
def blend_difference(a, b, m, o=0):
    """ Blends the subtraction of b, with optional offset o,
        from a, with smoothness m
    """
    args = [Shape.wrap(a), Shape.wrap(b), Shape.wrap(m), Shape.wrap(o)]
    return Shape(stdlib.blend_difference(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr,
        args[3].ptr))

stdlib.morph.argtypes = [libfive_tree, libfive_tree, tfloat]
stdlib.morph.restype = libfive_tree
def morph(a, b, m):
    """ Morphs between two shapes.
        m = 0 produces a, m = 1 produces b
    """
    args = [Shape.wrap(a), Shape.wrap(b), Shape.wrap(m)]
    return Shape(stdlib.morph(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr))

stdlib.loft.argtypes = [libfive_tree, libfive_tree, tfloat, tfloat]
stdlib.loft.restype = libfive_tree
def loft(a, b, zmin, zmax):
    """ Produces a blended loft between a (at zmin) and b (at zmax)
        a and b should be 2D shapes (i.e. invariant along the z axis)
    """
    args = [Shape.wrap(a), Shape.wrap(b), Shape.wrap(zmin), Shape.wrap(zmax)]
    return Shape(stdlib.loft(
        args[0].ptr,
        args[1].ptr,
        args[2].ptr,
        args[3].ptr))

stdlib.loft_between.argtypes = [libfive_tree, libfive_tree, tvec3, tvec3]
stdlib.loft_between.restype = libfive_tree
def loft_between(a, b, lower, upper):
    """ Produces a blended loft between a (at lower.z) and b (at upper.z),
        with XY coordinates remapped to slide between lower.xy and upper.xy.
        a and b should be 2D shapes (i.e. invariant along the z axis)
    """
    args = [Shape.wrap(a), Shape.wrap(b), list([Shape.wrap(i) for i in lower]), list([Shape.wrap(i) for i in upper])]
    return Shape(stdlib.loft_between(
        args[0].ptr,
        args[1].ptr,
        tvec3(*[a.ptr for a in args[2]]),
        tvec3(*[a.ptr for a in args[3]])))

blend = blend_expt_unit
################################################################################
# Hand-written functions which allow for arbitrary numbers of arguments
import functools

_prev_union = union


def _round_union(a, b, r):
    """ The union of two fields with a circular blend of radius r where their surfaces meet (they are about
        distances: negative inside).  Far from the meeting it is the plain union. """
    ua = (Shape.wrap(r) - a).max(0)
    ub = (Shape.wrap(r) - b).max(0)
    return a.min(b).max(r) - (ua.square() + ub.square()).sqrt()


def _boxed(out, box, inputs):
    """ `out` is held by `box` -- a box that holds it, not its exact extent (what is cut away, or blended, is not taken off it).  The render region is made
        to hold the models by these boxes, and a box that is known need not be searched for (a search does not come to an end for every shape) """
    if box is not None and not any(out is s for s in inputs):
        out._bounds = (tuple(float(v) for v in box[0]), tuple(float(v) for v in box[1]))
        out._bounds_loose = True
    return out


def _union_box(shapes, pad=0.0):
    boxes = [getattr(s, '_bounds', None) for s in shapes]
    if not boxes or not all(boxes):
        return None
    return (tuple(min(float(b[0][k]) for b in boxes) - pad for k in range(3)), tuple(max(float(b[1][k]) for b in boxes) + pad for k in range(3)))


def union(a, *args, radius=0):
    """ The union of any number of shapes.  radius (mm, default 0: a sharp union) blends the surfaces where they meet
        with a smooth transition of that radius, so parts that do not quite fit together are joined by a fillet --
        a lattice and the body it is added to, a rib and a plate.  It works on any field, lattices included.
        (A radius that is a field makes a blend that varies.) """
    if radius is not None and not (isinstance(radius, (int, float)) and radius == 0):
        a = Shape.wrap(a)
        out = a
        for b in args:
            out = _round_union(out, Shape.wrap(b), radius)
        pad = abs(float(radius)) if isinstance(radius, (int, float)) else None
        return _boxed(out, _union_box((a,) + args, pad) if pad is not None else None, (a,) + args)
    return _boxed(functools.reduce(_prev_union, args, a), _union_box((a,) + args), (a,) + args)

_prev_intersection = intersection
def intersection(a, *args):
    out = functools.reduce(_prev_intersection, args, a)
    # (what is common to shapes is in the box of every one of them that has one: a difference is the first one's box, as the other is turned inside out)
    boxes = [b for b in (getattr(s, '_bounds', None) for s in (a,) + args) if b]
    if boxes:
        lo = tuple(max(float(b[0][k]) for b in boxes) for k in range(3))
        hi = tuple(min(float(b[1][k]) for b in boxes) for k in range(3))
        if all(lo[k] <= hi[k] for k in range(3)):
            _boxed(out, (lo, hi), (a,) + args)
    return out
intersection.__doc__ = _prev_intersection.__doc__

def difference(a, b, *rest):
    """ Subtracts any number of shapes from the first argument
    """
    return intersection(a, inverse(union(b, *rest)))
