'''
Measures: what a topology optimization is asked to make best.

    best = topology_optimization(stress, conditions=[minimize(mass_1), keep_below(max_von_mises_1, 90), keep_below(max_displacement_1, 0.5)])
    best = topology_optimization([stress, vibration], conditions=[minimize(mass_1), keep_below(max_von_mises_1, 90), keep_above(frequency_1, 80)])

What is asked -- each item of `conditions=` -- is an optimization condition (analysis_results.py) made from results of the analyses: one the design keeps (keep_below, keep_above,
keep_between, or a comparison written whole, optimization_condition(max_von_mises_1 <= 90)) or a goal: minimize(x) makes x as small as it can be, maximize(x) as large, x a result
or a formula of them (minimize(mass_1 + 0.01 * compliance_1)).  At most one goal; without one the design only has to keep the conditions.  The optimizer reads a condition through
the text it carries ('max_von_mises <= 90', 'mass + 0.01 * compliance'): a formula of the numbers of the analyses' results ('mass', 'compliance', 'max_von_mises', 'frequencies[0]' ...).

The formula is not evaluated on results: it is read as a tree of the design's MEASURES -- the volume it uses, the compliance, the peak
stress or displacement, a natural frequency, the displacement at a point -- and the optimizer is given that tree.  The kernel
calculates every measure with its sensitivity to the density of every element, and differentiates the tree itself, so ANY formula of the
measures can be optimized.  The same tree, read on the analyses of the optimized part, gives the numbers the result reports.

With several analyses (a list: load cases, or a stress case and a vibration case) a number that more than one offers ('compliance' of
two load cases) counts in the objective as their SUM, and a condition is kept in the analysis its results are of.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import math
import re

from fieldes.stdlib.fea import FeaError
from fieldes.stdlib import analysis_results as _blocks

__all__ = []

# The operations of the kernel's program (kernel/include/libfive/fea/topopt.hpp, Program::Code)
CONST, BASE, ADD, SUB, MUL, DIV, POW, NEG, ABS, MIN, MAX, SQRT, LOG, EXP, END = range(15)
# The kinds of measure (TetProblem::TopOpt::Kind)
VOLUME, COMPLIANCE, MAX_DISPLACEMENT, MAX_VON_MISES, FREQUENCY, DISPLACEMENT_AT = range(6)


class MeasureError(FeaError):
    ''' What is asked of the optimisation cannot be read '''


class _Missing(MeasureError):
    ''' The analysis it is read in does not have this number '''


_NOT_A_NUMBER = ('a measure of the design is not a number while the optimisation is being set up: write what is asked as a text, or as arithmetic '
                 '(+ - * / **, abs) on the numbers of the result -- sqrt, log, exp, min and max can be written in a text')


# ---------------------------------------------------------------------------
# The tree

class Node:
    __slots__ = ()

    def __add__(self, o):
        return _make(ADD, self, o)

    def __radd__(self, o):
        return _make(ADD, o, self)

    def __sub__(self, o):
        return _make(SUB, self, o)

    def __rsub__(self, o):
        return _make(SUB, o, self)

    def __mul__(self, o):
        return _make(MUL, self, o)

    def __rmul__(self, o):
        return _make(MUL, o, self)

    def __truediv__(self, o):
        return _make(DIV, self, o)

    def __rtruediv__(self, o):
        return _make(DIV, o, self)

    def __pow__(self, o):
        return _make(POW, self, o)

    def __rpow__(self, o):
        return _make(POW, o, self)

    def __neg__(self):
        return _make(NEG, self)

    def __pos__(self):
        return self

    def __abs__(self):
        return _make(ABS, self)

    def __float__(self):
        raise MeasureError(_NOT_A_NUMBER)

    __int__ = __float__
    __index__ = __float__

    def _compare(self, other):
        raise MeasureError('a comparison of measures is made of results of an analysis: keep_below(max_von_mises_1, 90), or optimization_condition(max_von_mises_1 <= 90)')

    __lt__ = __le__ = __gt__ = __ge__ = _compare


class Const(Node):
    __slots__ = ('value',)

    def __init__(self, value):
        self.value = float(value)


class Leaf(Node):
    ''' A measure of the design: ('fraction',), ('compliance', case), ('max_von_mises', case), ('max_displacement', case), ('frequency', case, mode),
        ('displacement_at', case, component, x, y, z) '''
    __slots__ = ('key',)

    def __init__(self, key):
        self.key = tuple(key)


class Op(Node):
    __slots__ = ('code', 'args')

    def __init__(self, code, args):
        self.code, self.args = code, tuple(args)


def _wrap(v):
    if isinstance(v, Node):
        return v
    if isinstance(v, (int, float)):
        return Const(float(v))
    raise MeasureError('a measure is combined with a number, not with %r' % (v,))


def _apply(code, a):
    try:
        if code == ADD:
            return a[0] + a[1]
        if code == SUB:
            return a[0] - a[1]
        if code == MUL:
            return a[0] * a[1]
        if code == DIV:
            return a[0] / a[1] if a[1] else math.inf
        if code == POW:
            return a[0] ** a[1]
        if code == NEG:
            return -a[0]
        if code == ABS:
            return abs(a[0])
        if code == MIN:
            return min(a[0], a[1])
        if code == MAX:
            return max(a[0], a[1])
        if code == SQRT:
            return math.sqrt(a[0])
        if code == LOG:
            return math.log(a[0])
        if code == EXP:
            return math.exp(a[0])
    except (ValueError, OverflowError, ZeroDivisionError):
        return math.nan
    raise MeasureError('an operation that is not known')


def _make(code, *args):
    args = [_wrap(a) for a in args]
    if all(isinstance(a, Const) for a in args):
        return Const(_apply(code, [a.value for a in args]))
    return Op(code, args)


def evaluate(node, leaf_value):
    ''' The number a tree is, given the number of each measure (`leaf_value(key)`) '''
    if isinstance(node, Const):
        return node.value
    if isinstance(node, Leaf):
        return leaf_value(node.key)
    return _apply(node.code, [evaluate(a, leaf_value) for a in node.args])


def leaves(node, out=None):
    ''' The measures a tree reads, in the order they are first read '''
    out = [] if out is None else out
    if isinstance(node, Leaf):
        if node.key not in out:
            out.append(node.key)
    elif isinstance(node, Op):
        for a in node.args:
            leaves(a, out)
    return out


# ---------------------------------------------------------------------------
# What the numbers of an analysis are

class Seq:
    ''' A list of numbers of the result ('frequencies', 'reaction'): read with [i] '''

    def __init__(self, get, name):
        self._get, self._name = get, name

    def __getitem__(self, i):
        if not isinstance(i, int):
            raise MeasureError("'%s' is read with a whole number: %s[0]" % (self._name, self._name))
        return self._get(i)


_POINT_FIELDS = {'ux': 0, 'uy': 1, 'uz': 2, 'displacement': 3, 'temperature': 4}
_FUNCTIONS = {'abs': ABS, 'sqrt': SQRT, 'log': LOG, 'exp': EXP, 'min': MIN, 'max': MAX}


class Analysis:
    ''' One analysis of the optimisation: the result as it was made, what kind it is, and its number among them '''

    def __init__(self, index, result, kind):
        self.index, self.result, self.kind = index, result, kind


# What each kind of analysis offers (names of numbers of its result) is the table of analysis_results (register_result_kind): a new kind of result is a row there, not a change here.  The numbers of
# the design itself (volume, mass, volume_fraction) are in the same table


class Resolver:
    ''' What a name in a formula is: a number of the design (the volume it uses ...), or a number of an analysis.  bound=None: for the
        objective -- a number several analyses offer is their sum; bound=i: the number in the analysis i alone (a constraint is made for each) '''

    def __init__(self, analyses, bound=None, cases=None):
        self.analyses = analyses
        self.bound = bound
        # (a condition made of the results of several analyses: its names end in __a<k>, the k-th analysis of the condition, which is the analysis number cases[k] here)
        self.cases = cases

    def _scope(self):
        return range(len(self.analyses)) if self.bound is None else [self.bound]

    def _available(self):
        names = list(_blocks.design_numbers())
        for a in self.analyses:
            names += [n for n in _blocks.offered(a.kind) if n not in names]
        return names

    def _first(self):
        return self.analyses[0].result

    def name(self, n):
        if n == 'pi':
            return Const(math.pi)
        scope = self._scope()
        m = re.fullmatch(r'(.+)__a(\d+)', n)
        if m and self.cases is not None:
            n, scope = m.group(1), [self.cases[int(m.group(2))]]
        first = self._first()
        if n == 'volume_fraction':
            return Leaf(('fraction',))
        if n == 'volume':
            volume = self.analyses[0].body_volume() if self.analyses[0].kind == 'flow' else first.volume
            return Leaf(('fraction',)) * float(volume)
        if n == 'mass':
            if self.analyses[0].kind == 'flow':
                raise MeasureError("'mass': a body in a flow has a volume (volume, volume_fraction) -- the flow analysis gives it no material")
            mass = getattr(first, 'mass', None)
            if mass is None:
                raise MeasureError("'mass': the material has no density that is a number (it is a field), so the part has no mass to read")
            return Leaf(('fraction',)) * float(mass)
        pick = [i for i in scope if n in _blocks.offered(self.analyses[i].kind)]
        if not pick:
            others = [a for a in self.analyses if n in _blocks.offered(a.kind)]
            if others:
                raise _Missing("'%s' is not a number of this analysis" % n)
            raise MeasureError("'%s' is not a number the optimisation can make best (%s). It can be: %s" % (
                n, 'the analyses given do not have it', ', '.join(self._available())))
        # (a plain number of the analysis -- compliance, the peak stress or displacement, a force, a temperature: the rows of the table that say `simple` -- is one measure of its backend per
        # analysis, summed over them)
        if _blocks.is_simple(self.analyses[pick[0]].kind, n):
            return _sum([Leaf((n, i)) for i in pick])
        if n == 'safety_factor':
            terms = []
            for i in pick:
                y = getattr(self.analyses[i].result.material, 'yield_strength', None)
                if not y:
                    raise MeasureError("'safety_factor': the material has no yield strength")
                terms.append(Const(float(y)) / Leaf(('max_von_mises', i)))
            return _sum(terms)
        if n == 'frequencies':
            return Seq(lambda k: _sum([Leaf(('frequency', i, k)) for i in pick]), 'frequencies')
        if n in ('reaction', 'total_load'):
            return Seq(lambda k: _sum([Const(float(getattr(self.analyses[i].result, n)[k])) for i in pick]), n)
        raise MeasureError("'%s' cannot be read here" % n)

    def point(self, field, args, case=None):
        ''' ux(x, y, z), uy, uz, displacement: the displacement at a point (a static analysis); temperature(x, y, z): the temperature at a point (a thermal analysis) '''
        if len(args) != 3 or not all(isinstance(a, (int, float)) for a in args):
            raise MeasureError("'%s' is read at a place: %s(x, y, z), three numbers" % (field, field))
        kind = 'thermal' if field == 'temperature' else 'static'
        pick = [i for i in (self._scope() if case is None else [case]) if self.analyses[i].kind == kind]
        if not pick:
            raise _Missing("'%s' is read in a %s analysis" % (field, kind))
        if kind == 'thermal':
            return _sum([Leaf(('temperature_at', i) + tuple(float(a) for a in args)) for i in pick])
        return _sum([Leaf(('displacement_at', i, _POINT_FIELDS[field]) + tuple(float(a) for a in args)) for i in pick])


def _sum(terms):
    out = terms[0]
    for t in terms[1:]:
        out = out + t
    return out


class _Proxy:
    ''' The result a function is given: asking it for a number gives that number of the design '''

    def __init__(self, resolver):
        object.__setattr__(self, '_r', resolver)

    def __getattr__(self, name):
        r = object.__getattribute__(self, '_r')
        if name.startswith('_'):
            raise AttributeError(name)
        if name in _POINT_FIELDS:
            return lambda *a: r.point(name, a)
        return r.name(name)


def _node_of(n, resolver, text):
    def bad():
        raise MeasureError("'%s': only the numbers of the result, numbers, + - * / **, [index], abs, min, max, sqrt, log, exp and "
                           "displacement(x, y, z) / ux / uy / uz / temperature(x, y, z) can be written there" % text)

    def number(x):
        v = _node_of(x, resolver, text)
        if isinstance(v, Seq):
            raise MeasureError("'%s': '%s' is a list -- ask for one of its numbers with [index]" % (text, v._name))
        return v

    if isinstance(n, ast.Constant) and isinstance(n.value, (int, float)) and not isinstance(n.value, bool):
        return Const(float(n.value))
    if isinstance(n, ast.Name):
        return resolver.name(n.id)
    if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.USub, ast.UAdd)):
        v = number(n.operand)
        return -v if isinstance(n.op, ast.USub) else v
    if isinstance(n, ast.BinOp):
        a, b = number(n.left), number(n.right)
        op = n.op
        if isinstance(op, ast.Add):
            return a + b
        if isinstance(op, ast.Sub):
            return a - b
        if isinstance(op, ast.Mult):
            return a * b
        if isinstance(op, ast.Div):
            return a / b
        if isinstance(op, ast.Pow):
            return a ** b
        bad()
    if isinstance(n, ast.Subscript):
        base = _node_of(n.value, resolver, text)
        index = n.slice
        if isinstance(base, Seq) and isinstance(index, ast.Constant) and isinstance(index.value, int):
            try:
                return base[index.value]
            except (IndexError, TypeError, KeyError):
                raise MeasureError("'%s': the result has no entry %d of '%s'" % (text, index.value, base._name)) from None
        bad()
    if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and not n.keywords:
        f = n.func.id
        case = None
        m = re.fullmatch(r'(.+)__a(\d+)', f)
        if m and resolver.cases is not None:
            f, case = m.group(1), resolver.cases[int(m.group(2))]          # (a condition of several analyses: the place is read in the one its name says)
        if f in _POINT_FIELDS:
            args = []
            for a in n.args:
                v = _node_of(a, resolver, text)
                if not isinstance(v, Const):
                    bad()
                args.append(v.value)
            return resolver.point(f, args, case)
        if f in _FUNCTIONS:
            args = [number(a) for a in n.args]
            code = _FUNCTIONS[f]
            if code in (MIN, MAX):
                if len(args) < 2:
                    bad()
                out = args[0]
                for a in args[1:]:
                    out = _make(code, out, a)
                return out
            if len(args) != 1:
                bad()
            return _make(code, args[0])
    bad()


_COMPARISONS = {ast.Lt: '<', ast.LtE: '<=', ast.Gt: '>', ast.GtE: '>=', ast.Eq: '=='}
_EQUAL_TOLERANCE = 1e-3                   # (an exact value is kept to within this share of its size)


def _expression(text, resolver, what):
    try:
        tree = ast.parse(text.strip(), mode='eval').body
    except SyntaxError:
        raise MeasureError("%s: '%s' is not an expression" % (what, text)) from None
    out = _node_of(tree, resolver, text)
    if isinstance(out, Seq):
        raise MeasureError("%s: '%s' is a list -- ask for one of its numbers with [index]" % (what, text))
    return _wrap(out)


class Output:
    ''' The objective or a constraint, as a tree: the objective is minimised; a constraint is kept when its tree is 0 or less (the tree is
        in the size of the limit: 0.1 is a tenth over).  `case`: the analysis a constraint was made for (None: all of them, the objective) '''

    def __init__(self, role, text, node, case=None, scale=1.0, value=None):
        # (`value`: for a constraint, the number that is limited -- the left side of the comparison -- which the result reports)
        self.role, self.text, self.node, self.case, self.scale = role, text, node, case, scale
        self.value = value if value is not None else node


def _cases_of(condition, analyses):
    ''' The numbers of the analyses the results of an optimization condition are of, among the analyses the optimization is given, in the order of the condition's own '''
    out = []
    for own in condition.analyses:
        for i, a in enumerate(analyses):
            if a.result is own:
                out.append(i)
                break
        else:
            raise MeasureError('%r: it is made of the results of an analysis that the optimization is not given -- give the optimization the analysis its results are of' % (condition,))
    return out


