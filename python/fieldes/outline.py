'''
The model tree, read from the script's text.

The tree is not made by running the script.  `outline_json(...)` reads the text -- every statement that makes a model, how it is
written (its inputs, its numbers, its placeholders), what shows, hides, locks and edits it -- and answers at once, whatever the run is
doing: a script that is still calculating, stuck, stopped before a placeholder, or has failed has the same tree, because the tree is
a picture of the code.

What only a run can say -- exactly what kind of model a statement made, how far it reaches, what an import found, that a statement
failed -- is remembered here, statement by statement, as the run goes (`record`, `note_error`, `finish`), and laid over the rows
that the text gives: for a statement whose text is what it was when it ran, everything; for one that was edited, what its function
makes (a call to the same function makes the same kind); for a statement that never ran, a guess from its function's name.  A run
never adds a row and never takes one away.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

import ast
import hashlib
import fnmatch
import json
import os
import threading
import time
import types

from fieldes import app_support as A
from fieldes.runner import _holes

_LOCK = threading.RLock()

# What the runs found out about the statements, by (variable, the statement's text); and the latest for each variable
_KNOWN = {}
_LATEST = {}
# The same for displayed expressions (a statement that is only an expression), by the statement's text
_DISPLAYS = {}
# The statement the last run failed on: {'text', 'line', 'message'}; empty when it did not fail
_ERROR = {}

# The facts of a model that a run gives, and that are laid over the row the text gives (the rest of a row is the text's)
_FACT_KEYS = ('exact_bounds', 'colored', 'model', 'failed', 'error', 'result', 'type', 'shown', 'no_handles', 'condition_role', 'cls', 'bounds', 'can_expose',
              'expose_count', 'expose_cap', 'is_list', 'is_pair', 'custom_used', 'surface_selection', 'displayable_known')
# What a call to the same function is taken to make again, when the statement was edited (its text is not what ran)
_KIND_KEYS = ('model', 'result', 'type', 'shown', 'no_handles', 'condition_role', 'cls', 'is_list', 'is_pair', 'surface_selection')

# Calls that make nothing a model (a statement that is only such a call is not a displayed model)
_NOT_MODELS = {
    'print', 'len', 'range', 'int', 'float', 'str', 'list', 'dict', 'tuple', 'set', 'sum', 'min', 'max', 'abs', 'round', 'sorted',
    'enumerate', 'zip', 'open', 'map', 'filter', 'any', 'all', 'bool', 'type', 'isinstance', 'input', 'var', 'roi', 'roi_resolution',
    'progress', 'expose', 'handles', 'lock', 'render_cache', 'custom_resolution', 'export_stl', 'export_mesh', 'export_step',
    'mass_properties', 'volume_of', 'field_range', 'find_extent', 'evaluate', 'sample_grid',
}

# What a function of the library makes, by its name (a guess, for a statement that never ran: a run says exactly).  The functions of
# the menus are known from them (menu_catalog), the ones that make fields from what they declare (`_makes`, see fieldargs); these
# patterns are for the rest
_MAKES = (
    ('simulation', ('*_analysis', '*_optimization', 'analyse', 'analyze')),
    ('conditions', ('fixed', 'force', 'gravity', 'thermal_expansion', 'fixed_temperature', 'heat_input',
                    'heat_generation', 'convection', 'inlet', 'outlet', 'wall', 'slip')),
    ('material', ('Material', 'Fluid')),
    ('surface', ('select_surface', 'surface_from_bodies', 'surface_*')),
    ('point', ('point', 'center')),
    ('cell', ('cell_*',)),
    ('field', ('fit', 'fit_csv', 'fit_field', 'interpolate_field', 'X', 'Y', 'Z')),
)
_GEOMETRIC = ('solid', 'profile', 'field', 'surface')
_FUNCTION_KINDS = {}


def _function_kinds():
    ''' What the functions of the menus make, by name (the groups of menu_catalog say it) '''
    if not _FUNCTION_KINDS:
        try:
            from fieldes import menu_catalog as M
            for n, g, _t in M.PRIMITIVES:
                _FUNCTION_KINDS[n] = M.GROUP_KINDS.get(g, 'solid')
            for n, g, _t, _o in M.OPERATIONS:
                if g == 'Field math':
                    _FUNCTION_KINDS[n] = 'solid' if n == 'body_from_field' else 'field'
                elif g == 'Simulations':
                    _FUNCTION_KINDS[n] = 'simulation'
                elif g == 'Conditions':
                    _FUNCTION_KINDS[n] = 'conditions'
                elif g in ('Lattices', 'Importing'):
                    _FUNCTION_KINDS[n] = 'solid'
                elif g == 'Surfaces':
                    _FUNCTION_KINDS[n] = 'surface'
        except Exception:
            pass
    return _FUNCTION_KINDS


_ROLE_OF = {'fixed': 'supports', 'force': 'loads', 'gravity': 'loads', 'thermal_expansion': 'loads',
            'fixed_temperature': 'fixed_temperatures', 'heat_input': 'heat_inputs', 'heat_generation': 'heat_generations',
            'convection': 'convections', 'inlet': 'inlets', 'outlet': 'outlets', 'wall': 'boundaries', 'slip': 'boundaries', 'symmetry': 'boundaries'}


def _guess_role(callee):
    ''' What a condition that never ran probably is: the input of an analysis it goes in (`supports`, `loads`, `inlets` ...) '''
    return _ROLE_OF.get(callee or '', '')


def _guess_type(callee, inputs, by_name, ns=None):
    ''' What a statement that never ran probably makes: from its function (the menus', what it declares, its name), else -- a function of
        the script, say -- the kind of the first model it is given, if that is a shape or a field, else a solid '''
    name = callee or ''
    kinds = _function_kinds()
    if name in kinds:
        return kinds[name]
    declared = getattr(ns.get(name), '_makes', None) if ns else None
    if declared:
        return declared
    if by_name.get(name, {}).get('type') == 'field':
        return 'field'                          # (a fitted curve or surface of the script, called: the field it is a function of)
    for kind, patterns in _MAKES:
        if any(fnmatch.fnmatchcase(name, p) for p in patterns):
            return kind
    for i in inputs:
        src = by_name.get(i['name'])
        if src:
            return src['type'] if src.get('type') in _GEOMETRIC else 'solid'
    return 'solid'


def _hole_info(value, span):
    ''' Which argument of which call a placeholder at `span` stands for: {callee, param} (what a call is written with is not known to the
        tree: the library says) '''
    from fieldes import completion
    from fieldes.stdlib.fea import _SLOTS
    for call in ast.walk(value):
        if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Name):
            continue
        for k, a in enumerate(call.args):
            if isinstance(a, ast.Constant) and a.value is Ellipsis and A._span(a) == span:
                param = completion.parameter_at(call.func.id, k)
                return {'callee': call.func.id, 'param': param, 'list': param in _SLOTS}
        for kw in call.keywords:
            if kw.arg and isinstance(kw.value, ast.Constant) and kw.value.value is Ellipsis and A._span(kw.value) == span:
                return {'callee': call.func.id, 'param': kw.arg, 'list': kw.arg in _SLOTS}
    return {}


def facts_of(v):
    ''' What a value says about the model a statement made: the facts a run gives the tree '''
    from fieldes.kinds import kind_of
    Shape, FailedPart = A._types()
    failed = bool(FailedPart) and isinstance(v, FailedPart)
    shape = isinstance(v, Shape)
    result = not shape and callable(getattr(v, '_display', None))
    mkind = None if failed else kind_of(v)
    shown = shape or result
    f = {'model': bool(shape or failed or result or mkind is not None), 'failed': failed, 'result': result, 'type': mkind,
         'shown': shown, 'no_handles': bool(getattr(v, '_no_handles', False)) or result or not shown,
         'is_list': isinstance(v, list), 'is_pair': isinstance(v, tuple) and len(v) == 2}
    if failed:
        f['error'] = v.error
    if mkind == 'conditions':
        f['condition_role'] = A._condition_role(v)
    if mkind in ('conditions', 'material', 'surface'):
        f['cls'] = type(v).__name__
    f['surface_selection'] = bool(mkind == 'surface' and type(v).__name__ == 'SurfaceSelection')
    f['colored'] = hasattr(v, '_color_field')       # (a shape coloured by a field: it cannot be given a name to be edited by dragging)
    b = getattr(v, '_bounds', None) if shape else None
    if b:
        f['bounds'] = A._bounds_list(b)
        f['exact_bounds'] = True        # (the shape knows its extent: the region of interest can be made from it; an estimate is not that)
    used = getattr(v, '_custom_resolution_used', None)
    if isinstance(used, (int, float)):
        f['custom_used'] = used
    return f


def _display_shapes(v):
    ''' The shapes a value displays: itself, what an analysis result shows (its _display()), the shapes of a list '''
    Shape, _ = A._types()
    if isinstance(v, Shape):
        return [v]
    if isinstance(v, (list, tuple)):
        return [s for s in v if isinstance(s, Shape)]
    if callable(getattr(v, '_display', None)):
        try:
            d = v._display()
        except Exception:
            return []
        return [s for s in (d if isinstance(d, (list, tuple)) else [d]) if isinstance(s, Shape)]
    return []


def _text_of(src, stmt):
    return src.segment(stmt)


def record(stmt, src, gs, value):
    ''' A statement of the running script is done: what it made is remembered (cheap: the facts of the values) '''
    try:
        with _LOCK:
            text = _text_of(src, stmt)
            if isinstance(stmt, ast.Assign):
                callee = A._callee_name(stmt.value)
                for target in stmt.targets:
                    for t in (target.elts if isinstance(target, ast.Tuple) else [target]):
                        if isinstance(t, ast.Name) and t.id in gs:
                            facts = facts_of(gs[t.id])
                            _KNOWN[(t.id, text)] = facts
                            _LATEST[t.id] = (text, callee, facts)
            elif isinstance(stmt, ast.Expr):
                _DISPLAYS[text] = facts_of(value)
            if _ERROR and _ERROR.get('text') == text:
                _ERROR.clear()          # (the statement that failed ran this time)
    except Exception:       # (the tree is a convenience: a statement that cannot be described is not a reason to stop the script)
        pass


def note_error(stmt, src, message):
    ''' The run failed on this statement: its row says so, and nothing else changes '''
    try:
        with _LOCK:
            _ERROR.clear()
            _ERROR.update({'text': _text_of(src, stmt), 'line': stmt.lineno, 'message': str(message)})
    except Exception:
        pass


def run_began():
    with _LOCK:
        _ERROR.clear()


def finish(source, gs, out):
    ''' The run is over: the extents of what shows, and whether the surfaces of a shape can be dragged -- what takes a measurement of the
        shapes, within a time budget (the rest is measured when the script has run again) -- are laid down with the facts, and the
        fields the section viewer can show are listed '''
    import time as _time
    try:
        tree = ast.parse(source)
        src = A._Source(source)
        Shape, FailedPart = A._types()
        shown_names = set()
        for stmt in tree.body:
            if isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Name):
                shown_names.add(stmt.value.id)
        A._FIELD_SOURCES[:] = []
        measure = []                    # (priority, name, text, value)
        for stmt in tree.body:
            if not isinstance(stmt, ast.Assign):
                continue
            text = _text_of(src, stmt)
            for target in stmt.targets:
                for t in (target.elts if isinstance(target, ast.Tuple) else [target]):
                    if not isinstance(t, ast.Name) or t.id not in gs:
                        continue
                    v = gs[t.id]
                    with _LOCK:
                        facts = _KNOWN.get((t.id, text))
                    if facts is None:
                        continue
                    if facts.get('type') == 'field':
                        A._FIELD_SOURCES.append((t.id, v))
                    shapes = [] if facts.get('failed') else _display_shapes(v)
                    if shapes:
                        has_var = any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var'
                                      for n in ast.walk(stmt))
                        measure.append((0 if t.id in shown_names else 1, t.id, text, shapes,
                                        dict(facts, _has_var=has_var, _own=isinstance(v, Shape)), facts))
        for k, stmt in enumerate(tree.body):
            if isinstance(stmt, ast.Expr) and k < len(out):
                v = out[k]
                text = _text_of(src, stmt)
                facts = _DISPLAYS.get(text)
                if facts and facts.get('type') == 'field' and isinstance(v, Shape):
                    A._FIELD_SOURCES.append(('line:%d' % stmt.lineno, v))
                shapes = _display_shapes(v) if facts is not None else []
                if shapes:
                    measure.append((0, None, text, shapes, dict(facts, _has_var=False, _own=isinstance(v, Shape)), facts))
        deadline = _time.time() + A._BOUNDS_SECONDS
        for _, name, text, shapes, view, facts in sorted(measure, key=lambda m: m[0]):
            if 'bounds' not in facts and _time.time() < deadline:
                b = A._estimate_bounds(shapes, deadline)
                if b:
                    facts['bounds'] = b
        deadline = _time.time() + A._BOUNDS_SECONDS
        for _, name, text, shapes, view, facts in sorted(measure, key=lambda m: m[0]):
            if _time.time() > deadline:
                break
            if view.get('_has_var') or facts.get('colored') or not view.get('_own'):
                continue
            n = A._expose_count(shapes[0])
            facts['can_expose'] = 0 < n <= A._MAX_EXPOSED
            facts['expose_count'] = n
            facts['expose_cap'] = A._MAX_EXPOSED
    except Exception:
        if os.environ.get('FIELDES_OUTLINE_DEBUG'):
            import traceback
            traceback.print_exc()


def seed(items_json):
    ''' A tree that was kept from an earlier session (its rows say what the statements made): what it knows is the first the facts have '''
    try:
        scene = json.loads(items_json)
        source_items = scene.get('items', [])
    except Exception:
        return
    with _LOCK:
        for it in source_items:
            var = it.get('var')
            if not var or it.get('kind') == 'import':
                continue
            facts = {'model': True, 'failed': bool(it.get('failed')), 'result': bool(it.get('result')), 'type': it.get('type'),
                     'shown': it.get('displayable') is not False, 'no_handles': bool(it.get('no_handles'))}
            for k in ('error', 'condition_role', 'cls', 'bounds', 'can_expose', 'expose_count', 'expose_cap'):
                if k in it:
                    facts[k] = it[k]
            _LATEST.setdefault(var, (None, it.get('callee', ''), facts))


def _prior(name, text, callee):
    ''' The facts about the statement `name = ...` with this text, or -- the text was edited -- what the latest statement of that name that
        called the same function made, or None '''
    with _LOCK:
        facts = _KNOWN.get((name, text))
        if facts is not None:
            return facts, True
        latest = _LATEST.get(name)
        if latest is not None and latest[1] == callee:
            return {k: v for k, v in latest[2].items() if k in _KIND_KEYS}, False
    return None, False


def _value_of(value, targets, pos):
    ''' What is assigned to the target at `pos`: for `x, y = a, b` the one that goes to it, else the whole value '''
    if isinstance(value, ast.Tuple) and len(value.elts) == len(targets) and not any(isinstance(e, ast.Starred) for e in value.elts):
        return value.elts[pos]
    return value


def _root_of(node):
    ''' The name a chain like a.b(c)[d].e starts with '''
    while isinstance(node, (ast.Call, ast.Attribute, ast.Subscript)):
        node = node.func if isinstance(node, ast.Call) else node.value
    return node.id if isinstance(node, ast.Name) else None


def _script_names(tree, blocks):
    ''' What the script itself brings: the modules it imports, and the names it defines (functions, classes, imports of names, every
        name it assigns, the custom blocks) '''
    modules, defined = set(), set(blocks)
    for n in ast.walk(tree):
        if isinstance(n, ast.Import):
            for a in n.names:
                modules.add((a.asname or a.name).split('.')[0])
        elif isinstance(n, ast.ImportFrom):
            for a in n.names:
                if a.name != '*':
                    defined.add(a.asname or a.name)
        elif isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            defined.add(n.name)
        elif isinstance(n, ast.arg):
            defined.add(n.arg)
        elif isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store):
            defined.add(n.id)
    return modules, defined


def _looks_like_model(value, by_name, ns, known=None):
    ''' Whether a statement that never ran is taken to make a model: a call of a function of the library or of the script, or of a method
        of a model (not a call of Python's own functions, of a module, or of a name that is defined nowhere: that fails when it runs and
        makes nothing), a model written again, arithmetic on models '''
    if isinstance(value, ast.Call):
        name = A._short_name(A._call_name(value))
        full = A._call_name(value)
        if name in _NOT_MODELS or full.startswith(('math.', 'os.', 'np.', 'numpy.', 'random.', 'json.')):
            return False
        if known is None:
            return True
        modules, defined = known
        root = _root_of(value.func)
        if root is None:
            return True                         # (a call of something computed: not known)
        if root in by_name:
            return True                         # (a method of a model, or a call with one)
        if root in modules:
            return False                        # (time.sleep(...), a module's own function)
        if isinstance(value.func, ast.Name):
            if root in defined:
                return True                     # (a function of the script, a name it imports)
            if root in ns:
                return callable(ns[root])
            return False                        # (one of Python's own, or defined nowhere)
        # (a method of something that is not a model of the script: a library object -- Shape.X() -- is one that makes a model)
        return root in ns and root not in defined and not isinstance(ns[root], types.ModuleType)
    if isinstance(value, ast.Name):
        return value.id in by_name
    if isinstance(value, (ast.BinOp, ast.UnaryOp)):
        return any(isinstance(n, ast.Name) and n.id in by_name for n in ast.walk(value))
    if isinstance(value, ast.Subscript):
        base = value
        while isinstance(base, ast.Subscript):
            base = base.value
        return isinstance(base, ast.Name) and base.id in by_name
    return False


def _namespace():
    ''' The names a statement can call, for describing a script that has not run: the library's, and what the last run left '''
    ns = {}
    try:
        import fieldes
        ns.update({k: v for k, v in vars(fieldes).items() if not k.startswith('_')})
    except Exception:
        pass
    try:
        from fieldes import runner
        last = runner.last_globals
        if isinstance(last, dict):
            ns.update(last)
    except Exception:
        pass
    return ns


def outline_json(arg):
    ''' The model tree of a script's text, as JSON: {items, settings, kinds, ...}.  `arg` is JSON {source, run_done}: run_done says that
        the run of this text has just ended (what waits for the imports it made, and for the menus it changes, goes on) '''
    req = json.loads(arg)
    source = req['source']
    md5 = hashlib.md5(source.encode('utf-8')).hexdigest()
    try:
        tree = ast.parse(source)
    except SyntaxError as e:
        # (a line that is being typed: the tree stays as it was, and says nothing -- the editor says what is wrong)
        return json.dumps({'syntax_error': '%s (line %s)' % (e.msg, e.lineno), 'source_md5': md5})
    from fieldes.kinds import describe as describe_kinds
    ns = _namespace()
    blocks = A._block_names()
    src = A._Source(source)
    A._SPAN_LINES[:] = src.lines
    taken = set(ns) | {n.id for n in ast.walk(tree) if isinstance(n, ast.Name)}
    known = _script_names(tree, blocks)
    items = []
    by_name = {}
    settings = {}
    list_imports = {}

    def is_blocked(value):
        return isinstance(value, ast.Call) and isinstance(value.func, ast.Name) and value.func.id in blocks

    with _LOCK:
        error = dict(_ERROR)

    for stmt in tree.body:
        line, end = stmt.lineno, stmt.end_lineno
        text = _text_of(src, stmt)

        # --- view.set_bounds / set_resolution / set_quality
        if isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call):
            fn = A._call_name(stmt.value)
            if fn.startswith('view.') and A._short_name(fn) in A.SETTINGS_FUNCS:
                settings[A._short_name(fn)] = {'line': line, 'end_line': end, 'span': A._span(stmt), 'text': src.segment(stmt)}
                continue

        # --- imports
        call, sub = A._find_import(stmt, ns)
        if call is not None:
            func = A._short_name(A._call_name(call))
            path = None
            if call.args:
                a = call.args[0]
                if isinstance(a, ast.Constant) and isinstance(a.value, str):
                    path = a.value
                elif isinstance(a, ast.Name) and isinstance(ns.get(a.id), str):
                    path = ns[a.id]
            rev = rev_span = units = None
            for kw in call.keywords:
                if kw.arg == 'rev' and isinstance(kw.value, ast.Constant):
                    rev = kw.value.value
                    rev_span = A._span(kw.value)
                if kw.arg == 'units' and isinstance(kw.value, ast.Constant):
                    units = kw.value.value
            log = A._import_log_entry(path) if path else None
            item = {
                'kind': 'import', 'line': line, 'end_line': end, 'func': func, 'path': path,
                'label': os.path.basename(path) if path else func,
                'call': A._span(call), 'rev': rev, 'rev_span': rev_span,
                'units': units if units is not None else (log or {}).get('units'),
                'unit_mm': (log or {}).get('unit_mm'), 'note': (log or {}).get('note'),
                'parts': [dict(p) for p in (log or {}).get('parts', [])],
                'exists': bool(path) and os.path.exists(path),
                'text': A._short(src.segment(stmt), 90),
            }
            if sub is not None:
                k, knode = A._const_index(sub)
                if k is not None:
                    item['index'] = k
                    item['index_span'] = A._span(knode)
            if isinstance(stmt, ast.Assign):
                for target in stmt.targets:
                    elts = target.elts if isinstance(target, ast.Tuple) else [target]
                    first = elts[0] if elts else None
                    if isinstance(target, ast.Name):
                        facts, _ = _prior(target.id, text, A._callee_name(stmt.value))
                        if facts is not None and facts.get('is_list'):
                            item['list_var'] = target.id
                            list_imports[target.id] = item
                        elif facts is not None and facts.get('is_pair'):
                            item['part_tuple_var'] = target.id
                        elif facts is not None and facts.get('model'):
                            item['var'] = target.id
                        elif facts is None:
                            # (never ran: an import of a file is a list of its parts, unless one part is taken at once)
                            if sub is None:
                                item['list_var'] = target.id
                                list_imports[target.id] = item
                            else:
                                item['var'] = target.id
                    elif isinstance(first, ast.Name):
                        facts, _ = _prior(first.id, text, A._callee_name(stmt.value))
                        if facts is None or facts.get('model'):
                            item['var'] = first.id
                            if len(elts) > 1:
                                item['bounds_expr'] = src.segment(elts[1])
            if 'var' in item:
                facts, exact = _prior(item['var'], text, A._callee_name(stmt.value))
                facts = facts or {}
                item['failed'] = bool(facts.get('failed'))
                if facts.get('failed'):
                    item['error'] = facts.get('error')
                if exact and facts.get('bounds'):
                    item['bounds'] = facts['bounds']
                elif 'index' in item and item['index'] < len(item['parts']):
                    item['bounds'] = item['parts'][item['index']]['bounds']
                item['roi_expr'] = item.get('bounds_expr') or (item['var'] if item.get('bounds') else None)
                by_name[item['var']] = item
            elif 'list_var' in item:
                item['roi_expr'] = item['list_var']
            if item['parts']:
                lo = [min(p['bounds'][0][i] for p in item['parts']) for i in range(3)]
                hi = [max(p['bounds'][1][i] for p in item['parts']) for i in range(3)]
                item.setdefault('bounds', [lo, hi])
            if error and error.get('text') == text and 'var' in item:
                item['failed'], item['error'], item['kind'] = True, error['message'], 'failed'
            items.append(item)
            continue

        # --- "x = handles(x, ...)" / "x = expose(x, [...])" / "x = render_cache(x)" / "x = lock(x)": what edits the shape x
        if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name) and \
                isinstance(stmt.value, ast.Call) and \
                A._short_name(A._call_name(stmt.value)) in ('handles', 'expose', 'render_cache', 'lock', 'custom_resolution') and \
                stmt.value.args and isinstance(stmt.value.args[0], ast.Name) and \
                stmt.value.args[0].id == stmt.targets[0].id and stmt.targets[0].id in by_name:
            target = by_name[stmt.targets[0].id]
            what = A._short_name(A._call_name(stmt.value))
            if what == 'custom_resolution':
                arg = stmt.value.args[1] if len(stmt.value.args) > 1 else None
                for kw in stmt.value.keywords:
                    if kw.arg == 'resolution':
                        arg = kw.value
                number = arg.value if isinstance(arg, ast.Constant) and isinstance(arg.value, (int, float)) and \
                    not isinstance(arg.value, bool) else None
                info = {'line': line, 'end_line': end, 'call': A._span(stmt.value), 'text': A._short(src.segment(stmt), 60),
                        'value': number, 'span': A._span(arg) if arg is not None else None}
                facts, exact = _prior(stmt.targets[0].id, text, A._callee_name(stmt.value))
                used = (facts or {}).get('custom_used')
                if exact and isinstance(used, (int, float)) and number is not None and used < number * (1 - 1e-9):
                    info['used'] = used
                target['custom_resolution'] = info
                continue
            if what == 'render_cache':
                on = True
                flag = stmt.value.args[1] if len(stmt.value.args) > 1 else None
                for kw in stmt.value.keywords:
                    if kw.arg == 'on':
                        flag = kw.value
                if isinstance(flag, ast.Constant):
                    on = bool(flag.value)
                info = {'line': line, 'end_line': end, 'call': A._span(stmt.value), 'text': A._short(src.segment(stmt), 60)}
                target['cache' if on else 'cache_off'] = info
                continue
            if what == 'lock':
                target['locked'] = {'line': line, 'end_line': end, 'call': A._span(stmt.value), 'text': A._short(src.segment(stmt), 60)}
                continue
            if what == 'expose':
                target['exposed'] = {'line': line, 'end_line': end, 'call': A._span(stmt.value), 'text': A._short(src.segment(stmt), 60)}
                target['has_var'] = True
                continue
            mode, mode_span = None, None
            for kw in stmt.value.keywords:
                if kw.arg == 'mode' and isinstance(kw.value, ast.Constant) and isinstance(kw.value.value, str):
                    mode, mode_span = kw.value.value, A._span(kw.value)
            if mode not in ('click', 'never', 'always'):
                mode = 'click'
            target['handles'] = {
                'line': line, 'end_line': end, 'mode': mode, 'mode_span': mode_span,
                'call': A._span(stmt.value), 'text': src.segment(stmt),
                'has_scale': any(kw.arg == 'scale' for kw in stmt.value.keywords),
                'has_numbers': any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var'
                                   for n in ast.walk(stmt.value))}
            continue

        # --- assignments of models (and of parts taken from an import list)
        if isinstance(stmt, ast.Assign):
            callee = A._callee_name(stmt.value)
            for target in stmt.targets:
                elts = target.elts if isinstance(target, ast.Tuple) else [target]
                for pos, t in enumerate(elts):
                    if not isinstance(t, ast.Name):
                        continue
                    facts, exact = _prior(t.id, text, callee)
                    if facts is not None:
                        if not facts.get('model'):
                            continue
                    elif not (_looks_like_model(_value_of(stmt.value, elts, pos), dict(by_name, **list_imports), ns, known)
                              or (error and error.get('text') == text)):
                        continue
                    facts = facts or {}
                    failed = bool(facts.get('failed'))
                    if error and error.get('text') == text:
                        failed, facts = True, dict(facts, error=error['message'])
                    deps = sorted(n for n in A._names_in(stmt.value) if n in by_name and n != t.id)
                    inputs, variadic, numbers = A._inputs_of(stmt.value, ns, by_name, t.id)
                    mkind = None if failed else (facts.get('type') or _guess_type(callee, inputs, by_name, ns))
                    shown = facts.get('shown', mkind not in ('field', 'material', 'cell'))      # (a condition shows itself: it has an eye)
                    item = {'kind': 'failed' if failed else 'shape', 'line': line, 'end_line': end, 'var': t.id, 'label': t.id,
                            'result': bool(facts.get('result', mkind == 'simulation')),     # (what an analysis makes is a result, before it has run too)
                            'no_handles': facts.get('no_handles', not shown or mkind in ('simulation', 'conditions')
                                                  or bool(getattr(ns.get(callee), '_makes_no_handles', False))),
                            'text': A._short(src.segment(stmt.value), 70), 'deps': deps}
                    if mkind:
                        item['type'] = mkind
                    if mkind == 'field':
                        item['displayable'] = False
                    if not shown:
                        item['displayable'] = False
                    item['inputs'], item['variadic'], item['numbers'] = inputs, variadic, numbers
                    if variadic:
                        item['min_inputs'] = A._min_inputs(ns.get(callee))
                    item['slots'] = A._call_slots(stmt.value, ns, src)
                    if mkind == 'conditions':
                        item['condition_role'] = facts.get('condition_role') or _guess_role(callee)
                    if mkind == 'surface' and (facts.get('surface_selection') or callee == 'select_surface'):
                        first = [i for i in inputs if i.get('index') == 0 and not i.get('in_list') and 'keyword' not in i]
                        if first:
                            item['part_name'] = first[0]['name']
                    if mkind == 'conditions' and isinstance(stmt.value, ast.Call):
                        item['region_args'] = A._region_args_of(stmt.value, src)
                    if mkind in ('conditions', 'material', 'surface') and facts.get('cls'):
                        item['cls'] = facts['cls']
                    item['points'] = A._points_of(stmt.value, ns) if isinstance(stmt.value, ast.Call) else []
                    item['callee'] = callee
                    item['role'] = 'operation' if deps else 'primitive'
                    if is_blocked(stmt.value):
                        item['block'] = A._call_name(stmt.value)
                    item['has_var'] = any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var'
                                          for n in ast.walk(stmt.value)) or any(by_name[d].get('has_var') for d in deps)
                    if failed:
                        item['error'] = facts.get('error') or error.get('message')
                    if exact:
                        if facts.get('bounds'):
                            item['bounds'] = facts['bounds']
                            if facts.get('exact_bounds'):
                                item['roi_expr'] = t.id
                        if 'can_expose' in facts:
                            item['can_expose'] = facts['can_expose']
                            item['expose_count'] = facts['expose_count']
                            item['expose_cap'] = facts['expose_cap']
                    # placeholders: `...` written where an argument goes (such a statement does not run, nor does what is made from it)
                    if _holes(stmt):
                        spans = sorted(A._span(n) for n in ast.walk(stmt.value)
                                       if isinstance(n, ast.Constant) and n.value is Ellipsis)
                        item['hole'] = True
                        item['holes'] = [dict(_hole_info(stmt.value, s), span=s) for s in spans]
                    # "v = parts[k][0]" / "v, b = parts[k]" -> part k of an import
                    node = stmt.value
                    k = None
                    base = None
                    if isinstance(node, ast.Subscript):
                        inner = node.value
                        if isinstance(inner, ast.Subscript) and isinstance(inner.value, ast.Name) and inner.value.id in list_imports:
                            k, _ = A._const_index(inner)
                            base = inner.value.id
                        elif isinstance(inner, ast.Name) and inner.id in list_imports:
                            k, _ = A._const_index(node)
                            base = inner.id
                    if k is not None:
                        imp = list_imports[base]
                        item['part_of'] = imp['line']
                        item['part_index'] = k
                        if k < len(imp['parts']):
                            imp['parts'][k]['var'] = t.id
                            item.setdefault('bounds', imp['parts'][k]['bounds'])
                        item['roi_expr'] = '{}[{}]'.format(base, k)
                    by_name[t.id] = item
                    items.append(item)
            continue

        # --- displayed expressions
        if isinstance(stmt, ast.Expr):
            if isinstance(stmt.value, ast.Name) and stmt.value.id in by_name:
                by_name[stmt.value.id]['display_line'] = line
                continue
            with _LOCK:
                facts = _DISPLAYS.get(text)
            if facts is not None:
                if not facts.get('shown'):
                    continue
            elif not _looks_like_model(stmt.value, dict(by_name, **list_imports), ns, known) or isinstance(stmt.value, (ast.Name, ast.Constant)):
                continue
            facts = facts or {}
            deps = sorted(n for n in A._names_in(stmt) if n in by_name)
            item = {'kind': 'display', 'line': line, 'end_line': end, 'label': A._short(src.segment(stmt), 50),
                    'text': A._short(src.segment(stmt), 90), 'display_line': line, 'deps': deps}
            mkind = facts.get('type') or _guess_type(A._callee_name(stmt.value), [], by_name, ns)
            if mkind:
                item['type'] = mkind
                if mkind == 'field':
                    item['displayable'] = False
            item['inputs'], item['variadic'], item['numbers'] = A._inputs_of(stmt.value, ns, by_name)
            if item['variadic']:
                item['min_inputs'] = A._min_inputs(ns.get(A._callee_name(stmt.value)))
            item['slots'] = A._call_slots(stmt.value, ns, src)
            item['points'] = A._points_of(stmt.value, ns) if isinstance(stmt.value, ast.Call) else []
            item['callee'] = A._callee_name(stmt.value)
            item['role'] = 'operation' if deps else 'primitive'
            if is_blocked(stmt.value):
                item['block'] = A._call_name(stmt.value)
            if facts.get('shown', True) and not facts.get('result') and facts.get('type') != 'field' and not facts.get('failed')                     and not facts.get('colored'):
                item['can_name'] = True
                item['span'] = A._span(stmt)
                item['new_var'] = A._new_name(stmt.value, taken)
                item['has_var'] = any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var'
                                      for n in ast.walk(stmt)) or any(by_name[d].get('has_var') for d in deps)
            if facts.get('bounds'):
                item['bounds'] = facts['bounds']
            if 'can_expose' in facts:
                item['can_expose'], item['expose_count'], item['expose_cap'] = (
                    facts['can_expose'], facts['expose_count'], facts['expose_cap'])
            items.append(item)

    # --- "# hidden: <expr>" comment lines (top level)
    lines = src.lines
    i = 0
    while i < len(lines):
        m = A.HIDDEN_RE.match(lines[i])
        if not m or m.group(1):
            i += 1
            continue
        start = i
        text = m.group(2)
        j = i
        while True:
            try:
                ast.parse(text, mode='eval')
                break
            except SyntaxError:
                if j + 1 < len(lines) and A.HIDDEN_RE.match(lines[j + 1]):
                    j += 1
                    text += '\n' + A.HIDDEN_RE.match(lines[j]).group(2)
                else:
                    break
        try:
            ast.parse(text, mode='eval')
        except SyntaxError:
            i = start + 1
            continue
        name = text.strip()
        if name in by_name:
            by_name[name]['hidden_line'] = start + 1
        else:
            items.append({'kind': 'display', 'line': start + 1, 'end_line': j + 1, 'label': A._short(text, 50),
                          'text': A._short(text, 90), 'hidden_line': start + 1})
        i = j + 1

    for it in items:
        if it['kind'] == 'display':
            it['visible'] = 'hidden_line' not in it
            it['mode'], it['mode_explicit'] = 'click', False
        elif 'var' in it:
            it['visible'] = 'display_line' in it
            h = it.get('handles')
            if h:
                it['mode'], it['mode_explicit'] = h['mode'], True
            else:
                it['mode'], it['mode_explicit'] = 'click', False

    items.sort(key=lambda it: it['line'])
    for it in items:
        names = A.shadow_names(src.lines[it['line'] - 1:it.get('end_line', it['line'])])
        if names:
            it['shadows'] = sorted(names)
    versions = {}
    for it in items:
        if 'var' in it:
            versions.setdefault(it['var'], []).append(it)
    for it in items:
        for d in it.get('deps', []):
            if d in it.get('shadows', ()):
                continue
            earlier = [x for x in versions.get(d, []) if x['line'] < it['line']]
            if earlier and 'owner' not in earlier[-1] and earlier[-1] is not it:
                earlier[-1]['owner'] = A._key_of(it)
    last_of = {}
    for it in items:
        if 'var' in it:
            last_of[it['var']] = it
    for it in items:
        if 'var' in it and last_of[it['var']] is not it:
            it['reassigned'] = True
    truncated = 0
    if len(items) > A.MAX_ITEMS:
        keep = [it for it in items if it['kind'] in ('import', 'display') or it.get('visible') or 'hidden_line' in it]
        truncated = len(items) - len(keep)
        items = keep

    try:
        import _fieldes_host as host
        values = {'bounds': getattr(host, '__bounds', None), 'resolution': getattr(host, '__resolution', None),
                  'quality': getattr(host, '__quality', None)}
    except ImportError:
        values = {}
    for k, v in values.items():
        if v is not None:
            settings.setdefault(k, {})['value'] = v

    return json.dumps({'items': items, 'settings': settings, 'kinds': describe_kinds(), 'truncated': truncated,
                       'errored': bool(error), 'run_done': bool(req.get('run_done')),
                       'done_line': tree.body[-1].end_lineno if tree.body else 0, 'source_md5': md5,
                       'has_roi': 'roi' in ns, 'has_roi_resolution': 'roi_resolution' in ns}, default=str)
