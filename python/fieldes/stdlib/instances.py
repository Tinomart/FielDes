'''
Linked copies: `instance(shape)`.

A linked copy IS another shape -- the same field, so the same surfaces, sizes, colours and every other property -- and it follows it: change the original
(its numbers, its definition, anything it is made of) and the copy changes with it.  What a copy has of its own is only WHERE it is: its POSITION and its ROTATION are
its own, not the original's -- moving or turning the original does not move or turn the copy, and the other way round -- with its gizmo, `gizmo(copy, move=..., rotate=...)`.
Nothing else is its own: no scale knobs (a scale the original has is part of that shape, and the copy has it too).  Its surfaces CAN be dragged, as the original's can,
and they are linked: both are pulled by the numbers of the original's `handles(...)`, so dragging a surface of the copy moves the original's and every other copy's, and the
other way round.  (A copy that has a shape of its own is another thing: copy the model's statements instead -- Shift+D in FielDes.)

FielDes writes one with Ctrl+D (or Operations > Repeating > instance):

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
_NOT_TAKEN = ('ptr', '_gizmo', '_locked', '_placed_from', '_placed_parts', '_pivot_shape', '_render_cache', '_literal_seen')


def instance(shape):
    ''' A linked copy of `shape`: the very same shape -- its surfaces, sizes, colours and properties are the original's, and change with it -- that can only be
        moved and rotated, with its gizmo (`gizmo(copy, move=..., rotate=...)`).  It cannot be scaled: its shape is the original's -- and so are the numbers its
        surfaces are pulled by, so dragging a surface of the copy or of the original moves both.  FielDes writes it with Ctrl+D, holding the original as a reference (`# shadow: original`: the original keeps its own row in the model tree,
        locked or not).  A copy that has a shape of its own is a copy of the statements instead (Shift+D) '''
    if not isinstance(shape, Shape):
        raise TypeError('instance(): only a shape can be linked (a body, a 2D shape, a surface, a point ...), not %s -- a number, a material or the result of an '
                        'analysis has no shape to follow' % type(shape).__name__)
    if getattr(shape, '_ctrl_pos', None):
        raise TypeError('instance(): a profile or a curve is made of its points, which are the models to place -- link the points, or copy the profile with its '
                        'statements (Shift+D)')
    # What the copy is made of: the shape as it was BEFORE the gizmo of the original moved and turned it -- scaled as the gizmo scaled it -- so that where the original is, and how
    # it is turned, are the original's own; the copy has its own gizmo for that (FielDes writes it with the numbers the original's gizmo has, so that it starts on the original)
    source = shape
    parts = getattr(shape, '_placed_parts', None)
    if parts is not None:
        from fieldes.stdlib.transforms import scale_xyz
        base, about, scale = parts
        if all(isinstance(s, (int, float)) and not isinstance(s, bool) and s == 1 for s in scale):
            source = base
        else:
            source = scale_xyz(base, scale, about)
            for name in ('_bounds', '_bounds_loose'):
                source.__dict__.pop(name, None)
            for name, value in base.__dict__.items():
                if name not in ('ptr', '_bounds', '_bounds_loose') and name not in source.__dict__:
                    source.__dict__[name] = value
    # (the copy is made without the class's own constructor: some classes -- a point, a surface -- are built from numbers and fields, not from a tree)
    out = type(source).__new__(type(source))
    Shape.__init__(out, lib.libfive_tree_copy(source.ptr))
    for name, value in source.__dict__.items():
        if name not in _NOT_TAKEN:
            out.__dict__[name] = value
    out._linked = True              # (FielDes: it is dragged by its gizmo only, which has no scale)
    out._no_handles = True          # (it has no handles() line of its own: it is pulled by the numbers of the original's)
    out._instance_of = shape
    return out
