'''
1D shapes: a line or a spline through points.

    a = point(0, 0, 0)
    b = point(20, 10, 0)
    c = point(40, 0, 5)
    lines = polyline([a, b, c])                 # straight pieces between the points: a hard corner at each
    smooth = spline([a, b, c])                  # a smooth curve through the points
    loop = spline([a, b, c], closed=True)       # joined from the last point back to the first

A curve is a model of its own kind (a 1D shape), made of points and drawn as a line.  It stands on its own, and anything that takes a curve takes it as it is:

    face = profile(loop)                         # the 2D shape a closed curve encloses (see profile)
    tube = offset(smooth, 1.5)                  # a 3D shape: the points at most 1.5 from the curve (a tube round it)
    beam = path_extrude(face, smooth)           # a 2D shape swept along it

As a field a curve is the distance to it (zero on it, positive everywhere else), so `offset`, `thicken` and the field functions work on it.

A polyline is made of corners and a spline of spline points -- but each POINT can say otherwise (`point(..., smooth=True)` or `smooth=False`; FielDes toggles
it when you double-click the point), so a spline can have a corner and a polyline a smooth stretch.  The ROTATION of a spline point -- the rotate numbers of
its gizmo, which a point has no other use for -- turns the direction in which the curve leaves it.  With no rotation the direction comes from the
neighbouring points (a Catmull-Rom spline); the rotation turns it about x, then y, then z (degrees), the way a gizmo turns a shape.  The same
numbers twist what `path_extrude` sweeps along the curve.

The line is made by the kernel from the numbers the points have when the script runs (and drawn from the numbers they have while you drag them).  Its field
is plain math: the least of the distances to its straight pieces, so the renderer drops the pieces that are far from where it looks.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import math

from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['polyline', 'spline', 'Curve']

# How many straight pieces a curved segment is cut into (a segment between two corners is one piece: it is straight)
PIECES = 14


def value_of(c):
    ''' The number a coordinate has now: a number, or a field that is the same everywhere (a var(), an expression of var()s) '''
    if isinstance(c, Shape):
        return float(c(0.0, 0.0, 0.0))
    return float(c)


def vec(v):
    return tuple(Shape.wrap(c) for c in v)


# ---- small vector helpers over plain numbers (the lines are made of numbers: see the top of this file)
def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, k):
    return tuple(x * k for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(a):
    n = math.sqrt(dot(a, a))
    return tuple(x / n for x in a) if n > 1e-300 else tuple(a)


def rotated(t, rotation):
    ''' The vector t turned the way the gizmo turns a shape: about x, then y, then z (degrees) '''
    x, y, z = t
    for axis, angle in enumerate(rotation):
        if abs(angle) < 1e-12:
            continue
        a = math.radians(angle)
        c, s = math.cos(a), math.sin(a)
        if axis == 0:
            y, z = y * c - z * s, y * s + z * c
        elif axis == 1:
            x, z = x * c + z * s, z * c - x * s
        else:
            x, y = x * c - y * s, x * s + y * c
    return (x, y, z)


class Control:
    ''' One point of a curve or profile: where it is (three numbers; the expressions the script gave are kept for the viewport, which draws the line from them
        while the point is dragged), what it says of itself (-1: what the shape says, 0: a corner, 1: a spline point) and its rotation '''
    def __init__(self, item, rounds=False, radius_default=0.0, smooth_default=False):
        xyz = getattr(item, 'xyz', None)
        flag = getattr(item, '_smooth', None)
        rotation = None
        if xyz is None:
            if not isinstance(item, (tuple, list)) or len(item) not in (2, 3):
                raise TypeError('a point is point(x, y, z) or a tuple of two or three numbers, not %r' % (item,))
            xyz = tuple(item) + (0.0,) * (3 - len(item))
            flag = None
        else:
            gizmo = getattr(item, '_gizmo', None)
            if gizmo is not None:
                rotation = tuple(gizmo[3])
        self.item = item
        self.expr = vec(xyz)
        self.pos = tuple(value_of(c) for c in self.expr)
        self.flag = -1 if flag is None else (1 if flag else 0)
        self.rot_expr = vec(rotation) if rotation is not None else None
        self.rot = tuple(value_of(c) for c in self.rot_expr) if rotation is not None else (0.0, 0.0, 0.0)
        self.turned = any(abs(a) > 1e-12 for a in self.rot)
        # the scale of a point is ONE number: the mean of the three of its gizmo (they are kept equal by the gizmo of a point).  It is how strongly a spline
        # leaves the point (1: Catmull-Rom; more: rounder; less: tighter), and how thick `path_extrude` makes what it sweeps there
        scale = getattr(item, '_gizmo', None)
        self.scale_expr = None
        self.scale = 1.0
        if scale is not None and len(scale) > 4:
            parts = vec(scale[4])
            self.scale_expr = (parts[0] + parts[1] + parts[2]) * (1.0 / 3.0)
            self.scale = max(0.0, sum(value_of(c) for c in parts) / 3.0)
        # A ROUND corner (a point of a profile -- `rounds` -- that says `point(..., radius=r)`, or the profile's own `radius` for the points that do not say): an exact arc of that
        # radius, tangent to both edges, takes the place of the corner.  The point's scale multiplies the radius -- the gold ring of its gizmo -- as the scale of a sphere multiplies
        # its radius.  A spline point has no radius (it says so itself, or the profile says it); radius=0 is a hard corner whatever the profile says
        self.radius_expr = None
        self.radius_own = None                  # (the radius the POINT says -- the number the viewport drags by the arc of the corner -- as an expression; None: the sketch's)
        self.radius = 0.0
        if rounds:
            given = getattr(item, '_radius', None)
            if given is not None:
                self.radius_own = Shape.wrap(given)
            if given is None:
                given = radius_default
            spline = self.flag == 1 or (self.flag < 0 and bool(smooth_default))
            if not spline and value_of(given) > 0.0:
                self.flag = 0
                self.radius_expr = Shape.wrap(given) * (self.scale_expr if self.scale_expr is not None else 1.0)
                self.radius = value_of(given) * self.scale

    def smooth(self, default):
        return bool(default) if self.flag < 0 else self.flag == 1

    @property
    def rounded(self):
        return self.radius > 0.0


def controls(points, rounds=False, radius_default=0.0, smooth_default=False):
    ''' The controls of a curve (`rounds` False: a point that says it is round is a corner there) or of a profile (`rounds` True) '''
    return [Control(p, rounds, radius_default, smooth_default) for p in points]


def _array(ctype, values):
    return (ctype * len(values))(*values)


def owned(shape, cls=Shape):
    ''' A new `cls` that owns a copy of the tree of `shape`.  (`shape` is held for the whole call: a copy of `X(...).ptr` made inline would be made from a tree that
        is already freed, the temporary having died as soon as its pointer was read) '''
    return cls(lib.libfive_tree_copy(shape.ptr))


def trace(ctrls, default, closed, pieces=PIECES, normal=None, arc_pieces=32):
    ''' (the vertices of the line through the controls, as (x, y, z); for each the number of the control it is after and how far along towards the next it is)
        -- made by the kernel.  `normal`: the normal of the plane of a face the points belong to -- the rotation of a point is then one angle about it, in
        the plane (the z number of its rotation) -- or None for a line in space.  A round corner is its arc, `arc_pieces` to a quarter turn (0: only the two ends of
        the arc: the chord, which is what the exact field of a face is made on, see `fillets`) '''
    n = len(ctrls)
    if n == 0:
        return [], []
    pos = _array(ctypes.c_double, [c for ctl in ctrls for c in ctl.pos])
    flags = _array(ctypes.c_int, [ctl.flag for ctl in ctrls])
    turned = _array(ctypes.c_int, [1 if ctl.turned else 0 for ctl in ctrls])
    rot = _array(ctypes.c_double, [a for ctl in ctrls for a in ctl.rot])
    scales = _array(ctypes.c_double, [ctl.scale for ctl in ctrls])
    radii = _array(ctypes.c_double, [ctl.radius for ctl in ctrls])
    axis = _array(ctypes.c_double, list(normal)) if normal is not None else None
    cap = n * (pieces + 4 * max(arc_pieces, 1) + 2) + 2
    while True:
        verts = (ctypes.c_double * (3 * cap))()
        owner = (ctypes.c_int * cap)()
        along = (ctypes.c_double * cap)()
        got = lib.libfive_pointshape_polyline(pos, flags, turned, rot, scales, radii, axis, n, 1 if default else 0, 1 if closed else 0, pieces, arc_pieces, verts, owner, along, cap)
        if got <= cap:
            break
        cap = got
    return ([(verts[3 * i], verts[3 * i + 1], verts[3 * i + 2]) for i in range(got)],
            [(owner[i], along[i]) for i in range(got)])


def fillets(ctrls, default, closed, normal=None):
    ''' The round corners of the line or face through the controls, made by the kernel: a list of (control, a, b, centre, radius) -- the arc runs from `a` to `b` (the short
        way round) about `centre`, in the plane of the face; the radius is what the arc has been made with (smaller than the one asked for where it would run into the next
        arc or past the next point) '''
    n = len(ctrls)
    if n < 3 or not any(c.rounded for c in ctrls):
        return []
    pos = _array(ctypes.c_double, [c for ctl in ctrls for c in ctl.pos])
    flags = _array(ctypes.c_int, [ctl.flag for ctl in ctrls])
    radii = _array(ctypes.c_double, [ctl.radius for ctl in ctrls])
    axis = _array(ctypes.c_double, list(normal)) if normal is not None else None
    cap = n
    while True:
        out = (ctypes.c_double * (11 * cap))()
        got = lib.libfive_pointshape_fillets(pos, flags, radii, axis, n, 1 if default else 0, 1 if closed else 0, out, cap)
        if got <= cap:
            break
        cap = got
    return [(int(out[11 * i]), tuple(out[11 * i + 1:11 * i + 4]), tuple(out[11 * i + 4:11 * i + 7]), tuple(out[11 * i + 7:11 * i + 10]), out[11 * i + 10]) for i in range(got)]


def plane_of(ctrls):
    ''' (origin, u, v) of the plane through the first three points that are not on a line (numbers), or None '''
    n = len(ctrls)
    if n < 3:
        return None
    pos = _array(ctypes.c_double, [c for ctl in ctrls for c in ctl.pos])
    frame = (ctypes.c_double * 9)()
    if not lib.libfive_pointshape_plane(pos, n, frame):
        return None
    return (tuple(frame[0:3]), tuple(frame[3:6]), tuple(frame[6:9]))


def mark(ctrls, default, kind):
    ''' Tell each point what the curve or profile made of it: a corner or a spline point (the viewport draws the two differently, and the double click
        changes it), and the default it follows '''
    n = len(ctrls)
    for i, c in enumerate(ctrls):
        try:
            c.item._curve_smooth = c.smooth(default)
            c.item._curve_default = bool(default)
            c.item._curve_kind = kind
            c.item._curve_round = c.rounded
            # (how long the shorter of its two edges is: where a double click that makes the corner round starts its radius)
            if kind == 'profile' and n >= 3:
                c.item._curve_edge = min(math.dist(c.pos, ctrls[i - 1].pos), math.dist(c.pos, ctrls[(i + 1) % n].pos))
        except AttributeError:
            pass


def viewport_data(out, ctrls, default, closed):
    ''' What the viewport draws the line or the face from while a point is dragged: the expressions of each point's place and rotation (their numbers move with
        the script's var()s), and what each says of itself.  The line is made again from them at every frame, natively: no script is run '''
    out._ctrl_pos = [c.expr for c in ctrls]
    out._ctrl_rot = [c.rot_expr for c in ctrls]
    out._ctrl_flag = [c.flag for c in ctrls]
    out._ctrl_scale = [c.scale_expr for c in ctrls]
    out._ctrl_radius = [c.radius_expr for c in ctrls]          # (the radius of a round corner, as an expression -- the scale of the point is in it -- or None)
    out._ctrl_radius_own = [c.radius_own if c.rounded else None for c in ctrls]       # (the number the point itself gives: what an arc dragged in the viewport changes)
    out._ctrl_default = bool(default)
    out.closed = bool(closed)
    out._editable = True


