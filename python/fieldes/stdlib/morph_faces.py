''' The faces of a body of points (morph_body, morph_polyhedron), made of the points at every run (H164).

The user, 2026-11-08: "Morph bodies should not have any faces logic by default ... Sphere and spline points should just create the rules for the mesh in their area of influence", 2026-11-09: "IF AN ADDED POINT OR AN
ADJUSTMENT CHANGES THE SHAPE EVERYWHERE THE CONCEPT OF INFLUENCE IS LOST COMPLETELY" and "THERE IS NO OFF BY DEFAULT. IF IT IS BROKEN IT IS GONE".  No list of faces is kept in the script: the faces are made from the points, so they follow every edit that puts a point in
or takes one out, and a part of the body far from an edit keeps the faces it had.  The faces are a function of the points and of nothing else (the user, 2026-11-11, H171: "The face is deterministic always and is exactly what it would be when you run the code fresh.
Otherwise the user works on something closes and opens their file and it is completely different"): no faces are kept from the runs before, so a window that pressed and deleted points and a window that has just opened the file show the same body.

  1. NEIGHBOURS.  An ALPHA SHAPE of the points: the Delaunay tetrahedra (an empty circumsphere) whose circumradius is at most _REACH times the largest nearest-neighbour distance of their four points -- the reach of a point
     is the distance to its nearest neighbour, so a point that was pulled out reaches as far as it was pulled --, the triangles on the border of that.  The scale is taken from the points that are on the border (a point pressed
     into the body does not shrink it).  Measured on 2026-11-09: the same faces for _REACH 1.2 to 3.0 on a cube, a pulled-out point and a bar of 24 points, and a corner of the bar moved changes no face.
  2. FACES.  The coplanar triangles that are joined through an edge are one face of the body: a face of exactly four points that are the corners of a convex square is one square, any other group stays the triangles it is
     -- so a point pressed into a face is always the face split round it.
  3. A DENT.  A point that is not on the border (the user pressed the surface and dragged into it) is joined to the face nearest to it, which is cut in a fan round it.

Where it is used: morph._default_faces and morph._faces_between_closest_points.
'''
import itertools
import math

_REACH = 2.0              # (see 1.)
_SUPERS = (100.0, 10.0, 30.0, 300.0, 1e3, 1e4, 3.0)   # (how many times the size of the points the tetrahedron that holds them all is: tried in turn, see _delaunay)
_SPHERE_CUT = 0.73
_SHAKE = 1e-6


def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def _mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _norm(a): return math.sqrt(_dot(a, a))
def _cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(a):
    l = _norm(a)
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-300 else (0.0, 0.0, 1.0)


def _shake(pts):
    ''' The points in general position (a cube has eight points on one sphere): moved by 1e-6 along a fixed pseudo-random direction -- the same for the same index, so a point moved does not move the others '''
    return [tuple(p[k] + _SHAKE * math.sin(12.9898 * (i + 1) * (k + 1) + 78.233) for k in range(3)) for i, p in enumerate(pts)]


