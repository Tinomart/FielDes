# Parametric optimization 6: designs that cannot be solved, a limit nothing can keep, and apply=True.
#
# THE PROBLEM.  A bar 12 mm deep, held at one end and pulled down at the other with 200 N.  Its thickness `thin` is a var() with a range
# from 0.05 to 20 mm.  Make it as light as possible while it stays under 90 MPa.  The search starts from 10 mm.
#
# WHAT IT SHOULD DO.
#   1. The thinnest designs of the range (a twentieth of a millimetre) have no material for the mesh to hold: they are listed as FAILED in the
#      output pane and the search goes on to the others.
#   2. It finds the thinnest bar that keeps 90 MPa.  Beam theory puts that at  6 P L / (90 h^2) = about 9.3 mm (the finite elements read the
#      stress at the support a little differently, so the answer may be a little off).  The starting 10 mm is just above it.
#   3. apply=True: when the run is over, the number it found is WRITTEN INTO THE TEXT of this script, into `thin = var(10)` below.  Look at
#      that line in the editor after the run.
#   4. The second run asks for 1 MPa or less, which no thickness of the range can keep.  It says so ("NO design keeps every constraint") and
#      gives the design that breaks the limit least.
#
# WHAT TO LOOK AT.  The starting bar (front, plain) and the optimizer's answer (coloured by stress).
from fieldes import *

view.set_bounds([-5, -35, -5], [105, 25, 25], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

P, L, h = 200.0, 100.0, 12.0
print('KNOWN BEST by beam theory: thickness %.2f mm for 90 MPa' % (6.0 * P * L / (90.0 * h * h)))
print('STARTING design : thickness 10 mm')

thin = var(10)
bar = box((0, 0, 0), (100, thin, 12))
wall = fixed(box((-1, -1, -1), (1, 60, 60)))
pull = force(box((99, -1, -1), (101, 60, 60)), (0, 0, -200))
stress = static_analysis(bar, supports=[wall], loads=[pull], material=aluminium, element_size=4)

best = parametric_optimization(stress, (thin, 0.05, 20), minimize='mass', constraints='max_von_mises <= 90',
                               max_evaluations=16, apply=True)
impossible = parametric_optimization(stress, (thin, 8, 20), minimize='mass', constraints='max_von_mises <= 1', max_evaluations=16)
print('=== the run that asks for 1 MPa:')
print(impossible)

starting_bar = move(box((0, 0, 0), (100, 10, 12)), (0, -25, 0))
starting_bar
best
