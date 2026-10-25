# Thermal topology optimization: where should the metal be to carry the heat away?
#
# A 60 x 60 x 3 mm aluminium plate makes 6 W of heat all through its volume (heat_generation) and a 12 mm strip in the middle of its left edge is held at 20 C
# (fixed_temperature): the sink.  topology_optimization(analysis, constraints=[...]) -- the call of every other analysis -- designs the plate: minimize('input_temperature')
# makes the temperature where the heat is made (the heat-weighted mean, the usual goal of a heat sink) as low as it can be, and 'volume_fraction <= 0.3' lets the
# design keep 30 % of the plate.  The answer is the tree that every heat-conduction design finds: thick trunks from the sink, branching out to where the heat is made.
# What can be asked is what a thermal analysis reports: minimize('max_temperature'), a limit on 'max_heat_flux', on 'temperature(x, y, z)' at a point, on 'volume' or
# 'mass', or a formula of them -- all in the one list of constraints.
#
# The design is one density for each element of the tetrahedral mesh (1 metal, 0 nothing); the conductivity of an element is the metal's times a power of its density,
# and the temperature is solved once per iteration, with one adjoint solve for each thing asked, to move the density on the optimiser of every other design.
# extrude='z' makes the design the same through the thickness (a plate is drawn, not a sponge).  The design is kept symmetric about every plane the problem is symmetric
# about, as it is found.
#
# The result on its own shows the optimized part coloured by the density; the result card steps through the iterations (play to watch the tree grow).  .shape() is the
# optimized part, .analysis the thermal analysis of it (its numbers are the true ones), .history the objective per iteration, .values and .table() what was asked.
# The optimizer makes heat in the void too (the design space makes it, with the little conductivity a void has), so its numbers are higher than those of the part
# analysed again, where only the metal makes heat: the second is the part you would make.
# About half a minute; the next run reads the result back from the result cache at once.  Try: a chip on a block instead (heat_input(chip, 8.0) over a patch of the
# top face, fixed_temperature(base, 25) under it, minimize('max_temperature'), 'volume_fraction <= 0.3'): the metal gathers into a column under the chip and fans out
# to the cold face.
#
from fieldes import *

L, W, T = 60.0, 60.0, 3.0
view.set_bounds((-4, -4, -4), (L + 4, W + 4, T + 4), auto_scale=True)
view.set_resolution(5)
view.set_quality(8)

plate = box((0, 0, 0), (L, W, T))
sink = box((-1, 24, -1), (0.01, 36, T + 1))                       # a strip of the left edge, held at 20 C
heater = box((-1, -1, -1), (L + 1, W + 1, T + 1))                 # the heat is made all through the plate

plate_analysis = thermal_analysis(plate, [fixed_temperature(sink, 20)], [], [heat_generation(heater, 6.0)], [], aluminium, element_size=1.5)
design = topology_optimization(plate_analysis, constraints=[minimize('input_temperature'), 'volume_fraction <= 0.3'], extrude='z', iterations=60)
print('the plate itself: %.1f C where the heat is made (6 W through %.0f mm^3).  The design keeps %.0f %% of it: %.1f C (the optimizer, with heat made in the void too: %.1f C)' % (
    plate_analysis.input_temperature, plate_analysis.volume, 100.0 * design.analysis.volume / plate_analysis.volume, design.analysis.input_temperature, design.history[-1]))

design                                     # shown: the optimized part, coloured by the density, iteration by iteration
