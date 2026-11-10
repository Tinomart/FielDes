'''
Checks of the faces of a body of points (H164): they are made of the points at every run (see morph_faces), follow every edit, and a part of the body far from an edit keeps the faces it had.  Library checks, no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/ (not with -I).
'''
import math
import sys
import traceback

from fieldes import *
from fieldes.stdlib import morph as M

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def corners(lo, hi):
    return [(lo[0] if not i & 1 else hi[0], lo[1] if not i & 2 else hi[1], lo[2] if not i & 4 else hi[2]) for i in range(8)]


def norm(face):
    face = tuple(face)
    k = face.index(min(face))
    return face[k:] + face[:k]


def closed(faces):
    directed = {}
    for f in faces:
        for k in range(len(f)):
            e = (f[k], f[(k + 1) % len(f)])
            directed[e] = directed.get(e, 0) + 1
    return all(n == 1 and (e[1], e[0]) in directed for e, n in directed.items())


def sizes(faces):
    return sorted(len(f) for f in faces)


BOX = corners((-10, -10, -10), (10, 10, 10))


def box_and_press():
    check('a box is its six squares, a closed surface', sizes(M._default_faces('body', BOX)) == [4] * 6 and closed(M._default_faces('body', BOX)))
    # a ninth point pressed anywhere on the top face (the middle, near the diagonal, near an edge) splits it in four round the point, the other five faces stay squares
    for label, place in (('the middle', (0.0, 0.0, 10.0)), ('near the diagonal', (3.0, 2.5, 10.0)), ('near an edge', (8.0, 0.5, 10.0)), ('a corner of the face', (9.0, 9.0, 10.0))):
        faces = M._default_faces('body', BOX + [place])
        check('a point pressed in the top face (%s): the face split in four round it, the other five faces stay squares, a closed surface' % label, sizes(faces) == [3, 3, 3, 3, 4, 4, 4, 4, 4] and closed(faces), sizes(faces))
    for label, place in (('pulled 5 mm out', (3.0, 2.5, 15.0)), ('pushed 4 mm in (a dent)', (3.0, 2.5, 6.0))):
        faces = M._default_faces('body', BOX + [place])
        check('a point %s: the same faces, the face split in four round it, a closed surface' % label, sizes(faces) == [3, 3, 3, 3, 4, 4, 4, 4, 4] and closed(faces), sizes(faces))
    # one corner pulled far out: the basic body keeps its six squares (one of them bent), as it always had
    far = list(BOX)
    far[7] = (24.0, 22.0, 8.0)
    faces = M._default_faces('body', far)
    check('one corner of the box pulled far out: still six faces, a closed surface', len(faces) == 6 and closed(faces), sizes(faces))


def errors():
    def refused(label, points, word):
        try:
            M._default_faces('body', points)
            check(label, False)
        except ValueError as e:
            check('%s: %s' % (label, str(e)[:80]), word in str(e), str(e))
    refused('points in a line are an error', [(i, 0, 0) for i in range(7)], 'line')
    refused('four points in one plane are an error', [(0, 0, 0), (4, 0, 0), (4, 3, 0), (0, 3, 0)], 'plane')


