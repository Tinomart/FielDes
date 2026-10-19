# A flat roof across a 30 mm gap, a longest bridge of 20 mm asked: it is reported, not repaired.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - a bridge is only reported: there is no remedy for it
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('o8_bridge: a flat roof across a 30 mm gap, a longest bridge of 20 mm asked: it is reported, not repaired')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(union(box((0, 0, 0), (5, 10, 20)), box((35, 0, 0), (40, 10, 20))), box((0, 0, 20), (40, 10, 23)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(max_bridge=20.0)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=0.5, verify=True)
lo, hi = (-3, -3, -3), (43, 13, 26)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
a, r, _ = changed_volumes(part, res, lo, hi)
t.check('bridge: nothing was changed (a bridge is not repaired)', a < 1.5 and r < 1.5, '+%.3g -%.3g mm3' % (a, r), '0')
t.check('bridge: the tool reports the bridge as still broken', any(s['rule'] == 'max_bridge' for s in res.fail_states), [s['rule'] for s in res.fail_states], 'max_bridge')
after = manufacturing_analysis(part, constraints=[rule], samples=5000)
share = after.report['max_bridge']['share']
t.check('bridge: the independent check of the part finds the roof (some of the surface)', share > 0.02, '%.3g of the surface' % share, '> 0.02')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
