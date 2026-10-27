'''
Helpers used by the FielDes GUI (not part of the modelling API).

completion_info() feeds the editor's autocompletion / go-to-definition.
'''

import inspect


def completion_info():
    ''' Returns a list of tab-separated records for the editor:
            def<TAB>name<TAB>file<TAB>line     library definition locations
            member<TAB>owner<TAB>name           names offered after "owner."
            tip<TAB>name<TAB>signature          one-line call tips
    '''
    import importlib
    # (not `import fieldes.stdlib as stdlib`: that gives the library handle `stdlib` which the
    # package re-exports under the same name, and a table made from it has almost nothing in it)
    stdlib = importlib.import_module('fieldes.stdlib')
    shape_mod = importlib.import_module('fieldes.shape')
    out = []

    def add_def(name, obj):
        try:
            f = inspect.getsourcefile(obj)
            line = inspect.getsourcelines(obj)[1]
        except (TypeError, OSError):
            return
        if f:
            out.append('def\t{}\t{}\t{}'.format(name, f, line))

    def add_tip(name, obj):
        try:
            sig = str(inspect.signature(obj))
        except (TypeError, ValueError):
            return
        doc = inspect.getdoc(obj) or ''
        first = doc.strip().split('\n', 1)[0] if doc else ''
        tip = name + sig + ('  --  ' + first if first else '')
        out.append('tip\t{}\t{}'.format(name, tip.replace('\t', ' ').replace('\n', ' ')))

    for name in dir(stdlib):
        if name.startswith('_'):
            continue
        obj = getattr(stdlib, name)
        if inspect.isfunction(obj) or inspect.isclass(obj):
            add_def(name, obj)
            add_tip(name, obj)

    Shape = shape_mod.Shape
    add_def('Shape', Shape)
    for name in dir(Shape):
        if name.startswith('_'):
            continue
        out.append('member\t*\t' + name)
        obj = getattr(Shape, name, None)
        if obj is not None and callable(obj):
            # (read from the file: inspect would point at the wrapper that decorates the method)
            where = _package_methods(name)
            if where:
                out.append('def	{}	{}	{}'.format(name, where[0][0], where[0][1]))
            else:
                add_def(name, obj)
            add_tip(name, obj)

    try:
        from fieldes import blocks
        out.extend(blocks.records())
    except Exception:
        pass

    for name in ('set_bounds', 'set_resolution', 'set_quality'):
        out.append('member\tview\t' + name)
    out.append('tip\tset_bounds\tview.set_bounds([xmin, ymin, zmin], [xmax, ymax, zmax], auto_scale=True)'
               '  --  render region; it follows the models (grows, shrinks) in the script, unless auto_scale=False')
    out.append('tip\tset_resolution\tview.set_resolution(res)  --  voxels per unit')
    out.append('tip\tset_quality\tview.set_quality(q)  --  mesh accuracy (1-10)')

    # `app`: the inputs of a script that is run as an app (a tip only where no function of the library has the name: `text` is both)
    try:
        from fieldes import app as _app_module
        tipped = {r.split('\t')[1] for r in out if r.startswith('tip\t')}
        for name in _app_module.__all__:
            out.append('member\tapp\t' + name)
            fn = getattr(_app_module, name)
            if name not in tipped:
                first = (inspect.getdoc(fn) or '').split('\n')[0].strip()
                out.append('tip\t{}\tapp.{}{}{}'.format(name, name, inspect.signature(fn), '  --  ' + first if first else ''))
    except Exception:
        pass
    return out


# ---------------------------------------------------------------------------
# Go to definition across files
# ---------------------------------------------------------------------------

import ast
import importlib.machinery
import os
import sys

_PARSED = {}        # path -> (mtime, _FileInfo)
_OVERRIDE = {}      # path -> _FileInfo of the text being edited (which may not be saved yet)
_PACKAGE_INDEX = {}


class _FileInfo:
    ''' What a Python file defines (top level), imports and which methods it has '''
    def __init__(self, tree):
        self.defs = {}          # name -> line (functions, classes, plain assignments)
        self.imports = {}       # bound name -> (module, level, original name or None for `import x`)
        self.stars = []         # (module, level) of `from m import *`
        self.methods = {}       # method name -> [(class, line)]
        self._top(tree.body)
        for node in ast.walk(tree):
            if isinstance(node, ast.ImportFrom):
                for a in node.names:
                    if a.name == '*':
                        self.stars.append((node.module or '', node.level))
                    else:
                        self.imports.setdefault(a.asname or a.name, (node.module or '', node.level, a.name))
            elif isinstance(node, ast.Import):
                for a in node.names:
                    if a.asname:
                        self.imports.setdefault(a.asname, (a.name, 0, None))
                    else:
                        top = a.name.split('.')[0]
                        self.imports.setdefault(top, (top, 0, None))
            elif isinstance(node, ast.ClassDef):
                for item in node.body:
                    if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef)):
                        self.methods.setdefault(item.name, []).append((node.name, item.lineno))

    def _top(self, body):
        for node in body:
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                self.defs.setdefault(node.name, node.lineno)
            elif isinstance(node, ast.Assign):
                for t in node.targets:
                    if isinstance(t, ast.Name):
                        self.defs.setdefault(t.id, node.lineno)
            elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
                self.defs.setdefault(node.target.id, node.lineno)
            elif isinstance(node, (ast.If, ast.Try, ast.With)):
                for part in ('body', 'orelse', 'finalbody'):
                    self._top(getattr(node, part, []))
                for h in getattr(node, 'handlers', []):
                    self._top(h.body)


def _info(path):
    if path in _OVERRIDE:
        return _OVERRIDE[path]
    try:
        mtime = os.path.getmtime(path)
    except OSError:
        return None
    hit = _PARSED.get(path)
    if hit and hit[0] == mtime:
        return hit[1]
    try:
        import tokenize
        with tokenize.open(path) as f:
            tree = ast.parse(f.read(), path)
    except (OSError, SyntaxError, ValueError, UnicodeDecodeError):
        return None
    info = _FileInfo(tree)
    _PARSED[path] = (mtime, info)
    return info


def _module_file(module, level, importer):
    ''' The .py file of a module (or None), found on disk without importing anything:
        relative to `importer` for `from .x import y`, else in the importer's folder and on sys.path '''
    here = os.path.dirname(importer) if importer else os.getcwd()
    parts = module.split('.') if module else []
    if level:
        base = here
        for _ in range(level - 1):
            base = os.path.dirname(base)
        dirs = [base]
        if not parts:
            init = os.path.join(base, '__init__.py')
            return init if os.path.exists(init) else None
    else:
        dirs = [here] + [p for p in sys.path if p]
    for i, part in enumerate(parts):
        spec = importlib.machinery.PathFinder.find_spec(part, dirs)
        if spec is None:
            return None
        if i == len(parts) - 1:
            origin = spec.origin
            return origin if origin and origin.endswith('.py') else None
        dirs = list(spec.submodule_search_locations or [])
        if not dirs:
            return None
    return None


def _submodule_file(package_file, name):
    ''' `name` as a module inside the package whose __init__.py is `package_file` '''
    if os.path.basename(package_file) != '__init__.py':
        return None
    folder = os.path.dirname(package_file)
    for candidate in (os.path.join(folder, name + '.py'), os.path.join(folder, name, '__init__.py')):
        if os.path.exists(candidate):
            return candidate
    return None


def _bind(path, name, seen):
    ''' What `name` means in the file `path`: ('def', file, line), ('module', file, 1) or None '''
    if (path, name) in seen:
        return None
    seen.add((path, name))
    info = _info(path)
    if info is None:
        return None
    if name in info.defs:
        return ('def', path, info.defs[name])
    if name in info.imports:
        module, level, original = info.imports[name]
        target = _module_file(module, level, path)
        if original is None:
            return ('module', target, 1) if target else None
        if target:
            hit = _bind(target, original, seen)
            if hit:
                return hit
            sub = _submodule_file(target, original)
            if sub:
                return ('module', sub, 1)
        return None
    for module, level in info.stars:
        target = _module_file(module, level, path)
        if target:
            hit = _bind(target, name, seen)
            if hit:
                return hit
    return None


def _package_methods(name):
    ''' Methods called `name` in the fieldes package (Shape's first), as (file, line) '''
    here = os.path.dirname(os.path.abspath(__file__))
    if not _PACKAGE_INDEX:
        for folder, _, files in os.walk(here):
            for f in sorted(files):
                if f.endswith('.py'):
                    path = os.path.join(folder, f)
                    info = _info(path)
                    if info:
                        for m, hits in info.methods.items():
                            for cls, line in hits:
                                _PACKAGE_INDEX.setdefault(m, []).append((cls != 'Shape', path, line))
    hits = sorted(_PACKAGE_INDEX.get(name, []), key=lambda h: h[0])
    return [(p, l) for _, p, l in hits]


def find_definition(arg):
    ''' Where a name used in a file is defined, for the editor's go-to-definition.

        arg: "name<US>owner<US>file<US>text" (US: the character \\x1f) -- owner is the identifier
        before a "." (or empty), file the file the name is used in (empty: a script that was never
        saved; its folder is the current one), text what is being edited in it, which may differ
        from the file on disk.
        Returns "file<TAB>line", or "" when the name is not defined in any Python file we can read.
        Everything is read from the files (imports are followed, `from m import *` included);
        nothing is imported.
    '''
    name, owner, path, text = (arg.split('\x1f') + ['', '', '', ''])[:4]
    if not name:
        return ''
    here = path or os.path.join(os.getcwd(), '<script>')
    if text:
        try:
            _OVERRIDE[here] = _FileInfo(ast.parse(text))
        except (SyntaxError, ValueError):
            pass            # (half-typed: the saved file will have to do)
    try:
        if owner:
            hit = _bind(here, owner, set())
            if hit and hit[0] == 'module' and hit[1]:
                inner = _bind(hit[1], name, set())
                if inner and inner[0] == 'def':
                    return '{}\t{}'.format(inner[1], inner[2])
                sub = _submodule_file(hit[1], name)
                return '{}\t1'.format(sub) if sub else ''
            # a method of some object: the library's classes (Shape first)
            methods = _package_methods(name)
            return '{}\t{}'.format(*methods[0]) if methods else ''
        hit = _bind(here, name, set())
        return '{}\t{}'.format(hit[1], hit[2]) if hit and hit[1] else ''
    finally:
        _OVERRIDE.pop(here, None)


# ---------------------------------------------------------------------------
# Scene description for FielDes's model tree
# ---------------------------------------------------------------------------

import ast
import json
import os
import re

# (the ways of importing a file: tessellate() and reconstruct() are imports only when they are given a FILE -- tessellate()
# of a shape is an operation on it)
IMPORT_FUNCS = ('import_model', 'reconstruct', 'tessellate')
SETTINGS_FUNCS = ('set_bounds', 'set_resolution', 'set_quality')
HIDDEN_RE = re.compile(r'^(\s*)(?:#\s*hidden:\s?)+(.*)$')
MAX_ITEMS = 400


def _types():
    from fieldes.shape import Shape
    try:
        from fieldes.stdlib.cad_import import FailedPart
    except ImportError:
        FailedPart = ()
    return Shape, FailedPart


def _call_name(node):
    ''' "import_model" / "view.set_bounds" for a Call node '''
    f = node.func
    if isinstance(f, ast.Name):
        return f.id
    if isinstance(f, ast.Attribute):
        base = f.value.id if isinstance(f.value, ast.Name) else '?'
        return base + '.' + f.attr
    return ''


def _short_name(fn):
    return fn.rsplit('.', 1)[-1]


_SPAN_LINES = []   # source lines of the script being described (for _span)


def _char_col(line, col):
    ''' AST columns are UTF-8 byte offsets; the editor counts characters '''
    try:
        return len(_SPAN_LINES[line - 1].encode('utf-8')[:col].decode('utf-8', 'replace'))
    except IndexError:
        return col


def _span(node):
    return [node.lineno, _char_col(node.lineno, node.col_offset),
            node.end_lineno, _char_col(node.end_lineno, node.end_col_offset)]


def _names_in(node):
    return {n.id for n in ast.walk(node) if isinstance(n, ast.Name)}


class _Source:
    ''' Fast source-segment lookup (ast.get_source_segment re-splits the
        whole file on every call, which is quadratic for big scripts) '''
    def __init__(self, text):
        self.lines = text.split('\n')

    def segment(self, node):
        l0, c0, l1, c1 = node.lineno - 1, node.col_offset, node.end_lineno - 1, node.end_col_offset
        try:
            if l0 == l1:
                return self.lines[l0].encode('utf-8')[c0:c1].decode('utf-8', 'replace')
            parts = [self.lines[l0].encode('utf-8')[c0:].decode('utf-8', 'replace')]
            parts.extend(self.lines[l0 + 1:l1])
            parts.append(self.lines[l1].encode('utf-8')[:c1].decode('utf-8', 'replace'))
            return '\n'.join(parts)
        except IndexError:
            return ''


