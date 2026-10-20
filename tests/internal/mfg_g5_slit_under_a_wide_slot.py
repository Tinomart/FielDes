# A narrow slit of 0.2 mm at the bottom of a wide slot of 3 mm, for a 0.4 mm gap rule: only the slit is closed, up to its mouth, and the wide slot
# above it stays as wide as it was.
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

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('g5_slit_under_a_wide_slot: a narrow slit of 0.2 mm at the bottom of a wide slot of 3 mm, for a 0.4 mm gap rule: only the slit is closed, up to its mouth, and the wide slot above it stays as wide as it was')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = difference(box((0, 0, 0), (30, 10, 10)), union(box((13.5, -1, 3.0), (16.5, 11, 11)), box((14.9, -1, 0.5), (15.1, 11, 3.1))))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_gap=0.4)
lo, hi = (-2, -2, -2), (32, 12, 12)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
# THE ANSWER BY HAND: the slit (0.2 wide, from z = 0.5 to its mouth at z = 3) is narrower than 0.38 and is closed flush up to z = 3; the wide slot above it (3 mm) is not narrow and must not change
zs = [0.7 + 0.02 * i for i in range(0, 180)]                            # (from 0.2 above the floor of the slit: the corner where the walls meet the floor is its own question, asked below)
vals = evaluate(res, [(15.0, 5.0, z) for z in zs])
first_air = next((z for z, v in zip(zs, vals) if v > 0), None)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the slit is closed up to its mouth at z = 3 (the first air on the middle line from z = 0.7: %s)' % first_air, first_air is not None and 2.85 <= first_air <= 3.15, first_air, '2.85 .. 3.15')
# (within two steps of the differences of an inside corner -- 0.06 mm for this part of 30 mm -- the slit is not read: a crack that high remains above the floor)
corner = evaluate(res, [(15.0, 5.0, 0.62), (15.0, 5.0, 0.7)])
t.check('and down to the floor of the slit, to within two steps of the differences (0.06 mm): material 0.12 and 0.2 mm above it', all(v <= 0 for v in corner), [round(v, 4) for v in corner], '<= 0')
b, a = run_length(res, (15.0, 5.0, 5.0), (1, 0, 0), 5.0, 8000, inside=False)
t.near('the wide slot above (z = 5) is as wide as it was (3.0 mm)', b + a, 3.0, 0.05, ' mm')
cmp_ = compare(part, res, lo, hi, n=200000)
pts, volume = added_in(cmp_, (-1, -1, 3.2), (31, 11, 11))
t.check('nothing added above the mouth of the slit (%.2f mm3 found)' % volume, volume < 0.5, '%.2f mm3' % volume, '< 0.5')
t.near('the material added, the slit 0.2 x 10 x 2.5 = 5 mm3', added_in(cmp_, (-1, -1, -1), (31, 11, 3.2))[1], 5.0, 2.0, ' mm3')
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
if not after.compliant:
    print(after)
t.check('the independent check of the result finds no broken rule (%.3g of the surface)' % after.report['min_gap']['share'], after.compliant, '%.3g' % after.report['min_gap']['share'], 'none')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
