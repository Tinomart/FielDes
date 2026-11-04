'''
Checks of the image of a morph body in a symmetry (H70, 2026-11-02): symmetries._outline_image takes the surface of the body through the map of the image (morph.remake_mapped) instead of making the body again
through the images of its points.  The two must be the same surface.  Library checks, no window; the times printed are for information only (the window is the measure).
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import random
import time
import traceback

from fieldes import *
from fieldes.stdlib import symmetries

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def corners(lo, hi):
    return [(lo[0] if not i & 1 else hi[0], lo[1] if not i & 2 else hi[1], lo[2] if not i & 4 else hi[2]) for i in range(8)]


def make_body():
    pts = [point(position=c) for c in corners((0, 0, 0), (40, 30, 20))] + [point(position=(20, 15, 30)), point(position=(20, -6, 10))]
    return morph_body(pts)


def compare(label, sym, k, box):
    body = make_body()
    symmetries._FAST_BODY_IMAGE = False
    started = time.time()
    old = symmetry_image(body, sym, k)
    t_old = time.time() - started
    symmetries._FAST_BODY_IMAGE = True
    started = time.time()
    new = symmetry_image(body, sym, k)
    t_new = time.time() - started
    check('%s: the image is made through its points, and has a surface of its own' % label, getattr(new, '_morph_controls', None) is not None and getattr(new, '_remake_mapped', None) is not None)
    check('%s: the same number of points to press and drag' % label, len(new._ctrls) == len(old._ctrls), (len(new._ctrls), len(old._ctrls)))
    check('%s: the same faces' % label, sorted(tuple(sorted(f)) for f in new._morph_faces) == sorted(tuple(sorted(f)) for f in old._morph_faces))
    rng = random.Random(7)
    pts = [tuple(rng.uniform(box[0][i], box[1][i]) for i in range(3)) for _ in range(400)]
    a, b = evaluate(old, pts), evaluate(new, pts)
    size = max(box[1][i] - box[0][i] for i in range(3))
    worst = max(abs(x - y) for x, y in zip(a, b))
    sign = sum(1 for x, y in zip(a, b) if (x < 0) != (y < 0) and min(abs(x), abs(y)) > 0.02 * size)
    check('%s: the field of the fast image is the field of the image made again (largest difference %.4f of a size of %.1f)' % (label, worst, size), worst < 0.02 * size, worst)
    check('%s: inside and outside agree away from the surface' % label, sign == 0, sign)
    print('      (for information: %.0f ms made again, %.0f ms through the map)' % (t_old * 1000, t_new * 1000))


try:
    mirror = symmetry_planes(1, 'axis', (0, 0, 1), (0, 1, 0), position=(48, 0, 0))
    compare('a mirror image across x = 48', mirror, 1, ((50, -12, -8), (100, 42, 38)))
    turn = symmetry_planes(kind='turn', axis=(0, 0, 1), copies=2, position=(48, 0, 0))
    compare('a turn of 180 degrees about x = 48', turn, 1, ((50, -42, -8), (100, 12, 38)))
except Exception:
    traceback.print_exc()
    bad.append('an error')

print()
print('FAILED: %s' % bad if bad else 'ALL PASSED')
raise SystemExit(1 if bad else 0)
