# Two walls 0.5 mm thick with a gap of 0.3 mm between them (6 mm long, 4 high, on a base), for a 0.8 mm wall rule and a 0.4 mm gap rule together,
# drawn at 14 cells to the millimetre (the gap is 4 cells): the walls grow into the gap that was narrow already -- the result must keep both rules or
# the tool must say which it cannot and where.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - the rules of the 3D printer preset (walls, gaps, bridges)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('w6_walls_and_gap_together: two walls 0.5 mm thick with a gap of 0.3 mm between them (6 mm long, 4 high, on a base), for a 0.8 mm wall rule and a 0.4 mm gap rule together, drawn at 14 cells to the millimetre (the gap is 4 cells): the walls grow into the gap that was narrow already -- the result must keep both rules or the tool must say which it cannot and where')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
wall_a = box((0, 0, 0), (0.5, 6, 4))
wall_b = box((0.8, 0, 0), (1.3, 6, 4))
base = box((-1.5, -1.5, -1), (2.8, 7.5, 0))                     # (a floor under them, so that they are one part)
part = union(union(wall_a, wall_b), base)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = fff_constraint(nozzle=0.4)
lo, hi = (-2, -2, -1.5), (3.3, 8, 5)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=14)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0, verify=True)
print(res)
# THE ANSWER BY HAND: the walls are thin (0.5 < 0.8) and the gap is narrow (0.3 < 0.4): closing the gap makes one slab 1.3 mm thick, and thickening the outer faces by 0.15 makes it 1.6;
# whatever the tool does, no air narrower than 0.38 and no wall under 0.76 may be left in the middle of the walls (z = 2, y = 3)
material = evaluate(res, [(0.65, 3, 2)])[0] <= 0
print('the middle of the gap between the walls is %s' % ('material (closed)' if material else 'air (open)'))
if not material:
    air_b, air_a = run_length(res, (0.65, 3, 2), (1, 0, 0), 3.0, 6000, inside=False)
    t.check('the gap that is left is at least 0.38 mm wide (it is open: the check is only made then)', air_b + air_a >= 0.38, '%.3f mm' % (air_b + air_a), '>= 0.38')
thickest = 0.0
for x in (0.25, 1.05, 0.65):
    b2, a2 = run_length(res, (x, 3, 2), (1, 0, 0), 4.0, 8000)
    thickest = max(thickest, b2 + a2)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the material at the walls is at least 0.76 thick wherever it is measured (tape at x = 0.25, 0.65, 1.05)', thickest >= 0.76, '%.3f' % thickest, '>= 0.76')
after = manufacturing_analysis(res, constraints=[rule], samples=8000)
print(after)
broken = {k: round(v['share'], 4) for k, v in after.report.items() if v['broken']}
t.check('the independent check finds no broken rule, or the tool\'s own list says what is still broken (it does not claim a clean result that is not) -- the check found %s' % broken, after.compliant or bool(res.fail_states), '%d listed by the tool' % len(res.fail_states), 'compliant, or listed')
cmp_ = compare(part, res, lo, hi, n=300000)
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
t.check('no piece of added material floats beside the walls', not floating_clusters(cmp_), '', 0)
# THE ANSWER BY HAND: only the walls grow (0.15 on each outer face, and the gap closed): everything added lies between x = -0.15 and 1.45 and in the length and height of the walls, and the base
# (a plate 1 mm thick, which is thick enough) is not touched at its ends nor away from the walls
moved = [p for p, x in cmp_['added'] if x > 1e-3]            # (the result is grown by a hair of a few microns: that is not material added)
b = box_of(moved)
inside_box = b is not None and all(b[0][i] >= ((-0.3, -0.12, -0.12)[i]) and b[1][i] <= ((1.6, 6.12, 4.12)[i]) for i in range(3))
t.check('everything added lies on the walls (x from -0.3 to 1.6, the length and height of the walls): the box of the added material is %s' % (b,), inside_box, b, '((-0.3, -0.12, -0.12), (1.6, 6.12, 4.12))')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
