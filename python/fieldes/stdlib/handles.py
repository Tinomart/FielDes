''' Handles: edit a shape -- an imported part too -- by dragging in FielDes's viewport.

    FielDes's model tree has a handles button on every shape and part.  It cycles
    three modes, so the ways of editing never get in each other's way:

      gizmo    the part's own move arrows, rotation rings and scale knobs:
               `handles()` writes, under the shape's definition,

                   part = handles(part, move=(var(0), var(0), var(0)),
                                  rotate=(var(0), var(0), var(0)),
                                  scale=(var(1), var(1), var(1)), mode='gizmo')

               so what the gizmo does is ordinary script text, like any dragged
               var().  The shape is `part` scaled about its centre, rotated (x,
               then y, then z; degrees) and moved.
      handles  FielDes's own handles: hover a surface of the shape and drag it.
               Any shape with `var()` numbers has them; for a shape written with
               plain numbers -- a primitive, or a part imported from a STEP file,
               which is made of primitives -- `expose()` makes the numbers that
               place its surfaces variables:

                   part = expose(part, [var(6), var(40), ...])

               Dragging a face changes the number of that face (a plane's
               position, a radius) in the script text.
      lock     nothing is draggable: neither the gizmo nor the surface

    Nothing is stored anywhere but in the script: delete the `expose(...)` and
    `handles(...)` lines of a part (FielDes's Reimport does) and it is the file's
    version again. '''
import ctypes
import math
import struct

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib.transforms import move as _move, rotate_x, rotate_y, rotate_z, scale_xyz

__all__ = ['handles', 'expose']

MODES = ('gizmo', 'handles', 'lock')

# What an imported part carries that a transformed copy must keep: its
# features and name (roi_resolution, the model tree).  (Its own resolution is
# not copied: roi_resolution sets it later, on the part, and FielDes reads it
# from the part a placed shape was placed from.)
_CARRIED = ('_step_metrics', '_part_name', '_step_ref')


def _is_number(v):
    return isinstance(v, (int, float))


def _centre_of(shape):
    ''' The middle of a shape's bounding box: known for an imported part, else
        estimated by sampling it (briefly); (0, 0, 0) when neither works '''
    b = getattr(shape, '_bounds', None)
    if not b:
        try:
            import time
            from fieldes.app_support import _estimate_bounds
            b = _estimate_bounds([shape], time.time() + 2.0)
        except Exception:
            b = None
    if not b:
        return (0.0, 0.0, 0.0)
    return tuple(0.5 * (float(b[0][i]) + float(b[1][i])) for i in range(3))


def _carry(out, shape):
    for name in _CARRIED:
        if hasattr(shape, name):
            try:
                setattr(out, name, getattr(shape, name))
            except AttributeError:
                pass
    # (the part's own resolution, roi_resolution(...), is set on the part later in
    # the script than this call: FielDes looks for it on the part a shape was made from)
    out._placed_from = shape


