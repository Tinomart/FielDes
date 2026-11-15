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
# WHAT YOU SEE when you open it: the bar coloured by its temperature, with the hot end and the cooled end drawn on it (the conditions are in the row "boundary conditions" of `result`).
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, b, h = 100.0, 10.0, 10.0          # mm: the bar
T_HOT, T_AMBIENT, H_COEFF = 100.0, 20.0, 0.002
TOLERANCE_T, TOLERANCE_Q = 0.5, 0.03     # K and 3 %

view.set_bounds((-10, -10, -10), (L + 10, b + 10, h + 10), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)

bar = box((L, b, h), position=(L / 2, b / 2, h / 2))
hot_end = box((1.01, (b + 1) - (-1), (h + 1) - (-1)), position=(-0.495, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
hot = fixed_temperature(hot_end, T_HOT)                          # the end held at 100 C
cooled_end = box(((L + 1) - (L - 0.01), (b + 1) - (-1), (h + 1) - (-1)), position=(((L - 0.01) + (L + 1)) / 2, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
cooling = convection(cooled_end, coefficient=H_COEFF, ambient=T_AMBIENT)      # the other end, cooled by a fluid at 20 C
result = thermal_analysis(bar, fixed_temperatures=[hot], heat_inputs=[], heat_generations=[], convections=[cooling], material=aluminium, element_size=3.0, cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the analysis and the conditions it was solved with (the eye of each row in the model tree shows or hides it)
result                               # the bar, coloured by the temperature
hot                                  # the end held at 100 C
cooling                              # the other end, cooled by a fluid at 20 C

k = aluminium.conductivity                                       # W/(mm K)
exact_q = (T_HOT - T_AMBIENT) / (L / k + 1.0 / H_COEFF)         # W/mm^2, along +x
places = (25.0, 50.0, 75.0)
exact_t = [T_HOT - exact_q * x / k for x in places]
temperatures = [float(result.temperature(x, b / 2, h / 2)) for x in places]
fluxes = [float(result.qx(x, b / 2, h / 2)) for x in places]
print('\n'.join("x = %2.0f mm: T thermal_analysis %.2f C, exact %.2f C;  heat flux %.4f W/mm2, exact %.4f W/mm2" % (x, t, e, q, exact_q) for x, t, e, q in zip(places, temperatures, exact_t, fluxes)))
worst_t = max(abs(t - e) for t, e in zip(temperatures, exact_t))
worst_q = max(abs(q / exact_q - 1.0) for q in fluxes)
print("worst: %.2f K, %.1f %%  (the cooled end should read %.1f C)" % (worst_t, 100 * worst_q, T_AMBIENT + exact_q / H_COEFF))
assert worst_t <= TOLERANCE_T, "a temperature is %.2f K from the exact solution: more than %.1f K" % (worst_t, TOLERANCE_T)
assert worst_q <= TOLERANCE_Q, "the heat flux is %.1f %% from the exact solution: more than %.0f %%" % (100 * worst_q, 100 * TOLERANCE_Q)
print("ok")
