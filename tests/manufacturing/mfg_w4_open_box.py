# A box open at the top with walls of 0.5 mm, a 0.8 mm wall asked (the four walls and the floor), small enough to be drawn at 14 cells to the
# millimetre: a wall is 7 cells thick, the layer added to it 2.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('w4_open_box: a box open at the top with walls of 0.5 mm, a 0.8 mm wall asked (the four walls and the floor), small enough to be drawn at 14 cells to the millimetre: a wall is 7 cells thick, the layer added to it 2')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
shell_ = difference(box((0, 0, 0), (6, 6, 5)), box((0.5, 0.5, 0.5), (5.5, 5.5, 6)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(shell_, constraints=[rule], max_change=3.0)
lo, hi = (-1.5, -1.5, -1.5), (7.5, 7.5, 6.5)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=14)
tapes = [((3, 0.25, 2.5), (0, 1, 0)), ((3, 5.75, 2.5), (0, 1, 0)), ((0.25, 3, 2.5), (1, 0, 0)), ((5.75, 3, 2.5), (1, 0, 0)), ((3, 3, 0.25), (0, 0, 1)),
         ((1, 0.25, 1), (0, 1, 0)), ((5, 5.75, 4), (0, 1, 0))]
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
thin_wall_checks(t, shell_, res, [rule], lo, hi, 0.8, tapes, reach=0.5, name='open box')
# the cavity must still be there, smaller by what the walls grew, and nothing may stand above the rim (the wedge once grew a lip up to 0.35 mm round the edge of every wall)
inside = evaluate(res, [(3, 3, 3)])[0]
t.check('open box: the cavity is still air in the middle', inside > 0, '%.3g' % inside, '> 0')
cmp_ = compare(shell_, res, lo, hi, n=300000)
above = [p for p, _ in cmp_['added'] if p[2] > 5.05]
t.check('open box: nothing stands above the rim of the walls (z = 5): %d points of the added material are higher' % len(above), len(above) == 0, '%d points' % len(above), 0)

t.summary()
result = res
original = aside(shell_, lo, hi)[0]
result
original
