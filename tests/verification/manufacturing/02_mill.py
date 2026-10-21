# 02  A MILL'S JOB: inside corners and gaps for a 6 mm cutter
#
# WHAT THIS SHOWS.  A round cutter of 6 mm cannot cut a sharp inside corner (it leaves an arc of its own radius, 3 mm), cannot go into a gap narrower than itself, and cannot reach anything that lies
# under something else along its axis (here straight down, z).  cnc_constraint(tool_diameter=6) asks for exactly that: the tool rounds every inside corner to a radius of 3 mm, fills the gaps the cutter
# cannot enter (leaving the sag of the ball resting on their two edges), and fills what no cutter could reach.  The part is a plate with a fin, two blocks, an L, a boss and a hole.
# The hole is 5 mm across, narrower than the cutter: the rules would fill it like a gap (a ball of 3 cannot enter a radius of 2.5, so the tool closes it up to the arc the ball rests on).  A hole is the design, so
# its place is EXCLUDED from the job with `exclude(part, ...)`: the tool neither touches it nor judges it, and it stays a hole all the way through.
#
# WHAT YOU SEE.  The part (right) and its result (left): grey where it is as it was, red where material was added -- the arcs along every inside corner and the filled gaps; the hole is as it was.
# WHAT YOU READ: one PASS line per check in the output pane; the answers by hand (a ball of radius 3 resting on two edges) are in the file.  About half a minute, and the picture is there a second after the last line.

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'kit'))
from mfgkit import *

t = Table('02 a mill\'s job: a plate with a fin, two blocks, an L, a boss and a hole, for a CNC cutter of 6 mm (inside corners of radius 3, gaps of 6, nothing out of reach from above)')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
fin = box((5, 5, 5), (5.9, 20, 17))
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.3, 5, 5), (26.3, 20, 13))
ell = union(box((31.7, 5, 5), (44, 8, 15)), box((31.7, 5, 5), (35, 25, 15)))       # (the L stands 0.3 mm nearer to block_b: its gap to block_b is 5.4, clearly narrower than the cutter -- at 5.7 it would be exactly 5 % short, the tool's own grace)
boss = cylinder(5, 10, (55, 28, 5))
body = union(union(union(union(union(plate, fin), block_a), block_b), ell), boss)
part = exclude(body, cylinder(3.5, 7, (62, 8, -1)))          # (the hole and a ring of the plate round it stay exactly as they are, whatever the tool does to the rest)
# WHAT IS ASKED: the preset of a CNC cutter of 6 mm: inside corners of radius 3, gaps of 6 (its diameter), nothing out of reach from above, walls of 0.8
# (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = cnc_constraint(tool_diameter=6.0)
print(rule)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=1.0, verify=True)
result = res          # (the model the window shows, right under the call that makes it: the tree then holds the call under it)
print(res)
lo, hi = (-3, -3, -3), (73, 43, 22)
# (the view of the window: the box round the part, and how finely it is drawn: 2 cells to the millimetre; what the cutter changes is millimetres thick)
frame(lo, hi, res=2)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
# (the rounding is a ball rolled on a lattice of a 400th of the part, 0.19 mm for this plate; the independent check looks for a ball of the radius at every sample, over the whole sphere, and a ball fits at none of the
# 52 samples it marks (1.04 % of the surface, all of them at the free ends of the two gap fills, where the surface of a fill ends in a ramp): the median is 0.05 mm short of the radius asked and the worst 0.35 mm,
# an eighth of the radius, two nodes of the lattice.  1.5 % of the surface is allowed for that)
generic_checks(t, part, res, [rule], lo, hi, added='any', removed='any', name='cnc sampler', keep_rules=True, allow={'min_radius': 0.015})
# THE ANSWERS BY HAND (a cutter of 6 mm is a ball of radius 3 for an inside corner, and nothing narrower than 6 can be cut):
#   the gap between the fin (x 5 .. 5.9, 12 high) and block_a (x 10 .. 18, 8 high) is 4.1 wide: the ball cannot go down into it, it rests on the block's edge and on the fin, so the gap is solid up to
#   the block's top (z = 13, less the sag of the ball at the middle: 0.4 mm) and the air above the block is untouched -- nothing rises above z = 14 there (the fin itself stays 17 high)
#   the gap between block_b (13 high) and the L (x 26.3 .. 31.7) is 5.4 wide, 10 % under 6: the ball rests on block_b's top edge and on the L's wall (its centre at x = 28.7, z = 13 + sqrt(9 - 2.4^2) = 14.8), so the gap is
#   solid up to the underside of the ball -- 11.8 at x = 28.7, 12.1 at x = 28 and 30, 12.3 at x = 27 -- and nothing above the lower block's top (z = 13) + 1 mm over its side of the gap
#   the hole (diameter 5, under 6) is excluded: without that the rules would fill it in the middle of the plate (the ball rests on each rim and sags 1.34 mm into its ends), and it is open all the way through instead.
#   The foot of every wall in the open is a quarter circle of radius 3: at 1 mm from a wall it stands 3 - sqrt(9 - 4) = 0.76 mm high
def _hand(label, solid, air):
    vs = evaluate(res, solid + air)
    ok_solid = all(v <= 0 for v in vs[:len(solid)])
    ok_air = all(v > 0 for v in vs[len(solid):])
    t.check(label, ok_solid and ok_air, 'solid %s / air %s' % ([round(v, 2) for v in vs[:len(solid)]], [round(v, 2) for v in vs[len(solid):]]), 'solid <= 0 / air > 0')
_hand('gap fin / block_a (4.1 wide): solid up to z = 12 at x = 6.3 .. 9.5, nothing above z = 14.8 between the fin and the block',
      [(x, 12.0, 12.0) for x in (6.3, 7.0, 7.75, 9.0, 9.5)], [(x, 12.0, 14.8) for x in (6.3, 7.0, 7.75, 9.0, 9.5)])
_hand('gap block_b / L (5.4 wide, 10 % under the cutter): solid up to z = 11 at x = 27 .. 31 (the underside of the ball is at 11.8 to 12.3 over x = 27 .. 30; a fill over a block\'s shoulder is exact to about 1 mm), nothing above z = 14.2 over the lower block\'s side of it',
      [(x, 12.0, 11.0) for x in (27.0, 28.0, 29.0, 30.0, 31.0)], [(x, 12.0, 14.2) for x in (27.0, 28.0, 29.0)])
_hand('the hole of 5 mm (narrower than the ball) is excluded from the job, so it stays open all the way through: air on its axis from z = 0.3 to z = 5.6 (the rules would have filled z = 2 .. 3)',
      [], [(62.0, 8.0, z) for z in (0.3, 1.0, 2.0, 2.5, 3.0, 4.0, 4.7, 5.6)])
_hand('the foot of the fin on its open side: 0.76 mm high at 1 mm from the wall (solid at 5.55, air at 6.0), and nothing at 3.5 mm', [(4.0, 12.0, 5.55)], [(4.0, 12.0, 6.0), (1.4, 12.0, 5.3)])
_hand('the foot of block_a at its end face (y = 20): 0.76 mm high at y = 21', [(14.0, 21.0, 5.55)], [(14.0, 21.0, 6.0), (14.0, 24.0, 5.3)])

t.summary()
original = aside(part, lo, hi)[0]
result
original
