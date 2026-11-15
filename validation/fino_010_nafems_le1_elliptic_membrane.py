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
# stress at D and prints it beside the reference with the error, the number of elements and the time.  Open it in FielDes (File > Open) and it runs.  The nine sizes are the nine analyses below, 90 mm down to 50 mm: change a number to try another size.
# WHAT YOU SEE when you open it: the quarter plate of the finest mesh (`result_50`) coloured by its stress, with the two symmetry faces, the anchor and the traction drawn on it.  Each of the nine analyses is a row of the model tree
# (`result_90` ... `result_50`): its eye shows it.  To look at another size, state its name instead.
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
import math

from fieldes import *

REFERENCE = 92.7                                               # MPa
A_IN, B_IN, A_OUT, B_OUT, H = 2000.0, 1000.0, 3250.0, 2750.0, 100.0
P = 10.0                                                       # MPa, the traction on the outer edge
TOLERANCE = 0.10          # the finest mesh must be within 10 % of the benchmark's value, and the MEDIAN of the sizes within 2 % (the 10-node element at a smooth boundary point).  A single size that is only luckily right is not a validation: the sweep prints every size and the sizes more than 5 % off

view.set_bounds((-100, -100, -50), (3400, 2900, 150), auto_scale=True)
view.set_resolution(12)                 # ten samples across the longest side of a model for every unit (docs/caching-and-performance.md), for the result as for the conditions drawn on it: 120 across the plate of 3,350 mm, cells of 28 mm
view.set_quality(8)


def ellipse(a, b):
    ''' an elliptic cylinder of the semi-axes a and b, much longer than the plate in z.  Made a length (the mean semi-axis times the unit cylinder taken onto the ellipse): a support or a load picks the nodes whose value is
        within 0.05 of an element of its region (mm), and a value in units of the axes would have every node of the plate inside every region (see nafems_le10_thick_plate.py).
        NOT cut off at the faces of the plate: a hole whose ends lie exactly in the faces of the plate (faces that coincide) has the value 0 over its whole end, so the plate's field is 0 there and not positive, and a mesh whose
        lattice has vertices in those planes puts tetrahedra into the hole '''
    return cylinder(1.0, 10000.0).remap(Shape.X() / a, Shape.Y() / b, Shape.Z()) * ((a + b) / 2.0)


quarter = box((A_OUT + 100, B_OUT + 100, H), position=((A_OUT + 100) / 2, (B_OUT + 100) / 2, H / 2))
plate = intersection(difference(ellipse(A_OUT, B_OUT), ellipse(A_IN, B_IN)), quarter)       # the faces of the plate are those of the quarter box alone
face_y0 = box((A_OUT + 200, 2.0, H + 200), position=((A_OUT + 100) / 2, -1.0 + 0.01, H / 2))      # y = 0: held in y
hold_y0 = fixed(face_y0, displacement=(inf, 0, inf))
face_x0 = box((2.0, B_OUT + 200, H + 200), position=(-1.0 + 0.01, (B_OUT + 100) / 2, H / 2))       # x = 0: held in x
hold_x0 = fixed(face_x0, displacement=(0, inf, inf))
outer_edge = difference(ellipse(A_OUT + 1, B_OUT + 1), ellipse(A_OUT - 1, B_OUT - 1))           # a skin 2 mm thick round the outer ellipse, all the way through z: the triangles of the part's surface with all three corners in it are those of the outer edge
traction = pressure(outer_edge, -P)                              # 10 MPa along the normal of the outer edge, pulling outward

