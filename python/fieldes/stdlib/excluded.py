'''
exclude(): a region of a shape that stays exactly as it is

    part = import_model('bracket.step')[0][0]
    part = exclude(part, sphere(12, (0, 0, 6)))        # inside the sphere the part is locked
    light = shell(part, 2)                              # ... and nothing done to it afterwards touches that

exclude() cuts the shape into two fields that are always united again:

    the free field    the shape outside the region (the shape with the region cut out of it): what every later
                      operation reshapes
    the locked field  the shape inside the region, put there once and never reshaped by anything done to the shape
                      afterwards.  For a part imported from a STEP file it is the part's own exact surface, meshed
                      straight from the file (not what the import fitted) and made into a field; for any other
                      shape it is the shape itself inside the region.

An operation on an excluded shape (offset, shell, smooth, a lattice, a union, a cut, an analysis ...) works on the
whole shape, and then the locked field is put back: wherever the region is, the shape is the locked one, whatever the
operation did there.  (smooth() works on the field the shape was made from instead of the united one, whose values are
cut off at the distance to the region and would make the smoothed surface ripple round it.)  Moving, turning, scaling
and mirroring carry both fields along together.  Operations that would
tear the two apart -- copying the shape (arrays, symmetric_*, repeat, mirror_*) or deforming it (twist, bend, taper,
attract, repel ...) -- raise an ExcludedError: do them before exclude().  The optimisations (topology_optimization,
thermal_topology_optimization, flow_topology_optimization) read the exclusions a shape carries as regions to keep: the
locked field stays solid, as exact as it is, and the optimised shape is excluded the same way.  The tables below say, for every function of the
library, what it does with an excluded shape; none is left out.

The region is any shape (a field that is negative inside it), as it always was.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import functools
import sys

from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['exclude', 'ExcludedError']


class ExcludedError(ValueError):
    ''' An operation that would separate an excluded shape's locked field from the rest of it '''


# ---------------------------------------------------------------------------
# What an excluded shape is
# ---------------------------------------------------------------------------
#
# A shape with `_locks`: a tuple of (locked field, region), one for each exclude().  Its own tree is the whole of it,
#
#     min(max(free, -region_1, -region_2, ...), locked_1, locked_2, ...)
#
# (the free field outside every region, each locked field inside its region), so everything that reads a shape as a field
# -- rendering, analyses, the exact distance -- sees the united shape.

def locks_of(shape):
    return tuple(getattr(shape, '_locks', None) or ())


def _clip(body, locks):
    ''' `body` with every region cut out of it and the locked fields in their places '''
    out = body
    for _, region in locks:
        out = out.max(-region)
    for locked, _ in locks:
        out = out.min(locked)
    return out


def keep_regions(*shapes):
    """ What an optimisation must keep solid for these shapes: the locked field of every exclusion they carry (the shape
        inside its region, as it is) -- topology_optimization, thermal_topology_optimization and
        flow_topology_optimization add these to their `keep` by themselves, however many operations came between
        exclude() and the optimisation (the locks travel with the shape). """
    out, seen = [], set()
    for s in shapes:
        for locked, _ in locks_of(s):
            if id(locked) not in seen:
                seen.add(id(locked))
                out.append(locked)
    return out


def carry_locks(out, *shapes):
    """ `out`, made from `shapes` by something that cannot be guarded (an optimised body), is excluded the way they
        are: its locked regions are the optimisation's kept ones, so it is the shape there """
    found = [s for s in shapes if isinstance(s, Shape) and getattr(s, '_locks', None)]
    if found and isinstance(out, Shape):
        out._locks = _merged(found)
    return out


def _like(model, tree):
    ''' A shape of the kind `model` is, with the math of `tree` and the attributes of `model` (its colours, its name ...) '''
    ptr = lib.libfive_tree_copy(tree.ptr)
    try:
        out = type(model)(ptr)
    except TypeError:
        out = Shape(ptr)
    for name, value in model.__dict__.items():
        if name != 'ptr':
            out.__dict__[name] = value
    return out


def _operands(args, kwargs, first_only):
    ''' The shapes an operation was given that are excluded ones (a list or tuple of shapes is looked into) '''
    given = list(args) + list(kwargs.values())
    if first_only:
        given = given[:1]
    found = []
    for a in given:
        for item in (a if isinstance(a, (list, tuple)) else [a]):
            if isinstance(item, Shape) and getattr(item, '_locks', None):
                found.append(item)
    return found


