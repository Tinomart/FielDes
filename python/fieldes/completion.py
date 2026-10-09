'''
Writing a call: every argument visible, laid out cleanly.

FielDes is a code tool: what it writes for you -- an entry of a right-click menu, a function picked from the completion list -- is
code, and the same code however it was asked for.  This module is that one way.  A call it is given comes back COMPLETE:

    * what the call cannot do without and was not given is a placeholder, `...`, written after the argument's name (`region=...`: the run
      stops there until it is written, and the name says what goes there);
    * every other argument it was not given is written with its default, as a keyword -- so that what exists is in front of you,
      and what is not needed can be read, kept or deleted;
    * the arguments are laid out one to a line when the call does not fit on one (88 columns), as every nested call and list is.

    complete_call('select_surface(part, seed=(0, 0, 0))')
        select_surface(
            part,
            seed=(0, 0, 0),
            angle=15.0,
            mode='flat',
            radius=None,
            resolution=None,
            bounds=None,
        )

`split_call` / `join_call` do the same layout to a call that is already in a script (the editor's right-click menu).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import inspect
import json

WIDTH = 88                  # a call that does not fit within this many columns is laid out one argument to a line
INDENT = 4

# (calls that are numbers or notation more than functions: written as they are, with nothing added)
_PLAIN = {'var', 'point', 'range', 'len', 'min', 'max', 'abs', 'round', 'float', 'int', 'str', 'tuple', 'list', 'dict', 'set'}

_NAMESPACE = {}


def _library():
    ''' The functions a script has: everything `from fieldes import *` brings in, and the custom blocks '''
    if not _NAMESPACE:
        import fieldes
        for n in getattr(fieldes, '__all__', dir(fieldes)):
            if not n.startswith('_'):
                try:
                    _NAMESPACE[n] = getattr(fieldes, n)
                except AttributeError:
                    pass
        try:
            from fieldes import blocks
            _NAMESPACE.update(blocks.namespace())
        except Exception:
            pass
    return _NAMESPACE


def _name_of(value):
    ''' The name a library object (steel, water ...) has in a script, or None '''
    for n, v in _library().items():
        if v is value and not callable(v):
            return n
    return None


def _default_source(value):
    ''' A default as it is written in a script, or None when it is nothing that can be written (the argument is then left out) '''
    if value is None or isinstance(value, (bool, int, str)):
        return repr(value)
    if isinstance(value, float):
        text = repr(value)
        return text
    if isinstance(value, (tuple, list)):
        parts = [_default_source(v) for v in value]
        if any(p is None for p in parts):
            return None
        if isinstance(value, tuple):
            return '(%s%s)' % (', '.join(parts), ',' if len(parts) == 1 else '')
        return '[%s]' % ', '.join(parts)
    return _name_of(value)


def _signature(func):
    try:
        return inspect.signature(func)
    except (TypeError, ValueError):
        return None


def _is_library_call(node):
    return isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in _library() \
        and node.func.id not in _PLAIN and callable(_library()[node.func.id])


def _complete_call_node(call):
    ''' The call with what it was not given added: placeholders for what it needs, defaults for the rest (its arguments' own calls
        completed the same way) '''
    given_pos = [_complete(a) for a in call.args]
    given_kw = [ast.keyword(arg=k.arg, value=_complete(k.value)) for k in call.keywords]
    if not _is_library_call(call):
        return ast.Call(func=call.func, args=given_pos, keywords=given_kw)
    sig = _signature(_library()[call.func.id])
    if sig is None:
        return ast.Call(func=call.func, args=given_pos, keywords=given_kw)
    try:
        bound = sig.bind_partial(*([0] * len(given_pos)), **{k.arg: 0 for k in given_kw if k.arg})
    except TypeError:
        return ast.Call(func=call.func, args=given_pos, keywords=given_kw)       # (it does not fit the function: left as written)
    if any(k.arg is None for k in given_kw):
        return ast.Call(func=call.func, args=given_pos, keywords=given_kw)       # (`**options`: what it holds is not known)
    new_pos = list(given_pos)
    added = {}                                                                    # parameter name -> keyword node
    # (the names a call cannot do without though they have a default -- `_required`, in the order the call takes them after the regions:
    # a function that takes any number of positional arguments has been given the first of them already when it was given one)
    needs = tuple(getattr(_library()[call.func.id], '_required', ()))
    positional_names = tuple(getattr(_library()[call.func.id], '_positional', needs))
    taken = set(positional_names[:len(given_pos)]) if any(p.kind == p.VAR_POSITIONAL for p in sig.parameters.values()) else set()
    for p in sig.parameters.values():
        if p.kind == p.VAR_KEYWORD or p.name.startswith('_') or p.name == 'self':
            continue
        if p.kind == p.VAR_POSITIONAL:
            if not given_pos and not needs and p.name not in bound.arguments:
                new_pos.append(ast.Constant(value=Ellipsis))                      # (at least one: the placeholder says so)
            continue
        if p.name in bound.arguments or p.name in taken:
            continue
        if p.default is p.empty or p.name in needs:
            hole = ast.Constant(value=Ellipsis)
            if p.kind == p.POSITIONAL_ONLY:
                new_pos.append(hole)
            else:
                added[p.name] = ast.keyword(arg=p.name, value=hole)               # (the name says what goes there)
        else:
            text = _default_source(p.default)
            if text is None:
                continue
            added[p.name] = ast.keyword(arg=p.name, value=ast.parse(text, mode='eval').body)
    # (the keywords in the order the function lists them: the ones that were given and the ones that were added)
    order = [p.name for p in sig.parameters.values()]
    keywords = given_kw + list(added.values())
    keywords.sort(key=lambda k: order.index(k.arg) if k.arg in order else len(order))
    return ast.Call(func=call.func, args=new_pos, keywords=keywords)


def _complete(node):
    if isinstance(node, ast.Call):
        return _complete_call_node(node)
    if isinstance(node, ast.List):
        return ast.List(elts=[_complete(e) for e in node.elts], ctx=ast.Load())
    if isinstance(node, ast.Tuple):
        return ast.Tuple(elts=[_complete(e) for e in node.elts], ctx=ast.Load())
    return node


# ---------------------------------------------------------------------------------------------------------------------------------
# Layout

def _inline(node):
    return ast.unparse(node)


def _render(node, col, indent, force=False):
    ''' `node` as text that starts at column `col` of a line, its lines after the first indented to `indent`: on one line when it fits
        (and is not asked to be split), else one element to a line.  `force` splits this one even if it fits (not what is inside) '''
    if isinstance(node, (ast.Call, ast.List, ast.Tuple)):
        one = _inline(node)
        if not force and col + len(one) <= WIDTH:
            return one
        if isinstance(node, ast.Call):
            opening, closing = _inline(node.func) + '(', ')'
            items = [(None, a) for a in node.args] + [(k.arg, k.value) for k in node.keywords]
        elif isinstance(node, ast.List):
            opening, closing, items = '[', ']', [(None, e) for e in node.elts]
        else:
            if not node.elts or len(node.elts) <= 3 and col + len(one) <= WIDTH + 12 and not force:
                return one                           # (a position or a size is kept together)
            opening, closing, items = '(', ')', [(None, e) for e in node.elts]
        if not items:
            return one
        pad = ' ' * (indent + INDENT)
        lines = [opening]
        for name, value in items:
            prefix = '' if name is None else name + '='
            lines.append(pad + prefix + _render(value, indent + INDENT + len(prefix), indent + INDENT) + ',')
        lines.append(' ' * indent + closing)
        return '\n'.join(lines)
    return _inline(node)


def complete_call(text, column=0, indent=0):
    ''' The expression `text` (a call, as a menu entry or a completion writes it) complete -- see the module -- as text that starts at
        column `column` of a line whose statement is indented by `indent`.  What cannot be read is given back as it was '''
    try:
        node = ast.parse(text.strip(), mode='eval').body
    except SyntaxError:
        return text
    return _render(_complete(node), column, indent)


def split_call(text, column=0, indent=0):
    ''' The call `text` with its arguments one to a line (what it needs and does not have is not added) '''
    try:
        node = ast.parse(text.strip(), mode='eval').body
    except SyntaxError:
        return text
    return _render(node, column, indent, force=True)


def join_call(text):
    ''' The call `text` on one line, if it fits; as it is, if it would not '''
    try:
        node = ast.parse(text.strip(), mode='eval').body
    except SyntaxError:
        return text
    one = _inline(node)
    return one


# ---------------------------------------------------------------------------------------------------------------------------------
# What a menu does not make up

def _made_up(value):
    ''' Whether an argument is a value a menu made up: a number, a text, or a tuple or list of them.  (A model, a name, a call of the library
        is a choice of what the call is made of: it stays) '''
    if isinstance(value, ast.Constant):
        return value.value is not Ellipsis
    if isinstance(value, ast.UnaryOp) and isinstance(value.op, (ast.USub, ast.UAdd)):
        return _made_up(value.operand)
    if isinstance(value, (ast.Tuple, ast.List)):
        return bool(value.elts) and all(_made_up(e) for e in value.elts)
    return False


def _unmade(node, structure=False):
    ''' `node` without the values a menu made up; with `structure`, without the calls and lists it made up too (what an entry that was given no
        model to build from has written from its template: the arguments of the call it is made of are for the user to say) '''
    def gone(value):
        return _made_up(value) or (structure and isinstance(value, (ast.Call, ast.List, ast.Tuple)))

    if isinstance(node, ast.List):
        return ast.List(elts=[_unmade(e, structure) for e in node.elts], ctx=ast.Load())
    if isinstance(node, ast.Tuple):
        return ast.Tuple(elts=[_unmade(e, structure) for e in node.elts], ctx=ast.Load())
    if not isinstance(node, ast.Call):
        return node
    args = [_unmade(a, structure) for a in node.args]
    keywords = [ast.keyword(arg=k.arg, value=_unmade(k.value, structure)) for k in node.keywords]
    if not _is_library_call(node):
        return ast.Call(func=node.func, args=args, keywords=keywords)
    sig = _signature(_library()[node.func.id])
    if sig is None or any(k.arg is None for k in keywords):
        return ast.Call(func=node.func, args=args, keywords=keywords)
    params = [p for p in sig.parameters.values() if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD)]
    if any(p.kind == p.VAR_POSITIONAL for p in sig.parameters.values()) or len(args) > len(params):
        # (a call that takes any number of positional arguments: what is given there is models, left as it is)
        return ast.Call(func=node.func, args=args, keywords=[k for k in keywords if not gone(k.value)])
    kept_pos, moved = [], []
    dropped = False
    for i, value in enumerate(args):
        p = params[i]
        if gone(value) and p.kind != p.POSITIONAL_ONLY:
            dropped = True                             # (the arguments after it can no longer be told by their place: they are named)
        elif dropped and p.kind != p.POSITIONAL_ONLY:
            moved.append(ast.keyword(arg=p.name, value=value))
        else:
            kept_pos.append(value)
    # (a made-up value of a positional-only parameter stays: it cannot be named)
    return ast.Call(func=node.func, args=kept_pos, keywords=moved + [k for k in keywords if not gone(k.value)])


def without_made_up_values(text, structure=False):
    ''' The call `text` as a menu wrote it, without any value the menu made up -- a number, a text, a tuple of them.  What the call cannot do
        without is then a placeholder (`vector=...`) and every other argument has the library's own default, as `complete_call` writes them:
        the menu lays out what the call is made of (the models, the calls inside), and the numbers are for the user to say, in the script '''
    try:
        node = ast.parse(text.strip(), mode='eval').body
    except SyntaxError:
        return text
    if not isinstance(node, ast.Call):
        return text
    return ast.unparse(_unmade(node, structure))


# ---------------------------------------------------------------------------------------------------------------------------------
# What the application asks (see app_support)

def complete_for_editor(arg):
    ''' {name, column, indent} -> the call of the function `name` as the completion writes it, complete and laid out for a line whose text
        before the call is `column` wide and whose statement is indented by `indent`.  Empty when `name` is no function of the library '''
    r = json.loads(arg)
    name = r['name']
    lib = _library()
    if name not in lib or not callable(lib[name]) or name in _PLAIN:
        return ''
    return complete_call(name + '()', int(r.get('column', 0)), int(r.get('indent', 0)))


def parameter_at(function, index):
    ''' The name of the parameter that the argument number `index` (0-based, positional) of a call of `function` is, or '' '''
    func = _library().get(function)
    sig = _signature(func) if callable(func) else None
    if sig is None:
        return ''
    k = 0
    for p in sig.parameters.values():
        if p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD):
            if k == index:
                return p.name
            k += 1
        elif p.kind == p.VAR_POSITIONAL:
            return p.name
    return ''


def _param_docs(func):
    ''' {parameter name: what the docstring says of it} -- the lines of the docstring that start with a name and then at least two spaces
        (or a colon), the lines under them that are indented further being part of it '''
    import re
    doc = inspect.getdoc(func) or ''
    out = {}
    current = None
    head = re.compile(r'^(\w+(?:\s*,\s*\w+)*)(?:\s{2,}|\s*:\s+)(\S.*)$')
    for line in doc.split('\n'):
        m = head.match(line)
        if m and not line.startswith(' '):
            names = [n.strip() for n in m.group(1).split(',')]
            for n in names:
                out[n] = m.group(2).strip()
            current = names
        elif line.startswith('  ') and current and line.strip():
            for n in current:
                out[n] = out[n] + ' ' + line.strip()
        else:
            current = None
    return out


def argument_doc(arg):
    ''' {source, line, col} (a 0-based position in the script) -> what the documentation says of the argument whose name is there
        (`seed` in `select_surface(part, seed=...)`), as JSON {function, name, default, text}; empty when the position is no argument name
        of a call of the library '''
    r = json.loads(arg)
    try:
        tree = ast.parse(r['source'])
    except SyntaxError:
        return ''
    line, col = int(r['line']) + 1, int(r['col'])
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Name) or node.func.id not in _library():
            continue
        func = _library()[node.func.id]
        for k in node.keywords:
            if k.arg is None or not hasattr(k.value, 'lineno'):
                continue
            # (the name sits just before its `=`: the position of the keyword's value, less the name and the `=` and any space)
            end = (k.value.lineno, k.value.col_offset)
            lines = r['source'].split('\n')
            before = lines[end[0] - 1][:end[1]]
            stripped = before.rstrip()
            if stripped.endswith('='):
                stripped = stripped[:-1].rstrip()
            if not stripped.endswith(k.arg):
                continue
            start = len(stripped) - len(k.arg)
            if end[0] == line and start <= col <= start + len(k.arg):
                sig = _signature(func)
                default = ''
                if sig is not None and k.arg in sig.parameters and sig.parameters[k.arg].default is not inspect.Parameter.empty:
                    default = _default_source(sig.parameters[k.arg].default) or ''
                text = _param_docs(func).get(k.arg, '')
                if not text:
                    first = (inspect.getdoc(func) or '').split('\n\n')[0].replace('\n', ' ').strip()
                    text = first
                return json.dumps({'function': node.func.id, 'name': k.arg, 'default': default, 'text': text})
    return ''


def reformat_in_source(arg):
    ''' {source, line, col, mode: 'split' | 'join'} (a 0-based position in the script) -> {start: [line, col], end: [line, col], text} -- the
        innermost call that holds the position, laid out again -- or '' when there is none, or it holds a comment '''
    r = json.loads(arg)
    source = r['source']
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return ''
    line, col = int(r['line']) + 1, int(r['col'])
    best = None
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or not hasattr(node, 'end_lineno'):
            continue
        if (node.lineno, node.col_offset) <= (line, col) <= (node.end_lineno, node.end_col_offset):
            if best is None or (node.lineno, node.col_offset) >= (best.lineno, best.col_offset):
                best = node
    if best is None:
        return ''
    lines = source.split('\n')
    segment = ast.get_source_segment(source, best)
    if segment is None or '#' in segment:
        return ''
    first_line = lines[best.lineno - 1]
    indent = len(first_line) - len(first_line.lstrip())
    column = best.col_offset
    if r.get('mode') == 'join':
        text = join_call(segment)
    else:
        text = split_call(segment, column, indent)
    if text == segment:
        return ''
    return json.dumps({'start': [best.lineno - 1, best.col_offset], 'end': [best.end_lineno - 1, best.end_col_offset], 'text': text})
