'''
A body made of points: `morph_polyhedron` and `morph_body` -- the 3D equivalent of `profile`.

    a = point(position=(0, 0, 0))         # the eight points, in the order of the corners of a cube: x changes fastest, then y, then z
    b = point(position=(40, 0, 0))
    c = point(position=(0, 30, 0))
    d = point(position=(40, 30, 0))
    e = point(position=(0, 0, 20))
    f = point(position=(40, 0, 20))
    g = point(position=(0, 30, 20))
    h = point(position=(40, 30, 20), smooth=True)
    cube = morph_polyhedron([a, b, c, d, e, f, g, h])        # flat faces and sharp corners, unless a point says smooth=True or radius=r
    blob = morph_body([a, b, c, d, e, f, g, h])        # the same eight points, every point a spline point unless it says smooth=False or radius=r

The surface of the body is a cage of faces whose corners are the points -- without a list of faces named after the body the faces of the convex hull of the points (the six quads of a cube for the eight points above; four
points or more that are not all in one plane; a flat piece of four corners is one face of four, any other face is a triangle).  `faces=` says them when they are not the hull's -- after a press on
the surface has put a point there, or for a body that is not convex: three or four point numbers each, counterclockwise seen from outside.  PRESS THE SURFACE AND DRAG, as you press the outline of a profile: a new point is put where you pressed, the face that was there is split round it, and
the point goes where you drag it -- out of the surface (a bump) or into it (a dent).  The points are ordinary points (`point(position=(x, y, z))`, dragged with their gizmo).  Each is one of three kinds, as
the points of a `profile` are:

    a corner       the surface passes through the point and the faces meet in a sharp corner there (`smooth=False`, the default of `morph_polyhedron`); the edge between two corners is a straight sharp edge,
                   so a body of corners only is exactly the polyhedron of its faces
    a spline point the surface passes THROUGH the point and is smooth there, as the line of a spline goes through its points (`smooth=True`, the default of `morph_body`).  It is made the way the
                   line of a spline is: every edge of the cage is a cubic curve between its two points that leaves a spline point the way the spline leaves it -- along the edge, in the tangent plane
                   of the point (a spline of a profile leaves a point along the line through its neighbours; the tangent plane is the plane that is across the mean of the normals of the faces
                   that meet at the point) --, and each face is the surface that those curves hold (a patch of its four edges; a face of three is three such patches, one at each of its corners) -- and along an edge between two spline points the two faces have ONE tangent plane all the way, exactly, so the surface is smooth there and not only at the points.  The gold ring of the point's
                   gizmo -- its influence, and the wheel -- is how strongly the surface leaves it: 1, the default, is the spline of a profile (Catmull-Rom); more is rounder, less is tighter
    a sphere point `point(..., radius=r)`: a rounded corner -- the corner is cut by about a sphere of that radius (on a box, the cut is r * 0.73 deep along the diagonal and the rounding reaches about r
                   along each edge), so the surface does not reach the point; the scale of the point's gizmo and its influence multiply the radius, as they do the radius of a round corner of a profile.  The radius
                   grows with the scale up to half the shortest edge at the point and no further: a cube whose eight points have that radius is a sphere

The surface is made ONCE, here, as expressions of the numbers of the points: the viewport evaluates the control points of its patches (each once) at every step of a drag and draws the triangles
between sums of them directly -- the number of triangles does not cost more evaluations --, so a point follows
the hand at once -- nothing is meshed from a field to draw the body --, and the field that operations use (a union with it, an offset) is the distance to a coarser tessellation of the same
expressions, so the two cannot disagree except by the finer faceting of what is drawn.  The ROTATION of a spline point -- the rotate numbers of its gizmo -- turns the plane the surface leaves it in, as it turns the direction a curve leaves a spline point (see curves.py): about x, then y, then z, in degrees; with no rotation the plane comes from the faces that meet there.  The rotation of a corner or of a sphere point is not used.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import array
import ctypes
import gc
import math
import sys

from fieldes import ffi
from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib import curves as C
from fieldes.stdlib.shapes import _own, _bounded

__all__ = ['morph_polyhedron', 'morph_body']

# The corners of the cage are the bits of their number: x is 1, y is 2, z is 4.  Each face is four corners in the order that turns counterclockwise seen from outside
_QUADS = ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5))
CUBE_FACES = _QUADS

# The field that operations use (a union, an offset, a rounding, an export, the selection of an edge or of a face) is the exact signed distance to the TRIANGLES of the surface, made by the native structure that a mesh
# import and a tessellated STEP part have (boxes round the triangles: an evaluation looks at a few of them, not at all of them).  The user, 2026-11-02: "UNDERLYING SHOULD ALWAYS BE A FUNCTIONING FIELD THAT CAN BE INTERACTED WITH
# PROPERLY" -- it used to be an expression summed over every triangle, a lower bound of the distance near a corner of a triangle, that cost an evaluation of every triangle at every place: rounding the edges of a
# body of points did not finish, and the search for the edge or the face a click is on ran out of time.  How many pieces each edge of the cage is cut in for it (what is drawn: see _drawn_segments): a cube of 12
# pieces is 1728 triangles; a cage of more faces gets fewer pieces to an edge, so that it stays a few thousand triangles
def _field_segments(cage):
    faces = len(cage)
    return 12 if faces <= 12 else 10 if faces <= 24 else 8 if faces <= 64 else 4
# The least a patch goes across an edge between two spline points, as a part of the vector across it (see row_core in _morph): the faces on the two sides of an edge cannot fold over each other
_ALPHA_MIN = 0.1
# How far the radius of a sphere point can go, as a part of the shortest edge of the cage at the point: the radius that is used is r itself, up to this much of the shortest edge and no further however far the
# scale is turned up -- half an edge is where the roundings at the two ends of an edge meet, and a cube whose eight points have it is a sphere (the user, 2026-11-02: "the Radius still does not make the
# shape end in a proper sphere")
_SPHERE_LIMIT = 0.5
# How far in along the normal a sphere of radius r cuts the corner of a box: r * (sqrt(3) - 1)
_SPHERE_DEPTH = math.sqrt(3.0) - 1.0
# How deep a sphere point may cut a corner, in radii, however narrow the corner is (a ball in a wedge sits r / sin(half the angle) behind the corner), and the weight of the old normal in the solution of its faces (see _morph)
_SPHERE_DEPTH_MAX = 3.0
_SPHERE_FACES_RIDGE = 0.01
# How far each edge that leaves a sphere point leaves it in the plane across the normal, as a part of the radius used (about the distance from the corner at which a sphere of that radius touches the
# faces of a box): after that the edge is on its own line again, so that the rounding is where the corner is and does not dent the whole edge
_SPHERE_REACH = 0.6        # (was 1.0: with a neighbour a few millimetres away -- a point dragged out of the surface of a body of sphere points -- the edges that leave a sphere point reached across the next point and the surface crumpled; 0.6 does not, and a cube of eight sphere points is the same ball: the user, 2026-11-04, H126)
# Between two sphere points the faces meet in one tangent plane all along the edge, as they do at a spline, in the measure that the radius is what is possible there (see smooth_row: rho = the radius used as a
# part of half the shortest edge; the weight is rho to the power 4, so that a small radius leaves the edge an edge with flat faces and the full one makes the sphere), and the face leaves the edge
# 1 + this x rho times as strongly as a spline's does (the factor on alpha in row_core).  Measured against a true sphere, a cube of 40 whose eight points have radius 20 passes through the sphere at its
# corners and the middles of its edges, and without the factor it dips about 2 at the middles of its faces and 1 in between; at 1.7 it is within 0.55 everywhere (measured at 1.0 / 1.5 / 1.75 / 2.0 / 3.0:
# the middle of a face 1.96 / 0.51 / -0.21 / -0.90 / -3.47 off, the directions in between 0.96 / 0.66 / 0.53 / 0.41 / 0.03)
_SPHERE_GAIN = 0.7
# Along an edge between a corner point and a sphere point the crease of the corner fades out toward the sphere point and is gone at its middle, where the faces have one tangent plane (the user, 2026-11-02:
# "way too sutble letting the edge point affect it and showing a clear side up until the very center of its influence"): how far from the sphere point the smooth row reaches, as this many radii used,
# as a part of the edge (see fade_row in _morph); a spline point has no radius, its smooth row reaches all the way.  Measured on a cube of 5 with one point pulled out (the angle between the two faces
# along the edge from a corner point to the sphere point, in eight pieces, a sharp edge being 89): without the fade 89 89 89 89 87 85 77 37 for a radius of 1; at 2 radii 89 88 86 84 80 75 65 31; at 8
# 88 86 82 75 62 52 34 16, as a spline point at the end has it (88 84 77 70 61 46 37 23): the crease is gone toward the middle of the sphere point's influence
_SPHERE_FADE = 8.0
# How much of the smooth row the edge between two sphere points takes: rho to this power (rho: the radius as a part of what the point can show).  The user, 2026-11-02, of this logic in the build: "The sphere point does
# actually work almost perfectly the way I want" -- what he found wrong was the DEFAULT radius (see profile_edit._toggle), so the weight stays as it was and is not tuned here
_SPHERE_EDGE_POWER = 4


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _scale(s, a):
    return (a[0] * s, a[1] * s, a[2] * s)


def _norm(a):
    return (a[0].square() + a[1].square() + a[2].square()).sqrt() + 1e-9


def _unit(a):
    n = _norm(a)
    return (a[0] / n, a[1] / n, a[2] / n)


def _native_field(controls, vertices, triangles):
    ''' The signed distance to the surface (negative inside) as the NATIVE field of its triangles, with the numbers the points have when the script runs: the same exact, fast structure that a mesh import has.  The
        body is drawn from the numbers of its points at every step of a drag (see Shape::morphNow); a point that was dragged is in the field when the script runs again as it is let go.  `controls` are the
        control points of the patches (three expressions each), `vertices[v]` the vertex v as a list of (the number of a control point, its weight), `triangles` three vertex numbers each '''
    flat = C.values_of([c[a] for c in controls for a in range(3)])          # (all together: see curves.values_of)
    numbers = [tuple(flat[3 * k:3 * k + 3]) for k in range(len(controls))]
    xyz = array.array('f')
    for terms in vertices:
        x = y = z = 0.0
        for c, weight in terms:
            p = numbers[c]
            x += weight * p[0]
            y += weight * p[1]
            z += weight * p[2]
        xyz.extend((x, y, z))
    tri = array.array('I')
    for t in triangles:
        tri.extend(t)
    info = ffi.libfive_mesh_import_info_t()
    ptr = lib.libfive_mesh_from_arrays((ctypes.c_float * len(xyz)).from_buffer(xyz), len(xyz) // 3, (ctypes.c_uint32 * len(tri)).from_buffer(tri), len(tri) // 3, 1.0, ctypes.byref(info))
    if not ptr:
        raise ValueError('a body of points: its surface has no triangles to make a field of (%s)' % lib.libfive_import_mesh_last_message().decode('utf-8', 'replace'))
    return Shape(ptr)


def _face_normal(P, face):
    ''' Twice the area of a face, as a vector that faces out (the faces turn counterclockwise seen from outside) '''
    if len(face) == 4:
        return _cross(_sub(P[face[2]], P[face[0]]), _sub(P[face[3]], P[face[1]]))
    return _cross(_sub(P[face[1]], P[face[0]]), _sub(P[face[2]], P[face[0]]))


def _normals(P, cage):
    ''' The normal at each point: across the plane that the faces that meet there make together (the faces weighed by their area), facing out '''
    out = []
    for i in range(len(P)):
        total = None
        for face in cage:
            if i in face:
                n = _face_normal(P, face)
                total = n if total is None else _add(total, n)
        out.append(_unit(total) if total is not None else (Shape.wrap(0.0), Shape.wrap(0.0), Shape.wrap(1.0)))
    return out


def _turn(v, rotation):
    ''' The vector v turned the way the gizmo turns a shape -- about x, then y, then z, in degrees: curves.rotated, as expressions of the numbers of the rotation (they move while a ring is dragged) '''
    x, y, z = v
    for axis in range(3):
        angle = rotation[axis] * (math.pi / 180.0)
        c, s = angle.cos(), angle.sin()
        if axis == 0:
            y, z = y * c - z * s, y * s + z * c
        elif axis == 1:
            x, z = x * c + z * s, z * c - x * s
        else:
            x, y = x * c - y * s, x * s + y * c
    return (x, y, z)


def _bernstein(t):
    s = 1.0 - t
    return (s * s * s, 3.0 * t * s * s, 3.0 * t * t * s, t * t * t)


def _combine(terms):
    ''' The sum of `weight * point` over (weight, point) pairs, the weights plain numbers '''
    out = []
    for k in range(3):
        total = None
        for weight, point in terms:
            if abs(weight) < 1e-12:
                continue
            term = point[k] * weight
            total = term if total is None else total + term
        out.append(total if total is not None else Shape.wrap(0.0))
    return tuple(out)


def _drawn_segments(cage):
    ''' How many pieces each edge of the cage is drawn in.  The viewport shades every triangle flat -- the normal is the triangle's own --, so a rounded body only looks smooth when its triangles are
        small: a turn of two or three degrees from one to the next.  A cage of more faces gets fewer pieces to a face, so that what is drawn stays a few thousand triangles.  What it costs a drag
        step does not depend on it: the surface is evaluated at the control points of its patches (see _tessellate), and every vertex is a sum of those '''
    faces = len(cage)
    base = 20 if faces <= 12 else 16 if faces <= 24 else 10 if faces <= 64 else 6
    # The number follows the resolution of the view, as everything that is drawn does (the user, 2026-11-02: "It does not appear to respond to the resolution when you set it"): the pieces above are
    # the ones at the resolution 10, and a resolution of 5 draws half as many, one of 20 twice as many -- at least 4 to an edge, at most 40, an even number
    try:
        from fieldes import view
        res = view._scene_resolution()
    except Exception:
        res = None
    n = int(round(base * res / 10.0)) if res else base
    n = max(4, min(40, n))
    return n + (n & 1)


_PATCH_TERMS = {}


def _patch_terms(pieces, keys):
    ''' The weights of the vertices of ONE patch of four, `pieces` to an edge: for each place (k, l) of it the terms (slot, weight) -- a slot is 3 * (4 * i + j) + which, where (i, j) is a control point
        of the net and `which` is 0 for the net's own point and 1 or 2 for the two versions of an inner one (see `gregory`); `keys` are the (i, j) that have two versions.  They depend on the number of
        pieces and on which points have two versions, and not on the patch (the user, 2026-11-02, H70: a point put into a smooth body took 1.2 s, and the first call of _tessellate was 650 ms of it, the
        weights worked out again for every vertex of every patch), so they are worked out once per number of pieces '''
    key = (pieces, keys)
    table = _PATCH_TERMS.get(key)
    if table is None:
        table = {}
        for l in range(pieces + 1):
            for k in range(pieces + 1):
                u, v = k / float(pieces), l / float(pieces)
                bu, bv = _bernstein(u), _bernstein(v)
                # (how near the place is to each of the two edges at a corner: the version for an edge counts u / (u + v) there, v / (u + v) for the other -- where it is 1 on that edge)
                near = {(1, 1): (u, v), (2, 1): (1.0 - u, v), (2, 2): (1.0 - u, 1.0 - v), (1, 2): (u, 1.0 - v)}
                out = []
                for i in range(4):
                    for j in range(4):
                        w = bu[i] * bv[j]
                        if abs(w) <= 1e-12:
                            continue
                        if (i, j) in keys:
                            s, t = near[(i, j)]
                            wa, wb = (0.5, 0.5) if s + t < 1e-12 else (s / (s + t), t / (s + t))
                            out.append((3 * (4 * i + j) + 1, w * wa))
                            out.append((3 * (4 * i + j) + 2, w * wb))
                        else:
                            out.append((3 * (4 * i + j), w))
                table[(k, l)] = tuple(out)
        _PATCH_TERMS[key] = table
    return table


def _tessellate(cage, V, edge, n, gregory, triangle):
    ''' The surface of the cage as triangles between vertices, every edge of the cage drawn in `n` pieces (an even number).  `V` are the places of the points, `edge(i, j)` the control point next to
        point i on the cubic curve from i to j.  A face of four is a bicubic Gregory patch -- see `gregory` in _morph: the curves of its four edges, and inside them the surface that leaves each
        edge in the plane the face on the other side leaves it in --, and a face of three is three such patches, one at each of its corners (`triangle` in _morph): the face is cut by the lines from
        the middle of each of its edges to its middle, so that all that is drawn is patches of four.  The curve of an edge is the same from both faces that share it, so the surface has no crack.
        Returns (controls, vertices, triangles): `controls` are the distinct control points the patches are made of (three expressions each), `vertices[v]` the vertex v as a list of
        (the number of a control point, its weight) -- the vertex is the sum of weight x control point --, and the triangles three vertex numbers each.  A control point is worked out once, whatever
        the number of vertices that are made of it '''
    controls, where = [], {}
    verts, index, tris = [], {}, []
    q = n // 2                                                   # (the pieces of the half of an edge, which is where the patches of a face of three meet the edge)

    def control(point):
        # (by the identity of the point: the same tuple is the same control point -- the ones in `controls` stay alive, so that no identity is taken twice)
        if id(point) not in where:
            where[id(point)] = len(controls)
            controls.append(point)
        return where[id(point)]

    def vertex(key, make):
        if key not in index:
            index[key] = len(verts)
            verts.append(make())
        return index[key]

    def edge_key(a, b, t):
        return ('e', a, b, t) if a < b else ('e', b, a, n - t)

    def emit(fi, net, versions, ckeys, ekeys, pieces):
        ''' The vertices and triangles of one patch of four, `pieces` to an edge: the net of its sixteen control points, the two versions of each of its four inner ones (see `gregory`), the keys of
            its four corners and of the vertices along its four edges (a to b, b to c, d to c, a to d -- the same keys from both faces that share an edge) '''
        # (the weights of the vertices are the same for every patch of this number of pieces: see _patch_terms; the control points of THIS patch are put in their slots once, and not found again for every term
        # of every vertex)
        table = _patch_terms(pieces, tuple(sorted(versions)))
        slots = [None] * 48
        for i in range(4):
            for j in range(4):
                if (i, j) in versions:
                    slots[3 * (4 * i + j) + 1] = control(versions[(i, j)][0])
                    slots[3 * (4 * i + j) + 2] = control(versions[(i, j)][1])
                else:
                    slots[3 * (4 * i + j)] = control(net[i][j])

        def place(k, l):
            if (k, l) == (0, 0):
                key = ckeys[0]
            elif (k, l) == (pieces, 0):
                key = ckeys[1]
            elif (k, l) == (pieces, pieces):
                key = ckeys[2]
            elif (k, l) == (0, pieces):
                key = ckeys[3]
            elif l == 0:
                key = ekeys[0](k)
            elif k == pieces:
                key = ekeys[1](l)
            elif l == pieces:
                key = ekeys[2](k)
            elif k == 0:
                key = ekeys[3](l)
            else:
                key = ('f', fi, k, l)
            tpl = table[(k, l)]
            return vertex(key, lambda: [(slots[s], w) for s, w in tpl])

        grid = [[place(k, l) for k in range(pieces + 1)] for l in range(pieces + 1)]
        for l in range(pieces):
            for k in range(pieces):
                p00, p10, p11, p01 = grid[l][k], grid[l][k + 1], grid[l + 1][k + 1], grid[l + 1][k]
                # (the diagonal of a cell runs the other way in the cell next to it, so that the mesh of a face looks the same from either side of its middle -- a symmetric cage is a symmetric field)
                if (k + l) % 2 == 0:
                    tris.append((p00, p10, p11))
                    tris.append((p00, p11, p01))
                else:
                    tris.append((p00, p10, p01))
                    tris.append((p10, p11, p01))

    for fi, face in enumerate(cage):
        if len(face) == 4:
            a, b, c, d = face
            net = [[None] * 4 for _ in range(4)]
            net[0][0], net[3][0], net[3][3], net[0][3] = V[a], V[b], V[c], V[d]
            net[1][0], net[2][0] = edge(a, b), edge(b, a)
            net[3][1], net[3][2] = edge(b, c), edge(c, b)
            net[2][3], net[1][3] = edge(c, d), edge(d, c)
            net[0][2], net[0][1] = edge(d, a), edge(a, d)
            # (inside: the surface that has no twist at the corners -- each of the four inner control points makes a parallelogram with the two edge control points next to its corner; the Gregory
            # versions below replace them)
            net[1][1] = _sub(_add(net[1][0], net[0][1]), net[0][0])
            net[2][1] = _sub(_add(net[2][0], net[3][1]), net[3][0])
            net[2][2] = _sub(_add(net[2][3], net[3][2]), net[3][3])
            net[1][2] = _sub(_add(net[1][3], net[0][2]), net[0][3])
            emit(fi, net, gregory(face), (('c', a), ('c', b), ('c', c), ('c', d)),
                 (lambda j, a=a, b=b: edge_key(a, b, j), lambda j, b=b, c=c: edge_key(b, c, j), lambda j, d=d, c=c: edge_key(d, c, j), lambda j, a=a, d=d: edge_key(a, d, j)), n)
        else:
            a, b, c = face
            for s, (net, versions) in enumerate(triangle(face)):
                x, y, z = ((a, b, c), (b, c, a), (c, a, b))[s]                  # (the patch at the corner x: y is the next corner, z the one before)
                # (its corners: x, the middle of the edge x y, the middle of the face, the middle of the edge z x; the edges between the middles are the same from the two patches they separate)
                emit((fi, s), net, versions, (('c', x), edge_key(x, y, q), ('ctr', fi), edge_key(x, z, q)),
                     (lambda j, x=x, y=y: edge_key(x, y, j), lambda j, x=x: ('i', fi, x, j), lambda j, z=z: ('i', fi, z, j), lambda j, x=x, z=z: edge_key(x, z, j)), q)
    return controls, verts, tris


def _flen(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def _hull(pos):
    ''' The faces of the convex hull of the places `pos` (three numbers each), counterclockwise seen from outside, and the numbers of the places that are inside it (not a corner of it).  A face of the hull
        that has four corners is one face of four (a cube of eight places has its six squares); one with three is a face of three; a flat piece with more corners stays the triangles it is made of.  The
        hull is made by adding the places one by one to a tetrahedron: the faces a place can be seen from are taken away and the place is joined to the edge they leave round it.  A place that lies in a face
        of the hull (not in its edge) splits that face in three.  Raises ValueError for places that are all at one place, in one line or in one plane, and for a place that lies in an edge or on another '''
    n = len(pos)
    size = max(max(p[k] for p in pos) - min(p[k] for p in pos) for k in range(3))
    if size <= 0.0:
        raise ValueError('all the points are at one place')
    eps = 1e-9 * size

    def unit(a):
        l = _flen(a)
        return (a[0] / l, a[1] / l, a[2] / l)

    i0 = min(range(n), key=lambda i: pos[i])
    i1 = max(range(n), key=lambda i: _flen(_sub(pos[i], pos[i0])))
    axis = unit(_sub(pos[i1], pos[i0]))

    def off_line(i):
        v = _sub(pos[i], pos[i0])
        t = _dot(v, axis)
        return _flen(_sub(v, _scale(t, axis)))
    i2 = max(range(n), key=off_line)
    if off_line(i2) <= eps:
        raise ValueError('all the points are in one line: a body needs points that are not in one plane')
    across_ = unit(_cross(_sub(pos[i1], pos[i0]), _sub(pos[i2], pos[i0])))
    i3 = max(range(n), key=lambda i: abs(_dot(_sub(pos[i], pos[i0]), across_)))
    if abs(_dot(_sub(pos[i3], pos[i0]), across_)) <= eps:
        raise ValueError('all the points are in one plane: a body needs at least four points that are not (give the faces for a flat shape, or use profile)')
    centre = tuple((pos[i0][k] + pos[i1][k] + pos[i2][k] + pos[i3][k]) / 4.0 for k in range(3))

    def make(a, b, c):
        ''' (a, b, c, unit normal, offset) of a face, its normal turned out '''
        nrm = _cross(_sub(pos[b], pos[a]), _sub(pos[c], pos[a]))
        l = _flen(nrm)
        if l <= eps * size:
            raise ValueError('points %d, %d and %d are in a line on the surface: say the faces' % (a, b, c))
        nrm = (nrm[0] / l, nrm[1] / l, nrm[2] / l)
        return (a, b, c, nrm, _dot(nrm, pos[a]))

    def outward(a, b, c):
        f = make(a, b, c)
        return f if _dot(f[3], centre) - f[4] < 0 else make(a, c, b)

    faces = [outward(i0, i1, i2), outward(i0, i1, i3), outward(i0, i2, i3), outward(i1, i2, i3)]
    for p in range(n):
        if p in (i0, i1, i2, i3):
            continue
        seen = [f for f in faces if _dot(f[3], pos[p]) - f[4] > eps]
        if not seen:
            # (inside, or in a face: that face is split in three, a place in an edge or at a corner is refused)
            for f in faces:
                if abs(_dot(f[3], pos[p]) - f[4]) > eps:
                    continue
                a, b, c = f[0], f[1], f[2]
                whole = _dot(_cross(_sub(pos[b], pos[a]), _sub(pos[c], pos[a])), f[3])
                u = _dot(_cross(_sub(pos[b], pos[p]), _sub(pos[c], pos[p])), f[3]) / whole
                v = _dot(_cross(_sub(pos[c], pos[p]), _sub(pos[a], pos[p])), f[3]) / whole
                w = 1.0 - u - v
                if min(u, v, w) > 1e-9:
                    faces.remove(f)
                    faces += [make(a, b, p), make(b, c, p), make(c, a, p)]
                    break
                if min(u, v, w) > -1e-9:
                    # (in an edge: the line between the two triangles of a flat piece, the diagonal of a square, is no edge of the body -- the place splits both triangles in two --; an edge of the
                    # body is refused, and so is a place at a corner)
                    if sorted((u, v, w))[1] > 1e-9:
                        x, y, z = (a, b, c) if w <= 1e-9 else (b, c, a) if u <= 1e-9 else (c, a, b)
                        g = next((h for h in faces if (y, x) in ((h[0], h[1]), (h[1], h[2]), (h[2], h[0]))), None)
                        if g is not None and _dot(f[3], g[3]) > 1.0 - 1e-12:
                            d = next(t for t in g[:3] if t not in (x, y))
                            faces.remove(f)
                            faces.remove(g)
                            faces += [make(x, p, z), make(p, y, z), make(y, p, d), make(p, x, d)]
                            break
                    raise ValueError('point %d lies in an edge of the body or on another point: say the faces' % p)
            continue
        edges = {}
        for f in seen:
            for x, y in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
                edges[(x, y)] = True
        keep = [f for f in faces if f not in seen]
        for (x, y) in edges:
            if (y, x) not in edges:
                keep.append(make(x, y, p))
        faces = keep
    used = set(v for f in faces for v in f[:3])
    inside = [i for i in range(n) if i not in used]
    # (the triangles that are one flat piece and share an edge: a piece of four corners is one face)
    parent = list(range(len(faces)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    owner = {}
    for i, f in enumerate(faces):
        for x, y in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
            owner[(x, y)] = i
    for (x, y), i in owner.items():
        j = owner.get((y, x))
        if j is not None and _dot(faces[i][3], faces[j][3]) > 1.0 - 1e-12:
            parent[root(i)] = root(j)
    groups = {}
    for i in range(len(faces)):
        groups.setdefault(root(i), []).append(i)
    out = []
    for members in sorted(groups.values()):
        corners = set(v for i in members for v in faces[i][:3])
        if len(members) == 2 and len(corners) == 4:
            loop = {}
            made = set((faces[i][a], faces[i][b]) for i in members for a, b in ((0, 1), (1, 2), (2, 0)))
            for x, y in made:
                if (y, x) not in made:
                    loop[x] = y
            first = min(loop)
            ring = [first]
            while loop[ring[-1]] != first:
                ring.append(loop[ring[-1]])
            out.append(tuple(ring))
        else:
            out.extend(tuple(faces[i][:3]) for i in members)
    return out, inside


def _default_faces(name, pos):
    ''' The faces of a body of the places `pos` that says none: the convex hull of the places (see `_hull`).  Eight places that are the corners of a cube in the order the module says -- the six
        squares of the cube hold all the faces of the hull, and turn out -- have the six squares of the cube, as the body of eight always had; and eight of which one is pushed into the body (it is not
        a corner of the hull) too.  A body of other places with a place inside the hull has to say its faces '''
    try:
        hull, inside = _hull(pos)
    except ValueError as e:
        raise ValueError('%s: %s' % (name, e))
    if len(pos) == 8:
        cube = tuple(CUBE_FACES)
        if inside:
            # (a point pushed into the body keeps the cube -- unless the squares of the cube pass through each other, which they do when points were dragged through the body: the faces then follow the hull and the point is pressed in as a dent, H129)
            if not _cage_crosses(cube, pos):
                return cube
            return _hull_with_dents(hull, inside, pos)
        middle = tuple(sum(p[k] for p in pos) / 8.0 for k in range(3))
        turned_out = all(_dot(_cross(_sub(pos[f[1]], pos[f[0]]), _sub(pos[f[2]], pos[f[0]])),
                              _sub(tuple(sum(pos[v][k] for v in f) / 4.0 for k in range(3)), middle)) > 0.0 for f in cube)
        squares = [set(f) for f in cube]
        if turned_out and all(any(set(h) <= s for s in squares) for h in hull):
            return cube
    if inside:
        raise ValueError('%s: point %d lies inside the body made by the others: say its faces (a list `<the body>_faces = [(0, 2, 3, 1), ...]` above the line that makes the body) -- the numbers of three or four points each, counterclockwise seen from outside'
                         % (name, inside[0]))
    return tuple(hull)


def _without_collection(f):
    ''' f with the garbage collector off while it runs.  A body of points makes fifty thousand expressions, none of them in a cycle (a shape is a number that names a node of the library), and the
        collector goes over everything that is alive each time that many have been made -- a script of a few bodies took three times as long to run the third as the first (0.7 s against 0.23 s) '''
    def g(*args, **kwargs):
        was = gc.isenabled()
        gc.disable()
        try:
            return f(*args, **kwargs)
        finally:
            if was:
                gc.enable()
    g.__name__, g.__doc__ = f.__name__, f.__doc__
    return g


def _fill_holes(faces, gone):
    ''' The faces without the points `gone` -- those that name one of them are taken away -- and the holes that leaves closed again: the edges of the faces that went, those that lie between points that stay, run
        round each hole in the direction they had, and each loop of three or four points is one face (more: a fan of triangles).  The faces of the four triangles round a point that was pressed out of a square
        make the square again.  None when a hole is not a closed loop '''
    kept = [tuple(f) for f in faces if not any(v in gone for v in f)]
    edges = {}
    for f in faces:
        if not any(v in gone for v in f):
            continue
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            if a not in gone and b not in gone:
                edges[a] = b
    seen = set()
    for start in list(edges):
        if start in seen:
            continue
        loop, v = [], start
        while v in edges and v not in seen:
            seen.add(v)
            loop.append(v)
            v = edges[v]
        if v != start or len(loop) < 3:
            return None
        if len(loop) <= 4:
            kept.append(tuple(loop))
        else:
            kept.extend((loop[0], loop[i], loop[i + 1]) for i in range(1, len(loop) - 1))
    return kept


def _healed(name, cage, n, positions):
    ''' The faces of a body of `n` points when some of them name a point that is not there (a point was deleted and the faces were not told): they are taken away and the holes closed (see _fill_holes); when
        that cannot be done the faces are the convex hull's.  Faces that are right are returned as they are '''
    gone = set(v for f in cage for v in f if not 0 <= v < n)
    if not gone:
        return cage
    filled = _fill_holes(cage, gone)
    if filled is None:
        return tuple(_default_faces(name, positions))
    return tuple(filled)


