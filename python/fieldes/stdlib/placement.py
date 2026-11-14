''' Placement: where an object is, how it is turned, how big it is and how see-through.

    Every object that is made by a call -- a shape, the result of an operation, a copy, a lattice -- takes five arguments after its own, named and in this order (the fifth is written when it is set):

        position   (x, y, z) in mm: where the object is -- its ONE position, and it is global, in the world.  A shape made by its size alone (box((20, 10, 5)), sphere(5), cylinder(4, 20)) is made at the origin and moved
                   there (not given: it stands at (0, 0, 0); a cone and a pyramid stand on their base); the result of an operation, a copy, a lattice or an import stands where its parts put
                   it, and its position is where the middle of its box is (not given: where it is made)
        rotation   (x, y, z) in degrees: how it is turned about its origin (not given: its position), x first, then y, then z
        scale      (x, y, z) factors, or one number: how big it is, about its origin (not given: its position)
        alpha      0 to 1: how solid it is drawn (1 is solid, 0 is a ghost, 5 % solid at the least): the object is the same in every other way
        origin     (x, y, z) in mm, in the place the object is made (before its position moves it): the point it is turned and scaled about and where its gizmo is.  Not given: its position.  FielDes writes it when the
                   key O makes a point of the scene the origin of the selected model, and writes `position` again to make up for it, so that the model stays where it is

    They are ordinary arguments of the call.  FielDes's model tree shows them inside every object, a click on one selects its number in the code, and the gizmo in the
    viewport drags the first three: when the object is selected, FielDes writes them with `var()` numbers (`position=(var(40), var(0), var(0))`) and a drag changes the
    numbers in the text.  None of them is needed: an object without them stands where its definition puts it.

        part = box((20, 10, 5), position=(40, 0, 0), rotation=(0, 0, 30), alpha=0.5)

    What is not made by a call -- a part taken out of an imported model, a name given to another, a sum -- is placed by `place`, which takes the same arguments:

        part = place(part, position=(40, 0, 0))

    A shape that is made of points (a profile, a polyline, a spline) is placed through its points, which are what the gizmo drags.

    The arguments are given to every function that makes a model by `install`: a function that has an argument of that name of its own (`hole(position=...)`, `lattice(scale=...)`)
    keeps it and has no placement arguments -- a model it makes is placed with `place`.'''
import functools
import inspect

from fieldes.shape import Shape

__all__ = ['place']

PLACEMENT = ('position', 'rotation', 'scale', 'alpha', 'origin')

# The functions that make a model, by the module they are written in: they are given the placement arguments (see `install`).  A function that makes something that is not a model
# (a number, a result, a condition, a selection) is not here
PLACEABLE = {
    'shapes': ('array_polar', 'array_polar_z', 'array_x', 'array_xy', 'array_xyz'),     # (the primitives -- box, sphere, cylinder ... -- are SIZED, in shapes: see `install`)
    'csg': ('blend', 'blend_difference', 'blend_expt', 'blend_expt_unit', 'blend_rough', 'clearance', 'difference', 'intersection', 'inverse', 'loft', 'loft_between',
            'morph', 'offset', 'shell', 'union'),
    'transforms': ('attract', 'attract_x', 'attract_xy', 'attract_xz', 'attract_y', 'attract_yz', 'attract_z', 'move', 'reflect_x', 'reflect_xy', 'reflect_xz', 'reflect_y',
                   'reflect_yz', 'reflect_z', 'repel', 'repel_x', 'repel_xy', 'repel_xz', 'repel_y', 'repel_yz', 'repel_z', 'revolve_y', 'rotate', 'rotate_x', 'rotate_y',
                   'rotate_z', 'scale_x', 'scale_xyz', 'scale_y', 'scale_z', 'shear_x_y', 'symmetric_x', 'symmetric_y', 'symmetric_z', 'taper_x_y', 'taper_xy_z',
                   'twirl_axis_x', 'twirl_axis_y', 'twirl_axis_z', 'twirl_x', 'twirl_y', 'twirl_z'),
    'cad': ('circular_pattern', 'cut_plane', 'linear_pattern', 'mirror', 'reflect', 'revolve', 'rotate'),
    'fields': ('bend_z', 'body_from_field', 'chamfer', 'chamfer_union', 'fillet', 'intersection_all', 'mirror_x', 'mirror_y', 'mirror_z', 'offset_by', 'offset_exact', 'repeat',
               'repeat_polar', 'round_edges', 'shell_centered', 'shell_exact', 'shell_inside', 'shell_outside', 'smooth_difference', 'smooth_intersection', 'smooth_union',
               'thicken', 'twist_z', 'union_all'),
    'lattices': ('delaunay_lattice', 'fill', 'graph_lattice', 'lattice', 'planar_lattice', 'point_cloud', 'random_points', 'strut_lattice', 'surface_lattice', 'tpms', 'voronoi_lattice'),
    'conformal': ('lattice_surface_conform',),
    'profile': ('extrude', 'path_extrude', 'profile'),
    'curves': ('polyline', 'spline'),
    'points': ('surface',),
    'instances': ('instance',),
}

