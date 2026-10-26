'''
Your own primitives: which shapes the Primitives menu offers, and shapes of your own that it offers too.

The menus show a short list of PRIMITIVES (the most basic shape of each kind: a box, a circle ...) apart from the Parametric shapes (a rounded box, a ring, a profile ...).  What is in that list
is yours to change, from the Primitives menu of the menu bar:

    add         a Parametric shape (or any entry of the menus) to the list of Primitives
    remove      a primitive from the list: it is then offered with the Parametric shapes
    add selection   the model that is selected in the model tree -- with every statement of the script that it is made of -- becomes a primitive of your own, under the name you give it.
                    It is kept as a FILE of its own, `<name>.py` in the `primitives` folder of yours: a function that makes the model (`def name(): ... return model`), a block like any
                    other (see blocks.py): scripts run it, the editor completes it, shows its call tip and goes to its definition.  Picking it from the menu writes ONE line, `name_1 = name()`:
                    the model is one object, in the script and in the model tree, and what it is made of stays in its file -- as the geometry of an imported part stays in the cache files
                    of the import.  The numbers it is made of are the numbers it had (a var() of the script it came from is its number now); the handles of the model in a script are its own

What you choose is kept in ONE file of your own, `primitives.json`, and the primitives themselves in the folder `primitives` next to it -- in the folder that is yours: `%APPDATA%\\FielDes` on
Windows, `~/.local/share/FielDes` on Linux (the application sets FIELDES_USER_DIR) --, and are there in every script you open, whether or not the script you made a primitive in still exists.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import copy
import io
import json
import keyword
import os
import re
import sys
import tokenize

# What the menus call each kind of model: the group of the Primitives menu a primitive of that kind is in
GROUP_OF_TYPE = {'solid': '3D', 'profile': '2D', 'curve': '1D', 'point': 'Points', 'surface': 'Surfaces', 'field': 'Fields', 'symmetry': 'Symmetry',
                 'lattice': '3D', 'import': '3D', 'points': 'Points'}

# The section of the manager (the kind of the list) a model of each type of the model tree belongs to: a lattice or an imported part is a 3D shape, a set of points are points.  A type this
# does not know (a simulation, a material ...) is a 3D shape as well, so that what was made a primitive of the user's own is in a list, and can be found and renamed there
KIND_OF_TYPE = {'solid': 'solid', 'lattice': 'solid', 'import': 'solid', 'profile': 'profile', 'curve': 'curve', 'point': 'point', 'points': 'point', 'surface': 'surface', 'field': 'field',
                'symmetry': 'symmetry'}


def kind_of(typ):
    return KIND_OF_TYPE.get(typ or 'solid', 'solid')


_CACHE = {'stamp': None, 'data': None}


def user_dir():
    ''' The folder that is the user's: FIELDES_USER_DIR (set by the application to its data folder), else the usual place of the system '''
    given = os.environ.get('FIELDES_USER_DIR')
    if given:
        return given
    if sys.platform.startswith('win'):
        return os.path.join(os.environ.get('APPDATA') or os.path.expanduser('~'), 'FielDes')
    return os.path.join(os.environ.get('XDG_DATA_HOME') or os.path.join(os.path.expanduser('~'), '.local', 'share'), 'FielDes')


def path():
    return os.path.join(user_dir(), 'primitives.json')


def folder():
    ''' The folder of the primitives of the user's own: a file each (read as blocks: see fieldes.blocks) '''
    return os.path.join(user_dir(), 'primitives')


def _file(name):
    return os.path.join(folder(), name + '.py')


def _empty():
    return {'added': [], 'removed': [], 'labels': {}}


