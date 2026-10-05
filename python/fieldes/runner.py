'''
Python library of FielDes, built on the libfive CAD kernel
Copyright (C) 2021  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

import ast
import numbers

class VarTransformer(ast.NodeTransformer):
    def __init__(self):
        self._i = 0

    def check_var(self, node):
        if len(node.args) != 1 or node.keywords:
            return "var() must take 1 argument"
        if isinstance(node.args[0], ast.Constant):
            if not isinstance(node.args[0].value, numbers.Number):
                return "var() argument must be a number"
            else:
                pass # Numerical constant
        elif isinstance(node.args[0], ast.UnaryOp):
            if not isinstance(node.args[0].op, ast.USub):
                return "var() argument must be a constant"
            elif not isinstance(node.args[0].operand, ast.Constant):
                return "var() argument must be a constant"
            elif not isinstance(node.args[0].operand.value, numbers.Number):
                return "var() argument must be a number"
            else:
                pass # Unary subtraction
        else:
            return "var() argument must be a constant"

    def visit_Call(self, node):
        if isinstance(node.func, ast.Name) and node.func.id == 'var':
            # We'll be injecting more arguments into the var() call, but don't
            # want to mess up tagged lines and columns, so we'll use a set of
            # dummy fields that indicate that the new arguments have size 0.
            dummy = {
                'lineno': node.end_lineno,
                'end_lineno': node.end_lineno,
                'col_offset': node.end_col_offset,
                'end_col_offset': node.end_col_offset,
            }

            # Patch the call to be var('error string') if the AST is invalid.
            # This is checked by the var function and re-raised as a
            # RuntimeError, rather than raising it here (which would produce
            # a confusing traceback).
            err = self.check_var(node)
            if err:
                node.args[0] = ast.Constant(value=err, **dummy)
            else:
                # Each var gets a tag explaining where to find it in the text,
                # and is deduplicated based on order in the AST.
                node.args.append(ast.Constant(value=(
                    node.lineno, node.end_lineno,
                    node.col_offset + 4, node.end_col_offset), **dummy))
            self._i += 1
        return self.generic_visit(node) # Recurse, e.g. to patch sphere(var(1))

# Filled in by run() for the FielDes GUI: the (first, last) 1-based source
# lines of every top-level statement, in the same order as run()'s results,
# and a JSON description of the script for the model tree.
last_lines = []
last_scene = ''
# The names of the last run and what they hold (FielDes asks for the numbers of a shape by name)
last_globals = {}
# What the script printed (shown in FielDes's output pane)
last_output = ''


def _count_loops(module):
    ''' Top-level loops (and list / set / dict comprehensions) count their
        items for the progress bar: their iterable goes through
        run_progress.counted '''
    def wrap(it):
        call = ast.Call(func=ast.Name(id='__libfive_counted__', ctx=ast.Load()),
                        args=[it], keywords=[])
        return ast.copy_location(call, it)
    for p in module.body:
        if isinstance(p, ast.For):
            p.iter = wrap(p.iter)
        elif isinstance(p, (ast.Assign, ast.AnnAssign, ast.AugAssign, ast.Expr)):
            v = p.value
            if isinstance(v, (ast.ListComp, ast.SetComp, ast.DictComp)):
                v.generators[0].iter = wrap(v.generators[0].iter)
    ast.fix_missing_locations(module)


def _step_label(source_lines, p):
    ''' A top-level statement as the progress bar names it: its first line '''
    try:
        line = source_lines[p.lineno - 1].strip()
    except IndexError:
        line = ''
    return line if len(line) <= 48 else line[:47].rstrip() + '…'


# The 1-based source line of the breakpoint the last run stopped at, or -1 when
# it ran to the end; and the state of a run stopped there (resume() continues it)
last_paused = -1
_paused = None


def _hit(breakpoints, p, prev_end=0):
    ''' The breakpoint (a 1-based line) that stops the run before the top-level
        statement p, or 0: one inside the statement, or on the blank / comment
        lines since the statement before it (prev_end is that one's last line) '''
    for b in breakpoints or ():
        if prev_end < b <= p.end_lineno:
            return b
    return 0


def _prepare_imports():
    ''' A script can import modules that sit in its own folder (`from bracket import make`).  They are
        loaded again at every run, so that what was edited in another editor tab applies '''
    import os
    import sys
    here = os.path.normcase(os.path.abspath(os.getcwd()))
    if here not in [os.path.normcase(os.path.abspath(p)) for p in sys.path if p]:
        sys.path.append(os.getcwd())
    library = [os.path.normcase(os.path.dirname(os.path.abspath(__file__)))]
    library += [os.path.normcase(os.path.abspath(p)) for p in (sys.prefix, sys.base_prefix) if p]
    for name, module in list(sys.modules.items()):
        f = getattr(module, '__file__', None)
        if not f or name in ('__main__', '_fieldes_host') or name.split('.')[0] == 'fieldes':
            continue
        f = os.path.normcase(os.path.abspath(f))
        if f.startswith(here + os.sep) and not any(f.startswith(l + os.sep) for l in library) \
                and 'site-packages' not in f:
            del sys.modules[name]


def run(s, breakpoints=None, **env):
    ''' Evaluates a string, clause-by-clause.

        Returns a list of values from each expression in the string, or
        raises an error if something went wrong.

        breakpoints: 1-based source lines; the run stops BEFORE the first
        top-level statement that contains one (last_paused is that line) and
        returns what was evaluated so far.  resume() continues it.
    '''
    global _paused
    import contextlib
    import io
    _paused = None
    _prepare_imports()
    parsed = ast.parse(s)
    tagged = VarTransformer().generic_visit(parsed)
    from fieldes import run_progress
    _count_loops(tagged)
    state = {
        's': s,
        'body': tagged.body,
        'source_lines': s.splitlines(),
        'gs': {**env, '__libfive_counted__': run_progress.counted},
        'out': [],
        'lines': [],
        'i': 0,
        'printed': io.StringIO(),
    }
    return _continue(state, breakpoints, skip_first=False)


def resume(breakpoints=None):
    ''' Continues the run stopped at a breakpoint: the statement it stopped
        before runs, then the rest up to the next breakpoint (or the end).
        Returns the results of every statement so far. '''
    if _paused is None:
        raise RuntimeError('no script is stopped at a breakpoint')
    return _continue(_paused, breakpoints, skip_first=True)


class _PartialScenes:
    ''' The model tree while the script is still running.  A statement that takes a while (an import, a smoothing, an
        analysis) used to leave the tree as it was until the whole script was done; now, once a statement has been
        running for DELAY seconds, the tree is given the variables of the statements that are done (host.partial_scene).
        It runs beside the script, in a thread of its own, and only looks at what is done: the script is not slowed by
        it, and a script whose statements are all quick never makes one. '''
    DELAY = 0.15

    def __init__(self, state, host):
        import threading
        self.state, self.host = state, host
        self.sent = 0
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, name='fieldes-partial-scenes', daemon=True)
        self.thread.start()

    def _run(self):
        import time
        st = self.state
        while not self.stop.wait(0.05):
            n = st['i']
            if self.sent < n < len(st['body']) and time.time() - st['t0'] >= self.DELAY:
                self.sent = n
                try:
                    from fieldes.app_support import scene_json
                    text = scene_json(st['s'], dict(st['gs']), list(st['out'][:n]), upto=n, partial=True)
                    if not self.stop.is_set():
                        self.host.partial_scene(text)
                except Exception:       # (the model tree is a convenience only)
                    pass

    def close(self):
        ''' Ends it, after a tree it is making at this moment (the finished script's tree is made after that) '''
        self.stop.set()
        if self.thread.is_alive():
            self.thread.join()

    def error_scene(self):
        ''' The script stopped with an error: the tree lists what the statements before it made (there is no finished
            script's tree to follow) '''
        st = self.state
        try:
            from fieldes.app_support import scene_json
            n = st['i']
            self.host.partial_scene(scene_json(st['s'], dict(st['gs']), list(st['out'][:n]), upto=n))
        except Exception:
            pass