# The functions whose own place is an ARGUMENT -- a base, a centre, the point of a plane, a height -- and that are made in their own frame like the primitives (see shapes.SIZED): the argument
# goes (the function is made at its default, the origin of its frame) and the ONE global `position` says where that frame is.  (The user, 2026-11-03: "EVERYTHING HAS ONE GLOBLA POSITION.
# GLOBAL. AND ONE. NO EXCEPTION.")  module -> {function: the arguments that are its place}.  A pivot or a plane of an OPERATION (rotate about a centre, cut_plane through a point, a mirror) is
# not the place of what it makes: it stays an argument of the operation
LOCAL = {
    'cad': {'capsule': ('base',), 'slot': ('center',), 'threaded_rod': ('base',), 'threaded_rod_normed': ('base',), 'tube': ('base',)},
    'points': {'cylinder_surface': ('center',), 'plane': ('point',), 'point': ('x', 'y', 'z'), 'sphere_surface': ('center',), 'wave_surface': ('height',)},
    'surfaces': {'coil_spring': ('center',), 'ellipsoid': ('center',), 'elliptic_cylinder': ('center',), 'helical_revolve': ('center',), 'helical_sweep': ('center',),
                 'hyperboloid': ('center',), 'paraboloid': ('center',), 'wave_block': ('center',)},
    'symmetries': {'symmetry_planes': ('centre',)},
    'text': {'text': ('pos',)},
}


def _vec(value, name, default):
    ''' The three numbers of a placement argument (numbers, var()s or expressions of them): `scale` may be one number, a `position` of a flat shape two (x, y) '''
    if value is None:
        return tuple(default)
    if isinstance(value, (Shape, int, float)):
        if name == 'scale':
            return (value, value, value)
        raise TypeError('%s= is three numbers (x, y, z), not %r' % (name, value))
    values = tuple(value)
    if len(values) == 2 and name in ('position', 'origin'):
        values = values + (0.0,)
    if len(values) != 3:
        raise ValueError('%s= is three numbers (x, y, z), not %d of them' % (name, len(values)))
    return values


def _alpha_value(alpha):
    ''' How solid the object is drawn, 0 to 1: a number, or a var() (the number it has in this run) '''
    if alpha is None:
        return None
    if isinstance(alpha, Shape):
        from fieldes.stdlib.curves import value_of
        alpha = value_of(alpha)
    try:
        a = float(alpha)
    except (TypeError, ValueError):
        raise TypeError('alpha= is a number from 0 (a ghost) to 1 (solid), not %r' % (alpha,))
    return min(1.0, max(0.0, a))


def _copy_of(shape):
    ''' `shape` as another object with all it has (what lock() does too): the one that the alpha is set on, so that the shape it was made from stays as it is '''
    import copy
    if not isinstance(shape, Shape):
        return copy.copy(shape)
    from fieldes.ffi import lib
    ptr = lib.libfive_tree_copy(shape.ptr)
    try:
        out = type(shape)(ptr)
    except TypeError:
        out = Shape(ptr)
    for name, value in shape.__dict__.items():
        if name != 'ptr':
            out.__dict__[name] = value
    return out


def _minus(a, b):
    ''' a - b for numbers and for the expressions of var()s '''
    return a - b


