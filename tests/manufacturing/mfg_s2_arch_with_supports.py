# A tunnel with a round roof in a block, an overhang of 45 degrees asked with support="block" and a budget that refuses the teardrop: a block of
# support under the part of the roof that leans out, with a clearance of 0.3 mm to the roof and to the walls everywhere, and the part itself
# unchanged.
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

t = Table('s2_arch_with_supports: a tunnel with a round roof in a block, an overhang of 45 degrees asked with support="block" and a budget that refuses the teardrop: a block of support under the part of the roof that leans out, with a clearance of 0.3 mm to the roof and to the walls everywhere, and the part itself unchanged')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = difference(box((0, 0, 0), (30, 20, 30)), union(box((9, -1, 4), (21, 21, 12)), rotate_x(cylinder(6, 22, (15, 12, -21)), math.pi / 2)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, overhang_fix='cut', support='block', support_gap=0.3)
lo, hi = (-3, -3, -3), (33, 23, 33)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=12, max_change=0.004, verify=True)
print(res)
ov = [c for c in res.changes if c['remedy'] == 'overhang']
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the teardrop is refused (it would change 1 % against 0.4 %), and the tool says why', bool(ov) and not ov[0]['kept'] and '%' in ov[0]['why'], ov[0]['why'] if ov else '', '1 % against 0.4 %')
a, r, _ = changed_volumes(part, res, lo, hi)
t.check('the part itself is unchanged', a < 1.5 and r < 1.5, '+%.3g -%.3g mm3' % (a, r), 0)
t.check('the roof the block holds up is not listed as still broken (the card counts 0 places)', len(res.fail_states) == 0, '; '.join('%s at %s' % (s['rule'], s['where']) for s in res.fail_states) or 'none', 'none')
pts = points_in((8, 0, 3), (22, 20, 19), 240000, seed=11)
vals = evaluate(res.supports, pts)
inside = [p for p, v in zip(pts, vals) if v <= 0]
volume = (14 * 20 * 16) * len(inside) / 240000.0
# THE ANSWER BY HAND: the roof leans more than 46 degrees from the vertical where it is within 44 degrees of the crown, that is for |x - 15| < 6 sin 44 = 4.17: a prism under that part of the roof, from the
# floor (z = 4 + 0.3) to the roof less the clearance, 8.34 wide and about 12.9 high on average: 108 mm2 over 20 mm
t.between('the support volume, measured on its field (about 2150 mm3 by hand)', volume, 1700, 2700, ' mm3')
t.near('the tool\'s own number agrees', res.support_volume, volume, 0.1 * volume, ' mm3')
xs = [abs(p[0] - 15.0) for p in inside]
t.between('the support lies under the leaning part of the roof only: its half-width is about 4.2 (by hand), not the 6 of the tunnel', max(xs, default=0.0), 3.4, 5.2, ' mm')
pv = evaluate(part, inside[:30000])
t.check('a gap to the part everywhere (the part\'s field at the support, at least 0.2)', min(pv) >= 0.2, '%.3f' % min(pv), '>= 0.2')
t.check('the support is under the crown of the roof (material at (15, 10, 17.5), the roof at 18 less the clearance)', evaluate(res.supports, [(15.0, 10.0, 17.5)])[0] <= 0, '', '')
t.check('and not above the roof (the highest point of the support is under 17.75)', max([p[2] for p in inside], default=0.0) <= 17.75, '%.3f' % max([p[2] for p in inside], default=0.0), '<= 17.75')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
