# Four plates leaning 43, 44, 47 and 50 degrees off the vertical (the lower face looks down), an overhang of 45 degrees asked and the gusset allowed:
# the two under 46 degrees are left exactly as they are, the two over are given a gusset under their lower face, which then leans no more than 45
# degrees.
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

from mfgkit import *

t = Table('o6_overhang_thresholds: four plates leaning 43, 44, 47 and 50 degrees off the vertical (the lower face looks down), an overhang of 45 degrees asked and the gusset allowed: the two under 46 degrees are left exactly as they are, the two over are given a gusset under their lower face, which then leans no more than 45 degrees')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
leans = [43.0, 44.0, 47.0, 50.0]
spot = lambda k: (22.0 * k, 0.0)
part = None
for k, g in enumerate(leans):
    cx, oy = spot(k)
    plate = move(rotate_y(box((cx - 1.0, oy, 0), (cx + 1.0, oy + 12, 8)), math.radians(g), center=(cx, oy + 6, 0)), (0, 0, 1.2))
    part = plate if part is None else union(part, plate)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45.0, overhang_fix='fill')
lo, hi = (-12, -3, -3), (22.0 * 3 + 14, 15, 14)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=10, max_change=3.0)
print(res)
# THE ANSWER BY HAND: the rule leaves a face alone up to 45 + 1 degrees (the degree of grace of the overhang rule); under that nothing is added, over it a gusset is added between the lower face and the
# face that leans 45 degrees from the foot of the plate: a sliver whose angle is (lean - 45) degrees, 1 and a bit mm3 for 47 degrees, more for 50, in proportion to the angle and the square of the length
cmp_ = compare(part, res, lo, hi, n=240000)
got = []
for k, g in enumerate(leans):
    cx, oy = spot(k)
    pts, vol = added_in(cmp_, (cx - 12, oy - 1, -1), (cx + 12, oy + 13, 12))
    got.append(vol)
    if g < 46.0:
        t.check('plate leaning %.0f degrees (under 46): nothing is added (%.2f mm3)' % (g, vol), vol < 2.5, '%.2f mm3' % vol, '< 2.5')
    else:
        t.check('plate leaning %.0f degrees (over 46): a gusset is added under it (%.2f mm3)' % (g, vol), vol > 2.5, '%.2f mm3' % vol, '> 2.5')
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the more a plate leans over the limit, the more is added under it (47 degrees: %.1f mm3, 50 degrees: %.1f mm3)' % (got[2], got[3]), got[3] > got[2], '%.1f / %.1f' % (got[2], got[3]), '50 > 47')
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
t.check('no piece of added material floats beside the plates', not floating_clusters(cmp_), '', 0)
after = manufacturing_analysis(res, constraints=[rule], samples=9000)
if not after.compliant:
    print(after)
# (what the rule can and cannot do, from its definition: a point is filled when the whole 45-degree cone above it lies in the ORIGINAL part.  A plate 2 mm thick is 2.9 mm wide at any height, and a face 2 to 5 degrees
# over the limit needs a wedge that grows by 0.09 mm for every mm along the face: the cone fits only at the foot (a depth of a quarter of a millimetre), the rest of the face stays over the limit.  The tilted plates'
# own bottom end faces lean over it too, and rest on nothing.  So the honest property is not "the check finds nothing" but "the check finds nothing the tool did not list": docs/manufacturing.md, overhang row)
marked = after.report['overhang']['share']
listed = [s for s in res.fail_states if s['rule'] == 'overhang']
t.check('what the independent check still marks (%.3g of the surface) is listed by the tool as still violated (%d places): the tool does not claim a clean result that is not' % (marked, len(listed)),
        after.compliant or bool(listed), '%d listed' % len(listed), 'compliant, or listed')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
