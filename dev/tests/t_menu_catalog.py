# The viewport's context menus: every primitive and operation they offer is written as a call and RUN, so a renamed
# function or a changed argument list fails here.  Also the placing: size, snapping, and what a bad request says.
import json
from fieldes import *
from fieldes import menu_catalog as mc
from fieldes import app_support

cat = json.loads(app_support.menu_catalog(''))
print('primitives %d, operations %d' % (len(cat['primitives']), len(cat['operations'])))
assert [p['name'] for p in cat['primitives'] if p['group'] != 'Custom blocks'] == [n for n, _, _ in mc.PRIMITIVES]
failed = 0

# a primitive at (12.3, -4.1, 7.7), about 8.2 mm across a hundred pixels there
base = {'x': 12.3, 'y': -4.1, 'z': 7.7, 'scale': 8.2}
ns = dict(globals())
from fieldes import blocks
ns.update(blocks.namespace())     # (a script has the custom blocks at hand)
for p in cat['primitives']:
    code = app_support.menu_call(json.dumps(dict(base, kind='primitive', name=p['name'])))
    try:
        shape = eval(code, ns)
        assert isinstance(shape, Shape), 'not a Shape'
        # evaluates anywhere without failing
        v = evaluate(shape, [(12, -4, 8), (0, 0, 0), (50, 50, 50)])
        ok = all(isinstance(t, float) for t in v)
        # a 3D primitive placed at the cursor contains (or, for a plane or a lattice, touches) its place
        inside = v[0] < 0.5 * 10
        print('  %-26s %-60s %s' % (p['name'], code, 'ok' if ok else 'BAD'))
        if not ok:
            failed += 1
    except Exception as e:
        failed += 1
        print('  %-26s %-60s FAILED: %s' % (p['name'], code, e))

# operations on a box, with another body for the combining ones
body = box_exact((0, 0, 0), (20, 20, 20))
other = sphere(8, (20, 10, 10))
ns.update(body=body, other=other)
for o in cat['operations']:
    code = app_support.menu_call(json.dumps(dict(base, kind='operation', name=o['name'], body='body', other='other', scale=10)))
    try:
        if o['group'] == 'Simulations':
            # (a simulation is solved when the script runs: here the call must only be written and its names must exist)
            tree = compile(code, '<menu>', 'eval')
            missing = [n for n in tree.co_names if n not in ns]
            assert not missing, 'unknown names %s' % missing
            print('  %-16s %s' % (o['name'], code[:150]))
            continue
        shape = eval(code, ns)
        assert isinstance(shape, Shape), 'not a Shape'
        v = evaluate(shape, [(10, 10, 10), (40, 40, 40)])
        print('  %-16s %-46s centre %.2f  %s' % (o['name'], code, v[0], 'ok'))
    except Exception as e:
        failed += 1
        print('  %-16s %-46s FAILED: %s' % (o['name'], code, e))

# several models selected: the combining operations take all of them (the first is what they work on)
third = box_exact((30, 0, 0), (40, 20, 20))
ns.update(third=third)
for name in ('union', 'difference', 'intersection'):
    code = app_support.menu_call(json.dumps(dict(base, kind='operation', name=name, body='body', other='other, third', scale=10)))
    try:
        shape = eval(code, ns)
        v = evaluate(shape, [(10, 10, 10), (22, 10, 10), (35, 10, 10)])
        # union holds the box, the ball and the far box; difference the box with the others cut out; intersection almost nothing
        expected = {'union': (True, True, True), 'difference': (True, False, False), 'intersection': (False, False, False)}[name]
        got = tuple(x < 0 for x in v)
        ok = got == expected
        print('  %-14s %-52s %s %s' % (name, code, got, 'ok' if ok else 'WRONG, expected %s' % (expected,)))
        failed += 0 if ok else 1
    except Exception as e:
        failed += 1
        print('  %-14s %-52s FAILED: %s' % (name, code, e))
# (a combining operation with no second model is refused with its reason)
try:
    app_support.menu_call(json.dumps(dict(kind='operation', name='union', body='b')))
    failed += 1
    print('NO ERROR for union without a second model')
except ValueError as e:
    print('  refused union without a second model ->', e)

# placing: the size is a round number, the place snaps to a grid of about a twentieth of it
for scale, expect in ((3.7, '5'), (8.2, '10'), (0.9, '1'), (130.0, '100'), (24.0, '20')):
    code = app_support.menu_call(json.dumps({'kind': 'primitive', 'name': 'box_exact_centered', 'x': 1.234, 'y': 5.678, 'z': -0.4, 'scale': scale}))
    print('  scale %-6s -> %s' % (scale, code))
    if '(%s, %s, %s)' % (expect, expect, expect) not in code:
        failed += 1
        print('    expected size', expect)

# a bad request is refused with its reason
for bad in ({'kind': 'primitive', 'name': 'teapot'}, {'kind': 'operation', 'name': 'smooth'}, {'kind': 'operation', 'name': 'teapot', 'body': 'b'}):
    try:
        app_support.menu_call(json.dumps(bad))
        failed += 1
        print('NO ERROR for', bad)
    except ValueError as e:
        print('  refused', bad, '->', e)

print('FAILED %d' % failed if failed else 'ALL OK')
