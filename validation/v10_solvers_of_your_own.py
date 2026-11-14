# Verification 10: the solvers of your own (solve('heat', ...) and solve('elasticity', ...)) against exact answers.
#
# A steel-like bar 40 x 10 x 10 mm is solved with the solvers that come in the library of solvers (see docs/solvers.md), three ways whose answers follow from the equations alone:
#
#   heat, constant conductivity   held at 20 C at one end and 80 C at the other, the temperature is a straight line:     T(x) = 20 + 60 x / L
#   heat, conductivity that grows with the temperature   k = k0 (1 + beta T), held at 0 C and 100 C:   the combination  T + beta T^2 / 2  is a straight line of x (Kirchhoff's transform, derived in the script),
#                                                          so  T(x) = (-1 + sqrt(1 + 2 beta theta(x))) / beta  with  theta(x) = (100 + beta 100^2 / 2) x / L
#   elasticity    one end held and the other pulled by 1 % of the length, the sides free: the stress is E times the strain, in every node, and the sides draw in by Poisson's ratio times the strain
#
# The script prints the numbers against the exact ones and stops with an error when one is farther off than its tolerance.  A solver that you write is only as good as its equations and its checks: these three
# are the way to test yours.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
import math

from fieldes import *

L, W = 40.0, 10.0
bar = box((L, W, W), position=(L / 2, W / 2, W / 2))
end0 = box((1, (W + 1) - (-1), (W + 1) - (-1)), position=(-0.5, ((-1) + (W + 1)) / 2, ((-1) + (W + 1)) / 2))                           # the face x = 0
endL = box(((L + 1) - L, (W + 1) - (-1), (W + 1) - (-1)), position=((L + (L + 1)) / 2, ((-1) + (W + 1)) / 2, ((-1) + (W + 1)) / 2))     # the face x = L
corner_a = box(1.1, position=(-0.45, -0.45, -0.45))               # the edge points that stop the bar from moving as a whole
corner_b = box((1.1, (W + 1) - (W - 0.1), 1.1), position=(-0.45, ((W - 0.1) + (W + 1)) / 2, -0.45))
corner_c = box((1.1, 1.1, (W + 1) - (W - 0.1)), position=(-0.45, -0.45, ((W - 0.1) + (W + 1)) / 2))
pins = [(corner_a, (None, 0.0, 0.0)), (corner_b, (None, None, 0.0)), (corner_c, (None, 0.0, None))]
failures = []

# heat, constant conductivity
r = solve('heat', bar, element_size=5, fixed=[(end0, 20.0), (endL, 80.0)], conductivity=0.2)
err = max(abs(T - (20.0 + 60.0 * p[0] / L)) for T, p in zip(r.temperature, r.problem.nodes))
print("heat, k constant:        largest difference of a node from the straight line %.2e K  (%d nodes)" % (err, len(r.problem.nodes)))
if err > 0.01:
    failures.append("the temperature is %.3f K from the straight line: more than 0.01 K" % err)

# heat, conductivity that grows with the temperature
beta, k0 = 0.01, 0.2
r = solve('heat', bar, element_size=5, fixed=[(end0, 0.0), (endL, 100.0)], conductivity=lambda T, x, y, z: k0 * (1.0 + beta * T))
top = 100.0 + beta * 100.0 ** 2 / 2.0


def exact(x):
    theta = top * x / L
    return (-1.0 + math.sqrt(1.0 + 2.0 * beta * theta)) / beta


err = max(abs(T - exact(p[0])) for T, p in zip(r.temperature, r.problem.nodes))
print("heat, k = k0 (1 + beta T): largest difference of a node from Kirchhoff's solution %.3f K  (the straight line would be %.1f K off in the middle)" % (err, abs(exact(L / 2) - 50.0)))
if err > 1.0:
    failures.append("the temperature is %.2f K from Kirchhoff's solution: more than 1 K" % err)

# elasticity, pulled to a strain
E, nu, strain = 100000.0, 0.3, 0.01
r = solve('elasticity', bar, element_size=5, fixed=[(end0, (0.0, None, None)), (endL, (strain * L, None, None))] + pins, young=E, poisson=nu)
print("elasticity, strain 1 %%:  largest von Mises %.2f MPa, E x strain %.2f MPa;  lowest node %.2f MPa" % (r.max_von_mises, E * strain, min(r.von_mises)))
for what, value in (("largest", r.max_von_mises), ("lowest", min(r.von_mises))):
    if abs(value / (E * strain) - 1.0) > 0.01:
        failures.append("the %s stress is %.2f MPa, E x strain is %.2f MPa: more than 1 %% apart" % (what, value, E * strain))
side = [uy for uy, p in zip(r.uy, r.problem.nodes) if p[1] > W - 0.1 and abs(p[0] - L) < 0.1]
drawn_in = sum(side) / len(side)
print("                        the sides draw in by %.5f mm, Poisson's ratio x strain x width %.5f mm" % (drawn_in, -nu * strain * W))
if abs(drawn_in / (-nu * strain * W) - 1.0) > 0.02:
    failures.append("the sides draw in by %.5f mm, Poisson's ratio x strain x width is %.5f mm: more than 2 %% apart" % (drawn_in, -nu * strain * W))

if failures:
    raise AssertionError("; ".join(failures))
print("ok")