_PIVOTS = {}


def _stable_pivot(shape, position, pivot):
    ''' The pivot a shape that is made of points is placed about.  The middle of its box moves when its points are edited, and the position -- a number of the script -- does not: "the position less the pivot"
        then moved the shape away from the profile it is made of at every edit (the user, 2026-11-04: "editing the profile slowly moves the extrusion or the profile so they are no longer overlapping on the
        bottom face").  The pivot the shape had when its position was its middle is kept (by the numbers that place it, in the order the script makes them): while the position is the middle now it is the
        middle; once the points have moved the middle and the position has not, the pivot is the one before, and the shape stays with its points; a drag of the position moves the shape by what it moves '''
    if getattr(shape, '_prism_pos', None) is None and getattr(shape, '_sweep_ppos', None) is None:
        return pivot
    try:
        from fieldes.stdlib.curves import value_of
        from fieldes.stdlib.handles import _var_entries
        entries, at = _var_entries()
        values = position if isinstance(position, (tuple, list)) else (position,)
        key = tuple(at.get(id(v)) for v in values)
        if len(key) != 3 or any(k is None for k in key):
            return pivot
        where = tuple(float(value_of(v)) for v in values)
    except Exception:
        return pivot
    if max(abs(where[i] - pivot[i]) for i in range(3)) < 1e-6:
        _PIVOTS[key] = tuple(pivot)             # (the position is the middle: a new shape, or one that has not been edited)
        return pivot
    seen = _PIVOTS.get(key)
    return seen if seen is not None else pivot


def _apply(shape, position=None, rotation=None, scale=None, alpha=None, origin=None, frame=None):
    ''' `shape` placed by the arguments, and drawn as solid as `alpha` says.  `position` is GLOBAL, one for every model: where the model is in the world.
        frame 'origin': a primitive made in its own frame at the origin -- its position is where that origin is, rotation and scale are about it.
        frame None: everything else, made where its parts put it -- its position is where its pivot is (the middle of its box, or `origin`), so that it is moved from where it was made by
        the position less that pivot; rotation and scale are about the pivot (as the gizmo does) '''
    # An image of a symmetry has no numbers of its own (see symmetries.symmetry_image): where its original and the symmetry put it is where it is.  The program once wrote a place(...) with the numbers of a new
    # model -- (0, 0, 0), (0, 0, 0), (1, 1, 1) -- for every image point of a body made while a symmetry was in use, and global as the position is that put each image at the origin (his file, H118, the user, 2026-11-04:
    # "Morph shape symmetry does not work properly ... This caused an error"): such a line, which changes nothing it could mean, leaves the image where it is
    if getattr(shape, '_symmetry_of', None) is not None and hasattr(shape, 'xyz') and origin is None:
        try:
            from fieldes.stdlib.curves import value_of

            def _same(v, rest):
                if v is None:
                    return True
                if not isinstance(v, (tuple, list)):
                    v = (v,)
                return all(abs(float(value_of(c)) - rest) < 1e-12 for c in v)
            if _same(position, 0.0) and _same(rotation, 0.0) and _same(scale, 1.0):
                position = rotation = scale = None
        except Exception:
            pass
    if position is None and rotation is None and scale is None and alpha is None and origin is None:
        return shape
    if position is not None or rotation is not None or scale is not None or origin is not None:
        from fieldes.stdlib.handles import _place, _centre_of
        about = None if origin is None else tuple(_vec(origin, 'origin', (0, 0, 0)))
        gizmo = None                    # (what the gizmo is drawn and dragged by, where that is not what the shape is placed by)
        if frame == 'origin':
            move = _vec(position, 'position', (0, 0, 0))
            if about is None:
                about = (0, 0, 0)
        elif position is None:
            move = (0, 0, 0)
        else:
            pivot = tuple(about) if about is not None else tuple(_centre_of(shape))
            if about is None:
                pivot = _stable_pivot(shape, position, pivot)
            where = _vec(position, 'position', (0, 0, 0))
            move = tuple(_minus(where[i], pivot[i]) for i in range(3))
            about = pivot
            # The position is GLOBAL: the numbers the gizmo drags are the position's own (a var() is one, "the position less the pivot" is an expression the gizmo cannot find a number in), and
            # the gizmo stands where they say -- the place of a position that is a plain number is the gizmo's centre
            gizmo = (tuple(float(w) if isinstance(w, (int, float)) else 0.0 for w in where), where)
        out = _place(shape, move=move, rotate=_vec(rotation, 'rotation', (0, 0, 0)), scale=_vec(scale, 'scale', (1, 1, 1)), about=about, gizmo=gizmo)
    else:
        out = _copy_of(shape)
    a = _alpha_value(alpha)
    if a is not None:
        try:
            out._alpha = a
        except AttributeError:
            raise TypeError('alpha=: a %s has no surface to draw see-through' % type(shape).__name__)
    out._placement_args = (position, rotation, scale, alpha, origin, frame)
    # A copy of a model (instance) that is placed again -- FielDes writes `copy = instance(...)` and `copy = place(copy, ...)` for the copy it makes -- carries the parts of the model through this place too
    if isinstance(shape, Shape) and shape.__dict__.get('_linked') and getattr(shape, '_placed_parts', None) is not None:
        from fieldes.stdlib.instances import _carry_parts
        _carry_parts(out, None)
    return out


