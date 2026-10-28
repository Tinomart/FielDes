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

Three more points to snap to:

    middle = circle_center_at(part, (10.2, 0.1, 19.8))     # displayed: the centre of the circular edge nearest that place (the rim of a cylinder or a hole, the base of a cone)
    corner = corner_at(rounded_part, (10, 10, 20), rounded=True)    # the corner that the rounding of three edges replaced: where the planes of the flat faces round it meet
    on_edge = edge_point_at(rounded_part, (10, 0, 19.8), rounded=True)   # a point of the sharp edge that a rounding replaced
    centre = face_center(select_surface(part, (5, 5, 0)))  # the middle of a face (a selection of surface): the mean of the places it was sampled at

A rounded edge has no sharp edge: it turns by a little all along the rounding.  With `rounded=True` the faces on both sides are looked at from further and further away -- past
the rounding, onto the flat faces -- and the point is where their planes meet, once two looks at different distances agree on it.

What comes back are the ordinary things: a `point` (so it is a corner of a profile, the end of a `polyline`, a place for a gizmo) and a `polyline`
of two points.  They are computed when the script runs, from `near` and the body: the body can be changed and they follow, as long as the edge or
corner is still near that place.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

__all__ = ['corner_at', 'edge_at', 'edge_point_at', 'circle_center_at', 'face_center']

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
        The gradient is made of forward differences, whose step follows the neighbourhood that is looked at (a twentieth of it), so that a close look at an edge is not
        smeared by a step that is wider than the look '''

    def __init__(self, values, size):
        self.values = values
        self.size = size
        self.step = 1e-3 * size

    def both(self, points):
        ''' [(value, gradient)] at the points, the gradient by central differences '''
        # (a forward difference: four reads of the field for a point, not seven -- the field of an imported part costs some 45 microseconds a read, and the search is made of such reads (window t2,
        # 2026-10-28: 3.6 s for a click near a corner of the bracket, 96 % of it in the kernel).  Exact on a flat face, and on a curved one a bias of h / 2 R of a direction, far below what the faces
        # are told apart by)
        h = self.step
        batch = []
        for p in points:
            batch.append(p)
            for axis in range(3):
                q = list(p)
                q[axis] += h
                batch.append(tuple(q))
        v = self.values(batch)
        out = []
        for i in range(len(points)):
            b = 4 * i
            g = tuple((v[b + 1 + a] - v[b]) / h for a in range(3))
            out.append((v[b], g))
        return out

    def value(self, p):
        return self.values([p])[0]

    def project(self, points, steps=4):
        ''' The points carried onto the surface by Newton steps (down the gradient by value / |gradient|^2), as [(point, unit normal, distance left)]; None where
            the field has no gradient there '''
        cur = list(points)
        read = self.both(cur)
        done = 1e-3 * self.step                 # (a point is on the surface when it is that near it: the steps stop when every point is -- a flat face is reached by the first)
        for _ in range(steps):
            nxt = []
            moved = False
            for p, (v, g) in zip(cur, read):
                gg = _dot(g, g)
                if gg < 1e-12 or v != v:
                    nxt.append(p)
                    continue
                if abs(v) > done * math.sqrt(gg):
                    moved = True
                k = v / gg
                nxt.append((p[0] - k * g[0], p[1] - k * g[1], p[2] - k * g[2]))
            if not moved:
                break
            cur = nxt
            read = self.both(cur)
        out = []
        for p, (v, g) in zip(cur, read):
            n = _norm(g)
            out.append((p, (g[0] / n, g[1] / n, g[2] / n), v / n) if n > 1e-9 and v == v else None)
        return out

    def faces(self, center, r, flat=False):
        ''' The faces of the surface within about r of a place: [(unit normal, mean point, samples)], the biggest first -- the surface is sampled round the place
            and the samples whose normals are within 25 degrees of one another are one face.  With flat=True only flat faces are found: the samples whose normals are within 4
            degrees of one another are one, and the face is what lies within 2 degrees of its mean normal -- a rounding is not taken for part of the face next to it '''
        self.step = max(0.04 * r, 1e-5 * self.size)
        around = [(center[0] + r * d[0], center[1] + r * d[1], center[2] + r * d[2]) for d in _DIRECTIONS]
        samples = []
        for hit in self.project(around):
            if hit is not None and abs(hit[2]) < 0.1 * r and _norm(_sub(hit[0], center)) < 2.5 * r:
                samples.append((hit[0], hit[1]))
        clusters = []                       # [normal sum, [(point, normal) ...]]
        together = math.radians(4.0) if flat else _SHARP
        for p, n in samples:
            for c in clusters:
                if _angle(_unit(c[0]), n) < together:
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
            limit = math.radians(2.0) if flat else max(math.radians(3.0), 1.5 * deviations[len(deviations) // 2])
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
        radius = max(radius * 0.25, smallest)               # (a quarter each time, not a half: two steps from r to a sixteenth of it, not four -- the faces are looked for at one place less)
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
        # (the steps are read from the field in chunks, a few at first and more as the edge goes on: one call of the kernel for a chunk, not one for every step)
        walked, chunk = 0, 8
        while walked < 400 and not gone:
            count = min(chunk, 400 - walked)
            ts = []
            at = t
            for _ in range(count):
                at += step
                ts.append(at)
            for s, v in zip(ts, surface.values([_add(x, _mul(d, sign * s)) for s in ts])):
                t = s
                if abs(v) > 1e-4 * size + 1e-3 * s:
                    gone = True
                    break
                last = s
            walked += count
            chunk = min(2 * chunk, 64)
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
        # (no sharp corner or edge: a rounding, if there is one, has a corner or an edge that was replaced)
        rounded = {}
        for kind in ('corner', 'edge'):
            found_point = _find_rounded(surface, near, r, kind)
            if found_point is not None:
                rounded[kind] = found_point
                if kind == 'corner':
                    break
        return {'corner': None, 'edge': None, 'circle': None, 'rounded': rounded or None}
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
    circle = None
    if edge is not None:
        edge['straight'] = _straight(surface, edge)
        if edge['straight'] is None:
            found_circle = _find_circle(surface, near, r)
            if found_circle is not None:
                circle = {'center': found_circle[0], 'radius': found_circle[1]}
    return {'corner': corner, 'edge': {'point': edge['point'], 'straight': edge['straight']} if edge else None, 'circle': circle, 'rounded': None}


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


def _circle_through(a, b, c):
    ''' (centre, radius, unit normal) of the circle through three points, or None when they are on a line '''
    u, v = _sub(a, c), _sub(b, c)
    w = _cross(u, v)
    w2 = _dot(w, w)
    if w2 < 1e-18:
        return None
    # (the centre is c + ((|u|^2 v - |v|^2 u) x w) / (2 |w|^2))
    t = _sub(_mul(v, _dot(u, u)), _mul(u, _dot(v, v)))
    centre = _add(c, _mul(_cross(t, w), 1.0 / (2.0 * w2)))
    return centre, _norm(_sub(a, centre)), _unit(w)


def _find_circle(surface, near, r):
    ''' The circular edge nearest `near`: (centre, radius, unit normal), or None.  The edge found there gives a point and the way it runs; two points a little way along it, put
        onto the edge, give a first circle; the circle is then read again from the edge at wider and wider angles round it, up to 100 degrees either side, which fixes it to the
        accuracy of the points, and a point on the far side must be on it '''
    first = _find(surface, near, r)
    if first is None or first['kind'] != 'edge' or first['direction'] is None:
        return None
    p0, t0 = first['point'], first['direction']
    size = surface.size

    seen = [p0]

    def edge_near(q):
        # (a point the faces round a place say is a corner is a point of the edge too, where the edge is a circle: the samples on the curved side of a cone or a cylinder make a third
        # face now and then, and three faces make a corner)
        found = _find(surface, q, r)
        if found is None or _norm(_sub(found['point'], q)) > _REACH * r:
            return None
        seen.append(found['point'])
        return found['point']

    circle = None
    step = r
    for _ in range(3):
        pa = edge_near(_add(p0, _mul(t0, step)))
        pb = edge_near(_sub(p0, _mul(t0, step)))
        if pa is not None and pb is not None:
            circle = _circle_through(pa, p0, pb)
            if circle is not None and circle[1] < 1e3 * size and circle[1] > 0.2 * step:
                break
            circle = None
        step *= 2.0
    if circle is None:
        return None
    centre, radius, normal = circle
    theta = step / radius
    for _ in range(8):
        theta = min(theta * 2.0, math.radians(100.0))
        u = _unit(_sub(p0, centre))
        v = _cross(normal, u)

        def at(angle):
            return edge_near(_add(centre, _add(_mul(u, radius * math.cos(angle)), _mul(v, radius * math.sin(angle)))))

        pa, pb = at(theta), at(-theta)
        if pa is None or pb is None:
            return None
        circle = _circle_through(pa, p0, pb)
        if circle is None:
            return None
        centre, radius, normal = circle
        if theta >= math.radians(100.0) - 1e-9:
            break
    u = _unit(_sub(p0, centre))
    far = edge_near(_sub(centre, _mul(u, radius)))
    if far is None:
        return None
    # (every point that was found is on the circle, within a fiftieth of its radius and a thousandth of the body)
    tolerance = 0.02 * radius + 1e-3 * size
    if any(abs(_norm(_sub(p, centre)) - radius) > tolerance or abs(_dot(_sub(p, centre), normal)) > tolerance for p in seen):
        return None
    return centre, radius, normal


def _find_rounded(surface, near, r, kind):
    ''' The corner or the edge that a rounding replaced, near `near`: the point where the planes of the flat faces round it meet.  The faces are looked at within 2 r, then 2.8 r,
        3.9 r ... of the place, up to 0.4 of the body: the point is taken when two looks at different distances agree on it (to within a fiftieth of the distance).  A place that
        has one face within 2 r has no rounding, and none is looked for '''
    size = surface.size
    radius = 2.0 * r
    previous = None
    first = True
    while radius <= 0.4 * size:
        if first and len(surface.faces(near, radius)) < 2:
            return None
        first = False
        faces = surface.faces(near, radius, flat=True)
        cand = _candidate(faces, near) if len(faces) >= 2 else None
        if cand is not None and cand[0] == kind and _norm(_sub(cand[1], near)) <= 1.5 * radius:
            if previous is not None and _norm(_sub(cand[1], previous)) <= 0.02 * radius:
                return cand[1]
            previous = cand[1]
        else:
            previous = None
        radius *= 1.4
    return None


def _wider(find, scale, size, given):
    ''' `find(scale)` with the neighbourhood three times as wide each time it finds nothing, up to 0.4 of the body -- when the caller did not ask for a neighbourhood of its own (`given`): a place
        written in a call is where a feature was when it was made, and a face of the body that was pulled by its handle has carried the feature away from it, so the feature nearest the place is
        found again where it went (the user, 2026-10-28: "When handle dragging a part, where you added a point, it throws an error. Most optimally the point should move together with the drag").
        A neighbourhood that was asked for is looked at as asked '''
    found = find(scale)
    if found is not None or given:
        return found
    reach = scale
    while reach < 0.4 * size:
        reach = min(reach * 3.0, 0.4 * size)
        found = find(reach)
        if found is not None:
            return found
    return None


def _found_on(out, shape):
    ''' A feature that is found on a body is locked to it: it is where the faces of the body meet, so it is not something to drag -- no gizmo in the model tree, no handles, nothing that takes it off its
        corner by hand (the user, 2026-10-28: "you should not be able to grab a point and move it around, when it is locked to a corner. Same goes for the edge").  `_on` is the body it is on: what a snap
        -- the S key -- sorts the features by '''
    try:
        out._on = shape
        out._locked = True
        out._no_gizmo = True
    except AttributeError:
        pass
    return out


def _stays(shape, near, name):
    ''' The point for a feature that is not there any more, not even in the widest neighbourhood: the place the call writes, with a note in the console -- an error here would stop the whole script
        (and every model built on the point) for as long as a drag goes on '''
    from fieldes.stdlib.points import point
    print('%s: no feature of the body near (%.4g, %.4g, %.4g), not even in a wide neighbourhood: the point stays at that place' % ((name,) + tuple(near)))
    return _found_on(point(*_clean(near)), shape)


def _script_frame():
    ''' The frame of what makes the call of a feature -- the script, or a block of the user's: the first frame that is not the library's '''
    import os
    import sys
    try:
        library = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        frame = sys._getframe(0)
        while frame is not None and os.path.abspath(frame.f_code.co_filename).startswith(library):
            frame = frame.f_back
        return frame
    except Exception:
        return None


def _place_text(v):
    s = ('%.6f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('', '-0') else s


_LAST_WRITES = []


def _write_place(name, old, new, reach):
    ''' The place a call writes is where the feature was when the call was made.  A feature that is more than `reach` from it -- a face of the body was pulled by its handle and took it along -- is
        written again as the place it is at now, by the editor when the run is over (host.__resync, as the numbers of a dragged shape are): so the script says where the feature is, whatever is added
        above the call, and a file that is saved and opened again finds it where it is (the user, 2026-10-28: "when I added an edge at the same side, it literally broke the point to not be at the corner
        anymore").  Written only where the call stands in the script as `name(body, (x, y, z))` with three plain numbers, once for every change (what an undo took back is not written again), and
        never where there is no editor to say it to '''
    if _norm(_sub(new, old)) <= reach:
        return
    try:
        import ast
        import itertools
        import _fieldes_host as host
        from fieldes import runner
        from fieldes.stdlib.snapping import _number_node
    except ImportError:
        return
    try:
        resync = getattr(host, '__resync', None)
        source = getattr(runner, 'running_source', None)
        frame = _script_frame()
        if resync is None or not source or frame is None or frame.f_code.co_filename != '<file>':
            return
        position = tuple(next(itertools.islice(frame.f_code.co_positions(), frame.f_lasti // 2, None)))
        call = None
        for node in ast.walk(ast.parse(source)):
            if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == name
                    and (node.lineno, node.end_lineno, node.col_offset, node.end_col_offset) == position):
                call = node
                break
        if call is None:
            return
        place = call.args[1] if len(call.args) > 1 else next((k.value for k in call.keywords if k.arg == 'near'), None)
        if not isinstance(place, (ast.Tuple, ast.List)) or len(place.elts) != 3:
            return
        read = [_number_node(e) for e in place.elts]
        if any(r is None for r in read) or any(e.lineno != e.end_lineno for e in place.elts):
            return
        signature = (name,) + tuple(round(float(c), 5) for c in old) + tuple(round(float(c), 5) for c in new)
        if signature in _LAST_WRITES:
            return
        lines = source.split('\n')

        def col(line, byte_col):
            return len(lines[line].encode('utf-8')[:byte_col].decode('utf-8', 'ignore'))

        edits = []
        for e, r, v in zip(place.elts, read, new):
            text = _place_text(v)
            edits.append((e.lineno - 1, col(e.lineno - 1, e.col_offset), e.end_lineno - 1, col(e.end_lineno - 1, e.end_col_offset), 'var(%s)' % text if r[1] else text))
        if any(tuple(e[:4]) == tuple(q[:4]) for e in edits for q in resync):
            return                          # (the call has said it already in this run: a loop, a function)
        _LAST_WRITES.append(signature)
        del _LAST_WRITES[:-16]
        resync.extend(edits)
    except Exception:
        return


# A feature that was made on a body is where the body WAS when it was made: the place written in the call is a place of the world, and a body that is moved, turned or pulled by a handle has left it.
# So each run remembers where the feature was found and how its body was placed, and the next run carries the feature by the change of the placement -- exactly, however far the body went -- and looks for it
# there: a face that was pulled has moved it only a little from that place, and the nearest corner, edge or circle is found there (the user, 2026-10-28: "the point is not displaced correctly together with the
# body").  The memory is of this run of the program and is kept by the call (where it stands in the script, and the place it writes): a call that is edited, or that the S key gives a new place, is a new one and
# starts from what it writes, so nothing is moved twice
_followed = {}


def _matrix_of(shape):
    ''' (A, t): how this run places the body, p -> A p + t -- position, rotation, scale and origin as `place` does it, c + R S (p - c) + m (see placement._apply) -- or the identity for a body that has no
        placement arguments '''
    ident = (((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), (0.0, 0.0, 0.0))
    try:
        from fieldes.stdlib.curves import value_of
        from fieldes.stdlib.snapping import _numbers
        from fieldes.stdlib.symmetries import _turn_matrix
        args = getattr(shape, '_placement_args', None)
        parts = getattr(shape, '_placed_parts', None)
        if args is None or parts is None:
            return ident
        m, r = _numbers(args[0], (0.0, 0.0, 0.0)), _numbers(args[1], (0.0, 0.0, 0.0))
        s = args[2]
        s = (1.0, 1.0, 1.0) if s is None else (tuple(float(value_of(s)) for _ in range(3)) if not isinstance(s, (tuple, list)) else _numbers(s, (1.0, 1.0, 1.0)))
        c = tuple(float(v) for v in parts[1])
        R = [[float(v) for v in row] for row in _turn_matrix(r)]
        A = tuple(tuple(R[i][j] * float(s[j]) for j in range(3)) for i in range(3))
        t = tuple(c[i] + float(m[i]) - sum(A[i][j] * c[j] for j in range(3)) for i in range(3))
        return A, t
    except Exception:
        return ident


def _call_key(name, near):
    ''' What a call is, from one run to the next: its function, where it stands in the script and the place it writes '''
    try:
        # (the first frame that is not the library's: the script -- or a block of the user's -- that makes the call, whatever the library puts between, a wrapper that places the result, say)
        frame = _script_frame()
        where = (frame.f_code.co_filename, frame.f_lineno) if frame is not None else ('', 0)
    except Exception:
        where = ('', 0)
    return (name, where, tuple(round(float(c), 6) for c in near))


def _start(key, shape, near):
    ''' (how the body is placed now, where to look for the feature, whether that is a remembered place carried by the change of the placement): the written place for a call that has no memory '''
    matrix = _matrix_of(shape)
    old = _followed.get(key) if key is not None else None
    if old is None:
        return matrix, near, False
    A0, t0 = old['matrix']
    found = old['found']
    # (where the numbers that were changed since -- a face pulled by its handle -- have taken the feature, by the terms the run before made of it: the search goes on from there, not from where it was)
    live = old.get('live')
    if live and len(live) == len(found):
        found = [_advanced(p, t, old.get('count')) for p, t in zip(found, live)]
    centre = tuple(sum(p[i] for p in found) / len(found) for i in range(3))
    local = _solve3(A0, tuple(centre[i] - t0[i] for i in range(3)))           # (the place in the body's own frame, as the run before placed it)
    if local is None:
        return matrix, near, False
    A, t = matrix
    return matrix, tuple(sum(A[i][j] * local[j] for j in range(3)) + t[i] for i in range(3)), True


def _remember(key, matrix, points, planes=None, lives=None, shape=None):
    ''' Where the feature of this call was found (one point, or the two ends of a line), how its body was placed, and the faces it was made of ([(unit normal, a point inside the face)]: three for
        a corner, two for an edge), for the next run -- and how each point moves with the numbers of the script (`lives`, one list of terms for each point: see `_live_terms`) '''
    if key is None:
        return
    if len(_followed) > 4000:
        _followed.clear()
    live, count = None, None
    if lives is not None and len(lives) == len(points):
        live = []
        for terms in lives:
            indexed, count = _indexed_terms(shape, terms)
            live.append(indexed)
    _followed[key] = {'matrix': matrix, 'found': [tuple(float(c) for c in p) for p in points], 'planes': planes, 'live': live, 'count': count}


def _indexed_terms(shape, terms):
    ''' The terms as the run after this one can read them -- the var() objects are new then: each number by its place in the list of the numbers of the script -- without the numbers of the body's
    placement, whose change the carrying of the feature by the placement takes care of: ([(place, (dx, dy, dz) for one unit, value then)], how many numbers the script has) '''
    try:
        import _fieldes_host as host
        entries = list(getattr(host, '__vars', None) or [])
    except ImportError:
        return [], 0
    placed = _placement_vars(shape)
    out = []
    for var, move, value in terms or []:
        if id(var) in placed:
            continue
        for i, e in enumerate(entries):
            if e[0] is var:
                out.append((i, move, value))
                break
    return out, len(entries)


def _advanced(place, indexed, count):
    ''' `place` moved by the terms of the run before, to where the numbers of the script are now (the place it was left at when there are not the same numbers any more) '''
    try:
        import _fieldes_host as host
        entries = list(getattr(host, '__vars', None) or [])
    except ImportError:
        return place
    if not indexed or count is None or len(entries) != count:
        return place
    out = list(place)
    try:
        for i, move, value in indexed:
            now = float(entries[i][1])
            for a in range(3):
                out[a] += move[a] * (now - value)
    except (IndexError, TypeError, ValueError):
        return place
    return tuple(out)


def _carry_vector(old_matrix, matrix, v):
    ''' A direction as the body was placed in the run before, put by this run's placement (the linear part of the change) '''
    local = _solve3(old_matrix[0], v)
    if local is None:
        return None
    A = matrix[0]
    return tuple(sum(A[i][j] * local[j] for j in range(3)) for i in range(3))


def _carry_point(old_matrix, matrix, p):
    ''' A place as the body was placed in the run before, put by this run's placement '''
    A0, t0 = old_matrix
    local = _solve3(A0, tuple(p[i] - t0[i] for i in range(3)))
    if local is None:
        return None
    A, t = matrix
    return tuple(sum(A[i][j] * local[j] for j in range(3)) + t[i] for i in range(3))


def _plane_set(surface, point, r, count):
    ''' The faces that meet at `point`, as [(unit normal, a point inside the face near it)]: three for a corner, two for an edge -- the biggest that are independent, as _candidate takes them -- or None '''
    top = surface.faces(point, r)[:6]
    if count == 3:
        best = None
        for i in range(len(top)):
            for j in range(i + 1, len(top)):
                for k in range(j + 1, len(top)):
                    det = abs(_dot(top[i][0], _cross(top[j][0], top[k][0])))
                    if det >= _COPLANAR and (best is None or det > best[0]):
                        best = (det, (i, j, k))
        # (ALL the faces round the corner, not three: a corner where four faces meet -- the tip of a pyramid -- has no three that stay a corner when one of the four is pulled: the others
        # make two corners, and the one that is nearest where it was is the corner then.  The user, 2026-10-28: "dragging the sides of a pyramid shape sometimes teleport the point at the corner
        # to a different corner of the shape")
        return None if best is None else [(f[0], f[1]) for f in top]
    pair = None
    for i in range(len(top)):
        for j in range(i + 1, len(top)):
            s = _norm(_cross(top[i][0], top[j][0]))
            if s >= _PARALLEL and (pair is None or s > pair[0]):
                pair = (s, i, j)
    return None if pair is None else [(top[pair[1]][0], top[pair[1]][1]), (top[pair[2]][0], top[pair[2]][1])]


def _push(surface, values, c, n, size):
    ''' Where the face through `c` whose outward normal is `n` is now: the place nearest `c` where the field crosses zero, going outward, on the line c + t n -- within half the body, and when there is
        none, within twice the body: a face that was pulled by its handle all the way across (the user, 2026-10-28, a pyramid half a millimetre wide whose side was pulled by more than a millimetre) --
        (the place, the unit normal of the surface there), or None.  A face that was pulled by its handle is found at its new place, one that was not where it was '''
    for reach in (0.5, 2.0):
        hit = _push_within(surface, values, c, n, size, reach)
        if hit is not None:
            return hit
    return None


def _push_within(surface, values, c, n, size, reach):
    steps = 160
    h = reach * size / steps
    ts = [h * k for k in range(-steps, steps + 1)]
    v = values([(c[0] + t * n[0], c[1] + t * n[1], c[2] + t * n[2]) for t in ts])
    best = None
    for k in range(len(ts) - 1):
        if v[k] <= 0.0 < v[k + 1]:
            at = min(abs(ts[k]), abs(ts[k + 1]))
            if best is None or at < best[0]:
                best = (at, ts[k], ts[k + 1])
    if best is None:
        return None
    lo, hi = best[1], best[2]
    for _ in range(40):
        mid = 0.5 * (lo + hi)
        f = values([(c[0] + mid * n[0], c[1] + mid * n[1], c[2] + mid * n[2])])[0]
        if f <= 0.0:
            lo = mid
        else:
            hi = mid
    t = 0.5 * (lo + hi)
    q = (c[0] + t * n[0], c[1] + t * n[1], c[2] + t * n[2])
    g = surface.both([q])[0][1]
    normal = _unit(g) if _norm(g) > 1e-9 else n
    return q, normal


def _follow_planes(surface, values, old, matrix, size, count, start):
    ''' The feature of the run before found again on the SAME faces: each face it was made of is carried by the change of the placement and pushed along its normal to where the surface is now, and the
        corner (three faces) or the point of the edge (two faces, nearest `start` on the line they make) is where those faces meet -- {'point', 'direction' (an edge), 'planes'}, or None when a face
        cannot be found, has turned by more than 25 degrees, or the place is not on the surface '''
    planes = old.get('planes')
    if not planes or len(planes) < count or (count == 2 and len(planes) != 2):
        return None
    now = []
    for n, c in planes:
        n2, c2 = _carry_vector(old['matrix'], matrix, n), _carry_point(old['matrix'], matrix, c)
        # (a face that cannot be found again is left out, when the corner has faces to spare: of the four faces at the tip of a pyramid three make a corner)
        if n2 is None or c2 is None or _norm(n2) < 1e-9:
            if count == 2:
                return None
            continue
        n2 = _unit(n2)
        hit = _push(surface, values, c2, n2, size)
        if hit is None:
            if count == 2:
                return None
            continue
        q, normal = hit
        if _angle(normal, n2) > math.radians(25.0):
            if count == 2:
                return None
            continue
        now.append((normal, q))
    if len(now) < count:
        return None
    direction = None
    if count == 3:
        # (every three of the faces that are independent meet at a place: the places that are on the surface are the corners the faces make now, and the one nearest where the corner was is it)
        found = []
        for i in range(len(now)):
            for j in range(i + 1, len(now)):
                for k in range(j + 1, len(now)):
                    if abs(_dot(now[i][0], _cross(now[j][0], now[k][0]))) < _COPLANAR:
                        continue
                    place = _solve3([now[i][0], now[j][0], now[k][0]], [_dot(now[n][0], now[n][1]) for n in (i, j, k)])
                    if place is not None:
                        found.append(place)
        if not found:
            return None
        on = surface.values(found)
        best = None
        for place, v in zip(found, on):
            if abs(v) <= 2e-3 * size and (best is None or _norm(_sub(place, start)) < _norm(_sub(best, start))):
                best = place
        if best is None:
            return None
        return {'point': best, 'direction': None, 'planes': now}
    else:
        d = _cross(now[0][0], now[1][0])
        if _norm(d) < _PARALLEL:
            return None
        direction = _unit(d)
        x = _solve3([now[0][0], now[1][0], direction], [_dot(now[0][0], now[0][1]), _dot(now[1][0], now[1][1]), _dot(direction, start)])
    if x is None or abs(surface.value(x)) > 2e-3 * size:
        return None
    return {'point': x, 'direction': direction, 'planes': now}


def _placement_vars(shape, parts=(0, 1, 2)):
    ''' The numbers (var()) of the placement of a body: its position (part 0), rotation (1) and scale (2) '''
    out = set()
    try:
        args = getattr(shape, '_placement_args', None)
        if args is not None:
            for part in (args[i] for i in parts):
                for item in (part if isinstance(part, (tuple, list)) else (part,)):
                    out.add(id(item))
    except Exception:
        pass
    return out


def _turning_vars(shape):
    ''' The numbers (var()) that turn or scale the body as a whole -- the rotation and the scale of its placement: with them the faces turn, and a place that is made of them does not follow
        in a straight line '''
    return _placement_vars(shape, (1, 2))


def _live_note(text):
    ''' A line on stderr for the timing log (FIELDES_TIMING): what the live terms of a feature came to '''
    import os
    if os.environ.get('FIELDES_TIMING'):
        try:
            os.write(2, ('[live] ' + text + '\n').encode('utf-8', 'replace'))
        except OSError:
            pass


_LIVE_STEP = 1e-3               # how far a number is moved, to see where the feature goes with it (a thousandth of it, at least a thousandth)
_LIVE_NUMBERS = 64              # the numbers of the script that are looked at (the first ones: the cost grows with them)


def _meeting(surface, planes, count, at):
    ''' Of the faces found round a place, the `count` that meet AT it -- the planes it lies on -- the most independent of them: a corner where three faces meet is found with a fourth face of the
        neighbourhood as well, and the live terms are made of the three (None when fewer meet there) '''
    if not planes or at is None:
        return planes
    on = [p for p in planes if abs(_dot(p[0], _sub(at, p[1]))) <= 2e-3 * surface.size]
    if len(on) < count:
        return None
    best = None
    if count == 3:
        for i in range(len(on)):
            for j in range(i + 1, len(on)):
                for k in range(j + 1, len(on)):
                    det = abs(_dot(on[i][0], _cross(on[j][0], on[k][0])))
                    if best is None or det > best[0]:
                        best = (det, [on[i], on[j], on[k]])
    else:
        for i in range(len(on)):
            for j in range(i + 1, len(on)):
                s = _norm(_cross(on[i][0], on[j][0]))
                if best is None or s > best[0]:
                    best = (s, [on[i], on[j]])
    return best[1] if best is not None else None


def _live_terms(shape, surface, planes, count, direction=None, at=None):
    ''' How the place of a feature -- the corner of three faces (`count` 3), the point of an edge of two faces nearest where it was (`count` 2, `direction` the way the edge runs) -- moves with each
        number of the script, to first order: [(the number (the var of the script), (dx, dy, dz) for one unit of it, its value now)], or None.  Each face is a plane through the place that was found
        on it, and a number that moves a face moves it by the change of the field there over its gradient; the corner is where the moved planes meet.  The faces of a pyramid, a box, a prism move
        so with their handles, exactly.  A number that turns a face -- its direction is not what it was -- is left out: the place does not move in a straight line with it.  The viewport adds the
        terms to the place while a surface is pulled, so that the point follows the drag in real time (the user, 2026-10-28: "the points and edges follow the drag and move in real time"); the script
        runs once the mouse is let go, and says where the feature is for good '''
    try:
        import _fieldes_host as host
    except ImportError:
        return None
    entries = getattr(host, '__vars', None)
    retune = getattr(host, '__retune', None)
    planes = _meeting(surface, planes, count, at)
    if not entries or retune is None or not planes or len(planes) != count or (count == 2 and direction is None):
        _live_note('no terms: %d numbers in the script, retune %s, %s faces (%d wanted)' % (len(entries or []), 'there' if retune is not None else 'missing', len(planes) if planes else 0, count))
        return None
    try:
        turning = _turning_vars(shape)
        normals = [p[0] for p in planes]
        centres = [p[1] for p in planes]
        base = surface.both(centres)
        lengths = [_norm(g) for _v, g in base]
        if min(lengths) < 1e-9:
            return None
        rows = list(normals) + ([direction] if count == 2 else [])
        terms = []
        for e in list(entries)[:_LIVE_NUMBERS]:
            var = e[0]
            value = float(e[1])
            if id(var) in turning:
                continue
            step = _LIVE_STEP * max(1.0, abs(value))
            retune(var, value + step)
            try:
                moved = surface.both(centres)
            finally:
                retune(var, value)
            # (the gradient is a forward difference of float32 values: its direction is good to about 0.006 radians, so a face that is turned is one that turns by more than that)
            turned = max(_angle(b[1], m[1]) for b, m in zip(base, moved))
            outward = [-(m[0] - b[0]) / n for b, m, n in zip(base, moved, lengths)]
            if turned > 0.03:
                continue                            # (a face is turned by this number)
            if max(abs(x) for x in outward) < 1e-6 * step:
                continue                            # (no face of the feature moves with it)
            move = _solve3(rows, outward + ([0.0] if count == 2 else []))
            if move is None:
                return None
            terms.append((var, tuple(c / step for c in move), value))
        _live_note('%d terms of %d numbers: %s' % (len(terms), len(entries), ', '.join('#%d %s' % (next((i for i, e in enumerate(entries) if e[0] is t[0]), -1), tuple(round(c, 3) for c in t[1])) for t in terms)))
        return terms or None
    except Exception as error:
        _live_note('no terms: %s: %s' % (type(error).__name__, error))
        return None


def _live_point(place, terms):
    ''' A point at `place` -- an end of the line of `edge_at` -- whose coordinates, when it has terms (see `_live_terms`), are the place plus the terms as expressions of the numbers of the script:
        the line is made from the coordinates natively at every render, so that it follows the numbers while a face is pulled '''
    from fieldes.shape import Shape
    from fieldes.stdlib.points import point
    if not terms:
        return point(*_clean(place))
    coords = []
    for axis in range(3):
        c = Shape.wrap(round(float(place[axis]), 6) + 0.0)
        for var, move, value in terms:
            if abs(move[axis]) > 1e-12:
                c = c + (var - value) * move[axis]
        coords.append(c)
    return point(*coords)


def _influenced(out, influence):
    ''' A point that is found on a body says its influence too -- `corner_at(part, near, influence=var(1))` --: the radius of a field that is made of it (`radial_field`, `distance_to_point`), and the ring of
        its gizmo drags it.  The point is locked to its body, so it has no arrows and no rings: the gizmo it has is that ring (the numbers of a move, a turn and a scale are plain numbers: nothing to drag) --
        the user, 2026-10-28: "EVERY POINT AUTOMATICALLY CHANGES INFLUENCE" '''
    if influence is None:
        return out
    out._influence = influence
    if hasattr(influence, 'ptr'):
        at = getattr(out, '_point_pos', None)
        if at:
            out._gizmo = ('click', tuple(float(c) for c in at), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0))
    return out


