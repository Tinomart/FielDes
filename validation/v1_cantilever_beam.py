# Verification 1: a cantilever beam against beam theory.
#
# A steel bar 100 mm long and 10 x 10 mm in section is held at one end and loaded with 100 N across the other.  The tip moves, by
# the theory of beams that every engineer knows (Timoshenko's, which adds the small share of the shear to Euler-Bernoulli's):
#
#     tip = P L^3 / (3 E I)  +  P L / (k G A)        I = b h^3 / 12,  G = E / (2 (1 + nu)),  k = 5/6 for a rectangle
#
# The script solves the same problem with static_analysis (tetrahedra with linear shape functions: they are stiff in bending, so
# the answer is a little low, and comes nearer as the elements get smaller) and with 10-node tetrahedra, and prints the numbers.  It stops with an error when
# they differ by more than the tolerance, so that it can be run as a check after a change.
# WHAT YOU SEE when you open it: the bent beam coloured by its stress, with the held end and the load drawn on it.  The model tree has a row for each analysis
# (`result`, linear tetrahedra; `quad`, 10-node ones) and the two conditions are in the row "boundary conditions" of each; the eye of a row shows or hides it.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, b, h = 100.0, 10.0, 10.0          # mm: length, width, depth
P = 100.0                            # N, across the free end (down)
TOLERANCE = 0.05                     # 5 %: linear tetrahedra at this element size (the program gets about 2 % here)
TOLERANCE_10 = 0.02                  # 2 %: 10-node tetrahedra, two elements across the depth (the program gets about 0.8 % here)

view.set_bounds((-10, -10, -10), (L + 10, b + 10, h + 10), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)

beam = box((L, b, h), position=(L / 2, b / 2, h / 2))
wall = box((1.01, (b + 1) - (-1), (h + 1) - (-1)), position=(-0.495, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))                  # the held end: the face at x = 0
support = fixed(wall)
tip = box(((L + 1) - (L - 0.01), (b + 1) - (-1), (h + 1) - (-1)), position=(((L - 0.01) + (L + 1)) / 2, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))            # the loaded end: the face at x = L
load = force(tip, (0, 0, -P))
result = static_analysis(beam, supports=[support], loads=[load], material=steel, element_size=1.25, cache=False)
quad = static_analysis(beam, supports=[support], loads=[load], material=steel, element_size=5.0, element='tet10', cache=False)     # two elements across its depth read the bending better than eight linear ones

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the analysis and the conditions it was solved with (the eye of each row in the model tree shows or hides it)
result                               # the beam, coloured by the stress and bent
support                              # the held end (the face at x = 0)
load                                 # the 100 N across the free end

E, nu = steel.E, steel.nu
I, A, G = b * h ** 3 / 12.0, b * h, E / (2.0 * (1.0 + nu))
theory = P * L ** 3 / (3.0 * E * I) + P * L / (5.0 / 6.0 * G * A)
computed = abs(result.uz(L, b / 2, h / 2))                      # the movement of the middle of the free end
error = computed / theory - 1.0
print("tip deflection: static_analysis %.4f mm, beam theory %.4f mm: %+.1f %%  (%d elements)" % (computed, theory, 100 * error, result.elements))
assert abs(error) <= TOLERANCE, "the tip deflection is %.1f %% from beam theory: more than %.0f %%" % (100 * error, 100 * TOLERANCE)
print("ok: within %.0f %%" % (100 * TOLERANCE))

computed10 = abs(quad.uz(L, b / 2, h / 2))
error10 = computed10 / theory - 1.0
print("with 10-node elements:  %.4f mm, beam theory %.4f mm: %+.1f %%  (%d elements)" % (computed10, theory, 100 * error10, quad.elements))
assert abs(error10) <= TOLERANCE_10, "the tip deflection with 10-node elements is %.1f %% from beam theory: more than %.0f %%" % (100 * error10, 100 * TOLERANCE_10)
print("ok: within %.0f %%" % (100 * TOLERANCE_10))
