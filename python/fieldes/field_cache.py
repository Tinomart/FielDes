'''
One cache for every field a script builds.

A script runs again whenever it is opened or edited.  Most statements are quick, but a few build a field at real cost (a
lattice laid out on a part, a thickness field, an offset of an imported body).  Instead of caching each such function one by one,
the script runner asks this module about EVERY statement of the form

    name = <expression>

whose value is a field (a Shape).  The statement is known by what it is made of:

    * its text, and
    * the exact content of every name it reads: a field by the structural hash of its expression, a number by its bits, text,
      lists, the library's own functions by name, and so on (anything else -- a function you defined yourself, a module that
      reads files or the clock -- has no content key, and then the statement simply runs),
    * the code that would build it (the kernel library and the library's Python files, by size and time).

Run the same statement again with the same inputs and the field is not built again: in the same session the very same field is
handed back; in a later session (after closing and opening the application) it is read back from a file if every part of the
field can be saved (`libfive_tree_can_save`; a lattice, a body, any expression of them).  What the statement printed is printed
again.  A statement that reads a file (`import_...`, `load...`, `open`) is never kept this way: the file may have changed
(imports keep their own cache next to the file).

Nothing here approximates: a hit gives exactly what a run would build.

    stats()    {'hits': .., 'disk hits': .., 'misses': .., 'kept': ..}
    clear()    forget everything held in memory (the files stay; delete the folder to remove them)

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import builtins
import hashlib
import json
import os
import tempfile
import threading
import types

__all__ = ['stats', 'clear', 'lookup', 'store']

_lock = threading.RLock()
_memory = {}                 # key hash -> (shape, printed text)
_counts = {'hits': 0, 'disk hits': 0, 'misses': 0, 'kept': 0}
_identity = None
_FILE_READERS = ('import_', 'load', 'read', 'open', 'save', 'write', 'export', 'input', 'var', 'expose', 'handles', 'render_cache', 'progress')
_MIN_SECONDS = 0.25           # a statement that took less is not worth a file
_SAFE_MODULES = ('math', 'numbers', 'itertools', 'functools', 'collections', 'typing', 'fractions', 'statistics')
_LIMIT_BYTES = 3 * 1024 ** 3


def stats():
    ''' What the cache did: {'hits': .., 'disk hits': .., 'misses': .., 'kept': ..} '''
    with _lock:
        return dict(_counts)


def clear():
    ''' Forgets what is held in memory '''
    with _lock:
        _memory.clear()


class _No(Exception):
    ''' This statement has no content key '''


# ---------------------------------------------------------------------------------------------------------------------
# Where the files go

def _folder():
    d = os.environ.get('FIELDES_FIELD_CACHE_DIR')
    if d:
        return d
    render = os.environ.get('FIELDES_RENDER_CACHE_DIR')
    if render:
        return os.path.join(os.path.dirname(os.path.abspath(render)), 'field-cache')
    if os.name == 'nt':
        root = os.environ.get('LOCALAPPDATA') or os.path.expanduser('~')
        return os.path.join(root, 'FielDes', 'FielDes', 'cache', 'field-cache')
    return os.path.join(os.path.expanduser('~'), '.cache', 'FielDes', 'FielDes', 'cache', 'field-cache')


# ---------------------------------------------------------------------------------------------------------------------
# What built it: the library and its Python files (a rebuilt library, an edited stdlib: other fields)

def _code_identity():
    global _identity
    if _identity is not None:
        return _identity
    h = hashlib.sha1()
    try:
        from fieldes.ffi import lib
        path = getattr(lib, '_name', None)
        if path and os.path.exists(path):
            st = os.stat(path)
            h.update(('%s|%d|%d' % (os.path.basename(path), st.st_size, st.st_mtime_ns)).encode())
    except Exception:
        pass
    here = os.path.dirname(os.path.abspath(__file__))
    for root, _, files in sorted(os.walk(here)):
        for f in sorted(files):
            if f.endswith('.py'):
                try:
                    st = os.stat(os.path.join(root, f))
                    h.update(('%s|%d|%d' % (os.path.relpath(os.path.join(root, f), here), st.st_size, st.st_mtime_ns)).encode())
                except OSError:
                    pass
    _identity = h.hexdigest()
    return _identity


# ---------------------------------------------------------------------------------------------------------------------
# The key

def _text(p, source_lines):
    return '\n'.join(source_lines[p.lineno - 1:p.end_lineno])


def _eligible(p):
    ''' name = <expression>, one plain name; no call that reads or writes a file '''
    if not isinstance(p, ast.Assign) or len(p.targets) != 1 or not isinstance(p.targets[0], ast.Name):
        return False
    for n in ast.walk(p.value):
        if isinstance(n, ast.Call):
            f = n.func
            name = f.id if isinstance(f, ast.Name) else (f.attr if isinstance(f, ast.Attribute) else '')
            low = name.lower()
            if any(low.startswith(r) for r in _FILE_READERS):
                return False
        if isinstance(n, (ast.Yield, ast.YieldFrom, ast.Await, ast.NamedExpr)):
            return False
    return True


def _key_of(value):
    from fieldes.stdlib.content_cache import value_key, Uncacheable
    if isinstance(value, types.ModuleType):
        if value.__name__.split('.')[0] in _SAFE_MODULES:
            return ('module', value.__name__)
        raise _No()
    if isinstance(value, (types.BuiltinFunctionType, types.BuiltinMethodType)):
        return ('builtin', value.__name__)
    if isinstance(value, type) or isinstance(value, (types.FunctionType, types.MethodType)):
        mod = getattr(value, '__module__', '') or ''
        if mod.startswith('fieldes') or mod in ('builtins', 'math'):
            return ('fn', mod, getattr(value, '__qualname__', repr(value)))
        raise _No()                                  # a function of your own: not known by content
    try:
        return value_key(value)
    except Uncacheable:
        raise _No()


def _key(p, gs, source_lines):
    names = sorted({n.id for n in ast.walk(p.value) if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load)})
    parts = []
    for name in names:
        if name in gs:
            parts.append((name, _key_of(gs[name])))
        elif not hasattr(builtins, name):
            continue                                  # (a name local to a comprehension or lambda)
        else:
            parts.append((name, ('builtin', name)))
    text = _text(p, source_lines)
    try:
        from fieldes.stdlib.fields import _vars_key
        numbers = _vars_key()                          # (the numbers of the script's var()s: a field made from them depends on them)
    except Exception:
        numbers = None
    return hashlib.sha1(repr(('fdfc', 1, _code_identity(), text, tuple(parts), numbers)).encode('utf-8', 'surrogatepass')).hexdigest()


# ---------------------------------------------------------------------------------------------------------------------
# Hand back / keep

def _plain(v):
    ''' A value that survives a JSON round trip unchanged (tuples come back as tuples) '''
    if v is None or isinstance(v, (bool, int, float, str)):
        return True
    if isinstance(v, (list, tuple)):
        return all(_plain(x) for x in v)
    return False


def _jsonable(v):
    if isinstance(v, tuple):
        return {'__t__': [_jsonable(x) for x in v]}
    if isinstance(v, list):
        return [_jsonable(x) for x in v]
    return v


def _unjson(v):
    if isinstance(v, dict) and '__t__' in v:
        return tuple(_unjson(x) for x in v['__t__'])
    if isinstance(v, list):
        return [_unjson(x) for x in v]
    return v


def lookup(p, gs, source_lines):
    ''' The field this statement would build, if it has been built before from the same inputs: returns (hit, key), hit being
        (Shape, text it printed) or None; the key is for store() after a miss (None: this statement is not kept) '''
    if not _eligible(p):
        return None, None
    try:
        key = _key(p, gs, source_lines)
    except _No:
        return None, None
    except Exception:
        return None, None
    with _lock:
        got = _memory.get(key)
        if got is not None:
            _counts['hits'] += 1
            return got, key
    got = _read(key)
    if got is not None:
        with _lock:
            _memory[key] = got
            _counts['disk hits'] += 1
            _counts['hits'] += 1
        return got, key
    with _lock:
        _counts['misses'] += 1
    return None, key


def store(key, shape, printed, seconds):
    ''' Keeps a field this statement built (called after a miss), if it took long enough to be worth it '''
    from fieldes.shape import Shape
    if key is None or type(shape) is not Shape or seconds < _MIN_SECONDS:
        return
    with _lock:
        _memory[key] = (shape, printed)
    try:
        _write(key, shape, printed)
    except Exception:
        pass


def _paths(key):
    d = _folder()
    return os.path.join(d, key + '.fdtree'), os.path.join(d, key + '.fdfield')


def _write(key, shape, printed):
    from fieldes.ffi import lib
    if not hasattr(lib, 'libfive_tree_can_save') or not lib.libfive_tree_can_save(shape.ptr):
        return
    extra = {k: v for k, v in vars(shape).items() if k not in ('ptr', '_content_key')}
    if not all(_plain(v) for v in extra.values()):
        return                                         # (something the file cannot hold: kept in memory only)
    tree_path, head_path = _paths(key)
    os.makedirs(os.path.dirname(tree_path), exist_ok=True)
    tmp = tempfile.NamedTemporaryFile(delete=False, dir=os.path.dirname(tree_path), suffix='.part')
    tmp.close()
    try:
        if not lib.libfive_tree_save(shape.ptr, tmp.name.encode('mbcs' if os.name == 'nt' else 'utf-8')):
            return
        os.replace(tmp.name, tree_path)
        with open(head_path + '.part', 'w', encoding='utf-8') as f:
            json.dump({'extra': _jsonable(extra), 'printed': printed}, f)
        os.replace(head_path + '.part', head_path)
        with _lock:
            _counts['kept'] += 1
    finally:
        if os.path.exists(tmp.name):
            os.remove(tmp.name)
    _prune()


def _read(key):
    from fieldes.ffi import lib
    from fieldes.shape import Shape
    tree_path, head_path = _paths(key)
    if not (os.path.exists(tree_path) and os.path.exists(head_path)):
        return None
    try:
        with open(head_path, encoding='utf-8') as f:
            head = json.load(f)
        ptr = lib.libfive_tree_load(tree_path.encode('mbcs' if os.name == 'nt' else 'utf-8'))
        if not ptr:
            return None
        shape = Shape(ptr)
        for k, v in head.get('extra', {}).items():
            setattr(shape, k, _unjson(v))
        os.utime(head_path, None)                      # (used just now: kept longest)
        return shape, head.get('printed', '')
    except Exception:
        return None


def _prune():
    d = _folder()
    try:
        files = [(os.path.getmtime(os.path.join(d, f)), os.path.getsize(os.path.join(d, f)), f) for f in os.listdir(d)
                 if f.endswith(('.fdtree', '.fdfield'))]
    except OSError:
        return
    total = sum(s for _, s, _ in files)
    if total <= _LIMIT_BYTES:
        return
    # oldest first, a pair at a time
    for _, _, f in sorted(files):
        base = f.rsplit('.', 1)[0]
        for ext in ('.fdtree', '.fdfield'):
            try:
                path = os.path.join(d, base + ext)
                total -= os.path.getsize(path)
                os.remove(path)
            except OSError:
                pass
        if total <= 0.8 * _LIMIT_BYTES:
            break
