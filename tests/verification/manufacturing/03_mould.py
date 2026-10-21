# 03  AN INJECTION MOULD: draft, undercut and walls
#
# WHAT THIS SHOWS.  A moulded part has to leave the mould in one direction (here straight up, z): a wall that is parallel to the pull rubs along the steel, and anything under a lip that the
# two halves of the mould cannot reach is an undercut.  The constraint here asks for a draft of 2 degrees on every upright wall, no undercut along the pull and walls of 1.5 mm or more; the
# tool puts a draft on the walls (they grow wider towards the foot, by the depth times the tangent of the angle), fills the undercuts and thickens a thin wall.
#
# WHAT YOU SEE.  The part (right) and its result (left): grey where it is as it was, red where material was added.  The card names each remedy that was applied (draft, undercut, walls) and says
# how much each added.  WHAT YOU READ: one PASS line per check in the output pane; the answers by hand are in the file.  About half a minute, and the picture is there a second after the last line.

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'kit'))
from mfgkit import *

t = Table('03 an injection mould job: a part for injection moulding (walls of 1.5 mm, a draft of 2 degrees, nothing out of reach along the pull)')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
fin = box((5, 5, 5), (5.5, 20, 17))
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.3, 5, 5), (26.3, 20, 13))
ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
boss = cylinder(5, 10, (55, 28, 5))
part = union(union(union(union(union(plate, fin), block_a), block_b), ell), boss)
# WHAT IS ASKED: the three rules of a mould that this file is about: walls of 1.5, a draft of 2 degrees, nothing out of reach along the pull.  (The preset `molding_constraint` also asks for inside corners of half
# the wall; a fillet makes new vertical walls that a draft then has to lean, and a draft makes the fillet's air smaller, so corners and draft are two passes of the tool -- a second call of the tool -- and the corner
# rule is shown by 03.)  The only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone.
rule = manufacturing_constraint(min_wall=1.5, draft=2.0, undercut_along=(0, 0, 1), pull_direction=(0, 0, 1), name='injection mould, ABS')
print(rule)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=1.0, verify=True)
result = res          # (the model the window shows, right under the call that makes it: the tree then holds the call under it)
print(res)
lo, hi = (-3, -3, -3), (73, 43, 22)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)                   # (2 cells to the millimetre: the draft is a skin thinner than a cell, so the result is drawn as ONE body coloured by the change: red on every wall that was widened)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added='any', removed='any', name='molding sampler', keep_rules=True)
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
original = aside(part, lo, hi)[0]
result
original
