'''
Checks of the Python environment of the console (python/fieldes/pyenv.py): `pip install / uninstall / list / show` into the user's folder of packages, `env`, `env use`, and what is a command and what is Python.
No network: a small wheel made here stands in for a package, and the user's folder is a temporary one.

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/pyenv_checks.py
It prints PASS or FAIL for every check, and exits with 1 when one failed (about 15 s).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import os
import shutil
import sys
import tempfile
import zipfile

failures = []


def check(label, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail and not ok else ''))
    sys.stdout.flush()
    if not ok:
        failures.append(label)


folder = tempfile.mkdtemp(prefix='pyenv_checks_')
os.environ['FIELDES_USER_DIR'] = folder
try:
    from fieldes import pyenv

    wheel = os.path.join(folder, 'fieldes_checkpkg-1.0-py3-none-any.whl')
    with zipfile.ZipFile(wheel, 'w') as z:
        z.writestr('fieldes_checkpkg/__init__.py', 'ANSWER = 42\n')
        z.writestr('fieldes_checkpkg-1.0.dist-info/METADATA', 'Metadata-Version: 2.1\nName: fieldes-checkpkg\nVersion: 1.0\n')
        z.writestr('fieldes_checkpkg-1.0.dist-info/WHEEL', 'Wheel-Version: 1.0\nGenerator: check\nRoot-Is-Purelib: true\nTag: py3-none-any\n')
        z.writestr('fieldes_checkpkg-1.0.dist-info/RECORD', '')

    check('a line of Python is not a command', pyenv.command('x = 1 + 1') is None and pyenv.command('pip = 3') is None and pyenv.command('envelope') is None and pyenv.command('print(1)') is None)
    check('env says which Python is in use', 'Python' in (pyenv.command('env') or '') and 'folder of packages' in (pyenv.command('env') or ''))
    check('pip list of nothing is empty', (pyenv.command('pip list') or '').strip() == '')
    answer = pyenv.command('pip install "%s"' % wheel)
    check('pip install of a wheel says it was installed', answer is not None and 'Successfully installed fieldes-checkpkg-1.0' in answer, answer)
    try:
        import fieldes_checkpkg
        check('the installed package is imported from the user folder of packages', fieldes_checkpkg.ANSWER == 42 and pyenv.packages_dir() in fieldes_checkpkg.__file__, getattr(fieldes_checkpkg, '__file__', None))
    except ImportError as e:
        check('the installed package is imported from the user folder of packages', False, e)
    check('pip list names it', 'fieldes-checkpkg' in (pyenv.command('pip list') or ''))
    check('pip show names it', 'fieldes-checkpkg==1.0' in (pyenv.command('pip show fieldes-checkpkg') or ''))
    check('pip uninstall removes it', 'Removed fieldes-checkpkg' in (pyenv.command('pip uninstall fieldes-checkpkg') or '') and not os.listdir(pyenv.packages_dir()))
    check('pip uninstall of a package that is not there says so', 'is not installed' in (pyenv.command('pip uninstall nothing-here') or ''))
    check('env use of a folder that is no environment says so', 'has no site-packages' in (pyenv.command('env use %s' % os.path.join(folder, 'nothing')) or ''))
    check('the environment in use is still the own folder', pyenv.chosen() == '')
    fake = os.path.join(folder, 'fake_env')
    os.makedirs(os.path.join(fake, 'Lib', 'site-packages'))
    answer = pyenv.command('env use "%s"' % fake)
    check('env use of a folder with site-packages uses it', answer is not None and answer.startswith('In use:') and pyenv.chosen() == fake and os.path.join(fake, 'Lib', 'site-packages') in sys.path, answer)
    check('env use default goes back', 'in use again' in (pyenv.command('env use default') or '') and pyenv.chosen() == '')
finally:
    shutil.rmtree(folder, ignore_errors=True)

if failures:
    print('FAILED: ' + '; '.join(failures))
    sys.exit(1)
print('ALL CHECKS PASSED')