class Curve(Shape):
    ''' A 1D shape (see `polyline`, `spline`).  .points are the points it was made from, .smooth the default of its points, .closed whether it is joined at its
        ends, .vertices the line it is drawn as (x, y, z) '''
    _kind = 'curve'
    _no_gizmo = True                # (no gizmo() line with numbers of its own: its points have them, and the viewport shares them -- View::groupGizmo -- to move, turn and scale it)
    _outline_gizmo = True           # (the model tree's gizmo button gives it a line without numbers, `x = gizmo(x, mode=...)`: when that shared gizmo is shown, off until said otherwise)

    def _remake(self, points, improper=False):
        ''' The same line through other points (the images of its points in a symmetry: see symmetries.symmetry_images) '''
        return _make(points, self.smooth, self.closed)

    def _display(self):
        out = Curve(lib.libfive_tree_copy(self.ptr))
        for name, v in self.__dict__.items():
            if name != 'ptr':
                out.__dict__[name] = v
        shift = getattr(self, '_weld_shift', None)
        if shift is None and getattr(self, 'points', None):
            from fieldes.stdlib.points import weld_shift_of
            shift = weld_shift_of(self.points[0])               # (the image of a path: through the images of its points, which are drawn where the sweep's image is)
        if shift is not None:
            # The path of a sweep of a profile is drawn where the sweep goes -- from the middle of the profile, through the middle of the cross-section -- whatever its own numbers
            # say (the sweep starts at the profile: see path_extrude).  Its points are drawn there too (points.displayed), so a point dragged is dragged where it is seen
            out._ctrl_pos = [tuple(c + s for c, s in zip(expr, shift)) for expr in self._ctrl_pos]
            out.vertices = [tuple(a + s for a, s in zip(v, shift)) for v in self.vertices]
        return with_image_dots(self, out)