def corner_at(shape, near, scale=None, bounds=None, rounded=False, influence=None):
    ''' The corner of `shape` nearest `near`: a point, where three faces of the surface meet at sharp angles.

        near    a place on or near the surface (x, y, z) -- where the corner is looked for
        influence  how far the point reaches, in mm -- a number or a var(): the radius of a field made of it, as for `point(x, y, z, influence=r)`; its gizmo has a ring that drags it
        scale   how far round `near` is looked, in mm (default: 3 % of the body, and wider, up to 0.4 of it, when there is no corner so near -- a face that was pulled by its handle has
                carried the corner away from the place written, and the corner is found where it went)
        bounds  ((x0, y0, z0), (x1, y1, z1)) of the shape, if it cannot be found
        rounded True: a corner that the rounding of three edges replaced is found too -- the point where the planes of the flat faces round it meet

        Raises ValueError when there is no corner round `near` within the `scale` that was given; with no `scale` there is no error: the point stays at `near`.
        The corner follows its body (a move, a turn, a new origin) and the faces it is made of: a face that is pulled by its handle takes it along, as the three faces are remembered
        from the run before and found again where they are now -- and in real time while the handle is dragged, by how the corner moves with each number of the script (see `_live_terms`).
        See the module. '''
    from fieldes.stdlib.points import point
    given = scale is not None
    near, scale, size, values = _prepare(shape, near, scale, bounds, 'corner_at')
    key = None if given else _call_key('corner_at', near)
    matrix, start, tracked = _start(key, shape, near)
    surface = _Surface(values, size)
    r = max(min(scale, 0.2 * size), 2e-3 * size)

    def look(place):
        found = _wider(lambda s: find_feature(values, place, s, size, 'corner'), scale, size, given)
        if found is None and rounded:
            found = _wider(lambda s: _rounded_feature(values, place, s, size, 'corner'), scale, size, given)
        return found

    # (the three faces the corner was made of, found again where they are now: a face that was pulled by its handle has taken the corner with it)
    found, planes = None, None
    old = _followed.get(key) if tracked else None
    if old is not None:
        followed = _follow_planes(surface, values, old, matrix, size, 3, start)
        if followed is not None:
            found, planes = {'kind': 'corner', 'point': followed['point'], 'direction': None}, followed['planes']
    if found is None:
        found = look(start)
        if found is None and tracked:
            found = look(near)              # (the place the call writes: what was remembered may be of another script that stood here)
    if found is None:
        if not given:
            _remember(key, matrix, [start])
            return _influenced(_stays(shape, start, 'corner_at'), influence)
        raise ValueError('corner_at: no corner of the body near (%.4g, %.4g, %.4g): three faces that meet at sharp angles within %.3g mm of it' % (near + (scale,)))
    if planes is None:
        planes = None if given else _plane_set(surface, found['point'], r, 3)
    terms = None if given else _live_terms(shape, surface, planes, 3, at=found['point'])       # (how it moves with the numbers of the script: the viewport carries it along while a face is pulled, and the run after it starts from where they say it went)
    _remember(key, matrix, [found['point']], planes, [terms], shape)
    if not given:
        _write_place('corner_at', near, found['point'], r)       # (a corner that a pulled face took far from the place written: the call says where it is now)
    out = _found_on(point(*_clean(found['point'])), shape)
    if not given:
        out._live_terms = terms
    return _influenced(out, influence)