def load():
    ''' What the user chose: {added: [names], removed: [names], labels: {name: the name the menus show}}.  A file that is not there, or cannot be read, is no choices.  (A file of an older
        FielDes kept the primitives of the user's own in it as well, `custom`, with the statements they are made of: each of those is written as a file of its own, once, and leaves it) '''
    p = path()
    try:
        stamp = (os.path.getmtime(p), os.path.getsize(p))
    except OSError:
        _CACHE['stamp'], _CACHE['data'] = None, None
        return _empty()
    if _CACHE['stamp'] == stamp and _CACHE['data'] is not None:
        return _CACHE['data']
    out = _empty()
    old = None
    try:
        with open(p, encoding='utf-8') as f:
            raw = json.load(f)
        if isinstance(raw, dict):
            out['added'] = [str(n) for n in raw.get('added', []) if isinstance(n, str)]
            out['removed'] = [str(n) for n in raw.get('removed', []) if isinstance(n, str)]
            labels = raw.get('labels', {})
            if isinstance(labels, dict):
                out['labels'] = {str(k): str(v) for k, v in labels.items() if isinstance(k, str) and isinstance(v, str) and v.strip()}
            if 'custom' in raw:
                old = [c for c in raw['custom'] if isinstance(c, dict) and isinstance(c.get('name'), str) and isinstance(c.get('expr'), str)] if isinstance(raw['custom'], list) else []
    except (OSError, ValueError):
        out = _empty()
    if old is not None:
        _migrate(old)
        try:
            save(out)                       # (the file without `custom`: what it held is in the folder now)
        except OSError:
            pass
        return out
    _CACHE['stamp'], _CACHE['data'] = stamp, out
    return out


def save(data):
    ''' Writes the file whole, through a temporary file, so that a program that stops half way leaves the old one '''
    p = path()
    os.makedirs(os.path.dirname(p), exist_ok=True)
    temp = p + '.tmp'
    with open(temp, 'w', encoding='utf-8') as f:
        json.dump({'added': data.get('added', []), 'removed': data.get('removed', []), 'labels': data.get('labels', {})}, f, indent=1, ensure_ascii=False)
    os.replace(temp, p)
    _CACHE['stamp'], _CACHE['data'] = None, None


def basic(builtin):
    ''' The names the Primitives menu shows: the built-in list (`builtin`) less what was taken out, and what was put in '''
    d = load()
    return (set(builtin) - set(d['removed'])) | set(d['added'])


def labels():
    ''' The names the user gave the shapes of the library: {function name: what the menus call it} '''
    return load()['labels']


# ---------------------------------------------------------------------------
# The primitives of the user's own: a file each, `<name>.py`, that starts with `#@ primitive: <the kind of model it makes>` and holds the function `<name>()`

_SEEN = {}                 # path -> (stamp, entry): a file is read again when it has changed


def _entry_of(p):
    ''' {name, type, doc, file} of a file of the folder, or None when it is not a primitive (it does not start with `#@ primitive: <kind>`) '''
    try:
        st = os.stat(p)
    except OSError:
        return None
    stamp = (st.st_mtime_ns, st.st_size)
    hit = _SEEN.get(p)
    if hit is not None and hit[0] == stamp:
        return hit[1]
    entry = None
    try:
        with open(p, encoding='utf-8') as f:
            text = f.read()
        head = re.match(r'#@ primitive: (\w+)', text)
        name = os.path.splitext(os.path.basename(p))[0]
        if head is not None:
            doc = ''
            try:
                for node in ast.parse(text).body:
                    if isinstance(node, ast.FunctionDef) and node.name == name:
                        doc = (ast.get_docstring(node) or '').strip().split('\n', 1)[0]
                        break
            except SyntaxError:
                pass                    # (a file that does not read says so when a script runs it: see blocks.errors)
            entry = {'name': name, 'type': head.group(1), 'doc': doc, 'file': p}
    except OSError:
        pass
    _SEEN[p] = (stamp, entry)
    return entry


def entries():
    ''' The primitives of the user's own: [{name, type, doc, file}] '''
    d = folder()
    try:
        names = sorted(os.listdir(d))
    except OSError:
        return []
    out = []
    for n in names:
        if n.endswith('.py') and not n.startswith('_'):
            e = _entry_of(os.path.join(d, n))
            if e is not None:
                out.append(e)
    return out


def find(name):
    for e in entries():
        if e['name'] == name:
            return e
    return None


def catalog_entries():
    ''' The primitives of the user's own, as entries of the menu catalog '''
    out = []
    for c in entries():
        typ = c['type'] or 'solid'
        out.append({'name': c['name'], 'group': GROUP_OF_TYPE.get(typ, 'Custom blocks'), 'type': typ, 'with_bodies': False, 'primitive': True, 'custom': True,
                    'doc': c['doc'], 'label': c['name'].replace('_', ' ')})
    return out


# ---------------------------------------------------------------------------
# A model of the script, with every statement it is made of

def _statement_range(stmt):
    first = min([stmt.lineno] + [d.lineno for d in getattr(stmt, 'decorator_list', [])])
    return first - 1, stmt.end_lineno - 1


