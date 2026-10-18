# Two ledges of the same kind on one wall unit each, a constraint with support="block" that holds only in a region round the first, and a budget that
# refuses the gusset: supports are made under the first ledge only.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                      - supports: _supports_field (a block under what is left leaning out)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('s3_supports_in_a_region: two ledges of the same kind on one wall unit each, a constraint with support="block" that holds only in a region round the first, and a budget that refuses the gusset: supports are made under the first ledge only')

def unit(y0):
    return union(box((0, y0, 0), (10, y0 + 40, 40)), box((10, y0, 20), (30, y0 + 40, 25)))
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(unit(0), unit(60))
region = box((-5, -5, -5), (35, 50, 45))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, support='block', support_gap=0.3, region=region)
lo, hi = (-3, -3, -3), (33, 103, 43)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=25, max_change=0.01)
print(res)
pts = points_in((5, -2, -2), (32, 102, 22), 300000, seed=13)
vals = evaluate(res.supports, pts)
inside = [p for p, v in zip(pts, vals) if v <= 0]
in_a = [p for p in inside if p[1] < 50]
in_b = [p for p in inside if p[1] >= 50]
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('supports are made under the first ledge (%d points of the field are support)' % len(in_a), len(in_a) > 1000, len(in_a), '> 1000')
t.check('and none under the second, which is outside the region (%d points)' % len(in_b), len(in_b) == 0, len(in_b), 0)
t.check('the support under the first ledge is where the ledge is: at (20, 20, 10), and not at (20, 80, 10)', evaluate(res.supports, [(20.0, 20.0, 10.0)])[0] <= 0 < evaluate(res.supports, [(20.0, 80.0, 10.0)])[0], '', '')
a, r, _ = changed_volumes(part, res, lo, hi)
t.check('the part itself is unchanged', a < 1.5 and r < 1.5, '+%.3g -%.3g mm3' % (a, r), 0)
t.check('nothing is listed as still broken: the first ledge is held and the second is outside the region of the rule', len(res.fail_states) == 0, '; '.join('%s at %s' % (s['rule'], s['where']) for s in res.fail_states) or 'none', 'none')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