def edge_point_at(shape, near, scale=None, bounds=None, rounded=False, influence=None):
    ''' The point of an edge of `shape` nearest `near`: a point on the line where two faces meet at a sharp angle -- any edge, straight or curved (the rim of a
        cylinder too).  Where a corner is nearer, the corner.  With rounded=True an edge that a rounding replaced is found too: the point of the line where the planes of the
        flat faces on both sides meet.  Raises ValueError when there is no edge round `near` within a `scale` that was given; with none, the neighbourhood is widened (a pulled face carries
        its edge away) and the point stays at `near` when there is still none.  The point follows its body and the two faces of the edge, as the corner of `corner_at` follows its three.
        See the module and `corner_at`. '''
    from fieldes.stdlib.points import point
    given = scale is not None
    near, scale, size, values = _prepare(shape, near, scale, bounds, 'edge_point_at')
    key = None if given else _call_key('edge_point_at', near)
    matrix, start, tracked = _start(key, shape, near)
    surface = _Surface(values, size)
    r = max(min(scale, 0.2 * size), 2e-3 * size)

    def look(place):
        found = _wider(lambda s: find_feature(values, place, s, size), scale, size, given)
        if found is None and rounded:
            found = _wider(lambda s: _rounded_feature(values, place, s, size, 'edge'), scale, size, given)
        return found

    # (the two faces the edge was made of, found again where they are now: see corner_at)
    found, planes = None, None
    old = _followed.get(key) if tracked else None
    if old is not None:
        followed = _follow_planes(surface, values, old, matrix, size, 2, start)
        if followed is not None:
            found, planes = {'kind': 'edge', 'point': followed['point'], 'direction': followed['direction']}, followed['planes']
    if found is None:
        found = look(start)
        if found is None and tracked:
            found = look(near)              # (see corner_at)
    if found is None:
        if not given:
            _remember(key, matrix, [start])
            return _influenced(_stays(shape, start, 'edge_point_at'), influence)       # (see corner_at: the place is carried with the body, and the point stays there)
        raise ValueError('edge_point_at: no edge of the body near (%.4g, %.4g, %.4g): two faces that meet at a sharp angle within %.3g mm of it' % (near + (scale,)))
    if planes is None:
        planes = None if given else _plane_set(surface, found['point'], r, 2)
    terms = None
    if not given and found.get('direction') is not None:
        terms = _live_terms(shape, surface, planes, 2, found['direction'], at=found['point'])       # (see corner_at)
    _remember(key, matrix, [found['point']], planes, [terms], shape)
    if not given:
        _write_place('edge_point_at', near, found['point'], r)   # (see corner_at)
    out = _found_on(point(*_clean(found['point'])), shape)
    if terms is not None:
        out._live_terms = terms
    return _influenced(out, influence)


