# Verification 8: thermal stress (a heated bar, free and held) against the exact solutions of thermal expansion.
#
# A steel bar 100 mm long and 10 x 10 mm in section is heated evenly by 60 K (from 20 C, where it is stress-free, to 80 C).  Two cases have an exact answer:
#
#   free     held at one end only, it grows by  alpha dT L  and is not stressed:   tip movement = 12e-6 x 60 x 100 = 0.072 mm,   von Mises stress = 0
#   held     held at both ends, it cannot grow, and far from the ends it is stressed as a bar pressed between two walls:   sigma = E alpha dT = 200,000 x 12e-6 x 60 = 144 MPa (compression)
#
# (a bar that cannot grow is pressed back by exactly the strain it would have had, sigma = E alpha dT; this derivation is the only source claimed).  The script solves both with static_analysis and thermal_expansion(80) -- the part at 80 C expands by the material's
# coefficient for every degree above 20 -- and prints the numbers against these.  The held bar is ten times as long as it is deep: the middle of it is far enough from the ends for the bar's own stress, within a few percent.
# It stops with an error when a number is farther off than its tolerance.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, b, h = 100.0, 10.0, 10.0          # mm: the bar
T_HOT, T_REF = 80.0, 20.0            # C
TOLERANCE_GROWTH, TOLERANCE_FREE_STRESS, TOLERANCE_HELD = 0.02, 1.0, 0.05      # 2 % of the growth (the held end also keeps the end of the bar from growing sideways, which moves the tip by about 1 %), 1 MPa, 5 % of E alpha dT

view.set_bounds((-10, -10, -10), (L + 10, b + 10, h + 10), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

bar = box((L, b, h), position=(L / 2, b / 2, h / 2))
end_0 = box((1.01, (b + 1) - (-1), (h + 1) - (-1)), position=(-0.495, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
end_1 = box(((L + 1) - (L - 0.01), (b + 1) - (-1), (h + 1) - (-1)), position=(((L - 0.01) + (L + 1)) / 2, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
alpha, E = steel.expansion, steel.E

free = static_analysis(bar, supports=[fixed(end_0)], loads=[thermal_expansion(T_HOT, T_REF)], material=steel, element_size=2.5, cache=False)
growth = float(free.ux(L, b / 2, h / 2))
exact_growth = alpha * (T_HOT - T_REF) * L
free_stress = max(float(free.von_mises(x, b / 2, h / 2)) for x in (20.0, 50.0, 80.0))
print("free bar: tip movement %.4f mm, exact %.4f mm: %+.2f %%;  largest von Mises stress at three places %.3f MPa, exact 0" % (growth, exact_growth, 100 * (growth / exact_growth - 1.0), free_stress))

held = static_analysis(bar, supports=[fixed(end_0), fixed(end_1)], loads=[thermal_expansion(T_HOT, T_REF)], material=steel, element_size=2.5, cache=False)
exact_held = E * alpha * (T_HOT - T_REF)
middle = float(held.von_mises(L / 2, b / 2, h / 2))
print("held bar: von Mises stress in the middle %.1f MPa, E alpha dT %.1f MPa: %+.1f %%  (%d elements)" % (middle, exact_held, 100 * (middle / exact_held - 1.0), held.elements))

if abs(growth / exact_growth - 1.0) > TOLERANCE_GROWTH:
    raise AssertionError("the free bar grows %.1f %% from alpha dT L: more than %.0f %%" % (100 * (growth / exact_growth - 1.0), 100 * TOLERANCE_GROWTH))
if free_stress > TOLERANCE_FREE_STRESS:
    raise AssertionError("the free bar is stressed by %.2f MPa: more than %.0f MPa" % (free_stress, TOLERANCE_FREE_STRESS))
if abs(middle / exact_held - 1.0) > TOLERANCE_HELD:
    raise AssertionError("the stress of the held bar is %.1f %% from E alpha dT: more than %.0f %%" % (100 * (middle / exact_held - 1.0), 100 * TOLERANCE_HELD))
print("ok")

held                                 # shown: the held bar, coloured by the stress
