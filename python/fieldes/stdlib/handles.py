''' Handles and the gizmo: edit a shape -- an imported part too -- by dragging in FielDes's viewport.

    Two ways of editing by dragging work together, and the first has priority where they meet:

      the gizmo    the part's own move arrows, rotation rings and scale knobs.  They drag the placement arguments of the shape's
                   call (see fieldes.stdlib.placement), which FielDes writes in the call when the shape is selected,

                       part = box(a, b, position=(var(0), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))

                   so what the gizmo does is ordinary script text, like any dragged var().  The shape is scaled about its
                   centre, rotated (x, then y, then z; degrees) and moved.  When the gizmo is shown is the state of the model
                   tree's gizmo button (key E), which is not in the script:

                       'click'   (the default) the gizmo is shown while the shape is selected, i.e. after you click it
                       'never'   it is never shown, so that it cannot get in the way
                       'always'  it is shown on the shape whether it is selected or not

      handles      the handles on the shape's own surfaces: hover a surface of the shape and drag it.  They are always
                   there, whatever the mode of the gizmo.  Any shape with `var()` numbers has them; for a shape written
                   with plain numbers -- a primitive, or a part imported from a STEP file, which is made of primitives --
                   `handles()` makes the numbers that place its surfaces variables:

                       part = handles(part, [var(6), var(40)])

                   One line, however many numbers (nobody edits them by hand: they are dragged).  Dragging a face changes
                   the number of that face (a plane's position, a radius) in the script text; a round feature keeps its centre,
                   a box keeps the face opposite the one dragged.  FielDes writes the call itself when you select the shape.

    A lock button beside it (key R) is a separate switch: a locked shape cannot be dragged at all, neither by the
    gizmo nor by its handles, and keeps its gizmo mode, which comes back when it is unlocked:

                   part = lock(part)

    Nothing is stored anywhere but in the script: delete the `handles(...)` call of a part and the placement arguments of its
    definition (FielDes's Reimport does) and it is the file's version again; the `lock(...)` call only keeps it from being
    dragged. '''
import ctypes
import math
import struct

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib.transforms import move as _move, rotate_x, rotate_y, rotate_z, scale_xyz, _lazy_once

__all__ = ['handles', 'lock']

MODES = ('click', 'never', 'always')

# What an imported part carries that a transformed copy must keep: its
# features and name (roi_resolution, the model tree).  (Its own resolution is
# not copied: roi_resolution sets it later, on the part, and FielDes reads it
# from the part a placed shape was placed from.)
_CARRIED = ('_step_metrics', '_part_name', '_step_ref', '_kind', 'xyz', '_point_pos', 'size', '_smooth', '_radius', '_linked', '_instance_of', '_no_handles', '_symmetry_def', '_drawn_key',
            '_on',              # (`_on`: the body a point or an edge was made on -- corner_at(body, ...) -- which a snap sorts the features by: the copy a gizmo makes of the point is on it as well)
            '_pull_axis')       # (`_pull_axis`: the direction of the axis the sides of a shape are pulled away from, as the shape is made: `_place` turns it with the shape)


def _turned_axis(axis, rotate, scale):
    ''' The direction `axis`, scaled as a placement scales a shape and turned as it turns it (x, then y, then z degrees: plain numbers, or the numbers the var()s have in the run), one long
        again: the axis a shape's sides are pulled away from follows the shape (`_pull_axis`).  None when a number cannot be read '''
    from fieldes.stdlib.curves import value_of
    from fieldes.stdlib.symmetries import _turn_matrix
    try:
        s = [float(value_of(v)) for v in scale]
        R = _turn_matrix(rotate)
        d = [float(axis[i]) * s[i] for i in range(3)]
        out = [sum(float(value_of(R[i][j])) * d[j] for j in range(3)) for i in range(3)]
    except Exception:
        return None
    n = math.sqrt(sum(c * c for c in out))
    return tuple(c / n for c in out) if n > 1e-12 else None


def _is_number(v):
    return isinstance(v, (int, float))