def closure(source, line, end_line):
    ''' The statements of the script that the model written on lines `line` .. `end_line` (from 1) is made of, in the order of the script: the statement itself and, for every name it needs, the last
        statement above it that made that name -- and the same for those, all the way down.  Returns (head, main, expr, names): the statements before the model as text, the model's name, the
        text of the expression it is made of, and every name the statements make (the main one included) '''
    from fieldes import outline
    tree = ast.parse(source)
    stmts = list(tree.body)
    lines = source.split('\n')
    at = None
    for i, s in enumerate(stmts):
        a, b = _statement_range(s)
        if a <= line - 1 <= b:
            at = i
            break
    if at is None:
        raise ValueError('the selection is not a statement of the top level of the script')
    main = stmts[at]
    if not (isinstance(main, ast.Assign) and len(main.targets) == 1 and isinstance(main.targets[0], ast.Name)):
        raise ValueError('only a model that is made by a statement of the form `name = ...` can become a primitive')
    defs, needs = outline._statement_facts(tree)
    chosen, todo = {at}, [at]
    while todo:
        s = todo.pop()
        for name in needs[s]:
            maker = None
            for d in range(s - 1, -1, -1):
                if name in defs[d]:
                    maker = d
                    break
            if maker is not None and maker not in chosen:
                chosen.add(maker)
                todo.append(maker)
    head_parts = []
    names = set(defs[at])
    for i in sorted(chosen - {at}):
        a, b = _statement_range(stmts[i])
        head_parts.append('\n'.join(lines[a:b + 1]))
        names |= {n for n in defs[i] if not n.startswith('*')}
    expr = ast.get_source_segment(source, main.value)
    # (a function or a class stands apart from what is above and below it, as it does in the script it was made in)
    head = ''
    for k, part in enumerate(head_parts):
        if k:
            block = lambda t: t.lstrip().startswith(('def ', 'class ', '@', 'async def '))
            head += '\n\n\n' if (block(part) or block(head_parts[k - 1])) else '\n'
        head += part
    return head, main.targets[0].id, expr, sorted(n for n in names if not n.startswith('*'))


def renamed(text, mapping):
    ''' `text` with every name of `mapping` replaced -- a NAME of the code: not after a dot (an attribute), not the keyword of a call (`f(name=1)`), not inside a string or a comment '''
    if not mapping or not text:
        return text
    edits = []
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except (tokenize.TokenError, IndentationError):
        return text
    depth = 0
    for i, t in enumerate(tokens):
        if t.type == tokenize.OP and t.string in '([{':
            depth += 1
        elif t.type == tokenize.OP and t.string in ')]}':
            depth -= 1
        if t.type != tokenize.NAME or t.string not in mapping or keyword.iskeyword(t.string):
            continue
        before = next((u for u in reversed(tokens[:i]) if u.type not in (tokenize.NL, tokenize.NEWLINE, tokenize.INDENT, tokenize.DEDENT, tokenize.COMMENT)), None)
        after = next((u for u in tokens[i + 1:] if u.type not in (tokenize.NL, tokenize.NEWLINE, tokenize.COMMENT)), None)
        if before is not None and before.type == tokenize.OP and before.string == '.':
            continue
        if depth > 0 and after is not None and after.type == tokenize.OP and after.string == '=' and before is not None and before.type == tokenize.OP and before.string in '(,':
            continue
        edits.append((t.start[0] - 1, t.start[1], t.end[1], mapping[t.string]))
    lines = text.split('\n')
    for row, a, b, new in sorted(edits, reverse=True):
        lines[row] = lines[row][:a] + new + lines[row][b:]
    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# The file of a primitive

def _plain_numbers(text):
    ''' `text` with every var(5) written as 5: what a primitive is made of are the numbers it had, not the draggable numbers of the script it was made in (the model that a script has
        of it gets its own, from its own handles) '''
    return re.sub(r'\bvar\(\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*\)', r'\1', text or '')


_PLACEMENT_DEFAULTS = {'position': (0.0, 0.0, 0.0), 'rotation': (0.0, 0.0, 0.0), 'scale': (1.0, 1.0, 1.0), 'alpha': 1.0}


