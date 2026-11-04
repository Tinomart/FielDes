'''
Checks of morph_polyhedron and morph_body (fieldes.stdlib.morph): a body made of points that a press on its surface puts a point on.  Library checks, no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import json
import math
import re
import sys
import time
import traceback

from fieldes import *
from fieldes import menu_catalog as mc
from fieldes import profile_edit, runner
from fieldes.stdlib import curves as C

bad = []
start = time.time()


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def inside(s, p):
    return evaluate(s, [p])[0] < 0


def corners(lo, hi):
    return [(lo[0] if not i & 1 else hi[0], lo[1] if not i & 2 else hi[1], lo[2] if not i & 4 else hi[2]) for i in range(8)]


selected = sys.argv[1:]            # (the checks to run by their names below, e.g. `symmetry link`; none given: all of them -- a change to one is checked by that one)


def run(name, fn):
    if selected and name not in selected:
        return
    try:
        fn()
    except Exception:
        traceback.print_exc()
        bad.append(name + ' raised')


def box_like():
    pts = [point(position=c) for c in corners((0, 0, 0), (40, 30, 20))]
    cube = morph_polyhedron(pts)
    check('morph_polyhedron of the corners of a box: inside at the middle, outside beyond each face',
          inside(cube, (20, 15, 10)) and not inside(cube, (-0.5, 15, 10)) and not inside(cube, (40.5, 15, 10)) and not inside(cube, (20, -0.5, 10))
          and not inside(cube, (20, 30.5, 10)) and not inside(cube, (20, 15, -0.5)) and not inside(cube, (20, 15, 20.5)))
    ins = [(0.3, 0.3, 0.3), (39.7, 0.3, 0.3), (0.3, 29.7, 0.3), (39.7, 29.7, 0.3), (0.3, 0.3, 19.7), (39.7, 0.3, 19.7), (0.3, 29.7, 19.7), (39.7, 29.7, 19.7)]
    check('morph_polyhedron: a point 0.3 inside every corner is inside', all(inside(cube, p) for p in ins))
    check('morph_polyhedron: the field is a distance (0 on a face, 10 from it in the middle)', abs(evaluate(cube, [(40, 15, 10)])[0]) < 0.01 and abs(evaluate(cube, [(20, 15, 10)])[0] + 10.0) < 0.05,
          [evaluate(cube, [(40, 15, 10)])[0], evaluate(cube, [(20, 15, 10)])[0]])
    check('morph_polyhedron: it says it makes a solid before it has run, and is editable by pressing its surface', getattr(morph_polyhedron, '_makes', None) == 'solid' and getattr(cube, '_editable', False)
          and len(getattr(cube, '_morph_faces', ())) == 12)      # (a polyhedron is flat triangles only: the six squares of a box are twelve triangles)


def skewed():
    pts = corners((0, 0, 0), (40, 30, 20))
    pts[7] = (50, 40, 30)               # the far corner pulled out
    cube = morph_polyhedron([point(position=c) for c in pts])
    check('morph_polyhedron: a corner that is moved takes the faces with it', inside(cube, (45, 36, 26)) and not inside(cube, (45, 36, 31)) and inside(cube, (20, 15, 10)))


def body():
    pts = [point(position=c) for c in corners((0, 0, 0), (40, 30, 20))]
    cube, blob = morph_polyhedron(pts), morph_body(pts)
    check('morph_body: inside at the middle', inside(blob, (20, 15, 10)))
    through = [abs(evaluate(blob, [c])[0]) for c in corners((0, 0, 0), (40, 30, 20))]
    check('morph_body: the surface goes THROUGH its spline points (the field is 0 at each of the eight corners of its box)', inside(cube, (1, 1, 1)) and max(through) < 0.3, max(through))
    # (a surface that is smooth in all directions through the eight corners of a box bulges between them: the middle of the face x = 40 is inside the body, by 3.8 now that the faces meet in one tangent
    # plane along their edges, by 2.9 when they only did at the points; a flat face would be 0, a sphere through the corners 14.7)
    check('morph_body: a spline surface bulges out between its points (the middle of a face is inside the body by more than 1 and less than 6)', -6.0 < evaluate(blob, [(40, 15, 10)])[0] < -1.0, evaluate(blob, [(40, 15, 10)])[0])
    mostly_sharp = morph_body([point(position=c, smooth=(i == 7)) for i, c in enumerate(corners((0, 0, 0), (40, 30, 20)))])
    check('morph_body: the points that say smooth=False are corners (the corner at the origin is sharp) and the one that says smooth=True is a spline point the surface goes through',
          inside(mostly_sharp, (0.3, 0.3, 0.3)) and abs(evaluate(mostly_sharp, [(40, 30, 20)])[0]) < 0.3, [inside(mostly_sharp, (0.3, 0.3, 0.3)), evaluate(mostly_sharp, [(40, 30, 20)])[0]])
    one = morph_polyhedron([point(position=c, smooth=(i == 7)) for i, c in enumerate(corners((0, 0, 0), (40, 30, 20)))])
    check('morph_polyhedron with one spline point: the surface goes through it, the other corners are sharp', abs(evaluate(one, [(40, 30, 20)])[0]) < 0.3 and inside(one, (0.3, 0.3, 0.3)) and inside(one, (39.7, 0.3, 0.3)))
    sphere = morph_polyhedron([point(position=c, radius=(6 if i == 7 else 0)) for i, c in enumerate(corners((0, 0, 0), (40, 30, 20)))])
    check('morph_polyhedron: a sphere point is not a spline point (the surface does not reach it, and does reach the spline point)', not inside(sphere, (39.7, 29.7, 19.7)) and abs(evaluate(one, [(40, 30, 20)])[0]) < 0.3)


def symmetric_and_drawn():
    # the default body of a box is as round on one side as on the other (the user, 2026-10-29: "the morph body is not symmetrical like it should be"): the field at a place and at its mirror image
    # in each of the three middle planes is the same
    import random
    pts = [point(position=c) for c in corners((0, 0, 0), (40, 30, 20))]
    blob = morph_body(pts)
    rng = random.Random(3)
    worst = 0.0
    for _ in range(40):
        x, y, z = rng.uniform(-5, 45), rng.uniform(-5, 35), rng.uniform(-5, 25)
        a = evaluate(blob, [(x, y, z), (40 - x, y, z), (x, 30 - y, z), (x, y, 20 - z)])
        worst = max(worst, abs(a[0] - a[1]), abs(a[0] - a[2]), abs(a[0] - a[3]))
    check('morph_body of a box: the field is the same at a place and at its mirror image in each middle plane (the largest difference %.2g)' % worst, worst < 1e-3)
    # what is drawn: the control points of the patches as expressions of the points' numbers, every vertex as a sum of control points times weights, and the triangles -- the viewport shades every
    # triangle flat, so the six faces of the cube are drawn in n pieces to an edge (n from _drawn_segments): 6 (n-1)^2 + 12 (n-1) + 8 vertices, 12 n^2 triangles
    from fieldes.stdlib import morph as morph_module
    n = morph_module._drawn_segments(blob._morph_faces)
    vertices = len(blob._morph_vstart) - 1
    check('morph_body: it says the surface to draw from the numbers of its points (%d pieces to an edge: %d vertices, %d triangles)' % (n, 6 * (n - 1) ** 2 + 12 * (n - 1) + 8, 12 * n * n),
          vertices == 6 * (n - 1) ** 2 + 12 * (n - 1) + 8 and len(blob._morph_tris) == 12 * n * n, (vertices, len(blob._morph_tris)))
    check('morph_body: the control points are few (a cube has 8 corners, 24 points along its 12 edges and 8 inner points to each of 6 faces -- every inner point in two versions, one per edge at its corner, so that two faces share a tangent plane along their edge: 80) -- the drawing does not evaluate once per vertex',
          len(blob._morph_controls) == 80, len(blob._morph_controls))
    check('morph_body: every vertex is a weighted sum whose weights add up to 1 (the surface does not move when the points do)',
          all(abs(sum(blob._morph_vweight[blob._morph_vstart[v]:blob._morph_vstart[v + 1]]) - 1.0) < 1e-9 for v in range(vertices)))
    # the vertices that are drawn lie on the surface the field has, to the faceting: the middle of the far face is on both
    cube = morph_polyhedron(pts)

    def drawn(body, v):
        total = [0.0, 0.0, 0.0]
        for k in range(body._morph_vstart[v], body._morph_vstart[v + 1]):
            control = body._morph_controls[body._morph_vctrl[k]]
            for a in range(3):
                total[a] += body._morph_vweight[k] * float(C.value_of(control[a]))
        return total

    top = [drawn(cube, v) for v in range(len(cube._morph_vstart) - 1)]
    check('morph_polyhedron: what is drawn is the box (every vertex is inside or on its box to a thousandth)', all(-1e-3 <= v[0] <= 40.001 and -1e-3 <= v[1] <= 30.001 and -1e-3 <= v[2] <= 20.001 for v in top))
    # (the drawn surface of the rounded body lies on the surface of the field it is made with, to the faceting of the field: its vertices are near the field's zero)
    seen = [drawn(blob, v) for v in range(0, vertices, max(1, vertices // 60))]
    worst = max(abs(evaluate(blob, [tuple(p)])[0]) for p in seen)
    check('morph_body: the drawn vertices lie on the surface of its field (the largest distance %.3g of 40; at 2 pieces to an edge it was 1.9)' % worst, worst < 0.8, worst)


def spheres():
    pts = [point(position=c, radius=5 if i == 0 else None) for i, c in enumerate(corners((0, 0, 0), (40, 30, 20)))]
    cube = morph_polyhedron(pts)
    check('morph_polyhedron with a sphere point (radius 5): the corner is rounded (a point 0.5 inside the corner is outside, one 4 inside is inside)', not inside(cube, (0.5, 0.5, 0.5)) and inside(cube, (4, 4, 4)))
    check('morph_polyhedron with a sphere point: ... the other corners are sharp and the far faces are in place', inside(cube, (39.7, 29.7, 19.7)) and inside(cube, (39.7, 0.3, 0.3)) and abs(evaluate(cube, [(40, 15, 10)])[0]) < 0.05)
    bigger = morph_polyhedron([point(position=c, radius=10 if i == 0 else None) for i, c in enumerate(corners((0, 0, 0), (40, 30, 20)))])
    check('a bigger radius rounds more (the field at 2 mm from each face is higher for a radius of 10 than for 5)', evaluate(bigger, [(2, 2, 2)])[0] > evaluate(cube, [(2, 2, 2)])[0],
          [evaluate(bigger, [(2, 2, 2)])[0], evaluate(cube, [(2, 2, 2)])[0]])


def sphere_strength():
    # The sphere point makes a STRONG radius, as a round corner of a profile does (the user, 2026-11-02: "I want it to straight up make a strong radius, just like it does in a profile. Not to be a subtle tiny
    # rounding, that doesnt even seem to get affected with the influence"): measured along the diagonal of a box from its corner, where the surface starts; a sphere of radius r cuts a box corner r * 0.73 deep
    n = 1.0 / math.sqrt(3.0)
    ts = [0.02 * k for k in range(1500)]

    def made(r, influence=None):
        return morph_polyhedron([point(position=c, radius=(r if i == 0 else None), influence=(influence if i == 0 else None)) for i, c in enumerate(corners((0, 0, 0), (40, 30, 20)))])

    def start(body_):
        values = evaluate(body_, [(t * n, t * n, t * n) for t in ts])
        return next((ts[k] for k in range(1, len(ts)) if values[k] <= 0 < values[k - 1]), None)
    s3, s6, s10 = start(made(3.0)), start(made(6.0)), start(made(10.0))
    ideal = 3.0 * (math.sqrt(3.0) - 1.0)
    check('a sphere point of radius 3 cuts a corner of a box 40 x 30 x 20 about as deep as a sphere does (%.2f along the diagonal; a sphere of the radius used: %.2f; it was 0.47)' % (s3, ideal),
          s3 is not None and 0.8 * ideal < s3 < 1.5 * ideal, [s3, ideal])
    check('... and the radius shows: 3, 6, 10 start %.2f, %.2f, %.2f from the corner, each deeper than the one before, 6 at least 1.4 times as deep as 3' % (s3, s6, s10),
          s3 < s6 < s10 and s6 > 1.4 * s3, [s3, s6, s10])
    twice = start(made(3.0, influence=2.0))
    half = start(made(6.0, influence=0.5))
    check('... the influence multiplies the radius (radius 3 with influence 2 starts where radius 6 does: %.3f and %.3f; radius 6 with 0.5 where 3 does: %.3f and %.3f)' % (twice, s6, half, s3),
          abs(twice - s6) < 0.03 and abs(half - s3) < 0.03, [twice, s6, half, s3])
    huge = made(1000.0)
    check('... a radius far beyond the body does not turn it inside out (the surface starts %.1f along the diagonal, the middle is inside, the far corner is sharp)' % start(huge),
          start(huge) < 16.0 and inside(huge, (20, 15, 10)) and inside(huge, (39.7, 29.7, 19.7)), start(huge))
    pyramid = morph_polyhedron([point(radius=4.0), point(position=(30, 0, 0)), point(position=(30, 30, 0)), point(position=(0, 30, 0)), point(position=(15, 15, 25))])
    check('... on a body with faces of three and of four the round corner builds and the body is whole', inside(pyramid, (15, 15, 5)) and not inside(pyramid, (0.3, 0.3, 0.3)), evaluate(pyramid, [(0.3, 0.3, 0.3)])[0])


def sphere_faces_flat():
    # A sphere point rounds the corner and nothing else: the faces round it stay where they are (the user, 2026-11-02: "a strong radius, just like it does in a profile"): the middle of each of the three faces
    # at a corner with a sphere point of radius 5 on a box of 40 x 30 x 20 is on the face
    cube = morph_polyhedron([point(position=c, radius=5 if i == 0 else None) for i, c in enumerate(corners((0, 0, 0), (40, 30, 20)))])
    middles = evaluate(cube, [(20, 15, 0), (20, 0, 10), (0, 15, 10)])
    check('a sphere point of radius 5 on a box of 40 x 30 x 20 rounds the corner and leaves the faces round it flat: the field at their middles is %s (at most 0.6 off, the faces next to a rounded corner dip a little toward it)'
          % ' '.join('%.2f' % v for v in middles), all(abs(v) < 0.6 for v in middles), middles)


def sphere_ball():
    # A cube whose eight points are sphere points of half an edge is a sphere (the user, 2026-11-02: "the Radius still does not make the shape end in a proper sphere"): the field on the sphere of radius 20
    # about the middle of the cube, in eleven directions -- along the axes, along the diagonals of the faces and of the cube, and in between -- is zero, up to 5 per cent of the radius
    ball = morph_body([point(position=c, radius=20) for c in corners((0, 0, 0), (40, 40, 40))])        # (a body: sphere points belong to it, a polyhedron is flat triangles -- F37a)
    directions = [(1, 0, 0), (0, -1, 0), (0, 0, 1), (1, 1, 0), (-1, 0, 1), (0, 1, -1), (1, 1, 1), (-1, -1, 1), (1, 2, 3), (-3, 1, 2), (2, -3, 1)]
    unit = [tuple(v / math.sqrt(sum(w * w for w in d)) for v in d) for d in directions]
    on = evaluate(ball, [tuple(20.0 + 20.0 * u[k] for k in range(3)) for u in unit])
    worst = max(abs(v) for v in on)
    check('a cube of 40 whose eight points are sphere points of radius 20 is a sphere of radius 20: the field on that sphere, in %d directions, is at most %.2f off (1 is 5 per cent of the radius)' % (len(unit), worst),
          worst < 1.0, on)
    outside = evaluate(ball, [tuple(20.0 + 22.0 * u[k] for k in range(3)) for u in unit])
    inside_ = evaluate(ball, [tuple(20.0 + 18.0 * u[k] for k in range(3)) for u in unit])
    check('... 2 outside it every direction is outside (at least 1 out) and 2 inside it every direction is inside (at least 1 in): %.2f at the least out, %.2f at the least in' % (min(outside), -max(inside_)),
          min(outside) > 1.0 and max(inside_) < -1.0, [outside, inside_])
    huge = morph_body([point(position=c, radius=1000) for c in corners((0, 0, 0), (40, 40, 40))])
    again = evaluate(huge, [tuple(20.0 + 20.0 * u[k] for k in range(3)) for u in unit])
    check('... a radius beyond half an edge is the same sphere (radius 1000: the field differs by %.6f at most)' % max(abs(a - b) for a, b in zip(on, again)), max(abs(a - b) for a, b in zip(on, again)) < 1e-6, again)
    half = morph_body([point(position=c, radius=10) for c in corners((0, 0, 0), (40, 40, 40))])
    check('... half that radius is a rounded cube, not a sphere: the middle of a face is still on the face (field %.2f) and the corner is cut' % evaluate(half, [(20, 20, 0)])[0],
          abs(evaluate(half, [(20, 20, 0)])[0]) < 0.5 and not inside(half, (0.5, 0.5, 0.5)), evaluate(half, [(20, 20, 0)])[0])


def errors():
    def refused(label, make, word):
        try:
            make()
            check(label, False)
        except ValueError as e:
            check('%s: %s' % (label, str(e)[:90]), word in str(e), str(e))
    refused('3 points are an error (at least four)', lambda: morph_polyhedron([point(), point(position=(1, 0, 0)), point(position=(0, 1, 0))]), 'at least four')
    refused('points in a line are an error', lambda: morph_polyhedron([point(position=(i, 0, 0)) for i in range(7)]), 'line')
    refused('four points in one plane are an error', lambda: morph_polyhedron([point(), point(position=(4, 0, 0)), point(position=(4, 3, 0)), point(position=(0, 3, 0))]), 'plane')
    refused('a point inside the others, without faces, is an error (of nine)',
            lambda: morph_polyhedron([point(position=c) for c in corners((0, 0, 0), (4, 4, 4))] + [point(position=(2, 2, 2))]), 'inside')
    try:
        morph_polyhedron([point(position=c) for c in corners((0, 0, 0), (1, 1, 1))], radius=-1)
        check('a negative radius is an error', False)
    except ValueError:
        check('a negative radius is an error', True)


def hull_faces():
    # Four points or more make a body (the user, 2026-11-02: "at least 4 points", the body 3D): its faces are the convex hull of the points, a flat piece of four corners one face of four
    import random
    from fieldes.stdlib import morph as M

    def centre(pos):
        return tuple(sum(p[k] for p in pos) / len(pos) for k in range(3))

    def sub(a, b):
        return tuple(a[k] - b[k] for k in range(3))

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    def facing_out(pos, faces):
        mid = centre(pos)
        return all(sum(x * y for x, y in zip(cross(sub(pos[f[1]], pos[f[0]]), sub(pos[f[2]], pos[f[0]])), sub(centre([pos[v] for v in f]), mid))) > 0 for f in faces)

    def closed(faces):
        directed = {}
        for f in faces:
            for k in range(len(f)):
                e = (f[k], f[(k + 1) % len(f)])
                directed[e] = directed.get(e, 0) + 1
        return all(n == 1 and (e[1], e[0]) in directed for e, n in directed.items())

    tetra = [(0, 0, 0), (10, 0, 0), (0, 10, 0), (0, 0, 10)]
    faces, inner = M._hull(tetra)
    check('hull of four points: four triangles, facing out, closed', len(faces) == 4 and not inner and facing_out(tetra, faces) and closed(faces), faces)
    octa = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    faces, inner = M._hull(octa)
    check('hull of the six points of an octahedron: eight triangles, facing out, closed', len(faces) == 8 and not inner and facing_out(octa, faces) and closed(faces), len(faces))
    box = corners((0, 0, 0), (40, 30, 20))
    scrambled = [box[i] for i in (5, 2, 7, 0, 3, 6, 1, 4)]
    faces, inner = M._hull(scrambled)
    check('hull of the eight corners of a box in any order: its six squares, facing out, closed', len(faces) == 6 and all(len(f) == 4 for f in faces) and facing_out(scrambled, faces) and closed(faces), faces)
    body_ = morph_polyhedron([point(position=c) for c in scrambled])
    check('... and a morph polyhedron of them is the box', inside(body_, (20, 15, 10)) and not inside(body_, (-0.5, 15, 10)) and not inside(body_, (20, 30.5, 10)) and not inside(body_, (20, 15, 20.5)))
    flipped = [(x, y, 20 - z) for x, y, z in box]           # (the same cube, its corners in the order of a left-handed one: the cube's six faces would face inwards)
    flat = morph_polyhedron([point(position=c) for c in flipped])
    check('the corners of a cube in the order of a mirror image of the template: the faces still face out (the box is inside, beyond it is outside)',
          inside(flat, (20, 15, 10)) and not inside(flat, (20, 15, 20.5)) and not inside(flat, (-0.5, 15, 10)), len(flat._morph_faces))
    mid = box + [(20, 15, 20)]                               # (the middle of the top face: a place in a face of the hull splits it in three)
    faces, inner = M._hull(mid)
    check('a ninth point in the middle of the top face: the top face is split in four triangles round it, the other five stay squares', not inner and len(faces) == 9
          and sorted(len(f) for f in faces) == [3, 3, 3, 3, 4, 4, 4, 4, 4] and facing_out(mid, faces) and closed(faces), sorted(len(f) for f in faces))
    cut = morph_polyhedron([point(position=c) for c in mid])
    check('... and the morph polyhedron of it is still the box', inside(cut, (20, 15, 10)) and not inside(cut, (20, 15, 20.5)) and inside(cut, (20, 15, 19.5)))
    # (the cube of the old rule: eight places of which one is pushed into the body keep the cube's faces)
    dent = list(box)
    dent[7] = (30, 22, 14)
    pushed = morph_polyhedron([point(position=c) for c in dent])
    check('the cube with a corner pushed into the body keeps the twelve triangles of the cube (the six squares, each in two)', len(pushed._morph_faces) == 12 and inside(pushed, (20, 15, 10)) and not inside(pushed, (39, 29, 19)),
          len(pushed._morph_faces))
    rng = random.Random(7)
    for n in (5, 8, 12, 40):
        pts = []
        while len(pts) < n:
            v = (rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1))
            l = math.sqrt(sum(x * x for x in v))
            pts.append(tuple(30.0 * x / l for x in v))       # (on a sphere: every point is a corner of the hull)
        faces, inner = M._hull(pts)
        edges = sum(len(f) for f in faces) // 2
        check('hull of %d points on a sphere: every point a corner, %d triangles (2n - 4), facing out, closed, Euler' % (n, len(faces)),
              not inner and len(faces) == 2 * n - 4 and all(len(f) == 3 for f in faces) and facing_out(pts, faces) and closed(faces) and n - edges + len(faces) == 2, (len(faces), inner))
        body_ = morph_polyhedron([point(position=p) for p in pts])
        check('... and the morph polyhedron of them has the middle of its points inside and a place beyond the sphere outside', inside(body_, centre(pts)) and not inside(body_, (0, 0, 31))
              and not inside(body_, (31, 0, 0)))
    smooth = morph_body([point(position=p) for p in pts])
    check('a morph body of %d points on a sphere: the surface goes through the points' % len(pts), all(abs(evaluate(smooth, [p])[0]) < 0.05 for p in pts), max(abs(evaluate(smooth, [p])[0]) for p in pts))


def entry(name, **kw):
    r = dict(kind='primitive', name=name, x=0, y=0, z=0, scale=10, indent=0, var=name + '_1')
    r.update(kw)
    return mc.call(json.dumps(r))


def menu():
    for name in ('morph_polyhedron', 'morph_body'):
        text = entry(name)
        count = text.count('point(')
        check('menu: %s writes eight points and the call of them' % name, count == 8 and ('%s(' % name) in text, '%d points' % count)
        selected = [{'name': 'p%d' % i, 'type': 'point'} for i in range(1, 9)]
        made = entry(name, body='p1', body_info={'type': 'point'}, others=selected[1:])
        check('menu: %s of eight selected points is made of them' % name, 'p1' in made and 'p8' in made and 'point(' not in made, made)


def apply_edits(source, edits):
    lines = source.split('\n')
    # (every edit is in the coordinates of the script as it was: the last first)
    for l0, c0, l1, c1, text in sorted(edits, key=lambda e: (e[0], e[1]), reverse=True):
        head = lines[l0][:c0]
        tail = lines[l1][c1:]
        lines[l0:l1 + 1] = (head + text + tail).split('\n')
    return '\n'.join(lines)


def make(source):
    ns = {'var': lambda v: v}
    exec('from fieldes import *\n' + source, ns)
    runner.last_globals = ns
    return ns


def insertion():
    # the script the menu writes, as a user has it, and a press on a face of the body that is dragged: the point is put there and the body says its triangles
    for name in ('morph_polyhedron', 'morph_body'):
        script = entry(name).replace('#@@#\n', name + '_1 = ') + '\n' + name + '_1\n'      # (the program writes `var = ` before the call: see PRELUDE)
        ns = make(script)
        obj = ns[name + '_1']
        n0 = len(obj.points)
        line = max(i for i, l in enumerate(script.split('\n'), 1) if l.strip() == name + '_1')
        top = max(c[2] for c in [tuple(p.xyz) for p in obj.points] if True) if False else None
        zs = sorted(c.pos[2] for c in obj._ctrls)
        xs = sorted(c.pos[0] for c in obj._ctrls)
        ys = sorted(c.pos[1] for c in obj._ctrls)
        press = ((xs[0] + xs[-1]) / 2.0 + 2.5, (ys[0] + ys[-1]) / 2.0 - 1.0, zs[-1])        # (on the top face, off its diagonal: a press that is on an edge of the mesh is a sliver)
        request = {'op': 'insert', 'source': script, 'line': line, 'x': press[0], 'y': press[1], 'z': press[2], 'px': press[0], 'py': press[1], 'pz': press[2]}
        answer = json.loads(profile_edit.edit(json.dumps(request)))
        check('%s: the press answers with edits and the new point to select' % name, len(answer['edits']) == 2 and answer['name'] == name + '_1_p9', answer['name'])
        script2 = apply_edits(script, answer['edits'])
        ns2 = make(script2)
        obj2 = ns2[name + '_1']
        check('%s: after the edit the script runs and the body has nine points and its faces are split round the new point (a polyhedron: 12 triangles, the one pressed in three, 14; a body: 6 faces, the quad pressed in four, 9)' % name,
              len(obj2.points) == n0 + 1 and len(obj2._morph_faces) == (14 if name == 'morph_polyhedron' else 9),
              (len(obj2.points), len(obj2._morph_faces)))
        check('%s: the script says its triangles in a list above the call, no argument of it' % name, name + '_1_faces = [' in script2 and 'faces=' not in script2)
        # a second press: the call that says its faces is edited, not given another
        line2 = max(i for i, l in enumerate(script2.split('\n'), 1) if l.strip() == name + '_1')
        request2 = dict(request, source=script2, line=line2, x=xs[0] + 0.4, px=xs[0] + 0.4, y=(ys[0] + ys[-1]) / 2.0, py=(ys[0] + ys[-1]) / 2.0, z=(zs[0] + zs[-1]) / 2.0, pz=(zs[0] + zs[-1]) / 2.0)
        answer2 = json.loads(profile_edit.edit(json.dumps(request2)))
        script3 = apply_edits(script2, answer2['edits'])
        ns3 = make(script3)
        obj3 = ns3[name + '_1']
        check('%s: a second press: ten points, 16 faces for a polyhedron (a triangle split in three) or 11 or 12 for a body (a triangle or a quad split), one list of faces' % name,
              len(obj3.points) == n0 + 2 and len(obj3._morph_faces) in ((16,) if name == 'morph_polyhedron' else (11, 12)) and script3.count('_faces = [') == 1 and 'faces=' not in script3,
              (len(obj3.points), len(obj3._morph_faces), script3.count('_faces = [')))
        # the new point is on the surface where it was pressed (the body is the same shape), and dragged out of it the body has a bump, dragged in a dent
        new_name = answer['name']
        k = len(obj2.points) - 1
        base = obj2._ctrls[k].pos
        check('%s: the new point is where it was pressed' % name, math.dist(base, press) < 1e-3, base)
        up = script2.replace('%s = point(\n    position=(var(%s), var(%s), var(%s)),' % (new_name, '%g' % round(press[0], 4), '%g' % round(press[1], 4), '%g' % round(press[2], 4)),
                             '%s = point(\n    position=(var(%s), var(%s), var(%s)),' % (new_name, '%g' % round(press[0], 4), '%g' % round(press[1], 4), '%g' % round(press[2] + 10.0, 4)))
        down = script2.replace('%s = point(\n    position=(var(%s), var(%s), var(%s)),' % (new_name, '%g' % round(press[0], 4), '%g' % round(press[1], 4), '%g' % round(press[2], 4)),
                               '%s = point(\n    position=(var(%s), var(%s), var(%s)),' % (new_name, '%g' % round(press[0], 4), '%g' % round(press[1], 4), '%g' % round(press[2] - 4.0, 4)))
        check('%s: the text of the new point is what the test expects (a bump and a dent are made by changing its z)' % name, up != script2 and down != script2)
        bump = make(up)[name + '_1']
        dent = make(down)[name + '_1']
        above = (press[0], press[1], zs[-1] + 0.5)
        below = (press[0], press[1], zs[-1] - 1.0)
        check('%s: dragged out the body bulges there (a point 0.5 above the old top face is inside) and dragged in it dents (a point 1 under it is outside)' % name,
              inside(bump, above) and not inside(dent, below) and inside(obj2, below), [inside(bump, above), inside(dent, below), inside(obj2, below)])
        if name == 'morph_body':
            # a point made by a surface drag takes the type of the point he used last (H76): a double click makes p1 a sphere point (a radius in mm), and the press that follows names p1 as that point
            p1_line = next(i for i, l in enumerate(script.split('\n'), 1) if l.strip() == name + '_1_p1')
            sphere = apply_edits(script, json.loads(profile_edit.edit(json.dumps({'op': 'toggle', 'source': script, 'line': p1_line})))['edits'])
            line3 = max(i for i, l in enumerate(sphere.split('\n'), 1) if l.strip() == name + '_1')
            make(sphere)        # (the edit reads the body of the script that ran last: the one it is asked about)
            styled = json.loads(profile_edit.edit(json.dumps(dict(request, source=sphere, line=line3, style_of=name + '_1_p1'))))
            text3 = apply_edits(sphere, styled['edits'])
            made = text3.split(styled['name'] + ' = point(')[1].split('\n)')[0]
            # (the influence of 1 is written with the point: the tree wrote it a moment later otherwise, one line inside the statement, and the drag of the new point never began -- H88)
            check('%s: a point made by a surface drag takes the type of the last point used (a sphere point: a radius in mm and an influence of 1, no other edit follows)' % name, 'radius=var(' in made and 'influence=var(1)' in made, made)
            made_obj = make(text3)[name + '_1']
            check('%s: the script with it runs and the new point is a sphere point' % name, bool(getattr(made_obj._ctrls[-1], 'rounded', False)), made)
            make(script)
            plain = json.loads(profile_edit.edit(json.dumps(dict(request, source=script, line=line))))
            check('%s: with no point used before, the new point is as it was (an influence, no radius)' % name, 'influence=var(1)' in apply_edits(script, plain['edits']) and 'radius=var(' not in apply_edits(script, plain['edits']))


def toggling():
    # a double click on the dot of a point of a body made of points -- the edit the program asks for -- is a corner, a spline point, a round corner, and a corner again (a curve has no round corner;
    # a body of points has, as a profile has).  The round corner says a radius, a quarter of the shortest edge of the cage at the point (two figures)
    for name in ('morph_polyhedron', 'morph_body'):
        script = entry(name).replace('#@@#\n', name + '_1 = ') + '\n' + name + '_1\n'
        seen, radii = [], []
        for _ in range(4):
            ns = make(script)
            dot = ns[name + '_1_p1']
            seen.append('round' if dot._curve_round else ('spline' if dot._curve_smooth else 'corner'))
            line = next(i for i, l in enumerate(script.split('\n'), 1) if l.strip() == name + '_1_p1')
            answer = json.loads(profile_edit.edit(json.dumps({'op': 'toggle', 'source': script, 'line': line})))
            before = script
            script = apply_edits(script, answer['edits'])
            found = re.search(r'radius=var\(([-0-9.e]+)\)', script)
            if found and 'radius=' not in before:
                radii.append((float(found.group(1)), profile_edit._floor_figures(0.5 * dot._curve_edge, 2)))
        # (a polyhedron is flat triangles only: its points move and nothing else, a double click changes nothing there -- the user, 2026-11-02, F37a)
        want = ['corner', 'corner', 'corner', 'corner'] if name == 'morph_polyhedron' else ['spline', 'round', 'corner', 'spline']
        check('%s: four double clicks on a point make %s' % (name, ', '.join(want)), seen == want, seen)
        if name == 'morph_polyhedron':
            check('%s: a double click never gives a point a radius' % name, radii == [], radii)
        else:
            check('%s: the sphere point starts with half the shortest edge at the point, rounded down to two figures (the radius at which the corner is a true sphere), in mm, and says no influence' % name, len(radii) >= 1 and all(abs(a - b) < 1e-9 and a > 0 for a, b in radii), radii)


def tangent_planes():
    # SMOOTH IN ALL DIRECTIONS (the user, 2026-11-01): along every edge between two spline points the two faces have ONE tangent plane -- worked out here from the control points of their patches, at points
    # all along the edge, not from a mesh (a mesh of a curved surface turns by a few degrees at every step whatever the surface does: only the control points say whether the planes are the same).
    # A face of three is three patches of four (see `triangle` in morph.py): they are asked the same along the edge of the cage they share with the next face (each covers a half of it), and along the
    # three lines inside the face where they meet
    from fieldes.stdlib import morph as morph_module
    caught = {}
    original = morph_module._tessellate

    def wrapped(cage, V, edge, n, gregory, triangle):
        caught.update(cage=cage, V=V, edge=edge, gregory=gregory, triangle=triangle)
        return original(cage, V, edge, n, gregory, triangle)

    def num(v):
        return tuple(float(C.value_of(c)) for c in v)

    def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
    def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
    def sc(s, a): return (a[0] * s, a[1] * s, a[2] * s)
    def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

    def bern(k, d, t):
        return math.comb(d, k) * t ** k * (1 - t) ** (d - k)

    def combo(pts, d, t):
        out = (0.0, 0.0, 0.0)
        for k, p in enumerate(pts):
            out = add(out, sc(bern(k, d, t), p))
        return out

    def normal(B, R, s):
        ''' across the tangent plane of a patch at the place s of its edge: the edge's own direction and the cross-boundary derivative, which is three times the second row less the edge '''
        return cross(sc(3.0, combo([sub(B[k + 1], B[k]) for k in range(3)], 2, s)), sc(3.0, combo([sub(R[k], B[k]) for k in range(4)], 3, s)))

    def angle(n1, n2):
        # (the normals face out of the body on both sides, so a face that leaves the edge the wrong way -- folded back over the other -- makes an angle of up to 180 degrees, not 0)
        cosine = dot(n1, n2) / math.sqrt(dot(n1, n1) * dot(n2, n2))
        return math.degrees(math.acos(max(-1.0, min(1.0, cosine))))

    def measure(points, faces=None, sharp=False):
        ''' (the largest angle between the tangent planes of two patches along the edges of the cage between spline points -- or, with sharp, the edges that have a corner at an end --, how many edges,
            the same along the lines inside the faces of three, how many) '''
        caught.clear()
        morph_module._tessellate = wrapped
        try:
            body = morph_body(points, _faces=faces)
        finally:
            morph_module._tessellate = original
        cage, V, edge, gregory, triangle = caught['cage'], caught['V'], caught['edge'], caught['gregory'], caught['triangle']
        smooth = [p._curve_smooth for p in body.points]
        Vn = lambda i: num(V[i])
        E = lambda i, j: num(edge(i, j))
        found, inner = [], {}

        def put(x, y, whole, B, R, turns):
            # (the edge of a patch from x to y: the whole edge of the cage, or the half of it that starts at x -- where it starts and ends along the cage edge, counted from the lower point -- and whether
            # the face goes round x to y counterclockwise (then the interior is on the left and the normal across the edge and the way into the face faces out) or y to x)
            lo, hi = min(x, y), max(x, y)
            a_t = 0.0 if x == lo else 1.0
            b_t = (1.0 if whole else 0.5) if x == lo else (0.0 if whole else 0.5)
            found.append((lo, hi, a_t, b_t, B, R, 1.0 if turns else -1.0))
        for fi, face in enumerate(cage):
            if len(face) == 4:
                a, b, c, d = face
                ver = {k: (num(p), num(q)) for k, (p, q) in gregory(face).items()}
                put(a, b, True, (Vn(a), E(a, b), E(b, a), Vn(b)), (E(a, d), ver[(1, 1)][0], ver[(2, 1)][0], E(b, c)), True)
                put(b, c, True, (Vn(b), E(b, c), E(c, b), Vn(c)), (E(b, a), ver[(2, 1)][1], ver[(2, 2)][1], E(c, d)), True)
                put(d, c, True, (Vn(d), E(d, c), E(c, d), Vn(c)), (E(d, a), ver[(1, 2)][0], ver[(2, 2)][0], E(c, b)), False)
                put(a, d, True, (Vn(a), E(a, d), E(d, a), Vn(d)), (E(a, b), ver[(1, 1)][1], ver[(1, 2)][1], E(d, c)), False)
            else:
                a, b, c = face
                for s, (net, versions) in enumerate(triangle(face)):
                    x, y, z = ((a, b, c), (b, c, a), (c, a, b))[s]
                    N = [[num(p) for p in row] for row in net]
                    ver = {k: (num(p), num(q)) for k, (p, q) in versions.items()}
                    put(x, y, False, (N[0][0], N[1][0], N[2][0], N[3][0]), (N[0][1], ver[(1, 1)][0], ver[(2, 1)][0], N[3][1]), True)
                    put(x, z, False, (N[0][0], N[0][1], N[0][2], N[0][3]), (N[1][0], ver[(1, 1)][1], ver[(1, 2)][1], N[1][3]), False)
                    # (the lines inside the face, both ways from the middle of an edge to the middle of the face: the patch at x has the one for the edge x y as its edge b c and the one for the edge z x as its d c)
                    inner.setdefault((fi, x), []).append(((N[3][0], N[3][1], N[3][2], N[3][3]), (N[2][0], ver[(2, 1)][1], ver[(2, 2)][1], N[2][3]), 1.0))
                    inner.setdefault((fi, z), []).append(((N[0][3], N[1][3], N[2][3], N[3][3]), (N[0][2], ver[(1, 2)][0], ver[(2, 2)][0], N[3][2]), -1.0))
        by_edge = {}
        for record in found:
            by_edge.setdefault((record[0], record[1]), []).append(record)
        worst, edges = 0.0, 0
        for (lo, hi), records in by_edge.items():
            if bool(smooth[lo] and smooth[hi]) == sharp:
                continue                                                # (sharp=True looks at the edges that have a corner at an end: the measure must see those)
            edges += 1
            for t in (0.05, 0.2, 0.35, 0.5, 0.65, 0.8, 0.95):
                normals = []
                for _, _, a_t, b_t, B, R, turning in records:
                    s = (t - a_t) / (b_t - a_t)
                    if -1e-9 <= s <= 1.0 + 1e-9:
                        normals.append(sc(turning, normal(B, R, min(1.0, max(0.0, s)))))
                for i in range(len(normals)):
                    for j in range(i):
                        worst = max(worst, angle(normals[i], normals[j]))
        worst_inner = 0.0
        for pair in inner.values():
            for s in (0.05, 0.25, 0.5, 0.75, 0.95):
                worst_inner = max(worst_inner, angle(sc(pair[0][2], normal(pair[0][0], pair[0][1], s)), sc(pair[1][2], normal(pair[1][0], pair[1][1], s))))
        return worst, edges, worst_inner, len(inner)
    tolerance = 1e-3                                                    # (degrees: the kernel hands the points back in single precision, which is 1e-5 degrees; a real kink is degrees)
    box = corners((0, 0, 0), (40, 30, 20))
    worst, edges, _, _ = measure([point(position=c) for c in box])
    check('morph_body of eight spline points: along all %d edges the two faces have the same tangent plane (the largest angle between them %.6f degrees)' % (edges, worst), edges == 12 and worst < tolerance, (edges, worst))
    worst, edges, _, _ = measure([point(position=c, rotation=((10, 20, 50) if i in (0, 7) else (0, 0, 0))) for i, c in enumerate(box)])
    check('rotated spline points: the two faces still have one tangent plane along every edge (%.6f degrees)' % worst, edges == 12 and worst < tolerance, (edges, worst))
    skew = [(x * 1.0 + 0.15 * y, y * 1.0 - 0.1 * z, z + 0.2 * x) for x, y, z in box]               # (a box that is not a box: the planes are the same for any cage)
    worst, edges, _, _ = measure([point(position=c) for c in skew])
    check('a skewed cage of eight spline points: the same (%.6f degrees)' % worst, edges == 12 and worst < tolerance, (edges, worst))
    worst, edges, _, _ = measure([point(position=c, smooth=(i != 7)) for i, c in enumerate(box)])
    check('one corner among spline points: the edges between spline points are smooth, the three at the corner are not asked (%d edges, %.6f degrees)' % (edges, worst), edges == 9 and worst < tolerance, (edges, worst))
    kinked, edges, _, _ = measure([point(position=c, smooth=(i != 7)) for i, c in enumerate(box)], sharp=True)
    check('the measure sees a kink: the three edges at that corner turn by %.1f degrees (they are sharp by design)' % kinked, edges == 3 and kinked > 10.0, (edges, kinked))
    # a point put on the top face by a press: the face is split in four faces of three round it, and the body is smooth along the four new edges, along the four edges of the old top face (a face of four
    # and a face of three meet there) and along the lines inside each face of three
    lifted = [point(position=c) for c in box] + [point(position=(20, 15, 27))]
    others = [(0, 2, 3, 1), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    fans = [(4, 5, 8), (5, 7, 8), (7, 6, 8), (6, 4, 8)]
    worst, edges, inner, lines = measure(lifted, others + fans)
    check('a point pressed out of the top face: all %d edges of the cage have one tangent plane (%.6f degrees), and the %d lines inside the four faces of three (%.6f degrees)' % (edges, worst, lines, inner),
          edges == 16 and lines == 12 and worst < tolerance and inner < tolerance, (edges, worst, lines, inner))
    # the same pressed in (a dent): the normals face out on both sides of every edge, so a face that folded back over the one next to it would make 180 degrees, not 0
    for z in (22, 20, 16, 12):
        worst, edges, inner, lines = measure([point(position=c) for c in box] + [point(position=(20, 15, z))], others + fans)
        check('a point put at z = %d (the top face is at 20): one tangent plane along the %d edges, normals facing out on both sides (%.6f degrees), and along the %d lines inside the faces of three (%.6f degrees)' % (z, edges, worst, lines, inner),
              edges == 16 and lines == 12 and worst < tolerance and inner < tolerance, (edges, worst, lines, inner))
    # a body of faces of three only: an octahedron of spline points
    octa = [point(position=(20, 0, 0)), point(position=(-20, 0, 0)), point(position=(0, 15, 0)), point(position=(0, -15, 0)), point(position=(0, 0, 10)), point(position=(0, 0, -10))]
    faces = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
    worst, edges, inner, lines = measure(octa, faces)
    check('an octahedron of spline points, eight faces of three: all %d edges (%.6f degrees) and %d lines inside the faces (%.6f degrees) have one tangent plane' % (edges, worst, lines, inner),
          edges == 12 and lines == 24 and worst < tolerance and inner < tolerance, (edges, worst, lines, inner))


def cache():
    # the cache keeps a statement that took long enough and hands it back for the same inputs.  A body made of points marks the points of ITS run (curves.mark: what each is, how long its edges are), which a
    # result handed back would not do for the points of a later run: the double click on a point of the user's morph cube found "not a point of a profile or a curve" (2026-11-01), once the finer field made the
    # body slow enough to be kept (0.25 s)
    from fieldes import field_cache
    from fieldes.shape import Shape
    saved = field_cache._write
    field_cache._write = lambda *a, **k: None                  # (nothing of this test goes to a file)
    try:
        body = morph_body([point(position=c) for c in corners((0, 0, 0), (40, 30, 20))])
        plain = Shape.X() * 2.0 - 5.0
        field_cache.store('morph-test-body', body, '', 5.0)
        field_cache.store('morph-test-plain', plain, '', 5.0)
        kept_plain = field_cache._memory.get('morph-test-plain') is not None
        kept_body = field_cache._memory.get('morph-test-body') is not None
    finally:
        field_cache._write = saved
        field_cache._memory.pop('morph-test-body', None)
        field_cache._memory.pop('morph-test-plain', None)
    check('the cache keeps a plain field that took long enough (the test is a real one)', kept_plain)
    check('the cache does not keep a body made of points however long it took: it marks the points of its own run', not kept_body)


def rotation_of_a_spline_point():
    # The rotation of a spline point turns the plane its surface leaves it in (the user, 2026-11-02: a requirement of the feature, as for the line of a spline): the surface still goes through the
    # point, leaves it another way, and the faces that meet there still have one tangent plane with the faces beyond their edges
    box = corners((0, 0, 0), (40, 30, 20))
    plain = morph_body([point(position=c) for c in box])
    turned = morph_body([point(position=c, rotation=((0, 0, 60) if i == 7 else (0, 0, 0))) for i, c in enumerate(box)])
    check('rotation: a spline point that is turned still has the surface through it', abs(evaluate(turned, [(40, 30, 20)])[0]) < 0.3, evaluate(turned, [(40, 30, 20)])[0])
    spots = [(38, 28, 20), (40, 26, 18), (36, 30, 17), (40, 30, 16), (34, 24, 20)]
    a, b = evaluate(plain, spots), evaluate(turned, spots)
    check('rotation: it leaves the point another way (the field near the point is not the same, largest difference %.2f)' % max(abs(p - q) for p, q in zip(a, b)), max(abs(p - q) for p, q in zip(a, b)) > 0.05)
    quiet = morph_body([point(position=c, rotation=(0, 0, 0)) for c in box])
    a, b = evaluate(plain, spots), evaluate(quiet, spots)
    check('rotation: no rotation is the surface it always was (largest difference %.2g)' % max(abs(p - q) for p, q in zip(a, b)), max(abs(p - q) for p, q in zip(a, b)) < 1e-4)
    corner = morph_polyhedron([point(position=c, rotation=((0, 0, 60) if i == 7 else (0, 0, 0))) for i, c in enumerate(box)])
    check('rotation: the rotation of a corner is not used (the cube is the box)', inside(corner, (39.7, 29.7, 19.7)) and not inside(corner, (40.5, 30.5, 20.5)))


def polyhedra():
    # A morph shape made only of corner points is exactly the polyhedron of its faces (the user, 2026-11-02: "a morph shape that is only made from edge points should actually be a polygon ... I remember this
    # not being the case all the time"): every vertex of what is drawn lies in the plane of one of its faces -- also with a corner point pulled out of a face, or pushed in
    def drawn(body):
        out = []
        for v in range(len(body._morph_vstart) - 1):
            total = [0.0, 0.0, 0.0]
            for k in range(body._morph_vstart[v], body._morph_vstart[v + 1]):
                control = body._morph_controls[body._morph_vctrl[k]]
                for a in range(3):
                    total[a] += body._morph_vweight[k] * float(C.value_of(control[a]))
            out.append(tuple(total))
        return out

    def plane(P, face):
        a, b, c = P[face[0]], P[face[1]], P[face[2]]
        u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(sum(x * x for x in n)) or 1e-30
        return a, tuple(x / length for x in n)
    box = corners((0, 0, 0), (40, 30, 20))
    others = [(0, 2, 3, 1), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    fans = [(4, 5, 8), (5, 7, 8), (7, 6, 8), (6, 4, 8)]
    for label, extra in (('a cube of corners', None), ('a cube of corners with a corner point pulled out of its top', (20, 15, 27)), ('... and pushed in', (20, 15, 16))):
        pts = [point(position=c) for c in box] + ([point(position=extra)] if extra else [])
        body = morph_polyhedron(pts, _faces=others + fans) if extra else morph_polyhedron(pts)
        P = [tuple(c.pos) for c in body._ctrls]
        planes = [plane(P, f) for f in body._morph_faces]
        worst = 0.0
        for q in drawn(body):
            worst = max(worst, min(abs(sum((q[i] - a[i]) * n[i] for i in range(3))) for a, n in planes))
        check('%s is exactly its polyhedron: what is drawn is at most %.6f mm from the planes of its faces' % (label, worst), worst < 1e-3, worst)


def symmetry_link():
    # A symmetry applied LATER to a shape that was made without one (the user, 2026-11-02): Shift+S with the shape selected links the points that already have an exact mirror partner -- the later
    # point of a pair becomes the image of the earlier one, nothing is put on top of a point, a point on the plane stays -- and a second symmetry on the same points applies as well
    def point_text(name, x, y, z):
        return ('%s = point(\n    position=(var(%s), var(%s), var(%s)),\n    rotation=(var(0), var(0), var(0)),\n    scale=(var(1), var(1), var(1)),\n)\n%s\n' % (name, x, y, z, name))

    def symmetry_text(name, first):
        return ("%s = symmetry_planes(\n    planes=1,\n    kind='axis',\n    axis=(0, 0, 1),\n    first=%s,\n    position=(var(0), var(0), var(0)),\n    rotation=(var(0), var(0), var(0)),\n)\n%s\n"
                % (name, first, name))
    places = [(-20, -14, -10), (20, -14, -10), (-20, 14, -10), (20, 14, -10), (-16, -9, 10), (16, -9, 10), (-16, 9, 10), (16, 9, 10), (0, 0, 19)]
    faces = '[(0, 2, 3, 1), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5), (4, 5, 8), (5, 7, 8), (7, 6, 8), (6, 4, 8)]'
    body_text = ''.join(point_text('sh_p%d' % (k + 1), *p) for k, p in enumerate(places)) + 'sh_faces = %s\nsh = morph_polyhedron(\n    [%s],\n)\nsh\n' % (faces, ', '.join('sh_p%d' % k for k in range(1, 10)))
    request = {'var': 'mirror', 'centre': [0, 0, 0], 'elements': [{'mirror': [1, 0, 0], 'turn': None}]}
    request2 = {'var': 'mirror2', 'centre': [0, 0, 0], 'elements': [{'mirror': [0, 1, 0], 'turn': None}]}

    def display_line(script, name):
        return max(i for i, l in enumerate(script.split('\n'), 1) if l.strip() == name)

    def link(script, symmetry):
        make(script)                                 # (the program has run the script it edits: runner.last_globals is that run)
        return apply_edits(script, json.loads(profile_edit.edit(json.dumps({'op': 'symmetry_link', 'source': script, 'line': display_line(script, 'sh'), 'symmetry': symmetry})))['edits'])

    def positions(ns):
        return [tuple(float(v) for v in c.pos) for c in ns['sh']._ctrls]

    # the symmetry made AFTER the shape, where the program puts one that Shift+S makes
    script = body_text + symmetry_text('mirror', '(0, 1, 0)')
    before = positions(make(script))
    linked = link(script, request)
    ns = make(linked)
    check('symmetry later: four points are linked (the right ones are images of the left ones), the point on the plane stays a point', linked.count('symmetry_image(') == 4 and linked.count(' = point(') == 5,
          (linked.count('symmetry_image('), linked.count(' = point(')))
    check('... and the symmetry statement moved up above the first image that uses it, once', linked.count('mirror = symmetry_planes(') == 1
          and linked.index('mirror = symmetry_planes(') < linked.index('sh_p2 = symmetry_image(') and linked.index('sh_p1 = point(') < linked.index('mirror = symmetry_planes('))
    after = positions(ns)
    check('... and the shape is the one it was (no point moved)', max(abs(a - b) for p, q in zip(before, after) for a, b in zip(p, q)) < 1e-9)
    moved = make(linked.replace('var(-20), var(-14), var(-10)', 'var(-26), var(-12), var(-10)'))
    check('... and pulling a left point pulls its partner, mirrored', abs(positions(moved)[1][0] - 26.0) < 1e-9 and abs(positions(moved)[1][1] + 12.0) < 1e-9, positions(moved)[1])
    try:
        link(linked, request)
        check('... and applying it again has nothing to link (an error that says so)', False)
    except ValueError as e:
        check('... and applying it again has nothing to link (an error that says so): %s' % str(e)[:70], 'no point' in str(e))
    # the symmetry made BEFORE the shape stays where it is
    first = symmetry_text('mirror', '(0, 1, 0)') + body_text
    linked_first = link(first, request)
    check('symmetry before the shape: only the four points change, the symmetry does not move', linked_first.count('symmetry_image(') == 4 and linked_first.startswith('mirror = symmetry_planes('))
    # a second symmetry on the same points: x -> -x is applied, now y -> -y: the points that have a partner under it are linked as well, and all of them follow
    both = linked + symmetry_text('mirror2', '(1, 0, 0)')
    twice = link(both, request2)
    ns2 = make(twice)
    check('a second symmetry: more points are linked, both symmetries stay (mirror and mirror2)', twice.count('symmetry_image(') > 4 and 'mirror2' in twice and 'mirror, 1' in twice, twice.count('symmetry_image('))
    pulled = positions(make(twice.replace('var(-20), var(-14), var(-10)', 'var(-25), var(-12), var(-10)')))
    expected = [(-25, -12, -10), (25, -12, -10), (-25, 12, -10), (25, 12, -10)]
    check('... and pulling the first point moves its three partners (x, y and both), all mirrored', all(max(abs(a - b) for a, b in zip(pulled[k], expected[k])) < 1e-9 for k in range(4)), pulled[:4])
    # a shape beside the plane has no partners: nothing is linked
    lean = ''.join(point_text('sh_p%d' % (k + 1), 40 + x * 0.5 + y * 0.1, 10 + y + z * 0.3, z) for k, (x, y, z) in enumerate(places[:8])) + 'sh = morph_polyhedron([%s])\nsh\n' % ', '.join('sh_p%d' % k for k in range(1, 9))
    try:
        link(lean + symmetry_text('mirror', '(0, 1, 0)'), request)
        check('a shape beside the plane has no partner: an error that says so', False)
    except ValueError as e:
        check('a shape beside the plane has no partner: an error that says so: %s' % str(e)[:70], 'no point' in str(e))


def resolution_of_the_view():
    # What is drawn of a body of points follows the resolution of the view (the user, 2026-11-02: "It does not appear to respond to the resolution when you set it"): half the resolution, about a quarter of
    # the vertices; the resolution 10 is what it always was (20 pieces to an edge, 2402 vertices for a cube)
    from fieldes import view
    box = [point(position=c) for c in corners((0, 0, 0), (40, 30, 20))]
    counts = {}
    try:
        for res in (3, 5, 10, 20):
            view.set_resolution(res)
            body_ = morph_body(box)
            counts[res] = (len(body_._morph_vstart) - 1, len(body_._morph_tris))
    finally:
        view.settings.pop('resolution', None)
    check('the drawn surface of a morph body has more vertices the higher the resolution (3, 5, 10, 20: %s)' % ', '.join(str(counts[r][0]) for r in (3, 5, 10, 20)),
          counts[3][0] < counts[5][0] < counts[10][0] < counts[20][0], counts)
    check('... at the resolution 10 it is what it always was (2402 vertices, 4800 triangles)', counts[10] == (2402, 4800), counts[10])
    check('... and half the resolution draws about a quarter of the vertices (%.2f)' % (counts[5][0] / counts[10][0]), 0.2 < counts[5][0] / counts[10][0] < 0.35)
    none = morph_body(box)
    check('... with no resolution said it is what it was at 10', (len(none._morph_vstart) - 1, len(none._morph_tris)) == (2402, 4800))


def points_in_place():
    # The standard operations with the points of a morph shape selected (the user, 2026-11-01/02): move, scale, rotate and mirror change the points themselves, in place, and the shape follows
    box = [(x, y, z) for z in (0, 20) for y in (0, 30) for x in (0, 40)]
    script = ''.join('p%d = point(\n    position=(var(%s), var(%s), var(%s)),\n)\np%d\n' % (k + 1, x, y, z, k + 1) for k, (x, y, z) in enumerate(box))
    script += 'sh = morph_polyhedron([%s])\nsh\n' % ', '.join('p%d' % k for k in range(1, 9))
    names = ['p%d' % k for k in range(1, 9)]

    def run_op(how, chosen=names, scale=10):
        answer = json.loads(profile_edit.edit(json.dumps({'op': 'points_transform', 'source': script, 'names': chosen, 'how': how, 'scale': scale})))
        ns = make(apply_edits(script, answer['edits']))
        return [tuple(float(v) for v in c.pos) for c in ns['sh']._ctrls], answer['label']

    def close(a, b):
        return all(max(abs(x - y) for x, y in zip(p, q)) < 1e-3 for p, q in zip(a, b))
    after, label = run_op('move')
    check('move: every selected point moves by the step the menu moves a body by, the shape follows (%s)' % label, close(after, [(x + 10, y, z) for x, y, z in box]) and label == 'Move 8 points', after[:2])
    after, label = run_op('scale_xyz')
    mid = (20, 15, 10)
    check('scale: about the centre of the selected points, by 1.5 (%s)' % label, close(after, [tuple(mid[k] + (p[k] - mid[k]) * 1.5 for k in range(3)) for p in box]), after[:2])
    after, label = run_op('rotate_z')
    c, s = math.cos(math.radians(45)), math.sin(math.radians(45))
    check('rotate: 45 degrees about the z axis through the centre of the selected points (%s)' % label,
          close(after, [(20 + (x - 20) * c - (y - 15) * s, 15 + (x - 20) * s + (y - 15) * c, z) for x, y, z in box]), after[:2])
    after, label = run_op('mirror')
    check('mirror: across the plane x = 0, the plane the library mirror takes (%s)' % label, close(after, [(-x, y, z) for x, y, z in box]), after[:2])
    after, label = run_op('move', ['p1', 'p2'])
    check('only the points that are selected change (two of eight; %s)' % label, close(after[:2], [(10, 0, 0), (50, 0, 0)]) and close(after[2:], box[2:]) and label == 'Move 2 points')
    try:
        profile_edit.edit(json.dumps({'op': 'points_transform', 'source': 'a = 5\n', 'names': ['a'], 'how': 'move'}))
        check('something that is not a point is an error that says so', False)
    except ValueError as e:
        check('something that is not a point is an error that says so: %s' % str(e)[:60], 'point' in str(e))


def symmetry_menu():
    # A morph cube made from the menu while a symmetry is selected: the symmetry is applied to every individual point, and an image that lands on a point that is there IS that point (the user,
    # 2026-11-02) -- a cube with its middle on the plane has four points of its own and four that are their images; a cube beside the plane is mirrored as a whole, as it was
    import json
    from fieldes import menu_catalog as mc

    def text(x, elements):
        r = dict(kind='primitive', name='morph_polyhedron', x=x, y=0, z=0, scale=10, indent=0, var='morph_polyhedron_1',
                 symmetry={'var': 'symmetry_1', 'centre': [0, 0, 0], 'elements': elements})
        return mc.call(json.dumps(r))
    one = [{'mirror': [1, 0, 0]}]
    centred = text(0, one)
    check('menu with a symmetry: a cube with its middle on the plane has four points and four images of them, no point on top of another, and no image of the whole',
          centred.count('= symmetry_image(') == 4 and centred.count('= point(') == 4 and 'symmetry_images(' not in centred, (centred.count('= symmetry_image('), centred.count('= point(')))
    off = text(30, one)
    check('menu with a symmetry: a cube beside the plane has eight points of its own and its whole mirror image',
          off.count('= point(') == 8 and '= symmetry_image(' not in off and 'symmetry_images(morph_polyhedron_1, symmetry_1)' in off)
    both = text(0, ({'mirror': [1, 0, 0]}, {'mirror': [0, 1, 0]}, {'mirror': None, 'turn': [[0, 0, 1], 180]}))
    check('menu with two planes: the cube on both has two points of its own (a bottom and a top corner) and six images, three of each', both.count('= point(') == 2 and both.count('= symmetry_image(') == 6, (both.count('= point('), both.count('= symmetry_image(')))
    # the text runs, and its images are where the symmetry puts them
    script = centred.replace('#@@#\n', 'morph_polyhedron_1 = ') + '\nmorph_polyhedron_1\n'          # (the program writes `name = ` before the call: see PRELUDE)
    ns = {'var': lambda v: v}
    exec('from fieldes import *\nsymmetry_1 = symmetry_planes(planes=1, kind=%r, axis=(0, 0, 1), first=(0, 1, 0))\n' % 'axis' + script, ns)

    def at(p):
        return tuple(round(float(C.value_of(c)), 5) for c in p.xyz)
    check('... and run, each image is the mirror image of its point (x -> -x)',
          all(at(ns['morph_polyhedron_1_p%d' % b])[0] == -at(ns['morph_polyhedron_1_p%d' % a])[0] and at(ns['morph_polyhedron_1_p%d' % b])[1:] == at(ns['morph_polyhedron_1_p%d' % a])[1:] for a, b in ((1, 2), (3, 4), (5, 6), (7, 8))))
    check('... and the cube is the box of its eight points', inside(ns['morph_polyhedron_1'], (0, 0, 0)) and not inside(ns['morph_polyhedron_1'], (8, 0, 0)) and inside(ns['morph_polyhedron_1'], (4.5, 4.5, 4.5)))
    # a profile and a spline made the same way (profiles cut in half by the plane too: F41 -- the same rule, the points that have a partner are written as its image)

    def stroke(name, x):
        r = dict(kind='primitive', name=name, x=x, y=0, z=0, scale=10, indent=0, var=name + '_1', symmetry={'var': 'symmetry_1', 'centre': [0, 0, 0], 'elements': one})
        return mc.call(json.dumps(r))
    square = stroke('profile', 0)
    check('menu with a symmetry: a profile with its middle on the plane has two points and two images of them', square.count('= point(') == 2 and square.count('= symmetry_image(') == 2, (square.count('= point('), square.count('= symmetry_image(')))
    arc = stroke('spline', 0)
    check('menu with a symmetry: a spline with its middle on the plane has two points (one of them on the plane) and one image', arc.count('= point(') == 2 and arc.count('= symmetry_image(') == 1, (arc.count('= point('), arc.count('= symmetry_image(')))
    beside = stroke('profile', 30)
    check('menu with a symmetry: a profile beside the plane is mirrored as a whole, as it was (four points, an image of the stroke)', beside.count('= point(') == 4 and '_m1 = symmetry_image(' in beside, (beside.count('= point('), beside.count('= symmetry_image(')))
    script = square.replace('#@@#\n', 'profile_1 = ') + '\nprofile_1\n'
    ns = {'var': lambda v: v}
    exec('from fieldes import *\nsymmetry_1 = symmetry_planes(planes=1, kind=%r, axis=(0, 0, 1), first=(0, 1, 0))\n' % 'axis' + script, ns)
    check('... and the profile runs, each image the mirror image of its point', all(at(ns['profile_1_p%d' % b])[0] == -at(ns['profile_1_p%d' % a])[0] and at(ns['profile_1_p%d' % b])[1:] == at(ns['profile_1_p%d' % a])[1:] for a, b in ((1, 2), (3, 4))))


def example_file():
    # examples/38_morph_shapes.py: a body cut in half by a mirror plane (its right points the images of its left ones), and a second symmetry whose images are a mirrored clone of the whole body (the plane x = 48)
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'examples', '38_morph_shapes.py')
    ns = {'var': lambda v: v}
    exec(compile(open(path, encoding='utf-8').read(), path, 'exec'), ns)

    def at(p):
        return tuple(round(float(C.value_of(c)), 5) for c in p.xyz)
    body, clone = ns['body'], ns['body_clone']
    check('the example: each right point of the body is the mirror image of its left point',
          all(at(ns['body_p%d' % b])[0] == -at(ns['body_p%d' % a])[0] and at(ns['body_p%d' % b])[1:] == at(ns['body_p%d' % a])[1:] for a, b in ((1, 2), (3, 4), (5, 6), (7, 8))) and at(ns['body_p9'])[0] == 0.0)
    check('the example: the body has nine points and nine faces', len(body.points) == 9 and len(body._morph_faces) == 9)
    import random
    rng = random.Random(5)
    spots = [(rng.uniform(-30, 30), rng.uniform(-22, 22), rng.uniform(-16, 24)) for _ in range(60)]
    a = evaluate(body, spots)
    b = evaluate(body, [(-x, y, z) for x, y, z in spots])
    check('the example: the body has two identical sides (largest difference %.2g)' % max(abs(p - q) for p, q in zip(a, b)), max(abs(p - q) for p, q in zip(a, b)) < 1e-3)
    c = evaluate(clone, [(96.0 - x, y, z) for x, y, z in spots])
    check('the example: the clone is the whole body mirrored in the plane x = 48 (largest difference %.2g)' % max(abs(p - q) for p, q in zip(a, c)), max(abs(p - q) for p, q in zip(a, c)) < 1e-3
          and inside(body, (0, 0, 0)) and inside(clone, (96, 0, 0)) and not inside(clone, (0, 0, 0)))


run('box', box_like)
run('skewed', skewed)
run('body', body)
run('symmetric', symmetric_and_drawn)
run('spheres', spheres)
run('sphere strength', sphere_strength)
run('sphere ball', sphere_ball)
run('sphere flat faces', sphere_faces_flat)
run('errors', errors)
run('hull faces', hull_faces)
run('menu', menu)
run('insertion', insertion)
run('toggling', toggling)
run('cache', cache)
run('tangent planes', tangent_planes)
run('rotation', rotation_of_a_spline_point)
run('polyhedra', polyhedra)
run('symmetry menu', symmetry_menu)
run('symmetry link', symmetry_link)
run('points in place', points_in_place)
run('resolution', resolution_of_the_view)
run('example file', example_file)
print('=' * 70)
print('ALL CHECKS PASSED' if not bad else '%d check(s) failed %s' % (len(bad), bad), '    total %.1f s' % (time.time() - start))
sys.exit(1 if bad else 0)