def _centre_of(shape):
    ''' The middle of a shape's bounding box: known for an imported part, else
        estimated by sampling it (briefly); (0, 0, 0) when neither works '''
    pos = getattr(shape, '_point_pos', None)        # (a point: where it is, exactly -- its gizmo and its dot are there)
    if pos:
        return tuple(float(c) for c in pos)
    # A shape whose surfaces were dragged (handles()) has its pivot where the shape was before: the middle of the box that moves with every drag
    # would take the whole shape with it when the script runs again, if the gizmo turns or scales it
    before = getattr(shape, '_pivot_shape', None)
    if before is not None:
        shape = before
        pos = getattr(shape, '_point_pos', None)
        if pos:
            return tuple(float(c) for c in pos)
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


_PRISM = ('_prism_pos', '_prism_rot', '_prism_flag', '_prism_scale', '_prism_radius', '_prism_default', '_prism_a', '_prism_b')


def _carry_prism(out, shape, guard=(), still=True):
    ''' What makes a shape a prism (see profile.extrude) goes on to what places or edits it, with the numbers that must still be what they are for it to be nothing else
        than that prism: (a var(), its value) for each.  `still` False: it is not a prism any more (a number that is plain is not what it was) '''
    if not still or getattr(shape, '_prism_pos', None) is None:
        return
    for name in _PRISM:
        setattr(out, name, getattr(shape, name))
    out._prism_guard = list(getattr(shape, '_prism_guard', [])) + [(v, float(x)) for v, x in guard]


def _unplaced(shape):
    ''' (the shape as it was before the placement arguments of its call placed it, the arguments), or (shape, None): handles() pulls the surfaces of the shape as it is made '''
    from fieldes.stdlib.placement import unplaced
    return unplaced(shape)


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


def _placed_ring_guard(rotate, move):
    ''' What must stay as it is for a shape that a gizmo has placed to still lie in the plane z = 0 -- a turn about x or y, a move along z are 0 -- for the viewport to draw a
        sweep of it from its outline: None when one of them is not 0 now (a plain number, or a var()), else the var()s that are, with the 0 they must stay (while one is
        dragged the sweep is drawn from its field) '''
    from fieldes.stdlib.curves import value_of
    guard = []
    for v in (rotate[0], rotate[1], move[2]):
        if isinstance(v, Shape):
            if abs(value_of(v)) > 1e-12:
                return None
            guard.append((v, 0.0))
        elif abs(float(v)) > 1e-12:
            return None
    return guard


def _placed_ring(points, about, move, rotate, scale):
    ''' The outline `points` of a 2D shape, as the gizmo places it: scaled in x and y about `about`, turned about z, moved in x and y (numbers, or expressions of the gizmo's
        var()s: the outline follows them) '''
    if points is None:
        return None
    cx, cy = Shape.wrap(about[0]), Shape.wrap(about[1])
    sx, sy = Shape.wrap(scale[0]), Shape.wrap(scale[1])
    mx, my = Shape.wrap(move[0]), Shape.wrap(move[1])
    turn = not (_is_number(rotate[2]) and rotate[2] == 0)
    if turn:
        theta = Shape.wrap(rotate[2]) * (math.pi / 180.0)
        cs, sn = theta.cos(), theta.sin()
    out = []
    for x, y in points:
        px, py = (Shape.wrap(x) - cx) * sx, (Shape.wrap(y) - cy) * sy
        if turn:
            px, py = px * cs - py * sn, px * sn + py * cs
        out.append((cx + px + mx, cy + py + my))
    return out


def _placed_frame(frame, about, move, rotate, scale):
    ''' The plane a 2D shape is drawn in -- (origin, u, v), three expressions each; the plane z = 0 when `frame` is None -- as the gizmo places it: scaled about `about`, turned (x,
        then y, then z) about it, moved.  The shape is drawn as its face in that plane, so a shape that is turned out of z = 0 has its plane turned with it, and is not cut across
        by the plane it was in '''
    from fieldes.stdlib.symmetries import _add, _matmul_expr, _mul, _turn_matrix
    if frame is None:
        frame = ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))
    R = _turn_matrix(rotate)

    def turned(w):
        s = [_mul(scale[i], w[i]) for i in range(3)]
        return [_add(_add(_mul(R[i][0], s[0]), _mul(R[i][1], s[1])), _mul(R[i][2], s[2])) for i in range(3)]
    o, u, v = frame
    rel = [_add(o[i], _mul(-1.0, about[i])) for i in range(3)]
    t = turned(rel)
    origin = [_add(_add(about[i], t[i]), move[i]) for i in range(3)]
    return tuple(tuple(Shape.wrap(c) for c in axis) for axis in (origin, turned(u), turned(v)))


