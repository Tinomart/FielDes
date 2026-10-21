# A block 10 x 10 x 10 for a draft of 2 degrees with the mould opening ALONG X (not z): the faces across y and z lean, their width grows toward the
# foot (x = 0), the two faces across x stay where they were.
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
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('d3_pull_along_x: a block 10 x 10 x 10 for a draft of 2 degrees with the mould opening ALONG X (not z): the faces across y and z lean, their width grows toward the foot (x = 0), the two faces across x stay where they were')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = box((0, 0, 0), (10, 10, 10))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(draft=2.0, pull_direction=(1, 0, 0))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-3, -3, -3), (13, 13, 13)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
g = math.tan(math.radians(2.0))
print(res)
# THE ANSWER BY HAND: the top of the part along the pull is x = 10; a face across y at x is offset outwards by (10 - x) tan 2 degrees, so the width across y is 10 + 2 (10 - x) tan 2 degrees
for x in (0.4, 3.0, 6.0, 9.6):
    behind, ahead = run_length(res, (x, 5, 5), (0, 1, 0), 9.0, 6000)
    want = 10.0 + 2.0 * (10.0 - x) * g
    t.near('the width across y at x = %g mm (by hand %.3f)' % (x, want), behind + ahead, want, 0.06, ' mm')
    behind, ahead = run_length(res, (x, 5, 5), (0, 0, 1), 9.0, 6000)
    t.near('the width across z at x = %g mm (the same: %.3f)' % (x, want), behind + ahead, want, 0.06, ' mm')
ends_in, ends_out = evaluate(res, [(0.1, 5, 5), (9.9, 5, 5)]), evaluate(res, [(-0.1, 5, 5), (10.1, 5, 5)])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the two faces across x have not moved (material at 0.1 and 9.9, air at -0.1 and 10.1)', all(v <= 0 for v in ends_in) and all(v > 0 for v in ends_out), '%s / %s' % (ends_in, ends_out), '')
residue_check(t, 'block', part, res, [rule], samples=6000)

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
