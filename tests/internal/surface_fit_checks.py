'''
Checks of the argument surface_fit (H225, the user, 2026-11-15: "The threshold of when elements are removed ... sound like something that should be an argument that the user can choose to have an active choice
between less odd elements or more surface conformity", and, when the first version had three words: "I ASKED FOR AN ARGUEMTN THAT LETS THE FUCKING USER DETERMINE THIS SO WE DONT SET IT ARBITRARILY").  The argument is a
NUMBER from 0 (fewest odd elements) to 1 (most surface conformity), 0.5 by default.  It sets the numbers that decide what the mesher gives up: the poorest tetrahedron and the smallest dihedral angle that a move of a
node onto an edge may leave, how far the skin may stand off the exact surface, and the threshold under which the mender works on a tetrahedron (kernel/src/fea/tetmesh.hpp MeshLimits; python/fieldes/stdlib/sim_mesh.py,
fit_limits).  Library checks, no window.  Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import math
import sys

from fieldes import *
from fieldes.stdlib import sim_mesh

bad = []
MOST_ELEMENTS, DEFAULT, MOST_SURFACE = 0.0, 0.5, 1.0


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

    # --- the argument itself: a number from 0 to 1
    for bad_value in ('nice', 'balanced', 1.5, -0.1, True, [0.5]):
        message = raises(lambda: mesh_tet4(turned, 3.0, surface_fit=bad_value), ValueError)
        check("surface_fit=%r is an error that says it is a number from 0 to 1" % (bad_value,), message is not None and 'number from 0' in message and 'to 1' in message, message)
    limits = sim_mesh.fit_limits
    check("0.5 is what the mesher has always done (0.15, 10 degrees, 0.06 of an element, the mender's threshold 0.6)", limits(0.5) == (0.15, 10.0, 0.06, 0.60), limits(0.5))
    check('the default of the functions is 0.5', sim_mesh.FIT_DEFAULT == 0.5 and sim_mesh.surface_fit_value(None, 'x') == 0.5)
    check('0 and 1 are the two ends of the table of docs/analysis.md', limits(0.0) == (0.15, 10.0, 0.12, 0.70) and limits(1.0) == (0.03, 2.0, 0.02, 0.50), (limits(0.0), limits(1.0)))
    ladder = [limits(t / 20.0) for t in range(21)]
    check('towards 1 no limit gets stricter: the floor, the sliver angle, the skin tolerance and the mender threshold only fall',
          all(ladder[k + 1][j] <= ladder[k][j] + 1e-12 for k in range(20) for j in range(4)), ladder[::5])
    check('a number between two anchors is on the straight line between their limits (0.25 is half way from 0 to 0.5, 0.75 half way from 0.5 to 1)',
          all(abs(limits(0.25)[j] - 0.5 * (limits(0.0)[j] + limits(0.5)[j])) < 1e-12 and abs(limits(0.75)[j] - 0.5 * (limits(0.5)[j] + limits(1.0)[j])) < 1e-12 for j in range(4)))

    # --- the default changes nothing, and what was set for one mesh does not stand for the next
    plain = mesh_tet4(turned, 3.0)
    named = mesh_tet4(turned, 3.0, surface_fit=0.5)
    check('the default and 0.5 give the same mesh (%d tetrahedra, %d nodes)' % (len(plain.cells['tet4']) // 4, plain.node_count),
          len(plain.cells['tet4']) == len(named.cells['tet4']) and plain.node_count == named.node_count and list(plain.nodes) == list(named.nodes))
    results = {}
    for fit in (MOST_SURFACE, MOST_ELEMENTS, 0.75, DEFAULT):
        results[fit] = mesh_tet4(turned, 3.0, surface_fit=fit)
    after = mesh_tet4(turned, 3.0)
    check('a mesh made after the others is made with the default again (the number does not stand)', list(after.nodes) == list(plain.nodes) and len(after.cells['tet4']) == len(plain.cells['tet4']))
    check('the current number is the default outside of a call', sim_mesh.current_surface_fit() == 0.5)

    # --- what the number does to a box turned to a position the lattice does not fit: the edges run slanted across the lattice
    lean = {fit: leaning_count(results[fit], axes) for fit in results}
    worst = {fit: worst_tetrahedron(results[fit]) for fit in results}
    for fit in (MOST_SURFACE, 0.75, DEFAULT, MOST_ELEMENTS):
        print('      %-5s %d of %d faces lean, worst tetrahedron %.3f, %d tetrahedra' % (fit, lean[fit][0], lean[fit][1], worst[fit], len(results[fit].cells['tet4']) // 4))
    check('1 leaves no more leaning faces than the default (%d against %d)' % (lean[MOST_SURFACE][0], lean[DEFAULT][0]), lean[MOST_SURFACE][0] <= lean[DEFAULT][0])
    check('0 leaves no better a worst tetrahedron than the default asks for: %.3f is at least %.3f' % (worst[MOST_ELEMENTS], worst[DEFAULT]), worst[MOST_ELEMENTS] >= worst[DEFAULT] - 1e-9)
    check('no number leaves a sliver below 0.05 (worst %s)' % ', '.join('%.3f' % worst[f] for f in results), min(worst.values()) >= 0.05)
    faces = {fit: lean[fit][1] for fit in lean}
    check('the skin has the fewest triangles at 0 (the mender takes elements out of the skin: %d), the most at 1 (%d), the default between (%d)' % (faces[MOST_ELEMENTS], faces[MOST_SURFACE], faces[DEFAULT]),
          faces[MOST_ELEMENTS] < faces[DEFAULT] <= faces[MOST_SURFACE])
    # a ball has no edge: the number moves the skin and the elements a little, nothing is cut across
    # (meshed whole, symmetry=None: a ball is symmetric about three planes, and an octant of a ball only 2.7 elements along its radius is a coarse half mesh with planes for walls, whose skin the mender takes much more of out
    # at 0 -- 192 triangles against 336 at the default, 2026-11-16 -- than the whole ball's, whose skin does not change at all with the number)
    ball = {fit: mesh_tet4(sphere(8), 3.0, surface_fit=fit, symmetry=None) for fit in (MOST_SURFACE, DEFAULT, MOST_ELEMENTS)}
    ball_faces = {fit: len(boundary(ball[fit])) for fit in ball}
    print('      a ball of radius 8 at 3 mm: %s boundary triangles' % ', '.join('%s %d' % (fit, ball_faces[fit]) for fit in (MOST_SURFACE, DEFAULT, MOST_ELEMENTS)))
    check('a ball (no edge) is meshed with about the same skin whatever the number (within 15 %% of the default)', all(abs(ball_faces[fit] / ball_faces[DEFAULT] - 1.0) < 0.15 for fit in ball_faces), ball_faces)

    # --- an analysis takes it, and the result is not taken from the cache of another number
    beam = box((40, 10, 10), position=(0, 0, 0))
    base = box((4, 14, 14), position=(-18, 0, 0))
    tip = box((4, 14, 14), position=(18, 0, 0))
    solved = {}
    for fit in (MOST_SURFACE, MOST_ELEMENTS, DEFAULT):
        solved[fit] = static_analysis(beam, supports=[fixed(base)], loads=[force(tip, (0, 0, -100.0))], material=steel, element_size=2.5, surface_fit=fit)
    moves = {fit: float(solved[fit].max_displacement) for fit in solved}
    print('      the tip of a beam moves %s' % ', '.join('%s %.5f mm' % (fit, moves[fit]) for fit in (MOST_SURFACE, DEFAULT, MOST_ELEMENTS)))
    check('a static analysis takes surface_fit and the three results agree to 3 %% (the beam is a box: the number only decides the edges)', max(moves.values()) / min(moves.values()) < 1.03, moves)
    check('... each is a result of its own (not the one kept for another number)', solved[MOST_SURFACE] is not solved[MOST_ELEMENTS] and solved[DEFAULT] is not solved[MOST_SURFACE])
    again = static_analysis(beam, supports=[fixed(base)], loads=[force(tip, (0, 0, -100.0))], material=steel, element_size=2.5)
    check('... and the default asked again is the result kept for 0.5', again is solved[DEFAULT])
    mesh = mesh_tet4(beam, 2.5)
    message = raises(lambda: static_analysis(mesh, supports=[fixed(base)], loads=[force(tip, (0, 0, -100.0))], material=steel, surface_fit=0.9), FeaError)
    check('a mesh that is given beside a number other than the default is an error that says why', message is not None and 'surface_fit' in message and 'mesh' in message, message)
    message = raises(lambda: static_analysis(beam, supports=[fixed(base)], loads=[force(tip, (0, 0, -100.0))], material=steel, element_size=2.5, surface_fit='fast'), ValueError)
    check('an analysis refuses a word', message is not None and 'number from 0' in message, message)
    check('the current number is the default after the analyses', sim_mesh.current_surface_fit() == 0.5)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        bad.append('an error')
    print('ALL CHECKS PASSED' if not bad else 'FAILED: ' + '; '.join(bad))
    sys.exit(1 if bad else 0)
