'''
Checks of the surface of a body of points, the limit of a Catmull-Clark subdivision of control points made from its points (fieldes.stdlib.morph, H170, 2026-11-10): the surface goes through every spline point exactly, a moved point changes only what is near it, the rotation
and the influence of a spline point are exact, the edge between two corners is straight, a sphere point is round about its axis, of about the curvature of its radius.  Library checks on plain numbers (morph_numbers), no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import math
import sys
import time

from fieldes import *
from fieldes.stdlib import morph as M

bad = []
start = time.time()
CUBE = [(-5, -5, -5), (5, -5, -5), (5, 5, -5), (-5, 5, -5), (-5, -5, 5), (5, -5, 5), (5, 5, 5), (-5, 5, 5)]


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def make(points, corners=(), radii=None, rotations=None, influences=None, faces=None, pieces=16):
    ''' (the vertices, the triangles) of the body made on numbers, `pieces` to an edge '''
    M._drawn_segments = lambda cage, rounded=False, turn=0.0, n=pieces: n
    plist = []
    for i, p in enumerate(points):
        kw = {'position': p}
        if i in corners:
            kw['smooth'] = False
        if radii and radii[i]:
            kw['radius'] = float(radii[i])
        if rotations and i in rotations:
            kw['rotation'] = rotations[i]
        if influences and i in influences:
            kw['influence'] = influences[i]
        plist.append(point(**kw))
    controls, vertices, tris = M.morph_numbers(plist, faces=faces)
    verts = []
    for terms in vertices:
        x = y = z = 0.0
        for c, w in terms:
            q = controls[c]
            x += w * q[0]
            y += w * q[1]
            z += w * q[2]
        verts.append((x, y, z))
    return verts, tris


def vertex_normal(verts, tris, idx):
    n = [0.0, 0.0, 0.0]
    for a, b, c in tris:
        if idx in (a, b, c):
            p, q, r = verts[a], verts[b], verts[c]
            u = [q[k] - p[k] for k in range(3)]
            w = [r[k] - p[k] for k in range(3)]
            cr = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
            for k in range(3):
                n[k] += cr[k]
    length = math.sqrt(sum(x * x for x in n))
    return [x / length for x in n]


def angle(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))))


def through_the_points():
    # (the user, 2026-11-10: "The body does not go through the spline points always"): the vertex of the mesh at each point is the point, whatever the data -- also the alternating pattern of a cube, whose limit weight is 0
    for label, pts in (('a cube', CUBE),
                       ('a cube with alternating corners moved', [(x * (1.3 if i % 2 else 0.7), y, z) for i, (x, y, z) in enumerate(CUBE)]),
                       ('a cube with a corner pulled far out', [p if i != 6 else (12.0, 12.0, 12.0) for i, p in enumerate(CUBE)])):
        verts, tris = make(pts)
        err = max(math.dist(verts[i], pts[i]) for i in range(len(pts)))
        check('the surface of %s goes through its eight spline points' % label, err < 1e-9, '%.1e' % err)


def local_changes():
    # (the user: "shifting one point on one side of the geometry can COMPLETELY CHANGE THE OTHER SIDE"): the program makes the cage again from the points, so the two surfaces are compared by the nearest vertex of the other, and the surface at the far corner moves little
    base, _ = make(CUBE)
    moved_pts = [p if i != 6 else (7.0, 7.0, 7.0) for i, p in enumerate(CUBE)]
    moved, _ = make(moved_pts)

    def change_near(point_):
        return max(min(math.dist(v, w) for w in moved) for v in base if math.dist(v, point_) < 2.5)
    far, close = change_near(CUBE[0]), change_near(CUBE[6])
    check('a corner moved by 3.46: the surface within 2.5 of the opposite corner changes under 0.4 (the mesh has a spacing of 0.3)', far < 0.4, '%.3f against %.3f at the moved corner' % (far, close))


def rotation_and_influence():
    # a bipyramid: the apex has the valence of the cage; a rotation about x by 10 degrees of the point turns the normal of the surface there by 10 degrees (the normal is along z)
    for n in (3, 4, 5, 6, 8):
        pts = [(0.0, 0.0, 6.0), (0.0, 0.0, -6.0)] + [(5 * math.cos(2 * math.pi * k / n), 5 * math.sin(2 * math.pi * k / n), 0.0) for k in range(n)]
        cage = M._default_faces('morph_body', pts)
        base, bt = make(pts, faces=cage)
        turned, tt = make(pts, rotations={0: (10, 0, 0)}, faces=cage)
        tilt = angle(vertex_normal(base, bt, 0), vertex_normal(turned, tt, 0))
        check('a point of valence %d: a rotation of 10 degrees turns the normal of the surface by 10 degrees' % n, abs(tilt - 10.0) < 0.5, '%.2f' % tilt)
    cage = M._default_faces('morph_body', CUBE)
    base, _ = make(CUBE, faces=cage)
    fuller, _ = make(CUBE, influences={0: 1.8}, faces=cage)
    changed = max(math.dist(a, b) for a, b in zip(base, fuller))
    err = max(math.dist(fuller[i], CUBE[i]) for i in range(8))
    check('an influence of 1.8 changes the surface and still goes through the point', changed > 0.5 and err < 1e-9, 'change %.2f, error %.1e' % (changed, err))


def corners_are_sharp():
    # (the user: "CORNER POINTS STIL HAVE TO SHOW SHARP EDGES IN THEIR RANGE OF INFLUENCE"): the edge between two corners is a straight line, the corners are on the surface
    verts, tris = make(CUBE, corners=(0, 1))
    a, b = CUBE[0], CUBE[1]
    ab = [b[k] - a[k] for k in range(3)]
    worst, count = 0.0, 0
    for v in verts:
        t = sum((v[k] - a[k]) * ab[k] for k in range(3)) / sum(x * x for x in ab)
        if 0.0 < t < 1.0:
            proj = [a[k] + t * ab[k] for k in range(3)]
            d = math.dist(v, proj)
            if d < 0.5:
                count += 1
                worst = max(worst, d)
    err = max(math.dist(verts[i], CUBE[i]) for i in range(8))
    check('the edge between two corner points is a straight line (%d vertices on it)' % count, count >= 8 and worst < 1e-9, '%.1e' % worst)
    check('the points of a body with two corners are on its surface', err < 1e-9, '%.1e' % err)


def corner_influence():
    # (the user, 2026-11-10: "the influence for the corner points"; how far the sharp edges of a corner run): a cube with one corner point and seven spline points -- the three edges of the corner are sharp for part of their length and end in the smooth surface.  An influence of
    # the corner above 1 makes the sharp run longer and sharper, below 1 shorter and softer; the points stay on the surface whatever it is.  The sharpness is the largest turn between two neighbouring triangles of the drawn mesh
    def sharpest(verts, tris):
        normals = []
        for a_, b_, c_ in tris:
            p, q, r = verts[a_], verts[b_], verts[c_]
            u = [q[k] - p[k] for k in range(3)]
            w = [r[k] - p[k] for k in range(3)]
            cr = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
            ln = math.sqrt(sum(x * x for x in cr)) or 1.0
            normals.append([x / ln for x in cr])
        seen, worst = {}, 0.0
        for ti, (a_, b_, c_) in enumerate(tris):
            for e in ((a_, b_), (b_, c_), (c_, a_)):
                key = (min(e), max(e))
                if key in seen:
                    worst = max(worst, angle(normals[ti], normals[seen[key]]))
                else:
                    seen[key] = ti
        return worst
    got = {}
    for k in (0.5, 1.0, 1.8):
        verts, tris = make(CUBE, corners=(0,), influences={0: k}, pieces=32)
        err = max(math.dist(verts[i], CUBE[i]) for i in range(8))
        got[k] = (sharpest(verts, tris), err, verts)
    check('the influence of a corner point: its sharpest turn is %.1f, %.1f and %.1f degrees at the influence 0.5, 1 and 1.8 (more influence, a longer and sharper edge)' % (got[0.5][0], got[1.0][0], got[1.8][0]),
          got[0.5][0] < got[1.0][0] < got[1.8][0], [got[k][0] for k in got])
    check('the influence of a corner point changes the surface (by %.2f and %.2f) and the points stay on it' % (max(math.dist(p, q) for p, q in zip(got[1.0][2], got[0.5][2])), max(math.dist(p, q) for p, q in zip(got[1.0][2], got[1.8][2]))),
          all(got[k][1] < 1e-9 for k in got) and max(math.dist(p, q) for p, q in zip(got[1.0][2], got[1.8][2])) > 0.5, [got[k][1] for k in got])


def sphere_point_is_round():
    # (the user, 2026-11-10: a sphere point "should be round and have infinite symmetry axis at the point, so sphere like. But the curvature does not need to be that of a perfect sphere"): a sphere point of radius r at a corner of a box of 10 whose other
    # points are corners: round about its axis -- the curvature seen from the tip, 2 h / rho^2 (h the depth below the tangent plane at the tip, rho the distance along it), is nearly the same in every direction -- and about the curvature of a sphere of its radius
    # (0.5 for r = 2, 0.25 for r = 4) half a radius from the tip, falling steadily away from it
    corners = (0, 1, 2, 3, 4, 5, 7)
    for r in (2.0, 4.0):
        radii = [0, 0, 0, 0, 0, 0, r, 0]
        verts, tris = make(CUBE, corners=corners, radii=radii, pieces=32)
        tip = verts[6]
        normal = vertex_normal(verts, tris, 6)
        if sum(normal) < 0:
            normal = [-x for x in normal]
        rows = {}
        for frac in (0.5, 1.0):
            vals = []
            for v in verts:
                if abs(math.dist(v, tip) - frac * r) < 0.08 * r:
                    e = [tip[k] - v[k] for k in range(3)]
                    h = sum(e[k] * normal[k] for k in range(3))
                    rho2 = sum(x * x for x in e) - h * h
                    if rho2 > 1e-6:
                        vals.append(2.0 * h / rho2)
            rows[frac] = vals
        near, far = rows[0.5], rows[1.0]
        want = 1.0 / r
        check('a sphere point of radius %g: round about its axis -- the curvature half a radius from the tip is %.3f to %.3f in every direction (%d vertices), a sphere of its radius has %.3f' % (r, min(near), max(near), len(near), want),
              len(near) >= 20 and max(near) - min(near) < 0.5 * want and 0.8 * want < sum(near) / len(near) < 1.2 * want, [min(near), max(near)])
        check('a sphere point of radius %g: the curvature falls away from the tip, %.3f at one radius against %.3f at half a radius' % (r, sum(far) / len(far), sum(near) / len(near)), sum(far) / len(far) < sum(near) / len(near), [min(far), max(far)])
    # eight sphere points of half an edge: a sphere
    verts, tris = make(CUBE, radii=[5.0] * 8)
    d = [math.sqrt(x * x + y * y + z * z) for x, y, z in verts]
    check('eight sphere points of half an edge make a sphere of that radius to within 3 per cent (the surface through eight points is not an exact sphere, and a sphere point is round, not an exact ball)', min(d) > 4.85 and max(d) < 5.05, '%.3f .. %.3f' % (min(d), max(d)))


def pinched_points():
    # a cloud of 120 points on a rough sphere: the faces between its points leave points where two sheets of the surface touch (27 of them with this seed); each is split into one copy for every sheet, so the surface is still the subdivision surface through the
    # points, and not an error for a cage that is not a closed surface
    import random
    random.seed(3)
    pts = []
    for k in range(120):
        z = 1 - 2 * (k + 0.5) / 120
        r = math.sqrt(1 - z * z)
        a = k * math.pi * (3 - math.sqrt(5))
        s = 10 * (1 + 0.15 * random.random())
        pts.append((s * r * math.cos(a), s * r * math.sin(a), s * z))
    verts, tris = make(pts, pieces=8)
    err = max(math.dist(verts[i], pts[i]) for i in range(len(pts)))
    check('a cloud of 120 points with points where two sheets touch: the surface goes through its points', err < 1e-3, '%.1e' % err)


def any_number_of_pieces():
    # (the user, 2026-11-10: the mesher has an argument that says how fine it meshes, it does not subdivide a mesh that is there): the pieces to a cage edge are any even number, the mesh is one closed piece (every edge of
    # a triangle is shared by two, V - E + F = 2) and the surface still goes through the points
    cases = (('a cube', CUBE, None),
             ('a cube with a point pressed out of the top face (faces of three and four)', CUBE + [(0.3, -0.2, 7.5)], None),
             ('a bipyramid of valence 5 (faces of three)', [(0.0, 0.0, 6.0), (0.0, 0.0, -6.0)] + [(5 * math.cos(2 * math.pi * k / 5), 5 * math.sin(2 * math.pi * k / 5), 0.0) for k in range(5)], None))
    for label, pts, faces in cases:
        cage = faces if faces is not None else M._default_faces('morph_body', pts)
        for pieces in (6, 12, 20, 26):
            verts, tris = make(pts, faces=cage, pieces=pieces)
            edges = {}
            for a, b, c in tris:
                for u, v in ((a, b), (b, c), (c, a)):
                    edges[(min(u, v), max(u, v))] = edges.get((min(u, v), max(u, v)), 0) + 1
            closed = all(n == 2 for n in edges.values())
            euler = len(verts) - len(edges) + len(tris)
            err = max(min(math.dist(v, p) for v in verts) for p in pts)
            check('%s at %d pieces to an edge: %d vertices, one closed mesh (Euler %d) and through its points' % (label, pieces, len(verts), euler), closed and euler == 2 and err < 1e-6, (closed, euler, '%.1e' % err))      # (1e-6: the weights of a vertex under 1e-9 are dropped, valence 5 and faces of three leave 1e-7)


def scattered_bodies():
    # (the user, 2026-11-10: a body of scattered points, 23 and their mirror images with four corners among them, was drawn by the old ribbon code when its faces were not a closed surface): the faces the program makes for such points go round the same way and close
    # -- some points are pinched where two sheets touch, some have two edges --, the surface is one closed mesh and goes through the spline points
    import random
    for seed in (7, 8, 9, 10, 11):
        random.seed(seed)
        half = []
        while len(half) < 23:
            x, y, z = random.gauss(0, 1), random.gauss(0, 1), random.gauss(0, 1)
            n = math.sqrt(x * x + y * y + z * z)
            x, y, z = x / n, y / n, z / n
            if x < 0.25:
                continue
            r = 1.0 + random.uniform(-0.22, 0.22)
            p = (x * r, y * r * 1.25, z * r * 0.9)
            if all(math.dist(p, q) > 0.35 for q in half):
                half.append(p)
        pts = []
        for p in half:
            pts.append(p)
            pts.append((-p[0], p[1], p[2]))
        corners_ = (3, 12, 20, 31)
        verts, tris = make(pts, corners=corners_, pieces=8)
        edges = {}
        for a, b, c in tris:
            for u, v in ((a, b), (b, c), (c, a)):
                edges[(min(u, v), max(u, v))] = edges.get((min(u, v), max(u, v)), 0) + 1
        closed = all(k == 2 for k in edges.values())
        err = max(min(math.dist(v, p) for v in verts) for i, p in enumerate(pts))
        check('46 scattered points (seed %d, four corners): one closed mesh of %d vertices (Euler %d) through its points' % (seed, len(verts), len(verts) - len(edges) + len(tris)), closed and err < 1e-6, (closed, '%.1e' % err))


def kernel_levels_match_the_rule():
    # (the weights of the vertices of the finest mesh over the points of the net are made by the kernel, libfive_cc_levels; the rule it follows is the one of M._cc_step -- here the reference, with the limit as it was written in Python before: n^2 v + 4 sum of the edge neighbours
    # + sum of the diagonal neighbours over n (n + 5), a corner itself, two crease edges (e1 + 4 v + e2) / 6): the same rows, the same quadrilaterals and corners, the same triangles, on cages of quadrilaterals and triangles, with corners and without
    import random

    def limit(verts, faces, kind, creases):
        nv = len(verts)
        en, dn = [set() for _ in range(nv)], [set() for _ in range(nv)]
        for q in faces:
            for k in range(4):
                en[q[k]].add(q[(k + 1) % 4])
                en[q[k]].add(q[k - 1])
                dn[q[k]].add(q[(k + 2) % 4])
        cnb = M._crease_neighbours(nv, creases)
        out = []
        for v in range(nv):
            if kind[v] == M._K_CORNER or (kind[v] == M._K_AUTO and len(cnb[v]) >= 3):
                out.append(dict(verts[v]))
            elif kind[v] == M._K_AUTO and len(cnb[v]) == 2:
                out.append(M._lin([(verts[v], 4.0 / 6.0), (verts[cnb[v][0]], 1.0 / 6.0), (verts[cnb[v][1]], 1.0 / 6.0)]))
            else:
                n = len(en[v])
                denom = n * (n + 5.0)
                out.append(M._lin([(verts[v], n * n / denom)] + [(verts[e], 4.0 / denom) for e in en[v]] + [(verts[d], 1.0 / denom) for d in dn[v]]))
        return out

    def reference(topo, level):
        verts, faces, kind, creases = [{k: 1.0} for k in range(len(topo['v1']))], topo['f1'], topo['kind1'], topo['creases1']
        par = [(fi, ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0))) for fi in range(len(faces))]
        for _ in range(1, level):
            verts, faces, kind, creases, par = M._cc_step(verts, faces, kind, creases, par)
        rows = limit(verts, faces, kind, creases)
        tris = []
        for a, b, c, d in faces:
            tris.append((a, b, c))
            tris.append((a, c, d))
        return rows, tris, faces, par

    cubes = M._default_faces('body', [(float(x), float(y), float(z)) for z in (0, 1) for y in (0, 1) for x in (0, 1)])
    bipyramid = [(5, i, (i + 1) % 5) for i in range(5)] + [(6, (i + 1) % 5, i) for i in range(5)]
    random.seed(3)
    pts = [(random.gauss(0, 1), random.gauss(0, 1), random.gauss(0, 1)) for _ in range(30)]
    scattered = M._default_faces('body', [tuple(p) for p in pts])
    worst, compared, wrong = 0.0, 0, []
    for label, cage, sharp_sets in (('a cube', cubes, [frozenset(), frozenset([0]), frozenset([0, 3, 5, 6])]), ('a bipyramid of valence 5', bipyramid, [frozenset(), frozenset([5])]),
                                    ('30 scattered points', scattered, [frozenset(), frozenset([2, 7, 11, 19])])):
        for sharp in sharp_sets:
            topo = M._subdiv_topology([tuple(f) for f in cage], sharp)
            if topo is None:
                wrong.append('%s: no topology' % label)
                continue
            for level in (1, 2, 3, 4):
                topo['levels'].clear()
                got = M._subdiv_level(topo, level)
                rows, tris, faces, par = reference(topo, level)
                if got is None or len(got[0]) != len(rows) or got[1] != tris:
                    wrong.append('%s, %d corners, level %d: rows or triangles differ' % (label, len(sharp), level))
                    continue
                for have, want in zip(got[0], rows):
                    d = dict(have)
                    for k, w in want.items():
                        if abs(w) > 1e-9:
                            worst = max(worst, abs(d.get(k, 0.0) - w))
                    compared += 1
                f2, p2 = M._faces_par(topo, level)
                if [tuple(f) for f in f2] != [tuple(f) for f in faces] or [tuple(p) for p in p2] != [tuple(p) for p in par]:
                    wrong.append('%s, %d corners, level %d: the faces or their corners in the quadrilateral differ' % (label, len(sharp), level))
    check('the kernel makes the weights of every vertex of the finest mesh as the rule of M._cc_step and the limit do (%d rows compared on a cube, a bipyramid and 30 scattered points, with corners and without, at levels 1 to 4): the same rows to %.1e, the same triangles, faces and corners' % (compared, worst),
          not wrong and worst < 1e-12 and compared > 1000, wrong[:3])


def controls_match_the_dense_rule():
    # (the control points are made by the kernel, libfive_cc_controls: for a body of up to 400 points it is ONE dense solve, the one this check writes out as it was in Python before -- the weights B of the control point at point i are the solution y of (L L^T + mu I) y = L e_i,
    # for the points within M._REACH edges of i, made to add up to 1 -- so the bodies made until now are as they were)
    import random

    def invert(m):
        n = len(m)
        a = [list(row) + [1.0 if i == j else 0.0 for j in range(n)] for i, row in enumerate(m)]
        for col in range(n):
            piv = max(range(col, n), key=lambda r: abs(a[r][col]))
            a[col], a[piv] = a[piv], a[col]
            d = a[col][col]
            a[col] = [x / d for x in a[col]]
            for r in range(n):
                if r != col and a[r][col] != 0.0:
                    f = a[r][col]
                    a[r] = [x - f * y for x, y in zip(a[r], a[col])]
        return [row[n:] for row in a]

    def reference(topo, cage):
        nv, sharp, v1, f1 = len(topo['orig']), topo['sharp'], topo['v1'], topo['f1']
        ring_e, ring_f = [set() for _ in range(nv)], [set() for _ in range(nv)]
        for q in f1:
            ring_e[q[0]].update((q[1], q[3]))
            ring_f[q[0]].add(q[2])
        L = []
        for v in range(nv):
            if v in sharp:
                L.append({v: 1.0})
                continue
            n = len(ring_e[v])
            denom = n * (n + 5.0)
            L.append(M._lin([(v1[v], n * n / denom)] + [(v1[e], 4.0 / denom) for e in ring_e[v]] + [(v1[f], 1.0 / denom) for f in ring_f[v]]))
        near = [set() for _ in range(nv)]
        for f in cage:
            for k in range(len(f)):
                near[f[k]].add(f[(k + 1) % len(f)])
                near[f[(k + 1) % len(f)]].add(f[k])
        Lm = [[L[i].get(j, 0.0) for j in range(nv)] for i in range(nv)]
        G = [[sum(x * y for x, y in zip(Lm[i], Lm[j])) + (1e-7 if i == j else 0.0) for j in range(nv)] for i in range(nv)]
        inv = invert(G)
        rows = []
        for i in range(nv):
            if i in sharp:
                rows.append({i: 1.0})
                continue
            within, frontier = {i}, [i]
            for _ in range(M._REACH):
                frontier = [w for u in frontier for w in near[u] if w not in within and not within.add(w)]
            y = [sum(Lm[k][i] * inv[k][j] for k in range(nv)) if j in within else 0.0 for j in range(nv)]
            total = sum(y)
            rows.append({j: w / total for j, w in enumerate(y) if abs(w) > 1e-9})
        return rows

    cubes = M._default_faces('body', [(float(x), float(y), float(z)) for z in (0, 1) for y in (0, 1) for x in (0, 1)])
    bipyramid = [(5, i, (i + 1) % 5) for i in range(5)] + [(6, (i + 1) % 5, i) for i in range(5)]
    random.seed(3)
    scattered = M._default_faces('body', [(random.gauss(0, 1), random.gauss(0, 1), random.gauss(0, 1)) for _ in range(30)])
    worst, compared, wrong = 0.0, 0, []
    for label, cage, sharp_sets in (('a cube', cubes, [frozenset(), frozenset([0]), frozenset([0, 3, 5, 6])]), ('a bipyramid of valence 5', bipyramid, [frozenset(), frozenset([5])]),
                                    ('30 scattered points', scattered, [frozenset(), frozenset([2, 7, 11, 19])])):
        for sharp in sharp_sets:
            cage = [tuple(f) for f in cage]
            topo = M._subdiv_topology(cage, sharp)
            if topo is None or len(topo['orig']) != 1 + max(max(f) for f in cage):
                wrong.append('%s, %d corners: no topology, or a pinched point' % (label, len(sharp)))
                continue
            want = reference(topo, cage)
            for have, w in zip(topo['B'], want):
                d = dict(have)
                for j in set(d) | set(w):
                    worst = max(worst, abs(d.get(j, 0.0) - w.get(j, 0.0)))
                compared += 1
    check('the weights of the control points the kernel makes are the dense solution as it was (%d rows on a cube, a bipyramid and 30 scattered points, with corners and without): the same to %.1e' % (compared, worst), not wrong and worst < 1e-6 and compared >= 90, wrong[:3])


def more_than_240_points():
    # (the user, 2026-11-10: "I dont see why there would a reason to restrict the user"): a body of more points than the 240 it was once limited to -- here 420, which is also more than the 400 that the kernel solves on one dense matrix, so it is made of patches -- is one closed
    # surface and goes through its points
    import random
    random.seed(21)
    pts = []
    while len(pts) < 420:
        x, y, z = random.gauss(0, 1), random.gauss(0, 1), random.gauss(0, 1)
        n = math.sqrt(x * x + y * y + z * z)
        p = (10.0 * x / n, 10.0 * y / n, 10.0 * z / n)
        if all(math.dist(p, q) > 0.8 for q in pts):
            pts.append(p)
    started = time.time()
    verts, tris = make(pts, pieces=2)
    edges = {}
    for a, b, c in tris:
        for u, v in ((a, b), (b, c), (c, a)):
            edges[(min(u, v), max(u, v))] = edges.get((min(u, v), max(u, v)), 0) + 1
    closed = all(k == 2 for k in edges.values())
    err = max(min(math.dist(v, p) for v in verts) for p in pts)
    check('420 points on a sphere: one closed mesh of %d vertices (Euler %d) through its points, made in %.1f s' % (len(verts), len(verts) - len(edges) + len(tris), time.time() - started), closed and err < 1e-6 and len(verts) - len(edges) + len(tris) == 2, (closed, '%.1e' % err))


for name, fn in (('through_the_points', through_the_points), ('local_changes', local_changes), ('rotation_and_influence', rotation_and_influence), ('scattered_bodies', scattered_bodies), ('kernel_levels_match_the_rule', kernel_levels_match_the_rule),
                 ('controls_match_the_dense_rule', controls_match_the_dense_rule), ('more_than_240_points', more_than_240_points),
                 ('corners_are_sharp', corners_are_sharp), ('corner_influence', corner_influence), ('sphere_point_is_round', sphere_point_is_round), ('pinched_points', pinched_points),
                 ('any_number_of_pieces', any_number_of_pieces)):
    if len(sys.argv) > 1 and name not in sys.argv[1:]:
        continue
    try:
        fn()
    except Exception:
        import traceback
        traceback.print_exc()
        bad.append(name)
        print('FAIL  %s raised' % name)

print('%d failed, %.1f s' % (len(bad), time.time() - start))
sys.exit(1 if bad else 0)
