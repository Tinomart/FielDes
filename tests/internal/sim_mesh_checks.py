'''
Checks of a mesh as the part of an analysis (the user, 2026-11-03: "The user should be able to provide a mesh (result of the meshing operations) as an input for simulations. Right
now the meshing that FielDes uses for Simulations is very one dimensional and since there are already these meshing tools, why not give the user more freedom?").  The static, the
modal and the thermal analysis are given the result of mesh_volume in place of the body; the answers are compared with those of the same body meshed by the analysis itself, and the
refusals (a size beside a mesh, hexahedra, a surface mesh) are read.  Library checks, no window.  Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import sys
import traceback

from fieldes import *

bad = []


def shown(x):
    ''' A result's numbers are fields that answer float(): print them as numbers '''
    if isinstance(x, (int, float, str)):
        return x
    try:
        return float(x)
    except Exception:
        return x


def check(label, ok, extra=''):
    extra = tuple(shown(x) for x in extra) if isinstance(extra, tuple) else shown(extra)
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def close(a, b, share):
    a, b = float(a), float(b)
    return abs(a - b) <= share * max(abs(a), abs(b), 1e-12)


def refused(call, word):
    try:
        call()
    except Exception as e:
        return word in str(e), str(e)[:160]
    return False, 'no error'


def main():
    beam = box((40, 10, 10), position=(0, 0, 0))                  # x from -20 to 20
    check('a box is placed by its centre (the checks below rest on it)', evaluate(beam, [(0, 0, 0)])[0] < 0 and evaluate(beam, [(19, 0, 0)])[0] < 0 and evaluate(beam, [(21, 0, 0)])[0] > 0)
    base = box((4, 14, 14), position=(-20, 0, 0))                 # holds the end at x = -20
    tip = box((4, 14, 14), position=(20, 0, 0))                   # loads the end at x = 20
    support, push = fixed(base), force(tip, (0, 0, -100))

    # --- static: the mesh of the analysis itself, against the same mesh given
    own = static_analysis(beam, [support], [push], steel, element_size=2.5)
    mesh = mesh_volume(beam, 2.5)
    given = static_analysis(mesh, [support], [push], steel)
    check('static_analysis solves on a mesh', float(given.max_displacement) > 0, 'tip movement %.5g mm on %d tetrahedra' % (given.max_displacement, given.elements))
    check('... on the tetrahedra the mesh has (none added, none dropped)', given.elements == mesh.cell_count('tet4'), (given.elements, mesh.cell_count('tet4')))
    check('... and gets the movement of the body meshed by the analysis itself, within 8 %', close(given.max_displacement, own.max_displacement, 0.08), (given.max_displacement, own.max_displacement))
    check('... the element size of the result is the mesh\'s own (about 2.5 mm)', 1.5 < given.element_size < 3.5, given.element_size)
    again = static_analysis(mesh, [support], [push], steel)
    check('... asked again it is the same answer', float(again.max_displacement) == float(given.max_displacement))

    # --- a graded mesh: fine at the surface, coarse inside
    graded = mesh_volume(beam, graded_size(beam, wall_size=1.5, growth_rate=1.3, max_size=5.0))
    on_graded = static_analysis(graded, [support], [push], steel)
    check('a graded mesh is solved as it is', on_graded.elements == graded.cell_count('tet4') and on_graded.elements != given.elements, (on_graded.elements, given.elements))
    check('... and reads the tip movement within 12 % of the uniform mesh', close(on_graded.max_displacement, given.max_displacement, 0.12), (on_graded.max_displacement, given.max_displacement))

    # --- a 10-node mesh: its corners are taken, and the quadratic element is made on them
    ten = mesh_volume(beam, 3.0, element='tet10')
    on_ten = static_analysis(ten, [support], [push], steel)
    check('a 10-node mesh gives its corners to the linear element', on_ten.elements == ten.cell_count('tet10'), (on_ten.elements, ten.cell_count('tet10')))
    quadratic = static_analysis(ten, [support], [push], steel, element='tet10')
    check("... and element='tet10' makes the quadratic element on them (stiffer in bending than the linear one: a smaller tip movement)",
          0 < float(quadratic.max_displacement) and float(quadratic.max_displacement) >= float(on_ten.max_displacement) * 0.98, (quadratic.max_displacement, on_ten.max_displacement))

    # --- the refusals
    ok, text = refused(lambda: static_analysis(mesh, [support], [push], steel, element_size=2.0), 'element_size')
    check('element_size beside a mesh is an error that says so', ok, text)
    hexes = mesh_volume(beam, 4.0, element='hex')
    ok, text = refused(lambda: static_analysis(hexes, [support], [push], steel), 'hexahedra')
    check('a mesh of hexahedra is refused, with the reason', ok, text)
    ok, text = refused(lambda: static_analysis(mesh_surface(beam, 3.0), [support], [push], steel), 'no cells')
    check('a surface mesh is refused, with the reason', ok, text)
    ok, text = refused(lambda: static_analysis(mesh, [support], [push], steel, element='hex'), 'voxel')
    check("element='hex' takes no mesh", ok, text)

    # --- modal
    modes_own = modal_analysis(beam, [support], steel, modes=2, element_size=2.5)
    modes_given = modal_analysis(mesh, [support], steel, modes=2)
    check('modal_analysis takes a mesh and finds the first frequency of the body within 8 %', close(modes_given.frequencies[0], modes_own.frequencies[0], 0.08), (modes_given.frequencies[0], modes_own.frequencies[0]))

    # --- thermal
    warm_own = thermal_analysis(beam, [fixed_temperature(base, 20)], [heat_input(tip, 5.0)], [], [], aluminium, element_size=2.5)
    warm_given = thermal_analysis(mesh, [fixed_temperature(base, 20)], [heat_input(tip, 5.0)], [], [], aluminium)
    check('thermal_analysis takes a mesh and finds the hottest place of the body within 8 %', close(warm_given.max_temperature, warm_own.max_temperature, 0.08) and float(warm_given.max_temperature) > 20,
          (warm_given.max_temperature, warm_own.max_temperature))


