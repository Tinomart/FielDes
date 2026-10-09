''' Handles: edit a shape -- an imported part too -- by dragging in FielDes's viewport.

    Two ways of editing by dragging work together, and the first has priority where they meet:

      the gizmo    the part's own move arrows, rotation rings and scale knobs: `handles()` writes, under the shape's
                   definition,

                       part = handles(part, move=(var(0), var(0), var(0)),
                                      rotate=(var(0), var(0), var(0)),
                                      scale=(var(1), var(1), var(1)))

                   so what the gizmo does is ordinary script text, like any dragged var().  The shape is `part`
                   scaled about its centre, rotated (x, then y, then z; degrees) and moved.  When the gizmo is shown
                   is the `mode` of that line (the model tree's gizmo button, key E, goes through them):

                       'click'   (the default) the gizmo is shown while the shape is selected, i.e. after you click it
                       'never'   it is never shown, so that it cannot get in the way
                       'always'  it is shown on the shape whether it is selected or not

      handles      FielDes's own handles: hover a surface of the shape and drag it.  This is always there, whatever
                   the mode of the gizmo.  Any shape with `var()` numbers has them; for a shape written with plain
                   numbers -- a primitive, or a part imported from a STEP file, which is made of primitives --
                   `expose()` makes the numbers that place its surfaces variables:

                       part = expose(part, [var(6), var(40), ...])

                   Dragging a face changes the number of that face (a plane's position, a radius) in the script
                   text.  FielDes writes the line itself when you select the shape.

    A lock button beside it (key R) is a separate switch: a locked shape cannot be dragged at all, neither by the
    gizmo nor by its surfaces, and keeps its gizmo mode, which comes back when it is unlocked:

                   part = lock(part)

    Nothing is stored anywhere but in the script: delete the `expose(...)` and `handles(...)` lines of a part
    (FielDes's Reimport does) and it is the file's version again; the `lock(...)` line only keeps it from being
    dragged. '''
import ctypes
import math
import struct

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib.transforms import move as _move, rotate_x, rotate_y, rotate_z, scale_xyz, _lazy_once

__all__ = ['handles', 'expose', 'lock']

MODES = ('click', 'never', 'always')

# What an imported part carries that a transformed copy must keep: its
# features and name (roi_resolution, the model tree).  (Its own resolution is
# not copied: roi_resolution sets it later, on the part, and FielDes reads it
# from the part a placed shape was placed from.)
_CARRIED = ('_step_metrics', '_part_name', '_step_ref', '_kind', 'xyz', 'size')


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
    from fieldes.kinds import keep_kind
    keep_kind(out, shape)


def _placed_box(box, centre, move, rotate, scale):
    ''' The box a part's box becomes when it is scaled about `centre`, rotated about it (x, then y, then z: a box that holds the part
        whatever the direction of the turn) and moved; the numbers are plain, or var()s of the script being run.  None when a
        number cannot be read '''
    nums = _var_numbers(list(move) + list(rotate) + list(scale) + list(centre))
    if nums is None:
        return None
    m, r, s, c = nums[0:3], nums[3:6], nums[6:9], nums[9:12]
    lo = [c[i] + (float(box[0][i]) - c[i]) * s[i] for i in range(3)]
    hi = [c[i] + (float(box[1][i]) - c[i]) * s[i] for i in range(3)]
    lo, hi = [min(lo[i], hi[i]) for i in range(3)], [max(lo[i], hi[i]) for i in range(3)]
    if any(abs(a) > 1e-12 for a in r):
        reach = max(math.sqrt(sum((corner[i] - c[i]) ** 2 for i in range(3)))
                    for corner in ((x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])))
        lo, hi = [c[i] - reach for i in range(3)], [c[i] + reach for i in range(3)]
    return tuple(tuple(v[i] + m[i] for i in range(3)) for v in (lo, hi))


