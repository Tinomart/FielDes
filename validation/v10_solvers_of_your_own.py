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
# WHAT YOU SEE when you open it: the three solved bars side by side (each is built 20 mm further along y than the one before, so that the three can be seen at once): the first coloured by its temperature (a straight
# line from 20 to 80 C), the second by its temperature (the curve of Kirchhoff's solution), the third by its stress (the same everywhere), at the end of the file.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
import math

from fieldes import *

L, W = 40.0, 10.0

view.set_bounds((-10, -10, -10), (L + 10, 3 * 20 + 10, W + 10), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)


def set_up(y):
    ''' the bar with its faces at x = 0 and x = L and the three edge points that stop it from moving as a whole, built y mm along y from the origin: returns the bar, the two end faces and the three pins '''
    bar = box((L, W, W), position=(L / 2, y + W / 2, W / 2))
    end0 = box((1, (W + 1) - (-1), (W + 1) - (-1)), position=(-0.5, y + ((-1) + (W + 1)) / 2, ((-1) + (W + 1)) / 2))                           # the face x = 0
    endL = box(((L + 1) - L, (W + 1) - (-1), (W + 1) - (-1)), position=((L + (L + 1)) / 2, y + ((-1) + (W + 1)) / 2, ((-1) + (W + 1)) / 2))     # the face x = L
    corner_a = box(1.1, position=(-0.45, y - 0.45, -0.45))               # the edge points that stop the bar from moving as a whole
    corner_b = box((1.1, (W + 1) - (W - 0.1), 1.1), position=(-0.45, y + ((W - 0.1) + (W + 1)) / 2, -0.45))
    corner_c = box((1.1, 1.1, (W + 1) - (W - 0.1)), position=(-0.45, y - 0.45, ((W - 0.1) + (W + 1)) / 2))
    pins = [(corner_a, (None, 0.0, 0.0)), (corner_b, (None, None, 0.0)), (corner_c, (None, 0.0, None))]
    return bar, end0, endL, pins


failures = []

# heat, constant conductivity
bar_1, end0_1, endL_1, pins_1 = set_up(0.0)
straight = solve('heat', bar_1, element_size=5, fixed=[(end0_1, 20.0), (endL_1, 80.0)], conductivity=0.2)
err = max(abs(T - (20.0 + 60.0 * p[0] / L)) for T, p in zip(straight.temperature, straight.problem.nodes))
print("heat, k constant:        largest difference of a node from the straight line %.2e K  (%d nodes)" % (err, len(straight.problem.nodes)))
if err > 0.01:
    failures.append("the temperature is %.3f K from the straight line: more than 0.01 K" % err)

# heat, conductivity that grows with the temperature
beta, k0 = 0.01, 0.2
bar_2, end0_2, endL_2, pins_2 = set_up(20.0)
curved = solve('heat', bar_2, element_size=5, fixed=[(end0_2, 0.0), (endL_2, 100.0)], conductivity=lambda T, x, y, z: k0 * (1.0 + beta * T))
top = 100.0 + beta * 100.0 ** 2 / 2.0


def exact(x):
    theta = top * x / L
    return (-1.0 + math.sqrt(1.0 + 2.0 * beta * theta)) / beta


err = max(abs(T - exact(p[0])) for T, p in zip(curved.temperature, curved.problem.nodes))
print("heat, k = k0 (1 + beta T): largest difference of a node from Kirchhoff's solution %.3f K  (the straight line would be %.1f K off in the middle)" % (err, abs(exact(L / 2) - 50.0)))
if err > 1.0:
    failures.append("the temperature is %.2f K from Kirchhoff's solution: more than 1 K" % err)

# elasticity, pulled to a strain
E, nu, strain = 100000.0, 0.3, 0.01
bar_3, end0_3, endL_3, pins_3 = set_up(40.0)
pulled = solve('elasticity', bar_3, element_size=5, fixed=[(end0_3, (0.0, None, None)), (endL_3, (strain * L, None, None))] + pins_3, young=E, poisson=nu)
print("elasticity, strain 1 %%:  largest von Mises %.2f MPa, E x strain %.2f MPa;  lowest node %.2f MPa" % (pulled.max_von_mises, E * strain, min(pulled.von_mises)))
largest, lowest = pulled.max_von_mises, min(pulled.von_mises)
if abs(largest / (E * strain) - 1.0) > 0.01:
    failures.append("the largest stress is %.2f MPa, E x strain is %.2f MPa: more than 1 %% apart" % (largest, E * strain))
if abs(lowest / (E * strain) - 1.0) > 0.01:
    failures.append("the lowest stress is %.2f MPa, E x strain is %.2f MPa: more than 1 %% apart" % (lowest, E * strain))
side = [uy for uy, p in zip(pulled.uy, pulled.problem.nodes) if p[1] - 40.0 > W - 0.1 and abs(p[0] - L) < 0.1]
drawn_in = sum(side) / len(side)
print("                        the sides draw in by %.5f mm, Poisson's ratio x strain x width %.5f mm" % (drawn_in, -nu * strain * W))
if abs(drawn_in / (-nu * strain * W) - 1.0) > 0.02:
    failures.append("the sides draw in by %.5f mm, Poisson's ratio x strain x width is %.5f mm: more than 2 %% apart" % (drawn_in, -nu * strain * W))

if failures:
    raise AssertionError("; ".join(failures))
print("ok")

# SHOWN IN THE VIEWPORT: the three solved bars (the eye of each row in the model tree shows or hides it)
straight                                 # heat, constant conductivity: the temperature, a straight line from 20 C to 80 C
curved                                   # heat, conductivity that grows with the temperature: the temperature, Kirchhoff's curve from 0 C to 100 C
pulled                                   # elasticity: the bar pulled by 1 % of its length, the stress the same in every node
# the places where the three bars are held or loaded: the regions that solve(..., fixed=[(region, value)]) takes (a solver of your own takes plain regions, not condition models)
end0_1                                   # heat, constant conductivity: the end held at 20 C
endL_1                                   # ... and the end held at 80 C
end0_2                                   # heat, conductivity that grows with the temperature: the end held at 0 C
endL_2                                   # ... and the end held at 100 C
end0_3                                   # elasticity: the end held in x
endL_3                                   # ... and the end pulled 0.4 mm along x