def _short(text, n=60):
    text = ' '.join(text.split())
    return text if len(text) <= n else text[:n - 3] + '...'


def _bounds_list(b):
    try:
        lo, hi = b
        return [[float(v) for v in lo], [float(v) for v in hi]]
    except (TypeError, ValueError):
        return None


def _is_import_call(node, gs=None):
    ''' Whether a Call node imports a file: import_model(...) always; reconstruct(...) and tessellate(...) when they are
        given a path -- a string, or a name that is a string in the script's globals `gs` -- and not a model
        (tessellate(shape) is an operation on it, not an import) '''
    name = _short_name(_call_name(node))
    if name not in IMPORT_FUNCS:
        return False
    if name == 'import_model':
        return True
    arg = node.args[0] if node.args else next((kw.value for kw in node.keywords if kw.arg in ('path', 'source')), None)
    if isinstance(arg, ast.Constant):
        return isinstance(arg.value, str)
    if isinstance(arg, ast.Name):
        return gs is not None and isinstance(gs.get(arg.id), str)
    return False


def _find_import(stmt, gs=None):
    ''' (call, subscript-or-None) for the first import call in a statement '''
    for node in ast.walk(stmt):
        if isinstance(node, ast.Subscript) and isinstance(node.value, ast.Call) and _is_import_call(node.value, gs):
            return node.value, node
    for node in ast.walk(stmt):
        if isinstance(node, ast.Call) and _is_import_call(node, gs):
            return node, None
    return None, None


def _const_index(sub):
    ''' Integer index of x[k] (handles Python 3.8's ast.Index wrapper) '''
    sl = sub.slice
    if hasattr(ast, 'Index') and isinstance(sl, getattr(ast, 'Index')):
        sl = sl.value
    if isinstance(sl, ast.Constant) and isinstance(sl.value, int):
        return sl.value, sl
    return None, None


def _import_log_entry(path):
    try:
        from fieldes.stdlib.cad_import import _import_log
    except ImportError:
        return None
    return _import_log.get(os.path.normcase(os.path.abspath(path)))


_SEARCH = 1.0e6          # half-size of the box searched for a shape's extent
_BOUNDS_SECONDS = 1.5    # time budget per scene for estimating extents


def _var_values():
    ''' The var() numbers of the script being run, as (trees, values) arrays for a bounds search '''
    import ctypes
    try:
        import _fieldes_host as host
        entries = getattr(host, '__vars', None)
    except ImportError:
        return None
    if not entries:
        return None
    from fieldes.ffi import libfive_tree
    trees, values = [], []
    for e in entries:
        try:
            trees.append(e[0].ptr)
            values.append(float(e[1]))
        except (TypeError, ValueError, AttributeError, IndexError):
            continue
    if not trees:
        return None
    return (libfive_tree * len(trees))(*trees), (ctypes.c_float * len(values))(*values), len(trees)


def _look_for_box(s, known, half, box, limit):
    ''' One search of the extent of shape `s`: in the cube of half-size `half` about the origin, or -- `box` given -- in a cube round that box.
        (found, box, open: it reaches a side of the cube searched, so that it may go on beyond it, seconds it took) '''
    import ctypes
    import time
    from fieldes.ffi import lib, libfive_region_t
    search = libfive_region_t()
    for k, axis in enumerate((search.X, search.Y, search.Z)):
        if box is None:
            axis.lower, axis.upper = -half, half
        else:
            # (a cube round the box: a search over a box that is not a cube does not come to an end in the time it has)
            side = 0.51 * max(box[1][i] - box[0][i] for i in range(3)) + 1e-3
            centre = 0.5 * (box[0][k] + box[1][k])
            axis.lower, axis.upper = centre - side, centre + side
    out = libfive_region_t()
    open_sides = ctypes.c_int(0)
    started = time.time()
    if known is not None:
        found = lib.libfive_tree_bounds_vars(s.ptr, search, 100000, limit, ctypes.byref(out),
                                             ctypes.byref(open_sides), known[0], known[1], known[2])
    else:
        found = lib.libfive_tree_bounds(s.ptr, search, 100000, limit, ctypes.byref(out), ctypes.byref(open_sides))
    took = time.time() - started
    if not found:
        return False, None, False, took
    return True, ((out.X.lower, out.Y.lower, out.Z.lower), (out.X.upper, out.Y.upper, out.Z.upper)), bool(open_sides.value), took


def _search_extent(s, deadline):
    ''' The box of a shape searched for with interval arithmetic, or None when there is none to be had in the time: it has no inside, it
        goes on for ever (a half-space), or the search runs out of its time '''
    import time
    from fieldes.ffi import lib
    known = _var_values() if hasattr(lib, 'libfive_tree_bounds_vars') else None
    # A search that is cut short while it refines returns the coarser box it had got to -- a 500 mm box round a part of 30 mm, when the
    # machine is busy.  It is searched again inside the box that was found (the shape is in it), as long as that makes the box
    # smaller and there is time, so that the extent -- and what is sized by it: the element size of a simulation, the framing -- is
    # right whatever the load.  When there is no time for that look the coarse box is not an extent: nothing is returned (it is
    # measured when the script has run again), for the render region would grow round it
    if time.time() > deadline:
        return None
    first_limit = 0.4               # (a part is found in tens of milliseconds, one that has var()s in a few hundred)
    found, box, opened, took = _look_for_box(s, known, _SEARCH, None, first_limit)
    if not found or opened:
        return None
    coarse = took >= 0.9 * first_limit                  # (it used all its time: the box may be a coarse one)
    rounds = 0
    while coarse and rounds < 3 and time.time() < deadline:
        rounds += 1
        size_before = sum(box[1][i] - box[0][i] for i in range(3))
        found, again, opened, took = _look_for_box(s, known, _SEARCH, box, 1.0)
        if not found or opened:
            break                           # (no better box than the coarse one: that is not an extent)
        smaller = sum(again[1][i] - again[0][i] for i in range(3)) <= 0.98 * size_before
        box = again
        coarse = smaller and took >= 0.9            # (it made the box smaller and was cut short again: look once more)
    return None if coarse else box


def _estimate_bounds(shapes, deadline):
    ''' Bounding box of one or more Shapes' interiors, from their own
        _bounds when known, else estimated with interval arithmetic
        (libfive_tree_bounds).  None if unknown, unbounded or too slow. '''
    from fieldes.stdlib.content_cache import cache_for, shape_key
    memory = cache_for('scene_bounds', 256)         # (the extents found, by the expression -- a var() of the script is its number there)
    boxes = []
    for s in shapes:
        b = getattr(s, '_bounds', None)
        if b:
            boxes.append(b)
            continue
        key = None
        try:
            key = shape_key(s)
        except Exception:
            pass
        if key is not None:
            hit, kept = memory.get(key)
            if hit:
                boxes.append(kept)
                continue
        box = _search_extent(s, deadline)
        if box is None:
            return None
        if key is not None:
            memory.put(key, box)
        boxes.append(box)
    if not boxes:
        return None
    return [[min(b[0][i] for b in boxes) for i in range(3)],
            [max(b[1][i] for b in boxes) for i in range(3)]]


# (a part with more than this is left alone: hundreds of thousands of var()s make a script slow to run
# and the numbers of a free-form surface's fit are not something to drag anyway)
_MAX_HANDLES = 6000


def menu_catalog(_arg=''):
    ''' The primitives and operations the viewport's context menus offer, as JSON (see fieldes.menu_catalog) '''
    from fieldes import menu_catalog as catalog
    return catalog.catalog()


def menu_call(request):
    ''' The call a context-menu entry writes into the script: `request` is JSON (see fieldes.menu_catalog.call).
        Raises ValueError with the reason when the entry cannot be made. '''
    from fieldes import menu_catalog as catalog
    return catalog.call(request)


def preview_mesh(request):
    ''' A coarse mesh of what a context-menu entry would make where the mouse is, for the see-through preview of the entry that is under it.  `request` is the one
        of menu_call (the same entry, place and size).  The call is evaluated on its own: an entry that is made of models of the script, or whose call is not
        complete (a placeholder where the user has to say something), has nothing to evaluate and raises -- it is not previewed.  What is meshed is the shape, at a
        resolution that follows how long a first, coarse mesh took: what renders quickly is shown, whatever it is made of, and what does not is not (the limit is
        the time it takes, not a list of entries).  JSON {"v": [x, y, z, ...], "t": [a, b, c, ...], "ms": the time}, or an empty string when there is nothing to show '''
    import time
    started = time.perf_counter()
    req = json.loads(request) if isinstance(request, str) else request
    from fieldes import menu_catalog as catalog
    # (what is not drawn as a body when it is made -- a field, a symmetry, a point, a surface, a material, the conditions of an analysis -- has no shape to show)
    for n, group, _ in catalog.PRIMITIVES:
        if n == req.get('name') and (group in ('Fields', 'Symmetry', 'Points', 'Surfaces', 'Materials', 'Fluids') or n in catalog.BODY_CONDITIONS):
            return ''
    text = catalog.call(req).split('\n#@@@#\n')[0]
    head, _marker, expression = text.rpartition(catalog.PRELUDE)
    namespace = {}
    exec('from fieldes import *', namespace)
    if head.strip():
        exec(head, namespace)           # (a function or the points that the call is made of)
    shape = eval(expression.strip(), namespace)
    if not hasattr(shape, 'get_mesh'):
        return ''
    import math
    centre = [float(req.get(k, 0.0)) for k in ('x', 'y', 'z')]
    half = 2.5 * max(float(req.get('scale', 1.0)), 1e-6)
    # The box of the shape: known for what the library makes with a size of its own (`_bounds`), so that the shape is meshed at its own size, as large or as small as it is.  Where it is not known, the box
    # round the click is the guess, and is made larger (twice at most) while the shape touches its sides -- cut open there --; a shape that is still not inside is not shown
    known = getattr(shape, '_bounds', None)
    lo0 = hi0 = None
    if known is not None:
        lo0, hi0 = [float(c) for c in known[0]], [float(c) for c in known[1]]
        if not all(math.isfinite(c) for c in lo0 + hi0) or max(hi0[i] - lo0[i] for i in range(3)) <= 0:
            known = None

    def mesh(n):
        began = time.perf_counter()
        v, f = shape.get_mesh(lo, hi, resolution=n / span)
        return v, f, time.perf_counter() - began

    for _ in range(3):
        if known is None:
            lo0, hi0 = [c - half for c in centre], [c + half for c in centre]
        pad = 0.04 * max(hi0[i] - lo0[i] for i in range(3))
        lo, hi = [c - pad for c in lo0], [c + pad for c in hi0]
        span = max(hi[i] - lo[i] for i in range(3))
        # a coarse mesh first, 24 cells across the longest side: how long it takes says how fine the next one may be
        verts, tris, first = mesh(24)
        if first > 0.25 or not tris:
            return ''                   # (too slow to be shown as the mouse passes over an entry, or nothing there)
        if known is not None:
            break
        margin = 0.5 * span / 24
        if not any(min(v[i] for v in verts) < lo[i] + margin or max(v[i] for v in verts) > hi[i] - margin for i in range(3)):
            break
        half *= 2.0
    else:
        return ''
    # then as fine as the time allows.  The mesher works on a number of cells across that is a power of two (what is asked is rounded up to the next one: a threaded rod comes out the same at 40 and at 56
    # cells, and the next mesh is the one of 96), so the levels are 64 and 128 after the 32 of the coarse mesh above, and the cost of a level grows about with the square of its cells: the finest level that
    # is expected to take about a third of a second at most is made -- 128 across, as fine as a click makes, for what is quick, such as a thread -- and a mesh that has more than 150000 triangles (the answer
    # is passed on as text) gives way to the level below it, never to the coarse one: that threw away the 69572 triangles of a threaded rod and left it ragged.  The result is kept by the menu, so that the
    # mouse that passes the entry again finds it at once
    levels = []
    cells = 64
    while cells <= 128 and first * (cells / 32.0) ** 2 <= 0.35:
        levels.append(cells)
        cells *= 2
    for cells in reversed(levels):
        try:
            finer_verts, finer_tris, _took = mesh(cells - 0.5)
        except Exception:
            continue
        if finer_tris and len(finer_tris) <= 150000:
            verts, tris = finer_verts, finer_tris
            break
    return json.dumps({'v': [round(c, 4) for v in verts for c in v], 't': [i for t in tris for i in t],
                       'ms': int((time.perf_counter() - started) * 1000)}, separators=(',', ':'))


