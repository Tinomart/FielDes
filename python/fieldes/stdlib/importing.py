'''
Importing a model: import_model(), reconstruct() and tessellate().

    parts = import_model("bracket.step")           # STEP or mesh: each part the way that suits it
    part, bounds = parts[0]
    view.set_bounds(*roi(parts))

import_model() is the one function to know.  It reads a STEP file (.step, .stp) or a triangle mesh (.stl, .obj, .ply,
.3mf, .glb, .gltf) and returns a list of (shape, (lower corner, upper corner)), one entry per part -- one for a mesh --
each part placed where the file has it, in the units you ask for.  What is a field here is chosen by the file:

    a mesh                  the exact signed distance to its triangles
    a part of a STEP file   reconstructed (reconstruct()) -- planes, cylinders, cones, spheres and tori as exact
                            formulas, free-form faces fitted: fast, light, and the faces can be dragged -- unless
                            more than `threshold` (10 % by default) of its surface is free-form (B-spline): a
                            sculpted body, a gear, a thread, which a fit does not follow.  That part is tessellated
                            (tessellate()): its exact surface as triangles, made a distance field.  A surface body
                            (an open sheet of faces, which has no inside) is always tessellated
    the choice is made part by part (an assembly can have both); the model tree says which each part is

The other two are the same import, made one way by name when you want that:

    reconstruct(path)       every part rebuilt as CSG from its faces (STEP files only)
    tessellate(source)      the exact surface as a distance field -- of a STEP file's parts, of a mesh file, of ANY
                            field (tessellate(shape): the field's surface made a mesh and the exact distance to it,
                            which makes offsets, shells and lattices uniform), of a list of parts

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import json
import os

from fieldes import run_progress
from fieldes.ffi import lib, libfive_region_t, libfive_interval_t, libfive_mesh_import_info_t
from fieldes.shape import Shape
from fieldes.stdlib import cad_import as _cad
from fieldes.stdlib import mesh_import as _mesh
from fieldes.stdlib import tessellated_import as _tess
from fieldes.stdlib.content_cache import content_cached
from fieldes.stdlib.fields import _s, _shape_bounds, render_mesh, _vars_key

__all__ = ['import_model', 'reconstruct', 'tessellate']

_STEP = ('.step', '.stp')
_MESH = ('.stl', '.obj', '.ply', '.3mf', '.glb', '.gltf')

# The share of a part's surface that is free-form (B-spline) from which it is tessellated rather than reconstructed.
# Measured on the parts of the example files: the reconstruction is exact for
# analytic faces and fits a free-form face to within a fraction of a percent of its size, so a part with a few
# percent of free-form surface (a fillet, a blend) is still best rebuilt, while a part with a tenth of it or more
# has fitted faces that show; the sculpted parts all have more than half.
THRESHOLD = 0.10

# A reconstructed part has its free-form faces fitted by closed-form surfaces, and the reconstruction measures how far each fit is from
# the exact face.  From this deviation, in percent of the face's size, the part is tessellated instead: 1 % is where the fit starts to
# show (the model tree shades fits from 0.5 % grey to 10 % red).  A fit that is a fraction of a percent off is not seen, and
# the faces of such a part can be dragged.
FIT_TOLERANCE = 1.0


def _kind_of(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in _STEP:
        return 'step'
    if ext in _MESH:
        return 'mesh'
    return None


# ---------------------------------------------------------------------------
# What is in a STEP file: the share of each part's surface that is free-form
# ---------------------------------------------------------------------------

_survey_memo = {}


def _survey(step_path, cache=True):
    ''' What a STEP file holds, one entry per occurrence in the order import_model() returns them: {solid, instance,
        faces, bspline_faces, surface (a surface body: an open sheet), area, bspline_area, error}.  The areas come
        from a coarse tessellation (12 points a turn: a few percent off, which is all a share needs).  Kept next to
        the file, in the tessellation's folder, until the file or the tessellation changes. '''
    st = os.stat(step_path)
    memo_key = (os.path.normcase(step_path), st.st_size, st.st_mtime_ns)
    if memo_key in _survey_memo:
        return _survey_memo[memo_key]
    folder = step_path + _tess._SUFFIX
    stamp = None
    if cache:
        try:
            stamp = '{}:{}'.format(_cad._sha256_file(step_path), _tess._tessellation_version())
            with open(os.path.join(folder, 'survey.json'), 'r', encoding='utf-8') as f:
                saved = json.load(f)
            if saved.get('stamp') == stamp:
                _survey_memo[memo_key] = saved['items']
                return saved['items']
        except (OSError, ValueError, KeyError):
            pass
    with run_progress.task('looking at the STEP file'):
        run_progress.report(0.0, 'measuring the surfaces of ' + os.path.basename(step_path))
        ptr = lib.libfive_step_brep_read(step_path.encode('utf-8'), 12)
        if not ptr:
            raise RuntimeError('import_model({!r}): {}'.format(
                step_path, lib.libfive_import_step_last_message().decode('utf-8', 'replace')))
        try:
            b = ptr.contents
            solids = [(int(b.solids[i].faces), int(b.solids[i].bspline_faces), bool(b.solids[i].surface),
                       b.solids[i].error.decode('utf-8', 'replace') if b.solids[i].error else None)
                      for i in range(b.solid_count)]
            items = []
            for i in range(b.instance_count):
                p = b.instances[i]
                faces, bsplines, surface, error = solids[p.solid]
                items.append({'solid': int(p.solid), 'instance': int(p.instance), 'faces': faces,
                              'bspline_faces': bsplines, 'surface': surface, 'error': error,
                              'area': float(p.area_flat + p.area_curved), 'bspline_area': float(p.area_bspline)})
        finally:
            lib.libfive_step_brep_delete(ptr)
    _survey_memo[memo_key] = items
    if cache and stamp is not None:
        try:
            os.makedirs(folder, exist_ok=True)
            with open(os.path.join(folder, 'survey.json'), 'w', encoding='utf-8') as f:
                json.dump({'stamp': stamp, 'items': items}, f)
        except OSError:
            pass                                    # (a cache is an optimisation only)
    return items


