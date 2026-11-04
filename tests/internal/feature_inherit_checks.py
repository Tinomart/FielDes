'''
Checks that a model made of a threaded rod is meshed finely enough to show the thread (the user, 2026-10-28: a difference of a threaded rod and a cylinder had a torn, lumpy rim -- the mesher
took the resolution the cylinder asks for, not the one the turns of the thread need).  The smallest feature of the rod (`_feature_size`, one pitch) is noted on what is made of it (made_from.note),
and the viewport meshes at ten samples to it (Shape::autoResolution).  Library checks, no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import ast
import sys

from fieldes import *
from fieldes import made_from

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def run(source):
    ''' The statements of a script, one by one, as the application runs them: made_from.reads before, made_from.note after '''
    ns = {}
    exec('from fieldes import *', ns)
    for stmt in ast.parse(source).body:
        before = made_from.reads(stmt, ns)
        exec(compile(ast.Module(body=[stmt], type_ignores=[]), '<check>', 'exec'), ns)
        made_from.note(stmt, ns, before)
    return ns


ns = run('''
rod_1 = threaded_rod(1, 10)
cylinder_1 = cylinder(5, 8)
difference_1 = difference(cylinder_1, rod_1)
cylinder_2 = cylinder(2, 3)
difference_2 = difference(difference_1, cylinder_2)
plain = difference(cylinder(5, 8), cylinder(2, 3))
big_rod = threaded_rod(4, 20)
union_1 = union(cylinder_1, big_rod)
''')
pitch = ns['rod_1']._feature_size
check('a threaded rod of radius 1 knows its pitch (the proportion of an ISO thread, 0.3125 radii)', abs(pitch - 0.3125) < 1e-9, pitch)
check('a difference of a cylinder and the rod has the pitch of the thread as its smallest feature', abs(getattr(ns['difference_1'], '_feature_size', 0) - pitch) < 1e-9, getattr(ns['difference_1'], '_feature_size', None))
check('... and so has what is made of that (the feature goes on through the chain of operations)', abs(getattr(ns['difference_2'], '_feature_size', 0) - pitch) < 1e-9, getattr(ns['difference_2'], '_feature_size', None))
check('a difference of two plain cylinders says no feature (it is meshed at the resolution of the scene, as before)', getattr(ns['plain'], '_feature_size', None) is None)
check('a union with a coarser rod has the finer of the pitches (the rod of radius 4 has 1.25, the rod of radius 1 is not in it)', abs(getattr(ns['union_1'], '_feature_size', 0) - 1.25) < 1e-9, getattr(ns['union_1'], '_feature_size', None))
print('FAILED:' if bad else 'ALL CHECKS PASSED', bad)
sys.exit(1 if bad else 0)
