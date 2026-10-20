'''
Your own primitives: which shapes the Primitives menu offers, and shapes of your own that it offers too.

The menus show a short list of PRIMITIVES (the most basic shape of each kind: a box, a circle ...) apart from the Parametric shapes (a rounded box, a ring, a profile ...).  What is in that list
is yours to change, from the Primitives menu of the menu bar:

    add         a Parametric shape (or any entry of the menus) to the list of Primitives
    remove      a primitive from the list: it is then offered with the Parametric shapes
    add selection   the model that is selected in the model tree -- with every statement of the script that it is made of -- becomes a primitive of your own, under the name you give it:
                    picking it from the menu writes it into the script again, as a model of its own, with names that do not collide with those of the script

What you choose is kept in ONE file of your own, `primitives.json`, in the folder that is yours -- `%APPDATA%\\FielDes` on Windows, `~/.local/share/FielDes` on Linux (the application sets
FIELDES_USER_DIR) --, and is there in every script you open, whether or not the script you made a primitive in still exists.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import io
import json
import keyword
import os
import re
import sys
import tokenize

# What the menus call each kind of model: the group of the Primitives menu a primitive of that kind is in
GROUP_OF_TYPE = {'solid': '3D', 'profile': '2D', 'curve': '1D', 'point': 'Points', 'surface': 'Surfaces', 'field': 'Fields'}

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


def _empty():
    return {'added': [], 'removed': [], 'custom': []}


def load():
    ''' What the user chose: {added: [names], removed: [names], custom: [{name, type, head, expr, names, main, doc}]}.  A file that is not there, or cannot be read, is no choices '''
    p = path()
    try:
        stamp = (os.path.getmtime(p), os.path.getsize(p))
    except OSError:
        _CACHE['stamp'], _CACHE['data'] = None, None
        return _empty()
    if _CACHE['stamp'] == stamp and _CACHE['data'] is not None:
        return _CACHE['data']
    out = _empty()
    try:
        with open(p, encoding='utf-8') as f:
            raw = json.load(f)
        if isinstance(raw, dict):
            out['added'] = [str(n) for n in raw.get('added', []) if isinstance(n, str)]
            out['removed'] = [str(n) for n in raw.get('removed', []) if isinstance(n, str)]
            out['custom'] = [c for c in raw.get('custom', []) if isinstance(c, dict) and isinstance(c.get('name'), str) and isinstance(c.get('expr'), str)]
    except (OSError, ValueError):
        out = _empty()
    _CACHE['stamp'], _CACHE['data'] = stamp, out
    return out


def save(data):
    ''' Writes the file whole, through a temporary file, so that a program that stops half way leaves the old one '''
    p = path()
    os.makedirs(os.path.dirname(p), exist_ok=True)
    temp = p + '.tmp'
    with open(temp, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
    os.replace(temp, p)
    _CACHE['stamp'], _CACHE['data'] = None, None


def basic(builtin):
    ''' The names the Primitives menu shows: the built-in list (`builtin`) less what was taken out, and what was put in '''
    d = load()
    return (set(builtin) - set(d['removed'])) | set(d['added'])


def find(name):
    for c in load()['custom']:
        if c['name'] == name:
            return c
    return None


def catalog_entries():
    ''' The primitives of the user's own, as entries of the menu catalog '''
    out = []
    for c in load()['custom']:
        typ = c.get('type') or 'solid'
        out.append({'name': c['name'], 'group': GROUP_OF_TYPE.get(typ, 'Custom blocks'), 'type': typ, 'with_bodies': False, 'primitive': True, 'custom': True,
                    'doc': c.get('doc') or '', 'label': c['name'].replace('_', ' ')})
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


def _identifiers(source):
    ''' Every name that is written in the script: what a made-up name must not be '''
    try:
        return {t.string for t in tokenize.generate_tokens(io.StringIO(source).readline) if t.type == tokenize.NAME}
    except (tokenize.TokenError, IndentationError):
        return set(re.findall(r'[A-Za-z_]\w*', source))


def insert_text(custom, var, source):
    ''' What picking the primitive writes: its statements and then its model, `var` the name of the model.  Every name the statements make is kept unless the script has it already --
        then it gets a number, so that a primitive added twice, or into a script that has a `size` of its own, never overwrites anything '''
    taken = _identifiers(source or '') | {var}
    mapping = {custom['main']: var}
    for n in custom.get('names', []):
        if n == custom['main']:
            continue
        if n in taken:
            k = 2
            while '%s_%d' % (n, k) in taken:
                k += 1
            mapping[n] = '%s_%d' % (n, k)
            taken.add(mapping[n])
        else:
            taken.add(n)
    head = renamed(custom.get('head', ''), mapping)
    expr = renamed(custom['expr'], mapping)
    from fieldes.menu_catalog import PRELUDE
    return (head + PRELUDE + expr) if head.strip() else expr


# ---------------------------------------------------------------------------
# What the manager (the Primitives menu) asks of the library

def manage(arg):
    ''' JSON {op, ...} -> JSON.  ops:
          list {type}                    every entry of that kind of model (or of every kind): [{name, label, group, type, on, custom, builtin}]
          set {name, on}                 put a menu entry in the list of Primitives (on) or take it out of it
          add {source, line, end_line, name, type, doc}   the model written there becomes a primitive of the user's own
          remove {name}                  a primitive of the user's own is forgotten
        {ok: false, error} where it cannot be done '''
    from fieldes import menu_catalog as M
    r = json.loads(arg)
    op = r.get('op')
    try:
        if op == 'list':
            return json.dumps({'ok': True, 'entries': _entries(M, r.get('type'))})
        d = load()
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
            name = re.sub(r'\W+', '_', str(r.get('name') or '').strip()).strip('_')
            if not name or name[0].isdigit() or keyword.iskeyword(name):
                raise ValueError('a primitive needs a name made of letters, digits and underscores')
            if any(n == name for n, _g, _t in M.PRIMITIVES) or any(o[0] == name for o in M.OPERATIONS) or find(name):
                raise ValueError('"%s" is the name of something the menus have already: choose another' % name)
            head, main, expr, names = closure(r['source'], int(r['line']), int(r['end_line']))
            typ = r.get('type') or 'solid'
            d['custom'].append({'name': name, 'type': typ, 'head': head, 'main': main, 'expr': expr, 'names': names, 'doc': r.get('doc') or ''})
        elif op == 'remove':
            d['custom'] = [c for c in d['custom'] if c['name'] != r['name']]
        else:
            raise ValueError('unknown operation %s' % op)
        save(d)
        return json.dumps({'ok': True, 'entries': _entries(M, r.get('type'))})
    except Exception as e:
        return json.dumps({'ok': False, 'error': str(e)})


def _entries(M, typ):
    on = basic(M.BASIC)
    out = []
    for n, g, _t in M.PRIMITIVES:
        kind = M.GROUP_KINDS.get(g, 'solid')
        if typ and kind != typ:
            continue
        if g in ('Supports and loads', 'Thermal conditions', 'Flow conditions', 'Manufacturing constraints', 'Materials', 'Fluids') or g.endswith('cells'):
            continue
        out.append({'name': n, 'label': M.LABELS.get(n, n.replace('_', ' ')), 'group': g, 'type': kind, 'on': n in on, 'custom': False, 'builtin': n in M.BASIC})
    for c in catalog_entries():
        if typ and c['type'] != typ:
            continue
        out.append({'name': c['name'], 'label': c['label'], 'group': c['group'], 'type': c['type'], 'on': True, 'custom': True, 'builtin': False})
    return out
