'''
Checks of the solvers of your own (python/fieldes/stdlib/solvers.py, Settings > Solver...): the solvers of the library against answers that are known exactly, the
solvers folder (a file that is added, changed, broken), and what solve() refuses.

    heat          a bar held at two temperatures: the straight line (linear conductivity), and the line of Kirchhoff's transform when the conductivity grows with the temperature
    elasticity    a bar pulled to a given strain (the stress is E times the strain, in every node), pulled by a force (the stress is the force over the area), hanging by its weight
                  (the end moves by rho g L^2 / 2E), and with a stiffness that falls with the strain (the secant modulus, found by iterating)

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/solver_checks.py
It prints PASS or FAIL for every check, and exits with 1 when one failed (a few seconds to a minute).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import json
import math
import os
import shutil
import sys
import tempfile
import time
import traceback

folder = tempfile.mkdtemp(prefix='solver_checks_')
os.environ['FIELDES_SOLVERS'] = folder              # (the solvers folder of this run: the user's own is not touched)

from fieldes import *
from fieldes import app_support
from fieldes.stdlib import solvers

failures = []
t_all = time.time()


def check(label, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + detail) if detail else ''))
    sys.stdout.flush()
    if not ok:
        failures.append(label)


def near(value, exact, tolerance):
    return abs(value - exact) <= tolerance * max(abs(exact), 1e-12)


def run(label, fn):
    try:
        return fn()
    except Exception:
        check(label + ' (no exception)', False, traceback.format_exc(limit=4).replace('\n', ' | '))
        return None


L, W = 40.0, 10.0
bar = box((0, 0, 0), (L, W, W))
end0 = box((-1, -1, -1), (0, W + 1, W + 1))                 # the face x = 0
endL = box((L, -1, -1), (L + 1, W + 1, W + 1))              # the face x = L
corner_a = box((-1, -1, -1), (0.1, 0.1, 0.1))               # the edge points that stop the bar from moving as a whole
corner_b = box((-1, W - 0.1, -1), (0.1, W + 1, 0.1))
corner_c = box((-1, -1, W - 0.1), (0.1, 0.1, W + 1))
pins = [(corner_a, (None, 0.0, 0.0)), (corner_b, (None, None, 0.0)), (corner_c, (None, 0.0, None))]

print('--- the solvers there are')
names = solvers.solver_names()
check('heat and elasticity are in the library', 'heat' in names and 'elasticity' in names, str(names))

print('--- heat')
t0 = time.time()
r = run('heat, a bar held at 20 and 80', lambda: solve('heat', bar, element_size=5, fixed=[(end0, 20.0), (endL, 80.0)], conductivity=0.2))
if r is not None:
    err = max(abs(T - (20.0 + 60.0 * p[0] / L)) for T, p in zip(r.temperature, r.problem.nodes))
    # (the nodes the temperature is given on are those within a twentieth of an element of the end faces, and they lie on the faces to the precision of the mesh: a node 1e-4 mm off its
    # face is held at the temperature of the face, and the line at its own place is 1.5 K/mm * 1e-4 mm away.  So the answer is the line to that, and no closer)
    off = max([abs(p[0]) for T, p in zip(r.temperature, r.problem.nodes) if T == 20.0] + [abs(p[0] - L) for T, p in zip(r.temperature, r.problem.nodes) if T == 80.0])
    bound = 60.0 / L * off + 1e-6
    check('the temperature of the bar is the straight line', err <= bound, 'largest difference %.2e K; the held nodes lie up to %.2e mm off their faces, which allows %.2e K; %d nodes, %.1f s' % (
        err, off, bound, len(r.problem.nodes), time.time() - t0))
    check('and its numbers are the extremes', near(r.max_temperature, 80.0, 1e-9) and near(r.min_temperature, 20.0, 1e-9), '%r %r' % (r.max_temperature, r.min_temperature))

beta, k0 = 0.01, 0.2
t0 = time.time()
r = run('heat, a conductivity that grows with the temperature', lambda: solve('heat', bar, element_size=5, fixed=[(end0, 0.0), (endL, 100.0)],
                                                                                conductivity=lambda T, x, y, z: k0 * (1.0 + beta * T)))
if r is not None:
    a, b = 0.0, 100.0 + beta * 100.0 ** 2 / 2.0               # Kirchhoff: T + beta T^2 / 2 is a straight line of x

    def exact(x):
        theta = a + (b - a) * x / L
        return (-1.0 + math.sqrt(1.0 + 2.0 * beta * theta)) / beta

    err = max(abs(T - exact(p[0])) for T, p in zip(r.temperature, r.problem.nodes))
    middle = [T for T, p in zip(r.temperature, r.problem.nodes) if abs(p[0] - L / 2) < 0.5]
    check('the temperature follows the line of Kirchhoff\'s transform', err < 1.0 and middle and abs(sum(middle) / len(middle) - exact(L / 2)) < 1.0,
          'largest difference %.3f K (the straight line would be 8.1 K off in the middle), %.1f s' % (err, time.time() - t0))

print('--- elasticity')
E, nu = 100000.0, 0.3
strain = 0.01
t0 = time.time()
r = run('elasticity, a bar pulled to a strain', lambda: solve('elasticity', bar, element_size=5,
                                                              fixed=[(end0, (0.0, None, None)), (endL, (strain * L, None, None))] + pins, young=E, poisson=nu))
if r is not None:
    check('the stress is E times the strain', near(r.max_von_mises, E * strain, 0.005), '%.2f MPa against %.2f, %.1f s' % (r.max_von_mises, E * strain, time.time() - t0))
    lowest = min(r.von_mises)
    check('in every node', near(lowest, E * strain, 0.01), 'the lowest node %.2f MPa' % lowest)
    side = [uy for uy, p in zip(r.uy, r.problem.nodes) if p[1] > W - 0.1 and abs(p[0] - L) < 0.1]
    check('and the bar draws in at its sides by Poisson\'s ratio', side and near(sum(side) / len(side), -nu * strain * W, 0.02), 'uy at y = %g: %.5f mm against %.5f' % (W, sum(side) / len(side), -nu * strain * W))

force = 10000.0
r = run('elasticity, a bar pulled by a force', lambda: solve('elasticity', bar, element_size=5, fixed=[(end0, (0.0, None, None))] + pins,
                                                             forces=[(endL, (force, 0.0, 0.0))], young=E, poisson=nu))
if r is not None:
    sigma = force / (W * W)
    check('the stress is the force over the area', near(r.max_von_mises, sigma, 0.01), '%.2f MPa against %.2f' % (r.max_von_mises, sigma))
    check('the end moves by sigma L / E', near(max(r.ux), sigma * L / E, 0.01), '%.5f mm against %.5f' % (max(r.ux), sigma * L / E))

rho, g = 1e-6, 9810.0
r = run('elasticity, a bar hanging by its weight', lambda: solve('elasticity', bar, element_size=5, fixed=[(end0, (0.0, None, None))] + pins,
                                                                 young=E, poisson=nu, gravity=(g, 0.0, 0.0), density=rho))
if r is not None:
    exact_u = rho * g * L ** 2 / (2.0 * E)
    check('the end moves by rho g L^2 / 2E', near(max(r.ux), exact_u, 0.03), '%.4e mm against %.4e' % (max(r.ux), exact_u))

t0 = time.time()
law = lambda eq, x, y, z: 100000.0 / (1.0 + 50.0 * eq)
r = run('elasticity, a stiffness that falls with the strain', lambda: solve('elasticity', bar, element_size=5,
                                                                              fixed=[(end0, (0.0, None, None)), (endL, (strain * L, None, None))] + pins, young=law, poisson=nu))
if r is not None:
    eq = 2.0 * strain * (1.0 + nu) / 3.0                       # (the equivalent strain of a bar pulled with its sides free)
    exact_s = law(eq, 0, 0, 0) * strain
    check('the stress is the secant modulus times the strain', near(r.max_von_mises, exact_s, 0.01), '%.2f MPa against %.2f (the linear law would give %.0f), %.1f s' % (r.max_von_mises, exact_s, E * strain, time.time() - t0))

print('--- the solvers folder')
copy_source = app_support.solvers_template(json.dumps({'name': 'soft_steel', 'copy_of': 'elasticity'}))
check('the copy of a library solver is a file of its own, under the name asked', "@solver('soft_steel'" in copy_source and 'def soft_steel(' in copy_source and 'young=210000.0' in copy_source)
text = run('a copy of the elasticity solver', lambda: json.loads(app_support.solvers_save(json.dumps(
    {'folder': folder, 'file': 'soft_steel.py', 'source': copy_source.replace('young=210000.0', 'young=50000.0'), 'default': copy_source}))))
if text is not None:
    check('is saved and read', text.get('ok') is True and text.get('names') == ['soft_steel'], str(text))
    r2 = run('solve with the copy', lambda: solve('soft_steel', bar, element_size=5, fixed=[(end0, (0.0, None, None)), (endL, (strain * L, None, None))] + pins))
    if r2 is not None:
        check('the copy has the stiffness that was written into it', near(r2.max_von_mises, 50000.0 * strain, 0.005), '%.2f MPa against %.2f' % (r2.max_von_mises, 50000.0 * strain))
    listed = json.loads(app_support.solvers_list(json.dumps({'folder': folder})))
    mine = [e for e in listed if e['name'] == 'soft_steel']
    check('and is listed as editable, with its equation', mine and mine[0]['editable'] and 'div(stress)' in mine[0]['equation'], str(mine[:1]))
    check('and the text it started with is kept for Reset to default', mine and mine[0]['default'] == copy_source and mine[0]['source'] != copy_source)

source = '''from fieldes.stdlib.solvers import solver, Sparse, tet_gradients


