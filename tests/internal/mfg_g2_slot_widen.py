# Two blocks 0.2 mm apart, a gap of 0.4 mm asked and gap_fix=widen: the slot is made as wide as asked.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - gaps: _remedy_gaps (a narrow gap is closed, or made as wide as asked)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('g2_slot_widen: two blocks 0.2 mm apart, a gap of 0.4 mm asked and gap_fix=widen: the slot is made as wide as asked')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (12, 16, 18)), box((12.2, 0, 0), (24.2, 16, 18)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_gap=0.4, gap_fix='widen')
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-3, -3, -3), (27, 19, 21)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added=None, removed=0.2 * 16 * 18, tol=8.0, within=((11.8, 0, 0), (12.6, 16, 18)), name='slot widened')
for y, z in ((8, 9), (1, 1), (15, 17)):
    behind, ahead = run_length(res, (12.1, y, z), (1, 0, 0), 3.0, 6000, inside=False)
    t.between('slot widened: the air measured at y=%g z=%g' % (y, z), behind + ahead, 0.39, 0.45, ' mm')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
