# A mesh of your own as the part of an analysis: a beam meshed with a size that grows from its surface, and the static and the modal analysis solving on exactly those tetrahedra.
#
# An analysis that is given a body meshes it itself, with tetrahedra of one size (element_size).  mesh_tet4 does more -- a size that grows from the surface (graded_size), a field of
# sizes, a mended mesh, 10-node elements -- and the analyses take its result in place of the body: static_analysis(mesh, ...), modal_analysis(mesh, ...), thermal_analysis(mesh, ...), and
# for a flow fluid_analysis(body, mesh_of_the_fluid, ...).  The elements are the mesh's own (no element_size), the supports and loads are regions as ever, and the result is drawn on the
# body the mesh was made of.
#
# The result card's Elements button shows the tetrahedra the analysis used: small at the surface, large in the middle.  The number of them is the mesh's.
#
# Try: wall_size=3.0 (a coarse mesh) against 0.8 (a fine one) and watch the tip movement and the first frequency settle; mesh_tet10(beam, 3.0) gives a 10-node mesh whose
# corners the analysis takes, and static_analysis(mesh, ..., element='tet10') makes the quadratic element on them.
#
from fieldes import *

view.set_bounds((-24, -10, -10), (24, 10, 10), auto_scale=True)
view.set_resolution(25)
view.set_quality(8)

beam = box((40, 10, 10), position=(0, 0, 0))                       # x from -20 to 20 mm
mesh = mesh_tet4(beam, graded_size(beam, wall_size=1.5, growth_rate=1.3, max_size=5.0))        # fine at the surface, coarse inside
print(mesh)

base = box((4, 14, 14), position=(-20, 0, 0))                      # the end that is held
tip = box((4, 14, 14), position=(20, 0, 0))                        # the end that is pulled down
result = static_analysis(mesh, supports=[fixed(base)], loads=[force(tip, (0, 0, -100))], material=steel)
print('tip movement %.4f mm, on %d tetrahedra (the mesh has %d)' % (result.max_displacement, result.elements, mesh.cell_count('tet4')))

modes = modal_analysis(mesh, supports=[fixed(base)], material=steel, modes=3)
print('first natural frequency %.0f Hz' % modes.frequencies[0])

result                                                             # the stress on the beam; the Elements button shows the mesh that was solved
