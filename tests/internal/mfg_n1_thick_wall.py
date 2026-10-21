# A thick wall (5 mm) for a 0.8 mm rule: nothing to do, nothing may change.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('n1_thick_wall: a thick wall (5 mm) for a 0.8 mm rule: nothing to do, nothing may change')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
wall = box((0, 0, 0), (5, 16, 24))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(wall, constraints=[rule], max_change=3.0)
lo, hi = (-3, -3, -3), (8, 19, 27)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
added, removed, cells = changed_volumes(wall, res, lo, hi)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('thick wall: nothing added', added < 1.0, '%.3g mm3' % added, '0')
t.check('thick wall: nothing cut', removed < 1.0, '%.3g mm3' % removed, '0')
t.check('thick wall: the tool says there was nothing to do', abs(res.added) + abs(res.removed) < 1.0, '%.3g' % (res.added + res.removed), 0)
after = manufacturing_analysis(res, constraints=[rule])
t.check('thick wall: the independent check finds no broken rule', after.compliant, '', '')

t.summary()
result = res
original = aside(wall, lo, hi)[0]
result
original