# A BODY OF CORNERS WITH FLAT FACES (H69, the user, 2026-11-02: "Meshes with under 2000-5000 vertices often still need manual adjustment ... BUT 300 AS A CUTOFF???"; and H68, the drag lag): when every point is a corner
# and every face is flat -- a triangle, or four points in one plane --, the surface IS the polyhedron of its faces, so it needs no patches: every face is cut at the middles of its edges and its middle (a face of
# four in four cells, a face of three in three cells, six triangles or eight to a face, the same cutting the general path makes with two pieces to an edge), and every vertex is a weighted sum of the POINTS --
# no expression is made for it at all.  What cost 26 milliseconds a face (about two thousand nodes of expression for each) costs a few microseconds.  _FLAT_PATH = False makes every body go the general way (the
# check compares the two)
_FLAT_PATH = True


def _is_polyhedron(ctrls, cage, smooth):
    ''' Every point a corner, every face flat: the body is exactly the polyhedron of its faces '''
    if not _FLAT_PATH:
        return False
    for c in ctrls:
        if c.rounded or c.smooth(smooth):
            return False
    pos = [tuple(float(v) for v in c.pos) for c in ctrls]
    size = max(max(p[k] for p in pos) - min(p[k] for p in pos) for k in range(3)) or 1.0
    for f in cage:
        if len(f) == 4:
            a, b, c, d = (pos[i] for i in f)
            n = _cross(_sub(b, a), _sub(c, a))
            nn = _flen(n)
            if nn < 1e-12 * size * size:
                n = _cross(_sub(c, a), _sub(d, a))
                nn = _flen(n)
                if nn < 1e-12 * size * size:
                    return False
            if abs(_dot(_sub(d, a), n)) / nn > 1e-6 * size:
                return False
    return True


