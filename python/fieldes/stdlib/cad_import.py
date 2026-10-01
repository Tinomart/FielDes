'''
Python library of FielDes, built on the libfive CAD kernel

Hand-written (not code-generated): imports CAD files using FielDes's own
small, dependency-free STEP reader (see kernel/src/step/).

The main entry points are import_step_parts() and import_step(). Every
solid is rebuilt as CSG by the reconstruction algorithm (see
step_reconstruct.hpp): analytic faces (plane / cylinder / cone / sphere /
torus) as exact expressions.  A solid that can't be rebuilt doesn't stop the
rest of the file: its slot is a FailedPart (a normal tuple entry with real
bounds) that raises a specific RuntimeError the moment it's actually used.
The reconstruction takes a few seconds on a large file, so the result is
cached in a plain Python file next to the STEP file and only regenerated
when the STEP file changes or a new version of the import algorithm gives
different results (rebuilding the library does not).
B-spline faces are imported as fitted closed-form surfaces (planes,
quadrics, extruded / revolved / helical curves): an approximation, reported
after the import and marked in red where it is poor.
'''

import ctypes
import hashlib
import os
import re
import time

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes import run_progress

__all__ = [
    'exclude',
    'exact_region_mesh',
    'poor_fit_region',
    'import_step',
    'import_step_parts',
    'import_step_parts_reconstructed',
    'FailedPart',
    'roi',
    'roi_resolution',
    'step_length_unit_mm',
]

# ---------------------------------------------------------------------------
# Low-level: call the C entry points
# ---------------------------------------------------------------------------

class FailedPart(object):
    ''' Stands in for a solid that could not be reconstructed as native CSG
        (no Oracle fallback exists any more -- see cad_import.py's module
        docstring). Its bounds are still real, so the tuple can still be
        unpacked and inspected, but ANY attempt to actually use it as a
        Shape (meshing it, combining it with other shapes, even just
        printing its value) raises immediately with the specific reason,
        instead of silently substituting Oracle or approximate geometry. '''
    __slots__ = ('error',)

    def __init__(self, error):
        self.error = error

    def _raise(self):
        raise RuntimeError('this part could not be imported: {}'.format(self.error))

    def __getattr__(self, name):
        self._raise()

    def __repr__(self):
        self._raise()

    def __bool__(self):
        self._raise()
    __nonzero__ = __bool__


def _collect_parts(parts_ptr):
    ''' [(shape, bounds)] and the parts' names from a C parts list  '''
    out = []
    names = []
    try:
        parts = parts_ptr.contents
        for i in range(parts.count):
            p = parts.parts[i]
            b = p.bounds
            xyz_min = (b.X.lower, b.Y.lower, b.Z.lower)
            xyz_max = (b.X.upper, b.Y.upper, b.Z.upper)
            if p.error:
                # p.tree is a meaningless Tree(1e9) placeholder for a failed
                # part (see StepPart::error's comment) -- never wrapped in a
                # Shape, so free it directly instead of leaking it.
                lib.libfive_tree_delete(p.tree)
                if p.marker:
                    lib.libfive_tree_delete(p.marker)
                shape = FailedPart(p.error.decode('utf-8', 'replace'))
            else:
                shape = Shape(p.tree)
                if p.marker:
                    shape._fit_marker = Shape(p.marker)
                shape._step_ref = (int(p.solid), int(p.instance))
                shape._step_metrics = (p.detail, p.area_flat, p.area_curved)
            out.append((shape, (xyz_min, xyz_max)))
            names.append(p.name.decode('utf-8', 'replace') if p.name else '')
    finally:
        lib.libfive_step_parts_delete(parts_ptr)
    return out, names


# The B-spline faces each import approximated by fitted closed-form surfaces:
# path -> [(part, faces, worst deviation in mm, worst face, worst deviation
# relative to the face's size), ...]
_fit_reports = {}


def _read_fit_report():
    try:
        f = lib.libfive_import_step_fit_report
    except AttributeError:
        return []
    f.argtypes = []
    f.restype = ctypes.c_char_p
    out = []
    for line in (f() or b'').decode().splitlines():
        p = line.split()
        if len(p) == 5:
            out.append((int(p[0]), int(p[1]), float(p[2]), int(p[3]), float(p[4])))
    return out


# How approximated faces are shaded: from the part's grey at this deviation
# (relative to the face's size) ...
_FIT_SHADE_FROM = 0.005
# ... to fully red at this one
_FIT_SHADE_TO = 0.10


def _print_fit_report(path, report, factor, units):
    """ Tells what was approximated: B-spline faces are imported as fitted
        simple surfaces, not the exact spline """
    if not report:
        return
    unit = 'mm' if units in (None, 'mm') else units
    faces = sum(r[1] for r in report)
    print('{}: {} free-form face{} fitted by closed-form surfaces'.format(
        os.path.basename(path), faces, '' if faces == 1 else 's'))
    for part, n, worst, face, rel in sorted(report, key=lambda r: -r[4])[:6]:      # (the worst few)
        print('  part {}: {} face{}, worst {:.3g} {} ({:.2g} % of the face)'.format(
            part, n, '' if n == 1 else 's', worst * factor, unit, 100 * rel))
    if len(report) > 6:
        print('  ... and {} more parts'.format(len(report) - 6))
    if any(r[4] >= _FIT_SHADE_FROM for r in report):
        print('  Shaded {:g} % (grey) to {:g} % (red). exclude() or auto_exclude=True draws the exact surface.'
              .format(100 * _FIT_SHADE_FROM, 100 * _FIT_SHADE_TO))


def _mark_poor_fits(parts, report):
    """ Parts with approximated B-spline faces are shaded where the fit is
        off: grey up to 0.5 % of the face's size, turning red, fully red
        from 10 % (the marker only has faces from 0.5 %) """
    if not report:
        return parts
    from fieldes.stdlib.fea import colored
    out = []
    for i, (shape, bounds) in enumerate(parts):
        marker = getattr(shape, '_fit_marker', None)
        if marker is not None and not isinstance(shape, FailedPart):
            label = 'Fit deviation, % of face'
            name = getattr(shape, '_part_name', None)
            ref = getattr(shape, '_step_ref', None)
            metrics = getattr(shape, '_step_metrics', None)
            shape = colored(shape, marker * 100, range=(100 * _FIT_SHADE_FROM, 100 * _FIT_SHADE_TO),
                            label=label, colormap='fit')
            shape._fit_marker = marker
            if ref is not None:
                shape._step_ref = ref
            if metrics is not None:
                shape._step_metrics = metrics
            if name:
                shape._part_name = name
        out.append((shape, bounds))
    return out


def _reconstructed_parts(path):
    parts_ptr = lib.libfive_import_step_parts_reconstructed(path.encode())
    message = lib.libfive_import_step_last_message().decode()
    if not parts_ptr:
        raise RuntimeError('import_step_parts({!r}): {}'.format(path, message))
    _fit_reports[os.path.normcase(os.path.abspath(path))] = _read_fit_report()
    return _collect_parts(parts_ptr)