def with_image_dots(model, shown):
    ''' What is drawn for a profile or a line: `shown`, and -- when it is a placed copy of another (see handles.gizmo) -- the dots of its points, which are images of the original's
        points: each is pressed, dragged and double clicked, and it is the original's point that changes (see points.image_dot) '''
    dots = getattr(model, '_image_dots', None)
    if not dots:
        return shown
    # (each of what is returned is asked for its own display in turn: what is drawn of the outline is not asked for the dots again, or it would answer with them once more)
    shown.__dict__.pop('_image_dots', None)
    return [shown] + list(dots)


# ---- the fields: direct math (expressions of x, y, z), the way a vector renderer measures a polygon: no black boxes, so the renderer can drop what is far away
def _segment(coords, a, b):
    ''' The distance, squared, from the point (the expressions `coords`) to the segment from `a` to `b` (numbers), with what the sign of a polygon needs: the
        vector from `a` to the point and its dot product with the segment.  None for a segment that is a point '''
    e = [bj - aj for aj, bj in zip(a, b)]
    ee = sum(c * c for c in e)
    if ee < 1e-18:
        return None
    p = [c + (-aj) for c, aj in zip(coords, a)]
    dot = None
    for pj, ej in zip(p, e):
        if abs(ej) > 1e-15:
            term = pj * ej
            dot = term if dot is None else dot + term
    h = (dot * (1.0 / ee)).max(0.0).min(1.0)
    d2 = None
    for pj, ej in zip(p, e):
        q = pj + h * (-ej) if abs(ej) > 1e-15 else pj
        term = q.square()
        d2 = term if d2 is None else d2 + term
    return d2, p, dot, e


