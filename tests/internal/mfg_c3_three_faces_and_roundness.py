# A blind pocket with sharp corners (three faces meet at each bottom corner), a through pocket whose corners are rounded to 1.5 mm and one rounded to
# 5 mm, an inside corner radius of 3 mm asked: the sharp pocket gets rounded vertical edges, floor edges and a sphere at each of its four bottom
# corners, the 1.5 mm one is made 3 mm, the 5 mm one is not touched.
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
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('c3_three_faces_and_roundness: a blind pocket with sharp corners (three faces meet at each bottom corner), a through pocket whose corners are rounded to 1.5 mm and one rounded to 5 mm, an inside corner radius of 3 mm asked: the sharp pocket gets rounded vertical edges, floor edges and a sphere at each of its four bottom corners, the 1.5 mm one is made 3 mm, the 5 mm one is not touched')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
block = box((0, 0, 0), (100, 40, 12))
pocket_a = box((6, 13, 4), (20, 27, 13))                                                  # (blind: the floor at z = 4, sharp: the corners of three faces)
part = difference(difference(difference(block, pocket_a), rounded_pocket(35, 13, 14, 14, 1.5, -1, 13)), rounded_pocket(65, 13, 14, 14, 5.0, -1, 13))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_radius=3.0)
lo, hi = (-3, -3, -3), (103, 43, 15)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
# THE ANSWER BY HAND, from the ball that rolls in the corner: a ball of radius 3 touching the three faces of a bottom corner has its centre 3 mm from each of them, and the filled corner is the sphere of
# that ball: seen from the centre, the corner point is 3 sqrt(3) = 5.196 away, the sphere surface is 3 away.  The edges are the arcs of the same ball in the planes across them.
s3 = math.sqrt(3.0)
bad = []
for sx, sy, cx, cy in ((1, 1, 6, 13), (-1, 1, 20, 13), (1, -1, 6, 27), (-1, -1, 20, 27)):
    centre = (cx + 3.0 * sx, cy + 3.0 * sy, 4.0 + 3.0)
    mat = (centre[0] - sx * 3.2 / s3, centre[1] - sy * 3.2 / s3, centre[2] - 3.2 / s3)
    air = (centre[0] - sx * 2.8 / s3, centre[1] - sy * 2.8 / s3, centre[2] - 2.8 / s3)
    vm, va = evaluate(res, [mat, air])
    if vm > 0 or va <= 0:
        bad.append((cx, cy))
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('blind pocket: the corner of three faces is the sphere of the ball (material at 3.2, air at 2.8 mm from its centre, towards the corner) at all four bottom corners', not bad, 'off at %s' % bad, 'none')
arc_checks(t, res, (9.0, 16.0), 3.0, step=15, z=9.0, towards=(-1, -1), name='blind pocket, vertical corner (6, 13)')
arc_checks(t, res, (17.0, 24.0), 3.0, step=15, z=9.0, towards=(1, 1), name='blind pocket, vertical corner (20, 27)')
bad_floor = []
for deg in range(10, 81, 10):
    a = math.radians(deg)
    mat = (9.0 - math.cos(a) * 3.2, 20.0, 7.0 - math.sin(a) * 3.2)
    air = (9.0 - math.cos(a) * 2.8, 20.0, 7.0 - math.sin(a) * 2.8)
    vm, va = evaluate(res, [mat, air])
    if vm > 0 or va <= 0:
        bad_floor.append(deg)
t.check('blind pocket: the edge between the floor and the wall at x = 6 is the arc of the ball (radius 3 about (9, 7)) at every 10 degrees', not bad_floor, 'off at %s' % bad_floor, 'none')
arc_checks(t, res, (38.0, 16.0), 3.0, step=15, z=6.0, towards=(-1, -1), name='pocket of 1.5 mm, corner (35, 13), made 3 mm')
arc_checks(t, res, (70.0, 18.0), 5.0, step=15, z=6.0, towards=(-1, -1), name='pocket of 5 mm, corner (65, 13), left as it was')
cmp_ = compare(part, res, lo, hi, n=200000)
pts_b, vol_b = added_in(cmp_, (34, 12, -1), (50, 28, 13))
t.near('pocket of 1.5 mm: the material added, four corners (1 - pi/4) x (9 - 2.25) x 12 = 4 x 1.4486 x 12', vol_b, 4 * (1 - math.pi / 4) * (9.0 - 2.25) * 12.0, 7.0, ' mm3')
pts_c, vol_c = added_in(cmp_, (64, 12, -1), (80, 28, 13))
# (left as it was, to a skin of 0.01 mm over its wall: the fillet's distance is the distance to a mesh, good to about 0.03 mm, and a point of this cloud stands for 0.44 mm3; the wall is the 14 x 14 pocket with
# corners of radius 5, perimeter 4 (14 - 10) + 2 pi 5, 12 deep)
wall_c = (4.0 * (14.0 - 10.0) + 2.0 * math.pi * 5.0) * 12.0
t.check('pocket of 5 mm: left as it was, to a skin of 0.01 mm over its wall of %.0f mm2 (%.2f mm3 added)' % (wall_c, vol_c), vol_c < 0.01 * wall_c, '%.2f mm3' % vol_c, '< %.2f' % (0.01 * wall_c))
pts_a, vol_a = added_in(cmp_, (5, 12, 3), (21, 28, 13))
hand = 4 * (1 - math.pi / 4) * 9.0 * 5.0 + 4 * (1 - math.pi / 4) * 9.0 * 8.0 + 4 * (27.0 - 4.0 / 3.0 * math.pi * 27.0 / 8.0)
t.near('blind pocket: the material added, 4 vertical edges (length 5) + 4 floor edges (length 8) + 4 corners (a cube of 3 less an eighth of the ball): by hand %.1f' % hand, vol_a, hand, 0.09 * hand, ' mm3')
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
# (a fillet is an arc of exactly the radius of the ball: the ball touches it all along and its centre is at the arc's own centre, so a normal that is a degree or two off -- as that of a field made from a mesh is --
# moves the centre off by a twentieth of a millimetre, and a share of the points reads short: the tool lists them, and the check marks a third of what it marked before, under 2 % of the surface)
residue_check(t, 'pockets', part, res, [rule], samples=9000, exact=True, share=0.4, limit=0.02, what='read through exact_distance, as docs/manufacturing.md says for a part made by differences; the places left are tight arcs and creases')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
