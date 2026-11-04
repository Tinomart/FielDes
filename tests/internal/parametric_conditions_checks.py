'''
Checks of PC1: parametric_optimization reads optimization conditions (made from the results of the analysis, see analysis_results) the way topology_optimization does -- a goal as the goal minimize /
maximize of its formula, a comparison as its text -- and nothing else: a text or a function is refused.  Library checks, no window.  Run with FIELDES_DIR set to the folder of the program and
PYTHONPATH to python/.
'''
import sys

from fieldes import *
from fieldes.stdlib import analysis_results as ar
from fieldes.stdlib import parametric

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
        self.mass = 0.4


ar.wrap_class(Static)


analysis = Static()
mass = analysis.mass
stress = analysis.max_von_mises
goal, most, limit, window = minimize(mass), maximize(mass), keep_below(stress, 90), keep_between(stress, 10, 90)
items = parametric._conditions_as_items([goal, limit])
check('the conditions are read as they are: a goal and a comparison', items == [goal, limit] and items[0].is_goal and items[0].how == 'minimize' and items[0].text == 'mass', items)
check('a maximize condition is the goal maximize', parametric._conditions_as_items(most)[0].how == 'maximize')
check('a single condition, not in a list, is a list of one', parametric._conditions_as_items(limit) == [limit])
check('nothing is nothing', parametric._conditions_as_items(()) == [] and parametric._conditions_as_items(None) == [])
check('a text is refused, and the error says what is read', raises(lambda: parametric._conditions_as_items(['max_von_mises <= 90']), parametric.ParametricError, 'optimization condition'))
check('a function is refused', raises(lambda: parametric._conditions_as_items([lambda r: r.mass]), parametric.ParametricError, 'optimization condition'))
read = parametric._constraints_of(limit.text)
check('the text of a comparison is read as the comparison it is: one constraint', len(read) == 1, read)
check('a range is one constraint for each limit', len(parametric._constraints_of(window.text)) == 2, window.text)
check('a formula of results is the text of its measures', parametric._constraints_of(keep_below(mass * 2 + stress, 5).text) != [])
print('=' * 70)
print('FAILED: ' + ', '.join(bad) if bad else 'ALL CHECKS PASSED')
sys.exit(1 if bad else 0)
