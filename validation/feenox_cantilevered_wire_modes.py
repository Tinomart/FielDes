# Verification 5: the natural frequencies of a cantilevered wire (a modal analysis) against Euler-Bernoulli beam theory: the set-up of an open-source case.
#
# The case is "Five natural modes of a cantilevered wire" of the annotated examples of the open-source FEM program FeenoX
# (https://seamplex.com/feenox/examples/modal.html#five-natural-modes-of-a-cantilevered-wire).  A wire of aluminium, 0.5 x 303 mm = 151.5 mm long and 1.948 mm in diameter, is held at one end;
# its Young's modulus is 2 x 40.2 GPa, its Poisson's ratio 0, and its density makes the mass 0.5 x 2.67 g (density = mass / (pi (d/2)^2 l)).  The page compares the frequencies of its finite elements with those of
# Euler and Bernoulli's beam theory and lists the latter for the aluminium wire as 61.92, 388, 1086, 2129 and 3519 Hz (its finite elements read 61.91, 387.8, 1085, 2124 and 3506 Hz on a mesh of hexahedra).
#
# The theory: a cantilever bends with the frequencies  f_n = (x_n)^2 / (2 pi l^2) * sqrt(E I / (rho A)),  where x_n are the roots of  cos(x) cosh(x) = -1  (1.875, 4.694, 7.855, 10.996, 14.137: this script finds them
# itself) and, for a round section, I / A = d^2 / 16.  A wire bends the same way in every direction, so each frequency comes twice.
#
# The script solves the same wire with modal_analysis (10-node tetrahedra of 0.8 mm, element='tet10'), prints its first five frequencies (the mean of each pair) against the theory computed here and the numbers of the page,
# and stops with an error when one differs from the theory by more than the tolerance.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
import math

from fieldes import *
from fieldes.stdlib.fea import Material

l, d = 0.5 * 303.0, 1.948                        # mm: length and diameter of the wire
E = 2 * 40.2e3                                   # MPa
mass = 0.5 * 2.67e-3                             # kg
rho = mass / (math.pi * (0.5 * d * 1e-3) ** 2 * l * 1e-3) * 1e-12          # tonne / mm^3 (kg / m^3 x 1e-12)
PAGE = (61.92, 388.0, 1086.0, 2129.0, 3519.0)    # Hz, the page's Euler-Bernoulli column for the aluminium wire
TOLERANCE = 0.03                                 # 3 % (10-node tetrahedra of 0.8 mm: two and a half elements across the diameter; with 1 mm, two across, the first mode reads 3.9 % low and the volume of the mesh 6.9 % short: a wire that thin is not yet resolved, see docs/validation.md)

view.set_bounds((-1, -2, -2), (l + 1, 2, 2), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

wire = cylinder(0.5 * d, l, position=(l / 2, 0, 0), rotation=(0, 90, 0))                      # along x, from 0 to l
wall = box((1.01, 6, 6), position=(-0.495, 0, 0))                                             # the held end: the face at x = 0
aluminium_wire = Material('aluminium wire', E, 0.0, rho, 100.0, 0.167, 23e-6)
modes = modal_analysis(wire, supports=[fixed(wall)], material=aluminium_wire, modes=10, element_size=0.8, element='tet10', cache=False)


def roots(count):
    ''' The first `count` roots of cos(x) cosh(x) + 1 = 0, by scanning for a change of sign and halving '''
    g = lambda x: math.cos(x) * math.cosh(x) + 1.0
    found, x = [], 0.5
    while len(found) < count:
        a, b = x, x + 0.01
        if g(a) * g(b) < 0:
            for _ in range(60):
                m = 0.5 * (a + b)
                a, b = (m, b) if g(a) * g(m) > 0 else (a, m)
            found.append(0.5 * (a + b))
        x += 0.01
    return found


theory = [x * x / (2.0 * math.pi * l * l) * math.sqrt(E / rho) * d / 4.0 for x in roots(5)]         # sqrt(E I / (rho A)) = sqrt(E / rho) d / 4
frequencies = sorted(float(f) for f in modes.frequencies)
worst = 0.0
print("mode        modal_analysis [Hz]   Euler-Bernoulli [Hz]   page's Euler [Hz]   difference from the theory")
for n in range(5):
    f = 0.5 * (frequencies[2 * n] + frequencies[2 * n + 1])
    error = f / theory[n] - 1.0
    worst = max(worst, abs(error))
    print("%d (x2)     %10.2f            %10.2f             %8.2f           %+.2f %%" % (n + 1, f, theory[n], PAGE[n], 100 * error))
print("worst: %.2f %%  (the theory computed here differs from the page's by at most %.2f %%)" % (100 * worst, 100 * max(abs(t / p - 1.0) for t, p in zip(theory, PAGE))))
if worst > TOLERANCE:
    raise AssertionError("a frequency is %.1f %% from beam theory: more than %.0f %%" % (100 * worst, 100 * TOLERANCE))
print("ok: within %.0f %%" % (100 * TOLERANCE))

modes.modes[0]                       # shown: the first mode (play it on the result card)