def handles(shape, move=(0, 0, 0), rotate=(0, 0, 0), scale=(1, 1, 1), about=None, mode=None,
            show=None):
    ''' Scales, rotates and moves a shape, with the gizmo FielDes shows on it.

        scale: (x, y, z) factors about the shape's centre, `rotate`: (x, y, z)
        rotations in degrees about the same centre (x first, then y, then z),
        `move`: (x, y, z) translation.  Write the numbers as var(...) to drag
        them: FielDes's model tree does that for you.
        about: the centre (default: the middle of the shape's box)
        mode: 'gizmo' (the default) shows the gizmo, 'handles' lets the shape's own
        surfaces be dragged instead (see expose()), 'lock' makes it undraggable;
        the placement stays in every mode.  (show=True / False, as older scripts
        wrote it, is 'gizmo' / 'lock'; mode= wins.) '''
    if mode is None:
        mode = 'lock' if show is not None and not show else 'gizmo'
    if mode not in MODES:
        raise ValueError("handles(): mode is 'gizmo', 'handles' or 'lock'")
    if len(move) != 3 or len(rotate) != 3 or len(scale) != 3:
        raise ValueError('handles(): move=, rotate= and scale= take three numbers each')
    c = tuple(about) if about is not None else _centre_of(shape)
    to_rad = math.pi / 180.0
    out = shape
    if not all(_is_number(s) and s == 1 for s in scale):
        out = scale_xyz(out, tuple(scale), c)
    if not all(_is_number(r) and r == 0 for r in rotate):
        for rot, angle in ((rotate_x, rotate[0]), (rotate_y, rotate[1]), (rotate_z, rotate[2])):
            if not (_is_number(angle) and angle == 0):
                out = rot(out, angle * to_rad, c)
    # (always a new shape, to hang the handles on: the part itself is shared)
    out = _move(out, tuple(move))
    _carry(out, shape)
    # (the box of a part moved by plain numbers only follows them)
    b = getattr(shape, '_bounds', None)
    if b and all(_is_number(m) for m in move) and all(_is_number(r) and r == 0 for r in rotate) \
            and all(_is_number(s) and s == 1 for s in scale):
        out._bounds = tuple(tuple(float(b[k][i]) + move[i] for i in range(3)) for k in range(2))
    # what FielDes draws the gizmo from
    out._handles = (mode, tuple(float(v) for v in c), tuple(move), tuple(rotate), tuple(scale))
    return out


def _float32_text(v):
    ''' The shortest text that reads back as the same 32-bit float (20.0, not 2e+01) '''
    for digits in range(1, 10):
        s = '%.*g' % (digits, v)
        if struct.unpack('f', struct.pack('f', float(s)))[0] == v:
            return repr(float(s))
    return repr(v)


def _exposed_count(shape):
    fn = getattr(lib, 'libfive_tree_expose_count', None)
    if fn is None:
        raise RuntimeError('this FielDes library is too old for expose()')
    return fn(shape.ptr)


def exposed_values(shape):
    ''' The numbers expose() makes variables, in its order, as the text to write them as '''
    n = _exposed_count(shape)
    buf = (ctypes.c_float * max(1, n))()
    lib.libfive_tree_expose_values(shape.ptr, buf)
    return [_float32_text(buf[i]) for i in range(n)]


def expose(shape, values):
    ''' A shape whose surfaces can be dragged: the numbers that place its
        surfaces -- a plane's position, a radius, the faces of a box -- are
        replaced by `values`, in the order of their appearance in the shape
        (FielDes writes them as var(...) with the shape's own numbers, which
        makes them draggable: hover a surface, drag it).  Orientations and the
        placement of the whole part are not exposed, so a dragged face
        moves and neither turns nor takes the rest of the part along.

        With the shape's own numbers the result is the very same shape.  If the
        shape has a different number of them (the part was changed in the STEP
        file) it raises ValueError: reimport the part. '''
    values = list(values)
    n = _exposed_count(shape)
    if len(values) != n:
        raise ValueError('expose(): the shape has %d numbers to expose, %d were given -- the part has '
                         'changed since (Reimport it in the model tree)' % (n, len(values)))
    # (the wrapped numbers stay alive until the call has returned: plain numbers wrap to shapes
    # that would otherwise be freed with their pointers still in `arr`)
    wrapped = [Shape.wrap(v) for v in values]
    arr = (ctypes.c_void_p * max(1, n))(*[w.ptr for w in wrapped])
    p = lib.libfive_tree_expose(shape.ptr, arr, n)
    if not p:
        raise ValueError('expose(): the shape could not be rebuilt with those numbers')
    out = Shape(p)
    _carry(out, shape)
    # (what the importer, roi() and the viewport read off a part: its box, its colouring, its exact regions)
    from fieldes.stdlib import cad_import
    for name in ('_bounds', '_render_hint') + tuple(cad_import._CARRIED):
        if hasattr(shape, name):
            try:
                setattr(out, name, getattr(shape, name))
            except AttributeError:
                pass
    cad_import._carry_exact(out, shape)
    return out