def fluid():
    # the flow of example 16 (water past a post in a slab), solved on the mesh of the fluid and on the mesh the analysis makes itself
    W, H, T = 80.0, 40.0, 4.0
    post = cylinder(5.0, T + 2, position=(24.0, 20.0, -1.0))
    slab = box((W, H, T), position=(W / 2, H / 2, T / 2))
    faces = union(box((W + 2, H + 2, 1.01), position=(W / 2, H / 2, -0.495)), box((W + 2, H + 2, 1.01), position=(W / 2, H / 2, T + 0.495)))
    sides = union(box((W + 2, 1.01, T + 2), position=(W / 2, -0.495, T / 2)), box((W + 2, 1.01, T + 2), position=(W / 2, H + 0.495, T / 2)))
    inlet_face = box((1.01, H + 2, T + 2), position=(-0.495, H / 2, T / 2))
    outlet_face = box((1.01, H + 2, T + 2), position=(W + 0.495, H / 2, T / 2))
    items = dict(inlets=[inlet(inlet_face, speed=4.0)], outlets=[outlet(outlet_face, pressure=0)], boundaries=[slip(faces), slip(sides)], fluid=water)
    own = fluid_analysis(post, slab, element_size=2.0, **items)
    mesh = mesh_volume(difference(slab, post), 2.0)
    given = fluid_analysis(post, mesh, **items)
    check('fluid_analysis takes the mesh of the fluid as its domain and solves on its tetrahedra', given.elements == mesh.cell_count('tet4'), (given.elements, mesh.cell_count('tet4')))
    # (a flow at 2 mm past a post of 5 mm radius is sensitive to the mesh: with empty caches the same fluid gave 6.8e-7 on the analysis's own mesh, 7.8e-7 on a mesh_volume mesh whose edges were cut
    # and 9.2e-7 on one whose edges are square: the two meshings of mesh_volume differ by 15 % and the analysis's own mesh is 26 % under the second, so 15 % against it was luck; the check is that the
    # drag is the same flow's, of the same size)
    check('... and gets the drag on the post within 35 % of the same flow meshed by the analysis itself', close(given.wall_force[0], own.wall_force[0], 0.35) and float(given.wall_force[0]) != 0.0,
          (given.wall_force[0], own.wall_force[0]))


def example_39():
    # examples/39_mesh_controls_the_simulation.py: a plate with a hole meshed coarse, uniform and graded, the same static analysis on each (H106)
    import os
    import runpy
    path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'examples', '39_mesh_controls_the_simulation.py')
    ns = runpy.run_path(path)
    r = ns['results']
    textbook = ns['net_stress'] * ns['factor']
    check('example 39: the three meshes are solved, each on the tetrahedra of its own mesh', all(r[k].elements == ns[k].cell_count('tet4') for k in ('coarse', 'uniform', 'graded')),
          tuple(r[k].elements for k in ('coarse', 'uniform', 'graded')))
    check('... the coarse mesh has fewer elements than the graded one, which has fewer than the uniform one', r['coarse'].elements < r['graded'].elements < r['uniform'].elements,
          tuple(r[k].elements for k in ('coarse', 'graded', 'uniform')))
    check('... the coarse mesh bends the plate more than 3 times as much as the uniform one (too few elements across the thickness)', float(r['coarse'].max_displacement) > 3 * float(r['uniform'].max_displacement),
          (r['coarse'].max_displacement, r['uniform'].max_displacement))
    check('... the graded mesh bends it like the uniform one, within 20 %', close(r['graded'].max_displacement, r['uniform'].max_displacement, 0.2), (r['graded'].max_displacement, r['uniform'].max_displacement))
    check('... the uniform mesh finds the textbook peak stress within 15 %', close(r['uniform'].max_von_mises, textbook, 0.15), (r['uniform'].max_von_mises, textbook))
    check('... the graded mesh finds it within 25 %', close(r['graded'].max_von_mises, textbook, 0.25), (r['graded'].max_von_mises, textbook))


try:
    main()
    fluid()
    example_39()
except Exception:
    traceback.print_exc()
    bad.append('raised')
print('=' * 70)
print('%d check(s) failed %s' % (len(bad), bad) if bad else 'ALL CHECKS PASSED')
sys.exit(1 if bad else 0)