def _flat_tessellation(cage):
    ''' (vertices, triangles) of a body of corners with flat faces: `vertices[v]` is a list of (point number, weight), as _tessellate makes them '''
    verts, index, tris = [], {}, []

    def vertex(key, recipe):
        if key not in index:
            index[key] = len(verts)
            verts.append(recipe)
        return index[key]

    def corner(i):
        return vertex(('c', i), [(i, 1.0)])

    def mid(i, j):
        return vertex(('e', min(i, j), max(i, j)), [(i, 0.5), (j, 0.5)])

    for fi, f in enumerate(cage):
        if len(f) == 4:
            a, b, c, d = f
            ctr = vertex(('f', fi), [(a, 0.25), (b, 0.25), (c, 0.25), (d, 0.25)])
            grid = [[corner(a), mid(a, b), corner(b)], [mid(a, d), ctr, mid(b, c)], [corner(d), mid(d, c), corner(c)]]
            for l in range(2):
                for k in range(2):
                    p00, p10, p11, p01 = grid[l][k], grid[l][k + 1], grid[l + 1][k + 1], grid[l + 1][k]
                    # (the diagonal of a cell runs the other way in the cell next to it, as in _tessellate)
                    if (k + l) % 2 == 0:
                        tris.append((p00, p10, p11))
                        tris.append((p00, p11, p01))
                    else:
                        tris.append((p00, p10, p01))
                        tris.append((p10, p11, p01))
        else:
            a, b, c = f
            ctr = vertex(('f', fi), [(a, 1.0 / 3.0), (b, 1.0 / 3.0), (c, 1.0 / 3.0)])
            for x, y, z in ((a, b, c), (b, c, a), (c, a, b)):
                p00, p10, p11, p01 = corner(x), mid(x, y), ctr, mid(z, x)
                tris.append((p00, p10, p11))
                tris.append((p00, p11, p01))
    return verts, tris


