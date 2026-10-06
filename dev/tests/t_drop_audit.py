# What a drop on each library function does in the model tree, and whether a POINT works wherever a position goes.
# Part 1 (the tree's rules, replayed from a call written the way the menus write it): for every public function,
#   the body / field / point drop: where it would go, or that it is refused.
# Part 2 (the library): every position written as a tuple of three numbers, replaced by a Point at the same place, must
#   give the very same result as the tuple.  Prints the ones that do not.
import ast
import importlib
import inspect
import sys

from fieldes import *
from fieldes import app_support
from fieldes.shape import Shape
from fieldes.stdlib import points as P

MODULES = ['shapes', 'csg', 'transforms', 'handles', 'points', 'fields', 'regression', 'surfaces', 'lattices',
           'conformal', 'selection']
MODELISH = {'shape', 'body', 'part', 'model', 'solid', 'region', 'domain', 'a', 'b', 'base', 'tool', 'field', 'surface',
            'profile', 'f', 'g', 'other', 'target', 'cell', 'lattice', 'mesh', 'result', 'source', 'values', 'sdf', 'obj'}
POINTISH = {'p', 'point', 'center', 'centre', 'origin', 'position', 'pos', 'at', 'c', 'start', 'end', 'anchor',
            'location', 'corner', 'lo', 'hi', 'low', 'high', 'direction', 'normal', 'axis_point'}

app_support._SPAN_LINES = []


def functions():
    seen = set()
    for m in MODULES:
        mod = importlib.import_module('fieldes.stdlib.' + m)
        for name in getattr(mod, '__all__', []):
            obj = getattr(mod, name, None)
            if callable(obj) and name not in seen:
                seen.add(name)
                yield name, obj


def written(name, fn):
    ''' (call text, [(param, kind)]) as a menu would write the call: the required parameters, and the positions '''
    try:
        sig = inspect.signature(fn)
    except (TypeError, ValueError):
        return None, []
    parts, kinds = [], []
    skipped = False
    for p in sig.parameters.values():
        if p.kind in (p.VAR_POSITIONAL, p.VAR_KEYWORD, p.KEYWORD_ONLY):
            skipped = True
            continue
        d = p.default
        is_tuple = isinstance(d, (tuple, list)) and len(d) in (2, 3) and all(isinstance(c, (int, float)) for c in d)
        if d is not inspect.Parameter.empty and not is_tuple:
            skipped = True
            continue
        if p.name in MODELISH and d is inspect.Parameter.empty:
            text, kind = 'm%d' % (len(parts) + 1), 'model'
        elif is_tuple or p.name in POINTISH:
            text, kind = ('(%s)' % ', '.join(str(c) for c in (d if is_tuple else (1, 2, 3)))), 'tuple'
        else:
            text, kind = '2.5', 'number'
        parts.append(('%s=%s' % (p.name, text)) if skipped else text)
        kinds.append((p.name, kind))
    return '%s(%s)' % (name, ', '.join(parts)), kinds


def slots(call, gs):
    tree = ast.parse(call, mode='eval').body
    app_support._SPAN_LINES = [call]
    by_name = {'m%d' % i: 1 for i in range(1, 6)}
    inputs, variadic, numbers = app_support._inputs_of(tree, gs, by_name)
    return inputs, variadic, numbers, app_support._points_of(tree, gs)


rows = []
for name, fn in functions():
    call, kinds = written(name, fn)
    if call is None:
        continue
    try:
        inputs, variadic, numbers, points = slots(call, globals())
    except Exception as e:
        rows.append((name, call, 'SCENE ERROR %s' % e))
        continue
    direct = [i for i in inputs if not i.get('deep') and 'keyword' not in i]
    body = ('append' if variadic and direct else 'replace %s' % direct[0]['name'] if len(direct) == 1 else
            'refused (%d inputs)' % len(direct) if direct or variadic else 'refused (no model input)')
    plain = [n for n in numbers if '[' not in n['label']]
    field = plain[0]['label'] if plain else 'refused (no plain number)'
    point = points[0]['label'] if points else ('replace a point input' if any(k == 'model' for _, k in kinds) else 'refused (no position)')
    rows.append((name, call, 'body: %-22s field: %-26s point: %s' % (body, field, point)))

print('=== PART 1: drops (%d functions)' % len(rows))
for name, call, what in rows:
    print('%-34s %-70s %s' % (name, call[:70], what))

# Part 2 ---------------------------------------------------------------------------------------------------------------
print('\n=== PART 2: a Point where a position goes')
base_shape = lambda: box_exact((0, 0, 0), (10, 10, 10))


def build_args(fn, replace=None):
    ''' The arguments for a call, a number or tuple for each parameter, one position (by index) as a Point '''
    sig = inspect.signature(fn)
    args, kw, idx = [], {}, 0
    positions = []
    for p in sig.parameters.values():
        if p.kind in (p.VAR_POSITIONAL, p.VAR_KEYWORD, p.KEYWORD_ONLY):
            continue
        d = p.default
        is_tuple = isinstance(d, (tuple, list)) and len(d) == 3 and all(isinstance(c, (int, float)) for c in d)
        if d is not inspect.Parameter.empty and not is_tuple:
            continue
        if p.name in MODELISH and d is inspect.Parameter.empty:
            v = base_shape()
        elif is_tuple or p.name in POINTISH:
            tup = tuple(d) if is_tuple else (1.0, 2.0, 3.0)
            if len(tup) == 3:
                positions.append(p.name)
                v = P.point(*tup) if replace == p.name else tup
            else:
                v = tup
        else:
            v = 2.5
        kw[p.name] = v
    return kw, positions


tested = failed = 0
for name, fn in functions():
    try:
        kw, positions = build_args(fn)
    except Exception:
        continue
    if not positions:
        continue
    try:
        base = fn(**kw)
    except Exception as e:
        continue                                   # (the guessed arguments do not make a valid call: nothing to compare)
    for pos in positions:
        kw2, _ = build_args(fn, replace=pos)
        tested += 1
        try:
            out = fn(**kw2)
            if isinstance(base, Shape) and isinstance(out, Shape):
                pts = [(1, 2, 3), (5, 5, 5), (-3, 4, 8), (0, 0, 0)]
                a, b = evaluate(base, pts), evaluate(out, pts)
                if max(abs(u - v) for u, v in zip(a, b)) > 1e-6:
                    failed += 1
                    print('DIFFERENT  %s(%s=Point): %s vs %s' % (name, pos, a, b))
        except Exception as e:
            failed += 1
            print('FAILS      %s(%s=Point): %s: %s' % (name, pos, type(e).__name__, str(e)[:100]))
print('positions tested %d, differing or failing %d' % (tested, failed))
