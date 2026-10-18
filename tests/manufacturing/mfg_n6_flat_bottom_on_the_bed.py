# A block 10 x 10 x 10 standing on the print bed with an overhang of 45 asked: its flat bottom is the first layer, not an overhang, and nothing may
# change.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('n6_flat_bottom_on_the_bed: a block 10 x 10 x 10 standing on the print bed with an overhang of 45 asked: its flat bottom is the first layer, not an overhang, and nothing may change')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = box((0, 0, 0), (10, 10, 10))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-3, -3, -3), (13, 13, 13)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
print(res)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
a, r, _ = changed_volumes(part, res, lo, hi)
t.check('nothing added, nothing cut (+%.3g -%.3g mm3)' % (a, r), a < 1.5 and r < 1.5, '+%.3g -%.3g' % (a, r), 0)
t.check('the tool lists nothing as broken', not res.fail_states, [s['rule'] for s in res.fail_states], 'none')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
