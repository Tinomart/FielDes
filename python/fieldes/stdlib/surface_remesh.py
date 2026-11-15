'''
The surface of a body as a mesh of near-equilateral triangles (H223, the user, 2026-11-14, of mesh_surface_tri on the real bracket: "THAT IS HOW SALOME MESHES TETS IN YOUR VIEW OR WHAT").

Salome's Netgen meshes the SURFACE first -- an advancing front that lays near-equilateral triangles of the size asked on the boundary faces -- and runs its surface optimizer on them (swaps of edges, smoothing of the nodes
along the surface, splits and collapses).  mesh_surface_tri used to be the skin of the tetrahedra that mesh_tet4 cuts from a lattice, whose triangles are whatever the lattice left: neighbours of 2 mm and 9 mm at an element
size of 6 mm.  This is the isotropic remeshing of Botsch and Kobbelt (2004), the same idea as the optimizer: the triangles of the skin are

    split where an edge is longer than 4/3 of the size, collapsed where it is shorter than 4/5,
    flipped where that brings the number of triangles round the nodes nearer to six,
    and the nodes moved along the surface to the middle of their neighbours and put back on it (Newton steps on the field of the body),

a few rounds.  The edges where the surface turns by more than the feature angle, and their end nodes, are kept: they are not moved, flipped or collapsed (an edge on them is split in two).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

__all__ = []


def remesh(body, pos, tris, size, feature_angle=30.0, rounds=6):
    ''' `pos`: [[x, y, z]] the nodes of the skin, `tris`: [(a, b, c)] its triangles, turned outward, `size`: the edge length asked.  Returns (the nodes, the triangles) of the remeshed skin, the nodes that no
        triangle uses left out '''
    from fieldes.stdlib.meshing import _snap_to_surface
    pos = [list(p) for p in pos]
    T = [list(t) for t in tris]
    high, low = 4.0 / 3.0 * size, 4.0 / 5.0 * size
    cos_feature = math.cos(math.radians(feature_angle))

    def length(a, b):
        pa, pb = pos[a], pos[b]
        return math.sqrt((pa[0] - pb[0]) ** 2 + (pa[1] - pb[1]) ** 2 + (pa[2] - pb[2]) ** 2)

    def cross_of(pa, pb, pc):
        ux, uy, uz = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
        vx, vy, vz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
        return (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)

    def normal(t):
        return cross_of(pos[t[0]], pos[t[1]], pos[t[2]])

    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]

    def norm(u):
        return math.sqrt(dot(u, u))

    def key(a, b):
        return (a, b) if a < b else (b, a)

    def smallest_angle(pa, pb, pc):
        worst = 180.0
        for p, q, s in ((pa, pb, pc), (pb, pc, pa), (pc, pa, pb)):
            u = (q[0] - p[0], q[1] - p[1], q[2] - p[2])
            w = (s[0] - p[0], s[1] - p[1], s[2] - p[2])
            lu, lw = math.sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]), math.sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2])
            if lu == 0.0 or lw == 0.0:
                return 0.0
            worst = min(worst, math.degrees(math.acos(max(-1.0, min(1.0, (u[0] * w[0] + u[1] * w[1] + u[2] * w[2]) / (lu * lw))))))
        return worst

    def build():
        edges = {}
        around = [[] for _ in pos]
        for ti, t in enumerate(T):
            if t is None:
                continue
            for q in range(3):
                edges.setdefault(key(t[q], t[(q + 1) % 3]), []).append(ti)
                around[t[q]].append(ti)
        return edges, around

    def ring_of(v, around):
        out = set()
        for ti in around[v]:
            out.update(T[ti])
        out.discard(v)
        return out

    # the edges where the surface turns (or is open): kept as they are
    edges, around = build()
    feature = set()
    for k, ts in edges.items():
        if len(ts) != 2:
            feature.add(k)
            continue
        n0, n1 = normal(T[ts[0]]), normal(T[ts[1]])
        l0, l1 = norm(n0), norm(n1)
        if l0 == 0.0 or l1 == 0.0 or dot(n0, n1) / (l0 * l1) < cos_feature:
            feature.add(k)
    fixed = [False] * len(pos)
    for a, b in feature:
        fixed[a] = fixed[b] = True

    def project(indices):
        if not indices:
            return
        pts = [pos[i] for i in indices]
        done = _snap_to_surface(body, pts, [size] * len(pts))
        for i, q in zip(indices, done):
            if length_to(pos[i], q) <= 0.5 * size:
                pos[i] = [q[0], q[1], q[2]]

    def length_to(p, q):
        return math.sqrt((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2)

    def split(edges):
        mid = {}
        for k, ts in edges.items():
            if len(ts) == 2 and length(*k) > high:
                a, b = k
                pos.append([0.5 * (pos[a][i] + pos[b][i]) for i in range(3)])
                m = len(pos) - 1
                mid[k] = m
                fixed.append(k in feature)
                if k in feature:
                    feature.discard(k)
                    feature.add(key(a, m))
                    feature.add(key(b, m))
        if not mid:
            return 0
        out = []
        for t in T:
            if t is None:
                continue
            a, b, c = t
            m_ab, m_bc, m_ca = mid.get(key(a, b)), mid.get(key(b, c)), mid.get(key(c, a))
            count = (m_ab is not None) + (m_bc is not None) + (m_ca is not None)
            if count == 0:
                out.append(t)
            elif count == 1:
                if m_ab is not None:
                    out += [[a, m_ab, c], [m_ab, b, c]]
                elif m_bc is not None:
                    out += [[b, m_bc, a], [m_bc, c, a]]
                else:
                    out += [[c, m_ca, b], [m_ca, a, b]]
            elif count == 2:
                if m_ab is not None and m_bc is not None:
                    v0, v1, v2, m1, m2 = a, b, c, m_ab, m_bc
                elif m_bc is not None and m_ca is not None:
                    v0, v1, v2, m1, m2 = b, c, a, m_bc, m_ca
                else:
                    v0, v1, v2, m1, m2 = c, a, b, m_ca, m_ab
                out.append([m1, v1, m2])
                if length(v0, m2) <= length(m1, v2):
                    out += [[v0, m1, m2], [v0, m2, v2]]
                else:
                    out += [[v0, m1, v2], [m1, m2, v2]]
            else:
                out += [[a, m_ab, m_ca], [m_ab, b, m_bc], [m_ca, m_bc, c], [m_ab, m_bc, m_ca]]
        T[:] = out
        project([m for m in mid.values() if True])
        return len(mid)

    def collapse(edges, around):
        lock = set()
        done = 0
        chain = {}
        for u, v in feature:
            chain[u] = chain.get(u, 0) + 1
            chain[v] = chain.get(v, 0) + 1
        for k in sorted(edges, key=lambda e: length(*e)):
            if length(*k) >= low:
                break
            a, b = k
            ts = edges[k]
            on_feature = k in feature
            if a in lock or b in lock or len(ts) != 2:
                continue
            if on_feature:
                # (a very short piece of a feature edge between two nodes inside a chain of them -- the sliver that the lattice left at a rim: the two nodes become one, in the middle)
                if length(*k) >= 0.35 * size or chain.get(a, 0) != 2 or chain.get(b, 0) != 2:
                    continue
            elif fixed[a] and fixed[b]:
                continue
            if fixed[b] and not on_feature:
                a, b = b, a                                   # (the node that is kept is the fixed one)
            na, nb = ring_of(a, around), ring_of(b, around)
            opposite = set()
            for ti in ts:
                opposite.update(v for v in T[ti] if v != a and v != b)
            if (na & nb) != opposite:
                continue
            target = pos[a] if (fixed[a] and not on_feature) else [0.5 * (pos[a][i] + pos[b][i]) for i in range(3)]
            ok = True
            for w in nb:
                if w != a and length_to(target, pos[w]) > high:
                    ok = False
                    break
            if not ok:
                continue
            # no triangle round them may turn over or become a sliver
            for node, other in ((b, a), (a, b)):
                for ti in around[node]:
                    tv = T[ti]
                    if other in tv:
                        continue
                    old = normal(tv)
                    new = cross_of(*[(target if v in (a, b) else pos[v]) for v in tv])
                    lo, ln = norm(old), norm(new)
                    if lo == 0.0 or ln == 0.0 or dot(old, new) / (lo * ln) < 0.3:
                        ok = False
                        break
                if not ok:
                    break
            if not ok:
                continue
            for ti in around[b]:
                tv = T[ti]
                if a in tv:
                    T[ti] = None
                else:
                    T[ti] = [a if v == b else v for v in tv]
            pos[a] = [target[0], target[1], target[2]]
            if on_feature:
                feature.discard(k)
                for w in nb:
                    if key(b, w) in feature:
                        feature.discard(key(b, w))
                        if w != a:
                            feature.add(key(a, w))
            lock.update(na)
            lock.update(nb)
            lock.add(a)
            lock.add(b)
            done += 1
        T[:] = [t for t in T if t is not None]
        return done

    def flip(edges, around):
        valence = [len(around[v]) for v in range(len(pos))]
        touched = set()
        done = 0
        for k, ts in edges.items():
            if k in feature or len(ts) != 2 or ts[0] in touched or ts[1] in touched:
                continue
            a, b = k
            t0, t1 = T[ts[0]], T[ts[1]]
            # t0 = (a, b, c) and t1 = (b, a, d), turned the same way: if t0 runs b -> a, the names are exchanged
            i0 = t0.index(a)
            if t0[(i0 + 1) % 3] != b:
                a, b = b, a
                i0 = t0.index(a)
            c = t0[(i0 + 2) % 3]
            i1 = t1.index(b)
            if t1[(i1 + 1) % 3] != a:
                continue
            d = t1[(i1 + 2) % 3]
            if c == d or key(c, d) in edges:
                continue
            before = sum((valence[v] - 6) ** 2 for v in (a, b, c, d))
            after = (valence[a] - 7) ** 2 + (valence[b] - 7) ** 2 + (valence[c] - 5) ** 2 + (valence[d] - 5) ** 2
            if after >= before:
                # (a needle or a cap -- a triangle with a smallest angle under 25 degrees -- is flipped when the pair of triangles has a better smallest angle the other way: the Delaunay criterion)
                worst_now = min(smallest_angle(pos[a], pos[b], pos[c]), smallest_angle(pos[b], pos[a], pos[d]))
                worst_flipped = min(smallest_angle(pos[a], pos[d], pos[c]), smallest_angle(pos[d], pos[b], pos[c]))
                if not (worst_now < 25.0 and worst_flipped > worst_now + 2.0):
                    continue
            n_old = normal(t0)
            n_old = (n_old[0] + normal(t1)[0], n_old[1] + normal(t1)[1], n_old[2] + normal(t1)[2])
            lo = norm(n_old)
            n1 = cross_of(pos[a], pos[d], pos[c])
            n2 = cross_of(pos[d], pos[b], pos[c])
            if lo == 0.0 or norm(n1) == 0.0 or norm(n2) == 0.0 or dot(n1, n_old) / (norm(n1) * lo) < 0.7 or dot(n2, n_old) / (norm(n2) * lo) < 0.7:
                continue
            T[ts[0]] = [a, d, c]
            T[ts[1]] = [d, b, c]
            touched.update(ts)
            done += 1
        return done

    def smooth(around, steps=3):
        for _ in range(steps):
            moves = {}
            for v in range(len(pos)):
                if fixed[v] or not around[v]:
                    continue
                ring = ring_of(v, around)
                if len(ring) < 3:
                    continue
                c = [sum(pos[w][i] for w in ring) / len(ring) for i in range(3)]
                n = [0.0, 0.0, 0.0]
                for ti in around[v]:
                    q = normal(T[ti])
                    ql = norm(q)
                    if ql > 0.0:
                        for i in range(3):
                            n[i] += q[i] / ql
                nl = norm(n)
                if nl < 1e-12:
                    continue
                n = [x / nl for x in n]
                d = [c[i] - pos[v][i] for i in range(3)]
                dn = dot(d, n)
                moves[v] = [pos[v][i] + 0.6 * (d[i] - dn * n[i]) for i in range(3)]
            if not moves:
                return
            ids = list(moves)
            pts = [moves[v] for v in ids]
            done = _snap_to_surface(body, pts, [size] * len(pts))
            for v, q in zip(ids, done):
                if length_to(pos[v], q) <= 0.6 * size:
                    pos[v] = [q[0], q[1], q[2]]

    for _ in range(rounds):
        edges, around = build()
        if split(edges) == 0 and _ > 0:
            pass
        edges, around = build()
        collapse(edges, around)
        edges, around = build()
        flip(edges, around)
        edges, around = build()
        smooth(around)
    # the nodes that are used, numbered again
    used = sorted({v for t in T for v in t})
    number = {v: i for i, v in enumerate(used)}
    return [pos[v] for v in used], [(number[t[0]], number[t[1]], number[t[2]]) for t in T]


def _mirror_skin(pos, tris, axis, at, tol):
    ''' (pos, tris) with the mirror image about the plane `axis` = `at` added: the nodes within `tol` of the plane are put on it and shared, every other node gets an image, every triangle its image turned the other way '''
    new_pos = [list(p) for p in pos]
    image = {}
    for v in sorted({v for t in tris for v in t}):
        p = pos[v]
        if abs(p[axis] - at) <= tol:
            new_pos[v][axis] = at
            image[v] = v
        else:
            q = list(p)
            q[axis] = 2.0 * at - p[axis]
            new_pos.append(q)
            image[v] = len(new_pos) - 1
    out = list(tris)
    for t in tris:
        out.append((image[t[0]], image[t[2]], image[t[1]]))
    return new_pos, out


def remesh_skin(mesh, size, feature_angle=30.0, planes=()):
    ''' A Mesh that holds the skin of tetrahedra (the faces 'tri3'), its triangles made regular in place: see the top of this file.  A mesh with zones of its own is left as it is: the new triangles have no zone.
        `planes` ((axis, at)): the planes the skin is symmetric about (H226): the half on the upper side of them is remeshed -- the edges along a plane are open edges, which the remesher keeps -- and mirrored, so that the
        remeshed skin is symmetric too (the remesher goes through the triangles in an order that a mirror reverses).  A skin that is not cut cleanly by a plane (a triangle with nodes on both sides) is remeshed whole '''
    from array import array
    tris = mesh.faces.get('tri3')
    if tris is None or len(tris) == 0 or mesh.body is None:
        return False
    P = mesh.nodes
    tol = 1e-6 * size
    planes = [(int(a), float(at)) for a, at in planes]
    triples = [(tris[3 * i], tris[3 * i + 1], tris[3 * i + 2]) for i in range(len(tris) // 3)]
    if planes:
        def side(v, axis, at):
            c = P[3 * v + axis] - at
            return 0 if abs(c) <= tol else (1 if c > 0 else -1)
        clean = all(len({side(v, axis, at) for v in t} - {0}) <= 1 for t in triples for axis, at in planes)
        if clean:
            triples = [t for t in triples if all(side(v, axis, at) >= 0 for v in t for axis, at in planes)]
        else:
            planes = []
    ids = sorted({v for t in triples for v in t})
    number = {v: i for i, v in enumerate(ids)}
    pos = [[P[3 * v], P[3 * v + 1], P[3 * v + 2]] for v in ids]
    skin = [(number[t[0]], number[t[1]], number[t[2]]) for t in triples]
    new_pos, new_tris = remesh(mesh.body, pos, skin, size, feature_angle)
    for axis, at in planes:
        new_pos, new_tris = _mirror_skin(new_pos, new_tris, axis, at, 2e-2 * size)       # (a node of the half that the Newton steps of the remesher moved off the plane by a hair is still on it)
    out_nodes = array('d')
    for p in new_pos:
        out_nodes.extend(p)
    out_tris = array('i')
    for t in new_tris:
        out_tris.extend(t)
    mesh.nodes = out_nodes
    mesh.faces = {'tri3': out_tris}
    mesh.face_sizes = {}
    mesh.face_zone = {'tri3': array('i', [0] * len(new_tris))}
    mesh.zone_names = [mesh.zone_names[0] if mesh.zone_names else 'wall']
    return True
