# 13  A RULE IN ONE REGION: `region=`
#
# WHAT THIS SHOWS.  A constraint can hold in one region of the part only (`region=` a shape): the rest of the part is held to nothing -- or to another constraint of its own.  Two fins of 0.6 mm,
# the wall rule held in a region round the first: the first is thickened, the second stays as it is.
#
# WHAT YOU SEE.  Two fins, one thickened (red).  WHAT YOU READ: one PASS line per check.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('13 a rule in one region: two fins of 0.6 mm, the constraint holds only in a region round the first: the second stays as it is')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (0.6, 16, 24)), box((10, 0, 0), (10.6, 16, 24)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8, region=box((-2, -2, -2), (4, 18, 26)))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-3, -3, -3), (14, 19, 27)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
b1, a1 = run_length(res, (0.3, 8, 12), (1, 0, 0), 5.0, 5000)
b2, a2 = run_length(res, (10.3, 8, 12), (1, 0, 0), 5.0, 5000)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
# (a wall under the limit is thickened to the limit and 4 % over it, 0.8 x 1.04 = 0.832: the margin the check of the result needs to pass what was made)
t.near('region: the fin inside the region is thickened to 0.832 (the limit 0.8 and the margin)', b1 + a1, 0.832, 0.03, ' mm')
t.near('region: the fin outside the region is as it was (0.6)', b2 + a2, 0.6, 0.02, ' mm')
cmp_ = compare(part, res, lo, hi, n=200000)
outside = [p for p, _ in cmp_['added'] if p[0] > 4]
t.check('region: nothing is added outside the region (x > 4)', len(outside) == 0, '%d points' % len(outside), 0)
added, removed, _ = changed_volumes(part, res, lo, hi)
t.near('region: the material added is that of one fin (0.232 x 16 x 24 = 89.1)', added, 89.1, 7.0, ' mm3')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
