'''
parametric_optimization: the best numbers for the var()s of a model, over an analysis.

    from fieldes import *

    width = var(20)
    height = var(12)
    beam = box((0, 0, 0), (100, width, height))
    wall = fixed(box((-1, -1, -1), (1, 25, 25)))
    pull = force(box((99, -1, -1), (101, 25, 25)), (0, 0, -200))
    stress = static_analysis(beam, supports=[wall], loads=[pull], material=aluminium)

    best = parametric_optimization(stress, [(width, 8, 30), (height, 6, 24)], minimize='mass',
                                   constraints=['max_von_mises <= 90', 'max_displacement <= 0.5'])

The analysis is solved again with each set of numbers the search tries -- the model is made again from the numbers, the mesh, the
solve, every measure of the result -- and the numbers that give the best design stay in the var()s: the model, and everything after
the call, is the best design.  The search needs no derivatives: it samples the ranges, then closes in on the best design with a
search over surrogates of the objective and of every constraint that puts a design that meets the constraints before one that does not.  It tries a few tens of designs; every
one is an ordinary analysis that is kept by its content, so a run again (or another search over the same designs) finds what it
tried in the cache.  Everything it tried is listed in the result.

What it can be asked for -- `minimize=` or `maximize=` and the `constraints=` -- is a measure of the analysis' result: the name of
one of its numbers ('mass', 'max_von_mises', 'max_displacement', 'compliance', 'safety_factor', 'volume', 'max_temperature',
'pressure_drop', 'frequencies[0]' ...), an expression of them ('mass + 0.01 * compliance'), or a function of the result
(`lambda r: r.max_von_mises / r.mass`).  A constraint is a comparison of two measures, in a text ('max_von_mises <= 90',
'frequencies[0] >= 80'), or a function of the result that gives how much it is broken (0 or less when it is kept).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import math
import random
import struct
import sys
import time

from fieldes.ffi import lib
from fieldes.i18n import tr as _tr
from fieldes.shape import Shape
from fieldes.stdlib.fea import FeaError, FieldResult

__all__ = ['parametric_optimization', 'ParametricResult']


class ParametricError(FeaError):
    ''' The optimisation cannot be made as asked '''


def _f32(x):
    ''' The number a var() holds: a 32-bit float, so that what is tried and what is written into the script are the same number '''
    return struct.unpack('f', struct.pack('f', float(x)))[0]


def _host():
    try:
        import _fieldes_host as host
    except ImportError:
        raise ParametricError('parametric_optimization changes the numbers of the var()s of the script that runs: it works inside FielDes') from None
    if getattr(host, '__retune', None) is None or getattr(host, '__vars', None) is None:
        raise ParametricError('this FielDes cannot change a var() while a script runs')
    return host


def _entry_of(host, var):
    for e in (getattr(host, '__vars', None) or []):
        if e[0] is var:
            return e
    return None


def _set_var(host, var, value):
    getattr(host, '__retune')(var, float(value))


# ---------------------------------------------------------------------------
# Measures: what is asked of the result of an analysis

_FUNCTIONS = {'abs': abs, 'min': min, 'max': max, 'sqrt': math.sqrt, 'log': math.log, 'exp': math.exp}
_COMPARISONS = {ast.Lt: '<', ast.LtE: '<=', ast.Gt: '>', ast.GtE: '>='}


def _number_of(value, text):
    if isinstance(value, bool):
        return float(value)
    if isinstance(value, (int, float)):
        return float(value)
    try:
        return float(value)
    except (TypeError, ValueError):
        raise ParametricError("'{}' is not a number of the result ({})".format(text, type(value).__name__)) from None


def _evaluator(node, text):
    ''' A function of the result for an expression of its numbers: names are the result's attributes, `[i]` indexes a list, the
        arithmetic is Python's, and abs, min, max, sqrt, log, exp are there '''
    def ev(n, result):
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, float)):
            return float(n.value)
        if isinstance(n, ast.Name):
            if n.id in ('pi',):
                return math.pi
            try:
                return getattr(result, n.id)
            except AttributeError:
                raise ParametricError(_available(result, n.id, text)) from None
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.USub, ast.UAdd)):
            v = _number_of(ev(n.operand, result), text)
            return -v if isinstance(n.op, ast.USub) else v
        if isinstance(n, ast.BinOp):
            a, b = _number_of(ev(n.left, result), text), _number_of(ev(n.right, result), text)
            op = n.op
            if isinstance(op, ast.Add):
                return a + b
            if isinstance(op, ast.Sub):
                return a - b
            if isinstance(op, ast.Mult):
                return a * b
            if isinstance(op, ast.Div):
                return a / b if b else math.inf
            if isinstance(op, ast.Pow):
                return a ** b
        if isinstance(n, ast.Subscript):
            base = ev(n.value, result)
            index = n.slice
            if isinstance(index, ast.Constant) and isinstance(index.value, int):
                try:
                    return base[index.value]
                except (IndexError, TypeError, KeyError):
                    raise ParametricError("'{}': the result has no entry {} of that".format(text, index.value)) from None
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id in _FUNCTIONS and not n.keywords:
            return _FUNCTIONS[n.func.id](*[_number_of(ev(a, result), text) for a in n.args])
        raise ParametricError("'{}': only the numbers of the result, numbers, + - * / **, [index], abs, min, max, sqrt, log and exp can be written there".format(text))
    return lambda result: _number_of(ev(node, result), text)