# ---------------------------------------------------------------------------
# Units, bounds and the import log
# ---------------------------------------------------------------------------

_SI_PREFIX_MM = {'$': 1000.0, '.MILLI.': 1.0, '.CENTI.': 10.0, '.DECI.': 100.0,
                 '.DECA.': 1.0e4, '.HECTO.': 1.0e5, '.KILO.': 1.0e6,
                 '.MICRO.': 1.0e-3, '.NANO.': 1.0e-6}
_NAMED_UNIT_MM = {'INCH': 25.4, 'FOOT': 304.8, 'YARD': 914.4, 'MILE': 1609344.0,
                  'METRE': 1000.0, 'METER': 1000.0, 'MILLIMETRE': 1.0,
                  'MILLIMETER': 1.0, 'CENTIMETRE': 10.0, 'CENTIMETER': 10.0}
_UNITS_MM = {'mm': 1.0, 'cm': 10.0, 'dm': 100.0, 'm': 1000.0, 'in': 25.4,
             'inch': 25.4, 'ft': 304.8}


def step_length_unit_mm(path):
    ''' Millimetres per length unit of a STEP file, read from the unit its
        (first) representation context declares (SI_UNIT(.MILLI.,.METRE.)
        -> 1, SI_UNIT($,.METRE.) -> 1000, CONVERSION_BASED_UNIT('INCH', ...)
        -> 25.4, ...).  Files that declare nothing are taken to be in mm.
        (The importers themselves read every part's own unit: a file can
        mix them.) '''
    try:
        with open(path, 'r', encoding='latin-1') as f:
            text = f.read()
    except OSError:
        return 1.0

    def body(eid):
        m = re.search(r'(?<![\w#])#' + eid + r'\s*=(.*?);', text, re.S)
        return m.group(1) if m else ''

    def unit_mm(eid, depth=0):
        b = body(eid)
        if depth > 6 or 'UNIT' not in b:
            return None
        m = re.search(r'SI_UNIT\s*\(\s*(\$|\.\w+\.)\s*,\s*\.METRE\.\s*\)', b)
        if m:
            return _SI_PREFIX_MM.get(m.group(1), 1.0)
        m = re.search(r"CONVERSION_BASED_UNIT\s*\(\s*'([^']*)'\s*,\s*#(\d+)", b)
        if m:
            mb = body(m.group(2))
            mm = re.search(r'LENGTH_MEASURE\s*\(\s*([-+0-9.eE]+)\s*\)\s*,\s*#(\d+)', mb)
            if mm:
                inner = unit_mm(mm.group(2), depth + 1)
                if inner:
                    return float(mm.group(1)) * inner
            return _NAMED_UNIT_MM.get(m.group(1).upper())
        return None

    for ctx in re.finditer(r'GLOBAL_UNIT_ASSIGNED_CONTEXT\s*\(\s*\(([^)]*)\)', text):
        for eid in re.findall(r'#(\d+)', ctx.group(1)):
            if 'LENGTH_UNIT' in body(eid):
                u = unit_mm(eid)
                if u:
                    return u
    return 1.0


def _target_mm(units):
    if units in (None, 'file'):
        return None
    try:
        return _UNITS_MM[str(units).lower()]
    except KeyError:
        raise ValueError('unknown units {!r}; use one of {} or "file"'.format(
            units, ', '.join(sorted(_UNITS_MM))))


def _output_factor(path, units):
    ''' The importers deliver millimetres (every solid converted from its
        own unit and placed in the assembly); this rescales to `units`,
        or to the file's own (first declared) unit for units='file'.
        Returns (factor, file unit in mm). '''
    target = _target_mm(units)
    unit_mm = step_length_unit_mm(path)
    return 1.0 / (target if target is not None else unit_mm), unit_mm


def _name_parts(parts, names):
    for (shape, _), name in zip(parts, names):
        if name and not isinstance(shape, FailedPart):
            try:
                shape._part_name = name
            except AttributeError:
                pass


def _scale_parts(parts, factor):
    ''' Scales every (shape, bounds) part uniformly about the origin:
        f'(p) = factor * f(p / factor), bounds * factor. '''
    if factor == 1.0:
        return parts
    x, y, z = Shape.X(), Shape.Y(), Shape.Z()
    out = []
    for shape, (lo, hi) in parts:
        lo = tuple(v * factor for v in lo)
        hi = tuple(v * factor for v in hi)
        if not isinstance(shape, FailedPart):
            marker = getattr(shape, '_fit_marker', None)
            ref = getattr(shape, '_step_ref', None)
            metrics = getattr(shape, '_step_metrics', None)
            shape = shape.remap(x / factor, y / factor, z / factor) * factor
            if marker is not None:
                shape._fit_marker = marker.remap(x / factor, y / factor, z / factor)
            if ref is not None:
                shape._step_ref = ref
            if metrics is not None:
                shape._step_metrics = (metrics[0] * factor, metrics[1] * factor * factor,
                                       metrics[2] * factor * factor)
        out.append((shape, (lo, hi)))
    return out


# Every import made in this process: absolute path -> summary.  FielDes's
# scene panel reads this to list an import's parts (with their bounds and
# failure reasons) without importing the file a second time.
_import_log = {}


def _log_import(path, kind, parts, unit_mm, units, names=None):
    entries = []
    for i, (shape, (lo, hi)) in enumerate(parts):
        failed = isinstance(shape, FailedPart)
        entries.append({'index': i, 'ok': not failed,
                        'error': shape.error if failed else None,
                        'name': (names[i] if names and i < len(names) else '') or '',
                        'bounds': [list(lo), list(hi)]})
        if not failed:
            try:
                shape._bounds = (tuple(lo), tuple(hi))
            except AttributeError:
                pass
    _import_log[os.path.normcase(os.path.abspath(path))] = {
        'kind': kind, 'path': os.path.abspath(path), 'unit_mm': unit_mm,
        'units': units, 'parts': entries}


def _bounds_of(item):
    ''' Bounds boxes found in an import result / part / bounds / Shape  '''
    def is_vec(v):
        return isinstance(v, (tuple, list)) and len(v) == 3 and \
            all(isinstance(c, (int, float)) for c in v)
    if isinstance(item, (tuple, list)):
        if len(item) == 2 and is_vec(item[0]) and is_vec(item[1]):
            return [item]                                   # a bounds pair
        if len(item) == 2 and isinstance(item[1], (tuple, list)) and \
                len(item[1]) == 2 and is_vec(item[1][0]):
            return [item[1]]                                # (shape, bounds)
        out = []
        for sub in item:                                    # a parts list
            out.extend(_bounds_of(sub))
        return out
    b = getattr(item, '_bounds', None) if isinstance(item, Shape) else None
    return [b] if b else []


