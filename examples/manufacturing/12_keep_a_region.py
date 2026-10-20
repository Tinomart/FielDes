# 12  WHAT MUST NOT CHANGE: `keep=`
#
# WHAT THIS SHOWS.  Some of a part is not the tool's to touch -- a mounting face, a thread, a fit.  `keep=` takes a shape (or a list of them): nothing inside it is ever changed.  A fin of 0.6 mm
# whose upper half is kept is thickened in its lower half only.
#
# WHAT YOU SEE.  The fin, thickened (red) from the bottom up to the kept region and left alone above it.  WHAT YOU READ: one PASS line per check.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('12 what must not change: a fin of 0.6 mm with the upper half kept (keep=): only the lower half is thickened')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
fin = box((0, 0, 0), (0.6, 16, 24))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(fin, constraints=[rule], keep=[box((-1, -1, 12), (2, 17, 26))], max_change=3.0)
result = res          # (the model the window shows, right under the call that makes it: the tree then holds the call under it)
lo, hi = (-3, -3, -3), (6, 19, 27)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
cmp_ = compare(fin, res, lo, hi, n=200000)
top = [p for p, _ in cmp_['added'] if p[2] > 12.6]
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('keep: nothing is added above the kept height (z > 12.6)', len(top) == 0, '%d points' % len(top), 0)
# (a wall under the limit is thickened to the limit and 4 % over it, 0.8 x 1.04 = 0.832: the margin the check of the result needs to pass what was made)
for z, want in ((4.0, 0.832), (8.0, 0.832), (16.0, 0.6), (22.0, 0.6)):
    b, a = run_length(res, (0.3, 8, z), (1, 0, 0), 5.0, 5000)
    t.near('keep: the wall at z=%g (kept: 0.6, free: 0.832)' % z, b + a, want, 0.03, ' mm')
added, removed, _ = changed_volumes(fin, res, lo, hi)
t.check('keep: some thickening was made below (about half of 76.8 mm3)', 25 < added < 55, '%.3g mm3' % added, '25 .. 55')

t.summary()
original = aside(fin, lo, hi)[0]
result
original
