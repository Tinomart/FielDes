# Thermal topology optimization against the one layout whose best design is known exactly: a bar that conducts heat.
#
# THE PROBLEM.  A bar of aluminium 40 mm long and 10 x 10 mm in section, held at 20 C across one end, with 10 W entering across the other and nothing leaving its sides.  Find the layout of 50 % of the bar that
# keeps the end where the heat comes in coolest.
#
# THE EXACT ANSWER (derived here, no outside source claimed).  Through every cross-section of the bar the whole power Q flows, and the temperature rise from the cold end to the hot one is the integral of
# Q / (k A(x)) along the bar, A(x) the area of material across it.  For a given volume that integral is least when A(x) is the same everywhere (Jensen's inequality), so the coolest layout of volume fraction f
# has the temperature at the heat input
#
#     T(f) = 20 + Q L / (k A f),       and for the whole bar (f = 1)  T(1) = 20 + Q L / (k A)        k = 0.167 W/(mm K) for aluminium.
#
# No layout can be cooler than T(f): a lower bound.  WHAT THE SCRIPT DOES: solves the whole bar and checks T(1) (to 5 % of the rise); runs topology_optimization for 50 % (80 iterations: the design is sharpened a
# little at every iteration, and 40 leave a bar that is not yet one piece); reads the volume fraction the cut part really has and the temperature at the input in that part's own analysis; and stops with an
# AssertionError when the rise is smaller than the bound by more than 3 % (it cannot be: an error of the program) or more than 30 % larger (the optimizer must land on a nearly uniform bar).  THE BOUND USED IS THE
# WHOLE BAR'S OWN RISE DIVIDED BY THE VOLUME FRACTION, the same model with the same ends (the formula's Q L / (k A f) is printed beside it), so that the effect of the ends cancels.  About a minute.
from fieldes import *

L, b, h = 40.0, 10.0, 10.0           # mm: the bar
Q, FRACTION, T_COLD = 10.0, 0.5, 20.0
k = aluminium.conductivity           # W/(mm K)
A = b * h

bar = box((L, b, h), position=(L / 2, b / 2, h / 2))
cold = box((2, b + 2, h + 2), position=(0, b / 2, h / 2))
hot = box((2, b + 2, h + 2), position=(L, b / 2, h / 2))
sink = thermal_analysis(bar, fixed_temperatures=[fixed_temperature(cold, T_COLD)], heat_inputs=[heat_input(hot, Q)], heat_generations=[], convections=[],
                        material=aluminium, element_size=2.0, cache=False)
rise_whole_exact = Q * L / (k * A)
rise_whole = float(sink.input_temperature) - T_COLD
print('the whole bar: the temperature at the heat input rises %.3f K above the cold end, exact Q L / (k A) = %.3f K (%+.2f %%)' % (rise_whole, rise_whole_exact, 100 * (rise_whole / rise_whole_exact - 1)))
if abs(rise_whole / rise_whole_exact - 1) > 0.05:
    raise AssertionError('the rise of the whole bar is %.1f %% from Q L / (k A)' % (100 * (rise_whole / rise_whole_exact - 1)))

input_temperature_1 = sink.input_temperature
volume_fraction_1 = sink.volume_fraction
best = topology_optimization(sink, conditions=[minimize(input_temperature_1), keep_below(volume_fraction_1, FRACTION)], iterations=80, cache=False)
part = best.analysis
f_part = float(part.volume) / float(sink.volume)             # the share of the bar that the cut part really has: the volumes of the two meshes
rise_part = float(part.input_temperature) - T_COLD
rise_bound = rise_whole / f_part
print('the optimized part keeps %.1f %% of the bar (asked %.0f %%); the rise %.3f K; the coolest layout of that volume, the whole bar\'s %.3f K / f = %.3f K (%+.1f %%); the formula Q L / (k A f) gives %.3f K' % (
    100 * f_part, 100 * FRACTION, rise_part, rise_whole, rise_bound, 100 * (rise_part / rise_bound - 1), rise_whole_exact / f_part))
if rise_part < 0.97 * rise_bound:
    raise AssertionError('the part is %.1f %% cooler than the best layout that exists: more than the 3 %% of the tolerance' % (100 * (1 - rise_part / rise_bound)))
if rise_part > 1.30 * rise_bound:
    raise AssertionError('the part is %.1f %% hotter than the best layout: more than 30 %%' % (100 * (rise_part / rise_bound - 1)))
print('OK')