def _available(result, name, text):
    names = []
    for k in dir(result):
        if k.startswith('_'):
            continue
        try:
            raw = getattr(type(result), k, None)
            if callable(raw) and not isinstance(raw, property):
                continue
            v = getattr(result, k)
        except Exception:
            continue
        if isinstance(v, (int, float)) and not isinstance(v, bool) or (isinstance(v, (list, tuple)) and v and all(isinstance(x, (int, float)) for x in v)):
            names.append(k)
    return "'{}': the result of {} has no number '{}' -- it has: {}".format(
        text, type(result).__name__, name, ', '.join(sorted(names)) or '(none)')


def _measure(spec, what):
    ''' (a function of the result that gives the number, the text that says it) of a measure given as an expression or a function '''
    if callable(spec):
        return (lambda result: _number_of(spec(result), what)), getattr(spec, '__name__', what)
    if not isinstance(spec, str):
        raise ParametricError('{}: a measure is the name of a number of the result, an expression of them, or a function of the result '
                              '(not {!r})'.format(what, spec))
    try:
        tree = ast.parse(spec.strip(), mode='eval')
    except SyntaxError:
        raise ParametricError("{}: '{}' is not an expression".format(what, spec)) from None
    return _evaluator(tree.body, spec), spec.strip()


class _Constraint:
    ''' A limit the best design has to keep: `violation(result)` is 0 when it is kept and how far it is broken, in the size of the limit, when it is not '''

    def __init__(self, spec):
        self.text = None
        if callable(spec):
            self.text = getattr(spec, '__name__', 'constraint')
            self._left = lambda result: _number_of(spec(result), self.text)
            self._right = None
            self._op = '<='
            return
        if not isinstance(spec, str):
            raise ParametricError('constraints=: each is a comparison in a text (\'max_von_mises <= 90\') or a function of the result that is 0 or less '
                                  'when it is kept (not {!r})'.format(spec))
        try:
            tree = ast.parse(spec.strip(), mode='eval').body
        except SyntaxError:
            raise ParametricError("constraints=: '{}' is not a comparison".format(spec)) from None
        if not (isinstance(tree, ast.Compare) and len(tree.ops) == 1 and type(tree.ops[0]) in _COMPARISONS):
            raise ParametricError("constraints=: '{}' is not a comparison of two measures with <, <=, > or >=".format(spec))
        self.text = spec.strip()
        self._op = _COMPARISONS[type(tree.ops[0])]
        self._left = _evaluator(tree.left, spec)
        self._right = _evaluator(tree.comparators[0], spec)

    def value(self, result):
        ''' The left side of the comparison (what is measured) '''
        return self._left(result)

    def gap(self, result):
        ''' How far the limit is from being broken, in the size of the limit: below 0 it is kept with room to spare, above 0 it is broken '''
        left = self._left(result)
        if self._right is None:
            return left
        right = self._right(result)
        scale = abs(right) if right else 1.0
        return ((left - right) if self._op in ('<', '<=') else (right - left)) / scale


# ---------------------------------------------------------------------------
# The search

_FEASIBLE = 1e-6            # (a design that breaks a limit by less than this, of the limit, keeps it)


