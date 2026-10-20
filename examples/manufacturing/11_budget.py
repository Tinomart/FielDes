# 11  THE BUDGET: a change that is too big is not made
#
# WHAT THIS SHOWS.  `max_change` (default 25 %) is how much of the part a remedy may change.  A fin of 0.6 mm asked to be 0.8 mm would change a third of this small part: with a budget of 5 % the
# remedy is REFUSED, the part comes out exactly as it was, and the tool says why (and what it would have changed).  A change that big is a decision for the designer, not for the tool.
#
# WHAT YOU SEE.  The part, unchanged, and the card that says the remedy was not made.  WHAT YOU READ: one PASS line per check.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('11 the budget: a fin of 0.6 mm and a budget of 5 % (the thickening would change 33 %): the remedy is refused, the part comes out as it was, and the tool says why')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
fin = box((0, 0, 0), (0.6, 16, 24))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(fin, constraints=[rule], max_change=0.05, verify=True)
lo, hi = (-3, -3, -3), (6, 19, 27)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
walls = [c for c in res.changes if c['remedy'] == 'walls']
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('budget: the tool lists the wall remedy as NOT applied', bool(walls) and not walls[0]['kept'], [(c['remedy'], c['kept']) for c in res.changes], 'walls not kept')
t.check('budget: and says why, with the percentages', bool(walls) and '%' in walls[0]['why'], walls[0]['why'] if walls else '', 'about 33 % against 5 %')
a, r, _ = changed_volumes(fin, res, lo, hi)
t.check('budget: the part is exactly as it was (nothing added, nothing cut)', a < 1.0 and r < 1.0, '+%.3g -%.3g mm3' % (a, r), 0)
t.check('budget: the wall is still reported as broken', any(s['rule'] == 'min_wall' for s in res.fail_states), [s['rule'] for s in res.fail_states], 'min_wall')

t.summary()
result = res
original = aside(fin, lo, hi)[0]
result
original