def _partial_scenes(state):
    try:
        import _fieldes_host as host
        return _PartialScenes(state, host) if hasattr(host, 'partial_scene') else None
    except ImportError:         # (not inside the application)
        return None


def _continue(state, breakpoints, skip_first):
    global last_lines, last_scene, last_output, last_paused, _paused, last_globals
    import contextlib
    import time
    from fieldes import field_cache, run_progress
    body, gs, out, lines = state['body'], state['gs'], state['out'], state['lines']
    source_lines = state['source_lines']
    last_paused = -1
    _paused = None
    # Progress: which statement runs (how many there are is known now)
    progress_lib = run_progress._lib
    if progress_lib:
        progress_lib.libfive_run_begin(len(body))
    state['t0'] = time.time()
    watcher = _partial_scenes(state)
    try:
        with contextlib.redirect_stdout(state['printed']):
            while state['i'] < len(body):
                i = state['i']
                p = body[i]
                state['t0'] = time.time()
                b = 0 if (skip_first and state.get('skip') == i) else _hit(
                    breakpoints, p, body[i - 1].end_lineno if i else 0)
                if b:
                    last_paused = b
                    state['skip'] = i
                    _paused = state
                    break
                state['skip'] = -1
                run_progress.end_to(0)
                if progress_lib:
                    progress_lib.libfive_run_step(i, run_progress._b(_step_label(source_lines, p)))
                if isinstance(p, ast.Expr):
                    exp = ast.Expression(p.value)
                    f = compile(exp, '<file>', 'eval')
                    r = eval(f, gs)
                else:
                    mod = ast.Module([p])
                    mod.type_ignores = []
                    f = compile(mod, '<file>', 'exec')
                    # A field built by `name = ...` that has been built before from the same inputs is not built again
                    # (see field_cache.py): in this session, or from the file an earlier session kept
                    hit, key = field_cache.lookup(p, gs, source_lines)
                    if hit is not None:
                        gs[p.targets[0].id] = hit[0]
                        if hit[1]:
                            print(hit[1], end='')
                        r = None
                    else:
                        before = len(state['printed'].getvalue())
                        started = time.time()
                        r = exec(f, gs)
                        if key is not None:
                            field_cache.store(key, gs.get(p.targets[0].id), state['printed'].getvalue()[before:],
                                              time.time() - started)
                out.append(r)
                lines.append((p.lineno, p.end_lineno))
                state['i'] = i + 1
    except BaseException as e:
        # The script stopped with an error: its model tree lists what the statements before it made.  (Not when it
        # was stopped by the application, for a newer edit: that is not an error of the script's)
        if watcher and not (type(e) is Exception and not e.args):
            watcher.close()
            watcher.error_scene()
        raise
    finally:
        if watcher:
            watcher.close()
        last_output = state['printed'].getvalue()
        run_progress.end_to(0)
        if progress_lib:
            progress_lib.libfive_run_end()
    last_lines = list(lines)
    last_globals = gs
    try:
        from fieldes.app_support import scene_json
        last_scene = scene_json(state['s'], gs, out)
    except Exception as e:      # the model tree is a convenience only
        import json
        last_scene = json.dumps({'error': '{}: {}'.format(type(e).__name__, e)})
    return list(out)