def polyline_field(vertices, closed):
    ''' The distance to the straight pieces between the vertices (x, y, z): the least of the distances to the pieces, then one square root '''
    n = len(vertices)
    coords = (Shape.X(), Shape.Y(), Shape.Z())
    best = None
    for i in range(n if closed and n > 2 else n - 1):
        got = _segment(coords, vertices[i], vertices[(i + 1) % n])
        if got is not None:
            best = got[0] if best is None else best.min(got[0])
    if best is None:
        return Shape.wrap(1e9) if n == 0 else (Shape.X() + (-vertices[0][0])).square().__add__((Shape.Y() + (-vertices[0][1])).square()).__add__((Shape.Z() + (-vertices[0][2])).square()).sqrt()
    return best.sqrt()


def _triangulate(pts):
    ''' The polygon `pts` (a list of (s, t), no repeated point) cut into triangles, as triples of indices in counter-clockwise order, by cutting off ears; None when
        it cannot be (an outline that crosses itself) '''
    n = len(pts)
    area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
    if abs(area) < 1e-18:
        return None
    ring = list(range(n)) if area > 0 else list(range(n - 1, -1, -1))
    scale = max(max(p[0] for p in pts) - min(p[0] for p in pts), max(p[1] for p in pts) - min(p[1] for p in pts), 1e-12)
    tiny = 1e-12 * scale * scale

    def turn(a, b, c):
        return (pts[b][0] - pts[a][0]) * (pts[c][1] - pts[b][1]) - (pts[b][1] - pts[a][1]) * (pts[c][0] - pts[b][0])

    def inside(q, a, b, c):
        # (on the boundary counts as inside: an ear must not have another corner on it)
        return turn(a, b, q) >= -tiny and turn(b, c, q) >= -tiny and turn(c, a, q) >= -tiny

    triangles = []
    k = 0
    stuck = 0
    while len(ring) > 3:
        m = len(ring)
        k %= m
        a, b, c = ring[k - 1], ring[k], ring[(k + 1) % m]
        ear = turn(a, b, c) > tiny
        if ear:
            for q in ring:
                if q not in (a, b, c) and inside(q, a, b, c) and pts[q] not in (pts[a], pts[b], pts[c]):
                    ear = False
                    break
        if ear:
            triangles.append((a, b, c))
            ring.pop(k)
            stuck = 0
        else:
            k += 1
            stuck += 1
            if stuck > m:
                return None
    if turn(*ring) <= tiny:
        return None
    triangles.append(tuple(ring))
    return triangles


