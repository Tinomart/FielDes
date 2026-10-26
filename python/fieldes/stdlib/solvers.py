'''
Your own solvers: write the equations of a problem in Python, and FielDes meshes the part, runs them, and draws the result.

    from fieldes import *

    plate = box((0, 0, 0), (60, 20, 3))
    cold = box((-1, -1, -1), (1, 21, 4))                    # where the plate is held at 20 degrees
    hot = box((59, -1, -1), (61, 21, 4))                    # and where it is at 80

    result = solve('heat', plate, fixed=[(cold, 20), (hot, 80)], conductivity=0.2)
    result                                                  # displayed: the temperature of the plate, with the elements behind it (the Elements button)
    print(result.max_temperature)

`solve(name, part, element_size=None, **parameters)` meshes `part` with tetrahedra (`mesh_volume`, `element_size` as there; or give it a mesh you made), makes a `Problem` of the
mesh and calls the solver `name` with the problem and the parameters.  The solver gives back the fields it found -- one number at every node of the mesh -- and, if it likes,
some numbers.  The result is displayed like the result of any analysis: coloured by the field, with the mesh behind it.

A solver is a function with a decorator, in a file of the solvers folder (Settings > Solver... opens it, and edits the files):

    from fieldes.stdlib.solvers import solver, Sparse, tet_gradients

    @solver('heat', description='Steady heat conduction',
            equation='div(conductivity * grad(T)) + source = 0')
    def heat(problem, fixed=(), conductivity=1.0, source=0.0):
        K = Sparse(len(problem.nodes))
        load = [0.0] * len(problem.nodes)
        for tet in problem.tets:
            volume, g = tet_gradients(*[problem.nodes[i] for i in tet])
            for a in range(4):
                load[tet[a]] += source * volume / 4.0
                for b in range(4):
                    K.add(tet[a], tet[b], conductivity * volume * (g[a][0] * g[b][0] + g[a][1] * g[b][1] + g[a][2] * g[b][2]))
        values = {}
        for region, value in fixed:
            for i in problem.nodes_in(region):
                values[i] = value
        return {'temperature': K.solve(load, values)}

What a solver is given:

    problem.nodes           [(x, y, z), ...]: every node of the mesh
    problem.tets            [(a, b, c, d), ...]: the nodes of every tetrahedron
    problem.nodes_in(region)    the nodes that lie in a body, on a surface selection or on a field that is at most zero there (`tolerance=` mm beyond it)
    problem.boundary_nodes()    the nodes on the surface of the part
    problem.element_size    about how large an element is, mm
    problem.mesh, problem.body, problem.parameters

The tools it can use (they are plain Python: nothing here needs a library that is not there):

    tet_gradients(p0, p1, p2, p3)       the volume of a tetrahedron and the gradients of its four linear shape functions
    Sparse(n)                           a matrix of n rows: .add(i, j, value), .multiply(x), .solve(b, fixed={node: value}) -- conjugate gradients, with the fixed nodes taken out

What it gives back: a dictionary of fields `{'temperature': [one number for each node]}`, or `(fields, numbers)` with `numbers = {'max_temperature': 80.0}`.  Both are the result's: a field is
`result.fields['temperature']` and `result.temperature`; a number is `result.max_temperature`.  The solver `heat` here is the one the example is written for: copy it, change its
equation -- the conductivity as a function of the temperature, a source that depends on where it is -- and it is yours: `conductivity` and `source` may be functions
`f(T, x, y, z)`, and the temperature is then found by iterating (see below).  The solver `elasticity` is the equations of the static analysis in Python
(`solve('elasticity', part, fixed=[(face, (0, 0, 0))], forces=[(end, (0, 0, -500))], young=210000)`): `young` may be a function of the strain, `gravity` a body force; copy it
to change the law of the material, the loads, or the equations themselves.

It is Python, so it is as slow as Python is: a mesh of a few thousand nodes is a second or two, a hundred thousand is not for this.  The analyses FielDes has built in -- static, modal,
thermal, fluid -- are made in C++ and are for that.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import os
import runpy
import time
import traceback
from array import array

__all__ = ['solve', 'solver_names']

# The solvers by name: {name: {name, function, description, equation, path}}.  The ones of this module have no path; the ones of the solvers folder have the file they are in
_REGISTRY = {}
_FILES = {}                         # (the solvers folder: file -> the time it was last read)


def solver(name, description='', equation=''):
    ''' A decorator: the function is the solver `name` -- `solve(name, part, ...)` runs it.  `description` is a line about it, `equation` the equations it solves, in a line of Python, for
        the list of solvers (Settings > Solver...) '''
    def decorate(fn):
        text = description or ((fn.__doc__ or '').strip().split('\n')[0] if fn.__doc__ else '')
        _REGISTRY[name] = {'name': name, 'function': fn, 'description': text, 'equation': equation, 'path': getattr(fn, '__fieldes_file__', None) or _loading.get('path')}
        fn._solver_name = name
        return fn
    return decorate


_loading = {}                       # (the file that is being read: the solvers it registers are its)


class Problem(object):
    ''' What a solver is given (see the module): the mesh as lists, and the ways to find the nodes of a region '''

    def __init__(self, mesh, body, parameters):
        self.mesh = mesh
        self.body = body
        self.parameters = parameters
        P = mesh.nodes
        self.nodes = [(P[3 * i], P[3 * i + 1], P[3 * i + 2]) for i in range(len(P) // 3)]
        conn = mesh.cells.get('tet4', ())
        self.tets = [tuple(conn[4 * i:4 * i + 4]) for i in range(len(conn) // 4)]
        self.element_size = float(mesh.element_size or 1.0)

    def nodes_in(self, region, tolerance=None):
        ''' The nodes that lie in `region` -- a body, a surface selection, or a field that is at most zero there -- or within `tolerance` mm of it (default a twentieth of an element) '''
        from fieldes.stdlib.meshing import _eval
        tol = 0.05 * self.element_size if tolerance is None else float(tolerance)
        values = _eval(region, self.nodes)
        return [i for i, v in enumerate(values) if v <= tol]

    def boundary_nodes(self):
        ''' The nodes on the surface of the part '''
        found = set()
        for conn in self.mesh.faces.values():
            found.update(conn)
        return sorted(found)

    def per_tet(self, values):
        ''' The mean of a nodal field over each tetrahedron '''
        return [0.25 * (values[a] + values[b] + values[c] + values[d]) for a, b, c, d in self.tets]


def tet_gradients(p0, p1, p2, p3):
    ''' (volume, [g0, g1, g2, g3]) of the tetrahedron with these corners: the gradients of the four linear shape functions (each is 1 at its corner and 0 at the others) '''
    ax, ay, az = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    bx, by, bz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
    cx, cy, cz = p3[0] - p0[0], p3[1] - p0[1], p3[2] - p0[2]
    bcx, bcy, bcz = by * cz - bz * cy, bz * cx - bx * cz, bx * cy - by * cx
    det = ax * bcx + ay * bcy + az * bcz
    if abs(det) < 1e-300:
        raise ValueError('a tetrahedron with no volume')
    g1 = (bcx / det, bcy / det, bcz / det)
    g2 = ((cy * az - cz * ay) / det, (cz * ax - cx * az) / det, (cx * ay - cy * ax) / det)
    g3 = ((ay * bz - az * by) / det, (az * bx - ax * bz) / det, (ax * by - ay * bx) / det)
    g0 = (-(g1[0] + g2[0] + g3[0]), -(g1[1] + g2[1] + g3[1]), -(g1[2] + g2[2] + g3[2]))
    return abs(det) / 6.0, [g0, g1, g2, g3]


class Sparse(object):
    ''' A square matrix of n rows that is mostly zeros: .add(i, j, value) puts a number in (it adds to what is there), .multiply(x) is the matrix times a list, and .solve(b, fixed) finds
        x with A x = b for a symmetric matrix, with some of the x given -- `fixed` is {index: value} -- by conjugate gradients '''

    def __init__(self, n):
        self.n = n
        self.rows = [dict() for _ in range(n)]

    def add(self, i, j, value):
        row = self.rows[i]
        row[j] = row.get(j, 0.0) + value

    def multiply(self, x):
        return [sum(v * x[j] for j, v in row.items()) for row in self.rows]

    def solve(self, b, fixed=None, tolerance=1e-9, iterations=None):
        ''' x with A x = b, and x[i] = value for every i in `fixed`: the unknowns are the others, which conjugate gradients with a diagonal preconditioner finds (the matrix is symmetric
            and positive definite once enough nodes are fixed) '''
        fixed = dict(fixed or {})
        n = self.n
        free = [i for i in range(n) if i not in fixed]
        where = {i: k for k, i in enumerate(free)}
        rows = []
        rhs = []
        diag = []
        for i in free:
            cols = []
            r = b[i]
            d = 0.0
            for j, v in self.rows[i].items():
                if j in fixed:
                    r -= v * fixed[j]
                else:
                    cols.append((where[j], v))
                    if j == i:
                        d = v
            rows.append(cols)
            rhs.append(r)
            diag.append(d if abs(d) > 1e-300 else 1.0)
        m = len(free)
        x = [0.0] * m
        res = list(rhs)
        z = [res[k] / diag[k] for k in range(m)]
        p = list(z)
        rz = sum(res[k] * z[k] for k in range(m))
        norm = math.sqrt(sum(v * v for v in rhs)) or 1.0
        for _ in range(iterations or max(200, 4 * m)):
            if math.sqrt(sum(v * v for v in res)) <= tolerance * norm:
                break
            ap = [sum(v * p[j] for j, v in cols) for cols in rows]
            pap = sum(p[k] * ap[k] for k in range(m))
            if abs(pap) < 1e-300:
                break
            alpha = rz / pap
            for k in range(m):
                x[k] += alpha * p[k]
                res[k] -= alpha * ap[k]
            z = [res[k] / diag[k] for k in range(m)]
            rz_new = sum(res[k] * z[k] for k in range(m))
            beta = rz_new / rz if abs(rz) > 1e-300 else 0.0
            rz = rz_new
            p = [z[k] + beta * p[k] for k in range(m)]
        out = [0.0] * n
        for i, value in fixed.items():
            out[i] = float(value)
        for k, i in enumerate(free):
            out[i] = x[k]
        return out


class SolverResult(object):
    ''' What `solve` gives back: `fields` (a number for each node, by name) and `numbers`, each also an attribute by its name (`result.temperature`, `result.max_temperature`) '''

    def __init__(self, solver_name, problem, fields, numbers, seconds):
        self.solver = solver_name
        self.problem = problem
        self.fields = fields
        self.numbers = numbers
        self.seconds = seconds
        self._shown = None

    def __getattr__(self, name):
        if name.startswith('_') or name in ('fields', 'numbers', 'problem', 'solver', 'seconds'):
            raise AttributeError(name)
        if name in self.fields:
            return self.fields[name]
        if name in self.numbers:
            return self.numbers[name]
        raise AttributeError('the result of %s has no field or number %s: it has %s' % (self.solver, name, ', '.join(sorted(list(self.fields) + list(self.numbers))) or 'none'))

    def __repr__(self):
        parts = ['%s: %.5g .. %.5g' % (name, min(v), max(v)) for name, v in self.fields.items() if v]
        parts += ['%s = %.6g' % (name, v) for name, v in self.numbers.items()]
        return 'solve(%s): %d nodes, %d tetrahedra, %.2f s; %s' % (self.solver, len(self.problem.nodes), len(self.problem.tets), self.seconds, '; '.join(parts))

    def _display(self):
        ''' The part drawn as its elements, coloured by the first field (the others are in the card's list of fields): what FielDes draws for a result '''
        if self._shown is None:
            from fieldes.stdlib import meshing
            channels = []
            for name, values in self.fields.items():
                if len(values) != len(self.problem.nodes):
                    continue
                per = self.problem.per_tet(values)
                lo, hi = min(values), max(values)
                if not hi > lo:
                    hi = lo + 1.0
                channels.append((name, '%s - per element' % name, array('f', per).tobytes(), lo, hi, sum(values) / len(values)))
            self._shown = meshing._display_of(self.problem.mesh, extra=channels)
            # (the values of a solver are in its elements: a smooth surface has nothing of them to show, so the result starts with the elements drawn)
            self._shown._elements_first = True
        return self._shown


def _folder():
    ''' The folder of the solvers that are the user's: FIELDES_SOLVERS, or the one the application keeps '''
    path = os.environ.get('FIELDES_SOLVERS')
    if path:
        return path
    base = os.environ.get('APPDATA') or os.path.join(os.path.expanduser('~'), '.config')
    return os.path.join(base, 'FielDes', 'solvers')


def _load_user():
    ''' Reads the files of the solvers folder that are new or changed since they were read: each registers its solvers (the ones of a file that went are forgotten) '''
    folder = _folder()
    try:
        names = sorted(n for n in os.listdir(folder) if n.endswith('.py') and not n.startswith('_'))
    except OSError:
        names = []
    for gone in [p for p in _FILES if os.path.dirname(p) == folder and os.path.basename(p) not in names]:
        _FILES.pop(gone, None)
        for key in [k for k, e in _REGISTRY.items() if e.get('path') == gone]:
            _REGISTRY.pop(key, None)
    errors = []
    for n in names:
        path = os.path.join(folder, n)
        stamp = os.path.getmtime(path)
        if _FILES.get(path) == stamp:
            continue
        _FILES[path] = stamp
        for key in [k for k, e in _REGISTRY.items() if e.get('path') == path]:
            _REGISTRY.pop(key, None)
        _loading['path'] = path
        try:
            runpy.run_path(path, run_name='fieldes_solver_%s' % n[:-3])
        except Exception:
            errors.append((path, traceback.format_exc()))
        finally:
            _loading.clear()
    return errors


def solver_names():
    ''' The names of the solvers `solve` can run: the ones in this module and the ones of the solvers folder '''
    _load_user()
    return sorted(_REGISTRY)


def solve(name, part, element_size=None, **parameters):
    ''' Runs the solver `name` on `part` (see the module): the part is meshed with tetrahedra -- `element_size` as in mesh_volume, or give `part` as a mesh you made -- and the
        solver is called with the problem and `parameters`.  Returns a SolverResult '''
    errors = _load_user()
    entry = _REGISTRY.get(name)
    if entry is None:
        broken = [p for p, e in errors if os.path.basename(p)[:-3] == name or name in e]
        hint = ' (a file of the solvers folder failed to read: %s)' % broken[0] if broken else ''
        raise ValueError("solve: there is no solver called %r -- the ones there are: %s%s" % (name, ', '.join(sorted(_REGISTRY)) or 'none', hint))
    from fieldes.stdlib import meshing
    if isinstance(part, meshing.Mesh):
        mesh = part
    else:
        mesh = meshing.mesh_volume(part, element_size=element_size)
    if not mesh.cells.get('tet4'):
        raise ValueError('solve: the mesh has no tetrahedra: solvers are given the tetrahedra of a volume mesh')
    problem = Problem(mesh, part if not isinstance(part, meshing.Mesh) else part.body, parameters)
    started = time.time()
    out = entry['function'](problem, **parameters)
    fields, numbers = (out if isinstance(out, tuple) and len(out) == 2 else (out, {}))
    if not isinstance(fields, dict):
        raise ValueError("solve: the solver %r gives back a dictionary of fields {'name': [a number for each node]} (and, if it likes, a second one of numbers)" % name)
    fields = {k: [float(x) for x in v] for k, v in fields.items()}
    for k, v in fields.items():
        if len(v) != len(problem.nodes):
            raise ValueError('solve: the field %s has %d numbers and the mesh %d nodes' % (k, len(v), len(problem.nodes)))
    return SolverResult(name, problem, fields, dict(numbers or {}), time.time() - started)


# --- the solver the example is written for: copy it, and change its equation
@solver('heat', description='Steady heat conduction: the temperature of a part held at temperatures, with a source',
        equation="div(conductivity * grad(T)) + source = 0       # W/mm3, mm, degrees; conductivity and source: a number or a function f(T, x, y, z)")
def heat(problem, fixed=(), conductivity=1.0, source=0.0, iterations=25):
    ''' Steady heat conduction.  fixed=[(region, temperature), ...]: where the temperature is given.  conductivity (W/mm/K) and source (W/mm3): a number, or a function f(T, x, y, z) of the
        temperature and the place -- then the temperature is found by iterating: the conductivity of the last one is the next one's, until the temperature stops changing '''
    nodes, tets = problem.nodes, problem.tets
    given = {}
    for region, value in fixed:
        for i in problem.nodes_in(region):
            given[i] = float(value)
    if not given:
        raise ValueError('heat: fixed=[(region, temperature), ...] says where the temperature is given: without it the part has no temperature to find')
    T = [0.0] * len(nodes)
    for i, v in given.items():
        T[i] = v
    nonlinear = callable(conductivity) or callable(source)
    for _ in range(iterations if nonlinear else 1):
        K = Sparse(len(nodes))
        load = [0.0] * len(nodes)
        for tet in tets:
            corners = [nodes[i] for i in tet]
            volume, g = tet_gradients(*corners)
            mean = sum(T[i] for i in tet) / 4.0
            middle = tuple(sum(c[a] for c in corners) / 4.0 for a in range(3))
            k = conductivity(mean, *middle) if callable(conductivity) else conductivity
            q = source(mean, *middle) if callable(source) else source
            for a in range(4):
                load[tet[a]] += q * volume / 4.0
                for b in range(4):
                    K.add(tet[a], tet[b], k * volume * (g[a][0] * g[b][0] + g[a][1] * g[b][1] + g[a][2] * g[b][2]))
        new = K.solve(load, given)
        change = max(abs(a - b) for a, b in zip(new, T))
        T = new
        if change < 1e-6 * (1.0 + max(abs(v) for v in T)):
            break
    return {'temperature': T}, {'max_temperature': max(T), 'min_temperature': min(T)}


# --- the equations of the static analysis, in Python: copy it, and change them (a law of the material that depends on the strain, a load that depends on the place)
@solver('elasticity', description='Elasticity of a part held and loaded (small strains): the displacement and the stress',
        equation="div(stress) + density * gravity = 0;  stress = lambda * trace(strain) * I + 2 * mu * strain;  strain = sym(grad(u))      # mm, N, MPa; young: a number or a function f(equivalent_strain, x, y, z)")
def elasticity(problem, fixed=(), forces=(), young=210000.0, poisson=0.3, gravity=None, density=7.85e-9, iterations=20):
    ''' Static elasticity with small strains, linear tetrahedra.  fixed=[(region, (ux, uy, uz)), ...]: where the displacement is given (mm; None for a direction that is free, a single
        number for all three).  forces=[(region, (fx, fy, fz)), ...]: the total force (N) on a region, shared by the area of the surface it is on.  young (MPa): a number, or a function
        f(equivalent_strain, x, y, z) -- the stiffness of the material where the strain is that, found by iterating (the secant modulus: the stiffness of the last iteration is the
        next one's).  poisson.  gravity=(gx, gy, gz) in mm/s2 with density in t/mm3 (steel 7.85e-9).  Gives the fields von_mises (MPa), displacement (mm) and ux, uy, uz, and the numbers
        max_von_mises and max_displacement '''
    nodes, tets = problem.nodes, problem.tets
    n = len(nodes)

    def region_nodes(region):
        found = problem.nodes_in(region)
        if not found:
            raise ValueError('elasticity: a region of fixed= or forces= has no node of the mesh in it (a node counts when it is within a twentieth of an element of the region)')
        return found

    given = {}
    for region, value in fixed:
        values = (float(value),) * 3 if isinstance(value, (int, float)) else tuple(value)
        for i in region_nodes(region):
            for c in range(3):
                if values[c] is not None:
                    given[3 * i + c] = float(values[c])
    if not given:
        raise ValueError('elasticity: fixed=[(region, (ux, uy, uz)), ...] says where the part is held: without it the part has no displacement to find')

    load = [0.0] * (3 * n)
    triangles = list(problem.mesh.faces.get('tri3', ()))
    for region, force in forces:
        inside = set(region_nodes(region))
        weight = {}
        for t in range(0, len(triangles), 3):
            a, b, c = triangles[t], triangles[t + 1], triangles[t + 2]
            if a in inside and b in inside and c in inside:
                pa, pb, pc = nodes[a], nodes[b], nodes[c]
                ux, uy, uz = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
                vx, vy, vz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
                area = 0.5 * math.sqrt((uy * vz - uz * vy) ** 2 + (uz * vx - ux * vz) ** 2 + (ux * vy - uy * vx) ** 2)
                for i in (a, b, c):
                    weight[i] = weight.get(i, 0.0) + area / 3.0
        if not weight:
            weight = {i: 1.0 for i in inside}                 # (a region that is no surface of the part: the force is shared equally)
        total = sum(weight.values())
        for i, w in weight.items():
            for c in range(3):
                load[3 * i + c] += force[c] * w / total

    nonlinear = callable(young)
    eq_strain = [0.0] * len(tets)
    u = [0.0] * (3 * n)

    def strains(g, tet):
        ''' The strain of a tetrahedron from the displacements: its six components (xx, yy, zz, xy, yz, zx, the shears as tensor components) and the equivalent strain '''
        du = [[sum(u[3 * tet[a] + i] * g[a][j] for a in range(4)) for j in range(3)] for i in range(3)]
        exx, eyy, ezz = du[0][0], du[1][1], du[2][2]
        exy, eyz, ezx = 0.5 * (du[0][1] + du[1][0]), 0.5 * (du[1][2] + du[2][1]), 0.5 * (du[2][0] + du[0][2])
        mean = (exx + eyy + ezz) / 3.0
        dev = (exx - mean) ** 2 + (eyy - mean) ** 2 + (ezz - mean) ** 2 + 2.0 * (exy ** 2 + eyz ** 2 + ezx ** 2)
        return (exx, eyy, ezz, exy, eyz, ezx), math.sqrt(2.0 / 3.0 * dev)

    for _ in range(iterations if nonlinear else 1):
        K = Sparse(3 * n)
        rhs = list(load)
        for k, tet in enumerate(tets):
            corners = [nodes[i] for i in tet]
            volume, g = tet_gradients(*corners)
            middle = tuple(sum(c[a] for c in corners) / 4.0 for a in range(3))
            E = young(eq_strain[k], *middle) if nonlinear else float(young)
            lam = E * poisson / ((1.0 + poisson) * (1.0 - 2.0 * poisson))
            mu = E / (2.0 * (1.0 + poisson))
            for a in range(4):
                ga = g[a]
                for b in range(4):
                    gb = g[b]
                    dot = ga[0] * gb[0] + ga[1] * gb[1] + ga[2] * gb[2]
                    for i in range(3):
                        for j in range(3):
                            K.add(3 * tet[a] + i, 3 * tet[b] + j, volume * (lam * ga[i] * gb[j] + mu * ga[j] * gb[i] + (mu * dot if i == j else 0.0)))
            if gravity is not None:
                for a in range(4):
                    for i in range(3):
                        rhs[3 * tet[a] + i] += density * gravity[i] * volume / 4.0
        u = K.solve(rhs, given)
        change = 0.0
        for k, tet in enumerate(tets):
            volume, g = tet_gradients(*[nodes[i] for i in tet])
            eq = strains(g, tet)[1]
            change = max(change, abs(eq - eq_strain[k]))
            eq_strain[k] = eq
        if nonlinear and change < 1e-8 + 1e-6 * max(eq_strain):
            break

    # the stress of every tetrahedron, and at the nodes the mean of the tetrahedra around it (weighted by their volume)
    node_vm = [0.0] * n
    node_volume = [0.0] * n
    for k, tet in enumerate(tets):
        corners = [nodes[i] for i in tet]
        volume, g = tet_gradients(*corners)
        e, eq = strains(g, tet)
        middle = tuple(sum(c[a] for c in corners) / 4.0 for a in range(3))
        E = young(eq, *middle) if nonlinear else float(young)
        lam = E * poisson / ((1.0 + poisson) * (1.0 - 2.0 * poisson))
        mu = E / (2.0 * (1.0 + poisson))
        trace = e[0] + e[1] + e[2]
        sxx, syy, szz = lam * trace + 2.0 * mu * e[0], lam * trace + 2.0 * mu * e[1], lam * trace + 2.0 * mu * e[2]
        sxy, syz, szx = 2.0 * mu * e[3], 2.0 * mu * e[4], 2.0 * mu * e[5]
        vm = math.sqrt(0.5 * ((sxx - syy) ** 2 + (syy - szz) ** 2 + (szz - sxx) ** 2) + 3.0 * (sxy ** 2 + syz ** 2 + szx ** 2))
        for i in tet:
            node_vm[i] += vm * volume
            node_volume[i] += volume
    von_mises = [node_vm[i] / node_volume[i] if node_volume[i] > 0 else 0.0 for i in range(n)]
    ux, uy, uz = u[0::3], u[1::3], u[2::3]
    displacement = [math.sqrt(ux[i] ** 2 + uy[i] ** 2 + uz[i] ** 2) for i in range(n)]
    return ({'von_mises': von_mises, 'displacement': displacement, 'ux': ux, 'uy': uy, 'uz': uz},
            {'max_von_mises': max(von_mises), 'max_displacement': max(displacement)})