def complete_call(request):
    ''' The call of a function as the completion writes it, complete and laid out (see fieldes.completion): `request` is JSON {name, column, indent} '''
    from fieldes import completion
    return completion.complete_for_editor(request)


def forget_literals(_arg=''):
    ''' A script was opened (see fieldes.stdlib.handles) '''
    from fieldes.stdlib import handles
    handles.forget_literals()
    return '1'


def argument_doc(request):
    ''' What the documentation says of the argument whose name is at a position of the script, as JSON (see fieldes.completion) '''
    from fieldes import completion
    return completion.argument_doc(request)


def reformat_call(request):
    ''' The innermost call that holds a position of the script, laid out again, as JSON {start, end, text} (see fieldes.completion) '''
    from fieldes import completion
    return completion.reformat_in_source(request)


def set_blocks_folder(path):
    ''' The folder of the custom blocks (FIELDES_BLOCKS, or `blocks` next to FielDes: Settings > Show the blocks folder opens it); an empty path is the default one next to FielDes.
        Returns the folder in use '''
    import os
    from fieldes import blocks
    if path:
        os.environ['FIELDES_BLOCKS'] = path
    else:
        os.environ.pop('FIELDES_BLOCKS', None)
    blocks.refresh()
    return blocks.folder()


def _bound_names(scope_node):
    ''' The names a function (or a lambda) binds itself: its parameters and what it assigns, imports, defines or loops over,
        less what it declares global or nonlocal (those are the outer scope's) -- not looking into the scopes inside it '''
    names, outer = set(), set()
    args = scope_node.args
    for a in args.posonlyargs + args.args + args.kwonlyargs + ([args.vararg] if args.vararg else []) + \
            ([args.kwarg] if args.kwarg else []):
        names.add(a.arg)
    body = scope_node.body if isinstance(scope_node.body, list) else [scope_node.body]
    stack = list(body)
    while stack:
        n = stack.pop()
        if isinstance(n, (ast.Global, ast.Nonlocal)):
            outer.update(n.names)
        elif isinstance(n, ast.Name) and isinstance(n.ctx, (ast.Store, ast.Del)):
            names.add(n.id)
        elif isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            names.add(n.name)
            continue                # (what is inside is a scope of its own)
        elif isinstance(n, ast.Lambda):
            continue
        elif isinstance(n, (ast.Import, ast.ImportFrom)):
            for a in n.names:
                names.add((a.asname or a.name).split('.')[0])
        elif isinstance(n, ast.ExceptHandler) and n.name:
            names.add(n.name)
        stack.extend(ast.iter_child_nodes(n))
    return names - outer


def rename_edits(arg):
    ''' Renaming a variable of the script (a double click on its name in the model tree).  `arg` is JSON {source, old, new};
        the answer is JSON {"edits": [[line (0-based), first column, end column], ...]} -- every place the name is written as a
        name (not in a string, not after a dot, not a keyword argument, not another variable of the same name in a function),
        and in the `# hidden: name` lines the tree writes.  Raises ValueError with the reason when `new` cannot be the name '''
    import builtins
    import keyword
    req = json.loads(arg)
    source, old, new = req['source'], req['old'], req['new']
    if new == old:
        return json.dumps({'edits': []})
    if not new.isidentifier() or keyword.iskeyword(new):
        raise ValueError('%r is not a name a variable can have: letters, digits and _, not starting with a digit' % new)
    tree = ast.parse(source)
    taken = {n.id for n in ast.walk(tree) if isinstance(n, ast.Name)}
    taken |= {n.name for n in ast.walk(tree) if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))}
    taken |= {a.arg for n in ast.walk(tree) if isinstance(n, ast.arguments)
              for a in n.posonlyargs + n.args + n.kwonlyargs}
    if new in taken:
        raise ValueError('%s is used in the script already' % new)
    try:
        import fieldes
        if hasattr(fieldes, new) or hasattr(builtins, new):
            raise ValueError('%s is the name of a function of the library: a variable of that name would hide it' % new)
    except ImportError:
        pass

    found = []

    class Finder(ast.NodeVisitor):
        def visit_Name(self, n):
            if n.id == old:
                found.append(n)

        def _function(self, n):
            # (defaults, decorators and annotations belong to the outer scope; the body to the function's, unless it binds the name)
            for d in n.args.defaults + [d for d in n.args.kw_defaults if d is not None]:
                self.visit(d)
            for d in getattr(n, 'decorator_list', []):
                self.visit(d)
            if old in _bound_names(n):
                return
            for b in (n.body if isinstance(n.body, list) else [n.body]):
                self.visit(b)

        visit_FunctionDef = visit_AsyncFunctionDef = visit_Lambda = _function

        def _comprehension(self, n):
            bound = {t.id for g in n.generators for t in ast.walk(g.target) if isinstance(t, ast.Name)}
            if old in bound:
                self.visit(n.generators[0].iter)        # (the first iterable is evaluated outside)
                return
            self.generic_visit(n)

        visit_ListComp = visit_SetComp = visit_DictComp = visit_GeneratorExp = _comprehension

    Finder().visit(tree)
    lines = source.split('\n')

    def char_col(line, byte_col):
        return len(lines[line].encode('utf-8')[:byte_col].decode('utf-8', 'replace'))

    edits = []
    for n in found:
        if n.lineno == n.end_lineno:
            edits.append([n.lineno - 1, char_col(n.lineno - 1, n.col_offset), char_col(n.lineno - 1, n.end_col_offset)])
    # the `# hidden: name` lines (a hidden display)
    hidden = re.compile(r'^(\s*#\s*hidden:\s?)(.*)$')
    word = re.compile(r'(?<![\w.])' + re.escape(old) + r'(?!\w)')
    shadow = re.compile(r'#\s*shadow:\s*(.*)$')              # (the names a Ctrl+drag marked as references)
    for i, line in enumerate(lines):
        m = hidden.match(line)
        if m:
            for w in word.finditer(m.group(2)):
                edits.append([i, m.start(2) + w.start(), m.start(2) + w.end()])
        m = shadow.search(line)
        if m:
            for w in word.finditer(m.group(1)):
                edits.append([i, m.start(1) + w.start(), m.start(1) + w.end()])
    edits.sort()
    return json.dumps({'edits': edits})


def _arg_removals(call, names, var, start_of, end_of):
    ''' The edits that take models out of a call: a plain name written as one of its arguments (union(a, b)), or as an element of a
        list or tuple that is an argument or the value of a keyword, or of a list inside one (loads=[a, b], [push, pull]).  A list
        keeps its other elements.  When its last element goes a placeholder stands in the list -- `loads=[...]`, never an empty
        bracket, which would only fail when the script runs -- and a call is not left with nothing (ValueError) '''
    top = {'nodes': sorted(list(call.args) + list(call.keywords), key=lambda n: (n.lineno, n.col_offset)),
           'kind': 'call', 'node': None, 'parent': None, 'depth': 0}
    containers = [top]

    def walk(lst, kind, parent, depth):
        c = {'nodes': list(lst.elts), 'kind': kind, 'node': lst, 'parent': parent, 'depth': depth}
        containers.append(c)
        for k, e in enumerate(lst.elts):
            if isinstance(e, (ast.List, ast.Tuple)):
                walk(e, kind, (c, k), depth + 1)

    for a in call.args:
        if isinstance(a, (ast.List, ast.Tuple)):
            walk(a, 'positional', None, 1)
    for kw in call.keywords:
        if kw.arg and isinstance(kw.value, (ast.List, ast.Tuple)):
            walk(kw.value, 'keyword', None, 1)

    removals = {}                           # id(container) -> (container, the indices of the elements to take out)
    for name in names:
        found = call_done = False
        for c in containers:
            for i, n in enumerate(c['nodes']):
                if not (isinstance(n, ast.Name) and n.id == name):
                    continue
                if c['kind'] == 'call':
                    if call_done:
                        continue            # (the call's own arguments: one place, as it was)
                    call_done = True
                found = True
                removals.setdefault(id(c), (c, set()))[1].add(i)
        if not found:
            raise ValueError('%s is not one of the models %s is given' % (name, var))

    edits = []
    for depth in range(max(c['depth'] for c in containers), -1, -1):
        for c, idx in [e for e in removals.values() if e[0]['depth'] == depth]:
            nodes = c['nodes']
            if len(idx) < len(nodes):
                k = 0
                while k < len(nodes):
                    if k not in idx:
                        k += 1
                        continue
                    j = k
                    while j + 1 < len(nodes) and (j + 1) in idx:
                        j += 1
                    if j + 1 < len(nodes):
                        edits.append((start_of(nodes[k]), start_of(nodes[j + 1]), ''))
                    else:
                        edits.append((end_of(nodes[k - 1]), end_of(nodes[j]), ''))
                    k = j + 1
                rest = [n for i, n in enumerate(nodes) if i not in idx]
                if isinstance(c['node'], ast.Tuple) and len(rest) == 1:
                    edits.append((end_of(rest[0]), end_of(rest[0]), ','))      # (one element left: a tuple still)
            elif c['kind'] == 'keyword' and c['depth'] == 1 and isinstance(c['node'], ast.List):
                # nothing is left of a list that is what a keyword is given (`inlets=[a]`): the input is a placeholder again, `inlets=...`
                edits.append((start_of(c['node']), end_of(c['node']), '...'))
            elif c['kind'] != 'call':
                # nothing is left of the list: a placeholder is, as the call needs something there
                edits.append((start_of(c['node']), end_of(c['node']), '[...]' if isinstance(c['node'], ast.List) else '(...,)'))
            else:
                raise ValueError('%s is given nothing else: it cannot lose %s' % (var, ', '.join(names)))
    return edits


def arg_edits(arg):
    ''' Changing the models a call is given: the model tree's nesting, renesting and denesting are edits of the arguments.
        `arg` is JSON {source, statements: [{var, line, remove: [name, ...], insert: [{name, relative, side}, ...]}]}: for each
        statement (the `var = call(...)` on the 1-based `line`) the models taken out of the call and put into it -- `side` is
        "before" or "after" the model `relative` (a model of the call), or "end" (after the last one).  A model is a plain name
        written as an argument of the call, or in a list or tuple written in it.  The answer is JSON {"statements": [{line,
        end_line, text}, ...]}: the new text of the lines the statement is on (0-based, inclusive).  Raises ValueError with the
        reason when it cannot be done (the statement is not a call, a model is not one of its arguments, nothing would be left) '''
    req = json.loads(arg)
    source = req['source']
    tree = ast.parse(source)
    lines = source.split('\n')
    starts = [0]
    for text in lines[:-1]:
        starts.append(starts[-1] + len(text) + 1)

    def char_col(line, byte_col):
        return len(lines[line - 1].encode('utf-8')[:byte_col].decode('utf-8', 'replace'))

    def start_of(node):
        return starts[node.lineno - 1] + char_col(node.lineno, node.col_offset)

    def end_of(node):
        return starts[node.end_lineno - 1] + char_col(node.end_lineno, node.end_col_offset)

    out = []
    for want in req['statements']:
        var, line = want['var'], want['line']
        stmt = None
        for st in tree.body:
            targets = getattr(st, 'targets', None) or ([st.target] if hasattr(st, 'target') else [])
            if st.lineno == line and any(isinstance(t, ast.Name) and t.id == var for t in targets):
                stmt = st
                break
        if stmt is None or getattr(stmt, 'value', None) is None:
            raise ValueError('%s is not written as a call: edit the script' % var)
        call = stmt.value
        is_call = isinstance(call, ast.Call)
        if not is_call and (want.get('remove') or want.get('insert')):
            raise ValueError('%s is not written as a call: edit the script' % var)
        # (a model that is used in arithmetic, `x = a + b`, can only be replaced by a placeholder: `x = a + ...`)
        # the places a model can stand: the call's own arguments, and the lists and tuples written in them
        containers = [sorted(list(call.args) + list(call.keywords), key=lambda n: (n.lineno, n.col_offset))] if is_call else []
        for a in (call.args if is_call else []):
            if isinstance(a, (ast.List, ast.Tuple)):
                containers.append(list(a.elts))
        elements = []                       # (node, the nodes of its container), in the order of the script
        for nodes in containers:
            for n in nodes:
                if isinstance(n, ast.Name):
                    elements.append((n, nodes))
        elements.sort(key=lambda e: (e[0].lineno, e[0].col_offset))

        def find(name):
            for node, nodes in elements:
                if node.id == name:
                    return node, nodes
            raise ValueError('%s is not one of the models %s is given' % (name, var))

        edits = _arg_removals(call, want.get('remove', []), var, start_of, end_of) if want.get('remove') else []
        for name in want.get('hole', []):
            # (a model the call cannot do without is taken out: a placeholder `...` stands where it was, wherever it is written)
            nodes = [n for n in ast.walk(call) if isinstance(n, ast.Name) and n.id == name
                     and n is not getattr(call, 'func', None)]
            if not nodes:
                raise ValueError('%s is not used by %s' % (name, var))
            for n in nodes:
                edits.append((start_of(n), end_of(n), '...'))
                                            # (start, end, text) in offsets of the whole source
        for ins in want.get('insert', []):
            name, side, relative = ins['name'], ins.get('side', 'end'), ins.get('relative')
            if side == 'end' or not relative:
                if not elements:
                    raise ValueError('%s is given no model to put %s next to' % (var, name))
                last = elements[-1][0]
                edits.append((end_of(last), end_of(last), ', ' + name))
            else:
                node, _ = find(relative)
                if side == 'before':
                    edits.append((start_of(node), start_of(node), name + ', '))
                else:
                    edits.append((end_of(node), end_of(node), ', ' + name))
        first, last = stmt.lineno, stmt.end_lineno
        base = starts[first - 1]
        stop = starts[last - 1] + len(lines[last - 1])
        text = source[base:stop]
        # (from the end of the text to its beginning, so that the offsets stay what they were; at the same place the
        # removal first, then what is put in)
        for a, b, new in sorted(edits, key=lambda e: (-e[0], 0 if e[1] > e[0] else 1)):
            text = text[:a - base] + new + text[b - base:]
        out.append({'line': first - 1, 'end_line': last - 1, 'text': text})
    return json.dumps({'statements': out})


