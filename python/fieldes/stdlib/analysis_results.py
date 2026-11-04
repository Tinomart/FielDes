'''
Analysis results and optimization conditions: what an analysis gives that an optimization can be asked about, and what an optimization does with it -- made from the context menu, without writing a line.

    stress = static_analysis(part, supports=[fixed(base)], loads=[force(lug, (0, 0, -300))], material=steel)
    von_mises_1 = stress.max_von_mises                           # one analysis result for each RESULT: the peak von Mises stress of this analysis
    mass_1 = stress.mass                                         # (every entry of the analysis's properties is one: its fields and its numbers)
    limit_1 = keep_below(von_mises_1, 90)                        # a condition: at most 90 MPa
    goal_1 = minimize(mass_1)                                    # a condition: as little mass as possible
    best = topology_optimization(stress, conditions=[goal_1, limit_1])

AN ANALYSIS RESULT is one result of one analysis -- a von Mises stress, a deformation, a velocity field, a temperature field, a frequency, a force: not a whole simulation.  It is a field, a
value at every place that works in any expression, and it also says what it is (`kind`, `label`, `unit`), the analysis it came from, its number now (`value`).  The class is internal: nobody makes
one by hand, the analysis does, and it is in no menu by that name.

AN OPTIMIZATION CONDITION says what an optimization does with results of the analyses it is given.  Its parent class, OptimizationCondition, is what a comparison of results is (`von_mises_1 <= 90`,
`mass_1 / volume_1 < 5`: arithmetic on results keeps which results a formula is made of) and what optimization_condition(condition) takes: ONE argument, the whole condition, which must have an
analysis result in it -- a condition that no analysis can change is refused when it is written.  Its children hold their own logic and take a result and their limits: keep_below(result, limit),
keep_above(result, limit), keep_between(result, lower, upper), minimize(result) and maximize(result) (at most one goal).  The optimizations (topology_optimization, parametric_optimization) take
nothing else in `conditions=`.  A condition made of the results of one analysis is kept in that analysis; one made of the results of several (the load cases of one design:
`minimize(compliance_push + compliance_pull)`) reads each result in its own.

THE KINDS OF RESULT ARE A TABLE, not code scattered over the optimizer: `register_result_kind(physics, name, label, unit, ...)` says that an analysis of that physics ('static', 'modal', 'thermal',
'flow', or any new one) has a result of that name; the optimizer's list of numbers it can make best, the analysis results and the errors that name them are all made from this table.  A new kind of result
is a new row, here or in the module that calculates it -- nothing else in this module changes.  (The optimizer of a physics still has to be able to calculate the number: that stays in its own module.)

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import functools
import math
import numbers
import operator
import os
import re
from collections import OrderedDict

from fieldes.ffi import lib
from fieldes.shape import Shape, field_shape

__all__ = ['optimization_condition', 'keep_below', 'keep_above', 'keep_between', 'minimize', 'maximize']


class ResultKind:
    ''' One kind of result an analysis of a physics gives: its name (the name of the number in a formula: 'max_von_mises'), what to call it, its unit, the name of its field among the fields the
        result calculated (None: it has none), whether it is a plain number per analysis (the optimizer reads it as a measure of its own) and whether it is a list read with [i] '''

    def __init__(self, physics, name, label, unit, field, simple, sequence, measure):
        self.physics, self.name, self.label, self.unit, self.field, self.simple, self.sequence, self.measure = physics, name, label, unit, field, simple, sequence, measure


# physics -> {name: ResultKind}.  'design' holds the numbers of the design itself, which every physics has
_KINDS = OrderedDict()


def register_result_kind(physics, name, label, unit='', field=None, simple=True, sequence=False, measure=True):
    ''' Say that an analysis of `physics` has a result `name` (see the module).  `simple`: a plain number per analysis, which the optimizer asks its backend for under this name; `sequence`: a list of
        numbers ('frequencies'), read with [i]; `measure`: False for a result that is only a field (the velocity of a flow), which an analysis result shows but no optimization can be asked about -- the optimizer
        reads numbers (its peak, a force) '''
    _KINDS.setdefault(physics, OrderedDict())[name] = ResultKind(physics, name, label, unit, field, simple and not sequence and measure, sequence, measure)


def offered(physics):
    ''' The names of the numbers an analysis of this physics gives an optimization (not the numbers of the design, not the results that are only fields) '''
    return tuple(n for n, k in _KINDS.get(physics, {}).items() if k.measure)


def design_numbers():
    return tuple(_KINDS.get('design', ()))


def kind_of_result(physics, name):
    ''' The ResultKind of that name, in the physics or among the numbers of the design; None when there is none '''
    return _KINDS.get(physics, {}).get(name) or _KINDS.get('design', {}).get(name)


def is_simple(physics, name):
    k = _KINDS.get(physics, {}).get(name)
    return bool(k and k.simple)


def physics_of(result):
    ''' The physics of the result of an analysis ('static', 'modal', 'thermal', 'flow' ...): the `_physics` its class says '''
    return getattr(type(result), '_physics', None)


# The kinds the program has.  Static analysis (a part under loads), modal analysis, a heat conduction, a body in a flow
register_result_kind('static', 'compliance', 'Compliance', 'N mm')
register_result_kind('static', 'max_von_mises', 'Von Mises stress (peak)', 'MPa', field='von_mises')
register_result_kind('static', 'max_displacement', 'Deformation (peak)', 'mm', field='displacement')
register_result_kind('static', 'safety_factor', 'Safety factor', '', simple=False)
register_result_kind('static', 'reaction', 'Reaction force', 'N', simple=False, sequence=True)
register_result_kind('static', 'total_load', 'Total load', 'N', simple=False, sequence=True)
register_result_kind('modal', 'frequencies', 'Natural frequency', 'Hz', sequence=True)
register_result_kind('flow', 'drag_force', 'Drag force', 'N')
register_result_kind('flow', 'lift_force', 'Lift force', 'N')
register_result_kind('flow', 'speed', 'Velocity (field)', 'mm/s', field='speed', measure=False)
register_result_kind('flow', 'pressure', 'Pressure (field)', 'MPa', field='pressure', measure=False)
register_result_kind('thermal', 'max_temperature', 'Temperature (peak)', '', field='temperature')
register_result_kind('thermal', 'min_temperature', 'Temperature (lowest)', '', field='temperature')
register_result_kind('thermal', 'max_heat_flux', 'Heat flux (peak)', '')
register_result_kind('thermal', 'input_temperature', 'Temperature the heat comes in at', '')
register_result_kind('design', 'volume', 'Volume', 'mm3', simple=False)
register_result_kind('design', 'mass', 'Mass', '', simple=False)
register_result_kind('design', 'volume_fraction', 'Volume fraction', '', simple=False)

# What an analysis result is made of when its kind is not said: the result an analysis of that physics is mainly about
_MAIN = {'static': 'max_von_mises', 'modal': 'frequencies', 'flow': 'drag_force', 'thermal': 'max_temperature'}


_MARK = re.compile(r'__x(\d+)')


def _clean(text):
    ''' The text of a formula as it is read: without the marks that say which analysis each name is of '''
    return _MARK.sub('', text)


def _union(*groups):
    ''' The analysis results of several groups, each once, in the order they come '''
    out = OrderedDict()
    for group in groups:
        for r in group:
            out[id(r)] = r
    return tuple(out.values())


def _formula_of(x):
    ''' (text, results) of a thing in a formula of results: how the optimizer writes it, and the analysis results it is made of.  A number is its own text; anything else (a text, a field no
        analysis made) has none -- the condition that is made of it says so '''
    if isinstance(x, _ResultMath):
        return x._formula()
    if isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x):
        return repr(float(x)), ()
    return None, ()


def _formula_made(field, text, results, value=None):
    ''' The field `field` of an arithmetic step, as a formula of results: the same field, and the text and the results it is made of (and its number, where all of them are one number) '''
    out = ResultFormula(lib.libfive_tree_copy(field.ptr), text, tuple(results), value)
    out._kind = field._kind
    out._field_origin = field._field_origin
    return out


def _number_of(x):
    ''' The number of a thing in a formula of results, or None: a result that is one number, a formula of such, a plain number '''
    if isinstance(x, _ResultMath):
        return x._num()
    if isinstance(x, (int, float)) and not isinstance(x, bool):
        return float(x)
    return None


_OPERATIONS = {'+': operator.add, '-': operator.sub, '*': operator.mul, '/': operator.truediv, '**': operator.pow}


def _combined(a, b, symbol, swapped):
    ''' `a symbol b` (b symbol a when swapped) as a number, or None where either is not one '''
    if a is None or b is None:
        return None
    if swapped:
        a, b = b, a
    try:
        return float(_OPERATIONS[symbol](a, b))
    except (ZeroDivisionError, OverflowError, ValueError, TypeError):
        return None


def _compare(left, how, right):
    ''' The condition that `left how right` says, left being a result or a formula of results.  Where both sides are one number the condition also knows if it holds -- `if peak < 90:` in a script
        reads the numbers of the analysis (a condition on a field has no one answer: it is true, as every object is) '''
    text, results = left._formula()
    other_text, other_results = _formula_of(right)
    if text is None or other_text is None:
        raise TypeError('A condition compares results of an analysis and numbers: there is something in this comparison that is neither')
    condition = OptimizationCondition('%s %s %s' % (text, how, other_text), _union(results, other_results))
    a, b = left._num(), _number_of(right)
    if a is not None and b is not None:
        condition._truth = {'<': a < b, '<=': a <= b, '>': a > b, '>=': a >= b}[how]
    return condition


class _ResultMath:
    ''' What a result and a formula of results can do besides being a field: arithmetic that keeps WHICH results a formula is made of, and the comparisons that make a condition of it.  The field
        of the arithmetic is the field of every other arithmetic -- the formula is a field to work with -- and the text is what the optimizer reads '''

    def _formula(self):
        raise NotImplementedError

    def _num(self):
        ''' The one number this is (a result that is a number, a formula of such), or None: a field is a value at every place '''
        return None

    def _arith(self, name, symbol, other, swapped):
        field = getattr(Shape, name)(self, other)
        text, results = self._formula()
        other_text, other_results = _formula_of(other)
        joined = None
        if text is not None and other_text is not None:
            joined = '(%s %s %s)' % ((other_text, symbol, text) if swapped else (text, symbol, other_text))
        return _formula_made(field, joined, _union(results, other_results), _combined(self._num(), _number_of(other), symbol, swapped))

    def __add__(self, other):
        return self._arith('__add__', '+', other, False)

    def __radd__(self, other):
        return self._arith('__radd__', '+', other, True)

    def __sub__(self, other):
        return self._arith('__sub__', '-', other, False)

    def __rsub__(self, other):
        return self._arith('__rsub__', '-', other, True)

    def __mul__(self, other):
        return self._arith('__mul__', '*', other, False)

    def __rmul__(self, other):
        return self._arith('__rmul__', '*', other, True)

    def __truediv__(self, other):
        return self._arith('__truediv__', '/', other, False)

    def __rtruediv__(self, other):
        return self._arith('__rtruediv__', '/', other, True)

    def __pow__(self, other):
        return self._arith('__pow__', '**', other, False)

    def __rpow__(self, other):
        return self._arith('__rpow__', '**', other, True)

    def __neg__(self):
        field = Shape.__neg__(self)
        text, results = self._formula()
        number = self._num()
        return _formula_made(field, None if text is None else '(-%s)' % text, results, None if number is None else -number)

    def __lt__(self, other):
        return _compare(self, '<', other)

    def __le__(self, other):
        return _compare(self, '<=', other)

    def __gt__(self, other):
        return _compare(self, '>', other)

    def __ge__(self, other):
        return _compare(self, '>=', other)


class ResultFormula(_ResultMath, Shape):
    ''' A formula of analysis results (`mass_1 / volume_1`): a field that works in every expression, and also the text the optimizer reads and the results it is made of '''

    def __init__(self, ptr, text, results, value=None):
        Shape.__init__(self, ptr)
        self._text, self._results, self._value = text, tuple(results), value

    def _formula(self):
        return self._text, self._results

    def _num(self):
        return self._value

    def __float__(self):
        if self._value is None:
            raise TypeError('a formula of fields is a value at every place, not one number')
        return float(self._value)

    def __format__(self, spec):
        return format(float(self), spec) if self._value is not None else Shape.__format__(self, spec) if hasattr(Shape, '__format__') else str(self)

    @property
    def results(self):
        return self._results

    @property
    def text(self):
        return _clean(self._text)

    def __repr__(self):
        # (what a console shows for a formula of results: the formula, and its number where it is one -- never the name of a class of the program, the user, 2026-11-03, H82)
        if self._text is None:
            return 'a formula of fields'
        text = self.text
        return ('%s = %.6g' % (text, self._value)) if self._value is not None else text


class AnalysisResult(_ResultMath, Shape):
    ''' One result of one analysis (see the module), MADE BY THE ANALYSIS (see result_field): a field -- a value at every point, usable in any expression as every field is (the library's field is the class
        Shape with the kind 'field') -- that also says what it is: `analysis`, `kind` (a ResultKind), `index` (of a list such as the frequencies).  A result that is only a number (the mass, the
        compliance) is a field that has that number everywhere.  Nobody makes one by hand: the analysis does, and an optimization condition is made from it (the user, 2026-11-03: "TURN ALL OF THE ANALAYSIS
        RESULTS INTO THE NEW CLASS ... IT REMAINS A FIELD THAT YOU CAN WORK WITH") '''

    def __init__(self, analysis, kind, index=None, field=None):
        if field is None and kind.field:
            field = getattr(analysis, kind.field, None)
        if field is not None:
            Shape.__init__(self, lib.libfive_tree_copy(field.ptr))
            for name, value in field.__dict__.items():
                if name != 'ptr':
                    self.__dict__[name] = value              # (what the field carries: where it is about, the numbers it was drawn from ...)
            self._kind = 'field'
        else:
            number = 0.0
            try:
                v = _raw_number(analysis, kind.name)
                number = float(v[index] if index is not None else v)
            except Exception:
                pass                                         # (the analysis has not got the number yet: the optimizer asks the analysis for it, not this field)
            Shape.__init__(self, lib.libfive_tree_const(number))     # (a tree of its own: the copy of a temporary's pointer was freed before the copy was made)
            self._kind = 'const'
        self.analysis, self.kind, self.index = analysis, kind, index
        # (the model tree shows it in the colour of the domain of its analysis -- structural, thermal, fluid -- as it shows the analysis: see kinds.domain_of_value)
        from fieldes.kinds import domain_of_value
        self._engineering_domain = domain_of_value(analysis)

    def _formula(self):
        # (the name carries the analysis it is a result of: a condition made of results of several analyses reads each in its own -- see OptimizationCondition)
        name = '%s__x%d' % (self.kind.name, id(self.analysis))
        return name + ('[%d]' % self.index if self.index is not None else ''), (self,)

    @property
    def name(self):
        return self.kind.name

    @property
    def label(self):
        return self.kind.label + (' %d' % (self.index + 1) if self.index is not None else '')

    @property
    def unit(self):
        return self.kind.unit

    @property
    def text(self):
        ''' How a formula of the optimizer writes it: max_von_mises, frequencies[0] '''
        return self.kind.name + ('[%d]' % self.index if self.index is not None else '')

    @property
    def value(self):
        ''' The number the analysis has now '''
        v = _raw_number(self.analysis, self.kind.name)
        return v[self.index] if self.index is not None else v

    def _num(self):
        try:
            return self._number()
        except (TypeError, AttributeError, KeyError, IndexError):
            return None

    def _number(self):
        ''' The one number this result is -- the peak, the compliance, the mass, the n-th frequency --, or a TypeError where it is a field that has no one number (the displacement in x) '''
        v = _raw_number(self.analysis, self.kind.name)
        if self.index is not None and isinstance(v, (list, tuple)):
            v = v[self.index]
        if isinstance(v, numbers.Real) and not isinstance(v, bool):
            return float(v)
        raise TypeError("'%s' is a field: a value at every place, not one number" % self.kind.name)

    # A result that is a number is read as that number by whatever asks for one: `float(peak)`, `'%.3g' % peak`, `f'{peak:.2f}'`, `round(peak, 1)`, `print(peak)` (the user, 2026-11-03, H91: the numbers an
    # analysis gives are results, as its fields are)
    def __float__(self):
        return self._number()

    def __int__(self):
        return int(self._number())

    def __round__(self, ndigits=None):
        return round(self._number(), ndigits) if ndigits is not None else round(self._number())

    def __format__(self, spec):
        try:
            return format(self._number(), spec)
        except TypeError:
            return format(repr(self), spec)

    def __str__(self):
        try:
            return '%.6g' % self._number()
        except TypeError:
            return repr(self)

    @property
    def field(self):
        ''' The field of this result among the fields the analysis calculated, or None when this kind of result has none '''
        return self if self._kind == 'field' else None

    def at(self, x, y, z):
        ''' This result read at a place -- `static_analysis_1.uz.at(100, 0, 0)`, the displacement there, or `thermal_analysis_1.temperature.at(0, 0, 5)`, the temperature.  It is a number of the analysis, so
            an optimization condition can be made of it: keep_below(static_analysis_1.uz.at(100, 0, 0), 0.5).  Only the fields that are read at a place have it: ux, uy, uz, displacement and temperature '''
        name = self.kind.field
        if name not in _AT_A_PLACE:
            raise ValueError("'%s' is not read at a place: a condition can read the displacement (ux, uy, uz, displacement) of a static analysis and the temperature of a thermal one there" % self.kind.name)
        for v in (x, y, z):
            if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
                raise ValueError('at(x, y, z) reads a result at a place: x, y and z are three numbers')
        return ResultAtPlace(self, name, x, y, z)

    def __repr__(self):
        number = self._num()
        unit = (' ' + self.unit) if self.unit else ''
        # (what a console shows for a result: its number where it has one -- never the name of a class of the program)
        return ('%s = %.6g%s' % (self.text, number, unit)) if number is not None else '%s of the analysis%s' % (self.text, unit)


# The fields a result can be read at a place from (the optimizer asks its backend for the number there): the displacement of a static analysis, the temperature of a thermal one
_AT_A_PLACE = ('ux', 'uy', 'uz', 'displacement', 'temperature')


class ResultAtPlace(_ResultMath, Shape):
    ''' A result of an analysis read at a place (`result.at(x, y, z)`): one number of the analysis -- the displacement or the temperature at that place --, so an optimization condition is made of it as of
        any other result.  As a field it has that number everywhere; its text is what the optimizer reads: uz(100.0, 0.0, 0.0) '''

    def __init__(self, result, name, x, y, z):
        self._source, self.place = result, (float(x), float(y), float(z))
        Shape.__init__(self, lib.libfive_tree_const(self._number()))
        self._kind = 'const'
        base = result.kind
        self.analysis, self.index = result.analysis, None
        self.kind = ResultKind(base.physics, name, base.label + ' at a place', base.unit, None, False, False, True)
        from fieldes.kinds import domain_of_value
        self._engineering_domain = domain_of_value(self.analysis)

    def _number(self):
        try:
            from fieldes.stdlib.fields import evaluate as field_at
            return float(field_at(self._source, [self.place])[0])
        except Exception:
            return 0.0                                       # (the analysis has not calculated it yet: the optimizer asks the analysis for the number, not this field)

    def _formula(self):
        return ('%s__x%d(%r, %r, %r)' % ((self.kind.name, id(self.analysis)) + self.place)), (self,)

    @property
    def name(self):
        return self.kind.name

    @property
    def label(self):
        return self.kind.label

    @property
    def unit(self):
        return self.kind.unit

    @property
    def text(self):
        ''' How a formula of the optimizer writes it: uz(100.0, 0.0, 0.0) '''
        return '%s(%r, %r, %r)' % ((self.kind.name,) + self.place)

    @property
    def value(self):
        ''' The number the analysis has there now '''
        return self._number()

    def __repr__(self):
        return '%s of the analysis' % self.text


def main_result(analysis):
    ''' The result an analysis is mainly about -- the peak stress of a static one, the first frequency, the drag force --, which is what an optimization condition made of the analysis itself
        stands for '''
    physics = physics_of(analysis)
    if physics is None:
        raise TypeError('a condition is made of the results of an analysis of this script (static_analysis, modal_analysis, thermal_analysis, fluid_analysis) -- %s is not one' % type(analysis).__name__)
    name = _MAIN.get(physics)
    got = analysis.__dict__.get(name)
    if isinstance(got, ResultSequence):
        return got[0]
    if isinstance(got, AnalysisResult):
        return got
    found = kind_of_result(physics, name)
    return AnalysisResult(analysis, found, 0 if found.sequence else None)


def _raw_number(analysis, name):
    ''' The number (or list of numbers) of the analysis under that name: the number itself, not the result that stands for it (see wrap_numbers) '''
    raw = analysis.__dict__.get('_raw_numbers')
    if raw is not None and name in raw:
        return raw[name]
    return getattr(analysis, name)


class ResultSequence(AnalysisResult):
    ''' A result that is a list of numbers -- the natural frequencies, the reaction force: it is the first of them as a result, `[i]` is the i-th as one, and it counts and goes through them as a list does '''

    def __init__(self, analysis, kind):
        AnalysisResult.__init__(self, analysis, kind, 0)

    def _items(self):
        return _raw_number(self.analysis, self.kind.name)

    def __len__(self):
        return len(self._items())

    def __getitem__(self, i):
        n = len(self)
        if isinstance(i, slice):
            return [self[k] for k in range(*i.indices(n))]
        if not isinstance(i, int) or isinstance(i, bool):
            raise TypeError('a list of results is read with a whole number: %s[0] is the first' % self.kind.name)
        if i < 0:
            i += n
        if not 0 <= i < n:
            raise IndexError('%s has %d numbers: %s[%d]' % (self.kind.name, n, self.kind.name, i))
        return AnalysisResult(self.analysis, self.kind, i)

    def __iter__(self):
        return (self[i] for i in range(len(self)))


def _is_number(x):
    return isinstance(x, numbers.Real) and not isinstance(x, bool) and math.isfinite(float(x))


def wrap_numbers(result):
    ''' The numbers an analysis gives -- the compliance, the peak stress, the mass, the frequencies -- are analysis results too, every one an entry of its properties like the fields are:
        `stress.compliance` is an analysis result (a field that has that number everywhere, which float(), format() and print() read as the number), and no function takes the analysis to
        make one (the user, 2026-11-03, H91: "EACH OF THOSE FIELDS IS A FUCKING RESULT. NOT A NEW BLOCK THAT YOU MADE UP").  The number itself is kept for the library's own reading
        (`_raw_numbers`, see _raw_number).  Called at the end of the constructor of an analysis (see wrap_class) '''
    physics = physics_of(result)
    if physics is None:
        return
    raw = result.__dict__.setdefault('_raw_numbers', {})
    kinds = list(_KINDS.get(physics, {}).values()) + list(_KINDS.get('design', {}).values())
    for k in kinds:
        v = result.__dict__.get(k.name)
        if isinstance(v, AnalysisResult):
            continue
        if v is None:
            if k.physics == 'design':
                # (the volume, the mass and the volume fraction of the design: every analysis has them as results, `stress.mass`.  Where it knows no number of its own the optimization reads the design's)
                result.__dict__[k.name] = AnalysisResult(result, k)
            continue
        if k.sequence:
            if isinstance(v, (list, tuple)) and v and all(_is_number(x) for x in v):
                raw[k.name] = list(v)
                result.__dict__[k.name] = ResultSequence(result, k)
        elif _is_number(v):
            raw[k.name] = v
            result.__dict__[k.name] = AnalysisResult(result, k)


def property_result(analysis, physics, name, number):
    ''' A number that a PROPERTY of the class of an analysis gives -- the mass, the drag force --, as the result it is: a property of the class stands before the entry of the instance that wrap_numbers makes, so
        wrap_numbers cannot put it there and the property gives the result itself (the user, 2026-11-03, H91 / H100: every number of an analysis is a result; the flow example had `minimize(drag_force_1)` stop with
        "not from a float").  The number is kept for the library's own reading (_raw_number) and the same result is given each time; None stays None (the analysis knows no number) '''
    if number is None:
        return None
    raw = analysis.__dict__.setdefault('_raw_numbers', {})
    raw[name] = number
    key = '_%s_result' % name
    got = analysis.__dict__.get(key)
    if got is None or got.analysis is not analysis:
        got = AnalysisResult(analysis, _KINDS[physics][name])
        analysis.__dict__[key] = got
    return got


def mass_result(analysis, number):
    ''' The mass of an analysis as the result it is (see property_result) '''
    return property_result(analysis, 'design', 'mass', number)


def wrap_class(cls):
    ''' The class of an analysis, made so that the numbers it gives are analysis results (see wrap_numbers) when an analysis is made '''
    init = cls.__init__

    @functools.wraps(init)
    def made(self, *args, **kwargs):
        init(self, *args, **kwargs)
        wrap_numbers(self)

    cls.__init__ = made
    return cls


def rebind(copy):
    ''' An analysis found again in the cache for another shape is a copy of the one that was made (fea._copy_for): the numbers it gives -- `copy.compliance` -- are results of the copy, not of the analysis it was
        copied from.  An optimization is given the copy and asks each of its conditions whether it is made of results of the analyses it is given (the user, 2026-11-03, H101: the tour step that
        runs the script again after an edit raised "it is made of the results of an analysis that the optimization is not given") '''
    for name, v in list(copy.__dict__.items()):
        if isinstance(v, AnalysisResult) and v.analysis is not copy:
            copy.__dict__[name] = ResultSequence(copy, v.kind) if isinstance(v, ResultSequence) else AnalysisResult(copy, v.kind, v.index)


def _kind_of_field(analysis, name):
    ''' The kind of result the field `name` of this analysis is: the first result the table says has that field (the peak of the von Mises field is a result of the field von_mises); a field that no
        result has a number of (ux, the velocity) is a result of its own, which no optimization is asked about '''
    physics = physics_of(analysis) or 'fields'
    for k in _KINDS.get(physics, {}).values():
        if k.field == name:
            return k
    register_result_kind(physics, name, name.replace('_', ' ').capitalize(), '', field=name, measure=False)
    return _KINDS[physics][name]


def result_field(analysis, name, ptr):
    ''' What every analysis stores a field it calculated as -- `static_analysis_1.von_mises`, the temperature, the velocity, each mode's displacement: the analysis result of that name, a field that
        works in every expression and knows which result of which analysis it is.  `ptr` is the tree the solver made '''
    return AnalysisResult(analysis, _kind_of_field(analysis, name), field=field_shape(ptr))


_GOALS = ('minimize', 'maximize')
_LIBRARY_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class OptimizationCondition:
    ''' What an optimization does with the results of its analyses: a comparison it keeps (`how` is 'keep') or a goal (`how` is 'minimize' or 'maximize').  This parent is what a comparison of results
        is -- `von_mises_1 <= 90`, `mass_1 / volume_1 < 5` -- and the children hold their own logic and take a result and their limits: KeepBelow, KeepAbove, KeepBetween, Minimize, Maximize (the user,
        2026-11-03: "they are just inherited classes that only take a result and limit and implement their logic internally").  It is kept in the analysis its results are of; made of the results of several analyses (the load cases of one design) it reads each in its own.  `text` is what the
        optimizer reads: the comparison, or for a goal the formula that is made best '''
    how = 'keep'

    def __init__(self, text, results):
        results = tuple(results)
        if not results:
            raise ValueError('This condition uses no analysis result, so the design can never change whether it is kept. Make it from a result of an analysis: keep_below(von_mises_1, 90), or '
                             'optimization_condition(von_mises_1 <= 90)')
        for r in results:
            if not r.kind.measure:
                raise ValueError("'%s' is a field (%s), not a number an optimization can be asked about -- it can be asked about: %s" % (
                    r.kind.name, r.kind.label, ', '.join(offered(r.kind.physics) + design_numbers())))
        analyses = []
        for r in results:
            if not any(r.analysis is a for a in analyses):
                analyses.append(r.analysis)
        if len(analyses) == 1:
            text = _clean(text)                      # (one analysis: the names are plain, as the optimizer has always read them)
        else:
            numbers = {id(a): k for k, a in enumerate(analyses)}
            text = _MARK.sub(lambda m: '__a%d' % numbers[int(m.group(1))], text)      # (several: each name says which analysis of the condition it is read in)
        self._text, self.results = text, results
        self.analyses = tuple(analyses)
        self.analysis = analyses[0]
        self._engineering_domain = getattr(results[0], '_engineering_domain', '')

    @property
    def result(self):
        ''' The first of the results it is made of '''
        return self.results[0]

    @property
    def is_goal(self):
        return self.how in _GOALS

    @property
    def text(self):
        return self._text

    def __bool__(self):
        # (a chain such as 10 < von_mises_1 < 90, and `and` / `or`, ask whether a condition is true: the first half would be dropped without a word.  The library itself may ask)
        import sys
        name = sys._getframe(1).f_code.co_filename
        if not (os.path.isabs(name) and os.path.normcase(name).startswith(os.path.normcase(_LIBRARY_DIR))):
            truth = getattr(self, '_truth', None)
            if truth is not None:
                return bool(truth)          # (both sides are one number -- `if peak < 90:` in a script reads the numbers of the analysis)
            raise TypeError('A condition is given to an optimization, it is not true or false: a chain such as 10 < von_mises_1 < 90 (and `and`, `or`) cannot hold one. '
                            'For a range between two limits use keep_between(von_mises_1, 10, 90)')
        return True

    def __repr__(self):
        return ('%s %s' % (self.how, self._text)) if self.is_goal else self._text          # (the words of the condition, as the optimizer reads them: an error that names it does not name a class of the program)


def _result_of(result, who):
    ''' (text, results) of what a condition `who` is made from: a result of an analysis, or a formula of them '''
    if not isinstance(result, _ResultMath) and physics_of(result) is not None:
        result = main_result(result)           # (an analysis stands for its main result: the peak stress of a static one, the first frequency, the drag force, the peak temperature)
    if not isinstance(result, _ResultMath):
        raise TypeError('%s is made from a result of an analysis -- right-click a result in the model tree and choose it there -- not from %s'
                        % (who, 'a field that no analysis made' if isinstance(result, Shape) else 'a ' + type(result).__name__))
    text, results = result._formula()
    if text is None:
        raise TypeError('%s: there is something in this formula that is neither a result of an analysis nor a number' % who)
    return text, results


def _limit(value, who, what):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('%s: the %s is a number, for example %s(von_mises_1, 90)' % (who, what, who))
    return float(value)


class KeepBelow(OptimizationCondition):
    ''' The result stays at most `limit` '''

    def __init__(self, result, limit):
        text, results = _result_of(result, 'keep_below')
        self.limit = _limit(limit, 'keep_below', 'limit')
        OptimizationCondition.__init__(self, '%s <= %r' % (text, self.limit), results)


class KeepAbove(OptimizationCondition):
    ''' The result stays at least `limit` '''

    def __init__(self, result, limit):
        text, results = _result_of(result, 'keep_above')
        self.limit = _limit(limit, 'keep_above', 'limit')
        OptimizationCondition.__init__(self, '%s >= %r' % (text, self.limit), results)


class KeepBetween(OptimizationCondition):
    ''' The result stays between `lower` and `upper` '''

    def __init__(self, result, lower, upper):
        text, results = _result_of(result, 'keep_between')
        self.lower = _limit(lower, 'keep_between', 'lower limit')
        self.upper = _limit(upper, 'keep_between', 'upper limit')
        if self.lower > self.upper:
            raise ValueError('keep_between: the lower limit (%g) must not be above the upper limit (%g)' % (self.lower, self.upper))
        # (both limits the same number: the result is kept at that value, to within a thousandth, from both sides)
        OptimizationCondition.__init__(self, ('%s == %r' % (text, self.lower)) if self.lower == self.upper else '%r <= %s <= %r' % (self.lower, text, self.upper), results)


class Minimize(OptimizationCondition):
    ''' The result is made as small as it can be (a goal) '''
    how = 'minimize'

    def __init__(self, result):
        text, results = _result_of(result, 'minimize')
        OptimizationCondition.__init__(self, text, results)


class Maximize(OptimizationCondition):
    ''' The result is made as large as it can be (a goal) '''
    how = 'maximize'

    def __init__(self, result):
        text, results = _result_of(result, 'maximize')
        OptimizationCondition.__init__(self, text, results)


def keep_below(result, limit):
    ''' The optimization keeps the result (an analysis result, or a formula of them) at most `limit`: `keep_below(von_mises_1, 90)` '''
    return KeepBelow(result, limit)


def keep_above(result, limit):
    ''' The optimization keeps the result at least `limit`: `keep_above(frequency_1, 80)` '''
    return KeepAbove(result, limit)


def keep_between(result, lower, upper):
    ''' The optimization keeps the result between `lower` and `upper`: `keep_between(frequency_1, 80, 120)`.  With both limits the same number the result is kept at that value '''
    return KeepBetween(result, lower, upper)


def minimize(result):
    ''' The optimization makes the result as small as it can be: `minimize(mass_1)`.  At most one goal is given to an optimization (a trade-off is one formula: `minimize(mass_1 + 0.01 * compliance_1)`) '''
    return Minimize(result)


def maximize(result):
    ''' The optimization makes the result as large as it can be: `maximize(frequency_1)` '''
    return Maximize(result)


def optimization_condition(condition):
    ''' What an optimization is asked of, as ONE comparison of analysis results and numbers written whole: `optimization_condition(von_mises_1 <= 90)`, `optimization_condition(mass_1 / volume_1 < 5)`.
        It must have an analysis result in it (a condition that no analysis can change is refused).  keep_below, keep_above, keep_between, minimize and maximize are the same thing for the common
        cases, each with the logic inside.  Give the conditions to topology_optimization as `conditions=[...]` '''
    if isinstance(condition, OptimizationCondition):
        return condition
    if isinstance(condition, _ResultMath):
        raise TypeError('optimization_condition: a result is no condition by itself -- compare it (optimization_condition(%s <= 90)), or use keep_below, keep_above, keep_between, minimize or maximize'
                        % condition.text)
    if isinstance(condition, bool):
        raise TypeError('optimization_condition: this comparison has no analysis result in it, so it is %s whatever the design is -- a condition compares a result of an analysis: '
                        'optimization_condition(von_mises_1 <= 90)' % condition)
    raise TypeError('optimization_condition: give it a comparison of analysis results, for example optimization_condition(von_mises_1 <= 90) -- not %s' % type(condition).__name__)


# What each makes and takes, said next to it (see kinds.takes): an analysis result is made of an analysis (a simulation in the model tree) and is a model of its own kind, a result; a condition is made
# of an analysis result and is a condition of the domain of its analysis.  keep_below ... maximize say which input takes the result, so that the menu writes them from the result that is selected
from fieldes.kinds import takes as _takes_of, slot as _slot
for _fn in (keep_below, keep_above, keep_between, minimize, maximize):
    _fn._makes = 'conditions'
    _fn._takes = _takes_of({'result', 'simulation'}, [{'result', 'simulation'}])
    _fn._slots = [_slot('result', {'result', 'simulation'}, needed=True)]
optimization_condition._makes = 'conditions'
optimization_condition._takes = _takes_of({'result'}, [{'result'}])
