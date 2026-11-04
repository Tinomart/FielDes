'''
What a model was made from.

A statement `leg = path_extrude(leg_profile, leg_path)` makes a model out of other models of the script: the names it reads are its inputs -- the children it has in the model
tree.  This module notes them on the model it makes (`leg._made_from = [leg_profile, leg_path]`), whatever the function is: nothing of it is in the function, so every
operation, a library one or a block of your own, has it.  What looks for the parts of a model that can be dragged -- the profiles, the lines and the points it is made of -- to
carry them with its images (see `symmetries.symmetry_images`) walks this record.

The statements are run by `runner` (the application and its command line), by `app_package` (an app) and by `scripts/run_example.py`: each calls `reads` before a statement
runs -- it may give one of the names it reads a new value, as `leg = place(leg, ...)` does -- and `note` after it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast

__all__ = ['reads', 'note']


def reads(p, namespace):
    ''' The models the statement `name = <expression>` reads, as they are before it runs (in the order the names stand in it), or None for a statement that is not of that form '''
    if not isinstance(p, ast.Assign) or len(p.targets) != 1 or not isinstance(p.targets[0], ast.Name):
        return None
    from fieldes.shape import Shape
    seen, out = set(), []
    for n in ast.walk(p.value):
        if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load) and n.id not in seen:
            seen.add(n.id)
            v = namespace.get(n.id)
            if isinstance(v, Shape):
                out.append(v)
    return out


def note(p, namespace, before):
    ''' The statement has run: the model it made keeps the models it read (`before`, from `reads`) -- not itself: a function that gives its input back made nothing '''
    if not before:
        return
    from fieldes.shape import Shape
    value = namespace.get(p.targets[0].id)
    if not isinstance(value, Shape):
        return
    made = [m for m in before if m is not value]
    if made:
        try:
            value._made_from = made
        except AttributeError:
            pass
        _note_feature(value, made)
        _note_carried(value)


def carried(shape, depth=0):
    ''' The gizmos (`_gizmo`: mode, about, move, rotate, scale) of the lowest parts a model is made of that are placed by numbers a gizmo drags: a part is the lowest where it has such numbers
        of its own, the models a part is made of are not looked into.  Points are not parts (a profile, a line, a body of points is placed through its points, which the viewport shares the gizmo of) '''
    from fieldes.shape import Shape
    out = []
    if depth > 24:
        return out
    for m in getattr(shape, '_made_from', None) or ():
        if hasattr(m, 'xyz'):
            continue
        own = getattr(m, '_gizmo', None)
        if own is not None and len(own) == 5 and any(isinstance(v, Shape) for part in own[2:5] for v in part):
            out.append(own)
            continue
        out.extend(carried(m, depth + 1))
    return out


def _note_carried(value):
    ''' A model made of others that has no numbers of its own to be placed by carries the gizmos of its parts (`value._carried`): the viewport moves, turns and scales those with it -- the one
        position of an operation is where its parts put it, and the operation and its parts stay on top of each other (the user, 2026-11-03, H93: "the operation and the underlying parts should
        always stay on top of each other ... only a one way link is necessary").  Nothing of it is written in the script: the parts and the operation are what the script says they are '''
    if getattr(value, '_gizmo', None) is not None or hasattr(value, 'xyz') or callable(getattr(value, '_remake', None)) or getattr(value, '_sweep_ppos', None) is not None:
        return
    found, seen = [], set()
    for g in carried(value):
        if id(g) not in seen:
            seen.add(id(g))
            found.append(g)
    if found:
        try:
            value._carried = tuple(found)
        except AttributeError:
            pass


def _smallest_feature(shape, depth=0):
    ''' The size of the smallest feature of what a model is, in mm, or None: the pitch of a threaded rod (`_feature_size`) -- scaled by the placement the rod was given -- or, for a model
        made of others, the smallest of theirs '''
    from fieldes.shape import Shape
    if depth > 8 or not isinstance(shape, Shape):
        return None
    node, own = shape, None
    for _ in range(8):
        own = getattr(node, '_feature_size', None)
        node = getattr(node, '_placed_from', None)
        if own is not None or node is None:
            break
    if own is None:
        sizes = [f for f in (_smallest_feature(m, depth + 1) for m in getattr(shape, '_made_from', None) or ()) if f]
        return min(sizes) if sizes else None
    own = float(own)
    gizmo = getattr(shape, '_gizmo', None)
    if gizmo is not None and len(gizmo) > 4:
        try:
            from fieldes.stdlib.curves import value_of
            own *= min(abs(float(value_of(c) if isinstance(c, Shape) else c)) for c in gizmo[4])
        except (TypeError, ValueError):
            pass
    return own if own > 1e-9 else None


def _note_feature(value, made):
    ''' A model made of others has the smallest feature of theirs: a difference of a threaded rod and a cylinder is drawn at a resolution that shows the turns of the thread, not at the one the
        cylinder asks for (the user, 2026-10-28: the rim of the hole was torn lumps) '''
    node = value
    for _ in range(8):
        if getattr(node, '_feature_size', None) is not None:
            return
        node = getattr(node, '_placed_from', None)
        if node is None:
            break
    sizes = [f for f in (_smallest_feature(m) for m in made) if f]
    if sizes:
        try:
            value._feature_size = min(sizes)
        except AttributeError:
            pass
