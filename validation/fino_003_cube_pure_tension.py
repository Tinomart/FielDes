# Verification: a cube under pure tension (the open-source solver Fino's case 003, https://www.seamplex.com/fino/cases/003-cube-pure-tension, published under CC BY 4.0).
#
# THE PROBLEM, as that page gives it (non-dimensional: E = 1 MPa, nu = 0.3, a cube of 1 mm): a unit cube with one corner at the origin, a total tensile force of 1 N in +x on the face x = 1; the face x = 0 may not move in x
# (it slides in y and z); to remove the rigid body motions the corner at the origin is fixed in all three directions and the edge along z is kept from moving in y.
# THE ANSWER (exact): the stress in x is 1 everywhere and the other five components are 0 everywhere.  Linear tetrahedra represent a uniform stress exactly, so any mesh must give it: it is a PATCH TEST, the check that
# an element passes or fails, and the answer does not depend on how fine the mesh is.
#
# The script solves it with linear (element='tet') and 10-node tetrahedra, reads the six stresses at a grid of points in the cube and prints the smallest and the largest of each beside the exact value.  It stops
# with an error when one is further from it than the tolerance, so that it can be run as a check after a change.
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
from fieldes import *

TOLERANCE = 0.01                          # of the applied stress
SIZE = 0.15                               # mm, the largest element of the Fino page

view.set_bounds((-0.5, -0.5, -0.5), (1.5, 1.5, 1.5), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

unit = Material('unit', 1.0, 0.3)
cube = box((1.0, 1.0, 1.0), position=(0.5, 0.5, 0.5))
left = box((0.21, 3.0, 3.0), position=(-0.095, 0.5, 0.5))              # the face x = 0 (a region a hair deep, in mm: a support holds the nodes the region reaches)
right = box((0.21, 3.0, 3.0), position=(1.095, 0.5, 0.5))              # the face x = 1
origin = sphere(0.02, position=(0.0, 0.0, 0.0))                       # the corner at the origin
edge_z = box((0.04, 0.04, 3.0), position=(0.0, 0.0, 0.5))              # the edge along z
supports = [fixed(left, displacement=(0, inf, inf)), fixed(origin, displacement=(0, 0, 0)), fixed(edge_z, displacement=(inf, 0, inf))]
loads = [force(right, (1.0, 0.0, 0.0))]

points = [(0.1 + 0.16 * i, 0.1 + 0.16 * j, 0.1 + 0.16 * k) for i in range(6) for j in range(6) for k in range(6)]
names = ('sigma_x', 'sigma_y', 'sigma_z', 'tau_xy', 'tau_yz', 'tau_zx')
exact = (1.0, 0.0, 0.0, 0.0, 0.0, 0.0)
worst = 0.0
for element in ('tet', 'tet10'):
    r = static_analysis(cube, supports=supports, loads=loads, material=unit, element_size=SIZE, element=element, cache=False)
    got = [[], [], [], [], [], []]
    for x, y, z in points:
        for n, value in enumerate((r.sxx(x, y, z), r.syy(x, y, z), r.szz(x, y, z), r.sxy(x, y, z), r.syz(x, y, z), r.szx(x, y, z))):
            got[n].append(float(value))
    print('element=%s, %d elements (largest element %.2f mm)' % (element, r.elements, SIZE))
    print('  %-8s %12s %12s %8s' % ('stress', 'smallest', 'largest', 'exact'))
    for n, name in enumerate(names):
        print('  %-8s %+12.6f %+12.6f %8g' % (name, min(got[n]), max(got[n]), exact[n]))
        worst = max(worst, abs(min(got[n]) - exact[n]), abs(max(got[n]) - exact[n]))
if worst > TOLERANCE:
    raise AssertionError('a stress is %.4f from the exact value: more than %.2f' % (worst, TOLERANCE))
print('ok: every stress within %.2f of the exact value (the largest difference is %.5f)' % (TOLERANCE, worst))
