'''
FielDes apps, the other half: reading the form out of a script, bundling a script into a `.fieldesapp`, opening one, checking the
values that were chosen, and running an app without a window (`FielDes run app.fieldesapp --Radius 5`).  What a script writes to
declare its inputs is in fieldes.app.

A `.fieldesapp` is a zip file:

    manifest.json     the title, the inputs (the form), the version of FielDes that made it, which files came along
    app.py            the script
    blocks/*.py       the custom blocks the script uses (only those)
    files/...         the files the script reads, in the places it reads them from (`app.asset('step/bracket.step')`,
                      "step/bracket.step", a module next to the script): the app runs with `files` as its working folder

It is unpacked into the user's cache folder, once per version of the file, and run from there.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ast
import hashlib
import json
import os
import re
import shutil
import sys
import time
import traceback
import zipfile

from fieldes import app as _app

FORMAT = 1
EXTENSION = '.fieldesapp'
MAX_FILE = 800 * 1024 * 1024        # (one file that comes along: more is surely not meant to be bundled)


# ---------------------------------------------------------------------------------------------------------------------
# The form, read from the text of the script

def _app_call(node):
    ''' The name of `x` in a call `app.x(...)`, else None '''
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name) \
            and node.func.value.id == 'app':
        return node.func.attr
    return None


class _NotPlain(Exception):
    pass


def _plain(node):
    ''' The value of an argument written as a plain value (a number, a text, a list of them, app.asset('x')); raises _NotPlain '''
    kind = _app_call(node)
    if kind == 'asset' and len(node.args) == 1 and not node.keywords:
        inner = _plain(node.args[0])
        if isinstance(inner, str):
            return {'__asset__': inner}
    try:
        return ast.literal_eval(node)
    except (ValueError, SyntaxError, TypeError):
        raise _NotPlain()


def inputs_of(source):
    ''' What the script declares, read from its text (nothing is run):
        {'inputs': [spec ...], 'title', 'description', 'version', 'assets': [path ...], 'problems': [{'line', 'message'}]}
        Each spec is {'kind', 'label', 'key', 'default', ...} (see fieldes.app.spec_of) with 'line' added. '''
    out = {'inputs': [], 'title': '', 'description': '', 'version': '', 'assets': [], 'problems': [], 'viewport': None}
    try:
        tree = ast.parse(source)
    except SyntaxError as e:
        out['problems'].append({'line': e.lineno or 1, 'message': 'the script has a syntax error (%s)' % e.msg})
        return out
    calls = sorted((n for n in ast.walk(tree) if _app_call(n)), key=lambda n: (n.lineno, n.col_offset))
    seen = {}
    for node in calls:
        kind = _app_call(node)
        line = node.lineno
        if kind in _app.SIGNATURES:
            names = _app.SIGNATURES[kind]
            args, kwargs, bad = [], {}, []
            for i, a in enumerate(node.args):
                try:
                    args.append(_plain(a))
                except _NotPlain:
                    args.append(_app.DEFAULTS[kind].get(names[i]) if i else None)
                    bad.append(names[i] if i < len(names) else '#%d' % (i + 1))
            for kw in node.keywords:
                if kw.arg is None:
                    bad.append('**')
                    continue
                try:
                    kwargs[kw.arg] = _plain(kw.value)
                except _NotPlain:
                    bad.append(kw.arg)
            if args and args[0] is None or not args and 'name' not in kwargs:
                out['problems'].append({'line': line, 'message': 'app.%s(): its name is not a plain text, so the form cannot show it' % kind})
                continue
            try:
                spec = _app.spec_of(kind, args, kwargs)
            except TypeError as e:
                out['problems'].append({'line': line, 'message': str(e)})
                continue
            if bad:
                out['problems'].append({'line': line, 'message': 'app.%s(%r): %s %s not a plain value (a number, a text, a list), so the form '
                                        'shows the library\'s default there' % (kind, spec['label'], ', '.join(bad), 'is' if len(bad) == 1 else 'are')})
            if spec['key'] in seen:
                out['problems'].append({'line': line, 'message': 'two inputs are called %r (line %d and line %d): they would be one option'
                                        % (spec['label'], seen[spec['key']], line)})
                continue
            seen[spec['key']] = line
            spec['line'] = line
            out['inputs'].append(spec)
        elif kind == 'title':
            try:
                args = [_plain(a) for a in node.args]
                kwargs = {kw.arg: _plain(kw.value) for kw in node.keywords if kw.arg}
            except _NotPlain:
                out['problems'].append({'line': line, 'message': 'app.title(): the texts are not plain texts'})
                continue
            names = ('text', 'description', 'version')
            given = dict(zip(names, args))
            given.update(kwargs)
            out['title'] = str(given.get('text', '')).strip()
            out['description'] = str(given.get('description', '')).strip()
            out['version'] = str(given.get('version', '')).strip()
        elif kind == 'viewport':
            labels = True
            for kw in node.keywords:
                if kw.arg == 'labels':
                    try:
                        labels = bool(_plain(kw.value))
                    except _NotPlain:
                        pass
            out['viewport'] = {'labels': labels}
        elif kind == 'asset':
            try:
                out['assets'].append(_plain(node.args[0]) if node.args else None)
            except _NotPlain:
                out['problems'].append({'line': line, 'message': 'app.asset(): the path is not a plain text, so the file cannot be '
                                        'bundled: write the path as a text'})
            if out['assets'] and not isinstance(out['assets'][-1], str):
                out['assets'].pop()
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Bundling

def _slug(text):
    s = re.sub(r'[^\w]+', '-', str(text).strip().casefold(), flags=re.UNICODE).strip('-')
    return s or 'app'


def _imports_of(source):
    ''' The modules a script imports (top-level names) '''
    names = set()
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return names
    for n in ast.walk(tree):
        if isinstance(n, ast.Import):
            names.update(a.name.split('.')[0] for a in n.names)
        elif isinstance(n, ast.ImportFrom) and n.module and not n.level:
            names.add(n.module.split('.')[0])
    return names


def _used_names(source):
    ''' The names a script calls (`rib(base, 3)`): a custom block is used when it is called; a variable that has a block's name is not a use '''
    try:
        return {n.func.id for n in ast.walk(ast.parse(source)) if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)}
    except SyntaxError:
        return set()


