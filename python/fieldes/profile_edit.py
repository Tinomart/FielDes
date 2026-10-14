'''
Editing a profile or a curve from the viewport (fieldes.stdlib.profile, fieldes.stdlib.curves): a point put on its outline, a point toggled between a corner
and a spline point, and the points of a new profile put onto its plane.  All are edits of the script text, answered as JSON

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
    ''' (the assignment, the list of points) of the profile or curve `name`: its call's first argument is the list; a profile made of a curve is the curve's '''
    for _ in range(4):
        found = _assignments_of(tree, name, before)
        call = next((a for a in reversed(found) if _call_name(a.value) in ('profile', 'polyline', 'spline')), None)
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


def _number_of(node):
    ''' The number a literal is (`3`, `-2.5`), or None '''
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)) and not isinstance(node.value, bool):
        return float(node.value)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
        inner = _number_of(node.operand)
        return None if inner is None else (-inner if isinstance(node.op, ast.USub) else inner)
    return None


def _snap(req):
    ''' The points of a profile about to be made -- `req['call']`, `profile([a, b, c, d])` -- put onto its plane: the plane through the first three of them
        that are not on a line (they stay where they are), and every other point moved along the plane's normal onto it, by the numbers written in its
        `point(x, y, z)`.  A point whose numbers are not written as numbers is left (the profile projects it anyway).  The edits are in the coordinates of
        the script as it was '''
    from fieldes import runner
    from fieldes.stdlib import curves as C
    source = req['source']
    nothing = json.dumps({'edits': [], 'label': 'Put the points on the plane', 'line': -1, 'name': ''})
    try:
        call = ast.parse(req['call'].strip()).body[0].value
        names = [e.id for e in call.args[0].elts]
    except (SyntaxError, AttributeError, IndexError, TypeError):
        return nothing
    objs = [runner.last_globals.get(n) for n in names]
    if len(names) < 4 or any(o is None for o in objs):
        return nothing
    ctrls = C.controls(objs)
    plane = C.plane_of(ctrls)
    if plane is None:
        return nothing
    o, u, v = plane
    normal = C.cross(u, v)
    tree = ast.parse(source)
    lines = source.split('\n')
    edits = []
    for name, c in zip(names, ctrls):
        off = C.dot(C.sub(c.pos, o), normal)
        if abs(off) < 1e-7:
            continue
        delta = C.mul(normal, -off)
        assign = next((a for a in reversed(_assignments_of(tree, name)) if _call_name(a.value) == 'point'), None)
        if assign is None or len(assign.value.args) < 2:
            continue
        args = assign.value.args[:3]
        written = [_number_of(a) for a in args]
        if any(w is None for w in written):
            continue
        for a, w, d in zip(args, written, delta):
            if abs(d) < 1e-12 or a.lineno != a.end_lineno:
                continue
            text = ('%.6f' % (w + d)).rstrip('0').rstrip('.')
            edits.append([a.lineno - 1, _char_col(lines[a.lineno - 1], a.col_offset),
                          a.end_lineno - 1, _char_col(lines[a.end_lineno - 1], a.end_col_offset), '0' if text in ('', '-0') else text])
        if len(assign.value.args) == 2 and abs(delta[2]) > 1e-12:
            # (point(x, y): the third number is not written: it is added)
            last = assign.value.args[1]
            edits.append([last.end_lineno - 1, _char_col(lines[last.end_lineno - 1], last.end_col_offset),
                          last.end_lineno - 1, _char_col(lines[last.end_lineno - 1], last.end_col_offset),
                          ', ' + ('%.6f' % delta[2]).rstrip('0').rstrip('.')])
    return json.dumps({'edits': edits, 'label': 'Put the points on the plane', 'line': -1, 'name': ''})


def edit(arg):
    ''' `arg` is JSON {op: 'insert' | 'toggle' | 'snap', source, line (1-based, the line that displays the model), x, y, z (for insert: where), call (for snap)} '''
    from fieldes import runner
    from fieldes.stdlib import curves as C
    req = json.loads(arg)
    if req['op'] == 'snap':
        return _snap(req)
    source, line, op = req['source'], int(req['line']), req['op']
    tree = ast.parse(source)
    lines = source.split('\n')
    name = _display_name(tree, line)
    if name is None:
        raise ValueError('nothing is displayed on that line')
    obj = runner.last_globals.get(name)
    if op == 'toggle':
        return _toggle(tree, lines, name, line, obj)
    if op == 'radius':
        return _radius_var(tree, lines, name, line, int(req['index']))
    if obj is None or not hasattr(obj, 'vertices') or not hasattr(obj, '_owners'):
        raise ValueError('%s is not a profile or a curve of the last run' % name)
    owner, plist = _owner_list(tree, name, line)
    if owner is None:
        raise ValueError('the points of %s are not written as a list in its call: put a point there in the script' % name)
    points = list(getattr(obj, 'points', []))
    if len(points) != len(plist.elts):
        raise ValueError('the list of points of %s is not one point to an element' % name)
    V = [tuple(v) for v in obj.vertices]
    # (the path of a sweep of a profile is DRAWN where the sweep goes -- shifted, see Curve._display -- so what the press and the drop say is where it is seen: the numbers of the
    # script are the shift away)
    shift = getattr(obj, '_weld_shift', None) or (0.0, 0.0, 0.0)
    # (the piece that was pressed, from where the press was; the point goes where it was let go)
    press = (float(req.get('px', req['x'])) - shift[0], float(req.get('py', req['y'])) - shift[1], float(req.get('pz', req['z'])) - shift[2])
    piece, _on_the_line = _nearest_piece(V, bool(getattr(obj, 'closed', False)), press)
    at = (float(req['x']) - shift[0], float(req['y']) - shift[1], float(req['z']) - shift[2])
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


def _radius_var(tree, lines, name, line, index):
    ''' The radius a point says, made a number the viewport can drag: `point(.., radius=5)` becomes `point(.., radius=var(5))` (a number written into the script by a drag is a
        var(), so that the editor can find it again).  `name` is the profile on the 1-based `line`, `index` the number of the point in its list.  Nothing is changed when the
        radius is a var() already, or is not a plain number '''
    nothing = json.dumps({'edits': [], 'label': 'Radius', 'line': -1, 'name': ''})
    owner, plist = _owner_list(tree, name, line)
    if owner is None or not 0 <= index < len(plist.elts) or not isinstance(plist.elts[index], ast.Name):
        return nothing
    point = plist.elts[index].id
    assign = next((a for a in reversed(_assignments_of(tree, point, owner.lineno)) if _call_name(a.value) == 'point'), None)
    kw = next((k for k in assign.value.keywords if k.arg == 'radius'), None) if assign is not None else None
    if kw is None or _call_name(kw.value) == 'var' or _number_of(kw.value) is None:
        return nothing
    v = kw.value
    text = ast.get_source_segment('\n'.join(lines), v)
    edits = [[v.lineno - 1, _char_col(lines[v.lineno - 1], v.col_offset), v.end_lineno - 1, _char_col(lines[v.end_lineno - 1], v.end_col_offset), 'var(%s)' % text]]
    return json.dumps({'edits': edits, 'label': 'The radius of %s can be dragged' % point, 'line': -1, 'name': point})


def _toggle(tree, lines, name, line, obj):
    ''' A double click on the dot of a point of a profile or a curve: a corner becomes a spline point, a spline point a round corner (in a profile -- in a curve it goes back to a
        corner), a round corner a corner again.  The point says what it is with `smooth=` and `radius=`: a keyword that says what the profile says anyway is left out '''
    effective = getattr(obj, '_curve_smooth', None)
    default = getattr(obj, '_curve_default', False)
    if effective is None:
        raise ValueError('%s is not a point of a profile or a curve' % name)
    rounded = bool(getattr(obj, '_curve_round', False))
    in_profile = getattr(obj, '_curve_kind', None) == 'profile'
    assign = next((a for a in _assignments_of(tree, name, line) if _call_name(a.value) == 'point'), None)
    if assign is None:
        raise ValueError('%s is not written as point(...): what it is in a profile cannot be changed there' % name)
    call = assign.value
    state = 'round' if rounded else ('spline' if effective else 'corner')
    new_state = {'corner': 'spline', 'spline': 'round' if in_profile else 'corner', 'round': 'corner'}[state]
    # What the point says of itself now.  A round corner is a corner with a radius: it says no `smooth=` (a radius beats the profile's default), and its radius starts at a quarter of
    # its shorter edge
    smooth = {'corner': False, 'spline': True, 'round': None}[new_state]
    if smooth is not None and smooth == bool(default):
        smooth = None                                              # (what the profile says anyway)
    radius = None
    if new_state == 'round':
        edge = float(getattr(obj, '_curve_edge', 0.0) or 0.0)
        radius = _num_text(float('%.2g' % (0.25 * edge))) if edge > 0 else '1'
    source = '\n'.join(lines)
    kept = [ast.get_source_segment(source, n) for n in call.args]
    kept += [ast.get_source_segment(source, k) for k in call.keywords if k.arg not in ('smooth', 'radius')]
    if smooth is not None:
        kept.append('smooth=%r' % smooth)
    if radius is not None:
        kept.append('radius=var(%s)' % radius)             # (a var(): the number that the arc of the corner drags in the viewport)
    text = '%s(%s)' % (ast.get_source_segment(source, call.func), ', '.join(kept))
    edits = [[call.lineno - 1, _char_col(lines[call.lineno - 1], call.col_offset), call.end_lineno - 1, _char_col(lines[call.end_lineno - 1], call.end_col_offset), text]]
    label = {'corner': 'corner', 'spline': 'spline point', 'round': 'round corner'}[new_state]
    return json.dumps({'edits': edits, 'label': '%s: %s' % (name, label), 'line': -1, 'name': name})