def roi(*items, pad=0.1):
    ''' Region of interest covering the given imported models, for
        view.set_bounds(*roi(...)).  Accepts whatever the importers
        return: a parts list, one (shape, bounds) part, a bounds pair, or
        an imported Shape.  Each side is padded by `pad` times the box size.
        Returns (xyz_min, xyz_max). '''
    boxes = []
    for it in items:
        boxes.extend(_bounds_of(it))
    if not boxes:
        raise ValueError('roi(): no bounds found in the given items')
    lo = [min(b[0][i] for b in boxes) for i in range(3)]
    hi = [max(b[1][i] for b in boxes) for i in range(3)]
    size = [hi[i] - lo[i] for i in range(3)]
    ref = max(size) or 1.0
    p = [pad * max(size[i], 0.05 * ref) for i in range(3)]
    return (tuple(lo[i] - p[i] for i in range(3)),
            tuple(hi[i] + p[i] for i in range(3)))


def _parts_with_metrics(item):
    ''' [(shape, (smallest feature, flat area, curved area))] for each imported
        part found in an import result / part / Shape that carries them '''
    if isinstance(item, (tuple, list)):
        out = []
        for sub in item:
            out.extend(_parts_with_metrics(sub))
        return out
    m = getattr(item, '_step_metrics', None) if isinstance(item, Shape) else None
    return [(item, m)] if m else []


def _metrics_of(item):
    ''' The (smallest feature, flat area, curved area) of each imported part
        found in an import result / part / Shape that carries them '''
    return [m for _, m in _parts_with_metrics(item)]


# Mesh vertices per (surface area / cell^2), measured on dual contouring of
# an imported 95-part assembly (Cribadora.STEP, sheets and perforated plates
# to a machine's frame) at three cell sizes: 0.17 - 0.4 for its heaviest
# parts, 0.24 over the whole.  One factor for flat and curved faces alike,
# slightly pessimistic.
_VERTICES_PER_CELL_AREA = 0.3


# Every imported part is meshed by FielDes on its own: over a cube around it, at a
# resolution set by its own features and size (roi_resolution below) -- not at
# one resolution for the whole scene.  A thin sheet no longer needs the scene at
# a fine voxel, and a big simple part no longer gets one it does not need.

# The most vertices one part may cost: about 9 s of meshing, at 6 s per million
_PART_VERTICES = 1.5e6
# Mesh vertices per (surface area / cell^2): a wall thinner than a few cells
# cannot merge its cells across the thickness, so nearly every cell on the
# surface stays a leaf (measured on Cribadora: sheets 1.0 to 1.3, hollow tubes
# and perforated plates 1.1 to 2.3; over all 111 parts, meshed at their own
# resolutions, an average of 1.1 to 1.2 against thick bodies' 0.3, above).  In
# between the factor falls smoothly, from 1.3 for a wall of one cell or less to
# 0.3 for one of `_THIN_CELLS` cells or more.
_VERTICES_PER_THIN_CELL_AREA = 1.3
_THIN_CELLS = 6.0


def _cost(area, detail, cell):
    ''' The mesh vertices a part with this surface area and thinnest feature
        costs at this voxel size '''
    t = min(1.0, max(0.0, (_THIN_CELLS - detail / cell) / (_THIN_CELLS - 1.0)))
    f = _VERTICES_PER_CELL_AREA + (_VERTICES_PER_THIN_CELL_AREA - _VERTICES_PER_CELL_AREA) * t
    return f * area / (cell * cell)


def _cost_at(area, detail, side, depth):
    ''' The same for the voxel `side / 2**depth`, and never less than at a
        coarser depth: a finer voxel always costs more '''
    return max(_cost(area, detail, side / 2.0 ** k) for k in range(depth + 1))


def _import_parts(items):
    ''' [(shape, (smallest feature, flat area, curved area), box lo, box hi)]
        for each imported part found in an import result / part / Shape '''
    out = []

    def visit(it):
        if isinstance(it, (tuple, list)):
            if len(it) == 2 and isinstance(it[0], Shape) and isinstance(it[1], (tuple, list)) \
                    and len(it[1]) == 2:
                m = getattr(it[0], '_step_metrics', None)
                if m:
                    out.append((it[0], m, tuple(it[1][0]), tuple(it[1][1])))
                return
            for sub in it:
                visit(sub)
        elif isinstance(it, Shape):
            m = getattr(it, '_step_metrics', None)
            b = getattr(it, '_bounds', None)
            if m and b:
                out.append((it, m, tuple(b[0]), tuple(b[1])))

    for it in items:
        visit(it)
    return out


