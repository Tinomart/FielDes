# A gyroid sheet lattice with walls of 1.0 mm in a block 20 x 20 x 10, a wall rule of 1.6 mm: a surface that turns every way, with no flat face
# anywhere: every wall of the lattice, measured along its own normal at eight places on the surface, comes out 1.6 mm thick, and nothing is cut.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('x3_gyroid_walls: a gyroid sheet lattice with walls of 1.0 mm in a block 20 x 20 x 10, a wall rule of 1.6 mm: a surface that turns every way, with no flat face anywhere: every wall of the lattice, measured along its own normal at eight places on the surface, comes out 1.6 mm thick, and nothing is cut')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = lattice(box((0, 0, 0), (20, 20, 10)), cell=cell_periodic('gyroid'), cell_size=10, thickness=1.0)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=1.6)
lo, hi = (-2, -2, -2), (22, 22, 12)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
started = time.time()
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the tool ran in %.0f s' % (time.time() - started), True)
# THE ANSWER BY HAND: the wall of a sheet lattice is the distance between its two faces, along the normal: 1.0 now; the rule asks 1.6, so each face goes out by 0.3 and the wall is 1.6
lines = [((1.0, 3.0, 5.0), (19.0, 3.0, 5.0)), ((1.0, 9.0, 3.0), (19.0, 9.0, 3.0)), ((3.0, 1.0, 7.0), (3.0, 19.0, 7.0)), ((11.0, 1.0, 5.0), (11.0, 19.0, 5.0)), ((5.0, 5.0, 0.5), (5.0, 5.0, 9.5)), ((15.0, 14.0, 0.5), (15.0, 14.0, 9.5))]
pts = surface_points(part, lines, per_line=2)
t.check('%d points on the surface of the lattice were found to measure at' % len(pts), len(pts) >= 8, len(pts), '>= 8')
bad = []
for p in pts[:14]:
    w0, n = wall_through(part, p, part, limit=3.0)
    w1, _ = wall_through(res, p, part, limit=3.0)
    print('at %s: the wall was %.3f, now %.3f (normal %s)' % (tuple(round(c, 2) for c in p), w0, w1, tuple(round(c, 2) for c in n)))
    if not 1.5 <= w1 <= 1.75:
        bad.append((tuple(round(c, 1) for c in p), round(w0, 2), round(w1, 2)))
t.check('the wall along its normal is 1.6 mm (1.5 .. 1.75) at every one of the %d places' % min(len(pts), 14), not bad, bad, 'none off')
cmp_ = compare(part, res, lo, hi, n=240000)
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
t.check('no piece of added material floats beside the lattice', not floating_clusters(cmp_, cell=1.0), '', 0)
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
if not after.compliant:
    print(after)
t.check('the independent check of the result finds at most 1 %% of the surface under the wall rule (%.3g)' % after.report['min_wall']['share'], after.report['min_wall']['share'] <= 0.01, '%.3g' % after.report['min_wall']['share'], '<= 0.01')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
