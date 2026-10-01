'''
Content-keyed caches for the constructions that cost real time.

A script runs again on every edit.  Most of what it builds is a lazy expression
that costs nothing until it is evaluated, but a few constructions do real work
when they are called: meshing a shape and building the search structure of an
exact distance field, importing a mesh file, building a graph lattice, measuring
a volume.  Their results are remembered by *what they were built from* -- the
shape's expression (not its identity), the numbers, the file's path, size and
modification time -- so running the script again, or moving a slider that does
not touch them, asks nothing twice.  Change the shape, a number or the file and
the key differs: the new result is built and the old one stays until it is the
one used least.

    cache_info()      what is held: {name: (entries, hits, misses)}
    clear_caches()    forget everything (to measure, or to free the memory)

Nothing here approximates: a hit returns the very object a miss built.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import collections
import copy
import ctypes
import functools
import threading

__all__ = ['cache_info', 'clear_caches', 'content_cached']

_lock = threading.RLock()
_caches = {}


class _Cache:
    def __init__(self, name, limit):
        self.name = name
        self.limit = limit
        self.items = collections.OrderedDict()
        self.hits = 0
        self.misses = 0

    def get(self, key):
        with _lock:
            if key in self.items:
                self.items.move_to_end(key)
                self.hits += 1
                return True, self.items[key]
            self.misses += 1
            return False, None

    def put(self, key, value):
        with _lock:
            self.items[key] = value
            self.items.move_to_end(key)
            while len(self.items) > self.limit:
                self.items.popitem(last=False)


def cache_for(name, limit=8):
    ''' The named in-memory cache (made on first use) holding at most `limit` entries, least recently used out first '''
    with _lock:
        if name not in _caches:
            _caches[name] = _Cache(name, limit)
        return _caches[name]


def cache_info():
    ''' What the content caches hold: {name: (entries, hits, misses)} '''
    with _lock:
        return {name: (len(c.items), c.hits, c.misses) for name, c in _caches.items()}


def clear_caches():
    ''' Forgets everything the content caches hold '''
    with _lock:
        for c in _caches.values():
            c.items.clear()
            c.hits = c.misses = 0


class Uncacheable(Exception):
    ''' A value with no content key '''


def shape_key(shape):
    ''' An exact key of a shape's expression: the same expression built again -- a script run again -- has
        the same key, and a different one does not (a constant differing in the seventh digit, another
        imported mesh, another data field: all different).  It is the library's structural hash of the
        tree, not its printed text.  (Raises Uncacheable when the library has no such key.) '''
    from fieldes.ffi import lib
    mine = getattr(shape, '_content_key', None)
    if mine is not None and mine[0] == shape.ptr:
        return mine[1]
    fn = getattr(lib, 'libfive_tree_content_key', None)
    if fn is None:
        raise Uncacheable()
    p = fn(shape.ptr)
    if not p:
        raise Uncacheable()
    try:
        key = ('shape', ctypes.string_at(p).decode('ascii'))
    finally:
        lib.libfive_free_str(ctypes.cast(p, ctypes.c_char_p))
    try:
        shape._content_key = (shape.ptr, key)      # (asked once per shape)
    except Exception:
        pass
    return key


def value_key(v):
    ''' A hashable key for an argument, by its content: numbers (every bit), text, sequences, shapes (by
        their expression), and the library's own small objects (a material, a support, a load: by their
        fields).  Raises Uncacheable for anything else '''
    if v is None or isinstance(v, (int, str, bytes)) and not isinstance(v, bool):
        return v
    if isinstance(v, bool):
        return ('bool', v)
    if isinstance(v, float):
        return ('f', v.hex())
    from fieldes.shape import Shape
    if isinstance(v, Shape):
        return shape_key(v)
    if isinstance(v, (list, tuple)):
        return (type(v).__name__,) + tuple(value_key(x) for x in v)
    if isinstance(v, dict):
        return ('dict',) + tuple(sorted((str(k), value_key(x)) for k, x in v.items()))
    kind = type(v)
    if (kind.__module__ or '').startswith('fieldes.') and hasattr(v, '__dict__') and not callable(v):
        return ('obj', kind.__module__, kind.__qualname__) + tuple(
            sorted((k, value_key(x)) for k, x in vars(v).items()))
    raise Uncacheable()


def problem_key(kind, **parts):
    ''' A key of a whole problem -- a static analysis, a thermal analysis: what it is made of, each part by its
        content -- or None when some part has no content key (then nothing is remembered) '''
    try:
        return (kind,) + tuple((k, value_key(x)) for k, x in sorted(parts.items()))
    except Uncacheable:
        return None


def content_cached(name, limit=8, copy_result=False):
    ''' A decorator: the function's results are remembered by the content of its arguments.
        copy_result: hand out a copy of a mutable result (a dict) each time '''
    def deco(fn):
        cache = cache_for(name, limit)

        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            try:
                key = (value_key(args), value_key(kwargs))
            except Uncacheable:
                return fn(*args, **kwargs)
            hit, value = cache.get(key)
            if not hit:
                value = fn(*args, **kwargs)
                cache.put(key, value)
            return copy.deepcopy(value) if copy_result else value
        wrapper.cache = cache
        return wrapper
    return deco
