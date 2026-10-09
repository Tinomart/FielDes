'''
One cache for every analysis a script solves, kept across sessions.

The field cache (fieldes/field_cache.py) hands back a field that was built before from the same inputs.  Analyses --
static_analysis, modal_analysis, thermal_analysis, fluid_analysis, the topology optimisations -- are remembered the
same way: the whole problem as asked (the part by the content of its expression, every condition, the material, the
element size, every setting) is the key; a problem asked again in the same session is handed back from memory (each
analysis keeps its own few), and in a later session its solved form is read back from a file the kernel wrote
(`libfive_*_save`: the mesh, every field, the modes, the density after every iteration, the steps of a flow).  Nothing
is meshed or solved again; a change to any argument is a new problem, solved and kept beside the old one.

The results' field keys are made a function of the same problem key (the kernel's `set_salt`), so what was rendered
of a result is found in the render cache again too.

    stats()    {'hits': .., 'misses': .., 'kept': ..}  -- the files read, the solves that had no file, the files written

The files are in `result-cache` beside the field cache (FIELDES_RESULT_CACHE_DIR sets another folder); the oldest go
once they hold more than 4 GB.  A file the kernel cannot read as written is refused and the problem is solved.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import hashlib
import json
import os
import sys
import tempfile
import threading
import time

from fieldes.stdlib.content_cache import is_local

__all__ = ['stats', 'load', 'save', 'salt']


def _log(msg):
    ''' A line to FielDes's log (the process's stderr): never an error -- the embedded interpreter of the
        application has no sys.stderr while a script runs '''
    for out in (sys.stderr, sys.__stderr__):
        if out is not None:
            try:
                out.write(msg)
                out.flush()
                return
            except Exception:
                pass
    try:
        os.write(2, msg.encode('utf-8', 'replace'))
    except Exception:
        pass

_lock = threading.RLock()
_counts = {'hits': 0, 'misses': 0, 'kept': 0}
_LIMIT_BYTES = 4 * 1024 ** 3
_KINDS = ('fea', 'tetfea', 'tetthermal', 'tetflow', 'thermal')


def stats():
    ''' What the cache did: {'hits': .., 'misses': .., 'kept': ..} '''
    with _lock:
        return dict(_counts)


def _folder():
    d = os.environ.get('FIELDES_RESULT_CACHE_DIR')
    if d:
        return d
    from fieldes import field_cache
    return os.path.join(os.path.dirname(os.path.abspath(field_cache._folder())), 'result-cache')


# What the solvers' results are, as a number: it changes when a change to a solver changes what it gives for the same problem (a different
# optimiser, another mesh), and only then -- so that a program that is rebuilt or updated reads back what an earlier one solved
RESULT_VERSION = 4


def _key(kind, ekey):
    text = repr(('fdrc', RESULT_VERSION, kind, ekey))
    return hashlib.sha1(text.encode('utf-8', 'surrogatepass')).hexdigest()


def salt(kind, ekey):
    ''' The kernel's salt for this problem's result serials (never 0) '''
    return int(_key(kind, ekey)[:16], 16) | 1


def _paths(kind, ekey):
    base = os.path.join(_folder(), kind + '-' + _key(kind, ekey))
    return base + '.fdresult', base + '.json'


def _fs(path):
    return path.encode('utf-8')


def load(kind, ekey, what):
    ''' The solved problem of this key, read back from its file: (handle pointer, extras dict), or None.
        `what` names the analysis in the message FielDes logs. '''
    if ekey is None or kind not in _KINDS or is_local(ekey):
        return None
    from fieldes.ffi import lib
    loader = getattr(lib, 'libfive_%s_load' % kind, None)
    if loader is None:
        return None
    data, head = _paths(kind, ekey)
    if not (os.path.exists(data) and os.path.exists(head)):
        with _lock:
            _counts['misses'] += 1
        return None
    try:
        with open(head, encoding='utf-8') as f:
            extras = json.load(f)
        t0 = time.time()
        ptr = loader(_fs(data))
        if not ptr:
            raise OSError('not readable as written')
        os.utime(head, None)                      # (used just now: kept longest)
        with _lock:
            _counts['hits'] += 1
        _log('[result cache] read the %s back (%.1f MB, %.2f s)\n' % (
            what, os.path.getsize(data) / 1048576.0, time.time() - t0))
        return ptr, extras if isinstance(extras, dict) else {}
    except Exception as e:
        _log('[result cache] the file of the %s is not usable (%s): solving\n' % (what, e))
        with _lock:
            _counts['misses'] += 1
        return None


def save(kind, ekey, ptr, what, extras=None):
    ''' Keeps the solved problem behind `ptr` for this key, with the extras (plain JSON) the Python result needs '''
    if ekey is None or kind not in _KINDS or not ptr or is_local(ekey):       # (a part only this session can recognise: no file another could find)
        return
    from fieldes.ffi import lib
    saver = getattr(lib, 'libfive_%s_save' % kind, None)
    if saver is None:
        return
    data, head = _paths(kind, ekey)
    try:
        os.makedirs(os.path.dirname(data), exist_ok=True)
        tmp = tempfile.NamedTemporaryFile(delete=False, dir=os.path.dirname(data), suffix='.part')
        tmp.close()
        try:
            if not saver(ptr, _fs(tmp.name)):
                return
            os.replace(tmp.name, data)
        finally:
            if os.path.exists(tmp.name):
                os.remove(tmp.name)
        with open(head + '.part', 'w', encoding='utf-8') as f:
            json.dump(extras or {}, f)
        os.replace(head + '.part', head)
        with _lock:
            _counts['kept'] += 1
        _log('[result cache] kept the %s (%.1f MB)\n' % (what, os.path.getsize(data) / 1048576.0))
    except Exception as e:                       # (a full disk, a folder that cannot be made: the result is still good)
        _log('[result cache] could not keep the %s: %s\n' % (what, e))
        return
    _prune()


def _prune():
    d = _folder()
    try:
        files = [(os.path.getmtime(os.path.join(d, f)), os.path.getsize(os.path.join(d, f)), f) for f in os.listdir(d)
                 if f.endswith(('.fdresult', '.json'))]
    except OSError:
        return
    total = sum(s for _, s, _ in files)
    if total <= _LIMIT_BYTES:
        return
    for _, _, f in sorted(files):               # oldest first, a pair at a time
        base = f.rsplit('.', 1)[0]
        for ext in ('.fdresult', '.json'):
            try:
                path = os.path.join(d, base + ext)
                total -= os.path.getsize(path)
                os.remove(path)
            except OSError:
                pass
        if total <= 0.8 * _LIMIT_BYTES:
            break