def _circumsphere(a, b, c, d):
    rows = [_sub(b, a), _sub(c, a), _sub(d, a)]
    rhs = [_dot(r, r) / 2 for r in rows]

    def det3(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    det = det3(rows)
    if abs(det) < 1e-12 * (1.0 + _norm(rows[0]) * _norm(rows[1]) * _norm(rows[2])):
        return None
    x = []
    for col in range(3):
        m = [list(r) for r in rows]
        for i in range(3):
            m[i][col] = rhs[i]
        x.append(det3(m) / det)
    x = tuple(x)
    return _add(a, x), _norm(x)


def _delaunay_once(ps, super_size):
    ''' The Delaunay tetrahedra of the points, as (circumradius, sorted indices).  Computed by inserting the points one at a time into a tetrahedron that holds them all (Bowyer and Watson): the tetrahedra whose circumsphere
        holds the new point are taken out and the hole they leave is joined to the point.  It used to look for the tetrahedra among each point's nearest twelve only, and for a body of rings of points -- a horn -- that missed
        tetrahedra that reach across a ring: the tetrahedra did not fill the hull, and the border had holes and edges of three and four faces '''
    n = len(ps)
    lo = [min(p[a] for p in ps) for a in range(3)]
    hi = [max(p[a] for p in ps) for a in range(3)]
    mid = [(lo[a] + hi[a]) / 2.0 for a in range(3)]
    size = max(hi[a] - lo[a] for a in range(3)) * super_size + 1.0
    verts = list(ps) + [(mid[0] + size, mid[1] + size, mid[2] + size), (mid[0] + size, mid[1] - size, mid[2] - size),
                        (mid[0] - size, mid[1] + size, mid[2] - size), (mid[0] - size, mid[1] - size, mid[2] + size)]
    big = (0.0, 0.0, 0.0), 1e300

    def sphere(q):
        cs = _circumsphere(verts[q[0]], verts[q[1]], verts[q[2]], verts[q[3]])
        return cs if cs is not None else big

    tets = {(n, n + 1, n + 2, n + 3): sphere((n, n + 1, n + 2, n + 3))}
    for i in range(n):
        p = verts[i]
        bad = [q for q, (c, R) in tets.items() if _norm(_sub(p, c)) < R * (1 - 1e-12)]
        faces = {}
        for q in bad:
            for opp in range(4):
                f = tuple(sorted(q[k] for k in range(4) if k != opp))
                faces[f] = faces.get(f, 0) + 1
        for q in bad:
            del tets[q]
        for f, count in faces.items():
            if count == 1:
                q = tuple(sorted(f + (i,)))
                tets[q] = sphere(q)
    return [(cs[1], q) for q, cs in tets.items() if all(v < n for v in q)]


def _hull_closed(tetrahedra):
    ''' Whether the outer border of the tetrahedra is a closed surface: every edge of the faces that belong to one tetrahedron only is used by two of them '''
    faces = {}
    for R, q in tetrahedra:
        for opp in range(4):
            f = tuple(sorted(q[i] for i in range(4) if i != opp))
            faces[f] = faces.get(f, 0) + 1
    edges = {}
    for f, c in faces.items():
        if c == 1:
            for e in ((f[0], f[1]), (f[1], f[2]), (f[0], f[2])):
                edges[e] = edges.get(e, 0) + 1
    return bool(edges) and all(c == 2 for c in edges.values())


def _delaunay(ps):
    ''' The Delaunay tetrahedra (see _delaunay_once), made with the first size of the enclosing tetrahedron that gives a closed border round all of them: the sphere tests are done on floating point numbers and the border of
        the result is wrong for some sizes -- measured on two horn bodies: closed at 10 and 100 times the size of the points, open at 1000 or 10000 for one or the other '''
    result = None
    for size in _SUPERS:
        result = _delaunay_once(ps, size)
        if _hull_closed(result):
            return result
    return result


def _alpha_core(pts, scale_ids, reach=_REACH):
    ''' The border triangles of the alpha shape for the reach `reach` (a tetrahedron is kept when its circumsphere is at most that many times the largest nearest-neighbour distance of its four points), each with its outward normal, as (indices, normal) '''
    ps = _shake(pts)
    n = len(ps)
    nn_d = []
    for i in range(n):
        best = None
        for j in scale_ids:
            if j != i:
                d = _norm(_sub(ps[i], ps[j]))
                if best is None or d < best:
                    best = d
        nn_d.append(best if best is not None else 1.0)
    delaunay = _delaunay(ps)
    kept = set(q for R, q in delaunay if R <= reach * max(nn_d[i] for i in q))
    everything = [q for R, q in delaunay]
    face_tets = {}
    for q in everything:
        for opp in range(4):
            face_tets.setdefault(tuple(sorted(q[i] for i in range(4) if i != opp)), []).append(q)
    # (WHAT IS OUTSIDE.  The Delaunay tetrahedra fill the convex hull of the points; the ones the reach keeps are the body, and the others are outside only if they can be reached from outside the hull through other tetrahedra that the
    # reach turned away -- a hollow in the middle of the body, shut in by kept tetrahedra, is not outside and is solid.  The border is the faces between a solid tetrahedron and an outside one or the outside of the hull: always a closed
    # surface.  If the reach turns everything away -- a few points far apart -- the body is the convex hull)
    outside = set()
    stack = [q for q in everything if q not in kept and any(len(face_tets[tuple(sorted(q[i] for i in range(4) if i != opp))]) == 1 for opp in range(4))]
    while stack:
        q = stack.pop()
        if q in outside:
            continue
        outside.add(q)
        for opp in range(4):
            for other in face_tets[tuple(sorted(q[i] for i in range(4) if i != opp))]:
                if other != q and other not in kept and other not in outside:
                    stack.append(other)
    solid = set(q for q in everything if q not in outside)
    if not kept:
        solid = set(everything)
        outside = set()
    radius = dict((q, R) for R, q in delaunay)

    def border_of():
        tris = []
        for q in solid:
            for opp in range(4):
                f = tuple(sorted(q[i] for i in range(4) if i != opp))
                others = [t for t in face_tets[f] if t != q]
                if others and others[0] in solid:
                    continue
                a, b, c = [ps[i] for i in f]
                nn = _cross(_sub(b, a), _sub(c, a))
                l = _norm(nn)
                if l < 1e-300:
                    continue
                nn = (nn[0] / l, nn[1] / l, nn[2] / l)
                if _dot(nn, _sub(ps[q[opp]], a)) > 0:
                    nn = (-nn[0], -nn[1], -nn[2])
                tris.append([f, nn])
        return tris

    # (NO PINCHES: where two solid pieces meet along an edge only, that edge belongs to four border triangles and the surface is not a surface.  While there is such an edge, the smallest of the carved tetrahedra that has it is put back
    # into the solid -- for points all on the hull of one another, a few spread over a sphere, that gives the convex hull)
    tris = border_of()
    for _step in range(len(outside) + 1):
        count = {}
        for f, nn in tris:
            for e in ((f[0], f[1]), (f[1], f[2]), (f[0], f[2])):
                count[e] = count.get(e, 0) + 1
        pinched = set(e for e, c in count.items() if c > 2)
        if not pinched:
            break
        best = None
        for q in outside:
            if any(tuple(sorted((q[a], q[b]))) in pinched for a in range(4) for b in range(a + 1, 4)):
                if best is None or radius[q] < radius[best]:
                    best = q
        if best is None:
            break
        outside.discard(best)
        solid.add(best)
        tris = border_of()
    # (THE OUTER SURFACE ONLY: where the tetrahedra are looked for among the nearest points only -- a body of many points -- the middle of a ball of points is not filled, and the border has an inner surface too, with its normals into the
    # body.  The faces that are kept are those of the piece of the border that holds the farthest point -- the largest x, then y, then z: it is on the outside --, the pieces being joined through shared edges)
    if tris:
        parent = list(range(len(tris)))

        def find(i):
            while parent[i] != i:
                parent[i] = parent[parent[i]]
                i = parent[i]
            return i
        owner = {}
        for k, (f, nn) in enumerate(tris):
            for e in ((f[0], f[1]), (f[1], f[2]), (f[0], f[2])):
                if e in owner:
                    parent[find(k)] = find(owner[e])
                else:
                    owner[e] = k
        far = sorted(set(v for f, nn in tris for v in f), key=lambda v: (ps[v][0], ps[v][1], ps[v][2]))[-1]
        root = find(next(k for k, (f, nn) in enumerate(tris) if far in f))
        tris = [t for k, t in enumerate(tris) if find(k) == root]
    return tris


def _convex_ring(pts, group, nn):
    ''' Whether the points of `group` (in one plane, normal nn) are the corners of a convex polygon, every one of them a corner: two triangles are put together into a square only then -- a point in the middle of a face, pressed there,
        is inside the triangle of the three others, and the four are not a square '''
    on = tuple(group)
    c = tuple(sum(pts[m][a] for m in on) / len(on) for a in range(3))
    e1 = _unit(_sub(pts[on[0]], c))
    e2 = _cross(nn, e1)
    order = sorted(on, key=lambda m: math.atan2(_dot(_sub(pts[m], c), e2), _dot(_sub(pts[m], c), e1)))
    k = len(order)
    for i in range(k):
        a, b, d = pts[order[i]], pts[order[(i + 1) % k]], pts[order[(i + 2) % k]]
        u, v = _sub(b, a), _sub(d, b)
        # (every corner is a corner: the sine of the turn at it is at least 0.26, an interior angle of at most 165 degrees -- a point pressed near the diagonal of a face makes a kite whose angle is nearly flat, and that is a face split
        # round a point, not a square)
        if _dot(_cross(u, v), nn) <= 0.26 * _norm(u) * _norm(v):
            return False
    return True


def _polygons(pts, tris):
    ''' The coplanar triangles that share an edge put together into polygons of at most four points, each as (the points in order round it, the outward normal) '''
    # (the triangles are first grouped: the coplanar ones that are joined through a shared edge are one FACE of the body.  A face of exactly four points that are the corners of a convex square is one square; a face with a point
    # inside it -- a point pressed into a face -- or with more points stays the triangles it is, so that a point pressed into a face is always the face split round it, as it was)
    n = len(tris)
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(n):
        ti, ni = tris[i]
        a0 = ti[0]
        scale = _norm(_sub(pts[ti[1]], pts[a0]))
        for j in range(i + 1, n):
            tj, nj = tris[j]
            if _dot(ni, nj) > 1 - 1e-4 and len(set(ti) & set(tj)) == 2 and all(abs(_dot(_sub(pts[m], pts[a0]), ni)) < 1e-3 * scale for m in tj):
                parent[find(j)] = find(i)
    groups = {}
    for i in range(n):
        groups.setdefault(find(i), []).append(i)

    def ring(group, nn):
        on = tuple(group)
        c = tuple(sum(pts[m][a] for m in on) / len(on) for a in range(3))
        e1 = _unit(_sub(pts[on[0]], c))
        e2 = _cross(nn, e1)
        return sorted(on, key=lambda m: math.atan2(_dot(_sub(pts[m], c), e2), _dot(_sub(pts[m], c), e1)))

    polys = []
    for idxs in groups.values():
        group = set()
        for i in idxs:
            group |= set(tris[i][0])
        nn = tris[idxs[0]][1]
        if len(idxs) == 2 and len(group) == 4 and _convex_ring(pts, group, nn):
            polys.append((ring(group, nn), nn))
        else:
            for i in idxs:
                polys.append((ring(tris[i][0], tris[i][1]), tris[i][1]))
    return polys


def _closed(tris):
    ''' Whether the triangles are a closed surface: every edge is used by exactly two of them '''
    count = {}
    for f, nn in tris:
        for e in ((f[0], f[1]), (f[1], f[2]), (f[0], f[2])):
            e = tuple(sorted(e))
            count[e] = count.get(e, 0) + 1
    return bool(count) and all(c == 2 for c in count.values())


# The reaches that are tried, in turn, until the border is a closed surface that has most of the points on it (see polygons_of)
_REACHES = (_REACH, 3.0, 4.5, 7.0, 12.0, 1e9)
# The part of the points that must be on the surface (the others are pressed into it: dents)
_EXPOSED_SHARE = 0.75


def _has_handles(polys):
    ''' Whether a closed surface made of these polygons has a handle (a tunnel): the Euler number of one of its pieces -- points less edges plus faces -- is below 2 '''
    edges, points = set(), set()
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for ring, nn in polys:
        for k in range(len(ring)):
            a, b = ring[k], ring[(k + 1) % len(ring)]
            edges.add((min(a, b), max(a, b)))
            points.add(a)
            parent[find(a)] = find(b)
    pieces = len(set(find(p) for p in points))
    return len(points) - len(edges) + len(polys) < 2 * pieces


def _consistent(pts, polys):
    ''' THE FACES ALL GO ROUND THE SAME WAY (found 2026-11-10 on bodies of about 46 scattered points: a few faces were wound against their neighbours -- an edge used twice in the same direction -- and the surface made of them was not closed): the winding of a face is
        what its neighbour across each edge says -- two faces that share an edge use it in opposite directions --, carried from face to face over the edges that belong to exactly two faces, and every closed piece of the surface then goes round counterclockwise seen from
        outside: the volume it encloses is positive.  Returns the polygons as (the points in order, the outward normal) '''
    n = len(polys)
    rings = [list(o) for o, nn in polys]
    normals = [nn for o, nn in polys]
    owners = {}
    for i, o in enumerate(rings):
        for k in range(len(o)):
            a, b = o[k], o[(k + 1) % len(o)]
            owners.setdefault((min(a, b), max(a, b)), []).append(i)
    seen = [False] * n
    for s in range(n):
        if seen[s]:
            continue
        seen[s] = True
        piece, stack = [s], [s]
        while stack:
            i = stack.pop()
            o = rings[i]
            for k in range(len(o)):
                a, b = o[k], o[(k + 1) % len(o)]
                others = [j for j in owners[(min(a, b), max(a, b))] if j != i]
                if len(others) != 1 or seen[others[0]]:
                    continue
                j = others[0]
                oj = rings[j]
                for m in range(len(oj)):
                    if oj[m] == a and oj[(m + 1) % len(oj)] == b:
                        rings[j] = list(reversed(oj))
                        normals[j] = (-normals[j][0], -normals[j][1], -normals[j][2])
                        break
                seen[j] = True
                piece.append(j)
                stack.append(j)
        volume = 0.0
        for i in piece:
            o = rings[i]
            for m in range(1, len(o) - 1):
                volume += _dot(pts[o[0]], _cross(pts[o[m]], pts[o[m + 1]]))
        if volume < 0.0:
            for i in piece:
                rings[i] = list(reversed(rings[i]))
                normals[i] = (-normals[i][0], -normals[i][1], -normals[i][2])
    return [(rings[i], normals[i]) for i in range(n)]


def polygons_of(pts, with_dents=False):
    ''' The polygons of the body of these points (see 1.): a list of (points in order, outward normal).  THE REACH GROWS UNTIL THE BORDER IS A CLOSED SURFACE WITH THE POINTS ON IT: for a long thin body made of rings of points
        -- a horn, anything a user makes by pulling points out of a surface -- the fixed reach turned away most of the tetrahedra, and the border had holes, edges of three and four faces and an inner shell, or only a few of the
        points on it (measured on a horn of eight rings of six: edges used by four, three and one faces; of five rings, ten border triangles for 32 points): the cage was not a surface and the patches over it folded.  The first
        reach that gives a closed surface with at least 3/4 of the points on it is taken; for a convex body that is the first, and a body of a few points far apart gets the convex hull at the last '''
    n = len(pts)
    chosen = None
    for reach in _REACHES:
        tris = _alpha_core(pts, list(range(n)), reach)
        polys = _polygons(pts, tris)
        exposed = set(m for o, nn in polys for m in o)
        if len(exposed) < n:
            tris2 = _alpha_core(pts, sorted(exposed), reach)
            polys2 = _polygons(pts, tris2)
            exposed2 = set(m for o, nn in polys2 for m in o)
            tris, polys, exposed = tris2, polys2, exposed2
        if chosen is None:
            chosen = (tris, polys, exposed)
        if _closed(tris) and len(exposed) >= _EXPOSED_SHARE * n:
            chosen = (tris, polys, exposed)
            break
    tris, polys, exposed = chosen
    # (POINTS ALL ON THE HULL OF ONE ANOTHER -- spread over a sphere, the corners of a convex polyhedron -- have no inside to keep and no tunnel to make: the alpha shape of 40 points on a sphere was a closed surface with two handles, 18 triangles
    # that are no face of the hull and 12 of the hull missing, because every tetrahedron of such points has the same circumsphere and the reach keeps some and turns others away.  When the border has handles and every point is a corner of the
    # convex hull, the body is the hull.  Points with one inside the hull of the others -- a ring round a hole, a pressed-in point -- keep what the reach made)
    if _has_handles(polys):
        hull_tris = _alpha_core(pts, list(range(n)), _REACHES[-1])
        hull_polys = _polygons(pts, hull_tris)
        if _closed(hull_tris) and len(set(m for o, nn in hull_polys for m in o)) == n and not _has_handles(hull_polys):
            tris, polys, exposed = hull_tris, hull_polys, set(range(n))
    dents = [i for i in range(n) if i not in exposed]
    for d in dents:
        best = None
        for k, (order, nn) in enumerate(polys):
            c = tuple(sum(pts[m][a] for m in order) / len(order) for a in range(3))
            off = abs(_dot(_sub(pts[d], c), nn))
            lat = _norm(_sub(_sub(pts[d], c), _mul(nn, _dot(_sub(pts[d], c), nn))))
            rad = max(_norm(_sub(pts[m], c)) for m in order)
            score = off + max(0.0, lat - rad)
            if best is None or score < best[0]:
                best = (score, k)
        if best is None:
            continue
        order, nn = polys.pop(best[1])
        m = len(order)
        for i in range(m):
            a, b = order[i], order[(i + 1) % m]
            tn = _cross(_sub(pts[b], pts[a]), _sub(pts[d], pts[a]))
            l = _norm(tn)
            tn = (tn[0] / l, tn[1] / l, tn[2] / l) if l > 1e-300 else nn
            if _dot(tn, nn) < 0:
                tn = (-tn[0], -tn[1], -tn[2])
            ring = [d, a, b] if _dot(_cross(_sub(pts[a], pts[d]), _sub(pts[b], pts[d])), nn) > 0 else [d, b, a]
            polys.append((ring, tn))
        exposed.add(d)
    polys = _consistent(pts, polys)
    return (polys, dents) if with_dents else polys


def refuse_flat(name, pos):
    ''' ValueError, in plain words, for points that enclose no space: all at one place, all in a line, all in one plane '''
    n = len(pos)
    scale = max(_norm(_sub(pos[i], pos[j])) for i in range(n) for j in range(n)) or 1.0
    eps = 1e-9 * scale
    a = pos[0]
    far = max(range(n), key=lambda i: _norm(_sub(pos[i], a)))
    if _norm(_sub(pos[far], a)) <= eps:
        raise ValueError('%s: all the points are at one place' % name)
    d = _unit(_sub(pos[far], a))
    off = max(range(n), key=lambda i: _norm(_cross(_sub(pos[i], a), d)))
    if _norm(_cross(_sub(pos[off], a), d)) <= eps:
        raise ValueError('%s: all the points are in one line: a body needs points that are not in one plane' % name)
    nn = _unit(_cross(d, _sub(pos[off], a)))
    top = max(abs(_dot(_sub(p, a), nn)) for p in pos)
    if top <= 1e-6 * scale:
        raise ValueError('%s: all the points are in one plane: a body needs at least four points that are not (give the faces for a flat shape, or use profile)' % name)
