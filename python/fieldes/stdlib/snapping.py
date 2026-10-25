'''
Snapping one body onto another: the face and the point you chose on each.

    from fieldes import *

    foot = select_surface(bracket, seed=(0, 0, 0), angle=5)            # a face of the bracket
    foot_corner = corner_at(bracket, (10, 10, 0))                       # and a corner of it
    top = select_surface(plate, seed=(30, 20, 12), angle=5)            # a face of the plate
    hole_edge = edge_point_at(plate, (30, 20, 12))                     # and a point of an edge of it

    bracket = snap(bracket, foot, foot_corner, top, hole_edge)          # displayed: the bracket, standing on the plate with its corner on that point

`snap(body, face, point, onto_face, onto_point)` moves `body` so that its `face` lies against `onto_face` -- the two faces facing each other, which is the direction in which the
bodies do not go into one another -- and its `point` is at `onto_point`.  The faces are surfaces picked with `select_surface` (a flat face, or a gently curved one: it is
the way the face faces on the average that counts), or just a direction: a vector `(0, 0, 1)` is the way a face of the other body faces.  The points are points: `point(...)`,
a corner or an edge point from `corner_at` / `edge_point_at`, a place for a gizmo.  The other body is not given: where it is and which way it faces is all that is read of it,
so it can be anything -- a body, a surface, a plane of the scene.

    spin    degrees: the body turned about the line the two faces touch along (its normal), after it is in place -- the one thing the two faces and the two points leave free
    gap     mm: the faces are left this far apart (a clearance; negative: pressed into one another)

In FielDes, select the body, then its face and its point, then the face and the point it goes onto (Ctrl + click, in this order) and choose **Operations > Moving > snap**: the call
is written with them in their places.  What is not selected is a placeholder in the call.

The body is turned by the least turn that makes its face face away from the other one, about the point it is placed by, and then moved: so with the faces already facing each other
it is only moved.  What comes back is the moved body, a shape like any other.  It is made with `move` and `rotate_*`, so a body that has an excluded part (see `exclude`) is moved
as a whole, and a gizmo or `handles` of the body is left in the script after the snap, not before it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import numbers
import sys

from fieldes.shape import Shape

__all__ = ['snap']

_SENSE = {}             # (whether rotate_<axis> turns a shape counter-clockwise for a positive angle: asked of the library once)


def _library():
    ''' The functions of the package as a script sees them: the ones that carry an excluded part with a shape (see fieldes.stdlib.excluded) are the ones to move a body with '''
    return sys.modules['fieldes.stdlib']


def _sense(axis):
    ''' +1 when rotate_<axis>(shape, angle) turns the shape counter-clockwise for a positive angle (the right-handed turn), -1 when it turns it the other way: found by turning a
        plane a quarter turn and asking where it went, so that no convention is assumed '''
    if axis not in _SENSE:
        turned = getattr(_library(), 'rotate_' + axis)(Shape.X() - 1.0, math.pi / 2.0)          # (the plane x = 1)
        # a counter-clockwise quarter turn takes the point (1, 0, 0) to (0, 1, 0) about z, and to (0, 0, -1) about y
        where = (0.0, 1.0, 0.0) if axis == 'z' else (0.0, 0.0, -1.0)
        _SENSE[axis] = 1.0 if abs(float(turned(*where))) < 1e-3 else -1.0
    return _SENSE[axis]


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(a, what):
    n = math.sqrt(_dot(a, a))
    if not n > 1e-12:
        raise ValueError('snap: %s has no direction (its normals add up to nothing)' % what)
    return (a[0] / n, a[1] / n, a[2] / n)


def _axis_angle(k, angle):
    ''' The matrix of a turn of `angle` radians about the unit vector k (Rodrigues) '''
    c, s = math.cos(angle), math.sin(angle)
    t = 1.0 - c
    x, y, z = k
    return [[c + x * x * t, x * y * t - z * s, x * z * t + y * s],
            [y * x * t + z * s, c + y * y * t, y * z * t - x * s],
            [z * x * t - y * s, z * y * t + x * s, c + z * z * t]]


def _taking(a, b):
    ''' The matrix of the least turn that takes the unit vector a to the unit vector b '''
    k = _cross(a, b)
    s = math.sqrt(_dot(k, k))
    c = _dot(a, b)
    if s < 1e-9:
        if c > 0:
            return _axis_angle((0.0, 0.0, 1.0), 0.0)
        # (opposite: any turn of half a circle about an axis across a)
        across = min(((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), key=lambda e: abs(_dot(e, a)))
        return _axis_angle(_unit(_cross(a, across), 'an axis'), math.pi)
    return _axis_angle((k[0] / s, k[1] / s, k[2] / s), math.atan2(s, c))


def _rotated(shape, matrix, center):
    ''' The shape turned by the matrix about `center`, with the library's turns about z, y and z: R = Rz(a) Ry(b) Rz(c) is done as c first '''
    r = matrix
    b = math.atan2(math.hypot(r[0][2], r[1][2]), r[2][2])
    if math.sin(b) > 1e-9:
        a, c = math.atan2(r[1][2], r[0][2]), math.atan2(r[2][1], -r[2][0])
    elif r[2][2] > 0:
        a, c = math.atan2(r[1][0], r[0][0]), 0.0
    else:
        a, c = math.atan2(-r[1][0], -r[0][0]), 0.0
    out = shape
    for axis, angle in (('z', c), ('y', b), ('z', a)):
        if abs(angle) > 1e-12:
            out = getattr(_library(), 'rotate_' + axis)(out, _sense(axis) * angle, tuple(center))
    return out