def _arc_squared(s, t, arc):
    ''' The distance, squared, from the point (s, t) -- expressions -- to a circular arc: (a, b, centre, radius), the short way round from a to b, in the same coordinates.
        Exact: within the wedge between the two radii it is the distance to the circle, outside it the distance to the nearer end.  (Outside the wedge the circle's distance is
        made large, so that the least of the three is the right one: it is not a distance there, and it is never the least.) '''
    a, b, c, r = arc
    wx, wy = s + (-c[0]), t + (-c[1])
    e1 = (a[0] - c[0], a[1] - c[1])
    e2 = (b[0] - c[0], b[1] - c[1])
    turn = 1.0 if e1[0] * e2[1] - e1[1] * e2[0] >= 0.0 else -1.0
    across1 = (wy * e1[0] + wx * (-e1[1])) * turn                 # (e1 x w, turned the way the arc turns: not negative on the arc's side of the first radius)
    across2 = (wx * e2[1] + wy * (-e2[0])) * turn                 # (w x e2)
    outside = (across1.min(across2) * -1.0).max(0.0)               # (how far it is outside the wedge: 0 inside it)
    body = ((wx.square() + wy.square()).sqrt() + (-r)).square() + outside * 1000.0
    end_a = (s + (-a[0])).square() + (t + (-a[1])).square()
    end_b = (s + (-b[0])).square() + (t + (-b[1])).square()
    return end_a.min(end_b).min(body)