def roi_resolution(*items, cells=150, part_cells=64, detail_cells=4.0,
                   max_vertices=25.0e6, scene_vertices=2.0e6):
    ''' A render resolution (voxels per unit) for the given models,
        view.set_resolution(roi_resolution(...)) -- and, for each imported
        part among them, its OWN resolution.

        Each imported part knows its smallest feature (a sheet's thickness, a
        hollow tube's wall, a pin's or a fillet's radius) and its size.  It is
        meshed over a cube around itself, with a voxel of `detail_cells` cells
        across that feature, but no coarser than `part_cells` across its own
        size, and never to more than about a million and a half vertices by
        itself.  So a very thin part, or one with tiny features, renders at its
        own, higher resolution; nothing else has to follow it.  Together the
        parts stay under about `max_vertices` vertices (about 6 s of meshing
        per million): if they would not, the costliest parts are coarsened
        first -- down to 1.5 cells across their smallest feature, then 1, and
        only then below.  A part left thinner than about a cell is named.

        The number returned is the scene's resolution, for the shapes you make
        from them (a lattice, a cut): the voxel the typical part asks for,
        limited to about `scene_vertices` vertices and never coarser than
        `cells` voxels across the models.  Changing it scales every part's
        resolution with it.  A part still too fine to render properly is named.

        The mesher splits a region in halves until a cell is no larger than
        1 / resolution, so a mesh only changes size in steps of four to six
        times the vertices: each resolution here is the one that gives a whole
        number of halvings. '''
    import math
    lo, hi = roi(*items, pad=0.0)
    span = max(hi[i] - lo[i] for i in range(3)) or 1.0
    r_old = cells / span

    def two_digits(r):
        digits = 1 - int(math.floor(math.log10(abs(r))))
        return math.floor(r * 10.0 ** digits) / 10.0 ** digits

    parts = _import_parts(items)
    if not parts:
        return round(r_old, 1 - int(math.floor(math.log10(abs(r_old)))))
    plo, phi = roi(*items)          # the region FielDes meshes: set_bounds(*roi(...)), padded
    size = max(phi[i] - plo[i] for i in range(3)) or 1.0

    # 1. every part: as fine as its features and size ask, within its own limit
    own = []                        # [shape, detail, area, side, depth, lo, hi, voxel it asks for]
    for shape, m, blo, bhi in parts:
        area = m[1] + m[2]
        psize = max(bhi[i] - blo[i] for i in range(3)) or 1.0
        detail = m[0] if m[0] > 0 else psize
        side = psize + 4.0 * detail
        want = min(detail / detail_cells, psize / part_cells)
        d = max(0, int(math.ceil(math.log2(side / want))))
        while d > 0 and _cost_at(area, detail, side, d) > _PART_VERTICES:
            d -= 1
        own.append([shape, detail, area, side, d, blo, bhi, want])

    # 2. all of them: within the scene's vertex budget, the costliest coarsened first
    def cost(p):
        return _cost_at(p[2], p[1], p[3], p[4])

    total = sum(cost(p) for p in own)
    for floor in (1.5, 1.0, 0.0):                   # (cells across the smallest feature)
        while total > max_vertices:
            # (a part that costs next to nothing is left as fine as it is: coarsening
            # it would save nothing and only make it rough)
            cand = [p for p in own if p[4] > 0 and cost(p) > 0.002 * max_vertices
                    and p[1] / (p[3] / 2.0 ** (p[4] - 1)) >= floor]
            if not cand:
                break
            # the costliest part first: the most vertices saved for the fewest
            # parts given up (coarsening the parts with cells to spare first
            # instead spreads the loss over many more)
            p = max(cand, key=cost)
            total -= cost(p)
            p[4] -= 1
            total += cost(p)

    # 3. the scene's resolution: what the typical part asks (weighted by surface
    #    area), within a vertex budget, and never coarser than `cells` across
    area_all = sum(p[2] for p in own)
    budget_cell = math.sqrt(_VERTICES_PER_CELL_AREA * area_all / scene_vertices) if area_all > 0 else 0.0
    half = 0.5 * area_all
    typical, acc = max(p[7] for p in own), 0.0
    for want, area in sorted((p[7], p[2]) for p in own):
        acc += area
        if acc >= half:
            typical = want
            break
    cell = min(max(budget_cell, typical), size / cells)
    d_scene = max(0, int(math.ceil(math.log2(size / cell))))
    d_old = max(0, int(math.ceil(math.log2(max(size * r_old, 1.0)))))
    r_scene = round(r_old, 1 - int(math.floor(math.log10(abs(r_old))))) if d_scene == d_old \
        else two_digits(0.999 * 2.0 ** d_scene / size)

    # 4. each part's own resolution goes with it (FielDes reads it)
    def label(shape):
        name = (getattr(shape, '_part_name', '') or '').strip()
        if name:
            return name
        ref = getattr(shape, '_step_ref', None)
        return 'solid {}'.format(ref[0]) if ref else 'a part'

    thin = []
    for shape, detail, area, side, d, blo, bhi, want in own:
        shape._render_hint = (blo, bhi, 0.999 * 2.0 ** d / side, side, r_scene)
        if detail < 1.2 * side / 2.0 ** d:
            thin.append((detail, label(shape)))
    thin.sort()
    if thin:
        names = ', '.join('{} ({:.2g})'.format(n, t) for t, n in thin[:5])
        print('roi_resolution: {} of {} parts are too thin to render fully: {}{}. '
              'Raise max_vertices= to show more of them.'.format(
                  len(thin), len(own), names, ', ...' if len(thin) > 5 else ''))
    return r_scene


# ---------------------------------------------------------------------------
# Cache: a plain Python file next to the STEP file
# ---------------------------------------------------------------------------

_CACHE_FORMAT = 8  # bumped 2026-09-30: keyed to the import version, not the library file's hash
_CACHE_SUFFIX = '.fieldes-cache.py'
_END_MARKER = '# end of FielDes step cache\n'
_lib_key_memo = []


def _sha256_file(filename):
    h = hashlib.sha256()
    with open(filename, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def _import_version():
    ''' What a cache is keyed to besides the STEP file: the version of the
        import algorithm (kImportVersion in step_reconstruct.hpp), which is
        raised only when a change alters what an import produces.  So a
        rebuilt library keeps every cache; an older library without the
        function falls back to its own file's hash. '''
    try:
        return 'v{}'.format(lib.libfive_step_import_version())
    except AttributeError:
        return _library_hash()


def _library_hash():
    ''' A hash of the FielDes shared library file itself. '''
    if not _lib_key_memo:
        key = 'unknown'
        # lib._name is often just "fieldes.dll" (not a usable path), which
        # used to make the key 'unknown' and the cache immune to rebuilds.
        # Ask the OS which file the module handle really is.
        candidates = []
        try:
            import ctypes
            if hasattr(ctypes, 'windll'):
                buf = ctypes.create_unicode_buffer(4096)
                k32 = ctypes.windll.kernel32
                k32.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32]
                if k32.GetModuleFileNameW(ctypes.c_void_p(lib._handle), buf, 4096):
                    candidates.append(buf.value)
        except Exception:
            pass
        candidates.append(getattr(lib, '_name', None))
        for c in candidates:
            try:
                key = _sha256_file(c)
                break
            except (OSError, TypeError):
                continue
        _lib_key_memo.append(key)
    return _lib_key_memo[0]


class _Node:
    __slots__ = ('op', 'args', 'value')

    def __init__(self, op, args=None, value=None):
        self.op = op
        self.args = args
        self.value = value


def _parse_sexpr(text):
    ''' Parses FielDes's own str(Shape) serialization, e.g.
        "(max (- x 1.5) (min y z))", into nested _Node objects. Iterative,
        so arbitrarily deep trees are fine. Raises ValueError for anything
        that is not a plain arithmetic tree (e.g. an Oracle node). '''
    import re
    toks = re.findall(r"\(|\)|[^\s()]+", text)
    stack = []
    result = None
    i = 0
    while i < len(toks):
        t = toks[i]
        if t == '(':
            op = toks[i + 1]
            stack.append(_Node(op, []))
            i += 2
            continue
        if t == ')':
            node = stack.pop()
            if stack:
                stack[-1].args.append(node)
            else:
                result = node
            i += 1
            continue
        if t in ('x', 'y', 'z'):
            leaf = _Node(t)
        else:
            try:
                leaf = _Node('const', value=float(t))
            except ValueError:
                raise ValueError('not a plain arithmetic tree: {!r}'.format(t))
        if stack:
            stack[-1].args.append(leaf)
        else:
            result = leaf
        i += 1
    if result is None or stack:
        raise ValueError('malformed tree')
    return result


_BINARY = {'+': '{0} + {1}', '-': '{0} - {1}', '*': '{0} * {1}', '/': '{0} / {1}',
           'min': '{0}.min({1})', 'max': '{0}.max({1})'}
_UNARY = {'-': '-{0}', 'sqrt': '{0}.sqrt()', 'square': '{0}.square()'}


