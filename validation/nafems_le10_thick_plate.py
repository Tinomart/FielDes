# NAFEMS LE10, "thick plate pressure" (The Standard NAFEMS Benchmarks, 1990): the stress at one point of a thick elliptic plate under pressure, compared with the reference value of the benchmark.
#
# THE PROBLEM, as the open-source solver FeenoX/Fino documents it (https://www.seamplex.com/fino/cases/012-nafems-le10 -- the input of that case is the source of every number below), in mm:
#   a plate between two ellipses, the inner one with the semi-axes 2000 (x) and 1000 (y), the outer one 3250 (x) and 2750 (y), 600 thick; one quarter of it (x >= 0, y >= 0) is meshed
#   E = 210 GPa, nu = 0.3, a uniform pressure of 1 MPa on the upper face (z = 600), pressing down
#   held: the face y = 0 against moving in y, the face x = 0 against moving in x, the outer elliptic face against moving in x and y, and the edge of that face at the mid-plane (z = 300) against moving in z
#   asked: the stress in the y direction at D = (2000, 0, 600), the corner of the upper face on the inner ellipse
# THE REFERENCE: -5.38 MPa (the value of the NAFEMS benchmark; the same page and the benchmark pages of OptiStruct, COMSOL and others quote it).  It is a benchmark's value, found by many codes' converged
# meshes of 20-node hexahedra, not an exact solution.  MEASURED 2026-11-14 (docs/validation.md): the stress at D is a point at a corner of the body, read from the one element there, and it SCATTERS round the reference as the lattice of the mesh moves against the
# corner -- with linear tetrahedra of 100 to 50 mm by about -9 % to +12 %, with the 10-node element by more: read it as the error bar of a stress at a corner, not as a number that converges (see mesh_convergence_study.py).
#
# WHAT THIS SCRIPT DOES: meshes the quarter plate with mesh_tet4 (linear and 10-node tetrahedra, the nodes of the boundary on its surface) at four element sizes that divide the half thickness (100, 75, 60 and 50 mm: a coarser mesh has too few elements through the 600 mm of the plate), solves the analysis on
# exactly that mesh (static_analysis(mesh, ...)), reads the stress at D, and prints it beside the reference with the error, the number of elements and the time.  Open it in FielDes (File > Open) and it runs: about 4 minutes (the finest mesh has about 300,000 elements).  SIZES and ELEMENTS are at the top.
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
import math

from fieldes import *

ELEMENTS = ['tet', 'tet10']
SIZES = [100.0, 75.0, 60.0, 50.0]                              # mm
REFERENCE = -5.38                                              # MPa
A_IN, B_IN, A_OUT, B_OUT, H = 2000.0, 1000.0, 3250.0, 2750.0, 600.0
P = 1.0                                                        # MPa

view.set_bounds((-100, -100, -50), (3400, 2900, 700), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)


def ellipse(a, b):
    ''' an elliptic cylinder of the semi-axes a and b, much longer than the plate in z.  The unit cylinder taken onto the ellipse by remap has a value in units of the semi-axes, not in mm, and a support or a load picks the nodes whose
        value is within 0.05 of an element of its region (mm): with the value in units of the axes EVERY node of the plate was inside every region, the whole plate was held, and the stress came out 12 times too low.
        The value is made a length (about: the mean semi-axis times it) so that the regions below are where they say.
        NOT cut off at the faces of the plate: a hole whose ends lie exactly in the faces of the plate (faces that coincide) has the value 0 over its whole end, so the plate's field is 0 there and not positive, and a mesh whose
        lattice has vertices in those planes puts tetrahedra into the hole (found 2026-11-15 with the LE1 plate of fino_010_nafems_le1_elliptic_membrane.py) '''
    return cylinder(1.0, 10000.0).remap(Shape.X() / a, Shape.Y() / b, Shape.Z()) * ((a + b) / 2.0)


quarter = box((A_OUT + 100, B_OUT + 100, H), position=((A_OUT + 100) / 2, (B_OUT + 100) / 2, H / 2))
plate = intersection(difference(ellipse(A_OUT, B_OUT), ellipse(A_IN, B_IN)), quarter)
face_y0 = box((A_OUT + 200, 2.0, H + 200), position=((A_OUT + 100) / 2, -1.0 + 0.01, H / 2))      # y = 0: held in y
face_x0 = box((2.0, B_OUT + 200, H + 200), position=(-1.0 + 0.01, (B_OUT + 100) / 2, H / 2))       # x = 0: held in x
band = difference(ellipse(A_OUT + 40, B_OUT + 40), ellipse(A_OUT - 30, B_OUT - 30))              # a band round the outer ellipse (its surface inside it is the outer face)
outer_face = intersection(band, box((3 * A_OUT, 3 * B_OUT, H - 10.0), position=(0, 0, H / 2)))    # the outer face, 5 mm short of the upper and lower faces
def mid_edge_for(h):
    ''' the edge of the outer face at the mid-plane: the nodes within half an element of it, held in all three directions (a line of the benchmark is a row of nodes of the mesh) '''
    return intersection(band, box((3 * A_OUT, 3 * B_OUT, h), position=(0, 0, H / 2)))


top = box((A_OUT + 200, B_OUT + 200, 2.0), position=((A_OUT + 100) / 2, (B_OUT + 100) / 2, H + 1.0 - 0.01))   # the upper face z = 600
area = math.pi / 4.0 * (A_OUT * B_OUT - A_IN * B_IN)           # mm2, the quarter of the upper face
loads = [force(top, (0, 0, -P * area))]

print('NAFEMS LE10, stress in y at D = (2000, 0, 600) mm, reference %.2f MPa' % REFERENCE)
print('%-6s %9s %10s %12s %10s %8s' % ('elem.', 'size mm', 'elements', 'sigma_y MPa', 'error', 'seconds'))
rows = []
for element in ELEMENTS:
    for h in SIZES:
        mesh = mesh_tet10(plate, h) if element == 'tet10' else mesh_tet4(plate, h)
        supports = [fixed(face_y0, displacement=(inf, 0, inf)), fixed(face_x0, displacement=(0, inf, inf)), fixed(outer_face, displacement=(0, 0, inf)), fixed(mid_edge_for(h), displacement=(0, 0, 0))]
        result = static_analysis(mesh, supports=supports, loads=loads, material=steel, element=element, cache=False)
        sy = float(result.syy(A_IN, 0.0, H - 0.1))
        rows.append((element, h, result.elements, sy, sy / REFERENCE - 1.0, result.seconds))
        print('%-6s %9.1f %10d %12.3f %+9.1f %% %8.1f' % (element, h, result.elements, sy, 100 * (sy / REFERENCE - 1.0), result.seconds), flush=True)
rows