def _replace(shape, args):
    ''' `shape` placed again as an object was that was placed by `args` (what `_apply` was given): what handles() does with the shape it takes out of its placement and edits '''
    return _apply(shape, *args)


def unplaced(shape):
    ''' (the object as it was before the placement arguments placed it, the arguments), or (shape, None) for an object that was not placed by them '''
    args = getattr(shape, '_placement_args', None)
    parts = getattr(shape, '_placed_parts', None)
    if args is not None and parts is not None:
        return parts[0], args
    return shape, None


def place(value, position=None, rotation=None, scale=None, alpha=None, origin=None):
    ''' `value` placed: moved by `position` (x, y, z), turned by `rotation` (x, y, z degrees) and scaled by `scale` (x, y, z factors, or one number), all about the middle
        of its box, and drawn as solid as `alpha` (0 to 1) says.

        This is what the placement arguments of every model's call do: `box((20, 10, 5), position=(10, 0, 0))` is `place(box((20, 10, 5)), position=(10, 0, 0))`.  It is written for what is
        not made by a call -- a part taken out of an imported model, `part = place(part, position=(10, 0, 0))`.  FielDes's gizmo drags the numbers of either:
        write them as `var(...)` (the model tree does that when the model is selected). '''
    return _apply(value, position, rotation, scale, alpha, origin)


def _old_form(fn, signature, args, kwargs, error):
    ''' What a script written before the one global position is told when its call of a primitive does not fit any more -- box(a, b) with two corners, sphere(5, (0, 0, 0)), cylinder(5, 10, base=...)
        (the user's own saved files are in that form): the form to write now, with the numbers of the old call worked out where they are plain numbers.  None when the error is not about the
        arguments of the call (a TypeError from inside the function stays what it was) '''
    text = str(error)
    if 'the place of a model is its' in text:
        return None
    try:
        signature.bind(*args, **kwargs)
        return None
    except TypeError:
        pass
    name = getattr(fn, '__name__', 'function')
    if name == 'box' and len(args) == 2:
        def plain(v):
            try:
                return len(v) == 3 and all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v)
            except TypeError:
                return False
        if plain(args[0]) and plain(args[1]):
            a, b = args
            size = tuple(float(q) - float(p) for p, q in zip(a, b))
            middle = tuple((float(p) + float(q)) / 2.0 for p, q in zip(a, b))
            return ('box(): a box is made by its size and placed by its position (the middle of the box), not by two corners any more -- for the corners %s and %s write '
                    'box((%g, %g, %g), position=(%g, %g, %g))' % (tuple(a), tuple(b), size[0], size[1], size[2], middle[0], middle[1], middle[2]))
        return ('box(): a box is made by its size and placed by its position (the middle of the box), not by two corners any more -- write box((width, depth, height), position=(x, y, z)); '
                'the size is the difference of the two corners, the position their average')
    if ('takes' in text and 'positional' in text) or 'unexpected keyword' in text:
        return '%s(): %s -- the place of a model is its `position=(x, y, z)` argument, not an argument of its own' % (name, text.split('() ', 1)[-1] if '() ' in text else text)
    return None


