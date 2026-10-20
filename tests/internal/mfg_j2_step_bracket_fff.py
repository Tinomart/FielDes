# J2: the STEP bracket of the examples -- a part nobody designed for printing -- made ready for a 0.4 mm FFF printer, judged by the kit's own judge
# against the published rules (wall 0.8, overhang 45, a gap under 0.3 prints closed): the share of the surface that breaks each rule before and after,
# and the places that were fine left as they were.
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

t = Table("j2_step_bracket_fff: J2: the STEP bracket of the examples -- a part nobody designed for printing -- made ready for a 0.4 mm FFF printer, judged by the kit's own judge against the published rules (wall 0.8, overhang 45, a gap under 0.3 prints closed): the share of the surface that breaks each rule before and after, and the places that were fine left as they were")

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
parts = import_model(_step_file('PivotBearingSupportBracket.STEP'))
bracket, (blo, bhi) = parts[0]
lo, hi = tuple(c - 3 for c in blo), tuple(c + 3 for c in bhi)
# (an import has a lower bound of the distance for its field -- its walls read thinner than they are --: read through exact_distance, as docs/manufacturing.md says, for the tool and the judge alike)
bracket = exact_distance(bracket)
shift = 1.2 * (hi[0] - lo[0])
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([lo[0] - 5, lo[1] - 5, lo[2] - 5], [hi[0] + shift + 5, hi[1] + 5, hi[2] + 5], auto_scale=True)
view.set_resolution(1)
view.set_quality(8)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = fff_constraint(nozzle=0.4, overhang=45)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(bracket, constraints=[rule], max_change=0.25, max_support_height=10)
print(res)
job_table(t, 'STEP bracket, FFF', bracket, res, lo, hi, {'wall': 0.8, 'overhang': 45.0, 'up': (0, 0, 1), 'gap': 0.3}, count=2500)
# the part was not made for printing, and most of it was fine: the tool must leave what was fine as it was -- the share of the volume that changed is small
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('STEP bracket: most of the part was fine and is as it was (the tool changed %.2f %% of the volume)' % (100.0 * res.share), res.share < 0.15, '%.3f' % res.share, '< 0.15')

t.summary()
result = res
original = move(bracket, (shift, 0, 0))
result
original
