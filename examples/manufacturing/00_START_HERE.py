from fieldes import *
view.set_bounds([-5, -7, -4], [3, 2, 5], auto_scale=True)

# START HERE: MANUFACTURING.
#
# FielDes checks a part against what a process asks of it (a 3D printer, a mill, an injection mould) and changes the part until it keeps the rules.  The files of this folder are ONE EXAMPLE FOR
# EACH THING IT DOES.  Open one (File > Open example file..., then the folder `manufacturing`; or drag it onto the window): it builds a part, runs the tool, draws the result -- grey where the part is as it was, red
# where material was added, blue where it was cut -- and prints one PASS or FAIL line for each check in the output pane.  Every file is written to be read from the top: the part, what is asked
# of it, the call of the tool, the answers worked out by hand, the checks.  Look at the picture first; the numbers are an aid.  Turn the part with Shift + left drag; View > Top / Front / Right.
#
#   01_check_a_part.py                 THE CHECK.  manufacturing_analysis marks where a part breaks the rules of a printer (a 1.0 mm nozzle: walls of 2.0, gaps of 1.0), as colours and as numbers, and changes nothing.
#   02_printer_walls_and_gaps.py       A 3D PRINTER'S JOB (a 1.0 mm nozzle).  A thin fin is thickened to the wall the nozzle needs, a narrow slot is closed, what was fine is left alone.
#   03_mill_inside_corners.py          A MILL'S JOB.  A 6 mm cutter cannot cut a sharp inside corner or a gap narrower than itself: corners are rounded to its radius, gaps are filled.
#   04_mould_draft_and_undercut.py     AN INJECTION MOULD.  Draft on every upright wall, undercuts filled, thin walls thickened.
#   05_overhang_gusset.py              AN OVERHANG, FILLED.  A ledge past the printer's angle gets a gusset under it: the part is changed so that it needs no support.
#   06_overhang_teardrop.py            AN OVERHANG, CUT.  A round hole becomes a teardrop, which prints without support.
#   07_supports.py                     SUPPORTS, THE LAST RESORT.  Where changing the part is not enough, a support block is added and kept apart from the part.
#   08_print_files.py                  THE FILES FOR A SLICER.  The part turned the way it is printed, its supports and the places where none may go, written as one 3MF; the layers as DXF; a slice as SVG.
#   09_gyroid_walls.py                 A LATTICE.  The walls of a gyroid sheet lattice (a surface that turns every way) are thickened to the rule, with clean edges.
#   10_free_build_direction.py         THE PART TURNS.  Left free, the way up of the print is found and the part is drawn standing that way.
#   11_budget.py                       THE BUDGET.  A remedy that would change more of the part than `max_change` allows is not made, and the tool says so.
#   12_keep_a_region.py                WHAT MUST NOT CHANGE.  A region given in `keep=` is never touched.
#   13_rule_in_one_region.py           A RULE IN ONE REGION.  `region=` holds a rule in one part of the part only.
#   14_choose_the_remedy.py            CHOOSE THE REMEDY.  `remedies=` says which remedies may be used for a rule.
#   15_second_pass.py                  A SECOND PASS.  The result handed to the tool again: what the first pass left is made good.
#   16_what_cannot_be_fixed.py         WHAT THE TOOL CANNOT FIX.  It says so and lists where; the check of its result is independent of it.
#   17_step_bracket.py                 A REAL PART.  A bracket from a STEP file made ready for a printer.
#   18_topology_bracket.py             A TOPOLOGY-OPTIMIZED BRACKET made ready for a printer, a mill and a mould.  The optimizer made a part with struts of every thickness leaning every
#                                      way: nothing about it is written by hand.
#
# The folder `kit` holds the helper the checks use (a tape measure through a result's field, material counted on points); you do not need it.

print("Open one of the files listed above.")