def _place(shape, move=(0, 0, 0), rotate=(0, 0, 0), scale=(1, 1, 1), about=None, mode=None):
    ''' Scales, rotates and moves a shape, with the gizmo FielDes shows on it: what the placement arguments of a call do (see fieldes.stdlib.placement).

        scale: (x, y, z) factors about the shape's centre, `rotate`: (x, y, z)
        rotations in degrees about the same centre (x first, then y, then z),
        `move`: (x, y, z) translation.  The numbers are written as var(...) to drag
        them: FielDes's model tree does that for you.
        about: the centre (default: the middle of the shape's box)
        mode: when FielDes shows the gizmo: 'click' (the default) while the shape is selected, 'never', or
        'always'.  The shape's own surfaces (see handles()) can be dragged whatever the mode, and the gizmo has
        priority where it is shown.
        A profile, a polyline or a spline is placed through its points -- moved and turned about the middle of them -- as the same outline through the images of its points:
        whatever is made of points (it can make itself again through other points, `_remake`) is placed so, and what a linked copy of it, `instance(profile_1)`, is placed by.
        To make a shape undraggable, lock it: `shape = lock(shape)`. '''
    if mode is None:
        mode = 'click'
    if mode not in MODES:
        raise ValueError("the gizmo's mode is 'click', 'never' or 'always' (a shape is locked with lock(shape))")
    if len(move) != 3 or len(rotate) != 3 or len(scale) != 3:
        raise ValueError('position=, rotation= and scale= take three numbers each')
    if getattr(shape, '_outline_gizmo', False) and about is None and all(_is_number(v) for v in tuple(move) + tuple(rotate) + tuple(scale)) \
            and all(v == 0 for v in tuple(move) + tuple(rotate)) and all(v == 1 for v in scale):
        return _outlined(shape, mode)
    if getattr(shape, '_linked', False):
        # A linked copy is only moved and rotated: its shape is its original's.  A scale of numbers other than 1 would make it another shape -- refused; scale numbers
        # that are var()s (an older gizmo line, or one written before the copy was known to be linked) mean 1, and are not offered to be dragged
        if any(_is_number(s) and s != 1 for s in scale):
            raise ValueError('a linked copy (instance) can only be moved and rotated: its shape is its original\'s, so it is not scaled.  '
                             'Leave scale= out; a copy that has a shape of its own is a copy of the statements (Shift+D in FielDes)')
        scale = (1, 1, 1)
    if all(_is_number(s) and s == 1 for s in scale) and callable(getattr(shape, '_remake', None)) and getattr(shape, 'points', None):
        # What is made of points -- a profile, a polyline, a spline: it can make itself again through other points -- is placed through them, whatever the numbers are
        return _placed_outline(shape, move, rotate, mode, about)
    if not isinstance(shape, Shape) and callable(getattr(shape, '_display', None)):
        # The result of an analysis or an optimisation is not a shape: it has no surface of its own to place (what it shows is made from
        # its part).  Placed by nothing it is the result itself; placed by something, there is nothing it could mean
        nums = _var_numbers(list(move) + list(rotate) + list(scale))
        if nums is not None and all(n == 0 for n in nums[:6]) and all(n == 1 for n in nums[6:]):
            return shape
        raise TypeError('a %s is the result of an analysis, not a shape: it has no surface of its own to move, rotate or scale. '
                        'Place the part it was made from (its position=, rotation= and scale=) -- the result follows it.' % type(shape).__name__)
    # The pivot: where `about` says, else the middle of the shape as it was made (see _centre_of): plain numbers that no handle changes, so a side that is pulled moves that side
    # only, whatever the scale of the gizmo, and nothing jumps when the script is run again.  (It was an expression of the handle numbers once, so that the gizmo stayed at the middle of
    # the shape whatever side was pulled: with a scale other than 1 the middle moved with the pull, and the opposite side went along with it)
    if about is not None:
        c = tuple(about)
    else:
        c = _centre_of(shape)
    about_what = c
    to_rad = math.pi / 180.0
    out = shape
    if not all(_is_number(s) and s == 1 for s in scale):
        out = scale_xyz(out, tuple(scale), about_what)
    if not all(_is_number(r) and r == 0 for r in rotate):
        for rot, angle in ((rotate_x, rotate[0]), (rotate_y, rotate[1]), (rotate_z, rotate[2])):
            if not (_is_number(angle) and angle == 0):
                out = rot(out, angle * to_rad, about_what)
    # (always a new shape, to hang the handles on: the part itself is shared)
    out = _move(out, tuple(move))
    _carry(out, shape)
    # A 2D shape is drawn as its face in a plane (z = 0, or the one it says it lies in): placed out of that plane -- turned about x or y, moved along z -- its plane is placed
    # with it, else the face drawn would be the slice of the turned shape at z = 0 (squashed, and gone at a quarter turn)
    from fieldes.kinds import kind_of
    if kind_of(shape) == 'profile' and not getattr(shape, 'plane', None):
        out_of_plane = _var_numbers([rotate[0], rotate[1], move[2]])
        if out_of_plane is not None and any(abs(x) > 1e-9 for x in out_of_plane):
            out._flat_frame = _placed_frame(getattr(shape, '_flat_frame', None), about_what, tuple(move), tuple(rotate), tuple(scale))
    # (what a linked copy of the placed shape follows: the shape as it was before the gizmo moved and turned it -- scaled, as the gizmo scaled it: a copy has no scale of its own --
    # so that the copy and the original are each moved and turned by their own numbers)
    out._placed_parts = (shape, about_what, tuple(scale))
    if hasattr(shape, 'xyz'):
        # (a point that is moved is where it is moved to: its coordinates follow the move)
        out.xyz = tuple((c + m) if not (_is_number(c) and _is_number(m)) else float(c) + float(m)
                        for c, m in zip(shape.xyz, move))
        try:
            from fieldes.stdlib.points import _position_of
            out._point_pos = _position_of(out.xyz)       # (where its dot is drawn when the gizmo is not there)
        except Exception:
            pass
    # (the outline of a primitive goes on to what places it: the outline of the placed shape, made of the numbers of the gizmo -- for as long as the shape stays in the plane z = 0)
    make = getattr(shape, '_ring_points', None)
    if make is not None and not getattr(shape, '_linked', False):
        guard = _placed_ring_guard(rotate, move)
        if guard is not None:
            out._ring_points = lambda: _placed_ring(make(), about_what, move, rotate, scale)
            out._ring_guard = list(getattr(shape, '_ring_guard', [])) + guard
    # (a sweep drawn natively from its points is placed natively too: the viewport places it by the numbers of the gizmo, about the point `c`)
    if getattr(shape, '_sweep_ppos', None) is not None and getattr(shape, '_sweep_place', None) is None:
        for name in ('_sweep_ppos', '_sweep_prot', '_sweep_pflag', '_sweep_pscale', '_sweep_pradius', '_sweep_pdefault', '_sweep_qpos', '_sweep_qrot', '_sweep_qflag', '_sweep_qscale',
                     '_sweep_qdefault', '_sweep_qclosed', '_sweep_twist'):
            setattr(out, name, getattr(shape, name))
        out._sweep_free = getattr(shape, '_sweep_free', False)          # (the sweep of a primitive: its outline is in the plane z = 0)
        out._sweep_guard = list(getattr(shape, '_sweep_guard', []))
        out._sweep_place = (tuple(float(x) for x in c), tuple(Shape.wrap(x) for x in move), tuple(Shape.wrap(x) for x in rotate), tuple(Shape.wrap(x) for x in scale))
    # (a prism stays one while the gizmo places it nowhere: its numbers are what they are made with)
    if getattr(shape, '_prism_pos', None) is not None:
        guard, still = [], True
        for values, rest in ((move, 0.0), (rotate, 0.0), (scale, 1.0)):
            for v in values:
                if _is_number(v):
                    still = still and abs(float(v) - rest) < 1e-12
                else:
                    guard.append((v, rest))
        _carry_prism(out, shape, guard, still)
    # (the box of a part follows the numbers it is placed by: plain numbers, or the numbers the var()s have in the script being
    # run.  A placed part that lost its box would have to be searched for its extent by whatever is made from it -- a surface
    # selected on it, a lattice on that -- and that search fails on a mesh)
    b = getattr(shape, '_bounds', None)
    loose = bool(getattr(shape, '_bounds_loose', False))
    if not b and not hasattr(shape, 'xyz') and getattr(shape, '_point_pos', None) is None:
        # A shape that does not know its box has it searched for -- once: the search is kept by the shape's expression, which does not change
        # when the numbers of this gizmo do -- so that the box of what is moved follows the numbers at once, however the shape is made.  (The
        # render region is made to hold the models by these boxes: a model that is moved out of it must be known to be)
        try:
            import time
            from fieldes.app_support import _estimate_bounds
            b = _estimate_bounds([shape], time.time() + 2.0)
            loose = True
        except Exception:
            b = None
    if b:
        placed = _placed_box(b, c, move, rotate, scale)
        if placed is not None:
            out._bounds = placed
            if loose:
                out._bounds_loose = True
    # (the exact distance of an imported part follows the transforms above: see transforms._exact_follows)
    # what FielDes draws the gizmo from
    out._gizmo = (mode, tuple(float(v) for v in c), tuple(move), tuple(rotate), tuple(scale))
    _note_gizmo_vars(shape, move, rotate, scale)
    # (the axis the sides of the shape are pulled away from is turned and scaled with it)
    if getattr(shape, '_pull_axis', None) is not None:
        out._pull_axis = _turned_axis(shape._pull_axis, rotate, scale)
    return out


