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
import math
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
        call = next((a for a in reversed(found) if _call_name(a.value) in ('profile', 'polyline', 'spline', 'morph_polyhedron', 'morph_body')), None)
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


def _six(v):
    ''' A number written for the script: six decimals at most, none that is only a zero '''
    s = ('%.6f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('', '-0') else s


def _between(obj, piece, press, i, vertices):
    ''' (the scale, the influence) of a point put on the line of `obj` where `press` is -- on the piece `piece` of its vertices, which runs from the point `i` towards the next --, as the text of
        the two numbers: the two points it is put between say them, and the new one is what is between them at the place (how far along the way from one to the other the line is there).
        `1` and `1` where the points say nothing, or the line has no point to say it (a profile that is only numbers) '''
    ctrls = list(getattr(obj, '_ctrls', None) or [])
    n = len(ctrls)
    closed = bool(getattr(obj, 'closed', False))
    if not 0 <= i < n:
        return '1', '1'
    j = (i + 1) % n if (closed or i + 1 < n) else None
    if j is None:
        return _six(ctrls[i].scale), _six(ctrls[i].influence)
    # how far along the way from point i to point j: the kernel says it of each vertex (the number of the point it is after, and how far towards the next), and the place is between two of them
    owners = list(getattr(obj, '_owners', None) or [])
    t = 0.5
    try:
        m = len(vertices)
        a, b = vertices[piece], vertices[(piece + 1) % m]
        e = [b[k] - a[k] for k in range(3)]
        ee = sum(c * c for c in e)
        u = 0.0 if ee < 1e-18 else max(0.0, min(1.0, sum((press[k] - a[k]) * e[k] for k in range(3)) / ee))
        start = owners[piece][1] if owners[piece][0] == i else 0.0
        nxt = owners[(piece + 1) % m]
        stop = nxt[1] if nxt[0] == i else 1.0
        t = max(0.0, min(1.0, start + u * (stop - start)))
    except (IndexError, TypeError):
        pass
    scale = (1.0 - t) * ctrls[i].scale + t * ctrls[j].scale
    influence = (1.0 - t) * ctrls[i].influence + t * ctrls[j].influence
    return _six(scale), _six(influence)


def _number_of(node):
    ''' The number a literal is (`3`, `-2.5`), or None '''
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)) and not isinstance(node.value, bool):
        return float(node.value)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
        inner = _number_of(node.operand)
        return None if inner is None else (-inner if isinstance(node.op, ast.USub) else inner)
    return None


def _closest_on_triangle(p, a, b, c):
    ''' (the squared distance from p to the triangle a, b, c, the point of it that is nearest p) '''
    def sub(u, v):
        return (u[0] - v[0], u[1] - v[1], u[2] - v[2])

    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]

    def at(o, d, t):
        return (o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t)

    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        q = a
    else:
        bp = sub(p, b)
        d3, d4 = dot(ab, bp), dot(ac, bp)
        if d3 >= 0 and d4 <= d3:
            q = b
        else:
            vc = d1 * d4 - d3 * d2
            if vc <= 0 and d1 >= 0 and d3 <= 0:
                q = at(a, ab, d1 / (d1 - d3))
            else:
                cp = sub(p, c)
                d5, d6 = dot(ab, cp), dot(ac, cp)
                if d6 >= 0 and d5 <= d6:
                    q = c
                else:
                    vb = d5 * d2 - d1 * d6
                    if vb <= 0 and d2 >= 0 and d6 <= 0:
                        q = at(a, ac, d2 / (d2 - d6))
                    else:
                        va = d3 * d6 - d5 * d4
                        if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
                            q = at(b, sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)))
                        else:
                            den = 1.0 / (va + vb + vc) if abs(va + vb + vc) > 1e-30 else 0.0
                            q = at(at(a, ab, vb * den), ac, vc * den)
    d = sub(p, q)
    return dot(d, d), q


def _faces_text(faces, indent):
    ''' The faces of a body made of points (three or four point numbers each) as the text of a list, one to a line '''
    rows = ['%s    (%s),' % (indent, ', '.join('%d' % v for v in face)) for face in faces]
    return '[\n' + '\n'.join(rows) + '\n' + indent + ']'