def blocks_files(_arg=''):
    ''' The blocks folder, then the block files in it, one per line: what the application watches for changes '''
    import os
    from fieldes import blocks
    folder = blocks.folder()
    out = [folder]
    if os.path.isdir(folder):
        out += [os.path.join(folder, n) for n in sorted(os.listdir(folder)) if n.endswith('.py') and not n.startswith('_')]
    return '\n'.join(out)


def block_save(request):
    ''' File > Save as custom block, with the name the user typed: `request` is JSON {"source": the script, "name": the name asked for ("" = the one the script suggests), "suggest": true to only say
        what the name would be}.  The answer is the one of save_as_block; with "suggest" it is {"name"} and nothing is written '''
    import json
    r = json.loads(request)
    return save_as_block(r.get('source', ''), name=r.get('name') or None, suggest=bool(r.get('suggest')))


def save_as_block(source, name=None, suggest=False):
    ''' File > Save as custom block: the script as a function in a file of the blocks folder (see fieldes.blocks), so that the whole model is one call in any script and an entry of the right-click
        menus.  The statements that make the model are the body; the imports of the library, the settings of the view and the lines that only show a name are not; the model that is shown (the last
        name on a line of its own) is what the function returns.  A block this made earlier is updated, any other file is left alone.  The name of the block -- its file and its function -- is the one the
        user typed (a word of letters, digits and underscores), or the name of the model with _block after it.  JSON: {"path", "name"} or {"error"} '''
    import ast
    import json
    import os
    import re
    from fieldes import blocks
    try:
        tree = ast.parse(source)
    except SyntaxError as e:
        return json.dumps({'error': 'The script has an error in line %s: fix it first.' % (e.lineno,)})
    lines = source.split('\n')
    drop = set()
    shown = last = None
    for node in tree.body:
        span = range(node.lineno - 1, node.end_lineno)
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            if getattr(node, 'module', None) == 'fieldes' or 'fieldes' in [a.name for a in node.names]:
                drop.update(span)
        elif isinstance(node, ast.Expr):
            v = node.value
            if isinstance(v, ast.Name):
                shown = v.id
                drop.update(span)
            elif isinstance(v, ast.Call) and isinstance(v.func, ast.Attribute) and isinstance(v.func.value, ast.Name) and v.func.value.id == 'view':
                drop.update(span)
        elif isinstance(node, ast.Assign):
            for t in node.targets:
                if isinstance(t, ast.Name):
                    last = t.id
    result = shown or last
    if not result:
        return json.dumps({'error': 'The script makes no model: there is nothing to save as a block.'})
    body = [lines[i] for i in range(len(lines)) if i not in drop and not re.match(r'\s*#\s*hidden:', lines[i])]
    while body and (not body[0].strip() or body[0].lstrip().startswith('#')):
        del body[0]
    while body and not body[-1].strip():
        del body[-1]
    text = re.sub(r'\bvar\(\s*(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\s*\)', r'\1', '\n'.join(body))      # (a number dragged with a gizmo is a number in a block)
    suggested = re.sub(r'\W+', '_', result).strip('_') or 'model'
    if suggested[0].isdigit():
        suggested = 'm_' + suggested
    suggested += '_block'
    if suggest:
        return json.dumps({'name': suggested})
    # (the name the user typed: every run of anything but a letter, a digit or an underscore becomes an underscore, and a name that starts with a digit or is nothing gets the suggestion's way out)
    name = re.sub(r'\W+', '_', name).strip('_') if name else ''
    if not name:
        name = suggested
    elif name[0].isdigit():
        name = 'm_' + name
    marker = '# Saved from a script by File > Save as custom block.'
    try:
        folder = blocks.folder()
        os.makedirs(folder, exist_ok=True)
        base, k = name, 1
        while True:
            path = os.path.join(folder, name + '.py')
            if not os.path.exists(path):
                break
            with open(path, encoding='utf-8') as f:
                first = f.readline()
            if first.startswith(marker):
                break                                       # (a block this made earlier: it is updated)
            k += 1
            name = '%s_%d' % (base, k)
        indented = '\n'.join(('    ' + l) if l.strip() else '' for l in text.split('\n'))
        code = ('%s  Edit it as you like: every function in a file of this folder is a block, and is there in every script.\n'
                'def %s():\n    \'\'\' %s, saved from a script: one call makes the whole model \'\'\'\n%s\n    return %s\n') % (marker, name, result, indented, result)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(code)
    except OSError as e:
        return json.dumps({'error': 'The block could not be written: %s.  The environment variable FIELDES_BLOCKS names another folder.' % (e,)})
    return json.dumps({'path': path, 'name': name})


def blocks_used(source):
    ''' '1' when the script uses a custom block (one of the folder now, or one that was there: its file may be gone or
        broken), else '': a script that does is run again when a block file changes '''
    import ast
    from fieldes import blocks
    names = set(blocks.names()) | set(blocks._EVER)
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return ''
    return '1' if any(isinstance(n, ast.Name) and n.id in names for n in ast.walk(tree)) else ''


_console = {'base': None, 'mine': {}}      # (the names of the library, and what was assigned in the console)


def terminal_exec(request):
    ''' The console of the editor runs what was typed: `request` is JSON {"code": text}, the answer JSON {"output": what was printed, "value": the repr of the value of the last
        expression (or ""), "error": the last lines of the traceback of an error (or "")}.  It runs in the names of the script that ran last, the library's (`from fieldes import *`,
        as a script has it) and what was assigned in the console before -- a copy of them: a variable made or changed in the console is the console's own, the script and its tree
        are not touched.  Like Jupyter: the last expression of a block is shown '''
    import ast
    import contextlib
    import io
    import json
    import traceback
    from fieldes import runner
    code = json.loads(request).get('code', '')
    if _console['base'] is None:
        base = {'__name__': '__console__'}
        exec('from fieldes import *', base)
        _console['base'] = base
    glob = runner.last_globals if isinstance(runner.last_globals, dict) else {}
    ns = dict(_console['base'])
    ns.update(glob)
    ns.update(_console['mine'])
    out = io.StringIO()
    value, error = '', ''
    try:
        tree = ast.parse(code, '<console>', 'exec')
        last = None
        if tree.body and isinstance(tree.body[-1], ast.Expr):
            last = ast.Expression(tree.body.pop().value)
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            exec(compile(tree, '<console>', 'exec'), ns)
            if last is not None:
                result = eval(compile(last, '<console>', 'eval'), ns)
                if result is not None:
                    value = repr(result)
    except SystemExit:
        error = 'exit() is not available here: the application is not closed from the console'
    except BaseException:
        lines = traceback.format_exc().splitlines()
        # (the frames of this function are not the user's: the traceback from the line of the console on)
        start = next((i for i, l in enumerate(lines) if l.startswith('  File "<console>"')), None)
        error = '\n'.join(lines[start:] if start is not None else lines[-3:])
    # what was assigned or changed in the console is kept; the names of the library and of the script as they were are not
    for k, v in ns.items():
        if k in ('__builtins__', '__name__'):
            continue
        if (k not in glob or glob[k] is not v) and (k not in _console['base'] or _console['base'][k] is not v):
            _console['mine'][k] = v
    return json.dumps({'output': out.getvalue(), 'value': value, 'error': error})


def feature_near(request):
    ''' What a right-click on a body offers near the place it was made: the corner and the edge of the body nearest that place (see fieldes.stdlib.features), as JSON
        {"corner": [x, y, z] or null, "edge": {"point": [x, y, z], "straight": [[x, y, z], [x, y, z]] or null} or null}.  `request` is JSON {"var": the name of
        the body in the script that ran last, "point": [x, y, z] where the click was, "reach": the mm that about thirty pixels span there}.  An empty answer
        ({}) when the body is not one a variable holds, or has no extent to work from, or the search fails: the menu then offers nothing of this kind '''
    import json
    try:
        r = json.loads(request)
        from fieldes import runner
        from fieldes.shape import Shape
        from fieldes.stdlib import features
        from fieldes.stdlib.fields import _shape_bounds, _eval_many
        shape = runner.last_globals.get(r['var'])
        if not isinstance(shape, Shape):
            return '{}'
        lo, hi = _shape_bounds(shape, None)
        size = max(hi[i] - lo[i] for i in range(3)) or 1.0
        found = features.features_near(lambda points: _eval_many(shape, points), tuple(float(c) for c in r['point']), float(r['reach']), size)
        return json.dumps(found)
    except Exception:
        return '{}'


def snap_placement(request):
    ''' S: where the body the selected features are on has to be placed to be snapped onto the other (see fieldes.stdlib.snapping), as the numbers FielDes writes into its
        `position=` and `rotation=`.  `request` is JSON {"features": [the names of the selected faces, edges and points in the order they were selected], "body": the name of the
        body that moves (the first feature's), "written": [the placement arguments the call of the body has], "source": the script}.  The answer is JSON
        {"position": "(var(x), var(y), var(z))", "rotation": "(var(x), var(y), var(z))" (the text of the two arguments, in mm and degrees), "scale": whether the call may be given a
        scale, "edits": [[line0, col0, line1, col1, text]] (the places of the features that were made on the body, moved with it), "left": [the features on the body whose place is
        not written as numbers]}, or {"error": why it cannot be done} '''
    import json
    from fieldes import runner
    from fieldes.stdlib import snapping
    req = json.loads(request)
    gs = runner.last_globals if isinstance(runner.last_globals, dict) else {}
    names = [str(n) for n in req.get('features', [])]
    try:
        absent = [n for n in names + [req['body']] if n not in gs]
        if absent:
            raise ValueError('%s %s not in the last run of the script: run it again first' % (', '.join(absent), 'is' if len(absent) == 1 else 'are'))
        found = snapping._placement([gs[n] for n in names], req.get('written') or ())
        if snapping._body_key(gs[req['body']]) != snapping._body_key(found['body']):
            raise ValueError('the first of the selected features is not on %s but on another body' % req['body'])
        edits, left = snapping.feature_edits(req['source'], req['body'], found['motion'])
    except ValueError as e:
        return json.dumps({'error': str(e)})
    def written(values):
        # (as the gizmo's numbers are written: var() numbers, which its drag changes in the text)
        return '(%s)' % ', '.join('var(%s)' % (('%.8f' % v).rstrip('0').rstrip('.') if abs(v) >= 5e-9 else '0') for v in values)
    return json.dumps({'position': written(found['position']), 'rotation': written(found['rotation']), 'scale': found['scale'], 'edits': edits, 'left': left})


