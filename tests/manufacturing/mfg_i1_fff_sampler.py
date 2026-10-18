# A plate with a thin fin, a narrow slot, an L, a boss and a hole, for a 3D printer with a 0.4 mm nozzle (walls 0.8, gaps 0.4).
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - the rules of the 3D printer preset (walls, gaps, bridges)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('i1_fff_sampler: a plate with a thin fin, a narrow slot, an L, a boss and a hole, for a 3D printer with a 0.4 mm nozzle (walls 0.8, gaps 0.4)')

# THE PART (written out here: boxes and cylinders, nothing computed): a plate 70 x 40 x 5 with a hole, a fin 0.5 thick, two blocks 0.3 apart, an L of two walls, a round boss
plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
fin = box((5, 5, 5), (5.5, 20, 17))
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.3, 5, 5), (26.3, 20, 13))
ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
boss = cylinder(5, 10, (55, 28, 5))
part = union(union(union(union(union(plate, fin), block_a), block_b), ell), boss)
# WHAT IS ASKED: the preset of a printer with a 0.4 mm nozzle: walls of 0.8, gaps of 0.4, bridges up to 20 mm
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = fff_constraint(nozzle=0.4)
print(rule)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=1.0)
print(res)
lo, hi = (-3, -3, -3), (73, 43, 22)
# (drawn at 2 cells to the millimetre, for speed: the layers added -- 0.15 mm on the fin, 0.3 mm in the slot -- are thinner than a cell and are NOT drawn in red, although the card counts them;
# w4_open_box, w5_disc and w7_perforated draw such layers at 14 to 16 cells to the millimetre.  docs/manufacturing.md says so)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
# NO ANSWER BY HAND for a part like this: what is checked is that nothing floats beside the part, that the tool's numbers are the measured ones and that the independent check of the result finds only what the tool lists.  JUDGE THE PICTURE.
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added='any', removed='any', name='fff sampler', keep_rules='listed')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
