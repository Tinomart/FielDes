# The ledge with support="block" and a budget of 5 %: the gusset is refused, a block of support is made under the ledge, with a clearance of 0.3 mm to
# it and none in the strip kept free, and the part itself is unchanged.
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

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('s1_block_of_support: the ledge with support="block" and a budget of 5 %: the gusset is refused, a block of support is made under the ledge, with a clearance of 0.3 mm to it and none in the strip kept free, and the part itself is unchanged')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (10, 40, 40)), box((10, 0, 20), (30, 40, 25)))
strip = box((25, -1, -1), (31, 41, 41))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, support='block', support_gap=0.3, no_support=[strip])
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=25, max_change=0.05, verify=True)
lo, hi = (-3, -3, -3), (33, 43, 43)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
ov = [c for c in res.changes if c['remedy'] == 'overhang']
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('supports: the gusset is refused (it would change 25 % against 5 %), and the tool says why', bool(ov) and not ov[0]['kept'] and '%' in ov[0]['why'], ov[0]['why'] if ov else '', '25 % against 5 %')
a, r, _ = changed_volumes(part, res, lo, hi)
t.check('supports: the part itself is unchanged', a < 1.5 and r < 1.5, '+%.3g -%.3g mm3' % (a, r), 0)
# (the strip x 25 .. 31 is kept free of supports, so the ledge over it is held by nothing and is listed; the ledge over the block, x 10 .. 25, is held and is not)
t.check('supports: the part of the ledge over the block is not listed as still broken, the part over the strip kept free is (%d place(s): %s)' % (len(res.fail_states), '; '.join('%s at x = %.1f' % (s['rule'], s['where'][0]) for s in res.fail_states) or 'none'),
        len(res.fail_states) >= 1 and all(s['where'][0] >= 24.9 for s in res.fail_states), '%d listed' % len(res.fail_states), 'listed, all with x >= 24.9')
# THE END FACE: the block stands a little way in from the ledge's end (y = 0) and that distance is the same at every height: a face, not terraces (read on the field, 0.004 mm apart)
ys = [0.004 * i for i in range(0, 150)]
edge = []
for z in (2.0, 6.0, 10.0, 14.0, 18.0):
    v = evaluate(res.supports, [(17.0, y, z) for y in ys])
    edge.append(next((y for y, w in zip(ys, v) if w <= 0), None))
flat = None not in edge and max(edge) - min(edge) < 0.03
t.check('supports: the end face of the block is flat: the same distance in from the ledge\'s end at every height (spread under 0.03 mm)', flat, ' '.join('%s' % ('none' if e is None else '%.3f' % e) for e in edge), 'all within 0.03 of each other')
box_lo, box_hi = (9, -1, -1), (26, 41, 21)
pts = points_in(box_lo, box_hi, 200000, seed=9)
vals = evaluate(res.supports, pts)
inside = [p for p, v in zip(pts, vals) if v <= 0]
volume = (17 * 42 * 22) * len(inside) / 200000.0
t.between('supports: the support volume, measured on the field (the largest it can be, by hand: 11,820)', volume, 9500, 11900, ' mm3')
t.near('supports: the tool\'s own number agrees', res.support_volume, volume, 0.08 * volume, ' mm3')
t.check('supports: none in the strip kept free (x past 25.1)', all(p[0] <= 25.1 for p in inside), max([p[0] for p in inside], default=0), '<= 25.1')
t.check('supports: none touches the ledge (the highest point is under 19.75: a gap of 0.3 under the slab at z = 20)', max([p[2] for p in inside], default=0) <= 19.75, '%.3f' % max([p[2] for p in inside], default=0), '<= 19.75')
pv = evaluate(part, inside[:20000])
t.check('supports: a gap to the part everywhere (the part\'s field at the support, at least 0.2)', min(pv) >= 0.2, '%.3f' % min(pv), '>= 0.2')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
