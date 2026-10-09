#!/usr/bin/env python
'''
The translations of FielDes: which texts there are, and what each language lacks.

    python scripts/translations.py list              every text of the program that is translated (one JSON object, English -> English)
    python scripts/translations.py check [code ...]  what each translations/<code>.json lacks, has too much of, or has with other places
    python scripts/translations.py merge <file.json> <code>   puts the translations of a file ({"English": "text"}) into translations/<code>.json

The program's own words are T("English text") in app/src/*.cpp and tr('English text') in python/fieldes (see app/include/fieldes/i18n.hpp):
the English text is the key, a text that has no translation reads in English.  Whatever is not fixed in a text is %1, %2 (C++) or %s
(Python), and a translation must have the same ones -- another language puts them in another order, which %1 and %2 allow.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANGUAGES = ['es', 'fr', 'de', 'ja', 'zh', 'ru']


def decode_literal(body):
    ''' The text a C++ string literal's body stands for (its escapes made characters) '''
    out = []
    i = 0
    raw = bytearray()

    def flush():
        if raw:
            out.append(bytes(raw).decode('utf-8', 'replace'))
            raw.clear()

    while i < len(body):
        c = body[i]
        if c != '\\':
            flush()
            out.append(c)
            i += 1
            continue
        n = body[i + 1] if i + 1 < len(body) else ''
        simple = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', "'": "'", '\\': '\\', '?': '?', 'a': '\a', 'b': '\b', 'f': '\f', 'v': '\v'}
        if n in simple:
            flush()
            out.append(simple[n])
            i += 2
        elif n == 'x':
            m = re.match(r'[0-9a-fA-F]+', body[i + 2:])
            raw.append(int(m.group(0), 16) & 0xff)
            i += 2 + len(m.group(0))
        elif n in '01234567':
            m = re.match(r'[0-7]{1,3}', body[i + 1:])
            raw.append(int(m.group(0), 8) & 0xff)
            i += 1 + len(m.group(0))
        elif n == 'u':
            flush()
            out.append(chr(int(body[i + 2:i + 6], 16)))
            i += 6
        elif n == 'U':
            flush()
            out.append(chr(int(body[i + 2:i + 10], 16)))
            i += 10
        else:
            flush()
            out.append(n)
            i += 2
    flush()
    return ''.join(out)


def cpp_strings(text):
    ''' (start, end, body) of every string literal of C++ source text, adjacent ones joined; comments and character literals skipped '''
    i, n = 0, len(text)
    found = []
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '/' and text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            i = j + 1
        elif c == 'R' and text.startswith('R"', i):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if m:
                end = text.find(')' + m.group(1) + '"', i)
                i = end + len(m.group(1)) + 2
            else:
                i += 1
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            body = text[i + 1:j]
            if found and not text[found[-1][1]:i].strip():
                s, _, b = found[-1]
                found[-1] = (s, j + 1, b + body)
            else:
                found.append((i, j + 1, body))
            i = j + 1
        else:
            i += 1
    return found


def cpp_keys():
    keys = []
    src = os.path.join(ROOT, 'app', 'src')
    paths = []
    for base, _, files in os.walk(src):
        paths += [os.path.join(base, name) for name in files if name.endswith('.cpp')]
    for path in sorted(paths):
        text = io.open(path, encoding='utf-8', newline='').read().replace('\r\n', '\n')
        for s, e, body in cpp_strings(text):
            before = text[max(0, s - 12):s].rstrip()
            if re.search(r'(?<![A-Za-z0-9_])T\($', before):
                keys.append(decode_literal(body))
    return keys


def python_keys():
    keys = []
    pkg = os.path.join(ROOT, 'python', 'fieldes')
    for base, _, files in os.walk(pkg):
        for name in sorted(files):
            if not name.endswith('.py'):
                continue
            try:
                tree = ast.parse(io.open(os.path.join(base, name), encoding='utf-8').read())
            except SyntaxError:
                continue
            for node in ast.walk(tree):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in ('tr', '_tr') and node.args:
                    a = node.args[0]
                    if isinstance(a, ast.Constant) and isinstance(a.value, str):
                        keys.append(a.value)
    # the labels of the tables the menus are made from (they are texts of the program too, said when the menu is made)
    sys.path.insert(0, os.path.join(ROOT, 'python'))
    try:
        from fieldes import menu_catalog as mc
        for _, g, _ in mc.PRIMITIVES:
            keys.append(g)
        for _, g, _, _ in mc.OPERATIONS:
            keys.append(g)
        from fieldes import kinds
        for k in kinds.describe().values() if isinstance(kinds.describe(), dict) else kinds.describe():
            label = k.get('label') if isinstance(k, dict) else None
            if label:
                keys.append(label)
    except Exception as e:                              # (the tables are for the menus: not having them is not an error here)
        sys.stderr.write('translations: the menu tables could not be read (%s)\n' % e)
    return keys


