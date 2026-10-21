# The same thin wall (0.6 mm, a 0.8 mm wall asked) turned about the vertical axis and tilted, 12 ways: does the tool care which way a wall faces?.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('m2_orientation: the same thin wall (0.6 mm, a 0.8 mm wall asked) turned about the vertical axis and tilted, 12 ways: does the tool care which way a wall faces?')

# THE PART: one wall 0.6 mm thick, 16 long and 24 high, its middle at the origin; it is turned and tilted 12 ways and put at 12 places of one picture
wall = box((-0.3, -8, -12), (0.3, 8, 12))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_wall=0.8)
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
turns = [(0, 0), (15, 0), (30, 0), (45, 0), (60, 0), (75, 0), (90, 0), (0, 20), (30, 20), (60, 20), (45, 40), (120, 35)]     # (turned about z, then tilted about y), degrees
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([-25, -25, -20], [4 * 45, 3 * 45, 40], auto_scale=True)
view.set_resolution(4)
view.set_quality(8)
results = []
for k, (az, ay) in enumerate(turns):
    cx, cy, cz = 45.0 * (k % 4), 45.0 * (k // 4), 14.0
    turned = move(rotate_z(rotate_y(wall, math.radians(ay)), math.radians(az)), (cx, cy, cz))
    # (the way the wall faces now, by the turns just made.  The kernel's rotate_y turns the other way round from the right-handed rot_y of this file, so the tilt is taken with its sign changed:
    # the first run of this case measured the wall 1.052 mm along a normal 40 degrees off for a tilt of 20, 0.8 / cos 40 degrees, and 4.6 for a tilt of 40.  The two checks below hold the normal
    # against the field itself -- the gradient on the face, and the wall's inside and outside -- so that no convention is trusted)
    n = rot_z(rot_y((1.0, 0.0, 0.0), -ay), az)
    lo, hi = (cx - 20, cy - 20, cz - 20), (cx + 20, cy + 20, cz + 20)
    inside_in, outside_out = evaluate(turned, [(cx + 0.25 * n[0], cy + 0.25 * n[1], cz + 0.25 * n[2]), (cx + 0.5 * n[0], cy + 0.5 * n[1], cz + 0.5 * n[2])])
    name = 'turned %d tilted %d' % (az, ay)
    g = normal_of(turned, (cx + 0.3 * n[0], cy + 0.3 * n[1], cz + 0.3 * n[2]))
    t.check('%s: the normal the turns say is the gradient of the field on the face (their dot product)' % name, abs(sum(a * b for a, b in zip(n, g))) > 0.999, round(abs(sum(a * b for a, b in zip(n, g))), 5), '> 0.999')
    t.check('%s: the wall faces the way the turns say (inside at 0.25 mm along the normal, outside at 0.5)' % name, inside_in < 0 < outside_out, (round(inside_in, 3), round(outside_out, 3)), '(< 0, > 0)')
    res = manufacturing_optimization(turned, constraints=[rule], max_change=3.0)
    results.append(res)
    behind, ahead = run_length(res, (cx, cy, cz), n, 3.0, 6000)
    t.near('%s: the wall at its middle, measured along its normal (0.8 asked)' % name, behind + ahead, 0.8, 0.04, ' mm')
    added, removed, _ = changed_volumes(turned, res, lo, hi, n_coarse=80000, n_dense=80000)
    t.near('%s: the material added (0.2 x 16 x 24 = 76.8)' % name, added, 76.8, 9.0, ' mm3')
    cmp_ = compare(turned, res, lo, hi, n=80000)
    t.check('%s: no piece of added material floats beside the wall' % name, not floating_clusters(cmp_), '', 0)
    after = manufacturing_analysis(res, constraints=[rule], samples=3000)
    if not after.compliant:
        print(after)
    t.check('%s: the independent check finds at most 2 %% of the surface still thin' % name, after.report['min_wall']['share'] <= 0.02, '%.3g' % after.report['min_wall']['share'], '<= 0.02')

t.summary()
result = results
result