def _emit_shape(shape, prefix):
    ''' Returns (lines, result_name): flat, deduplicated single-assignment
        Python statements that rebuild `shape` (no nesting limit, shared
        sub-expressions written once). Raises ValueError if the shape is not
        a plain arithmetic tree (an Oracle-backed shape). '''
    root = _parse_sexpr(str(shape))
    lines = []
    names = {}
    counter = [0]

    def name_for(key, expr):
        nm = names.get(key)
        if nm is None:
            counter[0] += 1
            nm = '{}{}'.format(prefix, counter[0])
            names[key] = nm
            lines.append('{} = {}'.format(nm, expr))
        return nm

    # iterative post-order evaluation
    stack = [(root, False)]
    done = {}
    while stack:
        node, visited = stack.pop()
        if id(node) in done:
            continue
        if node.op in ('x', 'y', 'z'):
            done[id(node)] = node.op
        elif node.op == 'const':
            done[id(node)] = name_for(('c', node.value), 'Shape.wrap({!r})'.format(node.value))
        elif not visited:
            stack.append((node, True))
            for a in node.args:
                stack.append((a, False))
        else:
            kids = [done[id(a)] for a in node.args]
            if len(kids) == 1:
                if node.op not in _UNARY:
                    raise ValueError('unsupported unary op {!r}'.format(node.op))
                expr = _UNARY[node.op].format(kids[0])
                done[id(node)] = name_for((node.op, kids[0]), '(' + expr + ')')
            else:
                if node.op not in _BINARY:
                    raise ValueError('unsupported op {!r}'.format(node.op))
                acc = kids[0]
                for k in kids[1:]:
                    acc = name_for((node.op, acc, k), '(' + _BINARY[node.op].format(acc, k) + ')')
                done[id(node)] = acc
    return lines, done[id(root)]


def _write_cache(cache_path, step_path, step_key, parts, names, rev=None, fit_report=None):
    # The trees go in binary files next to the cache (written first; the
    # .py that refers to them last, so a partial cache is never used)
    trees_dir = os.path.splitext(cache_path)[0] + '.trees'
    os.makedirs(trees_dir, exist_ok=True)
    for f in os.listdir(trees_dir):
        if f.endswith('.tree'):
            os.remove(os.path.join(trees_dir, f))
    body = []
    body.append('# FielDes STEP import cache -- generated automatically; safe to delete.')
    body.append('# It is regenerated whenever the STEP file changes or the import algorithm does.')
    body.append('# format: {}'.format(_CACHE_FORMAT))
    body.append('# source: {}'.format(os.path.basename(step_path)))
    body.append('# source-sha256: {}'.format(step_key))
    body.append('# import-version: {}'.format(_import_version()))
    body.append('# parts: {}'.format(len(parts)))
    body.append('# rev: {}'.format(rev if rev is not None else 0))
    body.append('')
    body.append('import os')
    body.append('from fieldes.ffi import lib')
    body.append('from fieldes.shape import Shape')
    body.append('from fieldes.stdlib.cad_import import FailedPart')
    body.append('')
    body.append('# Each part\'s occurrence in the assembly (or its solid\'s name)')
    body.append('NAMES = {!r}'.format([n or '' for n in names]))
    body.append('# B-spline faces approximated: (part, faces, worst deviation mm, worst face)')
    body.append('FIT_REPORT = {!r}'.format(list(fit_report or [])))
    body.append('')
    body.append('')
    body.append('def build():')
    body.append('    """Rebuilds every part: returns [(shape, (xyz_min, xyz_max)), ...].')
    body.append('    A part whose solid could not be reconstructed as native CSG (no')
    body.append('    Oracle fallback exists) is a FailedPart that raises the specific')
    body.append('    reason the moment it is actually used, not when this runs."""')
    body.append('    trees = os.path.splitext(__file__)[0] + ".trees"')
    body.append('    def tree(name):')
    body.append('        p = lib.libfive_tree_load(os.path.join(trees, name).encode("utf-8"))')
    body.append('        if not p:')
    body.append('            raise OSError("missing cache tree " + name)')
    body.append('        return Shape(p)')
    body.append('    out = []')
    for i, (shape, bounds) in enumerate(parts):
        lo, hi = bounds
        bnd = '(({!r}, {!r}, {!r}), ({!r}, {!r}, {!r}))'.format(lo[0], lo[1], lo[2], hi[0], hi[1], hi[2])
        if isinstance(shape, FailedPart):
            body.append('    # part {}: FAILED to import -- {}'.format(i, shape.error.replace('\n', ' ')))
            body.append('    out.append((FailedPart({!r}), {}))'.format(shape.error, bnd))
            continue
        # (binary tree files: replaying the trees as Python source took
        # 24 s for Bandextruder.stp, nearly as long as importing it)
        if not lib.libfive_tree_save(shape.ptr, os.path.join(trees_dir, 'p{}.tree'.format(i)).encode('utf-8')):
            raise OSError('cannot write the cache')
        body.append('    # part {}: reconstructed CSG{}'.format(
            i, ' -- ' + names[i].replace('\n', ' ') if i < len(names) and names[i] else ''))
        body.append('    out.append((tree({!r}), {}))'.format('p{}.tree'.format(i), bnd))
        marker = getattr(shape, '_fit_marker', None)
        if marker is not None:
            if not lib.libfive_tree_save(marker.ptr, os.path.join(trees_dir, 'm{}.tree'.format(i)).encode('utf-8')):
                raise OSError('cannot write the cache')
            body.append('    out[-1][0]._fit_marker = tree({!r})  # where B-spline faces were approximated'
                        .format('m{}.tree'.format(i)))
        ref = getattr(shape, '_step_ref', None)
        if ref is not None:
            body.append('    out[-1][0]._step_ref = {!r}'.format(tuple(ref)))
        metrics = getattr(shape, '_step_metrics', None)
        if metrics is not None:
            body.append('    out[-1][0]._step_metrics = {!r}  # smallest feature, flat and curved area'
                        .format(tuple(float(v) for v in metrics)))
    body.append('    return out')
    body.append('')
    body.append(_END_MARKER.rstrip('\n'))
    text = '\n'.join(body) + '\n'
    tmp = '{}.tmp{}'.format(cache_path, os.getpid())
    try:
        with open(tmp, 'w', encoding='utf-8') as f:
            f.write(text)
        os.replace(tmp, cache_path)
    except OSError:
        try:
            os.remove(tmp)
        except OSError:
            pass


def _header_value(text, key):
    tag = '# {}: '.format(key)
    for line in text.split('\n', 12):
        if line.startswith(tag):
            return line[len(tag):].strip()
    return None


class _CountingLoads:
    ''' The library, as a cache file's build() sees it: tree loads count
        their file's bytes of all the tree files, for the progress bar '''
    def __init__(self, lib, trees_dir, name):
        self._lib = lib
        self._name = name
        try:
            self._sizes = {f: os.path.getsize(os.path.join(trees_dir, f)) for f in os.listdir(trees_dir)}
        except OSError:
            self._sizes = {}
        self._total = max(1, sum(self._sizes.values()))
        self._done = 0

    def __getattr__(self, name):
        return getattr(self._lib, name)

    def libfive_tree_load(self, path):
        out = self._lib.libfive_tree_load(path)
        self._done += self._sizes.get(os.path.basename(path.decode('utf-8', 'replace')), 0)
        run_progress.report(self._done / self._total, 'loading the cached import of {}'.format(self._name))
        return out


