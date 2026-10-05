# Lock is a switch of its own: `x = lock(x)` locks a shape and keeps its gizmo mode (click, never or always);
# handles() has those three modes only (the old 'gizmo' and 'handles' are refused).  Also what the model tree sees of it
# (app_support.scene_json).
import ast
import json
from fieldes import *
from fieldes import app_support

failed = 0


def check(what, ok):
    global failed
    print('  %-70s %s' % (what, 'ok' if ok else 'FAILED'))
    if not ok:
        failed += 1


# the library
a = sphere(5)
g = handles(a, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)), mode='always')
l = lock(g)
check('lock returns a shape', isinstance(l, Shape))
check('it is marked locked', getattr(l, '_locked', False) is True)
check('the shape it came from is not', not hasattr(g, '_locked'))
check('the handles stay (mode, numbers)', l._handles == g._handles and l._handles[0] == 'always')
check('and the field is the same', evaluate(l, [(1, 1, 1), (9, 0, 0)]) == evaluate(g, [(1, 1, 1), (9, 0, 0)]))
for bad, why in ((lambda: handles(a, mode='lock'), "mode='lock'"), (lambda: handles(a, show=False), 'show=False'),
                 (lambda: lock(3), 'lock(3)')):
    try:
        bad()
        check(why + ' is refused', False)
    except (ValueError, TypeError) as e:
        check(why + ' is refused: ' + str(e)[:60], True)
check("the default mode is click", handles(a)._handles[0] == 'click')
for mode in ('click', 'never', 'always'):
    check("handles(mode='%s') is accepted" % mode, handles(a, mode=mode)._handles[0] == mode)
for old in ('gizmo', 'handles'):
    try:
        handles(a, mode=old)
        check("handles(mode='%s') is refused (it is gone)" % old, False)
    except ValueError as e:
        check("handles(mode='%s') is refused: %s" % (old, str(e)[:50]), 'gone' in str(e))

# what the model tree sees
src = '''from fieldes import *
a = sphere(5)
a
b = sphere(3, (10, 0, 0))
b = handles(b, move=(var(0), var(0), var(0)), mode='never')
b = lock(b)
b
c = sphere(2, (0, 10, 0))
c = handles(c, move=(var(0), var(0), var(0)), mode='always')
c
'''
tree = ast.parse(src)
gs, results = {"var": var}, []
for stmt in tree.body:
    if isinstance(stmt, ast.Expr):
        results.append(eval(compile(ast.Expression(stmt.value), 'x', 'eval'), gs))
    else:
        exec(compile(ast.Module([stmt], []), 'x', 'exec'), gs)
        results.append(None)
scene = json.loads(app_support.scene_json(src, gs, results))
items = {it['var']: it for it in scene['items'] if 'var' in it}
check('a: not locked, click by default (not explicit)', 'locked' not in items['a'] and items['a']['mode'] == 'click' and not items['a']['mode_explicit'])
check('b: locked, on line 6', 'locked' in items['b'] and items['b']['locked']['line'] == 6)
check('b: its gizmo mode is still never', items['b']['mode'] == 'never' and items['b']['mode_explicit'])
check('c: always, not locked', items['c']['mode'] == 'always' and 'locked' not in items['c'])
check('the lock line is not a shape of its own', len([it for it in scene['items'] if it.get('var') == 'b']) == 1)

print('FAILED %d' % failed if failed else 'ALL OK')