def _arc_sign(s, t, arc):
    ''' -1 inside the circular segment between the chord of the arc and the arc, +1 outside it (0 on its edge): the side of the outline flips there '''
    a, b, c, r = arc
    mx, my = 0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1])
    nx, ny = c[0] - mx, c[1] - my
    length = math.hypot(nx, ny)
    if length < 1e-300:
        return None
    nx, ny = nx / length, ny / length
    side = s * nx + t * ny + (-(mx * nx + my * ny))                # (negative on the far side of the chord from the centre: where the arc bulges)
    disc = (s + (-c[0])).square() + (t + (-c[1])).square()
    return (disc.sqrt() + (-r)).max(side).compare(0.0)


def polygon_field(loop, frame, arcs=None):
    ''' The signed distance to the polygon whose corners are `loop` (s, t) -- negative inside, in the plane of `frame` (origin, u, v: s and t are the
        coordinates along u and v), or in x and y when there is none.  An exact distance, concave outlines too.

        `arcs`: round corners, (a, b, centre, radius) in the same coordinates, each the short way round from a to b; `loop` then has the chord from a to b as an edge
        in place of each of them (see `fillets`).  The distance is to the arc, exact (the chord is not an edge of the outline), and the side of the outline is that of the
        polygon of the chords turned over inside every circular segment between a chord and its arc: a segment that bulges out of the polygon is added to it, one that bulges
        into it is cut out.

        Made the way a vector renderer measures a polygon, of two parts, both a plain `min` over small pieces so that the renderer drops the pieces that are far
        from where it looks (a polygon of a hundred edges costs what the few pieces near the point cost):
        - how far: the least of the distances to the edges;
        - which side: the polygon is cut into triangles when the script runs, and a point is inside when it is inside one of them (a `min` over triangles of
          the `max` of three straight lines).  Across a cut a triangle goes a hair over, so that the cut itself is inside; the edges of the polygon are exact.
        An outline that crosses itself cannot be cut: its side is then the winding of the outline round the point (the sum of the angles each edge is seen
        under: a full turn inside, none outside) '''
    pts = []
    for q in loop:
        if not pts or abs(q[0] - pts[-1][0]) + abs(q[1] - pts[-1][1]) > 1e-12:
            pts.append((float(q[0]), float(q[1])))
    while len(pts) > 1 and abs(pts[0][0] - pts[-1][0]) + abs(pts[0][1] - pts[-1][1]) <= 1e-12:
        pts.pop()
    n = len(pts)
    if n == 0:
        return Shape.wrap(1e9)
    if frame is None:
        s, t = Shape.X(), Shape.Y()
    else:
        o, u, v = frame
        s = _linear_in(u, -sum(a * b for a, b in zip(o, u)))
        t = _linear_in(v, -sum(a * b for a, b in zip(o, v)))
    arcs = list(arcs or [])
    span0 = max(max(q[0] for q in pts) - min(q[0] for q in pts), max(q[1] for q in pts) - min(q[1] for q in pts), 1e-12)
    near = 1e-9 * span0

    def same(p, q):
        return abs(p[0] - q[0]) <= near and abs(p[1] - q[1]) <= near

    best = None
    for i in range(n if n > 2 else max(1, n - 1)):
        p, q = pts[i], pts[(i + 1) % n]
        # (the chord of an arc is no edge of the outline: the arc is)
        if any((same(p, arc[0]) and same(q, arc[1])) or (same(p, arc[1]) and same(q, arc[0])) for arc in arcs):
            continue
        got = _segment((s, t), p, q)
        if got is not None:
            best = got[0] if best is None else best.min(got[0])
    for arc in arcs:
        term = _arc_squared(s, t, arc)
        best = term if best is None else best.min(term)
    if best is None:
        best = (s + (-pts[0][0])).square() + (t + (-pts[0][1])).square()
    distance = best.sqrt()
    if n < 3:
        return distance
    flips = None
    for arc in arcs:
        sign = _arc_sign(s, t, arc)
        if sign is not None:
            flips = sign if flips is None else flips * sign
    triangles = _triangulate(pts)
    if triangles is None:
        turn = None
        for i in range(n):
            got = _segment((s, t), pts[i], pts[(i + 1) % n])
            if got is not None:
                d2, p, dot, e = got
                angle = (p[1] * e[0] + p[0] * (-e[1])).atan2((p[0].square() + p[1].square()) + (-1.0) * dot)
                turn = angle if turn is None else turn + angle
        out = distance * (turn.abs() * (-1.0 / math.pi) + 1.0)
        return out if flips is None else out * flips
    span = max(max(q[0] for q in pts) - min(q[0] for q in pts), max(q[1] for q in pts) - min(q[1] for q in pts))
    over = 1e-4 * span                  # (how far a triangle goes over a cut: far more than the rounding of a float, far less than anything that is drawn)
    inside = None
    for tri in triangles:
        lines = None
        for k in range(3):
            a, b = tri[k], tri[(k + 1) % 3]
            ex, ey = pts[b][0] - pts[a][0], pts[b][1] - pts[a][1]
            length = math.hypot(ex, ey)
            nx, ny = ey / length, -ex / length
            cut = (a - b) % n not in (1, n - 1)         # (the two corners are not neighbours in the polygon: the edge is a cut)
            line = s * nx + t * ny + (-(pts[a][0] * nx + pts[a][1] * ny) - (over if cut else 0.0))
            lines = line if lines is None else lines.max(line)
        inside = lines if inside is None else inside.min(lines)
    side = inside.compare(0.0)
    return distance * (side if flips is None else side * flips)