def _placed_point(xyz, about, move, rotate):
    ''' A place (three numbers or expressions) as the gizmo places it: turned about `about` (x, then y, then z; degrees) and moved -- numbers, or expressions of the var()s of the
        gizmo, so that what is placed follows them (see _placed_frame, which places the plane of a face the same way) '''
    from fieldes.stdlib.symmetries import _add, _mul, _turn_matrix
    R = _turn_matrix(rotate)
    rel = [_add(xyz[i], _mul(-1.0, about[i])) for i in range(3)]
    return tuple(_add(_add(about[i], _add(_add(_mul(R[i][0], rel[0]), _mul(R[i][1], rel[1])), _mul(R[i][2], rel[2]))), move[i]) for i in range(3))


def _placed_outline(shape, move, rotate, mode, about=None):
    ''' A profile, a polyline, a spline -- whatever is made of points and can make itself again through other points (`_remake`) -- placed by the gizmo: the same outline through the
        images of its points, which are where the points are now (their var()s are in the expressions, so a point dragged moves the images with it) turned about the middle of them
        and moved by the numbers of this gizmo.  The points of a shape are what carries its numbers, so its place is its points', and a placed outline is the shape again, made from the
        images: its plane, its face, what an extrusion or a sweep of it is made from are the ones of the images '''
    from fieldes.stdlib.curves import value_of
    from fieldes.stdlib.points import Point, image_dot
    from fieldes.stdlib.symmetries import _turn_matrix
    move, rotate = tuple(move), tuple(rotate)
    places = []
    for p in shape.points:
        xyz = getattr(p, 'xyz', None)
        if xyz is None:
            xyz = tuple(p) + (0.0,) * (3 - len(p))
        places.append((p, xyz))
    if about is not None:
        c = tuple(float(v) for v in about)
    else:
        nums = [tuple(value_of(v) for v in xyz) for _p, xyz in places]
        c = tuple(0.5 * (min(q[i] for q in nums) + max(q[i] for q in nums)) for i in range(3))
    # The map that places a place -- about c, turned, then moved -- as numbers, for the dots: a dot is the image of a point, and is dragged back through it (see points.image_dot)
    R = _turn_matrix(rotate)
    rows = [[value_of(R[i][j]) for j in range(3)] for i in range(3)]
    shift = tuple(value_of(v) for v in move)
    frozen = [v for v in move + rotate if isinstance(v, Shape)]
    dots, shown = [], []
    for p, xyz in places:
        x, y, z = _placed_point(xyz, c, move, rotate)
        if hasattr(p, 'xyz'):
            dot = image_dot(p, (x, y, z), rows, c, shift, frozen)     # (it is drawn with the outline: the points are part of what is placed)
            shown.append(dot)
        else:
            dot = Point(x, y, z)                                      # (a point that is only numbers has no dot to draw)
        dots.append(dot)
    out = shape._remake(dots)
    _carry(out, shape)
    out._no_gizmo = False                          # (it has numbers of its own now: they are this gizmo's)
    out._outline_gizmo = False
    out._editable = bool(getattr(shape, '_linked', False))      # (a press on its line puts a point on it: on the original it is a copy of, and on every copy of that)
    out._image_dots = shown
    out._edit_map = (tuple(v for row in rows for v in row), c, shift)       # (where a place of the original is taken to this one's: what a press on its outline is taken back by)
    out._edit_reversed = False
    out._placed_parts = (shape, c, (1, 1, 1))
    out._gizmo = (mode, c, move, rotate, (1, 1, 1))
    _note_gizmo_vars(shape, move, rotate, (1, 1, 1))
    return out