def _finish(name, items, smooth, radius, ctrls, cage, field, drawn_controls, drawn_vertices, drawn_tris):
    ''' The body made of its field and of the numbers that it is drawn from (see Shape::morphNow) '''
    out = _own(field)
    out.points, out.smooth, out.radius = items, smooth, radius
    out._ctrls = ctrls

    def remake(new_points, improper=False):
        ''' The same body made again through other points -- what the image of a body of points is made with (see symmetries._outline_image: the same body through the images of its points, so that it follows the
            original, is drawn from numbers like it, and is a real field like it).  A mirror image turns every face round, so that the faces still face outward '''
        faces_now = tuple(tuple(reversed(f)) for f in cage) if improper else cage
        return _morph(name, list(new_points), smooth, radius, faces_now)
    out._remake = remake

    def remake_mapped(new_points, mapper, field_now, improper=False):
        """ The image of this body in a mirror image or a turn, made WITHOUT making the surface again (H70, the user, 2026-11-02: "Dragging new points is a little faster now, though IT STILL LAGS THE PROGRAM"; the clone of a body
            through symmetry_images made the whole body again, as long as the body took): the surface of the image is the surface of the body taken through the map -- every control point of the patches through `mapper` (three
            expressions to three), the weights of the vertices and the triangles the same, the triangles turned round for a mirror image so that they still face outward.  `new_points` are the images of the points, for the
            points of the image (what is pressed and dragged on it); `field_now` is its field: the field of this body asked at the places the way back of the image takes it from (see symmetries._image_field) -- the mirror image of the
            field of the body exactly, not a mesh of the drawn triangles, which are as fine as the view asks and no finer than that"""
        faces_now = tuple(tuple(reversed(f)) for f in cage) if improper else cage
        items_now = list(new_points)
        ctrls_now = C.controls(items_now, rounds=True, radius_default=0 if smooth else radius, smooth_default=False)
        C.mark(ctrls_now, smooth, 'morph')
        controls_now = [mapper(tuple(Shape.wrap(v) for v in c)) for c in drawn_controls]
        tris_now = [(t[0], t[2], t[1]) for t in drawn_tris] if improper else drawn_tris
        return _finish(name, items_now, smooth, radius, ctrls_now, faces_now, field_now, controls_now, drawn_vertices, tris_now)
    out._remake_mapped = remake_mapped
    out._morph_faces = cage                                      # (the faces as they are now: what pressing the surface splits -- see profile_edit)
    # (what the viewport draws, from the numbers of the points at every step of a drag -- nothing is meshed from the field, see Shape::morphNow: the control points of the patches as expressions
    # of the numbers, each worked out once; every vertex of the surface as a list of (control point, weight) -- `_morph_vstart[v]` to `_morph_vstart[v + 1]` of the two lists `_morph_vctrl` and
    # `_morph_vweight` --; and the triangles between the vertices)
    out._morph_controls = [tuple(Shape.wrap(c) for c in p) for p in drawn_controls]
    out._morph_vstart, out._morph_vctrl, out._morph_vweight = [0], [], []
    for terms in drawn_vertices:
        for c, weight in terms:
            out._morph_vctrl.append(c)
            out._morph_vweight.append(float(weight))
        out._morph_vstart.append(len(out._morph_vctrl))
    out._morph_tris = drawn_tris
    out._editable = True                                         # (a press on its surface is where a point is put, not a surface to pull by the numbers of the points)
    out._no_gizmo = True                                         # (the points have the numbers: the gizmo of each moves it, and the shared one moves them all)
    lo = tuple(min(c.pos[k] for c in ctrls) for k in range(3))
    hi = tuple(max(c.pos[k] for c in ctrls) for k in range(3))
    pad = 0.35 * max(hi[k] - lo[k] for k in range(3))            # (room for a point that is dragged: the region is not searched for)
    _bounded(out, tuple(v - pad for v in lo), tuple(v + pad for v in hi))
    out._bounds_loose = True
    return out


@_without_collection
def _triangulated(cage):
    ''' The faces of a polyhedron as triangles only (the user, 2026-11-02, F37a: "A SEPARATE THING THAT CAN ONLY BE TRIANGLES"): a square is two triangles, cut along the diagonal from its first point '''
    out = []
    for f in cage:
        if len(f) == 3:
            out.append(tuple(f))
        else:
            out.append((f[0], f[1], f[2]))
            out.append((f[0], f[2], f[3]))
    return tuple(out)


_FOLLOW_LIMIT = 64          # (the most points a body can have for its faces to follow the hull: the hull is made in python, about the points times the faces; a body of more points -- an import -- keeps the faces it was given)


def _cage_crosses(cage, pos):
    ''' Whether the faces of a cage, as triangles, pass through each other.  A body whose points were dragged through its own surface -- two corners of a cube pulled down below its bottom -- has the faces it was made with, and those cross: the surface folds
        over itself wherever it is smoothed (the user, 2026-11-04, H129, after dragging two points: "It should not be so easy for the user to completely break the shape"; measured on his file: 184 pairs of crossing triangles in the surface, 48 edges folded over 150
        degrees, and in 36 random drags of two points of a cube every case that went wrong was a case whose faces cross) '''
    tris = []
    for f in cage:
        tris.append((f[0], f[1], f[2]))
        if len(f) == 4:
            tris.append((f[0], f[2], f[3]))
    boxes = []
    for t in tris:
        pts = [pos[v] for v in t]
        boxes.append((tuple(min(p[k] for p in pts) for k in range(3)), tuple(max(p[k] for p in pts) for k in range(3))))

    def pierces(p, q, a, b, c):
        d = _sub(q, p)
        e1, e2 = _sub(b, a), _sub(c, a)
        h = _cross(d, e2)
        det = _dot(e1, h)
        if abs(det) < 1e-12:
            return False
        f = 1.0 / det
        s = _sub(p, a)
        u = f * _dot(s, h)
        if u < 0.0 or u > 1.0:
            return False
        w = _cross(s, e1)
        v = f * _dot(d, w)
        if v < 0.0 or u + v > 1.0:
            return False
        t = f * _dot(e2, w)
        return 1e-6 < t < 1.0 - 1e-6

    for i in range(len(tris)):
        for j in range(i + 1, len(tris)):
            if set(tris[i]) & set(tris[j]):
                continue
            if any(boxes[i][1][k] < boxes[j][0][k] or boxes[j][1][k] < boxes[i][0][k] for k in range(3)):
                continue
            A = [pos[v] for v in tris[i]]
            B = [pos[v] for v in tris[j]]
            if any(pierces(A[k], A[(k + 1) % 3], B[0], B[1], B[2]) for k in range(3)) or any(pierces(B[k], B[(k + 1) % 3], A[0], A[1], A[2]) for k in range(3)):
                return True
    return False


