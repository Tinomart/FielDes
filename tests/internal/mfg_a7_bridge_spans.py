# Roofs held at both ends over gaps of 5, 10, 18, 22 and 30 mm and a ledge held at one end only, for a longest bridge of 20 mm: the check must mark
# the spans over 21 mm and not the open ledge.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                      - a bridge is only reported: there is no remedy for it
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('a7_bridge_spans: roofs held at both ends over gaps of 5, 10, 18, 22 and 30 mm and a ledge held at one end only, for a longest bridge of 20 mm: the check must mark the spans over 21 mm and not the open ledge')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
spans = [5.0, 10.0, 18.0, 22.0, 30.0]
spot = lambda k: (48.0 * (k % 3), 20.0 * (k // 3))                         # (three to a row, 48 mm apart, the rows 20 apart)
part = None
for k, s in enumerate(spans):
    x0, y0 = spot(k)
    piece = union(union(box((x0, y0, 0), (x0 + 4, y0 + 10, 8)), box((x0 + 4 + s, y0, 0), (x0 + 8 + s, y0 + 10, 8))), box((x0, y0, 8), (x0 + 8 + s, y0 + 10, 10)))
    part = piece if part is None else union(part, piece)
xl, yl = spot(5)
ledge = union(box((xl, yl, 0), (xl + 4, yl + 10, 8)), box((xl, yl, 8), (xl + 20, yl + 10, 10)))
part = union(part, ledge)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(max_bridge=20.0)
lo, hi = (-4, -4, -3), (48.0 * 2 + 40, 20.0 + 14, 14)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
report = manufacturing_analysis(part, constraints=[rule], samples=12000)
print(report)
# THE ANSWER BY HAND, from what a bridge is: a flat roof held at both ends; too long when its span is over 20 x 1.05 = 21 mm; a ledge held at one end is an overhang, not a bridge
for k, s in enumerate(spans):
    x0, y0 = spot(k)
    m = margin_at(report, 'max_bridge', [(x0 + 4 + s / 2.0, y0 + 5.0, 8.0)])[0]
    should = s > 21.0
    t.check('roof over %.0f mm: %s by the check' % (s, 'marked' if should else 'left alone'), (m > 0) == should, '%.2f' % m, '> 0' if should else '<= 0')
m_ledge = margin_at(report, 'max_bridge', [(xl + 12.0, yl + 5.0, 8.0)])[0]
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the ledge held at one end only is not a bridge', m_ledge <= 0, '%.2f' % m_ledge, '<= 0')
n = len(report.report['max_bridge']['regions'])
t.check('the report lists two regions: the spans of 22 and 30 mm', n == 2, n, 2)

t.summary()
result = report
original = aside(part, lo, hi)[0]
result
original
