# A hollow sphere (outer radius 10, wall 0.5) with a solid ball of radius 4 floating in its cavity, a wall rule of 0.8 mm: the shell is thickened on
# both sides to 0.8, the ball is not touched (look at it in the section view).
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

t = Table('x7_hollow_with_inner_body: a hollow sphere (outer radius 10, wall 0.5) with a solid ball of radius 4 floating in its cavity, a wall rule of 0.8 mm: the shell is thickened on both sides to 0.8, the ball is not touched (look at it in the section view)')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
shell_ = difference(sphere(10.0, (0, 0, 10)), sphere(9.5, (0, 0, 10)))
ball = sphere(4.0, (0, 0, 10))
part = union(shell_, ball)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
lo, hi = (-12, -12, -2), (12, 12, 22)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
for deg in (0, 40, 75, 130):
    a = math.radians(deg)
    d = (math.cos(a), 0.0, math.sin(a))
    start = (9.75 * d[0], 0.0, 10.0 + 9.75 * d[2])
    t.near('the shell at %d degrees from the horizontal, measured along its radius (0.8 asked)' % deg, sum(run_length(res, start, d, 3.0, 6000)), 0.8, 0.05, ' mm')
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.near('the ball is as it was: across its middle it is 8.0 mm', sum(run_length(res, (0, 0, 10), (1, 0, 0), 7.0, 8000)), 8.0, 0.08, ' mm')
t.check('and the cavity between the ball and the shell is still air (at 6.5 mm from the centre)', evaluate(res, [(6.5, 0, 10)])[0] > 0, '', '')
cmp_ = compare(part, res, lo, hi, n=240000)
ball_pts, ball_vol = added_in(cmp_, (-4.5, -4.5, 5.5), (4.5, 4.5, 14.5))
t.check('nothing was added to the ball (%.2f mm3)' % ball_vol, ball_vol < 1.5, '%.2f mm3' % ball_vol, '< 1.5')
hand = 4.0 / 3.0 * math.pi * ((10.15 ** 3 - 9.35 ** 3) - (10.0 ** 3 - 9.5 ** 3))
a_, r_, _ = changed_volumes(part, res, lo, hi, n_coarse=240000, n_dense=240000)
t.near('the material added to the shell, (4/3) pi ((10.15^3 - 9.35^3) - (10^3 - 9.5^3)) = %.1f' % hand, a_, hand, 0.08 * hand, ' mm3')
t.check('nothing cut', r_ < 2.0, '%.3g mm3' % r_, 0)
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
if not after.compliant:
    print(after)
t.check('the independent check of the result finds no wall under 0.8 mm (%.3g of the surface)' % after.report['min_wall']['share'], after.compliant, '%.3g' % after.report['min_wall']['share'], 'none')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