def _shown(text, cases):
    ''' The text of a condition made of the results of several analyses, as it is shown: compliance@1 + compliance@2 '''
    return text if cases is None else re.sub(r'__a(\d+)', lambda m: '@%d' % (cases[int(m.group(1))] + 1), text)


def objective(spec, goal, analyses, bound=None, cases=None):
    ''' The objective (to minimise) of `spec`: the text of a formula of results; goal 'minimize' or 'maximize' -- or 'satisfy': nothing is made
        best, the design only has to keep the constraints.  `bound`: the analysis alone it is read in (the goal of an optimization condition), None: all of them, summed '''
    if goal == 'satisfy':
        return Output('objective', '', Const(0.0))
    resolver = Resolver(analyses, bound=bound, cases=cases)
    if isinstance(spec, str):
        node = _expression(spec, resolver, goal)
        text = _shown(spec.strip(), cases)
    else:
        raise MeasureError('%s: what is made best is a formula of results, made as an optimization condition (not %r)' % (goal, spec))
    return Output('objective', text, -node if goal == 'maximize' else node)



def constraint(spec, analyses, baseline):
    ''' The constraints `spec` makes: one for each analysis that has the numbers it reads.  `baseline(node)` gives a tree's number for the
        analyses as they were given (to size the constraint) '''
    outs = []
    first_error = None
    cases = range(len(analyses))
    mapping = None
    if isinstance(spec, _blocks.OptimizationCondition):
        # (a condition made of the results of one analysis is kept in that analysis; one made of the results of several is one condition, each result read in its own)
        mapping = _cases_of(spec, analyses)
        cases = [mapping[0]] if len(mapping) == 1 else [None]
        spec = spec.text
    for i in cases:
        resolver = Resolver(analyses, bound=i, cases=mapping)
        try:
            if isinstance(spec, str):
                try:
                    tree = ast.parse(spec.strip(), mode='eval').body
                except SyntaxError:
                    raise MeasureError("conditions=: '%s' is not a comparison" % spec) from None
                if not (isinstance(tree, ast.Compare) and all(type(o) in _COMPARISONS for o in tree.ops)):
                    raise MeasureError("conditions=: '%s' is not a comparison of formulas with <, <=, >, >= or == (80 <= frequencies[0] <= 120 is a range)" % spec)
                shown = _shown(spec.strip(), mapping if mapping is not None and len(mapping) > 1 else None)
                operands = [_wrap(_node_of(o, resolver, spec)) for o in [tree.left] + list(tree.comparators)]
                for k, op in enumerate(tree.ops):
                    left, right = operands[k], operands[k + 1]
                    sign = _COMPARISONS[type(op)]
                    size = abs(left.value) if isinstance(left, Const) and not isinstance(right, Const) else abs(baseline(right))
                    if not (size > 1e-12 and math.isfinite(size)):
                        size = abs(baseline(left))
                    scale = size if size > 1e-12 and math.isfinite(size) else 1.0
                    text = shown if len(analyses) == 1 or i is None else '%s  (analysis %d)' % (shown, i + 1)
                    if len(tree.ops) > 1:
                        text += '  [%d/%d]' % (k + 1, len(tree.ops))
                    if sign == '==':
                        # Kept within a thousandth of the value, from both sides: as two smooth limits.  One limit on |left - right| has a kink at the value,
                        # where the linearisation the optimiser makes changes its sign at every step -- the volume of a design oscillated by 15 % round its target
                        for side, direction in (('at most', 1.0), ('at least', -1.0)):
                            outs.append(Output('constraint', '%s  [%s]' % (text, side), direction * (left - right) / scale - _EQUAL_TOLERANCE, i, scale,
                                               left if leaves(left) else right))
                        continue
                    gap = ((left - right) if sign in ('<', '<=') else (right - left)) / scale
                    outs.append(Output('constraint', text, gap, i, scale, left if leaves(left) else right))
            else:
                raise MeasureError('conditions=: each item is an optimization condition (not %r)' % (spec,))
        except _Missing as e:
            first_error = first_error or e
    if not outs:
        raise MeasureError('conditions=: %s' % (first_error or 'no analysis has what it reads'))
    return outs


