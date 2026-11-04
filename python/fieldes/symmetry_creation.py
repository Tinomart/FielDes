'''
What the viewport writes when something is made while a symmetry is selected (Shift+S): the images of what is made, as ONE model under it --
`leg_images = symmetry_images(leg, symmetry_1)` (all the images of a body, a 2D shape or a point, or `only=(1, 3)` of them) -- `symmetry_1` is the symmetry
model.  What an operation makes of other models (a path_extrude, a union ...) gets `symmetry_images(x, symmetry_1, distinct=True)`: the images that are not the model itself.  A surface
or a field is written with a model for each image, `symmetry_image(model, symmetry_1, 3)`: 3 is the number of the image in the symmetry (a surface that the menu makes has none: it is made once, H92).

The symmetry arrives in a request as {var: 'symmetry_1', centre: [x, y, z], elements: [{mirror: [nx, ny, nz] | null, turn: [[ax, ay, az], degrees] | null}, ...]}: its name, and
every image but the identity as the places it takes a point to -- for what has to be known now (an image that is the model itself is not made; where the images of the points of
a stroke are).  This file makes the statements of the images from it: of a single model (a body, a point ...), and of a stroke -- the points of a profile, a line or a spline,
whose images are made again through the images of the points, so that every point of an image follows the point it is the image of.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

# What follows the call a template writes, written under the model's display line: the images (`call()` of menu_catalog)
POST = '\n#@@@#\n'


def _matrix(mirror, turn):
    ''' The 3x3 matrix (rows) of an image -- the mirror image across the plane with normal `mirror`, then the turn about the axis of `turn` -- and the matrix of its way back '''
    m = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    if mirror is not None:
        n = mirror
        length = math.sqrt(sum(c * c for c in n))
        n = tuple(c / length for c in n)
        m = [[(1.0 if i == j else 0.0) - 2.0 * n[i] * n[j] for j in range(3)] for i in range(3)]
    if turn is not None:
        axis, degrees = turn
        length = math.sqrt(sum(c * c for c in axis))
        x, y, z = (c / length for c in axis)
        t = float(degrees) * math.pi / 180.0
        c, s = math.cos(t), math.sin(t)
        k = 1.0 - c
        r = [[c + x * x * k, x * y * k - z * s, x * z * k + y * s],
             [y * x * k + z * s, c + y * y * k, y * z * k - x * s],
             [z * x * k - y * s, z * y * k + x * s, c + z * z * k]]
        m = [[sum(r[i][l] * m[l][j] for l in range(3)) for j in range(3)] for i in range(3)]
    return m, [[m[j][i] for j in range(3)] for i in range(3)]


class Element:
    ''' One image of the symmetry: its number (1 ..), and the mirror image across the plane through the centre with normal `mirror` (or none), then the turn about `axis` through
        the centre by `degrees` (or none), as it is now '''

    def __init__(self, index, centre, mirror, turn):
        self.index = index
        self.centre = tuple(float(c) for c in centre)
        self.mirror = None if mirror is None else tuple(float(c) for c in mirror)
        self.turn = None if turn is None else (tuple(float(c) for c in turn[0]), float(turn[1]))
        self.matrix, self.back = _matrix(self.mirror, self.turn)
        self.improper = self.mirror is not None

    def apply(self, p):
        c = self.centre
        d = [p[i] - c[i] for i in range(3)]
        return tuple(c[i] + sum(self.matrix[i][j] * d[j] for j in range(3)) for i in range(3))

    def unapply(self, p):
        c = self.centre
        d = [p[i] - c[i] for i in range(3)]
        return tuple(c[i] + sum(self.back[i][j] * d[j] for j in range(3)) for i in range(3))


def elements_of(symmetry):
    ''' The images of a request's symmetry, or [] '''
    if not symmetry:
        return []
    centre = symmetry.get('centre') or (0, 0, 0)
    return [Element(k, centre, e.get('mirror'), e.get('turn')) for k, e in enumerate(symmetry.get('elements') or [], 1)]


