'''
Linked copies: `instance(shape)`.

A linked copy IS another shape -- the same field, so the same surfaces, sizes, colours and every other property -- and it follows it: change the original
(its numbers, its definition, anything it is made of) and the copy changes with it.  What a copy has of its own is where it is, how it is turned and how big it is: its POSITION, its ROTATION and its SCALE
are its own, not the original's -- moving, turning or scaling the original does not move, turn or scale the copy, and the other way round (the user, 2026-11-12: "all of the gizmo operations are completely
independent for clones") -- with its gizmo, which drags the `position=`, `rotation=` and `scale=` of the copy's call.  A body of points is placed by its points, so its copy is the body in a frame of its own: the points
moved together move the original and leave the copy where it is.  A copy of a profile or a line is placed through the images of its points, moved and turned; a copy written without `scale=` (a script of an earlier
version) is scaled as its original is.  Its surfaces CAN be dragged, as the original's can,
and they are linked: both are pulled by the numbers of the original's `handles(...)`, so dragging a surface of the copy moves the original's and every other copy's, and the
other way round.  (A copy that has a shape of its own is another thing: copy the model's statements instead -- Shift+D in FielDes.)

FielDes writes one with Ctrl+D (or Operations > Repeating > instance):

    plate_2 = instance(
        plate,
        position=(var(0), var(0), var(0)),
        rotation=(var(0), var(0), var(0)),
        scale=(var(1), var(1), var(1)),
    )  # shadow: plate

The original is only REFERRED to: the mark `# shadow: plate` keeps it where it is in the model tree (a shadow of it is shown under the copy), whether it is
locked or not -- taking the original into the copy would move its row away and make the copy look like what the original is made of.
Every model is cloned the same way, `copy = instance(model)`: a model that is not a shape -- a material, the conditions or the result of an analysis, a lattice cell -- is its own value
(no place to give it, nothing is solved again).  What is made of points -- a profile, a polyline, a spline -- is cloned like any shape: its copy is placed by its own placement arguments, as the
same outline through the images of the original's points, so a point dragged, a number typed or a point added in the original changes every copy of it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import copy

from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['instance']

# What belongs to the original's own statements, not to what the original IS: its gizmo (the copy has its own), its lock, what it was placed from, the pivot of the faces
# that were dragged, whether it is kept in the render cache
_NOT_TAKEN = ('ptr', '_gizmo', '_gizmo_centre', '_gizmo_centre_live', '_handle_numbers', '_locked', '_placed_from', '_placed_parts', '_pivot_shape', '_render_cache', '_literal_seen', '_outline_mode', '_display')

# What a body of points has of its own that its copy has not: the control points and the way to make the body again through other points, and what the viewport draws natively from its points -- the copy is meshed from its field
_BODY_OWN = ('_ctrls', '_remake', '_remake_mapped', '_morph_faces', '_morph_tape', '_morph_inputs', '_morph_controls', '_morph_vstart', '_morph_vctrl', '_morph_vweight', '_morph_tris', '_editable', '_placement_args')


def instance(shape, position=None, rotation=None, scale=None, alpha=None, origin=None):
    ''' A linked copy of `shape`: the very same shape -- its surfaces, sizes, colours and properties are the original's, and change with it -- with a place of its own: its `position=`, `rotation=` and `scale=`, the
        numbers of its gizmo, are the copy's, not the original's (the user, 2026-11-12, H174: "all of the gizmo operations are completely independent for clones"): moving, turning or scaling the original does not
        move, turn or scale the copy, and the other way round.  A copy that says no `scale=` (a script written before) is scaled as its original is, as it was.  The numbers its surfaces are pulled by are the
        original's, so dragging a surface of the copy or of the original moves both.  FielDes writes it with Ctrl+D, holding the original as a reference (`# shadow: original`: the original keeps its own row in the model tree,
        locked or not).  A copy that has a shape of its own is a copy of the statements instead (Shift+D) '''
    out = _linked_copy(shape, scale is not None)
    if not isinstance(out, Shape):
        return out
    from fieldes.stdlib.placement import _apply
    placed = _apply(out, position, rotation, scale, alpha, origin)
    _carry_parts(placed, shape)
    return placed


def _stage_map(stages):
    ''' The map a chain of placements makes of a place, `(rows, shift, where, frozen)`: `rows` and `shift` its numbers now (x' = A x + b: each stage `(about, move, rotate, scale)` is x' = L (x - c) + c + s, and the stages follow
        one another), `where(xyz)` the same as three EXPRESSIONS of the numbers of the stages (the var()s of the copy's gizmo: it follows them as they are dragged), `frozen` the numbers of the stages that are Shapes '''
    from fieldes.stdlib.curves import value_of
    from fieldes.stdlib.symmetries import _add, _mul, _turn_matrix
    # (the numbers of the whole map, x' = A x + b: each stage is x' = L (x - c) + c + s, and the stages follow one another)
    A = [[1.0 if i == j else 0.0 for j in range(3)] for i in range(3)]
    b = [0.0, 0.0, 0.0]
    frozen = []
    turns = []
    for about, move, rotate, scale in stages:
        R = _turn_matrix(rotate)
        S = [float(value_of(s)) for s in scale]
        L = [[float(value_of(R[i][j])) * S[j] for j in range(3)] for i in range(3)]
        c = [float(value_of(v)) for v in about]
        s = [float(value_of(v)) for v in move]
        offset = [c[i] - sum(L[i][j] * c[j] for j in range(3)) + s[i] for i in range(3)]
        A = [[sum(L[i][k] * A[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
        b = [sum(L[i][k] * b[k] for k in range(3)) + offset[i] for i in range(3)]
        frozen.extend(v for v in tuple(move) + tuple(rotate) + tuple(scale) if isinstance(v, Shape))
        turns.append((about, move, R, scale))
    rows = A
    centre = (0.0, 0.0, 0.0)
    shift = tuple(b)

    def where(xyz):
        cur = tuple(xyz)
        for about, move, R, scale in turns:
            rel = [_add(cur[i], _mul(-1.0, about[i])) for i in range(3)]
            scaled = [_mul(scale[i], rel[i]) for i in range(3)]
            cur = tuple(_add(_add(about[i], _add(_add(_mul(R[i][0], scaled[0]), _mul(R[i][1], scaled[1])), _mul(R[i][2], scaled[2]))), move[i]) for i in range(3))
        return cur
    return rows, shift, where, frozen


def _placed_part(part, stages):
    ''' What the copy draws of one PART of what it is made of -- a point, a line or an outline through points -- placed as the copy is: through every placement the copy went through, from the first
        (`instance`) to the last (the `place` that FielDes writes under it: `copy = instance(...)` then `copy = place(copy, ...)`), each `(about, move, rotate, scale)`: scaled about `about`, turned,
        moved (numbers, or expressions of the var()s of the copy's gizmo).  A point is a dot that is the image of the original point (pressed and dragged, it moves the ORIGINAL point, and the copy
        follows); a line or an outline is made again through the images of its points, with the same dots.  The same as a mirror image carries (symmetries._carried_images), through the copy's own
        place instead of a symmetry's '''
    from fieldes.stdlib.points import Point, image_dot
    rows, shift, where, frozen = _stage_map(stages)
    centre = (0.0, 0.0, 0.0)
    if hasattr(part, 'xyz'):
        return [image_dot(part, where(part.xyz), rows, centre, shift, frozen)]
    dots, shown = [], []
    for p in part.points:
        xyz = getattr(p, 'xyz', None)
        if xyz is None:
            xyz = tuple(p) + (0.0,) * (3 - len(p))
        x, y, z = where(xyz)
        if hasattr(p, 'xyz'):
            dot = image_dot(p, (x, y, z), rows, centre, shift, frozen)
            shown.append(dot)
        else:
            dot = Point(x, y, z)                    # (a point that is only numbers has no dot to drag)
        dots.append(dot)
    made = part._remake(dots)
    made._edit_map = (tuple(v for row in rows for v in row), centre, shift)     # (where a place of the original is taken to the copy's: what a press on its line is taken back by)
    made._edit_reversed = False
    made._editable = True
    made._instance_of = part
    made._no_handles = True
    made._outline_gizmo = False
    made._no_gizmo = True
    # (the line and its dots are ITEMS of what the copy displays, one after the other, as the images of a symmetry bring them (symmetries._carried_images): the viewport asks each item for its own display, and
    # a line that carried its dots in `_image_dots` answered with a list inside the list, which it does not draw -- seen in a window on his file: nothing was drawn)
    return [made] + shown


def _carry_parts(copy, original):
    ''' H174 (the user, 2026-11-14, his file StillFucked.py: "The clones body DOES NOT clone properly with every single part of it intact ... path extrude already HAVING LOGIC TO THIS PROPERLY when mirrored"): the copy
        of a model that is made of other models -- a path_extrude of a profile and a line, a body of points -- carries its PARTS as an image of it in a symmetry does: the lines and the outlines it is made from through its
        own place, and a dot for each of their points, so that what the original is made of is seen where the copy is, and a dot pressed and dragged moves the original's point.  They are no models and have no rows:
        the copy draws them with itself.  An original that has a place of its own (position=, rotation=, scale=) is not looked into: its parts are where it was made '''
    try:
        # The placements the copy went through, the first one first: `instance` makes the first, and the `place` under it -- which is what FielDes writes for a copy (`copy = instance(...)`, then
        # `copy = place(copy, ...)`, the numbers of its gizmo) -- the second.  The copy of his file was the `place`, which the first version did not look at: nothing of the parts was drawn (found 2026-11-14 in a
        # window on his StillFucked.py: no line and no dots at the clone)
        links, node = [], copy
        while True:
            parts_of = getattr(node, '_placed_parts', None)
            args = getattr(node, '_placement_args', None)
            if parts_of is None or args is None:
                break
            links.append((parts_of, args))
            node = parts_of[0]
        links.reverse()
        root = node
        original = getattr(root, '_instance_of', None)
        if not links or original is None or getattr(original, '_placed_parts', None) is not None:
            return
        from fieldes.stdlib.placement import _copy_of, _minus, _vec
        from fieldes.stdlib.symmetries import _carried_parts
        copy.__dict__.pop('_display', None)         # (one that the copy was made with is the placement before this one's)
        parts = _carried_parts(root)
        if not parts:
            return
        stages = []
        middle = _middle_of_points(original)
        if middle is not None:
            # (H174, his picture of 2026-11-14: the points of a clone appeared beside it.  The copy of a body of points is the body in ITS OWN frame -- its field moved by minus the middle of the points, `_linked_copy` --
            # so its points are drawn in that frame too, moved by minus the middle, and then placed as the copy is.  Without this each point was off by the middle of the body: nothing where the body is centred on the origin,
            # which is the only body the first checks used)
            stages.append(((0, 0, 0), tuple(m * -1.0 for m in middle), (0, 0, 0), (1, 1, 1)))
        for parts_of, args in links:
            about = tuple(parts_of[1])
            position, rotation, scale = args[0], args[1], args[2]
            rotate = _vec(rotation, 'rotation', (0, 0, 0))
            scaling = _vec(scale, 'scale', (1, 1, 1))
            if position is None:
                move = (0, 0, 0)
            else:
                at = _vec(position, 'position', (0, 0, 0))
                move = tuple(_minus(at[i], about[i]) for i in range(3))
            stages.append((about, move, rotate, scaling))
        extras = []
        native = None
        remake = getattr(original, '_remake_mapped', None)
        points = getattr(original, 'points', None)
        if middle is not None and callable(remake) and points and all(hasattr(p, 'xyz') for p in points):
            # (a body of points is drawn natively from the numbers of its points, nothing meshed from its field -- and so is its copy, as the image of it in a symmetry is: its dots, in the order of the points,
            # and its surface made from the control points of the original's taken through the map of the copy's placements, `remake_mapped`.  It follows a point that is dragged as the original does, in
            # real time, and is meshed as the original is: H174, the user, 2026-11-14, "The duplicate also doesnt sweep in real time, like the mirrors do and meshes differently ... Objects that dont mesh
            # directly from the field should obviously clone the sweep, just like it works for mirroring")
            for p in points:
                extras.extend(_placed_part(p, stages))
        else:
            for part in parts:
                extras.extend(_placed_part(part, stages))
        if not extras:
            return
        plain = _copy_of(copy)                      # (the copy as it is drawn by itself: made before it has a display of its own, which would ask for itself)
        if middle is not None and callable(remake) and len(extras) == len(points or ()):
            native = _native_body(remake, stages, extras, plain)
        copy._display = lambda: [native if native is not None else plain] + list(extras)
    except Exception:
        # (the copy is drawn without its parts; with FIELDES_TIMING set the log says why)
        import os
        if os.environ.get('FIELDES_TIMING'):
            import traceback
            traceback.print_exc()
        return


def _native_body(remake, stages, dots, plain):
    ''' The copy of a body of points as the viewport draws a body: the surface of the original taken through the map of the copy's placements (`remake_mapped` of the original: its control points through the map,
        three expressions to three, the weights and the triangles the same -- turned over when the map turns the body inside out), the dots the images of its points.  The attributes of the copy as it is drawn by
        itself -- its gizmo, its place, its colour -- go with it.  None when it cannot be made: the copy is drawn from its field then '''
    from fieldes.stdlib.symmetries import _determinant
    rows, shift, where, frozen = _stage_map(stages)
    try:
        made = remake(list(dots), where, plain, _determinant(rows) < 0)
    except Exception:
        import os
        if os.environ.get('FIELDES_TIMING'):
            import traceback
            traceback.print_exc()
        return None
    for name, value in plain.__dict__.items():
        if name != 'ptr' and name not in made.__dict__:
            made.__dict__[name] = value
    made._no_gizmo = False              # (the numbers of its gizmo are its own: the dots have the points')
    made._editable = False
    return made


def _middle_of_points(shape):
    ''' The middle of the box of the points a body of points is made of, as three expressions of the numbers of its points (a number where a coordinate is one): the frame the body is moved in when its points are moved
        together -- `position=` of a body is that middle, H151 -- or None for what is not a body of points '''
    ctrls = getattr(shape, '_ctrls', None)
    if not ctrls or not callable(getattr(shape, '_remake', None)):
        return None
    middle = []
    for k in range(3):
        coords = []
        for c in ctrls:
            expr = getattr(c, 'expr', None)
            coords.append(expr[k] if expr is not None else c.pos[k])
        low = high = Shape.wrap(coords[0])
        for v in coords[1:]:
            low = low.min(Shape.wrap(v))
            high = high.max(Shape.wrap(v))
        middle.append((low + high) * 0.5)
    return tuple(middle)


def _linked_copy(shape, own_scale):
    if not isinstance(shape, Shape):
        # A model that is not a shape -- a material, the conditions of an analysis, the result of one, a lattice cell, a number -- has no place to give a copy of its own: the copy IS the value
        # (the script runs again when the original changes, so the copy follows it), and an analysis is not solved again
        try:
            return copy.copy(shape)
        except Exception:
            return shape
    # What the copy is made of: the shape as it was BEFORE the gizmo of the original moved and turned it -- scaled as the gizmo scaled it, unless the copy has a scale of its own -- so that where the original is, and how
    # it is turned, are the original's own; the copy has its own gizmo for that (FielDes writes it with the numbers the original's gizmo has, so that it starts on the original)
    source = shape
    parts = getattr(shape, '_placed_parts', None)
    if parts is not None:
        from fieldes.stdlib.transforms import scale_xyz
        base, about, scale = parts
        if own_scale or all(isinstance(s, (int, float)) and not isinstance(s, bool) and s == 1 for s in scale):
            source = base
        else:
            source = scale_xyz(base, scale, about)
            for name in ('_bounds', '_bounds_loose'):
                source.__dict__.pop(name, None)
            for name, value in base.__dict__.items():
                if name not in ('ptr', '_bounds', '_bounds_loose') and name not in source.__dict__:
                    source.__dict__[name] = value
            # The box of the scaled shape, from the box the part had before: the app sizes the render of a shape from it, and a shape that does not know its box (the search for it
            # fails for a part that has var()s: the gizmo's scale is some) is meshed at the scene's plain resolution -- a small circle or a small body is a few cells wide there, and
            # a copy that was not drawn at all.  The numbers are the original's gizmo's: plain, or var()s of the script being run
            box = getattr(base, '_bounds', None)
            if box:
                from fieldes.stdlib.handles import _placed_box
                placed = _placed_box(box, about, (0, 0, 0), (0, 0, 0), scale)
                if placed is not None:
                    source._bounds = placed
                    if getattr(base, '_bounds_loose', False):
                        source._bounds_loose = True
    middle = _middle_of_points(source)
    if middle is not None:
        # A body of points is placed by its points: its own `position=` is the middle of them, and moving the body moves them all.  A copy made of the same points would move with it.  The copy is the body in ITS OWN frame
        # instead -- the field moved by minus the middle of the points, an expression of their numbers, so that the points moved together leave it where it was -- and is placed by the numbers of its own gizmo.  What
        # the viewport draws natively from the points is the original's: the copy is meshed from its field (H174, the user, 2026-11-12: "that copy moves together with the original, meaning they are always on top of each other")
        from fieldes.stdlib.transforms import move
        out = move(source, tuple(m * -1.0 for m in middle))
        for name, value in source.__dict__.items():
            if name in _NOT_TAKEN or name in _BODY_OWN or name in ('_bounds', '_bounds_loose') or name in out.__dict__:
                continue
            out.__dict__[name] = value
        out._no_gizmo = False           # (the numbers of its gizmo are its own: the points are not what moves it)
        out._editable = False
        # (its box: the body's box less the middle of the points, the numbers they have in the script being run.  A move by an expression leaves the copy with no box, and a place under the copy -- FielDes writes
        # one for every copy that is moved -- takes the middle of the copy from its box: where the box could not be found (the search fails for a part with var()s, as it does in the window) the middle was (0, 0, 0)
        # and the moved copy stood too far by the middle of the body.  H174, his picture of 2026-11-14)
        box = getattr(source, '_bounds', None)
        if box:
            try:
                from fieldes.stdlib.curves import value_of
                shift = [float(value_of(v)) for v in middle]
                out._bounds = (tuple(float(box[0][i]) - shift[i] for i in range(3)), tuple(float(box[1][i]) - shift[i] for i in range(3)))
                if getattr(source, '_bounds_loose', False):
                    out._bounds_loose = True
            except Exception:
                pass
    else:
        # (the copy is made without the class's own constructor: some classes -- a point, a surface -- are built from numbers and fields, not from a tree)
        out = type(source).__new__(type(source))
        Shape.__init__(out, lib.libfive_tree_copy(source.ptr))
        for name, value in source.__dict__.items():
            if name not in _NOT_TAKEN:
                out.__dict__[name] = value
    out._linked = True              # (FielDes: it has the placement of its own, and a gizmo with all three parts)
    out._no_handles = True          # (it has no handles() line of its own: it is pulled by the numbers of the original's)
    out._instance_of = shape
    if getattr(source, '_outline_gizmo', False):
        # (what is made of points -- a profile, a line -- has a gizmo without numbers, because its points have them: a copy is placed by numbers of its own, which place() applies
        # through the images of its points)
        out._no_gizmo = False
        out._outline_gizmo = False
        out._editable = False
    return out
