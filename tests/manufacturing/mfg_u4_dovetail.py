# A dovetail groove (4 mm at the mouth, 8 mm at the bottom, 6 deep) in the top of a block, an undercut along z asked: what lies under the two lips is
# filled, and the groove becomes a straight slot as wide as its mouth.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - undercut: _remedy_undercut (what lies under and over the part along the direction is filled)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('u4_dovetail: a dovetail groove (4 mm at the mouth, 8 mm at the bottom, 6 deep) in the top of a block, an undercut along z asked: what lies under the two lips is filled, and the groove becomes a straight slot as wide as its mouth')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
third = 1.0 / 3.0
h_r = half_space((1.0, 0.0, third), (15.0 + 2.0 + 20.0 * third, 0.0, 0.0))        # (inside: x - 15 + z / 3 <= 2 + 20 / 3)
h_l = half_space((-1.0, 0.0, third), (15.0 - 2.0 - 20.0 * third, 0.0, 0.0))        # (inside: -(x - 15) + z / 3 <= 2 + 20 / 3)
cutter = intersection(intersection(h_r, h_l), box((0, -1, 14), (30, 11, 21)))
part = difference(box((0, 0, 0), (30, 10, 20)), cutter)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the groove is where the case builds it: air at the middle of its bottom (z = 14.5), material beside its mouth', evaluate(part, [(15.0, 5.0, 14.5)])[0] > 0 and evaluate(part, [(12.0, 5.0, 19.5)])[0] <= 0, '', '')
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(undercut_along=(0, 0, 1))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-3, -3, -3), (33, 13, 23)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=6)
print(res)
# THE ANSWER BY HAND: the width of the groove at height z is 4 + 2 (20 - z) / 3; after the fill it is 4 mm all the way down to the floor at z = 14: the two triangles each 6 x 2 / 2 = 6 mm2 = 12 mm2 x 10 = 120 mm3
for z in (14.5, 16.0, 18.0, 19.8):
    behind, ahead = run_length(res, (15.0, 5.0, z), (1, 0, 0), 6.0, 6000, inside=False)
    t.near('the groove at z = %g mm is 4 mm wide (the mouth) after the fill' % z, behind + ahead, 4.0, 0.08, ' mm')
cmp_ = compare(part, res, lo, hi, n=200000)
pts, volume = added_in(cmp_, (-1, -1, 13), (31, 11, 21))
t.near('the material added, the two triangles under the lips (by hand 120 mm3)', volume, 120.0, 20.0, ' mm3')
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
t.check('the independent check of the result finds no undercut (%.3g of the surface)' % after.report['undercut_along']['share'], after.compliant, '%.3g' % after.report['undercut_along']['share'], 'none')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
