'''
Corners and edges of a body, found from its field: the points and lines a CAD program lets you snap to.

    from fieldes import *

    corner = corner_at(part, (10.2, 9.9, 20.1))            # displayed: the point where three faces of the part meet, nearest that place
    edge = edge_at(part, (10.2, 0.1, 19.8))                # displayed: the straight line of the edge nearest that place, from corner to corner
    on_edge = edge_point_at(part, (10.2, 0.1, 19.8))       # displayed: the point of the nearest edge that is nearest that place (any edge, a circle too)

`near` is a place on (or near) the surface -- where you right-clicked: in FielDes, right-click a body close to an edge or a corner and the menu offers
the call that fits (it is offered only where there is one), written under the body like `select_surface` is.

They are made from the part's FIELD alone -- its value and its gradient, which is the surface normal --, as `select_surface` is, and no mesh of the part is made or
read: around `near` the surface is sampled and its normals are sorted into faces; where two faces meet at a sharp angle there is an edge, where three do there is
a corner, and its place is where the planes of the faces meet (worked out again from smaller and smaller neighbourhoods, so that a curved face is followed).
What counts is a *jump* of the normal that stays a jump however closely it is looked at: a smooth surface -- a sphere, the side of a cylinder, a fillet --
turns by less and less the closer it is looked at, so it has neither edges nor corners; the rim of a cylinder, where the side meets the flat end, is an edge
(`edge_point_at` gives a point of it; `edge_at` needs a straight one, and says so when the edge curves).

What comes back are the ordinary things: a `point` (so it is a corner of a profile, the end of a `polyline`, a place for a gizmo) and a `polyline`
of two points.  They are computed when the script runs, from `near` and the body: the body can be changed and they follow, as long as the edge or
corner is still near that place.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

__all__ = ['corner_at', 'edge_at', 'edge_point_at']

_SHARP = math.radians(25.0)                 # surface normals this far apart belong to faces that meet at an edge
_PARALLEL = math.sin(math.radians(15.0))    # faces whose normals are this nearly parallel have no edge between them that can be placed
_COPLANAR = 0.15                            # three faces whose normals are this nearly in one plane have no corner
_REACH = 1.6                                # how far from `near` a feature may be, in neighbourhoods
_MIN_STRAIGHT = 0.02                        # an edge is a line when it runs this share of the body's size


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _mul(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


def _norm(a):
    return math.sqrt(_dot(a, a))


def _unit(a):
    n = _norm(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n > 0 else (0.0, 0.0, 0.0)


def _angle(a, b):
    return math.acos(max(-1.0, min(1.0, _dot(a, b))))


def _solve3(rows, rhs):
    ''' The point x with rows[i] . x = rhs[i] (Cramer's rule), or None when the rows are not independent '''
    a, b, c = rows
    det = _dot(a, _cross(b, c))
    if abs(det) < 1e-9:
        return None
    # x = (rhs0 (b x c) + rhs1 (c x a) + rhs2 (a x b)) / det
    x = _add(_add(_mul(_cross(b, c), rhs[0]), _mul(_cross(c, a), rhs[1])), _mul(_cross(a, b), rhs[2]))
    return _mul(x, 1.0 / det)


def _directions(n):
    ''' n directions spread evenly over the sphere (a Fibonacci spiral) '''
    out = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(n):
        y = 1.0 - 2.0 * (i + 0.5) / n
        r = math.sqrt(max(0.0, 1.0 - y * y))
        t = golden * i
        out.append((math.cos(t) * r, y, math.sin(t) * r))
    return out


_DIRECTIONS = _directions(96)


class _Surface:
    ''' The value and the gradient of a field, which is all that is read of the body: `values(points)` is the field at many points; `size` the size of the body.
        The gradient is made of central differences, whose step follows the neighbourhood that is looked at (a twentieth of it), so that a close look at an edge is not
        smeared by a step that is wider than the look '''

    def __init__(self, values, size):
        self.values = values
        self.size = size
        self.step = 1e-3 * size

    def both(self, points):
        ''' [(value, gradient)] at the points, the gradient by central differences '''
        h = self.step
        batch = []
        for p in points:
            batch.append(p)
            for axis in range(3):
                for sign in (-1.0, 1.0):
                    q = list(p)
                    q[axis] += sign * h
                    batch.append(tuple(q))
        v = self.values(batch)
        out = []
        for i in range(len(points)):
            b = 7 * i
            g = tuple((v[b + 2 + 2 * a] - v[b + 1 + 2 * a]) / (2.0 * h) for a in range(3))
            out.append((v[b], g))
        return out

    def value(self, p):
        return self.values([p])[0]

    def project(self, points, steps=4):
        ''' The points carried onto the surface by Newton steps (down the gradient by value / |gradient|^2), as [(point, unit normal, distance left)]; None where
            the field has no gradient there '''
        cur = list(points)
        for _ in range(steps):
            nxt = []
            for p, (v, g) in zip(cur, self.both(cur)):
                gg = _dot(g, g)
                if gg < 1e-12 or v != v:
                    nxt.append(p)
                    continue
                k = v / gg
                nxt.append((p[0] - k * g[0], p[1] - k * g[1], p[2] - k * g[2]))
            cur = nxt
        out = []
        for p, (v, g) in zip(cur, self.both(cur)):
            n = _norm(g)
            out.append((p, (g[0] / n, g[1] / n, g[2] / n), v / n) if n > 1e-9 and v == v else None)
        return out

    def faces(self, center, r):
        ''' The faces of the surface within about r of a place: [(unit normal, mean point, samples)], the biggest first -- the surface is sampled round the place
            and the samples whose normals are within 25 degrees of one another are one face '''
        self.step = max(0.04 * r, 1e-5 * self.size)
        around = [(center[0] + r * d[0], center[1] + r * d[1], center[2] + r * d[2]) for d in _DIRECTIONS]
        samples = []
        for hit in self.project(around):
            if hit is not None and abs(hit[2]) < 0.1 * r and _norm(_sub(hit[0], center)) < 2.5 * r:
                samples.append((hit[0], hit[1]))
        clusters = []                       # [normal sum, [(point, normal) ...]]
        for p, n in samples:
            for c in clusters:
                if _angle(_unit(c[0]), n) < _SHARP:
                    c[0] = _add(c[0], n)
                    c[1].append((p, n))
                    break
            else:
                clusters.append([n, [(p, n)]])
        faces = []
        for total, members in clusters:
            # (the samples that lie within a step of the edge have a gradient that is smeared across it: they are the ones whose normals are far from the face's, and are left
            # out -- the face is what most of its samples say, a flat one exactly)
            mean = _unit(total)
            deviations = sorted(_angle(mean, n) for _, n in members)
            limit = max(math.radians(3.0), 1.5 * deviations[len(deviations) // 2])
            kept = [(p, n) for p, n in members if _angle(mean, n) <= limit]
            if len(kept) < 3:
                continue
            normal = _unit(tuple(sum(n[a] for _, n in kept) for a in range(3)))
            centre = tuple(sum(p[a] for p, _ in kept) / len(kept) for a in range(3))
            faces.append((normal, centre, len(kept)))
        faces.sort(key=lambda f: -f[2])
        return faces


def _candidate(faces, near):
    ''' The corner or the edge that the faces make, as ('corner', point, None) / ('edge', point, direction), or None.  The point is where the planes of the faces meet
        -- for an edge, the point of the line they make that is nearest `near` '''
    best = None
    top = faces[:6]
    for i in range(len(top)):
        for j in range(i + 1, len(top)):
            for k in range(j + 1, len(top)):
                a, b, c = top[i][0], top[j][0], top[k][0]
                det = abs(_dot(a, _cross(b, c)))
                if det >= _COPLANAR and (best is None or det > best[0]):
                    best = (det, (top[i], top[j], top[k]))
    if best is not None:
        x = _solve3([f[0] for f in best[1]], [_dot(f[0], f[1]) for f in best[1]])
        if x is not None:
            return ('corner', x, None)
    pair = None
    for i in range(len(top)):
        for j in range(i + 1, len(top)):
            s = _norm(_cross(top[i][0], top[j][0]))
            if s >= _PARALLEL and (pair is None or s > pair[0]):
                pair = (s, top[i], top[j])
    if pair is not None:
        _, fa, fb = pair
        d = _unit(_cross(fa[0], fb[0]))
        x = _solve3([fa[0], fb[0], d], [_dot(fa[0], fa[1]), _dot(fb[0], fb[1]), _dot(d, near)])
        if x is not None:
            return ('edge', x, d)
    return None


def _find(surface, near, r):
    ''' The nearest corner or edge to `near`, looked for within about r: {'kind', 'point', 'direction'} or None.  The place is worked out from the faces round `near`,
        then again from the faces round that place in a neighbourhood half as big, and so on, so that curved faces -- planes only to a first approximation -- are
        followed to the sharp feature itself; and it is a feature only if the faces are still there when the neighbourhood is a sixteenth of what it was (a smooth
        surface turns by less and less the closer it is looked at) '''
    first = surface.faces(near, r)
    cand = _candidate(first, near) if len(first) >= 2 else None
    if cand is None:
        return None
    point = cand[1]
    smallest = max(r / 16.0, 2e-4 * surface.size)
    radius = r
    for _ in range(5):
        if _norm(_sub(point, near)) > _REACH * r:
            return None
        radius = max(radius * 0.5, smallest)
        faces = surface.faces(point, radius)
        cand = _candidate(faces, point) if len(faces) >= 2 else None
        if cand is None:
            return None
        moved = _norm(_sub(cand[1], point))
        point = cand[1]
        if radius <= smallest and moved < 0.05 * smallest:
            break
    if _norm(_sub(point, near)) > _REACH * r:
        return None
    # still a jump at the smallest neighbourhood, and on the surface
    faces = surface.faces(point, smallest)
    check = _candidate(faces, point) if len(faces) >= 2 else None
    if check is None or abs(surface.value(point)) > 0.3 * smallest + 1e-4 * surface.size:
        return None
    if _norm(_sub(check[1], point)) <= 2.0 * smallest:
        point = check[1]
    return {'kind': check[0], 'point': point, 'direction': check[2]}


def find_feature(values, near, scale, size, kind=None):
    ''' The corner or edge nearest `near` that can be found (see the module), or None: {'kind': 'corner' | 'edge', 'point': (x, y, z), 'direction': unit vector or None,
        'straight': the two ends of the edge when it is a straight line, or None}.  `values(points)` is the field at many points (a list of floats), `scale` how many mm
        about thirty pixels span there (the neighbourhood that is looked in), `size` the size of the body.  `kind` ('corner' / 'edge') asks for that kind only '''
    surface = _Surface(values, size)
    r = max(min(scale, 0.2 * size), 2e-3 * size)
    near = tuple(float(c) for c in near)
    # (the place is carried onto the surface first: a click is near it, not on it)
    surface.step = max(0.04 * r, 1e-5 * size)
    start = surface.project([near])[0]
    if start is not None and abs(start[2]) < 2.0 * r:
        near = start[0]
    found = _find(surface, near, r)
    if kind == 'corner' and (found is None or found['kind'] != 'corner'):
        # (a corner a little further off than the neighbourhood: the click was not at the corner, but near it)
        found = _find(surface, near, 2.0 * r)
        if found is not None and found['kind'] != 'corner':
            found = None
    if found is None:
        return None
    if kind == 'edge' and found['kind'] == 'corner':
        # (a corner has edges: the edge that goes nearest the place is looked for again, away from the corner)
        found = _edge_of_corner(surface, near, r, found)
        if found is None:
            return None
    elif kind is not None and found['kind'] != kind:
        return None
    found['straight'] = _straight(surface, found) if found['kind'] == 'edge' else None
    return found


def _edge_of_corner(surface, near, r, corner):
    ''' The edge that runs nearest `near` out of the corner that was found there: the place is moved from the corner towards `near` until the faces round it are two '''
    c = corner['point']
    to_near = _sub(near, c)
    if _norm(to_near) < 1e-9:
        return None
    for fraction in (1.0, 0.7, 0.5, 0.35):
        probe = _add(c, _mul(to_near, fraction))
        found = _find(surface, probe, r * 0.5)
        if found is not None and found['kind'] == 'edge':
            return found
    return None


def _straight(surface, edge):
    ''' The two ends of the edge as a line: from the point along its direction both ways for as long as the surface is on the line (the field stays at zero along it,
        to within a thousandth of the way gone), each end then placed at the corner that closes it; None when that is less than 2 % of the body, or when a corner does not close
        each end -- the edge curves '''
    size = surface.size
    d = edge['direction']
    x = edge['point']
    step = size / 100.0
    surface.step = 1e-3 * size

    def off(t, sign):
        return abs(surface.value(_add(x, _mul(d, sign * t)))) > 1e-4 * size + 1e-3 * t

    ends = []
    closed = 0
    for sign in (1.0, -1.0):
        t, last = 0.0, 0.0
        gone = False
        for _ in range(400):
            t += step
            if off(t, sign):
                gone = True
                break
            last = t
        if not gone:
            ends.append(_add(x, _mul(d, sign * last)))
            closed += 1                 # (it runs on past the body: nothing curves)
            continue
        lo, hi = last, t
        for _ in range(14):
            mid = 0.5 * (lo + hi)
            if off(mid, sign):
                hi = mid
            else:
                lo = mid
        end = _add(x, _mul(d, sign * lo))
        # (the corner that closes the edge, if the faces there say there is one close to where it ended)
        reach = max(3.0 * step, 4.0 * (hi - lo))
        closing = _find(surface, end, reach)
        if closing is not None and closing['kind'] == 'corner' and _norm(_sub(closing['point'], end)) <= 1.5 * reach:
            end = closing['point']
            closed += 1
        ends.append(end)
    # A straight edge is closed by a corner at each end: the stretch where the surface stays on the line is not enough to tell it from a very big circle, whose stretch within the tolerance
    # is as long as a short edge (window t340: the rim of a cylinder was offered as a line)
    if len(ends) == 2 and closed == 2 and _norm(_sub(ends[0], ends[1])) >= _MIN_STRAIGHT * size:
        return (ends[0], ends[1])
    return None


def features_near(values, near, scale, size):
    ''' What the right-click menu offers at a place: {'corner': point or None, 'edge': {'point', 'straight': (p0, p1) or None} or None} -- the nearest corner and the nearest
        edge.  A place with neither (a face, a smooth surface: nearly every click) costs a look round the place and no more '''
    surface = _Surface(values, size)
    r = max(min(scale, 0.2 * size), 2e-3 * size)
    near = tuple(float(c) for c in near)
    surface.step = max(0.04 * r, 1e-5 * size)
    start = surface.project([near])[0]
    if start is not None and abs(start[2]) < 2.0 * r:
        near = start[0]
    found = _find(surface, near, r)
    if found is None:
        return {'corner': None, 'edge': None}
    corner = edge = None
    if found['kind'] == 'corner':
        corner = found['point']
        edge = _edge_of_corner(surface, near, r, found)
    else:
        edge = found
        # (a corner a little further off than the neighbourhood: the click was near an edge, and the corner at its end is near too)
        wider = _find(surface, near, 2.0 * r)
        if wider is not None and wider['kind'] == 'corner':
            corner = wider['point']
    if edge is not None:
        edge['straight'] = _straight(surface, edge)
    return {'corner': corner, 'edge': {'point': edge['point'], 'straight': edge['straight']} if edge else None}


# ---------------------------------------------------------------------------------------------------------------------------------
# The functions of a script

def _clean(p):
    ''' A place without the noise of the last digits (and without a minus sign on a zero): what is written and read back in a script '''
    return tuple(round(float(c), 6) + 0.0 for c in p)


def _prepare(shape, near, scale, bounds, name):
    from fieldes.stdlib.fields import _s, _shape_bounds, _eval_many
    shape = _s(shape)
    near = tuple(float(c) for c in near)
    if len(near) != 3:
        raise ValueError('%s: near is a place (x, y, z)' % name)
    lo, hi = _shape_bounds(shape, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    if scale is None:
        scale = 0.03 * size             # (about the neighbourhood of a click on a body that fills the view)
    return near, float(scale), size, (lambda points: _eval_many(shape, points))


def corner_at(shape, near, scale=None, bounds=None):
    ''' The corner of `shape` nearest `near`: a point, where three faces of the surface meet at sharp angles.

        near    a place on or near the surface (x, y, z) -- where the corner is looked for
        scale   how far round `near` is looked, in mm (default: 3 % of the body)
        bounds  ((x0, y0, z0), (x1, y1, z1)) of the shape, if it cannot be found

        Raises ValueError when there is no corner round `near`.  See the module. '''
    from fieldes.stdlib.points import point
    near, scale, size, values = _prepare(shape, near, scale, bounds, 'corner_at')
    found = find_feature(values, near, scale, size, 'corner')
    if found is None:
        raise ValueError('corner_at: no corner of the body near (%.4g, %.4g, %.4g): three faces that meet at sharp angles within %.3g mm of it' % (near + (scale,)))
    return point(*_clean(found['point']))


def edge_point_at(shape, near, scale=None, bounds=None):
    ''' The point of an edge of `shape` nearest `near`: a point on the line where two faces meet at a sharp angle -- any edge, straight or curved (the rim of a
        cylinder too).  Where a corner is nearer, the corner.  Raises ValueError when there is no edge round `near`.  See the module and `corner_at`. '''
    from fieldes.stdlib.points import point
    near, scale, size, values = _prepare(shape, near, scale, bounds, 'edge_point_at')
    found = find_feature(values, near, scale, size)
    if found is None:
        raise ValueError('edge_point_at: no edge of the body near (%.4g, %.4g, %.4g): two faces that meet at a sharp angle within %.3g mm of it' % (near + (scale,)))
    return point(*_clean(found['point']))


def edge_at(shape, near, scale=None, bounds=None):
    ''' The straight edge of `shape` nearest `near`: a polyline of two points, from the corner at one end of the edge to the corner at the other.  Raises ValueError
        when there is no edge round `near`, or when it curves (a circle is not a line: `edge_point_at` gives a point of it).  See the module and `corner_at`. '''
    from fieldes.stdlib.points import point
    from fieldes.stdlib.curves import polyline
    near, scale, size, values = _prepare(shape, near, scale, bounds, 'edge_at')
    found = find_feature(values, near, scale, size, 'edge')
    if found is None:
        raise ValueError('edge_at: no edge of the body near (%.4g, %.4g, %.4g): two faces that meet at a sharp angle within %.3g mm of it' % (near + (scale,)))
    if found['straight'] is None:
        raise ValueError('edge_at: the edge near (%.4g, %.4g, %.4g) curves, so it is not a line: edge_point_at gives a point of it' % near)
    a, b = found['straight']
    return polyline([point(*_clean(a)), point(*_clean(b))])


# (a line like the one a Curve is: it has no gizmo() line with numbers of its own -- its points have them -- and a gizmo mode only.  The model tree decides this from what the call is
# when the entry that made the line selects it, before the run that says what the line is has been answered: without these two the tree wrote the gizmo with numbers, which hid the
# line (windows t347 and t351: a line along a box edge, made by the menu, was not drawn))
edge_at._makes_no_gizmo = True
edge_at._makes_outline_gizmo = True