def _outlined(shape, mode):
    ''' A profile, a polyline or a spline, with the mode of its gizmo (the state of the tree's button).  It is the outline itself in every other way -- a copy with
        its points, its face and its handles; a placed shape would be made again from a field and lose them --, because the gizmo of an outline has no numbers of its own: it
        moves, turns and scales the points the outline is made of (View::groupGizmo), and this only says when that gizmo is shown '''
    ptr = lib.libfive_tree_copy(shape.ptr)
    try:
        out = type(shape)(ptr)
    except TypeError:
        out = Shape(ptr)
    for name, value in shape.__dict__.items():
        if name != 'ptr':
            out.__dict__[name] = value
    out._outline_mode = mode
    return out


def _note_gizmo_vars(shape, move, rotate, scale):
    ''' The numbers of the placement arguments are written to the list of the run (host.__gizmo_vars, made empty when a script starts): FielDes keeps every one of them out of a
        surface pull.  A gizmo moves, turns and scales; the numbers a surface is pulled by are the handles -- and the placement of a 2D shape that an extrusion is made of
        has numbers in the extrusion's field too, which a pull of the extrusion's side would otherwise take instead of the handle (the whole part scaled, moved and turned:
        every other side went with it).  A point is the exception: the numbers of its gizmo are where it is, and the shapes made of points are pulled by them '''
    if hasattr(shape, 'xyz'):
        return
    try:
        import _fieldes_host as host
    except ImportError:
        return
    noted = getattr(host, '__gizmo_vars', None)
    if noted is None:
        return
    for v in tuple(move) + tuple(rotate) + tuple(scale):
        if isinstance(v, Shape):
            noted.append(v)


