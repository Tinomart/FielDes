# Parametric optimization 3: does it find the coolest slab, a thermal problem whose answer is known?
#
# THE PROBLEM.  A slab held at 20 C at one end, heated with 5 W at the other, cooled by the air along its top.  Its thickness t is a var(),
# 5 to 25 mm; the width of the slab is fixed.  Make its highest temperature as low as possible.  The search starts from 10 mm.
#
# THE KNOWN BEST CASE.  A thicker slab carries the heat to the cold end through more material (a bigger cross-section) and has more
# surface to give heat to the air, so the highest temperature falls steadily as the slab gets thicker: the best design is the thickest the
# range allows, 25 mm, and the starting 10 mm is far from it.  (No formula is needed: the answer is the upper bound.)
#
# WHAT TO LOOK AT.  Three slabs in a row: the starting slab (front, plain), the optimizer's answer (middle, coloured by temperature) and the
# known best, a 25 mm slab (back, plain).  The output pane lists every design that was tried with its highest temperature.
from fieldes import *

view.set_bounds([-5, -60, -5], [65, 90, 30], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

print('STARTING design : thickness 10 mm')
print('KNOWN BEST      : thickness 25 mm (the upper bound of the range)')

t = var(10)
slab = box((0, 0, 0), (60, t, 10))
cold = fixed_temperature(box((-1, -1, -1), (1, 40, 11)), 20.0)
heat = heat_input(box((59, -1, -1), (61, 40, 11)), 5.0)
air = convection(box((10, -1, -1), (50, 40, 11)), 10e-6, ambient=20.0)
hot = thermal_analysis(slab, fixed_temperatures=[cold], heat_inputs=[heat], heat_generations=[], convections=[air],
                       material=aluminium, element_size=3)

best = parametric_optimization(hot, (t, 5, 25), minimize='max_temperature', max_evaluations=14)

starting_slab = move(box((0, 0, 0), (60, 10, 10)), (0, -40, 0))
known_best_slab = move(box((0, 0, 0), (60, 25, 10)), (0, 40, 0))
starting_slab
best
known_best_slab
