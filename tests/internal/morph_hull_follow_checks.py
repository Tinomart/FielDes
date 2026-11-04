'''
Checks of the faces that follow the hull (H104, the user, 2026-11-03: a polyhedron's faces are made between the closest points, and a point pressed out of a face goes on being dragged): a body of up to
64 points whose points are all corners of their hull has the faces of the hull, whatever list of faces it was given; a body with a point inside the hull (a dent), and a body of more points (an import), keep
the faces they have.  Library checks, no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import sys
import time
import traceback

from fieldes import *
from fieldes.stdlib import morph as M

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def norm(face):
    face = tuple(face)
    k = face.index(min(face))
    return face[k:] + face[:k]


def faceset(faces):
    return set(norm(f) for f in faces)


def corners(lo, hi):
    return [(lo[0] if not i & 1 else hi[0], lo[1] if not i & 2 else hi[1], lo[2] if not i & 4 else hi[2]) for i in range(8)]


def closed(faces):
    directed = {}
    for f in faces:
        for k in range(len(f)):
            e = (f[k], f[(k + 1) % len(f)])
            directed[e] = directed.get(e, 0) + 1
    return all(n == 1 and (e[1], e[0]) in directed for e, n in directed.items())


BOX_FACES = [(0, 2, 3, 1), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]          # (the five squares that stay when the top is split round a ninth point)
SPLIT_TOP = [(4, 5, 8), (5, 7, 8), (7, 6, 8), (6, 4, 8)]                                   # (what pressing the top face leaves: the face split in four round the new point)


def follow():
    box = corners((-10, -10, -10), (10, 10, 10))
    out = box + [(22.0, 0.0, 14.0)]                      # (pulled out sideways beyond the top face, and up)
    hull, inner = M._hull(out)
    check('premise: the nine points are all corners of their hull', not inner, inner)
    given = BOX_FACES + SPLIT_TOP
    body = morph_polyhedron([point(position=c) for c in out], _faces=given)
    got = faceset(body._morph_faces)
    check('a point pulled out of a face: the faces are those of the hull (between the closest points)', got == faceset(M._triangulated(hull)), len(got))
    check('... not the faces that were given (the top face split round the point)', got != faceset(M._triangulated(given)))
    check('... and they are a closed surface', closed(body._morph_faces))
    # the same list, the point pressed into the body: a dent keeps the faces it has
    dent = box + [(0.0, 0.0, 6.0)]
    body = morph_polyhedron([point(position=c) for c in dent], _faces=given)
    check('a point pushed into the body (a dent) keeps the faces it was given', faceset(body._morph_faces) == faceset(M._triangulated(given)), len(body._morph_faces))
    # a point still in its face (just pressed): the hull splits the face in three -- the same surface as the split
    flat = box + [(0.0, 0.0, 10.0)]
    hull_flat, inner_flat = M._hull(flat)
    body = morph_polyhedron([point(position=c) for c in flat], _faces=BOX_FACES + [(4, 5, 8), (5, 7, 8), (7, 6, 8), (6, 4, 8)])
    check('a point just pressed (in its face): the faces are the hull of it, the face split round it', faceset(body._morph_faces) == faceset(M._triangulated(hull_flat)) and not inner_flat, len(body._morph_faces))
    # a body made of the hull's own faces is not changed
    plain = morph_polyhedron([point(position=c) for c in box])
    check('a box is a box', faceset(plain._morph_faces) == faceset(M._triangulated(M._hull(box)[0])), len(plain._morph_faces))


def big_and_cost():
    import math
    import random
    random.seed(7)

    def sphere(n):
        pts = []
        for _ in range(n):
            v = [random.gauss(0, 1) for _ in range(3)]
            l = math.sqrt(sum(x * x for x in v))
            pts.append(tuple(30.0 * x / l for x in v))
        return pts

    seen = []
    original = M._hull

    def spy(pos):
        seen.append(len(pos))
        return original(pos)

    M._hull = spy
    try:
        points = sphere(70)
        faces, inner = original(points)
        body = morph_polyhedron([point(position=c) for c in points], _faces=faces)
        check('seventy points keep the faces they were given: no hull was made for them', not seen, seen)
        check('... the faces are the given ones', faceset(body._morph_faces) == faceset(M._triangulated(faces)))
        seen[:] = []
        points = sphere(60)
        faces, inner = original(points)
        t0 = time.time()
        body = morph_polyhedron([point(position=c) for c in points], _faces=faces)
        took = time.time() - t0
        check('sixty points: the hull was made once for them', seen == [60], seen)
        t0 = time.time()
        original(points)
        cost = time.time() - t0
        check('the hull of sixty points takes under a quarter of a second (%.3f s; the whole body %.2f s)' % (cost, took), cost < 0.25, round(cost, 3))
    finally:
        M._hull = original


def run(name, fn):
    try:
        fn()
    except Exception:
        traceback.print_exc()
        bad.append(name + ' raised')


run('follow', follow)
run('big and cost', big_and_cost)
print('=' * 70)
print('ALL CHECKS PASSED' if not bad else '%d check(s) failed %s' % (len(bad), bad))
sys.exit(1 if bad else 0)
