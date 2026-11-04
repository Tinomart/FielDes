'''
Checks of analysis results and optimization conditions (fieldes.stdlib.analysis_results) and of how the optimizer reads them (optimization_measures).  Library checks, no window; the analyses are
stand-ins that only say what physics they are (the numbers they report are not read here).
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import sys
import traceback

from fieldes import *
from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib import fea, fluid, thermal
from fieldes.stdlib import optimization_measures as measures
from fieldes.stdlib import analysis_results as rb

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def raises(fn, kind, word):
    try:
        fn()
    except kind as e:
        return word in str(e)
    except Exception:
        return False
    return False


class Static:
    _physics = 'static'

    def __init__(self):
        self.max_von_mises = 87.5
        self.compliance = 12.0
        self.frequencies = None


class Modal:
    _physics = 'modal'

    def __init__(self):
        self.frequencies = [101.0, 250.0]


class Flow:
    _physics = 'flow'

    def __init__(self):
        self.drag_force = 3.0
        self.speed = rb.result_field(self, 'speed', lib.libfive_tree_copy((Shape.X() * 2).ptr))            # (the velocity: a field)


# (the numbers an analysis gives are analysis results: the real result classes are made so too, see analysis_results.wrap_class)
for _cls in (Static, Modal, Flow):
    rb.wrap_class(_cls)


def analysis(i, result, kind):
    a = measures.Analysis(i, result, kind)
    a.supports, a.loads = [], []
    return a


def kinds_of_the_program():
    check('the real result classes say their physics', fea.Result._physics == 'static' and fea.TetResult._physics == 'static' and fea.ModalResult._physics == 'modal'
          and thermal.ThermalResult._physics == 'thermal' and fluid.FluidResult._physics == 'flow')
    check('the numbers an analysis offers are the ones the optimizer always had, in the same order',
          rb.offered('static') == ('compliance', 'max_von_mises', 'max_displacement', 'safety_factor', 'reaction', 'total_load') and rb.offered('modal') == ('frequencies',)
          and rb.offered('flow') == ('drag_force', 'lift_force') and rb.offered('thermal') == ('max_temperature', 'min_temperature', 'max_heat_flux', 'input_temperature'),
          (rb.offered('static'), rb.offered('flow')))
    check('the numbers of the design are volume, mass and volume_fraction', rb.design_numbers() == ('volume', 'mass', 'volume_fraction'))
    check('the velocity of a flow is a result (a field) but no number to optimize: it is not offered', 'speed' not in rb.offered('flow') and rb.kind_of_result('flow', 'speed') is not None)


def results():
    s = Static()
    b = rb.main_result(s)
    check('an analysis result without a kind is the main result of its analysis (the peak von Mises stress of a static one)', b.name == 'max_von_mises' and b.unit == 'MPa' and b.value == 87.5, repr(b))
    check('an analysis result carries its text for a formula: max_von_mises', b.text == 'max_von_mises')
    check('an analysis result is a field, one that works in every expression', isinstance(b, Shape) and isinstance(b * 2, Shape) and isinstance(b + Shape.X(), Shape))
    m = Modal().frequencies[1]
    check('the second frequency is frequencies[1], a value of the analysis', m.text == 'frequencies[1]' and m.value == 250.0 and m.label.endswith('2'), (m.text, m.value, m.label))
    check('a list result without an index is its first number', Modal().frequencies.text == 'frequencies[0]')
    f = Flow().speed
    check('a field-only result (the velocity of a flow) is a field that knows what it is', isinstance(f, Shape) and f.field is f and f.unit == 'mm/s', repr(f))
    check('a design number is an analysis result of any analysis', s.mass.name == 'mass' and Flow().volume_fraction.name == 'volume_fraction')
    g = b * 3 + 1
    check('arithmetic on results is a field that remembers which results it is made of and its text', isinstance(g, Shape) and g.results == (b,) and g.text == '((max_von_mises * 3.0) + 1.0)', g.text)
    check('... also with a number on the left and with two results', (2 - b).text == '(2.0 - max_von_mises)' and (s.compliance / b).text == '(compliance / max_von_mises)')


def conditions():
    s = Static()
    b = rb.main_result(s)
    c = keep_below(b, 90)
    check('keep_below keeps the result at most a number, and says it as the text the optimizer reads', c.text == 'max_von_mises <= 90.0' and not c.is_goal and isinstance(c, rb.OptimizationCondition), c.text)
    check('keep_above and keep_between say theirs', keep_above(b, 5).text == 'max_von_mises >= 5.0' and keep_between(b, 10, 90).text == '10.0 <= max_von_mises <= 90.0')
    check('keep_between with both limits the same number keeps the exact value', keep_between(b, 5, 5).text == 'max_von_mises == 5.0')
    check('a comparison of results is the condition it says, whichever side the number is on', (b <= 90).text == c.text and (90 >= b).text == c.text and (b > 5).text == 'max_von_mises > 5.0')
    check('a comparison of a formula of results is a condition made of those results', ((b * 2 + 1) < 90).text == '((max_von_mises * 2.0) + 1.0) < 90.0' and ((b * 2) < 90).results == (b,))
    check('optimization_condition takes the one comparison written whole', optimization_condition(b <= 90).text == c.text)
    g = minimize(s.mass)
    check('a goal has no limit and its text is the formula that is made best', g.is_goal and g.text == 'mass' and g.how == 'minimize' and maximize(b).how == 'maximize')
    check('a goal can be a formula of results', minimize(s.mass + 0.01 * s.compliance).text == '(mass + (0.01 * compliance))')
    check('a condition knows its analysis and its first result', c.analysis is s and c.result is b)
    check('an analysis stands for its main result: the peak stress of a static one, the first frequency of a modal one', keep_below(s, 90).text == 'max_von_mises <= 90.0' and minimize(Modal()).text == 'frequencies[0]')
    check('the children are conditions: keep_below, keep_above, keep_between, minimize, maximize',
          all(isinstance(k, rb.OptimizationCondition) for k in (c, keep_above(b, 1), keep_between(b, 1, 2), g, maximize(b))))
    check('a condition with no result in it is refused, in plain words', raises(lambda: optimization_condition(5 < 3), TypeError, 'no analysis result'))
    check('a bare result is no condition: the error says what to write', raises(lambda: optimization_condition(b), TypeError, 'compare it'))
    check('a text is no condition', raises(lambda: optimization_condition('max_von_mises <= 90'), TypeError, 'comparison of analysis results'))
    check('a limit that is not a number is an error', raises(lambda: keep_below(b, 'ninety'), ValueError, 'number'))
    check('a lower limit above the upper one is an error', raises(lambda: keep_between(b, 90, 10), ValueError, 'must not be above'))
    check('a field-only result cannot be asked about, and the error names what can', raises(lambda: keep_below(Flow().speed, 5), ValueError, 'drag_force'))
    check('a plain field is no result: the error says where results come from', raises(lambda: keep_below(Shape.X(), 3), TypeError, 'result of an analysis'))
    fld = rb.AnalysisResult(s, rb._kind_of_field(s, 'displacement'), field=Shape.X())
    check('a chain such as 10 < r < 90 of a field is refused and points to keep_between', raises(lambda: (10 < fld < 90), TypeError, 'keep_between'))
    check('`and` between two conditions of a field is refused too: the first would be dropped without a word', raises(lambda: (fld <= 90 and fld >= 10), TypeError, 'keep_between'))
    check('... on the numbers of an analysis it is just true or false (H91): the peak is 87.5', bool(10 < b < 90) and not bool(b > 90) and bool(b <= 90))
    other = rb.main_result(Static())
    two = keep_below(b + other, 5)
    check('a condition may be made of the results of several analyses (load cases): each name says which analysis it is read in', len(two.analyses) == 2 and '__a0' in two.text and '__a1' in two.text, two.text)
    check('... the one made of a single analysis has plain names', '__' not in c.text)


def optimizer():
    a0, a1 = analysis(0, Static(), 'static'), analysis(1, Static(), 'static')
    base = lambda node: 1.0
    check('a text is not read: the optimizer is given conditions only, and the error says what they are', raises(lambda: measures.outputs_of(['max_von_mises <= 90'], [a0, a1], base), measures.MeasureError, 'optimization condition'))
    check('... nor a function', raises(lambda: measures.outputs_of([lambda r: r.compliance], [a0, a1], base), measures.MeasureError, 'optimization condition'))
    blk = keep_below(rb.main_result(a1.result), 90)
    out, goal = measures.outputs_of([blk], [a0, a1], base)
    check('a condition is kept in the analysis of its results only', len(out) == 2 and out[1].case == 1 and goal == 'satisfy', [o.case for o in out])
    check('... and it reads the measure of that analysis', measures.leaves(out[1].node) == [('max_von_mises', 1)], measures.leaves(out[1].node))
    mass = minimize(a0.result.compliance)
    out, goal = measures.outputs_of([mass, blk], [a0, a1], base)
    check('a goal is the objective, read in its own analysis', goal == 'minimize' and measures.leaves(out[0].node) == [('compliance', 0)] and len(out) == 2, measures.leaves(out[0].node))
    maxi = maximize(a1.result.compliance)
    out, goal = measures.outputs_of([maxi], [a0, a1], base)
    check('maximize makes the objective the negative', goal == 'maximize' and measures.leaves(out[0].node) == [('compliance', 1)])
    check('a single condition, not in a list, is read as a list of one', measures.outputs_of(blk, [a0, a1], base)[1] == 'satisfy')
    check('two goals are an error', raises(lambda: measures.outputs_of([mass, maxi], [a0, a1], base), measures.MeasureError, 'one thing'))
    stranger = keep_below(rb.main_result(Static()), 90)
    check('a condition made of an analysis the optimization is not given is an error that says so', raises(lambda: measures.outputs_of([stranger], [a0, a1], base), measures.MeasureError, 'not given'))
    f0 = analysis(0, Flow(), 'flow')
    out, _ = measures.outputs_of([keep_below(f0.result.drag_force, 5)], [f0], base)
    check('a flow is read by the same code: the drag force is one measure of the flow', measures.leaves(out[1].node) == [('drag_force', 0)], measures.leaves(out[1].node))
    formula = keep_below(a0.result.max_von_mises * 2 + 1, 90)
    out, _ = measures.outputs_of([formula], [a0], base)
    check('a formula of results is read as the tree of its measures', measures.leaves(out[1].node) == [('max_von_mises', 0)] and len(out) == 2, measures.leaves(out[1].node))
    out, _ = measures.outputs_of([keep_between(a0.result.compliance, 5, 5)], [a0], base)
    check('an exact value is kept from both sides: two limits', len(out) == 3, [o.text for o in out])
    out, _ = measures.outputs_of([keep_between(a0.result.compliance, 5, 9)], [a0], base)
    check('a range is two limits', len(out) == 3, [o.text for o in out])
    both = keep_below(a0.result.max_von_mises + a1.result.max_von_mises, 100)
    out, _ = measures.outputs_of([both], [a0, a1], base)
    check('load cases: a condition made of the results of two analyses reads each in its own, as one condition', len(out) == 2 and measures.leaves(out[1].node) == [('max_von_mises', 0), ('max_von_mises', 1)], measures.leaves(out[1].node))
    check('... and says so when it shows it', '@1' in out[1].text and '@2' in out[1].text, out[1].text)
    total = minimize(a0.result.compliance + a1.result.compliance)
    out, goal = measures.outputs_of([total], [a0, a1], base)
    check('load cases: the goal is the sum of the two compliances, written out', goal == 'minimize' and measures.leaves(out[0].node) == [('compliance', 0), ('compliance', 1)], measures.leaves(out[0].node))
    rb.register_result_kind('static', 'strain_energy', 'Strain energy', 'N mm')
    try:
        out, _ = measures.outputs_of([keep_below(rb.AnalysisResult(a0.result, rb.kind_of_result('static', 'strain_energy')), 1)], [a0], base)
        check('a new kind of result is one row in the table: the optimizer reads it without any other change', measures.leaves(out[1].node) == [('strain_energy', 0)], measures.leaves(out[1].node))
    finally:
        rb._KINDS['static'].pop('strain_energy', None)


def places():
    # A result read at a place (.at(x, y, z)) is one number of the analysis: a condition is made of it, and the optimizer reads it in the analysis it is of
    from fieldes.stdlib import parametric
    s = Static()
    u = rb.AnalysisResult(s, rb._kind_of_field(s, 'uz'), field=Shape.X() * 0.01)
    p = u.at(100, 0, 0)
    check('a field result read at a place is a result of its own: uz(100.0, 0.0, 0.0)', p.text == 'uz(100.0, 0.0, 0.0)' and p.place == (100.0, 0.0, 0.0) and isinstance(p, Shape), p.text)
    check('... its number is the field there', abs(p.value - 1.0) < 1e-9, p.value)
    check('... and it is arithmetic like any result: it remembers it is made of this result', (p * 2).text == '(uz(100.0, 0.0, 0.0) * 2.0)' and (p * 2).results == (p,), (p * 2).text)
    c = keep_below(p, 0.5)
    check('a condition is made of it, and says it as the text the optimizer reads', c.text == 'uz(100.0, 0.0, 0.0) <= 0.5' and c.analysis is s and not c.is_goal, c.text)
    check('the displacement (its peak is a result) and the temperature are read at a place as well',
          rb.AnalysisResult(s, rb._kind_of_field(s, 'displacement'), field=Shape.X()).at(0, 0, 0).text == 'displacement(0.0, 0.0, 0.0)')
    check('a result that is not a field read at a place is refused in plain words', raises(lambda: s.compliance.at(0, 0, 0), ValueError, 'not read at a place')
          and raises(lambda: s.max_von_mises.at(0, 0, 0), ValueError, 'not read at a place'))
    check('... and a place that is not three numbers', raises(lambda: u.at(1, 'a', 3), ValueError, 'three numbers'))
    a0, a1 = analysis(0, Static(), 'static'), analysis(1, Static(), 'static')
    base = lambda node: 1.0
    u0 = rb.AnalysisResult(a0.result, rb._kind_of_field(a0.result, 'uz'), field=Shape.X())
    u1 = rb.AnalysisResult(a1.result, rb._kind_of_field(a1.result, 'uz'), field=Shape.X())
    out, _ = measures.outputs_of([keep_below(u1.at(100, 0, 0), 0.5)], [a0, a1], base)
    check('the optimizer reads it as the displacement at that place, in the analysis it is of',
          measures.leaves(out[1].node) == [('displacement_at', 1, 2, 100.0, 0.0, 0.0)] and out[1].case == 1, measures.leaves(out[1].node))
    out, _ = measures.outputs_of([keep_below(u0.at(10, 0, 0) + u1.at(20, 0, 0), 1)], [a0, a1], base)
    check('load cases: a place in each of two analyses is read in its own',
          measures.leaves(out[1].node) == [('displacement_at', 0, 2, 10.0, 0.0, 0.0), ('displacement_at', 1, 2, 20.0, 0.0, 0.0)], measures.leaves(out[1].node))

    class Thermal:
        _physics = 'thermal'
        max_temperature = 80.0
    ta = analysis(0, Thermal(), 'thermal')
    temp = rb.AnalysisResult(ta.result, rb._kind_of_field(ta.result, 'temperature'), field=Shape.X())
    out, _ = measures.outputs_of([keep_below(temp.at(1, 2, 3), 60)], [ta], base)
    check('the temperature of a thermal analysis is read at a place', temp.at(1, 2, 3).text == 'temperature(1.0, 2.0, 3.0)' and measures.leaves(out[1].node) == [('temperature_at', 0, 1.0, 2.0, 3.0)],
          measures.leaves(out[1].node))

    class Beam:
        uz = staticmethod(lambda x, y, z: 0.3 * x / 100.0)
    measure, text = parametric._measure('uz(100.0, 0.0, 0.0)', 'goal')
    check('the parametric optimizer reads the text of a result at a place: the field of its result, asked there', abs(measure(Beam()) - 0.3) < 1e-12 and text == 'uz(100.0, 0.0, 0.0)', text)


def menu():
    import json
    from fieldes import menu_catalog as mc

    def write(name, **kw):
        r = dict(kind='operation', name=name, x=0, y=0, z=0, scale=10, indent=0, var=name + '_1', body='von_mises_1', body_info={'type': 'result'}, others=[])
        r.update(kw)
        return ' '.join(mc.call(json.dumps(r)).split())
    names = ('keep_below', 'keep_above', 'keep_between', 'minimize', 'maximize', 'optimization_condition')
    entries = {n: (g, o) for n, g, _t, o in mc.OPERATIONS if n in names}
    check('menu: the analysis result is no entry of any menu (the user, 2026-11-02: a class internal to the simulations, not something to create)', 'analysis_result' not in {n for n, _g, _t, _o in mc.OPERATIONS})
    check('menu: the six optimization condition entries are in the group of the simulations', set(entries) == set(names) and all(g == 'Simulations' for g, _o in entries.values()))
    catalog = json.loads(mc.catalog())['operations']
    mine = {o['name']: o for o in catalog if o['name'] in names}
    check('menu: all six are in a section of their own, in the domain Optimization, each with a tooltip',
          set(mine) == set(names) and all(o.get('submenu') and o.get('domain') == 'optimization' and o.get('hint') for o in mine.values()), sorted(mine))
    check('menu: keep below, keep above and keep between are written for the result that is selected, the limits are placeholders',
          write('keep_below').startswith('keep_below( von_mises_1, limit=...') and write('keep_above').startswith('keep_above( von_mises_1, limit=...')
          and 'lower=...' in write('keep_between') and 'upper=...' in write('keep_between'), write('keep_between'))
    check('menu: minimize and maximize need nothing more', write('minimize') == 'minimize(von_mises_1)' and write('maximize') == 'maximize(von_mises_1)', write('minimize'))
    check('menu: the generic condition is a comparison with a placeholder for the limit', write('optimization_condition') == 'optimization_condition(von_mises_1 <= ...)', write('optimization_condition'))
    check('menu: the entries fit a result and not a solid', all(mc._selection_fits('operation', n, [{'type': 'result'}]) and not mc._selection_fits('operation', n, [{'type': 'solid'}]) for n in names))
    check('menu: keep below ... maximize fit an analysis too (it stands for its main result), the comparison written whole does not', all(mc._selection_fits('operation', n, [{'type': 'simulation'}]) for n in names[:5])
          and not mc._selection_fits('operation', 'optimization_condition', [{'type': 'simulation'}]))


def tree_rows():
    # An analysis result and an optimization condition are rows of the model tree with a kind of their own (F35b): the analysis result a result, the condition a condition; both in the colour of the domain
    # of their analysis; each says what it makes and what it takes, so the right-click menus offer them for the models they are made of
    from fieldes import kinds

    class Structural(Static):
        _engineering_domain = 'structural'
    b = rb.main_result(Structural())
    c = keep_below(b, 90)
    check('an analysis result is a model of the kind result', kinds.kind_of(b) == 'result' and 'result' in kinds.KINDS, kinds.kind_of(b))
    check('a formula of results is a field to work with', kinds.kind_of(b * 2) == 'field', kinds.kind_of(b * 2))
    check('every optimization condition is a model of the kind conditions',
          all(kinds.kind_of(k) == 'conditions' for k in (c, keep_above(b, 1), keep_between(b, 1, 2), minimize(b), maximize(b), b <= 5, optimization_condition(b <= 5))))
    check('both are of the domain of their analysis (the tree draws them in its colour)', kinds.domain_of_value(b) == 'structural' and kinds.domain_of_value(c) == 'structural',
          (kinds.domain_of_value(b), kinds.domain_of_value(c)))
    thermal_one = type('T', (Static,), {'_physics': 'static', '_engineering_domain': 'thermal'})()
    check('the optimization condition of an analysis result of a thermal analysis is a thermal one', kinds.domain_of_value(minimize(rb.main_result(thermal_one))) == 'thermal')
    names = ('keep_below', 'keep_above', 'keep_between', 'minimize', 'maximize', 'optimization_condition')
    check('they say what they make: every optimization condition a condition', all(getattr(rb, n)._makes == 'conditions' for n in names))
    check('every optimization condition is offered for an analysis result', all(getattr(rb, n)._takes[0][0] >= {'result'} for n in names))
    check('the function analysis_result is gone: a result is an entry of its analysis (H91)', not hasattr(rb, 'analysis_result') and 'analysis_result' not in rb.__all__)
    check('keep_below ... maximize say which input takes the result', all(getattr(rb, n)._slots[0].param == 'result' for n in names[:5]))
    role = fea._slot_of(c)
    check('an optimization condition has the role of the conditions of a topology optimization: the tree lets it be dropped there (F35b)', role == 'conditions', role)
    held = [k.__name__ for k in fea._SLOTS['conditions'][0]]
    check('... and the manufacturing constraints have theirs, apart', 'OptimizationCondition' in held and 'OptimizationCondition' not in [k.__name__ for k in fea._SLOTS.get('constraints', ((),))[0]], held)


def numbers_are_results():
    # The numbers an analysis gives are analysis results, as its fields are (the user, 2026-11-03, H91): `stress.compliance`, no function that takes the analysis
    s, m = Static(), Modal()
    c = s.compliance
    check('a number an analysis gives is an analysis result of its analysis', isinstance(c, rb.AnalysisResult) and isinstance(c, Shape) and c.analysis is s and c.name == 'compliance', repr(c))
    check('... that reads as its number: float, int, format, %, print and round', float(c) == 12.0 and int(c) == 12 and '%.1f' % c == '12.0' and format(c, '.2f') == '12.00' and str(c) == '12' and round(c) == 12,
          (str(c), '%.1f' % c))
    check('... and as a field it works in every expression', isinstance(c * 2, Shape) and (c * 2).text == '(compliance * 2.0)')
    check('arithmetic on two numbers knows its number: the sum is one number too', float(c + s.max_von_mises) == 99.5 and float(c * 2) == 24.0 and float(2 - c) == -10.0, float(c + s.max_von_mises))
    check('a comparison of numbers is a condition for an optimization and true or false in a script', (c < 20).text == 'compliance < 20.0' and bool(c < 20) and not bool(c > 20))
    f = m.frequencies
    check('a list of numbers is a result that is the first of them, and [i] is the i-th as a result', isinstance(f, rb.ResultSequence) and len(f) == 2 and float(f) == 101.0 and float(f[1]) == 250.0
          and f[1].text == 'frequencies[1]' and [float(x) for x in f] == [101.0, 250.0] and float(f[-1]) == 250.0, repr(f))
    check('... and it is refused where it is read past its end', raises(lambda: f[2], IndexError, 'has 2 numbers'))
    check('the volume, the mass and the volume fraction of the design are results of every analysis', all(isinstance(getattr(s, n), rb.AnalysisResult) and getattr(s, n).kind.physics == 'design' for n in ('volume', 'mass', 'volume_fraction')))
    check('the field of a flow has no one number: float() says so', raises(lambda: float(Flow().speed), TypeError, 'not one number'))
    check('the main result of an analysis is the entry that is its main result', rb.main_result(s) is s.max_von_mises and rb.main_result(m).text == 'frequencies[0]')
    check('a condition is made of such a result with the same text the optimizer has always read', keep_below(s.max_von_mises, 90).text == 'max_von_mises <= 90.0' and minimize(s.mass).text == 'mass')


for name, fn in (('numbers are results', numbers_are_results), ('kinds', kinds_of_the_program), ('results', results), ('conditions', conditions), ('optimizer', optimizer), ('places', places), ('menu', menu), ('tree rows', tree_rows)):
    try:
        fn()
    except Exception:
        traceback.print_exc()
        bad.append(name + ' raised')
print('=' * 70)
print('ALL CHECKS PASSED' if not bad else '%d check(s) failed %s' % (len(bad), bad))
sys.exit(1 if bad else 0)