def image_statement(var, image_var, element, symmetry_var):
    ''' The statements of the image of the model `var`: it, with the model it follows and the symmetry held by reference, and the line that shows it '''
    return '%s  # shadow: %s, %s\n%s\n' % ('%s = symmetry_image(%s, %s, %d)' % (image_var, var, symmetry_var, element.index), var, symmetry_var, image_var)


def _keeps_the_plane(e):
    ''' The image of a flat shape (in the plane z = 0) is flat in that plane again: the image does not leave it '''
    m = e.matrix
    return (abs(abs(m[2][2]) - 1.0) < 1e-6 and abs(m[2][0]) < 1e-6 and abs(m[2][1]) < 1e-6 and abs(m[0][2]) < 1e-6 and abs(m[1][2]) < 1e-6
            and abs(e.apply((0.0, 0.0, 0.0))[2]) < 1e-6)


def shape_of(call_text, prelude=''):
    ''' What a call makes (the call is the library's, with numbers in it): None when it cannot be told here '''
    try:
        namespace = {}
        exec('from fieldes import *', namespace)
        if prelude:
            exec(prelude, namespace)
        return eval(call_text, namespace)
    except Exception:
        return None


def kept_elements(call_text, elements, around, size, prelude=''):
    ''' Which of the images of the model that `call_text` makes are images of their own: those that are not the model itself and not another image -- a sphere on a plane of the
        symmetry has none, a box off every plane has all of them.  Told by the field: the image of a model is the model's field at the places the way back takes it from, so two
        images are the same shape where their fields are the same at a spread of places round the model (`around`, `size`: where it is made and how large).  A flat shape (a 2D
        shape is drawn in the plane z = 0) has the images that stay in that plane.  When the call cannot be evaluated here every image is kept '''
    if not elements:
        return []
    try:
        import random
        from fieldes.stdlib.fields import evaluate
        from fieldes.kinds import kind_of
        namespace = {}
        exec('from fieldes import *', namespace)
        if prelude:
            exec(prelude, namespace)
        shape = eval(call_text, namespace)
        if kind_of(shape) == 'profile':
            allowed = [i for i, e in enumerate(elements) if _keeps_the_plane(e)]
            if len(allowed) < len(elements):
                kept = kept_elements(call_text, [elements[i] for i in allowed], around, size, prelude)
                return [allowed[k] for k in kept]
        if hasattr(shape, 'xyz') and all(isinstance(c, (int, float)) for c in shape.xyz):
            # (a point is where it is: an image that is on it, or on another image, is not one of its own)
            kept, seen = [], [tuple(float(c) for c in shape.xyz)]
            for i, e in enumerate(elements):
                q = e.apply(seen[0])
                tol = 1e-4 * max(1.0, size)
                if any(math.dist(q, s) < tol for s in seen):
                    continue
                seen.append(q)
                kept.append(i)
            return kept
        rng = random.Random(7)
        reach = 1.6 * size + max(math.dist(around, e.centre) for e in elements)
        pts = [tuple(around[k] + rng.uniform(-reach, reach) for k in range(3)) for _ in range(120)]
        base = evaluate(shape, pts)
        scale = max(1.0, max(abs(v) for v in base))
        kept, seen = [], [base]
        for i, e in enumerate(elements):
            values = evaluate(shape, [e.unapply(p) for p in pts])           # (the image's field at p: the model's at the place the way back takes p from)
            if any(max(abs(a - b) for a, b in zip(values, other)) < 2e-4 * scale for other in seen):
                continue
            seen.append(values)
            kept.append(i)
        return kept
    except Exception:
        return list(range(len(elements)))


def images_line(var, symmetry_var, arguments=''):
    ''' The statement of all the images of the model `var` and the line that shows it: `var_images = symmetry_images(var, symmetry_var<arguments>)` '''
    images = '%s_images' % var
    return '%s  # shadow: %s, %s\n%s\n' % ('%s = symmetry_images(%s, %s%s)' % (images, var, symmetry_var, arguments), var, symmetry_var, images)