def locality():
    # a bar of 24 points whose rings are slightly irregular: moving a corner at one end changes no face at the other end, and the faces never join points that are far apart
    bar = []
    for ix, x in enumerate((0.0, 5.0, 10.0, 15.0, 20.0, 25.0)):
        for iy, (y, z) in enumerate(((-2.5, -2.5), (2.5, -2.5), (2.5, 2.5), (-2.5, 2.5))):
            j = math.sin(12.9898 * (ix * 4 + iy + 1)) * 0.35
            bar.append((x + 0.4 * math.sin(78.233 * (ix * 4 + iy + 1)), y * (1 + 0.1 * j), z * (1 + 0.1 * math.cos(37.719 * (ix * 4 + iy + 1))) + j))
    moved = list(bar)
    moved[2] = (bar[2][0] - 1.5, bar[2][1] + 1.0, bar[2][2] + 1.0)
    a = set(norm(f) for f in M._default_faces('body', bar))
    b = set(norm(f) for f in M._default_faces('body', moved))
    changed = a ^ b
    check('a bar of 24 points, an end corner moved: no face changes at the other end', all(min(bar[v][0] for v in f) < 12.0 for f in changed), len(changed))
    longest = max(max(math.dist(bar[f[i]], bar[f[(i + 1) % len(f)]]) for i in range(len(f))) for f in a)
    check('... and no face of the bar joins points far apart (the longest edge of a face is %.1f, the bar is 25 long)' % longest, longest < 9.0, round(longest, 1))
    check('... a closed surface', closed([tuple(f) for f in M._default_faces('body', bar)]))


def many_points():
    import random
    random.seed(3)
    pts = []
    for _ in range(70):
        v = [random.gauss(0, 1) for _ in range(3)]
        l = math.sqrt(sum(x * x for x in v))
        pts.append(tuple(30.0 * x / l for x in v))
    given = [(0, 1, 2)]
    check('a body of more than %d points keeps the faces it was given' % M._FOLLOW_LIMIT, M._faces_between_closest_points(given, pts) is given)


