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
import re
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
_FACT_KEYS = ('exact_bounds', 'colored', 'model', 'failed', 'error', 'result', 'type', 'shown', 'no_gizmo', 'outline', 'condition_role', 'domain', 'cls', 'bounds', 'can_handles',
              'handle_count', 'handle_cap', 'ends_only', 'is_list', 'is_pair', 'custom_used', 'surface_selection', 'displayable_known', 'properties', 'exports', 'linked', 'symmetry_image')
# What a call to the same function is taken to make again, when the statement was edited (its text is not what ran)
_KIND_KEYS = ('model', 'result', 'type', 'shown', 'no_gizmo', 'outline', 'condition_role', 'domain', 'cls', 'is_list', 'is_pair', 'surface_selection')

# Calls that make nothing a model (a statement that is only such a call is not a displayed model)
_NOT_MODELS = {
    'print', 'len', 'range', 'int', 'float', 'str', 'list', 'dict', 'tuple', 'set', 'sum', 'min', 'max', 'abs', 'round', 'sorted',
    'enumerate', 'zip', 'open', 'map', 'filter', 'any', 'all', 'bool', 'type', 'isinstance', 'input', 'var', 'roi', 'roi_resolution',
    'progress', 'handles', 'gizmo', 'lock', 'render_cache', 'custom_resolution', 'export_stl', 'export_mesh', 'export_step',
    'export_fea', 'export_results', 'export_field', 'result_summary',
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
    ('points', ('random_points',)),
    ('lattice', ('lattice', 'lattice_surface_conform', 'surface_lattice')),
    ('field', ('fit', 'fit_csv', 'fit_field', 'interpolate_field', 'X', 'Y', 'Z')),
)
_GEOMETRIC = ('solid', 'profile', 'curve', 'field', 'surface', 'lattice')
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
                elif g in ('Periodic lattices', 'Non-periodic lattices'):
                    _FUNCTION_KINDS[n] = M.OPERATION_KINDS.get(n, 'lattice')
                elif g == 'Importing':
                    _FUNCTION_KINDS[n] = 'solid'
                elif g == 'Surfaces':
                    _FUNCTION_KINDS[n] = 'surface'
        except Exception:
            pass
    return _FUNCTION_KINDS


_ROLE_OF = {'fixed': 'supports', 'force': 'loads', 'gravity': 'loads', 'thermal_expansion': 'loads',
            'fixed_temperature': 'fixed_temperatures', 'heat_input': 'heat_inputs', 'heat_generation': 'heat_generations',
            'convection': 'convections', 'inlet': 'inlets', 'outlet': 'outlets', 'wall': 'boundaries', 'slip': 'boundaries', 'symmetry': 'boundaries',
            'manufacturing_constraint': 'constraints', 'fff_constraint': 'constraints', 'cnc_constraint': 'constraints', 'molding_constraint': 'constraints'}


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


def _call_takes(callee):
    ''' The kinds of model the call `callee` works on, all its places together (what the menus offer an entry by: see menu_catalog._selection_fits), or [] when the library does
        not say -- a model of another kind dropped on the call is refused: it is no argument of it '''
    try:
        from fieldes import menu_catalog as M
        signatures = M._takes(callee)
        if signatures:
            return sorted(signatures[0][0])
        if callee and any(n == callee for n, g, _, _ in M.OPERATIONS):
            return sorted(M._SHAPES)
    except Exception:
        pass
    return []


def _slot_kinds(callee, k):
    ''' The kinds of model the k-th argument of `callee` takes (the kinds the menus offer an entry by: see menu_catalog._takes), or None when the library does not say --
        a number, an option, a place that takes anything: whatever is put there is let through, and the run says if it does not fit '''
    try:
        from fieldes import menu_catalog as M
        signatures = M._takes(callee)
        if signatures:
            needs = signatures[0][1]
            return sorted(needs[k]) if k < len(needs) else None
        # (a geometric operation works on the first model it is given: what a model is -- a solid, a 2D shape, a curve, a field ... -- but not a condition or a material)
        if k == 0 and any(n == callee for n, g, _, _ in M.OPERATIONS):
            return sorted(M._SHAPES)
    except Exception:
        pass
    return None


def _hole_info(value, span):
    ''' Which argument of which call a placeholder at `span` stands for: {callee, param} (what a call is written with is not known to the
        tree: the library says), and `accepts`, the kinds of model the place takes when the library says (a model of another kind is not put there) '''
    from fieldes import completion
    from fieldes.stdlib.fea import _SLOTS
    for call in ast.walk(value):
        if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Name):
            continue
        for k, a in enumerate(call.args):
            if isinstance(a, ast.Constant) and a.value is Ellipsis and A._span(a) == span:
                param = completion.parameter_at(call.func.id, k)
                info = {'callee': call.func.id, 'param': param, 'list': param in _SLOTS}
                accepts = None if param in _SLOTS else _slot_kinds(call.func.id, k)
                if accepts:
                    info['accepts'] = accepts
                return info
        for kw in call.keywords:
            if kw.arg and isinstance(kw.value, ast.Constant) and kw.value.value is Ellipsis and A._span(kw.value) == span:
                return {'callee': call.func.id, 'param': kw.arg, 'list': kw.arg in _SLOTS}
    return {}


