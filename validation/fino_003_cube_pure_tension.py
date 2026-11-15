# Verification: a cube under pure tension (the open-source solver Fino's case 003, https://www.seamplex.com/fino/cases/003-cube-pure-tension, published under CC BY 4.0).
#
# THE PROBLEM, as that page gives it (non-dimensional: E = 1 MPa, nu = 0.3, a cube of 1 mm): a unit cube with one corner at the origin, a total tensile force of 1 N in +x on the face x = 1; the face x = 0 may not move in x
# (it slides in y and z); to remove the rigid body motions the corner at the origin is fixed in all three directions and the edge along z is kept from moving in y.
# THE ANSWER (exact): the stress in x is 1 everywhere and the other five components are 0 everywhere.  Linear tetrahedra represent a uniform stress exactly, so any mesh must give it: it is a PATCH TEST, the check that
# an element passes or fails, and the answer does not depend on how fine the mesh is.
#
# The script solves it twice, with linear (element='tet') and with 10-node tetrahedra, reads the six stresses at a grid of points in the cube and prints the smallest and the largest of each beside the exact value.  It stops
# with an error when one is further from it than the tolerance, so that it can be run as a check after a change.
# WHAT YOU SEE when you open it in FielDes: the cube coloured by its stress (one colour: the stress is the same everywhere) with the three supports and the pull drawn on it, at the end of the file.
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
from fieldes import *

TOLERANCE = 0.01                          # of the applied stress
SIZE = 0.15                               # mm, the largest element of the Fino page

view.set_bounds((-0.5, -0.5, -0.5), (1.5, 1.5, 1.5), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)

unit = Material('unit', 1.0, 0.3)
cube = box((1.0, 1.0, 1.0), position=(0.5, 0.5, 0.5))
left = box((0.21, 3.0, 3.0), position=(-0.095, 0.5, 0.5))              # the face x = 0 (a region a hair deep, in mm: a support holds the nodes the region reaches)
right = box((0.21, 3.0, 3.0), position=(1.095, 0.5, 0.5))              # the face x = 1
origin = sphere(0.02, position=(0.0, 0.0, 0.0))                       # the corner at the origin
edge_z = box((0.04, 0.04, 3.0), position=(0.0, 0.0, 0.5))              # the edge along z
hold_face = fixed(left, displacement=(0, inf, inf))                    # the face x = 0, held in x only
hold_corner = fixed(origin, displacement=(0, 0, 0))                    # the corner at the origin, held in every direction
hold_edge = fixed(edge_z, displacement=(inf, 0, inf))                  # the edge along z, held in y
pull = force(right, (1.0, 0.0, 0.0))                                   # the tension: 1 N on the face x = 1

linear = static_analysis(cube, supports=[hold_face, hold_corner, hold_edge], loads=[pull], material=unit, element_size=SIZE, element='tet', cache=False)
quadratic = static_analysis(cube, supports=[hold_face, hold_corner, hold_edge], loads=[pull], material=unit, element_size=SIZE, element='tet10', cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the analysis and the conditions it was solved with.  The model tree has a row for each result (`linear`, `quadratic`) and for each condition; the eye of a row shows or hides it.
quadratic                                 # the cube with 10-node tetrahedra, coloured by the stress
hold_face                                 # the face x = 0, held in x
hold_corner                               # the corner at the origin
hold_edge                                 # the edge along z
pull                                      # the 1 N tension on the face x = 1


points = [(0.1 + 0.16 * i, 0.1 + 0.16 * j, 0.1 + 0.16 * k) for i in range(6) for j in range(6) for k in range(6)]
names = ('sigma_x', 'sigma_y', 'sigma_z', 'tau_xy', 'tau_yz', 'tau_zx')
exact = (1.0, 0.0, 0.0, 0.0, 0.0, 0.0)


def report(element, result):
    ''' prints the smallest and the largest of the six stresses over the grid of points beside the exact ones; returns the largest difference from the exact value '''
    columns = [[float(f(x, y, z)) for (x, y, z) in points] for f in (result.sxx, result.syy, result.szz, result.sxy, result.syz, result.szx)]
    print('element=%s, %d elements (largest element %.2f mm)' % (element, result.elements, SIZE))
    print('  %-8s %12s %12s %8s' % ('stress', 'smallest', 'largest', 'exact'))
    for name, column, value in zip(names, columns, exact):
        print('  %-8s %+12.6f %+12.6f %8g' % (name, min(column), max(column), value))
    return max(max(abs(min(column) - value), abs(max(column) - value)) for column, value in zip(columns, exact))


worst = max(report('tet', linear), report('tet10', quadratic))
if worst > TOLERANCE:
    raise AssertionError('a stress is %.4f from the exact value: more than %.2f' % (worst, TOLERANCE))
print('ok: every stress within %.2f of the exact value (the largest difference is %.5f)' % (TOLERANCE, worst))