def _free_form_share(item):
    return item['bspline_area'] / item['area'] if item['area'] > 0 else 0.0


def _tessellate_it(item, threshold):
    ''' Whether a part (an entry of _survey) is better tessellated than reconstructed '''
    if item['surface']:
        return True                   # (a surface body has no inside to rebuild: if it cannot be tessellated, that says why)
    if item['error']:
        return False                  # (it could not be tessellated: reconstructing may still work, or say why not)
    return _free_form_share(item) >= threshold


# ---------------------------------------------------------------------------
# The three functions
# ---------------------------------------------------------------------------

def _path_of(path, who):
    if not isinstance(path, (str, os.PathLike)):
        raise TypeError('{}: the first argument is the path of the file ({} is a {})'.format(
            who, path, type(path).__name__))
    path = os.fspath(path)
    if not os.path.exists(path):
        raise RuntimeError('{}({!r}): the file does not exist'.format(who, path))
    return os.path.abspath(path)


def _poor_fits(path, survey, candidates, tolerance):
    ''' The solids among `candidates` (survey entries' `solid` numbers) whose reconstruction fitted a free-form face off its
        surface by `tolerance` percent of the face's size or more, and how far off the worst one is: {solid: share}.  The
        fit report is what the reconstruction measured (cad_import._fit_reports) '''
    if tolerance is None:
        return {}
    report = _cad._fit_reports.get(os.path.normcase(os.path.abspath(path))) or []
    poor = {}
    for part, faces, worst, face, rel in report:
        if not 0 <= part < len(survey):
            continue
        solid = survey[part]['solid']
        if solid in candidates and 100.0 * rel >= tolerance:
            poor[solid] = max(poor.get(solid, 0.0), rel)
    return poor


def _worst_fit(path, survey):
    ''' {solid: the worst deviation of a fitted face, as a share of the face's size} for the parts that have one '''
    report = _cad._fit_reports.get(os.path.normcase(os.path.abspath(path))) or []
    out = {}
    for part, faces, worst, face, rel in report:
        if 0 <= part < len(survey):
            out[survey[part]['solid']] = max(out.get(survey[part]['solid'], 0.0), rel)
    return out


