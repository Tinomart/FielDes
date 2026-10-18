# J1, J3, J4: the topology-optimized bracket (the optimizer shaped it for a point load, half the material) made ready for THREE processes -- a 0.4 mm
# FFF printer, a 3-axis mill with a 6 mm cutter, an injection mould (ABS) -- each judged by the kit's own judge against the published rules of that
# process: how much of the surface breaks each rule before and after, the faces that carry the load untouched, what the tool says is left.
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

from mfgkit import *

t = Table("j1_bracket_three_processes: J1, J3, J4: the topology-optimized bracket (the optimizer shaped it for a point load, half the material) made ready for THREE processes -- a 0.4 mm FFF printer, a 3-axis mill with a 6 mm cutter, an injection mould (ABS) -- each judged by the kit's own judge against the published rules of that process: how much of the surface breaks each rule before and after, the faces that carry the load untouched, what the tool says is left")

design, plates, lugs = _bracket_parts()
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
opt = topology_optimization(design, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))], material=aluminium, volume_fraction=0.50, element_size=5)
solid = exact_distance(opt.shape())
lo, hi = (-78, -92, -63), (78, 38, 31)
shift = 1.2 * (hi[0] - lo[0])
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([lo[0] - 5, lo[1] - 5, lo[2] - 5], [hi[0] + 2 * shift + 5, hi[1] + 5, hi[2] + 5], auto_scale=True)
view.set_resolution(1)
view.set_quality(8)
boxes = [((-76, 5, -61), (-45, 36, 29)), ((45, 5, -61), (76, 36, 29)), ((-60, -90, -61), (60, -60, 29))]
jobs = [
    # the rules and their sources are in JOB_TESTS.md: FFF wall 0.8 (two perimeters of a 0.4 nozzle), overhang 45, a gap under 0.3 prints closed; CNC wall 0.8 for a metal, inside radius at least a
    # third of the depth -- the cutter's own 3 mm radius at least --, nothing out of reach along the cutter; moulding ABS wall 1.1 to 3.6, draft 1 degree (Protolabs), no undercut along the pull
    ('FFF printer, nozzle 0.4', fff_constraint(nozzle=0.4, overhang=45), {'wall': 0.8, 'overhang': 45.0, 'up': (0, 0, 1), 'gap': 0.3}),
    ('CNC mill, cutter 6', cnc_constraint(tool_diameter=6), {'wall': 0.8, 'radius': 3.0, 'undercut': (0, 0, 1)}),
    ('injection mould, ABS', molding_constraint(min_wall=1.5, draft=1.0), {'wall': 1.1, 'draft': 1.0, 'pull': (0, 0, 1), 'undercut': (0, 0, 1)}),
]
results = []
for k, (label, rule, rules) in enumerate(jobs):
    res = manufacturing_optimization(solid, constraints=[rule], keep=[plates, lugs], max_change=0.25, max_support_height=10)
    print(label, res)
    results.append(res)
    job_table(t, label, solid, res, lo, hi, rules, count=2000)
    _keep_unchanged(t, label, solid, res, boxes, lo, hi)
    t.check('%s: the tool changed %.1f %% of the part, under its budget of 25 %%' % (label, 100.0 * res.share), res.share <= 0.25 + 1e-9, '%.3f' % res.share, '<= 0.25')

t.summary()
result = results[0]
original = move(solid, (shift, 0, 0))
result
original