def _literals_that_are_files(source, script_dir):
    ''' The relative paths the script writes as texts that name a file next to it (read as "step/bracket.step") '''
    found = []
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return found
    for n in ast.walk(tree):
        if isinstance(n, ast.Constant) and isinstance(n.value, str) and 0 < len(n.value) < 260 and '\n' not in n.value:
            p = n.value.replace('\\', '/')
            if os.path.isabs(n.value) or p.startswith('..') or '*' in p or '?' in p:
                continue
            full = os.path.join(script_dir, p)
            try:
                if os.path.isfile(full) and not p.lower().endswith(('.py', '.pyc', EXTENSION)):
                    found.append(p)
            except (OSError, ValueError):
                pass
    return found


def _blocks_used(source):
    ''' [(name, file)] of the custom blocks the script uses, and those the blocks it uses use in their turn '''
    try:
        from fieldes import blocks
        info = {b['name']: b['file'] for b in blocks.info()}
    except Exception:
        return {}
    files = {}
    used = _used_names(source) & set(info)
    queue = list(used)
    while queue:
        name = queue.pop()
        f = info[name]
        if f in files:
            continue
        try:
            with open(f, encoding='utf-8') as fh:
                text = fh.read()
        except OSError:
            continue
        files[f] = text
        for other in _used_names(text) & set(info):
            if info[other] not in files:
                queue.append(other)
    return files