def import_model(path, units='mm', file_units=None, rev=None, cache=True, threshold=THRESHOLD, quality=64,
                 thickness=None, auto_exclude=False, exclude_threshold=1.0, exclude_quality=64, fit_tolerance=FIT_TOLERANCE):
    ''' Imports a STEP file (.step, .stp) or a triangle mesh (.stl, .obj, .ply, .3mf, .glb, .gltf) as a list of
        (shape, (lower corner, upper corner)): one entry per part of a STEP file (an assembly comes out assembled: every
        part is where the file puts it, a part used several times has an entry for each place), one for a mesh.  Nothing
        else to say: the way each part is made a field is chosen for it -- see the module's text:

            parts = import_model("bracket.step")
            part, bounds = parts[2]
            view.set_bounds(*roi(parts))

        A part with more than `threshold` of its surface free-form (B-spline) is tessellated, any other
        reconstructed -- and so is a reconstructed part whose free-form faces were fitted badly (`fit_tolerance`); a surface
        body (an open sheet) is always tessellated.  reconstruct() and tessellate() make every
        part one way.  A part that cannot be imported is a FailedPart that says why the moment it is used; the others
        import normally.

        units       the units of your script ('mm', 'cm', 'm', 'in'...): a STEP file declares its own (each part
                    its own; the numbers are converted), 'file' gives the first declared one
        file_units  what the numbers of a mesh file mean ('mm' if it does not say: STL, OBJ and PLY do not; 3MF does,
                    glTF is metres) -- for mesh files only
        rev         a number that is part of what an import is kept by: another one imports again ("Reimport" in FielDes)
        cache       an import is kept in files next to the STEP file (<file>.fieldes-cache.py, ...-tessellation):
                    False: not
        threshold   the free-form share of a part's surface from which it is tessellated (0.10: a tenth).  0 tessellates
                    every part, 1 reconstructs them all (a surface body is tessellated anyway)
        quality     points per full turn of a circle of a tessellated part (64: 0.05 % of a radius off the surface)
        thickness   of a surface body that does not close, which has no inside: it is made a sheet this thick (default
                    0.4 % of the size of the file), in `units`
        fit_tolerance
                    a part that is reconstructed has its free-form faces fitted by closed-form surfaces; when the worst of them is
                    off by this many percent of the face's size (1.0 by default) the fit does not look like the part, and the part is
                    tessellated instead.  None keeps every reconstruction
        auto_exclude, exclude_threshold, exclude_quality
                    for the reconstructed parts: see reconstruct() '''
    path = _path_of(path, 'import_model')
    kind = _kind_of(path)
    if kind is None:
        raise ValueError('import_model({!r}): not a file it can read; use a STEP file (.step, .stp) or a mesh '
                         '(.stl, .obj, .ply, .3mf, .glb, .gltf)'.format(path))
    if kind == 'mesh':
        return [_import_mesh(path, units, file_units, rev)]
    if file_units is not None:
        raise ValueError('import_model: file_units is for mesh files; a STEP file declares its units')
    threshold = float(threshold)
    if not 0.0 <= threshold <= 1.0:
        raise ValueError('import_model: threshold is a share of the surface, 0 to 1')
    if fit_tolerance is not None:
        fit_tolerance = float(fit_tolerance)
        if fit_tolerance < 0:
            raise ValueError('import_model: fit_tolerance is a percent of the size of a face, 0 or more (or None)')
    survey = _survey(path, cache)
    to_tessellate = sorted({it['solid'] for it in survey if _tessellate_it(it, threshold)})
    to_reconstruct = sorted({it['solid'] for it in survey} - set(to_tessellate))
    # (an import that is all one way is exactly reconstruct() or tessellate(): the same files are kept)
    recon = tess = None
    poor = {}
    if to_reconstruct:
        recon = _cad._reconstruct_step(path, cache=cache, units=units, rev=rev, auto_exclude=auto_exclude,
                                       exclude_threshold=exclude_threshold, exclude_quality=exclude_quality,
                                       only=to_reconstruct if to_tessellate else None, log=False)
        # What the reconstruction measured: a part whose free-form faces were fitted badly does not look like the part, and is
        # tessellated instead (the others, with a few faces fitted well, keep their faces that can be dragged)
        poor = _poor_fits(path, survey, set(to_reconstruct), fit_tolerance)
        if poor:
            to_tessellate = sorted(set(to_tessellate) | set(poor))
            to_reconstruct = sorted(set(to_reconstruct) - set(poor))
    if to_tessellate:
        tess = _tess._tessellate_step(path, units=units, quality=quality, cache=cache, rev=rev,
                                      only=to_tessellate if to_reconstruct else None, thickness=thickness, log=False)
    if not to_reconstruct:
        recon = None                                    # (every part went to the tessellation)
    tess_note = tess[2] if tess else None
    if tess is None:
        parts, names = recon
    elif recon is None:
        parts, names = tess[0], tess[1]
    else:
        (rparts, rnames), (tparts, tnames) = recon, tess[:2]
        if len(rparts) != len(tparts) or len(rparts) != len(survey):
            raise RuntimeError('import_model({!r}): the parts are not the same ones both ways ({} reconstructed, '
                               '{} tessellated, {} measured)'.format(path, len(rparts), len(tparts), len(survey)))
        chosen = set(to_tessellate)
        by_tessellation = [it['solid'] in chosen for it in survey]
        parts = [tparts[i] if by_tessellation[i] else rparts[i] for i in range(len(survey))]
        names = [tnames[i] if by_tessellation[i] else rnames[i] for i in range(len(survey))]
    _, unit_mm = _cad._output_factor(path, units)
    _cad._log_import(path, 'step', parts, unit_mm, units, names)
    entry = _cad._import_log[os.path.normcase(path)]
    worst_fit = _worst_fit(path, survey)
    for i, item in enumerate(survey):
        way = 'tessellate' if item['solid'] in to_tessellate else 'reconstruct'
        entry['parts'][i]['method'] = way
        entry['parts'][i]['free_form'] = _free_form_share(item)
        entry['parts'][i]['surface'] = item['surface']
        if item['solid'] in poor:
            entry['parts'][i]['poor_fit'] = poor[item['solid']]            # (why: the fit of its free-form faces)
        elif item['solid'] in worst_fit and way == 'reconstruct':
            entry['parts'][i]['fit'] = worst_fit[item['solid']]
    n_tess = sum(1 for it in survey if it['solid'] in to_tessellate)
    n_rec = len(survey) - n_tess
    note = '{} part(s) reconstructed, {} tessellated (a part is tessellated when {:g} % of its surface or more is ' \
           'free-form, or when it is a surface body'.format(n_rec, n_tess, 100.0 * threshold)
    if fit_tolerance is not None:
        n_poor = sum(1 for it in survey if it['solid'] in poor)
        note += ', or when its fitted free-form faces are off by {:g} % of their size or more{}'.format(
            fit_tolerance, ': {} part(s) were'.format(n_poor) if n_poor else '')
    note += ')'
    if tess_note:
        note += '; ' + tess_note
    entry['note'] = note
    print('{}: {}'.format(os.path.basename(path), note))
    return parts


