# A round boss on a plate, a radius of 2 mm asked where it meets the plate: a ring of fillet.
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

from mfgkit import *

t = Table('c5_boss: a round boss on a plate, a radius of 2 mm asked where it meets the plate: a ring of fillet')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((-20, -20, 0), (20, 20, 4)), cylinder(6, 14, (0, 0, 4)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_radius=2.0)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-12, -12, -2), (12, 12, 20)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
ring = (1 - math.pi / 4) * 4 * 2 * math.pi * (6 + 0.2234 * 2)
# (skin 0.05: the fillet is the distance to a mesh, good to about 0.03 mm; a skin of that size along the boss wall above the ring is not a change of the boss)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added=ring, tol=7.0, within=((-8.2, -8.2, 4), (8.2, 8.2, 6.2)), name='boss', skin=0.05)
# seen in the plane y = 0, to the right of the boss: the arc of the ball whose centre is 2 above the plate and 2 outside the boss
bad = []
for deg in range(10, 81, 10):
    a = math.radians(deg)
    mat = (8 - math.cos(a) * 2.2, 0.0, 6 - math.sin(a) * 2.2)
    air = (8 - math.cos(a) * 1.8, 0.0, 6 - math.sin(a) * 1.8)
    vm, va = evaluate(res, [mat, air])
    if vm > 0 or va <= 0:
        bad.append(deg)
t.check('boss: the fillet is the arc of radius 2 about (8, 6) at every 10 degrees (+-0.2 mm)', not bad, 'off at %s' % bad, 'none')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
