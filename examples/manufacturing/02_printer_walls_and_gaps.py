# 02  A 3D PRINTER'S JOB: make the part printable
#
# WHAT THIS SHOWS.  manufacturing_optimization changes the part until it keeps the rules of a printer with a 1.0 mm nozzle (a large-format printer's: walls at least 2.0 mm, gaps at least 1.0 mm): the 1.2 mm fin
# is thickened to 2.0 mm, the 0.7 mm slot between the two blocks is closed, and what was fine -- the 3 mm walls of the L, the boss, the 5 mm hole -- is left exactly as it was.
#
# WHAT YOU SEE.  The result is grey where it is as it was and red where material was added (beside it, the part as it was).  The red is a thin skin on both faces of the fin and a slab in the slot;
# the card on the right says how much was added, for which rule.  Turn the part with Shift + left drag, or use View > Top / Front / Right.
# WHAT YOU READ: the output pane has one PASS line for each check; the answers worked out by hand are written in the file, under the tool's call.  About two minutes (the tool and the checks half a minute, the drawing a minute and a half).

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('02 a printer\'s job: a plate with a thin fin, a narrow slot, an L, a boss and a hole, for a 3D printer with a 1.0 mm nozzle (walls 2.0, gaps 1.0)')

# THE PART (written out here: boxes and cylinders, nothing computed): a plate 70 x 40 x 5 with a hole, a fin 1.2 thick, two blocks 0.7 apart, an L of two walls, a round boss
plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
fin = box((5, 5, 5), (6.2, 20, 17))
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.7, 5, 5), (26.7, 20, 13))
ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
boss = cylinder(5, 10, (55, 28, 5))
part = union(union(union(union(union(plate, fin), block_a), block_b), ell), boss)
# WHAT IS ASKED: the preset of a printer with a 1.0 mm nozzle: walls of 2.0, gaps of 1.0, bridges up to 20 mm (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = fff_constraint(nozzle=1.0)
print(rule)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies; nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=1.0, verify=True)
result = res          # (the model the window shows, right under the call that makes it: the tree then holds the call under it)
print(res)
lo, hi = (-3, -3, -3), (73, 43, 22)
# (the view of the window: the box round the part, and how finely it is drawn -- 4 cells to the millimetre: the skin on the fin, 0.4 mm on each side, is then 1.6 cells thick)
frame(lo, hi, res=4)
# NO ANSWER BY HAND for the whole of a part like this: what is checked is that nothing floats beside the part, that the tool's numbers are the measured ones and that the independent check of the result finds only what the tool lists.  JUDGE THE PICTURE.
generic_checks(t, part, res, [rule], lo, hi, added='any', removed='any', name='fff sampler', keep_rules=True)
# THE ANSWERS BY HAND (a nozzle of 1.0: walls of 2.0 and gaps of 1.0):
#   the fin is 1.2 thick (x 5 .. 6.2): a thin wall is thickened half on each side, 0.4 mm each (a little more: the tool aims 4 % past the limit), so it stands from about x = 4.6 to 6.6 (solid inside that, air beyond it)
#   the gap between block_a and block_b is 0.7 wide (x 18 .. 18.7), under 1.0: it is filled (one slab), from the plate to the top of the blocks (z 5 .. 13) and not above them
#   the hole of 5 mm is wider than a gap of 1.0: it stays open (a printer prints a hole; only a cutter cannot cut one)
def _hand(label, solid, air):
    vs = evaluate(res, solid + air)
    ok_solid = all(v <= 0 for v in vs[:len(solid)])
    ok_air = all(v > 0 for v in vs[len(solid):])
    t.check(label, ok_solid and ok_air, 'solid %s / air %s' % ([round(v, 3) for v in vs[:len(solid)]], [round(v, 3) for v in vs[len(solid):]]), 'solid <= 0 / air > 0')
_hand('the fin is thickened by 0.4 mm on each side: x = 4.6 .. 6.6 at mid-height', [(4.65, 12.0, 10.0), (6.55, 12.0, 10.0)], [(4.45, 12.0, 10.0), (6.75, 12.0, 10.0)])
_hand('the gap of 0.7 between the two blocks is filled from the plate to their top and not above it', [(18.35, 12.0, 6.0), (18.35, 12.0, 12.5)], [(18.35, 12.0, 13.6)])
_hand('the hole of 5 mm stays open', [], [(62.0, 8.0, 2.5), (62.0, 8.0, 4.5)])

t.summary()
original = aside(part, lo, hi)[0]
result
original