def reconstruct(path, units='mm', cache=True, rev=None, auto_exclude=False, exclude_threshold=1.0,
                exclude_quality=64):
    ''' Imports a STEP file (.step, .stp) with every part rebuilt as CSG from its faces, using FielDes's built-in reader:
        planes, cylinders, cones, spheres and tori as exact formulas, free-form (B-spline) faces as closed-form surfaces
        fitted to them (listed after the import; where a fit is off by more than 0.5 % of its face's size the model is
        shaded, turning fully red at 10 %).  Fast, light, and the faces of the result can be dragged -- but a gear, a
        thread or a sculpted body does not survive a fit: import_model() tessellates such parts instead (and
        tessellate() does it for all).

        Returns [(shape, (lower corner, upper corner))], one per solid -- see import_model() for what the list holds,
        and for units and rev.  A solid that cannot be rebuilt (a surface body, a face of a kind not supported) is a
        FailedPart that says why the moment it is used; the other solids import normally.

        cache       the result is kept in <file>.fieldes-cache.py (and a folder of trees) next to the STEP file until the
                    file or the import algorithm changes -- a rebuilt library keeps it; False: not
        auto_exclude  True excludes every poorly fitted place by itself: where the fit is off by more than
                    `exclude_threshold` (percent of the face's size, 1.0 by default; 0.5 is the lowest) the part is its
                    exact surface, meshed from the STEP file and made a field, locked against every later operation
                    (see exclude(); `exclude_quality` is its quality).  Off by default: the exact surfaces cost
                    tessellation time '''
    path = _path_of(path, 'reconstruct')
    if _kind_of(path) != 'step':
        raise ValueError('reconstruct({!r}): only a STEP file has faces to rebuild a part from; a mesh is imported by '
                         'import_model() (or tessellate())'.format(path))
    return _cad._reconstruct_step(path, cache=cache, units=units, rev=rev, auto_exclude=auto_exclude,
                                  exclude_threshold=exclude_threshold, exclude_quality=exclude_quality)[0]


