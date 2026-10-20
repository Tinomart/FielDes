# 18  A TOPOLOGY-OPTIMIZED BRACKET made ready for a printer, a mill and a mould
#
# WHAT THIS SHOWS.  The optimizer makes a bracket from nothing but loads and supports: struts of every thickness leaning every way, nothing about it written by hand and nothing designed for any
# process.  The same part is made ready for a 0.4 mm printer (walls, gaps, overhang), a mill with a 6 mm cutter (inside corners, reach) and an injection mould (draft, undercut, walls, inside corners); the plates
# and lugs that carry the load and the two bolt-hole regions are kept untouched.  Each result is judged against the published rules of its process, on points of the surface, before and after.
#
# WHAT YOU SEE.  The bracket as the optimizer made it (right) and the result for the printer (left).  WHAT YOU READ: one PASS line for each check, for each of the three processes.
# About six minutes (the optimization about a minute, the tool a minute for each process, the judge).

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table("18 a topology-optimized bracket: made ready for a printer, a mill and a mould, the part handed to the tool as it is, judged by the kit's judge")

def judge(label, res, rules):
    # (the kit's judge: what a result is asked, written once for the three processes -- it stands up here so that no block of Python lies between the models below and what is made of them in the tree)
    print(label, res)
    job_table(t, label, solid, res, lo, hi, rules, count=2000, boxes=boxes)
    _keep_unchanged(t, label, solid, res, boxes, lo, hi)
    t.check('%s: the tool changed %.1f %% of the part, under its budget of 25 %%' % (label, 100.0 * res.share), res.share <= 0.25 + 1e-9, '%.3f' % res.share, '<= 0.25')

design, plates, lugs = _bracket_parts()
opt = topology_optimization(design, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))], material=aluminium, volume_fraction=0.50, element_size=5)
solid = opt.shape()
lo, hi = (-78, -92, -63), (78, 38, 31)
shift = 1.2 * (hi[0] - lo[0])
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([lo[0] - 5, lo[1] - 5, lo[2] - 5], [hi[0] + 2 * shift + 5, hi[1] + 5, hi[2] + 5], auto_scale=True)
view.set_resolution(1)
view.set_quality(8)
boxes = BRACKET_KEPT          # (the plates, the lugs and the two bolt-hole regions: the part is kept as it is there, so nothing is asked of their surfaces)
# WHAT IS ASKED, AND THE TOOL RUNS HERE, once for each process: the rules of the process (the only conditions the tool is given: no loads, supports or material of its own -- it works on the shape alone), then the
# bracket as the optimizer made it goes in with the plates and the lugs kept as they are.  Each rule and each call is a statement of its own, written right under the one it is made from, so that the model tree
# holds the rule and the bracket under the call, and the call under `result`
printer_rule = fff_constraint(nozzle=0.4, overhang=45)
printer = manufacturing_optimization(solid, constraints=[printer_rule], keep=[plates, lugs], max_change=0.25, max_support_height=10, verify=True)
result = printer
mill_rule = cnc_constraint(tool_diameter=6)
mill = manufacturing_optimization(solid, constraints=[mill_rule], keep=[plates, lugs], max_change=0.25, max_support_height=10, verify=True)
mould_rule = molding_constraint(min_wall=1.5, draft=1.0)
mould = manufacturing_optimization(solid, constraints=[mould_rule], keep=[plates, lugs], max_change=0.25, max_support_height=10, verify=True)
original = move(solid, (shift, 0, 0))
# THE CHECKS: the kit's judge reads each result on points of its surface (what is kept is not judged), the numbers are the rules of the process
for label, res, rules in (('FFF printer, nozzle 0.4', printer, {'wall': 0.8, 'overhang': 45.0, 'up': (0, 0, 1), 'gap': 0.3}),
                          ('CNC mill, cutter 6', mill, {'wall': 0.8, 'radius': 3.0, 'undercut': (0, 0, 1)}),
                          ('injection mould, ABS', mould, {'wall': 1.1, 'draft': 1.0, 'pull': (0, 0, 1), 'undercut': (0, 0, 1), 'radius': 0.75})):
    judge(label, res, rules)

t.summary()
result
original