def outputs_of(items, analyses, baseline):
    ''' The objective (a constant 0 when there is no goal) and the constraints of the optimization conditions given as `conditions=`, and what the goal is ('minimize', 'maximize', or
        'satisfy' for none).  Only conditions are read: a text, a function or a number is refused, in plain words '''
    if isinstance(items, _blocks.OptimizationCondition):
        items = [items]
    items = list(items or ())
    for c in items:
        if not isinstance(c, _blocks.OptimizationCondition):
            raise MeasureError('conditions=: each item is an optimization condition made from a result of the analysis -- keep_below(max_von_mises_1, 90), keep_above, keep_between, '
                               'minimize(mass_1), maximize, or optimization_condition(max_von_mises_1 <= 90); right-click a result in the model tree to make them. '
                               'A text such as \'max_von_mises <= 90\' or a function is not one (%r)' % (c,))
    goals = [g for g in items if g.is_goal]
    if len(goals) > 1:
        raise MeasureError('conditions=: only one thing can be made best at a time -- write a trade-off as one formula: minimize(mass_1 + 0.01 * compliance_1)')
    if not goals:
        out = [objective(None, 'satisfy', analyses)]
        kind = 'satisfy'
    else:
        # (the goal: read in the analysis its results are of)
        cases = _cases_of(goals[0], analyses)
        out = [objective(goals[0].text, goals[0].how, analyses, bound=cases[0] if len(cases) == 1 else None, cases=cases)]
        kind = goals[0].how
    for c in items:
        if not c.is_goal:
            out += constraint(c, analyses, baseline)
    return out, kind