def edge_at(shape, near, scale=None, bounds=None):
    ''' The straight edge of `shape` nearest `near`: a polyline of two points, from the corner at one end of the edge to the corner at the other.  Raises ValueError
        when there is no edge round `near` within a `scale` that was given (with none, the neighbourhood is widened, and the line stays a short one at `near` when there is still no edge),
        or when it curves (a circle is not a line: `edge_point_at` gives a point of it).
        The line follows its body and the two faces of the edge, as the corner of `corner_at` follows its three: a face that is pulled by its handle takes it along.  See the module and `corner_at`. '''
    from fieldes.stdlib.points import point
    from fieldes.stdlib.curves import polyline
    given = scale is not None
    near, scale, size, values = _prepare(shape, near, scale, bounds, 'edge_at')
    key = None if given else _call_key('edge_at', near)
    matrix, start, tracked = _start(key, shape, near)
    surface = _Surface(values, size)
    r = max(min(scale, 0.2 * size), 2e-3 * size)

    def look(place):
        return _wider(lambda s: find_feature(values, place, s, size, 'edge'), scale, size, given)

    # (the two faces the edge was made of, found again where they are now, and the line they make walked to its corners: see corner_at)
    found, planes = None, None
    old = _followed.get(key) if tracked else None
    if old is not None:
        followed = _follow_planes(surface, values, old, matrix, size, 2, start)
        if followed is not None:
            candidate = {'kind': 'edge', 'point': followed['point'], 'direction': followed['direction']}
            candidate['straight'] = _straight(surface, candidate)
            if candidate['straight'] is not None:
                found, planes = candidate, followed['planes']
    if found is None:
        found = look(start)
        if found is None and tracked:
            found = look(near)              # (see corner_at)
    if found is None and not given:
        # (the edge is not there any more, not even in a wide neighbourhood: a short line at the place the body carried it to, and a note, where an error would stop the script -- see corner_at)
        a = start
        b = (start[0] + 1e-3 * size, start[1], start[2])
        _remember(key, matrix, [a, b])
        print('edge_at: no edge of the body near (%.4g, %.4g, %.4g), not even in a wide neighbourhood: the line stays at that place' % tuple(start))
        return _found_on(polyline([_found_on(point(*_clean(a)), shape), _found_on(point(*_clean(b)), shape)]), shape)
    if found is None:
        raise ValueError('edge_at: no edge of the body near (%.4g, %.4g, %.4g): two faces that meet at a sharp angle within %.3g mm of it' % (near + (scale,)))
    if found['straight'] is None:
        raise ValueError('edge_at: the edge near (%.4g, %.4g, %.4g) curves, so it is not a line: edge_point_at gives a point of it' % near)
    a, b = found['straight']
    if planes is None:
        planes = None if given else _plane_set(surface, found['point'], r, 2)
    # (each end is a corner of three faces -- the two of the edge and the one it ends at: how it moves with the numbers of the script makes the line follow a face that is pulled, in real time)
    terms_a = terms_b = None
    if not given:
        terms_a = _live_terms(shape, surface, _plane_set(surface, a, r, 3), 3, at=a)
        terms_b = _live_terms(shape, surface, _plane_set(surface, b, r, 3), 3, at=b)
    _remember(key, matrix, [a, b], planes, [terms_a, terms_b], shape)
    if not given:
        _write_place('edge_at', near, found['point'], r)         # (see corner_at)
    return _found_on(polyline([_found_on(_live_point(a, terms_a), shape), _found_on(_live_point(b, terms_b), shape)]), shape)