def _lhs(n, d, rng):
    ''' n points of the unit cube, one in each n-th of every axis, in an order that is the same every time '''
    columns = []
    for _ in range(d):
        order = list(range(n))
        rng.shuffle(order)
        columns.append([(k + 0.5) / n for k in order])
    return [tuple(columns[a][i] for a in range(d)) for i in range(n)]


def _rbf_fit(points, columns):
    ''' Cubic radial basis functions with a linear tail through the numbers of `columns` at `points`: the weights of each column.  (A little
        smoothing on the diagonal: what a mesh gives is not noise-free.)  Raises ZeroDivisionError when the points do not hold a fit '''
    n, d = len(points), len(points[0])
    size = n + d + 1
    m = len(columns)
    a = [[0.0] * (size + m) for _ in range(size)]
    for i in range(n):
        pi = points[i]
        row = a[i]
        for j in range(i):
            r = math.sqrt(sum((x - y) ** 2 for x, y in zip(pi, points[j])))
            row[j] = a[j][i] = r * r * r
        row[i] = 1e-6
        row[n] = a[n][i] = 1.0
        for k in range(d):
            row[n + 1 + k] = a[n + 1 + k][i] = pi[k]
        for c in range(m):
            row[size + c] = columns[c][i]
    for col in range(size):
        pivot = max(range(col, size), key=lambda r: abs(a[r][col]))
        if abs(a[pivot][col]) < 1e-12:
            raise ZeroDivisionError('singular')
        a[col], a[pivot] = a[pivot], a[col]
        inv = 1.0 / a[col][col]
        prow = a[col]
        for r in range(col + 1, size):
            f = a[r][col] * inv
            if f:
                row = a[r]
                for k in range(col, size + m):
                    row[k] -= f * prow[k]
    out = []
    for c in range(m):
        w = [0.0] * size
        for r in range(size - 1, -1, -1):
            s = a[r][size + c] - sum(a[r][k] * w[k] for k in range(r + 1, size))
            w[r] = s / a[r][r]
        out.append(w)
    return out


