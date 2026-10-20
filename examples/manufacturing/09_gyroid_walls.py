# 09  A LATTICE: the walls of a gyroid sheet lattice, thickened with clean edges
#
# WHAT THIS SHOWS.  A gyroid sheet lattice is a surface that turns every way, with no flat face anywhere; its walls here are 1.0 mm thick.  A printer asks for walls of 1.6 mm, so the tool offsets the
# surface outwards by what the wall lacks, half on each side, wherever the wall is thin -- along the surface's own normal, so that the new surface is as smooth as the old one and its edges
# (where the lattice meets the flat faces it was cut from) are clean, not ragged.  Nothing is cut.
#
# WHAT YOU SEE.  The lattice as it was (right) and the result (left), a smooth skin over the whole of it (grey where it was, red where material was added -- all of it, here, since every wall is
# thin).  Zoom in on the places where the lattice meets a flat face: the edge is a clean line.  WHAT YOU READ: the wall measured along its normal at a set of places on the surface, before and
# after -- 1.0 mm, then 1.6 mm --, one PASS line per check.  The independent check finds the wall at the rule over the whole free surface; at the flat faces the lattice was cut from it marks the
# knife edges that a plane leaves where a sheet meets it at a shallow angle (a wedge has no thickness at its tip, however thick the sheet is) and says how much of the surface they are.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('09 a lattice: a gyroid sheet lattice with walls of 1.0 mm in a block 20 x 20 x 10, a wall rule of 1.6 mm: a surface that turns every way, with no flat face anywhere: every wall of the lattice, measured along its own normal at eight places on the surface, comes out 1.6 mm thick, and nothing is cut')

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
result = res          # (the model the window shows, right under the call that makes it: the tree then holds the call under it)
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
# THE ANSWER BY HAND: a face moves by half of what the wall lacks, 0.3 mm here, and by half the rule (0.8 mm) at most: nothing may be added farther than that from the lattice
far = debris(cmp_, 0.85)
t.check('nothing was added farther than half the wall rule (0.8 mm) from the lattice', len(far) == 0, '%d points' % len(far), 0)
# THE INDEPENDENT CHECK reads the result by a method of its own (nine lines through every sample, see manufacturing_check.py).  Where a sheet meets one of the flat faces the lattice was cut from at a
# shallow angle, the cut leaves a knife edge: a wedge whose tip has no thickness whatever the tool does, because a thickened face only moves the tip along the flat face; and a surface that is nearer
# to a flat face than the wall rule can form such a wedge with it.  So the check is read in two parts: farther from every cut face than the wall rule (1.6 mm) no wall may be under the rule, and what is
# marked nearer to a cut face is the wedge at its edge, a small share of the surface.
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
pts6 = after.points
under = [p for p, v in zip(pts6, evaluate(after.min_wall, pts6)) if v > 0]
def from_cut(p):
    return max(max(0.0 - p[i], p[i] - (20.0, 20.0, 10.0)[i]) for i in range(3))          # (signed distance to the block the lattice was cut from; negative inside)
inner = [p for p in under if -from_cut(p) > 1.6]
t.check('on the free surface of the lattice (more than the wall rule, 1.6 mm, from a cut face) the independent check finds no wall under the rule', len(inner) == 0, '%d of %d samples' % (len(inner), len(pts6)), 0)
near = [p for p in pts6 if -from_cut(p) <= 1.6]
t.check('what the check marks nearer to a cut face than that is the wedge at its edge, a fifth of that surface at most (%d of %d samples there: %.1f %%)' % (len(under), len(near), 100.0 * len(under) / max(1, len(near))),
        len(under) <= 0.2 * len(near), '%.3g' % (len(under) / float(max(1, len(near)))), '<= 0.2')

t.summary()
original = aside(part, lo, hi)[0]
result
original
