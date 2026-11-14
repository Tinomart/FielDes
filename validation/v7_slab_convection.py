# Verification 7: conduction through a slab into a cooling surface (convection) against its exact solution.
#
# An aluminium bar 100 mm long and 10 x 10 mm in section is held at 100 C at one end, nothing leaves its sides, and its other end is cooled by a fluid at 20 C with a heat transfer
# coefficient of h = 0.002 W/(mm^2 K) (2,000 W/(m^2 K), a strong flow of water or oil).  The heat that flows through the bar is the temperature difference over the sum of the two resistances, the bar's and the surface's, and the temperature
# falls along the bar as a straight line:
#
#     q = (100 - 20) / (L / k + 1 / h)  W/mm^2        T(x) = 100 - q x / k        T(L) = 20 + q / h        (k = 0.167 W/(mm K) for aluminium)
#
# (the same heat flows through every layer in series, so the resistances add: the bar's L / k and the surface's 1 / h; this derivation is the only source claimed).  The script solves it with thermal_analysis and prints the temperature and
# the heat flux at three places against these.  It stops with an error when one is farther off than its tolerance.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, b, h = 100.0, 10.0, 10.0          # mm: the bar
T_HOT, T_AMBIENT, H_COEFF = 100.0, 20.0, 0.002
TOLERANCE_T, TOLERANCE_Q = 0.5, 0.03     # K and 3 %

view.set_bounds((-10, -10, -10), (L + 10, b + 10, h + 10), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

bar = box((L, b, h), position=(L / 2, b / 2, h / 2))
hot_end = box((1.01, (b + 1) - (-1), (h + 1) - (-1)), position=(-0.495, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
cooled_end = box(((L + 1) - (L - 0.01), (b + 1) - (-1), (h + 1) - (-1)), position=(((L - 0.01) + (L + 1)) / 2, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
result = thermal_analysis(bar,
                          fixed_temperatures=[fixed_temperature(hot_end, T_HOT)],
                          heat_inputs=[], heat_generations=[], convections=[convection(cooled_end, coefficient=H_COEFF, ambient=T_AMBIENT)],
                          material=aluminium, element_size=3.0, cache=False)

k = aluminium.conductivity                                       # W/(mm K)
exact_q = (T_HOT - T_AMBIENT) / (L / k + 1.0 / H_COEFF)         # W/mm^2, along +x
worst_t, worst_q = 0.0, 0.0
for x in (25.0, 50.0, 75.0):
    exact_t = T_HOT - exact_q * x / k
    t, q = float(result.temperature(x, b / 2, h / 2)), float(result.qx(x, b / 2, h / 2))
    worst_t, worst_q = max(worst_t, abs(t - exact_t)), max(worst_q, abs(q / exact_q - 1.0))
    print("x = %2.0f mm: T thermal_analysis %.2f C, exact %.2f C;  heat flux %.4f W/mm2, exact %.4f W/mm2" % (x, t, exact_t, q, exact_q))
print("worst: %.2f K, %.1f %%  (the cooled end should read %.1f C)" % (worst_t, 100 * worst_q, T_AMBIENT + exact_q / H_COEFF))
if worst_t > TOLERANCE_T:
    raise AssertionError("a temperature is %.2f K from the exact solution: more than %.1f K" % (worst_t, TOLERANCE_T))
if worst_q > TOLERANCE_Q:
    raise AssertionError("the heat flux is %.1f %% from the exact solution: more than %.0f %%" % (100 * worst_q, 100 * TOLERANCE_Q))
print("ok")

result                               # shown: the bar, coloured by the temperature
