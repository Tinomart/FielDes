# A block 10 x 10 x 200 for a draft of 2 degrees (the growth at the foot would be 7 mm, 140 % of the part) with the default budget: the tool refuses,
# says why with the numbers, and the part comes out exactly as it was.
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

t = Table('d6_draft_beyond_the_budget: a block 10 x 10 x 200 for a draft of 2 degrees (the growth at the foot would be 7 mm, 140 % of the part) with the default budget: the tool refuses, says why with the numbers, and the part comes out exactly as it was')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = box((0, 0, 0), (10, 10, 200))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(draft=2.0)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], verify=True)
lo, hi = (-8, -8, -3), (18, 18, 203)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=1)
print(res)
c = [c for c in res.changes if c['remedy'] == 'draft']
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the draft remedy is not applied', bool(c) and not c[0]['kept'], c[0]['kept'] if c else None, False)
t.check('the reason names the percentage of the part and the budget (%s)' % (c[0]['why'] if c else ''), bool(c) and '%' in c[0]['why'] and '25' in c[0]['why'], c[0]['why'] if c else '', 'the share and the 25 % allowed')
a, r, _ = changed_volumes(part, res, lo, hi)
t.check('the part is exactly as it was (+%.3g -%.3g mm3)' % (a, r), a < 1.5 and r < 1.5, '+%.3g -%.3g' % (a, r), 0)
t.check('the tool lists the wall without draft as still broken', any(s['rule'] == 'draft' for s in res.fail_states), [s['rule'] for s in res.fail_states], 'draft')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