def _without_default_placement(expr):
    ''' `expr` -- the call that makes a model -- less the placement arguments it carries at their defaults (position=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1), alpha=1: what FielDes writes
        into a call when it makes the model draggable): a primitive is what the model is made of, and the model that a script gets from it is placed by its own.  A placement that is not the
        default stays: it is where the model was put '''
    try:
        call = ast.parse(expr, mode='eval').body
    except SyntaxError:
        return expr
    if not isinstance(call, ast.Call):
        return expr
    lines = expr.split('\n')
    starts = [0]
    for line in lines:
        starts.append(starts[-1] + len(line) + 1)

    def offset(lineno, col):
        text = lines[lineno - 1]
        return starts[lineno - 1] + len(text.encode('utf-8')[:col].decode('utf-8', 'ignore'))

    cuts = []
    for kw in call.keywords:
        default = _PLACEMENT_DEFAULTS.get(kw.arg)
        if default is None:
            continue
        try:
            value = ast.literal_eval(_plain_numbers(ast.get_source_segment(expr, kw.value)))
        except (ValueError, SyntaxError, TypeError):
            continue
        if isinstance(value, (tuple, list)):
            value = tuple(float(v) for v in value)
        elif isinstance(value, (int, float)) and not isinstance(value, bool):
            value = float(value)
        if value == default or (kw.arg == 'scale' and value == 1.0):
            cuts.append((offset(kw.lineno, kw.col_offset), offset(kw.end_lineno, kw.end_col_offset)))
    if not cuts:
        return expr
    out, last = '', 0
    for a, b in cuts:
        while b < len(expr) and expr[b] in ' \t\r\n,':          # (the comma after it, and the room before the next)
            b += 1
        out += expr[last:a]
        last = b
    out += expr[last:]
    if out.endswith(')'):
        body = out[:-1].rstrip()
        out = (body[:-1] if body.endswith(',') else body) + ')'      # (nothing is left after the last argument but the bracket)
    return out


def function_text(name, typ, doc, head, expr):
    ''' The text of the file of a primitive: `#@ primitive: <kind>` and the function `name()` that runs the statements the model is made of and returns it '''
    doc = (doc or '').strip().split('\n', 1)[0] or 'Your own primitive, made from a model of a script (Primitives > Manage primitives).'
    out = ['#@ primitive: %s' % typ, 'def %s():' % name, '    %r' % doc]
    body = _plain_numbers(head)
    if body.strip():
        out.extend(('    ' + line) if line.strip() else '' for line in body.split('\n'))
    out.append('    return ' + _plain_numbers(_without_default_placement(expr)))
    return '\n'.join(out) + '\n'


