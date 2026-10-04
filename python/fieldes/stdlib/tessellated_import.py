'''
STEP import for parts that are almost all free-form (B-spline) surface: the
exact surface, as a distance field.

    parts = import_step_tessellated_parts("organic_bracket.step")
    part, bounds = parts[0]

The main importer (import_step_parts) rebuilds a solid as CSG: planes, cylinders,
cones, spheres and tori exactly, and a free-form face as a closed-form surface
fitted to it -- fast, but an approximation that a gear, a thread or a sculpted
body does not survive.  This function does not reconstruct or fit anything:
every solid is tessellated straight from its trimmed faces (the free-form faces
refined inside their outlines until no triangle turns the surface by more
than a turn's share, 2 pi over `quality`), the way a CAD program or nTop does
for an implicit body, and the triangles are made the exact signed distance
field of the part (see mesh_import).  What comes back are ordinary shapes, with the same parts, names,
units and bounds as import_step_parts(), and offsets, shells and lattices of
them are made from the true distance.

What it costs: tessellating a part with many free-form faces takes seconds
(done once, on all the processor's threads, and kept in a folder next to the
STEP file: a 90-part assembly 22 s, a worm gear 4 s); building the distance
field a fraction of a second a part; meshing it, in FielDes or for an export,
0.5 to 2.8 times as long as the main importer's formulas (about as long for
free-form parts, 2.2 to 2.8 times for analytic ones).  What it is not: a
rebuilt solid -- the planes and cylinders of the part are triangles here, and
the faces cannot be dragged (expose) -- so use import_step_parts() for parts
that are mostly analytic.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import json
import math
import os
import time

from fieldes import run_progress
from fieldes.ffi import lib, libfive_mesh_import_info_t
from fieldes.shape import Shape
from fieldes.stdlib import cad_import as _cad
from fieldes.stdlib.content_cache import cache_for

__all__ = ['import_step_tessellated_parts', 'import_step_tessellated']

_FORMAT = 1
_SUFFIX = '.fieldes-tessellation'


# ---------------------------------------------------------------------------
# The tessellation, kept in a folder next to the STEP file
# ---------------------------------------------------------------------------

def _tessellation_version():
    try:
        return int(lib.libfive_step_tessellation_version())
    except AttributeError:
        raise RuntimeError('import_step_tessellated_parts: this build of FielDes has no tessellated import')


def _tessellate(path, quality):
    ''' ({solids}, {instances}): the C library's tessellation of every solid of the file, copied out  '''
    run_progress.report(0.0, 'tessellating ' + os.path.basename(path))
    ptr = lib.libfive_step_brep_read(path.encode('utf-8'), int(quality))
    if not ptr:
        raise RuntimeError('import_step_tessellated_parts({!r}): {}'.format(
            path, lib.libfive_import_step_last_message().decode('utf-8', 'replace')))
    try:
        b = ptr.contents
        solids = []
        for i in range(b.solid_count):
            s = b.solids[i]
            entry = {'faces': int(s.faces), 'bspline_faces': int(s.bspline_faces),
                     'error': s.error.decode('utf-8', 'replace') if s.error else None,
                     'verts': b'', 'tris': b'', 'vert_count': 0, 'tri_count': 0}
            if s.mesh:
                m = s.mesh.contents
                entry['vert_count'], entry['tri_count'] = int(m.vert_count), int(m.tri_count)
                entry['verts'] = ctypes.string_at(m.verts, 12 * m.vert_count)
                entry['tris'] = ctypes.string_at(m.tris, 12 * m.tri_count)
            solids.append(entry)
        instances = []
        for i in range(b.instance_count):
            p = b.instances[i]
            bb = p.bounds
            instances.append({
                'solid': int(p.solid), 'instance': int(p.instance),
                'linear': [float(v) for v in p.linear], 'offset': [float(v) for v in p.offset],
                'name': p.name.decode('utf-8', 'replace') if p.name else '',
                'bounds': [[bb.X.lower, bb.Y.lower, bb.Z.lower], [bb.X.upper, bb.Y.upper, bb.Z.upper]],
                'metrics': [float(p.detail), float(p.area_flat), float(p.area_curved)]})
    finally:
        lib.libfive_step_brep_delete(ptr)
    return solids, instances


def _write_cache(folder, key, solids, instances):
    ''' (the meshes first, the file that describes them last: a half-written cache is never used) '''
    os.makedirs(folder, exist_ok=True)
    for name in os.listdir(folder):
        if name.endswith('.mesh') or name == 'meta.json':
            os.remove(os.path.join(folder, name))
    meta = {'format': _FORMAT, 'key': key, 'instances': instances, 'solids': []}
    for i, s in enumerate(solids):
        meta['solids'].append({k: s[k] for k in ('faces', 'bspline_faces', 'error', 'vert_count', 'tri_count')})
        if s['vert_count']:
            with open(os.path.join(folder, 's{}.mesh'.format(i)), 'wb') as f:
                f.write(s['verts'])
                f.write(s['tris'])
    tmp = os.path.join(folder, 'meta.json.tmp')
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(meta, f)
    os.replace(tmp, os.path.join(folder, 'meta.json'))


