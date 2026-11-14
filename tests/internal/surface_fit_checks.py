'''
Checks of the argument surface_fit (H225, the user, 2026-11-15: "The threshold of when elements are removed ... sound like something that should be an argument that the user can choose to have an active choice
between less odd elements or more surface conformity").  The three values -- 'balanced' (the default), 'surface' (more surface conformity), 'elements' (fewer odd elements) -- set the three numbers that decide what
the mesher gives up: the poorest tetrahedron and the smallest dihedral angle that a move of a node onto an edge may leave, and how far the skin may stand off the exact surface (kernel/src/fea/tetmesh.hpp MeshLimits;
python/fieldes/stdlib/sim_mesh.py).  Library checks, no window.  Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import math
import sys

from fieldes import *
from fieldes.stdlib import sim_mesh

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def boundary(mesh):
    ''' The triangles that only one tetrahedron has, as node triples '''
    T = mesh.cells['tet4']
    count = {}
    for k in range(len(T) // 4):
        t = (T[4 * k], T[4 * k + 1], T[4 * k + 2], T[4 * k + 3])
        for f in ((t[0], t[1], t[2]), (t[0], t[1], t[3]), (t[0], t[2], t[3]), (t[1], t[2], t[3])):
            key = tuple(sorted(f))
            count[key] = count.get(key, 0) + 1
    return [key for key, c in count.items() if c == 1]


def turned_axes(rz, rx):
    ''' The three face normals of a box that is turned by rz degrees about z and then by rx degrees about x '''
    cz, sz, cx, sx = math.cos(math.radians(rz)), math.sin(math.radians(rz)), math.cos(math.radians(rx)), math.sin(math.radians(rx))

    def turn(v):
        x, y, z = v
        x, y = cz * x - sz * y, sz * x + cz * y
        y, z = cx * y - sx * z, sx * y + cx * z
        return (x, y, z)
    return [turn((1.0, 0.0, 0.0)), turn((0.0, 1.0, 0.0)), turn((0.0, 0.0, 1.0))]


def leaning_count(mesh, axes, limit=12.0):
    ''' The boundary triangles of a mesh of a box whose normal is more than `limit` degrees from every one of the box's three face directions, and how many there are '''
    P = mesh.nodes
    cs = math.cos(math.radians(limit))
    lean = total = 0
    for a, b, c in boundary(mesh):
        u = [P[3 * b + i] - P[3 * a + i] for i in range(3)]
        w = [P[3 * c + i] - P[3 * a + i] for i in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        if length <= 0:
            continue
        total += 1
        if max(abs(n[0] * ax[0] + n[1] * ax[1] + n[2] * ax[2]) for ax in axes) / length < cs:
            lean += 1
    return lean, total


def worst_tetrahedron(mesh):
    from fieldes.stdlib import meshing
    P, T = mesh.nodes, mesh.cells['tet4']
    return min(meshing._tet_ratio(P, (T[4 * k], T[4 * k + 1], T[4 * k + 2], T[4 * k + 3])) for k in range(len(T) // 4))


def raises(call, kind):
    try:
        call()
    except kind as error:
        return str(error)
    except Exception as error:
        return None
    return None


def main():
    size = (30, 20, 10)
    turned = rotate(rotate(box(size, position=(0, 0, 0)), axis=(0, 0, 1), angle=17.0), axis=(1, 0, 0), angle=9.0)
    axes = turned_axes(17.0, 9.0)

    # --- the argument itself
    message = raises(lambda: mesh_tet4(turned, 3.0, surface_fit='nice'), ValueError)
    check("a value that is none of the three is an error that names them", message is not None and "'balanced'" in message and "'surface'" in message and "'elements'" in message, message)
    check("the three values are known", set(sim_mesh.SURFACE_FIT) == {'balanced', 'surface', 'elements'})
    check("the default is what the mesher has always done (0.15, 10 degrees, 0.06 of an element, the mender's threshold 0.6)", sim_mesh.SURFACE_FIT['balanced'] == (0.15, 10.0, 0.06, 0.60), sim_mesh.SURFACE_FIT['balanced'])
    fits = sim_mesh.SURFACE_FIT
    check("'surface' is at or under the default in every number (edge limits, skin tolerance, mender threshold) and 'elements' at or over it in the skin tolerance and the mender's threshold",
          all(fits['surface'][k] <= fits['balanced'][k] for k in range(4)) and fits['elements'][2] >= fits['balanced'][2] and fits['elements'][3] > fits['balanced'][3] > fits['surface'][3], fits)

    # --- the default changes nothing, and what was set for one mesh does not stand for the next
    plain = mesh_tet4(turned, 3.0)
    named = mesh_tet4(turned, 3.0, surface_fit='balanced')
    check('the default and \'balanced\' give the same mesh (%d tetrahedra, %d nodes)' % (len(plain.cells['tet4']) // 4, plain.node_count),
          len(plain.cells['tet4']) == len(named.cells['tet4']) and plain.node_count == named.node_count and list(plain.nodes) == list(named.nodes))
    results = {}
    for fit in ('surface', 'elements', 'balanced'):
        results[fit] = mesh_tet4(turned, 3.0, surface_fit=fit)
    after = mesh_tet4(turned, 3.0)
    check('a mesh made after the others is made with the default again (the choice does not stand)', list(after.nodes) == list(plain.nodes) and len(after.cells['tet4']) == len(plain.cells['tet4']))
    check('the current choice is the default outside of a call', sim_mesh.current_surface_fit() == 'balanced')

    # --- what the choice does to a box turned to a position the lattice does not fit: the edges run slanted across the lattice
    lean = {fit: leaning_count(results[fit], axes) for fit in results}
    worst = {fit: worst_tetrahedron(results[fit]) for fit in results}
    for fit in ('surface', 'balanced', 'elements'):
        print('      %-9s %d of %d faces lean, worst tetrahedron %.3f, %d tetrahedra' % (fit, lean[fit][0], lean[fit][1], worst[fit], len(results[fit].cells['tet4']) // 4))
    check("'surface' leaves no more leaning faces than the default (%d against %d)" % (lean['surface'][0], lean['balanced'][0]), lean['surface'][0] <= lean['balanced'][0])
    check("'elements' leaves no better a worst tetrahedron than the default asks for: %.3f is at least %.3f" % (worst['elements'], worst['balanced']), worst['elements'] >= worst['balanced'] - 1e-9)
    check("no value leaves a sliver below 0.05 (worst %.3f, %.3f, %.3f)" % (worst['surface'], worst['balanced'], worst['elements']), min(worst.values()) >= 0.05)
    faces = {fit: lean[fit][1] for fit in lean}
    check("the skin has the fewest triangles with 'elements' (the mender takes elements out of the skin: %d), the most with 'surface' (%d), the default between (%d)" % (faces['elements'], faces['surface'], faces['balanced']),
          faces['elements'] < faces['balanced'] <= faces['surface'])
    # a ball has no edge: the choice moves the skin and the elements a little, nothing is cut across
    ball = {fit: mesh_tet4(sphere(8), 3.0, surface_fit=fit) for fit in ('surface', 'balanced', 'elements')}
    ball_faces = {fit: len(boundary(ball[fit])) for fit in ball}
    print('      a ball of radius 8 at 3 mm: %s boundary triangles' % ', '.join('%s %d' % (fit, ball_faces[fit]) for fit in ('surface', 'balanced', 'elements')))
    check('a ball (no edge) is meshed with about the same skin whatever the choice (within 15 %% of the default)', all(abs(ball_faces[fit] / ball_faces['balanced'] - 1.0) < 0.15 for fit in ball_faces), ball_faces)

    # --- an analysis takes it, and the result is not taken from the cache of another choice
    beam = box((40, 10, 10), position=(0, 0, 0))
    base = box((4, 14, 14), position=(-18, 0, 0))
    tip = box((4, 14, 14), position=(18, 0, 0))
    solved = {}
    for fit in ('surface', 'elements', 'balanced'):
        solved[fit] = static_analysis(beam, supports=[fixed(base)], loads=[force(tip, (0, 0, -100.0))], material=steel, element_size=2.5, surface_fit=fit)
    moves = {fit: float(solved[fit].max_displacement) for fit in solved}
    print('      the tip of a beam moves %s' % ', '.join('%s %.5f mm' % (fit, moves[fit]) for fit in ('surface', 'balanced', 'elements')))
    check('a static analysis takes surface_fit and the three results agree to 3 %% (the beam is a box: the choice only decides the edges)', max(moves.values()) / min(moves.values()) < 1.03, moves)
    check('... each is a result of its own (not the one kept for another choice)', solved['surface'] is not solved['elements'] and solved['balanced'] is not solved['surface'])
    again = static_analysis(beam, supports=[fixed(base)], loads=[force(tip, (0, 0, -100.0))], material=steel, element_size=2.5)
    check('... and the default asked again is the result kept for \'balanced\'', again is solved['balanced'])
    mesh = mesh_tet4(beam, 2.5)
    message = raises(lambda: static_analysis(mesh, supports=[fixed(base)], loads=[force(tip, (0, 0, -100.0))], material=steel, surface_fit='surface'), FeaError)
    check('a mesh that is given beside a choice other than the default is an error that says why', message is not None and 'surface_fit' in message and 'mesh' in message, message)
    message = raises(lambda: static_analysis(beam, supports=[fixed(base)], loads=[force(tip, (0, 0, -100.0))], material=steel, element_size=2.5, surface_fit='fast'), ValueError)
    check('an analysis refuses a value that is none of the three', message is not None and "'balanced'" in message, message)
    check('the current choice is the default after the analyses', sim_mesh.current_surface_fit() == 'balanced')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        bad.append('an error')
    print('ALL CHECKS PASSED' if not bad else 'FAILED: ' + '; '.join(bad))
    sys.exit(1 if bad else 0)