def _merged(shapes):
    locks, seen = [], set()
    for s in shapes:
        for pair in s._locks:
            if id(pair) not in seen:
                seen.add(id(pair))
                locks.append(pair)
    return tuple(locks)


# ---------------------------------------------------------------------------
# What every function of the library does with an excluded shape
# ---------------------------------------------------------------------------
#
# KEEPS_ALL    works on the whole shape, then the locked fields of every excluded shape it was given are put back
# KEEPS_FIRST  the same, with the locked fields of the first argument only (the others are tools: the shape that is
#              subtracted, a lattice's field ...)
# DECORATES    returns the shape with something attached (colours): it stays excluded
# MOVES        space transforms (fieldes.stdlib.transforms): the locked fields and their regions move with it
# REFUSES      raises ExcludedError (copies or deforms the shape, which would separate the locked field from the rest)
# THROUGH      does not make a body from an excluded shape (a field, a number, an analysis ...), or only reads it
#              (analyses take the whole shape): nothing to keep
# NO_BODY      has no shape to be excluded among its arguments (primitives, importers, solver conditions ...)
#
# Everything is by module name -> function names.

KEEPS_ALL = {
    'csg': ['union', 'intersection', 'blend_expt', 'blend_expt_unit', 'blend_rough', 'blend', 'morph'],
    'fields': ['smooth_union', 'smooth_intersection', 'chamfer_union', 'union_all', 'intersection_all'],
}

KEEPS_FIRST = {
    'csg': ['inverse', 'difference', 'offset', 'clearance', 'shell', 'blend_difference'],
    'fields': ['thicken', 'shell_inside', 'shell_outside', 'shell_centered', 'offset_by', 'smooth_difference',
               'offset_exact', 'shell_exact', 'round_edges', 'fillet', 'smooth'],
    'lattices': ['fill', 'lattice'],
    'fea': ['topology_optimization'],
    'thermal': ['thermal_topology_optimization'],
    'fluid': ['flow_topology_optimization'],
}

DECORATES = {
    'fea': ['colored'],
}

MOVES = {
    'transforms': ['move', 'rotate_x', 'rotate_y', 'rotate_z', 'rotate', 'scale_x', 'scale_y', 'scale_z', 'scale_xyz',
                   'reflect_x', 'reflect_y', 'reflect_z', 'reflect_xy', 'reflect_yz', 'reflect_xz'],
}

REFUSES = {
    'shapes': ['array_x', 'array_xy', 'array_xyz', 'array_polar_z', 'array_polar', 'extrude_z'],
    'csg': ['loft', 'loft_between'],
    'transforms': ['symmetric_x', 'symmetric_y', 'symmetric_z', 'taper_x_y', 'taper_xy_z', 'shear_x_y',
                   'repel', 'repel_x', 'repel_y', 'repel_z', 'repel_xy', 'repel_yz', 'repel_xz',
                   'attract', 'attract_x', 'attract_y', 'attract_z', 'attract_xy', 'attract_yz', 'attract_xz',
                   'revolve_y', 'twirl_x', 'twirl_axis_x', 'twirl_y', 'twirl_axis_y', 'twirl_z', 'twirl_axis_z'],
    'fields': ['repeat', 'repeat_polar', 'mirror_x', 'mirror_y', 'mirror_z', 'twist_z', 'bend_z'],
    'handles': ['expose'],
}

THROUGH = {
    'handles': ['handles', 'lock', 'exposed_values'],
    'render_cache': ['render_cache', 'render_cache_key'],
    'custom_resolution': ['custom_resolution'],
    'fea': ['static_analysis', 'modal_analysis'],
    'thermal': ['thermal_analysis'],
    'fluid': ['fluid_analysis'],
    'boundary_conditions': ['static_boundary_conditions'],
    'selection': ['select_surface', 'surface_from_bodies'],
    'lattices': ['relative_density', 'lattice_parameter_for_density', 'voronoi_graph', 'surface_graph', 'cell_custom'],
    'conformal': ['lattice_surface_conform'],
    'fields': ['render_mesh', 'distance_to_surface', 'depth_below', 'signed_distance', 'clamp', 'ramp', 'remap_field',
               'normalize', 'lerp', 'mix', 'smoothstep', 'step_field', 'sum_fields', 'evaluate', 'sample_grid',
               'field_range', 'volume_of', 'mass_properties', 'find_extent', 'exact_distance', 'gradient_field',
               'gradient_magnitude', 'normal_field', 'overhang_angle', 'overhang_mask', 'wall_thickness',
               'curvature_field', 'add_fields', 'subtract_fields', 'multiply_fields', 'divide_fields', 'power_field',
               'min_fields', 'max_fields', 'abs_field', 'negate_field', 'sqrt_field', 'square_field', 'field_from_body',
               'body_from_field', 'maximum', 'minimum'],
    'cad_import': ['roi', 'roi_resolution', 'poor_fit_region'],
    'excluded': ['exclude'],
    'fieldargs': ['is_field'],
}