def repair():
    # a sliver: the long edge of two triangles with a corner of 157 degrees (the point 2 is almost on the edge 0 - 1) is flipped to the edge 2 - 3 (the faces of a thin horn, H170)
    places = [(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (2.0, 0.4, 0.0), (2.0, -1.5, 0.1)]
    flipped = M._slivers_flipped([(0, 1, 2), (1, 0, 3)], places)
    check('a triangle with a corner of 157 degrees: its long edge is flipped (both faces now have the points 2 and 3)', all(2 in f and 3 in f for f in flipped) and len(flipped) == 2, flipped)
    # the two right triangles of a square are not slivers: nothing moves, and they are not a needle: they stay two triangles
    square = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
    check('the two triangles of a square are left alone by the flips', [tuple(f) for f in M._slivers_flipped([(0, 1, 2), (0, 2, 3)], square)] == [(0, 1, 2), (0, 2, 3)])
    check('... and are not made one quad (neither is a needle)', sorted(len(f) for f in M._flat_pairs_merged([(0, 1, 2), (0, 2, 3)], square)) == [3, 3])
    # a flat rectangle of 4 x 1 cut across (a corner of 14 degrees): the two needles are one quad
    rect = [(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (4.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
    merged = M._flat_pairs_merged([(0, 1, 2), (0, 2, 3)], rect)
    check('two needles that make a flat rectangle are one quad of the four points', len(merged) == 1 and sorted(merged[0]) == [0, 1, 2, 3], merged)
    # the same pair folded along the diagonal by 82 degrees is not made a quad
    folded = [(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (4.0, 1.0, 0.0), (0.0, 0.3, 2.0)]
    check('the same two triangles folded by 82 degrees along the diagonal stay two triangles', sorted(len(f) for f in M._flat_pairs_merged([(0, 1, 2), (0, 2, 3)], folded)) == [3, 3])


def sphere_points(n, radius=10.0):
    golden = math.pi * (3.0 - math.sqrt(5.0))
    out = []
    for i in range(n):
        y = 1.0 - 2.0 * (i + 0.5) / n
        ring = math.sqrt(1.0 - y * y)
        out.append((radius * ring * math.cos(golden * i), radius * y, radius * ring * math.sin(golden * i)))
    return out


def same_faces(a, b):
    return sorted(norm(f) for f in a) == sorted(norm(f) for f in b)


def function_of_the_points():
    ''' H171 (the user, 2026-11-11): "The face is deterministic always and is exactly what it would be when you run the code fresh.  Otherwise the user works on something closes and opens their file and it is completely different".  The faces of a run
        are the faces its points make by themselves whatever the runs before were: a point pressed out on one side, deleted, and a new point pressed out on the other side was given the faces of the first (joined to its neighbours through the body) '''
    pts = sphere_points(20)
    e = (14.0, 0.5, 0.5)
    f = (-14.0, 0.5, 0.5)
    M._FACES_MADE.clear()
    runs = [pts + [e], pts, pts + [f], pts + [e], pts + [f], pts]
    for k, places in enumerate(runs):
        got = M._faces_of_the_places('morph_body', places)
        check('run %d of press / delete / press on the opposite side: the faces are those the points make by themselves' % (k + 1), same_faces(got, M._default_faces('morph_body', places)))
        check('run %d: the faces do not pass through each other' % (k + 1), not M._cage_crosses(got, places))
    far = [v for face in M._faces_of_the_places('morph_body', pts + [f]) if 20 in face for v in face if v != 20]
    check('the new point on the far side (x = -14) is joined only to points on its own side of the body (x < 2), none of the other side where the first point was', all(pts[v][0] < 2.0 for v in far), [(v, round(pts[v][0], 1)) for v in far])
    # a long drag of one point (every frame is a run) and the run after its release: all the same as the faces of the points
    ok = True
    for step in range(14):
        moved = list(pts)
        moved[3] = (pts[3][0] * (1.0 + 0.1 * step), pts[3][1] * (1.0 + 0.1 * step), pts[3][2] * (1.0 + 0.1 * step))
        ok = ok and same_faces(M._faces_of_the_places('morph_body', moved), M._default_faces('morph_body', moved))
    check('the 14 frames of a drag and the run after it are each the faces of the points', ok)
    check('the memory holds no more than its limit', len(M._FACES_MADE) <= M._FACES_MADE_MOST, len(M._FACES_MADE))


def deleting_a_point():
    ''' H171: the faces are healed when a point is deleted -- the hole is closed by faces of points that are near each other, never by a fan from whichever point comes first, and the faces that are left never pass through each other '''
    pts = sphere_points(24)
    faces = [tuple(x) for x in M._default_faces('morph_body', pts)]
    longest = max(math.dist(pts[face[k]], pts[face[(k + 1) % len(face)]]) for face in faces for k in range(len(face)))
    none, bad_ones, far = 0, [], []
    for v in range(len(pts)):
        filled = M._fill_holes(faces, {v}, pts)
        if filled is None:
            none += 1
            continue
        if any(v in face for face in filled) or M._cage_crosses(filled, pts) or not closed(filled):
            bad_ones.append(v)
        news = [face for face in filled if face not in faces]
        if any(math.dist(pts[face[k]], pts[face[(k + 1) % len(face)]]) > 2.0 * longest for face in news for k in range(len(face))):
            far.append(v)
    check('24 points of a sphere: deleting any one gives faces that name no deleted point, do not cross and are a closed surface (or none, then the points\' own)', not bad_ones, bad_ones)
    check('no hole is closed by an edge across the body (longer than twice the longest edge of the body)', not far, far)
    check('a hole is found a filling in at least 20 of the 24 deletions', none <= 4, none)
    # five deletions in a row on the faces that are left
    cur, gone = faces, set()
    for v in (3, 11, 17, 5, 20):
        filled = M._fill_holes(cur, {v}, pts)
        gone.add(v)
        if filled is None:
            break
        cur = filled
    check('five deletions in a row: the faces that are left do not cross', M._cage_crosses(cur, pts) is False)


def run(name, fn):
    try:
        fn()
    except Exception:
        traceback.print_exc()
        bad.append(name + ' raised')


run('function of the points', function_of_the_points)
run('deleting a point', deleting_a_point)
run('box and press', box_and_press)
run('repair', repair)
run('errors', errors)
run('locality', locality)
run('many points', many_points)
print('=' * 70)
print('ALL CHECKS PASSED' if not bad else '%d check(s) failed %s' % (len(bad), bad))
sys.exit(1 if bad else 0)
