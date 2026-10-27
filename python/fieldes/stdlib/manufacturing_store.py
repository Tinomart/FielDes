'''
Manufacturing results, kept from one session to the next.

manufacturing_analysis and manufacturing_optimization measure a part and shape it, which costs seconds to minutes.  A call that was made before in the same session is handed back
from memory (`_remembered`, manufacturing_field.py); this module keeps the result in a folder, so that the next session -- or a program that carries the result with it, as the guided
tour does (docs/caching-and-performance.md) -- reads it back and measures and shapes nothing.

What a result is, to the file: a few FIELDS (expression trees; the correction lattices of the kernel in them know how to save themselves), the checks of the part (a
ManufacturingAnalysis: plain numbers, lists and more fields) and plain numbers.  All the fields go into ONE file of the kernel (`libfive_trees_save`: what they share -- the
lattices above all, which are tens of millions of nodes for a small part -- is written once, and a lattice writes a stretch of one value as a count), everything else into one
JSON head that says where each field goes back.  The part and the constraints the call was given are not written: the result is read back onto the ones of the call that asks
(the key holds them by content, so they are the same).  Anything the files cannot hold -- an imported mesh in the part, a function in an object -- makes the result stay in memory
only: nothing is half kept, and the log says why.

The key is the whole call as made (the part by its expression and the numbers of its variables, the constraints, every setting -- with every argument the call did not give filled in
with its default), the version of this module, the content of the manufacturing files of the library and the environment switches that change what they make.  A script with var()s
is kept too (a part that was selected has its placement and its handles as var()s): the fields are written with the numbers the variables have, which the key holds, so that what is
read back is the result for those numbers (and does not follow a variable that is dragged: another number is another call).

    stats()   {'hits': .., 'misses': .., 'kept': ..}

The files (`head.json`, `trees.fdtrees`) are in `manufacturing/<key>/` beside the result cache of the solvers (FIELDES_RESULT_CACHE_DIR moves both); the program's own `seed-cache/result-cache/manufacturing/`
is read when the user's folder has none (FIELDES_NO_SEED=1 reads none).  The oldest go once the folder holds more than 3 GB.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import hashlib
import importlib
import json
import os
import shutil
import tempfile
import threading
import time
from collections import OrderedDict

__all__ = ['stats', 'load', 'save']

_VERSION = 1            # (1: the first; raise it when a change of the kernel's lattices or of anything but these files changes what the tools make)
_LIMIT_BYTES = 3 * 1024 ** 3
_MAX_ENTRY = 600 * 1024 ** 2          # (one result may take this much of it: a topology-optimized part has lattices of tens of millions of nodes)
_ENV = ('FIELDES_BAKE', 'FIELDES_SCOPE', 'FIELDES_WALLS_LATTICE', 'FIELDES_UNDERCUT_LATTICE', 'FIELDES_DRAFT_LATTICE', 'FIELDES_NO_CLIP', 'FIELDES_CLIP')

_lock = threading.RLock()
_counts = {'hits': 0, 'misses': 0, 'kept': 0}
_code_hash = None


def stats():
    ''' What the store did: {'hits': .., 'misses': .., 'kept': ..} '''
    with _lock:
        return dict(_counts)


def _log(text):
    from fieldes.stdlib.result_cache import _log as log
    log(text)


class _Cannot(Exception):
    ''' This result cannot be written to a file '''


# ---------------------------------------------------------------------------------------------------------------------
# Where, and under which key

def _folder():
    from fieldes.stdlib import result_cache
    return os.path.join(result_cache._folder(), 'manufacturing')


def _seed():
    from fieldes.stdlib import result_cache
    d = result_cache._seed_folder()
    return os.path.join(d, 'manufacturing') if d else None


def _code():
    ''' The content of the manufacturing files of the library (line endings as one: the same files on another system are the same) '''
    global _code_hash
    if _code_hash is None:
        here = os.path.dirname(os.path.abspath(__file__))
        h = hashlib.sha1()
        for name in sorted(os.listdir(here)):
            if name.startswith('manufacturing') and name.endswith('.py'):
                with open(os.path.join(here, name), 'rb') as f:
                    h.update(name.encode('ascii'))
                    h.update(f.read().replace(b'\r\n', b'\n'))
        _code_hash = h.hexdigest()
    return _code_hash


def _key(name, key):
    ''' The folder name of this call, or None when nothing is to be kept for it '''
    from fieldes.stdlib.content_cache import is_local
    if is_local(key):
        return None                                   # (a part only this session can recognise)
    # (a script with var()s -- a part that was selected has its placement and its handles as var()s -- is kept too: the key holds the numbers of the variables the part is made with, and the fields are
    # written with those numbers where the variables were, see `save`)
    text = repr(('fdmr', _VERSION, _code(), name, key, tuple(os.environ.get(e, '') for e in _ENV)))
    return hashlib.sha1(text.encode('utf-8', 'surrogatepass')).hexdigest()


def _given(args, kwargs):
    ''' The objects of the call that the result may refer to (the part, the constraints, the regions), in the order the call holds them: what the result shares with them is written as
        a mark, and read back as the one of the call that asks '''
    from fieldes.shape import Shape
    out = []

    def walk(v):
        if isinstance(v, (list, tuple)):
            for x in v:
                walk(x)
        elif isinstance(v, dict):
            for k in sorted(v, key=str):
                walk(v[k])
        elif isinstance(v, Shape) or (type(v).__module__ or '').startswith('fieldes.'):
            out.append(v)

    walk(list(args))
    walk(dict(kwargs))
    return out


def _path(p):
    return p.encode('mbcs' if os.name == 'nt' else 'utf-8')


# ---------------------------------------------------------------------------------------------------------------------
# A result as a head and files

_SKIP_SHAPE = ('ptr', '_content_key', '_made_from')
_SKIP_OBJECT = ('_resolving',)


class _Writer:
    def __init__(self, given):
        from fieldes.ffi import lib
        self.lib = lib
        self.given = {}
        for i, o in enumerate(given):
            self.given.setdefault(id(o), i)
        self.trees = []               # the fields that go into files, in order
        self.memo = {}                # id of a field or object -> its number
        self.alive = []               # (what is numbered stays alive while the numbers are used)
        self.depth = 0

    def enc(self, v):
        if v is None or isinstance(v, (bool, int, float, str)):
            return v
        i = self.given.get(id(v))
        if i is not None:
            return {'k': 'G', 'i': i}
        self.depth += 1
        try:
            if self.depth > 80:
                raise _Cannot('a result that holds itself')
            return self._enc(v)
        finally:
            self.depth -= 1

    def _enc(self, v):
        from fieldes.shape import Shape
        kind = type(v)
        if kind is tuple:
            return {'k': 't', 'v': [self.enc(x) for x in v]}
        if kind is list:
            return {'k': 'l', 'v': [self.enc(x) for x in v]}
        if kind is dict or kind is OrderedDict:
            return {'k': 'D' if kind is dict else 'O', 'v': [[self.enc(a), self.enc(b)] for a, b in v.items()]}
        seen = self.memo.get(id(v))
        if seen is not None:
            return {'k': 'R', 'r': seen}
        if kind is Shape:
            if not hasattr(self.lib, 'libfive_tree_can_save') or not self.lib.libfive_tree_can_save(v.ptr):
                raise _Cannot('a field holds something that cannot be written to a file')
            number = len(self.memo)
            self.memo[id(v)] = number
            self.alive.append(v)
            index = len(self.trees)
            self.trees.append(v)
            attrs = [[k, self.enc(x)] for k, x in vars(v).items() if k not in _SKIP_SHAPE]
            return {'k': 'T', 'r': number, 'f': index, 'a': attrs}
        module = kind.__module__ or ''
        if module.startswith('fieldes.') and hasattr(v, '__dict__') and not callable(v):
            number = len(self.memo)
            self.memo[id(v)] = number
            self.alive.append(v)
            attrs = [[k, None if k == '_shown' else self.enc(x)] for k, x in vars(v).items() if k not in _SKIP_OBJECT]
            return {'k': 'o', 'r': number, 'c': module + ':' + kind.__qualname__, 'a': attrs}
        raise _Cannot('a %s.%s' % (module, kind.__qualname__))


class _Reader:
    def __init__(self, folder, given, count):
        import ctypes
        from fieldes.ffi import lib
        self.lib = lib
        self.given = given
        self.refs = {}
        self.taken = set()
        self.ptrs = (ctypes.c_void_p * max(1, count))()
        if count:
            # (all the fields, from the one file they were written to: what they share is read once)
            if not hasattr(lib, 'libfive_trees_load') or lib.libfive_trees_load(_path(os.path.join(folder, 'trees.fdtrees')), self.ptrs, count) != count:
                raise OSError('the fields could not be read')
        self.count = count

    def release(self):
        ''' The fields of the file that no object took (a result that could not be read whole) '''
        for i in range(self.count):
            if i not in self.taken and self.ptrs[i]:
                self.lib.libfive_tree_delete(self.ptrs[i])
                self.ptrs[i] = None

    def dec(self, j):
        if j is None or isinstance(j, (bool, int, float, str)):
            return j
        k = j['k']
        if k == 'G':
            return self.given[j['i']]
        if k == 't':
            return tuple(self.dec(x) for x in j['v'])
        if k == 'l':
            return [self.dec(x) for x in j['v']]
        if k in ('D', 'O'):
            out = OrderedDict() if k == 'O' else {}
            for a, b in j['v']:
                out[self.dec(a)] = self.dec(b)
            return out
        if k == 'R':
            return self.refs[j['r']]
        if k == 'T':
            from fieldes.shape import Shape
            ptr = self.ptrs[j['f']]
            if not ptr or j['f'] in self.taken:
                raise OSError('a field is not in the file')
            self.taken.add(j['f'])
            shape = Shape(ptr)
            self.refs[j['r']] = shape
            for name, x in j['a']:
                shape.__dict__[name] = self.dec(x)
            return shape
        if k == 'o':
            module, _, qualname = j['c'].partition(':')
            if not module.startswith('fieldes.'):
                raise ValueError('not a class of the library')
            cls = importlib.import_module(module)
            for part in qualname.split('.'):
                cls = getattr(cls, part)
            obj = cls.__new__(cls)
            self.refs[j['r']] = obj
            for name, x in j['a']:
                obj.__dict__[name] = self.dec(x)
            return obj
        raise ValueError('unknown entry %r' % (k,))


def _size(folder):
    total = 0
    for root, _, files in os.walk(folder):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(root, f))
            except OSError:
                pass
    return total


def load(name, key, args, kwargs, what):
    ''' The result of this call, read back from the folder of the user or the program's own: or None.  `key` is the call's content key (manufacturing_field._remembered),
        `what` names the tool in the message FielDes logs '''
    folder = _key(name, key)
    if folder is None:
        return None
    found, shipped = None, False
    for base, own in ((_folder(), True), (_seed(), False)):
        if base and os.path.isfile(os.path.join(base, folder, 'head.json')):
            found, shipped = os.path.join(base, folder), not own
            break
    if found is None:
        with _lock:
            _counts['misses'] += 1
        return None
    try:
        t0 = time.time()
        with open(os.path.join(found, 'head.json'), encoding='utf-8') as f:
            head = json.load(f)
        given = _given(args, kwargs)
        if head.get('version') != _VERSION or head.get('given') != len(given):
            return None
        reader = _Reader(found, given, int(head.get('trees', 0)))
        try:
            value = reader.dec(head['value'])
        except Exception:
            reader.release()
            raise
        if not shipped:
            try:
                os.utime(os.path.join(found, 'head.json'), None)               # (used just now: kept longest)
            except OSError:
                pass
        with _lock:
            _counts['hits'] += 1
        _log('[manufacturing store] read the %s back (%.1f MB, %.2f s%s)\n' % (what, _size(found) / 1048576.0, time.time() - t0, ', shipped with the program' if shipped else ''))
        return value
    except Exception as e:
        _log('[manufacturing store] the files of the %s are not usable (%s): making it\n' % (what, e))
        with _lock:
            _counts['misses'] += 1
        return None


def save(name, key, value, args, kwargs, what):
    ''' Keeps the result of this call for the next session, if it can be written whole '''
    folder = _key(name, key)
    if folder is None or value is None:
        return
    t0 = time.time()
    given = _given(args, kwargs)
    writer = _Writer(given)
    try:
        head = writer.enc(value)
    except _Cannot as e:
        _log('[manufacturing store] the %s stays in memory only: %s\n' % (what, e))
        return
    except Exception as e:
        _log('[manufacturing store] the %s stays in memory only (%s: %s)\n' % (what, type(e).__name__, e))
        return
    base = _folder()
    tmp = None
    try:
        os.makedirs(base, exist_ok=True)
        tmp = tempfile.mkdtemp(prefix=folder + '.', suffix='.part', dir=base)
        count = len(writer.trees)
        if count:
            # (all the fields into ONE file: what they share -- the lattices, the part -- is written once)
            import ctypes
            ptrs = (ctypes.c_void_p * count)(*[t.ptr for t in writer.trees])
            target = _path(os.path.join(tmp, 'trees.fdtrees'))
            known = None
            try:
                from fieldes.app_support import _var_values
                known = _var_values()                 # (the script's var()s and the numbers they have: written in their place)
            except Exception:
                known = None
            if known is None:
                if not hasattr(writer.lib, 'libfive_trees_save'):
                    raise _Cannot('this library writes one field to a file, not several')
                saved = writer.lib.libfive_trees_save(ptrs, count, target)
            else:
                if not hasattr(writer.lib, 'libfive_trees_save_bound'):
                    raise _Cannot('this library cannot write fields that hold variables')
                saved = writer.lib.libfive_trees_save_bound(ptrs, count, known[0], known[1], known[2], target)
            if not saved:
                raise _Cannot('the fields could not be written')
            size = _size(tmp)
            if size > _MAX_ENTRY:
                raise _Cannot('it is %.0f MB, more than the %.0f MB a result may take' % (size / 1048576.0, _MAX_ENTRY / 1048576.0))
        with open(os.path.join(tmp, 'head.json'), 'w', encoding='utf-8') as f:
            json.dump({'version': _VERSION, 'name': name, 'given': len(given), 'trees': count, 'value': head}, f)
        final = os.path.join(base, folder)
        if os.path.isdir(final):
            shutil.rmtree(final, ignore_errors=True)
        os.replace(tmp, final)
        tmp = None
        with _lock:
            _counts['kept'] += 1
        _log('[manufacturing store] kept the %s (%.1f MB, %.2f s, %d fields)\n' % (what, _size(final) / 1048576.0, time.time() - t0, count))
    except Exception as e:                     # (a full disk, a folder that cannot be made: the result is still good)
        _log('[manufacturing store] could not keep the %s: %s\n' % (what, e))
    finally:
        if tmp is not None:
            shutil.rmtree(tmp, ignore_errors=True)
    _prune()


def _prune():
    d = _folder()
    try:
        names = os.listdir(d)
    except OSError:
        return
    now = time.time()
    entries = []
    for n in names:
        p = os.path.join(d, n)
        head = os.path.join(p, 'head.json')
        try:
            if n.endswith('.part'):
                if now - os.path.getmtime(p) > 86400:
                    shutil.rmtree(p, ignore_errors=True)         # (left by a program that stopped while it was writing)
            elif os.path.isfile(head):
                entries.append((os.path.getmtime(head), _size(p), p))
        except OSError:
            pass
    total = sum(s for _, s, _ in entries)
    if total <= _LIMIT_BYTES:
        return
    for _, s, p in sorted(entries):                          # oldest first
        shutil.rmtree(p, ignore_errors=True)
        total -= s
        if total <= 0.8 * _LIMIT_BYTES:
            break
