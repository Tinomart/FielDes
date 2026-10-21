# Round holes 0.3, 0.5 and 1.0 mm across in a small plate (14 x 8 x 5, 16 cells to the millimetre), for a 0.4 mm gap rule: the check must mark the
# wall of the 0.3 hole -- a round hole is a gap as wide as its diameter, and the commonest narrow gap there is -- and leave the 0.5 and the 1.0 alone;
# the scene is small because the check reads the surface through differences of 0.001 of the part's size.
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
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table("a2b_round_holes: round holes 0.3, 0.5 and 1.0 mm across in a small plate (14 x 8 x 5, 16 cells to the millimetre), for a 0.4 mm gap rule: the check must mark the wall of the 0.3 hole -- a round hole is a gap as wide as its diameter, and the commonest narrow gap there is -- and leave the 0.5 and the 1.0 alone; the scene is small because the check reads the surface through differences of 0.001 of the part's size")

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
diameters = [0.3, 0.5, 1.0]
plate = box((0, 0, 0), (14, 8, 5))
for k, d in enumerate(diameters):
    plate = difference(plate, cylinder(d / 2.0, 7, (3 + 4 * k, 4, -1)))        # a round hole straight through
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_gap=0.4)
lo, hi = (-2, -2, -2), (16, 10, 7)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=16)
report = manufacturing_analysis(plate, constraints=[rule], samples=6000)
print(report)
for k, d in enumerate(diameters):
    m = margin_at(report, 'min_gap', [(3 + 4 * k - d / 2.0, 4, 2.5)])[0]
    should = d < 0.38
    t.check('hole %.1f mm across: its wall is %s by the check' % (d, 'marked' if should else 'left alone'), (m > 0) == should, '%.3f' % m, '> 0' if should else '<= 0')
n = len(report.report['min_gap']['regions'])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the report lists one region: the hole 0.3', n == 1, n, 1)

t.summary()
result = report
original = aside(plate, lo, hi)[0]
result
original