# (modules whose functions never take a shape that could be excluded)
NO_BODY_MODULES = ['text', 'importing', 'tessellated_import', 'mesh_import', 'regression', 'surfaces']
NO_BODY = {
    'shapes': ['circle', 'ring', 'polygon', 'rectangle', 'rounded_rectangle', 'rectangle_exact',
               'rectangle_centered_exact', 'triangle', 'box_mitered', 'box_mitered_centered', 'box_exact_centered',
               'box_exact', 'rounded_box', 'sphere', 'half_space', 'cylinder_z', 'cone_ang_z', 'cone_z', 'pyramid_z',
               'torus_z', 'gyroid', 'emptiness', 'cube', 'cube_centered', 'box', 'box_centered', 'rounded_cube',
               'cylinder', 'cone_ang', 'cone', 'torus'],
    'cad_import': ['step_length_unit_mm',
                   'exact_field'],
    'excluded': ['locks_of', 'moved', 'install', 'install_methods', 'keep_regions', 'carry_locks'],
    'points': ['point', 'surface', 'plane', 'sphere_surface', 'cylinder_surface', 'wave_surface'],
    'fea': ['fixed', 'force', 'gravity', 'thermal_expansion'],
    'thermal': ['fixed_temperature', 'heat_input', 'heat_generation', 'convection'],
    'fluid': ['inlet', 'outlet', 'wall', 'slip', 'symmetry'],
    'fields': ['x_field', 'y_field', 'z_field', 'radial_field', 'angle_field', 'polar_field', 'distance_to_point',
               'distance_to_points', 'distance_to_line', 'distance_to_segment', 'distance_to_polyline',
               'distance_to_plane', 'attractor', 'wave', 'field_from_points', 'field_from_csv', 'noise_field',
               'low_level_field', 'low_level_body'],
    'lattices': ['cartesian', 'cylindrical', 'spherical', 'tpms', 'unit_cell_beams', 'strut_lattice', 'planar_lattice',
                 'cell_periodic', 'cell_non_periodic', 'cell_custom_truss', 'cell_custom_tpms', 'graph_lattice',
                 'points_graph'],
}


def _free_field(shape):
    ''' The field an excluded shape was made from, before its regions were locked (None where it is not known) '''
    return getattr(shape, '_free', None) if getattr(shape, '_locks', None) else None


def _on_free_field(args, kwargs):
    ''' The operands of an operation that works on the first of them, with that one -- if it is an excluded shape --
        replaced by its free field.  The united tree of an excluded shape has the locks in its values: inside the
        shape and near a region the field is cut off at the distance to the region, which is right for drawing it but
        not a distance, and an operation that averages the field over the points round a point (smooth) reads those
        values and ripples there. '''
    args, kwargs = list(args), dict(kwargs)
    if args:
        free = _free_field(args[0])
        if free is not None:
            args[0] = free
    elif kwargs:
        k = next(iter(kwargs))
        free = _free_field(kwargs[k])
        if free is not None:
            kwargs[k] = free
    return args, kwargs


def _keeps(name, fn, first_only, on_free=False):
    def g(*args, **kwargs):
        found = _operands(args, kwargs, first_only)
        if not found:
            return fn(*args, **kwargs)
        locks = _merged(found)
        if on_free:
            a, kw = _on_free_field(args, kwargs)
            result = fn(*a, **kw)
        else:
            result = fn(*args, **kwargs)
        if not isinstance(result, Shape):
            return result
        out = _like(result, _clip(result, locks))
        out._locks = locks
        if on_free:
            out._free = result          # (so that the next one works on it too)
        return out
    functools.update_wrapper(g, fn)       # (its signature, for the call tips and for what the model tree can add to it)
    g.__name__ = name
    return g


