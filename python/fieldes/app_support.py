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
    from fieldes.stdlib.content_cache import cache_for, shape_key
    from fieldes.stdlib.fields import _vars_key
    memory = cache_for('scene_bounds', 256)         # (the extents found, by the expression and the numbers of the script's variables)
    boxes = []
    for s in shapes:
        b = getattr(s, '_bounds', None)
        if b:
            boxes.append(b)
            continue
        key = None
        try:
            key = (shape_key(s), _vars_key())
        except Exception:
            pass
        if key is not None:
            hit, kept = memory.get(key)
            if hit:
                boxes.append(kept)
                continue
        if time.time() > deadline:
            return None
        known = _var_values() if hasattr(lib, 'libfive_tree_bounds_vars') else None
        box = None
        # A search that is cut short (80 ms) returns the coarser box it had got to -- a 500 mm box round a part of 30 mm, when the
        # machine is busy.  It is searched again inside the box that was found (the shape is in it), as long as that makes the box
        # smaller and there is time, so that the extent -- and what is sized by it: the element size of a simulation, the framing -- is
        # right whatever the load
        for attempt in range(2):
            search = libfive_region_t()
            for k, axis in enumerate((search.X, search.Y, search.Z)):
                if box is None:
                    axis.lower, axis.upper = -_SEARCH, _SEARCH
                else:
                    # (a cube round the box: a search over a box that is not a cube does not come to an end in the time it has)
                    half = 0.51 * max(box[1][i] - box[0][i] for i in range(3)) + 1e-3
                    centre = 0.5 * (box[0][k] + box[1][k])
                    axis.lower, axis.upper = centre - half, centre + half
            out = libfive_region_t()
            open_sides = ctypes.c_int(0)
            started = time.time()
            limit = 0.08 if attempt == 0 else 0.5       # (the first look is quick; the one inside the box it found takes the time it needs)
            if known is not None:
                found = lib.libfive_tree_bounds_vars(s.ptr, search, 100000, limit, ctypes.byref(out),
                                                     ctypes.byref(open_sides), known[0], known[1], known[2])
            else:
                found = lib.libfive_tree_bounds(s.ptr, search, 100000, limit, ctypes.byref(out),
                                                ctypes.byref(open_sides))
            if not found or open_sides.value:
                break                       # (nothing better than the box that was found before, if there is one)
            found_box = ((out.X.lower, out.Y.lower, out.Z.lower), (out.X.upper, out.Y.upper, out.Z.upper))
            size_before = sum(box[1][i] - box[0][i] for i in range(3)) if box else None
            box = found_box
            if size_before is None:
                # (the first answer may be a coarse one -- the search ran out of its 80 ms: look again inside it; one that came quickly is final)
                if time.time() - started >= 0.06 and time.time() < deadline:
                    continue
                break
            if sum(box[1][i] - box[0][i] for i in range(3)) > 0.98 * size_before or time.time() > deadline:
                break
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
    ''' The folder of the custom blocks (Settings > Blocks folder); an empty path is the default one next to FielDes.
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
        if getattr(shape, '_locks', None) or getattr(shape, '_kind', None) in ('point', 'field', 'const'):
            return 0                # (a point is moved by its gizmo, not by pulling the surface of its ball; a field has no
                                    # surface to pull: its numbers are written in the script, or moved by its gizmo)
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
    ''' The fields of the last scene, for the field viewer: [(key, Shape, origin)] -- origin is where the field is about, as three
        numbers (the point or the middle of the body it was made from), or None (the viewer starts at the origin) '''
    return [(key, shape, _origin_numbers(getattr(shape, '_field_origin', None))) for key, shape in _FIELD_SOURCES]


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
                if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD, p.KEYWORD_ONLY):
                    params.append(p.name)
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
        if kw.arg:
            note(kw.arg, kw.value)
    out = {'params': params, 'args': given}
    if last is not None:
        out['last'] = last
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

def edit_reaches_running(new_text):
    ''' '1' when an edit of the script -- the new text is `new_text` -- begins at or above the statement that runs now: what it makes may
        be another thing, so a solver that works on it is stopped at once, not waited for.  '' when the edit is below it (an eye toggled
        under it, a line added at the end): that cannot change what the statement makes, it finishes -- and is kept for the next run '''
    from fieldes import runner
    running = getattr(runner, 'running_lines', None)
    old = getattr(runner, 'running_source', None)
    if not running or old is None:
        return ''
    a, b = old.splitlines(), new_text.splitlines()
    n = 0
    while n < len(a) and n < len(b) and a[n] == b[n]:
        n += 1
    if n == len(a) == len(b):
        return ''
    return '1' if n + 1 <= running[1] else ''


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