def _hull_with_dents(hull, inside, pos):
    ''' The faces of the hull with every point that is inside it pressed in as a dent: the face the point lies under -- the one whose plane it is nearest to, among the faces it is under -- is split in three round it.  The faces are triangles '''
    faces = []
    for f in hull:
        faces.append((f[0], f[1], f[2]))
        if len(f) == 4:
            faces.append((f[0], f[2], f[3]))
    for q in inside:
        best, best_score = None, None
        for fi, (a, b, c) in enumerate(faces):
            n = _cross(_sub(pos[b], pos[a]), _sub(pos[c], pos[a]))
            l = math.sqrt(_dot(n, n))
            if l < 1e-12:
                continue
            nn = (n[0] / l, n[1] / l, n[2] / l)
            dist = _dot(_sub(pos[q], pos[a]), nn)
            foot = (pos[q][0] - nn[0] * dist, pos[q][1] - nn[1] * dist, pos[q][2] - nn[2] * dist)
            u = _dot(_cross(_sub(pos[b], foot), _sub(pos[c], foot)), nn) / l
            v = _dot(_cross(_sub(pos[c], foot), _sub(pos[a], foot)), nn) / l
            w = 1.0 - u - v
            score = (0 if min(u, v, w) >= -1e-9 else 1, abs(dist))
            if best_score is None or score < best_score:
                best, best_score = fi, score
        if best is None:
            continue
        a, b, c = faces.pop(best)
        faces += [(a, b, q), (b, c, q), (c, a, q)]
    return tuple(faces)


def _faces_between_closest_points(cage, positions):
    ''' THE FACES ARE MADE BETWEEN THE CLOSEST POINTS (H104, the user, 2026-11-03: "faces are created between the closest points ... the feature stays the same"; when a face could be any polygon a point pressed out of
        it looked right, since the switch to triangles it only looked so).  A body whose points are all corners of their convex hull has the faces of the hull, whatever list of faces it was given -- a point pressed out of a face
        starts as the face split round it, which is the hull while the point is above the face, and the faces follow the hull as the point is dragged on --, and a body with a point inside the hull (a dent), a body whose
        hull cannot be made, and a body of more than _FOLLOW_LIMIT points keep the faces they have: nothing is made of them that costs the time of the points '''
    if len(positions) > _FOLLOW_LIMIT:
        return cage
    try:
        hull, inside = _hull(positions)
    except ValueError:
        return cage
    if inside:
        # (the faces they have, unless those pass through each other: then the faces follow the hull and every point inside it is pressed in as a dent -- H129)
        if not _cage_crosses(cage, positions):
            return cage
        return _hull_with_dents(hull, inside, positions)
    return tuple(tuple(f) for f in hull)


