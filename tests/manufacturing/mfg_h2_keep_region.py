# A fin of 0.6 mm with the upper half kept (keep=): only the lower half is thickened.
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

t = Table('h2_keep_region: a fin of 0.6 mm with the upper half kept (keep=): only the lower half is thickened')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
fin = box((0, 0, 0), (0.6, 16, 24))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(fin, constraints=[rule], keep=[box((-1, -1, 12), (2, 17, 26))], max_change=3.0)
lo, hi = (-3, -3, -3), (6, 19, 27)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
cmp_ = compare(fin, res, lo, hi, n=200000)
top = [p for p, _ in cmp_['added'] if p[2] > 12.6]
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('keep: nothing is added above the kept height (z > 12.6)', len(top) == 0, '%d points' % len(top), 0)
for z, want in ((4.0, 0.8), (8.0, 0.8), (16.0, 0.6), (22.0, 0.6)):
    b, a = run_length(res, (0.3, 8, z), (1, 0, 0), 5.0, 5000)
    t.near('keep: the wall at z=%g (kept: 0.6, free: 0.8)' % z, b + a, want, 0.03, ' mm')
added, removed, _ = changed_volumes(fin, res, lo, hi)
t.check('keep: some thickening was made below (about half of 76.8 mm3)', 25 < added < 55, '%.3g mm3' % added, '25 .. 55')

t.summary()
result = res
original = aside(fin, lo, hi)[0]
result
original