def _decorates(name, fn):
    def g(*args, **kwargs):
        found = _operands(args, kwargs, True)
        result = fn(*args, **kwargs)
        if not found or not isinstance(result, Shape):
            return result
        if any(result is a for a in args):
            result = _like(result, result)
        result._locks = _merged(found)
        free = _free_field(found[0])
        if free is not None:
            result._free = free
        return result
    functools.update_wrapper(g, fn)       # (its signature, for the call tips and for what the model tree can add to it)
    g.__name__ = name
    return g


def _refuses(name, fn):
    def g(*args, **kwargs):
        if _operands(args, kwargs, False):
            if name == 'expose':
                raise ExcludedError("expose(): an excluded shape cannot be dragged by its surfaces (that would move "
                                    "its locked part too). Drag the shape with its gizmo, or expose() before exclude().")
            raise ExcludedError("%s() cannot work on an excluded shape: it copies or deforms the shape, which would "
                                "tear its locked part away from the rest. Do it before exclude(), e.g. "
                                "exclude(%s(part, ...), region)." % (name, name))
        return fn(*args, **kwargs)
    functools.update_wrapper(g, fn)       # (its signature, for the call tips and for what the model tree can add to it)
    g.__name__ = name
    return g


def moved(prev, shape, args, kwargs):
    ''' The locked fields and regions of `shape`, moved the way `prev` (a space transform) has just moved the shape '''
    return tuple((prev(locked, *args, **kwargs), prev(region, *args, **kwargs)) for locked, region in shape._locks)


_TABLES = ((KEEPS_ALL, lambda n, f: _keeps(n, f, False)),
           (KEEPS_FIRST, lambda n, f: _keeps(n, f, True, on_free=(n == 'smooth'))),
           (DECORATES, _decorates),
           (REFUSES, _refuses))
_wrapped = {}           # (module, name) -> the guarded function (one for the package and for the methods of Shape)


def _each_guarded():
    for table, make in _TABLES:
        for module_name, names in table.items():
            module = sys.modules.get('fieldes.stdlib.' + module_name)
            if module is None:
                continue
            for n in names:
                raw = getattr(module, n, None)
                if raw is None:         # (a name the library no longer has)
                    continue
                if (module_name, n) not in _wrapped or _wrapped[(module_name, n)][0] is not raw:
                    _wrapped[(module_name, n)] = (raw, make(n, raw))
                yield n, raw, _wrapped[(module_name, n)][1]


def install_methods():
    ''' The methods of Shape that are functions of the library (shape.py binds them as it is loaded: union, offset, the
        transforms ...) guarded.  Done again, it changes nothing. '''
    for n, raw, guarded in _each_guarded():
        if getattr(Shape, n, None) is raw:
            setattr(Shape, n, guarded)


def install():
    ''' Puts the guards round the functions of the library (when the library is loaded): the functions the tables above
        name are replaced by versions that look at their arguments, in the package (what `from fieldes import *`
        brings into a script) and in the methods of Shape.  The library's own calls of one another, inside its modules,
        are not guarded: a difference is built from an inverse and an intersection, and only the difference is asked
        about the shape it is given.  A call that is given no excluded shape goes straight to the function. '''
    import fieldes.stdlib as pkg
    for n, raw, guarded in _each_guarded():
        if getattr(pkg, n, None) is raw:
            setattr(pkg, n, guarded)
    install_methods()


# ---------------------------------------------------------------------------
# exclude()
# ---------------------------------------------------------------------------

def _cad():
    from fieldes.stdlib import cad_import
    return cad_import


def _union_of(regions):
    out = None
    for r in regions:
        r = Shape.wrap(r)
        out = r if out is None else out.min(r)
    return out