def origin_placement(request):
    ''' O: a point of the scene made the origin of a model -- the point it is turned and scaled about and where its gizmo is (`origin=`, see fieldes.stdlib.placement) -- while the model stays where it is.
        `request` is JSON {"body": the name of the model, "point": the name of the point}.  A model is placed by  p -> c + R S (p - c) + m  (c the origin, S the scale, R the rotation, m the position):
        with the origin c' at the point P the gizmo is at, c' + m' = P, and the model stays where it is when  m' = (R S)^-1 (m + (I - R S)(c - P)) -- the position the model has now when it is neither turned nor
        scaled.  The answer is JSON {"origin": "(x, y, z)", "position": "(var(x), var(y), var(z))", "position_changed": whether the position has to be written again}, or {"error": why not} '''
    import json
    from fieldes import runner
    req = json.loads(request)
    gs = runner.last_globals if isinstance(runner.last_globals, dict) else {}
    try:
        body, point = gs.get(req['body']), gs.get(req['point'])
        absent = [n for n, v in ((req['body'], body), (req['point'], point)) if v is None]
        if absent:
            raise ValueError('%s %s not in the last run of the script: run it again first' % (', '.join(absent), 'is' if len(absent) == 1 else 'are'))
        xyz = getattr(point, 'xyz', None)
        if xyz is None:
            raise ValueError('%s is not a point' % req['point'])
        from fieldes.stdlib.curves import value_of
        from fieldes.stdlib.snapping import _numbers, _pivot
        from fieldes.stdlib.symmetries import _turn_matrix
        P = tuple(float(value_of(v)) for v in xyz)
        args = getattr(body, '_placement_args', None)
        parts = getattr(body, '_placed_parts', None)
        if args is not None and parts is not None:
            m, r = _numbers(args[0], (0.0, 0.0, 0.0)), _numbers(args[1], (0.0, 0.0, 0.0))
            s = args[2]
            s = (1.0, 1.0, 1.0) if s is None else (tuple(float(value_of(s)) for _ in range(3)) if not isinstance(s, (tuple, list)) else _numbers(s, (1.0, 1.0, 1.0)))
            c = tuple(float(v) for v in parts[1])
        else:
            m, r, s, c = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), tuple(float(v) for v in _pivot(body))
        if any(abs(v) < 1e-9 for v in s):
            raise ValueError('%s is scaled to nothing in one direction: it has no origin to change' % req['body'])
        R = [[float(v) for v in row] for row in _turn_matrix(r)]
        A = [[R[i][j] * s[j] for j in range(3)] for i in range(3)]
        d = [c[i] - P[i] for i in range(3)]
        rhs = [m[i] + d[i] - sum(A[i][j] * d[j] for j in range(3)) for i in range(3)]
        # (A = R S with R a turn: its inverse is S^-1 R^T)
        m2 = [sum(R[j][i] * rhs[j] for j in range(3)) / s[i] for i in range(3)]
        o2 = [P[i] - m2[i] for i in range(3)]
    except (ValueError, TypeError, KeyError) as e:
        return json.dumps({'error': str(e)})
    def num(v):
        return ('%.8f' % v).rstrip('0').rstrip('.') if abs(v) >= 5e-9 else '0'
    changed = any(abs(m2[i] - m[i]) > 1e-7 for i in range(3))
    return json.dumps({'origin': '(%s)' % ', '.join(num(v) for v in o2), 'position': '(%s)' % ', '.join('var(%s)' % num(v) for v in m2), 'position_changed': changed})


def handles_text(name):
    ''' The statement that makes the surfaces of the shape a script variable holds (as of the last run)
        draggable -- "name = handles(name, [var(...), ...])" -- for FielDes to write under its definition.  It is ONE line, whatever the number of
        numbers (the exception to the layout of every other call): nobody edits those numbers by hand, they are dragged, and the call stays out of the
        way in the editor.  Raises ValueError with the reason when it cannot be done. '''
    from fieldes import runner
    from fieldes.shape import Shape
    from fieldes.stdlib.handles import handle_values
    shape = runner.last_globals.get(name)
    if not isinstance(shape, Shape):
        raise ValueError('%s is not a shape of the last run' % name)
    # (the variable may hold the shape under a placement -- name = place(name, ...): its own numbers are
    # the shape it was made from, whose definition the numbers of its surfaces go under)
    while getattr(shape, '_gizmo', None) is not None and isinstance(getattr(shape, '_placed_from', None), Shape):
        shape = shape._placed_from
    if getattr(shape, '_locks', None):
        raise ValueError('%s has an excluded part: dragging its surfaces would move that too (use the gizmo)' % name)
    values = handle_values(shape)
    if not values:
        raise ValueError('%s has no numbers that place its surfaces to make handles of' % name)
    if len(values) > _MAX_HANDLES:
        raise ValueError('%s has %d numbers that place its surfaces: too many to write into the script '
                         '(the limit is %d)' % (name, len(values), _MAX_HANDLES))
    return '%s = handles(%s, [%s])' % (name, name, ', '.join('var(%s)' % v for v in values))


def _handle_count(shape):
    ''' How many numbers place the shape's surfaces (what handles() would write), 0 when there are none or it cannot be
        done (a shape under a gizmo is asked about the shape it was made from, as handles_text does) '''
    try:
        from fieldes.shape import Shape
        from fieldes.stdlib.handles import _handle_count
        while getattr(shape, '_gizmo', None) is not None and isinstance(getattr(shape, '_placed_from', None), Shape):
            shape = shape._placed_from
        if getattr(shape, '_locks', None) or getattr(shape, '_kind', None) in ('point', 'field', 'const') or getattr(shape, '_editable', False):
            return 0                # (a point is moved by its gizmo, not by pulling the surface of its ball; a field has no
                                    # surface to pull: its numbers are written in the script, or moved by its gizmo)
        return int(_handle_count(shape))
    except Exception:
        return 0


def _handles_ends_only(shape):
    ''' Whether the numbers handles() would write for the shape are only the two ends of an extrusion of a 2D shape that has handles of its own (profile.extrude): the sides of the extrusion
        are pulled by the numbers of that shape.  A shape under a gizmo is asked about the shape it was made from, as _handle_count does -- the model tree measures the value that is displayed '''
    try:
        from fieldes.shape import Shape
        while getattr(shape, '_gizmo', None) is not None and isinstance(getattr(shape, '_placed_from', None), Shape):
            shape = shape._placed_from
        return bool(getattr(shape, '_handles_ends_only', False))
    except Exception:
        return False


def _new_name(value, taken):
    ''' A free variable name for the shape of an expression: "sphere_1" for sphere(...) '''
    base = 'shape'
    if isinstance(value, ast.Call) and isinstance(value.func, ast.Name) and value.func.id.isidentifier():
        base = value.func.id
    k = 1
    while '%s_%d' % (base, k) in taken:
        k += 1
    name = '%s_%d' % (base, k)
    taken.add(name)
    return name


def _is_number_node(node):
    ''' A number written in the script: 5, -2.5, or var(5) '''
    if isinstance(node, ast.Constant):
        return isinstance(node.value, (int, float)) and not isinstance(node.value, bool)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return _is_number_node(node.operand)
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == 'var' and len(node.args) == 1:
        return _is_number_node(node.args[0])
    return False


_SHADOW_RE = re.compile(r'#\s*shadow:\s*([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)')


def shadow_names(lines):
    ''' The names the `# shadow: a, b` comments of these lines of a statement list: models the statement holds a reference to
        without being their first user (what a Ctrl+drag of a model that nothing else uses writes) '''
    out = set()
    for line in lines:
        for m in _SHADOW_RE.finditer(line):
            out.update(n.strip() for n in m.group(1).split(','))
    return out


# The fields of the last scene: [(key, Shape)] -- the variable's name, or 'line:N' for a field that is only displayed -- which the
# application gives to the section viewer (a field is not drawn: it is shown there when it is selected)
_FIELD_SOURCES = []


def _origin_numbers(origin):
    ''' Where a field is "about" as three numbers, or None: a position, a point (where it is now), or the middle of a body '''
    import numbers
    import time
    try:
        if origin is None:
            return None
        pos = getattr(origin, '_point_pos', None)       # (a point: where it is now, even when its coordinates are var()s)
        if pos:
            return [float(c) for c in pos]
        if hasattr(origin, 'xyz'):
            origin = origin.xyz
        if isinstance(origin, (tuple, list)):
            if len(origin) == 3 and all(isinstance(c, numbers.Number) for c in origin):
                return [float(c) for c in origin]
            return None
        box = _estimate_bounds([origin], time.time() + 0.4)
        if box:
            return [(box[0][i] + box[1][i]) / 2.0 for i in range(3)]
    except Exception:
        pass
    return None


def field_sources():
    ''' The fields of the last scene, for the field viewers: [(key, Shape, origin, shown)] -- origin is where the field is about, as three
        numbers (the point or the middle of the body it was made from), or None (its icon is at the origin); shown is its eye (the script
        displays it).  An app's viewport adds the fields it is given '''
    out = []
    seen = set()
    try:
        from fieldes import app as _app
        in_app = _app.in_app()
    except Exception:
        _app, in_app = None, False
    # (in an app only the viewport shows fields: the ones the script displays by their names are for the editor)
    for key, shape, shown in ([] if in_app else _FIELD_SOURCES):
        origin = getattr(shape, '_field_origin', None)
        out.append((key, shape, _origin_numbers(origin), bool(shown)))
        seen.add(key)
    try:
        for key, shape in _app._viewport_fields():
            if key in seen:
                continue
            origin = getattr(shape, '_field_origin', None)
            out.append((key, shape, _origin_numbers(origin), True))
    except Exception:
        pass
    return out


# Parameters that are whole numbers (a count, a seed ...): a field cannot be one, so a dropped field never takes their place
_DISCRETE = re.compile(r'^(n|nx|ny|nz|n_\w+|\w+_count|count|counts|num\w*|steps|seed|octaves|neighbou?rs|segments|sides|iterations|'
                       r'index|samples|levels|resolution)$')

# Tuples that are not positions (a size, a direction, a range ...): a dropped point never takes their place
_NOT_POSITION = re.compile(r'^(size|sizes|scale|delta|spacing|period|dimensions|extent|direction|normal|norm|build_direction|axis|'
                           r'offset|input_range|output_range|range|lo|hi|low|high|min|max|bounds|cell_size|thickness)$')


def _base_label(label):
    ''' `upper` for `upper[2]` '''
    return label.split('[')[0]


def _numbers_of(value, gs):
    ''' The numbers a call is written with, for the model tree -- what dropping a FIELD on it can replace (anywhere a number
        goes, a field goes): [{label, span, text}] -- the label is the parameter's name when it is known (`thickness`,
        `upper[2]` for the third number of a point written as a tuple) '''
    names = []
    f = value.func
    callee = gs.get(f.id) if isinstance(f, ast.Name) else None
    if callable(callee):
        try:
            names = [p.name for p in inspect.signature(callee).parameters.values()
                     if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD)]
        except (TypeError, ValueError):
            pass
    out = []

    def add(node, label):
        if _is_number_node(node) and node.lineno == node.end_lineno and len(out) < 24:
            span = _span(node)
            lines = _SPAN_LINES
            text = lines[node.lineno - 1][span[1]:span[3]] if 0 < node.lineno <= len(lines) else ''
            entry = {'label': label, 'span': span, 'text': text}
            if _DISCRETE.match(_base_label(label)):
                entry['discrete'] = True            # (a count: a field cannot take its place)
            out.append(entry)

    for i, a in enumerate(value.args):
        label = names[i] if i < len(names) else 'argument %d' % (i + 1)
        if isinstance(a, (ast.List, ast.Tuple)):
            for k, e in enumerate(a.elts):
                add(e, '%s[%d]' % (label, k))
        elif not isinstance(a, ast.Starred):
            add(a, label)
    for kw in value.keywords:
        if kw.arg:
            add(kw.value, kw.arg)
    return out


def _points_of(value, gs):
    ''' The positions a call is written with as tuples of numbers -- `distance_to_point((0, 0, 0))`, `center=(5, 0, 0)` --
        for the model tree: what dropping a POINT on the model can take the place of (a point reads as its coordinates
        wherever a position goes): [{label, span, text}], the label is the parameter's name when it is known '''
    names = []
    f = value.func
    callee = gs.get(f.id) if isinstance(f, ast.Name) else None
    if callable(callee):
        try:
            names = [p.name for p in inspect.signature(callee).parameters.values()
                     if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD)]
        except (TypeError, ValueError):
            pass
    out = []

    scaling = _callee_name(value).startswith(('scale', 'shear', 'taper'))

    def add(node, label):
        if (isinstance(node, ast.Tuple) and len(node.elts) in (2, 3) and all(_is_number_node(e) for e in node.elts)
                and node.lineno == node.end_lineno and len(out) < 8
                and not scaling and not _NOT_POSITION.match(_base_label(label))):
            span = _span(node)
            lines = _SPAN_LINES
            text = lines[node.lineno - 1][span[1]:span[3]] if 0 < node.lineno <= len(lines) else ''
            out.append({'label': label, 'span': span, 'text': text, 'dims': len(node.elts)})

    for i, a in enumerate(value.args):
        add(a, names[i] if i < len(names) else 'argument %d' % (i + 1))
    for kw in value.keywords:
        if kw.arg:
            add(kw.value, kw.arg)
    return out


def _callee_name(value):
    ''' The name of the function a call statement calls (`offset` in `x = offset(a, 1)`), for the model tree to say why '''
    if isinstance(value, ast.Call):
        f = value.func
        if isinstance(f, ast.Name):
            return f.id
        if isinstance(f, ast.Attribute):
            return f.attr
    return ''


