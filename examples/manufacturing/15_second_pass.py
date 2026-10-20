# 15  A SECOND PASS
#
# WHAT THIS SHOWS.  The result of the tool is a part like any other: give it to the tool again and the rules are checked again.  A part that already keeps its rules comes out unchanged (the second
# pass of the ledge, whose gusset already keeps the angle, makes nothing), and the card says "no change required".
#
# WHAT YOU SEE.  The ledge with its gusset after the first pass and after the second: the same.  WHAT YOU READ: one PASS line per check.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('15 a second pass: the ledge made twice: the result of the first pass given to the shell again comes out unchanged')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (10, 40, 40)), box((10, 0, 20), (30, 40, 25)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
first = manufacturing_optimization(part, constraints=[rule], max_support_height=25, max_change=0.6, verify=True)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
second = manufacturing_optimization(first, constraints=[rule], max_support_height=25, max_change=0.6, verify=True)
lo, hi = (-3, -3, -3), (33, 43, 43)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
a, r, _ = changed_volumes(first, second, lo, hi, n_coarse=30000, n_dense=30000)
t.check('second pass: the first pass added the gusset (more than 3000 mm3)', first.added > 3000, '%.4g mm3' % first.added, '> 3000')
t.check('second pass: it adds and cuts almost nothing (under 2 % of the 20,000 mm3 part)', a + r < 400, '+%.3g -%.3g mm3' % (a, r), '< 400')
t.check('second pass: the shell lists nothing as broken', not second.fail_states, [s['rule'] for s in second.fail_states], 'none')

t.summary()
result = second
original = aside(part, lo, hi)[0]
result
original
