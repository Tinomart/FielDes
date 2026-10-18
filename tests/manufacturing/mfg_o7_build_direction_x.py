# The ledge of o1 turned so that the print stands on the x axis (the part with its x and z exchanged, the way up (1, 0, 0) given): the same gusset
# must come out as in o1.
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

from mfgkit import *

t = Table('o7_build_direction_x: the ledge of o1 turned so that the print stands on the x axis (the part with its x and z exchanged, the way up (1, 0, 0) given): the same gusset must come out as in o1')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (40, 40, 10)), box((20, 0, 10), (25, 40, 30)))              # (o1 with x and z exchanged: the wall is a slab at the foot, the ledge a rib standing out of it)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, overhang_fix='fill', build_direction=(1, 0, 0))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=25, max_change=0.6)
lo, hi = (-3, -3, -3), (43, 43, 33)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
print(res)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=200000, n_dense=200000)
t.between('the gusset volume: the same as o1 (the two hand answers are 5083 and 5333)', a, 4700, 5500, ' mm3')
t.check('nothing cut', r < 5.0, '%.3g' % r, 0)
residue_check(t, 'way up along x', part, res, [rule])

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
