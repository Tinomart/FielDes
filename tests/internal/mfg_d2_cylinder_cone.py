# A vertical cylinder of radius 5 and height 20 for a draft of 2 degrees: the wall becomes a cone, the radius at each height is 5 + (20 - z) tan 2
# degrees and the top is as it was.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - draft: _remedy_draft (a wall that leans too little is offset in proportion to its depth)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('d2_cylinder_cone: a vertical cylinder of radius 5 and height 20 for a draft of 2 degrees: the wall becomes a cone, the radius at each height is 5 + (20 - z) tan 2 degrees and the top is as it was')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = cylinder(5.0, 20.0, (0, 0, 0))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(draft=2.0)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-9, -9, -3), (9, 9, 23)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
g = math.tan(math.radians(2.0))
print(res)
# THE ANSWER BY HAND: a wall that stands at 0 degrees from the pull must lean 2 degrees: it is offset outwards by (20 - z) tan 2 degrees, so the radius at height z is 5 + (20 - z) x 0.03492
for z in (0.4, 5.0, 10.0, 15.0, 19.6):
    behind, ahead = run_length(res, (0, 0, z), (1, 0, 0), 9.0, 6000)
    want = 5.0 + (20.0 - z) * g
    t.near('the radius at z = %g mm (by hand %.3f)' % (z, want), ahead, want, 0.05, ' mm')
top_in, top_out = evaluate(res, [(4.8, 0, 19.9), (5.3, 0, 19.9)])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the top is as it was: at z = 19.9 the radius is 5, not more (inside at 4.8, outside at 5.3)', top_in <= 0 < top_out, '%.3f / %.3f' % (top_in, top_out), '<= 0 / > 0')
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
if not after.compliant:
    print(after)
t.check('the independent check of the result finds no wall without draft (%.3g of the surface)' % after.report['draft']['share'], after.compliant, '%.3g' % after.report['draft']['share'], 'none')
cmp_ = compare(part, res, lo, hi, n=200000)
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