# ---------------------------------------------------------------------------
# The program the kernel is given

class Program:
    ''' The measures the outputs read, and their formulas in postfix, in the kernel's terms '''

    def __init__(self, outputs):
        self.outputs = outputs
        self.measures = []
        for o in outputs:
            for key in leaves(o.node):
                if key not in self.measures:
                    self.measures.append(key)
        self.ops = []
        for o in outputs:
            self._emit(o.node)
            self.ops.append((END, 0.0, 0))

    def _emit(self, node):
        if isinstance(node, Const):
            self.ops.append((CONST, node.value, 0))
        elif isinstance(node, Leaf):
            self.ops.append((BASE, 0.0, self.measures.index(node.key)))
        else:
            for a in node.args:
                self._emit(a)
            self.ops.append((node.code, 0.0, 0))

    def kernel_measure(self, key):
        ''' (kind, case, index, a, b, c) of a measure for libfive_tetfea_opt_measure '''
        name = key[0]
        if name == 'fraction':
            return (VOLUME, 0, 0, 0.0, 0.0, 0.0)
        if name == 'compliance':
            return (COMPLIANCE, key[1], 0, 0.0, 0.0, 0.0)
        if name == 'max_von_mises':
            return (MAX_VON_MISES, key[1], 0, 0.0, 0.0, 0.0)
        if name == 'max_displacement':
            return (MAX_DISPLACEMENT, key[1], 0, 0.0, 0.0, 0.0)
        if name == 'frequency':
            return (FREQUENCY, key[1], key[2], 0.0, 0.0, 0.0)
        if name == 'displacement_at':
            return (DISPLACEMENT_AT, key[1], key[2], key[3], key[4], key[5])
        raise MeasureError('a measure that the kernel does not have: %s' % (key,))

    def flow_measure(self, key):
        ''' The kind of a measure for libfive_tetflow_opt_measure (TetFlowProblem::DensityMeasure): 0 the force on the body along the flow, 1 across it,
            2 the volume of the body as a share of its own '''
        name = key[0]
        if name == 'drag_force':
            return 0
        if name == 'lift_force':
            return 1
        if name == 'fraction':
            return 2
        raise MeasureError('a measure that a flow does not have: %s' % (key,))

    def thermal_measure(self, key):
        ''' (kind, a, b, c) of a measure for libfive_tetthermal_opt_measure (TetThermalProblem::DensityMeasure): 0 the volume of the design as a share of the part's,
            1 the peak temperature, 2 the lowest, 3 the peak heat flux, 4 the temperature the heat comes in at, 5 the temperature at the point a, b, c '''
        name = key[0]
        if name == 'fraction':
            return (0, 0.0, 0.0, 0.0)
        kinds = {'max_temperature': 1, 'min_temperature': 2, 'max_heat_flux': 3, 'input_temperature': 4}
        if name in kinds:
            return (kinds[name], 0.0, 0.0, 0.0)
        if name == 'temperature_at':
            return (5, key[2], key[3], key[4])
        raise MeasureError('a measure that a thermal analysis does not have: %s' % (key,))

    def key(self):
        ''' What the program is, for the cache '''
        return (tuple(self.measures), tuple(self.ops))

    def start_fraction(self):
        ''' The share of the part the design starts with: the most that the constraints on the volume alone allow (1 when none limits it) '''
        pure = [o for o in self.outputs if o.role == 'constraint' and all(k == ('fraction',) for k in leaves(o.node))]
        if not pure:
            return 1.0
        for step in range(100, 4, -1):
            f = step / 100.0
            if all(evaluate(o.node, lambda key: f) <= 1e-9 for o in pure):
                return f
        return 1.0