def _load_cache(cache_path, step_path, step_key, rev=None):
    ''' Returns (parts, names) from a valid cache file, or None if there is
        no cache, it is stale, corrupt, or incomplete. '''
    try:
        with open(cache_path, 'r', encoding='utf-8') as f:
            text = f.read()
    except OSError:
        return None
    if not text.endswith(_END_MARKER):
        return None                                   # truncated / partial write
    reason = None
    if _header_value(text, 'format') != str(_CACHE_FORMAT):
        reason = 'older cache format'
    elif _header_value(text, 'source-sha256') != step_key:
        reason = 'the file changed'
    elif _header_value(text, 'import-version') != _import_version():
        reason = 'import algorithm changed ({} -> {})'.format(
            _header_value(text, 'import-version') or '?', _import_version())
    elif (_header_value(text, 'rev') or '0') != str(rev if rev is not None else 0):
        return None                                   # a reimport was asked for: nothing to say
    if reason:
        # (never silent: an import that could have been a second long is a minute)
        print('{}: cache not used ({}), importing again.'.format(os.path.basename(step_path), reason))
        return None
    try:
        ns = {'__file__': cache_path}
        exec(compile(text, cache_path, 'exec'), ns)
        # Progress: the bytes of tree files read (reading them is the time)
        ns['lib'] = _CountingLoads(ns['lib'], os.path.splitext(cache_path)[0] + '.trees',
                                   os.path.basename(step_path))
        with run_progress.task('loading the cached import'):
            parts = ns['build']()
        names = list(ns.get('NAMES', []))
        _fit_reports[os.path.normcase(os.path.abspath(step_path))] = \
            [tuple(r) for r in ns.get('FIT_REPORT', [])]
    except Exception:
        return None                                   # corrupt cache: regenerate
    if str(len(parts)) != _header_value(text, 'parts') or len(names) != len(parts):
        return None
    return parts, names


# ---------------------------------------------------------------------------
# Main entry points
# ---------------------------------------------------------------------------

def import_step_parts(path, cache=True, units='mm', rev=None, auto_exclude=False,
                      exclude_threshold=1.0, exclude_quality=64):
    ''' Imports a STEP (.step/.stp) file as a separate Shape PER SOLID, using
        FielDes's built-in reader -- no external CAD-kernel dependency.

        Every solid is rebuilt as CSG by the reconstruction algorithm:
        analytic faces (plane, cylinder, cone, sphere, torus) as exact
        expressions, free-form B-spline faces as fitted closed-form surfaces
        (an approximation: the faces that fit poorly are listed after the
        import and shown in red).  The results are ordinary Shapes.
        A solid the reconstruction can't resolve does not stop the rest of
        the file from importing -- its list entry is a FailedPart instead,
        which raises a specific RuntimeError describing why the moment it's
        actually used (meshed, combined with other shapes, etc).

        The reconstruction can take several seconds on a large file, so the
        result is saved to a Python file next to the STEP file
        ("<file>.fieldes-cache.py") and reused on later calls -- for example
        every time a FielDes script is re-run -- until the STEP file changes or
        the import algorithm does (a rebuilt library keeps it). cache=False
        disables it; cache='some/path.py'
        stores it elsewhere.

        Meshing each part returned here separately, over its own `bounds`,
        lets you pick a resolution matched to that part's own scale:

            for i, (shape, bounds) in enumerate(import_step_parts(path)):
                shape.save_stl(f'part_{i}.stl', bounds[0], bounds[1],
                                resolution=...)

        Returns a list of (Shape, (xyz_min, xyz_max)) tuples, one per solid,
        where xyz_min/xyz_max are that solid's own tight bounding box
        (three-element tuples, ready to pass straight into
        save_stl()/get_mesh()). Raises RuntimeError if the file can't be
        read or contains no importable solids.

        Assemblies come out assembled: every part is placed where the
        file's assembly structure puts it.  A component used several
        times (four identical screws) gives one entry per occurrence --
        the first at its solid's index, the others appended after the last
        solid, so part indices stay the same as for a single occurrence.
        Each part Shape's `_part_name` is its occurrence in the assembly
        (e.g. 'Drive:1/Motor:1/M3x10-Screw:2').

        units: the model is converted from the unit each part of the STEP
        file declares (millimetres, metres, inches, ...; one file can mix
        them) into these units ('mm' by default; 'cm', 'm', 'in' also
        work), so a file exported in metres isn't a thousand times too
        small.  units='file' gives the numbers in the file's first declared
        unit.

        rev: an arbitrary revision number that is part of the cache key --
        changing it (FielDes's "Reimport" does exactly that) forces a fresh
        reconstruction instead of reusing the cached result.

        B-spline faces are imported as fitted simple surfaces (planes,
        quadrics, extruded / revolved / helical curves), listed after the import; where
        a fit is off by more than 0.5 % of its face's size, the model is
        shaded there, turning fully red at 10 %.
        Faces that must be exact (threads, gear teeth): exclude() a region
        around them -- that uses the STEP geometry itself.

        auto_exclude=True does that for every poorly fitted place by itself: each
        part's fit marker is read, the places where the fit is off by more than
        `exclude_threshold` (percent of the face's size, 1.0 by default; 0.5 is the
        lowest) are excluded from the part (the region is the field poor_fit_region),
        so the FielDes viewport draws the STEP file's own surface there and the fitted
        field everywhere else.  The region is cut out of the part's field, which is
        what further modelling sees.  `exclude_quality` is exclude()'s `quality`.
        Off by default: the exact pieces cost meshing time.
    '''
    step_path = os.path.abspath(path)
    factor, unit_mm = _output_factor(step_path, units)
    loaded = None
    if cache:
        cache_path = cache if isinstance(cache, str) else step_path + _CACHE_SUFFIX
        try:
            step_key = _sha256_file(step_path)
        except OSError:
            step_key = None                           # let the C reader report the error
        if step_key is not None:
            loaded = _load_cache(cache_path, step_path, step_key, rev)
            if loaded is None:
                loaded = _reconstructed_parts(path)
                try:
                    _write_cache(cache_path, step_path, step_key, loaded[0], loaded[1], rev,
                                 _fit_reports.get(os.path.normcase(step_path)))
                except Exception:
                    pass                              # a cache is an optimisation only
    if loaded is None:
        loaded = _reconstructed_parts(path)
    parts, names = loaded
    parts = _scale_parts(parts, factor)
    _name_parts(parts, names)
    _log_import(step_path, 'step', parts, unit_mm, units, names)
    report = _fit_reports.get(os.path.normcase(step_path))
    _print_fit_report(step_path, report, factor, units)
    parts = _mark_poor_fits(parts, report)
    for shape, _ in parts:
        ref = getattr(shape, '_step_ref', None)
        if ref is not None and not isinstance(shape, FailedPart):
            scale = [[factor if r == c and r < 3 else (1.0 if r == c else 0.0) for c in range(4)]
                     for r in range(4)]
            shape._exact_source = _ExactSource(step_path, ref[0], ref[1], scale)
    if auto_exclude:
        parts = _auto_exclude(parts, exclude_threshold, exclude_quality)
    return parts


