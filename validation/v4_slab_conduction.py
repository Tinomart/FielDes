# Verification 4: heat conduction through a slab (one-dimensional) against its exact solution.
#
# An aluminium bar 100 mm long and 10 x 10 mm in section is held at 100 C at one end and at 20 C at the other; nothing leaves its sides.  The temperature is then
# a straight line along it, and the heat that flows through it is Fourier's law:
#
#     T(x) = 100 - 0.8 x                    q = k (T_hot - T_cold) / L      (q the heat flux in W/mm2, k = 167 W/(m K) = 0.167 W/(mm K) for aluminium)
#
# The script solves it with thermal_analysis and prints the temperature and the heat flux at three places against these.  Linear tetrahedra represent a straight line
# exactly, so the answer should be right to a hair: the tolerances are small.  It stops with an error when one is farther off.
# WHAT YOU SEE when you open it: the bar coloured by its temperature, with the hot end and the cold end drawn on it (the conditions are in the row "boundary conditions" of `result`).
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, b, h = 100.0, 10.0, 10.0          # mm: the bar
HOT, COLD = 100.0, 20.0              # C
TOLERANCE_T, TOLERANCE_Q = 0.3, 0.02     # 0.3 K, 2 %

view.set_bounds((-10, -10, -10), (L + 10, b + 10, h + 10), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)

bar = box((L, b, h), position=(L / 2, b / 2, h / 2))
hot_end = box((1.01, (b + 1) - (-1), (h + 1) - (-1)), position=(-0.495, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
hot = fixed_temperature(hot_end, HOT)                                  # the end held at 100 C
cold_end = box(((L + 1) - (L - 0.01), (b + 1) - (-1), (h + 1) - (-1)), position=(((L - 0.01) + (L + 1)) / 2, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
cold = fixed_temperature(cold_end, COLD)                               # the end held at 20 C
result = thermal_analysis(bar, fixed_temperatures=[hot, cold], heat_inputs=[], heat_generations=[], convections=[], material=aluminium, element_size=3.0, cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the analysis and the conditions it was solved with (the eye of each row in the model tree shows or hides it)
result                               # the bar, coloured by the temperature
hot                                  # the end held at 100 C
cold                                 # the end held at 20 C

k = aluminium.conductivity
exact_q = k * (HOT - COLD) / L                                  # W/mm2, along +x
places = (25.0, 50.0, 75.0)
exact_t = [HOT + (COLD - HOT) * x / L for x in places]
temperatures = [float(result.temperature(x, b / 2, h / 2)) for x in places]
fluxes = [float(result.qx(x, b / 2, h / 2)) for x in places]
print('\n'.join("x = %2.0f mm: T thermal_analysis %.2f C, exact %.2f C;  heat flux %.4f W/mm2, Fourier %.4f W/mm2" % (x, t, e, q, exact_q) for x, t, e, q in zip(places, temperatures, exact_t, fluxes)))
worst_t = max(abs(t - e) for t, e in zip(temperatures, exact_t))
worst_q = max(abs(q / exact_q - 1.0) for q in fluxes)
print("worst: %.2f K, %.1f %%  (%d elements)" % (worst_t, 100 * worst_q, result.elements))
assert worst_t <= TOLERANCE_T, "a temperature is %.2f K from the straight line: more than %.1f K" % (worst_t, TOLERANCE_T)
assert worst_q <= TOLERANCE_Q, "the heat flux is %.1f %% from Fourier's law: more than %.0f %%" % (100 * worst_q, 100 * TOLERANCE_Q)
print("ok")