# texts that are shown through T(variable) -- a word of the program's own that is chosen in code
EXTRA = ['click', 'never', 'always', 'material', 'fluid', 'conditions', 'supports', 'loads', 'boundary', 'support', 'load', 'condition',
         'File', 'Edit', 'View', 'Settings', 'Help', 'Navigation', 'Editor', 'Tree', 'Model', 'Viewport', 'Section', 'Import', 'Export',
         'Window', 'Selection', 'Boundary conditions',
         # (the words Qt itself shows: the buttons of a question, the menu of a text field)
         'qt:QPlatformTheme|OK', 'qt:QPlatformTheme|Cancel', 'qt:QPlatformTheme|&Yes', 'qt:QPlatformTheme|&No', 'qt:QPlatformTheme|Yes to &All',
         'qt:QPlatformTheme|N&o to All', 'qt:QPlatformTheme|Save', 'qt:QPlatformTheme|Save All', 'qt:QPlatformTheme|Open', 'qt:QPlatformTheme|Close',
         'qt:QPlatformTheme|Apply', 'qt:QPlatformTheme|Reset', 'qt:QPlatformTheme|Help', 'qt:QPlatformTheme|Discard', "qt:QPlatformTheme|Don't Save",
         'qt:QPlatformTheme|Abort', 'qt:QPlatformTheme|Retry', 'qt:QPlatformTheme|Ignore', 'qt:QPlatformTheme|Restore Defaults',
         'qt:QMessageBox|Show Details...', 'qt:QMessageBox|Hide Details...', 'qt:QMessageBox|OK',
         'qt:QLineEdit|&Undo', 'qt:QLineEdit|&Redo', 'qt:QLineEdit|Cu&t', 'qt:QLineEdit|&Copy', 'qt:QLineEdit|&Paste', 'qt:QLineEdit|Delete',
         'qt:QLineEdit|Select All',
         'qt:QWidgetTextControl|&Undo', 'qt:QWidgetTextControl|&Redo', 'qt:QWidgetTextControl|Cu&t', 'qt:QWidgetTextControl|&Copy',
         'qt:QWidgetTextControl|&Paste', 'qt:QWidgetTextControl|Delete', 'qt:QWidgetTextControl|Select All',
         'qt:QDialogButtonBox|OK', 'qt:QDialogButtonBox|Cancel', 'qt:QDialogButtonBox|Close']


def all_keys():
    seen = {}
    for k in cpp_keys() + python_keys() + EXTRA:
        if k and k not in seen:
            seen[k] = True
    return list(seen)


PLACEHOLDER = re.compile(r'%(?:[1-9][0-9]?|s|d|\([a-z_]+\)s|[0-9]*\.[0-9]+[gf])')
TAG = re.compile(r'</?[a-zA-Z][^>]*>')


def problems(key, text):
    out = []
    if set(PLACEHOLDER.findall(key)) != set(PLACEHOLDER.findall(text)):         # (a place may be used more than once, or not at all in a text that does not need it twice)
        out.append('places differ: %s vs %s' % (sorted(set(PLACEHOLDER.findall(key))), sorted(set(PLACEHOLDER.findall(text)))))
    # (%s, %d and %.4g are filled in in the order of the English text, where %1, %2 can be put in any order)
    positional = lambda t: [m for m in PLACEHOLDER.findall(t) if not re.match(r'%[1-9]', m)]
    if positional(key) != positional(text) and set(positional(key)) == set(positional(text)) and len(positional(key)) == len(positional(text)):
        out.append('the %s / %d / %.4g places are in another order than in the English')
    if sorted(re.sub(r'\s+style=[\'"][^\'"]*[\'"]', '', t) for t in TAG.findall(key)) != sorted(re.sub(r'\s+style=[\'"][^\'"]*[\'"]', '', t) for t in TAG.findall(text)):
        out.append('tags differ')
    if key.count('\n') != text.count('\n'):
        out.append('line breaks differ')
    return out


def folder():
    return os.path.join(ROOT, 'translations')


def load(code):
    path = os.path.join(folder(), code + '.json')
    if not os.path.exists(path):
        return {}
    return json.load(io.open(path, encoding='utf-8'))


def save(code, table):
    os.makedirs(folder(), exist_ok=True)
    with io.open(os.path.join(folder(), code + '.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(table, f, ensure_ascii=False, indent=1, sort_keys=False)
        f.write('\n')


def main(argv):
    cmd = argv[1] if len(argv) > 1 else 'check'
    keys = all_keys()
    if cmd == 'list':
        print(json.dumps({k: k for k in keys}, ensure_ascii=False, indent=1))
        return 0
    if cmd == 'merge':
        given = json.load(io.open(argv[2], encoding='utf-8'))
        code = argv[3]
        table = load(code)
        bad = 0
        for k, v in given.items():
            p = problems(k, v)
            if p:
                bad += 1
                print('REFUSED %r: %s' % (k[:70], '; '.join(p)))
                continue
            table[k] = v
        # (in the order of the program's texts, then whatever else is there)
        order = {k: i for i, k in enumerate(keys)}
        table = dict(sorted(table.items(), key=lambda kv: order.get(kv[0], len(order))))
        save(code, table)
        print('%s: %d texts (%d refused)' % (code, len(table), bad))
        return 1 if bad else 0
    codes = argv[2:] or LANGUAGES
    status = 0
    for code in codes:
        table = load(code)
        missing = [k for k in keys if k not in table]
        extra = [k for k in table if k not in set(keys)]
        wrong = [(k, p) for k, v in table.items() for p in [problems(k, v)] if p]
        print('%s: %d of %d texts translated; %d missing, %d no longer in the program, %d with other places or tags'
              % (code, len(table) - len(extra), len(keys), len(missing), len(extra), len(wrong)))
        for k in missing[:15]:
            print('   missing: %r' % k[:100])
        for k in extra[:5]:
            print('   unused:  %r' % k[:100])
        for k, p in wrong[:10]:
            print('   wrong:   %r: %s' % (k[:70], '; '.join(p)))
        status = status or (1 if missing or wrong else 0)
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv))
