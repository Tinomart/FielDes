'''
Every function of the library exposes all of its arguments when a call is made for the user (H201, the user, 2026-11-13: "THE FEATURE IN FIELDES THAT MAKES EVERY FUNCTION EXPOSE ITS VARIABLES ON CREATION ... SHOULD
ALWAYS AUTOMATICALLY HAPPEN ... THE MESHING FUNCTION DOES NOT HAVE IT RIGHT NOW, WHICH MEANS IT WAS HARD CODED NOT FUNDAMENTAL").

The check is the rule, not a list of functions: for EVERY callable of the library the completion knows -- whatever its name, whatever module it is in -- the call `name()` is completed (what the right-click
menu and the completion list write), and the result must name every parameter of the function's signature, except the ones that are not arguments of a call (`self`, a name that starts with `_`, `**options`)
and the placement arguments of a function that places itself (the gizmo's: written when it is used).  A function that is written short -- some parameter missing -- is listed, and the check fails.

Run with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/completion_every_function_checks.py

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import inspect
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
from fieldes import *
from fieldes import completion

library = completion._library()
checked, short, unreadable = 0, [], []
for name in sorted(library):
    fn = library[name]
    if not callable(fn) or name in completion._PLAIN or name.startswith('_'):
        continue
    if not completion._is_library_call(ast.parse(name + '()', mode='eval').body):
        continue
    sig = completion._signature(fn)
    if sig is None:
        continue
    text = completion.complete_call(name + '()')
    try:
        node = ast.parse(text.strip(), mode='eval').body
    except SyntaxError:
        unreadable.append(name)
        continue
    written = {k.arg for k in node.keywords}
    positional = len(node.args)
    wanted = []
    for p in sig.parameters.values():
        if p.kind == p.VAR_KEYWORD or p.name.startswith('_') or p.name == 'self':
            continue
        if p.name in ('position', 'rotation', 'scale', 'alpha') and getattr(fn, '_placeable', False):
            continue
        if p.kind == p.VAR_POSITIONAL:
            continue
        wanted.append(p.name)
    missing = [w for w in wanted if w not in written]
    # (a positional-only parameter or one written by position is not a keyword: count the leading ones)
    positional_names = [p.name for p in sig.parameters.values() if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD) and p.name in wanted][:positional]
    missing = [m for m in missing if m not in positional_names]
    checked += 1
    if missing:
        short.append((name, missing))

print('functions of the library whose completed call was checked: %d' % checked)
for name, missing in short:
    print('FAIL  %s is written without %s' % (name, ', '.join(missing)))
for name in unreadable:
    print('FAIL  %s: the completed call cannot be read' % name)
if short or unreadable:
    print('FAILED: %d function(s) are written short or unreadable' % (len(short) + len(unreadable)))
    sys.exit(1)
print('ALL CHECKS PASSED: every function of the library exposes all of its arguments on creation')