def post_for(var, call_text, request, prelude=''):
    ''' The images of the single model made by `call_text` (named `var`): the statements that go under its display line, '' when it has none '''
    symmetry = request.get('symmetry') or {}
    elements = elements_of(symmetry)
    if not elements or not symmetry.get('var'):
        return ''
    around = tuple(float(request.get(k, 0.0)) for k in ('x', 'y', 'z'))
    keep = kept_elements(call_text, elements, around, float(request.get('scale', 10.0)), prelude)
    if not keep:
        return ''
    kind = None
    shape = shape_of(call_text, prelude)
    if shape is not None:
        try:
            from fieldes.kinds import kind_of
            kind = kind_of(shape)
        except Exception:
            kind = None
    if kind == 'surface':
        # (a surface is made once: its image in the symmetry in use was a second surface, the same and slightly offset, that nobody asked for -- the user, 2026-11-03, H92: "There is only one option and it says
        # new surface, which when clicked creates to surfaces, both the same, slightly offset ... That is obviously completely wrong behaviour, fix it".  A surface that is wanted in the symmetry is added by
        # hand, Shift+S, as every model is)
        return ''
    if kind == 'field':
        # (a field has no union that means anything: a model for each image)
        return ''.join(image_statement(var, '%s_m%d' % (var, k), elements[i], symmetry['var']) for k, i in enumerate(keep, 1))
    # One model for all the images: the union of those of a body or a 2D shape, the set of the images of a point
    only = '' if len(keep) == len(elements) else ', only=(%s)' % ', '.join(str(elements[i].index) for i in keep)
    return images_line(var, symmetry['var'], only)


def _join_meeting(strokes):
    ''' Strokes that meet at their ends -- half a shape drawn on one side of a plane and its image on the other, which meet on the plane -- are one outline: joined end to end by
        the point they share; one that comes round to where it began is a closed shape.  (closed, open) '''
    strokes = [list(s) for s in strokes]
    closed = []
    again = True
    while again:
        again = False
        for i in range(len(strokes)):
            s = strokes[i]
            if len(s) >= 4 and s[0] == s[-1]:
                closed.append(s[:-1])
                del strokes[i]
                again = True
                break
            for j in range(i + 1, len(strokes)):
                a, b = strokes[i], list(strokes[j])
                if a[-1] == b[0]:
                    merged = a + b[1:]
                elif a[-1] == b[-1]:
                    merged = a + b[::-1][1:]
                elif a[0] == b[-1]:
                    merged = b + a[1:]
                elif a[0] == b[0]:
                    merged = b[::-1] + a[1:]
                else:
                    continue
                strokes[i] = merged
                del strokes[j]
                again = True
                break
            if again:
                break

    def unique(lists):
        out = []
        for one in lists:
            if not any(set(one) == set(other) and len(one) == len(other) for other in out):
                out.append(one)
        return out
    return unique(closed), unique(strokes)


def stroke_structure(points, elements, name, close, tol):
    ''' What a stroke makes with the symmetry: the points it is made of -- the ones that were put down, then the images of them (an image that lands on a point that is there
        is that point) -- and the shapes through them.
        Returns (images, shapes): images = [(i, e, k)] the image points to make (the image of the point i by the element e, as node k); shapes = [(call name, [nodes])], the first
        one the model itself, the others its images.  A node is an index: the points that were put down are 0 .. len(points) - 1, the image points follow '''
    n = len(points)
    nodes = [tuple(p) for p in points]
    made = []

    def node(q, i, e):
        for k, pos in enumerate(nodes):
            if math.dist(pos, q) < tol:
                return k
        nodes.append(q)
        made.append((i, e, len(nodes) - 1))
        return len(nodes) - 1

    strokes = [list(range(n))]
    for e, el in enumerate(elements):
        ids = [node(el.apply(points[i]), i, e) for i in range(n)]
        if el.improper:
            ids.reverse()                  # (a mirror image turns the way round: reversed, it faces as the stroke does)
        if not any(set(ids) == set(other) for other in strokes):
            strokes.append(ids)
    if close:
        shapes = [('profile', s) for s in strokes]
    else:
        loops, rest = _join_meeting(strokes) if len(strokes) > 1 else ([], strokes)
        shapes = [('profile', s) for s in loops] + [(name, s) for s in rest]
    return made, shapes