def facts_of(v):
    ''' What a value says about the model a statement made: the facts a run gives the tree '''
    from fieldes.kinds import kind_of, domain_of_value
    Shape, FailedPart = A._types()
    failed = bool(FailedPart) and isinstance(v, FailedPart)
    shape = isinstance(v, Shape)
    result = not shape and callable(getattr(v, '_display', None))
    mkind = None if failed else kind_of(v)
    shown = shape or result
    f = {'model': bool(shape or failed or result or mkind is not None), 'failed': failed, 'result': result, 'type': mkind,
         'shown': shown, 'no_gizmo': bool(getattr(v, '_no_gizmo', False)) or result or not shown or mkind == 'field',   # (a field: its icon is what is dragged)
         'outline': bool(getattr(v, '_outline_gizmo', False)),         # (a profile, a polyline, a spline: no numbers of its own, but a gizmo mode)
         'is_list': isinstance(v, list), 'is_pair': isinstance(v, tuple) and len(v) == 2}
    if failed:
        f['error'] = v.error
    if mkind == 'conditions':
        f['condition_role'] = A._condition_role(v)
    if mkind in ('conditions', 'simulation'):
        domain = domain_of_value(v)         # (the colour and the icon of its engineering domain)
        if domain:
            f['domain'] = domain
    if mkind in ('conditions', 'material', 'surface'):
        f['cls'] = type(v).__name__
    if getattr(v, '_is_viewport', False):
        f['viewport'] = True            # (app.viewport(...): a setting of an app, not a model: no row)
        f['shown'] = False
        f['model'] = False
    f['surface_selection'] = bool(mkind == 'surface' and type(v).__name__ == 'SurfaceSelection')
    f['linked'] = bool(getattr(v, '_linked', False))          # (a linked copy, instance(x): moved and turned only -- its gizmo has no scale)
    f['symmetry_image'] = getattr(v, '_symmetry_of', None) is not None        # (the image of a model, symmetry_image(x, ...): it has no numbers of its own -- x is dragged)
    f['colored'] = hasattr(v, '_color_field')       # (a shape coloured by a field: it cannot be given a name to be edited by dragging)
    b = getattr(v, '_bounds', None) if shape else None
    if b:
        f['bounds'] = A._bounds_list(b)
        # (the shape knows its extent: the region of interest can be made from it; an estimate is not that, nor is the box of a part whose
        # surfaces have been dragged -- one that holds it)
        f['exact_bounds'] = not getattr(v, '_bounds_loose', False)
    used = getattr(v, '_custom_resolution_used', None)
    if isinstance(used, (int, float)):
        f['custom_used'] = used
    return f


def _property_text(v):
    ''' How a property of a result is shown in the property drawer (its kind and a short text), or None when it is not one to offer:
        numbers, vectors and short lists of numbers, flags, short texts '''
    import math
    if isinstance(v, bool):
        return 'flag', 'yes' if v else 'no'
    if isinstance(v, (int, float)):
        return ('number', '%.4g' % v) if math.isfinite(v) else None
    if isinstance(v, (tuple, list)) and 2 <= len(v) <= 8 and all(isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x) for x in v):
        return 'number', '(' + ', '.join('%.4g' % x for x in v) + ')'
    if isinstance(v, str) and 0 < len(v) <= 28 and '\n' not in v:
        return 'text', v
    return None


def exports_of(value):
    ''' What can be exported of a result (see fieldes.stdlib.exporting): "fea" and "results" for the tetrahedral static analysis, "summary" (its key
        numbers as text) for any result that has some, "mesh" when it has a body '''
    out = []
    try:
        from fieldes.stdlib import exporting as E
        from fieldes.stdlib import fea
        if isinstance(value, fea.TetResult):
            # (the finite-element model is what was solved: a 10-node analysis has none that another solver could read yet)
            out += ['results'] if getattr(value, 'element', 'tet') == 'tet10' else ['fea', 'results']
        if E.result_summary(value):
            out.append('summary')
        try:
            E._body_of(value, 'exports')
            out.append('mesh')
        except Exception:
            pass
    except Exception:
        pass
    return out