def import_step(path, cache=True, units='mm', rev=None):
    ''' Imports a STEP (.step/.stp) file as ONE Shape combining every solid
        (the union of import_step_parts()), so it can be used with the rest
        of fieldes.stdlib (union/difference/intersection with anything else,
        transforms, etc). Cached exactly as import_step_parts() is.

        Meshing a multi-part assembly as a single Shape forces one
        resolution across the whole assembly's bounding box, driven by
        whatever feature is smallest anywhere in the file; for a large
        assembly, mesh each part from import_step_parts() over its own
        bounds instead.

        Raises RuntimeError (with a message describing why) if the file
        can't be read, contains no importable solids, or ANY solid could
        not be reconstructed as native CSG (unlike import_step_parts(),
        which lets you still use the other, good parts of the same file by
        index -- combining every part into one Shape here means one
        FailedPart necessarily fails the whole union).
    '''
    parts = import_step_parts(path, cache=cache, units=units, rev=rev)
    whole = parts[0][0]
    for shape, _ in parts[1:]:
        whole = whole.min(shape)
    lo, hi = roi(parts, pad=0.0)
    try:
        whole._bounds = (lo, hi)
        whole._exact_sources = [src for shape, _ in parts for src in _sources_of(shape)]
    except AttributeError:
        pass
    return whole


# Kept so existing scripts keep working: the reconstruction is now the
# default, so this is simply the main function under its old name.
import_step_parts_reconstructed = import_step_parts



# ---------------------------------------------------------------------------
# Exact regions: the part's real geometry, meshed straight from the STEP file
# ---------------------------------------------------------------------------
#
# Where the imported field is only approximate (B-spline faces fitted by
# simple surfaces -- shown in red --, threads, ...), exclude() cuts a box out
# of the field and puts the part's exact geometry back inside it: FielDes
# meshes the solid directly from the STEP file (no field), cuts it to the
# box and joins it to the field's mesh with a mesh boolean, for display and
# STL export alike.  The field itself (for further modelling) only has the
# box cut out.
#
# A part remembers where it came from (_ExactSource: file, solid, instance
# and its placement since the import, a 4x4 matrix); moving, rotating,
# scaling or mirroring it with the fieldes.stdlib transforms carries that
# along.  The numbers may be FielDes's draggable variables (Shapes): FielDes
# evaluates them when it renders, so dragging a region's box updates the
# exact piece too.

def _num(v):
    return isinstance(v, (int, float))


def _mul(a, b):
    if (_num(a) and a == 0) or (_num(b) and b == 0):
        return 0.0
    if _num(a) and a == 1:
        return b
    if _num(b) and b == 1:
        return a
    if _num(a) and _num(b):
        return float(a * b)
    return Shape.wrap(a) * Shape.wrap(b)


def _add(a, b):
    if _num(a) and a == 0:
        return b
    if _num(b) and b == 0:
        return a
    if _num(a) and _num(b):
        return float(a + b)
    return Shape.wrap(a) + Shape.wrap(b)


def _matmul(A, B):
    out = []
    for r in range(4):
        row = []
        for c in range(4):
            v = 0.0
            for k in range(4):
                v = _add(v, _mul(A[r][k], B[k][c]))
            row.append(v)
        out.append(row)
    return out