def exclude(shape, *regions, source=None, quality=64, threshold=1.0):
    ''' `shape`, with the places inside `regions` locked: the shape there stays exactly as it is, whatever is done to
        the shape afterwards.  (See the top of this file for what that means; in the model tree's right-click menu it
        is Combining > exclude, with the shape first and the regions after it.)

            part = import_model('bracket.step')[0][0]
            part = exclude(part, sphere(12, (0, 0, 6)), box_exact((0, 0, 0), (5, 5, 5)))
            light = shell(part, 2)           # shelled everywhere but inside the sphere and the box

        A region is any shape -- a sphere, a box, a part, a part offset by some distance, a union of those: the
        places where its field is negative.  Several regions are one region, their union.

        For a part imported from a STEP file the locked part is the part's EXACT surface, meshed straight from the STEP
        file and made into a field (not the import's fit, which is only approximate where B-spline faces were fitted
        by simple surfaces -- the red places -- or where there are threads).  For any other shape the locked part is
        the shape itself, as it is now.  Either way it is a field of its own, united with the rest after every
        operation; an operation that would separate them (copying or deforming the shape) raises an ExcludedError.

        Without a region, a STEP part's poorly fitted places are the region (poor_fit_region), in every part that has
        any:

            kitchen = import_model('step/kitchen.stp')
            kitchen = exclude(kitchen)

        What you give it decides how much it works on.  The WHOLE IMPORT (what import_model returns): the region
        goes into every part it touches, and the same list comes back, the parts in the same places.  One entry of the
        import, kitchen[19], or one part, kitchen[19][0]: that part only.

            kitchen[19] = exclude(kitchen[19], region)

        For a shape made from an imported part (cut, filled with a lattice ...) say which part its locked geometry
        comes from, and exclude last, to the finished shape:

            final = exclude(light, region, source=part)

        The part may have been moved, rotated, scaled or mirrored with the fieldes.stdlib transforms since the
        import (the locked part follows).

        quality: points per full turn of a circle in the exact STEP mesh.
        threshold: for the default region, in percent of the face's size. '''
    ci = _cad()
    if isinstance(shape, (list, tuple)) and not ci._is_pair(shape):
        if source is not None:
            raise ValueError("exclude(): source= is for one part; the import's parts are their own sources")
        field = _union_of(regions)
        done = []
        for item in shape:
            part = item[0] if ci._is_pair(item) else item
            if not isinstance(part, Shape) or not ci._sources_of(part):
                done.append(item)               # (a part that failed to import, anything else: as it is)
                continue
            mine = field if field is not None else ci.poor_fit_region(part, threshold)
            if mine is None:
                done.append(item)               # (fitted well everywhere: nothing to do)
                continue
            bounds = item[1] if ci._is_pair(item) else getattr(part, '_bounds', None)
            if field is not None and bounds is not None and not ci._may_touch(mine, bounds):
                done.append(item)               # (the region does not reach this part)
                continue
            done.append(exclude(item, mine, quality=quality))
        return type(shape)(done)
    if ci._is_pair(shape):
        return (exclude(shape[0], *regions, source=source, quality=quality, threshold=threshold), shape[1])
    if not isinstance(shape, Shape):
        raise TypeError('exclude(shape, region): shape must be a Shape, not {}'.format(type(shape).__name__))

    of = source if source is not None else shape
    sources = ci._sources_of(of)
    if regions:
        region = _union_of(regions)
    else:
        region = ci.poor_fit_region(of, threshold) if sources else None
        if region is None:
            if sources:
                return shape                    # (fitted well everywhere: nothing to do)
            raise ValueError('exclude(shape, region): say which region of the shape to lock (a shape: the places '
                             'where it is negative)')

    # the locked field: the part's exact surface inside the region, or the shape itself
    locked = (ci.exact_field(sources, quality) if sources else shape).max(region)
    lock = (locked, region)
    out = _clip(shape, (lock,))
    for name in ci._CARRIED:
        if hasattr(shape, name):
            try:
                setattr(out, name, getattr(shape, name))
            except AttributeError:
                pass
    # (the part's own render resolution, set by roi_resolution on it or on this shape, whichever comes first:
    # FielDes follows this link to the part)
    out._placed_from = shape
    # The red marking of poorly fitted B-spline faces doesn't apply where the geometry is now exact
    if getattr(out, '_color_map', None) == 'fit' and getattr(out, '_color_field', None) is not None:
        out._color_field = out._color_field.min((region * 1.0e6).max(0))
    ci._carry_exact(out, shape)
    out._locks = locks_of(shape) + (lock,)
    # the field it was made from, which smooth() works on (the united tree has the locks in its values: see _on_free_field)
    free = _free_field(shape)
    out._free = free if free is not None else shape
    return out
