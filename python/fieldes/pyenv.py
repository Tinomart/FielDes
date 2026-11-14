'''
The Python environment of FielDes, reached from its console (H181, the user, 2026-11-12: "I want the user to have some direct access to the python envrionment ... for installing different
packages, switching environments, etc.").

FielDes runs its own Python (the one in its `runtime` folder).  The console takes two kinds of line besides Python itself -- neither is valid Python, so no code is changed by them:

    pip install requests numpy      installs packages into FielDes's own folder of packages (`python-packages` in the user's folder) -- or into the environment that is in use
    pip uninstall requests          takes them out again
    pip list                        what is installed there;  pip show <name>,  pip freeze
    env                             which Python, which folder of packages, which environment is in use
    env use <folder>                use another environment: the folder of a venv or a conda environment (or of any Python) -- its packages can be imported by every script and by the console;
                                    `pip` then installs into it with its own Python.  `env use default` goes back to FielDes's own folder
    env new <name>                  a new venv (made with FielDes's Python) in the user's folder, and in use at once
    env list                        the environments that `env new` made

What is chosen is kept (`python_environment.json` in the user's folder) and is in force at the next start: `activate()` puts the folders on `sys.path`.  A package with compiled parts has to be
built for the Python version of FielDes (3.13): an environment of another version is used for its pure-Python packages, and the console says so.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import glob
import json
import os
import shlex
import shutil
import subprocess
import sys

from fieldes.i18n import tr as _tr

__all__ = []


def _user_dir():
    from fieldes.user_primitives import user_dir
    return user_dir()


def packages_dir():
    ''' FielDes's own folder of packages '''
    return os.path.join(_user_dir(), 'python-packages')


def _settings_file():
    return os.path.join(_user_dir(), 'python_environment.json')


def _environments_dir():
    return os.path.join(_user_dir(), 'environments')


def chosen():
    ''' The environment in use (the folder of a venv, a conda environment ...), or '' for FielDes's own folder of packages '''
    try:
        with open(_settings_file(), 'r', encoding='utf-8') as f:
            return str(json.load(f).get('environment') or '')
    except Exception:
        return ''


def _choose(path):
    os.makedirs(_user_dir(), exist_ok=True)
    with open(_settings_file(), 'w', encoding='utf-8') as f:
        json.dump({'environment': path}, f)


def runtime_python():
    ''' The Python that FielDes runs (the program itself is not one: a process that pip is run in has to be) '''
    base = sys.base_prefix
    for name in ('python.exe', os.path.join('bin', 'python3'), os.path.join('bin', 'python')):
        candidate = os.path.join(base, name)
        if os.path.exists(candidate):
            return candidate
    return ''


def environment_python(env):
    ''' The Python of an environment folder (a venv, a conda environment), or '' '''
    for name in (os.path.join('Scripts', 'python.exe'), 'python.exe', os.path.join('bin', 'python3'), os.path.join('bin', 'python')):
        candidate = os.path.join(env, name)
        if os.path.exists(candidate):
            return candidate
    return ''


def site_packages_of(env):
    ''' The folders of packages of an environment folder (Windows: Lib\\site-packages; elsewhere lib/pythonX.Y/site-packages) '''
    found = []
    flat = os.path.join(env, 'Lib', 'site-packages')
    if os.path.isdir(flat):
        found.append(flat)
    found.extend(p for p in sorted(glob.glob(os.path.join(env, 'lib', 'python*', 'site-packages'))) if os.path.isdir(p))
    return found


def activate():
    ''' Puts the folders of packages that the user has on `sys.path` (called when FielDes starts and by the console): its own folder, and the environment that is in use '''
    paths = []
    own = packages_dir()
    if os.path.isdir(own):
        paths.append(own)
    env = chosen()
    if env:
        paths.extend(site_packages_of(env))
    for p in paths:
        if p not in sys.path:
            sys.path.append(p)
    return paths


