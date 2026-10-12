'''
A 2D shape from points or from a closed curve: `sketch`.

    a = point(0, 0, 0)
    b = point(40, 0, 0)
    c = point(40, 20, 0)
    d = point(0, 20, 0, smooth=True)
    plate = sketch([a, b, c, d])                # the face the points enclose; hard edges unless a point says smooth=True
    round = sketch([a, b, c, d], smooth=True)   # ...or every point a spline, but where a point says smooth=False
    outline = curve([a, b, c, d], closed=True)
    plate = sketch(outline)                     # the same, from a curve
    body = extrude(plate, 0, 5)                 # a solid: the face pulled 5 along its normal

The points are ordinary points (`point(x, y, z)`, dragged with their gizmo): any number of them, joined in the order they are given and closed (the last to
the first).  The shape lies in the PLANE through the first three points (the first that is not on a line with the first two); every other point is
projected onto that plane, so a sketch is made whatever the points are.  A tuple of three numbers stands for a point too.

Each point is a corner (a hard edge: straight to its neighbours) or a spline point (the outline is a smooth curve through it) -- see `curve`, which makes the
outline.  `smooth=` of the sketch says which a point is that does not say itself (False, a hard edge, by default); `point(..., smooth=True)` says it for
that point -- FielDes toggles it when you double-click the point.  The ROTATION of a spline point -- its gizmo's rotate numbers, which a point has no other use
for -- turns the direction in which the outline leaves it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib import curves as C

__all__ = ['sketch', 'Sketch', 'extrude', 'path_extrude']


def _frame(vals, pts):
    ''' (origin, u, v) of the plane through the first three points that are not on a line, as fields: u along the first edge, v across it in the plane.
        None when all the points are on a line (or there are fewer than three) '''
    if len(pts) < 3:
        return None
    a, b = vals[0], vals[1]
    e1 = [b[i] - a[i] for i in range(3)]
    if sum(c * c for c in e1) < 1e-18:
        return None
    third = None
    for k in range(2, len(pts)):
        e2 = [vals[k][i] - a[i] for i in range(3)]
        cr = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        if sum(c * c for c in cr) > 1e-12 * sum(c * c for c in e1) * max(1.0, sum(c * c for c in e2)):
            third = k
            break
    if third is None:
        return None
    A = pts[0]
    d1 = C.sub(pts[1], A)
    d2 = C.sub(pts[third], A)
    u = C.unit(d1)
    n = C.unit(C.cross(d1, d2))
    v = C.cross(n, u)
    return A, u, v


def _polygon_field(S, T, V):
    ''' The distance to the closed polygon V (a list of (s, t) fields), negative inside: an exact distance, the sign by how many times a ray in +s
        crosses the outline (even = outside), so that any outline works, a concave one too '''
    n = len(V)
    up = [((v[1] - T).compare(0)).max(0) for v in V]            # 1 where the vertex is above the query point
    best = None
    crossings = None
    for i in range(n):
        a, b = V[i], V[(i + 1) % n]
        ex, ey = b[0] - a[0], b[1] - a[1]
        wx, wy = S - a[0], T - a[1]
        h = ((wx * ex + wy * ey) / (ex * ex + ey * ey + 1e-12)).max(0).min(1)
        dx, dy = wx - ex * h, wy - ey * h
        d = (dx.square() + dy.square()).sqrt()
        best = d if best is None else best.min(d)
        c = ((up[(i + 1) % n] - up[i]) * (ex * wy - ey * wx)).compare(0).max(0)
        crossings = c if crossings is None else crossings + c
    # (+1 where an even number of crossings is outside, -1 inside: written without a subtraction of a number, which FielDes would offer as a handle)
    return Shape.wrap(0.5).compare(crossings % 2) * best


class Sketch(Shape):
    ''' A 2D shape made of points (see `sketch`).  .points are the points it was made from, .frame the plane it lies in -- (origin, u, v), three fields each --
        and .smooth the default of its points '''
    _kind = 'profile'
    _no_gizmo = True                # (its points have the gizmos: they are what is dragged)

    def _display(self):
        from fieldes.stdlib.points import displayed
        return displayed(self)


def sketch(source, smooth=False):
    ''' The 2D shape the points of `source` enclose -- a list of points, or a curve -- as a closed outline through them in the order given, in the plane
        through the first three of them (the others are projected onto it).  `smooth` is what a point is that does not say (point(..., smooth=True) or
        smooth=False): False, a corner (a hard edge: the default), or True, a spline point.  A spline point leaves in the direction the rotation of its gizmo
        says (no rotation: from its neighbours).  With fewer than three points, or all on a line, it is empty '''
    if isinstance(source, C.Curve):
        items = list(source.points)
        smooth = bool(smooth) or source.smooth          # (a curve that is smooth is smooth as the outline too)
    else:
        items = list(source)
    smooth = bool(smooth)
    ctrls = C.controls(items, smooth)
    for c in ctrls:
        c.mark('sketch')
    pts = [c.pos for c in ctrls]
    vals = [tuple(C.value_of(x) for x in p) for p in pts]
    frame = _frame(vals, pts)
    if frame is None:
        out = Sketch(lib.libfive_tree_copy(Shape.wrap(1e9).ptr))
        out.points, out.frame, out.smooth = items, None, bool(smooth)
        return out
    A, u, v = frame
    tans = C.tangents(ctrls, True)
    C.mark_tangents(ctrls, tans)
    owners = []
    V3 = C.vertices(ctrls, tans, True, owners)
    V = [(C.dot(C.sub(p, A), u), C.dot(C.sub(p, A), v)) for p in V3]
    q = (Shape.X(), Shape.Y(), Shape.Z())
    S, T = C.dot(C.sub(q, A), u), C.dot(C.sub(q, A), v)
    field = _polygon_field(S, T, V)
    out = Sketch(lib.libfive_tree_copy(field.ptr))
    out.points, out.frame, out.smooth = items, frame, bool(smooth)
    out._flat_frame = frame
    # (a point of a sketch is dragged in the plane of the sketch: that is where its outline is)
    normal = C.cross(u, v)
    for c in ctrls:
        try:
            c.item._curve_plane = tuple(normal)
        except AttributeError:
            pass
    out._editable = True
    out.closed = True
    out._vertices3d, out._owners = V3, owners
    return out


sketch._makes_no_gizmo = True


def extrude(shape, a, b):
    ''' A 2D shape pulled into a solid: the part of space between the distances `a` and `b` from its plane, along the plane's normal (the plane is
        z = 0 for the shapes that lie there -- a circle, a rectangle -- so that is zmin and zmax; the plane of a sketch is the plane of its points,
        and the normal is the direction u x v of that plane).  `extrude(circle(5), 0, 10)` is a cylinder 10 tall '''
    shape = Shape.wrap(shape)
    frame = getattr(shape, '_flat_frame', None)
    if frame is None:
        w = Shape.Z()
    else:
        A, u, v = frame
        n = C.cross(u, v)
        q = (Shape.X(), Shape.Y(), Shape.Z())
        w = C.dot(C.sub(q, A), n)
    body = shape.max(Shape.wrap(a) - w).max(w - Shape.wrap(b))
    return Shape(lib.libfive_tree_copy(body.ptr))


def _profile_at(profile, a, b):
    ''' The 2D shape as a field of two coordinates a and b (fields): where its own plane's origin is the point of the path and a, b run along its
        own two axes -- x and y for a shape in the plane z = 0, u and v for a sketch '''
    profile = Shape.wrap(profile)
    frame = getattr(profile, '_flat_frame', None)
    if frame is None:
        return profile.remap(a, b, Shape.wrap(0))
    A, u, v = frame
    return profile.remap(*[A[i] + a * u[i] + b * v[i] for i in range(3)])


def _transported(P, T, N):
    ''' The normal at the next vertex, carried along from this one without turning about the path (the double reflection of Wang et al.): P, T, N are
        this vertex's place, tangent and normal, then the next vertex's place and tangent follow in the lists '''
    v1 = C.sub(P[1], P[0])
    c1 = C.dot(v1, v1) + 1e-12
    rL = C.sub(N, C.mul(v1, C.dot(v1, N) * 2 / c1))
    tL = C.sub(T[0], C.mul(v1, C.dot(v1, T[0]) * 2 / c1))
    v2 = C.sub(T[1], tL)
    c2 = C.dot(v2, v2) + 1e-12
    return C.sub(rL, C.mul(v2, C.dot(v2, rL) * 2 / c2))


def path_extrude(profile, path, twist=0):
    ''' A 2D shape swept along a curve: the `profile` -- its origin on the curve, its own axes across it (x and y for a shape in the plane z = 0, the
        two axes of the plane of a sketch) -- carried along `path`, turning with it, without twisting about it unless the points of the curve say so:
        the ROTATION of a point (its gizmo's rotate numbers) twists the profile about the curve there, and the twist runs between the points as a
        smooth change.  `twist` (degrees) turns the whole profile about the curve besides.  The ends are cut square.  A curve with corners is swept
        with mitered joints.  `path_extrude(circle(2), curve([a, b, c]))` is a tube; `extrude` is the straight case '''
    if isinstance(path, (list, tuple)):
        path = C.curve(path)                # (the points of the path: a curve through them, as given)
    if not isinstance(path, C.Curve):
        raise TypeError('path_extrude(profile, path): the path is a curve (curve([points])), or a list of points')
    V = list(path.vertices)
    owners = list(path._owners)
    n = len(V)
    if n < 2:
        return Shape(lib.libfive_tree_copy(Shape.wrap(1e9).ptr))
    closed = path.closed
    # the pieces of the path: between consecutive vertices (a closed path ends back at the first)
    pieces = n if closed else n - 1
    T = [C.unit(C.sub(V[(k + 1) % n], V[k])) for k in range(pieces)]
    # the vertex's own tangent: along the one piece at the end of an open path, between the two pieces elsewhere (a closed path wraps)
    def tangent_at(k):
        if closed:
            return C.unit(C.add(T[k - 1], T[k]))
        if k == 0:
            return T[0]
        if k == n - 1:
            return T[pieces - 1]
        return C.unit(C.add(T[k - 1], T[k]))
    # the first normal: across the first piece, from the axis it is least along
    t0 = [C.value_of(c) for c in T[0]]
    axis = min(range(3), key=lambda i: abs(t0[i]))
    ref = tuple(1.0 if i == axis else 0.0 for i in range(3))
    N = [None] * pieces
    d = C.dot(ref, T[0])
    N[0] = C.unit(C.add(C.mul(T[0], -d), ref))
    for k in range(pieces - 1):
        N[k + 1] = C.unit(_transported((V[k], V[k + 1]), (T[k], T[k + 1]), N[k]))
    B = [C.cross(T[k], N[k]) for k in range(pieces)]
    # the twist: a point's rotation turns its normal about the path; between the points the angle runs linearly
    ctrls = path._ctrls
    roll = []
    for c, k in zip(ctrls, range(len(ctrls))):
        roll.append(None)
    angle_of = {}
    for i, c in enumerate(ctrls):
        if not c.turned:
            continue
        # (the normal of the piece that starts at this point's vertex, or the last piece at the end of an open path)
        vertex = next((j for j, o in enumerate(owners) if o[0] == i and o[1] == 0.0), None)
        if vertex is None:
            continue
        k = min(vertex, pieces - 1)
        Nr = C.rotated(N[k], c.rotation)
        angle_of[i] = (C.dot(Nr, B[k])).atan2(C.dot(Nr, N[k]))
    def roll_at(j):
        i, t = owners[j]
        a0 = angle_of.get(i, 0.0)
        i1 = (i + 1) % len(ctrls)
        a1 = angle_of.get(i1, 0.0) if (closed or i + 1 < len(ctrls)) else a0
        return a0 * (1 - t) + a1 * t
    total = twist * math.pi / 180.0 if not isinstance(twist, Shape) else twist * (math.pi / 180.0)
    q = (Shape.X(), Shape.Y(), Shape.Z())
    best = None
    for k in range(pieces):
        j = (k + 1) % n
        phi = (roll_at(k) + roll_at(j)) * 0.5 + total
        cs, sn = (phi.cos(), phi.sin()) if isinstance(phi, Shape) else (math.cos(phi), math.sin(phi))
        Nk = C.add(C.mul(N[k], cs), C.mul(B[k], sn))
        Bk = C.sub(C.mul(B[k], cs), C.mul(N[k], sn))
        w = C.sub(q, V[k])
        f = _profile_at(profile, C.dot(w, Nk), C.dot(w, Bk))
        # the piece ends at the planes that bisect the corners (square at the ends of an open path)
        mk = tangent_at(k)
        mj = tangent_at(j)
        # (a hair past the plane, so that the two pieces overlap there and the joint is inside, not a plane where the field is zero)
        start = -(C.dot(w, mk) + 1e-3)
        stop = C.dot(C.sub(q, V[j]), mj) + (-1e-3)
        body = f.max(start).max(stop)
        best = body if best is None else best.min(body)
    return Shape(lib.libfive_tree_copy(best.ptr))
