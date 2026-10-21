# Eight free walls leaning 0, 0.5, -0.5, 0.96, -0.96, 1.2, 3 and 10 degrees off the pull direction, for a draft of 1 degree: the check must mark the
# walls under 0.95 degrees, whichever way they lean, and no top.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - draft: _remedy_draft (a wall that leans too little is offset in proportion to its depth)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('a4_drafted_walls: eight free walls leaning 0, 0.5, -0.5, 0.96, -0.96, 1.2, 3 and 10 degrees off the pull direction, for a draft of 1 degree: the check must mark the walls under 0.95 degrees, whichever way they lean, and no top')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
leans = [0.0, 0.5, -0.5, 0.96, -0.96, 1.2, 3.0, 10.0]
part = None
for k, a in enumerate(leans):
    cx = 14.0 * k
    wall = rotate_y(box((cx - 3, 0, 0), (cx + 3, 10, 20)), math.radians(a), center=(cx, 5, 0))
    part = wall if part is None else union(part, wall)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(draft=1.0, pull_direction=(0, 0, 1))
lo, hi = (-8, -3, -3), (14 * 7 + 12, 13, 26)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
report = manufacturing_analysis(part, constraints=[rule], samples=9000)
print(report)
# THE ANSWER BY HAND, from what a draft is: the angle between a wall and the direction the part is pulled in; too little when under 1 x 0.95 = 0.95 degrees, and it does not matter which way it leans
tops = []
for k, a in enumerate(leans):
    cx = 14.0 * k
    behind, ahead = run_length(part, (cx, 5, 10.0), (1, 0, 0), 8.0, 4000)
    right = (cx + ahead, 5, 10.0)
    left = (cx - behind, 5, 10.0)
    top_behind, top_ahead = run_length(part, (cx, 5, 10.0), (0, 0, 1), 15.0, 6000)
    tops.append((cx, 5, 10.0 + top_ahead - 0.01))              # (a point of the top face, just inside it)
    mr, ml = margin_at(report, 'draft', [right, left])
    should = abs(a) < 0.95
    t.check('wall leaning %+.2f degrees: both sides are %s' % (a, 'marked' if should else 'left alone'), (mr > 0) == should and (ml > 0) == should, '%.3f / %.3f' % (mr, ml), '> 0' if should else '<= 0')
    if should:
        t.near('wall leaning %+.2f degrees: how much it is short, 1 - %.2f - 0.05 degrees' % (a, abs(a)), mr, 1.0 - abs(a) - 0.05, 0.1, ' degrees')
top_margins = margin_at(report, 'draft', tops)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('no top face is marked (a top is not a wall)', all(v <= 0 for v in top_margins), ['%.2f' % v for v in top_margins], '<= 0')
# the front and back faces of every wall stand upright whatever the wall's lean across (the lean is about y only): they have no draft and are marked in all eight walls
fronts = margin_at(report, 'draft', [(14.0 * k, 0.0, 10.0) for k in range(8)] + [(14.0 * k, 10.0, 10.0) for k in range(8)])
t.check('the upright front and back faces of all eight walls are marked (no draft, whichever way the wall leans across)', all(v > 0 for v in fronts), ['%.2f' % v for v in fronts], '> 0')

t.summary()
result = report
original = aside(part, lo, hi)[0]
result
original
