'''
1D shapes: curves through points.

    a = point(0, 0, 0)
    b = point(20, 10, 0)
    c = point(40, 0, 5, smooth=True)
    path = curve([a, b, c])                    # a line through the points: hard corners (straight pieces) unless a point says smooth=True
    wavy = curve([a, b, c], smooth=True)       # every point a spline point (a smooth curve through it), except where a point says smooth=False
    loop = curve([a, b, c], closed=True)       # joined from the last point back to the first

A curve is a model of its own kind (a 1D shape), made of points and drawn as a line.  Anything that takes a curve takes it as it is:

    face = sketch(loop)                        # the 2D shape a closed curve encloses (see sketch)
    tube = offset(path, 1.5)                   # a 3D shape: the points at most 1.5 from the curve (a tube round it)
    beam = path_extrude(face, path)            # a 2D shape swept along it

As a field a curve is the distance to it (zero on it, positive everywhere else), so `offset`, `thicken` and the field functions work on it.

Points are what edit it: each point is a corner (a hard edge) or a spline point, `smooth=` of the curve says which a point is that does not say itself, and the
ROTATION of a spline point -- the rotate numbers of its gizmo, which a point has no other use for -- turns the direction in which the curve leaves it.  With no
rotation the direction comes from the neighbouring points (a Catmull-Rom spline); the rotation turns it about x, then y, then z (degrees), the way a gizmo turns
a shape.  The same numbers twist what `path_extrude` sweeps along the curve.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['curve', 'Curve']

# How many straight pieces a curved segment is cut into (a segment between two corners is one piece: it is straight)
PIECES = 14


def value_of(c):
    ''' The number a coordinate has now: a number, or a field that is the same everywhere (a var(), an expression of var()s) '''
    if isinstance(c, Shape):
        return float(c(0.0, 0.0, 0.0))
    return float(c)


def vec(v):
    return tuple(Shape.wrap(c) for c in v)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, k):
    return tuple(x * k for x in a)


def dot(a, b):
    out = a[0] * b[0]
    for x, y in zip(a[1:], b[1:]):
        out = out + x * y
    return out


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(a):
    n = dot(a, a).sqrt()
    return tuple(x / n for x in a)


def rotated(t, rotation):
    ''' The vector t (three fields) turned the way the gizmo turns a shape: about x, then y, then z (degrees) '''
    k = math.pi / 180.0
    x, y, z = t
    for axis, angle in zip('xyz', rotation):
        if isinstance(angle, Shape):
            a = angle * k
            c, s = a.cos(), a.sin()
        else:
            if abs(float(angle)) < 1e-12:
                continue
            c, s = math.cos(float(angle) * k), math.sin(float(angle) * k)
        if axis == 'x':
            y, z = y * c - z * s, y * s + z * c
        elif axis == 'y':
            x, z = x * c + z * s, z * c - x * s
        else:
            x, y = x * c - y * s, x * s + y * c
    return (x, y, z)


class Control:
    ''' One point of a curve: where it is (three fields), whether the curve is smooth through it, and its rotation (three numbers or fields, or None:
        none) -- with the object the script gave, so that what it is can be told to the viewport '''
    def __init__(self, item, smooth_default):
        xyz = getattr(item, 'xyz', None)
        self.item = item
        self.default = bool(smooth_default)
        self.rotation = None
        flag = getattr(item, '_smooth', None)
        if xyz is None:
            if not isinstance(item, (tuple, list)) or len(item) not in (2, 3):
                raise TypeError('a point is point(x, y, z) or a tuple of two or three numbers, not %r' % (item,))
            xyz = tuple(item) + (0.0,) * (3 - len(item))
            flag = None
        else:
            gizmo = getattr(item, '_gizmo', None)
            if gizmo is not None:
                self.rotation = tuple(gizmo[3])
        self.pos = vec(xyz)
        self.smooth = bool(smooth_default) if flag is None else bool(flag)
        self.turned = self.rotation is not None and any(
            isinstance(a, Shape) or abs(float(a)) > 1e-12 for a in self.rotation)

    def mark(self, kind):
        ''' Tell the object the script gave what the curve made of it: a corner or a spline point (the viewport draws the two differently) '''
        try:
            self.item._curve_smooth = self.smooth
            self.item._curve_default = self.default
            self.item._curve_kind = kind
        except AttributeError:
            pass


def controls(points, smooth_default):
    return [Control(p, smooth_default) for p in points]


def mark_tangents(ctrls, tans):
    ''' Tell each spline point the direction (three fields) its curve leaves it in: the viewport draws it as a line through the point while it is selected '''
    for c, t in zip(ctrls, tans):
        if t is None:
            continue
        try:
            c.item._curve_tangent = tuple(t)
        except AttributeError:
            pass


def tangents(ctrls, closed):
    ''' The tangent (three fields) at every spline point of a curve: from its neighbours (Catmull-Rom: half the way from the one before to the one after;
        an end of an open curve looks along its one edge), turned by the point's rotation.  None at a corner '''
    n = len(ctrls)
    out = []
    for i, c in enumerate(ctrls):
        if not c.smooth:
            out.append(None)
            continue
        if closed:
            auto = mul(sub(ctrls[(i + 1) % n].pos, ctrls[(i - 1) % n].pos), 0.5)
        elif i == 0:
            auto = sub(ctrls[1].pos, c.pos)
        elif i == n - 1:
            auto = sub(c.pos, ctrls[n - 2].pos)
        else:
            auto = mul(sub(ctrls[i + 1].pos, ctrls[i - 1].pos), 0.5)
        out.append(rotated(auto, c.rotation) if c.turned else auto)
    return out