def lock(shape):
    ''' `shape`, locked: FielDes does not let it be dragged -- neither by the gizmo nor by its surfaces -- until the
        line is deleted.  It is the shape itself in every other way (its bounds, handles, colours and exact regions
        stay), and the way of editing it had (gizmo or handles) comes back when it is unlocked.

        Write it under a shape's definition, after its handles() call:

            part = lock(part)

        (FielDes's model tree has a lock button on every model -- shapes, results, conditions, materials, points -- and the key R toggles it.) '''
    if not isinstance(shape, Shape):
        # Any other block -- the result of an analysis, a condition, a material, a point, a lattice cell -- is locked the same way: it is
        # itself in every way, and the model tree does not let it be dragged (the lock is the line: `result = lock(result)`)
        import copy
        try:
            out = copy.copy(shape)
            out._locked = True
            return out
        except Exception:
            return shape
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


def _handle_count(shape):
    shape = _unplaced(shape)[0]                 # (the numbers of a shape's surfaces are the shape's own, not the numbers it is placed by)
    params = getattr(shape, '_handle_params', None)
    if params is not None:
        return len(params)                  # (a shape made of numbers of its own: those are its handles)
    fn = getattr(lib, 'libfive_tree_expose_count', None)
    if fn is None:
        raise RuntimeError('this FielDes library is too old for handles()')
    return fn(shape.ptr)


def handle_values(shape):
    ''' The numbers handles() makes variables, in its order, as the text to write them as '''
    shape = _unplaced(shape)[0]
    params = getattr(shape, '_handle_params', None)
    if params is not None:
        return [_float32_text(struct.unpack('f', struct.pack('f', float(p)))[0]) for p in params]
    n = _handle_count(shape)
    buf = (ctypes.c_float * max(1, n))()
    lib.libfive_tree_expose_values(shape.ptr, buf)
    return [_float32_text(buf[i]) for i in range(n)]


