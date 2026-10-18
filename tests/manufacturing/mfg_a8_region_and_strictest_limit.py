# Thin plates on both sides of a plane, a constraint that holds only on one side, and then two constraints on one part: the check marks only what is
# inside the region, and holds each rule to the strictest of its limits.
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

t = Table('a8_region_and_strictest_limit: thin plates on both sides of a plane, a constraint that holds only on one side, and then two constraints on one part: the check marks only what is inside the region, and holds each rule to the strictest of its limits')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
thickness = [0.3, 1.2, 2.0]
part = None
for x0 in (-20.0, 8.0):                                                       # (three plates on each side of x = 0, 6 mm apart, 4.5 x 4.5 mm)
    for k, h in enumerate(thickness):
        plate = box((x0 + 6.0 * k, 0, 0), (x0 + 6.0 * k + 4.5, 4.5, h))
        part = plate if part is None else union(part, plate)
left = box((-100, -100, -100), (0, 100, 100))
lo, hi = (-24, -3, -2), (28, 8, 5)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=14)
# (i) a wall rule of 0.8 that holds on the left of x = 0 only
scoped = manufacturing_analysis(part, constraints=[manufacturing_constraint(min_wall=0.8, region=left)], samples=8000)
print(scoped)
# THE ANSWER BY HAND: inside the region a wall under 0.76 is marked (the plate of 0.3, short by 0.8 - 0.3 - 0.04 = 0.46), 1.2 and 2.0 are thick enough; outside the region nothing is marked at all
for k, h in enumerate(thickness):
    l_top = margin_at(scoped, 'min_wall', [(-20.0 + 6.0 * k + 2.25, 2.25, h)])[0]
    r_top = margin_at(scoped, 'min_wall', [(8.0 + 6.0 * k + 2.25, 2.25, h)])[0]
    t.check('on the left (in the region) the plate of %.1f mm is %s' % (h, 'marked' if h < 0.76 else 'left alone'), (l_top > 0) == (h < 0.76), '%.3f' % l_top, '> 0' if h < 0.76 else '<= 0')
    t.check('on the right (outside the region) the plate of %.1f mm is left alone' % h, r_top <= 0, '%.3f' % r_top, '<= 0')
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the report lists one region: the thin plate on the left', len(scoped.report['min_wall']['regions']) == 1, len(scoped.report['min_wall']['regions']), 1)
# (ii) two constraints on the whole part: a wall of 0.8 and a wall of 1.5: the rule is held to 1.5
both = manufacturing_analysis(part, constraints=[manufacturing_constraint(min_wall=0.8), manufacturing_constraint(min_wall=1.5)], samples=8000)
print(both)
t.near('the limit the report holds the wall rule to is the strictest of the two (1.5)', both.report['min_wall']['limit'], 1.5, 1e-9, ' mm')
for k, h in enumerate(thickness):
    m = margin_at(both, 'min_wall', [(-20.0 + 6.0 * k + 2.25, 2.25, h)])[0]
    should = h < 1.5 * 0.95
    t.check('with 0.8 and 1.5 asked together the plate of %.1f mm is %s (too thin when under 1.5 x 0.95 = 1.425)' % (h, 'marked' if should else 'left alone'), (m > 0) == should, '%.3f' % m, '> 0' if should else '<= 0')
    if should:
        t.near('the plate of %.1f mm: how much it is short, 1.5 - %.1f - 0.075' % (h, h), m, 1.5 - h - 0.075, 0.03, ' mm')

t.summary()
result = both
original = aside(part, lo, hi)[0]
result
original
