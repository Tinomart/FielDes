'''
Python library of FielDes, built on the libfive CAD kernel
Copyright (C) 2021  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

import ast
import numbers

def _statement_base(stmt):
    ''' What names a top-level statement, for the var()s in it: `name=function` of `name = function(...)`, `expr:function` of a bare
        call, else the kind of statement.  Not its line, and not its numbers: a line added above, or a number dragged, leave it as it is '''
    def callee(v):
        if isinstance(v, ast.Call):
            if isinstance(v.func, ast.Name):
                return v.func.id
            if isinstance(v.func, ast.Attribute):
                return v.func.attr
        return type(v).__name__
    if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name):
        return 'assign:{}={}'.format(stmt.targets[0].id, callee(stmt.value))
    if isinstance(stmt, ast.Expr):
        return 'expr:' + callee(stmt.value)
    return type(stmt).__name__


class VarTransformer(ast.NodeTransformer):
    def __init__(self):
        self._i = 0
        self._base = ''
        self._in_statement = 0

    def transform(self, module):
        ''' Tags the var()s of every top-level statement.  Each gets a KEY -- the statement (see _statement_base, and which one of that name
            it is) and its number in it -- by which the host finds it again in the next run: the variable of a model is the same one
            whatever is added or taken out above it, so that a shape that has numbers is the same shape, and its render goes on '''
        seen = {}
        for stmt in module.body:
            base = _statement_base(stmt)
            count = seen.get(base, 0)
            seen[base] = count + 1
            self._base = '{}@{}'.format(base, count)
            self._in_statement = 0
            self.visit(stmt)
        return module

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
                node.args.append(ast.Constant(value='{}/{}'.format(self._base, self._in_statement), **dummy))
                self._in_statement += 1
            self._i += 1
        return self.generic_visit(node) # Recurse, e.g. to patch sphere(var(1))

# Filled in by run() for the FielDes GUI: the (first, last) 1-based source
# lines of every top-level statement, in the same order as run()'s results,
# and a JSON description of the script for the model tree.
last_lines = []
last_scene = ''  # (no longer made: the model tree is read from the text, see outline.py)
# The names of the last run and what they hold (FielDes asks for the numbers of a shape by name)
last_globals = {}
# The (first, last) lines of the statement that runs now and the text that is being run (None between runs): an edit of the script asks
# where it begins -- see app_support.edit_in_place
running_lines = None
running_source = None
# The state of the run that is going (None between runs): an edit that changes nothing of what it makes is put into it -- see apply_edit
running_state = None
# The results of the statements that were done when the last run stopped with an error (None when it ran to its end): FielDes shows
# what ran before the error
last_partial = None
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
# (line, number of placeholders, the statement's first line) when the last run stopped before a statement that has a placeholder, else None
last_hole = None


def _holes(stmt):
    ''' The placeholders -- `...` written where a call is given an argument -- in a top-level statement, as [(line, column)].  A statement
        that has one is not run: the run stops before it, until what goes there is written (FielDes puts a placeholder in place of a
        model that is taken out of a call that cannot do without it, and in the calls it writes for you).  `...` that is no argument
        (the body of a stub function, `x[..., 0]`) is not one '''
    found = []

    def walk(node, in_call):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)):
            return
        if isinstance(node, ast.Constant) and node.value is Ellipsis:
            if in_call:
                found.append((node.lineno, node.col_offset))
            return
        if isinstance(node, ast.Subscript):
            walk(node.value, in_call)       # (what is indexed, not the index: numpy's `x[..., 0]`)
            return
        for child in ast.iter_child_nodes(node):
            walk(child, in_call or isinstance(node, ast.Call))

    walk(stmt, False)
    return found


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


def _blocks():
    ''' (the custom blocks (fieldes.blocks) every script has at hand, what could not be read: said in the output) '''
    try:
        import os
        from fieldes import blocks
        found = blocks.namespace()
        said = ''.join('custom block file {}: {}\n'.format(os.path.basename(p), why) for p, why in blocks.errors())
        return found, said
    except Exception:
        return {}, ''


def run(s, breakpoints=None, **env):
    ''' Evaluates a string, clause-by-clause.

        Returns a list of values from each expression in the string, or
        raises an error if something went wrong.

        breakpoints: 1-based source lines; the run stops BEFORE the first
        top-level statement that contains one (last_paused is that line) and
        returns what was evaluated so far.  resume() continues it.
    '''
    global _paused, last_partial
    import contextlib
    import io
    _paused = None
    last_partial = None         # (a script that does not even parse ran nothing: what is shown stays)
    _prepare_imports()
    parsed = ast.parse(s)
    tagged = VarTransformer().transform(parsed)
    from fieldes import run_progress
    _count_loops(tagged)
    block_functions, block_trouble = _blocks()
    state = {
        's': s,
        'body': tagged.body,
        'source_lines': s.splitlines(),
        'gs': {**block_functions, **env, '__libfive_counted__': run_progress.counted},
        'out': [],
        'lines': [],
        'i': 0,
        'printed': io.StringIO(),
    }
    state['printed'].write(block_trouble)
    return _continue(state, breakpoints, skip_first=False)


def resume(breakpoints=None):
    ''' Continues the run stopped at a breakpoint: the statement it stopped
        before runs, then the rest up to the next breakpoint (or the end).
        Returns the results of every statement so far. '''
    if _paused is None:
        raise RuntimeError('no script is stopped at a breakpoint')
    return _continue(_paused, breakpoints, skip_first=True)


def _display_name(stmt):
    ''' The name a statement only shows (`ball`: the eye of a model in the tree writes and takes out such a line), else None.  It makes
        nothing: a run that has gone past it can have it put in or taken out without any of what it made being any other '''
    if isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Name):
        return stmt.value.id
    return None


def _position(stmt):
    return (stmt.lineno, stmt.end_lineno)


def _line_rule(old_text, new_text):
    ''' For a text that is not a script just now (it is being typed): 'reaches' when the edit begins at or above the statement that runs,
        else 'defer' -- the run goes on to the end of that statement and then makes way for the run of the text '''
    running = running_lines
    if not running:
        return None
    a, b = old_text.splitlines(), new_text.splitlines()
    n = 0
    while n < len(a) and n < len(b) and a[n] == b[n]:
        n += 1
    if n == len(a) == len(b):
        return None
    return 'reaches' if n + 1 <= running[1] else 'defer'


def plan_edit(new_text):
    ''' What an edit of the script does to the run that is going.  None: nothing runs (or the run is past its last statement).
        'reaches': it changes what the statement that runs makes -- or what it is made of: that statement, or one before it that is not
        only a model shown (a changed number, a line added above it), so the run is stopped and made again.  'defer': the text is not a
        script just now, and the edit is below the statement that runs.  Else the plan to put it into the run: the statements the run has
        done (and the one that runs) are what they were, so what they made stays; a model shown or hidden above, and whatever comes
        after the statement that runs, are the new text's.  Called with the run's lock '''
    st = running_state
    old_text = running_source
    if st is None or old_text is None:
        return None
    i = st['i']
    if i >= len(st['body']):
        return None
    try:
        old = ast.parse(old_text).body
        module = ast.parse(new_text)
    except SyntaxError:
        return _line_rule(old_text, new_text)
    new = module.body
    if i >= len(old) or _display_name(old[i]) is not None:
        return None                         # (the one that runs only shows a model: it is over at once)
    core_old = [(k, s) for k, s in enumerate(old[:i + 1]) if _display_name(s) is None]
    core_new = [(k, s) for k, s in enumerate(new) if _display_name(s) is None]
    n = len(core_old)
    if len(core_new) < n:
        return 'reaches'
    shifts = []
    for (_, a), (_, b) in zip(core_old, core_new):
        # (the same statement: what it made is what the new text says)
        if ast.dump(a) != ast.dump(b):
            return 'reaches'
        if _position(a) != _position(b):
            # (... a few lines further on, or nearer: a comment or a blank line was put above it, or taken away -- the statement is the same
            # text, so what it made is what the new text says, and the places of its numbers move by the same lines.  Only when it is the same
            # text to the letter: the columns of its numbers are known from it)
            if ast.get_source_segment(old_text, a) != ast.get_source_segment(new_text, b):
                return 'reaches'
            shifts.append((a.lineno, a.end_lineno, b.lineno - a.lineno))
    if shifts:
        # (the statement that runs is moved too: the numbers it has not asked for yet are tagged with the lines it had, and so are the
        # breakpoints a run was begun with -- a text with those, and a move, is made again)
        running = core_old[n - 1][1]
        if st.get('breakpoints') or any(isinstance(x, ast.Call) and isinstance(x.func, ast.Name) and x.func.id == 'var' for x in ast.walk(running)):
            return 'reaches'
    end = core_new[n - 1][0] + 1            # (the statement that runs, in the new text, and one past it)
    done_new = new[:end - 1]
    gs = st['gs']
    for s in done_new:
        name = _display_name(s)
        if name is not None and name not in gs:
            # (it shows a name that is not there -- half typed, or made below: this run has nothing to show for it, and only a run of the
            # new text says what is wrong.  The statement that runs is not touched: it ends, and its result is kept)
            return 'defer'
    return {'module': module, 'done': done_new, 'end': end, 'core_done': [k for k, _ in core_old[:-1]], 'shifts': shifts}


def apply_edit(new_text):
    ''' Puts an edit of the script that does not reach the statement that runs into the run that is going: '1' when it is in (the run goes
        on with the new text, its results are the new text's; nothing is stopped and no other run follows), else '' '''
    st = running_state
    if st is None:
        return ''
    global running_source
    with st['lock']:
        plan = plan_edit(new_text)
        if not isinstance(plan, dict):
            return ''
        out, lines, body, gs = st['out'], st['lines'], st['body'], st['gs']
        i = st['i']
        values = iter(out[k] for k in plan['core_done'])
        done_out, done_lines = [], []
        for s in plan['done']:
            name = _display_name(s)
            done_out.append(gs[name] if name is not None else next(values))
            done_lines.append(_position(s))
        # the statements after the one that runs are the new text's (tagged as a run tags them: see VarTransformer)
        tagged = VarTransformer().transform(plan['module'])
        _count_loops(tagged)
        running = body[i]
        if plan['shifts']:
            _shift_places(running, plan['shifts'])
        body[:] = list(tagged.body[:len(done_out)]) + [running] + list(tagged.body[plan['end']:])
        out[:] = done_out
        lines[:] = done_lines
        st['i'] = len(done_out)
        st['s'] = new_text
        st['source_lines'][:] = new_text.splitlines()
        from fieldes import app_support
        st['src'] = app_support._Source(new_text)
        running_source = new_text
        st['republish'] = True              # (what is shown is drawn again: a model shown or hidden is seen at once)
        st['stop'] = False                  # (a stop asked for by an edit before this one is over with: the run is the new text's now)
    return '1'


def _shift_places(running, shifts):
    ''' The statements the run has done, and the one that runs, are on other lines in the new text (`shifts`: first line, last line, how many
        lines further): what the run keeps of where they are is moved with them -- the line of the statement that runs, and the places of the
        numbers (var) it has found so far, which the editor writes a drag to '''
    global running_lines

    def moved(line):
        for first, last, delta in shifts:
            if first <= line <= last:
                return line + delta
        return line

    delta_running = moved(running.lineno) - running.lineno
    if delta_running:
        ast.increment_lineno(running, delta_running)
    running_lines = (running.lineno, running.end_lineno)
    try:
        from fieldes import run_progress
        lib = run_progress._lib
        if lib and hasattr(lib, 'libfive_run_step_line'):
            lib.libfive_run_step_line(running.lineno)
    except Exception:
        pass
    try:
        import _fieldes_host as host
        entries = getattr(host, '__vars', None)
        if isinstance(entries, list):
            for k, item in enumerate(entries):
                if isinstance(item, tuple) and len(item) == 3 and isinstance(item[2], tuple) and len(item[2]) == 4:
                    first, last, col, end_col = item[2]
                    entries[k] = (item[0], item[1], (moved(first), moved(last), col, end_col))
    except ImportError:
        pass


def stop_after_statement():
    ''' Asks the run that is going to stop when the statement that runs is done -- not in it: what it makes is whole and kept (a solver's
        result, a field), so that the run that follows finds it made -- for an edit below it that cannot be put into the run '''
    st = running_state
    if st is not None:
        st['stop'] = True


def _publish_partial(out, lines, running_line=-1):
    ''' The viewport draws what the statements so far have shown (their values and the lines they are on) before a long statement runs: a part
        hidden or shown, a number edited, is seen at once, whatever the analysis after it takes. (Only inside the application.) '''
    try:
        import _fieldes_host as host
        publish = getattr(host, 'partial_shapes', None)
        if publish is not None:
            publish(list(out), list(lines), running_line)
    except Exception:
        pass


class _PartialScenes:
    ''' The model tree is read from the script's text (see outline.py), so it does not wait for the run.  What the run adds is what its
        statements found out, which `outline.record` keeps as each one is done; the tree is told to take it up (host.partial_scene)
        once a statement has been running for DELAY seconds -- so a script whose statements are all quick says nothing until it is done.
        It runs beside the script, in a thread of its own, and only tells: the script is not slowed by it. '''
    DELAY = 0.15
    SHOW_AFTER = 0.4        # a statement still going after this long -- whatever it is -- has what the statements before it show drawn

    def __init__(self, state, host):
        import threading
        self.state, self.host = state, host
        self.sent = 0
        self.shown = -1         # the statement whose picture was drawn
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, name='fieldes-partial-scenes', daemon=True)
        self.thread.start()

    def _run(self):
        import time
        st = self.state
        while not self.stop.wait(0.05):
            if st.get('republish'):
                # an edit was put into the run (a model shown or hidden above the statement that runs): what the statements done show is drawn
                # again now, not when the run is over
                st['republish'] = False
                try:
                    if not self.stop.is_set():
                        i = st['i']
                        _publish_partial(list(st['out']), list(st['lines']), st['body'][i].lineno - 1 if i < len(st['body']) else -1)
                except Exception:
                    pass
            n = st['i']
            if self.shown != n < len(st['body']) and time.time() - st['t0'] >= self.SHOW_AFTER:
                # a statement takes long (an analysis, an optimisation, an import ...): what the statements before it have shown is drawn
                # now, and an edit meanwhile -- a model shown or hidden -- is seen at once, not when the statement is over
                self.shown = n
                try:
                    if not self.stop.is_set():
                        _publish_partial(list(st['out']), list(st['lines']), st['body'][n].lineno - 1)
                except Exception:
                    pass
            if self.sent < n < len(st['body']) and time.time() - st['t0'] >= self.DELAY:
                self.sent = n
                try:
                    if not self.stop.is_set():
                        self.host.partial_scene(json.dumps({'done_line': st['body'][n - 1].end_lineno, 'errored': False}))
                except Exception:       # (the model tree is a convenience only)
                    pass

    def close(self):
        ''' Ends it, after a tree it is making at this moment (the finished script's tree is made after that) '''
        self.stop.set()
        if self.thread.is_alive():
            self.thread.join()

    def error_scene(self):
        ''' The script stopped with an error: the tree is told to take up what the statement that failed says (its row shows it) '''
        st = self.state
        try:
            n = st['i']
            self.host.partial_scene(json.dumps({'done_line': st['body'][n - 1].end_lineno if n else 0, 'errored': True}))
        except Exception:
            pass


