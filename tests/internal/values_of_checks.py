'''
Checks of curves.values_of (the numbers of many fields worked out together, every distinct node once: libfive_tree_eval_many) against value_of (one evaluator for each): the same numbers, for numbers, for expressions
that share nodes, and for a tree the kernel does not read directly (it is read the old way).  Library checks, no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import sys
import time

from fieldes import *
from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib import curves as C

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def close(a, b, tol=1e-4):
    return all(abs(x - y) <= tol * max(1.0, abs(y)) for x, y in zip(a, b)) and len(a) == len(b)


a, b, c = Shape.wrap(3.0), Shape.wrap(4.0), Shape.wrap(-2.5)
mixed = [a, a + b, (a * a + b * b).sqrt(), (a - c) * (b + c) / (a + 1.0), a.min(b), a.max(c), a * -1.0, 7, 2.5, b.square()]
expected = [3.0, 7.0, 5.0, (3.0 + 2.5) * (4.0 - 2.5) / 4.0, 3.0, 3.0, -3.0, 7.0, 2.5, 16.0]
got = C.values_of(mixed)
check('numbers, fields and expressions, together', close(got, expected), got)
check('the same numbers as value_of, one at a time', close(got, [C.value_of(x) for x in mixed]))
check('an empty list is an empty list', C.values_of([]) == [])
check('plain numbers only', C.values_of([1, 2.5, -3]) == [1.0, 2.5, -3.0])

# expressions made of one another, as the control points of a body of points are: each shares everything the one before it is made of
chain, s = [], a
for k in range(400):
    s = s * 1.001 + b * 0.5
    chain.append(s)
started = time.perf_counter()
together = C.values_of(chain)
t_together = time.perf_counter() - started
started = time.perf_counter()
one_by_one = [C.value_of(x) for x in chain]
t_one = time.perf_counter() - started
check('400 expressions that share their nodes: the same numbers as one evaluator each', close(together, one_by_one, 1e-3), 'together %.1f ms, one by one %.1f ms' % (1000 * t_together, 1000 * t_one))
if hasattr(lib, 'libfive_tree_eval_many'):
    check('the kernel has libfive_tree_eval_many and it is the faster of the two', t_together < t_one, 'together %.1f ms, one by one %.1f ms' % (1000 * t_together, 1000 * t_one))
else:
    print('NOTE  this library has no libfive_tree_eval_many: values_of reads the numbers one at a time')

# a tree the kernel does not read directly (the coordinate x is no operation of the direct reading): read the old way, at (0, 0, 0)
check('a tree with the coordinate x is read the old way', close(C.values_of([Shape.X() + 1.0, a, Shape.X() * 2.0 + b]), [1.0, 3.0, 4.0]))

if bad:
    print('FAILED: ' + ', '.join(bad))
    sys.exit(1)
print('ALL CHECKS PASSED')