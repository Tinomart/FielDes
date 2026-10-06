# Nesting, renesting and denesting are edits of a call's arguments: what app_support.arg_edits writes
import json
import sys

from fieldes import *
from fieldes import app_support

failures = []


def check(label, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail != '' else ''))
    if not ok:
        failures.append(label)


def edit(source, var, line, remove=(), insert=()):
    request = {'source': source, 'statements': [{'var': var, 'line': line, 'remove': list(remove), 'insert': list(insert)}]}
    answer = json.loads(app_support.arg_edits(json.dumps(request)))['statements'][0]
    lines = source.split('\n')
    lines[answer['line']:answer['end_line'] + 1] = answer['text'].split('\n')
    return '\n'.join(lines)


def refused(source, var, line, **kw):
    try:
        edit(source, var, line, **kw)
        return None
    except ValueError as e:
        return str(e)


def ins(name, side, relative=None):
    return {'name': name, 'side': side, 'relative': relative}


src = 'from fieldes import *\nu = union(a, b, c)\n'
head = 'from fieldes import *\n'
check('the first model is taken out', edit(src, 'u', 2, remove=['a']) == head + 'u = union(b, c)\n')
check('a middle one', edit(src, 'u', 2, remove=['b']) == head + 'u = union(a, c)\n')
check('the last one', edit(src, 'u', 2, remove=['c']) == head + 'u = union(a, b)\n')
check('two at once', edit(src, 'u', 2, remove=['a', 'c']) == head + 'u = union(b)\n')
check('the only one cannot be', 'nothing else' in (refused('u = union(a)\n', 'u', 1, remove=['a']) or ''))
check('a name that is not an argument is refused', 'not one of the models' in (refused(src, 'u', 2, remove=['zzz']) or ''))

check('one is put before another', edit(src, 'u', 2, insert=[ins('x', 'before', 'b')]) == head + 'u = union(a, x, b, c)\n')
check('after another', edit(src, 'u', 2, insert=[ins('x', 'after', 'b')]) == head + 'u = union(a, b, x, c)\n')
check('at the end', edit(src, 'u', 2, insert=[ins('x', 'end')]) == head + 'u = union(a, b, c, x)\n')
check('before the first', edit(src, 'u', 2, insert=[ins('x', 'before', 'a')]) == head + 'u = union(x, a, b, c)\n')

check('a model is moved: c to the front', edit(src, 'u', 2, remove=['c'], insert=[ins('c', 'before', 'a')]) == head + 'u = union(c, a, b)\n')
check('a to the end', edit(src, 'u', 2, remove=['a'], insert=[ins('a', 'end')]) == head + 'u = union(b, c, a)\n')
check('b after c', edit(src, 'u', 2, remove=['b'], insert=[ins('b', 'after', 'c')]) == head + 'u = union(a, c, b)\n')
check('a before b (where it is): nothing changes', edit(src, 'u', 2, remove=['a'], insert=[ins('a', 'before', 'b')]) == src)
check('a after b', edit(src, 'u', 2, remove=['a'], insert=[ins('a', 'after', 'b')]) == head + 'u = union(b, a, c)\n')

# keyword arguments stay, and what is put in goes before them
kw = 'm = blend(a, b, 0.5, k=c)\n'
check('a model is put in front of the keywords', edit(kw, 'm', 1, insert=[ins('x', 'after', 'b')]) == 'm = blend(a, b, x, 0.5, k=c)\n')
check('the last positional model is taken out, the keyword stays', edit('m = blend(a, b, k=c)\n', 'm', 1, remove=['b']) == 'm = blend(a, k=c)\n')
check('a keyword model cannot be taken out (the call is given it by name)',
      'not one of the models' in (refused('m = blend(a, b, k=c)\n', 'm', 1, remove=['c']) or ''))

# a list written in the call
lst = 'u = union_all([a, b, c])\n'
check('out of a list', edit(lst, 'u', 1, remove=['b']) == 'u = union_all([a, c])\n')
check('into a list, at the end', edit(lst, 'u', 1, insert=[ins('x', 'end')]) == 'u = union_all([a, b, c, x])\n')

# a call on several lines
many = 'u = union(\n    a,\n    b,\n    c,\n)\nprint(u)\n'
got = edit(many, 'u', 1, remove=['c'])
check('one model per line: the last is taken out', got == 'u = union(\n    a,\n    b,\n)\nprint(u)\n', repr(got))
got = edit(many, 'u', 1, remove=['a'])
check('the first', got == 'u = union(\n    b,\n    c,\n)\nprint(u)\n', repr(got))
got = edit(many, 'u', 1, insert=[ins('x', 'after', 'a')])
check('a model is put in', got == 'u = union(\n    a, x,\n    b,\n    c,\n)\nprint(u)\n', repr(got))
two = 'u = union(a,\n          b)\n'
got = edit(two, 'u', 1, remove=['b'])
check('a call on two lines loses its last model', got == 'u = union(a)\n', repr(got))

# it is a call, and the statement is found by its line and name
check('not a call', 'not written as a call' in (refused('x = a + b\n', 'x', 1, remove=['a']) or ''))
check('two statements on one line: the right one', edit('p = f(a); q = g(a, b)\n', 'q', 1, remove=['a']) == 'p = f(a); q = g(b)\n')
check('the wrong variable', 'not written as a call' in (refused(src, 'nope', 2, remove=['a']) or ''))

# columns are characters, not bytes
uni = "café = union(a, b)  # é\nu = union(été, b)\n"
got = edit(uni, 'u', 2, remove=['b'])
check('non-ASCII text on the line', got == "café = union(a, b)  # é\nu = union(été)\n", repr(got))

# several statements in one request
request = {'source': 'u = union(a, b)\nv = union(c, d)\n', 'statements': [
    {'var': 'u', 'line': 1, 'remove': ['a'], 'insert': []},
    {'var': 'v', 'line': 2, 'remove': [], 'insert': [ins('a', 'end')]}]}
answer = json.loads(app_support.arg_edits(json.dumps(request)))['statements']
check('two statements', [x['text'] for x in answer] == ['u = union(b)', 'v = union(c, d, a)'], answer)

print('FAILED: ' + ', '.join(failures) if failures else 'ALL OK')
sys.exit(1 if failures else 0)
