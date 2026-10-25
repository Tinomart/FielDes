# J7: the same job on a second shape: the bracket the optimizer makes for a different load (the load turned to the side), made ready for the same
# three processes, judged in the same way: nothing the first bracket showed is a property of that one part.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                      - draft: _remedy_draft (a wall that leans too little is offset in proportion to its depth)
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                      - the rules of the 3D printer preset (walls, gaps, bridges)
#                      - the rules of the CNC preset (corners, gaps, undercuts, walls)
#                      - the rules of the moulding preset (walls, draft, undercuts, corners)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('j7_second_bracket: J7: the same job on a second shape: the bracket the optimizer makes for a different load (the load turned to the side), made ready for the same three processes, judged in the same way: nothing the first bracket showed is a property of that one part')

design, plates, lugs = _bracket_parts()
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
opt_stress = static_analysis(design, supports=[fixed(plates)], loads=[force(lugs, (1500, -1500, 0))], material=aluminium, element_size=5)
opt = topology_optimization(opt_stress, constraints=[minimize('compliance'), 'volume_fraction <= 0.4'])
solid = exact_distance(opt.shape())
lo, hi = (-78, -92, -63), (78, 38, 31)
shift = 1.2 * (hi[0] - lo[0])
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([lo[0] - 5, lo[1] - 5, lo[2] - 5], [hi[0] + 2 * shift + 5, hi[1] + 5, hi[2] + 5], auto_scale=True)
view.set_resolution(1)
view.set_quality(8)
boxes = BRACKET_KEPT          # (the plates, the lugs and the two bolt-hole regions: kept as they are, so not judged)
results = []
for label, rule, rules in (
        ('FFF printer, nozzle 0.4', fff_constraint(nozzle=0.4, overhang=45), {'wall': 0.8, 'overhang': 45.0, 'up': (0, 0, 1), 'gap': 0.3}),
        ('CNC mill, cutter 6', cnc_constraint(tool_diameter=6), {'wall': 0.8, 'radius': 3.0, 'undercut': (0, 0, 1)}),
        ('injection mould, ABS', molding_constraint(min_wall=1.5, draft=1.0), {'wall': 1.1, 'draft': 1.0, 'pull': (0, 0, 1), 'undercut': (0, 0, 1)})):
    res = manufacturing_optimization(solid, constraints=[rule], keep=[plates, lugs, _bracket_holes()], max_change=0.25, max_support_height=10)          # (the part is held as exact_distance, which carries no locks: the two hole regions are given as keep=)
    print(label, res)
    results.append(res)
    job_table(t, label + ', second bracket', solid, res, lo, hi, rules, count=2000, boxes=boxes)
    _keep_unchanged(t, label + ', second bracket', solid, res, boxes, lo, hi)

t.summary()
result = results[0]
original = move(solid, (shift, 0, 0))
result
original