def properties_of(value):
    ''' What a result offers to be taken out of it, for the property drawer of the model tree: [{name, kind, text}] -- the fields it calculated
        (`result.fields`, each also an attribute by its name: `stress = result.von_mises`) first, then its numbers, flags and short texts (an
        attribute of the result: `peak = result.max_von_mises`) '''
    out, seen = [], set()
    try:
        fields = value.fields
    except Exception:
        fields = {}
    if isinstance(fields, dict):
        for name in fields:
            if isinstance(name, str) and not name.startswith('_'):
                out.append({'name': name, 'kind': 'field', 'text': ''})
                seen.add(name)
    try:
        attrs = list(vars(value).items())
    except TypeError:
        attrs = []
    for name, v in attrs:
        if name.startswith('_') or name in seen:
            continue
        got = _property_text(v)
        if got:
            out.append({'name': name, 'kind': got[0], 'text': got[1]})
            seen.add(name)
    try:
        from fieldes.stdlib.exporting import _COMPUTED_NUMBERS
        for name in _COMPUTED_NUMBERS:
            got = _property_text(getattr(value, name, None))
            if got and name not in seen:
                out.append({'name': name, 'kind': got[0], 'text': got[1]})
    except Exception:
        pass
    return out


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
                        A._FIELD_SOURCES.append((t.id, v, t.id in shown_names))
                    if facts.get('result') and not facts.get('failed'):
                        try:
                            facts['properties'] = properties_of(v)
                            facts['exports'] = exports_of(v)
                        except Exception:
                            pass
                    shapes = [] if facts.get('failed') else _display_shapes(v)
                    if shapes and isinstance(v, Shape):
                        # (the box a statement was given when it ran is the box of the shape then: the statements after it -- handles(), gizmo() --
                        # move and stretch it, and the model is what they have made of it.  A box that is kept from before would leave a model that
                        # was moved out of the render region, which is made to hold the models by it, cut off)
                        final = getattr(v, '_bounds', None)
                        if final:
                            facts['bounds'] = A._bounds_list(final)
                            facts['exact_bounds'] = not getattr(v, '_bounds_loose', False)
                        else:
                            facts.pop('bounds', None)
                            facts.pop('exact_bounds', None)
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
                # (a field that is displayed by its name -- `swell` -- is the field of that name, shown: it has no entry of its own)
                if facts and facts.get('type') == 'field' and isinstance(v, Shape) and not isinstance(stmt.value, ast.Name):
                    A._FIELD_SOURCES.append(('line:%d' % stmt.lineno, v, True))
                shapes = _display_shapes(v) if facts is not None else []
                if shapes:
                    measure.append((0, None, text, shapes, dict(facts, _has_var=False, _own=isinstance(v, Shape)), facts))
        deadline = _time.time() + A._BOUNDS_SECONDS
        for _, name, text, shapes, view, facts in sorted(measure, key=lambda m: m[0]):
            # (only a body has an extent to search for: a field has none -- its viewer's bounds are the box of its origin --, a point or a line has no inside, and a 2D shape goes on
            # for ever along its normal: a search for any of them cannot end well, and costs the time it is given, at every run)
            if 'bounds' not in facts and facts.get('type') in (None, 'solid', 'lattice') and _time.time() < deadline:
                b = A._estimate_bounds(shapes, deadline)
                if b:
                    facts['bounds'] = b
        deadline = _time.time() + A._BOUNDS_SECONDS
        for _, name, text, shapes, view, facts in sorted(measure, key=lambda m: m[0]):
            if _time.time() > deadline:
                break
            if view.get('_has_var') or facts.get('colored') or not view.get('_own') or getattr(shapes[0], '_no_handles', False):
                continue                    # (a shape that is made from another -- a profile of a surface -- has no faces of its own to pull)
            n = A._handle_count(shapes[0])
            facts['can_handles'] = 0 < n <= A._MAX_HANDLES
            facts['handle_count'] = n
            facts['handle_cap'] = A._MAX_HANDLES
            # (the numbers are the two ends of an extrusion of a 2D shape that has handles of its own: the sides are pulled by those, see profile.extrude)
            facts['ends_only'] = A._handles_ends_only(shapes[0])
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
                     'shown': it.get('displayable') is not False, 'no_gizmo': bool(it.get('no_gizmo')), 'outline': bool(it.get('outline'))}
            for k in ('error', 'condition_role', 'domain', 'cls', 'bounds', 'can_handles', 'handle_count', 'handle_cap', 'ends_only'):
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


# A section of the script: the comment `# SECTION name` (or `# %% name`) -- it folds what follows, up to the next one, and the model tree
# shows it as a row the models below it stand under
_SECTION_RE = re.compile(r'^(\s*#\s*)(SECTION\b|%%)[ \t]*[:\-]?[ \t]*(.*?)[ \t]*$')


def _sections_of(lines):
    ''' The sections of a script, in order: [{line, end_line, name, span, marker}] -- `span` is the name in the comment (line, column, line,
        column: a rename writes there), `end_line` the line before the next section (or the last of the script) '''
    out = []
    for i, text in enumerate(lines):
        m = _SECTION_RE.match(text)
        if m:
            out.append({'line': i + 1, 'name': m.group(3), 'span': [i + 1, m.start(3), i + 1, m.end(3)], 'marker': m.group(2)})
    for k, s in enumerate(out):
        s['end_line'] = out[k + 1]['line'] - 1 if k + 1 < len(out) else len(lines)
    return out


# --- The plain Python of a script.  The tree is a picture of the code: the statements at the top level that make no model -- an if, a loop, a function, a class --
# are rows of their own ("python": movable among the top-level rows, to structure the code), with what is in them under them (locked: the code is edited in the
# code editor, not in the tree).  A name in a header or assigned in a body is a row that a click selects in the code, ready to be replaced
_PY_BLOCKS = {ast.If: 'if', ast.For: 'for', ast.AsyncFor: 'for', ast.While: 'while', ast.With: 'with', ast.AsyncWith: 'with', ast.Try: 'try',
              ast.FunctionDef: 'def', ast.AsyncFunctionDef: 'def', ast.ClassDef: 'class'}
if hasattr(ast, 'Match'):
    _PY_BLOCKS[ast.Match] = 'match'
if hasattr(ast, 'TryStar'):
    _PY_BLOCKS[ast.TryStar] = 'try'
_PY_MAX_CHILDREN = 80           # (per level: a loop of two hundred statements is not two hundred rows)
_PY_MAX_DEPTH = 8
_LIB_NAMES = []


def _library_names():
    ''' The names that are the language's or the library's (a name in a condition that is one of them is not a variable of the script) '''
    if not _LIB_NAMES:
        import builtins
        names = set(dir(builtins))
        try:
            import fieldes
            names |= {k for k in vars(fieldes) if not k.startswith('_')}
        except Exception:
            pass
        _LIB_NAMES.append(names)
    return _LIB_NAMES[0]


def _py_span_of(first, last):
    a, b = A._span(first), A._span(last)
    return [a[0], a[1], b[2], b[3]]


