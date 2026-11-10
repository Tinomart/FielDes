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

The surface of the body is a cage of faces whose corners are the points -- without a list of faces named after the body the faces made of the points at every run (`morph_faces`) (the six quads of a cube for the eight points above; four
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
import os
import sys
import time

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
# The most a patch goes across an edge between two spline points, as a part of the vector across it.  The raw value is 0.8 to 2.2 on every edge of a cube of eight spline points with two points dragged out; with the limit at 1 it was cut
# down on every one of them and the patch turned in a narrow band beside the edge (the worst 1 per cent of the pieces away from the points turned 14.7 degrees against a median of 1.2: the line along the face that is seen); at 3
# the worst 1 per cent turn 9.9 degrees (measured 2026-11-10 on private copies of this file)
# How far the radius of a sphere point can go, as a part of the shortest edge of the cage at the point: the radius that is used is r itself, up to this much of the shortest edge and no further however far the
# scale is turned up -- half an edge is where the roundings at the two ends of an edge meet, and a cube whose eight points have it is a sphere (the user, 2026-11-02: "the Radius still does not make the
# shape end in a proper sphere")
_SPHERE_LIMIT = 0.5
# How far in along the normal a sphere of radius r cuts the corner of a box: r * (sqrt(3) - 1)
_SPHERE_DEPTH = math.sqrt(3.0) - 1.0
# How deep a sphere point may cut a corner, in radii, however narrow the corner is (a ball in a wedge sits r / sin(half the angle) behind the corner), and the weight of the old normal in the solution of its faces (see _morph)
_SPHERE_DEPTH_MAX = 3.0
_SPHERE_FACES_RIDGE = 0.01
# The least influence a sphere point gets from the smallness of its radius against its edges (see _morph)
_SPHERE_K_MIN = 0.4

# H164 step 1b (the user, 2026-11-09: "ON SMALLER FILES IT SHOULD LEGIT TAKE NO TIME AT ALL"): the code that makes the patches can run on plain numbers (morph_num.Num) instead of building a graph of expression nodes;
# _NUMERIC is set only by morph_numbers(), the reference the native port is compared against -- the viewport still uses the graph
_NUMERIC = [False]


_INFO = {}           # (what the run on numbers leaves for the one that finishes the body: its controls, its faces, whether it is a flat polyhedron)
_FIELD_RUN = [False]  # (H164: the run on numbers that makes the FIELD's tessellation, not the drawn one)
_NO_TAPE = [False]
_TRACE = [None]      # (H164 step 2) the list of the inputs of a run that is TRACED into a tape: (expression, number of the input); None otherwise


def _wrap(v):
    from fieldes.stdlib import morph_num
    if _TRACE[0] is not None:
        return morph_num.wrap_trace(v)
    return morph_num.wrap(v) if _NUMERIC[0] else Shape.wrap(v)


def _input(expr):
    ''' A leaf of the tape that is an expression of the point (one of its numbers, a rotation, a radius): the same expression is one input '''
    from fieldes.stdlib import morph_num
    table = _TRACE[0]
    got = table.get(id(expr))
    if got is None:
        got = (expr, len(table))
        table[id(expr)] = got
    return morph_num.Tr.input(got[1])


def _val(expr):
    ''' An expression of the point as it is used here: itself, or its number when the code runs on numbers '''
    if not _NUMERIC[0] or expr is None:
        return expr
    from fieldes.stdlib import morph_num
    if _TRACE[0] is not None:
        if isinstance(expr, (tuple, list)):
            return tuple(_input(e) for e in expr)
        return _input(expr)
    if isinstance(expr, (tuple, list)):
        return tuple(morph_num.Num(C.value_of(e)) for e in expr)
    return morph_num.Num(C.value_of(expr))


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
    return _native_field_numbers(numbers, vertices, triangles)


def _native_field_numbers(numbers, vertices, triangles):
    ''' The same field from the control points as three numbers each (H164: a body made on numbers has no expressions to read them from) '''
    numbers = [tuple(float(v) for v in p) for p in numbers]
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
    ''' The normal at each point, facing out: the unit sum of the normals that the cross-section circles of its edges want at it -- for each edge i -> j with two faces, the circle through the point and the far corner of each of the faces (the other neighbour of i in a face of four, the apex of a face of three), in the plane across
        the chord: its normal at the point, facing the way the two faces face.  A corner of a box gives its diagonal; a narrow face and a wide face share the turn in proportion to their widths by the geometry of the circle.  A point whose edges all have one face keeps the unit normals of its faces weighted by one over their area '''
    faces_of = {}
    for f in cage:
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            faces_of.setdefault((min(a, b), max(a, b)), []).append(f)
    out = []
    for i in range(len(P)):
        total = None
        for (lo, hi), fs in faces_of.items():
            if i not in (lo, hi) or len(fs) != 2:
                continue
            j = hi if i == lo else lo
            O = P[i]
            tau = _unit(_sub(P[j], O))
            nbar = _add(_unit(_face_normal(P, fs[0])), _unit(_face_normal(P, fs[1])))
            m = _unit(_sub(nbar, _scale(_dot(nbar, tau), tau)))
            g = _unit(_cross(tau, m))
            pts_ = []
            for f in fs:
                if len(f) == 4:
                    k = f.index(i)
                    prev_, next_ = f[k - 1], f[(k + 1) % 4]
                    far = P[prev_] if next_ == j else P[next_]
                else:
                    far = P[[v for v in f if v not in (i, j)][0]]
                d = _sub(far, O)
                d = _sub(d, _scale(_dot(d, tau), tau))
                pts_.append((_dot(d, g), _dot(d, m)))
            (xa, ya), (xb, yb) = pts_
            sa, sb = xa * xa + ya * ya, xb * xb + yb * yb
            Ng, Nm = sa * yb - sb * ya, xa * sb - xb * sa
            Nn = (Ng * Ng + Nm * Nm).sqrt() + 1e-12
            sgn = Nm / (Nm * Nm + 1e-12).sqrt()
            nd = _add(_scale(Ng / Nn * sgn, g), _scale(Nm / Nn * sgn, m))
            total = nd if total is None else _add(total, nd)
        if total is None:
            vecs = [_face_normal(P, face) for face in cage if i in face]
            if not vecs:
                out.append((_wrap(0.0), _wrap(0.0), _wrap(1.0)))
                continue
            s2 = [v[0].square() + v[1].square() + v[2].square() for v in vecs]
            top = s2[0]
            for q in s2[1:]:
                top = top.max(q)
            floor = top * 0.02 + 1e-18
            for v, q in zip(vecs, s2):
                term = _scale(1.0 / (q + floor), v)
                total = term if total is None else _add(total, term)
        out.append(_unit(total))
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


def _cage_turn(places, cage):
    ''' How much the surface turns across a face of the cage, in degrees, the most over all the faces: the angle between the normals at the corners of a face, taken at the numbers of the points (the normal of a point as `_morph`
        makes it: the faces that meet there, and where its neighbours are all on one side -- a point pulled out -- the axis they leave along).  A face of a flat body turns 0, a face of a cube of spline points 55, the faces at the tip
        of a horn 90 and more: the number of pieces a face is drawn in follows it (see _drawn_segments) '''
    n = len(places)

    def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

    def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    def unit(a):
        l = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
        return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-300 else (0.0, 0.0, 1.0)
    total = [(0.0, 0.0, 0.0)] * n
    around = [set() for _ in range(n)]
    for f in cage:
        fn = unit(cross(sub(places[f[1]], places[f[0]]), sub(places[f[2]], places[f[0]])))
        for k in range(len(f)):
            v = f[k]
            total[v] = (total[v][0] + fn[0], total[v][1] + fn[1], total[v][2] + fn[2])
            around[v].add(f[(k + 1) % len(f)])
            around[v].add(f[k - 1])
    normals = []
    for i in range(n):
        nv = unit(total[i])
        if len(around[i]) >= 2:
            mean = [0.0, 0.0, 0.0]
            for j in around[i]:
                e = unit(sub(places[j], places[i]))
                mean = [mean[0] + e[0], mean[1] + e[1], mean[2] + e[2]]
            mean = [c / len(around[i]) for c in mean]
            length = math.sqrt(sum(c * c for c in mean))
            if length > 1e-9:
                m = min(1.0, length)
                nv = unit(tuple(nv[a] * (1.0 - m) - (mean[a] / length) * m for a in range(3)))
        normals.append(nv)
    worst = 0.0
    for f in cage:
        for a in range(len(f)):
            for b in range(a + 1, len(f)):
                d = sum(normals[f[a]][k] * normals[f[b]][k] for k in range(3))
                worst = max(worst, math.degrees(math.acos(max(-1.0, min(1.0, d)))))
    return worst


def _drawn_segments(cage, rounded=False, turn=0.0):
    ''' How many pieces each edge of the cage is drawn in.  The viewport shades every triangle flat -- the normal is the triangle's own --, so a rounded body only looks smooth when its triangles are
        small: a turn of two or three degrees from one to the next.  A cage of more faces gets fewer pieces to a face, so that what is drawn stays a few thousand triangles.  What it costs a drag
        step does not depend on it: the surface is evaluated at the control points of its patches (see _tessellate), and every vertex is a sum of those '''
    faces = len(cage)
    base = 20 if faces <= 12 else 16 if faces <= 24 else 10 if faces <= 64 else 6
    # H161 (the user's report, 2026-11-08, "Morph shapes with sphere points get wrinkly"): measured on his script -- 20 pieces to an edge turned neighbouring triangles by up to 20.2 degrees where a sphere
    # point rounds its corner, the creases of his picture, and the surface itself is smooth (40 pieces: 12.8 degrees at most, 60: 9.4).  A body with a rounded corner is drawn in twice as many pieces
    # (only a small body: a body of many faces is drawn in as few pieces as it was -- a run of its statement costs more with more pieces, and a drag runs it again at every step, H154)
    if rounded and faces <= 12:
        base = 80
    # H164 (the user, 2026-11-10, of a horn made by pulling out a corner of the basic body: "garbage unorganic faces in all their glory"; measured on the drawn surface: with 48 pieces to an edge the angle between neighbouring triangles is at
    # most 12 degrees except within 3 mm of the tip, with 20 -- what a body of six faces was drawn in -- up to 45 and the ridge of the horn showed as a line): a face that turns a great deal is drawn in more pieces: one for every
    # 2.5 degrees of the turn across the faces, so that neighbouring triangles turn a few degrees and the flat shading does not show them.  A body of many faces is held to a number of triangles that a drag can bear (about 60,000)
    if turn:
        wanted = int(math.ceil(turn / 2.5))
        cap = max(6, int(math.sqrt(60000.0 / (2.0 * faces))))
        base = max(base, min(wanted, cap))
    # The number follows the resolution of the view, as everything that is drawn does (the user, 2026-11-02: "It does not appear to respond to the resolution when you set it"): the pieces above are
    # the ones at the resolution 10, and a resolution of 5 draws half as many, one of 20 twice as many -- at least 4 to an edge, at most 40, an even number
    try:
        from fieldes import view
        res = view._scene_resolution()
    except Exception:
        res = None
    n = int(round(base * res / 10.0)) if res else base
    n = max(4, min(100 if rounded else 64, n))
    return n + (n & 1)


def _flen(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


_SLIVER_ANGLE = 120.0      # (a corner of a triangle face over this many degrees makes the face a sliver)
_SLIVER_GAIN = 5.0          # (a flip has to take the largest corner of the two faces down by at least this much)
_SLIVER_DIHEDRAL = 70.0     # (and the two faces have to meet at no more than this angle: across a sharper edge a flip changes the shape of the body, not only its faces)


def _fan_centers(faces):
    ''' The points with exactly four faces, all of them triangles: the point a press on a face puts down, the face split in four round it (see the faces of a pressed face in profile_edit): neither a flip nor a merge is to take one of its
        four edges away '''
    count, triangles = {}, {}
    for f in faces:
        for v in f:
            count[v] = count.get(v, 0) + 1
            if len(f) == 3:
                triangles[v] = triangles.get(v, 0) + 1
    return set(v for v, n in count.items() if n == 4 and triangles.get(v, 0) == 4)


def _slivers_flipped(faces, places):
    ''' THE FACES WITHOUT SLIVERS.  The faces are made of the points (see morph_faces) and a triangle with a corner of 138 degrees and two of 23 and 19 -- a long thin face -- is one of them where the points lie so (the horn the user
        marked: the dark wedge down such a face, see the sliver face of the points 26, 30 and 29 of sheep_horn.py).  The surface through a sliver has to turn the whole way across a face that is a third of a millimetre wide, and shows it
        as a groove.  The edge a sliver has along its long side is flipped: the two triangles that share it become the two that the other diagonal of their four points makes, when that takes the largest corner down by at least
        _SLIVER_GAIN degrees, the four points make a convex quad seen along the face, and the two faces meet at no more than _SLIVER_DIHEDRAL degrees (a flip across a sharper edge would change the shape of the body, not only its
        faces).  Worst corner first, until no edge flips.  Measured on the horn (16 pieces to an edge): the length of the lines where neighbouring triangles turn more than 20 degrees went from 3.10 to 1.91 body sizes, and
        on eight bodies that are a cube with corners dragged about it changed nothing.  The faces come back in the order they went in, each face with the turning it had '''
    def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

    def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

    def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    def unit(a):
        l = math.sqrt(dot(a, a))
        return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-300 else (0.0, 0.0, 1.0)

    def corner(p, q, r):
        return math.degrees(math.acos(max(-1.0, min(1.0, dot(unit(sub(places[q], places[p])), unit(sub(places[r], places[p])))))))

    def widest(t):
        return max(corner(t[0], t[1], t[2]), corner(t[1], t[2], t[0]), corner(t[2], t[0], t[1]))

    def normal(t):
        return cross(sub(places[t[1]], places[t[0]]), sub(places[t[2]], places[t[0]]))
    faces = [tuple(f) for f in faces]
    for _ in range(4 * len(faces) + 4):
        edge_faces = {}
        for i, f in enumerate(faces):
            for k in range(len(f)):
                a, b = f[k], f[(k + 1) % len(f)]
                edge_faces.setdefault((min(a, b), max(a, b)), []).append(i)
        best = None
        centers = _fan_centers(faces)
        for (a, b), fs in edge_faces.items():
            if len(fs) != 2 or len(faces[fs[0]]) != 3 or len(faces[fs[1]]) != 3 or a in centers or b in centers:
                continue
            f, g = faces[fs[0]], faces[fs[1]]
            old = max(widest(f), widest(g))
            if old <= _SLIVER_ANGLE or (best is not None and old <= best[0]):
                continue
            k = f.index(a)
            y, z = (a, b) if f[(k + 1) % 3] == b else (b, a)                  # (f runs x, y, z and g runs z, y, w)
            x = [v for v in f if v not in (a, b)][0]
            w = [v for v in g if v not in (a, b)][0]
            if (min(x, w), max(x, w)) in edge_faces:
                continue
            first, second = (x, y, w), (x, w, z)
            n = unit(normal(f))
            if dot(normal(first), n) <= 0.0 or dot(normal(second), n) <= 0.0:
                continue
            if math.degrees(math.acos(max(-1.0, min(1.0, dot(n, unit(normal(g))))))) > _SLIVER_DIHEDRAL:
                continue
            quad = (x, y, w, z)
            if any(dot(cross(sub(places[quad[(i + 1) % 4]], places[quad[i]]), sub(places[quad[i - 1]], places[quad[i]])), n) <= 0.0 for i in range(4)):
                continue
            if max(widest(first), widest(second)) > old - _SLIVER_GAIN:
                continue
            best = (old, fs, first, second)
        if best is None:
            break
        faces[best[1][0]], faces[best[1][1]] = best[2], best[3]
    return faces


_MERGE_COST = 45.0          # (a quad is made of two triangles when none of its corners is more than this many degrees from a right angle...)
_MERGE_DIHEDRAL = 25.0      # (...the two triangles meet at no more than this angle...)
_MERGE_OFFPLANE = 0.25      # (...and the fourth point is no farther from the plane of the other three than this part of the mean edge)
_MERGE_NEEDLE = 30.0        # (and only for a pair of which one triangle is a needle: a corner under this many degrees, or over 110: a pair of well shaped triangles stays two triangles)


def _flat_pairs_merged(faces, places):
    ''' THE FACES AS QUADS WHERE TWO TRIANGLES ARE NEARLY ONE FLAT QUAD.  A face of four is a patch that crosses the face at constant speed (see `chords_across` in _morph); a face of three is cut in three patches by lines to its middle, and
        the creases of a surface made of them lie along the edges of those triangles.  Two triangles that share an edge and are nearly flat together -- the four points convex as seen along the faces, no corner more than _MERGE_COST degrees
        from a right angle, the two triangles meeting at no more than _MERGE_DIHEDRAL degrees, the fourth point no farther than _MERGE_OFFPLANE of the mean edge from the plane of the others, and one of the two a needle (a corner
        under _MERGE_NEEDLE degrees or over 110) -- are made one quad.  Greedy, the squarest first, each triangle in one quad at most.  Measured on the horn (16 pieces to an edge, with `_slivers_flipped` before it): the length of the lines where neighbouring triangles turn more than 20 degrees went from 1.91 to
        0.54 body sizes (all pairs, needles or not: 0.62), and the pairs over 15 degrees from 268 to 198; on eight bodies that are a cube with corners dragged about it left the length of their creases as it was (one of them has a quad now) '''
    def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

    def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

    def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    def unit(a):
        l = math.sqrt(dot(a, a))
        return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-300 else (0.0, 0.0, 1.0)

    def length(a):
        return math.sqrt(dot(a, a))

    def corner(p, q, r):
        return math.degrees(math.acos(max(-1.0, min(1.0, dot(unit(sub(q, p)), unit(sub(r, p)))))))

    def needle(t):
        a, b, c = (places[v] for v in t)
        angles = (corner(a, b, c), corner(b, c, a), corner(c, a, b))
        return min(angles) < _MERGE_NEEDLE or max(angles) > _SLIVER_ANGLE - 10.0
    faces = [tuple(f) for f in faces]
    edge_faces = {}
    for i, f in enumerate(faces):
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            edge_faces.setdefault((min(a, b), max(a, b)), []).append(i)
    candidates = []
    centers = _fan_centers(faces)
    for (a, b), fs in sorted(edge_faces.items()):
        if len(fs) != 2 or len(faces[fs[0]]) != 3 or len(faces[fs[1]]) != 3 or a in centers or b in centers:
            continue
        f, g = faces[fs[0]], faces[fs[1]]
        k = f.index(a)
        y, z = (a, b) if f[(k + 1) % 3] == b else (b, a)
        x = [v for v in f if v not in (a, b)][0]
        w = [v for v in g if v not in (a, b)][0]
        quad = (x, y, w, z)
        P = [places[v] for v in quad]
        n = unit(cross(sub(P[2], P[0]), sub(P[3], P[1])))
        worst, convex = 0.0, True
        for i in range(4):
            p, q, r = P[i], P[(i + 1) % 4], P[i - 1]
            ang = corner(p, q, r)
            worst = max(worst, abs(ang - 90.0))
            if dot(cross(sub(q, p), sub(r, p)), n) <= 0.0 or ang > 165.0:
                convex = False
        if not convex or worst > _MERGE_COST:
            continue
        if not (needle(f) or needle(g)):
            continue
        nf = unit(cross(sub(places[f[1]], places[f[0]]), sub(places[f[2]], places[f[0]])))
        ng = unit(cross(sub(places[g[1]], places[g[0]]), sub(places[g[2]], places[g[0]])))
        if math.degrees(math.acos(max(-1.0, min(1.0, dot(nf, ng))))) > _MERGE_DIHEDRAL:
            continue
        n3 = unit(cross(sub(P[1], P[0]), sub(P[2], P[0])))
        mean_edge = sum(length(sub(P[(i + 1) % 4], P[i])) for i in range(4)) / 4.0
        if abs(dot(sub(P[3], P[0]), n3)) / mean_edge > _MERGE_OFFPLANE:
            continue
        candidates.append((worst, fs[0], fs[1], quad))
    candidates.sort()
    used, quads = set(), {}
    for worst, i, j, quad in candidates:
        if i in used or j in used:
            continue
        used.update((i, j))
        quads[i] = quad
    return [quads[i] if i in quads else f for i, f in enumerate(faces) if i not in used or i in quads]


def _alpha_faces(pos, with_dents=False, repair=True):
    ''' `repair`: the faces are made better shaped for a smooth surface (_slivers_flipped, _flat_pairs_merged); not for a polyhedron, whose faces are the flat faces of the hull of its points '''
    from fieldes.stdlib import morph_faces as MF
    places = [tuple(float(v) for v in p) for p in pos]
    polys, dents = MF.polygons_of(places, True)
    # (the faces in a canonical order: each starts at its smallest point, keeping its turning, and the list is sorted -- the same faces come out in the same order wherever the points are, so that a drag that changes no face changes no
    # vertex of the drawn surface but those near the point, the drawn vertices can be compared one for one, and a structure that is the same is the same structure for the traced numbers)
    rings = [tuple(int(i) for i in ring) for ring, nn in polys]
    if repair:
        rings = _flat_pairs_merged(_slivers_flipped(rings, places), places)
    canonical = []
    for ring in rings:
        k = ring.index(min(ring))
        canonical.append(tuple(ring[k:] + ring[:k]))
    faces = tuple(sorted(canonical))
    return (faces, dents) if with_dents else faces


# THE FACES OF A BODY ARE A FUNCTION OF ITS POINTS AND NOTHING ELSE (H171, the user, 2026-11-11: "The face is deterministic always and is exactly what it would be when you run the code fresh. Otherwise the user works on something closes and opens their file and it
# is completely different").  Until then the faces of the run before were kept and given to the next run whose points were the same but for four ("what you pull is not what you get", 2026-11-10: pressed out and released, 16 of 49 faces of sheep_horn differed): a point that was
# deleted and a new point put on the other side of the body was, to that test, a point that had moved, and was joined to the neighbours of the point that was there, through the body; and a window that had pressed and deleted showed a body that the same file shows
# otherwise in a window that has just opened it.  There are no faces of the runs before any more: what is pulled is what the points make, at every frame and after the release.
# What is kept is only a memory of what THESE places make -- found again for the same places instead of made again, which is the same as making them again -- and never of what other places made.
_FACES_MADE = {}           # (the kind of body, the places) -> the faces those places make
_FACES_MADE_MOST = 16      # (how many sets of places the memory holds: the newest)


def _faces_of_the_places(name, pos, made_as=None):
    ''' The faces of the body of the places `pos`: the faces those places make by themselves (see _default_faces, which is told `made_as or name`), the same at every run and in every window -- a function of the places and of nothing that happened before '''
    kind = made_as or name
    key = (kind, tuple(tuple(float(v) for v in p) for p in pos))
    cage = _FACES_MADE.get(key)
    if cage is None:
        cage = _default_faces(kind, pos)
        while len(_FACES_MADE) >= _FACES_MADE_MOST:
            del _FACES_MADE[next(iter(_FACES_MADE))]
        _FACES_MADE[key] = cage
    return cage


def _default_faces(name, pos):
    ''' The faces of a body of the places `pos` that says none: made of the places at every run (see morph_faces): the faces between the places that are near each other, the hull where the body is convex, a place pressed into
        the body joined to the face under it.  Eight places that are the corners of a cube in the order the module says have the six squares of the cube, as the body of eight always had -- as long as the squares turn out and
        do not pass through each other '''
    from fieldes.stdlib import morph_faces as MF
    places = [tuple(float(v) for v in p) for p in pos]
    MF.refuse_flat(name, places)
    faces, dents = _alpha_faces(places, True, name != 'morph_polyhedron')
    if len(places) == 8:
        cube = tuple(CUBE_FACES)
        squares = [set(f) for f in cube]
        if all(any(set(h) <= s for s in squares) for h in faces):
            middle = tuple(sum(p[k] for p in places) / 8.0 for k in range(3))
            turned_out = all(_dot(_cross(_sub(places[f[1]], places[f[0]]), _sub(places[f[2]], places[f[0]])),
                                  _sub(tuple(sum(places[v][k] for v in f) / 4.0 for k in range(3)), middle)) > 0.0 for f in cube)
            # (as the body of eight always had: a point pushed into the body keeps the six squares -- the surface goes THROUGH a spline point, so the squares at it are bent in -- unless the squares pass through each other; with every point out, the six squares while each one turns outward)
            # (a square is a square only while it IS one: a quad whose four corners are twisted -- the two ways it can be cut in triangles lean opposite ways, the dot of their normals negative or nearly zero -- is folded
            # over itself, and its patch is a fold; found by the stress test over random bodies made of a cube with corners dragged about: of 40, the ones that folded used squares of that kind.  Such a body is made of
            # triangles, the faces its points make)
            def lean(a, b, c, d):
                ''' How much the triangles (a, b, c) and (a, c, d) lean the same way: the dot of their unit normals '''
                n1 = _cross(_sub(places[b], places[a]), _sub(places[c], places[a]))
                n2 = _cross(_sub(places[c], places[a]), _sub(places[d], places[a]))
                return _dot(n1, n2) / (math.sqrt(_dot(n1, n1) * _dot(n2, n2)) + 1e-300)
            proper = all(min(lean(f[0], f[1], f[2], f[3]), lean(f[1], f[2], f[3], f[0])) > 0.3 for f in cube)
            if proper and ((dents and not _cage_crosses(cube, places)) or (not dents and turned_out)):
                return cube
    return faces


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


def _fill_holes(faces, gone, pos):
    ''' The faces without the points `gone` -- those that name one of them are taken away -- and the holes that leaves closed again: the edges of the faces that went, those that lie between points that stay, run
        round each hole in the direction they had, and each loop is closed by faces of the points that are there, at the places `pos` (all the points, the ones that go too): a loop of three is one triangle, a loop of four
        that is a convex quadrilateral one square (the four triangles round a point that was pressed out of a square make the square again), any other loop the triangles of the cheapest triangulation of it -- the least total
        area -- none of whose new edges is an edge of the body already (H171, the user, 2026-11-11: "I deleted a point and the randomly a point on the opposite side of the body got chosen to be connected, so then the mesh goes
        through itself": the loop was closed by a fan from its first point, whichever that was, and on random bodies 11 per cent of the deletions of one point left faces that cut through each other, 61 per cent of five in a row).
        None when a hole is not a closed loop, a loop cannot be closed so, or the faces that are left cut through each other or are not a closed surface -- the faces are then those that the points make.
        Where the places are not known (`pos` is None: the script has not run) only a loop of three, one triangle, and a loop of four, one square, are closed; any other hole is None '''
    kept = [tuple(f) for f in faces if not any(v in gone for v in f)]
    edges = {}
    for f in faces:
        if not any(v in gone for v in f):
            continue
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            if a not in gone and b not in gone:
                edges[a] = b
    existing = set()
    for f in kept:
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            existing.add((min(a, b), max(a, b)))
    made, seen = [], set()
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
        closed = _close_loop(loop, pos, existing)
        if closed is None:
            return None
        made.extend(closed)
    result = kept + made
    count = {}
    for f in result:
        for k in range(len(f)):
            e = (min(f[k], f[(k + 1) % len(f)]), max(f[k], f[(k + 1) % len(f)]))
            count[e] = count.get(e, 0) + 1
    if any(c != 2 for c in count.values()) or (pos is not None and _cage_crosses(result, pos)):
        return None
    return result


def _close_loop(loop, pos, existing):
    ''' The faces that close the hole whose boundary is `loop` (the points in the order the edges of the faces that went run round it), see _fill_holes; None when there are none '''
    n = len(loop)
    if n == 3:
        return [tuple(loop)]
    if pos is None:
        return [tuple(loop)] if n == 4 else None
    p = [pos[v] for v in loop]
    if n == 4:
        normal = (0.0, 0.0, 0.0)
        for k in range(4):
            c = _cross(p[k], p[(k + 1) % 4])
            normal = (normal[0] + c[0], normal[1] + c[1], normal[2] + c[2])
        if all(_dot(_cross(_sub(p[(k + 1) % 4], p[k]), _sub(p[(k + 2) % 4], p[(k + 1) % 4])), normal) > 0.0 for k in range(4)):
            return [tuple(loop)]

    def side_or_free(i, j):
        # (a side of the loop, or a new edge between two points that no face joins yet)
        if j == i + 1 or (i == 0 and j == n - 1):
            return True
        a, b = loop[i], loop[j]
        return (min(a, b), max(a, b)) not in existing
    inf = float('inf')
    cost, split = {}, {}
    for span in range(2, n):
        for i in range(0, n - span):
            j = i + span
            best, at = inf, None
            for k in range(i + 1, j):
                if not (side_or_free(i, k) and side_or_free(k, j)):
                    continue
                c = cost.get((i, k), 0.0) if k - i >= 2 else 0.0
                d = cost.get((k, j), 0.0) if j - k >= 2 else 0.0
                if c == inf or d == inf:
                    continue
                area = 0.5 * _flen(_cross(_sub(p[k], p[i]), _sub(p[j], p[i])))
                if c + d + area < best:
                    best, at = c + d + area, k
            cost[(i, j)], split[(i, j)] = best, at
    if cost.get((0, n - 1), inf) == inf:
        return None
    out, todo = [], [(0, n - 1)]
    while todo:
        i, j = todo.pop()
        if j - i < 2:
            continue
        k = split[(i, j)]
        out.append((loop[i], loop[k], loop[j]))
        todo.append((i, k))
        todo.append((k, j))
    return out


def _healed(name, cage, n, positions):
    ''' The faces of a body of `n` points when some of them name a point that is not there (a point was deleted and the faces were not told): they are taken away and the holes closed (see _fill_holes); when
        that cannot be done the faces are those made of the points.  Faces that are right are returned as they are '''
    gone = set(v for f in cage for v in f if not 0 <= v < n)
    if not gone:
        return cage
    filled = _fill_holes(cage, gone, positions)
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
        graph_controls = drawn_controls.expressions() if isinstance(drawn_controls, TapeOut) else drawn_controls
        controls_now = [mapper(tuple(Shape.wrap(v) for v in c)) for c in graph_controls]
        tris_now = [(t[0], t[2], t[1]) for t in drawn_tris] if improper else drawn_tris
        return _finish(name, items_now, smooth, radius, ctrls_now, faces_now, field_now, controls_now, drawn_vertices, tris_now)
    out._remake_mapped = remake_mapped
    out._morph_faces = cage                                      # (the faces as they are now: what pressing the surface splits -- see profile_edit)
    # (what the viewport draws, from the numbers of the points at every step of a drag -- nothing is meshed from the field, see Shape::morphNow: the control points of the patches as expressions
    # of the numbers, each worked out once; every vertex of the surface as a list of (control point, weight) -- `_morph_vstart[v]` to `_morph_vstart[v + 1]` of the two lists `_morph_vctrl` and
    # `_morph_vweight` --; and the triangles between the vertices)
    if isinstance(drawn_controls, TapeOut):
        ops = drawn_controls.tape.ops
        out._morph_tape = ([o[0] for o in ops], [o[1] for o in ops], [o[2] for o in ops], list(drawn_controls.tape.constants), [i for o in drawn_controls.outs for i in o])
        out._morph_inputs = [Shape.wrap(e) for e in drawn_controls.inputs]
    else:
        out._morph_controls = [tuple(Shape.wrap(c) for c in p) for p in drawn_controls]
    out._morph_vstart, out._morph_vctrl, out._morph_vweight = [0], [], []
    for terms in drawn_vertices:
        for c, weight in terms:
            out._morph_vctrl.append(c)
            out._morph_vweight.append(float(weight))
        out._morph_vstart.append(len(out._morph_vctrl))
    out._morph_tris = drawn_tris
    out._editable = True                                        # (a press on its surface is where a point is put, not a surface to pull by the numbers of the points)
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


def _faces_between_closest_points(cage, positions, name='body'):
    ''' THE FACES ARE MADE FROM THE POINTS (H104, the user, 2026-11-03: "faces are created between the closest points ... the feature stays the same"; H164, the user, 2026-11-09: no face logic of its own, no list of
        faces that locks a point): whatever list of faces a body was given, a body of up to _FOLLOW_LIMIT points has the faces its points make by themselves -- see `_default_faces` -- and nothing else, the same in every window
        and after every close and open of the file (H171, 2026-11-11; see `_faces_of_the_places`), and a body of more points (an import) keeps the faces it was given: nothing is made of them that costs the time of the points '''
    if len(positions) > _FOLLOW_LIMIT:
        return cage
    return _faces_of_the_places(name, positions, 'body')


_T_START = [0.0]





_SUBDIV_CACHE = {}
_REACH = 4                           # (how many edges away a point still has a weight on a control point, see _subdiv_topology)
_PATCH_POINTS = 48                   # (the control points of a body of more than 400 points are solved on patches: a cluster of this many points and every point within _REACH + _PATCH_HALO edges of it, see libfive_cc_controls)
_PATCH_HALO = 3


def _lin(terms):
    ''' The linear combination of dictionaries (a point of the subdivision as the weights of the control points) '''
    out = {}
    get = out.get
    for d, c in terms:
        for k, w in d.items():
            out[k] = get(k, 0.0) + c * w
    return out


# (the kinds of the points of a mesh: a corner point stays (1), a spline point is always smooth -- a crease that comes to it from a corner ends there (2) --, the points that the steps make are what their edges say (0): no crease at them smooth, two a crease through them, more a corner)
_K_AUTO, _K_CORNER, _K_SMOOTH = 0, 1, 2


def _crease_neighbours(n, creases):
    out = [[] for _ in range(n)]
    for a, b in creases:
        out[a].append(b)
        out[b].append(a)
    return out


def _cc_step(verts, faces, kind, creases, par=None):
    ''' One step of Catmull-Clark on a closed mesh of polygons, with creases: the points of the edges first (the two points of the edge and the points of the two faces on it, each a quarter; a crease edge: the middle of it), the points of the faces (their mean), the new places of the points
        ((F + 2 R + (n - 3) v) / n, F the mean of the points of its faces, R the mean of the middles of its edges, n its valence; a corner stays; a point with two crease edges moves along the crease: (e1 + 6 v + e2) / 8); every face of m sides makes m quadrilaterals and
        every crease edge two.  None where the mesh is not closed '''
    nv = len(verts)
    edge_faces = {}
    for fi, f in enumerate(faces):
        m = len(f)
        for k in range(m):
            a, b = f[k], f[(k + 1) % m]
            edge_faces.setdefault((min(a, b), max(a, b)), []).append(fi)
    for fl in edge_faces.values():
        if len(fl) != 2:
            return None
    fpts = [_lin([(verts[v], 1.0 / len(f)) for v in f]) for f in faces]
    edge_index, ev = {}, []
    for key, fl in edge_faces.items():
        a, b = key
        edge_index[key] = nv + len(ev)
        if key in creases:
            ev.append(_lin([(verts[a], 0.5), (verts[b], 0.5)]))
        else:
            ev.append(_lin([(verts[a], 0.25), (verts[b], 0.25), (fpts[fl[0]], 0.25), (fpts[fl[1]], 0.25)]))
    vfaces = [[] for _ in range(nv)]
    vedges = [[] for _ in range(nv)]
    for fi, f in enumerate(faces):
        for v in f:
            vfaces[v].append(fi)
    for a, b in edge_faces:
        vedges[a].append(b)
        vedges[b].append(a)
    cnb = _crease_neighbours(nv, creases)
    new_verts = []
    for v in range(nv):
        n = len(vedges[v])
        if n < 2 or len(vfaces[v]) != n:
            return None
        if kind[v] == _K_CORNER or (kind[v] == _K_AUTO and len(cnb[v]) >= 3):
            new_verts.append(dict(verts[v]))
            continue
        if kind[v] == _K_AUTO and len(cnb[v]) == 2:
            new_verts.append(_lin([(verts[v], 0.75), (verts[cnb[v][0]], 0.125), (verts[cnb[v][1]], 0.125)]))
            continue
        # (the rule (F + 2 R + (n - 3) v) / n -- F the mean of the points of its faces, R the mean of the middles of its edges -- is (n - 2) / n v + 1 / n^2 (the sum of its neighbours + the sum of the points of its faces): one sum, not a sum of sums of sums, which was
        # most of the time of a first drawing of a body with a new point in it)
        n2 = 1.0 / (n * n)
        new_verts.append(_lin([(verts[v], (n - 2.0) / n)] + [(verts[w], n2) for w in vedges[v]] + [(fpts[fi], n2) for fi in vfaces[v]]))
    fbase = nv + len(ev)
    faces2 = []
    par2 = [] if par is not None else None
    for fi, f in enumerate(faces):
        m = len(f)
        for k in range(m):
            e_next = edge_index[(min(f[k], f[(k + 1) % m]), max(f[k], f[(k + 1) % m]))]
            e_prev = edge_index[(min(f[k - 1], f[k]), max(f[k - 1], f[k]))]
            faces2.append((f[k], e_next, fbase + fi, e_prev))
            if par is not None:
                # (where the four corners of the new quadrilateral are in the square (s, t) of the quadrilateral of the net it comes from: the corner, the middle of the edge to the next, the middle of the face, the middle of the edge from the
                # last -- the parameter lines along which the mesh is drawn at any number of pieces, see _sample_pieces)
                quad, pts = par[fi]
                pk, pn, pp = pts[k], pts[(k + 1) % m], pts[k - 1]
                mid = (sum(p[0] for p in pts) / m, sum(p[1] for p in pts) / m)
                par2.append((quad, (pk, ((pk[0] + pn[0]) / 2.0, (pk[1] + pn[1]) / 2.0), mid, ((pk[0] + pp[0]) / 2.0, (pk[1] + pp[1]) / 2.0))))
    creases2 = set()
    for a, b in creases:
        e = edge_index[(a, b)]
        creases2.add((min(a, e), max(a, e)))
        creases2.add((min(b, e), max(b, e)))
    kind2 = list(kind) + [_K_AUTO] * (len(ev) + len(faces))
    if par is not None:
        return new_verts + ev + fpts, faces2, kind2, creases2, par2
    return new_verts + ev + fpts, faces2, kind2, creases2


def _combine_terms(terms, controls, a):
    ''' The coordinate a of the sum of weight * control point over (the number of a control point, weight) pairs '''
    total = None
    for c, w in terms:
        t = controls[c][a] * w
        total = t if total is None else total + t
    return total


def _first_harmonic(n):
    ''' What the first harmonic of the ring of a point of valence n says about the surface at it.  The first step of Catmull-Clark takes the first harmonic (a, b) of the edge points and of the face points of the ring (a point k of the ring is Re(a w^k), w = exp(2 pi i / n)) to
        (a / 4 + (1 + conj w) (a (1 + w) + b) / 16, (a (1 + w) + b) / 4): a matrix of determinant 1/16 and trace 1/2 + (1 + cos(2 pi / n)) / 8, the larger eigenvalue lam of which makes the TANGENT PLANE of the limit surface: the limit tangent, as a complex vector, is  a + lb b  with
        lb = (1 + conj w) / (16 (lam - 1/4))  (the left eigenvector).  Returns (w, lb) '''
    import cmath
    w = cmath.exp(2j * math.pi / n)
    c = math.cos(2.0 * math.pi / n)
    tr = 0.5 + (1.0 + c) / 8.0
    lam = (tr + math.sqrt(tr * tr - 0.25)) / 2.0
    return w, (1.0 + w.conjugate()) / (16.0 * (lam - 0.25))


def _subdiv_topology(cage, sharp):
    ''' What depends on the cage and on which points are corners only: the first step of the subdivision (the net), and the weights B (control point C_i = sum_j B_ij of the points) that make the LIMIT position of every point the point itself as far as the control points can -- the limit of a point of valence n is
        (n^2 v + 4 sum of the edge points of its ring + sum of the face points of its ring) / (n (n + 5)) of the net, a corner point stays; solved by inverting the matrix L of the limit weights.  The edges that have a corner at an end are crease edges along the half of the edge next to the corner: the crease ends at the middle of the edge, where
        the surface is smooth.  None where the cage is not a closed surface (every edge in two faces, every point in at least three edges) '''
    nv0 = 1 + max(max(f) for f in cage)
    faces = [tuple(f) for f in cage]
    # (a point where two sheets of the surface touch -- the faces round it make more than one fan, joined by no edge -- is split into one copy for each fan, at the place of the point: each sheet is a smooth surface of its own through it, and the cage is a closed
    # surface again)
    orig = list(range(nv0))
    at_point = [[] for _ in range(nv0)]
    for fi, f in enumerate(faces):
        for v in f:
            at_point[v].append(fi)
    for v in range(nv0):
        around = at_point[v]
        parent = {fi: fi for fi in around}

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x
        by_neighbour = {}
        for fi in around:
            f = faces[fi]
            k = f.index(v)
            for u in (f[(k + 1) % len(f)], f[k - 1]):
                by_neighbour.setdefault(u, []).append(fi)
        for fl in by_neighbour.values():
            for x in fl[1:]:
                parent[find(x)] = find(fl[0])
        fans = {}
        for fi in around:
            fans.setdefault(find(fi), []).append(fi)
        for fan in list(fans.values())[1:]:
            new_id = len(orig)
            orig.append(v)
            for fi in fan:
                faces[fi] = tuple(new_id if x == v else x for x in faces[fi])
    nv = len(orig)
    sharp = frozenset(i for i in range(nv) if orig[i] in sharp)
    cage_faces = faces
    edge_set = set()
    for f in cage_faces:
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            edge_set.add((min(a, b), max(a, b)))
    creases0 = {key for key in edge_set if key[0] in sharp or key[1] in sharp}
    kind0 = [_K_CORNER if v in sharp else _K_SMOOTH for v in range(nv)]
    verts0 = [{i: 1.0} for i in range(nv)]
    step = _cc_step(verts0, cage_faces, kind0, creases0)
    if step is None:
        return None
    verts1, faces1, kind1, creases1 = step
    # (THE CREASE OF A CORNER RUNS ALONG THE HALF OF EACH OF ITS EDGES that is next to it -- both halves of an edge between two corners --, and ends at the middle of the edge, where the surface is smooth: a corner shows its sharp edges in its own range and does not run a
    # crease the whole way to the next point, which on a body of scattered points ran valleys and craters across the surface)
    creases1 = {(x, e) for (x, e) in creases1 if x in sharp}
    # (the order of the neighbours round every point, counterclockwise seen from outside, the face between two neighbours that follow each other, and where the points of the edges and of the faces are in the net: what the first harmonic of a ring -- the
    # tangent plane of the limit surface at the point -- is made of, for the influence and the rotation of a point)
    faces_at = [[] for _ in range(nv)]
    for fi, f in enumerate(cage_faces):
        for v in f:
            faces_at[v].append(fi)
    ring, ringf = [], []
    for v in range(nv):
        succ, via = {}, {}
        for fi in faces_at[v]:
            f = cage_faces[fi]
            k = f.index(v)
            succ[f[(k + 1) % len(f)]] = f[k - 1]
            via[f[(k + 1) % len(f)]] = fi
        start = next(iter(succ))
        order, cur = [], start
        while cur is not None and cur not in order:
            order.append(cur)
            cur = succ.get(cur)
        if cur != start or len(order) != len(succ):
            return None
        ring.append(order)
        ringf.append([via[u] for u in order])
    edge_net, face_net, idx = {}, {}, 0
    for fi, f in enumerate(cage_faces):
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            edge_net[(min(a, b), max(a, b))] = faces1[idx][1]
            face_net[fi] = faces1[idx][2]
            idx += 1
    ring_e = [set() for _ in range(nv)]
    ring_f = [set() for _ in range(nv)]
    for q in faces1:
        v = q[0]
        ring_e[v].add(q[1])
        ring_e[v].add(q[3])
        ring_f[v].add(q[2])
    L = []
    for v in range(nv):
        if v in sharp:
            L.append({v: 1.0})
            continue
        n = len(ring_e[v])
        denom = n * (n + 5.0)
        L.append(_lin([(verts1[v], n * n / denom)] + [(verts1[e], 4.0 / denom) for e in ring_e[v]] + [(verts1[f], 1.0 / denom) for f in ring_f[v]]))
    # (the limit matrix L can be singular -- the corners of a cube: the alternating mode has the limit weight 0, a point of valence 3 has no weight on itself in its first step -- so the control points are the minimal solution of L C = P:  C = L^T (L L^T + mu I)^-1 P, which is the exact solution wherever one exists)
    # (THE REACH, the user, 2026-11-10: "shifting one point on one side of the geometry can COMPLETELY CHANGE THE OTHER SIDE"): the weight of a point on the control point of another falls away with the number of edges between them -- the matrix L is the
    # smoothing of the surface and its inverse is the opposite, which dies out the way a smoothing does.  A point further than _REACH edges away has no weight on the control point at all, and what is left is made one: the weights of a control point add up to 1, so that a body
    # that moves moves its surface with it.  The rows of the weights B (control point C_i = sum_j B_ij P_j) are the solution of (L L^T + mu I) y = L e_i for the points within the reach: the kernel makes them -- for the points of a body of up to 400 on one dense matrix, as this
    # function did, for more points on patches of the body, in time proportional to its size: libfive_cc_controls, the user, 2026-11-10: "I dont see why there would a reason to restrict the user")
    near = [set() for _ in range(nv)]
    for f in cage_faces:
        for k in range(len(f)):
            near[f[k]].add(f[(k + 1) % len(f)])
            near[f[(k + 1) % len(f)]].add(f[k])
    l_start, l_column, l_weight = array.array('i', [0]), array.array('i'), array.array('d')
    for row in L:
        for column, weight in sorted(row.items()):
            l_column.append(column)
            l_weight.append(weight)
        l_start.append(len(l_column))
    n_start, n_column = array.array('i', [0]), array.array('i')
    for u in range(nv):
        n_column.extend(sorted(near[u]))
        n_start.append(len(n_column))
    flags = array.array('i', [1 if v in sharp else 0 for v in range(nv)])           # (a corner is its own control point, exactly: the edge between two corners is a straight line)
    handle = lib.libfive_cc_controls(nv, l_start.buffer_info()[0], l_column.buffer_info()[0], l_weight.buffer_info()[0], n_start.buffer_info()[0], n_column.buffer_info()[0],
                                     flags.buffer_info()[0], _REACH, _PATCH_HALO, _PATCH_POINTS)
    if not handle:
        return None
    try:
        rows_, count_, none_ = ctypes.c_int32(), ctypes.c_int32(), ctypes.c_int32()
        lib.libfive_cc_sizes(handle, ctypes.byref(rows_), ctypes.byref(count_), ctypes.byref(none_))
        start = array.array('i', bytes(4 * (rows_.value + 1)))
        column = array.array('i', bytes(4 * count_.value))
        weight = array.array('d', bytes(8 * count_.value))
        lib.libfive_cc_copy(handle, start.buffer_info()[0], column.buffer_info()[0], weight.buffer_info()[0], None, None, None)
    finally:
        lib.libfive_cc_free(handle)
    s, col, wt = start.tolist(), column.tolist(), weight.tolist()
    B = [list(zip(col[s[i]:s[i + 1]], wt[s[i]:s[i + 1]])) for i in range(nv)]
    return {'orig': orig, 'v1': verts1, 'f1': faces1, 'kind1': kind1, 'creases1': creases1, 'sharp': sharp, 'levels': {}, 'B': B,
            'ring': ring, 'ringf': ringf, 'edge_net': edge_net, 'face_net': face_net}


def _subdiv_level(topo, level):
    ''' The mesh after `level` steps (1 or more) from the net (the mesh after the first step) as (the weights of the points of the net of every point -- at its LIMIT position --, the triangles).  The steps of the rule and the limit are made by the kernel
        (libfive_cc_levels: the same rule as _cc_step, and the limit position of a vertex (n^2 v + 4 sum of its edge neighbours + sum of its diagonal neighbours) / (n (n + 5)) -- a corner is itself, a point with two crease edges is (e1 + 4 v + e2) / 6 along the
        crease --; the drawn surface is the limit, not the mesh of the last step: a vertex that is a point of the cage is the point, the user, 2026-11-10: "the body does not go through the spline points always").  The Python of it took 0.4 s of a body
        of 36 points, at every run that put a point in; None where the mesh is not a closed surface '''
    got = topo['levels'].get(level)
    if got is not None:
        return got
    quads = array.array('i')
    for f in topo['f1']:
        quads.extend(f)
    kind = array.array('i', topo['kind1'])
    pairs = array.array('i')
    for a, b in topo['creases1']:
        pairs.extend((a, b))
    handle = lib.libfive_cc_levels(len(topo['v1']), len(topo['f1']), quads.buffer_info()[0], kind.buffer_info()[0], len(pairs) // 2, pairs.buffer_info()[0] if pairs else None, level)
    if not handle:
        return None
    try:
        nv, nw, nq = ctypes.c_int32(), ctypes.c_int32(), ctypes.c_int32()
        lib.libfive_cc_sizes(handle, ctypes.byref(nv), ctypes.byref(nw), ctypes.byref(nq))
        nv, nw, nq = nv.value, nw.value, nq.value
        start = array.array('i', bytes(4 * (nv + 1)))
        column = array.array('i', bytes(4 * nw))
        weight = array.array('d', bytes(8 * nw))
        fquads = array.array('i', bytes(16 * nq))
        parent = array.array('i', bytes(4 * nq))
        corner = array.array('d', bytes(64 * nq))
        lib.libfive_cc_copy(handle, start.buffer_info()[0], column.buffer_info()[0], weight.buffer_info()[0], fquads.buffer_info()[0], parent.buffer_info()[0], corner.buffer_info()[0])
    finally:
        lib.libfive_cc_free(handle)
    s, col, wt = start.tolist(), column.tolist(), weight.tolist()
    vl = [list(zip(col[s[i]:s[i + 1]], wt[s[i]:s[i + 1]])) for i in range(nv)]
    q = fquads.tolist()
    tr = []
    for i in range(0, len(q), 4):
        a, b, c, d = q[i], q[i + 1], q[i + 2], q[i + 3]
        tr.append((a, b, c))
        tr.append((a, c, d))
    topo.setdefault('cc_raw', {})[level] = (fquads, parent, corner)
    topo['levels'][level] = (vl, tr)
    return topo['levels'][level]


def _faces_par(topo, level):
    ''' The faces of the mesh at `level` and, for each, its quadrilateral of the net and where its four corners are in it (s, t) -- what _sample_pieces reads; made from the arrays of _subdiv_level when it is asked for '''
    got = topo.setdefault('faces_par', {}).get(level)
    if got is None:
        fquads, parent, corner = topo['cc_raw'][level]
        q, p, c = fquads.tolist(), parent.tolist(), corner.tolist()
        faces = [tuple(q[i:i + 4]) for i in range(0, len(q), 4)]
        par = [(p[i], ((c[8 * i], c[8 * i + 1]), (c[8 * i + 2], c[8 * i + 3]), (c[8 * i + 4], c[8 * i + 5]), (c[8 * i + 6], c[8 * i + 7]))) for i in range(len(p))]
        got = topo['faces_par'][level] = (faces, par)
    return got


def _sample_pieces(topo, level, vl, pieces):
    ''' THE MESH AT ANY NUMBER OF PIECES (the user, 2026-11-10: the mesher has an argument that says how fine it meshes): a cage edge is drawn in `pieces` pieces (even: half of them on each of the two quadrilaterals of the net that have it as a side), every
        quadrilateral of the net as a square (s, t) of pieces / 2 to a side.  The mesh of the surface at `level` (2 ** (level - 1) cells to a side of a quadrilateral of the net, vertices at their limit places, `vl`) is as fine as that or finer; each vertex of the mesh
        that is drawn is the bilinear mean of the four vertices of the cell that it is in -- on a side of a quadrilateral only the two on that side, so that two quadrilaterals that meet there agree on it and the mesh is one piece -- and a vertex that is one of the
        fine mesh is that vertex (the points of the cage are).  Returns (vertices, triangles) '''
    faces, par = _faces_par(topo, level)
    q = pieces // 2
    g = 2 ** (level - 1)
    cells = {}
    for fi, f in enumerate(faces):
        quad, pts = par[fi]
        cells[(quad, int(round(min(p[0] for p in pts) * g)), int(round(min(p[1] for p in pts) * g)))] = (f, pts)
    made, ids_of, tris = [], {}, []
    for quad in range(len(topo['f1'])):
        grid = [[None] * (q + 1) for _ in range(q + 1)]
        for i in range(q + 1):
            for j in range(q + 1):
                s, t = i / float(q), j / float(q)
                cx, cy = min(int(s * g + 1e-9), g - 1), min(int(t * g + 1e-9), g - 1)
                f, pts = cells[(quad, cx, cy)]
                u, v = s * g - cx, t * g - cy
                w = {}
                for vid, (ps, pt) in zip(f, pts):
                    wu = u if abs(ps * g - (cx + 1)) < 1e-9 else 1.0 - u
                    wv = v if abs(pt * g - (cy + 1)) < 1e-9 else 1.0 - v
                    if wu * wv > 1e-12:
                        w[vid] = w.get(vid, 0.0) + wu * wv
                key = tuple(sorted((vid, round(x, 9)) for vid, x in w.items()))
                if key not in ids_of:
                    ids_of[key] = len(made)
                    made.append(w)
                grid[i][j] = ids_of[key]
        for i in range(q):
            for j in range(q):
                a, b, c, d = grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]
                tris.append((a, b, c))
                tris.append((a, c, d))
    out = []
    for w in made:
        acc = {}
        for vid, x in w.items():
            for c, cw in vl[vid]:
                acc[c] = acc.get(c, 0.0) + x * cw
        out.append([(c, x) for c, x in acc.items() if abs(x) > 1e-12])
    return out, tris


def _drawn_pieces(n, top):
    ''' How many pieces a cage edge is drawn in -- any even number: the argument of the mesher (see _sample_pieces).  The resolution scales it, as it does what every other body is drawn in: half the resolution half the pieces, twice the resolution twice --
        from what the body is drawn in at the resolution 10, held to 32 pieces so that a script run and a drag stay as quick as they were; at most 2 ** top, what a drag can bear '''
    res = None
    try:
        from fieldes import view
        res = view._scene_resolution()
    except Exception:
        pass
    scale = (float(res) / 10.0) if res else 1.0
    base = min(float(n) / scale, 32.0)
    pieces = max(4, min(int(round(base * scale)), 2 ** top))
    return pieces + (pieces & 1)


def _drawn_top(cage):
    ''' The finest mesh a body is drawn in, as a level (2 ** level pieces to an edge): the one whose triangles stay within what a drag can bear -- about 60,000: a quadrilateral of the cage is 2 n^2 triangles at n pieces to an edge, a face of three 1.5 n^2 -- between 3 and 6 '''
    triangles_per_n2 = sum(2.0 if len(f) == 4 else 1.5 for f in cage)
    most = math.sqrt(60000.0 / triangles_per_n2)
    return max(3, min(6, int(math.floor(math.log(most) / math.log(2.0)))))


def _subdiv_levels_for(n):
    return max(1, min(4, int(round(math.log(max(n, 2)) / math.log(2.0)))))


def _morph(name, points, smooth, radius, faces):
    _T_START[0] = time.perf_counter()
    if not _NUMERIC[0] and _TRACE[0] is None and not _NO_TAPE[0]:
        made = _morph_native(name, points, smooth, radius, faces)
        if made is not None:
            return made
    items = C.expand_points(points)
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
        cage = _faces_of_the_places(name, [tuple(float(v) for v in c.pos) for c in ctrls])
    else:
        cage = tuple(tuple(int(v) for v in f) for f in faces)
        # (a face that names a point that is not there -- the point was deleted -- is not an error: the faces are healed, see _healed; the user, 2026-11-02, on deleting a point of a morph shape)
        cage = _healed(name, cage, len(items), [tuple(float(v) for v in c.pos) for c in ctrls])
        for f in cage:
            if len(f) not in (3, 4) or any(not 0 <= v < len(items) for v in f) or len(set(f)) != len(f):
                raise ValueError('%s: a face is the numbers of three or four different points (0 to %d): %r' % (name, len(items) - 1, f))
        cage = _faces_between_closest_points(cage, [tuple(float(v) for v in c.pos) for c in ctrls], name)
    # (the normal at a point is made of the faces as they were given -- a square is one face of its own area at each of its four corners --, not of the triangles that a square is cut in: along one
    # diagonal the corners it touches twice weigh it twice, and eight identical sphere points of a cube were not the same at the eight corners, the user, 2026-11-02, H47)
    normal_cage = cage
    if name == 'morph_polyhedron':
        cage = _triangulated(cage)
    if _TRACE[0] is not None:
        P = [tuple(_input(e) for e in c.expr) for c in ctrls]
    else:
        P = [tuple(_wrap(v) for v in c.pos) for c in ctrls] if _NUMERIC[0] else [c.expr for c in ctrls]
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
        if _NUMERIC[0]:
            verts, tris = _flat_tessellation(cage)
            _INFO['poly'] = True
            return P, verts, tris
        verts, tris = _flat_tessellation(cage)
        field = _native_field(P, verts, tris)
        return _finish(name, items, smooth, radius, ctrls, cage, field, P, verts, tris)
    N = _normals(P, normal_cage)
    # THE NORMAL AT A POINT PULLED OUT INTO A SPIKE (the user, 2026-11-09, "the beak like spike when dragging a point down and out"; 2026-11-10: a line along the horn).  The normal is the sum of the normals of the faces that meet at the
    # point -- until the neighbours of the point are all on one side of it: at a point pulled far out the sum is tilted about 40 degrees off the axis the spike runs along, the neighbours all fall in one sector of the tangent plane, and the
    # faces that meet there fold over each other (measured on the drawn surface of a cube with one corner pulled out: the angle between neighbouring triangles along the spike, 2 to 6 mm from the tip, went 92, 71, 141, 149 degrees as
    # the surface was refined: a fold, not a curve, and the line seen along the horn).  The normal then is the axis the neighbours leave along, turned round, blended with the face normal by how one-sided the neighbours are -- the length
    # of the mean of the unit vectors to them: 0 for a ring of them in a plane, 1 for all of them in one direction --, so that a point in a flat or round place is as it was
    for i in range(len(P)):
        if len(neighbours[i]) < 2:
            continue
        total = None
        for j in neighbours[i]:
            e = _unit(_sub(P[j], P[i]))
            total = e if total is None else _add(total, e)
        k = float(len(neighbours[i]))
        mean = (total[0] / k, total[1] / k, total[2] / k)
        length = _norm(mean)
        m = _wrap(length).min(1.0)
        blend = tuple(N[i][a] * (1.0 - m) - (mean[a] / length) * m for a in range(3))
        N[i] = _unit(blend)
    # What each point is: where the surface is at it (V), how far the curves that leave it go the way a spline's do -- along the tangent plane, not straight at the next point -- (S: 0 for a
    # corner, 1 for a spline point) and how strongly they leave it (K: the influence of the point, 1 being the spline of a profile)
    V, S, K = [], [], []
    sphere =[bool(c.rounded) for c in ctrls]
    for i, c in enumerate(ctrls):
        if c.rounded:
            # a sphere point: a rounded corner (the user, 2026-11-02: "I want it to straight up make a strong radius, just like it does in a profile", after a build whose cut was a fraction of the radius and
            # went with the square of it).  The radius used is the radius itself -- the scale and the influence of the point are in it -- until it is a good part of the shortest edge at the point; the
            # point is cut in along its normal by the depth a sphere of that radius cuts the corner of a box, and the edges that leave it leave it in the plane across the normal for the length of the radius
            # (see `edge`), so that the corner is rounded where it is and the rest of the edge is the edge
            shortest = None
            for j in neighbours[i]:
                d = ((P[i][0] - P[j][0]).square() + (P[i][1] - P[j][1]).square() + (P[i][2] - P[j][2]).square()).sqrt()
                shortest = d if shortest is None else shortest.min(d)
            r = _val(c.radius_expr)
            used = r if shortest is None else _wrap(r).min(shortest * _SPHERE_LIMIT)
            # (the ball that rounds the corner touches every face that meets there: its centre is r u behind the corner, u being the vector for which the outward unit normal m of each face has m . u = 1 -- solved
            # over the faces as they are given: for the corner of a box that is u = (1, 1, 1) in the axes of its faces, whichever way a square is cut in triangles.  The normal of the point is the way of u, and the
            # cut is r (|u| - 1) deep: r (sqrt(3) - 1) at the corner of a box, as it was, and more for a corner that has been pulled out into a wedge, which the constant of the box left a beak -- the user,
            # 2026-11-04, of a morph_body with one point pulled out: "it is actually broken"; measured: the apex stopped 0.73 r short of the corner whatever the wedge)
            faces_here = [_unit(_face_normal(P, face)) for face in cage if i in face]
            if faces_here:
                eps = _SPHERE_FACES_RIDGE
                s00, s01, s02, s11, s12, s22 = _wrap(eps), _wrap(0.0), _wrap(0.0), _wrap(eps), _wrap(0.0), _wrap(eps)
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
            # (A SPHERE POINT IS ROUND ABOUT ITS AXIS, NOT AN EXACT BALL -- the user, 2026-11-10: "it should be round and have infinite symmetry axis at the point, so sphere like. But the curvature does not need to be that of a perfect sphere"; an exact ball set on the surface
            # stood out of it as a pimple with a dimple round its foot, at every radius, since a cap tighter than the surface it sits on cannot pass back into it without a concave neck.  The surface is the spline through the tip, rounder or tighter by its influence:
            # HOW STRONGLY THE SURFACE LEAVES A SPHERE POINT: the corner of a box rounded by a ball much smaller than the edges that meet at it is a tight corner, and the faces round it stay flat only if the surface leaves the point less strongly -- sqrt(2 r / edge), 1 where the
            # radius is half the edge, as for the eight sphere points of a cube, and never below _SPHERE_K_MIN -- measured: on a box of 40 x 30 x 20 with a sphere point of radius 5 the middles of the faces dipped 1.1, 2.0 and 3.0 toward it without it, within 0.6 with it.  Only for a
            # blunt corner: a corner pulled out into a spike has the surface leave it as it does, or the spike is a thin cone: the strength comes in as the cut-in depth goes from 0.8 to 1.6 radii)
            if shortest is None:
                K.append(1.0)
            else:
                blunt = ((1.6 - depth_factor) / 0.8).max(0.0).min(1.0) if faces_here else 1.0
                K.append(1.0 - (1.0 - ((used / shortest) * 2.0).min(1.0).sqrt().max(_SPHERE_K_MIN)) * blunt)
        elif c.smooth(smooth):
            V.append(P[i])
            S.append(1.0)
            K.append(_val(c.influence_expr) if c.influence_expr is not None else 1.0)
        else:
            V.append(P[i])
            S.append(0.0)
            K.append(_val(c.influence_expr) if c.influence_expr is not None else 1.0)       # (a corner's influence: how far its sharp edges run, see subdiv_tessellate)
    def subdiv_tessellate(level, pieces=None):
        # (THE SURFACE AS THE LIMIT OF A CATMULL-CLARK SUBDIVISION THROUGH THE POINTS: the control points are fixed linear combinations of the points, the surface of them: (controls, vertices, triangles) as ribbon_tessellate gives them; None where it does not apply)
        sharp = frozenset(i for i in range(len(V)) if not S[i])
        key = (tuple(tuple(f) for f in cage), sharp)
        if key not in _SUBDIV_CACHE:
            _SUBDIV_CACHE[key] = _subdiv_topology(cage, sharp)
        topo = _SUBDIV_CACHE[key]
        if topo is None:
            raise ValueError('%s: the faces of this body do not make a closed surface (every edge of a face must belong to exactly two faces, every point to at least two edges): %r' % (name, cage))
        meshed = _subdiv_level(topo, level)
        if meshed is None:
            raise ValueError('%s: the faces of this body do not make a closed surface' % name)
        ring, ringf, edge_net, face_net = topo['ring'], topo['ringf'], topo['edge_net'], topo['face_net']
        orig = topo['orig']
        nv_ = len(orig)
        Vx = [V[o] for o in orig]
        Sx = [S[o] for o in orig]
        Kx = [K[o] for o in orig]
        ctrlsx = [ctrls[o] for o in orig]
        spherex = [sphere[o] for o in orig]
        # 1. THE FIRST MESH OF THE SUBDIVISION: the control points of the points are fixed linear combinations of the points (the minimal solution of the limit being the point), the first mesh is the first step of the subdivision of them
        Cs = [tuple(sum(w * Vx[j][a] for j, w in topo['B'][i]) for a in range(3)) for i in range(nv_)]
        net = [tuple(sum(w * Cs[j][a] for j, w in topo['v1'][k].items()) for a in range(3)) for k in range(len(topo['v1']))]

        def turned(t, cs, ax):
            # (the vector t turned by the rotation that takes a unit vector to another: cs the cosine of the angle, ax the cross product of the two, whose length is the sine; Rodrigues without a division by the sine)
            cr = _cross(ax, t)
            k_ = _dot(ax, t) / (cs + 1.0 + 1e-9)
            return tuple(t[a] * cs + cr[a] + ax[a] * k_ for a in range(3))
        # 2. THE INFLUENCE AND THE ROTATION OF A SPLINE POINT, ADDED TO IT (the user, 2026-11-10: "the point control with influence and rotation"): the limit surface has a tangent plane at the point, which is the first harmonic of the edge points and the face points of its
        # ring (see _first_harmonic); the rotation turns that plane (the normal is turned the way the gizmo turns a shape, the plane by the rotation that takes the normal to the turned one) and the influence multiplies its tangent vectors, how strongly the surface
        # leaves the point.  The change goes into the edge points of the ring as a first harmonic whose response -- the limit tangent, the face points staying as they are -- is exactly the change asked for.  A point that says neither has nothing added: its first mesh is the one of the control points
        shifts = {}
        for i in range(nv_):
            c_ = ctrlsx[i]
            if not Sx[i]:
                # THE INFLUENCE OF A CORNER (the user, 2026-11-10: "the last thing that you have to add is the influence for the corner points"; 2026-11-09: how far its edges are crisp): the sharp run of each edge that leaves a corner for a point that is not a corner ends at the point of
                # the first mesh on that edge, which lies half way along it; the influence moves that point along the edge, K times as far from the corner -- a longer sharp run for more, a shorter one for less, 1 as it was.  The point at the other end of the edge has it
                # among its ring, and is made exact after it, so it stays on the surface.  An edge between two corners is straight and sharp all along and has nothing to move
                if isinstance(Kx[i], float) and Kx[i] == 1.0:
                    continue
                for j in ring[i]:
                    if not Sx[j]:
                        continue
                    key = edge_net[(min(i, j), max(i, j))]
                    d = _scale(Kx[i] - 1.0, _sub(net[key], net[i]))
                    shifts[key] = d if key not in shifts else _add(shifts[key], d)
                continue
            rot =c_.rot_expr is not None and c_.smooth(smooth) and not spherex[i]
            infl = not (isinstance(Kx[i], float) and Kx[i] == 1.0)
            if not (rot or infl):
                continue
            r = ring[i]
            n = len(r)
            if n < 3:
                continue                                        # (a point of two edges has no tangent plane to turn)
            w, lb = _first_harmonic(n)
            ar = ai = br = bi = None
            for k, j in enumerate(r):
                ang = 2.0 * math.pi * k / n
                e = net[edge_net[(min(i, j), max(i, j))]]
                f = net[face_net[ringf[i][k]]]
                cr_, sr_ = 2.0 / n * math.cos(ang), 2.0 / n * math.sin(ang)
                ta, tb, tc_, td = _scale(cr_, e), _scale(-sr_, e), _scale(cr_, f), _scale(-sr_, f)
                ar = ta if ar is None else _add(ar, ta)
                ai = tb if ai is None else _add(ai, tb)
                br = tc_ if br is None else _add(br, tc_)
                bi = td if bi is None else _add(bi, td)
            lr, li = lb.real, lb.imag
            tau_r = _add(ar, _sub(_scale(lr, br), _scale(li, bi)))
            tau_i = _add(ai, _add(_scale(lr, bi), _scale(li, br)))
            new_r, new_i = tau_r, tau_i
            if rot:
                nrm = _unit(_cross(tau_i, tau_r))
                nr = _turn(nrm, _val(c_.rot_expr))
                cs, ax = _dot(nrm, nr), _cross(nrm, nr)
                new_r, new_i = turned(tau_r, cs, ax), turned(tau_i, cs, ax)
            if infl:
                new_r, new_i = _scale(Kx[i], new_r), _scale(Kx[i], new_i)
            dr, di = _sub(new_r, tau_r), _sub(new_i, tau_i)
            for k, j in enumerate(r):
                ang = 2.0 * math.pi * k / n
                d = _sub(_scale(math.cos(ang), dr), _scale(math.sin(ang), di))
                key = edge_net[(min(i, j), max(i, j))]
                shifts[key] = d if key not in shifts else _add(shifts[key], d)
        for key, d in shifts.items():
            net[key] = _add(net[key], d)
        # 3. THE POINT OF THE NET AT EACH POINT, LAST: so that the limit surface is the point itself, whatever was added round it -- (n^2 v' + 4 sum of its edge points + sum of its face points) / (n (n + 5)) = the point; a corner stays.  Where the global solution
        # already had the point on the surface this is what it had
        for i in range(nv_):
            if i in topo['sharp']:
                net[i] = Vx[i]
                continue
            n = len(ring[i])
            se = sf = None
            for j in ring[i]:
                e = net[edge_net[(min(i, j), max(i, j))]]
                se = e if se is None else _add(se, e)
            for fi in ringf[i]:
                sf = net[face_net[fi]] if sf is None else _add(sf, net[face_net[fi]])
            net[i] = tuple((Vx[i][a] * (n * (n + 5.0)) - se[a] * 4.0 - sf[a]) / float(n * n) for a in range(3))
        if pieces is None or pieces // 2 == 2 ** (level - 1):
            return net, meshed[0], meshed[1]
        vl2, tr2 = _sample_pieces(topo, level, meshed[0], pieces)
        return net, vl2, tr2

    _t0 = time.perf_counter()
    _INFO['ctrls'], _INFO['cage'], _INFO['items'] = ctrls, cage, items
    if _NUMERIC[0] and _FIELD_RUN[0]:
        return subdiv_tessellate(_subdiv_levels_for(_field_segments(cage)))
    n_drawn = _drawn_segments(cage, any(sphere), _cage_turn([tuple(float(v) for v in c.pos) for c in ctrls], cage))
    top_level = _drawn_top(cage)
    pieces = _drawn_pieces(n_drawn, top_level)
    drawn_controls, drawn_vertices, drawn_tris = subdiv_tessellate(min(top_level, 1 + int(math.ceil(math.log(max(pieces // 2, 1)) / math.log(2.0) - 1e-9))), pieces)
    _t1 = time.perf_counter()
    if _NUMERIC[0]:
        return drawn_controls, drawn_vertices, drawn_tris
    field_controls, field_vertices, field_tris = subdiv_tessellate(_subdiv_levels_for(_field_segments(cage)))
    _t2 = time.perf_counter()
    field = _native_field(field_controls, field_vertices, field_tris)
    if os.environ.get('FIELDES_TIMING'):
        sys.__stderr__.write('[morph-py] %s: the points, normals, edges before the tessellations %.0f ms; drawn tessellation (and the nets) %.0f ms; field tessellation %.0f ms; the native field %.0f ms (%d controls, %d vertices drawn)\n'
                             % (name, 1000.0 * (_t0 - _T_START[0]), 1000.0 * (_t1 - _t0), 1000.0 * (_t2 - _t1), 1000.0 * (time.perf_counter() - _t2), len(drawn_controls), len(drawn_vertices)))
        sys.__stderr__.flush()
    return _finish(name, items, smooth, radius, ctrls, cage, field, drawn_controls, drawn_vertices, drawn_tris)


def morph_numbers(points, smooth=True, radius=0.0, faces=None, name='morph_body'):
    ''' The drawn control points, the vertices and the triangles of a body of points, worked out on plain numbers (H164 step 1b): (controls, vertices, triangles) with every control point three floats --
    what the graph of expressions gives when it is evaluated at the numbers of the points.  The reference the native port is compared against '''
    _NUMERIC[0] = True
    try:
        controls, vertices, tris = _morph(name, list(points), smooth, radius, faces)
    finally:
        _NUMERIC[0] = False
    return [tuple(float(c) for c in p) for p in controls], vertices, tris


class TapeOut(object):
    """ The drawn control points of a body as the tape of operations that makes them from the numbers of its points (see morph_num.Tape): `inputs` are the expressions the tape's leaves read, `outs` the
        tape numbers of the three coordinates of each control point """

    def __init__(self, tape, inputs, outs):
        self.tape, self.inputs, self.outs = tape, inputs, outs

    def __len__(self):
        return len(self.outs)

    def expressions(self):
        """ The control points as expressions again (a graph is made only where something still asks for one: the image of a body in a symmetry) """
        from fieldes.stdlib import morph_num as MN
        val = [None] * len(self.tape.ops)
        for k, (op, a, b) in enumerate(self.tape.ops):
            if op == MN.OP_INPUT:
                val[k] = Shape.wrap(self.inputs[a])
            elif op == MN.OP_CONST:
                val[k] = Shape.wrap(self.tape.constants[a])
            elif op == MN.OP_ADD:
                val[k] = val[a] + val[b]
            elif op == MN.OP_SUB:
                val[k] = val[a] - val[b]
            elif op == MN.OP_MUL:
                val[k] = val[a] * val[b]
            elif op == MN.OP_DIV:
                val[k] = val[a] / val[b]
            elif op == MN.OP_MIN:
                val[k] = val[a].min(val[b])
            elif op == MN.OP_MAX:
                val[k] = val[a].max(val[b])
            elif op == MN.OP_SQRT:
                val[k] = val[a].sqrt()
            elif op == MN.OP_SQUARE:
                val[k] = val[a].square()
            elif op == MN.OP_NEG:
                val[k] = -val[a]
            elif op == MN.OP_SIN:
                val[k] = val[a].sin()
            elif op == MN.OP_COS:
                val[k] = val[a].cos()
        return [tuple(val[i] for i in o) for o in self.outs]


def _morph_native(name, points, smooth, radius, faces):
    """ H164 step 2 (the user, 2026-11-09: "ON SMALLER FILES IT SHOULD LEGIT TAKE NO TIME AT ALL"): the body made WITHOUT a graph of expression nodes.  The drawn patches are TRACED into a tape of plain operations
        (the viewport runs it at every step of a drag, see Shape::morphDirect) and the field's patches are worked out on numbers.  None when the body is a flat polyhedron, which the old way makes """
    from fieldes.stdlib import morph_num
    _INFO.clear()
    tape = morph_num.Tape()
    morph_num._TAPE[0] = tape
    _NUMERIC[0] = True
    _TRACE[0] = {}
    try:
        controls, vertices, tris = _morph(name, list(points), smooth, radius, faces)
        table = _TRACE[0]
    finally:
        _NUMERIC[0] = False
        _TRACE[0] = None
    if _INFO.get('poly') or 'ctrls' not in _INFO:
        return None
    ctrls, cage, items = _INFO['ctrls'], _INFO['cage'], _INFO['items']
    inputs = [e for e, _ in sorted(table.values(), key=lambda t: t[1])]
    outs = [tuple(morph_num.wrap_trace(c).i for c in p) for p in controls]
    _NUMERIC[0] = True
    _FIELD_RUN[0] = True
    try:
        field_controls, field_vertices, field_tris = _morph(name, list(points), smooth, radius, cage)
    finally:
        _NUMERIC[0] = False
        _FIELD_RUN[0] = False
    field = _native_field_numbers([tuple(float(c) for c in p) for p in field_controls], field_vertices, field_tris)
    return _finish(name, items, smooth, radius, ctrls, cage, field, TapeOut(tape, inputs, outs), vertices, tris)


def morph_traced(points, smooth=True, radius=0.0, faces=None, name='morph_body'):
    ''' H164 step 2: the same run as morph_numbers, but every operation is RECORDED into a tape (morph_num.Tape) whose leaves are the expressions of the points: (tape, inputs, controls, vertices, tris) with
        `inputs` the expressions (one per input number) and `controls` the tape numbers of the three coordinates of each control point '''
    from fieldes.stdlib import morph_num
    tape = morph_num.Tape()
    morph_num._TAPE[0] = tape
    _NUMERIC[0] = True
    _TRACE[0] = {}
    try:
        controls, vertices, tris = _morph(name, list(points), smooth, radius, faces)
        table = _TRACE[0]
    finally:
        _NUMERIC[0] = False
        _TRACE[0] = None
    inputs = [e for e, _ in sorted(table.values(), key=lambda t: t[1])]
    return tape, inputs, [tuple(morph_num.wrap_trace(c).i for c in p) for p in controls], vertices, tris


def _faces_of_the_script():
    ''' The faces of the body that the call now running makes, from the script: the list `body_faces` above the line `body = morph_polyhedron(...)` (the user, 2026-11-02, "THIS SHOULD BE AN INTERNAL FUCKING
        VARIABLE, BUT I DONT SEE ANY ADVANTAGE TO EXPOSING THIS AS A CALL": the faces are no argument of the call).  The name the result is stored under is read from the instruction after the call in the
        script's own code; None when the call is not stored under a name, or there is no such list -- the faces are then made of the points '''
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


def morph_polyhedron(points, smooth=False, radius=0, *, position=None, _faces=None):
    ''' A body made of points -- the 3D equivalent of `profile`: four points or more (not all in one plane), joined by flat faces, with a sharp corner at each point.  The faces are those of the convex
        hull of the points (the eight points of a cube, in the order x changes fastest, then y, then z, have its six squares) unless the script has a list of faces named after the body --
        `body_faces = [(0, 2, 3, 1), ...]` above `body = morph_polyhedron(...)`, three or four point numbers each, counterclockwise seen from outside --, which the program writes when the faces are not
        the hull's (a point pressed out of the surface, a dent); it is no argument of the call.  Press its surface and drag: a point is put there and follows the hand.  `smooth` is what a point is that does
        not say (point(..., smooth=True) is a spline point: the surface goes through it and is smooth there; smooth=False, a corner: the default); `radius` is what a corner is that does not say, 0 (a sharp
        corner) by default; point(..., radius=r) is a sphere point: a corner rounded by about a sphere of radius r.  See the module.  With the points at the corners of a box it is that box; made of corners
        only it is exactly the polyhedron of its faces.  `position` (x, y, z) is where the body is: the middle of the box of its points.  The body is made of its points, so the number adds no move of
        its own: FielDes keeps the two in step -- a number typed in the code moves every point by the difference, a point or the whole body dragged writes the number again '''
    return _morph('morph_polyhedron', points, smooth, radius, _faces if _faces is not None else _faces_of_the_script())


def morph_body(points, smooth=True, radius=0, *, position=None, _faces=None):
    ''' The same body as `morph_polyhedron` (four points or more, the faces made of the points unless the script has the list `body_faces` above the call), with a spline point at each point (a rounded body whose surface
        goes through its points, as the line of a spline goes through its points); `smooth=False` makes every point a corner that does not say otherwise, point(..., smooth=False) makes that one a corner, and
        point(..., radius=r) a sphere point.  Press its surface and drag to put a point there.  `position` (x, y, z) is where the body is: the middle of the box of its points; a number typed in the code
        moves every point by the difference, and a point or the whole body dragged writes the number again.  See the module '''
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