# the nine sizes, from a plate 100 mm thick with elements about as big as it is thick to one with two elements through it; each size lays its mesh differently against D.  The anchor of each: the few nodes round one point of the
# face y = 0 at the mid-plane, (2700, 0, 50), held in z -- the plate is free in z and a motion of the whole plate in z is no deformation, so it has to be held somewhere, and a point 700 mm from D = (2000, 0), ten elements and more, does not touch the stress there
anchor_90 = fixed(sphere(0.75 * 90.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_90 = static_analysis(mesh_tet10(plate, 90.0), supports=[hold_y0, hold_x0, anchor_90], loads=[traction], material=steel, element='tet10', cache=False)
anchor_85 = fixed(sphere(0.75 * 85.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_85 = static_analysis(mesh_tet10(plate, 85.0), supports=[hold_y0, hold_x0, anchor_85], loads=[traction], material=steel, element='tet10', cache=False)
anchor_80 = fixed(sphere(0.75 * 80.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_80 = static_analysis(mesh_tet10(plate, 80.0), supports=[hold_y0, hold_x0, anchor_80], loads=[traction], material=steel, element='tet10', cache=False)
anchor_75 = fixed(sphere(0.75 * 75.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_75 = static_analysis(mesh_tet10(plate, 75.0), supports=[hold_y0, hold_x0, anchor_75], loads=[traction], material=steel, element='tet10', cache=False)
anchor_70 = fixed(sphere(0.75 * 70.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_70 = static_analysis(mesh_tet10(plate, 70.0), supports=[hold_y0, hold_x0, anchor_70], loads=[traction], material=steel, element='tet10', cache=False)
anchor_65 = fixed(sphere(0.75 * 65.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_65 = static_analysis(mesh_tet10(plate, 65.0), supports=[hold_y0, hold_x0, anchor_65], loads=[traction], material=steel, element='tet10', cache=False)
anchor_60 = fixed(sphere(0.75 * 60.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_60 = static_analysis(mesh_tet10(plate, 60.0), supports=[hold_y0, hold_x0, anchor_60], loads=[traction], material=steel, element='tet10', cache=False)
anchor_55 = fixed(sphere(0.75 * 55.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_55 = static_analysis(mesh_tet10(plate, 55.0), supports=[hold_y0, hold_x0, anchor_55], loads=[traction], material=steel, element='tet10', cache=False)
anchor_50 = fixed(sphere(0.75 * 50.0, position=(2700.0, 0.0, H / 2)), displacement=(inf, inf, 0))
result_50 = static_analysis(mesh_tet10(plate, 50.0), supports=[hold_y0, hold_x0, anchor_50], loads=[traction], material=steel, element='tet10', cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the analysis of the finest mesh and the conditions it was solved with (the eye of each row in the model tree shows or hides it; `result_90` ... `result_55` are the other sizes)
result_50                                 # the quarter plate, coloured by the stress
hold_y0                                   # the face y = 0, held in y
hold_x0                                   # the face x = 0, held in x
anchor_50                                 # the few nodes held in z round (2700, 0, 50)
traction                                  # the 10 MPa pulling outward on the outer edge

sizes = (90.0, 85.0, 80.0, 75.0, 70.0, 65.0, 60.0, 55.0, 50.0)
results = (result_90, result_85, result_80, result_75, result_70, result_65, result_60, result_55, result_50)
stresses = [float(r.syy(A_IN, 0.0, H / 2)) for r in results]
errors = [s / REFERENCE - 1.0 for s in stresses]
print('NAFEMS LE1, stress in y at D = (2000, 0), reference %.2f MPa' % REFERENCE)
print('%9s %10s %12s %10s %8s' % ('size mm', 'elements', 'sigma_y MPa', 'error', 'seconds'))
print('\n'.join('%9.1f %10d %12.3f %+9.1f %% %8.1f' % (h, r.elements, s, 100 * e, r.seconds) for h, r, s, e in zip(sizes, results, stresses, errors)))
median = sorted(abs(e) for e in errors)[len(errors) // 2]
spoiled = [h for h, e in zip(sizes, errors) if abs(e) > 0.05]
print('median error %.1f %%; sizes more than 5 %% off: %s' % (100 * median, ', '.join('%g' % s for s in spoiled) or 'none'))
assert abs(errors[-1]) <= TOLERANCE, 'the stress at D on the finest mesh is %+.1f %% from the benchmark: more than %.0f %%' % (100 * errors[-1], 100 * TOLERANCE)
assert median <= 0.02, 'the median error of the sizes is %.1f %%: more than 2 %%' % (100 * median)
print('ok: the finest mesh is within %.0f %% of the benchmark and the median of the sizes within 2 %%' % (100 * TOLERANCE))
