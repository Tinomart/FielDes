# Verification 12: the meshes of the Mesh menu against the exact volume and surface of the body they are made of.
#
# A mesh is the body cut into cells, so the cells must add up to the body.  Three bodies whose volume and surface follow from their dimensions alone (the formulas are written beside the numbers):
#   * a plate 40 x 20 x 6 mm with a hole of radius 4 mm through it:  V = a b c - pi r^2 c = 4,498.41 mm^3,  A = 2 a b - 2 pi r^2 + 2 (a + b) c + 2 pi r c = 2,370.27 mm^2
#   * a ball of radius 10 mm:  V = 4/3 pi r^3 = 4,188.79 mm^3,  A = 4 pi r^2 = 1,256.64 mm^2
#   * a cylinder of radius 4 mm and length 14 mm turned by 30 degrees about x (its edges lie off the grid of the cut cells):  V = pi r^2 L = 703.72 mm^3,  A = 2 pi r L + 2 pi r^2 = 452.39 mm^2
# Every function of the Volume section of the menu meshes the plate (the ball too, except `mesh_hex_extrusion`, which sweeps a prism and refuses a ball), the two functions of the Surface section mesh both; the turned cylinder is meshed by the tetrahedra and by the cut cells.
# The script prints the volume the cells add up to (`mesh.volume()`), the area of the boundary (`mesh.surface_area()`), the number of cells that are inside out or flat (`mesh.quality().negative`),
# and stops with an AssertionError when a number is farther off than its tolerance or a cell is inside out.
#
# THE TOLERANCES SAY WHAT KIND OF MESH IT IS, they are not tuned to the answers:  tetrahedra that follow the surface 2 %;  cells of a grid cut by the surface (`mesh_cartesian`) 3 %;
# the hexahedra of `mesh_hex_quanta` and `mesh_hex20_quanta` follow the surface by a staircase of whole cells (the one flag `quanta` says how much of a cell has to be inside), so 10 % on the volume and no check of the area;
# the swept hexahedra of `mesh_hex_extrusion` follow the outline of the section exactly, but their section is a polygon on the circle, so 2 %.
#
# WHAT YOU SEE when you open it: every mesh that was measured, in a gallery of four rows (each body is moved along x and y only to put the meshes side by side: the numbers are measured on the meshes shown), at the end of the file.
# Row 1, the plate with a hole: mesh_tet4, mesh_tet10, mesh_hex_quanta, mesh_hex20_quanta, mesh_cartesian, mesh_hex_extrusion.  Row 2, the ball: mesh_tet4, mesh_tet10, mesh_hex_quanta, mesh_cartesian.
# Row 3, the turned cylinder: mesh_tet4, mesh_cartesian.  Row 4, the surface meshes: triangles and quadrilaterals of the plate and of the ball.  Each mesh is a row of the model tree; the result card of a mesh shows its cells.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR MESH: a result is only as good as its mesh, see the page "Validation and accuracy" and the section Quality of docs/meshing.md.
#
import math

from fieldes import *

view.set_bounds((-30, -30, -15), (330, 130, 15), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)

failures = []


def check(name, mesh, exact_volume, volume_tolerance, exact_area=None, area_tolerance=None):
    volume = mesh.volume()
    negative = mesh.quality().negative
    line = "%-44s %8d cells   volume %9.2f (exact %9.2f) %+6.2f %%" % (name, mesh.cell_count(), volume, exact_volume, 100 * (volume / exact_volume - 1))
    if exact_area is not None:
        area = mesh.surface_area()
        line += "   area %9.2f (exact %9.2f) %+6.2f %%" % (area, exact_area, 100 * (area / exact_area - 1))
        if abs(area / exact_area - 1) > area_tolerance:
            failures.append("%s: the area is %+.2f %% off (more than %.0f %%)" % (name, 100 * (area / exact_area - 1), 100 * area_tolerance))
    print(line + ("   inside out: %d" % negative if negative else ""))
    if abs(volume / exact_volume - 1) > volume_tolerance:
        failures.append("%s: the volume is %+.2f %% off (more than %.0f %%)" % (name, 100 * (volume / exact_volume - 1), 100 * volume_tolerance))
    if negative:
        failures.append("%s: %d cells are inside out or flat" % (name, negative))


def surface_check(name, mesh, exact_area, tolerance):
    area = mesh.surface_area()
    print("%-44s %8d faces   area %9.2f (exact %9.2f) %+6.2f %%" % (name, mesh.face_count(), area, exact_area, 100 * (area / exact_area - 1)))
    if abs(area / exact_area - 1) > tolerance:
        failures.append("%s: the area is %+.2f %% off (more than %.0f %%)" % (name, 100 * (area / exact_area - 1), 100 * tolerance))