def _morph(name, points, smooth, radius, faces):
    items = list(points)
    if len(items) < 4:
        raise ValueError('%s needs at least four points (that are not all in one plane): %d were given' % (name, len(items)))
    if C.value_of(radius) < 0:
        raise ValueError('%s(..., radius=%g): a radius is not negative (0 is a corner)' % (name, C.value_of(radius)))
    smooth = bool(smooth)
    # (the controls are made as for a body whose points are corners unless they say otherwise: a point with a radius of its own is a sphere point whatever the body's default is; the default radius
    # is for the points that are corners)
    ctrls = C.controls(items, rounds=True, radius_default=0 if smooth else radius, smooth_default=False)
    C.mark(ctrls, smooth, 'morph')
    # (what kind of point each is, for its gizmo: a corner point only moves, a sphere point moves and scales, a spline point has the whole gizmo -- the user, 2026-11-04, H130)
    for c in ctrls:
        try:
            c.item._point_kind = 'sphere' if c.rounded else ('spline' if c.smooth(smooth) else 'corner')
        except AttributeError:
            pass
    if faces is None:
        cage = _default_faces(name, [tuple(float(v) for v in c.pos) for c in ctrls])
    else:
        cage = tuple(tuple(int(v) for v in f) for f in faces)
        # (a face that names a point that is not there -- the point was deleted -- is not an error: the faces are healed, see _healed; the user, 2026-11-02, on deleting a point of a morph shape)
        cage = _healed(name, cage, len(items), [tuple(float(v) for v in c.pos) for c in ctrls])
        for f in cage:
            if len(f) not in (3, 4) or any(not 0 <= v < len(items) for v in f) or len(set(f)) != len(f):
                raise ValueError('%s: a face is the numbers of three or four different points (0 to %d): %r' % (name, len(items) - 1, f))
        cage = _faces_between_closest_points(cage, [tuple(float(v) for v in c.pos) for c in ctrls])
    # (the normal at a point is made of the faces as they were given -- a square is one face of its own area at each of its four corners --, not of the triangles that a square is cut in: along one
    # diagonal the corners it touches twice weigh it twice, and eight identical sphere points of a cube were not the same at the eight corners, the user, 2026-11-02, H47)
    normal_cage = cage
    if name == 'morph_polyhedron':
        cage = _triangulated(cage)
    P = [c.expr for c in ctrls]
    # The mean length of the edges at each point: what a sphere point's radius is measured against
    neighbours = [set() for _ in P]
    for f in cage:
        for k in range(len(f)):
            neighbours[f[k]].add(f[(k + 1) % len(f)])
            neighbours[f[(k + 1) % len(f)]].add(f[k])
    for i, c in enumerate(ctrls):
        # (how long the shortest edge of the cage at the point is: where a double click that makes it a sphere point starts its radius, a quarter of it -- see profile_edit._toggle)
        near = [math.dist(c.pos, ctrls[j].pos) for j in neighbours[i]]
        if near:
            try:
                c.item._curve_edge = min(near)
            except AttributeError:
                pass
            if c.rounded and c.radius_expr is not None:
                # (the largest influence the point can show: the radius that is used stops at _SPHERE_LIMIT of the shortest edge, so a number beyond that is one the drawing ignores -- the wheel and the ring of the
                # viewport keep the influence below it, the user, 2026-11-02: "THE VALUES SHOULD BE FUCKING CAPPED, NOT JUST NOT DISPLAY")
                try:
                    inf_now = C.value_of(c.influence_expr) if c.influence_expr is not None else 1.0
                    rad_now = C.value_of(c.radius_expr)
                    if inf_now > 1e-9 and rad_now > 1e-9:
                        c.item._influence_max = float(inf_now * (_SPHERE_LIMIT * min(near)) / rad_now)
                except Exception:
                    pass
    if _is_polyhedron(ctrls, cage, smooth):
        verts, tris = _flat_tessellation(cage)
        field = _native_field(P, verts, tris)
        return _finish(name, items, smooth, radius, ctrls, cage, field, P, verts, tris)
    N = _normals(P, normal_cage)
    # The rotation of a spline point turns the plane its surface leaves it in (the user, 2026-11-02: "rotation for splines" was a requirement of the feature, as it is for the line of a spline): the plane is
    # across the normal, so the normal is what is turned.  Everything that leaves the point -- the curves of its edges and, through them, the faces that meet there and the tangent plane they share with
    # the face on the other side of each edge -- follows it
    for i, c in enumerate(ctrls):
        if c.rot_expr is not None and not c.rounded and c.smooth(smooth):
            N[i] = _turn(N[i], c.rot_expr)
    # What each point is: where the surface is at it (V), how far the curves that leave it go the way a spline's do -- along the tangent plane, not straight at the next point -- (S: 0 for a
    # corner, 1 for a spline point) and how strongly they leave it (K: the influence of the point, 1 being the spline of a profile)
    NORMALS = N                                                  # (the normals of the points under a name that triangle() does not use for the normal of an edge)
    V, S, K, R = [], [], [], []
    SHORT = {}                                                   # (the shortest edge at each sphere point)
    sphere = [bool(c.rounded) for c in ctrls]
    for i, c in enumerate(ctrls):
        R.append(None)
        if c.rounded:
            # a sphere point: a rounded corner (the user, 2026-11-02: "I want it to straight up make a strong radius, just like it does in a profile", after a build whose cut was a fraction of the radius and
            # went with the square of it).  The radius used is the radius itself -- the scale and the influence of the point are in it -- until it is a good part of the shortest edge at the point; the
            # point is cut in along its normal by the depth a sphere of that radius cuts the corner of a box, and the edges that leave it leave it in the plane across the normal for the length of the radius
            # (see `edge`), so that the corner is rounded where it is and the rest of the edge is the edge
            shortest = None
            for j in neighbours[i]:
                d = ((P[i][0] - P[j][0]).square() + (P[i][1] - P[j][1]).square() + (P[i][2] - P[j][2]).square()).sqrt()
                shortest = d if shortest is None else shortest.min(d)
            r = c.radius_expr
            used = r if shortest is None else Shape.wrap(r).min(shortest * _SPHERE_LIMIT)
            R[i] = used
            SHORT[i] = shortest
            # (the ball that rounds the corner touches every face that meets there: its centre is r u behind the corner, u being the vector for which the outward unit normal m of each face has m . u = 1 -- solved
            # over the faces as they are given: for the corner of a box that is u = (1, 1, 1) in the axes of its faces, whichever way a square is cut in triangles.  The normal of the point is the way of u, and the
            # cut is r (|u| - 1) deep: r (sqrt(3) - 1) at the corner of a box, as it was, and more for a corner that has been pulled out into a wedge, which the constant of the box left a beak -- the user,
            # 2026-11-04, of a morph_body with one point pulled out: "it is actually broken"; measured: the apex stopped 0.73 r short of the corner whatever the wedge)
            faces_here = [_unit(_face_normal(P, face)) for face in cage if i in face]
            if faces_here:
                eps = _SPHERE_FACES_RIDGE
                s00, s01, s02, s11, s12, s22 = Shape.wrap(eps), Shape.wrap(0.0), Shape.wrap(0.0), Shape.wrap(eps), Shape.wrap(0.0), Shape.wrap(eps)
                q0, q1, q2 = N[i][0] * eps, N[i][1] * eps, N[i][2] * eps
                for m in faces_here:
                    s00, s01, s02 = s00 + m[0] * m[0], s01 + m[0] * m[1], s02 + m[0] * m[2]
                    s11, s12, s22 = s11 + m[1] * m[1], s12 + m[1] * m[2], s22 + m[2] * m[2]
                    q0, q1, q2 = q0 + m[0], q1 + m[1], q2 + m[2]
                det = s00 * (s11 * s22 - s12 * s12) - s01 * (s01 * s22 - s02 * s12) + s02 * (s01 * s12 - s02 * s11)
                u0 = ((s11 * s22 - s12 * s12) * q0 + (s02 * s12 - s01 * s22) * q1 + (s01 * s12 - s02 * s11) * q2) / det
                u1 = ((s02 * s12 - s01 * s22) * q0 + (s00 * s22 - s02 * s02) * q1 + (s01 * s02 - s00 * s12) * q2) / det
                u2 = ((s01 * s12 - s02 * s11) * q0 + (s01 * s02 - s00 * s12) * q1 + (s00 * s11 - s01 * s01) * q2) / det
                u_len = (u0 * u0 + u1 * u1 + u2 * u2).sqrt()
                N[i] = (u0 / u_len, u1 / u_len, u2 / u_len)
                # (the normal of a sphere point is the way its edges leave it, turned round: the mean of the unit vectors to its neighbours -- not the one the ball-touch condition gives, which for a point pulled far out of its body leaves the three edges in a sector of 76 degrees around it and one face with a corner of 284 degrees, and the patch of that face folds over the faces on both sides of its ridges.  Measured on his file: 63 pairs of crossing triangles along the horn with that normal, none with this one, and the tip exactly at the cut-in place.  For the corner of a box, whose three edges are its axes, the two are the same.  The user, 2026-11-04, H127, of a picture of the horn: "Your own image litearlly shows how trash it is, it even is intersecting in your image")
                _mean = None
                for j in neighbours[i]:
                    _e = _unit(_sub(P[j], P[i]))
                    _mean = _e if _mean is None else _add(_mean, _e)
                N[i] = _unit((-_mean[0], -_mean[1], -_mean[2]))
                depth_factor = (u_len - 1.0).max(0.0).min(_SPHERE_DEPTH_MAX)
            else:
                depth_factor = _SPHERE_DEPTH
            depth = used * depth_factor
            V.append(tuple(P[i][a] - N[i][a] * depth for a in range(3)))
            S.append(1.0)
            K.append(1.0)
        elif c.smooth(smooth):
            V.append(P[i])
            S.append(1.0)
            K.append(c.influence_expr if c.influence_expr is not None else 1.0)
        else:
            V.append(P[i])
            S.append(0.0)
            K.append(1.0)
    made = {}

    def edge(i, j):
        ''' The control point next to point i on the cubic curve from i to j: a third of the way along the leaving direction, which for a spline point has no part across the normal '''
        if (i, j) not in made:
            if sphere[i]:
                # (a sphere point: the edge leaves it flat, in the plane across its normal, toward where the edge is, for the length of the radius used -- not the whole edge, so that the rounding is local)
                to = _sub(P[j], P[i])
                along = _dot(to, N[i])
                flat = _unit(tuple(to[a] - N[i][a] * along for a in range(3)))
                # (a third of the chord between the two places the surface is at, a little more -- 1.1 -- as the handle of the circle through them is, so that two sphere points half an edge apart make the
                # arc of a sphere)
                chord = _norm(_sub(V[j], V[i]))
                factor = 1.1
                if sphere[j]:
                    # (between two sphere points the curve is the arc of a circle through the two places, and the handle of an arc of the angle theta between the normals at its ends is chord / (3 cos^2 (theta / 4)): 1.10
                    # where the corners of a cube are 70.5 degrees apart, 1.27 across a face -- the diagonal of a square that a morph polyhedron is cut in --, where the 1.1 left the middle of a face of a ball of eight
                    # sphere points 1.1 inside the sphere, the user, 2026-11-02, H47)
                    cos_theta = Shape.wrap(_dot(N[i], N[j])).max(-1.0).min(1.0)
                    factor = 2.0 / (1.0 + ((cos_theta + 1.0) * 0.5).sqrt())
                reach = Shape.wrap(R[i] * _SPHERE_REACH).min(chord * (factor / 3.0))
                made[(i, j)] = tuple(V[i][a] + flat[a] * reach for a in range(3))
            else:
                # (the edge goes toward the place of the point at its end, not toward the dent of a sphere point: the dent is the sphere point's own, and the edge is straight again after its reach)
                d = _sub(P[j], V[i])
                along = _dot(d, N[i])
                tangent = tuple((d[a] - N[i][a] * (along * S[i])) * K[i] for a in range(3))
                made[(i, j)] = tuple(V[i][a] + tangent[a] * (1.0 / 3.0) for a in range(3))
        return made[(i, j)]

    # SMOOTH IN ALL DIRECTIONS (the user, 2026-11-01: "Right now spline just looks like a smoothed corner. A Spline should just be smoothed in all directions", and "THIS IS AN ENGINEERING TOOL": the
    # surface itself must be smooth, not its shading).  Two faces that meet along an edge between two spline points have ONE tangent plane all along it, exactly (G1).  The patch of each face is a
    # Gregory patch: a bicubic patch whose four inner control points each have two versions, one for each edge at its corner, blended by u / (u + v) and v / (u + v), so that along each boundary the
    # second row of control points can be chosen on its own.  It is chosen so that the cross-boundary derivative of the patch is  alpha(t) T(t) + beta(t) E'(t)  with the SAME transversal T(t) -- the
    # vector across the edge from one face to the other, between the two ends -- on both sides of the edge: E' is the derivative of the edge's own curve, so both derivatives lie in the plane
    # T and E' span.  An edge with a corner at an end, or between two corners, is a sharp edge: its second row makes a parallelogram, as it did
    spline = [bool(c.rounded or c.smooth(smooth)) for c in ctrls]               # (a sphere point is smooth too: its tangent plane is across its normal, and the faces meet in it with one tangent plane)
    on_edge = {}
    for f in cage:
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            on_edge.setdefault((min(a, b), max(a, b)), []).append(f)

    def next_to(face, v, other):
        ''' The point of the face next to the point v that is not `other`: the end of the face's other edge at v '''
        k = face.index(v)
        before, after = face[k - 1], face[(k + 1) % len(face)]
        return after if before == other else before

    def anchor(x):
        ''' Where the parallelogram of the second row is measured from at point x: the place the surface is at.  At a sphere point that is the place of the corner itself in the measure that the radius is small
            (rho to the power 4 is the part that is the place of the surface): the handles of a sphere point go back to the edges of the faces, and a parallelogram from the place of the surface, which is
            cut in, would put the inner control point outside the face and the face would bulge (measured: 2 mm on a cube of 40 with a radius of 10) '''
        if not sphere[x] or SHORT.get(x) is None:
            return V[x]
        rho = Shape.wrap(R[x] / (SHORT[x] * _SPHERE_LIMIT))
        weight = rho * rho * rho * rho
        return tuple(P[x][a] * (1.0 - weight) + V[x][a] * weight for a in range(3))

    def parallel_row(face, x, y):
        ''' The second row of control points next to the edge x -> y of the patch of `face` when nothing is asked of it: each makes a parallelogram with the edge's control point and the other edge at that corner '''
        return (_add(edge(x, y), _sub(edge(x, next_to(face, x, y)), anchor(x))), _add(edge(y, x), _sub(edge(y, next_to(face, y, x)), anchor(y))))

    def split(vec, t, along):
        ''' vec = alpha t + beta along, in the plane the two span '''
        m = _cross(t, along)
        mm = _dot(m, m) + 1e-12
        return _dot(_cross(vec, along), m) / mm, _dot(_cross(t, vec), m) / mm

    def row_core(B, o0, o1, t0, t1, gain=1.0, k0=1.0, k1=1.0):
        ''' The two control points of the second row next to an edge whose curve has the Bezier points B, such that the patch leaves the edge in the plane that t (a vector across the edge, linear from t0 at
            the start to t1 at the end) and the edge's own direction span all along it.  o0 and o1 are the offsets of the patch's own other edge at the two ends (from the corner to the control point next to it) '''
        e = [_scale(3.0, _sub(B[k + 1], B[k])) for k in range(3)]                # (the derivative of the edge's curve: quadratic, these are its Bezier points)
        a0, b0 = split(_scale(3.0, o0), t0, e[0])
        a1, b1 = split(_scale(3.0, o1), t1, e[2])
        # (alpha is how much of the way across the edge the patch goes: 1/2 for two faces that leave it evenly.  Where the other edge of the patch at an end lies on the wrong side of the edge -- a point
        # pressed far into a face, the tangent plane of a corner turned away from it -- it is negative, and the patch would leave the edge backwards and fold over the face on the other side: it is kept
        # a little positive, so that the faces stay on their own sides and still have one tangent plane)
        a0, a1 = a0.max(_ALPHA_MIN).min(1.0) * gain, a1.max(_ALPHA_MIN).min(1.0) * gain
        # (alpha is at most the whole way across, and beta -- how much of the edge's own direction the patch takes -- is within one edge derivative: where the vector across the edge lies nearly along the edge,
        # `split` divides by almost nothing and both run off to thousands.  The user, 2026-11-04, a cube of eight spline points with one corner pulled out: "a complete disaster case", a snout that ran 50 mm past
        # the corner; measured on his file: the mesh ran to the edge of the sampled region, and with these two limits it stays 1.5 mm outside the points)
        b0, b1 = b0.max(-1.0).min(1.0), b1.max(-1.0).min(1.0)
        # (k0 and k1 take beta away at an end that is a sphere point: there the patch leaves the edge in the plane across the normal of the point, and the edge's own direction -- which a little way on has turned toward the neighbour, far along a horn -- took the second row of the three faces about twice the radius back from the cut-in place and drew the point out into a needle under 0.2 mm wide.  The user, 2026-11-04, H127, a sphere point dragged far out of a body of spline points: "The sphere points ends in a beak like point when dragged to a point like this"; measured: the second row 4.4 mm behind the point for a radius of 2.4, 0.2 mm with beta taken away, and the tip a rounded end)
        b0, b1 = b0 * k0, b1 * k1
        # (the cross-boundary derivative alpha T + beta E' as a cubic: alpha and beta are linear from one end to the other, T linear, E' quadratic -- its Bezier points, raised to degree three)
        p0, p1, p2 = _scale(a0, t0), _scale(0.5, _add(_scale(a0, t1), _scale(a1, t0))), _scale(a1, t1)
        c1 = _add(_scale(1.0 / 3.0, _add(p0, _scale(2.0, p1))), _scale(1.0 / 3.0, _add(_scale(2.0 * b0, e[1]), _scale(b1, e[0]))))
        c2 = _add(_scale(1.0 / 3.0, _add(_scale(2.0, p1), p2)), _scale(1.0 / 3.0, _add(_scale(b0, e[2]), _scale(2.0 * b1, e[1]))))
        return (_add(B[1], _scale(1.0 / 3.0, c1)), _add(B[2], _scale(1.0 / 3.0, c2)))

    across_made = {}

    def across(face, x, y, mixed=False):
        ''' The vector across the edge x -> y of `face`, at x and at y, from the face on the other side to this one: the offset of this face's other edge at the point, less the same of the other face.  It is
            the same for both faces up to its sign.  None where the edge is sharp (a corner at an end, or nothing on the other side) '''
        key = (id(face), x, y, mixed)
        if key not in across_made:
            result = None
            # (`mixed`: also for an edge with a corner at ONE end -- the vector across it at the corner is the difference of the two faces' other edges there -- which fade_row uses)
            if (spline[x] and spline[y]) or (mixed and (spline[x] or spline[y])):
                beyond = [f for f in on_edge[(min(x, y), max(x, y))] if f is not face]
                if beyond:
                    g = beyond[0]
                    result = (_sub(_sub(edge(x, next_to(face, x, y)), V[x]), _sub(edge(x, next_to(g, x, y)), V[x])),
                              _sub(_sub(edge(y, next_to(face, y, x)), V[y]), _sub(edge(y, next_to(g, y, x)), V[y])))
            across_made[key] = result
        return across_made[key]

    def fade_weight(i, j):
        ''' How much of the smooth row the second-row point at the end i of the edge i -> j takes: nothing at a corner, all of it at a spline point, and at a sphere point the part of the edge that
            _SPHERE_FADE radii make (all of it from half the edge on) '''
        if sphere[i]:
            length = _norm(_sub(P[j], P[i]))
            return Shape.wrap(R[i] * _SPHERE_FADE).min(length) / length
        return 1.0 if spline[i] else 0.0

    def fade_row(face, x, y):
        ''' The second row of an edge with a corner at one end and a smooth point -- a spline point or a sphere point -- at the other: sharp at the corner, where the two faces still meet in a crease, and
            smooth toward the smooth point, so that the crease of the corner fades out along the edge and is gone at the smooth point, where the faces have one tangent plane (the user, 2026-11-02, on a
            spline point and a sphere point next to a corner point: "the corner point affects the spline point up to the very inner part of its influence. At some point towards the center of the influence,
            it should return to being smooth without creases").  An edge between two corners stays sharp '''
        sharp = parallel_row(face, x, y)
        if spline[x] == spline[y]:
            return sharp
        t = across(face, x, y, True)
        if t is None:
            return sharp
        B = (V[x], edge(x, y), edge(y, x), V[y])
        o_x, o_y = _sub(edge(x, next_to(face, x, y)), V[x]), _sub(edge(y, next_to(face, y, x)), V[y])
        smooth = row_core(B, o_x, o_y, t[0], t[1], 1.0, 0.0 if sphere[x] else 1.0, 0.0 if sphere[y] else 1.0)
        out = []
        for k, (i, j) in enumerate(((x, y), (y, x))):
            w = fade_weight(i, j)
            out.append((smooth[k] if w >= 1.0 else sharp[k]) if isinstance(w, float) else tuple(sharp[k][a] * (1.0 - w) + smooth[k][a] * w for a in range(3)))
        return tuple(out)

    def smooth_row(face, x, y):
        ''' The two control points of the second row next to the edge x -> y of the patch of `face`, such that the patch leaves the edge in the tangent plane the face on the other side leaves it in '''
        t = across(face, x, y)
        if t is None:
            return fade_row(face, x, y)
        B = (V[x], edge(x, y), edge(y, x), V[y])
        o_x, o_y = _sub(edge(x, next_to(face, x, y)), V[x]), _sub(edge(y, next_to(face, y, x)), V[y])
        if not (sphere[x] and sphere[y]) or SHORT.get(x) is None or SHORT.get(y) is None:
            return row_core(B, o_x, o_y, t[0], t[1], 1.0, 0.0 if sphere[x] else 1.0, 0.0 if sphere[y] else 1.0)
        # (an edge between two sphere points: rho is how much of the radius is possible there -- 1 at half the shortest edge, where a cube is a sphere and the faces meet in one tangent plane all along the
        # edge, near 0 for a small radius, where the corners are rounded and the edge stays an edge with flat faces on its two sides; the second row is the one of the sharp edge and the one of the smooth
        # edge, weighed by rho to the power 4)
        rho = Shape.wrap(R[x] / (SHORT[x] * _SPHERE_LIMIT)).min(Shape.wrap(R[y] / (SHORT[y] * _SPHERE_LIMIT)))
        weight = 1.0
        for _step in range(_SPHERE_EDGE_POWER):
            weight = weight * rho
        smooth = row_core(B, o_x, o_y, t[0], t[1], 1.0 + _SPHERE_GAIN * rho)
        sharp = parallel_row(face, x, y)
        return tuple(tuple(sharp[k][a] * (1.0 - weight) + smooth[k][a] * weight for a in range(3)) for k in range(2))

    gregory_made, triangle_made = {}, {}

    def gregory(face):
        ''' The two versions of each of the four inner control points of the patch of a face of four, at its corners a, b, c, d: the version for each of the two edges at that corner '''
        if id(face) not in gregory_made:
            a, b, c, d = face
            r_ab, r_bc, r_dc, r_ad = smooth_row(face, a, b), smooth_row(face, b, c), smooth_row(face, d, c), smooth_row(face, a, d)
            gregory_made[id(face)] = {(1, 1): (r_ab[0], r_ad[0]), (2, 1): (r_ab[1], r_bc[0]), (2, 2): (r_dc[1], r_bc[1]), (1, 2): (r_dc[0], r_ad[1])}
        return gregory_made[id(face)]

    def lead(p, normal, to, ahead=None):
        ''' The control point next to p on a curve to `to` that leaves p in the plane across `normal`: the way the curves leave a spline point.  With `ahead` -- the way into a face from the edge that p is
            the middle of --, the curve is not let to leave backwards: where the plane is so steep that the way to `to`, flattened into it, would point away from the face, it leaves a quarter of the
            distance along `ahead` at least (backwards, the patch of the face would fold over the face on the other side of the edge) '''
        d = _sub(to, p)
        along = _dot(d, normal)
        flat = tuple(d[a] - normal[a] * along for a in range(3))
        if ahead is not None:
            h = _unit(ahead)
            c = _dot(flat, h)
            push = c.max(0.25 * _norm(d)) - c
            flat = tuple(flat[a] + h[a] * push for a in range(3))
        return tuple(p[a] + flat[a] * (1.0 / 3.0) for a in range(3))

    def triangle(face):
        ''' A face of three as three patches of four, one at each corner.  The face is cut by the lines from the middle of each of its edges to the middle of the face, which is where the surface of the face
            would be at its middle if it were the cubic triangle of Vlachos and others (the one the face used to be), and the face is smooth there like a spline point.  Each patch is a Gregory patch,
            joined to the next one in the plane they share, and to the face on the other side of an edge of the cage in the plane that face leaves it in: along the half of the edge that is its own it
            takes the same transversal across the edge the face beyond takes, which is linear from one end to the other, so that the two faces have ONE tangent plane all along it, as two faces of four do.
            Returns the three (net of control points, versions of the inner ones) of the patches at the corners a, b, c, each with its corners in the order (the corner, the middle of the edge to the next
            corner, the middle of the face, the middle of the edge from the corner before) '''
        if id(face) in triangle_made:
            return triangle_made[id(face)]
        a, b, c = face
        b210, b120, b021, b012, b102, b201 = edge(a, b), edge(b, a), edge(b, c), edge(c, b), edge(c, a), edge(a, c)
        around = _combine([(1.0 / 6.0, p) for p in (b210, b120, b021, b012, b102, b201)])
        corners = _combine([(1.0 / 3.0, V[a]), (1.0 / 3.0, V[b]), (1.0 / 3.0, V[c])])
        b111 = _add(around, _combine([(0.5, around), (-0.5, corners)]))
        cubic = {(3, 0, 0): V[a], (0, 3, 0): V[b], (0, 0, 3): V[c], (2, 1, 0): b210, (1, 2, 0): b120, (0, 2, 1): b021, (0, 1, 2): b012, (1, 0, 2): b102, (2, 0, 1): b201, (1, 1, 1): b111}
        # (the middle of the face and the way the surface faces there: the cubic triangle's point and normal at its centre)
        middle = _combine([((1.0 if k in ((3, 0, 0), (0, 3, 0), (0, 0, 3)) else 6.0 if k == (1, 1, 1) else 3.0) / 27.0, p) for k, p in cubic.items()])
        if sphere[a] and sphere[b] and sphere[c] and all(SHORT.get(s) is not None for s in (a, b, c)):
            # (a face of three sphere points whose radius is what is possible there -- the face of a ball of sphere points --: the centre of the cubic triangle of a face that is part of a sphere lies inside the sphere
            # (measured: 1 on a ball of 40 with eight points of radius 20), so it is brought onto the spheres the three points are the corners of -- the point of each of them, centre c + R u along the way to the
            # middle of the three places --, the more so the nearer their radius is to half the shortest edge: rho to the power 4, as at an edge between two sphere points (see smooth_row)).  The user, 2026-11-02, H47
            onto = []
            for s in (a, b, c):
                centre_s = tuple(V[s][k] - NORMALS[s][k] * R[s] for k in range(3))
                way = _unit(_sub(corners, centre_s))
                onto.append((1.0 / 3.0, tuple(centre_s[k] + way[k] * R[s] for k in range(3))))
            target = _combine(onto)
            rho = None
            for s in (a, b, c):
                one = Shape.wrap(R[s] / (SHORT[s] * _SPHERE_LIMIT))
                rho = one if rho is None else rho.min(one)
            weight = rho * rho * rho * rho
            middle = tuple(middle[k] * (1.0 - weight) + target[k] * weight for k in range(3))

        def slope(du, dv, dw):
            terms = []
            for alpha, w in (((2, 0, 0), 1.0), ((0, 2, 0), 1.0), ((0, 0, 2), 1.0), ((1, 1, 0), 2.0), ((1, 0, 1), 2.0), ((0, 1, 1), 2.0)):
                for axis, step in enumerate((du, dv, dw)):
                    if step != 0:
                        terms.append((3.0 * w / 9.0 * step, cubic[tuple(alpha[i] + (1 if i == axis else 0) for i in range(3))]))
            return _combine(terms)
        N_mid = _unit(_cross(slope(-1, 1, 0), slope(-1, 0, 1)))
        order = ((a, b, c), (b, c, a), (c, a, b))                            # (x, the next corner y, the corner z before it)
        rec = {}
        for x, y, z in order:
            B = (V[x], edge(x, y), edge(y, x), V[y])
            M = _combine([(0.125, B[0]), (0.375, B[1]), (0.375, B[2]), (0.125, B[3])])
            tr = across(face, x, y)
            d = _combine([(-0.75, B[0]), (-0.75, B[1]), (0.75, B[2]), (0.75, B[3])])           # (the direction of the edge's curve at its middle)
            # (the surface faces away from the face there: across the edge's direction and the way into this face -- the transversal at the middle where the edge is smooth, the way to the middle of the
            # face where it is sharp)
            into = _scale(0.5, _add(tr[0], tr[1])) if tr is not None else _sub(middle, M)
            N = _unit(_cross(d, into))
            rec[x] = {'M': M, 'tr': tr, 'L1': _combine([(0.5, B[0]), (0.5, B[1])]), 'L2': _combine([(0.25, B[0]), (0.5, B[1]), (0.25, B[2])]),
                      'R1': _combine([(0.25, B[1]), (0.5, B[2]), (0.25, B[3])]), 'R2': _combine([(0.5, B[2]), (0.5, B[3])]),
                      'p1': lead(M, N, middle, into if tr is not None else None), 'p2': lead(middle, N_mid, M)}
        nxt, prv = {a: b, b: c, c: a}, {a: c, b: a, c: b}
        spoke = {}
        for s in (a, b, c):
            # (the line from the middle of the edge s -> next to the middle of the face separates the patch at s from the patch at the next corner: what each of them has as its other edge at the two ends)
            oxm, oym = _sub(rec[s]['L2'], rec[s]['M']), _sub(rec[s]['R1'], rec[s]['M'])
            oxc, oyc = _sub(rec[prv[s]]['p2'], middle), _sub(rec[nxt[s]]['p2'], middle)
            spoke[s] = (oxm, oxc, oym, oyc, _sub(oxm, oym), _sub(oxc, oyc))
        out = []
        for x, y, z in order:
            ex, ez = rec[x], rec[z]
            Mx, Mz = ex['M'], ez['M']
            AB, BC = (V[x], ex['L1'], ex['L2'], Mx), (Mx, ex['p1'], ex['p2'], middle)
            DC, AD = (Mz, ez['p1'], ez['p2'], middle), (V[x], ez['R2'], ez['R1'], Mz)
            o_a_ab, o_b_ab = _sub(ez['R2'], V[x]), _sub(ex['p1'], Mx)
            o_a_ad, o_d_ad = _sub(ex['L1'], V[x]), _sub(ez['p1'], Mz)
            if ex['tr'] is None:
                r_ab = (_add(AB[1], o_a_ab), _add(AB[2], o_b_ab))
            else:
                r_ab = row_core(AB, o_a_ab, o_b_ab, ex['tr'][0], _scale(0.5, _add(ex['tr'][0], ex['tr'][1])))
            if ez['tr'] is None:
                r_ad = (_add(AD[1], o_a_ad), _add(AD[2], o_d_ad))
            else:
                r_ad = row_core(AD, o_a_ad, o_d_ad, ez['tr'][1], _scale(0.5, _add(ez['tr'][0], ez['tr'][1])))
            oxm, oxc, oym, oyc, tm, tc = spoke[x]
            r_bc = row_core(BC, oxm, oxc, tm, tc)
            oxm, oxc, oym, oyc, tm, tc = spoke[z]
            r_dc = row_core(DC, oym, oyc, _scale(-1.0, tm), _scale(-1.0, tc))
            net = [[None] * 4 for _ in range(4)]
            net[0][0], net[3][0], net[3][3], net[0][3] = V[x], Mx, middle, Mz
            net[1][0], net[2][0] = AB[1], AB[2]
            net[3][1], net[3][2] = BC[1], BC[2]
            net[2][3], net[1][3] = DC[2], DC[1]
            net[0][1], net[0][2] = AD[1], AD[2]
            net[1][1] = _sub(_add(net[1][0], net[0][1]), net[0][0])
            net[2][1] = _sub(_add(net[2][0], net[3][1]), net[3][0])
            net[2][2] = _sub(_add(net[2][3], net[3][2]), net[3][3])
            net[1][2] = _sub(_add(net[1][3], net[0][2]), net[0][3])
            out.append((net, {(1, 1): (r_ab[0], r_ad[0]), (2, 1): (r_ab[1], r_bc[0]), (2, 2): (r_dc[1], r_bc[1]), (1, 2): (r_dc[0], r_ad[1])}))
        triangle_made[id(face)] = out
        return out
    drawn_controls, drawn_vertices, drawn_tris = _tessellate(cage, V, edge, _drawn_segments(cage), gregory, triangle)
    field_controls, field_vertices, field_tris = _tessellate(cage, V, edge, _field_segments(cage), gregory, triangle)
    field = _native_field(field_controls, field_vertices, field_tris)
    return _finish(name, items, smooth, radius, ctrls, cage, field, drawn_controls, drawn_vertices, drawn_tris)