def _py_label(stmt, src):
    seg = lambda n, k=50: A._short(src.segment(n), k)
    if isinstance(stmt, ast.If):
        return 'if ' + seg(stmt.test)
    if isinstance(stmt, (ast.For, ast.AsyncFor)):
        return 'for ' + seg(stmt.target, 24) + ' in ' + seg(stmt.iter, 40)
    if isinstance(stmt, ast.While):
        return 'while ' + seg(stmt.test)
    if isinstance(stmt, (ast.With, ast.AsyncWith)):
        return 'with ' + ', '.join(seg(i.context_expr, 30) for i in stmt.items)
    if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef)):
        a = stmt.args
        names = [x.arg for x in list(getattr(a, 'posonlyargs', [])) + list(a.args)]
        if a.vararg:
            names.append('*' + a.vararg.arg)
        names += [x.arg for x in a.kwonlyargs]
        if a.kwarg:
            names.append('**' + a.kwarg.arg)
        return 'def %s(%s)' % (stmt.name, A._short(', '.join(names), 40))
    if isinstance(stmt, ast.ClassDef):
        return 'class ' + stmt.name
    if hasattr(ast, 'Match') and isinstance(stmt, ast.Match):
        return 'match ' + seg(stmt.subject)
    return 'try'


def _py_header_children(stmt, src):
    ''' The names a block's header holds: what a condition or a loop reads (dim: it is used there), the variable of a loop, the parameters of a function '''
    out, seen = [], set()
    lib = _library_names()

    def add(node, kind, filtered=True):
        if node is None:
            return
        for n in sorted((x for x in ast.walk(node) if isinstance(x, ast.Name)), key=lambda x: (x.lineno, x.col_offset)):
            if n.id in seen or (filtered and n.id in lib):
                continue
            seen.add(n.id)
            out.append({'kind': kind, 'label': n.id, 'span': A._span(n), 'line': n.lineno, 'end_line': n.end_lineno,
                        'text': A._short(src.segment(node), 80)})

    if isinstance(stmt, (ast.If, ast.While)):
        add(stmt.test, 'uses')
    elif isinstance(stmt, (ast.For, ast.AsyncFor)):
        add(stmt.target, 'loopvar', False)
        add(stmt.iter, 'uses')
    elif isinstance(stmt, (ast.With, ast.AsyncWith)):
        for item in stmt.items:
            add(item.context_expr, 'uses')
            add(item.optional_vars, 'var', False)
    elif isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef)):
        a = stmt.args
        params = list(getattr(a, 'posonlyargs', [])) + list(a.args) + list(a.kwonlyargs)
        if a.vararg:
            params.append(a.vararg)
        if a.kwarg:
            params.append(a.kwarg)
        for p in params:
            out.append({'kind': 'param', 'label': p.arg, 'span': A._span(p), 'line': p.lineno, 'end_line': p.end_lineno,
                        'text': 'parameter ' + p.arg})
    elif isinstance(stmt, ast.ClassDef):
        for b in stmt.bases:
            add(b, 'uses')
    elif hasattr(ast, 'Match') and isinstance(stmt, ast.Match):
        add(stmt.subject, 'uses')
    return out


def _py_branch(label, stmts, src, depth):
    ''' An else, an except, a finally, a case: a row of its own holding its statements '''
    return {'kind': 'branch', 'label': label, 'span': _py_span_of(stmts[0], stmts[-1]), 'line': stmts[0].lineno, 'end_line': stmts[-1].end_lineno,
            'text': label, 'children': _py_children(stmts, src, depth)}


def _assigned_names(target):
    ''' The names an assignment target makes: a name, or the names of a tuple or a list that is unpacked -- `self.size = 4` or `a[0] = 1` makes no name (it changes something that is there) '''
    if isinstance(target, ast.Name):
        return [target]
    if isinstance(target, (ast.Tuple, ast.List)):
        return [n for e in target.elts for n in _assigned_names(e)]
    if isinstance(target, ast.Starred):
        return _assigned_names(target.value)
    return []


def _py_children(stmts, src, depth):
    out = []
    for s in stmts:
        if len(out) >= _PY_MAX_CHILDREN:
            out.append({'kind': 'more', 'label': '... %d more' % (len(stmts) - _PY_MAX_CHILDREN), 'span': _py_span_of(s, stmts[-1]),
                        'line': s.lineno, 'end_line': stmts[-1].end_lineno, 'text': 'more statements'})
            break
        if type(s) in _PY_BLOCKS:
            out.append(_py_block(s, src, depth + 1))
            continue
        names = []
        if isinstance(s, (ast.Assign, ast.AnnAssign, ast.AugAssign)):
            targets = s.targets if isinstance(s, ast.Assign) else [s.target]
            for t in targets:
                names += _assigned_names(t)
        if names:
            for n in names:
                out.append({'kind': 'var', 'label': n.id, 'span': A._span(n), 'line': s.lineno, 'end_line': s.end_lineno,
                            'text': A._short(src.segment(s), 90)})
        else:
            out.append({'kind': 'stmt', 'label': A._short(src.segment(s), 50), 'span': A._span(s), 'line': s.lineno, 'end_line': s.end_lineno,
                        'text': A._short(src.segment(s), 90)})
    return out