def _handle_distance(shape, out, values, get):
    ''' The distance for the offsets, shells and thickenings of a part with handles: the exact distance of the part with
        the numbers it has now, and what dragging them changes after that (the change of the part's own field, which
        follows the variables). With the part's own numbers that is its exact distance. Faces that were dragged
        before the script was run give the exact distance of the part as edited (meshed again, cached by content). '''
    base, ref = get(), shape
    nums = _var_numbers(values)
    orig = [float(x) for x in handle_values(shape)]
    if nums is not None and len(nums) == len(orig) and             any(abs(a - b) > 1e-4 * max(1.0, abs(b)) for a, b in zip(nums, orig)):
        ref = handles(shape, nums)                       # the part as edited, with plain numbers
        b = getattr(shape, '_bounds', None)
        if b:
            pad = 0.15 * max(float(b[1][i]) - float(b[0][i]) for i in range(3))
            b = (tuple(float(v) - pad for v in b[0]), tuple(float(v) + pad for v in b[1]))
        from fieldes.stdlib.fields import exact_distance
        base = exact_distance(ref, bounds=b)
    return base + (out - ref)


_EPOCH = 0           # (a new script: what the last one was seen to have is forgotten)


def forget_literals():
    ''' A script was opened: the numbers the last run saw in the calls of the old one say nothing about this one '''
    global _EPOCH
    _EPOCH += 1


