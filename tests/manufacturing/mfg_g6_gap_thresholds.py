# Slots 0.3, 0.37, 0.39, 0.41 and 0.5 mm wide, for a 0.4 mm gap rule: the 0.3 slot is closed, the 0.39 / 0.41 / 0.5 ones are left alone, and the one
# inside the ramp of the grace (0.37) must be closed or left, never made NARROWER.
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

t = Table('g6_gap_thresholds: slots 0.3, 0.37, 0.39, 0.41 and 0.5 mm wide, for a 0.4 mm gap rule: the 0.3 slot is closed, the 0.39 / 0.41 / 0.5 ones are left alone, and the one inside the ramp of the grace (0.37) must be closed or left, never made NARROWER')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
widths = [0.3, 0.37, 0.39, 0.41, 0.5]
block = box((0, 0, 0), (40, 10, 6))
for k, w in enumerate(widths):
    block = difference(block, box((4 + 8 * k, -1, 1), (4 + 8 * k + w, 11, 7)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_gap=0.4)
lo, hi = (-2, -2, -2), (42, 12, 8)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=10)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(block, constraints=[rule], max_change=3.0)
print(res)
# THE ANSWER BY HAND: too narrow when under 0.4 x 0.95 = 0.38 mm, so 0.3 (and 0.37) are narrow and 0.39, 0.41, 0.5 are not; a remedy may close a narrow slot or leave it, but a slot made NARROWER than it was
# (still open, and less than the 0.38 asked) is worse than before
for k, w in enumerate(widths):
    x = 4 + 8 * k
    b, a = run_length(res, (x + w / 2.0, 5, 3.5), (1, 0, 0), 2.0, 8000, inside=False)
    got = b + a
    material = evaluate(res, [(x + w / 2.0, 5, 3.5)])[0] <= 0
    if w >= 0.385:
        t.near('slot %.2f mm: not narrow, untouched (air measured)' % w, got, w, 0.02, ' mm')
    else:
        t.check('slot %.2f mm: closed (material at its middle), or left, never narrower than it was (air %.3f mm, material %s)' % (w, got, material), material or got >= w - 0.02, '%.3f' % got, 'closed or >= %.2f' % (w - 0.02))
after = manufacturing_analysis(res, constraints=[rule], samples=8000)
if not after.compliant:
    print(after)
before = manufacturing_analysis(block, constraints=[rule], samples=8000)
s_b, s_a = before.report['min_gap']['share'], after.report['min_gap']['share']
# (what the check still marks, one or two samples, lies where a slot is as wide as asked or narrows to its root: at most a tenth of what it marked before and under 1 %)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the independent check marked %.2f %% of the surface before and marks %.3f %% after (a sample or two at the limit): at most a tenth of it and under 1 %%' % (100.0 * s_b, 100.0 * s_a), s_a <= 0.1 * s_b and s_a < 0.01, '%.3f %% (was %.2f %%)' % (100.0 * s_a, 100.0 * s_b), '<= a tenth, < 1 %')
cmp_ = compare(block, res, lo, hi, n=200000)
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)

t.summary()
result = res
original = aside(block, lo, hi)[0]
result
original