def _py_block(stmt, src, depth=0, label=None):
    start = min([stmt.lineno] + [d.lineno for d in getattr(stmt, 'decorator_list', [])])
    out = {'kind': 'block' if depth else 'python', 'type': _PY_BLOCKS[type(stmt)], 'label': label or _py_label(stmt, src),
           'line': start, 'end_line': stmt.end_lineno, 'span': A._span(stmt), 'text': A._short(src.segment(stmt), 120)}
    kids = _py_header_children(stmt, src)
    if depth < _PY_MAX_DEPTH:
        kids += _py_children(stmt.body if not (hasattr(ast, 'Match') and isinstance(stmt, ast.Match)) else [], src, depth)
        orelse = getattr(stmt, 'orelse', None)
        if isinstance(stmt, ast.If) and len(orelse) == 1 and isinstance(orelse[0], ast.If):
            kids.append(_py_block(orelse[0], src, depth + 1, 'elif ' + A._short(src.segment(orelse[0].test), 50)))
        elif orelse:
            kids.append(_py_branch('else', orelse, src, depth))
        for h in getattr(stmt, 'handlers', []):
            kids.append(_py_branch('except' + (' ' + A._short(src.segment(h.type), 40) if h.type is not None else ''), h.body, src, depth))
        if getattr(stmt, 'finalbody', None):
            kids.append(_py_branch('finally', stmt.finalbody, src, depth))
        for case in getattr(stmt, 'cases', []):
            kids.append(_py_branch('case ' + A._short(src.segment(case.pattern), 40), case.body, src, depth))
    out['children'] = kids
    return out


def _python_blocks(tree, src):
    ''' The blocks of plain Python at the top level of a script: [{kind: 'python', type, label, line, end_line, children: [...]}] '''
    out = []
    for stmt in tree.body:
        if type(stmt) in _PY_BLOCKS:
            try:
                block = _py_block(stmt, src)
            except Exception:
                continue            # (a statement the tree cannot describe stays code in the editor)
            # (the names the block reads: a loop, an if, a with, a try runs where it stands, so it is a user of the models it reads, as a statement is -- the models directly above it that it reads are its children
            # in the tree.  A def or a class reads its free names when it is CALLED, not where it is written: it is not a user, and it separates the rows like any block)
            if not isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                block['reads'] = sorted({n.id for n in ast.walk(stmt) if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load)})
            out.append(block)
    return out


# ---------------------------------------------------------------------------
# Moving statements: what a move of lines of the script must not break.  Every statement of the top level says which names it makes (`defs`) and which it
# needs from the statements above it (`needs`: what it reads when it runs, and what the functions and classes it names read when they are called -- a `def`
# reads nothing until something calls it).  A move is judged by the order it makes: every statement has to read each name from the same statement it read it
# from before (the last one above it that made the name).  That catches a name read before it is made -- an error, a function called above its `def` --
# and a name that would be read from another line (the same model, made by another statement).  The statements that have to go along to keep it so are found.

_BUILTIN_NAMES = frozenset(dir(__import__('builtins')))
_SCOPES = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
_COMPREHENSIONS = (ast.ListComp, ast.SetComp, ast.GeneratorExp, ast.DictComp)


def _target_names(node, out):
    ''' The names a target (an assignment, a loop variable, `with ... as`) makes '''
    if node is None:
        return
    for n in ast.walk(node):
        if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store):
            out.add(n.id)


def _alias_names(node):
    return [(a.asname or a.name).split('.')[0] for a in node.names if a.name != '*']


def _defs_of(stmt):
    ''' The names a statement of the top level makes: assigned, defined, imported (what is inside a function or a class is a scope of its own) '''
    out = set()

    def visit(n):
        if isinstance(n, _SCOPES):
            out.add(n.name)
            return
        if isinstance(n, (ast.Lambda,) + _COMPREHENSIONS):
            return
        if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store):
            out.add(n.id)
        elif isinstance(n, (ast.Import, ast.ImportFrom)):
            out.update(_alias_names(n))
        elif isinstance(n, ast.ExceptHandler) and n.name:
            out.add(n.name)
        for child in ast.iter_child_nodes(n):
            visit(child)

    visit(stmt)
    return out


def _body_loads(func):
    ''' The names a function reads when it is called: every name in it that is not its own (a parameter, something it assigns) '''
    a = func.args
    local = {p.arg for p in list(getattr(a, 'posonlyargs', [])) + list(a.args) + list(a.kwonlyargs)}
    if a.vararg:
        local.add(a.vararg.arg)
    if a.kwarg:
        local.add(a.kwarg.arg)
    declared = set()
    for n in ast.walk(func):
        if n is func:
            continue
        if isinstance(n, (ast.Global, ast.Nonlocal)):
            declared.update(n.names)
        elif isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store):
            local.add(n.id)
        elif isinstance(n, _SCOPES):
            local.add(n.name)
        elif isinstance(n, ast.ExceptHandler) and n.name:
            local.add(n.name)
        elif isinstance(n, (ast.Import, ast.ImportFrom)):
            local.update(_alias_names(n))
    local -= declared
    return {n.id for n in ast.walk(func) if isinstance(n, ast.Name) and isinstance(n.ctx, (ast.Load, ast.Del)) and n.id not in local}


