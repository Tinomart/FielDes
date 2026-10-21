# The same part for injection moulding (walls of 1.5 mm, a draft of 2 degrees, nothing out of reach along the pull).
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                      - draft: _remedy_draft (a wall that leans too little is offset in proportion to its depth)
#                      - the rules of the moulding preset (walls, draft, undercuts, corners)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('i3_molding_sampler: the same part for injection moulding (walls of 1.5 mm, a draft of 2 degrees, nothing out of reach along the pull)')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
fin = box((5, 5, 5), (5.5, 20, 17))
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.3, 5, 5), (26.3, 20, 13))
ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
boss = cylinder(5, 10, (55, 28, 5))
part = union(union(union(union(union(plate, fin), block_a), block_b), ell), boss)
# WHAT IS ASKED: the preset of injection moulding: walls of 1.5, a draft of 2 degrees, nothing out of reach along the pull, inside corners of half the wall
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = molding_constraint(min_wall=1.5, draft=2.0)
print(rule)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=1.0, verify=True)
print(res)
lo, hi = (-3, -3, -3), (73, 43, 22)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)                   # (2 cells to the millimetre: the draft is a skin thinner than a cell, so the result is drawn as ONE body coloured by the change: red on every wall that was widened)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added='any', removed='any', name='molding sampler', keep_rules='listed')
# THE ANSWERS BY HAND (a draft of 2 degrees: a wall that is vertical comes out wider at its foot by tan(2 degrees) x its depth below the top of the part, which is the parting plane at z = 17):
#   the end face of block_a (y = 20, away from the other pieces) at z = 7 has moved outwards by (17 - 7) x 0.0349 = 0.349 mm, at z = 9 by 0.279 mm; at z = 5.3 (the plate's top is 5) it is inside the quarter circle
#   that the inside corner takes (0.75 mm), so it is not read there.  Outside that distance the air is untouched.
def _hand(label, solid, air):
    vs = evaluate(res, solid + air)
    ok_solid = all(v <= 0 for v in vs[:len(solid)])
    ok_air = all(v > 0 for v in vs[len(solid):])
    t.check(label, ok_solid and ok_air, 'solid %s / air %s' % ([round(v, 3) for v in vs[:len(solid)]], [round(v, 3) for v in vs[len(solid):]]), 'solid <= 0 / air > 0')
_hand('draft: the end face of block_a stands 0.349 mm out at z = 7 and 0.279 mm out at z = 9 (solid inside, air a tenth of a millimetre beyond)',
      [(14.0, 20.30, 7.0), (14.0, 20.23, 9.0)], [(14.0, 20.45, 7.0), (14.0, 20.38, 9.0)])
_hand('draft: the top of the part is the parting plane: block_a (13 high) has moved out only 0.14 mm at its top, nothing 0.3 mm out at z = 12.5', [(14.0, 20.08, 12.5)], [(14.0, 20.30, 12.5)])

import math
_ring = lambda r, z: [(55.0 + r * math.cos(math.radians(a)), 28.0 + r * math.sin(math.radians(a)), z) for a in range(0, 360, 45)]
_hand('draft on the boss (a cylinder of radius 5, axis (55, 28)): its wall becomes a cone, the same all the way round -- 0.349 mm out at z = 7 and 0.210 mm out at z = 11 at eight angles (solid 0.05 inside that, air 0.05 beyond)',
      _ring(5.30, 7.0) + _ring(5.16, 11.0), _ring(5.40, 7.0) + _ring(5.26, 11.0))

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