@solver('poisson', description='Poisson equation', equation='-laplace(u) = f')
def poisson(problem, fixed=(), f=1.0):
    K = Sparse(len(problem.nodes))
    load = [0.0] * len(problem.nodes)
    for tet in problem.tets:
        volume, g = tet_gradients(*[problem.nodes[i] for i in tet])
        for a in range(4):
            load[tet[a]] += f * volume / 4.0
            for b in range(4):
                K.add(tet[a], tet[b], volume * (g[a][0] * g[b][0] + g[a][1] * g[b][1] + g[a][2] * g[b][2]))
    values = {}
    for region, value in fixed:
        for i in problem.nodes_in(region):
            values[i] = value
    u = K.solve(load, values)
    return {'u': u}, {'max_u': max(u)}
'''
saved = run('a solver written from nothing', lambda: json.loads(app_support.solvers_save(json.dumps({'folder': folder, 'file': 'poisson.py', 'source': source}))))
if saved is not None:
    check('is saved and read', saved.get('ok') is True and saved.get('names') == ['poisson'], str(saved))
    r3 = run('solve with it', lambda: solve('poisson', bar, element_size=5, fixed=[(end0, 0.0), (endL, 0.0)], f=1.0))
    if r3 is not None:
        exact_max = 1.0 * L ** 2 / 8.0                         # u'' = -1 with u = 0 at both ends: the middle is L^2 / 8 (the sides are insulated)
        check('and finds the parabola of the equation', near(r3.max_u, exact_max, 0.02), '%.3f against %.3f' % (r3.max_u, exact_max))

broken = run('a file that does not read', lambda: json.loads(app_support.solvers_save(json.dumps({'folder': folder, 'file': 'broken.py', 'source': 'def oops(:\n'}))))
if broken is not None:
    check('is refused, with the line', broken.get('ok') is False and 'line' in (broken.get('error') or '').lower(), str(broken)[:200])
    check('and is not left in the folder', not os.path.exists(os.path.join(folder, 'broken.py')))

# a file that changes is read again (its time is a few seconds later)
path = os.path.join(folder, 'soft_steel.py')
changed = open(path, encoding='utf-8').read().replace('young=50000.0', 'young=25000.0')
open(path, 'w', encoding='utf-8').write(changed)
os.utime(path, (time.time() + 5, time.time() + 5))
r4 = run('solve with the copy after the file was changed', lambda: solve('soft_steel', bar, element_size=5, fixed=[(end0, (0.0, None, None)), (endL, (strain * L, None, None))] + pins))
if r4 is not None:
    check('a file that was changed is read again', near(r4.max_von_mises, 25000.0 * strain, 0.005), '%.2f MPa against %.2f' % (r4.max_von_mises, 25000.0 * strain))
os.remove(path)
check('and a file that was taken away is forgotten', 'soft_steel' not in solvers.solver_names())

print('--- the way back: Reset to default and Previous version')
way = os.path.join(folder, 'way_back.py')
first = copy_source.replace('soft_steel', 'way_back')
second = first.replace('young=210000.0', 'young=1000.0')
third = second.replace('young=1000.0', 'young=2.0')


def save_solver(file, source, **more):
    request = {'folder': folder, 'file': file, 'source': source}
    request.update(more)
    return json.loads(app_support.solvers_save(json.dumps(request)))


made = run('a solver is made with the text it starts from', lambda: save_solver('way_back.py', first, default=first))
check('and a solver that is new has no previous version', made is not None and made.get('ok') is True and not os.path.exists(way + '.bak'))
run('it is changed and saved', lambda: save_solver('way_back.py', second))
run('and changed and saved again (a default that is given later is not taken)', lambda: save_solver('way_back.py', third, default='not the default'))
listing = json.loads(app_support.solvers_list(json.dumps({'folder': folder})))
entry = [e for e in listing if e['name'] == 'way_back']
check('the list gives the text it started with, the one before the last save and the one it has now',
      bool(entry) and entry[0]['default'] == first and entry[0]['backup'] == second and entry[0]['source'] == third)
back = run('the default is saved again', lambda: save_solver('way_back.py', first))
os.utime(way, (time.time() + 7, time.time() + 7))
check('Reset to default: the file is the text it started with again, and what it had is the previous version',
      back is not None and back.get('ok') is True and open(way, encoding='utf-8').read() == first and open(way + '.bak', encoding='utf-8').read() == third)
solvers.solver_names()
check('and the solver has the stiffness of the library again', solvers._REGISTRY['way_back']['function'].__defaults__ is not None and 210000.0 in solvers._REGISTRY['way_back']['function'].__defaults__)
by_hand = os.path.join(folder, 'by_hand.py')
open(by_hand, 'w', encoding='utf-8').write(first.replace('way_back', 'by_hand'))
entry2 = [e for e in json.loads(app_support.solvers_list(json.dumps({'folder': folder}))) if e['name'] == 'by_hand']
check('a file put into the folder by hand has no default and no previous version', bool(entry2) and entry2[0]['default'] == '' and entry2[0]['backup'] == '' and entry2[0]['editable'])
os.remove(by_hand)
gone = json.loads(app_support.solvers_delete(json.dumps({'path': way})))
check('deleting a solver takes what was kept beside it', gone.get('ok') is True and not any(os.path.exists(way + s) for s in ('', '.default', '.bak')))

print('--- what is refused')
try:
    solve('no_such_solver', bar)
    check('an unknown solver is refused', False)
except ValueError as e:
    check('an unknown solver is refused, with the ones there are', 'no_such_solver' in str(e) and 'heat' in str(e), str(e)[:160])
try:
    solve('heat', bar, element_size=5)
    check('heat without a place where the temperature is given is refused', False)
except ValueError as e:
    check('heat without a place where the temperature is given is refused', 'fixed' in str(e), str(e)[:160])
try:
    solve('elasticity', bar, element_size=5, fixed=[(box((100, 100, 100), (101, 101, 101)), (0.0, 0.0, 0.0))])
    check('a region with no node in it is refused', False)
except ValueError as e:
    check('a region with no node in it is refused', 'no node' in str(e), str(e)[:160])


@solvers.solver('returns_a_number')
def _bad(problem):
    return 3.0


try:
    solve('returns_a_number', bar, element_size=5)
    check('a solver that gives back no fields is refused', False)
except ValueError as e:
    check('a solver that gives back no fields is refused', 'dictionary of fields' in str(e), str(e)[:160])

shutil.rmtree(folder, ignore_errors=True)
print('--- done in {:.1f} s'.format(time.time() - t_all))
if failures:
    print('FAILED: ' + ', '.join(failures))
    sys.exit(1)
print('ALL CHECKS PASS')
