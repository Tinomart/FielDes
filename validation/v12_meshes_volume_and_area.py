# Verification 12: the meshes of the Mesh menu against the exact volume and surface of the body they are made of.
#
# A mesh is the body cut into cells, so the cells must add up to the body.  Two bodies whose volume and surface follow from their dimensions alone (the formulas are written beside the numbers):
#   * a plate 40 x 20 x 6 mm with a hole of radius 4 mm through it:  V = a b c - pi r^2 c = 4,498.41 mm^3,  A = 2 a b - 2 pi r^2 + 2 (a + b) c + 2 pi r c = 2,370.27 mm^2
#   * a ball of radius 10 mm:  V = 4/3 pi r^3 = 4,188.79 mm^3,  A = 4 pi r^2 = 1,256.64 mm^2
# Every function of the Volume section of the menu meshes the plate (the ball too, except `mesh_hex_extrusion`, which sweeps a prism and refuses a ball), the two functions of the Surface section mesh both.
# The script prints the volume the cells add up to (`mesh.volume()`), the area of the boundary (`mesh.surface_area()`), the number of cells that are inside out or flat (`mesh.quality().negative`),
# and stops with an AssertionError when a number is farther off than its tolerance or a cell is inside out.
#
# THE TOLERANCES SAY WHAT KIND OF MESH IT IS, they are not tuned to the answers:  tetrahedra that follow the surface 2 %;  cells of a grid cut by the surface (`mesh_cartesian`) 3 %;
# the hexahedra of `mesh_hex_quanta` and `mesh_hex20_quanta` follow the surface by a staircase of whole cells (the one flag `quanta` says how much of a cell has to be inside), so 10 % on the volume and no check of the area;
# the swept hexahedra of `mesh_hex_extrusion` follow the outline of the section exactly, but their section is a polygon on the circle, so 2 %.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR MESH: a result is only as good as its mesh, see the page "Validation and accuracy" and the section Quality of docs/meshing.md.
#
import math

from fieldes import *

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


plate = difference(box((40, 20, 6)), cylinder(4, 20))
plate_volume = 40 * 20 * 6 - math.pi * 4 ** 2 * 6
plate_area = 2 * 40 * 20 - 2 * math.pi * 4 ** 2 + 2 * (40 + 20) * 6 + 2 * math.pi * 4 * 6
ball = sphere(10)
ball_volume = 4.0 / 3.0 * math.pi * 10 ** 3
ball_area = 4 * math.pi * 10 ** 2

print("The plate with a hole (element size 2 mm)")
check("mesh_tet4: tetrahedra", mesh_tet4(plate, 2.0), plate_volume, 0.02, plate_area, 0.04)
check("mesh_tet10: tetrahedra of 10 nodes", mesh_tet10(plate, 2.0), plate_volume, 0.02, plate_area, 0.04)
check("mesh_hex_quanta: hexahedra", mesh_hex_quanta(plate, 2.0), plate_volume, 0.10)
check("mesh_hex20_quanta: hexahedra of 20 nodes", mesh_hex20_quanta(plate, 2.0), plate_volume, 0.10)
check("mesh_cartesian: a grid cut by the surface", mesh_cartesian(plate, 2.0), plate_volume, 0.03)
check("mesh_hex_extrusion: the section swept along z", mesh_hex_extrusion(plate, 1.0), plate_volume, 0.02)
print("The ball of radius 10 (element size 2 mm)")
check("mesh_tet4: tetrahedra", mesh_tet4(ball, 2.0), ball_volume, 0.02, ball_area, 0.04)
check("mesh_tet10: tetrahedra of 10 nodes", mesh_tet10(ball, 2.0), ball_volume, 0.02, ball_area, 0.04)
check("mesh_hex_quanta: hexahedra", mesh_hex_quanta(ball, 2.0), ball_volume, 0.10)
check("mesh_cartesian: a grid cut by the surface", mesh_cartesian(ball, 2.0), ball_volume, 0.03)


def surface_check(name, mesh, exact_area, tolerance):
    area = mesh.surface_area()
    print("%-44s %8d faces   area %9.2f (exact %9.2f) %+6.2f %%" % (name, mesh.face_count(), area, exact_area, 100 * (area / exact_area - 1)))
    if abs(area / exact_area - 1) > tolerance:
        failures.append("%s: the area is %+.2f %% off (more than %.0f %%)" % (name, 100 * (area / exact_area - 1), 100 * tolerance))


print("The surface of the bodies (element size 1 mm; flat triangles inscribed in a curve are always a little small)")
surface_check("mesh_surface_tri (triangles), plate", mesh_surface_tri(plate, 1.0), plate_area, 0.03)
surface_check("mesh_surface_quad (quadrilaterals), plate", mesh_surface_quad(plate, 1.0), plate_area, 0.03)
surface_check("mesh_surface_tri (triangles), ball", mesh_surface_tri(ball, 1.0), ball_area, 0.03)
surface_check("mesh_surface_quad (quadrilaterals), ball", mesh_surface_quad(ball, 1.0), ball_area, 0.03)

if failures:
    raise AssertionError("; ".join(failures))
print("ok")
