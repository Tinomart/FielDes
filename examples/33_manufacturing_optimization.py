# Manufacturing optimization: a real part made easier to make.
#
# THE PART.  The bracket that comes with FielDes (step/PivotBearingSupportBracket.STEP) is the design space, and a topology optimization shapes it: the stiffest bracket in half of the material,
# held at its two bolting plates and pulled by 2 kN at its lugs (example 08).  What comes out is what an optimizer makes: struts of every thickness leaning every way.  Nothing about it was
# written by hand, and nothing in it was designed for any process.
#
# THE PROBLEM.  A 3D printer cannot print a strut that leans too far over air, a wall thinner than its nozzle or a gap narrower than its line.  A mill's cutter cannot reach into a sharp inside
# corner.  A mould cannot release a wall that has no draft or a ledge that hooks.  manufacturing_analysis(part, ...) shows where a part breaks the rules of a process;
# manufacturing_optimization(part, ...) changes the part until it keeps them -- and says what it changed and what it could not.
#
# THE TOOL.  One call for each process, on the bracket as the optimizer made it.  keep=[...] are the regions that carry the load and are never touched: the two bolting plates and the lugs
# (the two bolt holes were excluded when the part was imported, so they stay as they are as well).  max_change is the most of the part the tool may change.  verify=True checks the result
# again, on points of its surface, against the rules.
#
# WHAT YOU SEE.  The bracket for the printer (left), grey where it is as the optimizer made it, red where the tool added material (a gusset under an overhang that leans too far, a thin wall
# thickened) and blue where it cut; and the bracket as the optimizer made it (right), to compare.  Turn them with Shift + left drag; View > Right and Front show the underside of the arms.  The result's
# card lists what was added and cut and the places that still break a rule and need a support.  The mill's and the mould's results are models of the tree as well: click the eye of `mill` or
# `mould` to look at them.
#
# About four minutes the first time: the optimization takes about a minute, the printer about 20 seconds, the mill about a minute and the mould about two. Run the script again and the optimization is read back from the cache, which leaves about three minutes for the three processes.
from fieldes import *

parts = import_model("step/PivotBearingSupportBracket.STEP")
space, (lo, hi) = parts[0]
# hidden: space

# the box of the part, and where the second bracket stands beside the first
box_lo, box_hi = (-78, -92, -63), (78, 38, 31)
shift = 1.2 * (box_hi[0] - box_lo[0])
view.set_bounds([box_lo[0] - 5, box_lo[1] - 5, box_lo[2] - 5], [box_hi[0] + 2 * shift + 5, box_hi[1] + 5, box_hi[2] + 5], auto_scale=True)
view.set_resolution(1)
view.set_quality(8)

# the bolting plates (held), the lug ends (the pull acts here) and the two bolt holes with the ring of material round each: they stay as they are
plates = union(box((-76, 5, -61), (-45, 36, 29)),
               box((45, 5, -61), (76, 36, 29)))
# hidden: plates
lugs = box((-60, -90, -61), (60, -60, 29))
# hidden: lugs
holes = union(box((-48, -88, -22), (-26, -44, 22)),
              box((26, -88, -22), (48, -44, 22)))
# hidden: holes
design = exclude(space, holes)
# hidden: design

# the bracket as the optimizer makes it from the loads and the supports alone
opt_stress = static_analysis(design, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))], material=aluminium, element_size=5)
opt = topology_optimization(opt_stress, constraints=[minimize('compliance'), 'volume_fraction <= 0.5'])
# hidden: opt
bracket = opt.shape()
# hidden: bracket

# THE PRINTER: a 0.4 mm nozzle, a layer that may lean 45 degrees over the one below it.  Walls are thickened, narrow gaps closed, gussets grown under what leans too far
printer_rule = fff_constraint(nozzle=0.4, overhang=45)
printer = manufacturing_optimization(bracket, constraints=[printer_rule], keep=[plates, lugs], max_change=0.25, max_support_height=10, verify=True)
result = printer
print("3D printer, nozzle 0.4 mm:", printer)

# THE MILL: a cutter of 6 mm cannot cut an inside corner sharper than its own radius, nor a gap narrower than itself
mill_rule = cnc_constraint(tool_diameter=6)
mill = manufacturing_optimization(bracket, constraints=[mill_rule], keep=[plates, lugs], max_change=0.25, max_support_height=10, verify=True)
print("CNC mill, cutter 6 mm:", mill)

# THE MOULD: ABS walls of at least 1.5 mm, 1 degree of draft on every upright wall, nothing that hooks against the pull
mould_rule = molding_constraint(min_wall=1.5, draft=1.0)
mould = manufacturing_optimization(bracket, constraints=[mould_rule], keep=[plates, lugs], max_change=0.25, max_support_height=10, verify=True)
print("Injection mould, ABS:", mould)

original = move(bracket, (shift, 0, 0))
result
original
