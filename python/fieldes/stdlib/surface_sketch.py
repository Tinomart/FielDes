'''
The outline of a surface, for `sketch(surface)` (see fieldes.stdlib.sketch): a surface picked on a part (`select_surface`) is made of the points of the part's surface
that the walk took, and the way the surface faces at each.  This finds the plane they lie in and the outline they cover on it -- one loop round, and one for every hole.

  * The plane is the one through the middle of the points, facing the way the surface faces on average.  A flat face lies in it exactly; a surface that is
    not flat is PROJECTED onto it, along that direction.
  * The outline is traced on a fine grid over the plane (a cell is one step of the walk), the way a picture is traced: a marching-squares contour of the
    cells the points fall in, which gives a loop for the outside and one for each hole.
  * A FLAT face has its edges made exact: every point of the contour is moved along the way out until it reaches where the part's own field leaves
    zero -- the edge of the face, to a ten-thousandth of its size -- and the straight stretches of the loop are then lines, whose corners are where those
    lines meet.  A surface that is not flat keeps the contour of the projection, within a step of the walk.
  * The loop is simplified: the points that a straight run of it needs, and as many on a curve as it takes to follow it to about a thousandth of the size.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

__all__ = ['outline_of']

# (what a marching-squares square makes, by which of its corners are inside: a, b, c, d = (0,0), (1,0), (1,1), (0,1); the edges are 0 bottom, 1 right, 2 top, 3 left;
# each piece goes from one edge to another with the inside on its left)
_PIECES = {1: [(0, 3)], 2: [(1, 0)], 3: [(1, 3)], 4: [(2, 1)], 6: [(2, 0)], 7: [(2, 3)], 8: [(3, 2)], 9: [(0, 2)], 11: [(1, 2)], 12: [(3, 1)], 13: [(0, 1)], 14: [(3, 0)]}
_SADDLE_JOINED = {5: [(0, 1), (2, 3)], 10: [(3, 0), (1, 2)]}        # (the middle of the square is inside)
_SADDLE_APART = {5: [(0, 3), (2, 1)], 10: [(1, 0), (3, 2)]}         # (the middle of the square is outside)


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(a):
    n = math.sqrt(_dot(a, a))
    return tuple(x / n for x in a) if n > 1e-300 else None


def _plane(points, normals):
    ''' ((origin, u, v), how far the points are from the plane at most): the plane through the middle of the points, facing the way the surface faces on average;
        u x v is that direction '''
    n = len(points) // 3
    origin = (sum(points[0::3]) / n, sum(points[1::3]) / n, sum(points[2::3]) / n)
    facing = _unit((sum(normals[0::3]), sum(normals[1::3]), sum(normals[2::3])))
    if facing is None:
        facing = _unit((normals[0], normals[1], normals[2])) or (0.0, 0.0, 1.0)
    axis = min(range(3), key=lambda i: abs(facing[i]))
    ref = tuple(1.0 if i == axis else 0.0 for i in range(3))
    u = _unit(tuple(r - _dot(ref, facing) * f for r, f in zip(ref, facing)))
    v = _cross(facing, u)
    away = max(abs(_dot((points[3 * i] - origin[0], points[3 * i + 1] - origin[1], points[3 * i + 2] - origin[2]), facing)) for i in range(n))
    return (origin, u, v), away


def _contours(flat, h):
    ''' The loops of the outline of the points `flat` ([(s, t)]) on a grid of cells of size h: [[(s, t)]] with the inside on the left of each (so the loop round the
        outside runs counter-clockwise and a hole's runs clockwise) '''
    smin, smax = min(p[0] for p in flat), max(p[0] for p in flat)
    tmin, tmax = min(p[1] for p in flat), max(p[1] for p in flat)
    s0, t0 = smin - 3 * h, tmin - 3 * h
    nx, ny = int((smax - s0) / h) + 5, int((tmax - t0) / h) + 5
    occupied = [bytearray(ny) for _ in range(nx)]
    for s, t in flat:
        occupied[int((s - s0) / h)][int((t - t0) / h)] = 1

    def grown(cells, take):
        # (every cell as the largest, or the least, of itself and its eight neighbours)
        out = [bytearray(ny) for _ in range(nx)]
        for i in range(1, nx - 1):
            for j in range(1, ny - 1):
                block = (cells[i - 1][j - 1], cells[i - 1][j], cells[i - 1][j + 1], cells[i][j - 1], cells[i][j], cells[i][j + 1],
                         cells[i + 1][j - 1], cells[i + 1][j], cells[i + 1][j + 1])
                out[i][j] = take(block)
        return out

    closed = grown(grown(occupied, max), min)                 # (gaps between the points are closed)
    # a value for each cell: its own and its neighbours' share of it, blurred, so that the contour at a half is where the edge of the cells is, to a fraction of one
    value = [[0.0] * ny for _ in range(nx)]
    for i in range(1, nx - 1):
        for j in range(1, ny - 1):
            c = closed
            value[i][j] = (c[i - 1][j - 1] + 2 * c[i - 1][j] + c[i - 1][j + 1] + 2 * c[i][j - 1] + 4 * c[i][j] + 2 * c[i][j + 1] +
                           c[i + 1][j - 1] + 2 * c[i + 1][j] + c[i + 1][j + 1]) / 16.0

    def edge_id(kind, i, j):
        return (kind * ny + j) * nx + i

    def edge_point(eid):
        kind, rest = divmod(eid, ny * nx)
        j, i = divmod(rest, nx)
        if kind == 0:
            fa, fb = value[i][j], value[i + 1][j]
            return (s0 + (i + 0.5 + (0.5 - fa) / (fb - fa)) * h, t0 + (j + 0.5) * h)
        fa, fb = value[i][j], value[i][j + 1]
        return (s0 + (i + 0.5) * h, t0 + (j + 0.5 + (0.5 - fa) / (fb - fa)) * h)

    follow = {}
    for i in range(1, nx - 2):
        for j in range(1, ny - 2):
            a, b, c, d = value[i][j], value[i + 1][j], value[i + 1][j + 1], value[i][j + 1]
            case = (a >= 0.5) + 2 * (b >= 0.5) + 4 * (c >= 0.5) + 8 * (d >= 0.5)
            if case in (0, 15):
                continue
            if case in _SADDLE_JOINED:
                pieces = (_SADDLE_JOINED if (a + b + c + d) / 4.0 >= 0.5 else _SADDLE_APART)[case]
            else:
                pieces = _PIECES[case]
            edges = (edge_id(0, i, j), edge_id(1, i + 1, j), edge_id(0, i, j + 1), edge_id(1, i, j))
            for start, end in pieces:
                follow[edges[start]] = edges[end]
    loops = []
    seen = set()
    for first in list(follow):
        if first in seen:
            continue
        loop, here = [], first
        while here in follow and here not in seen:
            seen.add(here)
            loop.append(here)
            here = follow[here]
        if here == first and len(loop) >= 3:
            loops.append([edge_point(e) for e in loop])
    return loops


def _area(loop):
    return 0.5 * sum(loop[i][0] * loop[(i + 1) % len(loop)][1] - loop[(i + 1) % len(loop)][0] * loop[i][1] for i in range(len(loop)))


def _distance_to_segment(p, a, b):
    ex, ey = b[0] - a[0], b[1] - a[1]
    ee = ex * ex + ey * ey
    t = 0.0 if ee < 1e-300 else max(0.0, min(1.0, ((p[0] - a[0]) * ex + (p[1] - a[1]) * ey) / ee))
    return math.hypot(p[0] - a[0] - t * ex, p[1] - a[1] - t * ey)


def _douglas_peucker(points, eps):
    ''' The indices of the points of an open run that keep it within `eps` of the run (the ends always) '''
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        best, at = 0.0, -1
        for k in range(a + 1, b):
            d = _distance_to_segment(points[k], points[a], points[b])
            if d > best:
                best, at = d, k
        if at >= 0 and best > eps:
            keep[at] = True
            stack.append((a, at))
            stack.append((at, b))
    return [i for i, kept in enumerate(keep) if kept]


def _line(points):
    ''' (a point on it, its direction) of the line that fits the points best (least squares across it) '''
    n = len(points)
    cx, cy = sum(p[0] for p in points) / n, sum(p[1] for p in points) / n
    sxx = sum((p[0] - cx) ** 2 for p in points)
    syy = sum((p[1] - cy) ** 2 for p in points)
    sxy = sum((p[0] - cx) * (p[1] - cy) for p in points)
    angle = 0.5 * math.atan2(2 * sxy, sxx - syy)
    return (cx, cy), (math.cos(angle), math.sin(angle))


def _corners_made_exact(loop, kept, h):
    ''' The corners of the simplified loop moved to where the lines of the two straight runs beside each meet.  `loop` is the whole, finely traced, loop, `kept` the
        indices of the corners in it (increasing).  A corner of the traced loop is cut off by a short diagonal (a step of the grid): that diagonal between two long
        straight runs is ONE corner, where the runs' lines meet.  A corner whose runs are short, or nearly parallel, stays where it is '''
    m = len(kept)
    n = len(loop)
    length = []
    lines = []
    for k in range(m):
        a, b = kept[k], kept[(k + 1) % m]
        count = (b - a) % n
        a_end, b_end = loop[a], loop[b]
        length.append(math.hypot(b_end[0] - a_end[0], b_end[1] - a_end[1]))
        # (the part of the run that is not at either end, where the loop turns)
        middle = []
        for q in range(1, count):
            p = loop[(a + q) % n]
            if math.hypot(p[0] - a_end[0], p[1] - a_end[1]) > 1.2 * h and math.hypot(p[0] - b_end[0], p[1] - b_end[1]) > 1.2 * h:
                middle.append(p)
        lines.append(_line(middle) if len(middle) >= 4 and length[-1] >= 3 * h else None)

    def meet(before, after, near, reach):
        if before is None or after is None:
            return None
        (c1, d1), (c2, d2) = before, after
        denom = d1[0] * d2[1] - d1[1] * d2[0]
        if abs(denom) <= 0.34:                                   # (the lines are less than twenty degrees apart)
            return None
        a = ((c2[0] - c1[0]) * d2[1] - (c2[1] - c1[1]) * d2[0]) / denom
        point = (c1[0] + a * d1[0], c1[1] + a * d1[1])
        return point if math.hypot(point[0] - near[0], point[1] - near[1]) <= reach else None

    # a short run between two long ones is the diagonal that cuts a corner off
    cut = [lines[k] is None and length[k] < 2.5 * h and lines[k - 1] is not None and lines[(k + 1) % m] is not None for k in range(m)]
    out = []
    for k in range(m):
        corner = loop[kept[k]]
        if cut[k - 1]:
            continue                                             # (this corner was made with the one before it)
        if cut[k]:
            mid = ((corner[0] + loop[kept[(k + 1) % m]][0]) / 2, (corner[1] + loop[kept[(k + 1) % m]][1]) / 2)
            corner = meet(lines[k - 1], lines[(k + 1) % m], mid, 3 * h + length[k]) or corner
            out.append(corner)
            continue
        out.append(meet(lines[k - 1], lines[k], corner, 2.5 * h) or corner)
    return out


def outline_of(selection):
    ''' (plane, loops, flat, away): `plane` is (origin, u, v) of the plane the outline is in (u x v is the way the surface faces), `loops` a list of (polygon, hole): the
        polygon as [(s, t)] along u and v, and whether it is a hole, `flat` whether the surface is flat, and `away` how far from the plane its points are at most '''
    points = getattr(selection, 'sample_points', None)
    normals = getattr(selection, 'sample_normals', None)
    if not points or not normals or len(points) < 9:
        raise ValueError('sketch(surface): this surface has no points to make an outline from -- a sketch is made from a surface picked on a part (select_surface)')
    n = len(points) // 3
    step = float(getattr(selection, 'spacing', 0.0)) or 1.0
    plane, away = _plane(points, normals)
    origin, u, v = plane
    flat2d = []
    for i in range(n):
        d = (points[3 * i] - origin[0], points[3 * i + 1] - origin[1], points[3 * i + 2] - origin[2])
        flat2d.append((_dot(d, u), _dot(d, v)))
    size = max(max(p[0] for p in flat2d) - min(p[0] for p in flat2d), max(p[1] for p in flat2d) - min(p[1] for p in flat2d), step)
    flat = away <= 0.25 * step + 1e-6 * size
    h = step
    while ((size / h) + 6) ** 2 > 4.0e6:                         # (a grid of a few million cells at most)
        h *= 1.5
    traced = _contours(flat2d, h)
    if not traced:
        raise ValueError('sketch(surface): no outline could be traced from this surface (is the patch a single point or a line?)')
    field = getattr(selection, 'shape', None)
    delta = 1e-4 * size
    if flat and field is not None:
        # (what "on the face" means: the field's value on the points the walk took, which are on the face, is zero to the noise of the arithmetic -- a part that is turned
        # has more of it -- and the face stops where the value rises above three times that)
        noise = max(abs(float(field(points[3 * i], points[3 * i + 1], points[3 * i + 2]))) for i in range(0, n, max(1, n // 40)))
        delta = min(max(3.0 * noise, 1.5e-4 * size), 1e-3 * size)
    loops = []
    for loop in traced:
        if len(loop) < 4:
            continue
        loop = _refined(loop, plane, field, h, delta) if flat and field is not None else loop
        eps = 1.5e-3 * size if flat else max(0.6 * h, 2e-3 * size)
        # (the run starts at the point farthest from the first, so that the two halves of the loop are each far from straight at the ends)
        far = max(range(len(loop)), key=lambda k: math.hypot(loop[k][0] - loop[0][0], loop[k][1] - loop[0][1]))
        run = [loop[(far + q) % len(loop)] for q in range(len(loop) + 1)]
        kept = [(far + q) % len(loop) for q in _douglas_peucker(run, eps)[:-1]]
        if len(kept) < 3:
            continue
        kept.sort()
        polygon = _corners_made_exact(loop, kept, h) if flat else [loop[k] for k in kept]
        loops.append((polygon, _area(loop) < 0))
    if not any(not hole for _, hole in loops):
        raise ValueError('sketch(surface): no outline could be traced from this surface')
    return plane, loops, flat, away


def _refined(loop, plane, field, h, delta):
    ''' The points of the loop moved along the way out to the edge of the face: where the part's field, which is zero on the face, stops being zero '''
    origin, u, v = plane
    n = len(loop)

    def on_face(s, t):
        x = origin[0] + s * u[0] + t * v[0]
        y = origin[1] + s * u[1] + t * v[1]
        z = origin[2] + s * u[2] + t * v[2]
        return abs(float(field(x, y, z))) <= delta

    out = []
    for k in range(n):
        # (the way the loop runs here, from the points two steps either side: at the tip of a spike the two next to it are the same point)
        p, a, b = loop[k], loop[(k - 2) % n], loop[(k + 2) % n]
        tangent = (b[0] - a[0], b[1] - a[1])
        length = math.hypot(*tangent)
        if length < 1e-300:
            out.append(p)
            continue
        way = (tangent[1] / length, -tangent[0] / length)          # (the inside is on the left, so out is on the right)
        # (a point beyond a corner is further from the face than a step: look further in for a point that is on it)
        lo = next((-back * h for back in (1.0, 2.0, 3.0) if on_face(p[0] - back * h * way[0], p[1] - back * h * way[1])), None)
        hi = 1.5 * h
        if lo is None or on_face(p[0] + hi * way[0], p[1] + hi * way[1]):
            out.append(p)                                          # (not at the edge of a face: where the surface ends, not the face)
            continue
        for _ in range(12):
            mid = 0.5 * (lo + hi)
            if on_face(p[0] + mid * way[0], p[1] + mid * way[1]):
                lo = mid
            else:
                hi = mid
        # (the field is a distance there, so it is `delta` where the face has ended `delta` before: that much back)
        out.append((p[0] + (lo - delta) * way[0], p[1] + (lo - delta) * way[1]))
    return out
