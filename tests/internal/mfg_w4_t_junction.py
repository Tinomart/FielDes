# A T: a wall of 0.5 mm on a wall of 2 mm, small enough for 14 cells to the millimetre; only the thin one may grow.
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

t = Table('w4_t_junction: a T: a wall of 0.5 mm on a wall of 2 mm, small enough for 14 cells to the millimetre; only the thin one may grow')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
thin = box((0, 0, 0), (0.5, 8, 6))
thick = box((0, 0, 0), (8, 2, 6))
part = union(thin, thick)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-1.5, -1.5, -1.5), (9.5, 9.5, 7.5)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=14)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
thin_wall_checks(t, part, res, [rule], lo, hi, 0.8, [((0.25, 5, 3), (1, 0, 0)), ((0.25, 7, 5), (1, 0, 0)), ((0.25, 3, 1), (1, 0, 0))],
                 expect_added=0.3 * 6 * 6, tol_added=3, reach=0.5, name='T')
behind, ahead = run_length(res, (4, 1, 3), (0, 1, 0), 6.0)
t.near('T: the 2 mm wall is untouched (tape at its middle)', behind + ahead, 2.0, 0.03, ' mm')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