def _linear_in(n, off):
    ''' n . (x, y, z) + off, of as few operations as it can be '''
    terms = None
    for axis, c in zip((Shape.X(), Shape.Y(), Shape.Z()), n):
        if abs(c) < 1e-15:
            continue
        term = axis * c
        terms = term if terms is None else terms + term
    if terms is None:
        return Shape.wrap(off)
    return terms + off if abs(off) > 1e-15 else terms


def _make(points, default, closed):
    items = list(points)
    ctrls = controls(items)
    mark(ctrls, default, 'curve')
    if len(ctrls) < 2:
        out = owned(Shape.wrap(1e9), Curve)
        out.vertices, out._owners = [], []
    else:
        verts, owners = trace(ctrls, default, closed)
        out = owned(polyline_field(verts, closed), Curve)
        out.vertices, out._owners = verts, owners
    out.points, out.smooth = items, bool(default)
    out._ctrls = ctrls
    viewport_data(out, ctrls, default, closed)
    return out


def polyline(points, closed=False):
    ''' A set of lines: straight pieces through the points, in the order given, a corner at each (a point can say it is a spline point: `point(..., smooth=True)`),
        joined from the last point back to the first when `closed`.  As a field it is the distance to the lines '''
    return _make(points, False, closed)


def spline(points, closed=False):
    ''' A smooth curve through the points, in the order given (a point can say it is a corner: `point(..., smooth=False)`), joined from the last point back to the first
        when `closed`.  It leaves each point in the direction the rotation of its gizmo says (no rotation: from the neighbouring points).  As a field it is the
        distance to the curve '''
    return _make(points, True, closed)


polyline._makes_no_gizmo = True
spline._makes_no_gizmo = True
polyline._makes_outline_gizmo = True
spline._makes_outline_gizmo = True
