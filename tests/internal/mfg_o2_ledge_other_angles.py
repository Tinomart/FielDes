# The ledge of o1 for an overhang of 30 degrees and of 60 degrees: the gusset under it leans at the angle asked (depth under a point d from the tip is
# d / tan(angle)).
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

t = Table('o2_ledge_other_angles: the ledge of o1 for an overhang of 30 degrees and of 60 degrees: the gusset under it leans at the angle asked (depth under a point d from the tip is d / tan(angle))')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
results = []
lo, hi = (-3, -3, -3), (33, 43, 43)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
for angle in (30.0, 60.0):
    part = union(box((0, 0, 0), (10, 40, 40)), box((10, 0, 20), (30, 40, 25)))
    rule = manufacturing_constraint(overhang=angle, overhang_fix='fill')
    res = manufacturing_optimization(part, constraints=[rule], max_support_height=25, max_change=1.5)
    results.append(res)
    print(res)
    tn = math.tan(math.radians(angle))
    # THE ANSWER BY HAND: the surface of the gusset leans `angle` degrees from the vertical: under a point of the ledge a distance d from its tip (x = 30) it is d / tan(angle) deep
    for d in (3.0, 6.0, 9.0):
        want = d / tn
        if want > 24.0:
            continue
        behind, ahead = run_length(res, (30.0 - d, 20, 19.5), (0, 0, -1), 25.0, 6000)
        t.near('%d degrees: the gusset under a point %g mm from the tip is %.2f mm deep' % (angle, d, want), 0.5 + ahead, want, 0.9, ' mm')
    residue_check(t, '%d degrees' % angle, part, res, [rule])
    a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=150000, n_dense=150000)
    t.check('%d degrees: nothing cut' % angle, r < 5.0, '%.3g mm3' % r, 0)

t.summary()
result = results[-1]
result