def _bundled_pip():
    ''' The pip that comes with the Python of FielDes as a wheel (ensurepip carries it): a wheel is run as it is, nothing is installed to have pip '''
    found = sorted(glob.glob(os.path.join(sys.base_prefix, 'Lib', 'ensurepip', '_bundled', 'pip-*.whl')) + glob.glob(os.path.join(sys.base_prefix, 'lib', 'python*', 'ensurepip', '_bundled', 'pip-*.whl')))
    return found[-1] if found else ''


def _run(command, cwd=None, write=None):
    ''' The text a command prints.  With `write` (a function of one text: the console's) it is passed on LINE BY LINE as the command prints it -- the steps of a `pip install`, one after another, as in a terminal (H205, the user,
        2026-11-14: "I want the fieldes console to display things in real time like other consoles") -- and what is returned is '' (it has been written) '''
    environment = dict(os.environ)
    for name in ('PYTHONHOME', 'PYTHONPATH', 'PYTHONSTARTUP'):
        environment.pop(name, None)
    flags = 0x08000000 if sys.platform == 'win32' else 0            # (no console window of its own)
    if write is None:
        try:
            done = subprocess.run(command, cwd=cwd, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, creationflags=flags, timeout=1800)
        except subprocess.TimeoutExpired:
            return _tr('It took more than half an hour and was stopped.')
        except OSError as e:
            return _tr('It could not be run: %s') % e
        return done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n').replace('\r', '\n').strip()
    import queue
    import threading
    import time
    try:
        proc = subprocess.Popen(command, cwd=cwd, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, creationflags=flags)
    except OSError as e:
        return _tr('It could not be run: %s') % e
    lines = queue.Queue()

    def reader():
        try:
            for raw in iter(proc.stdout.readline, b''):
                lines.put(raw)
        finally:
            lines.put(None)
    threading.Thread(target=reader, daemon=True).start()
    deadline = time.time() + 1800
    while True:
        try:
            raw = lines.get(timeout=0.5)
        except queue.Empty:
            if time.time() > deadline:
                proc.kill()
                write(_tr('It took more than half an hour and was stopped.') + '\n')
                break
            continue
        if raw is None:
            break
        write(raw.decode('utf-8', 'replace').replace('\r\n', '\n').replace('\r', '\n'))
    try:
        proc.wait(timeout=30)
    except Exception:
        proc.kill()
    return ''


def _version_note(env):
    ''' '' when the environment has the Python of FielDes, else what to know of it '''
    python = environment_python(env)
    if not python:
        return ''
    text = _run([python, '-c', 'import sys; print("%d.%d" % sys.version_info[:2])'])
    mine = '%d.%d' % sys.version_info[:2]
    if text and text != mine:
        return _tr('This environment has Python %s and FielDes runs Python %s: packages that are only Python work, a package with compiled parts (numpy ...) has to be built for %s.') % (text, mine, mine)
    return ''


def _has_pip(env):
    ''' Whether the environment folder holds a pip of its own '''
    return any(os.path.isdir(os.path.join(p, 'pip')) for p in site_packages_of(env))


def _pip(arguments, write=None):
    env = chosen()
    if env:
        python = environment_python(env)
        if not python:
            return _tr('The environment %s has no Python (a folder with Scripts\\python.exe or bin/python): `env use default` leaves it.') % env
        if not _has_pip(env) and _bundled_pip():
            # (an environment made where `ensurepip` is not part of the Python -- the Linux package leaves it out -- has no pip: the pip wheel of FielDes is run with the environment's Python instead, which installs into that environment)
            return _run([python, os.path.join(_bundled_pip(), 'pip')] + arguments, write=write)
        return _run([python, '-m', 'pip'] + arguments, write=write)
    wheel, python = _bundled_pip(), runtime_python()
    if not wheel or not python:
        return _tr('This copy of FielDes has no pip of its own. Make an environment with a Python that has one -- `env use <folder of a venv or a conda environment>` -- and pip installs into it.')
    return _run([python, os.path.join(wheel, 'pip')] + arguments, write=write)