class _Search:
    ''' The search: a design of experiments over the ranges, then -- design after design -- a surrogate of the objective and of every constraint
        (radial basis functions through everything tried), many candidate designs near the best one and spread over the ranges, and the one
        the surrogates say is feasible and good and not too near what was tried is solved for real, which improves the surrogates.  The size
        of the neighbourhood grows with success and shrinks with failure, so that it closes in on a design on a bound or on a limit '''

    def __init__(self, variables, evaluate, tolerance, budget, progress):
        self.vars = variables
        self.d = len(variables)
        self.evaluate = evaluate            # (the numbers) -> a record
        self.tol = tolerance
        self.budget = budget
        self.progress = progress
        self.records = []
        self._seen = {}
        self.rng = random.Random(20261014 + self.d)
        self.rho = 0.25
        self.round = 0

    def point(self, u):
        ''' The numbers of a point of the unit cube, as the var()s hold them '''
        return tuple(_f32(v.low + min(1.0, max(0.0, c)) * (v.high - v.low)) for v, c in zip(self.vars, u))

    def unit(self, x):
        return tuple((c - v.low) / (v.high - v.low) for v, c in zip(self.vars, x))

    def run_at(self, u):
        x = self.point(u)
        if x in self._seen:
            return self._seen[x]
        if len(self.records) >= self.budget:
            return None
        rec = self.evaluate(x)
        rec['u'] = self.unit(x)
        self._seen[x] = rec
        self.records.append(rec)
        self.progress(len(self.records), self.budget, rec)
        return rec

    @staticmethod
    def rank(rec):
        ''' The order of designs: one that meets every limit before one that does not, a smaller objective first (a smaller break first, for those that do not) '''
        if rec['error']:
            return (2, 0.0, 0.0)
        if rec['violation'] <= _FEASIBLE:
            return (0, 0.0, rec['cost'])
        return (1, rec['violation'], rec['cost'])

    def best(self):
        return min(self.records, key=self.rank)

    def start(self, u0):
        n0 = max(self.d + 2, min(self.budget // 3, 3 * self.d + 3))
        points = [tuple(min(1.0, max(0.0, c)) for c in u0)] + _lhs(n0 - 1, self.d, self.rng)
        for u in points:
            if self.run_at(u) is None:
                break

    def candidates(self, best):
        d, rng = self.d, self.rng
        out = []
        count = 120 * d + 200
        keep = 1.0 if d <= 3 else max(1.0 / d, 0.4)
        for _ in range(count // 2):                            # (near the best design)
            u = list(best['u'])
            moved = False
            for k in range(d):
                if rng.random() < keep:
                    u[k] += rng.gauss(0.0, self.rho)
                    moved = True
            if not moved:
                k = rng.randrange(d)
                u[k] += rng.gauss(0.0, self.rho)
            out.append(tuple(min(1.0, max(0.0, c)) for c in u))
        for _ in range(count - count // 2):                    # (anywhere)
            out.append(tuple(rng.random() for _ in range(d)))
        for k in range(d):                                     # (the best design with one variable on a bound)
            for edge in (0.0, 1.0):
                u = list(best['u'])
                u[k] = edge
                out.append(tuple(u))
        return out

    def next_point(self):
        usable = [r for r in self.records if not r['error']]
        failed = [r['u'] for r in self.records if r['error']]
        if not usable:
            return tuple(self.rng.random() for _ in range(self.d))
        best = min(usable, key=self.rank)
        cands = [c for c in self.candidates(best) if self.point(c) not in self._seen]
        if not cands:
            return None
        points = [r['u'] for r in usable]
        m = len(usable[0]['gaps'])
        costs = [r['cost'] for r in usable]
        lo, hi = min(costs), max(costs)
        columns = [[(c - lo) / (hi - lo) if hi > lo else 0.0 for c in costs]] + [[r['gaps'][j] for r in usable] for j in range(m)]
        try:
            weights = _rbf_fit(points, columns)
        except ZeroDivisionError:
            weights = None
        n, d = len(points), self.d
        everything = points + failed
        scored = []
        for c in cands:
            dmin = min(math.sqrt(sum((a - b) ** 2 for a, b in zip(c, p))) for p in everything)
            if dmin < 1e-3 or (failed and min(math.sqrt(sum((a - b) ** 2 for a, b in zip(c, p))) for p in failed) < 0.02):
                continue
            if weights is None:
                scored.append((c, 0.0, [], dmin))
                continue
            radii = [math.sqrt(sum((a - b) ** 2 for a, b in zip(c, p))) ** 3 for p in points]
            preds = []
            for w in weights:
                preds.append(sum(w[i] * radii[i] for i in range(n)) + w[n] + sum(w[n + 1 + k] * c[k] for k in range(d)))
            scored.append((c, preds[0], preds[1:], dmin))
        if not scored:
            return None
        feasible = [s for s in scored if all(g <= 0.0 for g in s[2])]
        w = (0.95, 0.8, 0.95, 0.5)[self.round % 4]
        self.round += 1
        if not feasible:                                       # (no design is thought to keep the limits: the one that is thought to break them least)
            return min(scored, key=lambda s: (max(s[2]) if s[2] else 0.0, -s[3]))[0]
        fmin, fmax = min(s[1] for s in feasible), max(s[1] for s in feasible)
        dmin_, dmax_ = min(s[3] for s in feasible), max(s[3] for s in feasible)

        def score(s):
            vr = (s[1] - fmin) / (fmax - fmin) if fmax > fmin else 0.0
            vd = (dmax_ - s[3]) / (dmax_ - dmin_) if dmax_ > dmin_ else 0.0
            return w * vr + (1.0 - w) * vd
        return min(feasible, key=score)[0]

    def run(self, u0):
        self.start(u0)
        failures = 0
        while len(self.records) < self.budget:
            before = self.rank(self.best())
            u = self.next_point()
            if u is None:
                break
            rec = self.run_at(u)
            if rec is None:
                break
            after = self.rank(self.best())
            # (an improvement is one that matters: more than the tolerance of what the objective has been seen to range over)
            costs = [r['cost'] for r in self.records if not r['error']]
            span = (max(costs) - min(costs)) if costs else 0.0
            improved = after < before and (after[0] != before[0] or after[1] < before[1] - self.tol or
                                           (after[1] == before[1] and before[2] - after[2] > self.tol * span))
            if improved:
                failures = 0
                if after[0] != before[0] or self.records[-1] is self.best():
                    self.rho = min(0.4, self.rho * 1.5)
            else:
                failures += 1
                if failures % 3 == 0:
                    self.rho = max(self.tol / 2.0, self.rho * 0.5)
            if self.rho <= self.tol and failures >= 4 + self.d:
                break
        return self.best()


class _Variable:
    def __init__(self, shape, low, high, name, start):
        self.shape, self.low, self.high, self.name, self.start = shape, float(low), float(high), name, float(start)


def _script_names(shapes):
    ''' The names the script gives its var()s: the variables of the top-level statements `width = var(20)` '''
    f = sys._getframe(2)
    while f is not None:
        g = f.f_globals
        if '__libfive_counted__' in g:
            found = []
            for s in shapes:
                names = [k for k, v in g.items() if v is s and not k.startswith('_')]
                found.append(names[0] if names else None)
            return found
        f = f.f_back
    return [None] * len(shapes)


def _variables(spec, host):
    items = [spec] if (isinstance(spec, (tuple, list)) and len(spec) == 3 and isinstance(spec[0], Shape)) else spec
    if not isinstance(items, (list, tuple)) or not items:
        raise ParametricError('parametric_optimization: variables is a list of (var, low, high) -- the var() that is changed, and the range it is searched in')
    out = []
    shapes = []
    for item in items:
        if not (isinstance(item, (list, tuple)) and len(item) == 3):
            raise ParametricError('parametric_optimization: each variable is (var, low, high), the var() that is changed and the range it is searched in '
                                  '-- got {!r}'.format(item))
        shape, low, high = item
        if not isinstance(shape, Shape) or not lib.libfive_tree_is_var(shape.ptr):
            raise ParametricError('parametric_optimization: {!r} is not a var() of the script -- write the number it changes as var(...) and give '
                                  'the variable'.format(shape))
        if any(shape is s for s in shapes):
            raise ParametricError('parametric_optimization: a var() is given twice')
        entry = _entry_of(host, shape)
        if entry is None:
            raise ParametricError('parametric_optimization: that var() is not one of this script (a var() made inside a function cannot be changed from here)')
        low, high = float(low), float(high)
        if not high > low:
            raise ParametricError('parametric_optimization: the range of a variable is (low, high) with low below high -- got ({:g}, {:g})'.format(low, high))
        shapes.append(shape)
        out.append(_Variable(shape, low, high, None, float(entry[1])))
    for v, name in zip(out, _script_names(shapes)):
        v.name = name
    for k, v in enumerate(out):
        if v.name is None:
            v.name = 'x%d' % (k + 1)
    return out


def _recipe_of(analysis):
    recipe = getattr(analysis, '_recipe', None) if hasattr(analysis, '__dict__') else None
    if recipe is None:
        raise ParametricError('parametric_optimization is given the result of an analysis made in this script (static_analysis, modal_analysis, thermal_analysis, '
                              'fluid_analysis ...) -- {} is not one'.format(type(analysis).__name__))
    return recipe


def _text(v):
    if v is None:
        return '-'
    if abs(v) >= 1e5 or (v != 0 and abs(v) < 1e-3):
        return '%.4g' % v
    return '%.5g' % v


class ParametricResult(FieldResult):
    ''' What parametric_optimization found.  It is the analysis of the best design (its fields, its numbers: `best.max_von_mises`, `best.von_mises`)
        and what the search did:

            values       the best numbers, {name of the var: number}
            objective    the best design's value of what was minimised or maximised
            feasible     whether the best design keeps every constraint (when no design does, it is the one that breaks them the least)
            history      every design tried, in order: {values, objective, violation, feasible, measures, seconds, error}
            result       the analysis of the best design
            evaluations  how many designs were tried
            apply()      writes the best numbers into the var()s of the script (apply=True in the call does it at the run) '''

    def __init__(self, result, variables, values, objective, goal, constraints, history, seconds, feasible, depends):
        self.result = result
        self._variables = variables
        self.values = values
        self.objective = objective
        self.goal = goal
        self._constraints = constraints
        self.history = history
        self.seconds = seconds
        self.feasible = feasible
        self.evaluations = len(history)
        self._depends = depends

    # (what the best analysis is, the result is: its numbers, its fields)
    def __getattr__(self, name):
        if name.startswith('_') or self.__dict__.get('_forwarding'):
            raise AttributeError(name)
        self.__dict__['_forwarding'] = True
        try:
            return getattr(self.result, name)
        finally:
            self.__dict__['_forwarding'] = False

    def _as_field(self):
        return self.result._as_field()

    @property
    def fields(self):
        return self.result.fields

    @property
    def _engineering_domain(self):
        from fieldes.kinds import domain_of_value
        return domain_of_value(self.result)

    def _display(self):
        out = self.result._display()
        try:
            rows = [(_tr('best %s') % self.goal[1], _text(self.objective), '')]
            rows += [(v.name, _text(self.values[v.name]), '') for v in self._variables[:4]]
            rows.append((_tr('designs tried'), '%d' % self.evaluations, ''))
            rows += list(getattr(out, '_key_results', None) or [])[:3]
            out._key_results = rows
        except Exception:
            pass
        return out

    def apply(self):
        ''' Writes the best numbers into the var()s of the script, in the text, when the run is over '''
        host = _host()
        resync = getattr(host, '__resync', None)
        if resync is None:
            raise ParametricError('this FielDes cannot write numbers into the script')
        from fieldes.stdlib.handles import _float32_text
        for v in self._variables:
            entry = _entry_of(host, v.shape)
            if entry is None:
                continue
            line, end_line, col, end_col = entry[2]
            resync.append((line - 1, col, end_line - 1, end_col - 1, _float32_text(self.values[v.name])))
        return self

    def table(self):
        ''' Every design tried, as text '''
        measures = [c.text for c in self._constraints]
        head = ['#'] + [v.name for v in self._variables] + [self.goal[1]] + measures + ['']
        rows = [head]
        best = self.history.index(self._best_record) if getattr(self, '_best_record', None) in self.history else -1
        for k, h in enumerate(self.history):
            marks = '<- best' if k == best else ('failed: ' + h['error'] if h['error'] else ('' if h['feasible'] else 'breaks a limit'))
            rows.append([str(k + 1)] + [_text(x) for x in h['values']] + [_text(h['objective'])] + [_text(m) for m in h['measures']] + [marks])
        widths = [max(len(r[i]) for r in rows) for i in range(len(head))]
        return '\n'.join('  '.join(c.rjust(w) if i < len(head) - 1 else c for i, (c, w) in enumerate(zip(r, widths))).rstrip() for r in rows)

    def __repr__(self):
        goal, what = self.goal
        head = (_tr('Parametric optimization: minimize %s over %d variable(s), %d designs tried (%.1f s)') if goal == 'minimize' else
                _tr('Parametric optimization: maximize %s over %d variable(s), %d designs tried (%.1f s)'))
        lines = [head % (what, len(self._variables), self.evaluations, self.seconds)]
        best = ', '.join('%s = %s' % (v.name, _text(self.values[v.name])) for v in self._variables)
        if self.feasible:
            lines.append(_tr('  best design: %s  ->  %s = %s') % (best, what, _text(self.objective)))
        else:
            lines.append(_tr('  NO design keeps every constraint; the one that breaks them least: %s  ->  %s = %s') % (best, what, _text(self.objective)))
        for c, v in zip(self._constraints, self._best_measures):
            lines.append(_tr('    %s   (measured %s)') % (c.text, _text(v)))
        if not self._depends:
            lines.append(_tr('  note: none of the numbers the search looked at changed with the variables -- is the analysis made of them?'))
        lines.append(self.table())
        return '\n'.join(lines)


def parametric_optimization(analysis, variables, minimize=None, maximize=None, constraints=(), max_evaluations=None, tolerance=0.002, apply=False):
    ''' The best numbers for the var()s of a model, over an analysis: the designs the search tries are solved with `analysis` again, each with
        other numbers for the variables, and the numbers of the best one stay in the var()s.

        analysis      the result of an analysis of this script (static_analysis, modal_analysis, thermal_analysis, fluid_analysis ...), made of the
                      var()s
        variables     what is changed: (var, low, high) -- the var() and the range it is searched in -- or a list of them
        minimize      what is made as small as it can be: a measure of the result -- the name of one of its numbers ('mass', 'max_von_mises', ...),
        maximize      or what is made as large (give one of the two)       an expression of them, or a function of the result
        constraints   what the best design keeps: a list of comparisons in a text ('max_von_mises <= 90', 'frequencies[0] >= 80'), or functions of the
                      result that are 0 or less when kept
        max_evaluations  how many designs it tries at most (default 20 + 10 for each variable)
        tolerance     it stops when the designs it is looking at are this close, of the ranges (default 0.2 %)
        apply         True writes the best numbers into the var()s of the text when the run is over (otherwise they hold for this run only)

        Returns a ParametricResult: the analysis of the best design, `.values`, `.objective`, `.feasible`, `.history`.  When no design keeps the
        constraints it says so and is the one that breaks them least. '''
    t0 = time.time()
    recipe = _recipe_of(analysis)
    host = _host()
    if (minimize is None) == (maximize is None):
        raise ParametricError('parametric_optimization: say what is made best -- minimize=<measure> or maximize=<measure> (one of them)')
    if isinstance(constraints, (str,)) or callable(constraints):
        constraints = [constraints]
    goal_word = 'minimize' if minimize is not None else 'maximize'
    sign = 1.0 if minimize is not None else -1.0
    objective_fn, objective_text = _measure(minimize if minimize is not None else maximize, goal_word)
    limits = [_Constraint(c) for c in (constraints or ())]
    vars_ = _variables(variables, host)
    d = len(vars_)
    budget = int(max_evaluations) if max_evaluations else 20 + 10 * d
    if budget < d + 2:
        raise ParametricError('parametric_optimization: max_evaluations must be at least %d for %d variable(s)' % (d + 2, d))
    original = [v.start for v in vars_]
    fn, args, kwargs = recipe
    state = {'best_cost': None}

    def solve():
        return fn(*args, **kwargs)

    def evaluate(x):
        for v, value in zip(vars_, x):
            _set_var(host, v.shape, value)
        started = time.time()
        rec = {'values': list(x), 'objective': None, 'cost': math.inf, 'violation': math.inf, 'feasible': False, 'measures': [], 'gaps': [], 'seconds': 0.0, 'error': None}
        try:
            result = solve()
        except ParametricError:
            raise
        except FeaError as e:                                  # (a design the mesh cannot hold, a part that fell apart: a design that failed, said in the list)
            if str(e).endswith('cancelled'):
                raise
            rec['error'] = str(e).strip().split('\n')[0][:120] or type(e).__name__
            rec['measures'] = [None] * len(limits)
            rec['seconds'] = time.time() - started
            return rec
        # (a measure that cannot be read is a mistake in the call, not in a design: it is said at once)
        value = objective_fn(result)
        measures = [c.value(result) for c in limits]
        gaps = [c.gap(result) for c in limits]
        violation = max([max(0.0, g) for g in gaps], default=0.0)
        if math.isfinite(value) and math.isfinite(violation) and all(math.isfinite(g) for g in gaps):
            rec.update(objective=value, cost=sign * value, violation=violation, feasible=violation <= _FEASIBLE, measures=measures, gaps=gaps)
        else:
            rec['error'] = 'a measure is not a number'
            rec['measures'] = [None] * len(limits)
        rec['seconds'] = time.time() - started
        return rec

    from fieldes import run_progress

    def progress(done, total, rec):
        run_progress.report(min(1.0, done / float(total)), _tr('design %d of %d') % (done, total))

    search = _Search(vars_, evaluate, float(tolerance), budget, progress)
    try:
        with run_progress.task(_tr('parametric optimization')):
            start = tuple((v.start - v.low) / (v.high - v.low) for v in vars_)
            best = search.run(start)
    except BaseException:
        for v, value in zip(vars_, original):             # (a run that is stopped leaves the numbers as they were)
            _set_var(host, v.shape, value)
        raise
    values = {v.name: best['values'][k] for k, v in enumerate(vars_)}
    for v, value in zip(vars_, best['values']):
        _set_var(host, v.shape, value)
    if best['error']:
        for v, value in zip(vars_, original):
            _set_var(host, v.shape, value)
        raise ParametricError('parametric_optimization: no design could be analysed -- the first failed with: ' + best['error'])
    result = solve()
    varied = {round(r['objective'], 9) for r in search.records if r['objective'] is not None}
    depends = len(varied) > 1 or any(len({round(r['measures'][k], 9) for r in search.records if r['measures'] and r['measures'][k] is not None}) > 1 for k in range(len(limits)))
    out = ParametricResult(result, vars_, values, best['objective'], (goal_word, objective_text), limits, search.records, time.time() - t0,
                           best['feasible'], depends)
    out._best_record = best
    out._best_measures = best['measures']
    if apply:
        out.apply()
    return out


# It is given the result of an analysis (a simulation), and writes `variables=` and `minimize=` as placeholders: the ranges and what is made best are the user's
from fieldes.kinds import takes as _declare
parametric_optimization._takes = _declare({'simulation'}, [{'simulation'}])
parametric_optimization._required = ('variables', 'minimize')
