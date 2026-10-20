# A ledge sticking out of a wall, an overhang of 45 degrees asked: a gusset under it, every face leaning at most 45 degrees.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('o1_ledge: a ledge sticking out of a wall, an overhang of 45 degrees asked: a gusset under it, every face leaning at most 45 degrees')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (10, 40, 40)), box((10, 0, 20), (30, 40, 25)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, overhang_fix='fill')
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=25, max_change=0.6)
lo, hi = (-3, -3, -3), (33, 43, 43)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=200000, n_dense=200000)
t.between('ledge: the gusset volume (the two hand answers are 5083 and 5333)', a, 4700, 5500, ' mm3')
t.check('ledge: nothing cut', r < 5.0, '%.3g' % r, 0)
# the depth of the gusset under the middle of the ledge: 45 degrees from the tip (x = 30), from the sides (y = 0, y = 40)
for x in (15, 20, 25):
    behind, ahead = run_length(res, (x, 20, 19.5), (0, 0, -1), 25.0, 5000)
    t.near('ledge: depth of the gusset under x=%g on the middle line (45 degrees from the tip: %g)' % (x, 30 - x), 0.5 + ahead, 30 - x, 0.7, ' mm')
for y in (3, 6):
    behind, ahead = run_length(res, (20, y, 19.5), (0, 0, -1), 25.0, 5000)
    t.near('ledge: depth of the gusset under y=%g at x=20 (45 degrees from the side: %g)' % (y, y), 0.5 + ahead, y, 0.7, ' mm')
covered = evaluate(res, [(x, y, 19.8) for x in (12, 18, 24) for y in (8, 20, 32)])
t.check('ledge: material right under the ledge at nine places', all(v <= 0 for v in covered), [round(v, 2) for v in covered], '<= 0')
residue_check(t, 'ledge', part, res, [rule])

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
