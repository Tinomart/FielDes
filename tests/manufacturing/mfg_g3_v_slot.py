# A V-shaped slot, 0 mm wide at its root and 3 mm at its mouth, for a 0.4 mm gap rule: the fill closes the slot up to where it is wide enough and
# leaves the rest of the V as it is, with a continuous surface between.
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

t = Table('g3_v_slot: a V-shaped slot, 0 mm wide at its root and 3 mm at its mouth, for a 0.4 mm gap rule: the fill closes the slot up to where it is wide enough and leaves the rest of the V as it is, with a continuous surface between')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
spread = 0.15                                                           # (the slot is 0.3 z wide at height z)
n1 = (1.0, 0.0, -spread)
n2 = (-1.0, 0.0, -spread)
h1, h2 = half_space(n1, (15.0, 0.0, 0.0)), half_space(n2, (15.0, 0.0, 0.0))
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the two planes of the slot face the way the case assumes (the middle of the slot is inside both)', evaluate(h1, [(15.0, 5.0, 5.0)])[0] < 0 and evaluate(h2, [(15.0, 5.0, 5.0)])[0] < 0, '', '')
cutter = intersection(intersection(h1, h2), box((-1, -1, 0), (31, 11, 11)))
part = difference(box((0, 0, 0), (30, 10, 10)), cutter)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_gap=0.4)
lo, hi = (-2, -2, -2), (32, 12, 12)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
# THE ANSWER BY HAND: the width at height z is 0.3 z and the gap across it (perpendicular to its faces) 0.3 z cos(8.5 degrees) = 0.2967 z: narrower than 0.38 for z under 1.28 mm;
# the fill must reach about that high (1.0 to 1.7 mm) and nothing above it may change: the slot at z = 3 and z = 6 is as wide as it was (0.9 and 1.8 mm)
zs = [0.25 + 0.05 * i for i in range(0, 76)]                             # (from 0.25 above the root: the point of the V itself is a slit narrower than two steps of the field's differences, a question of its own, asked next.  The line began at 0.2, where the slit is 0.06 wide -- exactly two steps of 0.03 mm, the edge of what is read; the step is 0.0305 now (it is no round number, see manufacturing_field._Context), two steps are 0.061, and 0.2 came to be on the other side of the edge.  0.25 is 0.075 wide, clear of it)
vals = evaluate(res, [(15.0, 5.0, z) for z in zs])
first_air = next((z for z, v in zip(zs, vals) if v > 0), None)
t.check('the slot is filled from its root up to a height of 1.0 to 1.7 mm (the first air on the middle line from z = 0.25: %s)' % first_air, first_air is not None and 1.0 <= first_air <= 1.7, first_air, '1.0 .. 1.7')
# (the slot is read through differences of a step of 0.001 of the part's size, 0.03 mm here: a slit narrower than two steps -- the point of the V, up to z = 0.2 where it is 0.06 wide -- is not read
# and stays as it was, 0.015 wide at z = 0.1; from there up the slot is closed)
root = evaluate(res, [(15.0, 5.0, 0.25), (15.0, 5.0, 0.3)])
b0, a0 = run_length(res, (15.0, 5.0, 0.1), (1, 0, 0), 1.0, 8000, inside=False)
t.check('and the root of the V is closed down to where the slit is two steps of the differences wide: material at 0.25 and 0.3 mm above it, and the air across at 0.1 mm above it no wider than 0.06 (it was 0.03): %.3f' % (b0 + a0), all(v <= 0 for v in root) and b0 + a0 <= 0.06, '%s, %.3f mm' % ([round(v, 4) for v in root], b0 + a0), '<= 0 and <= 0.06 mm')
for z, width in ((3.0, 0.9), (6.0, 1.8), (9.0, 2.7)):
    b, a = run_length(res, (15.0, 5.0, z), (1, 0, 0), 4.0, 8000, inside=False)
    t.near('the slot at z = %g mm is still as wide as it was (%g mm)' % (z, width), b + a, width, 0.05, ' mm')
cmp_ = compare(part, res, lo, hi, n=200000)
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
pts, volume = added_in(cmp_, (-1, -1, 2.0), (31, 11, 11))
t.check('nothing added higher than 2.0 mm above the root (%.2f mm3 found)' % volume, volume < 0.5, '%.2f mm3' % volume, '< 0.5')
t.check('no piece of added material floats', not floating_clusters(cmp_, cell=0.8, touch=0.4), '', 0)
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
if not after.compliant:
    print(after)
before = manufacturing_analysis(part, constraints=[rule], samples=6000)
s_b, s_a = before.report['min_gap']['share'], after.report['min_gap']['share']
# (what the check still marks, one or two samples, lies where a slot is as wide as asked or narrows to its root: at most a tenth of what it marked before and under 1 %)
t.check('the independent check marked %.2f %% of the surface before and marks %.3f %% after (a sample or two at the limit): at most a tenth of it and under 1 %%' % (100.0 * s_b, 100.0 * s_a), s_a <= 0.1 * s_b and s_a < 0.01, '%.3f %% (was %.2f %%)' % (100.0 * s_a, 100.0 * s_b), '<= a tenth, < 1 %')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
