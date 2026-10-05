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

    for name in ('set_bounds', 'set_resolution', 'set_quality'):
        out.append('member\tview\t' + name)
    out.append('tip\tset_bounds\tview.set_bounds([xmin, ymin, zmin], [xmax, ymax, zmax])'
               '  --  render region')
    out.append('tip\tset_resolution\tview.set_resolution(res)  --  voxels per unit')
    out.append('tip\tset_quality\tview.set_quality(q)  --  mesh accuracy (1-10)')
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

IMPORT_FUNCS = ('import_step_parts', 'import_step', 'import_step_parts_reconstructed',
                'import_step_tessellated_parts', 'import_step_tessellated', 'import_mesh')
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
    ''' "import_step_parts" / "view.set_bounds" for a Call node '''
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


def _find_import(stmt):
    ''' (call, subscript-or-None) for the first import call in a statement '''
    for node in ast.walk(stmt):
        if isinstance(node, ast.Subscript) and isinstance(node.value, ast.Call) and \
                _short_name(_call_name(node.value)) in IMPORT_FUNCS:
            return node.value, node
    for node in ast.walk(stmt):
        if isinstance(node, ast.Call) and _short_name(_call_name(node)) in IMPORT_FUNCS:
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
_BOUNDS_SECONDS = 0.6    # time budget per scene for estimating extents


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


def _estimate_bounds(shapes, deadline):
    ''' Bounding box of one or more Shapes' interiors, from their own
        _bounds when known, else estimated with interval arithmetic
        (libfive_tree_bounds).  None if unknown, unbounded or too slow. '''
    import ctypes
    import time
    from fieldes.ffi import lib, libfive_region_t
    boxes = []
    for s in shapes:
        b = getattr(s, '_bounds', None)
        if b:
            boxes.append(b)
            continue
        if time.time() > deadline:
            return None
        search = libfive_region_t()
        for axis in (search.X, search.Y, search.Z):
            axis.lower, axis.upper = -_SEARCH, _SEARCH
        out = libfive_region_t()
        open_sides = ctypes.c_int(0)
        known = _var_values() if hasattr(lib, 'libfive_tree_bounds_vars') else None
        if known is not None:
            found = lib.libfive_tree_bounds_vars(s.ptr, search, 100000, 0.08, ctypes.byref(out),
                                                 ctypes.byref(open_sides), known[0], known[1], known[2])
        else:
            found = lib.libfive_tree_bounds(s.ptr, search, 100000, 0.08, ctypes.byref(out),
                                            ctypes.byref(open_sides))
        if not found or open_sides.value:
            return None
        boxes.append(((out.X.lower, out.Y.lower, out.Z.lower),
                      (out.X.upper, out.Y.upper, out.Z.upper)))
    if not boxes:
        return None
    return [[min(b[0][i] for b in boxes) for i in range(3)],
            [max(b[1][i] for b in boxes) for i in range(3)]]


# (a part with more than this is left alone: hundreds of thousands of var()s make a script slow to run
# and the numbers of a free-form surface's fit are not something to drag anyway)
_MAX_EXPOSED = 6000


def menu_catalog(_arg=''):
    ''' The primitives and operations the viewport's context menus offer, as JSON (see fieldes.menu_catalog) '''
    from fieldes import menu_catalog as catalog
    return catalog.catalog()


def menu_call(request):
    ''' The call a context-menu entry writes into the script: `request` is JSON (see fieldes.menu_catalog.call).
        Raises ValueError with the reason when the entry cannot be made. '''
    from fieldes import menu_catalog as catalog
    return catalog.call(request)


def expose_text(name):
    ''' The statement that makes the surfaces of the shape a script variable holds (as of the last run)
        draggable -- "name = expose(name, [var(...), ...])", one line per eight numbers -- for FielDes
        to write under its definition.  Raises ValueError with the reason when it cannot be done. '''
    from fieldes import runner
    from fieldes.shape import Shape
    from fieldes.stdlib.handles import exposed_values
    shape = runner.last_globals.get(name)
    if not isinstance(shape, Shape):
        raise ValueError('%s is not a shape of the last run' % name)
    # (the variable may hold the shape under a gizmo -- name = handles(name, ...): its own numbers are
    # the shape it was made from, whose definition the exposed numbers go under)
    while getattr(shape, '_handles', None) is not None and isinstance(getattr(shape, '_placed_from', None), Shape):
        shape = shape._placed_from
    if getattr(shape, '_locks', None):
        raise ValueError('%s has an excluded part: dragging its surfaces would move that too (use the gizmo)' % name)
    values = exposed_values(shape)
    if not values:
        raise ValueError('%s has no numbers that place its surfaces to expose' % name)
    if len(values) > _MAX_EXPOSED:
        raise ValueError('%s has %d numbers that place its surfaces: too many to write into the script '
                         '(the limit is %d)' % (name, len(values), _MAX_EXPOSED))
    rows = []
    for i in range(0, len(values), 8):
        rows.append('    ' + ', '.join('var(%s)' % v for v in values[i:i + 8]) + ',')
    return '%s = expose(%s, [\n%s\n])' % (name, name, '\n'.join(rows))