def _inputs_of(value, gs, by_name, own=None):
    ''' The models a call is given as its inputs, for the model tree -- what dropping a model on it can add or replace:
        ([{name, span, index | keyword, in_list}], variadic, numbers).  An input is a plain name that is a model of the script
        (a name inside a list written in the call counts, as in union_all([a, b])); `variadic` says that the function
        takes any number of them (union, difference, ...), so another can be added; `numbers` are the numbers the call is
        written with (see _numbers_of): a field dropped on the model can take the place of one '''
    if not isinstance(value, ast.Call):
        return [], False, []
    found = []

    def add(arg, index, keyword, in_list):
        if isinstance(arg, ast.Name) and arg.id in by_name and arg.id != own:
            d = {'name': arg.id, 'span': _span(arg)}
            if keyword is not None:
                d['keyword'] = keyword
            else:
                d['index'] = index
            if in_list:
                d['in_list'] = True
            found.append(d)

    for i, a in enumerate(value.args):
        if isinstance(a, (ast.List, ast.Tuple)):
            for e in a.elts:
                add(e, i, None, True)
        elif not isinstance(a, ast.Starred):
            add(a, i, None, False)
    for kw in value.keywords:
        if kw.arg:
            add(kw.value, None, kw.arg, False)
    # A model used deeper in the call (`offset(move(c, (1, 0, 0)), 2)`) can be replaced too, though it is no argument of it
    seen = {tuple(d['span']) for d in found}
    for arg in list(value.args) + [kw.value for kw in value.keywords]:
        for n in ast.walk(arg):
            if isinstance(n, ast.Name) and n.id in by_name and n.id != own and isinstance(n.ctx, ast.Load):
                span = _span(n)
                if tuple(span) not in seen:
                    seen.add(tuple(span))
                    found.append({'name': n.id, 'span': span, 'deep': True})
    # A name written as an element of a list or tuple that the call is given -- positionally ([push, pull]) or as the value of a keyword
    # (loads=[a, b]), or in a list inside such a list -- can be taken out of that list while the call keeps the rest: `listed`.
    # `list_size` is how many elements that list has, `list_keyword` whether it is the value of a keyword (such a list may be left empty)
    listed = {}

    def note(lst, keyword):
        for e in lst.elts:
            if isinstance(e, ast.Name):
                listed[tuple(_span(e))] = (len(lst.elts), keyword)
            elif isinstance(e, (ast.List, ast.Tuple)):
                note(e, keyword)

    for a in value.args:
        if isinstance(a, (ast.List, ast.Tuple)):
            note(a, False)
    for kw in value.keywords:
        if kw.arg and isinstance(kw.value, (ast.List, ast.Tuple)):
            note(kw.value, True)
    for d in found:
        info = listed.get(tuple(d['span']))
        if info:
            d['listed'] = True
            d['list_size'] = info[0]
            d['list_keyword'] = info[1]
    variadic = False
    f = value.func
    callee = gs.get(f.id) if isinstance(f, ast.Name) else None
    if callable(callee):
        try:
            variadic = any(p.kind == p.VAR_POSITIONAL for p in inspect.signature(callee).parameters.values())
        except (TypeError, ValueError):
            pass
    return found, variadic, _numbers_of(value, gs)


def _region_args_of(call, src):
    ''' The arguments of a condition's call that are its regions -- the leading positional ones that are not numbers, tuples of numbers or
        keywords -- as [{span, text}] '''
    out = []
    for a in call.args:
        literal = isinstance(a, (ast.Constant, ast.Tuple)) or (isinstance(a, ast.UnaryOp) and isinstance(a.operand, ast.Constant))
        if literal or isinstance(a, ast.Starred):
            break
        out.append({'span': _span(a), 'text': src.segment(a)})
    return out


def _condition_role(value):
    ''' What a condition is, for a drop on the call that takes it: the name of the input of an analysis it goes in -- `supports`, `loads`,
        `fixed_temperatures`, `inlets` ... (every kind of condition has an input of its own); '' for anything else '''
    try:
        from fieldes.stdlib.fea import _slot_of
        return _slot_of(value) or ''
    except Exception:
        return ''


def _min_inputs(callee):
    ''' How many models a call of this function needs at the least, from its signature (nothing is listed by hand, so a new function is known
        too): the positional parameters it cannot do without -- `difference(a, b, *rest)` needs two, `union(a, *rest)` one -- and at least one
        for a function that takes any number of models.  A model taken out of a call that has no more than this leaves a placeholder instead '''
    needed = 0
    try:
        for p in inspect.signature(callee).parameters.values():
            if p.kind == p.VAR_POSITIONAL:
                break
            if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD) and p.default is p.empty:
                needed += 1
    except (TypeError, ValueError):
        pass
    return max(1, needed)


PLACEMENT_ARGS = ('position', 'rotation', 'scale', 'alpha', 'origin')


def _keyword_span(kw):
    ''' The span of a keyword argument, `name=value` (ast gives it a place of its own) '''
    return [kw.lineno, _char_col(kw.lineno, kw.col_offset), kw.end_lineno, _char_col(kw.end_lineno, kw.end_col_offset)]


def _is_placeable(callee):
    ''' Whether calls of this function take the placement arguments (see fieldes.stdlib.placement) '''
    return bool(getattr(callee, '_placeable', False))


def _placement_args(call, first, src):
    ''' The placement arguments written in a call: ({name: {span (of the value), arg_span (of `name=value`), text}}, whether a number of position, rotation or scale is a var()).
        `first`: how many positional arguments are not placement (the call of `place` has one: what is placed) '''
    args, numbers = {}, False
    for i, a in enumerate(call.args[first:]):
        if i < len(PLACEMENT_ARGS) and not isinstance(a, ast.Starred):
            args[PLACEMENT_ARGS[i]] = {'span': _span(a), 'arg_span': _span(a), 'text': _short(src.segment(a), 80)}
            numbers = numbers or (PLACEMENT_ARGS[i] != 'alpha' and any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var' for n in ast.walk(a)))
    for kw in call.keywords:
        if kw.arg in PLACEMENT_ARGS:
            args[kw.arg] = {'span': _span(kw.value), 'arg_span': _keyword_span(kw), 'text': _short(src.segment(kw.value), 80)}
            numbers = numbers or (kw.arg != 'alpha' and any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var' for n in ast.walk(kw.value)))
    return args, numbers


def _placement_of(value, gs, src):
    ''' How the placement of a model is written in its definition `value` -- the call that makes it (the arguments position, rotation, scale and alpha, see
        fieldes.stdlib.placement): {form: 'call' when the call has some, else 'none', placeable: whether this call can take them, call: the span of the call,
        args: {name: {span, arg_span, text}}, has_numbers: whether the numbers of position, rotation or scale are var()s (the gizmo drags them)}.  A definition that is not a
        call of a function that takes them is placed by a statement of `place` after it (see _placement_statement) '''
    out = {'form': 'none', 'placeable': False, 'args': {}, 'has_numbers': False}
    if not isinstance(value, ast.Call):
        return out
    f = value.func
    callee = gs.get(f.id) if isinstance(f, ast.Name) else None
    out['call'] = _span(value)
    if not _is_placeable(callee):
        return out
    out['placeable'] = True
    out['args'], out['has_numbers'] = _placement_args(value, len(value.args), src)
    if out['args']:
        out['form'] = 'call'
    return out


def _placement_statement(stmt, src):
    ''' The statement `x = place(x, position=..., rotation=..., scale=..., alpha=...)` that places a model that is not made by a call that takes them: the same description as
        _placement_of gives, with the lines of the statement '''
    call = stmt.value
    args, numbers = _placement_args(call, 1, src)
    return {'form': 'statement', 'placeable': True, 'args': args, 'has_numbers': numbers, 'call': _span(call), 'line': stmt.lineno, 'end_line': stmt.end_lineno,
            'text': src.segment(stmt)}


_PLACEMENT_SHOWN = (('position', '(0, 0, 0)'), ('rotation', '(0, 0, 0)'), ('scale', '1'), ('alpha', '1'))


def _placement_rows(placement, symmetry=False):
    ''' The placement of a model as rows of the model tree: always the same four -- position, rotation, scale, alpha --, the ones the code writes with what they are written as,
        the others with their defaults (`default`: the tree shows them dimmed, and a click writes them).  A symmetry has no scale '''
    written = (placement or {}).get('args') or {}
    rows = []
    for name, default in _PLACEMENT_SHOWN:
        if name == 'scale' and symmetry:
            continue
        if name in written:
            rows.append({'param': name, 'text': written[name]['text'], 'span': written[name]['span'], 'kind': 'expression'})
        else:
            rows.append({'param': name, 'text': default, 'span': [], 'kind': 'expression', 'default': True})
    if 'origin' in written:                  # (the fifth argument is a row only when it is written: the middle of the box is what it is without it)
        rows.append({'param': 'origin', 'text': written['origin']['text'], 'span': written['origin']['span'], 'kind': 'expression'})
    return rows


def _call_slots(value, gs, src):
    ''' What a call is given, argument by argument, and what it could be given: {params: [the names of the callee's
        parameters], args: {parameter: {span, text, list (a list written in the call: its `last` element's span, or none
        when it is empty)}}, last: the span of the call's last argument} -- for the model tree: a material or a condition
        dropped on the call goes in place of the argument it has, or becomes the one it has not '''
    if not isinstance(value, ast.Call):
        return {}
    f = value.func
    callee = gs.get(f.id) if isinstance(f, ast.Name) else None
    params = []
    positional = []
    if callable(callee):
        try:
            for p in inspect.signature(callee).parameters.values():
                if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD):
                    positional.append(p.name)
                if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD, p.KEYWORD_ONLY) and not (_is_placeable(callee) and p.name in PLACEMENT_ARGS):
                    params.append(p.name)           # (the placement arguments are not for a model to be dropped on)
        except (TypeError, ValueError):
            pass
    given = {}
    last = None

    def note(name, node):
        nonlocal last
        span = _span(node)
        d = {'span': span, 'text': _short(src.segment(node), 60)}
        if isinstance(node, ast.List):
            d['text'] = _short(src.segment(node), 2000)         # (the drop reads the names in it: not cut short)
            d['list'] = True
            if node.elts:
                d['last'] = _span(node.elts[-1])
        if name:
            given[name] = d
        if last is None or (span[2], span[3]) > (last[2], last[3]):
            last = span

    for i, a in enumerate(value.args):
        if isinstance(a, ast.Starred):
            continue
        note(positional[i] if i < len(positional) else None, a)
    for kw in value.keywords:
        if kw.arg and not (kw.arg in PLACEMENT_ARGS and _is_placeable(callee)):
            note(kw.arg, kw.value)
    out = {'params': params, 'args': given}
    if last is not None:
        out['last'] = last
    return out


def export_model(arg):
    ''' A one-off export asked for by a menu: JSON {kind, var, path, ...} -> JSON {path, text}.  `kind` is "mesh" (a body as a mesh file), "fea" (the
        finite-element model of a static analysis), "results" (what an analysis found) or "field" (a field on a grid): the model is the
        variable `var` of the script that ran last, and the file is written by fieldes.stdlib.exporting -- the same functions a script calls '''
    import os
    from fieldes import runner
    from fieldes.stdlib import exporting as E
    req = json.loads(arg)
    var, path, kind = req['var'], req['path'], req['kind']
    glob = runner.last_globals if isinstance(runner.last_globals, dict) else {}
    if var not in glob:
        raise RuntimeError('%s is not a model of the script that ran last: run the script, then export' % var)
    value = glob[var]
    try:
        if kind == 'mesh':
            try:
                E.export_mesh(value, path)
            except ValueError as e:
                # (a body whose extent is not known -- built from fields: the region of the scene is meshed)
                host = E._host('__bounds', None)
                if host is None or 'extent' not in str(e):
                    raise
                E.export_mesh(value, path, bounds=host)
        elif kind == 'fea':
            E.export_fea(value, path)
        elif kind == 'results':
            E.export_results(value, path)
        elif kind == 'field':
            host = E._host('__bounds', None)
            E.export_field(value, path, bounds=host)
        elif kind == 'meshfile':
            value.export(path)              # (a mesh of mesh_volume or mesh_surface: the file's format is its extension)
        else:
            raise ValueError('unknown kind of export %r' % kind)
    except Exception as e:
        raise RuntimeError(str(e))
    size = os.path.getsize(path) if os.path.exists(path) else 0
    return json.dumps({'path': path, 'text': '%s (%s)' % (path, ('%.1f MB' % (size / 1e6)) if size > 1e6 else ('%.0f KB' % (size / 1e3)))})


def convert_mesh(arg):
    ''' The viewport's meshes saved as STL, written in another format: JSON {source, target} -> the number of triangles '''
    from fieldes.stdlib import meshfiles
    req = json.loads(arg)
    return str(meshfiles.convert_mesh(req['source'], req['target']))


