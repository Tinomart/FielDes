# The judge of the job tests tried on parts whose answer is known -- a slab 0.6 mm thick, a block, a ledge with a flat underside, a wall that is
# vertical and a cone that is not, a block with a side groove, an inside corner -- before it is trusted to judge a job.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - draft: _remedy_draft (a wall that leans too little is offset in proportion to its depth)
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('j0_the_judge_on_known_parts: the judge of the job tests tried on parts whose answer is known -- a slab 0.6 mm thick, a block, a ledge with a flat underside, a wall that is vertical and a cone that is not, a block with a side groove, an inside corner -- before it is trusted to judge a job')

lo_, hi_ = (-2, -2, -2), (32, 32, 32)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo_, hi_, res=2)
# 1. a slab 30 x 30 x 0.6: both big faces are thin for a wall rule of 0.8, the four edges are not
slab = box((0, 0, 0), (30, 30, 0.6))
pts, nrm = judge_surface_points(slab, (-1, -1, -1), (31, 31, 2), 1500, seed=3)
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
th = judge_thickness(slab, pts, nrm, 0.8)
thin = [1 if x < 0.76 else 0 for x in th]
big = [x for x, n in zip(th, nrm) if abs(n[2]) > 0.9]
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('slab: %d points found on the surface' % len(pts), len(pts) >= 1200, len(pts), '>= 1200')
# (0.9615 by hand, less the strip along the edges that the thickness reads as thicker than the slab: 0.4 mm wide round the 120 mm of rim on both faces = 96 mm2 = 0.051 of the surface, so 0.910 by hand; the proofs' README says the same)
t.between('slab: the share of the surface under 0.76 mm: the two big faces are 1800 of 1872 mm2, 0.9615 by hand, 0.910 without the 0.4 mm strip along the rim', judge_share(thin), 0.89, 0.97, '')
t.near('slab: the thickness read on the big faces (median) is 0.6', sorted(big)[len(big) // 2], 0.6, 0.03, ' mm')
# 2. a block 10 mm each way: nothing is thin
block = box((0, 0, 0), (10, 10, 10))
pts, nrm = judge_surface_points(block, (-1, -1, -1), (11, 11, 11), 800, seed=4)
share = judge_share([1 if x < 0.76 else 0 for x in judge_thickness(block, pts, nrm, 0.8)])
t.check('block: nothing under 0.76 mm (%.3g of its surface)' % share, share == 0.0, '%.3g' % share, 0)
# 3. a wall 10 x 10 x 20 with a ledge 15 x 10 x 3 whose flat underside is at z = 12: the underside is an overhang of 90 degrees (150 mm2 of 1390 mm2), the foot of the wall on the plate is not
ledge = union(box((0, 0, 0), (10, 10, 20)), box((10, 0, 12), (25, 10, 15)))
pts, nrm = judge_surface_points(ledge, (-1, -1, -1), (26, 11, 21), 2000, seed=5)
flags = judge_overhang(pts, nrm, (0, 0, 1), 45.0)
t.between('ledge: the share that is an overhang of more than 45 degrees (the underside, 150 of 1390 mm2: 0.108 by hand)', judge_share(flags), 0.07, 0.15, '')
at_foot = [f for p, f in zip(pts, flags) if p[2] < 0.6]
t.check('ledge: the foot of the wall on the plate is not an overhang (%d points near the plate, %d flagged)' % (len(at_foot), sum(at_foot)), sum(at_foot) == 0, sum(at_foot), 0)
# 4. draft: a vertical cylinder wall is all under the draft, a cone leans 25.6 degrees and is not
post = cylinder(5.0, 20.0, (10, 10, 0))
pts, nrm = judge_surface_points(post, (4, 4, -1), (16, 16, 21), 1200, seed=6)
walls, bad = judge_draft(nrm, (0, 0, 1), 1.0)
# (a point of the sample that lies ON the rim edge has a normal averaged across the edge: one in a thousand may lean more than the wall does)
t.check('post (a vertical wall): every wall-like point but the odd one on a rim edge leans less than 1 degree (%d of %d)' % (bad, walls), walls > 500 and bad >= 0.995 * walls, '%d of %d' % (bad, walls), 'at least 99.5 %')
horn = cone(12.0, 25.0, (15, 15, 0))
pts, nrm = judge_surface_points(horn, (2, 2, -1), (28, 28, 26), 1200, seed=7)
walls, bad = judge_draft(nrm, (0, 0, 1), 1.0)
t.check('cone (a wall that leans 25.6 degrees): no wall-like point but the odd one on a rim edge leans less than 1 degree (%d of %d)' % (bad, walls), walls > 500 and bad <= 0.005 * walls, '%d of %d' % (bad, walls), 'at most 0.5 %')
# 5. a groove 20 wide, 6 high and 8 deep in the side of a block of 30: every face of the groove is out of reach along z, nothing else is
grooved = difference(box((0, 0, 0), (30, 30, 30)), box((-1, 5, 12), (8, 25, 18)))
pts, nrm = judge_surface_points(grooved, (-1, -1, -1), (31, 31, 31), 2500, seed=8)
cut = judge_undercut(grooved, pts, nrm, (0, 0, 1), 40.0)
t.between('grooved block: the share of the surface out of reach along z (the groove, 536 of 5816 mm2: 0.092 by hand)', judge_share(cut), 0.06, 0.13, '')
outside = [c for p, c in zip(pts, cut) if not (-0.5 <= p[0] <= 8.5 and 4.5 <= p[1] <= 25.5 and 11.5 <= p[2] <= 18.5)]
t.check('grooved block: no point outside the groove is flagged (%d flagged of %d)' % (sum(outside), len(outside)), sum(outside) == 0, sum(outside), 0)
# 6. an inside corner (an L of two walls 3 thick and 20 high): a ball of 3 cannot be put against the faces within 3 mm of the corner, and can against the rest
ell = union(box((0, 0, 0), (30, 3, 20)), box((0, 0, 0), (3, 30, 20)))
pts, nrm = judge_surface_points(ell, (-1, -1, -1), (31, 31, 21), 2500, seed=9)
reach = judge_reach(ell, pts, nrm, 3.0)
close = [r for p, r in zip(pts, reach) if 3.0 < p[2] < 17.0 and ((abs(p[1] - 3.0) < 0.05 and 3.0 < p[0] < 5.5) or (abs(p[0] - 3.0) < 0.05 and 3.0 < p[1] < 5.5))]
far = [r for p, r in zip(pts, reach) if 3.0 < p[2] < 17.0 and ((abs(p[1] - 3.0) < 0.05 and p[0] > 9.0) or (abs(p[0] - 3.0) < 0.05 and p[1] > 9.0))]
t.check('L: the points of the inner faces within 2.5 mm of the corner reach less than a ball of 3 (%d points, the largest reach %.2f)' % (len(close), max(close, default=0.0)), len(close) > 15 and max(close) < 2.8, '%d, %.2f' % (len(close), max(close, default=0.0)), 'reach < 2.8')
t.check('L: the points of the inner faces far from the corner reach all of it (%d points, the smallest reach %.2f)' % (len(far), min(far, default=0.0)), len(far) > 100 and min(far) >= 2.9, '%d, %.2f' % (len(far), min(far, default=0.0)), 'reach 3')

t.summary()
result = slab
result