def _wrap(fn, frame=None):
    ''' `fn` with the placement arguments: the result is placed by them when any is given, and is what `fn` made when none is.  `frame`: see _apply '''
    try:
        signature = inspect.signature(fn)
    except (TypeError, ValueError):
        return fn
    if any(name in signature.parameters for name in PLACEMENT):
        return fn                                           # (it has an argument of that name of its own: it is not given the placement)
    if getattr(fn, '_placeable', False):
        return fn

    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        given = {name: kwargs.pop(name) for name in PLACEMENT if name in kwargs}
        try:
            out = fn(*args, **kwargs)
        except TypeError as e:
            note = _old_form(fn, signature, args, kwargs, e) if frame == 'origin' else None
            if note is None:
                raise
            raise TypeError(note) from None
        if not given:
            return out
        return _apply(out, given.get('position'), given.get('rotation'), given.get('scale'), given.get('alpha'), given.get('origin'), frame)

    parameters = list(signature.parameters.values())
    extra = [inspect.Parameter(name, inspect.Parameter.KEYWORD_ONLY, default=None) for name in PLACEMENT]
    if parameters and parameters[-1].kind == inspect.Parameter.VAR_KEYWORD:
        parameters = parameters[:-1] + extra + parameters[-1:]
    else:
        parameters = parameters + extra
    wrapper.__signature__ = signature.replace(parameters=parameters)
    wrapper._placeable = True
    wrapper._placement_frame = frame
    return wrapper


def _local(fn, hidden):
    ''' `fn` without the arguments `hidden` (its place: see LOCAL): they stay at their defaults, the origin of the frame it is made in '''
    signature = inspect.signature(fn)
    kept = signature.replace(parameters=[p for name, p in signature.parameters.items() if name not in hidden])

    @functools.wraps(fn)
    def local(*args, **kwargs):
        try:
            bound = kept.bind(*args, **kwargs)
        except TypeError as e:
            # (the place of a model is its `position=` argument: a script that still gives it as an argument of its own -- point(1, 2, 3), sphere(5, (0, 0, 0)), box(centre=...) -- is told where it went)
            if 'positional' in str(e) or any(name in str(e) for name in hidden):
                raise TypeError('%s(): %s -- the place of a model is its `position=(x, y, z)` argument, not an argument of its own' % (getattr(fn, '__name__', 'function'), e)) from None
            raise
        call_args, call_kwargs = [], {}
        for name, value in bound.arguments.items():
            kind = kept.parameters[name].kind
            if kind is inspect.Parameter.VAR_POSITIONAL:
                call_args = list(value)
            elif kind is inspect.Parameter.VAR_KEYWORD:
                call_kwargs.update(value)
            else:
                call_kwargs[name] = value
        return fn(*call_args, **call_kwargs)

    local.__signature__ = kept
    return local


def install(namespace):
    ''' Gives the functions of PLACEABLE the placement arguments, in `namespace` (what `from fieldes import *` brings in) '''
    import importlib
    for module, names in PLACEABLE.items():
        mod = importlib.import_module('fieldes.stdlib.' + module)
        for name in names:
            raw = getattr(mod, name, None)
            current = namespace.get(name)
            if raw is None or current is None:
                continue
            # (the function in the namespace: it may be one that another module wrote after this one under the same name -- `rotate` of the CAD module over the transforms')
            wrapped = _wrap(current)
            if wrapped is not current:
                namespace[name] = wrapped
    # The primitives: what a script calls is made of the SIZE alone, in its own frame, and placed by its ONE global position (see shapes.SIZED)
    from fieldes.stdlib.shapes import SIZED
    for name, function in SIZED.items():
        namespace[name] = _wrap(function, 'origin')
    for module, table in LOCAL.items():
        for name, hidden in table.items():
            current = namespace.get(name)
            if current is not None:
                namespace[name] = _wrap(_local(current, hidden), 'origin')


def takes_placement(function):
    ''' Whether a function's calls take the placement arguments (what the model tree asks to know whether a model's placement is written in its call) '''
    return bool(getattr(function, '_placeable', False))
