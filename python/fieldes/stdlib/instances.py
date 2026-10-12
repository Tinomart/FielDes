'''
Linked copies: `instance(shape)`.

A linked copy IS another shape -- the same field, so the same surfaces, sizes, colours and every other property -- and it follows it: change the original
(its numbers, its definition, anything it is made of) and the copy changes with it.  What a copy has of its own is only WHERE it is: it can be moved and rotated
(with its gizmo, `gizmo(copy, move=..., rotate=...)`), and nothing else -- no scale knobs, and its surfaces cannot be dragged, since its shape is the
original's.  (A copy that is scaled, or whose surfaces are pulled, is another shape: copy the model's statements instead -- Shift+D in FielDes.)

FielDes writes one with Ctrl+D (or Operation > Repeating > instance):

    plate_2 = instance(plate)  # shadow: plate
    plate_2 = gizmo(
        plate_2,
        move=(var(0), var(0), var(0)),
        rotate=(var(0), var(0), var(0)),
    )

The original is only REFERRED to: the mark `# shadow: plate` keeps it where it is in the model tree (a shadow of it is shown under the copy), whether it is
locked or not -- taking the original into the copy would move its row away and make the copy look like what the original is made of.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['instance']

# What belongs to the original's own statements, not to what the original IS: its gizmo (the copy has its own), its lock, what it was placed from, the pivot of the faces
# that were dragged, whether it is kept in the render cache
_NOT_TAKEN = ('ptr', '_gizmo', '_locked', '_placed_from', '_pivot_shape', '_render_cache', '_literal_seen')


def instance(shape):
    ''' A linked copy of `shape`: the very same shape -- its surfaces, sizes, colours and properties are the original's, and change with it -- that can only be
        moved and rotated, with its gizmo (`gizmo(copy, move=..., rotate=...)`).  Its surfaces cannot be dragged and it cannot be scaled: its shape is the
        original's.  FielDes writes it with Ctrl+D, holding the original as a reference (`# shadow: original`: the original keeps its own row in the model tree,
        locked or not).  A copy that has a shape of its own is a copy of the statements instead (Shift+D) '''
    if not isinstance(shape, Shape):
        raise TypeError('instance(): only a shape can be linked (a body, a 2D shape, a surface, a point ...), not %s -- a number, a material or the result of an '
                        'analysis has no shape to follow' % type(shape).__name__)
    if getattr(shape, '_ctrl_pos', None):
        raise TypeError('instance(): a sketch or a curve is made of its points, which are the models to place -- link the points, or copy the sketch with its '
                        'statements (Shift+D)')
    # (the copy is made without the class's own constructor: some classes -- a point, a surface -- are built from numbers and fields, not from a tree)
    out = type(shape).__new__(type(shape))
    Shape.__init__(out, lib.libfive_tree_copy(shape.ptr))
    for name, value in shape.__dict__.items():
        if name not in _NOT_TAKEN:
            out.__dict__[name] = value
    out._linked = True              # (FielDes: it is dragged by its gizmo only, which has no scale)
    out._no_handles = True          # (its surfaces are the original's: nothing of it is exposed to be dragged)
    out._instance_of = shape
    return out
