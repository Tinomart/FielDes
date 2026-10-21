# A block 14 x 14 x 8 with a round hole of radius 3 and a round boss of radius 2 that stands 0.5 mm from two of its edges, for a 0.8 mm wall rule:
# nothing here is thin, so nothing may be added, nothing reported, and the window draws it as fast as the part itself (found by a speed test: a remedy
# that looks for strips and wedges added specks along the edges of such a part).
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

t = Table('n8_thick_block_with_hole_and_boss: a block 14 x 14 x 8 with a round hole of radius 3 and a round boss of radius 2 that stands 0.5 mm from two of its edges, for a 0.8 mm wall rule: nothing here is thin, so nothing may be added, nothing reported, and the window draws it as fast as the part itself (found by a speed test: a remedy that looks for strips and wedges added specks along the edges of such a part)')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
block = difference(box((0, 0, 0), (14, 14, 8)), cylinder(3.0, 10, (7, 7, -1)))
part = union(block, cylinder(2.0, 6.0, (2.5, 2.5, 8)))
lo, hi = (-2, -2, -2), (16, 16, 16)
res = control(t, part, lo, hi, 'thick block', res_=6, rules=manufacturing_constraint(min_wall=0.8))
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the tool added nothing it counts (%.3g mm3)' % res.added, res.added < 0.05, '%.3g mm3' % res.added, '< 0.05')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