def _place(point, what):
    ''' Where a point is, as three numbers: a `point`, a tuple, or anything with a position '''
    if isinstance(point, (tuple, list)) and len(point) == 3 and all(isinstance(c, numbers.Number) for c in point):
        return tuple(float(c) for c in point)
    here = getattr(point, '_point_pos', None)
    if here is not None:
        return tuple(float(c) for c in here)
    xyz = getattr(point, 'xyz', None)
    if xyz is not None:
        from fieldes.stdlib.points import _position_of
        here = _position_of(xyz)
        if here is not None:
            return here
    raise ValueError('snap: %s is a place: a point(...), or (x, y, z) -- a corner_at(...) or an edge_point_at(...) is one' % what)


def _normal(face, at, what):
    ''' The way a face faces, as a unit vector: a direction (x, y, z) as it is; a selected surface by the average of the normals its samples have (they point out of the body
        it was picked on); any other surface or field by its gradient at `at` '''
    if isinstance(face, (tuple, list)) and len(face) == 3 and all(isinstance(c, numbers.Number) for c in face):
        return _unit(tuple(float(c) for c in face), what)
    n = getattr(face, 'sample_normals', None)
    if n:
        return _unit((sum(n[0::3]), sum(n[1::3]), sum(n[2::3])), what)
    field = face if isinstance(face, Shape) else None
    body = getattr(face, 'shape', None)
    if field is None and isinstance(body, Shape):
        field = body
    if field is None:
        raise ValueError('snap: %s is a face: a surface from select_surface(...), a surface or a plane, or the direction it faces (x, y, z)' % what)
    h = 1e-2
    g = []
    for axis in range(3):
        lo, hi = list(at), list(at)
        lo[axis] -= h
        hi[axis] += h
        g.append((float(field(*hi)) - float(field(*lo))) / (2.0 * h))
    return _unit(tuple(g), what)


def snap(body, face, point, onto_face, onto_point, spin=0.0, gap=0.0):
    ''' `body` moved so that its `face` lies against `onto_face` and its `point` is at `onto_point` (see the module): the least turn that makes the two faces face each other, then
        a move.

        face, onto_face     the faces: surfaces from select_surface(...), or the directions they face (x, y, z)
        point, onto_point   the places: point(...), corner_at(...), edge_point_at(...), or (x, y, z)
        spin                degrees: the body turned about the normal of the contact, once it is in place (default 0)
        gap                 mm: the faces are left this far apart (default 0) '''
    own, there = _place(point, 'point'), _place(onto_point, 'onto_point')
    a = _normal(face, own, 'face')
    b = _normal(onto_face, there, 'onto_face')
    library = _library()
    turned = _rotated(body, _taking(a, (-b[0], -b[1], -b[2])), own)
    at = tuple(there[i] + float(gap) * b[i] for i in range(3))
    moved = library.move(turned, tuple(at[i] - own[i] for i in range(3)))
    if spin:
        moved = _rotated(moved, _axis_angle(b, math.radians(float(spin))), at)
    return moved
