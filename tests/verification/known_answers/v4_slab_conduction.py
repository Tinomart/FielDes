# Verification 4: heat conduction through a slab (one-dimensional) against its exact solution.
#
# An aluminium bar 100 mm long and 10 x 10 mm in section is held at 100 C at one end and at 20 C at the other; nothing leaves its sides.  The temperature is then
# a straight line along it, and the heat that flows through it is Fourier's law:
#
#     T(x) = 100 - 0.8 x                    q = k (T_hot - T_cold) / L      (q the heat flux in W/mm2, k = 167 W/(m K) = 0.167 W/(mm K) for aluminium)
#
# The script solves it with thermal_analysis and prints the temperature and the heat flux at three places against these.  Linear tetrahedra represent a straight line
# exactly, so the answer should be right to a hair: the tolerances are small.  It stops with an error when one is farther off.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, b, h = 100.0, 10.0, 10.0          # mm: the bar
HOT, COLD = 100.0, 20.0              # C
TOLERANCE_T, TOLERANCE_Q = 0.3, 0.02     # 0.3 K, 2 %

view.set_bounds((-10, -10, -10), (L + 10, b + 10, h + 10), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

bar = box((0, 0, 0), (L, b, h))
hot_end = box((-1, -1, -1), (0.01, b + 1, h + 1))
cold_end = box((L - 0.01, -1, -1), (L + 1, b + 1, h + 1))
result = thermal_analysis(bar,
                          fixed_temperatures=[fixed_temperature(hot_end, HOT), fixed_temperature(cold_end, COLD)],
                          heat_inputs=[], heat_generations=[], convections=[],
                          material=aluminium, element_size=3.0, cache=False)

k = aluminium.conductivity
exact_q = k * (HOT - COLD) / L                                  # W/mm2, along +x
worst_t, worst_q = 0.0, 0.0
for x in (25.0, 50.0, 75.0):
    exact_t = HOT + (COLD - HOT) * x / L
    t, q = result.temperature(x, b / 2, h / 2), result.qx(x, b / 2, h / 2)
    worst_t, worst_q = max(worst_t, abs(t - exact_t)), max(worst_q, abs(q / exact_q - 1.0))
    print("x = %2.0f mm: T thermal_analysis %.2f C, exact %.2f C;  heat flux %.4f W/mm2, Fourier %.4f W/mm2" % (x, t, exact_t, q, exact_q))
print("worst: %.2f K, %.1f %%  (%d elements)" % (worst_t, 100 * worst_q, result.elements))
if worst_t > TOLERANCE_T:
    raise AssertionError("a temperature is %.2f K from the straight line: more than %.1f K" % (worst_t, TOLERANCE_T))
if worst_q > TOLERANCE_Q:
    raise AssertionError("the heat flux is %.1f %% from Fourier's law: more than %.0f %%" % (100 * worst_q, 100 * TOLERANCE_Q))
print("ok")

result                               # shown: the bar, coloured by the temperature
