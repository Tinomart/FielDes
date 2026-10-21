from fieldes import *
view.set_bounds([-5, -7, -4], [3, 2, 5], auto_scale=True)

# START HERE: MANUFACTURING.
#
# FielDes checks a part against what a process asks of it (a 3D printer, a mill, an injection mould) and changes the part until it keeps the rules.  There is ONE FILE FOR EACH PROCESS, and one for a real part.
# Open one (File > Open example file..., then the folder `manufacturing`; or drag it onto the window): it builds a part, runs the tool, draws the result -- grey where the part is as it was, red where material
# was added, blue where it was cut, beside the part as it was -- and prints one PASS or FAIL line for each check in the output pane.  Every file is written to be read from the top: the part, what is asked
# of it, the call of the tool, the answers worked out by hand, the checks.  Look at the picture first; the numbers are an aid.  Turn the part with Shift + left drag; View > Top / Front / Right.
#
#   01_printer.py         A 3D PRINTER'S JOB (a 1.0 mm nozzle).  The check first: where the part breaks the rules, as colours and as numbers.  Then the tool: a thin fin thickened, a narrow slot closed, a gusset grown
#                         under a ledge, a teardrop cut above a round hole; what was fine -- an L, a boss, a hole, a wall -- left exactly as it was.
#   02_mill.py            A MILL'S JOB (a cutter of 6 mm).  A cutter cannot cut a sharp inside corner or a gap narrower than itself: corners are rounded to its radius, gaps are filled, a hole that is the design is kept.
#   03_mould.py           AN INJECTION MOULD.  Draft on every upright wall, undercuts filled, thin walls thickened.
#   04_real_part.py       A TOPOLOGY-OPTIMIZED BRACKET made ready for a printer, a mill and a mould.  The optimizer made a part with struts of every thickness leaning every way: nothing about it is written by hand.
#
# The folder `kit` holds the helper the checks use (a tape measure through a result's field, material counted on points); you do not need it.

print("Open one of the files listed above.")
