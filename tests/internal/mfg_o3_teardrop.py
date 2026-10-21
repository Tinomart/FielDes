# A horizontal hole of radius 5 in a block, an overhang of 45 degrees asked with overhang_fix=cut: a teardrop above the hole.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('o3_teardrop: a horizontal hole of radius 5 in a block, an overhang of 45 degrees asked with overhang_fix=cut: a teardrop above the hole')

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
