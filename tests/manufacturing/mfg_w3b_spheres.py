# Thin curved walls of 0.5 mm, hollow spheres of radius 6 and 12, a wall of 0.8 mm asked: a wall that curves two ways.
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

from mfgkit import *

t = Table('w3b_spheres: thin curved walls of 0.5 mm, hollow spheres of radius 6 and 12, a wall of 0.8 mm asked: a wall that curves two ways')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
defs = [('sphere r 6', sphere(6.0, (0, 0, 6)), sphere(5.5, (0, 0, 6)), (6.0, 0.0, 6.0), 8),
        ('sphere r 12', sphere(12.0, (0, 0, 12)), sphere(11.5, (0, 0, 12)), (12.0, 0.0, 12.0), 14)]

result, original = curved_shells(t, defs, [(0, 0), (30, 0)], ([-16, -16, -3], [48, 16, 28]))

t.summary()
result
original
