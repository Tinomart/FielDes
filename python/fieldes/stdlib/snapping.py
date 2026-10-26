'''
Snapping one body onto another: select the faces, edges and points on the two bodies, and press S.

A snap is a move: the body is put where it goes, nothing new is made.  FielDes works out the move and writes it where the gizmo writes one -- into the body's own `position=` and
`rotation=` (see `place`) -- so the mesh that is drawn is simply carried to its new place, with no new model and no new calculation.

    foot = select_surface(bracket, seed=(0, 0, 0), angle=5)            # a face of the bracket
    foot_corner = corner_at(bracket, (10, 10, 0))                       # and a corner of it
    top = select_surface(plate, seed=(30, 20, 12), angle=5)            # a face of the plate
    hole_edge = edge_point_at(plate, (30, 20, 12))                     # and a point of an edge of it

    # select the four, press S: the bracket's call gets position=(var(...), ...) and rotation=(var(...), ...), the bracket standing on the plate with its corner on that point,
    # and the places of `foot` and `foot_corner` are moved with it, so they stay on the bracket

The faces (`select_surface`), the edges (`edge_at`) and the points (`corner_at`, `edge_point_at`, `circle_center_at`, `face_center`, `point`) that were made on **two bodies** are given in
the order they were selected, and the FIRST body -- the one the first feature is on -- is moved onto the second.  They are sorted out like this:

    * the faces: the body is turned by the least turn that makes its face lie against the other body's face -- the two facing each other, the direction in which the bodies do
      not go into one another;  with a second face on each body, the turn about that contact lines them up as well (two flat faces of a box on the table's edge)
    * no face on one of the bodies: the edges are made parallel (the least turn), and a place is only moved
    * the points: the body is moved so that its point is on the other body's point; a face or an edge that has no point of its own is met by its middle, so two faces alone meet
      middle to middle (a cone's base put on a cylinder's end, on its axis)
    * a feature that is on no body -- a `point(...)` of the scene -- is a place of the second body

The move is the body's own placement, so it is worked out ON TOP of the position and rotation the body has: a body that was placed already is moved from where it is.  A body
that has a part excluded from an analysis (see `exclude`) is moved as a whole, as it is by the gizmo.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import numbers

__all__ = []

# The names of the functions that make a feature on a body from a PLACE of the scene -- `select_surface(body, seed)`, `corner_at(body, near)` ... --: the place is where the body was when the
# feature was made, so when the body is moved the place is moved with it (see feature_edits)
FEATURES_AT = ('select_surface', 'corner_at', 'edge_point_at', 'edge_at', 'circle_center_at')


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _neg(a):
    return (-a[0], -a[1], -a[2])


def _unit(a, what):
    n = math.sqrt(_dot(a, a))
    if not n > 1e-12:
        raise ValueError('snap: %s has no direction (its normals add up to nothing)' % what)
    return (a[0] / n, a[1] / n, a[2] / n)


def _axis_angle(k, angle):
    ''' The matrix of a turn of `angle` radians about the unit vector k (Rodrigues) '''
    c, s = math.cos(angle), math.sin(angle)
    t = 1.0 - c
    x, y, z = k
    return [[c + x * x * t, x * y * t - z * s, x * z * t + y * s],
            [y * x * t + z * s, c + y * y * t, y * z * t - x * s],
            [z * x * t - y * s, z * y * t + x * s, c + z * z * t]]


def _apply(m, v):
    return tuple(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3))


def _matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _taking(a, b):
    ''' The matrix of the least turn that takes the unit vector a to the unit vector b '''
    k = _cross(a, b)
    s = math.sqrt(_dot(k, k))
    c = _dot(a, b)
    if s < 1e-9:
        if c > 0:
            return _axis_angle((0.0, 0.0, 1.0), 0.0)
        # (opposite: any turn of half a circle about an axis across a)
        across = min(((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), key=lambda e: abs(_dot(e, a)))
        return _axis_angle(_unit(_cross(a, across), 'an axis'), math.pi)
    return _axis_angle((k[0] / s, k[1] / s, k[2] / s), math.atan2(s, c))


def _place(point, what):
    ''' Where a point is, as three numbers: a `point`, a tuple, or anything with a position '''
    if isinstance(point, (tuple, list)) and len(point) == 3 and all(isinstance(c, numbers.Number) for c in point):
        return tuple(float(c) for c in point)
    here = getattr(point, '_point_pos', None)
    if here is not None:
        return tuple(float(c) for c in here)
    xyz = getattr(point, 'xyz', None)
    if xyz is not None:
        from fieldes.stdlib.points import _position_of
        here = _position_of(xyz)
        if here is not None:
            return here
    raise ValueError('snap: %s is a place: a point(...), or (x, y, z) -- a corner_at(...) or an edge_point_at(...) is one' % what)


def _body_key(body):
    ''' What says that two features are on the same body: the tree of the body (the same variable of a script is the same tree) '''
    ptr = getattr(body, 'ptr', None)
    try:
        return int(ptr) if ptr is not None else id(body)
    except (TypeError, ValueError):
        return id(body)


def _describe(feature, index):
    ''' What a feature gives to a snap: {kind: 'face' | 'edge' | 'point', body (the shape it is on, or None), at (a place: the point, or the middle of the face or edge), normal (a
        face: the way it faces, out of its body), axis (an edge: its direction)} '''
    what = 'feature %d' % (index + 1)
    samples = getattr(feature, 'sample_points', None)
    if samples is not None and getattr(feature, 'sample_normals', None):
        # a selected face: the way it faces on the average (the samples' normals point out of the body it was picked on), and the middle of its area
        n = len(samples) // 3
        at = (sum(samples[0::3]) / n, sum(samples[1::3]) / n, sum(samples[2::3]) / n)
        normals = feature.sample_normals
        return {'kind': 'face', 'body': getattr(feature, 'shape', None), 'at': at,
                'normal': _unit((sum(normals[0::3]), sum(normals[1::3]), sum(normals[2::3])), what)}
    points = getattr(feature, 'points', None)
    if points and not hasattr(feature, 'xyz') and len(points) >= 2:
        # an edge: a line through points -- the direction from the first to the last, and its middle
        first, last = _place(points[0], what), _place(points[-1], what)
        axis = _unit(tuple(last[i] - first[i] for i in range(3)), what)
        return {'kind': 'edge', 'body': getattr(feature, '_on', None), 'at': tuple(0.5 * (first[i] + last[i]) for i in range(3)), 'axis': axis}
    try:
        at = _place(feature, what)
    except ValueError:
        raise ValueError('snap: %s is not something to snap by: a face (select_surface), an edge (edge_at) or a point (corner_at, edge_point_at, circle_center_at, face_center, point)'
                         % what)
    return {'kind': 'point', 'body': getattr(feature, '_on', None), 'at': at}


def _anchor(group):
    ''' The place a group of features is met at: the first point, else the middle of the first face or edge '''
    for it in group:
        if it['kind'] == 'point':
            return it['at']
    return group[0]['at']


def _motion(features):
    ''' The rigid move that puts the first body the features are on onto the second: {body (the shape that moves), matrix (the turn, a 3 x 3 list), own (the place of the body that
        is turned about and met by), at (where that place goes)}.  A place p of the body goes to matrix * (p - own) + at.

        features   faces, edges and points (the ones made on the bodies), in any order: the first body is the one the first of them is on '''
    if len(features) < 2:
        raise ValueError('snap: give it the features on the two bodies -- faces, edges and points -- at least one on each (select them all in FielDes and press S)')
    items = [_describe(f, i) for i, f in enumerate(features)]
    order = []
    for it in items:
        if it['body'] is not None and _body_key(it['body']) not in order:
            order.append(_body_key(it['body']))
    if not order:
        raise ValueError('snap: none of the features says which body it is on (make them with select_surface(body, ...), corner_at(body, ...) ...): the first body is the one that moves')
    if len(order) > 2:
        raise ValueError('snap: the features are on %d bodies: it snaps one body onto another, so they are on two' % len(order))
    moving = order[0]
    mine = [it for it in items if it['body'] is not None and _body_key(it['body']) == moving]
    there = [it for it in items if it['body'] is None or _body_key(it['body']) != moving]
    if not there:
        raise ValueError('snap: all the features are on the same body: give it some on the body it goes onto (or a point(...) of the scene)')
    body = mine[0]['body']

    own, onto = _anchor(mine), _anchor(there)
    mn = [it['normal'] for it in mine if it['kind'] == 'face']
    tn = [it['normal'] for it in there if it['kind'] == 'face']
    me = [it['axis'] for it in mine if it['kind'] == 'edge']
    te = [it['axis'] for it in there if it['kind'] == 'edge']
    turn = _axis_angle((0.0, 0.0, 1.0), 0.0)
    contact = None                                  # (the line the body can still be turned about: the normal of the contact)
    first, second = None, None                      # (the directions of the body that the second turn lines up with the other body's, and theirs)
    if mn and tn:
        # the faces face each other: the least turn that makes the body's face face the other way from the other body's
        contact = tn[0]
        turn = _taking(mn[0], _neg(contact))
        first, second = mn[1:], tn[1:]
    elif me and te:
        # parallel edges: the lesser of the two turns
        a, b = me[0], te[0]
        if _dot(a, b) < 0:
            b = _neg(b)
        contact = b
        turn = _taking(a, b)
        first, second = me[1:], te[1:]
    matrix = turn
    if contact is not None and first and second:
        # (a second face or edge on each body: turned about the contact until they lie the same way)
        u = _apply(turn, first[0])
        w = second[0]
        u = tuple(u[i] - _dot(u, contact) * contact[i] for i in range(3))
        w = tuple(w[i] - _dot(w, contact) * contact[i] for i in range(3))
        if _dot(u, u) > 1e-9 and _dot(w, w) > 1e-9:
            angle = math.atan2(_dot(_cross(u, w), contact), _dot(u, w))
            if not (mn and tn) and abs(angle) > math.pi / 2.0:
                angle += -math.pi if angle > 0 else math.pi          # (edges: a line has no sense)
            matrix = _matmul(_axis_angle(contact, angle), matrix)
    return {'body': body, 'matrix': matrix, 'own': own, 'at': onto}


def _euler(r):
    ''' The angles (x, y, z, in degrees) whose placement turns by the matrix r: x first, then y, then z, which is Rz * Ry * Rx (see symmetries._turn_matrix) '''
    s = max(-1.0, min(1.0, r[2][0]))
    if abs(s) < 1.0 - 1e-9:
        x, z = math.atan2(r[2][1], r[2][2]), math.atan2(r[1][0], r[0][0])
    else:
        # (y is a quarter turn: x and z turn about the same line -- all of it is given to x)
        x, z = math.atan2(-s * r[0][1], -s * r[0][2]), 0.0
    y = math.asin(s)
    turn = lambda a: math.atan2(math.sin(a), math.cos(a))               # (an angle between -180 and 180 degrees)
    # a turn has a second set of angles, (x + 180, 180 - y, z + 180): the one with the smaller angles is written (a half turn about y is (0, 180, 0), not (180, 0, 180))
    other = (turn(x + math.pi), turn(math.pi - y), turn(z + math.pi))
    if abs(other[0]) + abs(other[1]) + abs(other[2]) < abs(x) + abs(y) + abs(z) - 1e-9:
        x, y, z = other
    return tuple(math.degrees(v) for v in (x, y, z))


def _numbers(value, default):
    ''' The three numbers a placement argument has in this run: (x, y, z), numbers or var()s, or `default` for one that is not given (a position of a flat shape may be two) '''
    if value is None:
        return tuple(default)
    from fieldes.stdlib.curves import value_of
    out = [value_of(v) for v in tuple(value)]
    return tuple(out + [0.0] * (3 - len(out)))


def _pivot(body):
    ''' The point a placement turns the body about when the body is placed for the first time: the middle of its box -- of its points, for what is made of points (see handles._place) '''
    if callable(getattr(body, '_remake', None)) and getattr(body, 'points', None):
        from fieldes.stdlib.curves import value_of
        nums = []
        for p in body.points:
            xyz = getattr(p, 'xyz', None)
            if xyz is None:
                xyz = tuple(p) + (0.0,) * (3 - len(p))
            nums.append(tuple(value_of(v) for v in xyz))
        return tuple(0.5 * (min(q[i] for q in nums) + max(q[i] for q in nums)) for i in range(3))
    from fieldes.stdlib.handles import _centre_of
    return tuple(_centre_of(body))


def _clean(v, digits):
    ''' A number written to `digits` places: the faces and points a snap goes by are read from samples of the surface, which say a corner to some 5e-5 mm and a face's turn to some 1e-4 degree --
        a ten-thousandth of a mm and of a degree is what they can say, so 0 is written as 0 and not as the dust of the sum (-0.000031) '''
    v = round(float(v), digits)
    return 0.0 if v == 0 else v


def _placement(features, written=()):
    ''' Where the first body of the features has to be placed to be snapped: {body, position (x, y, z), rotation (x, y, z degrees), scale (whether the call may be given a scale:
        not what is made of points), motion (matrix, own, at: see _motion)}.

        `written` says which of position, rotation and scale the call of the body has (the ones FielDes will write the numbers into): a body whose call has none is placed from where
        it is, and one that has some is moved on top of them.  The body was placed by  c + R S (p - c) + m  (p its place as the definition makes it, c the pivot, S the scale, R the
        rotation, m the position); it is moved by the snap, p -> M (p - own) + at, so it is placed by  R' = M R  and  m' = M (c + m - own) + at - c  (S stays) '''
    from fieldes.stdlib.symmetries import _turn_matrix
    motion = _motion(features)
    body = motion['body']
    args = getattr(body, '_placement_args', None)
    parts = getattr(body, '_placed_parts', None)
    placed = bool(parts is not None and args is not None and any(a in written for a in ('position', 'rotation', 'scale')) and any(v is not None for v in args[:3]))
    if placed:
        m0, r0, c = _numbers(args[0], (0.0, 0.0, 0.0)), _numbers(args[1], (0.0, 0.0, 0.0)), tuple(float(v) for v in parts[1])
    else:
        m0, r0, c = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), _pivot(body)
    R0 = [[float(v) for v in row] for row in _turn_matrix(r0)]
    M, own, at = motion['matrix'], motion['own'], motion['at']
    inside = tuple(c[i] + m0[i] - own[i] for i in range(3))
    moved = _apply(M, inside)
    position = tuple(_clean(moved[i] + at[i] - c[i], 4) for i in range(3))
    rotation = tuple(_clean(v, 4) for v in _euler(_matmul(M, R0)))
    return {'body': body, 'position': position, 'rotation': rotation, 'scale': not (callable(getattr(body, '_remake', None)) and bool(getattr(body, 'points', None))),
            'motion': motion}


def _moved(motion, p):
    ''' A place of the world after the snap: where the point p of the body that moves goes '''
    inside = _apply(motion['matrix'], tuple(p[i] - motion['own'][i] for i in range(3)))
    return tuple(inside[i] + motion['at'][i] for i in range(3))


def _number_node(node):
    ''' (the number a literal is, whether it is written inside var(...)) for `3`, `-2.5`, `var(4)`; else None '''
    import ast
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == 'var' and len(node.args) == 1 and not node.keywords:
        inner = _number_node(node.args[0])
        return None if inner is None or inner[1] else (inner[0], True)
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)) and not isinstance(node.value, bool):
        return float(node.value), False
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
        inner = _number_node(node.operand)
        if inner is None or inner[1]:
            return None
        return (-inner[0] if isinstance(node.op, ast.USub) else inner[0]), False
    return None


def _text(v):
    s = ('%.4f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('', '-0') else s


def feature_edits(source, body_name, motion):
    ''' The edits that move with the body the faces, edges and points that were made on it (see FEATURES_AT): their places are where the body WAS when they were made, so a body that is
        moved needs them moved as well, else `select_surface(bracket, seed=(0, 0, 0))` would look for its face where the bracket is no more.  The places are numbers written in the
        call (`(10, 10, 0)`, `(var(10), var(10), var(0))`): each becomes where the snap takes it, in the same form.

        Returns (edits [[line0, col0, line1, col1, text]], the names of the features that are on the body but whose place is not written as three numbers: they are left as they are).
        The lines are 0-based and the columns count characters, in the script as it is now '''
    import ast
    lines = source.split('\n')

    def col(line, byte_col):
        return len(lines[line].encode('utf-8')[:byte_col].decode('utf-8', 'ignore'))

    edits, left = [], []
    for stmt in ast.walk(ast.parse(source)):
        if not isinstance(stmt, ast.Assign) or not isinstance(stmt.value, ast.Call) or len(stmt.targets) != 1 or not isinstance(stmt.targets[0], ast.Name):
            continue
        call = stmt.value
        if not isinstance(call.func, ast.Name) or call.func.id not in FEATURES_AT or not call.args:
            continue
        if not isinstance(call.args[0], ast.Name) or call.args[0].id != body_name:
            continue
        place = call.args[1] if len(call.args) > 1 else next((k.value for k in call.keywords if k.arg in ('seed', 'near')), None)
        label = stmt.targets[0].id
        if not isinstance(place, (ast.Tuple, ast.List)) or len(place.elts) != 3:
            left.append(label)
            continue
        read = [_number_node(e) for e in place.elts]
        if any(r is None for r in read) or any(e.lineno != e.end_lineno for e in place.elts):
            left.append(label)
            continue
        to = _moved(motion, tuple(r[0] for r in read))
        for e, r, v in zip(place.elts, read, to):
            text = _text(v)
            edits.append([e.lineno - 1, col(e.lineno - 1, e.col_offset), e.end_lineno - 1, col(e.end_lineno - 1, e.end_col_offset), 'var(%s)' % text if r[1] else text])
    return edits, left
