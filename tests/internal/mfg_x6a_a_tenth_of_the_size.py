# The fin of 0.6 mm and the slot of 0.2 mm of h4, every length times 0.1 (a fin of 0.06 mm, rules of 0.08 and 0.04 mm): the tool does the same thing
# at a tenth of the size.
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

t = Table('x6a_a_tenth_of_the_size: the fin of 0.6 mm and the slot of 0.2 mm of h4, every length times 0.1 (a fin of 0.06 mm, rules of 0.08 and 0.04 mm): the tool does the same thing at a tenth of the size')



result, original = scaled_fin_and_slot(t, 0.1)

t.summary()
result
original
