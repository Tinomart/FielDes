# Walls that stand 0, 0.5 and 3 degrees off the pull direction (narrower at the top), for a draft of 1 degree: the first two come out leaning 1
# degree, the third is left exactly as it is, and the lean after is measured from the field.
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

from mfgkit import *

t = Table('d4_walls_already_drafted: walls that stand 0, 0.5 and 3 degrees off the pull direction (narrower at the top), for a draft of 1 degree: the first two come out leaning 1 degree, the third is left exactly as it is, and the lean after is measured from the field')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
slope = lambda a: math.tan(math.radians(a))
part = None
for k, a in enumerate((0.0, 0.5, 3.0)):
    x0, x1 = 14.0 * k, 14.0 * k + 8.0
    tn = slope(a)
    left = half_space((-1.0, 0.0, tn), (x0, 0.0, 0.0))                        # (inside: x - x0 >= z tan a)
    right = half_space((1.0, 0.0, tn), (x1, 0.0, 0.0))                        # (inside: x <= x1 - z tan a)
    wall = intersection(intersection(left, right), box((x0 - 1, 0, 0), (x1 + 1, 10, 20)))
    part = wall if part is None else union(part, wall)
probe_in = evaluate(part, [(14.0 * 0 + 4.0, 5.0, 10.0), (14.0 * 2 + 4.0 + 0.0, 5.0, 10.0)])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the walls are where the case builds them (material at the middle of two of them)', all(v <= 0 for v in probe_in), '', '')
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(draft=1.0)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-4, -3, -3), (14 * 2 + 12, 13, 23)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
print(res)
# THE ANSWER BY HAND: the lean of a wall is read from how its width changes with height; between z = 2 and z = 18 a face that leans a degrees changes the width by 2 x 16 x tan(a);
# the walls under 0.95 degrees are made to lean 1 degree (a width change of 0.559), the one of 3 degrees is not touched (1.677)
for k, a in enumerate((0.0, 0.5, 3.0)):
    x_mid = 14.0 * k + 4.0
    w = []
    for z in (2.0, 18.0):
        xc = x_mid + (2.0 - 0.0) * 0.0
        behind, ahead = run_length(res, (x_mid, 5, z), (1, 0, 0), 8.0, 8000)
        w.append(behind + ahead)
    lean = math.degrees(math.atan((w[0] - w[1]) / (2.0 * 16.0)))
    want = 1.0 if a < 0.95 else a
    t.near('the wall that stood %.1f degrees off: the lean now, from the widths at z = 2 and 18 (%.3f and %.3f mm)' % (a, w[0], w[1]), lean, want, 0.08, ' degrees')
cmp_ = compare(part, res, lo, hi, n=200000)
# (the box leaves out the two END faces of the wall, at y = 0 and y = 10: they are vertical, and the rule drafts them -- 28 mm3 each; it is the wall's own two faces that must not change)
pts, vol3 = added_in(cmp_, (14 * 2 - 1, 0.5, -1), (14 * 2 + 9, 9.5, 21))
t.check('the wall of 3 degrees, which has the draft asked, was not touched (%.2f mm3 added)' % vol3, vol3 < 2.0, '%.2f mm3' % vol3, '< 2')
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
