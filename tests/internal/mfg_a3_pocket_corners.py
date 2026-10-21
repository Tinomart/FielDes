# Six square pockets whose inside corners have radius 0, 1, 1.8, 2.7, 3 and 5 mm, for a rule of 3 mm: the check must mark the corners that are well
# short of the limit (under 2 mm), must not mark those at the radius or above (3 and 5 mm), and says what it does in the zone between (2.7 mm); never
# an outside corner.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - corners: the library fillet (a ball rolls in the inside corner)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('a3_pocket_corners: six square pockets whose inside corners have radius 0, 1, 1.8, 2.7, 3 and 5 mm, for a rule of 3 mm: the check must mark the corners that are well short of the limit (under 2 mm), must not mark those at the radius or above (3 and 5 mm), and says what it does in the zone between (2.7 mm); never an outside corner')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
radii = [0.0, 1.0, 1.8, 2.7, 3.0, 5.0]
block = box((0, 0, 0), (96, 16, 10))
for k, r in enumerate(radii):
    block = difference(block, rounded_pocket(2 + 15.5 * k, 2, 12, 12, r, -1, 11))        # (through the block: a pocket with a floor has a sharp floor edge, which is an inside corner of radius 0 too)
# (the corner rule reads the VALUE of the field up to the cutter's radius from the surface: a part made by differences of boxes whose cutters overshoot the block has a lower bound of the distance in its
# pockets, so it is read through exact_distance, as docs/manufacturing.md says)
block = exact_distance(block)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_radius=3.0)
lo, hi = (-3, -3, -3), (99, 19, 13)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
report = manufacturing_analysis(block, constraints=[rule], samples=9000)
print(report)
# THE ANSWER BY HAND, from what the rule says and how it is measured: a cutter of radius 3 cannot cut a corner sharper than 3.  What the check reads is how far from the part the centre of a ball of 3 that touches
# the corner can be: in a corner of 90 degrees and radius r that is r + (3 - r) / sqrt 2 = 2.12 + 0.29 r (2.91 for r = 2.7), and it is marked where it falls short of 3 by more than the grace of 5 %
# (0.15): 2.12 + 0.29 r < 2.85, that is r < 2.5.  A corner has to be about 17 % short of the limit to be marked: the clearance of a ball's centre grows by 0.29 mm for every mm of the corner's radius
for k, r in enumerate(radii):
    x0 = 2 + 15.5 * k
    if r > 0:
        c = (x0 + r, 2 + r)                                   # (the centre of the arc of the corner at x0, 2)
        p = (c[0] - r * 0.70710678, c[1] - r * 0.70710678, 6.0)
    else:
        p = (x0 + 1.0, 2.0, 6.0)                              # (a sharp corner: the wall 1 mm from the edge; at the edge itself the field has a kink that no step of a difference can read)
    m = margin_at(report, 'min_radius', [p])[0]
    if 2.0 < r < 2.95:
        # (the zone between about 83 % and 100 % of the radius asked: the reading of a tight arc is good to about 0.3 mm in the margin -- the normal it probes along is read from the field with a
        # stencil, and a degree off at a radius of 3 moves the ball by 0.05 mm, a few degrees by 0.15 -- so a corner here may or may not be marked and the case asks for neither; it says which)
        print('corner radius %.2f mm, in the zone the check cannot resolve (83 to 100 %% of the radius asked): margin %.3f, %s' % (r, m, 'marked' if m > 0 else 'left alone'))
        continue
    should = r < 2.0
    t.check('corner radius %.2f mm: the check %s it (probed at %s)' % (r, 'marks' if should else 'leaves alone', tuple(round(c, 2) for c in p)), (m > 0) == should, '%.3f' % m, '> 0' if should else '<= 0')
m_out = margin_at(report, 'min_radius', [(-0.01, -0.01, 5.0), (96.01, 16.01, 5.0), (48.0, -0.01, 5.0)])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the three outside corners and edges of the block are never marked', all(v <= 0 for v in m_out), ['%.3f' % v for v in m_out], '<= 0')
regions = report.report['min_radius']['regions']
pockets = [(2 + 15.5 * k, 2 + 15.5 * k + 12) for k in range(4)]                       # (the x range of the pockets of 0, 1, 1.8 and 2.7 mm: the last is in the zone the check cannot resolve)
inside = all(any(a_ - 0.3 <= g['at'][0] <= b_ + 0.3 for a_, b_ in pockets) for g in regions)
t.check('the report lists regions in the pockets of 0, 1 and 1.8 mm (and perhaps 2.7) and in no other, none in those of 3 and 5 mm (%d regions, each with its sample inside one of x = 2 .. 14, 17.5 .. 29.5, 33 .. 45, 48.5 .. 60.5)' % len(regions), len(regions) >= 3 and inside, '%d, inside: %s' % (len(regions), inside), '>= 3, inside')

t.summary()
result = report
original = aside(block, lo, hi)[0]
result
original