def _uninstall_own(names):
    ''' pip cannot take packages out of a folder that `--target` filled: the files of each are listed in its dist-info (RECORD) and are removed here '''
    root = packages_dir()
    out = []
    for name in names:
        key = name.lower().replace('-', '_')
        infos = [p for p in glob.glob(os.path.join(root, '*.dist-info')) if os.path.basename(p).split('-')[0].lower().replace('.', '_') == key.replace('.', '_')]
        if not infos:
            out.append(_tr('%s is not installed in %s') % (name, root))
            continue
        for info in infos:
            record = os.path.join(info, 'RECORD')
            tops = set()
            try:
                with open(record, 'r', encoding='utf-8') as f:
                    for line in f:
                        path = line.split(',')[0].strip()
                        if path:
                            tops.add(path.replace('\\', '/').split('/')[0])
            except OSError:
                pass
            tops.add(os.path.basename(info))
            for top in tops:
                target = os.path.join(root, top)
                if os.path.isdir(target):
                    shutil.rmtree(target, ignore_errors=True)
                elif os.path.exists(target):
                    try:
                        os.remove(target)
                    except OSError:
                        pass
        out.append(_tr('Removed %s') % name)
    return '\n'.join(out)


def pip(arguments, write=None):
    ''' `pip <install|uninstall|list|show|freeze> ...` as the console takes it: the text it prints.  With `write` what pip prints is passed to it line by line as it prints it, and the text returned is what was not
        written (nothing of pip's own output) '''
    if not arguments:
        return 'pip install <packages> | pip uninstall <packages> | pip list | pip show <package> | pip freeze'
    verb, rest = arguments[0].lower(), arguments[1:]
    env = chosen()
    if verb == 'install':
        if not rest:
            return _tr('pip install: name the packages (pip install requests)')
        os.makedirs(packages_dir(), exist_ok=True)
        text = _pip(['install', '--disable-pip-version-check'] + rest, write) if env else _pip(['install', '--disable-pip-version-check', '--target', packages_dir(), '--upgrade'] + rest, write)
        activate()
        return text
    if verb == 'uninstall':
        if not rest:
            return _tr('pip uninstall: name the packages')
        flags = [a for a in rest if a.startswith('-')]
        names = [a for a in rest if not a.startswith('-')]
        return _pip(['uninstall', '-y'] + names, write) if env else _uninstall_own(names)
    if verb in ('list', 'freeze'):
        return _pip([verb, '--disable-pip-version-check'] + rest, write) if env else _pip([verb, '--disable-pip-version-check', '--path', packages_dir()] + rest, write)
    if verb == 'show':
        if env:
            return _pip(['show'] + rest)
        found = _pip(['list', '--disable-pip-version-check', '--path', packages_dir(), '--format', 'freeze'])
        return found if not rest else '\n'.join(line for line in found.splitlines() if line.lower().split('==')[0] in [r.lower() for r in rest]) or _tr('%s is not installed in %s') % (', '.join(rest), packages_dir())
    return _tr('pip %s is not available here: pip install | uninstall | list | show | freeze') % verb


def describe():
    env = chosen()
    mine = '%d.%d.%d' % sys.version_info[:3]
    lines = ['Python %s  (%s)' % (mine, runtime_python() or sys.executable)]
    if env:
        lines.append(_tr('environment in use: %s') % env)
        lines.extend(_tr('  packages: %s') % p for p in site_packages_of(env))
        note = _version_note(env)
        if note:
            lines.append(note)
    else:
        lines.append(_tr('environment in use: FielDes\'s own folder of packages, %s%s') % (packages_dir(), '' if os.path.isdir(packages_dir()) else _tr(' (made by the first pip install)')))
    return '\n'.join(lines)


