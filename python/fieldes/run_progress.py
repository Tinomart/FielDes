'''
A script run's progress, for FielDes's progress bar (see run_progress.hpp).

The runner (runner.py) says which top-level statement runs; the operations
inside one count how far they are: FEA solves and optimisations (in C++),
STEP imports (their own count), top-level loops (the runner counts their
items), loading a cached import (its tree files), and progress() for your
own long computations.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

import contextlib

try:
    from fieldes.ffi import lib as _lib
    _lib.libfive_run_task_begin      # (an older library: no progress, no harm)
except Exception:
    _lib = None

# The operations begun from Python, innermost last: (name, depth) -- the
# C++ side keeps the real stack; this one tells progress() whether its own
# operation is open at the current depth
_open = []


def _b(text):
    return str(text).encode('utf-8', 'replace')


def begin(name, shown=None):
    ''' shown: the name the bar shows (default: name) '''
    if _lib:
        _lib.libfive_run_task_begin(_b(name if shown is None else shown))
    _open.append(name)


def end():
    if _open:
        _open.pop()
        if _lib:
            _lib.libfive_run_task_end()


def end_to(depth):
    ''' Ends the operations begun deeper than depth (left open by a
        progress() call, or by an error) '''
    while len(_open) > depth:
        end()


def report(fraction, detail=''):
    if _lib:
        _lib.libfive_run_task_set(float(fraction), _b(detail))


def span(a, b):
    if _lib:
        _lib.libfive_run_task_span(float(a), float(b))


@contextlib.contextmanager
def task(name):
    ''' with task('meshing'): ... report(f) ...  -- an operation that counts
        how far it is, 0..1 (report(f)); one begun inside it counts within the share
        span(a, b) gave it '''
    depth = len(_open)
    begin(name)
    try:
        yield
    finally:
        end_to(depth)


def counted(items, name='item'):
    ''' Yields items, counting them as the progress of the current step (or
        of the operation it runs in); what runs for one item counts within
        that item's share.  Items with no len(): counted, but how far
        along isn't known. '''
    try:
        n = len(items)
    except Exception:
        n = None
    depth = len(_open)
    begin(name)
    try:
        for i, x in enumerate(items):
            end_to(depth + 1)
            if n:
                span(i / n, (i + 1) / n)
                report(i / n, '{} {} of {}'.format(name, i + 1, n))
            else:
                report(0.0, '{} {}'.format(name, i + 1))
            yield x
    finally:
        end_to(depth)


def progress(fraction, text=''):
    ''' Says how far a long computation of yours is, 0..1, for the progress
        bar (text: what it is doing).  Call it as often as you like, e.g.
        progress(i / n, 'smoothing') in a loop; it counts within whatever
        runs it (a loop's item, a step of the script). '''
    top = _open[-1] if _open else None
    if top != '_progress':
        begin('_progress', '')
    report(fraction, text)
