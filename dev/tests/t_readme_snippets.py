# The code snippets of the README, the tutorial and the getting-started page must run
from fieldes import *

# README: the smallest script
view.set_bounds([-35, -25, -8], [35, 25, 30])
view.set_resolution(6)
view.set_quality(8)
plate = box_exact((-30, -20, 0), (30, 20, var(6)))
anchor = point(20, 10, 6)
swell = ramp(distance_to_point(anchor), (0, 45), (3.0, 0.5))
r = offset(plate, swell)
print('readme snippet: field at (0,0,3) =', evaluate(r, (0, 0, 3)))

# getting started
anchor = point(10, 4, 0)
swell = ramp(distance_to_point(anchor), (0, 20), (2.0, 0.3))
g = offset(sphere(6), swell)
print('getting-started snippet:', evaluate(g, (6.5, 0, 0)))

# docs/fields.md
post = cylinder_z(10, 30, (105, 20, 0))
block = box_exact((80, 8, 0), (140, 32, 10))
blend = ramp(x_field(), (80, 140), (0.5, 8.0))
joined = smooth_union(post, block, blend)
foam = lattice(box_exact((160, 0, 0), (220, 40, 30)), cell_periodic('gyroid'), cell_size=ramp(x_field(), (160, 220), (16, 6)), thickness=1.0)
print('fields doc snippets ok')

# tutorial: the lattice line
body = box_exact((0, 0, 0), (80, 30, 30))
l = lattice(body, cell_periodic('gyroid'), cell_size=ramp(x_field(), (0, 80), (16, 6)), thickness=1)
print('tutorial lattice ok')

# the analysis page
beam = box_exact((0, 0, 0), (100, 20, 10))
wall = box_exact((-1, -1, -1), (0.01, 21, 11))
top = box_exact((-1, -1, 9.99), (101, 21, 11))
graded = Material('graded', ramp(x_field(), (0, 100), (aluminium.E, aluminium.E / 10)), aluminium.nu)
conditions = static_boundary_conditions(beam, supports=[fixed(wall)], loads=[force(top, (0, 0, -200), profile=ramp(x_field(), (0, 100), (0, 1)))])
result = static_analysis(beam, conditions, material=graded, element_size=3)
print('analysis snippet ok:', result)
