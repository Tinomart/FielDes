# J8: the topology-optimized bracket made ready for THREE processes -- a 0.4 mm FFF printer, a 3-axis mill with a 6 mm cutter, an injection mould (ABS) -- with the optimizer's part handed to
# the tool as it is (`opt.shape()`), the way a person hands it over after `topology_optimization`.  It is j1 with one line changed: j1 wraps the part in `exact_distance` first, which is a walk of about
# 32 calls of the part's field at every point and does not finish in the time a test should take (KNOWN_ISSUES.md).  Here the tool's lattices read the part directly: the tool takes about a minute for each
# process, and the result is judged by the kit's own judge against the published rules of that process, exactly as in j1.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         the bracket the optimizer makes (half the material, a point load), `opt.shape()`: nothing about it is computed by hand
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open
#   answers by hand  the rules of each process, from their sources (JOB_TESTS.md): FFF wall 0.8, overhang 45, a gap under 0.3 prints closed; CNC wall 0.8, inside radius at least 3, nothing out of reach
#                    along the cutter; moulding ABS wall 1.1 to 3.6, draft 1 degree, no undercut along the pull
#   the checks       helpers in mfgkit.py / mfgtest.py: the judge counts, on points of the surface, how much of it breaks each rule before and after; the faces that carry the load must be untouched
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).
# Time: about 8 minutes (the optimization about a minute, the tool a minute or two for each process, the judge, the first drawing).

from mfgkit import *

t = Table("j8_bracket_three_processes_direct: the topology-optimized bracket made ready for a printer, a mill and a mould, the part handed to the tool as it is, judged by the kit's judge")

design, plates, lugs = _bracket_parts()
opt = topology_optimization(design, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))], material=aluminium, volume_fraction=0.50, element_size=5)
solid = opt.shape()
lo, hi = (-78, -92, -63), (78, 38, 31)
shift = 1.2 * (hi[0] - lo[0])
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([lo[0] - 5, lo[1] - 5, lo[2] - 5], [hi[0] + 2 * shift + 5, hi[1] + 5, hi[2] + 5], auto_scale=True)
view.set_resolution(1)
view.set_quality(8)
boxes = [((-76, 5, -61), (-45, 36, 29)), ((45, 5, -61), (76, 36, 29)), ((-60, -90, -61), (60, -60, 29))]
jobs = [
    ('FFF printer, nozzle 0.4', fff_constraint(nozzle=0.4, overhang=45), {'wall': 0.8, 'overhang': 45.0, 'up': (0, 0, 1), 'gap': 0.3}),
    ('CNC mill, cutter 6', cnc_constraint(tool_diameter=6), {'wall': 0.8, 'radius': 3.0, 'undercut': (0, 0, 1)}),
    ('injection mould, ABS', molding_constraint(min_wall=1.5, draft=1.0), {'wall': 1.1, 'draft': 1.0, 'pull': (0, 0, 1), 'undercut': (0, 0, 1)}),
]
results = []
for k, (label, rule, rules) in enumerate(jobs):
    res = manufacturing_optimization(solid, constraints=[rule], keep=[plates, lugs], max_change=0.25, max_support_height=10, verify=False)
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
