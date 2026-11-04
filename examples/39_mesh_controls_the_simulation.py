# One plate with a hole, pulled; the same simulation on three meshes you control -- and how much the mesh changes the answer.
#
# A simulation does not solve the shape, it solves the mesh made of it, so a number such as the peak stress belongs to the mesh as much as to the part.  Here the plate has a hole, which makes the
# stress pile up at its edge, and the plate is meshed three ways with mesh_volume, which the analyses take in place of the body (static_analysis(mesh, ...)):
#
#   coarse   tetrahedra 6 mm everywhere                                 few elements; the plate is 6 mm thick, so one of them fits across it
#   uniform  tetrahedra 2 mm everywhere                                 resolved everywhere, and the whole plate pays for it
#   graded   1 mm at the hole growing to 3 mm away from it             the elements are where the stress is: about half as many as the uniform one
#
# The output prints, for each mesh, its elements, the peak von Mises stress and the peak displacement, and the peak stress a textbook gives for a plate with a hole (Peterson's stress
# concentration factor, from the width of the plate and the diameter of the hole).  Look at the displacement first: the coarse mesh has a peak stress that is far off (about 150 % of the textbook's), and a plate
# that bends several times too much -- the mistake a coarse mesh makes without saying so.  The other two agree on the displacement and are close to the textbook on the stress (the peak of
# a mesh of tetrahedra is the stress of one element, which is why the graded mesh, with the smallest elements at the hole, is a little higher than the uniform one).  The result card's
# Elements button shows the tetrahedra that were solved (small at the hole in the graded one).
#
# Try: element_size=3 in `coarse` (the displacement comes right), or element_size=4 (the displacement is right and the peak stress 17 % low), or wall_size=0.6 in `graded` (more elements at the hole, the peak settles), or growth_rate=1.3 (the mesh stays fine
# for longer away from the hole).  Docs: docs/analysis.md ("A mesh of your own", "How far the numbers can be trusted is the mesh's"), docs/meshing.md.
from fieldes import *

view.set_bounds((-45, -25, -10), (45, 25, 10), auto_scale=True)
view.set_resolution(12)                # (the result of an analysis is drawn at this many samples to the mm: 12 draws the plate in a few seconds)
view.set_quality(8)

width, height, thickness, hole_radius, pull = 80.0, 40.0, 6.0, 6.0, 2000.0          # mm, mm, mm, mm, newtons

hole = cylinder(hole_radius, thickness * 3, position=(0, 0, 0))      # (through the plate: taller than it)
plate = difference(box((width, height, thickness), position=(0, 0, 0)), hole)

# the three meshes: the same plate, the same material, the same supports and loads -- only the mesh differs
coarse = mesh_volume(plate, element_size=6.0)
uniform = mesh_volume(plate, element_size=2.0)
graded = mesh_volume(plate, element_size=graded_size(hole, wall_size=1.0, growth_rate=1.5, max_size=3.0))

held = box((2, height + 2, thickness + 2), position=(-width / 2, 0, 0))           # the end that is held
pulled = box((2, height + 2, thickness + 2), position=(width / 2, 0, 0))          # the end that is pulled, along the plate

# what a textbook says: the stress in the narrowest section, times the stress concentration factor of a hole in a plate (Peterson)
ratio = 2 * hole_radius / height
net_stress = pull / ((height - 2 * hole_radius) * thickness)
factor = 3.0 - 3.14 * ratio + 3.667 * ratio ** 2 - 1.527 * ratio ** 3
print('textbook: net section stress %.1f MPa, factor %.2f, peak stress about %.1f MPa' % (net_stress, factor, net_stress * factor))
print()

results = {}
for name, mesh in (('coarse', coarse), ('uniform', uniform), ('graded', graded)):
    results[name] = static_analysis(mesh, supports=[fixed(held)], loads=[force(pulled, (pull, 0, 0))], material=steel)
    peak = float(results[name].max_von_mises)
    print('%-8s %7d elements   peak stress %6.1f MPa (%3.0f %% of the textbook)   peak displacement %.5f mm'
          % (name, results[name].elements, peak, 100 * peak / (net_stress * factor), float(results[name].max_displacement)))
print()
print('the coarse mesh bends the plate %.1f times as much as the uniform one' % (float(results['coarse'].max_displacement) / float(results['uniform'].max_displacement)))

results['graded']                                                                    # the stress on the plate; the Elements button shows the graded mesh that was solved