def build(source, script_dir, out_path, title=None):
    ''' Bundles a script into a `.fieldesapp` file.  Returns a report:
        {'path', 'size', 'title', 'inputs': [...], 'blocks': [names], 'files': [paths], 'problems': [{'line', 'message'}], 'warnings': [text]} '''
    found = inputs_of(source)
    script_dir = os.path.abspath(script_dir or os.getcwd())
    problems = list(found['problems'])
    warnings = []
    chosen_title = (title or found['title'] or '').strip()
    if not chosen_title:
        chosen_title = os.path.splitext(os.path.basename(out_path))[0]

    files = {}          # name in the zip -> full path on disk
    assets = {}         # the path as app.asset() has it -> name under files/
    external = 0
    for a in found['assets']:
        if os.path.isabs(a):
            src = a
            if not os.path.isfile(src):
                problems.append({'line': 0, 'message': 'app.asset(%r): there is no such file' % a})
                continue
            external += 1
            rel = '_external/%d_%s' % (external, os.path.basename(a))
        else:
            src = os.path.join(script_dir, a)
            if not os.path.isfile(src):
                problems.append({'line': 0, 'message': 'app.asset(%r): there is no such file next to the script (%s)' % (a, script_dir)})
                continue
            rel = a.replace('\\', '/')
        files['files/' + rel] = src
        assets[a] = 'files/' + rel
    for p in _literals_that_are_files(source, script_dir):
        files.setdefault('files/' + p, os.path.join(script_dir, p))
    # modules next to the script that it imports (and they import in turn)
    modules, queue = {}, list(_imports_of(source))
    while queue:
        m = queue.pop()
        for cand in (os.path.join(script_dir, m + '.py'), os.path.join(script_dir, m, '__init__.py')):
            if os.path.isfile(cand) and m not in modules:
                modules[m] = cand
                if cand.endswith('__init__.py'):
                    base = os.path.dirname(cand)
                    for root, _, names in os.walk(base):
                        for n in names:
                            if n.endswith('.py'):
                                files['files/' + os.path.relpath(os.path.join(root, n), script_dir).replace('\\', '/')] = os.path.join(root, n)
                else:
                    files['files/' + m + '.py'] = cand
                try:
                    with open(cand, encoding='utf-8') as fh:
                        queue.extend(_imports_of(fh.read()))
                except OSError:
                    pass
    blocks = _blocks_used(source)
    total = 0
    for name, src in list(files.items()):
        try:
            size = os.path.getsize(src)
        except OSError:
            del files[name]
            continue
        if size > MAX_FILE:
            problems.append({'line': 0, 'message': '%s is %d MB: too big to bundle' % (os.path.basename(src), size // (1024 * 1024))})
            del files[name]
            continue
        total += size
    if total > 1024 * 1024 * 1024:
        warnings.append('the files that come along are %d MB' % (total // (1024 * 1024)))
    if not found['inputs']:
        warnings.append('the script declares no inputs (app.number, app.file, ...): the app will have a Run button and nothing to choose')
    for line_no, text in _absolute_paths(source):
        warnings.append('line %d reads %s, a path on this computer: another computer will not have it -- make it an input '
                        '(app.file) or bring the file along (app.asset)' % (line_no, text))

    manifest = {
        'format': FORMAT,
        'id': _slug(chosen_title),
        'title': chosen_title,
        'description': found['description'],
        'version': found['version'],
        'created': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'fieldes': _fieldes_version(),
        'inputs': found['inputs'],
        'viewport': found['viewport'],
        'assets': assets,
        'blocks': sorted(os.path.splitext(os.path.basename(f))[0] for f in blocks),
        'files': sorted(n[len('files/'):] for n in files),
    }
    folder = os.path.dirname(os.path.abspath(out_path))
    if folder and not os.path.isdir(folder):
        os.makedirs(folder, exist_ok=True)
    tmp = out_path + '.part'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('manifest.json', json.dumps(manifest, indent=1, ensure_ascii=False))
        z.writestr('app.py', source)
        for f, text in sorted(blocks.items()):
            z.writestr('blocks/' + os.path.basename(f), text)
        for name, src in sorted(files.items()):
            z.write(src, name)
    os.replace(tmp, out_path)
    return {'path': out_path, 'size': os.path.getsize(out_path), 'title': chosen_title, 'inputs': found['inputs'],
            'blocks': manifest['blocks'], 'files': manifest['files'], 'problems': problems, 'warnings': warnings}


def _absolute_paths(source):
    ''' [(line, text)] of the texts in a script that are absolute paths of files that exist on this computer '''
    out = []
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return out
    # (a path given to app.asset comes along: it is not the problem this warns of)
    brought = {id(n.args[0]) for n in ast.walk(tree) if _app_call(n) == 'asset' and n.args}
    for n in ast.walk(tree):
        if isinstance(n, ast.Constant) and isinstance(n.value, str) and os.path.isabs(n.value) and 3 < len(n.value) < 260 \
                and '\n' not in n.value and id(n) not in brought:
            try:
                if os.path.exists(n.value):
                    out.append((n.lineno, n.value))
            except (OSError, ValueError):
                pass
    return out


def _fieldes_version():
    try:
        import fieldes
        return getattr(fieldes, '__version__', '') or ''
    except Exception:
        return ''


# ---------------------------------------------------------------------------------------------------------------------
# Opening

def cache_folder():
    ''' Where apps are unpacked: FIELDES_APPS_DIR, else the cache of the user '''
    chosen = os.environ.get('FIELDES_APPS_DIR')
    if chosen:
        return chosen
    if sys.platform == 'win32':
        base = os.environ.get('LOCALAPPDATA') or os.path.join(os.path.expanduser('~'), 'AppData', 'Local')
        return os.path.join(base, 'FielDes', 'FielDes', 'apps')
    base = os.environ.get('XDG_CACHE_HOME') or os.path.join(os.path.expanduser('~'), '.cache')
    return os.path.join(base, 'FielDes', 'apps')


def documents_folder():
    ''' The folder a relative output path means in the window: Documents of the user '''
    home = os.path.expanduser('~')
    for cand in (os.path.join(home, 'Documents'), os.path.join(home, 'OneDrive', 'Documents'), home):
        if os.path.isdir(cand):
            return cand
    return home


def _safe_member(name):
    n = name.replace('\\', '/')
    return not (n.startswith('/') or re.match(r'^[A-Za-z]:', n) or any(part == '..' for part in n.split('/')))


def _sha(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()[:12]


def _unpack(path):
    with zipfile.ZipFile(path) as z:
        if 'manifest.json' not in z.namelist() or 'app.py' not in z.namelist():
            raise ValueError('%s is not a FielDes app (no manifest.json and app.py in it)' % os.path.basename(path))
        manifest = json.loads(z.read('manifest.json').decode('utf-8'))
        if int(manifest.get('format', 0)) > FORMAT:
            raise ValueError('%s was made by a newer FielDes (app format %s): update FielDes to run it' % (os.path.basename(path), manifest.get('format')))
        root = os.path.join(cache_folder(), '%s-%s' % (_slug(manifest.get('id') or manifest.get('title') or 'app'), _sha(path)))
        done = os.path.join(root, '.unpacked')
        if not os.path.isfile(done):
            if os.path.isdir(root):
                shutil.rmtree(root, ignore_errors=True)
            os.makedirs(root, exist_ok=True)
            for name in z.namelist():
                if name.endswith('/') or not _safe_member(name):
                    continue
                target = os.path.join(root, *name.split('/'))
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with z.open(name) as src, open(target, 'wb') as dst:
                    shutil.copyfileobj(src, dst)
            os.makedirs(os.path.join(root, 'files'), exist_ok=True)
            os.makedirs(os.path.join(root, 'blocks'), exist_ok=True)
            with open(done, 'w') as f:
                f.write('ok')
            _forget_old_versions(root)
    return root, manifest


def _forget_old_versions(root):
    ''' A newer version of an app was unpacked: the older ones of the same app are no use any more '''
    folder = os.path.dirname(root)
    stem = os.path.basename(root).rsplit('-', 1)[0] + '-'
    for name in os.listdir(folder):
        full = os.path.join(folder, name)
        if full != root and name.startswith(stem) and re.fullmatch(r'[0-9a-f]{12}', name[len(stem):]):
            # (a program cannot delete the folder it is working in: the run that follows goes to the new version's folder anyway)
            try:
                here = os.path.normcase(os.path.abspath(os.getcwd()))
                if here == os.path.normcase(full) or here.startswith(os.path.normcase(full) + os.sep):
                    os.chdir(root)
            except OSError:
                pass
            shutil.rmtree(full, ignore_errors=True)


def _resolve_default(spec, assets, root, cwd):
    ''' The default of an input: what app.asset('x') stands for where it was written there becomes the full path of the file '''
    d = spec.get('default')
    if isinstance(d, dict) and '__asset__' in d:
        a = d['__asset__']
        if a in assets:
            return os.path.join(root, *assets[a].split('/'))
        return a if os.path.isabs(a) else os.path.abspath(os.path.join(cwd, *a.replace('\\', '/').split('/')))
    return d


def open_app(path):
    ''' Opens an app for running: a `.fieldesapp` (unpacked) or a plain script (run where it is -- the engineer's preview).
        Returns {'kind': 'app' | 'script', 'path', 'root', 'cwd', 'script': text, 'manifest': {title, description, version, inputs
        (the defaults made paths), assets}, 'problems': [...]}.  Raises ValueError / OSError with a message that says what is wrong. '''
    path = os.path.abspath(path)
    if not os.path.isfile(path):
        raise ValueError('there is no file %s' % path)
    if path.lower().endswith(EXTENSION):
        root, manifest = _unpack(path)
        with open(os.path.join(root, 'app.py'), encoding='utf-8') as f:
            script = f.read()
        kind = 'app'
        cwd = os.path.join(root, 'files')
        assets = {a: os.path.join(root, *p.split('/')) for a, p in (manifest.get('assets') or {}).items()}
        inputs = manifest.get('inputs') or []
        problems = []
    else:
        with open(path, encoding='utf-8') as f:
            script = f.read()
        kind = 'script'
        root = os.path.dirname(path)
        cwd = root
        found = inputs_of(script)
        manifest = {'title': found['title'] or os.path.splitext(os.path.basename(path))[0], 'description': found['description'],
                    'version': found['version'], 'id': _slug(found['title'] or os.path.splitext(os.path.basename(path))[0]),
                    'viewport': found['viewport']}
        assets = {}
        inputs = found['inputs']
        problems = found['problems']
    shown = []
    for spec in inputs:
        spec = dict(spec)
        spec['default'] = _resolve_default(spec, manifest.get('assets') or {}, root, cwd)
        shown.append(spec)
    manifest = dict(manifest)
    manifest['inputs'] = shown
    return {'kind': kind, 'path': path, 'root': root, 'cwd': cwd, 'script': script, 'manifest': manifest, 'assets': assets,
            'problems': problems}


# ---------------------------------------------------------------------------------------------------------------------
# Values

def check(opened, raw, outputs=None, running=True):
    ''' The values of an opened app's inputs from what was chosen (`raw`: key -> value; inputs not in it have their defaults).
        Returns (values, errors): the values by key, already checked and made full paths, and {key: why} for the ones that do not
        fit.  When all fit the run is set up (fieldes.app.begin) -- the script's inputs have these values. '''
    _app.begin({}, opened['root'] if opened['kind'] == 'app' else None, outputs, opened.get('assets'))
    values, errors = {}, {}
    for spec in opened['manifest']['inputs']:
        key = spec['key']
        value = raw[key] if key in raw else spec.get('default')
        try:
            values[key] = _app.coerce(spec, value)
        except _app.InputError as e:
            errors[key] = e.why
        except Exception as e:     # (a spec that is wrong in itself)
            errors[key] = str(e)
    if not errors:
        if opened['kind'] == 'app':
            blocks_dir = os.path.join(opened['root'], 'blocks')
            os.environ['FIELDES_BLOCKS'] = blocks_dir
        _app.begin(values, opened['root'] if opened['kind'] == 'app' else None, outputs, opened.get('assets'), running=running)
        _app._started = time.time()
    else:
        _app.end()
    return values, errors


def outputs_made(started=None):
    ''' [{'label', 'path', 'exists', 'size', 'fresh'}] of the files the last run said it makes: `fresh` when it was written since the run began '''
    started = started if started is not None else getattr(_app, '_started', 0)
    seen, out = set(), []
    for o in _app._outputs:
        p = o['path']
        if p in seen:
            continue
        seen.add(p)
        try:
            st = os.stat(p)
            out.append({'label': o['label'], 'path': p, 'exists': True, 'size': st.st_size, 'fresh': st.st_mtime >= started - 2.5})
        except OSError:
            out.append({'label': o['label'], 'path': p, 'exists': False, 'size': 0, 'fresh': False})
    return out


def watched_files(opened, values):
    ''' The files a watching runner looks at: the files chosen for the inputs, and the app itself '''
    out = []
    for spec in opened['manifest']['inputs']:
        if spec['kind'] == 'file' and values.get(spec['key']):
            out.append(values[spec['key']])
    out.append(opened['path'])
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Running without a window

def _option_table(opened):
    rows = []
    for s in opened['manifest']['inputs']:
        d = s.get('default')
        kind = s['kind']
        if kind == 'number':
            what = 'a whole number' if s.get('integer') else 'a number'
            if s.get('min') is not None and s.get('max') is not None:
                what += ', %g to %g' % (s['min'], s['max'])
            elif s.get('min') is not None:
                what += ', at least %g' % s['min']
            elif s.get('max') is not None:
                what += ', at most %g' % s['max']
            if s.get('unit'):
                what += ' ' + s['unit']
        elif kind == 'choice':
            what = ' | '.join(s['options'])
        elif kind == 'flag':
            what = 'yes or no (--%s, --no-%s)' % (s['label'].replace(' ', '_'), s['label'].replace(' ', '_'))
        elif kind == 'file':
            what = 'a file' + (' (%s)' % s['filter'] if s.get('filter') else '')
        elif kind == 'save_as':
            what = 'where to save it'
        elif kind == 'folder':
            what = 'a folder'
        else:
            what = 'a text'
        default = '' if (kind == 'file' and not d) else ', default %s' % (('yes' if d else 'no') if kind == 'flag' else d)
        rows.append(('--' + s['label'].replace(' ', '_'), what + default + (' -- ' + s['help'] if s.get('help') else '')))
    return rows


def usage(opened=None):
    lines = ['FielDes run APP [--name value ...] [--watch] [--log FILE] [--quiet] [--folder DIR]', '']
    if opened:
        m = opened['manifest']
        lines.append(m['title'] + (' ' + m['version'] if m.get('version') else '') + (' -- ' + m['description'] if m.get('description') else ''))
        lines.append('')
        rows = _option_table(opened)
        if rows:
            lines.append('Inputs:')
            width = max(len(r[0]) for r in rows)
            for name, what in rows:
                lines.append('  %s  %s' % (name.ljust(width), what))
        else:
            lines.append('This app has no inputs.')
        lines.append('')
    lines += ['Options:',
              '  --watch        run again whenever an input file or the app itself changes (Ctrl+C ends it)',
              '  --log FILE     write what is printed to FILE too',
              '  --quiet        print only errors',
              '  --folder DIR   a relative output path means DIR (the folder the command was started in by default)',
              '  --help         this text (with the inputs of APP)']
    return '\n'.join(lines)


def parse_options(opened, argv):
    ''' ({key: text}, {'watch', 'log', 'quiet', 'help', 'folder'}) from the command line after the app; raises ValueError '''
    by_name = {}
    for s in opened['manifest']['inputs']:
        by_name[_app.key_of(s['label'])] = s
    raw, opts = {}, {'watch': False, 'log': None, 'quiet': False, 'help': False, 'folder': None}
    i = 0
    while i < len(argv):
        a = argv[i]
        i += 1
        if not a.startswith('--') or len(a) < 3:
            raise ValueError('"%s" is not an option: inputs are written --name value' % a)
        body = a[2:]
        value = None
        if '=' in body:
            body, value = body.split('=', 1)
        low = body.casefold()
        if low in ('watch', 'quiet', 'help'):
            opts[low] = True
            continue
        if low in ('log', 'folder'):
            if value is None:
                if i >= len(argv):
                    raise ValueError('--%s needs a value' % low)
                value = argv[i]
                i += 1
            opts[low] = value
            continue
        negate = False
        spec = by_name.get(_app.key_of(body))
        if spec is None and low.startswith(('no-', 'no_')):
            flag_spec = by_name.get(_app.key_of(body[3:]))
            if flag_spec is not None and flag_spec['kind'] == 'flag':
                spec, negate = flag_spec, True
        if spec is None:
            known = ', '.join('--' + s['label'].replace(' ', '_') for s in opened['manifest']['inputs']) or 'none'
            raise ValueError('this app has no input called "%s" (its inputs: %s)' % (body, known))
        if negate:
            raw[spec['key']] = False
            continue
        if value is None:
            if spec['kind'] == 'flag' and (i >= len(argv) or argv[i].startswith('--')):
                value = True
            else:
                if i >= len(argv):
                    raise ValueError('--%s needs a value' % body)
                value = argv[i]
                i += 1
        raw[spec['key']] = value
    return raw, opts


def execute(source, filename, cwd):
    ''' Runs the script as the editor does, but without a window: its statements one after another, `var(x)` being x.  Raises what the
        script raises '''
    from fieldes import blocks
    os.chdir(cwd)
    if cwd not in sys.path:
        sys.path.append(cwd)
    tree = ast.parse(source, filename)
    namespace = {'__name__': '__main__', '__file__': filename, 'var': lambda x: x}
    namespace.update(blocks.namespace())
    for node in tree.body:
        exec(compile(ast.Module([node], []), filename, 'exec'), namespace)


def _where(exc, filename):
    ''' The line of the script an exception came from '''
    line = None
    for frame in traceback.extract_tb(exc.__traceback__):
        if frame.filename == filename:
            line = frame.lineno
    return line


class _Tee:
    def __init__(self, *streams):
        self.streams = [s for s in streams if s]

    def write(self, text):
        for s in self.streams:
            try:
                s.write(text)
                s.flush()               # (a watching run goes on for hours: what it printed is in the log now)
            except Exception:
                pass
        return len(text)

    def flush(self):
        for s in self.streams:
            try:
                s.flush()
            except Exception:
                pass


def run_cli(argv):
    ''' FielDes run APP [options]: returns the exit code (0 done, 1 the script failed, 2 something said wrongly on the command line) '''
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors='replace')
        except (AttributeError, ValueError):
            pass
    if not argv or argv[0] in ('-h', '--help'):
        print(usage())
        return 0 if argv else 2
    path = argv[0]
    try:
        opened = open_app(path)
    except Exception as e:
        print('FielDes run: %s' % e, file=sys.stderr)
        return 2
    try:
        raw, opts = parse_options(opened, argv[1:])
    except ValueError as e:
        print('FielDes run: %s\n\n%s' % (e, usage(opened)), file=sys.stderr)
        return 2
    if opts['help']:
        print(usage(opened))
        return 0
    log = None
    if opts['log']:
        log = open(opts['log'], 'a', encoding='utf-8')
        sys.stdout = _Tee(sys.__stdout__, log)
        sys.stderr = _Tee(sys.__stderr__, log)
    def say(*a):
        if not opts['quiet']:
            print(*a)
            sys.stdout.flush()
    base = opts['folder'] or os.getcwd()
    try:
        return _cli_loop(opened, raw, opts, base, say)
    finally:
        if log:
            sys.stdout, sys.stderr = sys.__stdout__, sys.__stderr__
            log.close()


def _cli_loop(opened, raw, opts, base, say):
    manifest = opened['manifest']
    say('%s%s' % (manifest['title'], ' ' + manifest['version'] if manifest.get('version') else ''))
    code = 0
    while True:
        # (an app that is opened again is read again: a watching run follows the file it was made from)
        if opts['watch'] and code is not None:
            try:
                fresh = open_app(opened['path'])
                opened = fresh
            except Exception as e:
                say('the app cannot be read: %s' % e)
        values, errors = check(opened, raw, base)
        if errors:
            for spec in opened['manifest']['inputs']:
                if spec['key'] in errors:
                    print('FielDes run: %s: %s' % (spec['label'], errors[spec['key']]), file=sys.stderr)
            code = 2
        else:
            started = time.time()
            filename = os.path.join(opened['root'], 'app.py') if opened['kind'] == 'app' else opened['path']
            try:
                execute(opened['script'], filename, opened['cwd'])
                code = 0
                made = [o for o in outputs_made(started) if o['exists']]
                say('Done in %.1f s.' % (time.time() - started))
                for o in made:
                    say('  %s: %s (%s)' % (o['label'], o['path'], _size(o['size'])))
            except KeyboardInterrupt:
                raise
            except BaseException as e:
                code = 1
                line = _where(e, filename)
                print('FielDes run: the app stopped after %.1f s%s: %s: %s' % (time.time() - started, ' at line %d' % line if line else '',
                                                                           type(e).__name__, e), file=sys.stderr)
                if os.environ.get('FIELDES_TRACEBACK'):
                    traceback.print_exc()
            finally:
                _app.end()
        for stream in (sys.stdout, sys.stderr):
            try:
                stream.flush()
            except Exception:
                pass
        if not opts['watch']:
            return code
        files = watched_files(opened, values) if not errors else [opened['path']]
        say('Watching %d file%s -- Ctrl+C ends.' % (len(files), '' if len(files) == 1 else 's'))
        try:
            _wait_for_change(files)
        except KeyboardInterrupt:
            return code
        say('')
        say('--- a file changed: running again ---')


def _size(n):
    if n < 1024:
        return '%d bytes' % n
    if n < 1024 * 1024:
        return '%.0f KB' % (n / 1024)
    return '%.1f MB' % (n / 1024 / 1024)


def _stamp(p):
    try:
        st = os.stat(p)
        return (st.st_mtime_ns, st.st_size)
    except OSError:
        return None


def _wait_for_change(files, interval=0.4):
    ''' Waits until one of the files has changed, and then until it has been still for a moment (a file that is being written is not read
        half written) '''
    before = {f: _stamp(f) for f in files}
    while True:
        time.sleep(interval)
        if any(_stamp(f) != before[f] for f in files):
            break
    while True:
        now = {f: _stamp(f) for f in files}
        time.sleep(0.6)
        if all(_stamp(f) == now[f] for f in files):
            return


def run_export(argv):
    ''' FielDes export SCRIPT [-o FILE] [--title TEXT]: bundles a script into an app, as File > Export as app does '''
    out, title, script = None, None, None
    i = 0
    while i < len(argv):
        a = argv[i]
        i += 1
        if a in ('-o', '--out', '--output') and i < len(argv):
            out = argv[i]
            i += 1
        elif a == '--title' and i < len(argv):
            title = argv[i]
            i += 1
        elif a.startswith('-'):
            print('FielDes export: unknown option %s' % a, file=sys.stderr)
            return 2
        else:
            script = a
    if not script:
        print('FielDes export SCRIPT [-o FILE.fieldesapp] [--title TEXT]', file=sys.stderr)
        return 2
    script = os.path.abspath(script)
    try:
        with open(script, encoding='utf-8') as f:
            source = f.read()
    except OSError as e:
        print('FielDes export: %s' % e, file=sys.stderr)
        return 2
    out = out or os.path.splitext(script)[0] + EXTENSION
    if not out.lower().endswith(EXTENSION):
        out += EXTENSION
    report = build(source, os.path.dirname(script), out, title)
    print('%s: %d input%s, %d block%s, %d file%s, %s' % (report['path'], len(report['inputs']), '' if len(report['inputs']) == 1 else 's',
                                                      len(report['blocks']), '' if len(report['blocks']) == 1 else 's', len(report['files']),
                                                      '' if len(report['files']) == 1 else 's', _size(report['size'])))
    for p in report['problems']:
        print('  problem%s: %s' % (' (line %d)' % p['line'] if p.get('line') else '', p['message']), file=sys.stderr)
    for w in report['warnings']:
        print('  note: %s' % w)
    return 1 if report['problems'] else 0


def run_info(argv):
    ''' FielDes info APP: what an app asks for '''
    if not argv:
        print('FielDes info APP', file=sys.stderr)
        return 2
    try:
        opened = open_app(argv[0])
    except Exception as e:
        print('FielDes info: %s' % e, file=sys.stderr)
        return 2
    print(usage(opened))
    return 0


def main(argv):
    ''' The command line: run, export, info '''
    if not argv or argv[0] in ('-h', '--help', 'help'):
        print('FielDes run APP [--name value ...]   run an app without a window\n'
              'FielDes export SCRIPT [-o FILE]       bundle a script into an app\n'
              'FielDes info APP                      what an app asks for')
        return 0 if argv else 2
    cmd, rest = argv[0], argv[1:]
    if cmd == 'run':
        return run_cli(rest)
    if cmd == 'export':
        return run_export(rest)
    if cmd == 'info':
        return run_info(rest)
    print('FielDes: "%s" is not a command (run, export, info)' % cmd, file=sys.stderr)
    return 2
