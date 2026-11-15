# NAFEMS LE1, "elliptic membrane" (The Standard NAFEMS Benchmarks, 1990): the stress at one point of a plate between two ellipses, loaded by a pressure on its outer edge, compared with the reference value of the benchmark.
# It is the test of pressure(region, value): a load at right angles to a CURVED surface, which force() (one total vector) cannot make.
#
# THE PROBLEM, as the open-source solver FeenoX/Fino documents it (https://www.seamplex.com/fino/cases/010-nafems-le1 -- the input of that case is the source of every number below), in mm:
#   a plate between two ellipses, the inner one with the semi-axes 2000 (x) and 1000 (y), the outer one 3250 (x) and 2750 (y), 100 thick (plane stress); one quarter of it (x >= 0, y >= 0) is meshed
#   E = 210 GPa, nu = 0.3, a pressure of 10 MPa on the outer edge, along the normal of the edge and pulling outward (a traction)
#   held: the face y = 0 against moving in y, the face x = 0 against moving in x (the symmetry), and the few nodes round the point (2700, 0, 50) against moving in z (a free plate has nothing else to hold it in z)
#   asked: the stress in the y direction at D = (2000, 0), the vertex of the inner ellipse on the axis x
# THE REFERENCE: 92.7 MPa (the value of the NAFEMS benchmark; the Fino page and the benchmark pages of other codes quote it).  A benchmark's value, found by many codes' converged meshes, not an exact solution.
#
# WHAT THIS SCRIPT DOES: meshes the quarter plate with mesh_tet10 (10-node tetrahedra, the nodes of the boundary on the surface) at nine element sizes, solves the analysis on exactly that mesh (static_analysis(mesh, ...)), reads the
# stress at D and prints it beside the reference with the error, the number of elements and the time.  Open it in FielDes (File > Open) and it runs.  SIZES is at the top.
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
import math

from fieldes import *

SIZES = [90.0, 85.0, 80.0, 75.0, 70.0, 65.0, 60.0, 55.0, 50.0]  # mm: every size from a plate 100 mm thick with elements about as big as it is thick to one with two elements through it; each size lays its mesh differently against D
REFERENCE = 92.7                                               # MPa
A_IN, B_IN, A_OUT, B_OUT, H = 2000.0, 1000.0, 3250.0, 2750.0, 100.0
P = 10.0                                                       # MPa, the traction on the outer edge

view.set_bounds((-100, -100, -50), (3400, 2900, 150), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)


def ellipse(a, b):
    ''' an elliptic cylinder of the semi-axes a and b, much longer than the plate in z.  Made a length (the mean semi-axis times the unit cylinder taken onto the ellipse): a support or a load picks the nodes whose value is
        within 0.05 of an element of its region (mm), and a value in units of the axes would have every node of the plate inside every region (see nafems_le10_thick_plate.py).
        NOT cut off at the faces of the plate: a hole whose ends lie exactly in the faces of the plate (faces that coincide) has the value 0 over its whole end, so the plate's field is 0 there and not positive, and a mesh whose
        lattice has vertices in those planes puts tetrahedra into the hole (flat slivers beside D at the sizes 85, 60 and 55 mm: found 2026-11-15 by looking at the field at the corners of the sliver cells) '''
    return cylinder(1.0, 10000.0).remap(Shape.X() / a, Shape.Y() / b, Shape.Z()) * ((a + b) / 2.0)


quarter = box((A_OUT + 100, B_OUT + 100, H), position=((A_OUT + 100) / 2, (B_OUT + 100) / 2, H / 2))
plate = intersection(difference(ellipse(A_OUT, B_OUT), ellipse(A_IN, B_IN)), quarter)       # the faces of the plate are those of the quarter box alone
face_y0 = box((A_OUT + 200, 2.0, H + 200), position=((A_OUT + 100) / 2, -1.0 + 0.01, H / 2))      # y = 0: held in y
face_x0 = box((2.0, B_OUT + 200, H + 200), position=(-1.0 + 0.01, (B_OUT + 100) / 2, H / 2))       # x = 0: held in x
outer_edge = difference(ellipse(A_OUT + 1, B_OUT + 1), ellipse(A_OUT - 1, B_OUT - 1))           # a skin 2 mm thick round the outer ellipse, all the way through z: the triangles of the part's surface with all three corners in it are those of the outer edge (a band 70 mm wide, as the first version had, also took the strips of the two faces beside the edge: a tension on them, sigma_z of +10 MPa)


def anchor(h):
    ''' the few nodes round one point of the face y = 0 at the mid-plane, (2700, 0, 50), held in z: the plate is free in z and a motion of the whole plate in z is no deformation, so it has to be held somewhere, and a point far from D = (2000, 0)
        (700 mm: ten elements and more) does not touch the stress there.  (A whole row of nodes along y = 0 held in z, as a first version of this script did, holds the plate where D is and gave -3,573 %, -4.6 % and +39 %) '''
    return sphere(0.75 * h, position=(2700.0, 0.0, H / 2))


TOLERANCE = 0.10          # the finest mesh must be within 10 % of the benchmark's value, and the MEDIAN of the sizes within 2 % (the 10-node element at a smooth boundary point).  Before the mesher put a node at the corner where the wall of the hole, the plane y = 0 and a face of the plate meet, three sizes (85, 60 and 55 mm) read 14 to 29 % high: the sweep prints every size and the sizes more than 5 % off, and a single size that is only luckily right is not a validation
print('NAFEMS LE1, stress in y at D = (2000, 0), reference %.2f MPa' % REFERENCE)
print('%9s %10s %12s %10s %8s' % ('size mm', 'elements', 'sigma_y MPa', 'error', 'seconds'))
rows = []
for h in SIZES:
    mesh = mesh_tet10(plate, h)
    supports = [fixed(face_y0, displacement=(inf, 0, inf)), fixed(face_x0, displacement=(0, inf, inf)), fixed(anchor(h), displacement=(inf, inf, 0))]
    result = static_analysis(mesh, supports=supports, loads=[pressure(outer_edge, -P)], material=steel, element='tet10', cache=False)
    sy = float(result.syy(A_IN, 0.0, H / 2))
    rows.append((h, result.elements, sy, sy / REFERENCE - 1.0, result.seconds))
    print('%9.1f %10d %12.3f %+9.1f %% %8.1f' % (h, result.elements, sy, 100 * (sy / REFERENCE - 1.0), result.seconds), flush=True)
errors = sorted(abs(r[3]) for r in rows)
median = errors[len(errors) // 2]
spoiled = [r[0] for r in rows if abs(r[3]) > 0.05]
print('median error %.1f %%; sizes more than 5 %% off: %s' % (100 * median, ', '.join('%g' % s for s in spoiled) or 'none'))
if abs(rows[-1][3]) > TOLERANCE:
    raise AssertionError('the stress at D on the finest mesh is %+.1f %% from the benchmark: more than %.0f %%' % (100 * rows[-1][3], 100 * TOLERANCE))
if median > 0.02:
    raise AssertionError('the median error of the sizes is %.1f %%: more than 2 %%' % (100 * median))
print('ok: the finest mesh is within %.0f %% of the benchmark and the median of the sizes within 2 %%' % (100 * TOLERANCE))
