'''
A 2D shape from points or from a closed curve: `sketch`.

    a = point(0, 0, 0)
    b = point(40, 0, 0)
    c = point(40, 20, 0)
    d = point(0, 20, 0, smooth=True)
    plate = sketch([a, b, c, d])                # the face the points enclose; hard edges unless a point says smooth=True
    round = sketch([a, b, c, d], smooth=True)   # ...or every point a spline, but where a point says smooth=False
    outline = spline([a, b, c, d], closed=True)
    plate = sketch(outline)                     # the same, from a curve
    body = extrude(plate, 0, 5)                 # a solid: the face pulled 5 along its normal

The points are ordinary points (`point(x, y, z)`, dragged with their gizmo): any number of them, joined in the order they are given and closed (the last to
the first).  The shape lies in the PLANE through the first three points (the first that is not on a line with the first two); every other point is
projected onto that plane, so a sketch is made whatever the points are.  A tuple of three numbers stands for a point too.

Each point is a corner (a hard edge: straight to its neighbours) or a spline point (the outline is a smooth curve through it) -- see `spline`, which makes the
outline.  `smooth=` of the sketch says which a point is that does not say itself (False, a hard edge, by default); `point(..., smooth=True)` says it for
that point -- FielDes toggles it when you double-click the point.  The ROTATION of a spline point -- its gizmo's rotate numbers, which a point has no other use
for -- turns the direction in which the outline leaves it.

A sketch is drawn from the numbers its points have, natively, while you drag them.  Its field (for `extrude`, `offset`, analyses ...) is plain math, made
when the script runs: the signed distance to the outline -- the distance to the nearest edge, with the side taken from that nearest edge or corner alone, so
the renderer drops the edges that are far from where it looks.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import math

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib import curves as C

__all__ = ['sketch', 'Sketch', 'extrude', 'path_extrude']


class Sketch(Shape):
    ''' A 2D shape made of points (see `sketch`).  .points are the points it was made from, .plane the plane it lies in -- (origin, u, v), three numbers each --
        .smooth the default of its points, .profile2d the same field in the coordinates of its plane (x along u, y along v) '''
    _kind = 'profile'
    _no_gizmo = True                # (its points have the gizmos: they are what is dragged)

    def _display(self):
        # What the viewport draws is the face of the outline, made natively from the points' numbers: no field is needed for that, so none is made
        out = C.owned(Shape.wrap(1.0))
        for name, v in self.__dict__.items():
            if name != 'ptr':
                out.__dict__[name] = v
        out._kind = 'profile'
        out._sketch_face = True
        return out


class SurfaceSketch(Shape):
    ''' A 2D shape made from a surface (see `sketch`): the outline of the surface in its plane, holes included.  .plane the plane it lies in -- (origin, u, v), three
        numbers each, u x v the way the surface faces --, .profile2d the same field in the coordinates of that plane, .flat whether the surface is flat (it is projected
        onto the plane when it is not), .away how far the surface is from the plane at most, .loops the outline as [(polygon, hole)] with the points as (s, t) '''
    _kind = 'profile'
    _no_gizmo = True                # (it follows the part it was picked on)

    def _display(self):
        # Drawn from its field, as any 2D shape is, in its plane
        out = C.owned(self)
        for name, v in self.__dict__.items():
            if name != 'ptr':
                out.__dict__[name] = v
        out._kind = 'profile'
        out._flat2d = True
        o, u, v = self.plane
        out._flat_frame = tuple(tuple(Shape.wrap(float(c)) for c in axis) for axis in (o, u, v))
        return out


def _polygon_field(loop, frame):
    return C.owned(C.polygon_field(loop, frame))


def _from_surface(selection):
    ''' The 2D shape a surface picked on a part makes (see `sketch`) '''
    from fieldes.stdlib import surface_sketch
    plane, loops, flat, away = surface_sketch.outline_of(selection)

    def field(frame):
        out = None
        for polygon, hole in loops:
            if hole:
                continue
            part = C.polygon_field(polygon, frame)
            out = part if out is None else out.min(part)
        for polygon, hole in loops:
            if hole:
                out = out.max(C.polygon_field(polygon, frame) * -1.0)          # (what is inside a hole is outside)
        return out

    made = C.owned(field(plane), SurfaceSketch)          # (the field is held for the whole call: see curves.owned)
    made.plane, made.profile2d, made.flat, made.away, made.loops = plane, C.owned(field(None)), flat, away, loops
    made.surface = selection
    made._no_handles = True                  # (no faces of its own to pull: it is made from the surface)
    if not flat:
        print('sketch: the surface is not flat (it is up to %.3g mm from the plane through its middle): its outline is projected onto that plane, along the way it '
              'faces on average' % away)
    return made


def sketch(source, smooth=False):
    ''' The 2D shape the points of `source` enclose -- a list of points, or a curve -- as a closed outline through them in the order given, in the plane
        through the first three of them (the others are projected onto it).  `smooth` is what a point is that does not say (point(..., smooth=True) or
        smooth=False): False, a corner (a hard edge: the default), or True, a spline point.  A spline point leaves in the direction the rotation of its gizmo
        says (no rotation: from its neighbours).  With fewer than three points, or all on a line, it is empty.

        `source` can also be a SURFACE picked on a part (`select_surface`): the 2D shape of its outline -- the outline of the face, holes included, in the plane
        of the face (exact for a flat face), or, for a surface that is not flat, of its projection onto the plane through its middle, along the way it faces on
        average.  It is made from the surface and moves with it: it has no points of its own to drag '''
    from fieldes.stdlib.selection import SurfaceSelection
    if isinstance(source, SurfaceSelection):
        return _from_surface(source)
    if isinstance(source, C.Curve):
        items = list(source.points)
        smooth = bool(smooth) or source.smooth          # (a curve that is a spline is a spline as the outline too)
    else:
        items = list(source)
    smooth = bool(smooth)
    ctrls = C.controls(items)
    C.mark(ctrls, smooth, 'sketch')
    plane = C.plane_of(ctrls)
    if plane is None:
        out = C.owned(Shape.wrap(1e9), Sketch)
        out.vertices, out._owners, out.plane, out.profile2d = [], [], None, out
    else:
        o, u, v = plane
        verts, owners = C.trace(ctrls, smooth, True, normal=C.cross(u, v))        # (the rotation of a point of a sketch is one angle, about the normal of its plane)
        loop = [(C.dot(C.sub(p, o), u), C.dot(C.sub(p, o), v)) for p in verts]
        field = _polygon_field(loop, plane)
        out = Sketch(lib.libfive_tree_copy(field.ptr))
        out.plane = plane
        out.profile2d = _polygon_field(loop, None)
        out.vertices, out._owners = verts, owners
    out.points, out.smooth = items, smooth
    out._ctrls = ctrls
    C.viewport_data(out, ctrls, smooth, True)
    return out


sketch._makes_no_gizmo = True


def _linear(n, off):
    ''' The field n . (x, y, z) + off: a plane's distance, made of as few operations as it can be (no subtraction of a bare number: FielDes would offer that
        number as a handle) '''
    terms = None
    for axis, c in zip((Shape.X(), Shape.Y(), Shape.Z()), n):
        if abs(c) < 1e-15:
            continue
        t = axis * c
        terms = t if terms is None else terms + t
    if terms is None:
        return Shape.wrap(off)
    return terms + off if abs(off) > 1e-15 else terms


def extrude(shape, a, b):
    ''' A 2D shape pulled into a solid: the part of space between the distances `a` and `b` from its plane, along the plane's normal (the plane is
        z = 0 for the shapes that lie there -- a circle, a rectangle -- so that is zmin and zmax; the plane of a sketch is the plane of its points,
        and the normal is the direction u x v of that plane).  `extrude(circle(5), 0, 10)` is a cylinder 10 tall '''
    shape = Shape.wrap(shape)
    plane = getattr(shape, 'plane', None)
    if plane is None:
        w = Shape.Z()
    else:
        o, u, v = plane
        n = C.cross(u, v)
        w = _linear(n, -C.dot(n, o))
    body = shape.max(Shape.wrap(a) - w).max(w - Shape.wrap(b))
    return Shape(lib.libfive_tree_copy(body.ptr))


def _profile_at(profile, a, b, size=None):
    ''' The 2D shape as a field of two coordinates a and b (fields): where its own plane's origin is the point of the path and a, b run along its own two axes
        -- x and y for a shape in the plane z = 0, u and v for a sketch.  `size` (a field, or None): how many times as big it is there -- the shape
        is looked at from 1 / size as far, and the distance it says is made as many times as long, so that it is still a distance '''
    profile = Shape.wrap(profile)
    inner = getattr(profile, 'profile2d', None)
    base = inner if inner is not None else profile
    if size is None:
        return base.remap(a, b, Shape.wrap(0))
    return base.remap(a / size, b / size, Shape.wrap(0)) * size


def _transported(p0, p1, t0, t1, n0):
    ''' The normal at the next vertex, carried along from this one without turning about the path (the double reflection of Wang et al.): the places of
        the two vertices, their tangents, and the normal here '''
    v1 = C.sub(p1, p0)
    c1 = C.dot(v1, v1) + 1e-300
    rl = C.sub(n0, C.mul(v1, C.dot(v1, n0) * 2 / c1))
    tl = C.sub(t0, C.mul(v1, C.dot(v1, t0) * 2 / c1))
    v2 = C.sub(t1, tl)
    c2 = C.dot(v2, v2) + 1e-300
    return C.sub(rl, C.mul(v2, C.dot(v2, rl) * 2 / c2))


def path_extrude(profile, path, twist=0):
    ''' A 2D shape swept along a curve: the `profile` -- its origin on the curve, its own axes across it (x and y for a shape in the plane z = 0, the
        two axes of the plane of a sketch) -- carried along `path`, turning with it, without twisting about it unless the points of the curve say so:
        the ROTATION of a point (its gizmo's rotate numbers) twists the profile about the curve there, and the twist runs between the points as a
        smooth change.  `twist` (degrees) turns the whole profile about the curve besides.  The ends are cut square.  A curve with corners is swept
        with mitered joints.  `path_extrude(circle(2), spline([a, b, c]))` is a bent tube; `extrude` is the straight case.  `path` may be a list of points:
        the spline through them '''
    if isinstance(path, (list, tuple)):
        from fieldes.stdlib.curves import spline
        path = spline(path)
    if not isinstance(path, C.Curve):
        raise TypeError('path_extrude(profile, path): the path is a curve (polyline([points]), spline([points])), or a list of points')
    V = list(path.vertices)
    owners = list(path._owners)
    n = len(V)
    if n < 2:
        return C.owned(Shape.wrap(1e9))
    closed = path.closed
    pieces = n if closed else n - 1
    T = [C.unit(C.sub(V[(k + 1) % n], V[k])) for k in range(pieces)]

    def tangent_at(k):
        # the vertex's own direction: along the one piece at the end of an open path, between the two pieces elsewhere (a closed path wraps)
        if closed:
            return C.unit(C.add(T[k - 1], T[k]))
        if k == 0:
            return T[0]
        if k == n - 1:
            return T[pieces - 1]
        return C.unit(C.add(T[k - 1], T[k]))

    # the first normal: across the first piece, from the axis it is least along (so a path that starts up the z axis has the profile's x along x and y along y)
    axis = min(range(3), key=lambda i: abs(T[0][i]))
    ref = tuple(1.0 if i == axis else 0.0 for i in range(3))
    N = [None] * pieces
    N[0] = C.unit(C.add(C.mul(T[0], -C.dot(ref, T[0])), ref))
    for k in range(pieces - 1):
        N[k + 1] = C.unit(_transported(V[k], V[k + 1], T[k], T[k + 1], N[k]))
    B = [C.cross(T[k], N[k]) for k in range(pieces)]
    # the twist: a point's rotation turns its normal about the path; between the points the angle runs along the line
    ctrls = path._ctrls
    angle_of = {}
    for i, c in enumerate(ctrls):
        if not c.turned:
            continue
        vertex = next((j for j, o in enumerate(owners) if o[0] == i and o[1] == 0.0), None)
        if vertex is None:
            continue
        k = min(vertex, pieces - 1)
        nr = C.rotated(N[k], c.rot)
        angle_of[i] = math.atan2(C.dot(nr, B[k]), C.dot(nr, N[k]))

    def roll_at(j):
        i, t = owners[j]
        a0 = angle_of.get(i, 0.0)
        i1 = (i + 1) % len(ctrls)
        a1 = angle_of.get(i1, 0.0) if (closed or i + 1 < len(ctrls)) else a0
        return a0 * (1 - t) + a1 * t

    total = math.radians(C.value_of(twist))
    # the scale of a point is how thick the profile is there; between the points it runs along the line, as the twist does
    sizes = [c.scale for c in ctrls]

    def size_at(j):
        i, t = owners[j]
        s0 = sizes[i]
        i1 = (i + 1) % len(ctrls)
        s1 = sizes[i1] if (closed or i + 1 < len(ctrls)) else s0
        return max(s0 * (1 - t) + s1 * t, 1e-3)

    best = None
    for k in range(pieces):
        j = (k + 1) % n
        phi = (roll_at(k) + roll_at(j)) * 0.5 + total
        cs, sn = math.cos(phi), math.sin(phi)
        nk = C.add(C.mul(N[k], cs), C.mul(B[k], sn))
        bk = C.sub(C.mul(B[k], cs), C.mul(N[k], sn))
        a = _linear(nk, -C.dot(nk, V[k]))
        b = _linear(bk, -C.dot(bk, V[k]))
        s0, s1 = size_at(k), size_at(j)
        size = None
        if abs(s0 - 1) > 1e-9 or abs(s1 - 1) > 1e-9:
            # (thicker or thinner here: the size runs along the piece from one end to the other)
            span = math.sqrt(C.dot(C.sub(V[j], V[k]), C.sub(V[j], V[k]))) + 1e-300
            slope = (s1 - s0) / span
            size = _linear(C.mul(T[k], slope), s0 - C.dot(T[k], V[k]) * slope).max(1e-3)
        f = _profile_at(profile, a, b, size)
        # the piece ends at the planes that bisect the corners (square at the ends of an open path), a hair past them, so that two pieces overlap there and
        # the joint is inside, not a plane where the field is zero
        mk, mj = tangent_at(k), tangent_at(j)
        start = _linear(C.mul(mk, -1.0), C.dot(mk, V[k]) - 1e-3)
        stop = _linear(mj, -C.dot(mj, V[j]) - 1e-3)
        body = f.max(start).max(stop)
        best = body if best is None else best.min(body)
    return Shape(lib.libfive_tree_copy(best.ptr))