def _rounded_feature(values, near, scale, size, kind):
    ''' {'point': ...} of the corner or edge that a rounding replaced near `near`, or None '''
    surface = _Surface(values, size)
    r = max(min(scale, 0.2 * size), 2e-3 * size)
    surface.step = max(0.04 * r, 1e-5 * size)
    start = surface.project([near])[0]
    if start is not None and abs(start[2]) < 2.0 * r:
        near = start[0]
    found = _find_rounded(surface, near, r, kind)
    return None if found is None else {'point': found}


def circle_center_at(shape, near, scale=None, bounds=None, influence=None):
    ''' The centre of the circular edge of `shape` nearest `near`: a point on the axis of a cylinder's rim, of a hole's edge, of the base of a cone.  The circle is read from the
        edge itself at several places round it, so it is the circle that the edge is, not one fitted to a mesh.  Raises ValueError when there is no circular edge round `near` within a
        `scale` that was given; with none, the neighbourhood is widened (a pulled face carries its edge away) and the point stays at `near` when there is still none.  See the module and `corner_at`. '''
    from fieldes.stdlib.points import point
    given = scale is not None
    near, scale, size, values = _prepare(shape, near, scale, bounds, 'circle_center_at')
    surface = _Surface(values, size)
    key = None if given else _call_key('circle_center_at', near)
    matrix, start, tracked = _start(key, shape, near)

    def look(around):
        def attempt(s):
            r = max(min(s, 0.2 * size), 2e-3 * size)
            surface.step = max(0.04 * r, 1e-5 * size)
            place = around
            projected = surface.project([around])[0]
            if projected is not None and abs(projected[2]) < 2.0 * r:
                place = projected[0]
            return _find_circle(surface, place, r)
        return _wider(attempt, scale, size, given)

    found = look(start)
    if found is None and tracked:
        found = look(near)                  # (see corner_at)
    if found is None and not given:
        _remember(key, matrix, [start])
        return _influenced(_stays(shape, start, 'circle_center_at'), influence)        # (see corner_at)
    if found is None:
        raise ValueError('circle_center_at: no circular edge of the body near (%.4g, %.4g, %.4g): an edge that is a circle within %.3g mm of it' % (near + (scale,)))
    _remember(key, matrix, [found[0]])
    if not given:
        # (the place a call writes for a circle is a place on its rim: the point of the rim nearest the place that was looked at is written, when the circle has gone far from it -- see corner_at)
        d = _sub(start, found[0])
        d = _sub(d, _mul(found[2], _dot(d, found[2])))
        if _norm(d) > 1e-12:
            rim = _add(found[0], _mul(d, found[1] / _norm(d)))
            _write_place('circle_center_at', near, rim, max(min(scale, 0.2 * size), 2e-3 * size))
    return _influenced(_found_on(point(*_clean(found[0])), shape), influence)


