# A rounded box 30 x 20 x 15 with 3 mm edges: a smooth part that keeps every rule.
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
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('n5_rounded_box: a rounded box 30 x 20 x 15 with 3 mm edges: a smooth part that keeps every rule')

# (rounded_box(a, b, r) takes the radius as a fraction of HALF the shortest side: r = 0.4 of 7.5 mm is the 3 mm of the title.  Written as 3 it was a radius of 22.5 mm, a diamond a few
# millimetres across -- which the tool rightly listed as breaking six rules; the picture of the first run showed it)
part = rounded_box((0, 0, 0), (30, 20, 15), 0.4)
lo, hi = (-3, -3, -3), (33, 23, 18)
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
res = control(t, part, lo, hi, 'rounded box')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