def _write_text(name, text):
    ''' Writes the file of a primitive, through a temporary file; a text that is not Python is refused '''
    compile(text, name + '.py', 'exec')
    os.makedirs(folder(), exist_ok=True)
    p = _file(name)
    temp = p + '.tmp'
    with open(temp, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    os.replace(temp, p)


def _migrate(old):
    ''' The primitives an older FielDes kept in primitives.json -- {name, type, head, expr, doc} -- as files of their own (one that has a file already is left as it is) '''
    for c in old:
        try:
            name = c['name']
            if not os.path.exists(_file(name)):
                _write_text(name, function_text(name, c.get('type') or 'solid', c.get('doc') or '', c.get('head') or '', c['expr']))
        except (OSError, SyntaxError, ValueError):
            pass


# ---------------------------------------------------------------------------
# What the manager (the Primitives menu) asks of the library

def manage(arg):
    ''' JSON {op, ...} -> JSON.  ops:
          list {type}                    every entry of that kind of model (or of every kind): [{name, label, group, type, on, custom, builtin}]
          set {name, on}                 put a menu entry in the list of Primitives (on) or take it out of it
          add {source, line, end_line, name, type, doc}   the model written there becomes a primitive of the user's own: a file `<name>.py` of the folder of primitives
          remove {name}                  a primitive of the user's own is forgotten (its file is deleted: a script that calls it needs the file)
          rename {name, label}           a primitive of the user's own gets another name (a new one: it is what the menus call it, what its file and its function are called, and what the model
                                         it writes is called after); a shape of the library keeps its function and gets the name the menus show (an empty label: the library's name again)
          kind {type}                    the section of the manager a model of that type of the model tree belongs to: {kind}
        {ok: false, error} where it cannot be done '''
    from fieldes import menu_catalog as M
    r = json.loads(arg)
    op = r.get('op')
    try:
        if op == 'list':
            return json.dumps({'ok': True, 'entries': _entries(M, r.get('type'))})
        if op == 'kind':
            return json.dumps({'ok': True, 'kind': kind_of(r.get('type'))})
        # (a copy: an operation that is refused half way leaves what was read from the file as it is)
        d = copy.deepcopy(load())
        extra = {}                  # (what an operation tells besides the list: the name a rename ended up with)
        if op == 'set':
            name, on = r['name'], bool(r['on'])
            builtin = name in M.BASIC
            d['added'] = [n for n in d['added'] if n != name]
            d['removed'] = [n for n in d['removed'] if n != name]
            if on and not builtin:
                d['added'].append(name)
            elif not on and builtin:
                d['removed'].append(name)
            if find(name):
                raise ValueError('"%s" is a primitive of your own: remove it to take it out' % name)
        elif op == 'add':
            if kind_of(r.get('type')) in ('point', 'symmetry'):
                raise ValueError('a point or a symmetry is a single button in the menus, not a list of shapes: it cannot be made a primitive')
            name = re.sub(r'\W+', '_', str(r.get('name') or '').strip()).strip('_')
            if not name or name[0].isdigit() or keyword.iskeyword(name):
                raise ValueError('a primitive needs a name made of letters, digits and underscores')
            if _taken(M, name):
                raise ValueError('"%s" is the name of something the menus or the library have already: choose another' % name)
            head, main, expr, names = closure(r['source'], int(r['line']), int(r['end_line']))
            _write_text(name, function_text(name, r.get('type') or 'solid', r.get('doc') or 'The model %s of a script, made a primitive of your own' % main, head, expr))
        elif op == 'remove':
            mine = find(r['name'])
            if mine is not None:
                os.remove(mine['file'])
        elif op == 'rename':
            name, label = r['name'], str(r.get('label') or '').strip()
            mine = find(name)
            if mine is not None:
                new = re.sub(r'\W+', '_', label).strip('_')
                if not new or new[0].isdigit() or keyword.iskeyword(new):
                    raise ValueError('a primitive needs a name made of letters, digits and underscores')
                if new != name and _taken(M, new):
                    raise ValueError('"%s" is the name of something the menus or the library have already: choose another' % new)
                if new != name:
                    with open(mine['file'], encoding='utf-8') as f:
                        text = f.read()
                    _write_text(new, renamed(text, {name: new}))
                    os.remove(mine['file'])
                extra['label'] = new.replace('_', ' ')
            else:
                if not any(n == name for n, _g, _t in M.PRIMITIVES):
                    raise ValueError('"%s" is not a primitive' % name)
                if label:
                    d['labels'][name] = label
                else:
                    d['labels'].pop(name, None)
                extra['label'] = label or M.LABELS.get(name, name.replace('_', ' '))
        else:
            raise ValueError('unknown operation %s' % op)
        save(d)
        return json.dumps(dict({'ok': True, 'entries': _entries(M, r.get('type'))}, **extra))
    except Exception as e:
        return json.dumps({'ok': False, 'error': str(e)})


def _taken(M, name):
    ''' Whether `name` is the name of something already: an entry of the menus, a function of the library, a primitive of the user's own '''
    if any(n == name for n, _g, _t in M.PRIMITIVES) or any(o[0] == name for o in M.OPERATIONS) or find(name) is not None:
        return True
    try:
        import fieldes
        return name in set(fieldes.__all__)
    except Exception:
        return False


def _entries(M, typ):
    on = basic(M.BASIC)
    given = labels()
    out = []
    for n, g, _t in M.PRIMITIVES:
        kind = M.GROUP_KINDS.get(g, 'solid')
        if typ and kind != typ:
            continue
        if g in ('Supports and loads', 'Thermal conditions', 'Flow conditions', 'Manufacturing constraints', 'Materials', 'Fluids') or g.endswith('cells'):
            continue
        out.append({'name': n, 'label': given.get(n) or M.LABELS.get(n, n.replace('_', ' ')), 'renamed': n in given, 'group': g, 'type': kind, 'on': n in on, 'custom': False,
                    'builtin': n in M.BASIC})
    for c in catalog_entries():
        # (the section a primitive of the user's own is in is the kind of its type: a lattice or an imported part is among the 3D shapes)
        if typ and kind_of(c['type']) != typ:
            continue
        out.append({'name': c['name'], 'label': c['label'], 'renamed': False, 'group': c['group'], 'type': kind_of(c['type']), 'on': True, 'custom': True, 'builtin': False})
    return out
