# Verification 1: a cantilever beam against beam theory.
#
# A steel bar 100 mm long and 10 x 10 mm in section is held at one end and loaded with 100 N across the other.  The tip moves, by
# the theory of beams that every engineer knows (Timoshenko's, which adds the small share of the shear to Euler-Bernoulli's):
#
#     tip = P L^3 / (3 E I)  +  P L / (k G A)        I = b h^3 / 12,  G = E / (2 (1 + nu)),  k = 5/6 for a rectangle
#
# The script solves the same problem with static_analysis (tetrahedra with linear shape functions: they are stiff in bending, so
# the answer is a little low, and comes nearer as the elements get smaller) and prints both numbers.  It stops with an error when
# they differ by more than the tolerance, so that it can be run as a check after a change.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, b, h = 100.0, 10.0, 10.0          # mm: length, width, depth
P = 100.0                            # N, across the free end (down)
TOLERANCE = 0.05                     # 5 %: linear tetrahedra at this element size (the program gets about 2 % here)

view.set_bounds((-10, -10, -10), (L + 10, b + 10, h + 10), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

beam = box((0, 0, 0), (L, b, h))
wall = box((-1, -1, -1), (0.01, b + 1, h + 1))                  # the held end: the face at x = 0
tip = box((L - 0.01, -1, -1), (L + 1, b + 1, h + 1))            # the loaded end: the face at x = L
result = static_analysis(beam, supports=[fixed(wall)], loads=[force(tip, (0, 0, -P))], material=steel, element_size=1.25, cache=False)

E, nu = steel.E, steel.nu
I, A, G = b * h ** 3 / 12.0, b * h, E / (2.0 * (1.0 + nu))
theory = P * L ** 3 / (3.0 * E * I) + P * L / (5.0 / 6.0 * G * A)
computed = abs(result.uz(L, b / 2, h / 2))                      # the movement of the middle of the free end
error = computed / theory - 1.0
print("tip deflection: static_analysis %.4f mm, beam theory %.4f mm: %+.1f %%  (%d elements)" % (computed, theory, 100 * error, result.elements))
if abs(error) > TOLERANCE:
    raise AssertionError("the tip deflection is %.1f %% from beam theory: more than %.0f %%" % (100 * error, 100 * TOLERANCE))
print("ok: within %.0f %%" % (100 * TOLERANCE))

result                               # shown: the beam, bent