def face_center(face):
    ''' The middle of a face -- a selection of surface (`select_surface`): the mean of the places it was sampled at, which are spread evenly over it, so it is the centre of its
        area (for a circular face the centre of the circle).  `center(face)` is the middle of the box round it, which is the same for a disc and not for an L '''
    from fieldes.stdlib.points import point
    xyz = getattr(face, 'sample_points', None)
    if not xyz:
        from fieldes.stdlib.measure import center
        out = center(face)
    else:
        n = len(xyz) // 3
        out = point(*_clean((sum(xyz[0::3]) / n, sum(xyz[1::3]) / n, sum(xyz[2::3]) / n)))
    return _found_on(out, getattr(face, 'shape', None))         # (the body the face is on: what a snap -- the S key -- sorts the features by)


# (a line like the one a Curve is: it has no placement arguments of its own -- its points have them -- and a gizmo mode only.  The model tree decides this from what the call is
# when the entry that made the line selects it, before the run that says what the line is has been answered: without these two the tree wrote the gizmo with numbers, which hid the
# line (windows t347 and t351: a line along a box edge, made by the menu, was not drawn))
edge_at._makes_no_gizmo = True
edge_at._makes_outline_gizmo = True

# (and a point that is found on a body is locked to it: the tree writes no gizmo into the call when it is selected -- nothing to grab and move it off its corner, the user, 2026-10-28)
corner_at._makes_no_gizmo = True
edge_point_at._makes_no_gizmo = True
circle_center_at._makes_no_gizmo = True
face_center._makes_no_gizmo = True
