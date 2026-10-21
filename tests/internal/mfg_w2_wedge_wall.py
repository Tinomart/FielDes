# A wedge wall 0.2 mm thick at one end and 1.1 mm at the other, over 20 mm (4 mm wide), for a 0.8 mm wall rule: the fix follows the wall, thickens
# only the part under 0.8 and leaves no cliff where the wall crosses it.
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

t = Table('w2_wedge_wall: a wedge wall 0.2 mm thick at one end and 1.1 mm at the other, over 20 mm (4 mm wide), for a 0.8 mm wall rule: the fix follows the wall, thickens only the part under 0.8 and leaves no cliff where the wall crosses it')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
slope = 0.9 / 20.0
plane = half_space((-slope, 0.0, 1.0), (0.0, 0.0, 0.2))
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the plane of the wedge faces the way the case assumes (inside below it)', evaluate(plane, [(10.0, 2.0, 0.0)])[0] < 0 < evaluate(plane, [(10.0, 2.0, 3.0)])[0], '', '')
part = intersection(box((0, 0, 0), (20, 4, 1.3)), plane)           # (z from 0 up to 0.2 + 0.045 x)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
lo, hi = (-1.5, -1.5, -1.5), (21.5, 5.5, 2.8)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=14)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
# THE ANSWER BY HAND: the thickness along z at x is 0.2 + 0.045 x (the slope is 2.6 degrees: along the normal it is 0.999 of that), so the wall is thin up to x = 13.3 (0.8) and 12.2 (0.76, the grace);
# the fix must make it 0.8 there and leave the rest, the profile between the two continuous
prev = None
worst_jump = 0.0
for x in [1.0 + 2.0 * i for i in range(10)]:
    z_mid = (0.2 + slope * x) / 2.0
    behind, ahead = run_length(res, (x, 2, z_mid), (0, 0, 1), 4.0, 8000)
    got = behind + ahead
    old = 0.2 + slope * x
    t.between('x = %.0f mm: the wall measured (it was %.2f, the rule wants at least 0.78)' % (x, old), got, (min(old, 0.78) - 0.02), (0.87 if old < 0.76 else old + 0.02), ' mm')
    if prev is not None:
        worst_jump = max(worst_jump, abs(got - prev))
    prev = got
t.check('the thickness changes smoothly along the wall: the largest step between neighbours 2 mm apart is at most 0.12 mm (the wedge itself changes 0.09)', worst_jump <= 0.12, '%.3f' % worst_jump, '<= 0.12')
cmp_ = compare(part, res, lo, hi, n=300000)
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
t.check('no piece of added material floats beside the wall', not floating_clusters(cmp_), '', 0)
# THE ANSWER BY HAND: the wall is brought up to 0.8 where it is under it, from x = 0 to x = (0.8 - 0.2) / 0.045 = 13.3: 4 x (0.6 x 13.3 - 0.0225 x 13.3^2) = 16 mm3, and nothing is added where the wall is thicker:
# not on its faces, and not along their edges (a ridge along the sides or a lip round the thick end)
a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=250000, n_dense=250000)
t.near('the material added: the wedge brought up to 0.8 mm from x = 0 to 13.3 (by hand 16 mm3)', a, 16.0, 2.0, ' mm3')
pts, vol = added_in(cmp_, (15.0, -1, -1), (21, 5, 3))
t.check('nothing is added where the wall is thick enough (x from 15 to 20: %.2f mm3 found)' % vol, vol < 0.3, '%.2f mm3' % vol, '< 0.3')
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
if not after.compliant:
    print(after)
before = manufacturing_analysis(part, constraints=[rule], samples=6000)
t.check('the independent check marked %.1f %% of the surface before and marks %.2f %% after (the notches where the sloping top meets the end face and along the edges: listed above): under 1 %%' % (100.0 * before.report['min_wall']['share'], 100.0 * after.report['min_wall']['share']),
        after.report['min_wall']['share'] < 0.01, '%.2f %%' % (100.0 * after.report['min_wall']['share']), '< 1 %')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