def vertices(ctrls, tans, closed, owners=None):
    ''' The vertices of the curve as a polyline (each three fields): the points themselves, and between two points that are not both corners the pieces of
        a cubic curve that leaves one in its tangent and arrives at the other in its.  An open curve ends at its last point.  When `owners` is a list
        it is filled with (the number of the point the vertex is after, how far along towards the next: 0 to 1) for every vertex '''
    n = len(ctrls)
    out = []
    last = n if closed else n - 1
    for i in range(last):
        j = (i + 1) % n
        a, b = ctrls[i].pos, ctrls[j].pos
        if not ctrls[i].smooth and not ctrls[j].smooth:
            out.append(a)
            if owners is not None:
                owners.append((i, 0.0))
            continue
        chord = sub(b, a)
        h1 = add(a, mul(tans[i] if ctrls[i].smooth else chord, 1.0 / 3.0))
        h2 = sub(b, mul(tans[j] if ctrls[j].smooth else chord, 1.0 / 3.0))
        for k in range(PIECES):
            t = k / float(PIECES)
            w = ((1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t ** 2, t ** 3)
            out.append(tuple(a[c] * w[0] + h1[c] * w[1] + h2[c] * w[2] + b[c] * w[3] for c in range(len(a))))
            if owners is not None:
                owners.append((i, t))
    if not closed:
        out.append(ctrls[-1].pos)
        if owners is not None:
            owners.append((n - 1, 0.0))
    return out


def _distance_to_polyline(q, V, closed):
    ''' The distance from the point q (three fields) to the polyline V: the least of its distances to the pieces '''
    n = len(V)
    best = None
    for i in range(n if closed else n - 1):
        a, b = V[i], V[(i + 1) % n]
        e = sub(b, a)
        w = sub(q, a)
        h = (dot(w, e) / (dot(e, e) + 1e-12)).max(0).min(1)
        d = sub(w, mul(e, h))
        dist = dot(d, d).sqrt()
        best = dist if best is None else best.min(dist)
    return best


class Curve(Shape):
    ''' A 1D shape (see `curve`).  .points are the points it was made from, .smooth the default of its points, .closed whether it is joined at its ends,
        .vertices the polyline it is drawn as (three fields each) '''
    _kind = 'curve'
    _no_gizmo = True                # (its points have the gizmos: they are what is dragged)

    def _display(self):
        out = Curve(lib.libfive_tree_copy(self.ptr))
        for name, v in self.__dict__.items():
            if name != 'ptr':
                out.__dict__[name] = v
        return out


def curve(points, smooth=False, closed=False):
    ''' A curve through the points, in the order given: straight between corners, smooth through spline points (`smooth` is what a point is that does not
        say: point(..., smooth=True) or smooth=False -- False, a corner, by default), joined from the last back to the first when `closed`.  A spline point
        leaves in the direction the rotation of its gizmo says (no rotation: from its neighbours).  As a field it is the distance to the curve '''
    items = list(points)
    ctrls = controls(points, smooth)
    for c in ctrls:
        c.mark('curve')
    if len(ctrls) < 2:
        out = Curve(lib.libfive_tree_copy(Shape.wrap(1e9).ptr))
        out.points, out.smooth, out.closed, out.vertices = items, bool(smooth), bool(closed), []
        return out
    tans = tangents(ctrls, closed)
    mark_tangents(ctrls, tans)
    owners = []
    V = vertices(ctrls, tans, closed, owners)
    q = (Shape.X(), Shape.Y(), Shape.Z())
    field = _distance_to_polyline(q, V, closed)
    out = Curve(lib.libfive_tree_copy(field.ptr))
    out.points, out.smooth, out.closed, out.vertices = items, bool(smooth), bool(closed), V
    out._ctrls, out._tangents, out._owners = ctrls, tans, owners
    out._editable = True
    out._vertices3d = V
    return out


curve._makes_no_gizmo = True
