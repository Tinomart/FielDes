# Six free plates 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick (3 x 3 mm each) for a 0.8 mm wall rule: the wall fix thickens the thin ones to the rule,
# leaves the others exactly as they are, and sticks out past no edge.
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

t = Table('w1_staircase_wall: six free plates 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick (3 x 3 mm each) for a 0.8 mm wall rule: the wall fix thickens the thin ones to the rule, leaves the others exactly as they are, and sticks out past no edge')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
thickness = [0.3, 0.7, 0.74, 0.78, 0.85, 2.0]
part = None
for k, h in enumerate(thickness):
    plate = box((4 * k, 0, 0), (4 * k + 3, 3, h))
    part = plate if part is None else union(part, plate)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
lo, hi = (-1.5, -1.5, -1.5), (4 * 5 + 4.5, 4.5, 3.5)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=14)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
cmp_ = compare(part, res, lo, hi, n=300000)
# THE ANSWER BY HAND, from what the rule asks: a wall under 0.76 (0.8 less the 5 % grace) is made 0.8 thick; the gate of the fix is complete from 0.72 down, so 0.3 and 0.7 come out 0.8 (+0.5 and +0.1
# over 3 x 3 mm: 4.5 and 0.9 mm3); 0.74 is in the ramp of the grace and must come out at least 0.76; 0.78, 0.85 and 2.0 are not thin and must not change at all
for k, h in enumerate(thickness):
    x0 = 4 * k
    behind, ahead = run_length(res, (x0 + 1.5, 1.5, h / 2.0), (0, 0, 1), 3.0, 6000)
    got = behind + ahead
    if h <= 0.72:
        t.between('plate %.2f mm: the wall made, measured with a tape (0.8 asked)' % h, got, 0.78, 0.87, ' mm')
    elif h < 0.76:
        t.between('plate %.2f mm: inside the ramp of the grace: at least 0.76 and at most 0.87' % h, got, 0.76, 0.87, ' mm')
    else:
        t.near('plate %.2f mm: not thin, so untouched (tape)' % h, got, h, 0.02, ' mm')
    pts, volume = added_in(cmp_, (x0 - 0.6, -0.6, -0.6), (x0 + 3.6, 3.6, 2.6))
    by_hand = 9.0 * max(0.0, 0.8 - h) if h <= 0.72 else (None if h < 0.76 else 0.0)
    if by_hand is not None:
        t.near('plate %.2f mm: the material added, measured (by hand %.2f mm3)' % (h, by_hand), volume, by_hand, 0.15 * by_hand + 0.4, ' mm3')
    b = box_of(pts)
    if b is not None:
        grow = max(0.0, 0.8 - h) / 2.0                    # (each face moves out by half of what the wall lacks)
        inside = b[0][0] >= x0 - 0.12 and b[0][1] >= -0.12 and b[1][0] <= x0 + 3.12 and b[1][1] <= 3.12 and b[0][2] >= -grow - 0.06 and b[1][2] <= h + grow + 0.06
        t.check('plate %.2f mm: what was added lies on the plate and goes out by half of what the wall lacked (%.2f mm) and no more: no lip past its edge' % (h, grow), inside, b, 'inside the plate +- 0.12, z within +- %.2f' % grow)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('nothing was cut anywhere', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
after = manufacturing_analysis(res, constraints=[rule], samples=8000)
if not after.compliant:
    print(after)
before = manufacturing_analysis(part, constraints=[rule], samples=8000)
# (what the check still marks, a few samples of 8000, lies at the rim edges of the thinnest plates, where the wall remedy leaves the notch the docs describe: at most a tenth of what it marked before and under 1 %)
s_b, s_a = before.report['min_wall']['share'], after.report['min_wall']['share']
t.check('the independent check marked %.1f %% of the surface before and marks %.3f %% after (the rim edges of the thinnest plates): at most a tenth of it and under 1 %%' % (100.0 * s_b, 100.0 * s_a), s_a <= 0.1 * s_b and s_a < 0.01, '%.3f %% (was %.1f %%)' % (100.0 * s_a, 100.0 * s_b), '<= a tenth, < 1 %')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
