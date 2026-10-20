# A plate on a post (the post under it), the way up of the print not given: the tool finds that the plate has to be at the bottom and turns the part.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                      - the way up: _direction_of (the one that leaves the least of the surface leaning out)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('b1_build_direction_free: a plate on a post (the post under it), the way up of the print not given: the tool finds that the plate has to be at the bottom and turns the part')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
post = box((0, 0, 0), (10, 10, 30))
plate = box((-20, -20, 30), (30, 30, 35))
part = union(post, plate)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
free = manufacturing_constraint(overhang=45, build_direction=None)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[free], max_change=0.9)
lo, hi = (-23, -23, -38), (33, 33, 38)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
d = res.build_direction
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the way up found is (0, 0, -1)', abs(d[0]) < 0.05 and abs(d[1]) < 0.05 and d[2] < -0.95, tuple(round(c, 3) for c in d), '(0, 0, -1)')
a, r, _ = changed_volumes(part, res, (-23, -23, -3), (33, 33, 38))
t.check('turned the right way up there is no overhang: nothing added, nothing cut', a < 2.0 and r < 2.0, '+%.3g -%.3g mm3' % (a, r), 0)
turned = res.oriented()
pts = points_in((-40, -40, -32.5), (40, 40, -32.5), 60000, seed=4)
area_plate = 6400.0 * sum(1 for v in evaluate(turned, pts) if v <= 0) / 60000.0
pts = points_in((-40, -40, -10), (40, 40, -10), 60000, seed=5)
area_post = 6400.0 * sum(1 for v in evaluate(turned, pts) if v <= 0) / 60000.0
t.near('the turned part: the cross-section at the bottom (z = -32.5) is the plate, 50 x 50', area_plate, 2500.0, 200.0, ' mm2')
t.near('the turned part: the cross-section higher up (z = -10) is the post, 10 x 10', area_post, 100.0, 40.0, ' mm2')
pts = points_in((-40, -40, -34.9), (40, 40, -34.9), 60000, seed=6)
area_floor = 6400.0 * sum(1 for v in evaluate(turned, pts) if v <= 0) / 60000.0
t.near('the turned part: the lowest layer (z = -34.9) is the whole plate, 50 x 50: it stands on its plate', area_floor, 2500.0, 200.0, ' mm2')

t.summary()
result = turned
original = aside(part, lo, hi)[0]
result
original