def _faces_text_fit(faces, indent, count):
    ''' The faces as the text of a list that is exactly `count` lines long -- the lines the old list took.  The model tree takes a point out of a body by writing the lines from the faces down to the call back one to
        one, and cuts the point's own lines at the numbers they had: a list that came out longer or shorter would move them (the user, 2026-11-03: the script broke after a point was deleted).  The faces go into the
        rows between the brackets, as many to a row as it takes, and a row that is not needed is an empty line (the list is in the region of the script that the editor does not show) '''
    cells = ['(%s)' % ', '.join('%d' % v for v in face) for face in faces]
    if count <= 1:
        return '[' + ', '.join(cells) + ']'
    if count == 2:
        return '[' + ', '.join(cells) + ',\n' + indent + ']'
    rows = count - 2
    per = max(1, -(-len(cells) // rows))
    out = []
    for r in range(rows):
        chunk = cells[r * per:(r + 1) * per]
        out.append((indent + '    ' + ', '.join(chunk) + ',') if chunk else '')
    return '[\n' + '\n'.join(out) + '\n' + indent + ']'


INTERNAL_BEGIN = '# fieldes-internal: begin'
INTERNAL_END = '# fieldes-internal: end'


def _faces_block(faces_name, faces, indent):
    ''' The faces of a body made of points as the lines the program keeps INSIDE the script, in a region the editor does not show (the user, 2026-11-02, H67: "YOU DO NOT FUCKING EXPOSE THIS TO THER FUCKING
        USER"): a begin mark, the variable `<name>_faces = [...]` (eight faces to a line), an end mark.  Every line ends with a newline '''
    rows, row = [], []
    for face in faces:
        row.append('(%s)' % ', '.join('%d' % v for v in face))
        if len(row) == 8:
            rows.append(indent + '    ' + ', '.join(row) + ',')
            row = []
    if row:
        rows.append(indent + '    ' + ', '.join(row) + ',')
    return (indent + INTERNAL_BEGIN + ' (the faces of the body below: the program writes and reads them, they are no part of the model)\n'
            + indent + faces_name + ' = [\n' + '\n'.join(rows) + '\n' + indent + ']\n'
            + indent + INTERNAL_END + '\n')


def _point_style(tree, name):
    ''' What the point written as `name = point(...)` is: 'round' (it says a radius), 'spline' or 'corner' (it says `smooth=`), None (it says neither: what the body says) '''
    if not name:
        return None
    found = next((a for a in reversed(_assignments_of(tree, name)) if _call_name(a.value) == 'point'), None)
    if found is None:
        return None
    keywords = {k.arg: k.value for k in found.value.keywords}
    if 'radius' in keywords:
        return 'round'
    if 'smooth' in keywords:
        value = keywords['smooth']
        return 'spline' if isinstance(value, ast.Constant) and value.value else 'corner'
    return None


def _insert_morph(tree, lines, source, name, line, obj, req):
    ''' A press on the surface of a body made of points (morph_polyhedron, morph_body) and a drag: a new point where the press was, the triangle that was there split in three round it -- the body's list of
        points gets the point, and the list of its triangles (`body_faces = [...]`, above the call) is written again.  The point is put on the surface, and the program goes on dragging it (the body bulges or dents as it goes) '''
    assign = next((a for a in reversed(_assignments_of(tree, name, line)) if _call_name(a.value) in ('morph_polyhedron', 'morph_body')), None)
    if assign is None or not assign.value.args or not isinstance(assign.value.args[0], ast.List):
        raise ValueError('the points of %s are not written as a list in its call: put the points there in the script' % name)
    call, plist = assign.value, assign.value.args[0]
    points = list(getattr(obj, 'points', []))
    if len(points) != len(plist.elts):
        raise ValueError('the list of points of %s is not one point to an element' % name)
    faces = [tuple(t) for t in obj._morph_faces]
    where = [tuple(c.pos) for c in obj._ctrls]
    press = (float(req.get('px', req['x'])), float(req.get('py', req['y'])), float(req.get('pz', req['z'])))
    best = None
    for index, face in enumerate(faces):
        # (a quad is two triangles to look at: the one the press is nearest)
        for t in range(1, len(face) - 1):
            d, _q = _closest_on_triangle(press, where[face[0]], where[face[t]], where[face[t + 1]])
            if best is None or d < best[0]:
                best = (d, index)
    if best is None:
        raise ValueError('%s has no surface to put a point on' % name)
    face = faces[best[1]]
    new_index = len(points)
    # (the face that was pressed is split round the new point: a triangle in three, a quad in four -- one to each edge)
    faces = faces[:best[1]] + faces[best[1] + 1:] + [(face[k], face[(k + 1) % len(face)], new_index) for k in range(len(face))]
    owner_var = assign.targets[0].id
    taken = set(re.findall(r'[A-Za-z_]\w*', source))
    n = len(plist.elts) + 1
    while '%s_p%d' % (owner_var, n) in taken:
        n += 1
    new = '%s_p%d' % (owner_var, n)
    # (a body that is made symmetric -- some of the points of its list are `symmetry_image(point, symmetry, index)` -- gets an image of the new point in each of those symmetries, and the face the image falls on
    # is split round it, so that the shape stays symmetric: the user, 2026-11-04, H120, "when I drag out points from the surface they are not mirrored to the other side like they should")
    faces, mirrors = _split_for_images(tree, plist, line, press, faces, where, new_index)
    mirror_names = []
    for _unused in mirrors:
        n += 1
        while '%s_p%d' % (owner_var, n) in taken:
            n += 1
        mirror_names.append('%s_p%d' % (owner_var, n))
    x, y, z = (_num_text(c) for c in press)
    indent = ' ' * assign.col_offset
    # (a point made by a surface drag is of the type of the point he used last -- the user, 2026-11-03, H76 --: a corner, a spline point or a sphere point; a polyhedron's points are corners, flat triangles only.  A sphere
    # point says its radius in mm -- half the shortest edge it will have, to the corners of the face it splits, rounded down to two figures -- and an influence of 1, which multiplies the radius by 1: it
    # is written with the point, because the tree writes it for a point that lacks it the moment the point is selected -- one line added INSIDE the statement, which moved the line the view was told
    # (the point to be dragged is found by that line), so the drag never began and the point stayed where it was pressed: the user, 2026-11-03, H88, "When you drag a sphere point out of the surface, it
    # does not follow your mouse")
    style = None if _call_name(assign.value) == 'morph_polyhedron' else _point_style(tree, str(req.get('style_of') or ''))
    if style == 'round':
        import math as _math
        shortest = min(_math.dist(press, where[v]) for v in face)
        typed = ['    radius=var(%s),' % _num_text(_floor_figures(0.5 * shortest, 2)), '    influence=var(1),']
    elif style == 'spline':
        typed = ['    smooth=True,', '    influence=var(1),']
    elif style == 'corner':
        typed = ['    smooth=False,', '    influence=var(1),']
    else:
        typed = ['    influence=var(1),']
    block = ['%s = point(' % new, '    position=(var(%s), var(%s), var(%s)),' % (x, y, z), '    rotation=(var(0), var(0), var(0)),', '    scale=(var(1), var(1), var(1)),'] + typed + [')', new]
    block_length = len(block)
    for image_name, (image_symmetry, image_index) in zip(mirror_names, mirrors):
        block += ['%s = symmetry_image(%s, %s, %d)  # shadow: %s, %s' % (image_name, new, image_symmetry, image_index, new, image_symmetry), image_name]
    edits = [[assign.lineno - 1, 0, assign.lineno - 1, 0, ''.join(indent + b + '\n' for b in block)]]
    last = plist.elts[-1]
    l1, c1 = last.end_lineno - 1, _char_col(lines[last.end_lineno - 1], last.end_col_offset)
    edits.append([l1, c1, l1, c1, ', ' + ', '.join([new] + mirror_names)])
    # (the faces are a variable of their own above the call, `body_faces = [...]`, no argument of it: the user, 2026-11-02, "THIS SHOULD BE AN INTERNAL FUCKING VARIABLE")
    faces_name = owner_var + '_faces'
    existing = next((st for st in tree.body if isinstance(st, ast.Assign) and st.lineno < assign.lineno
                     and any(isinstance(t_, ast.Name) and t_.id == faces_name for t_ in st.targets)), None)
    shift = 0
    if existing is not None and isinstance(existing.value, ast.List):
        # (the whole statement is written again with the marks of the hidden region round it: a script of an older program has none, and its list was seen)
        text = _faces_block(faces_name, faces, ' ' * existing.col_offset)
        first, last = existing.lineno - 1, existing.end_lineno - 1
        if first > 0 and lines[first - 1].strip().startswith(INTERNAL_BEGIN):
            first -= 1
        if last + 1 < len(lines) and lines[last + 1].strip().startswith(INTERNAL_END):
            last += 1
        edits.append([first, 0, last + 1, 0, text])
        shift = text.count('\n') - (last + 1 - first)
    else:
        edits[0][4] += _faces_block(faces_name, faces, indent)
    answer_line = assign.lineno - 1 + block_length - 1 + shift
    if __import__('os').environ.get('FIELDES_TIMING'):
        __import__('sys').__stderr__.write('[insert] new point %s: the line the view is told is %d (the call of the body is at line %d, the block has %d lines, the faces list moved the text by %d)\n' % (new, answer_line, assign.lineno, len(block), shift))
        __import__('sys').__stderr__.flush()
    return json.dumps({'edits': edits, 'label': 'Add a point to %s' % name, 'line': answer_line, 'name': new})


def _split_for_images(tree, plist, line, press, faces, where, new_index):
    ''' The faces of a body of points after a point was put on its surface at `press` and the faces were split round it (`faces`), and what is to be done for the symmetry the body is made with:
        the (symmetry variable, index) of every `symmetry_image(point, symmetry, index)` among the points of its list, and for each one the place the image of the new point falls on -- worked out
        by the library, as the script will make it -- and the face it is on split round it, the new point having the number after the last.  Returns (the faces, [(symmetry variable, index)]) -- one
        pair for each image that is to be written; none for a body that is not made symmetric, or for a point on a plane (it is its own image) '''
    pairs = []
    for elt in plist.elts:
        if not isinstance(elt, ast.Name):
            continue
        made = next((a for a in reversed(_assignments_of(tree, elt.id, line)) if _call_name(a.value) == 'symmetry_image'), None)
        if made is None:
            continue
        args = made.value.args
        if len(args) < 2 or not isinstance(args[1], ast.Name):
            continue
        index = args[2].value if len(args) > 2 and isinstance(args[2], ast.Constant) and isinstance(args[2].value, int) else 1
        if (args[1].id, index) not in pairs:
            pairs.append((args[1].id, index))
    if not pairs:
        return faces, []
    try:
        from fieldes import runner
        from fieldes.stdlib import curves as C
        from fieldes.stdlib.points import point
        from fieldes.stdlib.symmetries import symmetry_image
    except Exception:
        return faces, []
    positions = list(where) + [press]
    done = []
    for svar, index in pairs:
        symmetry = runner.last_globals.get(svar)
        if symmetry is None:
            continue
        try:
            image = symmetry_image(point(*press), symmetry, index)
            there = tuple(float(c) for c in C.controls([image])[0].pos)
        except Exception:
            continue
        if sum((a - b) ** 2 for a, b in zip(there, press)) < 1e-12:
            continue
        best = None
        for number, face in enumerate(faces):
            for t in range(1, len(face) - 1):
                d, _q = _closest_on_triangle(there, positions[face[0]], positions[face[t]], positions[face[t + 1]])
                if best is None or d < best[0]:
                    best = (d, number)
        if best is None:
            continue
        face = faces[best[1]]
        number = len(positions)
        faces = faces[:best[1]] + faces[best[1] + 1:] + [(face[k], face[(k + 1) % len(face)], number) for k in range(len(face))]
        positions.append(there)
        done.append((svar, index))
    return faces, done


def _snap(req):
    ''' The points of a profile about to be made -- `req['call']`, `profile([a, b, c, d])` -- put onto its plane: the plane through the first three of them
        that are not on a line (they stay where they are), and every other point moved along the plane's normal onto it, by the numbers written in its
        `point(position=(x, y, z))`.  A point whose numbers are not written as numbers is left (the profile projects it anyway).  The edits are in the coordinates of
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


def _block_end(tree, stmt):
    ''' The last line (1-based) of `stmt` and of the line that displays what it makes, `name` on a line of its own right after it '''
    end = stmt.end_lineno
    nxt = next((s for s in tree.body if s.lineno > stmt.end_lineno), None)
    if (isinstance(stmt, ast.Assign) and isinstance(nxt, ast.Expr) and isinstance(nxt.value, ast.Name) and isinstance(stmt.targets[0], ast.Name)
            and nxt.value.id == stmt.targets[0].id and nxt.lineno == stmt.end_lineno + 1):
        end = nxt.end_lineno
    return end


def _link_symmetry(tree, lines, name, line, obj, req):
    ''' Shift+S with a shape made of points selected, and a symmetry in the script: the points of the shape that already have an exact mirror partner under the symmetry -- the shape is
        cut in half by the plane, or has its two sides, but was made without the symmetry -- are LINKED to it: the later point of a pair is written again as the image of the earlier one,
        `p_5 = symmetry_image(p_1, symmetry_1, 1)`, so that pulling one pulls the other, and no point is put on top of another (the user, 2026-11-02: "a symmetry applies to points that
        already have an exact mirror partner, even if they were created without a symmetry", "a point may have an initial mirror and another mirror and both apply", "we do not add unique
        weird GUI items, when we have an established control method" -- Shift+S).  A point that is on the plane has no partner and stays as it is, and so does a point without one; points that
        are images already are left alone.  The request carries the symmetry the way the creation of a shape does: {var, centre, elements}.  The symmetry statement is moved up above the first
        point it is used by when it came after it (a symmetry made by Shift+S is put at the end of the script) '''
    from fieldes import runner
    from fieldes import symmetry_creation as sc
    from fieldes.stdlib import curves as C
    symmetry = req.get('symmetry') or {}
    elements = sc.elements_of(symmetry)
    svar = symmetry.get('var')
    if not elements or not svar:
        raise ValueError('no symmetry is selected: select the symmetry (Shift+S) and then the shape')
    owner, plist = _owner_list(tree, name, line)
    if owner is None:
        raise ValueError('the points of %s are not written as a list in its call' % name)
    ctrls = getattr(obj, '_ctrls', None)
    if ctrls is None or len(ctrls) != len(plist.elts):
        raise ValueError('the list of points of %s is not one point to an element' % name)
    sym_assign = next((a for a in reversed(_assignments_of(tree, svar)) if isinstance(a, ast.Assign)), None)
    if sym_assign is None:
        raise ValueError('the symmetry %s is not made in the script' % svar)
    if sym_assign.lineno > owner.lineno:
        # (made after the shape, as Shift+S makes one: it is moved up above the points it is used by, below)
        pass
    places = [tuple(float(v) for v in c.pos) for c in ctrls]
    size = max(max(p[k] for p in places) - min(p[k] for p in places) for k in range(3)) or 1.0
    pairs = sc.partner_pairs(places, elements, 1e-4 * max(1.0, size))
    work = []
    for j, (i, e) in sorted(pairs.items()):
        ej, ei = plist.elts[j], plist.elts[i]
        if not (isinstance(ej, ast.Name) and isinstance(ei, ast.Name)):
            continue                                            # (a point written as a triple is not a model: nothing to link)
        aj = next((a for a in reversed(_assignments_of(tree, ej.id, owner.lineno)) if _call_name(a.value) == 'point'), None)
        ai = next((a for a in reversed(_assignments_of(tree, ei.id, owner.lineno))), None)
        if aj is None or ai is None or ai.lineno > aj.lineno:
            continue                                            # (already an image, or defined after the point it follows)
        work.append((aj, ei.id, ej.id, elements[e].index))
    if not work:
        raise ValueError('no point of %s has an exact mirror partner under %s that could be linked (a point on the plane is its own image)' % (name, svar))
    first = min(w[0].lineno for w in work)
    move_up = sym_assign.lineno > first
    block = ''
    end = _block_end(tree, sym_assign)
    if move_up:
        # (the symmetry is made after the points: it moves up to the first of them -- what it uses must be there already)
        used = {n.id for n in ast.walk(sym_assign.value) if isinstance(n, ast.Name)}
        later = [a.id for s in tree.body if isinstance(s, ast.Assign) and s.lineno >= first and s is not sym_assign for a in s.targets if isinstance(a, ast.Name) and a.id in used]
        if later:
            raise ValueError('%s uses %s, which is made after the points: move the symmetry above them in the script' % (svar, later[0]))
        block = '\n'.join(lines[sym_assign.lineno - 1:end]) + '\n'
    edits = []
    for aj, original, image, index in sorted(work, key=lambda w: w[0].lineno):
        text = '%s = symmetry_image(%s, %s, %d)  # shadow: %s, %s' % (image, original, svar, index, original, svar)
        if move_up and aj.lineno == first:
            text = block + text                              # (one edit: an insertion at the place where another edit begins would leave the order of the two to chance)
        edits.append([aj.lineno - 1, 0, aj.end_lineno - 1, len(lines[aj.end_lineno - 1]), text])
    if move_up:
        # (the old place of the symmetry: its lines taken out)
        if end < len(lines):
            edits.append([sym_assign.lineno - 1, 0, end, 0, ''])
        else:
            edits.append([sym_assign.lineno - 1, 0, end - 1, len(lines[end - 1]), ''])
    return json.dumps({'edits': edits, 'label': 'Link the mirror partners of %s' % name, 'line': -1, 'name': ''})


def _mirror_problem(tree, name, obj, svar):
    ''' Why the model `name` cannot be added to the symmetry `svar`: (kind, text) -- kind 'skip' when it is no shape (a material, a result, a condition, the symmetry itself, an image: Shift+S is
        then what it always was, the symmetry in use or not), 'have' when it is in the symmetry already -- or None '''
    from fieldes.kinds import kind_of
    if name == svar:
        return 'skip', '%s is the symmetry: it cannot be mirrored' % name
    last = next(iter(reversed(_assignments_of(tree, name))), None)
    if last is not None and _call_name(last.value) in ('symmetry_planes', 'symmetry_image', 'symmetry_images'):
        return 'skip', '%s is a symmetry or an image already: it cannot be mirrored' % name
    if kind_of(obj) not in ('solid', 'profile', 'curve', 'lattice', 'import', 'point'):
        return 'skip', '%s is not a shape: it cannot be mirrored' % name
    for stmt in tree.body:
        if isinstance(stmt, ast.Assign) and isinstance(stmt.value, ast.Call) and _call_name(stmt.value) in ('symmetry_image', 'symmetry_images'):
            args = stmt.value.args
            if len(args) >= 2 and isinstance(args[0], ast.Name) and isinstance(args[1], ast.Name) and args[0].id == name and args[1].id == svar:
                return 'have', '%s is in the symmetry %s already (%s)' % (name, svar, stmt.targets[0].id)
    return None


def _is_image_point(tree, elt, svar, before):
    ''' Whether the element `elt` of a list of points is a point that is the image of another through the symmetry `svar` '''
    if not isinstance(elt, ast.Name):
        return False
    last = next(iter(reversed(_assignments_of(tree, elt.id, before))), None)
    if last is None or _call_name(last.value) != 'symmetry_image':
        return False
    args = last.value.args
    return len(args) >= 2 and isinstance(args[1], ast.Name) and args[1].id == svar


def _offset(lines, lineno, col):
    """ The place in the whole text of a position of the syntax tree (a line, a column in bytes) """
    return sum(len(l) + 1 for l in lines[:lineno - 1]) + _char_col(lines[lineno - 1], col)


def _images_of_points(tree, names, svar):
    """ Whether a point of `names` has an image through the symmetry `svar` in the script already """
    for stmt in tree.body:
        if isinstance(stmt, ast.Assign) and isinstance(stmt.value, ast.Call) and _call_name(stmt.value) == 'symmetry_image':
            args = stmt.value.args
            if len(args) >= 2 and isinstance(args[0], ast.Name) and isinstance(args[1], ast.Name) and args[0].id in names and args[1].id == svar:
                return True
    return False


def _mirror_stroke(tree, lines, name, line, obj, elements, svar):
    """ The statements that mirror a profile, a line or a spline (a shape made of points; symmetry_images refuses it: "each of its images is a model of its own"): an image of each of its points
        -- `p_m1 = symmetry_image(p, mirror, 1)`, a point that lies on the plane is its own image and is used as it is, and a point written as three numbers is mirrored as numbers --, and the
        shape again through them, the same call: `c_m1 = spline([...])` (in reverse for a mirror image, which turns a closed shape round).  What a stroke drawn while a symmetry is in use gets
        (symmetry_creation.stroke_structure), made later (the user, 2026-11-03: "a morph shape or spline that does not have any mirror partner also gets mirrored when shift s is pressed on it") """
    import math
    owner, plist = _owner_list(tree, name, line)
    if owner is None:
        raise ValueError('the points of %s are not written as a list in its call: it cannot be mirrored' % name)
    ctrls = getattr(obj, '_ctrls', None)
    if ctrls is None or len(ctrls) != len(plist.elts):
        raise ValueError('the list of points of %s is not one point to an element: it cannot be mirrored' % name)
    source = '\n'.join(lines)
    places = [tuple(float(v) for v in c.pos) for c in ctrls]
    size = max(max(p[k] for p in places) - min(p[k] for p in places) for k in range(3)) or 1.0
    tol = 1e-4 * max(1.0, size)
    taken = set(re.findall(r'[A-Za-z_]\w*', source))

    def fresh(base):
        k = 0
        while (base if k == 0 else '%s_%d' % (base, k)) in taken:
            k += 1
        out = base if k == 0 else '%s_%d' % (base, k)
        taken.add(out)
        return out
    call = owner.value
    a, b = _offset(lines, call.lineno, call.col_offset), _offset(lines, call.end_lineno, call.end_col_offset)
    ls, le = _offset(lines, plist.lineno, plist.col_offset), _offset(lines, plist.end_lineno, plist.end_col_offset)
    blocks = []
    for e in elements:
        mapped = [e.apply(p) for p in places]
        if all(any(math.dist(q, p) < tol for p in places) for q in mapped):
            continue                          # (a shape on the plane is its own image under it)
        statements, items = [], []
        for elt, q in zip(plist.elts, mapped):
            same = next((j for j, p in enumerate(places) if math.dist(p, q) < tol), None)
            if same is not None:
                other = plist.elts[same]
                items.append(source[_offset(lines, other.lineno, other.col_offset):_offset(lines, other.end_lineno, other.end_col_offset)])
            elif isinstance(elt, ast.Name):
                image = fresh('%s_m%d' % (elt.id, e.index))
                statements.append('%s = symmetry_image(%s, %s, %d)  # shadow: %s, %s\n%s' % (image, elt.id, svar, e.index, elt.id, svar, image))
                items.append(image)
            else:
                items.append('(%s, %s, %s)' % tuple(_num_text(c) for c in q))
        if e.improper:
            items.reverse()
        image_var = fresh('%s_m%d' % (name, e.index))
        text = source[a:ls] + '[' + ', '.join(items) + ']' + source[le:b]
        statements.append('%s = %s\n%s' % (image_var, text, image_var))
        blocks.append('\n'.join(statements))
    if not blocks:
        raise ValueError('%s lies on the plane of the symmetry: it is its own image' % name)
    return '\n'.join(blocks)


def _add_to_symmetry(req):
    ''' Shift+S with models selected, and a symmetry in the script (the user, 2026-11-03: "selecting a part an pressing shift s adds it to the current symmetry", "the mirroring should be universal,
        also for shapes that do not have a mirror point partner already", "with shape I mean any body", "a morph shape or spline that does not have any mirror partner also gets mirrored"): each
        model is ADDED to the symmetry -- its images are made under it, as when something is made while the symmetry is in use (symmetry_creation.images_line): `leg_images = symmetry_images(leg,
        symmetry_1, distinct=True)`, the images that are not the model itself or another image (the run tells them).  A shape made of points that has points with an exact mirror partner, selected
        alone, is linked point by point instead (_link_symmetry): nothing is put on top of a point.  The line goes under the model's display line, or after the symmetry when that is made later
        (as Shift+S makes one).  {op: 'symmetry_add', source, lines: [the lines that display the models], symmetry: {var, centre, elements}} '''
    from fieldes import runner
    from fieldes import symmetry_creation as sc
    source = req['source']
    tree = ast.parse(source)
    lines = source.split('\n')
    symmetry = req.get('symmetry') or {}
    svar = symmetry.get('var')
    if not sc.elements_of(symmetry) or not svar:
        raise ValueError('no symmetry is selected: select the symmetry (Shift+S) and then the model')
    sym_assign = next((a for a in reversed(_assignments_of(tree, svar)) if isinstance(a, ast.Assign)), None)
    if sym_assign is None:
        raise ValueError('the symmetry %s is not made in the script' % svar)
    wanted = [int(n) for n in (req.get('lines') or [])]
    skipped, refused, made = [], [], []
    for line in wanted:
        name = _display_name(tree, line)
        if name is None:
            continue
        obj = runner.last_globals.get(name)
        problem = _mirror_problem(tree, name, obj, svar)
        if problem is not None:
            (skipped if problem[0] == 'skip' else refused).append(problem[1])
            continue
        stroke = hasattr(obj, 'vertices') and hasattr(obj, '_owners')            # (a profile, a line, a spline: made of points, mirrored through the images of its points)
        if stroke:
            owner, plist = _owner_list(tree, name, line)
            if owner is not None and _images_of_points(tree, {e.id for e in plist.elts if isinstance(e, ast.Name)}, svar):
                refused.append('%s is in the symmetry %s already (its points have images)' % (name, svar))
                continue
        if len(wanted) == 1:
            try:
                return _link_symmetry(tree, lines, name, line, obj, {'symmetry': symmetry})
            except ValueError as why:
                if 'is made after the points' in str(why):
                    raise
                owner, plist = _owner_list(tree, name, line)
                if owner is not None and any(_is_image_point(tree, e, svar, owner.lineno) for e in plist.elts):
                    refused.append('%s is in the symmetry %s already (its points are linked)' % (name, svar))
                    continue
        made.append((name, line, obj, stroke))
    if not made:
        if refused:
            raise ValueError(refused[0])
        raise ValueError(skipped[0] if skipped else 'nothing here can be mirrored')
    after_symmetry = _block_end(tree, sym_assign)
    inserts = {}
    for name, line, obj, stroke in made:
        at = max(line, after_symmetry) - 1
        text = _mirror_stroke(tree, lines, name, line, obj, sc.elements_of(symmetry), svar) if stroke else sc.images_line(name, svar, ', distinct=True').rstrip('\n')
        inserts.setdefault(at, []).append(text)
    edits = [[at, len(lines[at]), at, len(lines[at]), ''.join('\n' + t for t in texts)] for at, texts in sorted(inserts.items())]
    label = 'Add %s to the symmetry %s' % (made[0][0] if len(made) == 1 else '%d models' % len(made), svar)
    return json.dumps({'edits': edits, 'label': label, 'line': -1, 'name': ''})


def _literal(node):
    ''' The number a plain numeric literal is (`-20`, `3.5`), or None '''
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)) and not isinstance(node.value, bool):
        return float(node.value)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)) and isinstance(node.operand, ast.Constant) and isinstance(node.operand.value, (int, float)):
        return -float(node.operand.value) if isinstance(node.op, ast.USub) else float(node.operand.value)
    return None


def _transform_points(req):
    ''' The standard operations of the Moving group -- move, scale, rotate, mirror -- with POINTS selected (the points of a morph shape, a profile, a line): they change the points themselves,
        in place, and the shape made of them follows (the user, 2026-11-02: "NEVER ASK ME A QUESTION LIKE THAT ... CREATE NEW POINTS?"; "all the standard operations (scale, move, mirror) work
        properly with the new morph shape points").  It rewrites the three numbers of each `point(position=(x, y, z), ...)`: move by the step the menu moves a body by (`move`: along x), scale by the
        1.5 the menu scales by and rotate by its 45 degrees (about the z axis) both about the centre of the selected points, mirror across the plane x = 0, the plane the library's `mirror`
        takes.  {op: 'points_transform', source, names, how, scale (the size of the view the menu was opened in)} '''
    from fieldes import menu_catalog as mc
    how, names, source = req['how'], list(req['names']), req['source']
    tree = ast.parse(source)
    lines = source.split('\n')
    found = []
    for name in names:
        assign = next((a for a in reversed(_assignments_of(tree, name)) if _call_name(a.value) == 'point'), None)
        if assign is None:
            last = next(iter(reversed(_assignments_of(tree, name))), None)
            if last is not None and _call_name(last.value) == 'symmetry_image':
                continue                                   # (the image of a point follows it: it moves when the point does)
        pos = next((k.value for k in assign.value.keywords if k.arg == 'position'), None) if assign is not None else None
        if pos is None or not isinstance(pos, (ast.Tuple, ast.List)) or len(pos.elts) != 3:
            raise ValueError('%s is not written as point(position=(x, y, z), ...): change it by hand' % name)
        # (the numbers of its gizmo are var(x): the number inside is the one that is rewritten -- H93, one global position)
        args = [e.args[0] if isinstance(e, ast.Call) and _call_name(e) == 'var' and len(e.args) == 1 else e for e in pos.elts]
        values = [_literal(a) for a in args]
        if any(v is None for v in values):
            raise ValueError('%s is not written with plain numbers (x, y, z): change it by hand' % name)
        found.append((name, args, values))
    if not found:
        raise ValueError('the selected points are images of other points, and follow them: select the points they are the images of')
    n = float(len(found))
    centre = [sum(v[2][k] for v in found) / n for k in range(3)]
    step = mc._nice(float(req.get('scale', 10.0)))
    angle = math.radians(45.0)

    def moved(p):
        if how == 'move':
            return (p[0] + step, p[1], p[2])
        if how == 'scale_xyz':
            return tuple(centre[k] + (p[k] - centre[k]) * 1.5 for k in range(3))
        if how in ('rotate_z', 'rotate'):
            dx, dy = p[0] - centre[0], p[1] - centre[1]
            return (centre[0] + dx * math.cos(angle) - dy * math.sin(angle), centre[1] + dx * math.sin(angle) + dy * math.cos(angle), p[2])
        if how == 'mirror':
            return (-p[0], p[1], p[2])
        raise ValueError('%s does not change points' % how)
    edits = []
    for name, args, values in found:
        for arg, old, new in zip(args, values, moved(values)):
            if abs(new - old) < 5e-5:
                continue
            l0, l1 = arg.lineno - 1, arg.end_lineno - 1
            edits.append([l0, _char_col(lines[l0], arg.col_offset), l1, _char_col(lines[l1], arg.end_col_offset), _num_text(new)])
    if not edits:
        raise ValueError('the %s leaves the selected points where they are' % how)
    label = {'move': 'Move', 'scale_xyz': 'Scale', 'rotate_z': 'Rotate', 'rotate': 'Rotate', 'mirror': 'Mirror'}.get(how, how)
    return json.dumps({'edits': edits, 'label': '%s %d point%s' % (label, len(found), '' if len(found) == 1 else 's'), 'line': -1, 'name': ''})


def _body_point_numbers(tree, body):
    ''' The numbers of the points a body made of points is made of (`body = morph_body([a, b, c, ...])`): [(name, [the three nodes of the numbers], [their values])], the images of points
        (`p2 = symmetry_image(p1, ...)`) left out -- they follow their originals --, or None when the body is not written so or a number is not a plain number '''
    assign = next((a for a in reversed(_assignments_of(tree, body)) if _call_name(a.value) in ('morph_polyhedron', 'morph_body')), None)
    if assign is None or not assign.value.args or not isinstance(assign.value.args[0], ast.List):
        return None
    found, seen = [], set()
    for elt in assign.value.args[0].elts:
        if not isinstance(elt, ast.Name):
            return None
        if elt.id in seen:
            continue
        seen.add(elt.id)
        made = next((a for a in reversed(_assignments_of(tree, elt.id)) if _call_name(a.value) == 'point'), None)
        if made is None:
            continue
        pos = next((k.value for k in made.value.keywords if k.arg == 'position'), None)
        if pos is None or not isinstance(pos, (ast.Tuple, ast.List)) or len(pos.elts) != 3:
            return None
        args = [e.args[0] if isinstance(e, ast.Call) and _call_name(e) == 'var' and len(e.args) == 1 else e for e in pos.elts]
        values = [_literal(a) for a in args]
        if any(v is None for v in values):
            return None
        found.append((elt.id, args, values))
    return found or None


def body_middle(source, body):
    ''' The position of a body made of points: the middle of the box of its points (where the gizmo of the whole body stands), as (x, y, z), or None when it cannot be told from the script '''
    try:
        found = _body_point_numbers(ast.parse(source), body)
    except SyntaxError:
        return None
    if not found:
        return None
    return tuple(0.5 * (min(f[2][k] for f in found) + max(f[2][k] for f in found)) for k in range(3))


def _body_position(req):
    ''' The position of a body made of points typed (H151, the user, 2026-11-04: "Have point dependent objects like the morph body also have a position argument, so you can precisely position
        them"; the position is the middle of its points): every point moves by the difference between the new position and the old.  {op: 'body_position', source, body, position: [x, y, z]} '''
    source, body = req['source'], req['body']
    tree = ast.parse(source)
    lines = source.split('\n')
    found = _body_point_numbers(tree, body)
    if not found:
        raise ValueError('%s is not written as a list of points with plain numbers: change its position by hand' % body)
    middle = body_middle(source, body)
    want = [float(v) for v in req['position']]
    delta = [want[k] - middle[k] for k in range(3)]
    edits = []
    for name, args, values in found:
        for k, (arg, old) in enumerate(zip(args, values)):
            if abs(delta[k]) < 5e-5:
                continue
            l0, l1 = arg.lineno - 1, arg.end_lineno - 1
            edits.append([l0, _char_col(lines[l0], arg.col_offset), l1, _char_col(lines[l1], arg.end_col_offset), _num_text(old + delta[k])])
    if not edits:
        raise ValueError('%s is at that position already' % body)
    return json.dumps({'edits': edits, 'label': 'Position of %s' % body, 'line': -1, 'name': ''})


_LAST_MIDDLE = {}          # (the middle of the points of each body at the last run: which of the two, the points or the written position, changed since)


def _morph_call(tree, body):
    assign = next((a for a in reversed(_assignments_of(tree, body)) if _call_name(a.value) in ('morph_polyhedron', 'morph_body')), None)
    return assign.value if assign is not None else None


def _three(text_value):
    return '(%s, %s, %s)' % tuple(_six(c) for c in text_value)


def _position_values(node):
    ''' The three numbers of a written `position=(x, y, z)` (a number inside var() too), or None when they are not plain numbers '''
    if not isinstance(node, (ast.Tuple, ast.List)) or len(node.elts) != 3:
        return None
    args = [e.args[0] if isinstance(e, ast.Call) and _call_name(e) == 'var' and len(e.args) == 1 else e for e in node.elts]
    values = [_literal(a) for a in args]
    return None if any(v is None for v in values) else values


def body_sync(source, body):
    ''' What the tree needs to know of the position of a body made of points (H151): {middle, written (the numbers of its `position=` or None), edits (the edits that bring the points and the written
        position in step, or none)}.  The points are the body: when they have moved since the last run (a drag of a point or of the body) the position is written again as their middle; when only the
        position was changed (typed in the code) every point moves by the difference.  None when the body is not written as a list of points with plain numbers '''
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return None
    call = _morph_call(tree, body)
    found = _body_point_numbers(tree, body)
    if call is None or not found:
        return None
    middle = body_middle(source, body)
    out = {'middle': list(middle), 'written': None}
    keyword = next((k for k in call.keywords if k.arg == 'position'), None)
    if keyword is None:
        _LAST_MIDDLE[body] = middle
        return out
    written = _position_values(keyword.value)
    if written is None:
        _LAST_MIDDLE[body] = middle
        return out
    out['written'] = written
    if all(abs(written[k] - middle[k]) < 1e-3 for k in range(3)):
        _LAST_MIDDLE[body] = middle
        return out
    last = _LAST_MIDDLE.get(body)
    lines = source.split('\n')
    if last is None or any(abs(middle[k] - last[k]) > 1e-3 for k in range(3)):
        node = keyword.value
        l0, l1 = node.lineno - 1, node.end_lineno - 1
        out['edits'] = [[l0, _char_col(lines[l0], node.col_offset), l1, _char_col(lines[l1], node.end_col_offset), _three(middle)]]
        out['label'] = 'Position of %s' % body
        return out
    delta = [written[k] - middle[k] for k in range(3)]
    edits = []
    for name, args, values in found:
        for k, (arg, old) in enumerate(zip(args, values)):
            if abs(delta[k]) < 5e-5:
                continue
            l0, l1 = arg.lineno - 1, arg.end_lineno - 1
            edits.append([l0, _char_col(lines[l0], arg.col_offset), l1, _char_col(lines[l1], arg.end_col_offset), _num_text(old + delta[k])])
    out['edits'] = edits
    out['label'] = 'Position of %s' % body
    return out


def _body_position_write(req):
    ''' The argument `position=(x, y, z)` written into the call of a body made of points, with the middle of its points (the click on its dimmed row, as for every model).  {op: 'body_position_write', source, body} '''
    source, body = req['source'], req['body']
    tree = ast.parse(source)
    lines = source.split('\n')
    call = _morph_call(tree, body)
    middle = body_middle(source, body)
    if call is None or middle is None:
        raise ValueError('%s is not written as a list of points with plain numbers: write its position by hand' % body)
    last = call.keywords[-1] if call.keywords else call.args[-1]
    l1 = last.end_lineno - 1
    at = _char_col(lines[l1], last.end_col_offset)
    text = 'position=%s' % _three(middle)
    if call.lineno != call.end_lineno:
        indent = re.match(r'\s*', lines[l1]).group(0)
        text = ',\n' + indent + text
    else:
        text = ', ' + text
    _LAST_MIDDLE[body] = middle
    return json.dumps({'edits': [[l1, at, l1, at, text]], 'label': 'Position of %s' % body, 'line': -1, 'name': ''})


def _split_plane(req):
    """ A body of points whose symmetry plane other bodies are made through too gets a plane of its own (H62, the user, 2026-11-02: "If there are other bodies that are using that symmetry plane, then a new
        symmetry plane should be automatically generated"): the statement of the plane is written again right after it, under a new name -- the numbers of its position and rotation are the copy's own --
        and the images of the body's points are made through the copy.  {op: 'split_plane', source, plane (its name), names (the points of the body that have images through it)} """
    source, plane, names = req['source'], req['plane'], list(req['names'])
    tree = ast.parse(source)
    lines = source.split('\n')
    stmt = next((a for a in reversed(_assignments_of(tree, plane)) if _call_name(a.value) == 'symmetry_planes'), None)
    if stmt is None:
        raise ValueError('%s is not written as symmetry_planes(...): copy it by hand' % plane)
    taken = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Name):
            taken.add(node.id)
    k = 2
    new = '%s_%d' % (plane, k)
    while new in taken:
        k += 1
        new = '%s_%d' % (plane, k)
    edits = []
    for node in ast.walk(tree):
        if (isinstance(node, ast.Call) and _call_name(node) in ('symmetry_image', 'symmetry_images') and len(node.args) >= 2 and isinstance(node.args[0], ast.Name)
                and node.args[0].id in names and isinstance(node.args[1], ast.Name) and node.args[1].id == plane):
            arg = node.args[1]
            l0, l1 = arg.lineno - 1, arg.end_lineno - 1
            edits.append([l0, _char_col(lines[l0], arg.col_offset), l1, _char_col(lines[l1], arg.end_col_offset), new])
    if not edits:
        raise ValueError('no image of these points is made through %s' % plane)
    first, last = stmt.lineno - 1, stmt.end_lineno - 1
    text = lines[first:last + 1]
    if not text[0].lstrip().startswith(plane):
        raise ValueError('%s is not written as a line of its own: copy it by hand' % plane)
    text[0] = text[0].replace(plane, new, 1)
    edits.append([last, len(lines[last]), last, len(lines[last]), '\n' + '\n'.join(text) + '\n' + new])
    return json.dumps({'edits': edits, 'label': 'Give the body a symmetry plane of its own (%s)' % new, 'line': -1, 'name': ''})


def edit(arg):
    ''' `arg` is JSON {op: 'insert' | 'toggle' | 'snap' | 'symmetry_link' | 'symmetry_add' | 'points_transform', source, line (1-based, the line that displays the model), x, y, z (for insert: where), call (for
        snap), symmetry (for symmetry_link: {var, centre, elements}), names / how / scale (for points_transform: see _transform_points)} '''
    from fieldes import runner
    from fieldes.stdlib import curves as C
    req = json.loads(arg)
    if req['op'] == 'snap':
        return _snap(req)
    if req['op'] == 'points_transform':
        return _transform_points(req)
    if req['op'] == 'body_position':
        return _body_position(req)
    if req['op'] == 'body_position_write':
        return _body_position_write(req)
    if req['op'] == 'symmetry_add':
        return _add_to_symmetry(req)
    if req['op'] == 'split_plane':
        return _split_plane(req)
    source, line, op = req['source'], int(req['line']), req['op']
    tree = ast.parse(source)
    lines = source.split('\n')
    name = _display_name(tree, line)
    if name is None:
        raise ValueError('nothing is displayed on that line')
    obj = runner.last_globals.get(name)
    if op == 'toggle':
        # (a mirror image of a point -- `p2 = symmetry_image(p1, symmetry_1, 1)` -- has no kind of its own, it follows its original: a double click on it changes the original, and the image with it
        # (the user, 2026-11-04, H148: "Double clicking a point on the mirrored points does not change it"))
        for _ in range(4):
            made = _assignments_of(tree, name, line)
            last = made[-1] if made else None
            if last is None or _call_name(last.value) != 'symmetry_image' or not last.value.args or not isinstance(last.value.args[0], ast.Name):
                break
            name = last.value.args[0].id
            obj = runner.last_globals.get(name)
        return _toggle(tree, lines, name, line, obj)
    if op == 'symmetry_link':
        return _link_symmetry(tree, lines, name, line, obj, req)
    if op == 'radius':
        return _radius_var(tree, lines, name, line, int(req['index']))
    if op == 'insert' and obj is not None and getattr(obj, '_morph_faces', None) is not None:
        return _insert_morph(tree, lines, source, name, line, obj, req)
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
    size, reach = _between(obj, piece, press, i, V)
    edits = []
    select = -1
    if isinstance(elt, ast.Name):
        indent = ' ' * owner.col_offset
        # (the new point is as thick -- the scale of a point is how thick a sweep is there -- and reaches as far as the line does where it is put: between the two points it is put between,
        # not the default 1 -- the user, 2026-10-28: "inserting a point on a path extrude, when you have edited the size, just adds a point at a default size")
        block = ['%s = point(' % new, '    position=(var(%s), var(%s), var(%s)),' % (x, y, z), '    rotation=(var(0), var(0), var(0)),',
                 '    scale=(var(%s), var(%s), var(%s)),' % (size, size, size)] + ['    influence=var(%s),' % reach] + [')', new]
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


def _floor_figures(x, figures):
    ''' `x` rounded DOWN to `figures` significant figures: a number that must not pass a limit (the radius of a sphere point stays within half the shortest edge) '''
    import math
    if not x > 0:
        return 0.0
    e = math.floor(math.log10(x)) - (figures - 1)
    return round(math.floor(x / 10.0 ** e + 1e-9) * 10.0 ** e, 10)


def _toggle(tree, lines, name, line, obj):
    ''' A double click on the dot of a point of a profile or a curve: a corner becomes a spline point, a spline point a round corner (in a profile -- in a curve it goes back to a
        corner), a round corner a corner again.  The point says what it is with `smooth=` and `radius=`: a keyword that says what the profile says anyway is left out '''
    effective = getattr(obj, '_curve_smooth', None)
    default = getattr(obj, '_curve_default', False)
    if effective is None:
        raise ValueError('%s is not a point of a profile or a curve' % name)
    # A point of a polyhedron stays a corner: the polyhedron is a body of flat triangles, its points move and nothing else (the user, 2026-11-02, F37a: "DOES NOT LET YOU CHANGE ITS POINTS SO THAT IT WILL
    # ALWAYS STAY A POLYHEDRON"): the double click changes nothing there
    for st_ in tree.body:
        if (isinstance(st_, ast.Assign) and isinstance(st_.value, ast.Call) and _call_name(st_.value) == 'morph_polyhedron'
                and st_.value.args and isinstance(st_.value.args[0], ast.List)
                and any(isinstance(e_, ast.Name) and e_.id == name for e_ in st_.value.args[0].elts)):
            return json.dumps({'edits': [], 'label': '', 'line': -1, 'name': name})
    rounded = bool(getattr(obj, '_curve_round', False))
    # (a profile and a body of points have round corners -- the third thing a double click makes of a point --, a curve does not)
    rounds = getattr(obj, '_curve_kind', None) in ('profile', 'morph')
    assign = next((a for a in _assignments_of(tree, name, line) if _call_name(a.value) == 'point'), None)
    if assign is None:
        raise ValueError('%s is not written as point(...): what it is in a profile cannot be changed there' % name)
    call = assign.value
    state = 'round' if rounded else ('spline' if effective else 'corner')
    new_state = {'corner': 'spline', 'spline': 'round' if rounds else 'corner', 'round': 'corner'}[state]
    # What the point says of itself now.  A round corner is a corner with a radius: it says no `smooth=` (a radius beats the profile's default), and its radius starts at a quarter of
    # its shorter edge
    smooth = {'corner': False, 'spline': True, 'round': None}[new_state]
    if smooth is not None and smooth == bool(default):
        smooth = None                                              # (what the profile says anyway)
    radius = None
    if new_state == 'round':
        edge = float(getattr(obj, '_curve_edge', 0.0) or 0.0)
        # (a quarter of the shorter edge on a body of points too, as in a profile: a strong radius, and the influence of the point can double it, up to half the edge, the strongest rounding a body of points
        # takes -- a ball at that corner.  It started at that half, so that the influence did nothing above 1: the user, 2026-11-02, "ITS INFLUENCE STILL DOES NOTHING AND IT IS STILL NOT A REAL SPHERE")
        if getattr(obj, '_curve_kind', None) == 'morph':
            # (a sphere point of a body: half the shortest edge at it -- the most it can show, morph._SPHERE_LIMIT -- is the radius at which the corner is a true sphere, a body of eight of them is a ball; rounded DOWN to two
            # figures so that it stays within that limit.  It IS the number the wheel and the ring change, in mm (see Point.__init__).  The user, 2026-11-03: "the value for that radius should be chosen automatically with the
            # scale of the bounding box for the body, so that it looks properly spherical ... NOT this weird rounded cube it does currently by default" -- a quarter of the edge gave the rounded cube)
            radius = _num_text(_floor_figures(0.5 * edge, 2)) if edge > 0 else '1'
        else:
            radius = _num_text(float('%.2g' % (0.25 * edge))) if edge > 0 else '1'
    source = '\n'.join(lines)
    kept = [ast.get_source_segment(source, n) for n in call.args]
    # (a sphere point of a body says its radius in mm and no influence: the number the wheel changes is the radius itself)
    dropped = ('smooth', 'radius', 'influence') if new_state == 'round' and getattr(obj, '_curve_kind', None) == 'morph' else ('smooth', 'radius')
    kept += [ast.get_source_segment(source, k) for k in call.keywords if k.arg not in dropped]
    if smooth is not None:
        kept.append('smooth=%r' % smooth)
    if radius is not None:
        kept.append('radius=var(%s)' % radius)             # (a var(): the number that the arc of the corner drags in the viewport)
    text = '%s(%s)' % (ast.get_source_segment(source, call.func), ', '.join(kept))
    edits = [[call.lineno - 1, _char_col(lines[call.lineno - 1], call.col_offset), call.end_lineno - 1, _char_col(lines[call.end_lineno - 1], call.end_col_offset), text]]
    label = {'corner': 'corner', 'spline': 'spline point', 'round': 'round corner'}[new_state]
    return json.dumps({'edits': edits, 'label': '%s: %s' % (name, label), 'line': -1, 'name': name})
