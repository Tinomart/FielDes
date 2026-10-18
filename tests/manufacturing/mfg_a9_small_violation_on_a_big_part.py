# A big plate 60 x 60 x 3 mm with ONE thin fin 0.3 x 3 x 3 mm on it, for a wall rule of 0.8 mm: the check must find the one small violation among
# thousands of samples, and the same call twice must give the same report.
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

t = Table('a9_small_violation_on_a_big_part: a big plate 60 x 60 x 3 mm with ONE thin fin 0.3 x 3 x 3 mm on it, for a wall rule of 0.8 mm: the check must find the one small violation among thousands of samples, and the same call twice must give the same report')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (60, 60, 3)), box((30, 30, 3), (30.3, 33, 6)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
lo, hi = (-3, -3, -2), (63, 63, 8)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=10)
first = manufacturing_analysis(part, constraints=[rule], samples=8000)
print(first)
# THE ANSWER BY HAND: the fin (0.3 thick) is short by 0.8 - 0.3 - 0.04 = 0.46 mm; it is 2 x 3 x 3 = 18 mm2 of a surface of about 7500 mm2: 0.24 %, some 20 of 8000 samples
m1, m2 = margin_at(first, 'min_wall', [(30.0, 31.5, 4.5), (30.3, 31.5, 4.5)])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('both faces of the fin are marked (%.3f / %.3f; 0.46 by hand)' % (m1, m2), m1 > 0.4 and m2 > 0.4, '%.3f / %.3f' % (m1, m2), '~ 0.46')
regions = first.report['min_wall']['regions']
t.check('the report finds the fin: one region', len(regions) == 1, len(regions), 1)
if regions:
    at = regions[0]['at']
    d = math.sqrt((at[0] - 30.15) ** 2 + (at[1] - 31.5) ** 2 + (at[2] - 4.5) ** 2)
    t.check('the region is at the fin (the position %s is %.1f mm from its middle, on the surface)' % (tuple(round(c, 1) for c in at), d), d <= 3.0, '%.2f mm' % d, '<= 3')
    print('the fin was hit by %d of %d samples' % (regions[0]['samples'], first.samples))
second = manufacturing_analysis(part, constraints=[rule], samples=8000)
same = [g['at'] for g in second.report['min_wall']['regions']] == [g['at'] for g in regions] and second.report['min_wall']['share'] == first.report['min_wall']['share']
t.check('the same call a second time gives the same report (the places and the share)', same, '', '')

t.summary()
result = first
original = aside(part, lo, hi)[0]
result
original
