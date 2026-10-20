# The real part: the topology-optimized bracket (an optimizer shaped it: struts of every thickness, leaning every way), for the same printer.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                      - the rules of the 3D printer preset (walls, gaps, bridges)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('x1_topology_bracket: the real part: the topology-optimized bracket (an optimizer shaped it: struts of every thickness, leaning every way), for the same printer')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
parts = import_model(_step_file('PivotBearingSupportBracket.STEP'))
space, (slo, shi) = parts[0]
box_roi = ((-75.01, -85.07, -60.02), (75.01, 35.06, 28))
plates = union(box((-76, 5, -61), (-45, 36, 29)), box((45, 5, -61), (76, 36, 29)))
lugs = box((-60, -90, -61), (60, -60, 29))
holes = union(box((-48, -88, -22), (-26, -44, 22)), box((26, -88, -22), (48, -44, 22)))
design = exclude(space, holes)
opt = topology_optimization(design, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))], material=aluminium, volume_fraction=0.50, element_size=5)
optimized = opt.shape()
solid = exact_distance(optimized)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = fff_constraint(nozzle=0.4, overhang=45)
print(manufacturing_analysis(solid, constraints=[rule]))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(solid, constraints=[rule], max_change=0.25, max_support_height=10)
print(res)
lo, hi = (-78, -92, -63), (78, 38, 31)
shift = 1.2 * (hi[0] - lo[0])
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([lo[0] - 5, lo[1] - 5, lo[2] - 5], [hi[0] + shift + 5, hi[1] + 5, hi[2] + 5], auto_scale=True)
view.set_resolution(1)
view.set_quality(8)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
complicated_checks(t, solid, res, rule, lo, hi, 'topology bracket', n=30000)

t.summary()
result = res
original = move(solid, (shift, 0, 0))
result
original