def _read_cache(folder, key):
    try:
        with open(os.path.join(folder, 'meta.json'), 'r', encoding='utf-8') as f:
            meta = json.load(f)
        if meta.get('format') != _FORMAT or meta.get('key') != key:
            return None
        solids = []
        for i, s in enumerate(meta['solids']):
            entry = dict(s)
            entry['verts'], entry['tris'] = b'', b''
            if s['vert_count']:
                with open(os.path.join(folder, 's{}.mesh'.format(i)), 'rb') as f:
                    data = f.read()
                nv, nt = 12 * s['vert_count'], 12 * s['tri_count']
                if len(data) != nv + nt:
                    return None
                entry['verts'], entry['tris'] = data[:nv], data[nv:]
            solids.append(entry)
        return solids, meta['instances']
    except (OSError, ValueError, KeyError, TypeError):
        return None


# ---------------------------------------------------------------------------
# The parts
# ---------------------------------------------------------------------------

def _distance_field(entry):
    ''' The signed distance field of one solid's triangles (its own coordinates), or an error message '''
    n_v, n_t = entry['vert_count'], entry['tri_count']
    verts = (ctypes.c_float * (3 * n_v)).from_buffer_copy(entry['verts'])
    tris = (ctypes.c_uint32 * (3 * n_t)).from_buffer_copy(entry['tris'])
    info = libfive_mesh_import_info_t()
    ptr = lib.libfive_mesh_from_arrays(verts, n_v, tris, n_t, 1.0, ctypes.byref(info))
    if not ptr:
        return None, lib.libfive_import_mesh_last_message().decode('utf-8', 'replace'), None
    return Shape(ptr), None, info