def environment(arguments):
    ''' `env`, `env use <folder>|default`, `env new <name>`, `env list` as the console takes it: the text it prints '''
    verb = arguments[0].lower() if arguments else 'show'
    rest = arguments[1:]
    if verb == 'show':
        return describe()
    if verb == 'list':
        root = _environments_dir()
        found = sorted(p for p in glob.glob(os.path.join(root, '*')) if os.path.isdir(p))
        return '\n'.join(found) if found else _tr('No environment was made with `env new` (%s)') % root
    if verb == 'use':
        if not rest:
            return 'env use <folder of a venv or a conda environment> | env use default'
        target = ' '.join(rest).strip().strip('"\'')
        if target.lower() == 'default':
            _choose('')
            return _tr('FielDes\'s own folder of packages is in use again. Packages of the other environment are on the path until FielDes is started again.')
        target = os.path.abspath(os.path.expanduser(target))
        if os.path.isfile(target) and os.path.basename(target).lower().startswith('python'):
            target = os.path.dirname(target)
            if os.path.basename(target).lower() in ('scripts', 'bin'):
                target = os.path.dirname(target)
        if not site_packages_of(target):
            return _tr('%s has no site-packages (a venv or a conda environment: Lib\\site-packages, lib/pythonX.Y/site-packages)') % target
        _choose(target)
        activate()
        note = _version_note(target)
        return _tr('In use: %s') % target + '\n' + '\n'.join(_tr('  packages: %s') % p for p in site_packages_of(target)) + ('\n' + note if note else '')
    if verb == 'new':
        if not rest:
            return 'env new <name>'
        name = ''.join(c for c in rest[0] if c.isalnum() or c in '-_.')
        if not name:
            return _tr('env new: a name of letters, digits, - and _')
        target = os.path.join(_environments_dir(), name)
        if os.path.exists(target):
            return _tr('%s is there already: `env use %s`') % (target, target)
        python = runtime_python()
        if not python:
            return _tr('This copy of FielDes has no Python of its own to make an environment with.')
        os.makedirs(_environments_dir(), exist_ok=True)
        text = _run([python, '-m', 'venv', target])
        wheel = _bundled_pip()
        if site_packages_of(target) and not _has_pip(target) and wheel:
            # (`venv` asks `ensurepip` to put pip into the environment, and a Python without it -- the Linux package -- ends with an error and no pip: the pip wheel of FielDes installs pip into it, as ensurepip would have)
            fixed = _run([environment_python(target), os.path.join(wheel, 'pip'), 'install', '--no-index', '--no-deps', '--disable-pip-version-check', wheel])
            text = '' if _has_pip(target) else (text + '\n' + fixed).strip()
        if not site_packages_of(target):
            return _tr('The environment could not be made%s') % (':\n' + text if text else '.')
        _choose(target)
        activate()
        return _tr('Made and in use: %s') % target + ('\n' + text if text else '')
    return 'env | env use <folder> | env use default | env new <name> | env list'


def command(code, write=None):
    ''' The text a console line prints when it is one of the commands above, else None (a line of Python).  `pip ...`, `!pip ...`, `%pip ...`, and `env ...` -- `env` alone, or `env` and one of use, new, list, show --
        are commands: none of them is Python, so no code is taken for one.  `write`: what a command prints as it runs (pip) is passed to it line by line, and the text returned is the rest (see `pip`) '''
    line = code.strip()
    if '\n' in line:
        return None
    for prefix in ('!', '%'):
        if line.startswith(prefix):
            line = line[1:].lstrip()
    try:
        words = [w.strip('"\'') for w in shlex.split(line, posix=False)]       # (a path with spaces is in quotes; the backslashes of a Windows path stay)
    except ValueError:
        return None
    if not words:
        return None
    if words[0] == 'pip' and len(words) >= 2 and words[1].isidentifier() and not line.startswith('pip ' + '=') and words[1] not in ('=', '==', '.'):
        return pip(words[1:], write)
    if words[0] == 'env' and (len(words) == 1 and code.strip() in ('env',) or (len(words) >= 2 and words[1] in ('use', 'new', 'list', 'show'))):
        return environment(words[1:])
    return None
