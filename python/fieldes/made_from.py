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
