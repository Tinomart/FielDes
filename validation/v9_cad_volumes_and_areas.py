# Verification 9: the volumes and areas of shapes made by the CAD operations against their exact values.
#
# Every shape below has a volume or an area that follows from its dimensions alone (the formulas of elementary geometry, written next to each case).  The script makes the shape with the program's own operations --
# a box, a ball, a cylinder, a hole cut in a plate, a body turned about an axis, a shape moved and rotated -- measures it with `volume_of` (grid sampling, about 100 samples along the longest side) and
# `surface_area` (a mesh of triangles on the surface, always a little low on a curved surface), prints both numbers and stops with an error when one is farther off than its tolerance.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
import math

from fieldes import *

failures = []


def check(name, measured, exact, tolerance):
    error = measured / exact - 1.0
    print("%-62s measured %10.2f   exact %10.2f   %+6.2f %%" % (name, measured, exact, 100 * error))
    if abs(error) > tolerance:
        failures.append("%s: %+.2f %% (more than %.0f %%)" % (name, 100 * error, 100 * tolerance))


check("volume of a box 20 x 10 x 6 mm (V = a b c)", volume_of(box((20, 10, 6))), 20 * 10 * 6, 0.02)
check("volume of a ball of radius 10 mm (V = 4/3 pi r^3)", volume_of(sphere(10)), 4.0 / 3.0 * math.pi * 10 ** 3, 0.02)
check("volume of a cylinder of radius 5 and height 20 (V = pi r^2 h)", volume_of(cylinder(5, 20)), math.pi * 5 ** 2 * 20, 0.02)
plate = difference(box((40, 20, 6)), cylinder(4, 20))
check("volume of a plate 40 x 20 x 6 with a hole of radius 4 (V = a b c - pi r^2 c)", volume_of(plate), 40 * 20 * 6 - math.pi * 4 ** 2 * 6, 0.02)
moved = move(rotate(box((20, 10, 6)), (0, 0, 37)), (13, -7, 4))
check("volume of the same box turned 37 degrees and moved (the volume is kept)", volume_of(moved, lower=(0, -20, 0), upper=(26, 6, 8)), 20 * 10 * 6, 0.04)   # (slanted edges: the grid of samples cuts them by up to half a sample, 0.13 mm here)
two = union(sphere(10), move(sphere(10), (10, 0, 0)))
overlap = math.pi * (4 * 10 + 10) * (2 * 10 - 10) ** 2 / 12.0                                  # two balls of radius r whose centres are d = r apart overlap by pi (4 r + d) (2 r - d)^2 / 12
check("volume of two balls of radius 10 whose centres are 10 apart (V = 2 V1 - overlap)", volume_of(two, lower=(-12, -12, -12), upper=(22, 12, 12)),2 * 4.0 / 3.0 * math.pi * 10 ** 3 - overlap, 0.02)
check("area of the surface of a box 20 x 10 x 6 mm (A = 2 (ab + ac + bc))", surface_area(box((20, 10, 6))), 2 * (20 * 10 + 20 * 6 + 10 * 6), 0.02)
check("area of the surface of a ball of radius 10 mm (A = 4 pi r^2), a mesh of triangles", surface_area(sphere(10), 0.6), 4 * math.pi * 10 ** 2, 0.03)

if failures:
    raise AssertionError("; ".join(failures))
print("ok")
