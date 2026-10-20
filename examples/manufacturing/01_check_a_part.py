# 01  THE CHECK: where does a part break what a 3D printer asks of it?
#
# WHAT THIS SHOWS.  manufacturing_analysis reads a part and says where it breaks the rules of a process -- here a printer with a 1.0 mm nozzle (a large-format printer's: walls at least 2.0 mm, gaps at
# least 1.0 mm, bridges at most 20 mm) -- as colours on the part itself and as numbers: which rule, how much of the surface, how badly, in which places.  Nothing is changed.
#
# THE PART: a plate with a thin fin (1.2 mm), two blocks only 0.7 mm apart, an L of two 3 mm walls, a round boss and a 5 mm hole.  Only the fin (too thin) and the slot between the
# blocks (too narrow) break the rules; the L, the boss and the hole are fine and must stay unmarked.
#
# WHAT YOU SEE.  The part is red where a rule is broken and plain where they are kept (the result card on the right lets you switch to one rule).  Turn it with Shift + left drag, or use
# View > Top / Front / Right.  WHAT YOU READ: the output pane prints the report, then one PASS line for every check.  The last check runs the same part against a relaxed printer
# (a 0.1 mm nozzle): nothing is broken, which shows that the check marks only what is wrong.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('01 the check: a plate with a thin fin, a narrow slot, an L, a boss and a hole, against a 1.0 mm nozzle')

# THE PART (boxes and cylinders, written out by hand)
plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
fin = box((5, 5, 5), (6.2, 20, 17))                        # 1.2 mm thick
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.7, 5, 5), (26.7, 20, 13))                # 0.7 mm from block_a
ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
boss = cylinder(5, 10, (55, 28, 5))
part = union(union(union(union(union(plate, fin), block_a), block_b), ell), boss)

# WHAT THE PRINTER ASKS
rule = fff_constraint(nozzle=1.0)

# THE CHECK
report = manufacturing_analysis(part, constraints=[rule])
print(report)
lo, hi = (-3, -3, -3), (73, 43, 22)
frame(lo, hi, res=4)                   # (4 cells to the millimetre: the fin is 1.2 mm and the slot 0.7 mm, and a picture coarser than they are closes them)


def near(point, box_lo, box_hi, slack=1.5):
    return all(box_lo[i] - slack <= point[i] <= box_hi[i] + slack for i in range(3))


# THE ANSWERS BY HAND: the fin is the only thin wall (x 5 .. 6.2, y 5 .. 20, up to z = 17), the slot between the blocks the only narrow gap (x 18 .. 18.7, y 5 .. 20, z 5 .. 13)
wall = report.report['min_wall']
gap = report.report['min_gap']
t.check('the thin fin is found: %.2g %% of the surface is under the wall rule' % (100 * wall['share']), wall['share'] > 0, '%.4f' % wall['share'], '> 0')
t.check('every place under the wall rule is on the fin (%d place(s))' % len(wall['regions']), all(near(g['at'], (5, 5, 5), (6.2, 20, 17)) for g in wall['regions']),
        [tuple(round(c, 1) for c in g['at']) for g in wall['regions']], 'on the fin')
t.check('the narrow slot is found: %.2g %% of the surface is under the gap rule' % (100 * gap['share']), gap['share'] > 0, '%.4f' % gap['share'], '> 0')
t.check('every place under the gap rule is in the slot between the blocks (%d place(s))' % len(gap['regions']), all(near(g['at'], (18, 5, 5), (18.7, 20, 13)) for g in gap['regions']),
        [tuple(round(c, 1) for c in g['at']) for g in gap['regions']], 'in the slot')
t.check('the L, the boss and the hole are not marked', not any(near(g['at'], (32, 5, 5), (44, 25, 15)) or near(g['at'], (50, 23, 5), (60, 33, 15)) or near(g['at'], (59.5, 5.5, 0), (64.5, 10.5, 5))
                                                            for r in report.report.values() for g in r['regions']), '', 'none marked')
relaxed = manufacturing_analysis(part, constraints=[fff_constraint(nozzle=0.1)])
t.check('the same part against a 0.1 mm nozzle breaks no rule', relaxed.compliant, 'compliant' if relaxed.compliant else 'rules broken', 'compliant')

t.summary()
result = report
result