def _arg_kind(node):
    ''' What an argument is written as, for the model tree's argument rows: number, string, constant (True, None ...), tuple, list, name
        (of something that is not a model), call, or expression '''
    if isinstance(node, ast.Constant):
        v = node.value
        if isinstance(v, bool) or v is None:
            return 'constant'
        if isinstance(v, (int, float)):
            return 'number'
        if isinstance(v, str):
            return 'string'
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)) and isinstance(node.operand, ast.Constant) \
            and isinstance(node.operand.value, (int, float)) and not isinstance(node.operand.value, bool):
        return 'number'
    if isinstance(node, ast.Tuple):
        return 'tuple'
    if isinstance(node, ast.List):
        return 'list'
    if isinstance(node, ast.Name):
        return 'name'
    if isinstance(node, ast.Call):
        return 'call'
    return 'expression'


def _args_of(value, gs, by_name, src):
    ''' Every argument a call is written with, in the order written, for the model tree: [{param, text, span, kind}].  The tree shows each
        one as a row under the statement, as the code reads: a click on it selects it in the code, ready to be replaced.  A model that is
        an argument (a name of a model of the script, or a list of them) is left out: it is a row of its own -- a child of the statement
        or a shadow of it -- and so is a placeholder (`...`) '''
    if not isinstance(value, ast.Call):
        return []
    f = value.func
    callee = gs.get(f.id) if isinstance(f, ast.Name) else None
    positional = []
    if callable(callee):
        try:
            for p in inspect.signature(callee).parameters.values():
                if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD):
                    positional.append(p.name)
        except (TypeError, ValueError):
            pass

    def only_models(node):
        if isinstance(node, ast.Name):
            return node.id in by_name
        if isinstance(node, (ast.List, ast.Tuple)):
            return bool(node.elts) and all(only_models(e) for e in node.elts)
        return False

    out = []

    def number_of_var(node):
        # The number inside `var(5)` (the draggable form of a number): what a click on the row selects, so that typing a number keeps the var -- `var(9)`, not `9`
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == 'var' and len(node.args) == 1 and not node.keywords):
            return None
        inner = node.args[0]                    # (`-5` is a sign and a number: the selection takes both)
        core = inner.operand if isinstance(inner, ast.UnaryOp) and isinstance(inner.op, (ast.USub, ast.UAdd)) else inner
        if isinstance(core, ast.Constant) and isinstance(core.value, (int, float)) and not isinstance(core.value, bool):
            return inner
        return None

    def add(name, node):
        if only_models(node) or (isinstance(node, ast.Constant) and node.value is Ellipsis):
            return
        entry = {'param': name or '', 'text': _short(src.segment(node), 80), 'span': _span(node), 'kind': _arg_kind(node)}
        number = number_of_var(node)
        if number is not None:
            entry['select'] = _span(number)
        out.append(entry)

    for i, a in enumerate(value.args):
        if not isinstance(a, ast.Starred):
            add(positional[i] if i < len(positional) else None, a)
    for kw in value.keywords:
        if kw.arg and not (kw.arg in PLACEMENT_ARGS and _is_placeable(callee)):       # (the placement arguments are rows of their own: see _placement_of)
            add(kw.arg, kw.value)
    return out


def _block_names():
    ''' The names of the custom blocks (see fieldes.blocks), or none '''
    try:
        from fieldes import blocks
        return set(blocks.names())
    except Exception:
        return set()


def _key_of(it):
    ''' The key the model tree knows a row by (the same as ScenePanel's keyOf) '''
    kind = it.get('kind', '')
    if 'var' in it:
        return kind + ':' + it['var']
    if 'list_var' in it:
        return kind + ':' + it['list_var']
    return kind + ':' + it.get('label', '')

_view_shapes = {}           # the shapes the viewport draws, by the pointer of their tree (oldest first): what the field viewers look up
_view_exact = {}            # ... and the exact distance made of one, by the same pointer (kept alive for the viewers)
_VIEW_SHAPES_KEPT = 512


def register_view_shape(shape):
    ''' A shape that the viewport draws and that has an exact distance (an imported part, moved or edited or not) is told here by the
        application, so that a field viewer -- the section's Field mode, the field disc -- can ask for that distance when it samples:
        the part's own field is exact in sign only (outside it is the boxy distance to the infinite surfaces of its faces) '''
    key = int(shape.ptr)
    _view_shapes.pop(key, None)
    _view_shapes[key] = shape
    while len(_view_shapes) > _VIEW_SHAPES_KEPT:
        old = next(iter(_view_shapes))
        del _view_shapes[old]
        _view_exact.pop(old, None)


def exact_view_tree(key):
    ''' The pointer (as text) of the tree of the exact distance of the shape registered under `key`, or '' when there is none (it is
        made the first time it is asked for, about a second for a part, and kept) '''
    try:
        key = int(key)
        shape = _view_shapes.get(key)
        if shape is None:
            return ''
        exact = _view_exact.get(key)
        if exact is None:
            from fieldes.stdlib.fields import _dist
            exact = _dist(shape)
            if exact is shape and getattr(shape, '_display_field', None) is None:
                return ''
            # (a distance field the viewport meshes from its own, plainer field: the distance the viewers show is the shape's itself)
            _view_exact[key] = exact
        return str(int(exact.ptr))
    except Exception:
        return ''


def edit_in_place(new_text):
    ''' What an edit of the script -- the new text is `new_text` -- does to the run that is going, and does it where it can:
        'inplace'  it changes nothing the statement that runs is made of or makes (a model shown or hidden above it, whatever comes
                   after it): it is put into the run, which goes on with the new text -- nothing is stopped, and no other run follows;
        'reaches'  it begins at or above what that statement is made of (a number changed, a line added above it): what it makes may be
                   another thing, so a solver that works on it is stopped at once, not waited for;
        'defer'    the text is not a script just now (it is being typed) and the edit is below the statement: the run goes on to the end
                   of that statement, whose result is kept, and then makes way for the run of the text;
        ''         nothing runs (or the run is over its last statement): the edit makes its run '''
    from fieldes import runner
    st = runner.running_state
    if st is None or new_text == runner.running_source:
        # (the same text asks for a run of it again: a breakpoint set, a file that was imported changed on disk, Retry)
        return ''
    with st['lock']:
        plan = runner.plan_edit(new_text)
        if plan is None:
            return ''
        if isinstance(plan, dict):
            return 'inplace' if runner.apply_edit(new_text) == '1' else 'reaches'
        if plan == 'defer':
            runner.stop_after_statement()
        return plan


def outline_json(arg):
    ''' The model tree of the script's text, read from the text (see outline.py): JSON {source, run_done} -> the tree's JSON '''
    from fieldes import outline
    debug = os.environ.get('FIELDES_OUTLINE_DEBUG')         # (a file the answers, and the errors, are written to: for finding out why a tree is not there)
    try:
        out = outline.outline_json(arg)
    except Exception:
        if debug:
            import traceback
            with open(debug, 'a') as f:
                traceback.print_exc(file=f)
        raise
    if debug:
        with open(debug, 'a') as f:
            f.write(out[:3000] + '\n----\n')
    return out


def primitives_manage(arg):
    ''' The Primitives menu of the menu bar: which shapes the Primitives menus offer, and the primitives of the user's own (see user_primitives.py): JSON {op, ...} -> JSON '''
    from fieldes import user_primitives
    return user_primitives.manage(arg)


def move_check(arg):
    ''' Would moving lines of the script break it (a name read above the statement that makes it, a function called above its def)?  JSON {source, moved, dest}
        -> JSON, see outline.move_check: what it would break, and the statements that have to go along to keep the script as it is '''
    from fieldes import outline
    return outline.move_check(arg)


def seed_known(arg):
    ''' A tree kept from an earlier session tells what its statements made, until a run says it again (see outline.seed) '''
    from fieldes import outline
    outline.seed(arg)
    return '1'


def live_settings(arg):
    ''' The render settings the running script has given so far (view.set_bounds, set_resolution, set_quality), as JSON: what it has not
        given yet is left out.  The viewport renders the picture of an optimisation that is running with them '''
    try:
        import _fieldes_host as host
    except ImportError:
        return '{}'
    out = {}
    bounds = getattr(host, '__bounds', None)
    if bounds:
        out['min'], out['max'] = [float(c) for c in bounds[0]], [float(c) for c in bounds[1]]
    for key, name in (('res', '__resolution'), ('quality', '__quality')):
        v = getattr(host, name, None)
        if v is not None:
            out[key] = float(v)
    import json
    return json.dumps(out)


# ---------------------------------------------------------------------------
# FielDes apps (fieldes.app, fieldes.app_package): what the runner window asks
# ---------------------------------------------------------------------------

_opened_app = None      # the app the runner has open (app_open), which app_check sets the values of


def _json_default(o):
    return str(o)


def app_open(arg):
    ''' The runner opens an app (a `.fieldesapp`, or a script to try): {"path"} -> JSON {"ok": true, "kind", "path", "root", "cwd",
        "script", "manifest", "problems", "documents"} or {"ok": false, "error"} '''
    global _opened_app
    import json
    from fieldes import app_package
    try:
        opened = app_package.open_app(json.loads(arg)['path'])
    except Exception as e:
        return json.dumps({'ok': False, 'error': str(e)})
    _opened_app = opened
    out = dict(opened)
    out['ok'] = True
    out['documents'] = app_package.documents_folder()
    return json.dumps(out, default=_json_default)


def app_check(arg):
    ''' The values the person chose ({"values": {key: value}, "outputs": folder}) are checked and, when all fit, the run is set up with
        them (the script's app.number(...) are these values) and the working folder is the app's: JSON {"ok", "errors": {key: why}} '''
    import json
    import os
    from fieldes import app_package
    if _opened_app is None:
        return json.dumps({'ok': False, 'errors': {}, 'error': 'no app is open'})
    req = json.loads(arg)
    # ("live": only the picture of the app's viewport is made, the person has not pressed Run: app.running() is False)
    values, errors = app_package.check(_opened_app, req.get('values') or {}, req.get('outputs') or None, running=not req.get('live'))
    if not errors:
        try:
            os.chdir(_opened_app['cwd'])
        except OSError:
            pass
    return json.dumps({'ok': not errors, 'errors': errors, 'watch': app_package.watched_files(_opened_app, values) if not errors else []},
                      default=_json_default)


def app_end(arg):
    ''' The run is over: the inputs are their defaults again '''
    from fieldes import app
    app.end()
    return '1'


def app_outputs(arg):
    ''' The files the last run said it makes, as JSON [{"label", "path", "exists", "size", "fresh"}] '''
    import json
    from fieldes import app_package
    return json.dumps(app_package.outputs_made())


def app_undeclared(arg):
    ''' The inputs the last run declared that the form (read from the text of the script) did not have, as JSON [label]: an input written in a
        loop or a function '''
    import json
    from fieldes import app
    if _opened_app is None:
        return '[]'
    known = {s['key'] for s in _opened_app['manifest']['inputs']}
    return json.dumps([s['label'] for k, s in app._seen.items() if k not in known])


def app_inputs(arg):
    ''' What the script text declares ({"source"}), for the editor: JSON of fieldes.app_package.inputs_of '''
    import json
    from fieldes import app_package
    return json.dumps(app_package.inputs_of(json.loads(arg)['source']), default=_json_default)


def app_export(arg):
    ''' File > Export as app: {"source", "dir", "out", "title"} -> JSON report of fieldes.app_package.build, or {"error"} '''
    import json
    from fieldes import app_package
    req = json.loads(arg)
    try:
        return json.dumps(app_package.build(req['source'], req.get('dir') or None, req['out'], req.get('title') or None), default=_json_default)
    except Exception as e:
        return json.dumps({'error': '%s: %s' % (type(e).__name__, e)})


def app_cli(arg):
    ''' `FielDes run|export|info ...` ({"argv": [...]}): runs it, printing to the console; the answer is the exit code '''
    import json
    import sys
    from fieldes import app_package
    try:
        return str(app_package.main(json.loads(arg)['argv']))
    finally:
        for stream in (sys.stdout, sys.stderr):
            try:
                stream.flush()
            except Exception:
                pass


