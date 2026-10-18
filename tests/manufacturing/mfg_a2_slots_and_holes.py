# Slots 0.2, 0.3, 0.35, 0.39, 0.45 and 1.0 mm, for a 0.4 mm gap rule: the check must mark the narrow ones and only those (the round holes are in a2b,
# in a scene small enough to read them).
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

from mfgkit import *

t = Table('a2_slots_and_holes: slots 0.2, 0.3, 0.35, 0.39, 0.45 and 1.0 mm, for a 0.4 mm gap rule: the check must mark the narrow ones and only those (the round holes are in a2b, in a scene small enough to read them)')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
widths = [0.2, 0.3, 0.35, 0.39, 0.45, 1.0]
block = box((0, 0, 0), (28, 8, 5))
for k, w in enumerate(widths):
    x = 2 + 4 * k
    block = difference(block, box((x, -1, 1), (x + w, 9, 6)))             # a slot: from the top down to a floor 1 mm thick, through the whole depth
part = block
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_gap=0.4)
lo, hi = (-2, -2, -2), (30, 10, 7)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=16)                                                   # (the narrowest slot, 0.2 mm, is 3 cells: just enough to see)
report = manufacturing_analysis(part, constraints=[rule], samples=8000)
print(report)
# THE ANSWER BY HAND, from what a gap is: too narrow when under 0.4 x 0.95 = 0.38 mm, short by 0.4 - width - 0.02 mm; a round hole is a gap as wide as its diameter
for k, w in enumerate(widths):
    left, right = margin_at(report, 'min_gap', [(2 + 4 * k, 4, 3.5), (2 + 4 * k + w, 4, 3.5)])
    should = w < 0.38
    t.check('slot %.2f mm: both walls are %s by the check' % (w, 'marked' if should else 'left alone'), (left > 0) == should and (right > 0) == should,
            '%.3f / %.3f' % (left, right), '> 0' if should else '<= 0')
    if should:
        t.near('slot %.2f mm: how much the gap is short, 0.4 - %.2f - 0.02' % (w, w), left, 0.4 - w - 0.02, 0.025, ' mm')
n = len(report.report['min_gap']['regions'])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the report lists three regions: the slots 0.2, 0.3 and 0.35', n == 3, n, 3)

t.summary()
result = report
original = aside(part, lo, hi)[0]
result
original