def handles(shape, move=(0, 0, 0), rotate=(0, 0, 0), scale=(1, 1, 1), about=None, mode=None):
    ''' Scales, rotates and moves a shape, with the gizmo FielDes shows on it.

        scale: (x, y, z) factors about the shape's centre, `rotate`: (x, y, z)
        rotations in degrees about the same centre (x first, then y, then z),
        `move`: (x, y, z) translation.  Write the numbers as var(...) to drag
        them: FielDes's model tree does that for you.
        about: the centre (default: the middle of the shape's box)
        mode: when FielDes shows the gizmo: 'click' (the default) while the shape is selected, 'never', or
        'always'.  The shape's own surfaces (see expose()) can be dragged whatever the mode, and the gizmo has
        priority where it is shown.
        To make a shape undraggable, lock it: `shape = lock(shape)`. '''
    if mode is None:
        mode = 'click'
    if mode in ('gizmo', 'handles'):
        raise ValueError("handles(): mode='%s' is gone: a shape's surfaces can always be dragged now, and the gizmo's "
                         "mode is 'click' (shown while the shape is selected: the default), 'never' or 'always'" % mode)
    if mode not in MODES:
        raise ValueError("handles(): mode is 'click', 'never' or 'always' (a shape is locked with lock(shape))")
    if len(move) != 3 or len(rotate) != 3 or len(scale) != 3:
        raise ValueError('handles(): move=, rotate= and scale= take three numbers each')
    if not isinstance(shape, Shape) and callable(getattr(shape, '_display', None)):
        # The result of an analysis or an optimisation is not a shape: it has no surface of its own to place (what it shows is made from
        # its part).  Placed by nothing it is the result itself; placed by something, there is nothing it could mean
        nums = _var_numbers(list(move) + list(rotate) + list(scale))
        if nums is not None and all(n == 0 for n in nums[:6]) and all(n == 1 for n in nums[6:]):
            return shape
        raise TypeError('handles(): a %s is the result of an analysis, not a shape: it has no surface of its own to move, rotate or scale. '
                        'Place the part it was made from (handles() on that part) -- the result follows it.' % type(shape).__name__)
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
    if hasattr(shape, 'xyz'):
        # (a point that is moved is where it is moved to: its coordinates follow the move)
        out.xyz = tuple((c + m) if not (_is_number(c) and _is_number(m)) else float(c) + float(m)
                        for c, m in zip(shape.xyz, move))
    # (the box of a part follows the numbers it is placed by: plain numbers, or the numbers the var()s have in the script being
    # run.  A placed part that lost its box would have to be searched for its extent by whatever is made from it -- a surface
    # selected on it, a lattice on that -- and that search fails on a mesh)
    b = getattr(shape, '_bounds', None)
    if b:
        placed = _placed_box(b, c, move, rotate, scale)
        if placed is not None:
            out._bounds = placed
    # (the exact distance of an imported part follows the transforms above: see transforms._exact_follows)
    # what FielDes draws the gizmo from
    out._handles = (mode, tuple(float(v) for v in c), tuple(move), tuple(rotate), tuple(scale))
    return out


def lock(shape):
    ''' `shape`, locked: FielDes does not let it be dragged -- neither by the gizmo nor by its surfaces -- until the
        line is deleted.  It is the shape itself in every other way (its bounds, handles, colours and exact regions
        stay), and the way of editing it had (gizmo or handles) comes back when it is unlocked.

        Write it under a shape's definition, with its handles() and expose() lines:

            part = lock(part)

        (FielDes's model tree has a lock button on every shape, and the key R toggles it.) '''
    if not isinstance(shape, Shape):
        raise TypeError('lock(shape): shape must be a Shape, not {}'.format(type(shape).__name__))
    ptr = lib.libfive_tree_copy(shape.ptr)
    try:
        out = type(shape)(ptr)
    except TypeError:
        out = Shape(ptr)
    for name, value in shape.__dict__.items():
        if name != 'ptr':
            out.__dict__[name] = value
    out._locked = True
    return out


def _var_numbers(values):
    ''' The numbers of these values: plain numbers, or the current numbers of var() shapes (the script being run
        keeps them), or None if one of them is neither '''
    by_id = {}
    try:
        import _fieldes_host as host
        for e in (getattr(host, '__vars', None) or []):
            try:
                by_id[id(e[0])] = float(e[1])
            except (TypeError, ValueError, IndexError):
                pass
    except ImportError:
        pass
    out = []
    for v in values:
        if _is_number(v):
            out.append(float(v))
        elif id(v) in by_id:
            out.append(by_id[id(v)])
        else:
            return None
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