def _identity():
    return [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def _translation(v):
    m = _identity()
    for i in range(3):
        m[i][3] = v[i]
    return m


def _neg(v):
    return -v if _num(v) else -Shape.wrap(v)


def _about(center, m):
    """ m applied about `center` instead of the origin """
    return _matmul(_translation(center), _matmul(m, _translation([_neg(c) for c in center])))


class _ExactSource:
    """ Where a part came from: solid `solid` (instance `instance`) of a STEP
        file, placed by `matrix` since the import (4x4, numbers or Shapes) """
    def __init__(self, path, solid, instance, matrix):
        self.path, self.solid, self.instance, self.matrix = path, solid, instance, matrix

    def transformed(self, m):
        return _ExactSource(self.path, self.solid, self.instance, _matmul(m, self.matrix))


class _ExactRegion:
    """ The exact geometry of one source inside the region `field` (a Shape,
        negative inside it; None: everywhere), the region placed by `region`
        (4x4) -- what FielDes meshes and joins """
    def __init__(self, source, field, region, quality):
        self.source, self.field, self.region, self.quality = source, field, region, quality

    def transformed(self, m):
        return _ExactRegion(self.source.transformed(m), self.field, _matmul(m, self.region), self.quality)

    def _flat(self):
        """ For FielDes: (path, solid, instance, part matrix (16 entries),
            region matrix (16), the region's field or None, quality) """
        s = self.source
        return (s.path, int(s.solid), int(s.instance),
                tuple(v for row in s.matrix for v in row),
                tuple(v for row in self.region for v in row),
                self.field, self.quality)


def _sources_of(shape):
    s = getattr(shape, '_exact_source', None)
    if s is not None:
        return [s]
    return list(getattr(shape, '_exact_sources', None) or [])


_CARRIED = ('_color_field', '_color_range', '_color_label', '_color_map', '_fit_marker',
            '_part_name', '_step_ref', '_step_metrics', '_bounds')


def _carry_exact(out, src, m=None):
    """ Gives `out` (made from `src`) src's exact-geometry information,
        moved by the 4x4 matrix m (None: not moved) """
    try:
        sources = _sources_of(src)
        regions = list(getattr(src, '_exact', None) or [])
        if m is not None:
            sources = [s.transformed(m) for s in sources]
            regions = [r.transformed(m) for r in regions]
        if sources:
            out._exact_sources = sources
        if regions:
            out._exact = regions
    except AttributeError:
        pass
    return out


def _is_pair(item):
    """ (Shape, (lower corner, upper corner)): one entry of what import_step_parts returns """
    return (isinstance(item, (tuple, list)) and len(item) == 2 and isinstance(item[0], Shape)
            and isinstance(item[1], (tuple, list)) and len(item[1]) == 2)


def _may_touch(region, bounds):
    """ Whether the region (a field) can be negative -- inside -- anywhere in the box `bounds`:
        interval arithmetic, so a region that is certainly outside gives False """
    try:
        from fieldes.ffi import libfive_region_t, libfive_interval_t
        from fieldes import app_support
        known = app_support._var_values()
        box = libfive_region_t(*[libfive_interval_t(float(bounds[0][i]), float(bounds[1][i]))
                                 for i in range(3)])
        trees, values, n = known if known is not None else (None, None, 0)
        return lib.libfive_tree_interval_lower(region.ptr, box, trees, values, n) <= 0
    except Exception:
        return True


def poor_fit_region(part, threshold=1.0):
    """ The places where an imported part's B-spline fit is worse than `threshold`
        percent of the face's size, as a region: a field that is negative there.
        It is made from the part's fit marker (the field that shades the poor fits
        grey to red), nothing is sampled; where it is negative, the exact surface of
        the STEP file is the better one.  None if nothing of the part was fitted.

            kitchen = exclude(kitchen, poor_fit_region(kitchen[19][0], 2.0))

        exclude() uses it when it is given no region. """
    marker = getattr(part, '_fit_marker', None)
    if marker is None:
        return None
    return Shape.wrap(threshold / 100.0) - marker


def exclude(shape, region=None, source=None, quality=64, threshold=1.0):
    """ Draws the EXACT surface of the imported part(s) `shape`, meshed straight
        from the STEP file, inside a region -- for places the import only
        approximates (B-spline faces fitted by simple surfaces, which the import
        marks in red; threads; ...) that have to stay exact.

        The region is a FIELD OBJECT: any shape -- a sphere, a box, a part, a
        part offset by some distance, a union of those -- is a region, the
        inside of the shape (where its field is negative):

            rails = union(kitchen[59][0], kitchen[60][0])   # the parts' own fields
            kitchen = exclude(kitchen, offset(rails, 90))   # everything within 90 mm of them

        Without a region, it is the places where the fit is worse than
        `threshold` percent of the face's size (poor_fit_region), in every part
        that has any:

            kitchen = import_step_parts('step/kitchen.stp')
            kitchen = exclude(kitchen)

        What you give it decides how much it works on.  The WHOLE IMPORT (what
        import_step_parts returns): the region goes into every part it
        touches, and the same list comes back, the parts in the same places.
        One entry of the import, kitchen[19], or one part, kitchen[19][0]:
        that part only.

            kitchen[19] = exclude(kitchen[19], region)

        Inside the region FielDes draws the exact surface instead of the shape's
        own (in the view and in the exported STL): the shape's mesh loses its
        triangles there and the STEP file's surface is cut by the region's field
        and put in, the edge between them jagged by one cell.  The field itself
        -- what further modelling, FEA or lattices see -- is not changed.  The
        region's surface should cross no badly fitted face (the red one): where
        the fit is off, the exact surface and the fitted one do not meet at the
        region's surface and the seam shows as a step.  A region that encloses
        the whole bad face has no such seam.

        For a shape made from a part (moved, mirrored, cut, filled with a
        lattice ...) say which imported part its exact geometry comes from,
        and apply exclude() last, to the finished shape:

            part = import_step_parts('bracket.step')[0][0]
            light = ...                  # anything made from part
            final = exclude(light, region, source=part)

        The part may have been moved, rotated, scaled or mirrored with the
        fieldes.stdlib transforms since the import (the exact piece follows);
        several regions can be excluded one after the other.

        quality: points per full turn of a circle in the exact mesh.
        threshold: for the default region, in percent of the face's size.
    """
    if isinstance(shape, (list, tuple)) and not _is_pair(shape):
        if source is not None:
            raise ValueError('exclude(): source= is for one part; the import\'s parts are their own sources')
        field = None if region is None else Shape.wrap(region)
        done = []
        for item in shape:
            part = item[0] if _is_pair(item) else item
            if not isinstance(part, Shape) or not _sources_of(part):
                done.append(item)               # (a part that failed to import, anything else: as it is)
                continue
            mine = field if field is not None else poor_fit_region(part, threshold)
            if mine is None:
                done.append(item)               # (fitted well everywhere: nothing to do)
                continue
            bounds = item[1] if _is_pair(item) else getattr(part, '_bounds', None)
            if field is not None and bounds is not None and not _may_touch(mine, bounds):
                done.append(item)               # (the region does not reach this part)
                continue
            done.append(exclude(item, mine, quality=quality))
        return type(shape)(done)
    if _is_pair(shape):
        return (exclude(shape[0], region, source=source, quality=quality, threshold=threshold), shape[1])

    of = source if source is not None else shape
    sources = _sources_of(of)
    if not sources:
        raise ValueError('exclude(): the source must be a part imported from a STEP file '
                         '(import_step_parts / import_step), or made from one by moving, '
                         'rotating, scaling or mirroring it')
    if region is None:
        region = poor_fit_region(of, threshold)
        if region is None:
            return shape                        # (fitted well everywhere: nothing to do)
    region = Shape.wrap(region)
    # (a shape of its own with the same field: the original keeps no exact regions)
    out = shape.max(Shape.wrap(-1.0e9))
    for name in _CARRIED:
        if hasattr(shape, name):
            try:
                setattr(out, name, getattr(shape, name))
            except AttributeError:
                pass
    # (the part's own render resolution, set by roi_resolution on it or on this shape, whichever
    # comes first: FielDes follows this link to the part)
    out._placed_from = shape
    # The red marking of poorly fitted B-spline faces doesn't apply where
    # the geometry is now exact
    if getattr(out, '_color_map', None) == 'fit' and getattr(out, '_color_field', None) is not None:
        out._color_field = out._color_field.min((region * 1.0e6).max(0))
    _carry_exact(out, shape)
    out._exact = list(getattr(shape, '_exact', None) or []) + \
        [_ExactRegion(s, region, _identity(), quality) for s in sources]
    return out


def _auto_exclude(parts, threshold, quality):
    """ import_step_parts(auto_exclude=True): every part with poorly fitted places gets them excluded """
    out = exclude(parts, quality=quality, threshold=threshold)
    changed = sum(1 for before, after in zip(parts, out) if after[0] is not before[0])
    print('auto_exclude: exact surface added to %d of %d parts (fit worse than %g %%)'
          % (changed, len(parts), threshold))
    return out


def exact_region_mesh(shape, cell=1.0):
    """ The exact pieces of the regions excluded from `shape` (see exclude)
        as (vertices, triangles) lists -- outside FielDes, e.g. to check
        them: the STEP file's surface inside each region, the triangles the
        region's surface crosses cut down to `cell` long.  Every number must
        be a plain number (no FielDes variables), and no region may have been
        moved after exclude(). """
    def val(v):
        return float(v) if _num(v) else float(Shape.wrap(v)(0, 0, 0))

    verts, tris = [], []
    for r in getattr(shape, '_exact', None) or []:
        path, solid, inst, m, region, field, quality = r._flat()
        if any(abs(val(v) - (1.0 if i % 5 == 0 else 0.0)) > 1e-12 for i, v in enumerate(region)):
            raise ValueError('exact_region_mesh: a region was moved after exclude() (FielDes only)')
        mat = (ctypes.c_double * 16)(*[val(v) for v in m])
        mesh = lib.libfive_step_exact_clipped(path.encode('utf-8'), solid, inst, mat,
                                              None if field is None else field.ptr, float(cell),
                                              int(val(quality)))
        if not mesh:
            raise RuntimeError(lib.libfive_import_step_last_message().decode('utf-8', 'replace'))
        base = len(verts)
        mc = mesh.contents
        verts.extend((mc.verts[i].x, mc.verts[i].y, mc.verts[i].z) for i in range(mc.vert_count))
        tris.extend((base + mc.tris[i].a, base + mc.tris[i].b, base + mc.tris[i].c)
                    for i in range(mc.tri_count))
        lib.libfive_mesh_delete(mesh)
    return verts, tris