def at(shape, column, row):
    ''' the body moved to the place of its mesh in the gallery: 60 mm along x for each column, 40 mm along y for each row (the volume and the area do not change) '''
    return move(shape, (60.0 * column, 40.0 * row, 0.0))


plate_volume = 40 * 20 * 6 - math.pi * 4 ** 2 * 6
plate_area = 2 * 40 * 20 - 2 * math.pi * 4 ** 2 + 2 * (40 + 20) * 6 + 2 * math.pi * 4 * 6
ball_volume = 4.0 / 3.0 * math.pi * 10 ** 3
ball_area = 4 * math.pi * 10 ** 2
tilt_volume = math.pi * 4 ** 2 * 14
tilt_area = 2 * math.pi * 4 * 14 + 2 * math.pi * 4 ** 2
plate = difference(box((40, 20, 6)), cylinder(4, 20))
ball = sphere(10)
tilt = cylinder(4, 14, rotation=(30, 0, 0))

print("The plate with a hole (element size 2 mm)")
plate_tet4 = mesh_tet4(at(plate, 0, 0), 2.0)
check("mesh_tet4: tetrahedra", plate_tet4, plate_volume, 0.02, plate_area, 0.04)
plate_tet10 = mesh_tet10(at(plate, 1, 0), 2.0)
check("mesh_tet10: tetrahedra of 10 nodes", plate_tet10, plate_volume, 0.02, plate_area, 0.04)
plate_hex = mesh_hex_quanta(at(plate, 2, 0), 2.0)
check("mesh_hex_quanta: hexahedra", plate_hex, plate_volume, 0.10)
plate_hex20 = mesh_hex20_quanta(at(plate, 3, 0), 2.0)
check("mesh_hex20_quanta: hexahedra of 20 nodes", plate_hex20, plate_volume, 0.10)
plate_cartesian = mesh_cartesian(at(plate, 4, 0), 2.0)
check("mesh_cartesian: a grid cut by the surface", plate_cartesian, plate_volume, 0.03, plate_area, 0.04)
plate_extrusion = mesh_hex_extrusion(at(plate, 5, 0), 1.0)
check("mesh_hex_extrusion: the section swept along z", plate_extrusion, plate_volume, 0.02)
print("The ball of radius 10 (element size 2 mm)")
ball_tet4 = mesh_tet4(at(ball, 0, 1), 2.0)
check("mesh_tet4: tetrahedra", ball_tet4, ball_volume, 0.02, ball_area, 0.04)
ball_tet10 = mesh_tet10(at(ball, 1, 1), 2.0)
check("mesh_tet10: tetrahedra of 10 nodes", ball_tet10, ball_volume, 0.02, ball_area, 0.04)
ball_hex = mesh_hex_quanta(at(ball, 2, 1), 2.0)
check("mesh_hex_quanta: hexahedra", ball_hex, ball_volume, 0.10)
ball_cartesian = mesh_cartesian(at(ball, 3, 1), 2.0)
check("mesh_cartesian: a grid cut by the surface", ball_cartesian, ball_volume, 0.03, ball_area, 0.04)
print("The cylinder of radius 4 and length 14 turned by 30 degrees (element size 1 mm)")
tilt_tet4 = mesh_tet4(at(tilt, 0, 2), 1.0)
check("mesh_tet4: tetrahedra", tilt_tet4, tilt_volume, 0.02, tilt_area, 0.04)
tilt_cartesian = mesh_cartesian(at(tilt, 1, 2), 1.0)
check("mesh_cartesian: a grid cut by the surface", tilt_cartesian, tilt_volume, 0.03, tilt_area, 0.04)

print("The surface of the bodies (element size 1 mm; flat triangles inscribed in a curve are always a little small)")
plate_tri = mesh_surface_tri(at(plate, 0, 3), 1.0)
surface_check("mesh_surface_tri (triangles), plate", plate_tri, plate_area, 0.03)
plate_quad = mesh_surface_quad(at(plate, 1, 3), 1.0)
surface_check("mesh_surface_quad (quadrilaterals), plate", plate_quad, plate_area, 0.03)
ball_tri = mesh_surface_tri(at(ball, 2, 3), 1.0)
surface_check("mesh_surface_tri (triangles), ball", ball_tri, ball_area, 0.03)
ball_quad = mesh_surface_quad(at(ball, 3, 3), 1.0)
surface_check("mesh_surface_quad (quadrilaterals), ball", ball_quad, ball_area, 0.03)

if failures:
    raise AssertionError("; ".join(failures))
print("ok")

# SHOWN IN THE VIEWPORT: every mesh that was measured, side by side (the eye of each row in the model tree shows or hides it)
plate_tet4
plate_tet10
plate_hex
plate_hex20
plate_cartesian
plate_extrusion
ball_tet4
ball_tet10
ball_hex
ball_cartesian
tilt_tet4
tilt_cartesian
plate_tri
plate_quad
ball_tri
ball_quad
