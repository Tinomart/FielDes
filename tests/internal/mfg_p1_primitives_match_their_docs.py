# The primitives the manufacturing tests are built from -- box, sphere, cylinder, cone and torus -- drawn as their documentation says: the volume of
# each, counted on points, against its formula (found while testing: the cone was not a cone).
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

t = Table('p1_primitives_match_their_docs: the primitives the manufacturing tests are built from -- box, sphere, cylinder, cone and torus -- drawn as their documentation says: the volume of each, counted on points, against its formula (found while testing: the cone was not a cone)')

# THE ANSWERS BY HAND, from the documentation of each primitive and the formula of its volume
shapes = [
    ('box 8 x 6 x 4', box((0, 0, 0), (8, 6, 4)), (0, 0, 0), (8, 6, 4), 8 * 6 * 4),
    ('sphere radius 5', sphere(5.0, (20, 5, 5)), (15, 0, 0), (25, 10, 10), 4.0 / 3.0 * math.pi * 125.0),
    ('cylinder radius 4, height 12', cylinder(4.0, 12.0, (35, 5, 0)), (31, 1, 0), (39, 9, 12), math.pi * 16.0 * 12.0),
    ('cone base radius 6, height 12', cone(6.0, 12.0, (50, 5, 0)), (43, -2, 0), (57, 12, 30), math.pi * 36.0 * 12.0 / 3.0),
    ('torus 8 and 2 (the ring of radius 8, the tube of radius 2)', torus(8.0, 2.0, (72, 5, 5)), (62, -5, 2), (82, 15, 8), 2.0 * math.pi ** 2 * 8.0 * 4.0),
]
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = None
for name, shape, lo_, hi_, volume in shapes:
    part = shape if part is None else union(part, shape)
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([-3, -8, -3], [88, 18, 16], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)
for name, shape, lo_, hi_, volume in shapes:
    pts = points_in(lo_, hi_, 120000, seed=3)
    inside = sum(1 for v in evaluate(shape, pts) if v <= 0)
    box_volume = (hi_[0] - lo_[0]) * (hi_[1] - lo_[1]) * (hi_[2] - lo_[2])
    got = box_volume * inside / 120000.0
    t.near('%s: the volume counted on points, against its formula (%.1f mm3)' % (name, volume), got, volume, 0.04 * volume + 1.0, ' mm3')
# the cone's own points: the tip is where the documentation puts it, and the base is as wide as asked
c = cone(6.0, 12.0, (50, 5, 0))
tip_in, tip_out = evaluate(c, [(50.0, 5.0, 11.8), (50.0, 5.0, 12.4)])
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the tip of the cone is at the height asked (inside at 11.8, outside at 12.4: %.2f / %.2f)' % (tip_in, tip_out), tip_in < 0 < tip_out, '', '')
base_in, base_out = evaluate(c, [(55.8, 5.0, 0.05), (56.3, 5.0, 0.05)])
t.check('the base of the cone is as wide as asked (inside at radius 5.8, outside at 6.3: %.2f / %.2f)' % (base_in, base_out), base_in < 0 < base_out, '', '')

t.summary()
result = part
result
