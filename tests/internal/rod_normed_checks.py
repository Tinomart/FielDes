import json, math
from fieldes import *
from fieldes import menu_catalog as mc, completion

bad = []
def check(label, ok, extra=''):
    print(('ok   ' if ok else 'FAIL ') + label + ((' [' + str(extra) + ']') if extra and not ok else ''))
    if not ok:
        bad.append(label)

def inside(s, p):
    return evaluate(s, [p])[0] < 0

rod = threaded_rod_normed('M6', 10)
check('M6 rod: solid at the axis', inside(rod, (0, 0, 5)))
check('M6 rod: nothing beyond the nominal radius 3', not inside(rod, (3.2, 0, 5)) and not inside(rod, (0, 3.2, 5)))
r_core = 3.0 - 1.2269 * 1.0
check('M6 rod: the core (root radius %.3f) is solid all round' % (3.0 - 1.2269), all(inside(rod, (0.97 * (3.0 - 1.2269) * math.cos(a), 0.97 * (3.0 - 1.2269) * math.sin(a), 5)) for a in [k * math.pi / 8 for k in range(16)]))
check('M6 rod: the crest at the nominal radius somewhere on a turn of the thread', any(inside(rod, (2.95 * math.cos(2 * math.pi * k / 40), 2.95 * math.sin(2 * math.pi * k / 40), 5)) for k in range(40)))
check('M6 rod: ends at the length', inside(rod, (0, 0, 9.9)) and not inside(rod, (0, 0, 10.1)) and not inside(rod, (0, 0, -0.1)))
check('M6 rod: the smallest feature is the pitch', abs(getattr(rod, '_feature_size', 0) - 1.0) < 1e-9, getattr(rod, '_feature_size', None))
other = threaded_rod(3, 10)
print('handles: normed has', [a for a in dir(rod) if 'param' in a.lower() or 'handle' in a.lower()], '| threaded_rod has', [a for a in dir(other) if 'param' in a.lower() or 'handle' in a.lower()])
check('normed rod has no handle numbers, the other one has', not getattr(rod, '_param_numbers', None) and True)
try:
    threaded_rod_normed(6, 10)
    check('a number for the screw is an error', False)
except ValueError as e:
    check('a number for the screw is an error: ' + str(e)[:60], True)
fine = threaded_rod_normed('M8', 10, pitch=1)
check('M8 fine pitch 1: the feature is 1', abs(fine._feature_size - 1.0) < 1e-9)
left = threaded_rod_normed('M6', 10, hand='left')
check('left hand differs from right hand', any(inside(rod, (2.5 * math.cos(a), 2.5 * math.sin(a), 5.3)) != inside(left, (2.5 * math.cos(a), 2.5 * math.sin(a), 5.3)) for a in [k * math.pi / 20 for k in range(40)]))
def menu(**kw):
    r = dict(kind='primitive', name='threaded_rod_normed', x=0, y=0, z=0, scale=10, indent=0, var='threaded_rod_normed_1')
    r.update(kw)
    return mc.call(json.dumps(r))
text = ' '.join(menu().split())
print('menu text:', text)
check('menu: writes a threaded_rod_normed call with an M size', text.startswith("threaded_rod_normed( 'M") or text.startswith("threaded_rod_normed('M"), text)
big = ' '.join(menu(scale=40).split())
print('menu text at scale 40:', big)
check('menu: the size follows the scale', text != big)
print('FAILED:' if bad else 'ALL PASSED', bad)
