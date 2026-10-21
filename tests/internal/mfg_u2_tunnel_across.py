# A tunnel 6 x 6 mm across a block of 20 (along x), an undercut along z asked: a tunnel across the way a mould opens cannot be moulded, it is filled
# whole.
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

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('u2_tunnel_across: a tunnel 6 x 6 mm across a block of 20 (along x), an undercut along z asked: a tunnel across the way a mould opens cannot be moulded, it is filled whole')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = difference(box((0, 0, 0), (20, 20, 20)), box((-1, 7, 7), (21, 13, 13)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(undercut_along=(0, 0, 1))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-3, -3, -3), (23, 23, 23)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
print(res)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added=6 * 6 * 20, tol=30.0, within=((-0.2, 6.8, 6.8), (20.2, 13.2, 13.2)), name='tunnel')
inside = evaluate(res, [(10, 10, 10), (1, 8, 8), (19, 12, 12), (10, 7.2, 12.8)])
t.check('tunnel: material in the middle of the tunnel, at its two ends and at a corner', all(v <= 0 for v in inside), [round(v, 3) for v in inside], '<= 0')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
