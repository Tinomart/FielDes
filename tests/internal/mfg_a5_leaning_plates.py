# Seven plates leaning 20, 40, 44, 47, 50, 60 and 80 degrees off the vertical, a post with its flat bottom on the plate and a flat roof high up, for
# an overhang of 45 degrees: the check must mark the faces that look down and lean more than 46 degrees, and the roof, and not the up-facing faces nor
# the post on the plate.
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

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('a5_leaning_plates: seven plates leaning 20, 40, 44, 47, 50, 60 and 80 degrees off the vertical, a post with its flat bottom on the plate and a flat roof high up, for an overhang of 45 degrees: the check must mark the faces that look down and lean more than 46 degrees, and the roof, and not the up-facing faces nor the post on the plate')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
leans = [20.0, 40.0, 44.0, 47.0, 50.0, 60.0, 80.0]
spot = lambda k: (22.0 * (k % 4), 20.0 * (k // 4))                        # (four to a row, 22 mm apart, the rows 20 apart)
part = None
for k, g in enumerate(leans):
    cx, oy = spot(k)
    plate = move(rotate_y(box((cx - 1.0, oy, 0), (cx + 1.0, oy + 12, 8)), math.radians(g), center=(cx, oy + 6, 0)), (0, 0, 1.2))      # (lifted: turned about its foot, a plate dips up to 1 mm below it, and the lowest point of the whole part is the print bed)
    part = plate if part is None else union(part, plate)
px, py = spot(7)
post = cylinder(4.0, 8.0, (px, py + 6, 0))                                           # (a post with its flat bottom on the plate: the first layer is no overhang)
rx, ry = spot(8)
roof = union(box((rx - 6, ry, 8), (rx + 6, ry + 12, 10)), box((rx - 1, ry + 5, 0), (rx + 1, ry + 7, 8)))      # (a flat roof on a thin post, its underside at z = 8: an overhang of 90 degrees)
part = union(union(part, post), roof)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45.0)
lo, hi = (-12, -3, -3), (22.0 * 3 + 14, 20.0 * 2 + 15, 14)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
report = manufacturing_analysis(part, constraints=[rule], samples=12000)
print(report)
# THE ANSWER BY HAND, from what an overhang is: a surface that faces down and leans out more than 45 degrees from the vertical, less 1 degree of grace: under 46 degrees is left alone, and the
# first layer on the plate is not an overhang.  A plate leaning g degrees has one face that looks down and leans g degrees, and one that looks up (which never counts)
for k, g in enumerate(leans):
    cx, oy = spot(k)
    f1, f2 = leaning_faces(part, cx, g, oy)
    m1, m2 = margin_at(report, 'overhang', [f1, f2])
    should = g > 46.0
    marked = [m for m in (m1, m2) if m > 0]
    t.check('plate leaning %.0f degrees: %s by the check (one face looks down, the other up)' % (g, 'its lower face is marked and its upper face is not' if should else 'neither face is marked'),
            (len(marked) == 1) if should else (len(marked) == 0), '%.2f / %.2f' % (m1, m2), 'one > 0' if should else 'both <= 0')
    if should and marked:
        t.near('plate leaning %.0f degrees: how far its lower face leans past the limit, %.0f - 45 - 1 degrees' % (g, g), marked[0], g - 46.0, 1.5, ' degrees')
m_post, m_roof = margin_at(report, 'overhang', [(px + 2.0, py + 6.0, 0.0), (rx + 4, ry + 6.0, 8.0)])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the flat bottom of the post on the plate is not an overhang (the first layer rests on the plate)', m_post <= 0, '%.2f' % m_post, '<= 0')
t.check('the underside of the roof (90 degrees) is marked', m_roof > 0, '%.2f' % m_roof, '> 0')

t.summary()
result = report
original = aside(part, lo, hi)[0]
result
original