def tessellate(source, units='mm', file_units=None, rev=None, cache=True, quality=64, thickness=None, bounds=None,
               resolution=None):
    ''' The exact surface, as a distance field.  `source` is

        a file          a STEP file: every part as the triangles of its faces (free-form faces refined inside their
                        outlines until no triangle turns the surface by more than 2 pi over `quality`), made the exact signed
                        distance field -- nothing is reconstructed or fitted, so a gear, a thread, a sculpted body or a
                        thin wall is what the file says.  A mesh file: what import_model() makes of it.
                        Returns [(shape, bounds)] as import_model() does.  A surface body that does not close is made a
                        sheet `thickness` thick
        a field         any shape: its surface is meshed (`resolution` samples per unit, default about 200 along its
                        longest side; `bounds` = ((x0, y0, z0), (x1, y1, z1)) if they cannot be found) and the result is
                        the exact signed distance to that mesh.  Booleans, blends and warps have fields that are only
                        roughly a distance; after this offsets and shells are uniform.  Returns a shape.  (A feature
                        thinner than the mesh cells falls between its samples: raise `resolution`)
        a list          of (shape, bounds) -- what import_model() returns -- or of shapes: each tessellated

        What it costs: tessellating a part with many free-form faces takes seconds (done once, on all the processor's
        threads, kept in <file>.fieldes-tessellation); the distance field a fraction of a second; meshing it 0.5 to 2.8
        times as long as a reconstructed part.  What it is not: a rebuilt solid -- the planes of the part are triangles
        here, and its faces cannot be dragged.  units, rev, cache: see import_model() '''
    if isinstance(source, (str, os.PathLike)):
        path = _path_of(source, 'tessellate')
        kind = _kind_of(path)
        if kind is None:
            raise ValueError('tessellate({!r}): not a file it can read; use a STEP file or a mesh'.format(path))
        if kind == 'mesh':
            return [_import_mesh(path, units, file_units, rev)]
        if file_units is not None:
            raise ValueError('tessellate: file_units is for mesh files; a STEP file declares its units')
        parts, names, note = _tess._tessellate_step(path, units=units, quality=quality, cache=cache, rev=rev,
                                                    thickness=thickness)
        print('{}: {}'.format(os.path.basename(path), note))
        return parts
    if isinstance(source, (list, tuple)):
        if _is_part(source):                  # (one (shape, bounds))
            return (tessellate(source[0], bounds=source[1], resolution=resolution), source[1])
        return [tessellate(item, bounds=bounds, resolution=resolution) for item in source]
    shape = _s(source)
    if getattr(shape, '_tessellated_import', False):
        return shape                      # (it is the distance to its own triangles already)
    lo, hi = _shape_bounds(shape, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    res = float(resolution) if resolution else 200.0 / size
    return _tessellate_field(shape, tuple(lo), tuple(hi), res, _vars_key())


def _is_part(item):
    return (isinstance(item, (tuple, list)) and len(item) == 2 and isinstance(item[0], Shape)
            and isinstance(item[1], (tuple, list)) and len(item[1]) == 2)


@content_cached('tessellate', limit=8)
def _tessellate_field(shape, lo, hi, resolution, script_vars):
    ''' The signed distance to the mesh of a field's surface, '''
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    pad = 0.03 * size
    region = libfive_region_t(*[libfive_interval_t(a - pad, b + pad) for a, b in zip(lo, hi)])
    mesh_p = render_mesh(shape, region, resolution)
    if not mesh_p:
        raise ValueError('tessellate: the shape could not be meshed')
    try:
        mesh = mesh_p[0]
        if mesh.tri_count == 0:
            raise ValueError('tessellate: the shape has no surface in its bounds')
        verts = ctypes.cast(mesh.verts, ctypes.POINTER(ctypes.c_float))
        tris = ctypes.cast(mesh.tris, ctypes.POINTER(ctypes.c_uint32))
        info = libfive_mesh_import_info_t()
        ptr = lib.libfive_mesh_from_arrays(verts, mesh.vert_count, tris, mesh.tri_count, 1.0, ctypes.byref(info))
    finally:
        lib.libfive_mesh_delete(mesh_p)
    if not ptr:
        raise ValueError('tessellate: ' + lib.libfive_import_mesh_last_message().decode('utf-8', 'replace'))
    out = Shape(ptr)
    out._bounds = (tuple(lo), tuple(hi))
    out._tessellated_import = True
    return out


def _import_mesh(path, units, file_units, rev):
    ''' (shape, bounds) of a mesh file '''
    return _mesh._import_mesh(path, units=units, file_units=file_units, rev=rev)
