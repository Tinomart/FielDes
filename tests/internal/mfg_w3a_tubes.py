# Thin curved walls of 0.5 mm, tubes of radius 3 and 10, a wall of 0.8 mm asked: a wall that curves about one axis, tight and slack.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - (the checks of the part: see below)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('w3a_tubes: thin curved walls of 0.5 mm, tubes of radius 3 and 10, a wall of 0.8 mm asked: a wall that curves about one axis, tight and slack')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
defs = [('tube r 3', cylinder(3.0, 12, (0, 0, 0)), cylinder(2.5, 14, (0, 0, -1)), (3.0, 0.0, 6.0), 6),
        ('tube r 10', cylinder(10.0, 12, (0, 0, 0)), cylinder(9.5, 14, (0, 0, -1)), (10.0, 0.0, 6.0), 12)]

result, original = curved_shells(t, defs, [(0, 0), (26, 0)], ([-14, -14, -3], [40, 14, 16]))

t.summary()
result
original