def _faces_of_the_script():
    ''' The faces of the body that the call now running makes, from the script: the list `body_faces` above the line `body = morph_polyhedron(...)` (the user, 2026-11-02, "THIS SHOULD BE AN INTERNAL FUCKING
        VARIABLE, BUT I DONT SEE ANY ADVANTAGE TO EXPOSING THIS AS A CALL": the faces are no argument of the call).  The name the result is stored under is read from the instruction after the call in the
        script's own code; None when the call is not stored under a name, or there is no such list -- the faces are then those of the convex hull '''
    try:
        import dis
        frame = sys._getframe(0)
        while frame is not None and ((frame.f_globals.get('__name__') or '').startswith('fieldes')):
            frame = frame.f_back
        if frame is None:
            return None
        code, here = frame.f_code, frame.f_lasti
        instructions = list(dis.get_instructions(code))
        name = None
        for k, ins in enumerate(instructions):
            if ins.offset == here:
                for nxt in instructions[k + 1:k + 4]:
                    if nxt.opname in ('STORE_NAME', 'STORE_GLOBAL', 'STORE_FAST'):
                        name = nxt.argval
                        break
                    if nxt.opname not in ('CACHE', 'PRECALL', 'NOP', 'EXTENDED_ARG'):
                        break
                break
        if not name:
            return None
        found = frame.f_locals.get(name + '_faces')
        if found is None:
            found = frame.f_globals.get(name + '_faces')
        return found
    except Exception:
        return None