def _expose_count(shape):
    ''' How many numbers place the shape's surfaces (what expose() would write), 0 when there are none or it cannot be
        done (a shape under a gizmo is asked about the shape it was made from, as expose_text does) '''
    try:
        from fieldes.shape import Shape
        from fieldes.stdlib.handles import _exposed_count
        while getattr(shape, '_handles', None) is not None and isinstance(getattr(shape, '_placed_from', None), Shape):
            shape = shape._placed_from
        if getattr(shape, '_locks', None):
            return 0
        return int(_exposed_count(shape))
    except Exception:
        return 0


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


def scene_json(source, gs, results, upto=None, partial=False):
    ''' Describes the evaluated script for FielDes's model tree.  Returns a
        JSON string; every position is 1-based lines / 0-based columns.
        While a script is still running the tree is given what has been made so far: upto is the number of
        top-level statements that are done, and partial leaves out what takes a measurement of the shapes (their
        extents, whether their surfaces can be dragged), which the finished script's tree has. '''
    Shape, FailedPart = _types()
    src = _Source(source)
    _SPAN_LINES[:] = src.lines
    tree = ast.parse(source)
    if upto is not None:
        tree.body = tree.body[:upto]

    def is_shape(v):
        return isinstance(v, Shape)

    def is_result(v):
        # (an analysis result: displayed through its _display(), listed like a shape)
        return not isinstance(v, Shape) and callable(getattr(v, '_display', None))

    def display_shapes(v):
        ''' The shapes a value displays (a result's _display() may be one or several) '''
        if is_shape(v):
            return [v]
        if is_result(v):
            try:
                d = v._display()
            except Exception:
                return []
            return [s for s in (d if isinstance(d, (list, tuple)) else [d]) if is_shape(s)]
        if isinstance(v, (list, tuple)):
            return [s for s in v if is_shape(s)]
        return []

    def is_failed(v):
        return bool(FailedPart) and isinstance(v, FailedPart)

    taken = set(gs) | {n.id for n in ast.walk(tree) if isinstance(n, ast.Name)}   # (names not to reuse)
    items = []
    by_name = {}        # shape variable -> its item
    settings = {}
    list_imports = {}   # parts-list variable -> import item

    for index, stmt in enumerate(tree.body):
        value = results[index] if index < len(results) else None
        line, end = stmt.lineno, stmt.end_lineno

        # --- view.set_bounds / set_resolution / set_quality
        if isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call):
            fn = _call_name(stmt.value)
            if fn.startswith('view.') and _short_name(fn) in SETTINGS_FUNCS:
                settings[_short_name(fn)] = {'line': line, 'end_line': end,
                                             'span': _span(stmt),
                                             'text': src.segment(stmt)}
                continue

        # --- imports
        call, sub = _find_import(stmt)
        if call is not None:
            func = _short_name(_call_name(call))
            path = None
            if call.args:
                a = call.args[0]
                if isinstance(a, ast.Constant) and isinstance(a.value, str):
                    path = a.value
                elif isinstance(a, ast.Name) and isinstance(gs.get(a.id), str):
                    path = gs[a.id]
            rev = None
            rev_span = None
            units = None
            for kw in call.keywords:
                if kw.arg == 'rev' and isinstance(kw.value, ast.Constant):
                    rev = kw.value.value
                    rev_span = _span(kw.value)
                if kw.arg == 'units' and isinstance(kw.value, ast.Constant):
                    units = kw.value.value
            log = _import_log_entry(path) if path else None
            item = {
                'kind': 'import', 'line': line, 'end_line': end,
                'func': func, 'path': path,
                'label': os.path.basename(path) if path else func,
                'call': _span(call), 'rev': rev, 'rev_span': rev_span,
                'units': units if units is not None else (log or {}).get('units'),
                'unit_mm': (log or {}).get('unit_mm'),
                'note': (log or {}).get('note'),
                'parts': [dict(p) for p in (log or {}).get('parts', [])],
                'exists': bool(path) and os.path.exists(path),
                'text': _short(src.segment(stmt), 90),
            }
            if sub is not None:
                k, knode = _const_index(sub)
                if k is not None:
                    item['index'] = k
                    item['index_span'] = _span(knode)

            # What does the statement bind?
            if isinstance(stmt, ast.Assign):
                for target in stmt.targets:
                    elts = target.elts if isinstance(target, ast.Tuple) else [target]
                    first = elts[0] if elts else None
                    if isinstance(target, ast.Name):
                        v = gs.get(target.id)
                        if isinstance(v, list):
                            item['list_var'] = target.id
                            list_imports[target.id] = item
                        elif is_shape(v) or is_failed(v):
                            item['var'] = target.id
                        elif isinstance(v, tuple) and len(v) == 2:
                            item['part_tuple_var'] = target.id
                    elif isinstance(first, ast.Name):
                        v = gs.get(first.id)
                        if is_shape(v) or is_failed(v):
                            item['var'] = first.id
                            if len(elts) > 1:
                                item['bounds_expr'] = src.segment(elts[1])
            if 'var' in item:
                v = gs.get(item['var'])
                item['failed'] = is_failed(v)
                if is_failed(v):
                    item['error'] = v.error
                b = getattr(v, '_bounds', None) if is_shape(v) else None
                if b:
                    item['bounds'] = _bounds_list(b)
                elif 'index' in item and item['index'] < len(item['parts']):
                    item['bounds'] = item['parts'][item['index']]['bounds']
                item['roi_expr'] = item.get('bounds_expr') or \
                    (item['var'] if b else None)
                by_name[item['var']] = item
            elif 'list_var' in item:
                item['roi_expr'] = item['list_var']
            if item['parts']:
                lo = [min(p['bounds'][0][i] for p in item['parts']) for i in range(3)]
                hi = [max(p['bounds'][1][i] for p in item['parts']) for i in range(3)]
                item.setdefault('bounds', [lo, hi])
            items.append(item)
            continue

        # --- "x = handles(x, ...)" / "x = expose(x, [...])" / "x = render_cache(x)" / "x = lock(x)": what edits the
        # shape x (not shapes of their own)
        if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and \
                isinstance(stmt.targets[0], ast.Name) and isinstance(stmt.value, ast.Call) and \
                _short_name(_call_name(stmt.value)) in ('handles', 'expose', 'render_cache', 'lock') and stmt.value.args and \
                isinstance(stmt.value.args[0], ast.Name) and \
                stmt.value.args[0].id == stmt.targets[0].id and stmt.targets[0].id in by_name:
            target = by_name[stmt.targets[0].id]
            if _short_name(_call_name(stmt.value)) == 'render_cache':
                # (the render cache keeps every shape's mesh unless the script says render_cache(x, False): that line
                # is the opt-out; render_cache(x) is the default said aloud)
                on = True
                flag = stmt.value.args[1] if len(stmt.value.args) > 1 else None
                for kw in stmt.value.keywords:
                    if kw.arg == 'on':
                        flag = kw.value
                if isinstance(flag, ast.Constant):
                    on = bool(flag.value)
                info = {'line': line, 'end_line': end, 'call': _span(stmt.value), 'text': _short(src.segment(stmt), 60)}
                target['cache' if on else 'cache_off'] = info
                continue
            if _short_name(_call_name(stmt.value)) == 'lock':
                # (a locked shape cannot be dragged; the way of editing it had stays, for when it is unlocked)
                target['locked'] = {'line': line, 'end_line': end, 'call': _span(stmt.value),
                                    'text': _short(src.segment(stmt), 60)}
                continue
            if _short_name(_call_name(stmt.value)) == 'expose':
                # (its numbers are var()s: the shape's surfaces can be dragged)
                target['exposed'] = {'line': line, 'end_line': end, 'call': _span(stmt.value),
                                     'text': _short(src.segment(stmt), 60)}
                target['has_var'] = True
                continue
            mode, mode_span = None, None
            for kw in stmt.value.keywords:
                if kw.arg == 'mode' and isinstance(kw.value, ast.Constant) and \
                        isinstance(kw.value.value, str):
                    mode, mode_span = kw.value.value, _span(kw.value)
            if mode not in ('click', 'never', 'always'):
                mode = 'click'
            target['handles'] = {
                'line': line, 'end_line': end, 'mode': mode, 'mode_span': mode_span,
                'call': _span(stmt.value), 'text': src.segment(stmt),
                'has_scale': any(kw.arg == 'scale' for kw in stmt.value.keywords),
                # (a bare handles(x, mode='never') has no numbers to drag: no gizmo)
                'has_numbers': any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and
                                   n.func.id == 'var' for n in ast.walk(stmt.value))}
            continue

        # --- assignments of shapes (and of parts taken from an import list)
        if isinstance(stmt, ast.Assign):
            for target in stmt.targets:
                elts = target.elts if isinstance(target, ast.Tuple) else [target]
                for pos, t in enumerate(elts):
                    if not isinstance(t, ast.Name):
                        continue
                    v = gs.get(t.id)
                    if not (is_shape(v) or is_failed(v) or is_result(v)):
                        continue
                    item = {'kind': 'failed' if is_failed(v) else 'shape',
                            'line': line, 'end_line': end,
                            'var': t.id, 'label': t.id,
                            'result': is_result(v),
                            'no_handles': bool(getattr(v, '_no_handles', False)) or is_result(v),
                            'text': _short(src.segment(stmt.value), 70),
                            'deps': sorted(n for n in _names_in(stmt.value)
                                           if n in by_name and n != t.id)}
                    # A shape made with var() numbers (its own, or those of what it is made of) has
                    # FielDes's handles: hover a surface and drag it
                    item['has_var'] = any(
                        isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var'
                        for n in ast.walk(stmt.value)) or \
                        any(by_name[d].get('has_var') for d in item['deps'])
                    if is_failed(v):
                        item['error'] = v.error
                    b = getattr(v, '_bounds', None) if is_shape(v) else None
                    if b:
                        item['bounds'] = _bounds_list(b)
                        item['roi_expr'] = t.id
                    # "v = parts[k][0]" / "v, b = parts[k]" -> part k of an import
                    node = stmt.value
                    k = None
                    if isinstance(node, ast.Subscript):
                        inner = node.value
                        if isinstance(inner, ast.Subscript) and isinstance(inner.value, ast.Name) \
                                and inner.value.id in list_imports:
                            k, _ = _const_index(inner)
                            base = inner.value.id
                        elif isinstance(inner, ast.Name) and inner.id in list_imports:
                            k, _ = _const_index(node)
                            base = inner.id
                    if k is not None:
                        imp = list_imports[base]
                        item['part_of'] = imp['line']
                        item['part_index'] = k
                        if k < len(imp['parts']):
                            imp['parts'][k]['var'] = t.id
                            item.setdefault('bounds', imp['parts'][k]['bounds'])
                        item['roi_expr'] = '{}[{}]'.format(base, k)
                    by_name[t.id] = item
                    items.append(item)
            continue

        # --- displayed expressions
        if isinstance(stmt, ast.Expr):
            shown = is_shape(value) or is_result(value) or (isinstance(value, (list, tuple)) and value and
                                                            all(is_shape(v) for v in value))
            if isinstance(stmt.value, ast.Name) and stmt.value.id in by_name:
                it = by_name[stmt.value.id]
                it['display_line'] = line
                continue
            if shown:
                deps = sorted(n for n in _names_in(stmt) if n in by_name)
                item = {'kind': 'display', 'line': line, 'end_line': end,
                        'label': _short(src.segment(stmt), 50),
                        'text': _short(src.segment(stmt), 90),
                        'display_line': line,
                        'deps': deps,
                        '_value': value}
                if is_shape(value) and not hasattr(value, '_color_field'):
                    # (a displayed expression can be given a name, to be edited by dragging: the model
                    # tree's handles button does that, then edits the new variable)
                    item['can_name'] = True
                    item['span'] = _span(stmt)
                    item['new_var'] = _new_name(stmt.value, taken)
                    item['has_var'] = any(
                        isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var'
                        for n in ast.walk(stmt)) or any(by_name[d].get('has_var') for d in deps)
                items.append(item)

    # --- "# hidden: <expr>" comment lines (top level)
    lines = src.lines
    i = 0
    while i < len(lines):
        m = HIDDEN_RE.match(lines[i])
        if not m or m.group(1):
            i += 1
            continue
        start = i
        text = m.group(2)
        # Multi-line hidden expressions: keep consuming hidden lines until
        # the collected text parses as a single expression
        j = i
        while True:
            try:
                ast.parse(text, mode='eval')
                break
            except SyntaxError:
                if j + 1 < len(lines) and HIDDEN_RE.match(lines[j + 1]):
                    j += 1
                    text += '\n' + HIDDEN_RE.match(lines[j]).group(2)
                else:
                    break
        name = text.strip()
        if name in by_name:
            by_name[name]['hidden_line'] = start + 1
        elif not (partial and start + 1 > (tree.body[-1].end_lineno if tree.body else 0)):
            # (a run in progress has not reached the lines after its last statement)
            items.append({'kind': 'display', 'line': start + 1, 'end_line': j + 1,
                          'label': _short(text, 50), 'text': _short(text, 90),
                          'hidden_line': start + 1})
        i = j + 1

    for it in items:
        if it['kind'] == 'display':
            it['visible'] = 'hidden_line' not in it
            it['mode'], it['mode_explicit'] = 'click', False
        elif 'var' in it:
            it['visible'] = 'display_line' in it
            # The gizmo mode of its button: written by a handles() line ('click', 'never' or 'always'), else the
            # default, 'click'.  Whether it is locked is a line of its own
            h = it.get('handles')
            if h:
                it['mode'], it['mode_explicit'] = h['mode'], True
            else:
                it['mode'], it['mode_explicit'] = 'click', False

    # Extents of displayed shapes whose bounds aren't known yet (plain CSG,
    # shapes built from imports): needed to frame them and to make them the
    # region of interest.  Visible ones first, within a time budget.
    import time
    deadline = time.time() + (0 if partial else _BOUNDS_SECONDS)
    for it in sorted(items, key=lambda i: not i.get('visible')):
        if 'bounds' in it or it.get('failed') or partial:
            continue
        if it['kind'] == 'display':
            v = it.get('_value')
            shapes = [v] if is_shape(v) else (list(v) if isinstance(v, (list, tuple)) else [])
        elif it['kind'] == 'shape' and 'var' in it:
            shapes = display_shapes(gs.get(it['var']))
        else:
            continue
        shapes = [s for s in shapes if is_shape(s)]
        if shapes:
            b = _estimate_bounds(shapes, deadline)
            if b:
                it['bounds'] = b
    # Shapes written with plain numbers: can their surfaces be made draggable?  (the first click on
    # the handles button makes them so; when they cannot, it goes to the gizmo)
    deadline = time.time() + _BOUNDS_SECONDS
    for it in sorted(items, key=lambda i: not i.get('visible')):
        if it.get('failed') or it.get('has_var') or it.get('exposed') or partial \
                or it['kind'] not in ('shape', 'display', 'import') or time.time() > deadline:
            continue
        if it['kind'] == 'display':
            v = it.get('_value') if it.get('can_name') else None
        else:
            v = gs.get(it.get('var'))
        if is_shape(v):
            n = _expose_count(v)
            # (FielDes writes the numbers of a small shape itself when it is selected, and offers the others in the
            # shape's menu: `expose_count` says how many there are)
            it['can_expose'] = 0 < n <= _MAX_EXPOSED
            it['expose_count'] = n
    for it in items:
        it.pop('_value', None)

    items.sort(key=lambda it: it['line'])
    last_of = {}
    for it in items:
        if 'var' in it:
            last_of[it['var']] = it
    for it in items:
        if 'var' in it and last_of[it['var']] is not it:
            it['reassigned'] = True       # (the handles button belongs to its last statement)
    truncated = 0
    if len(items) > MAX_ITEMS:
        # Huge generated scripts: only the intermediate shapes are left out
        # of the tree.  Imports, displayed and hidden items are all kept,
        # however many there are (every part of an import can be shown)
        keep = [it for it in items if it['kind'] in ('import', 'display') or
                it.get('visible') or 'hidden_line' in it]
        truncated = len(items) - len(keep)
        items = keep

    # Current render settings (as the script left them)
    try:
        import _fieldes_host as host
        values = {'bounds': getattr(host, '__bounds', None),
                  'resolution': getattr(host, '__resolution', None),
                  'quality': getattr(host, '__quality', None)}
    except ImportError:
        values = {}
    for k, v in values.items():
        if v is not None:
            settings.setdefault(k, {})['value'] = v

    return json.dumps({'items': items, 'settings': settings,
                       'truncated': truncated,
                       'has_roi': 'roi' in gs, 'has_roi_resolution': 'roi_resolution' in gs},
                      default=str)