def partner_links(points, elements, tol):
    ''' A body made of points that is its own image in every image of the symmetry -- a cube made with its middle on a plane of the symmetry: each point has its mirror partner among
        the points -- needs no image of its own, and no point on top of another: the symmetry is applied to every individual point, and an image that lands on a point that is there IS that
        point (the user, 2026-11-02: "if one already has one that is the same thing ... shouldnt create another point on top of the current ones").  Returns {j: (i, e)}: the point j is the
        image of the earlier point i by the image number e (0 ..), which is how it is written -- `symmetry_image(p_i, symmetry, e + 1)` -- so that moving i moves j.  A point that is its own
        image (on a plane) or the first of its partners is not in it.  None when some point has no partner in some image: the body is mirrored as a whole then, as it was '''
    def index_at(q):
        for k, p in enumerate(points):
            if math.dist(p, q) < tol:
                return k
        return None
    for el in elements:
        for p in points:
            if index_at(el.apply(p)) is None:
                return None
    return partner_pairs(points, elements, tol)


def partner_pairs(points, elements, tol):
    ''' The points that have an exact mirror partner earlier in the list: {j: (i, e)}, the point j is the image of the earlier point i by the image number e (0 ..).  Not all or nothing, as
        `partner_links` is: the points that have no partner are simply not in it -- a symmetry applied LATER to a shape that was made without one links the points that have an exact
        partner under it and leaves the others as they are (the user, 2026-11-02: "a symmetry applies to points that already have an exact mirror partner").  A point that is its own image
        (on a plane) is not in it, and a point that is an image already has no partners of its own (its partners are the images of the first of them) '''
    links = {}
    for j, pj in enumerate(points):
        for i in range(j):
            if i in links:
                continue                                     # (an image already: its partners are the images of the first of them)
            hit = next((e for e, el in enumerate(elements) if math.dist(el.apply(points[i]), pj) < tol), None)
            if hit is not None and math.dist(points[i], pj) >= tol:
                links[j] = (i, hit)
                break
    return links


def simple_images(points, elements, tol):
    ''' The images (elements) of a stroke that are strokes of their own touching nothing: no point of an image is on a point of the stroke or of another image.  Those are written as
        the images of the stroke, `symmetry_image(stroke, symmetry, k)`, like the image of any model -- the stroke's points have their dots on them, and whatever is done to the stroke
        is done to them.  None when an image meets the stroke or another image (a half drawn on one side of a plane and its image on the other, which are one outline): the points
        of the images are what joins them then (see stroke_structure) '''
    nodes = [tuple(p) for p in points]
    keep = []
    for el in elements:
        images = [el.apply(p) for p in points]
        on = [any(math.dist(q, pos) < tol for pos in nodes) for q in images]
        if all(on):
            # (the stroke is its own image: nothing to make -- unless it falls on something else, which is not told here)
            if all(math.dist(q, p) < tol for q, p in zip(images, points)) or all(math.dist(q, p) < tol for q, p in zip(images, reversed(points))):
                continue
            return None
        if any(on):
            return None
        nodes.extend(images)
        keep.append(el)
    return keep


def point_name(var, node, n, made):
    ''' The name of a node: `var_p3` for the third point put down, `var_p3_m2` for its image by the second image '''
    if node < n:
        return '%s_p%d' % (var, node + 1)
    for i, e, k in made:
        if k == node:
            return '%s_p%d_m%d' % (var, i + 1, e + 1)
    raise ValueError('no such node')
