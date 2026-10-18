# A thin cone and a thin ring (torus) of 0.5 mm, a wall of 0.8 mm asked: a wall that turns along the axis, and a wall round a tube.
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

t = Table('w3c_cone_and_ring: a thin cone and a thin ring (torus) of 0.5 mm, a wall of 0.8 mm asked: a wall that turns along the axis, and a wall round a tube')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
cone_slope = 12.0 / 25.0
cone_in = 0.5 * math.sqrt(1.0 + cone_slope ** 2)               # (the cone's wall is 0.5 along its normal: the inner cone is that much lower, measured straight in)
defs = [('cone', cone(12.0, 25.0, (0, 0, 0)), cone(12.0 - cone_in * 1.0, 25.0 - cone_in * 25.0 / 12.0, (0, 0, 0)), (7.2, 0.0, 10.0), 14, lambda z: z < 0.4 or z > 24.0),
        ('ring (torus)', torus(12.0, 4.0, (0, 0, 4)), torus(12.0, 3.5, (0, 0, 4)), (16.0, 0.0, 4.0), 18)]

result, original = curved_shells(t, defs, [(0, 0), (40, 0)], ([-16, -18, -3], [62, 18, 30]))

t.summary()
result
original
