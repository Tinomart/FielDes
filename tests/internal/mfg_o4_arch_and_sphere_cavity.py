# A tunnel with a round roof (walls 8 mm high, a half-circle of radius 6 on top) and a closed spherical cavity of radius 6, an overhang of 45 degrees
# asked with overhang_fix=cut: the roof of each becomes a teardrop, a pointed arch and a cone, with its apex 6 sqrt 2 = 8.49 mm above the centre and
# its faces leaning 45 degrees.
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
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('o4_arch_and_sphere_cavity: a tunnel with a round roof (walls 8 mm high, a half-circle of radius 6 on top) and a closed spherical cavity of radius 6, an overhang of 45 degrees asked with overhang_fix=cut: the roof of each becomes a teardrop, a pointed arch and a cone, with its apex 6 sqrt 2 = 8.49 mm above the centre and its faces leaning 45 degrees')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
arch_block = difference(box((0, 0, 0), (30, 20, 30)), union(box((9, -1, 4), (21, 21, 12)), rotate_x(cylinder(6, 22, (15, 12, -21)), math.pi / 2)))
cavity_block = difference(box((40, 0, 0), (70, 20, 30)), sphere(6.0, (55, 10, 15)))
part = union(arch_block, cavity_block)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the tunnel is where the case builds it: air at its springline, material above its roof', evaluate(part, [(15, 10, 12)])[0] > 0 and evaluate(part, [(15, 10, 19)])[0] <= 0, '', '')
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, overhang_fix='cut')
lo, hi = (-3, -3, -3), (73, 23, 33)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=10, max_change=0.5)
print(res)
# THE ANSWER BY HAND: the faces of the teardrop are the tangents to the circle that lean 45 degrees from the vertical; they meet over the centre at r sqrt(2) = 8.485 (the circle's top is at r = 6).  The area
# cut from the section is the kite (r x r = 36) less the sector of 90 degrees (pi r^2 / 4 = 28.27): 7.73 mm2, over the 20 mm of the tunnel: 154.5 mm3.  The sphere has the cone of the same tangents: its volume is
# the cone from the apex to the circle of tangency (pi r^3 / (6 sqrt 2)) less the cap of the sphere above that circle (pi h^2 (3 r - h) / 3, h = r (1 - 1/sqrt 2)): 0.127 r^3 = 27.4 mm3
below, above = run_length(res, (15, 10, 12), (0, 0, 1), 12.0, 8000, inside=False)
t.near('tunnel: the air above the springline reaches the apex, 6 sqrt 2 = 8.485 mm', above, 6 * math.sqrt(2), 0.25, ' mm')
t.near('tunnel: the walls below the springline are as they were, 12 mm across at z = 8', sum(run_length(res, (15, 10, 8), (1, 0, 0), 9.0, 6000, inside=False)), 12.0, 0.08, ' mm')
for u in (1.5, 3.0, 4.0):
    mat, air = evaluate(res, [(15 + u + 0.25, 10, 12 + 6 * math.sqrt(2) - u), (15 + u - 0.25, 10, 12 + 6 * math.sqrt(2) - u)])
    t.check('tunnel: the face of the teardrop leans 45 degrees: %.1f mm under the apex it is %.1f mm to the side (material 0.25 outside, air 0.25 inside)' % (u, u), mat <= 0 < air, '%.3f / %.3f' % (mat, air), '<= 0 / > 0')
below_c, above_c = run_length(res, (55, 10, 15), (0, 0, 1), 12.0, 8000, inside=False)
t.near('cavity: the air above the centre reaches the apex, 6 sqrt 2 = 8.485 mm', above_c, 6 * math.sqrt(2), 0.3, ' mm')
t.near('cavity: the air below the centre is the sphere still (6 mm)', below_c, 6.0, 0.15, ' mm')
for u in (1.5, 3.0):
    mat, air = evaluate(res, [(55 + u + 0.25, 10, 15 + 6 * math.sqrt(2) - u), (55 + u - 0.25, 10, 15 + 6 * math.sqrt(2) - u)])
    t.check('cavity: the cone leans 45 degrees: %.1f mm under the apex it is %.1f mm to the side' % (u, u), mat <= 0 < air, '%.3f / %.3f' % (mat, air), '<= 0 / > 0')
a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=200000, n_dense=200000)
cmp_ = compare(part, res, lo, hi, n=240000)
pts_t, vol_t = [p for p, _ in cmp_['removed'] if p[0] < 35], 0.0
vol_t = len(pts_t) * cmp_['box'] / float(cmp_['n'])
pts_k = [p for p, _ in cmp_['removed'] if p[0] >= 35]
vol_k = len(pts_k) * cmp_['box'] / float(cmp_['n'])
t.near('tunnel: the material cut (by hand 154.5 mm3)', vol_t, 154.5, 22.0, ' mm3')
t.near('cavity: the material cut (by hand 27.4 mm3)', vol_k, 27.4, 8.0, ' mm3')
t.check('nothing added', a < 2.0, '%.3g mm3' % a, 0)
residue_check(t, 'tunnel and cavity', part, res, [rule], samples=8000)

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
