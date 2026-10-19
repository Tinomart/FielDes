# The same part for a CNC cutter of 6 mm (inside corners of radius 3, gaps of 6, nothing out of reach from above).
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - the rules of the CNC preset (corners, gaps, undercuts, walls)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('i2_cnc_sampler: the same part for a CNC cutter of 6 mm (inside corners of radius 3, gaps of 6, nothing out of reach from above)')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
fin = box((5, 5, 5), (5.5, 20, 17))
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.3, 5, 5), (26.3, 20, 13))
ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
boss = cylinder(5, 10, (55, 28, 5))
part = union(union(union(union(union(plate, fin), block_a), block_b), ell), boss)
# WHAT IS ASKED: the preset of a CNC cutter of 6 mm: inside corners of radius 3, gaps of 6 (its diameter), nothing out of reach from above, walls of 0.8
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = cnc_constraint(tool_diameter=6.0)
print(rule)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=1.0, verify=True)
print(res)
lo, hi = (-3, -3, -3), (73, 43, 22)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)                   # (2 cells to the millimetre: a layer thinner than a cell, the fin's 0.15 mm, is counted by the card and not drawn in red)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added='any', removed='any', name='cnc sampler', keep_rules='listed')
# THE ANSWERS BY HAND (a cutter of 6 mm is a ball of radius 3 for an inside corner, and nothing narrower than 6 can be cut):
#   the gap between the fin (x 5 .. 5.5, 12 high) and block_a (x 10 .. 18, 8 high) is 4.5 wide: the ball cannot go down into it, it rests on the block's edge and on the fin, so the gap is solid up to
#   the block's top (z = 13, less the sag of the ball at the middle: 0.4 mm) and the air above the block is untouched -- nothing rises above z = 14 there (the fin itself stays 17 high)
#   the gap between block_b and the L (x 26.3 .. 32) is 5.7 wide, also under 6: solid up to z = 12.5 at least, nothing above the lower block's top (z = 13) + 1 mm
#   the hole (diameter 5, under 6) is filled in the middle of the plate (the ball rests on each rim and sags 1.34 mm into its ends, which stay open), and the foot of every wall in the open is a quarter circle of radius 3: at 1 mm from a wall it stands 3 - sqrt(9 - 4) = 0.76 mm high
def _hand(label, solid, air):
    vs = evaluate(res, solid + air)
    ok_solid = all(v <= 0 for v in vs[:len(solid)])
    ok_air = all(v > 0 for v in vs[len(solid):])
    t.check(label, ok_solid and ok_air, 'solid %s / air %s' % ([round(v, 2) for v in vs[:len(solid)]], [round(v, 2) for v in vs[len(solid):]]), 'solid <= 0 / air > 0')
_hand('gap fin / block_a (4.5 wide): solid up to z = 12 at x = 6 .. 9.5, nothing above z = 14.8 between the fin and the block',
      [(x, 12.0, 12.0) for x in (6.0, 7.0, 7.75, 9.0, 9.5)], [(x, 12.0, 14.8) for x in (6.0, 7.0, 7.75, 9.0, 9.5)])
_hand('gap block_b / L (5.7 wide): solid up to z = 12 at x = 27 .. 30 and up to z = 10.5 at x = 31 (1 mm from the L: the exact arc of the ball is at 12.07 there, and a fill over a block\'s shoulder is exact to about 1 mm), nothing above z = 14.2 over the lower block\'s side of it',
      [(x, 12.0, 12.0) for x in (27.0, 28.0, 29.15, 30.0)] + [(31.0, 12.0, 10.5)], [(x, 12.0, 14.2) for x in (27.0, 28.0, 29.15)])
_hand('the hole of 5 mm (narrower than the ball) is filled in the middle of the plate and not at its ends: the ball rests on each rim and sags 3 - sqrt(9 - 2.5^2) = 1.34 mm into it, so z = 2 .. 3 is solid and z = 0.3 and 4.7 are open',
      [(62.0, 8.0, z) for z in (2.0, 2.5, 3.0)], [(62.0, 8.0, 0.3), (62.0, 8.0, 4.7), (62.0, 8.0, 5.6)])
_hand('the foot of the fin on its open side: 0.76 mm high at 1 mm from the wall (solid at 5.55, air at 6.0), and nothing at 3.5 mm', [(4.0, 12.0, 5.55)], [(4.0, 12.0, 6.0), (1.4, 12.0, 5.3)])
_hand('the foot of block_a at its end face (y = 20): 0.76 mm high at y = 21', [(14.0, 21.0, 5.55)], [(14.0, 21.0, 6.0), (14.0, 24.0, 5.3)])

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
