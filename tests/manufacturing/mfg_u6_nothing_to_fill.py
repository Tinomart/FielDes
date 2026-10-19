# Three parts with nothing out of reach along z -- a blind pocket from the top, a hole along z, a ledge with air below -- and an undercut along z
# asked: nothing may change and the tool reports nothing.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - undercut: _remedy_undercut (what lies under and over the part along the direction is filled)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('u6_nothing_to_fill: three parts with nothing out of reach along z -- a blind pocket from the top, a hole along z, a ledge with air below -- and an undercut along z asked: nothing may change and the tool reports nothing')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
pocket = difference(box((0, 0, 0), (16, 16, 16)), box((4, 4, 6), (12, 12, 17)))
along = difference(box((22, 0, 0), (38, 16, 16)), cylinder(3.0, 18, (30, 8, -1)))
ledge = union(box((44, 0, 0), (50, 16, 16)), box((44, 0, 10), (60, 16, 16)))
part = union(union(pocket, along), ledge)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(undercut_along=(0, 0, 1))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0, verify=True)
lo, hi = (-3, -3, -3), (63, 19, 19)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
print(res)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
a, r, _ = changed_volumes(part, res, lo, hi)
t.check('nothing added, nothing cut (+%.3g -%.3g mm3)' % (a, r), a < 1.5 and r < 1.5, '+%.3g -%.3g' % (a, r), 0)
t.check('the tool lists nothing as broken', not res.fail_states, [s['rule'] for s in res.fail_states], 'none')
after = manufacturing_analysis(part, constraints=[rule], samples=8000)
t.check('the independent check of the part finds no undercut either', after.compliant, '%.3g' % after.report['undercut_along']['share'], 'none')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