def call_edit(arg):
    ''' Edits that give keyword arguments of a call new values -- the card of the symmetry changes the planes of its model that way.  `arg` is JSON {source, var, line, set: {name:
        text}}: the statement `var = call(...)` on the 1-based `line`, and the text each argument is to have.  An argument that is there has its value replaced, one that is not is
        added after the last.  The answer is JSON {"edits": [[line, column, end line, end column, text], ...]} (0-based, as profile_edit gives them), the last place first '''
    req = json.loads(arg)
    source = req['source']
    tree = ast.parse(source)
    lines = source.split('\n')
    starts = [0]
    for text in lines[:-1]:
        starts.append(starts[-1] + len(text) + 1)

    def offset(line, byte_col):
        return starts[line - 1] + len(lines[line - 1].encode('utf-8')[:byte_col].decode('utf-8', 'replace'))

    def start_of(node):
        return offset(node.lineno, node.col_offset)

    def end_of(node):
        return offset(node.end_lineno, node.end_col_offset)

    def position(at):
        line = max(i for i, s in enumerate(starts) if s <= at)
        return [line, at - starts[line]]

    var, line = req['var'], req['line']
    call = None
    for st in tree.body:
        targets = getattr(st, 'targets', None) or ([st.target] if hasattr(st, 'target') else [])
        if st.lineno == line and any(isinstance(t, ast.Name) and t.id == var for t in targets) and isinstance(getattr(st, 'value', None), ast.Call):
            call = st.value
            break
    if call is None:
        raise ValueError('%s is not written as a call: edit the script' % var)
    edits = []
    for name, text in req['set'].items():
        kw = next((k for k in call.keywords if k.arg == name), None)
        if kw is not None and text == '':
            # (taken out: its line when it is alone on one, else with the comma that goes with it)
            a, b = offset(kw.lineno, kw.col_offset), end_of(kw.value)
            j = b
            while j < len(source) and source[j] in ' \t':
                j += 1
            comma = j < len(source) and source[j] == ','
            if comma:
                j += 1
            k = j
            while k < len(source) and source[k] in ' \t':
                k += 1
            line_start = max(i for i in starts if i <= a)
            alone = source[line_start:a].strip() == '' and (k >= len(source) or source[k] == '\n')
            if alone:
                edits.append((line_start, min(k + 1, len(source)), ''))
            elif comma:
                edits.append((a, k, ''))
            else:
                i = a - 1
                while i >= 0 and source[i] in ' \t\n':
                    i -= 1
                edits.append((i if i >= 0 and source[i] == ',' else a, b, ''))
        elif kw is not None:
            edits.append((start_of(kw.value), end_of(kw.value), text))
        elif text == '':
            continue
        else:
            nodes = list(call.args) + [k.value for k in call.keywords]
            last = max([end_of(n) for n in nodes] or [end_of(call) - 1])
            lastkw = max(call.keywords, key=lambda k: end_of(k.value)) if call.keywords else None
            if lastkw is not None and lastkw.lineno > call.lineno and end_of(lastkw.value) == last:
                # (written one argument to a line: the new one has a line of its own, as far in as the last one)
                edits.append((last, last, ',\n%s%s=%s' % (' ' * lastkw.col_offset, name, text)))
            else:
                edits.append((last, last, ', %s=%s' % (name, text)))
    edits.sort(key=lambda e: -e[0])
    return json.dumps({'edits': [position(a) + position(b) + [text] for a, b, text in edits]})


def profile_edit(arg):
    ''' The edits that put a point on the outline of a profile or a curve, or toggle one of its points between a corner and a spline point (see fieldes.profile_edit) '''
    from fieldes import profile_edit as _edit
    return _edit.edit(arg)



# ---------------------------------------------------------------------------------------------------------------------------------
# The solvers (Settings > Solver...): the analyses FielDes has built in, with their equations, and the solvers that are written in Python (see fieldes.stdlib.solvers)

_BUILT_IN_SOLVERS = (
    ('static_analysis', 'Static analysis (linear elasticity on tetrahedra)',
     "# unknown: the displacement u at every node.  C: the material (Young's modulus, Poisson's ratio), f: the loads and the weight\n"
     "strain = sym(grad(u))\n"
     "stress = C(E, nu) : strain\n"
     "div(stress) + f = 0          # in the part\n"
     "u = 0                        # on a fixed support\n"
     "stress . n = traction        # where a force acts\n"
     "# solved in C++ by conjugate gradients on the linear (or 10-node) tetrahedra; safety factor = yield / von Mises"),
    ('modal_analysis', 'Modal analysis (natural frequencies)',
     "K u = omega**2 * M u          # K: the stiffness, M: the mass of the tetrahedra (consistent), u = 0 on a fixed support\n"
     "frequency = omega / (2 * pi)  # Hz, the lowest `modes` of them (Lanczos)"),
    ('thermal_analysis', 'Thermal analysis (steady heat conduction)',
     "div(k * grad(T)) + q = 0      # in the part: k the conductivity (a number or a field), q the heat generation\n"
     "T = T0                        # where a temperature is fixed\n"
     "-k * dT/dn = flux             # where heat enters (heat_input)\n"
     "-k * dT/dn = h * (T - T_ambient)   # where the surface is cooled (convection)\n"
     "# solved in C++ on the tetrahedra; a topology design over it keeps the same equation"),
    ('fluid_analysis', 'Fluid analysis (laminar, incompressible flow)',
     "rho * (u . grad(u)) = -grad(p) + mu * laplace(u)     # momentum (left out for creeping flow: Stokes)\n"
     "div(u) = 0                                           # incompressible\n"
     "u = u_inlet on an inlet, u = 0 on a wall, p = p_outlet on an outlet\n"
     "# solved in C++ on the tetrahedra of the fluid by Picard then Newton iterations; steady or in time (backward Euler)"),
)


def _sidecar(path, suffix):
    ''' The text kept beside a solver file: the one it started with (`.default`: made, copied or added), or the one it had before the last save (`.bak`); '' when there is none '''
    if not path:
        return ''
    try:
        with open(path + suffix, encoding='utf-8') as f:
            return f.read()
    except OSError:
        return ''


def _write_text(path, text):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def _solver_entries(folder):
    import inspect
    import json
    import os
    from fieldes.stdlib import solvers
    os.environ.setdefault('FIELDES_SOLVERS', folder)
    errors = solvers._load_user()
    out = []
    for name, title, equation in _BUILT_IN_SOLVERS:
        out.append({'name': name, 'kind': 'builtin', 'description': title, 'equation': equation, 'path': '', 'source': '', 'default': '', 'backup': '', 'editable': False})
    for name in sorted(solvers._REGISTRY):
        entry = solvers._REGISTRY[name]
        path = entry.get('path') or ''
        try:
            source = open(path, encoding='utf-8').read() if path else inspect.getsource(inspect.getmodule(entry['function'])).split('# --- the solver the example is written for')[-1]
        except (OSError, TypeError):
            source = ''
        out.append({'name': name, 'kind': 'user' if path else 'library', 'description': entry['description'], 'equation': entry['equation'], 'path': path, 'source': source,
                    'default': _sidecar(path, '.default'), 'backup': _sidecar(path, '.bak'), 'editable': bool(path)})
    for path, message in errors:
        out.append({'name': os.path.basename(path), 'kind': 'broken', 'description': 'this file did not read', 'equation': message, 'path': path,
                    'source': open(path, encoding='utf-8').read(), 'default': _sidecar(path, '.default'), 'backup': _sidecar(path, '.bak'), 'editable': True})
    return json.dumps(out)


def solvers_folder(arg):
    ''' The folder of the solvers that are the user's (made when something is written to it) '''
    from fieldes.stdlib import solvers
    return solvers._folder()


def solvers_list(arg):
    ''' The solvers for the dialog of Settings > Solver...: JSON {folder} -> JSON [{name, kind (builtin, library, user, broken), description, equation, path, source, default (the text it started with), backup (the text before the last save), editable}] '''
    import json
    req = json.loads(arg)
    return _solver_entries(req.get('folder', ''))


_SOLVER_TEMPLATE = '''from fieldes.stdlib.solvers import solver, Sparse, tet_gradients


@solver('%(name)s', description='%(description)s',
        equation="%(equation)s")
def %(name)s(problem, fixed=(), conductivity=1.0, source=0.0):
    # problem.nodes: [(x, y, z), ...]    problem.tets: [(a, b, c, d), ...]    problem.nodes_in(region): the nodes of a body or a face
    # What is returned: the fields found, one number for each node -- and, if you like, a second dictionary of numbers.
    nodes, tets = problem.nodes, problem.tets
    K = Sparse(len(nodes))
    load = [0.0] * len(nodes)
    for tet in tets:
        volume, g = tet_gradients(*[nodes[i] for i in tet])
        for a in range(4):
            load[tet[a]] += source * volume / 4.0
            for b in range(4):
                K.add(tet[a], tet[b], conductivity * volume * (g[a][0] * g[b][0] + g[a][1] * g[b][1] + g[a][2] * g[b][2]))
    given = {}
    for region, value in fixed:
        for i in problem.nodes_in(region):
            given[i] = value
    return {'temperature': K.solve(load, given)}
'''


def solvers_template(arg):
    ''' The source of a new solver, to start from: JSON {name} -> its text (the solver `heat`, steady conduction, under that name) '''
    import json
    req = json.loads(arg)
    name = ''.join(c if (c.isalnum() or c == '_') else '_' for c in (req.get('name') or 'my_solver')) or 'my_solver'
    if name[0].isdigit():
        name = '_' + name
    if req.get('copy_of'):
        # (a copy of a solver of the library, under a name of its own: the same equations to change)
        import inspect
        import re
        from fieldes.stdlib import solvers
        entry = solvers._REGISTRY.get(req['copy_of'])
        if entry is not None and not entry.get('path'):
            source = inspect.getsource(entry['function'])
            source = re.sub(r"@solver\(\s*'[^']*'", "@solver('%s'" % name, source, count=1)
            source = re.sub(r"def\s+%s\(" % re.escape(entry['function'].__name__), 'def %s(' % name, source, count=1)
            return 'import math\n\nfrom fieldes.stdlib.solvers import solver, Sparse, tet_gradients\n\n\n' + source
    return _SOLVER_TEMPLATE % {'name': name, 'description': 'My solver: steady heat conduction to change', 'equation': 'div(conductivity * grad(T)) + source = 0'}


def solvers_save(arg):
    ''' Writes a solver file into the solvers folder and reads it: JSON {folder, file, source, default (given when the solver is new, copied or added: the text it can be reset to)} ->
        JSON {ok, error, path, names} (the solvers the file registers).  A text with a syntax error is not written; the text a file had before is kept as `<file>.bak` '''
    import json
    import os
    from fieldes.stdlib import solvers
    req = json.loads(arg)
    folder = req['folder']
    file = os.path.basename(req['file'])
    if not file.endswith('.py'):
        file += '.py'
    try:
        compile(req['source'], file, 'exec')
    except SyntaxError as e:
        return json.dumps({'ok': False, 'error': 'line %s: %s' % (e.lineno, e.msg), 'path': '', 'names': []})
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, file)
    # What was there is kept: the text before this save as `<file>.bak` (the Previous version), and, once, the text a solver started with as `<file>.default` (Reset to default)
    try:
        with open(path, encoding='utf-8') as f:
            before = f.read()
    except OSError:
        before = None
    if before is not None and before != req['source']:
        _write_text(path + '.bak', before)
    if isinstance(req.get('default'), str) and not os.path.exists(path + '.default'):
        _write_text(path + '.default', req['default'])
    _write_text(path, req['source'])
    os.environ['FIELDES_SOLVERS'] = folder
    before = set(solvers._REGISTRY)
    errors = [e for e in solvers._load_user() if e[0] == path]
    if errors:
        return json.dumps({'ok': False, 'error': errors[0][1].strip().splitlines()[-1], 'path': path, 'names': []})
    names = sorted(k for k, e in solvers._REGISTRY.items() if e.get('path') == path)
    if not names:
        return json.dumps({'ok': False, 'error': 'the file registers no solver: a function with the decorator @solver(\'name\') does', 'path': path, 'names': []})
    return json.dumps({'ok': True, 'error': '', 'path': path, 'names': names})


def solvers_delete(arg):
    ''' Deletes a solver file of the solvers folder: JSON {path} -> JSON {ok} '''
    import json
    import os
    req = json.loads(arg)
    path = req['path']
    try:
        os.remove(path)
        for suffix in ('.default', '.bak'):             # (what was kept beside the file goes with it)
            if os.path.exists(path + suffix):
                os.remove(path + suffix)
    except OSError as e:
        return json.dumps({'ok': False, 'error': str(e)})
    return json.dumps({'ok': True, 'error': ''})


def units_set(arg):
    ''' Settings > Units: chooses the units numbers are shown and mesh files are written in (arg: {"choice": {"length": "in", "stress": "psi"}}; what is not given is its
        default) and answers what the window needs: {"choice", "options" (what can be chosen, with how each is shown), "table" (how each unit of the program is shown)} '''
    from fieldes import units
    request = json.loads(arg) if arg else {}
    units.configure(request.get('choice'))
    return json.dumps({'choice': units.current(), 'options': units.options(), 'table': units.table()})
