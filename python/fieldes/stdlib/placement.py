''' Placement: where an object is, how it is turned, how big it is and how see-through.

    Every object that is made by a call -- a shape, the result of an operation, a copy, a lattice -- takes five arguments after its own, named and in this order (the fifth is written when it is set):

        position   (x, y, z) in mm: how far the object is moved from where its definition puts it (0 is where it is made)
        rotation   (x, y, z) in degrees: how it is turned about the middle of its box, x first, then y, then z
        scale      (x, y, z) factors, or one number: how big it is, about the middle of its box
        alpha      0 to 1: how solid it is drawn (1 is solid, 0 is a ghost, 5 % solid at the least): the object is the same in every other way
        origin     (x, y, z) in mm, in the place its definition puts it: the point it is turned and scaled about and where its gizmo is.  Not given: the middle of its box.  FielDes writes it when the
                   key O makes a point of the scene the origin of the selected model, and moves `position` to make up for it when the model is turned or scaled, so that the model stays where it is

    They are ordinary arguments of the call.  FielDes's model tree shows them inside every object, a click on one selects its number in the code, and the gizmo in the
    viewport drags the first three: when the object is selected, FielDes writes them with `var()` numbers (`position=(var(40), var(0), var(0))`) and a drag changes the
    numbers in the text.  None of them is needed: an object without them stands where its definition puts it.

        part = box((0, 0, 0), (20, 10, 5), position=(40, 0, 0), rotation=(0, 0, 30), alpha=0.5)

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
    'shapes': ('array_polar', 'array_polar_z', 'array_x', 'array_xy', 'array_xyz', 'box', 'circle', 'cone', 'cylinder', 'half_space', 'polygon', 'prism', 'pyramid',
               'rectangle', 'ring', 'rounded_box', 'rounded_rectangle', 'sphere', 'torus', 'triangle'),
    'csg': ('blend', 'blend_difference', 'blend_expt', 'blend_expt_unit', 'blend_rough', 'clearance', 'difference', 'intersection', 'inverse', 'loft', 'loft_between',
            'morph', 'offset', 'shell', 'union'),
    'transforms': ('attract', 'attract_x', 'attract_xy', 'attract_xz', 'attract_y', 'attract_yz', 'attract_z', 'move', 'reflect_x', 'reflect_xy', 'reflect_xz', 'reflect_y',
                   'reflect_yz', 'reflect_z', 'repel', 'repel_x', 'repel_xy', 'repel_xz', 'repel_y', 'repel_yz', 'repel_z', 'revolve_y', 'rotate', 'rotate_x', 'rotate_y',
                   'rotate_z', 'scale_x', 'scale_xyz', 'scale_y', 'scale_z', 'shear_x_y', 'symmetric_x', 'symmetric_y', 'symmetric_z', 'taper_x_y', 'taper_xy_z',
                   'twirl_axis_x', 'twirl_axis_y', 'twirl_axis_z', 'twirl_x', 'twirl_y', 'twirl_z'),
    'cad': ('capsule', 'circular_pattern', 'cut_plane', 'linear_pattern', 'mirror', 'reflect', 'revolve', 'rotate', 'slot', 'threaded_rod', 'tube'),
    'fields': ('bend_z', 'body_from_field', 'chamfer_union', 'fillet', 'intersection_all', 'mirror_x', 'mirror_y', 'mirror_z', 'offset_by', 'offset_exact', 'repeat',
               'repeat_polar', 'round_edges', 'shell_centered', 'shell_exact', 'shell_inside', 'shell_outside', 'smooth_difference', 'smooth_intersection', 'smooth_union',
               'thicken', 'twist_z', 'union_all'),
    'lattices': ('delaunay_lattice', 'fill', 'graph_lattice', 'lattice', 'planar_lattice', 'point_cloud', 'random_points', 'strut_lattice', 'surface_lattice', 'tpms', 'voronoi_lattice'),
    'conformal': ('lattice_surface_conform',),
    'profile': ('extrude', 'path_extrude', 'profile'),
    'curves': ('polyline', 'spline'),
    'points': ('cylinder_surface', 'plane', 'point', 'sphere_surface', 'surface', 'wave_surface'),
    'surfaces': ('coil_spring', 'ellipsoid', 'elliptic_cylinder', 'helical_revolve', 'helical_sweep', 'hyperboloid', 'paraboloid', 'wave_block'),
    'instances': ('instance',),
    'symmetries': ('symmetry_planes',),
    'text': ('text',),
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


def _apply(shape, position=None, rotation=None, scale=None, alpha=None, origin=None):
    ''' `shape` placed by the arguments: moved, turned and scaled about the middle of its box (as the gizmo does), and drawn as solid as `alpha` says '''
    if position is None and rotation is None and scale is None and alpha is None and origin is None:
        return shape
    if position is not None or rotation is not None or scale is not None or origin is not None:
        from fieldes.stdlib.handles import _place
        out = _place(shape, move=_vec(position, 'position', (0, 0, 0)), rotate=_vec(rotation, 'rotation', (0, 0, 0)), scale=_vec(scale, 'scale', (1, 1, 1)),
                     about=None if origin is None else _vec(origin, 'origin', (0, 0, 0)))
    else:
        out = _copy_of(shape)
    a = _alpha_value(alpha)
    if a is not None:
        try:
            out._alpha = a
        except AttributeError:
            raise TypeError('alpha=: a %s has no surface to draw see-through' % type(shape).__name__)
    out._placement_args = (position, rotation, scale, alpha, origin)
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

        This is what the placement arguments of every model's call do: `box(a, b, position=(10, 0, 0))` is `place(box(a, b), position=(10, 0, 0))`.  It is written for what is
        not made by a call -- a part taken out of an imported model, `part = place(part, position=(10, 0, 0))`.  FielDes's gizmo drags the numbers of either:
        write them as `var(...)` (the model tree does that when the model is selected). '''
    return _apply(value, position, rotation, scale, alpha, origin)


def _wrap(fn):
    ''' `fn` with the placement arguments: the result is placed by them when any is given, and is what `fn` made when none is '''
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
        out = fn(*args, **kwargs)
        if not given:
            return out
        return _apply(out, given.get('position'), given.get('rotation'), given.get('scale'), given.get('alpha'), given.get('origin'))

    parameters = list(signature.parameters.values())
    extra = [inspect.Parameter(name, inspect.Parameter.KEYWORD_ONLY, default=None) for name in PLACEMENT]
    if parameters and parameters[-1].kind == inspect.Parameter.VAR_KEYWORD:
        parameters = parameters[:-1] + extra + parameters[-1:]
    else:
        parameters = parameters + extra
    wrapper.__signature__ = signature.replace(parameters=parameters)
    wrapper._placeable = True
    return wrapper


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


def takes_placement(function):
    ''' Whether a function's calls take the placement arguments (what the model tree asks to know whether a model's placement is written in its call) '''
    return bool(getattr(function, '_placeable', False))
