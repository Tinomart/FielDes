# 16  WHAT THE TOOL CANNOT FIX, AND THE CHECK THAT TELLS
#
# WHAT THIS SHOWS.  A roof over a gap of 30 mm is a bridge longer than the printer's 20 mm: the tool reports it, it never repairs it by itself (an arch or a gable is a design decision).  A fin of 0.6 mm is
# repaired.  With `verify=True` the tool checks what it made, lists the bridge as still broken, says where, and does not list the fin; without it the tool does not check its own result.  Either way
# `manufacturing_analysis` on the result is the independent check.
#
# WHAT YOU SEE.  The part with the fin thickened and the bridge as it was.  WHAT YOU READ: one PASS line per check, and the list of what is still broken.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('16 what cannot be fixed: a roof over a gap of 30 mm (a bridge: reported, never repaired) and a fin 0.6 mm thick (repaired), a bridge rule of 20 and a wall rule of 0.8: with verify=True the tool lists the bridge as still broken, says where, and does not list the fin; with verify=False it does not check what it made: no list and no second check')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
roof = union(union(box((0, 0, 0), (5, 10, 20)), box((35, 0, 0), (40, 10, 20))), box((0, 0, 20), (40, 10, 23)))
part = union(roof, box((46, 0, 0), (46.6, 10, 20)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(max_bridge=20.0, min_wall=0.8)
lo, hi = (-3, -3, -3), (50, 13, 26)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=7)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
checked = manufacturing_optimization(part, constraints=[rule], max_change=3.0, verify=True)
result = checked          # (the model the window shows, right under the call that makes it: the tree then holds the call under it)
print(checked)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
unchecked = manufacturing_optimization(part, constraints=[rule], max_change=3.0, verify=False)
rules_left = [s['rule'] for s in checked.fail_states]
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('verify=True: the bridge is listed as still broken (%s)' % rules_left, 'max_bridge' in rules_left, rules_left, 'max_bridge in the list')
where = [s['where'] for s in checked.fail_states if s['rule'] == 'max_bridge']
inside_roof = bool(where) and all(-0.6 <= w[0] <= 40.6 and -0.6 <= w[1] <= 10.6 and 19.4 <= w[2] <= 23.6 for w in where)
t.check('and where: on the roof (%s)' % [tuple(round(c, 1) for c in w) for w in where][:2], inside_roof, where[:2], 'a point on the roof, 0..40 x 0..10 x 20..23')
t.check('the fin, which was repaired, is not on the list', 'min_wall' not in rules_left, rules_left, 'no min_wall')
t.check('verify=True: the result carries the check of what was made', checked.after is not None, checked.after is not None, True)
t.check('verify=False: the tool does not check what it made (no second check)', unchecked.after is None, unchecked.after is None, True)
t.check('verify=False: and so does not claim a clean result (no list, no "all rules satisfied" in what it prints)', not unchecked.fail_states and 'all rules satisfied' not in repr(unchecked), '%d listed' % len(unchecked.fail_states), 'none, and no claim')
cmp_ = compare(checked, unchecked, lo, hi, n=200000)
t.check('verify changes the check and not the part: the two results are the same part', len(cmp_['added']) + len(cmp_['removed']) < 40, '%d + %d points' % (len(cmp_['added']), len(cmp_['removed'])), 'about 0')
t.near('the fin was thickened to 0.8', sum(run_length(checked, (46.3, 5, 10), (1, 0, 0), 4.0, 6000)), 0.8, 0.05, ' mm')
pts, vol = added_in(compare(part, checked, lo, hi, n=200000), (-1, -1, 19), (41, 11, 24))
t.check('the roof was not touched (%.2f mm3 added there)' % vol, vol < 2.0, '%.2f mm3' % vol, '< 2')
after = manufacturing_analysis(checked, constraints=[rule], samples=8000)
t.check('the independent check of the result agrees: the bridge is broken, the wall is not', after.report['max_bridge']['broken'] and not after.report['min_wall']['broken'], '%s / %s' % (after.report['max_bridge']['broken'], after.report['min_wall']['broken']), 'True / False')

t.summary()
original = aside(part, lo, hi)[0]
result
original
