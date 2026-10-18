# A staircase of six free plates, 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick, for a 0.8 mm wall rule: the check must mark the first three and leave
# the other three alone.
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

from mfgkit import *

t = Table('a1_staircase_wall: a staircase of six free plates, 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick, for a 0.8 mm wall rule: the check must mark the first three and leave the other three alone')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
thickness = [0.3, 0.7, 0.74, 0.78, 0.85, 2.0]
part = None
for k, h in enumerate(thickness):
    plate = box((9 * k, 0, 0), (9 * k + 6, 6, h))
    part = plate if part is None else union(part, plate)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
lo, hi = (-2, -2, -2), (9 * 5 + 8, 8, 4)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=14)                                          # (the thinnest plate, 0.3 mm, is more than 4 cells thick: a picture of it is evidence)
report = manufacturing_analysis(part, constraints=[rule], samples=8000)
print(report)
# THE ANSWER BY HAND, from what a wall is: it is too thin when it is under the limit less the grace of 5 %: under 0.8 x 0.95 = 0.76 mm, and by how much it is short is 0.8 - thickness - 0.04 mm
for k, h in enumerate(thickness):
    m_top, m_bottom = margin_at(report, 'min_wall', [(9 * k + 3, 3, h), (9 * k + 3, 3, 0.0)])
    should = h < 0.76
    t.check('plate %.2f mm: both faces are %s by the check' % (h, 'marked' if should else 'left alone'), (m_top > 0) == should and (m_bottom > 0) == should,
            '%.3f / %.3f' % (m_top, m_bottom), '> 0' if should else '<= 0')
    if should:
        t.near('plate %.2f mm: how much the wall is short, 0.8 - %.2f - 0.04' % (h, h), m_top, 0.8 - h - 0.04, 0.025, ' mm')
boxes = region_boxes(report, 'min_wall')
which = sorted(int(round((0.5 * (b[0][0] + b[1][0]) - 3) / 9.0)) for b in boxes)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the report lists three regions, one on each of the three thin plates', which == [0, 1, 2], which, '[0, 1, 2]')
t.check('the position of each region is on a thin plate, not between them (it is a sample of the surface)',
        all(any(9 * k - 0.5 <= g['at'][0] <= 9 * k + 6.5 for k in (0, 1, 2)) and g['at'][2] <= 0.76 for g in report.report['min_wall']['regions']),
        [tuple(round(c, 2) for c in g['at']) for g in report.report['min_wall']['regions']], 'on plates 0, 1, 2')
t.check('the part is not compliant', not report.compliant, report.compliant, False)

t.summary()
result = report
original = aside(part, lo, hi)[0]
result
original