def morph_polyhedron(points, smooth=False, radius=0, *, _faces=None):
    ''' A body made of points -- the 3D equivalent of `profile`: four points or more (not all in one plane), joined by flat faces, with a sharp corner at each point.  The faces are those of the convex
        hull of the points (the eight points of a cube, in the order x changes fastest, then y, then z, have its six squares) unless the script has a list of faces named after the body --
        `body_faces = [(0, 2, 3, 1), ...]` above `body = morph_polyhedron(...)`, three or four point numbers each, counterclockwise seen from outside --, which the program writes when the faces are not
        the hull's (a point pressed out of the surface, a dent); it is no argument of the call.  Press its surface and drag: a point is put there and follows the hand.  `smooth` is what a point is that does
        not say (point(..., smooth=True) is a spline point: the surface goes through it and is smooth there; smooth=False, a corner: the default); `radius` is what a corner is that does not say, 0 (a sharp
        corner) by default; point(..., radius=r) is a sphere point: a corner rounded by about a sphere of radius r.  See the module.  With the points at the corners of a box it is that box; made of corners
        only it is exactly the polyhedron of its faces '''
    return _morph('morph_polyhedron', points, smooth, radius, _faces if _faces is not None else _faces_of_the_script())


def morph_body(points, smooth=True, radius=0, *, _faces=None):
    ''' The same body as `morph_polyhedron` (four points or more, the faces those of the convex hull unless the script has the list `body_faces` above the call), with a spline point at each point (a rounded body whose surface
        goes through its points, as the line of a spline goes through its points); `smooth=False` makes every point a corner that does not say otherwise, point(..., smooth=False) makes that one a corner, and
        point(..., radius=r) a sphere point.  Press its surface and drag to put a point there.  See the module '''
    return _morph('morph_body', points, smooth, radius, _faces if _faces is not None else _faces_of_the_script())


for _fn in (morph_polyhedron, morph_body):
    # (the faces are read from the script, no argument of the call: the private keyword `_faces` is for the library -- editable_polyhedron, the checks -- and is not in the signature that the manual and the completion show)
    import inspect as _inspect
    _sig = _inspect.signature(_fn)
    _fn.__signature__ = _sig.replace(parameters=[_p for _p in _sig.parameters.values() if _p.name != '_faces'])
    _fn._makes = 'solid'
    _fn._makes_no_gizmo = True
    _fn._short_call = True

# ---- a measurement for the lag after a point is added to a morph body (H68c): with FIELDES_TIMING set (a test window's launcher sets it, the program as the user starts it does not) each stage that takes more
# than 20 ms writes a line of the window's log with the clock the automation lines use
import os as _os
if _os.environ.get('FIELDES_TIMING'):
    import functools as _ft
    import sys as _sys
    import time as _time

    # (how many expression steps -- Shape.new calls -- each stage makes: the same stage took 100 ms in the first run of a script and 600 ms in the second, and this says whether the work or the speed differs)
    _ops = [0]
    _orig_new = Shape.new.__func__

    def _counting_new(cls, op, *args):
        _ops[0] += 1
        return _orig_new(cls, op, *args)
    Shape.new = classmethod(_counting_new)

    def _timed(label, fn):
        @_ft.wraps(fn)
        def wrapper(*a, **k):
            started = _time.perf_counter()
            steps = _ops[0]
            try:
                return fn(*a, **k)
            finally:
                took = _time.perf_counter() - started
                if took > 0.02:
                    _sys.__stderr__.write('[at %d ms] [morph-stage] %s took %.0f ms, %d expression steps\n' % (int(_time.time() * 1000) % 100000000, label, took * 1000, _ops[0] - steps))
                    _sys.__stderr__.flush()
        return wrapper
    for _name in ('_morph', '_native_field', '_normals', '_tessellate', '_finish', '_flat_tessellation', '_healed', '_default_faces', '_is_polyhedron'):
        if _name in globals():
            globals()[_name] = _timed(_name, globals()[_name])
    try:
        C.controls = _timed('curves.controls', C.controls)
        C.mark = _timed('curves.mark', C.mark)
    except Exception:
        pass