def _immediate(node, bound, out):
    ''' The names that a statement reads while it runs, in the order Python runs it, that no earlier part of it made (`bound` grows as names are made).  The body
        of a function is not run by its `def`: only its decorators and defaults are; the body of a class is '''
    if node is None:
        return
    if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
        a = node.args
        for e in list(node.decorator_list) + list(a.defaults) + [d for d in a.kw_defaults if d is not None]:
            _immediate(e, bound, out)
        bound.add(node.name)
    elif isinstance(node, ast.ClassDef):
        for e in list(node.decorator_list) + list(node.bases) + [k.value for k in node.keywords]:
            _immediate(e, bound, out)
        inner = set(bound)
        for s in node.body:
            _immediate(s, inner, out)
        bound.add(node.name)
    elif isinstance(node, ast.Lambda):
        a = node.args
        for d in list(a.defaults) + [d for d in a.kw_defaults if d is not None]:
            _immediate(d, bound, out)
        inner = set(bound) | {p.arg for p in list(getattr(a, 'posonlyargs', [])) + list(a.args) + list(a.kwonlyargs)}
        _immediate(node.body, inner, out)
    elif isinstance(node, _COMPREHENSIONS):
        inner = set(bound)
        for g in node.generators:
            _immediate(g.iter, inner, out)
            _target_names(g.target, inner)
            for c in g.ifs:
                _immediate(c, inner, out)
        for e in ([node.key, node.value] if isinstance(node, ast.DictComp) else [node.elt]):
            _immediate(e, inner, out)
    elif isinstance(node, ast.Name):
        if isinstance(node.ctx, ast.Store):
            bound.add(node.id)
        elif node.id not in bound:
            out.add(node.id)
    elif isinstance(node, ast.Assign):
        _immediate(node.value, bound, out)
        for t in node.targets:
            _immediate(t, bound, out)
    elif isinstance(node, ast.AnnAssign):
        _immediate(node.value, bound, out)
        _immediate(node.target, bound, out)
    elif isinstance(node, ast.AugAssign):
        _immediate(node.value, bound, out)
        if isinstance(node.target, ast.Name):
            if node.target.id not in bound:
                out.add(node.target.id)
            bound.add(node.target.id)
        else:
            _immediate(node.target, bound, out)
    elif isinstance(node, (ast.For, ast.AsyncFor)):
        _immediate(node.iter, bound, out)
        _immediate(node.target, bound, out)
        for s in list(node.body) + list(node.orelse):
            _immediate(s, bound, out)
    elif isinstance(node, (ast.Import, ast.ImportFrom)):
        bound.update(_alias_names(node))
    elif isinstance(node, ast.ExceptHandler):
        _immediate(node.type, bound, out)
        if node.name:
            bound.add(node.name)
        for s in node.body:
            _immediate(s, bound, out)
    elif isinstance(node, ast.NamedExpr):
        _immediate(node.value, bound, out)
        _immediate(node.target, bound, out)
    else:
        for child in ast.iter_child_nodes(node):
            _immediate(child, bound, out)


def _statement_facts(tree):
    ''' For each statement of the top level: the names it makes (`defs`) and the names it needs from above (`needs`).  A star import (`from mfgkit import *`) makes a name
        of its own, `*<its number>`, which what reads a name that nothing in the script makes -- and the library does not -- needs '''
    stmts = list(tree.body)
    defs = [_defs_of(s) for s in stmts]
    stars = [i for i, s in enumerate(stmts) if isinstance(s, ast.ImportFrom) and any(a.name == '*' for a in s.names) and not (s.module or '').split('.')[0] == 'fieldes']
    for i in stars:
        defs[i].add('*%d' % i)
    everything = set().union(*defs) if defs else set()
    lib = _library_names()
    deferred = {}
    for s in stmts:
        for n in ast.walk(s):
            if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)):
                deferred.setdefault(n.name, set()).update(_body_loads(n))
            elif isinstance(n, ast.ClassDef):
                loads = set()
                for m in ast.walk(n):
                    if isinstance(m, (ast.FunctionDef, ast.AsyncFunctionDef)):
                        loads |= _body_loads(m)
                deferred.setdefault(n.name, set()).update(loads)
    needs = []
    for i, s in enumerate(stmts):
        now = set()
        _immediate(s, set(), now)
        seen, queue = set(), list(now)
        while queue:
            name = queue.pop()
            if name in seen:
                continue
            seen.add(name)
            queue.extend(m for m in deferred.get(name, ()) if m not in seen)
        mine = {n for n in seen if n in everything}
        if isinstance(s, _SCOPES):
            mine.discard(s.name)                    # (a function that calls itself does not need its own line above it)
        unknown = [n for n in seen if n not in everything and n not in lib and n not in _BUILTIN_NAMES]
        if unknown:
            mine.update('*%d' % k for k in stars)
        needs.append(mine)
    return defs, needs


