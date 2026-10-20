# 05  AN OVERHANG: a gusset under a ledge
#
# WHAT THIS SHOWS.  A ledge sticking out of a wall hangs in the air: a printer lays each layer on the one under it, and a face that leans out more than about 45 degrees from the vertical has nothing
# to lie on.  With an overhang of 45 degrees asked, the tool grows a GUSSET under the ledge -- a slope of exactly that angle, as deep as the ledge is wide and no deeper -- so that every face of
# the part leans at most 45 degrees.  The part itself is not touched.
#
# WHAT YOU SEE.  The ledge (right) and the ledge with its gusset (left), the gusset in red.  WHAT YOU READ: one PASS line per check; the answers by hand (the slope, the volume of the gusset) are in the file.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('05 an overhang filled: a ledge sticking out of a wall, an overhang of 45 degrees asked: a gusset under it, every face leaning at most 45 degrees')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (10, 40, 40)), box((10, 0, 20), (30, 40, 25)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, overhang_fix='fill')
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=25, max_change=0.6)
result = res          # (the model the window shows, right under the call that makes it: the tree then holds the call under it)
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
original = aside(part, lo, hi)[0]
result
original
