# A plate with a thin fin, a narrow slot, an L, a boss and a hole, for a 3D printer with a 0.4 mm nozzle (walls 0.8, gaps 0.4).
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

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('i1_fff_sampler: a plate with a thin fin, a narrow slot, an L, a boss and a hole, for a 3D printer with a 0.4 mm nozzle (walls 0.8, gaps 0.4)')

# THE PART (written out here: boxes and cylinders, nothing computed): a plate 70 x 40 x 5 with a hole, a fin 0.5 thick, two blocks 0.3 apart, an L of two walls, a round boss
plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
fin = box((5, 5, 5), (5.5, 20, 17))
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.3, 5, 5), (26.3, 20, 13))
ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
boss = cylinder(5, 10, (55, 28, 5))
part = union(union(union(union(union(plate, fin), block_a), block_b), ell), boss)
# WHAT IS ASKED: the preset of a printer with a 0.4 mm nozzle: walls of 0.8, gaps of 0.4, bridges up to 20 mm
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = fff_constraint(nozzle=0.4)
print(rule)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=1.0, verify=True)
print(res)
lo, hi = (-3, -3, -3), (73, 43, 22)
# (drawn at 2 cells to the millimetre, for speed: the layers added -- 0.15 mm on the fin, 0.3 mm in the slot -- are thinner than a cell and are NOT drawn in red, although the card counts them;
# w4_open_box, w5_disc and w7_perforated draw such layers at 14 to 16 cells to the millimetre.  docs/manufacturing.md says so)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
# NO ANSWER BY HAND for a part like this: what is checked is that nothing floats beside the part, that the tool's numbers are the measured ones and that the independent check of the result finds only what the tool lists.  JUDGE THE PICTURE.
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added='any', removed='any', name='fff sampler', keep_rules='listed')
# THE ANSWERS BY HAND (a nozzle of 0.4: walls of 0.8 and gaps of 0.4):
#   the fin is 0.5 thick (x 5 .. 5.5): a thin wall is thickened half on each side, 0.15 mm each, so it stands from x = 4.85 to 5.65 (solid inside that, air 0.1 mm beyond it)
#   the gap between block_a and block_b is 0.3 wide (x 18 .. 18.3), under 0.4: it is filled (one slab), from the plate to the top of the blocks (z 5 .. 13) and not above them
#   the hole of 5 mm is wider than a gap of 0.4: it stays open (a printer prints a hole; only a cutter cannot cut one)
def _hand(label, solid, air):
    vs = evaluate(res, solid + air)
    ok_solid = all(v <= 0 for v in vs[:len(solid)])
    ok_air = all(v > 0 for v in vs[len(solid):])
    t.check(label, ok_solid and ok_air, 'solid %s / air %s' % ([round(v, 3) for v in vs[:len(solid)]], [round(v, 3) for v in vs[len(solid):]]), 'solid <= 0 / air > 0')
_hand('the fin is thickened by 0.15 mm on each side: x = 4.85 .. 5.65 at mid-height', [(4.88, 12.0, 10.0), (5.62, 12.0, 10.0)], [(4.78, 12.0, 10.0), (5.72, 12.0, 10.0)])
_hand('the gap of 0.3 between the two blocks is filled from the plate to their top and not above it', [(18.15, 12.0, 6.0), (18.15, 12.0, 12.5)], [(18.15, 12.0, 13.4)])
_hand('the hole of 5 mm stays open', [], [(62.0, 8.0, 2.5), (62.0, 8.0, 4.5)])

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
