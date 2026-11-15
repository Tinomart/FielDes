# Verification 9: the volumes and areas of shapes made by the CAD operations against their exact values.
#
# Every shape below has a volume or an area that follows from its dimensions alone (the formulas of elementary geometry, written next to each case).  The script makes the shape with the program's own operations --
# a box, a ball, a cylinder, a hole cut in a plate, a body turned about an axis, a shape moved and rotated -- measures it with `volume_of` (grid sampling, about 100 samples along the longest side) and
# `surface_area` (a mesh of triangles on the surface, always a little low on a curved surface), prints both numbers and stops with an error when one is farther off than its tolerance.
# WHAT YOU SEE when you open it: the six shapes that were measured, side by side (each is moved along x only for this picture; the numbers are measured on the shapes where they were made), each a model of its own
# in the tree (`slab_shown` ... `two_shown`) with its own eye.  This script makes no analysis, so it has no supports and loads: its results are the numbers it prints.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
import math

from fieldes import *

view.set_bounds((-120, -30, -20), (125, 30, 20), auto_scale=True)
view.set_resolution(3)                 # samples across the longest side of each model, ten per unit: 30 across a 40 mm shape, so the ball, the post and the hole of the plate are drawn
view.set_quality(8)

failures = []


def check(name, measured, exact, tolerance):
    error = measured / exact - 1.0
    print("%-62s measured %10.2f   exact %10.2f   %+6.2f %%" % (name, measured, exact, 100 * error))
    if abs(error) > tolerance:
        failures.append("%s: %+.2f %% (more than %.0f %%)" % (name, 100 * error, 100 * tolerance))


slab = box((20, 10, 6))
ball = sphere(10)
post = cylinder(5, 20)
plate = difference(box((40, 20, 6)), cylinder(4, 20))
moved = move(rotate(box((20, 10, 6)), (0, 0, 37)), (13, -7, 4))
two = union(sphere(10), move(sphere(10), (10, 0, 0)))

# SHOWN IN THE VIEWPORT, directly after the shapes and before any check: each shape that is measured, moved along x for this picture only (the numbers below are measured on the shapes where they were made), as a model of its
# own with its own eye in the model tree
slab_shown = move(slab, (-90, 0, 0))
ball_shown = move(ball, (-60, 0, 0))
post_shown = move(post, (-35, 0, 0))
plate_shown = plate                                  # the plate with its hole stays where it is made
moved_shown = move(moved, (30, 0, 0))
two_shown = move(two, (90, 0, 0))
slab_shown                                           # the box 20 x 10 x 6
ball_shown                                           # the ball of radius 10
post_shown                                           # the cylinder of radius 5 and height 20
plate_shown                                          # the plate 40 x 20 x 6 with a hole of radius 4
moved_shown                                          # the same box turned 37 degrees and moved
two_shown                                            # the two balls whose centres are 10 apart

check("volume of a box 20 x 10 x 6 mm (V = a b c)", volume_of(slab), 20 * 10 * 6, 0.02)
check("volume of a ball of radius 10 mm (V = 4/3 pi r^3)", volume_of(ball), 4.0 / 3.0 * math.pi * 10 ** 3, 0.02)
check("volume of a cylinder of radius 5 and height 20 (V = pi r^2 h)", volume_of(post), math.pi * 5 ** 2 * 20, 0.02)
check("volume of a plate 40 x 20 x 6 with a hole of radius 4 (V = a b c - pi r^2 c)", volume_of(plate), 40 * 20 * 6 - math.pi * 4 ** 2 * 6, 0.02)
check("volume of the same box turned 37 degrees and moved (the volume is kept)", volume_of(moved, lower=(0, -20, 0), upper=(26, 6, 8)), 20 * 10 * 6, 0.04)   # (slanted edges: the grid of samples cuts them by up to half a sample, 0.13 mm here)
overlap = math.pi * (4 * 10 + 10) * (2 * 10 - 10) ** 2 / 12.0                                  # two balls of radius r whose centres are d = r apart overlap by pi (4 r + d) (2 r - d)^2 / 12
check("volume of two balls of radius 10 whose centres are 10 apart (V = 2 V1 - overlap)", volume_of(two, lower=(-12, -12, -12), upper=(22, 12, 12)), 2 * 4.0 / 3.0 * math.pi * 10 ** 3 - overlap, 0.02)
check("area of the surface of a box 20 x 10 x 6 mm (A = 2 (ab + ac + bc))", surface_area(slab), 2 * (20 * 10 + 20 * 6 + 10 * 6), 0.02)
check("area of the surface of a ball of radius 10 mm (A = 4 pi r^2), a mesh of triangles", surface_area(ball, 0.6), 4 * math.pi * 10 ** 2, 0.03)

if failures:
    raise AssertionError("; ".join(failures))
print("ok")