def _exposed_distance(shape, out, values, get):
    ''' The distance for the offsets, shells and thickenings of an exposed part: the exact distance of the part with
        the numbers it has now, and what dragging them changes after that (the change of the part's own field, which
        follows the variables). With the part's own numbers that is its exact distance. Faces that were dragged
        before the script was run give the exact distance of the part as edited (meshed again, cached by content). '''
    base, ref = get(), shape
    nums = _var_numbers(values)
    orig = [float(x) for x in exposed_values(shape)]
    if nums is not None and len(nums) == len(orig) and             any(abs(a - b) > 1e-4 * max(1.0, abs(b)) for a, b in zip(nums, orig)):
        ref = expose(shape, nums)                       # the part as edited, with plain numbers
        b = getattr(shape, '_bounds', None)
        if b:
            pad = 0.15 * max(float(b[1][i]) - float(b[0][i]) for i in range(3))
            b = (tuple(float(v) - pad for v in b[0]), tuple(float(v) + pad for v in b[1]))
        from fieldes.stdlib.fields import exact_distance
        base = exact_distance(ref, b)
    return base + (out - ref)


_EPOCH = 0           # (a new script: what the last one was seen to have is forgotten)


def forget_literals():
    ''' A script was opened: the numbers the last run saw in the calls of the old one say nothing about this one '''
    global _EPOCH
    _EPOCH += 1


def _follow_literals(shape, values):
    ''' The number you change in the call of a shape that has been dragged wins.  `expose` holds the dragged numbers (the list of var()s below the
        call), and a number written in the call -- `cylinder(15, 10, ...)` -- is only what the shape was made with: editing it did nothing, the list
        below the call went on saying what the shape is.  Here the run notices that a number of the call is not what it was at the last run while
        the list is as it was (nothing was dragged): the number of the call is taken, and the list is rewritten to say it, by the editor, once the
        run is over (host.__resync).  Dragging, or editing the list, still works as it did: a number of the call that has not changed has no say. '''
    try:
        import _fieldes_host as host
    except ImportError:
        return
    entries = getattr(host, '__vars', None) or []
    where = {id(e[0]): e for e in entries}
    own = [float(x) for x in exposed_values(shape)]
    for v, c in zip(values, own):
        entry = where.get(id(v))
        if entry is None:
            continue
        value = float(entry[1])
        seen = getattr(v, '_literal_seen', None)
        tol = 1e-4 * max(1.0, abs(c))
        if seen is not None and seen[0] == _EPOCH and abs(seen[1] - c) > tol and abs(seen[2] - value) <= tol:
            getattr(host, '__retune')(v, c)
            line, end_line, col, end_col = entry[2]
            resync = getattr(host, '__resync', None)
            if resync is not None:
                resync.append((line - 1, col, end_line - 1, end_col - 1, _float32_text(c)))
            value = c
        try:
            v._literal_seen = (_EPOCH, c, value)
        except AttributeError:
            pass


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
    _follow_literals(shape, values)
    # (the wrapped numbers stay alive until the call has returned: plain numbers wrap to shapes
    # that would otherwise be freed with their pointers still in `arr`)
    wrapped = [Shape.wrap(v) for v in values]
    arr = (ctypes.c_void_p * max(1, n))(*[w.ptr for w in wrapped])
    p = lib.libfive_tree_expose(shape.ptr, arr, n)
    if not p:
        raise ValueError('expose(): the shape could not be rebuilt with those numbers')
    out = Shape(p)
    _carry(out, shape)
    # an offset, a shell or a thickening of it (fields._dist) uses the exact distance of the part
    get = getattr(shape, '_distance_of', None)
    if get is not None:
        out._distance_of = _lazy_once(lambda: _exposed_distance(shape, out, values, get))
    # (what the importer, roi() and the viewport read off a part: its box, its colouring, its exact regions)
    from fieldes.stdlib import cad_import
    for name in ('_bounds', '_render_hint') + tuple(cad_import._CARRIED):
        if hasattr(shape, name):
            try:
                setattr(out, name, getattr(shape, name))
            except AttributeError:
                pass
    cad_import._carry_exact(out, shape)
    from fieldes.kinds import keep_kind
    keep_kind(out, shape)
    return out
