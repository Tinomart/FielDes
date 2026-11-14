# Verification: a cylinder under pure compression (the open-source solver Fino's case 006, https://www.seamplex.com/fino/cases/006-cylinder-pure-compression, published under CC BY 4.0).
#
# THE PROBLEM, as that page gives it (non-dimensional: E = 1 MPa, nu = 0.3): a cylinder of radius 1/2 and height 1 mm, its base on the plane z = 0, a uniform compressive pressure of 1 MPa on the top.
# THE ANSWER (exact): the stress in z is -1 everywhere and the other five components are 0 everywhere (a patch test: any mesh must give it).  THE CURVED SIDE makes the loaded face a polygon of facets, whose area
# is a little under the circle's: the page says it too ("the actual area of the discretized surface ... does depend on the mesh coarseness"): a pressure is a total force over the area of the surface, so with
# a force of pressure x the exact area the stress is -1 times the exact area over the facets' area, a little more than 1, and falls to 1 as the elements get smaller.
#
# DEVIATION FROM THE PAGE: the page removes the rigid body motions by a radial symmetry condition on the base.  Here a QUARTER of the cylinder is meshed with the two symmetry planes (x = 0 held in x, y = 0 held in y)
# and the base held in z: the same exact solution, with no condition in the middle of the mesh.
# The script solves it with linear and 10-node tetrahedra, reads the six stresses at a grid of points, prints the smallest and the largest of each and stops with an error when one is further than the tolerance.
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
import math

from fieldes import *

TOLERANCE = 0.03                          # of the applied pressure (the facets' area: see above)
SIZE = 0.1                                # mm
R, H = 0.5, 1.0

view.set_bounds((-0.5, -0.5, -0.5), (1.0, 1.0, 1.5), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

unit = Material('unit', 1.0, 0.3)
quarter = intersection(cylinder(R, H, position=(0.0, 0.0, H / 2)), box((3.0, 3.0, 3.0), position=(1.5, 1.5, 0.5)))        # the part of the cylinder with x >= 0 and y >= 0
bottom = box((3.0, 3.0, 0.21), position=(0.0, 0.0, -0.095))              # the base z = 0
plane_x = box((0.21, 3.0, 3.0), position=(-0.095, 0.0, 0.5))             # the plane x = 0
plane_y = box((3.0, 0.21, 3.0), position=(0.0, -0.095, 0.5))             # the plane y = 0
top = box((3.0, 3.0, 0.21), position=(0.0, 0.0, H + 0.095))              # the top z = 1
supports = [fixed(bottom, displacement=(inf, inf, 0)), fixed(plane_x, displacement=(0, inf, inf)), fixed(plane_y, displacement=(inf, 0, inf))]
area = math.pi * R * R / 4.0                                             # the exact area of the quarter circle
loads = [force(top, (0.0, 0.0, -1.0 * area))]                            # a pressure of 1 MPa

points = []
for rho in (0.1, 0.2, 0.3, 0.4):
    for angle in (10, 30, 50, 70, 80):
        for z in (0.1, 0.3, 0.5, 0.7, 0.9):
            points.append((rho * math.cos(math.radians(angle)), rho * math.sin(math.radians(angle)), z))
names = ('sigma_x', 'sigma_y', 'sigma_z', 'tau_xy', 'tau_yz', 'tau_zx')
exact = (0.0, 0.0, -1.0, 0.0, 0.0, 0.0)
worst = 0.0
for element in ('tet', 'tet10'):
    r = static_analysis(quarter, supports=supports, loads=loads, material=unit, element_size=SIZE, element=element, cache=False)
    got = [[], [], [], [], [], []]
    for x, y, z in points:
        for n, value in enumerate((r.sxx(x, y, z), r.syy(x, y, z), r.szz(x, y, z), r.sxy(x, y, z), r.syz(x, y, z), r.szx(x, y, z))):
            got[n].append(float(value))
    print('element=%s, %d elements of about %.2f mm' % (element, r.elements, SIZE))
    print('  %-8s %12s %12s %8s' % ('stress', 'smallest', 'largest', 'exact'))
    for n, name in enumerate(names):
        print('  %-8s %+12.6f %+12.6f %8g' % (name, min(got[n]), max(got[n]), exact[n]))
        worst = max(worst, abs(min(got[n]) - exact[n]), abs(max(got[n]) - exact[n]))
if worst > TOLERANCE:
    raise AssertionError('a stress is %.4f from the exact value: more than %.2f' % (worst, TOLERANCE))
print('ok: every stress within %.2f of the exact value (the largest difference is %.5f)' % (TOLERANCE, worst))
