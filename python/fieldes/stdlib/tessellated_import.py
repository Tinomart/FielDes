'''
The tessellated import of a STEP file (tessellate(path), see fieldes.stdlib.importing): the exact
surface, as a distance field -- for parts that are almost all free-form (B-spline) surface.

    parts = tessellate("organic_bracket.step")
    part, bounds = parts[0]

reconstruct() rebuilds a solid as CSG: planes, cylinders,
cones, spheres and tori exactly, and a free-form face as a closed-form surface
fitted to it -- fast, but an approximation that a gear, a thread or a sculpted
body does not survive.  This import does not reconstruct or fit anything:
every solid is tessellated straight from its trimmed faces (the free-form faces
refined inside their outlines until no triangle turns the surface by more
than a turn's share, 2 pi over `quality`), the way a CAD program or nTop does
for an implicit body, and the triangles are made the exact signed distance
field of the part (see mesh_import).  What comes back are ordinary shapes, with the same parts, names,
units and bounds as reconstruct(), and offsets, shells and lattices of
them are made from the true distance.

What it costs: tessellating a part with many free-form faces takes seconds
(done once, on all the processor's threads, and kept in a folder next to the
STEP file: a 90-part assembly 22 s, a worm gear 4 s); building the distance
field a fraction of a second a part; meshing it, in FielDes or for an export,
0.5 to 2.8 times as long as the main importer's formulas (about as long for
free-form parts, 2.2 to 2.8 times for analytic ones).  What it is not: a
rebuilt solid -- the planes and cylinders of the part are triangles here, and
the faces cannot be dragged (expose) -- so reconstruct() is the choice for parts
that are mostly analytic (import_model() makes it for you, part by part).

A file of SURFACES (a surface model: shells of faces that are not closed, as a CAD program exports a sheet or a
skin) has no inside.  Each of its shells that does not close is made a sheet: the distance to its triangles less
half a thickness (`thickness`, default 0.4 % of the size of the file), so that it can be drawn, offset, lattice-filled
and selected like anything else.  A shell that closes is a solid.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import hashlib
import json
import math
import os
import time

from fieldes import run_progress
from fieldes.ffi import lib, libfive_mesh_import_info_t
from fieldes.shape import Shape
from fieldes.stdlib import cad_import as _cad
from fieldes.stdlib.content_cache import cache_for

__all__ = []

_FORMAT = 2
_SUFFIX = '.fieldes-tessellation'


# ---------------------------------------------------------------------------
# The tessellation, kept in a folder next to the STEP file
# ---------------------------------------------------------------------------

def _tessellation_version():
    try:
        return int(lib.libfive_step_tessellation_version())
    except AttributeError:
        raise RuntimeError('tessellate: this build of FielDes has no tessellated import')


def _tessellate(path, quality, only=None):
    ''' ({solids}, {instances}): the C library's tessellation of every solid of the file (only those of `only`, if
        given: the others have an error that says they were left out), copied out  '''
    run_progress.report(0.0, 'tessellating ' + os.path.basename(path))
    if only is None:
        ptr = lib.libfive_step_brep_read(path.encode('utf-8'), int(quality))
    else:
        wanted = (ctypes.c_int32 * max(1, len(only)))(*only)
        ptr = lib.libfive_step_brep_read_only(path.encode('utf-8'), int(quality), wanted, len(only))
    if not ptr:
        raise RuntimeError('tessellate({!r}): {}'.format(
            path, lib.libfive_import_step_last_message().decode('utf-8', 'replace')))
    try:
        b = ptr.contents
        solids = []
        for i in range(b.solid_count):
            s = b.solids[i]
            entry = {'faces': int(s.faces), 'bspline_faces': int(s.bspline_faces), 'surface': bool(s.surface),
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
                'metrics': [float(p.detail), float(p.area_flat), float(p.area_curved), float(p.area_bspline)]})
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
        meta['solids'].append({k: s[k] for k in ('faces', 'bspline_faces', 'surface', 'error', 'vert_count', 'tri_count')})
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
    ''' The distance field of one solid's triangles (its own coordinates): (field, error message, info, sheet).  The
        signed distance (negative inside) of a closed surface; of a surface body that does not close -- it has no
        inside -- the UNSIGNED distance to its triangles (`sheet`: the caller gives it a thickness) '''
    n_v, n_t = entry['vert_count'], entry['tri_count']
    verts = (ctypes.c_float * (3 * n_v)).from_buffer_copy(entry['verts'])
    tris = (ctypes.c_uint32 * (3 * n_t)).from_buffer_copy(entry['tris'])
    info = libfive_mesh_import_info_t()
    ptr = lib.libfive_mesh_from_arrays(verts, n_v, tris, n_t, 1.0, ctypes.byref(info))
    if not ptr:
        return None, lib.libfive_import_mesh_last_message().decode('utf-8', 'replace'), None, False
    if entry.get('surface') and not info.watertight:
        lib.libfive_tree_delete(ptr)
        everything = (ctypes.c_uint8 * n_t)()
        ctypes.memset(everything, 1, n_t)
        sheet_info = libfive_mesh_import_info_t()
        ptr = lib.libfive_mesh_patch(verts, n_v, tris, n_t, everything, ctypes.byref(sheet_info))
        if not ptr:
            return None, lib.libfive_import_mesh_last_message().decode('utf-8', 'replace'), None, False
        return Shape(ptr), None, info, True
    return Shape(ptr), None, info, False


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


def _tessellate_step(path, units='mm', quality=64, cache=True, rev=None, only=None, thickness=None, log=True):
    ''' tessellate(path) of a STEP file (fieldes.stdlib.importing): a separate Shape PER PART whose surface is the file's
        own, exactly (see the module's text): the solids are tessellated from their faces, and each tessellation is
        the exact signed distance field of its triangles.  For parts that are almost all free-form (B-spline)
        faces -- sculpted bodies, gears, threads -- which the fitted closed-form surfaces of reconstruct() do not
        follow; for the rest reconstruct() is the faster and lighter choice.

        Returns ([(Shape, (xyz_min, xyz_max))], names, note), one part per occurrence, as reconstruct() does: the same
        order, the same names (`_part_name`), the assemblies assembled, each part in `units` ('mm', 'cm',
        'm', 'in', or 'file').  A solid that could not be tessellated is a FailedPart that says why the moment
        it is used.

        quality    points per full turn of a circle (2 pi over it is the most a triangle may turn the surface
                   by): 64 is about 0.05 % of a radius off the surface; 128 halves the triangles' size ... and
                   makes four times as many of them
        cache      the tessellation is kept in the folder '<file>.fieldes-tessellation' next to the STEP file
                   (cache=False: not; a string: some other folder) until the file, the quality or the
                   tessellation changes
        rev        a number that is part of what the tessellation is kept by: another one tessellates again
                   ("Reimport" in FielDes)
        only       the solids to tessellate (the others are FailedParts that say they were left out)
        thickness  of the sheets (see the module), in `units`: default 0.4 % of the size of the file
        log        False when the caller logs the import itself (see importing.import_model) '''
    step_path = os.path.abspath(path)
    quality = int(quality)
    if quality < 8:
        raise ValueError('tessellate: quality is points per turn, at least 8')
    factor, unit_mm = _cad._output_factor(step_path, units)
    wanted = None if only is None else set(only)
    try:
        st = os.stat(step_path)
        memo_key = (os.path.normcase(step_path), st.st_size, st.st_mtime_ns, units, quality,
                    rev, _tessellation_version(), None if only is None else tuple(sorted(only)), thickness)
    except OSError:
        memo_key = None
    memo = cache_for('import_step_tessellated', 4)
    if memo_key is not None:
        hit, rec = memo.get(memo_key)
        if hit:
            parts, names, note = rec
            if log:
                _cad._log_import(step_path, 'step-tessellated', parts, unit_mm, units, names)
                _cad._import_log[os.path.normcase(step_path)]['note'] = note
            return list(parts), list(names), note

    t0 = time.time()
    key = None
    folder = None
    got = None
    if cache:
        folder = cache if isinstance(cache, str) else step_path + _SUFFIX
        try:
            # (what was tessellated is part of what the tessellation is kept by)
            key = '{}:{}:{}:{}:{}'.format(_cad._sha256_file(step_path), _tessellation_version(), quality,
                                          rev if rev is not None else 0,
                                          'all' if only is None else ','.join(str(s) for s in sorted(only)))
        except OSError:
            key = None
        if key is not None:
            got = _read_cache(folder, key)
    from_cache = got is not None
    if got is None:
        with run_progress.task('tessellating the STEP surface'):
            got = _tessellate(step_path, quality, only)
        if key is not None:
            try:
                _write_cache(folder, key, *got)
            except OSError:
                pass                                # (a cache is an optimisation only)
    solids, instances = got
    t_tess = time.time() - t0

    # The distance field of every solid that is used (once, however often the file places it)
    fields = {}
    sheets = set()
    errors = {}
    infos = {}
    used = sorted({p['solid'] for p in instances})
    if wanted is not None:
        used = [si for si in used if si in wanted]
    t1 = time.time()
    with run_progress.task('building the distance fields'):
        for n, si in enumerate(used):
            run_progress.report(n / max(1, len(used)), 'distance field of solid {} of {}'.format(n + 1, len(used)))
            s = solids[si]
            if s['error'] or not s['vert_count']:
                errors[si] = s['error'] or 'solid {}: it has no surface'.format(si)
                continue
            field, error, info, sheet = _distance_field(s)
            if field is None:
                errors[si] = 'solid {}: {}'.format(si, error)
            else:
                fields[si], infos[si] = field, info
                if sheet:
                    sheets.add(si)
    t_field = time.time() - t1

    # (a sheet is as thick as asked, or 0.4 % of the size of the file's surface bodies)
    sheet_thickness = None
    if sheets:
        if thickness is not None:
            sheet_thickness = float(thickness)
            if not sheet_thickness > 0:
                raise ValueError('tessellate: thickness is the thickness of a surface body, a positive length')
        else:
            lo = [1e300] * 3
            hi = [-1e300] * 3
            for p in instances:
                if p['solid'] in sheets:
                    for i in range(3):
                        lo[i] = min(lo[i], p['bounds'][0][i])
                        hi[i] = max(hi[i], p['bounds'][1][i])
            diagonal = math.sqrt(sum((hi[i] - lo[i]) ** 2 for i in range(3))) * factor
            sheet_thickness = float('%.2g' % (0.004 * diagonal)) or 0.01

    mesh_keys = {}                  # (what each solid's triangles are, for the viewport to keep them by)

    def mesh_key(si):
        if si not in mesh_keys:
            h = hashlib.blake2b(digest_size=16)
            h.update(solids[si]['verts'])
            h.update(solids[si]['tris'])
            mesh_keys[si] = h.hexdigest()
        return mesh_keys[si]

    parts, names = [], []
    for p in instances:
        lo = tuple(v * factor for v in p['bounds'][0])
        hi = tuple(v * factor for v in p['bounds'][1])
        si = p['solid']
        if si in errors:
            shape = _cad.FailedPart(errors[si])
        elif si not in fields:
            shape = _cad.FailedPart('solid {}: left out of the tessellation (it is imported another way)'.format(si))
        else:
            shape = _placed(fields[si], p['linear'], p['offset'], factor)
            if si in sheets:
                shape = shape - sheet_thickness / 2.0
                lo = tuple(v - sheet_thickness / 2.0 for v in lo)
                hi = tuple(v + sheet_thickness / 2.0 for v in hi)
            shape._step_ref = (si, p['instance'])
            d, a_flat, a_curved = p['metrics'][:3]
            shape._step_metrics = (d * factor, a_flat * factor * factor, a_curved * factor * factor)
            shape._distance_of = _own_distance(shape)         # (it IS the exact distance)
            shape._tessellated_import = True
            # drawn from its own triangles (the viewport: a wall of any thinness at any resolution), placed as the file
            # places it: p = factor * (linear q + offset)
            lin = p['linear']
            shape._display_mesh = (mesh_key(si), solids[si]['verts'], solids[si]['tris'],
                                   tuple(float(v) for r in range(3)
                                         for v in (lin[3 * r] * factor, lin[3 * r + 1] * factor, lin[3 * r + 2] * factor,
                                                   p['offset'][r] * factor)))
        parts.append((shape, (lo, hi)))
        names.append(p['name'])
    _cad._name_parts(parts, names)

    triangles = sum(solids[si]['tri_count'] for si in used)
    bsplines = sum(solids[si]['bspline_faces'] for si in used)
    faces = sum(solids[si]['faces'] for si in used)
    broken = [si for si in used if infos.get(si) is not None and not infos[si].watertight and si not in sheets]
    note = 'the STEP surface itself as {} triangles ({} of {} faces free-form), nothing fitted; {} {:.1f} s, ' \
           'distance fields {:.1f} s'.format(triangles, bsplines, faces,
                                             'tessellation kept' if from_cache else 'tessellated in', t_tess,
                                             t_field)
    if sheets:
        note += '; {} surface bod{} that {} not close made sheets {:g} {} thick (thickness=)'.format(
            len(sheets), 'ies' if len(sheets) != 1 else 'y', 'do' if len(sheets) != 1 else 'does', sheet_thickness,
            units if units != 'file' else 'file units')
    if broken:
        note += '; solid {} not watertight (the winding number decides inside and outside there)'.format(
            ', '.join(str(i) for i in broken))
    if log:
        _cad._log_import(step_path, 'step-tessellated', parts, unit_mm, units, names)
        _cad._import_log[os.path.normcase(step_path)]['note'] = note
    if memo_key is not None:
        memo.put(memo_key, (list(parts), list(names), note))
    return parts, names, note
