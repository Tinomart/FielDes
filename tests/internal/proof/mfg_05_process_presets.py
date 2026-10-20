# Manufacturing 5: do the presets ask the right things of a part?
#
# Each preset is a manufacturing_constraint with the numbers of a process written in.  The same part is checked for all three, so that the
# difference between them shows:
#
#   fff_constraint(nozzle=0.4, overhang=45)      min_wall 0.8, min_gap 0.4, max_bridge 20, overhang 45
#   cnc_constraint(tool_diameter=6)              min_radius 3 (the cutter's radius), min_gap 6 (its diameter), undercut_along z, min_wall 0.8
#   molding_constraint(min_wall=1.0, draft=1.0)  min_wall 1.0, draft 1 degree, undercut_along z (the pull), min_radius 0.5 (half the wall)
#
# THE PART (the broken part of mfg_01 without the bridge): a fin 0.6 thick, a 0.3 mm slot, an L wall with a sharp inside corner, a shelf that
# sticks out over nothing, a block with a groove in its side, on a plate.  WHAT EACH PROCESS SHOULD FLAG:
#   fff      the fin (0.6 < 0.8), the slot (0.3 < 0.4, narrower than the nozzle), the shelf's underside (overhang 45); NOT the groove (a printer
#            does not mind an undercut), NOT the L corner (no radius rule)
#   cnc      the fin, the slot (0.3 < 6), every inside corner (sharper than radius 3), the groove (an undercut along z); NOT the shelf (no overhang rule)
#   molding  the fin (0.6 < 1.0), every vertical wall (less than 1 degree of draft), the groove, the inside corners (sharper than 0.5)
# The output pane prints the three constraints and the three reports.  The viewport shows the part with the shell run for the process named
# in `show` (change it to 'fff', 'cnc' or 'molding' and run it again).
from fieldes import *

show = 'cnc'

view.set_bounds([-5, -105, -5], [110, 95, 50], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

plate = box((0, 0, 0), (105, 90, 5))
fin = box((10, 10, 5), (10.6, 22, 25))
gap_a = box((25, 10, 5), (35, 22, 20))
gap_b = box((35.3, 10, 5), (45.3, 22, 20))
l_long = box((60, 10, 5), (90, 14, 25))
l_short = box((60, 10, 5), (64, 40, 25))
wall = box((10, 45, 5), (14, 75, 40))
shelf = box((14, 45, 25), (34, 75, 28))
block = difference(box((45, 45, 5), (75, 75, 35)), box((44, 52, 17), (51, 68, 23)))
part = union(plate, fin, gap_a, gap_b, l_long, l_short, wall, shelf, block)

processes = {
    'fff': fff_constraint(nozzle=0.4, overhang=45),
    'cnc': cnc_constraint(tool_diameter=6),
    'molding': molding_constraint(min_wall=1.0, draft=1.0),
}
for name, preset in processes.items():
    print('=== ' + name + ': ' + repr(preset))
    print(manufacturing_analysis(part, constraints=[preset]))

better = manufacturing_optimization(part, constraints=[processes[show]], max_support_height=25)
original = move(part, (0, -100, 0))
original
better