def _partial_scenes(state):
    try:
        import _fieldes_host as host
        return _PartialScenes(state, host) if hasattr(host, 'partial_scene') else None
    except ImportError:         # (not inside the application)
        return None


def _continue(state, breakpoints, skip_first):
    global last_lines, last_scene, last_output, last_paused, _paused, last_globals, last_hole, last_partial, running_lines, running_source
    global running_state
    import contextlib
    import threading
    import time
    from fieldes import field_cache, made_from, run_progress
    body, gs, out, lines = state['body'], state['gs'], state['out'], state['lines']
    source_lines = state['source_lines']
    last_paused = -1
    last_hole = None
    last_partial = None
    _paused = None
    from fieldes import outline
    if 'src' not in state:
        from fieldes import app_support
        state['src'] = app_support._Source(state['s'])
    if not skip_first:
        outline.run_began()
        try:
            from fieldes.stdlib import cad_import
            cad_import._begin_run()             # (a script begins: it has imported nothing, and set no resolution of its own)
        except Exception:
            pass
    # Progress: which statement runs (how many there are is known now)
    progress_lib = run_progress._lib
    if progress_lib:
        progress_lib.libfive_run_begin(len(body))
    state['t0'] = time.time()
    running_source = state['s']
    state['breakpoints'] = list(breakpoints or ())          # (an edit that moves the lines of the statements below it asks whether lines matter)
    lock = state.setdefault('lock', threading.RLock())
    state['stop'] = False
    running_state = state
    watcher = _partial_scenes(state)
    try:
        with contextlib.redirect_stdout(state['printed']):
            while state['i'] < len(body):
                # (an edit that does not reach the statement that runs is put into the run between its statements: see apply_edit)
                with lock:
                    if state['stop']:
                        raise Exception()   # (a run that an edit has replaced ends so: nothing is shown of it)
                    i = state['i']
                    if i >= len(body):
                        break
                    p = body[i]
                state['t0'] = time.time()
                holes = _holes(p)
                pending = state.setdefault('pending', set())
                waits = bool(holes) or (bool(pending) and any(
                    isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load) and n.id in pending for n in ast.walk(p)))
                if waits:
                    # A placeholder: the statement does not run, and nor does what is made from it (a statement that uses a name it would
                    # have made) -- the rest of the script does, so that the part, the render settings and everything that does not wait
                    # are drawn.  The run is over when it reaches the end; the first statement that waits is told (last_hole), and the
                    # script is run again when what goes there is written
                    pending |= {n.id for n in ast.walk(p) if isinstance(n, ast.Name) and isinstance(n.ctx, (ast.Store, ast.Del))}
                    if isinstance(p, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                        pending.add(p.name)
                    if holes and 'hole' not in state:
                        state['hole'] = (p.lineno, len(holes), source_lines[p.lineno - 1].strip() if p.lineno <= len(source_lines) else '')
                    with lock:
                        i = state['i']
                        out.append(None)
                        lines.append((p.lineno, p.end_lineno))
                        state['skip'] = -1
                        state['i'] = i + 1
                    continue
                b = 0 if (skip_first and state.get('skip') == i) else _hit(
                    breakpoints, p, body[i - 1].end_lineno if i else 0)
                if b:
                    last_paused = b
                    state['skip'] = i
                    _paused = state
                    break
                state['skip'] = -1
                running_lines = (p.lineno, p.end_lineno)
                run_progress.end_to(0)
                if progress_lib:
                    progress_lib.libfive_run_step(i, run_progress._b(_step_label(source_lines, p)))
                    if hasattr(progress_lib, 'libfive_run_step_line'):
                        progress_lib.libfive_run_step_line(p.lineno)
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
                    inputs = made_from.reads(p, gs)         # (the models it reads, before it runs: see made_from.py)
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
                    made_from.note(p, gs, inputs)           # (the model keeps the models it was made from: a field the cache handed back too -- these are this run's)
                with lock:
                    i = state['i']      # (an edit put into the run while the statement ran has moved it: see apply_edit)
                    out.append(r)
                    lines.append((p.lineno, p.end_lineno))
                    outline.record(p, state['src'], gs, r)
                    if pending:
                        # (a name that a statement that waits would have made is made by this one after all: nothing waits for it any more)
                        pending -= {n.id for n in ast.walk(p) if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store)}
                    state['i'] = i + 1
    except BaseException as e:
        # The script stopped with an error: its model tree lists what the statements before it made.  (Not when it
        # was stopped by the application, for a newer edit or by the red dot -- a solver that gave up says "cancelled" --
        # that is not an error of the script's)
        stopped = (type(e) in (Exception, KeyboardInterrupt) and not e.args) or str(e).endswith('cancelled')
        if not stopped and state['i'] < len(body):
            outline.note_error(body[state['i']], state['src'], e)
        # (what the statements before it made, and the lines they are on: the viewer shows it beside the error)
        last_partial = list(out)
        last_lines = list(lines)
        last_globals = gs
        if watcher and not stopped:
            watcher.close()
            watcher.error_scene()
        raise
    finally:
        running_lines = None
        with lock:
            running_state = None            # (an edit now finds no run to put itself into: it makes one)
        if watcher:
            watcher.close()
        last_output = state['printed'].getvalue()
        run_progress.end_to(0)
        if progress_lib:
            progress_lib.libfive_run_end()
    last_lines = list(lines)
    last_globals = gs
    if state.get('hole') and last_paused <= 0:
        # (a run that stopped at a breakpoint says that first: the statements that wait are told when it has run to its end)
        last_paused = state['hole'][0]
        last_hole = state['hole']
    outline.finish(state['s'], gs, out)         # (what takes a measurement of the shapes: the tree is asked for again when it is done)
    return list(out)