def _inverse(m):
    (a, b, c), (d, e, f), (g, h, i) = m
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if not det:
        raise ValueError('a placement that cannot be turned round')
    return det, [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
                 [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
                 [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]


def _placed(field, linear, offset, factor):
    ''' `field` (of a solid, in its own coordinates) where the file's assembly puts it, in the units of the
        script: p = factor * (linear q + offset)   (the field stays a distance: its value times the scale) '''
    m = [[linear[3 * r + c] * factor for c in range(3)] for r in range(3)]
    o = [offset[r] * factor for r in range(3)]
    identity = all(abs(m[r][c] - (1.0 if r == c else 0.0)) < 1e-12 for r in range(3) for c in range(3)) \
        and all(abs(v) < 1e-12 for v in o)
    if identity:
        return field
    det, inv = _inverse(m)
    x, y, z = Shape.X(), Shape.Y(), Shape.Z()
    px, py, pz = x - o[0], y - o[1], z - o[2]
    q = [inv[r][0] * px + inv[r][1] * py + inv[r][2] * pz for r in range(3)]
    return field.remap(q[0], q[1], q[2]) * math.pow(abs(det), 1.0 / 3.0)


def _own_distance(shape):
    return lambda: shape


def import_step_tessellated_parts(path, units='mm', quality=64, cache=True, rev=None):
    ''' Imports a STEP (.step/.stp) file as a separate Shape PER PART whose surface is the file's own, exactly
        (see the module's text): the solids are tessellated from their faces, and each tessellation is the
        exact signed distance field of its triangles.  For parts that are almost all free-form (B-spline)
        faces -- sculpted bodies, gears, threads -- which the fitted closed-form surfaces of import_step_parts()
        do not follow; for the rest import_step_parts() is the faster and lighter choice.

        Returns a list of (Shape, (xyz_min, xyz_max)), one per part, as import_step_parts() does: the same
        order, the same names (`_part_name`), the assemblies assembled, each part in `units` ('mm', 'cm',
        'm', 'in', or 'file').  A solid that could not be tessellated is a FailedPart that says why the moment
        it is used.

        quality   points per full turn of a circle (2 pi over it is the most a triangle may turn the surface
                  by): 64 is about 0.05 % of a radius off the surface; 128 halves the triangles' size ... and
                  makes four times as many of them
        cache     the tessellation is kept in the folder '<file>.fieldes-tessellation' next to the STEP file
                  (cache=False: not; a string: some other folder) until the file, the quality or the
                  tessellation changes
        rev       a number that is part of what the tessellation is kept by: another one tessellates again
                  ("Reimport" in FielDes) '''
    step_path = os.path.abspath(path)
    quality = int(quality)
    if quality < 8:
        raise ValueError('import_step_tessellated_parts: quality is points per turn, at least 8')
    factor, unit_mm = _cad._output_factor(step_path, units)
    try:
        st = os.stat(step_path)
        memo_key = (os.path.normcase(step_path), st.st_size, st.st_mtime_ns, units, quality,
                    rev, _tessellation_version())
    except OSError:
        memo_key = None
    memo = cache_for('import_step_tessellated', 4)
    if memo_key is not None:
        hit, rec = memo.get(memo_key)
        if hit:
            parts, names, note = rec
            _cad._log_import(step_path, 'step-tessellated', parts, unit_mm, units, names)
            _cad._import_log[os.path.normcase(step_path)]['note'] = note
            return list(parts)

    t0 = time.time()
    key = None
    folder = None
    got = None
    if cache:
        folder = cache if isinstance(cache, str) else step_path + _SUFFIX
        try:
            key = '{}:{}:{}:{}'.format(_cad._sha256_file(step_path), _tessellation_version(), quality,
                                       rev if rev is not None else 0)
        except OSError:
            key = None
        if key is not None:
            got = _read_cache(folder, key)
    from_cache = got is not None
    if got is None:
        with run_progress.task('tessellating the STEP surface'):
            got = _tessellate(step_path, quality)
        if key is not None:
            try:
                _write_cache(folder, key, *got)
            except OSError:
                pass                                # (a cache is an optimisation only)
    solids, instances = got
    t_tess = time.time() - t0

    # The distance field of every solid that is used (once, however often the file places it)
    fields = {}
    errors = {}
    infos = {}
    used = sorted({p['solid'] for p in instances})
    t1 = time.time()
    with run_progress.task('building the distance fields'):
        for n, si in enumerate(used):
            run_progress.report(n / max(1, len(used)), 'distance field of solid {} of {}'.format(n + 1, len(used)))
            s = solids[si]
            if s['error'] or not s['vert_count']:
                errors[si] = s['error'] or 'solid {}: it has no surface'.format(si)
                continue
            field, error, info = _distance_field(s)
            if field is None:
                errors[si] = 'solid {}: {}'.format(si, error)
            else:
                fields[si], infos[si] = field, info
    t_field = time.time() - t1

    parts, names = [], []
    for p in instances:
        lo = tuple(v * factor for v in p['bounds'][0])
        hi = tuple(v * factor for v in p['bounds'][1])
        si = p['solid']
        if si in errors:
            shape = _cad.FailedPart(errors[si])
        else:
            shape = _placed(fields[si], p['linear'], p['offset'], factor)
            shape._step_ref = (si, p['instance'])
            d, a_flat, a_curved = p['metrics']
            shape._step_metrics = (d * factor, a_flat * factor * factor, a_curved * factor * factor)
            shape._distance_of = _own_distance(shape)         # (it IS the exact distance)
            shape._tessellated_import = True
        parts.append((shape, (lo, hi)))
        names.append(p['name'])
    _cad._name_parts(parts, names)
    _cad._log_import(step_path, 'step-tessellated', parts, unit_mm, units, names)

    triangles = sum(solids[si]['tri_count'] for si in used)
    bsplines = sum(solids[si]['bspline_faces'] for si in used)
    faces = sum(solids[si]['faces'] for si in used)
    broken = [si for si in used if infos.get(si) is not None and not infos[si].watertight]
    note = 'the STEP surface itself as {} triangles ({} of {} faces free-form), nothing fitted; {} {:.1f} s, ' \
           'distance fields {:.1f} s'.format(triangles, bsplines, faces,
                                             'tessellation kept' if from_cache else 'tessellated in', t_tess,
                                             t_field)
    if broken:
        note += '; solid {} not watertight (the winding number decides inside and outside there)'.format(
            ', '.join(str(i) for i in broken))
    _cad._import_log[os.path.normcase(step_path)]['note'] = note
    print('{}: {}'.format(os.path.basename(step_path), note))
    if memo_key is not None:
        memo.put(memo_key, (list(parts), list(names), note))
    return parts


def import_step_tessellated(path, units='mm', quality=64, cache=True, rev=None):
    ''' import_step_tessellated_parts() as ONE Shape (the union of its parts), as import_step() is of
        import_step_parts().  Raises RuntimeError if any solid could not be tessellated. '''
    parts = import_step_tessellated_parts(path, units=units, quality=quality, cache=cache, rev=rev)
    whole = parts[0][0]
    for shape, _ in parts[1:]:
        whole = whole.min(shape)
    lo, hi = _cad.roi(parts, pad=0.0)
    try:
        whole._bounds = (lo, hi)
    except AttributeError:
        pass
    return whole
