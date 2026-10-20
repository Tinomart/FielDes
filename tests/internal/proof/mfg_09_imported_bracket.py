# Manufacturing 9: a real part.  The bracket imported from a STEP file, checked and changed for a 3D printer.
#
# Every other script in this folder is made of boxes whose answers can be worked out by hand.  This one imports the bracket of the
# examples (150 x 120 x 88 mm, 275 cm^3; the STEP file is the one in examples/step) and runs the same two
# tools on it, for a printer with a 0.4 mm nozzle (walls 0.8, gaps 0.4, bridges 20) that also asks for no overhang over 45 degrees.
#
# There is no answer worked out by hand for this part: look at where the report puts the broken places and judge whether they are the
# places of the bracket that a printer would struggle with (and whether anything is flagged that is not).  The shell changes the geometry
# and colours what changed; run manufacturing_analysis on `better` to check it again.
# A part of this size takes minutes: the first import alone is about 20 s, and the shell asks the part's field for its value at many places.
from fieldes import *

parts = import_model("../../examples/step/PivotBearingSupportBracket.STEP")
bracket, (lo, hi) = parts[0]

view.set_bounds(*roi(parts), auto_scale=True)
view.set_resolution(1)
view.set_quality(8)

printer = fff_constraint(nozzle=0.4, overhang=45)
report = manufacturing_analysis(bracket, constraints=[printer])
print(report)
better = manufacturing_optimization(bracket, constraints=[printer], max_support_height=10)
better