def move_check(arg):
    ''' Would a move of lines break the script?  JSON {source, moved: [[first, last], ...], dest} -- the lines (from 0) that go, and the line they go above -- ->
        JSON {unknown} when the text is not a script; {ok: true}; or {ok: false, problems: [text, ...], along: [[first, last], ...]}: what the move would break, and the
        other statements (their lines) that have to move with it so that nothing breaks '''
    req = json.loads(arg)
    source = req['source']
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return json.dumps({'unknown': True})
    stmts = list(tree.body)
    n = len(stmts)
    lines = source.split('\n')
    starts = [min([s.lineno] + [d.lineno for d in getattr(s, 'decorator_list', [])]) - 1 for s in stmts]
    ends = [s.end_lineno - 1 for s in stmts]
    defs, needs = _statement_facts(tree)
    moved = {i for i in range(n) if any(starts[i] >= a and ends[i] <= b for a, b in req['moved'])}
    dest = int(req['dest'])
    if not moved:
        return json.dumps({'ok': True})
    place = next((i for i in range(n) if starts[i] >= dest), n)

    def order_of(goes):
        return [i for i in range(n) if i < place and i not in goes] + sorted(goes) + [i for i in range(n) if i >= place and i not in goes]

    def reading(order):
        last, out = {}, {}
        for s in order:
            for name in needs[s]:
                out[(s, name)] = last.get(name)
            for name in defs[s]:
                last[name] = s
        return out

    before = reading(list(range(n)))

    def broken(goes):
        now = reading(order_of(goes))
        return [(s, name, d) for (s, name), d in before.items() if d is not None and now[(s, name)] != d]

    first = broken(moved)
    if not first:
        return json.dumps({'ok': True})
    goes = set(moved)
    for _ in range(n + 1):
        bad = broken(goes)
        if not bad:
            break
        grew = False
        for s, name, d in bad:
            for x in (s, d):
                if x not in goes:
                    goes.add(x)
                    grew = True
                    break
        if not grew:
            break
    short = lambda i: A._short(lines[starts[i]].strip(), 50)
    problems, seen = [], set()
    for s, name, d in first:
        if (s, name) in seen:
            continue
        seen.add((s, name))
        if s in moved:
            problems.append({'reader': short(s), 'name': name, 'maker': short(d), 'line': starts[d] + 1, 'kind': 'before'})
        else:
            problems.append({'reader': short(s), 'name': name, 'maker': short(d), 'line': starts[d] + 1, 'kind': 'after'})
    return json.dumps({'ok': False, 'problems': problems[:8], 'along': [[starts[i], ends[i]] for i in sorted(goes - moved)],
                       'alongLabels': [short(i) for i in sorted(goes - moved)]})


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
    from fieldes.kinds import describe as describe_kinds, describe_domains, domain_of
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
                entry = {'line': line, 'end_line': end, 'span': A._span(stmt), 'text': src.segment(stmt)}
                if A._short_name(fn) == 'set_bounds':
                    # (auto_scale, on unless the call says False: the region grows in the script when a model reaches out of it)
                    entry['auto_scale'] = not any(kw.arg == 'auto_scale' and isinstance(kw.value, ast.Constant) and kw.value.value is False
                                                  for kw in stmt.value.keywords)
                settings[A._short_name(fn)] = entry
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
                'args': A._args_of(call, ns, by_name, src),
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

        # --- "x = gizmo(x, ...)" / "x = handles(x, [...])" / "x = render_cache(x)" / "x = lock(x)": what edits the shape x
        if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name) and \
                isinstance(stmt.value, ast.Call) and \
                A._short_name(A._call_name(stmt.value)) in ('gizmo', 'handles', 'render_cache', 'lock', 'custom_resolution') and \
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
            if what == 'handles':
                target['handles'] = {'line': line, 'end_line': end, 'call': A._span(stmt.value), 'text': A._short(src.segment(stmt), 60)}
                target['has_var'] = True
                continue
            mode, mode_span = None, None
            for kw in stmt.value.keywords:
                if kw.arg == 'mode' and isinstance(kw.value, ast.Constant) and isinstance(kw.value.value, str):
                    mode, mode_span = kw.value.value, A._span(kw.value)
            if mode not in ('click', 'never', 'always'):
                mode = 'click'
            target['gizmo'] = {
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
                            'no_gizmo': facts.get('no_gizmo', not shown or mkind in ('simulation', 'conditions', 'field')
                                                  or bool(getattr(ns.get(callee), '_makes_no_gizmo', False))),
                            'outline': bool(facts.get('outline', getattr(ns.get(callee), '_makes_outline_gizmo', False))),      # (no numbers, but a gizmo mode: see handles.gizmo)
                            'text': A._short(src.segment(stmt.value), 70), 'deps': deps}
                    if mkind:
                        item['type'] = mkind
                    if not shown:
                        item['displayable'] = False
                    item['inputs'], item['variadic'], item['numbers'] = inputs, variadic, numbers
                    if variadic:
                        item['min_inputs'] = A._min_inputs(ns.get(callee))
                    item['slots'] = A._call_slots(stmt.value, ns, src)
                    item['args'] = A._args_of(stmt.value, ns, by_name, src)
                    # `x = result.von_mises`: a property taken out of a model (the property drawer writes them).  It is made of the model, but does not
                    # own it: the model stays where it is, and the property row holds a reference to it
                    node = stmt.value
                    while isinstance(node, ast.Attribute):
                        node = node.value
                    if isinstance(stmt.value, ast.Attribute) and isinstance(node, ast.Name) and node.id in by_name and node.id != t.id:
                        item['attribute_of'] = node.id
                    if mkind == 'conditions':
                        item['condition_role'] = facts.get('condition_role') or _guess_role(callee)
                    if mkind in ('conditions', 'simulation'):
                        domain = facts.get('domain') or domain_of(callee)
                        if domain:
                            item['domain'] = domain
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
                    if _call_takes(callee):
                        item['takes'] = _call_takes(callee)
                    item['role'] = 'operation' if deps else 'primitive'
                    if is_blocked(stmt.value):
                        item['block'] = A._call_name(stmt.value)
                    item['has_var'] = any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'var'
                                          for n in ast.walk(stmt.value)) or any(by_name[d].get('has_var') for d in deps)
                    if failed:
                        item['error'] = facts.get('error') or error.get('message')
                    if exact:
                        if facts.get('properties'):
                            item['properties'] = facts['properties']
                        if facts.get('exports'):
                            item['exports'] = facts['exports']
                        if facts.get('bounds'):
                            item['bounds'] = facts['bounds']
                            if facts.get('exact_bounds'):
                                item['roi_expr'] = t.id
                        if 'can_handles' in facts:
                            item['can_handles'] = facts['can_handles']
                            item['handle_count'] = facts['handle_count']
                            item['handle_cap'] = facts['handle_cap']
                            if facts.get('ends_only'):
                                item['ends_only'] = True
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
            if isinstance(stmt.value, ast.Call) and A._call_name(stmt.value).startswith('app.'):
                continue                # (app.title(...), app.viewport(...): settings of an app, not models)
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
            if mkind in ('conditions', 'simulation'):
                domain = facts.get('domain') or domain_of(A._callee_name(stmt.value))
                if domain:
                    item['domain'] = domain
            item['inputs'], item['variadic'], item['numbers'] = A._inputs_of(stmt.value, ns, by_name)
            if item['variadic']:
                item['min_inputs'] = A._min_inputs(ns.get(A._callee_name(stmt.value)))
            item['slots'] = A._call_slots(stmt.value, ns, src)
            item['args'] = A._args_of(stmt.value, ns, by_name, src)
            item['points'] = A._points_of(stmt.value, ns) if isinstance(stmt.value, ast.Call) else []
            item['callee'] = A._callee_name(stmt.value)
            if _call_takes(item['callee']):
                item['takes'] = _call_takes(item['callee'])
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
            if 'can_handles' in facts:
                item['can_handles'], item['handle_count'], item['handle_cap'] = (
                    facts['can_handles'], facts['handle_count'], facts['handle_cap'])
                if facts.get('ends_only'):
                    item['ends_only'] = True
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
            h = it.get('gizmo')
            if h:
                it['mode'], it['mode_explicit'] = h['mode'], True
            else:
                # (the gizmo of a profile, a polyline or a spline starts off: it is there when the tree's gizmo button, or a gizmo() line, says so)
                it['mode'], it['mode_explicit'] = ('never' if it.get('outline') else 'click'), False

    items.sort(key=lambda it: it['line'])
    for it in items:
        names = set(A.shadow_names(src.lines[it['line'] - 1:it.get('end_line', it['line'])]))
        if names:
            it['shadows'] = sorted(names)
    versions = {}
    for it in items:
        if 'var' in it:
            versions.setdefault(it['var'], []).append(it)
    # A model belongs to the first statement that uses it -- within its section: a section is a chapter of the script, and what is made in
    # one stands in it, however later chapters use it (they hold a reference to it, as a shadow)
    sections = _sections_of(src.lines)

    def section_of(line):
        k = -1
        for i, s in enumerate(sections):
            if s['line'] <= line:
                k = i
            else:
                break
        return k

    py_blocks = _python_blocks(tree, src)

    # THE NESTING.  The rows of the top level stand in the order of the script, and a model is the CHILD of its first user only when it stands directly above it: every row between the model and the user is one of
    # the user's own inputs (with the models those hold).  Any other row between them -- another model, a variable, a display line, an import, a block of plain Python -- separates them: the model keeps its own row
    # where it is made, and the user shows a shadow of it.  (It used to go down under the first user however far away, past whatever lay between: the tree listed a loop above the topology optimization its
    # numbers came from.)  A block of plain Python (a loop, a def, an if) is a user as a statement is: the models directly above it that it reads are inside its row.  Users are taken in the order of the script,
    # so the first user of a model is the one that holds it
    rows = sorted([(it['line'], it, False) for it in items] + [(b['line'], b, True) for b in py_blocks], key=lambda r: r[0])

    def row_key(row, is_block):
        return 'python@%d' % row['line'] if is_block else A._key_of(row)

    def wants(row, is_block):
        ''' the names a row reads, except the ones it holds by reference (`# shadow:`) or takes a property of: neither makes the model a child '''
        if is_block:
            return set(row.get('reads', ()))
        return set(d for d in row.get('deps', []) if d not in row.get('shadows', ()) and d != row.get('attribute_of'))

    for index, (uline, user, user_block) in enumerate(rows):
        group = [(user, user_block)]
        group_keys = {row_key(user, user_block)}
        j = index - 1
        while j >= 0:
            _, row, is_block = rows[j]
            if is_block or 'var' not in row:
                break
            if 'owner' in row:
                if row['owner'] in group_keys:           # (an input of an input: it stands in the run, and is held by that input)
                    j -= 1
                    continue
                break
            first = None
            for member, member_block in sorted(group, key=lambda m: m[0]['line']):
                if row['var'] in wants(member, member_block):
                    earlier = [x for x in versions.get(row['var'], []) if x['line'] < member['line']]
                    if earlier and earlier[-1] is row and section_of(row['line']) == section_of(member['line']):
                        first = (member, member_block)
                        break
            if first is None:
                break
            row['owner'] = row_key(*first)
            group.append((row, False))
            group_keys.add(row_key(row, False))
            j -= 1
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

    return json.dumps({'items': items, 'settings': settings, 'kinds': describe_kinds(), 'domains': describe_domains(), 'truncated': truncated,
                       'sections': sections, 'python': py_blocks,
                       'errored': bool(error), 'run_done': bool(req.get('run_done')),
                       'done_line': tree.body[-1].end_lineno if tree.body else 0, 'source_md5': md5,
                       'has_roi': 'roi' in ns, 'has_roi_resolution': 'roi_resolution' in ns}, default=str)
