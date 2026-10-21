# A thin disc: a plate of 0.5 mm and radius 4, a 0.8 mm wall asked, drawn at 16 cells to the millimetre (the layer added is 2.4 cells).
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

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('w5_disc: a thin disc: a plate of 0.5 mm and radius 4, a 0.8 mm wall asked, drawn at 16 cells to the millimetre (the layer added is 2.4 cells)')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
disc = cylinder(4, 0.5, (0, 0, 0))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(disc, constraints=[rule], max_change=3.0)
lo, hi = (-5.5, -5.5, -1.5), (5.5, 5.5, 2)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=16)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
thin_wall_checks(t, disc, res, [rule], lo, hi, 0.8, [((0, 0, 0.25), (0, 0, 1)), ((2.5, 0, 0.25), (0, 0, 1)), ((0, -3, 0.25), (0, 0, 1)), ((3.6, 0, 0.25), (0, 0, 1))],
                 expect_added=math.pi * 16 * 0.3, tol_added=3, reach=0.5, name='disc')
cmp_ = compare(disc, res, lo, hi, n=300000)
far = max((math.hypot(p[0], p[1]) for p, _ in cmp_['added']), default=0.0)
t.check('disc: nothing added outside the disc\'s own radius 4', far <= 4.12, '%.3f mm' % far, '<= 4.1')

t.summary()
result = res
original = aside(disc, lo, hi)[0]
result
original