def _follow_literals(shape, values):
    ''' The number you change in the call of a shape that has been dragged wins.  `handles` holds the dragged numbers (the list of var()s below the
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
    own = [float(x) for x in handle_values(shape)]
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


def handles(shape, values=None, **removed):
    ''' A shape whose surfaces have handles: the numbers that place its
        surfaces -- a plane's position, a radius, the faces of a box -- are
        replaced by `values`, in the order of their appearance in the shape
        (FielDes writes them as var(...) with the shape's own numbers, which
        makes them draggable: hover a surface, drag it).  Orientations and the
        placement of the whole part are not exposed, so a dragged face
        moves and neither turns nor takes the rest of the part along.

        With the shape's own numbers the result is the very same shape.  If the
        shape has a different number of them (the part was changed in the STEP
        file) it raises ValueError: reimport the part.

        (This is what expose() was called.  The gizmo -- the arrows, rings and knobs
        that move, turn and scale a shape -- drags its position=, rotation= and scale=.)

        A shape that is placed by the arguments of its call (see fieldes.stdlib.placement) has its surfaces pulled where it is made, and is placed again by the same
        arguments: the numbers are those of the shape as it is made. '''
    if removed or values is None or isinstance(values, (str, bytes)):
        raise TypeError('handles(shape, [numbers]) makes the surfaces of a shape draggable: its second argument is the list of numbers that '
                        'place them (FielDes writes it when the shape is selected).  It does not take %s -- the gizmo, the arrows that move, '
                        'turn and scale a shape, drags the position=, rotation= and scale= of its call'
                        % ('move=, rotate=, scale=, about= or mode=' if removed else 'no numbers'))
    if getattr(shape, '_linked', False):
        raise TypeError('handles(): a linked copy (instance) has no surfaces of its own to drag -- its shape is its original\'s: drag the original\'s surfaces, '
                        'and the copy follows')
    inner, placed_by = _unplaced(shape)
    if placed_by is not None:
        from fieldes.stdlib.placement import _replace
        return _replace(handles(inner, values), placed_by)
    values = list(values)
    n = _handle_count(shape)
    if len(values) != n:
        raise ValueError('handles(): the shape has %d numbers that place its surfaces, %d were given -- the part has changed since '
                         '(Reimport it in the model tree)' % (n, len(values)))
    _follow_literals(shape, values)
    # (the wrapped numbers stay alive until the call has returned: plain numbers wrap to shapes
    # that would otherwise be freed with their pointers still in `arr`)
    wrapped = [Shape.wrap(v) for v in values]
    rebuild = getattr(shape, '_handle_rebuild', None)
    if rebuild is not None:
        # (a shape made of numbers of its own -- a rectangle's sides -- is made again of these)
        out = rebuild(wrapped)
    else:
        arr = (ctypes.c_void_p * max(1, n))(*[w.ptr for w in wrapped])
        p = lib.libfive_tree_expose(shape.ptr, arr, n)
        if not p:
            raise ValueError('handles(): the shape could not be rebuilt with those numbers')
        out = Shape(p)
    _carry(out, shape)
    # (a circle: the outline is made of the numbers it has now -- its centre and its radius, in that order, as var()s too)
    spec = getattr(shape, '_ring_params', None)
    if spec is not None and rebuild is None and len(wrapped) == spec[0]:
        try:
            from fieldes.stdlib.curves import value_of
            if all(abs(float(a) - value_of(b)) < 1e-9 for a, b in zip(handle_values(shape), spec[2])):
                out._ring_points = spec[1](wrapped)
        except Exception:
            pass
    # (what the shape was before its numbers were edited: the pivot of a gizmo that turns or scales it is the middle of this one's box)
    out._pivot_shape = getattr(shape, '_pivot_shape', None) or shape
    # an offset, a shell or a thickening of it (fields._dist) uses the exact distance of the part
    get = getattr(shape, '_distance_of', None)
    if get is not None:
        out._distance_of = _lazy_once(lambda: _handle_distance(shape, out, values, get))
    # (what the importer, roi() and the viewport read off a part: its box, its colouring, its exact regions)
    from fieldes.stdlib import cad_import
    for name in ('_bounds', '_render_hint', '_resolution_override', '_custom_resolution') + tuple(cad_import._CARRIED):
        if hasattr(shape, name):
            try:
                setattr(out, name, getattr(shape, name))
            except AttributeError:
                pass
    cad_import._carry_exact(out, shape)
    from fieldes.kinds import keep_kind
    keep_kind(out, shape)
    _widen_box(out, shape, values)
    # (a prism stays one while its numbers are what it was made with: with a face pulled it is not that prism)
    # (a shape that is made again of its numbers -- the extrusion of a profile, made again with the a and b the handles give it -- is a prism of its own already, drawn from those numbers)
    if getattr(shape, '_prism_pos', None) is not None and rebuild is None:
        try:
            orig = [float(x) for x in handle_values(shape)]
            guard, still = [], len(orig) == len(values)
            for v, x in zip(values, orig):
                if _is_number(v):
                    still = still and abs(float(v) - x) < 1e-4 * max(1.0, abs(x))
                else:
                    guard.append((v, x))
            _carry_prism(out, shape, guard, still)
        except Exception:
            pass
    return out


def _widen_box(out, shape, values):
    ''' The box a shape had is the box of its own numbers: when a surface has been dragged (a number is not what the shape was made with)
        the part reaches further than the box says -- a face pulled out of a box, a radius made larger -- and the render region would cut it
        off.  A surface moves by no more than the numbers have changed (each is a position or a length), so the box grows by the sum of
        the changes on every side: not the exact extent, one that holds the part (`_bounds_loose`: it is not offered as an exact box) '''
    b = getattr(out, '_bounds', None)
    if not b:
        return
    try:
        nums = _var_numbers(values)
        orig = [float(x) for x in handle_values(shape)]
        if nums is None or len(orig) != len(nums):
            _forget_box(out)
            return
        grow = sum(abs(a - b0) for a, b0 in zip(nums, orig))
        if grow > 1e-9:
            # (a 2D shape that lies in the plane z = 0 -- its box is flat -- stays in it: the numbers of its handles are positions and lengths in that plane, none of them takes it out of it.  A flat box that
            # was made thick in z lost what the extrusion of it needs -- extrude() carries the box of the face along z only from a flat one -- and the extrusion, and its placement, had its box SEARCHED for
            # at every run: a second or two for each statement, and the whole run, with the viewport waiting for it, once the sides had been pulled)
            flat = abs(float(b[0][2])) < 1e-12 and abs(float(b[1][2])) < 1e-12
            grow_z = 0.0 if flat else grow
            out._bounds = ((float(b[0][0]) - grow, float(b[0][1]) - grow, float(b[0][2]) - grow_z),
                           (float(b[1][0]) + grow, float(b[1][1]) + grow, float(b[1][2]) + grow_z))
            out._bounds_loose = True
    except Exception:
        _forget_box(out)                # (not known: it is searched for, with the numbers it has now)


def _forget_box(out):
    try:
        del out._bounds
    except AttributeError:
        pass
