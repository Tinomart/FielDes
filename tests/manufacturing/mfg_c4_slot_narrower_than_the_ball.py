# A slot 4 mm wide through a block, narrower than the ball of an inside corner radius of 3 mm, and a slot 8 mm wide beside it: the narrow slot is
# filled and left with the sag of the ball resting on its two edges (0.76 mm deep), the wide one only gets its two floor corners rounded.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - corners: the library fillet (a ball rolls in the inside corner)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('c4_slot_narrower_than_the_ball: a slot 4 mm wide through a block, narrower than the ball of an inside corner radius of 3 mm, and a slot 8 mm wide beside it: the narrow slot is filled and left with the sag of the ball resting on its two edges (0.76 mm deep), the wide one only gets its two floor corners rounded')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
block = box((0, 0, 0), (60, 20, 12))
part = difference(difference(block, box((13, -1, 2), (17, 21, 13))), box((40, -1, 2), (48, 21, 13)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_radius=3.0)
lo, hi = (-3, -3, -3), (63, 23, 15)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
# THE ANSWER BY HAND: a ball of radius 3 cannot enter a slot 4 wide: it rests on the two edges of the mouth, its centre sqrt(9 - 4) = 2.236 above them, and it sags 3 - 2.236 = 0.764 mm below the top; the
# slot is solid up to the arc of the ball: at the middle of the slot (x = 15) the surface is 0.764 below the top (z = 12), 1 mm from the middle it is 2.236 - sqrt(9 - 1) = 0.592 below, at the edges it is flush
for dx in (0.0, 1.0, 1.7):
    zs = 12.0 + 2.236068 - math.sqrt(9.0 - dx * dx)
    mat, air = evaluate(res, [(15.0 + dx, 10.0, zs - 0.2), (15.0 + dx, 10.0, zs + 0.2)])
    t.check('narrow slot, %.1f mm from its middle: material up to the arc of the ball (z = %.3f) and air above it' % (dx, zs), mat <= 0 < air, '%.3f / %.3f' % (mat, air), '<= 0 / > 0')
deep = evaluate(res, [(15.0, 10.0, 3.0), (15.0, 10.0, 8.0)])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('narrow slot: solid all the way down (z = 3 and 8 at its middle)', all(v <= 0 for v in deep), [round(v, 3) for v in deep], '<= 0')
# the wide slot: the floor corners are the arcs of the ball, the middle of the floor and the walls are as they were
bad = []
for deg in range(10, 81, 10):
    a = math.radians(deg)
    for cx, sx in ((43.0, -1.0), (45.0, 1.0)):
        mat = (cx + sx * math.cos(a) * 3.2, 10.0, 5.0 - math.sin(a) * 3.2)
        air = (cx + sx * math.cos(a) * 2.8, 10.0, 5.0 - math.sin(a) * 2.8)
        vm, va = evaluate(res, [mat, air])
        if vm > 0 or va <= 0:
            bad.append((cx, deg))
t.check('wide slot: the two floor corners are the arc of the ball (radius 3 about (43, 5) and (45, 5)) at every 10 degrees', not bad, 'off at %s' % bad, 'none')
behind, ahead = run_length(res, (44.0, 10.0, 8.0), (1, 0, 0), 9.0, 8000, inside=False)
t.near('wide slot: its width above the corners is 8 mm as it was', behind + ahead, 8.0, 0.05, ' mm')
cmp_ = compare(part, res, lo, hi, n=200000)
pts_n, vol_n = added_in(cmp_, (12, -1, 1), (18, 21, 13))
sag = 9.0 * (math.asin(2.0 / 3.0) - (2.0 / 3.0) * math.sqrt(1 - 4.0 / 9.0))
t.near('narrow slot: the material added, the slot (4 x 10) less the sag (%.3f mm2) over its 20 mm length' % sag, vol_n, (40.0 - sag) * 20.0, 0.07 * 758.0, ' mm3')
pts_w, vol_w = added_in(cmp_, (39, -1, 1), (49, 21, 8))
t.near('wide slot: the material added, two floor corners (1 - pi/4) x 9 x 20 each', vol_w, 2 * (1 - math.pi / 4) * 9.0 * 20.0, 8.0, ' mm3')
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
residue_check(t, 'slots', part, res, [rule], samples=8000, exact=True, share=0.4, limit=0.02, what='read through exact_distance, as docs/manufacturing.md says for a part made by differences; the places left are tight arcs and creases')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