def true_value(key, results):
    ''' The number a measure has in the analyses of a design (a list of results, in the order of the optimisation's) '''
    name = key[0]
    if name == 'fraction':
        raise MeasureError('the share of the part is read from the part')
    r = results[key[1]]
    if name in ('drag_force', 'lift_force'):
        value = getattr(r, name)
        if value is None:
            raise MeasureError("'%s': the direction of the flow is not known -- give an inlet a velocity=(vx, vy, vz)" % name)
        return float(value)
    if name == 'compliance':
        return float(r.compliance)
    if name == 'max_von_mises':
        return float(r.max_von_mises)
    if name == 'max_displacement':
        return float(r.max_displacement)
    if name == 'frequency':
        return float(r.frequencies[key[2]])
    if name in ('max_temperature', 'min_temperature', 'max_heat_flux'):
        return float(getattr(r, name))
    if name == 'input_temperature':
        value = getattr(r, 'input_temperature', None)
        if value is None:
            raise MeasureError("'input_temperature': the temperature the heat comes in at is read from a thermal analysis with tetrahedral elements (element='tet')")
        return float(value)
    if name == 'temperature_at':
        from fieldes.stdlib.fields import evaluate as field_at
        return float(field_at(r.temperature, [(key[2], key[3], key[4])])[0])
    if name == 'displacement_at':
        from fieldes.stdlib.fields import evaluate as field_at
        field = ('ux', 'uy', 'uz', 'displacement')[key[2]]
        return float(field_at(getattr(r, field), [(key[3], key[4], key[5])])[0])
    raise MeasureError('a measure that is not known: %s' % (key,))
