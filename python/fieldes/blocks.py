'''
Custom blocks: your own functions, kept in a folder, there in every script.

A block is a function you write once, in a file of the blocks folder, and then use like any function of the library
in any script you open:

    # blocks/rib.py
    def rib(base, thickness=2.0, height=10.0):
        \'\'\' A rib standing on the top of a body \'\'\'
        top = base.max(-(Shape.Z() - ...))
        return union(base, ...)

    # any script
    part = rib(box_1, 3)

That is all: every function in a file of the folder whose name does not start with an underscore is a block, named as the
function is.  A block is written with the whole library at hand (no imports needed), and may use the blocks of the files
before it.  Its docstring's first line is what the call tip and the menus say about it.  Save the file and the scripts that use it
run again; the editor completes it, shows its call tip and goes to its definition (Ctrl+click).

The folder is the one chosen in Settings > Blocks folder; by default `blocks` next to FielDes (and the python folder).  A block
that has no required argument but the first is offered in the right-click menus: in "Add operation" under Custom blocks
when its first argument is the model to work on, in "New custom block" when it needs none.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import inspect
import os
import sys
import threading
import traceback

_LOCK = threading.RLock()
_BLOCKS = {}        # name -> {'fn', 'file', 'line'}
_FILES = {}         # path -> mtime of the version loaded
_ERRORS = {}        # path -> what went wrong when it was loaded
_EVER = set()      # every name a block has had in this session (a script that uses one that is gone is to be run again)


def folder():
    ''' The blocks folder: FIELDES_BLOCKS (what the application sets from its settings), else `blocks` next to the
        python folder this library is in '''
    chosen = os.environ.get('FIELDES_BLOCKS')
    if chosen:
        return chosen
    here = os.path.dirname(os.path.abspath(__file__))              # .../python/fieldes
    return os.path.join(os.path.dirname(os.path.dirname(here)), 'blocks')


def _library():
    ''' What a block is written with: the library, as `from fieldes import *` gives it '''
    import fieldes
    return {name: getattr(fieldes, name) for name in fieldes.__all__}


def _load(path):
    ''' Runs one file of the folder; returns the blocks it defines, or raises '''
    with open(path, encoding='utf-8') as f:
        source = f.read()
    ns = _library()
    library = set(ns)
    ns.update({name: b['fn'] for name, b in _BLOCKS.items() if b['file'] != path})    # (the blocks before it)
    stem = os.path.splitext(os.path.basename(path))[0]
    ns['__name__'] = 'fieldes_block_' + stem
    ns['__file__'] = path
    exec(compile(source, path, 'exec'), ns)
    found = {}
    for name, value in list(ns.items()):
        if name.startswith('_') or not inspect.isfunction(value) or value.__module__ != ns['__name__']:
            continue
        if name in library:
            raise ValueError('the block {!r} has the name of a function of the library: call it something else'
                             .format(name))
        try:
            line = inspect.getsourcelines(value)[1]
        except (OSError, TypeError):
            line = 1
        found[name] = {'fn': value, 'file': path, 'line': line}
    return found


def refresh():
    ''' Reads the files of the folder that are new or have changed since they were read, forgets what is gone; True when
        anything changed '''
    with _LOCK:
        d = folder()
        files = {}
        if os.path.isdir(d):
            for name in sorted(os.listdir(d)):
                if name.endswith('.py') and not name.startswith('_'):
                    p = os.path.join(d, name)
                    try:
                        files[p] = os.stat(p).st_mtime_ns
                    except OSError:
                        pass
        changed = False
        for p in list(_FILES):
            if p not in files:
                del _FILES[p]
                _ERRORS.pop(p, None)
                for n in [n for n, b in _BLOCKS.items() if b['file'] == p]:
                    del _BLOCKS[n]
                changed = True
        for p, mtime in files.items():
            if _FILES.get(p) == mtime:
                continue
            changed = True
            for n in [n for n, b in _BLOCKS.items() if b['file'] == p]:
                del _BLOCKS[n]
            _FILES[p] = mtime
            _ERRORS.pop(p, None)
            try:
                found = _load(p)
                for n in found:
                    if n in _BLOCKS:
                        raise ValueError('the block {!r} is in {} as well'.format(n, os.path.basename(_BLOCKS[n]['file'])))
                _BLOCKS.update(found)
                _EVER.update(found)
            except BaseException as e:
                if isinstance(e, (KeyboardInterrupt, SystemExit)):
                    raise
                _ERRORS[p] = '{}: {}'.format(type(e).__name__, e) if not isinstance(e, SyntaxError) \
                    else 'SyntaxError: {} (line {})'.format(e.msg, e.lineno)
        return changed


def namespace():
    ''' The blocks as {name: function}, for a script to run with '''
    refresh()
    with _LOCK:
        return {name: b['fn'] for name, b in _BLOCKS.items()}


def names():
    refresh()
    with _LOCK:
        return sorted(_BLOCKS)


def errors():
    ''' [(file, what went wrong)] for the files that could not be read '''
    refresh()
    with _LOCK:
        return sorted(_ERRORS.items())


def info():
    ''' The blocks, for the menus: [{name, file, line, signature, doc, first, needs_body, required}] -- `first` is the name of
        the first parameter, `needs_body` whether a block can be called with a model alone (all other parameters have defaults) '''
    refresh()
    out = []
    with _LOCK:
        for name, b in sorted(_BLOCKS.items()):
            fn = b['fn']
            try:
                sig = inspect.signature(fn)
            except (TypeError, ValueError):
                continue
            params = [p for p in sig.parameters.values()
                      if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD, p.KEYWORD_ONLY)]
            required = [p.name for p in params if p.default is p.empty]
            doc = (inspect.getdoc(fn) or '').strip().split('\n', 1)[0]
            out.append({'name': name, 'file': b['file'], 'line': b['line'], 'signature': str(sig), 'doc': doc,
                        'first': params[0].name if params else '', 'required': required,
                        'operation': bool(required) and len(required) == 1,
                        'primitive': not required})
    return out


def records():
    ''' The tab-separated records the editor's completion tables take (see app_support.completion_info) '''
    out = []
    for b in info():
        out.append('def\t{}\t{}\t{}'.format(b['name'], b['file'], b['line']))
        tip = b['name'] + b['signature'] + ('  --  ' + b['doc'] if b['doc'] else '')
        out.append('tip\t{}\t{}'.format(b['name'], tip.replace('\t', ' ').replace('\n', ' ')))
    return out
