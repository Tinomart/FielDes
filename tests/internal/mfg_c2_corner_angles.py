# Three pocket corners of 60, 90 and 120 degrees, for an inside corner radius of 3 mm: the rounding is the arc of a ball of radius 3 touching both
# faces, whatever the angle, and the faces beyond the arc are untouched.
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

t = Table('c2_corner_angles: three pocket corners of 60, 90 and 120 degrees, for an inside corner radius of 3 mm: the rounding is the arc of a ball of radius 3 touching both faces, whatever the angle, and the faces beyond the arc are untouched')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
r = 3.0
angles = [60.0, 90.0, 120.0]
part = None
for k, theta in enumerate(angles):
    th = math.radians(theta)
    h_a = half_space((0.0, -1.0, 0.0), (0.0, 0.0, 0.0))                              # (inside = y >= 0)
    h_b = half_space((-math.sin(th), math.cos(th), 0.0), (0.0, 0.0, 0.0))            # (inside = the side of the ray at the angle theta that holds the angles under it)
    air = intersection(intersection(h_a, h_b), box((-30, -30, -1), (30, 30, 11)))
    t.check('%d degrees: the two planes face the way the case assumes (a point of the wedge is inside both)' % theta,
            evaluate(h_a, [(3 * math.cos(th / 2), 3 * math.sin(th / 2), 5)])[0] < 0 and evaluate(h_b, [(3 * math.cos(th / 2), 3 * math.sin(th / 2), 5)])[0] < 0, '', '')
    block = move(difference(box((-15, -15, 0), (15, 15, 10)), air), (40.0 * k, 0, 0))
    part = block if part is None else union(part, block)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_radius=r)
lo, hi = (-18, -18, -3), (40.0 * 2 + 18, 18, 13)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
print(res)
# THE ANSWER BY HAND, from the geometry of a ball of radius r in the corner of two planes that make the angle theta: its centre is r / sin(theta/2) from the apex along the bisector, and it touches the
# faces r / tan(theta/2) from the apex; the surface of the filled corner on the bisector is r / sin(theta/2) - r from the apex
for k, theta in enumerate(angles):
    th = math.radians(theta)
    bis = (math.cos(th / 2), math.sin(th / 2), 0.0)
    ox = 40.0 * k
    start = (ox + 2 * r * bis[0], 2 * r * bis[1], 5.0)
    behind, ahead = run_length(res, start, (-bis[0], -bis[1], 0.0), 2 * r, 8000, inside=False)
    d_new = r / math.sin(th / 2) - r
    t.near('%d degrees: the air along the bisector from 6 mm out to the new surface (the corner is filled up to %.3f mm from the apex, so %.3f mm of air is left)' % (theta, d_new, 2 * r - d_new),
           ahead, 2 * r - d_new, 0.15, ' mm')
    tangent = r / math.tan(th / 2)
    far = tangent + 1.5
    p_face = (ox + far, 0.05, 5.0)                                              # (on the face along the ray at angle 0, beyond the arc, a hair into the air)
    a, b = evaluate(part, [p_face])[0], evaluate(res, [p_face])[0]
    t.check('%d degrees: beyond the arc (%.2f mm from the apex along a face) the face is untouched (field %.4f before, %.4f after)' % (theta, far, a, b), abs(a - b) < 0.01, '%.4f' % abs(a - b), '< 0.01')
cmp_ = compare(part, res, lo, hi, n=200000)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
frags = floating_clusters(cmp_)
t.check('no piece of added material floats', not frags, '%d piece(s): %s' % (len(frags), ' '.join('%d points in %s' % (len(c), tuple(tuple(round(v, 1) for v in corner) for corner in box_of(c))) for c in frags[:3])), 0)
# (the part is made by a difference whose cutter overshoots the block, so the corner rule reads it through exact_distance, as docs/manufacturing.md says for such a part)
# (the arcs are exactly the radius asked, which is the limit: a tight arc read through a mesh, at its ends where it meets the faces, shows a few points short -- docs/manufacturing.md, KNOWN_ISSUES.md)
residue_check(t, 'corners', part, res, [rule], samples=6000, exact=True, share=0.4, limit=0.01, what='read through exact_distance; what is left is at the creases where the arcs meet the faces')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
