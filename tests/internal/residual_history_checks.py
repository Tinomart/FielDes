'''
Checks of the residual history of a static analysis (the tester of version 1.2.0: "residual plot missing"): the solver keeps the residual after every iteration, the result has it as `residual_history` (at
most 400 numbers, the first is the start, the last the residual the solve stopped at), and the model that the result is shown as carries it for the curve on the result card.  Library checks, no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import sys
import traceback

from fieldes import *

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def main():
    beam = box((40, 10, 10), position=(0, 0, 0))
    base = box((4, 14, 14), position=(-20, 0, 0))
    tip = box((4, 14, 14), position=(20, 0, 0))
    for element in ('tet', 'tet10'):
        r = static_analysis(beam, [fixed(base)], [force(tip, (0, 0, -100))], steel, element_size=3.0, element=element)
        h = r.residual_history
        check('%s: the residual after every iteration is kept (%d numbers for %d iterations)' % (element, len(h), r.iterations), 2 <= len(h) <= 400 and len(h) >= min(r.iterations, 400) - 1, (len(h), r.iterations))
        check('%s: it starts above where it stops, and stops at the residual of the result' % element, h[0] > h[-1] and abs(h[-1] - float(r.residual)) <= 1e-6 * max(abs(h[-1]), 1e-30) + 1e-9, (h[0], h[-1], float(r.residual)))
        shown = r._display()
        shown = shown[0] if isinstance(shown, (list, tuple)) else shown
        check('%s: the model the result is shown as carries it for the result card' % element, list(getattr(shown, '_residual_history', [])) == list(h))


try:
    main()
except Exception:
    traceback.print_exc()
    bad.append('an error')
print('%d failed' % len(bad))
sys.exit(1 if bad else 0)
