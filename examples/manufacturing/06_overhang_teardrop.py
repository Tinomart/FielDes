# 06  AN OVERHANG: a round hole becomes a teardrop
#
# WHAT THIS SHOWS.  The roof of a horizontal round hole leans out more and more towards the top, so a printer sags there.  Asked with overhang_fix='cut', the tool does the other thing to the same
# rule: it CUTS the air above the hole into a teardrop whose roof leans at the angle asked (45 degrees), so every layer of the roof lies on the one under it.
#
# WHAT YOU SEE.  The block with its round hole (right) and with the teardrop (left), the material cut in blue.  Turn it to look along the hole.  WHAT YOU READ: one PASS line per check; the shape
# of the roof is compared with the ideal 45 degree cap in the file.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('06 an overhang cut: a horizontal hole of radius 5 in a block, an overhang of 45 degrees asked with overhang_fix=cut: a teardrop above the hole')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
block = box((0, 0, 0), (40, 20, 30))
hole = rotate_x(cylinder(5, 30, (20, 15, -25)), math.pi / 2)
part = difference(block, hole)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, overhang_fix='cut')
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=10, max_change=0.5)
lo, hi = (-3, -3, -3), (43, 23, 33)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
cut = (1 - math.pi / 4) * 25 * 20
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added=None, removed=cut, tol=12.0, within=((14, 0, 15), (26, 20, 23)), name='teardrop', keep_rules=False)
residue_check(t, 'teardrop', part, res, [rule])
below, above = run_length(res, (20, 10, 15), (0, 0, 1), 12.0, 6000, inside=False)
t.near('teardrop: the air above the middle of the hole reaches the apex (centre + radius x sqrt 2 = 7.07)', above, 5 * math.sqrt(2), 0.2, ' mm')
t.near('teardrop: the air below the middle is still the circle (5)', below, 5.0, 0.1, ' mm')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
