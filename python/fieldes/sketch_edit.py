'''
Editing a sketch or a curve from the viewport (fieldes.stdlib.sketch, fieldes.stdlib.curves): a point put on its outline, a point toggled between a corner
and a spline point.  Both are edits of the script text, answered as JSON

    {edits: [[line0, col0, line1, col1, text], ...], label, line (the 0-based line that displays the new point, or -1), name (the new point's variable)}

(see ScenePanel::editPoints: lines are 0-based and columns count characters; every edit is in the coordinates of the script as it was).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import json
import re


def _char_col(line, byte_col):
    ''' ast gives columns in bytes of UTF-8; the editor counts characters '''
    return len(line.encode('utf-8')[:byte_col].decode('utf-8', 'ignore'))


def _display_name(tree, line):
    ''' The name displayed by the statement that holds the 1-based line: `plate` on a line of its own '''
    for stmt in tree.body:
        if stmt.lineno <= line <= stmt.end_lineno and isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Name):
            return stmt.value.id
    return None


def _assignments_of(tree, name, before=None):
    ''' The statements that assign `name` at the top of the script (before the 1-based line `before`), in order '''
    out = []
    for stmt in tree.body:
        if before is not None and stmt.lineno >= before:
            break
        if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name) and stmt.targets[0].id == name:
            out.append(stmt)
    return out


def _call_name(node):
    return node.func.id if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) else None


def _owner_list(tree, name, before):
    ''' (the assignment, the list of points) of the sketch or curve `name`: its call's first argument is the list; a sketch made of a curve is the curve's '''
    for _ in range(4):
        found = _assignments_of(tree, name, before)
        call = next((a for a in reversed(found) if _call_name(a.value) in ('sketch', 'curve')), None)
        if call is None or not call.value.args:
            return None, None
        first = call.value.args[0]
        if isinstance(first, ast.List):
            return call, first
        if isinstance(first, ast.Name):
            name, before = first.id, call.lineno
            continue
        return None, None
    return None, None


def _nearest_piece(vertices, closed, p):
    ''' (the number of the piece of the polyline that is nearest to p, the point of it) '''
    best, piece, at = None, 0, p
    n = len(vertices)
    for k in range(n if closed else n - 1):
        a, b = vertices[k], vertices[(k + 1) % n]
        e = [b[i] - a[i] for i in range(3)]
        w = [p[i] - a[i] for i in range(3)]
        ee = sum(c * c for c in e)
        t = 0.0 if ee < 1e-18 else max(0.0, min(1.0, sum(w[i] * e[i] for i in range(3)) / ee))
        q = [a[i] + e[i] * t for i in range(3)]
        d = sum((p[i] - q[i]) ** 2 for i in range(3))
        if best is None or d < best:
            best, piece, at = d, k, tuple(q)
    return piece, at


def _num_text(v):
    s = ('%.4f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('', '-0') else s


def edit(arg):
    ''' `arg` is JSON {op: 'insert' | 'toggle', source, line (1-based, the line that displays the model), x, y, z (for insert: where)} '''
    from fieldes import runner
    from fieldes.stdlib import curves as C
    req = json.loads(arg)
    source, line, op = req['source'], int(req['line']), req['op']
    tree = ast.parse(source)
    lines = source.split('\n')
    name = _display_name(tree, line)
    if name is None:
        raise ValueError('nothing is displayed on that line')
    obj = runner.last_globals.get(name)
    if op == 'toggle':
        return _toggle(tree, lines, name, line, obj)
    if obj is None or not hasattr(obj, '_vertices3d'):
        raise ValueError('%s is not a sketch or a curve of the last run' % name)
    owner, plist = _owner_list(tree, name, line)
    if owner is None:
        raise ValueError('the points of %s are not written as a list in its call: put a point there in the script' % name)
    points = list(getattr(obj, 'points', []))
    if len(points) != len(plist.elts):
        raise ValueError('the list of points of %s is not one point to an element' % name)
    V = [tuple(C.value_of(c) for c in v) for v in obj._vertices3d]
    piece, at = _nearest_piece(V, bool(getattr(obj, 'closed', False)), (float(req['x']), float(req['y']), float(req['z'])))
    i = obj._owners[piece][0]
    elt = plist.elts[i]
    # the new point: a model of its own, with its gizmo, when the others are names; else an element of the list
    owner_var = owner.targets[0].id
    taken = set(re.findall(r'[A-Za-z_]\w*', source))
    k = len(plist.elts) + 1
    while '%s_p%d' % (owner_var, k) in taken:
        k += 1
    new = '%s_p%d' % (owner_var, k)
    x, y, z = (_num_text(c) for c in at)
    edits = []
    select = -1
    if isinstance(elt, ast.Name):
        indent = ' ' * owner.col_offset
        block = ['%s = point(%s, %s, %s)' % (new, x, y, z),
                 '%s = gizmo(' % new, '    %s,' % new, '    move=(var(0), var(0), var(0)),', '    rotate=(var(0), var(0), var(0)),',
                 '    scale=(var(1), var(1), var(1)),', ')', new]
        text = ''.join(indent + b + '\n' for b in block)
        edits.append([owner.lineno - 1, 0, owner.lineno - 1, 0, text])
        select = owner.lineno - 1 + len(block) - 1
        insert = ', ' + new
    else:
        insert = ', (%s, %s, %s)' % (x, y, z)
    l1, c1 = elt.end_lineno - 1, _char_col(lines[elt.end_lineno - 1], elt.end_col_offset)
    edits.append([l1, c1, l1, c1, insert])
    return json.dumps({'edits': edits, 'label': 'Add a point to %s' % name, 'line': select, 'name': new if select >= 0 else ''})


def _toggle(tree, lines, name, line, obj):
    effective = getattr(obj, '_curve_smooth', None)
    default = getattr(obj, '_curve_default', False)
    if effective is None:
        raise ValueError('%s is not a point of a sketch or a curve' % name)
    new_state = not effective
    assign = next((a for a in _assignments_of(tree, name, line) if _call_name(a.value) == 'point'), None)
    if assign is None:
        raise ValueError('%s is not written as point(...): its smoothness cannot be changed there' % name)
    call = assign.value
    kw = next((k for k in call.keywords if k.arg == 'smooth'), None)
    edits = []
    nothing = json.dumps({'edits': [], 'label': 'Point %s' % name, 'line': -1, 'name': name})
    if new_state == default:
        # (what the sketch says anyway: the keyword is taken out)
        if kw is None:
            return nothing
        before = [n for n in list(call.args) + [k for k in call.keywords if k is not kw]
                  if (n.end_lineno, n.end_col_offset) <= (kw.lineno, kw.col_offset)]
        if not before:
            return nothing
        start = max(before, key=lambda n: (n.end_lineno, n.end_col_offset))
        edits.append([start.end_lineno - 1, _char_col(lines[start.end_lineno - 1], start.end_col_offset),
                      kw.end_lineno - 1, _char_col(lines[kw.end_lineno - 1], kw.end_col_offset), ''])
    elif kw is not None:
        v = kw.value
        edits.append([v.lineno - 1, _char_col(lines[v.lineno - 1], v.col_offset),
                      v.end_lineno - 1, _char_col(lines[v.end_lineno - 1], v.end_col_offset), repr(new_state)])
    else:
        last = max(list(call.args) + list(call.keywords), key=lambda n: (n.end_lineno, n.end_col_offset))
        l1, c1 = last.end_lineno - 1, _char_col(lines[last.end_lineno - 1], last.end_col_offset)
        edits.append([l1, c1, l1, c1, ', smooth=%r' % new_state])
    return json.dumps({'edits': edits, 'label': '%s: %s' % (name, 'spline point' if new_state else 'corner'), 'line': -1, 'name': name})
