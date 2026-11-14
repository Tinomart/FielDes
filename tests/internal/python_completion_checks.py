'''
Checks of the completion of Python in the editor and the console (python/fieldes/app_support.py: complete_python) and of the live output of the console (pyenv._run with `write`, the _Live buffer and terminal_poll).

H206 (the user, 2026-11-14): "it should obviously do auto completes for everything python realted in real time, not just parts internal to FielDes" -- after `pip install numpy`, `import nump` was not
completed to numpy.  H205: "I want the fieldes console to display things in real time like other consoles".

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/python_completion_checks.py
It prints PASS or FAIL for every check, and exits with 1 when one failed (a few seconds).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import json
import os
import sys
import tempfile
import threading
import time

from fieldes import app_support as support
from fieldes import pyenv

failures = []


def check(label, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail else ''))
    sys.stdout.flush()
    if not ok:
        failures.append(label)


def complete(before, source='', console=False):
    return json.loads(support.complete_python(json.dumps({'before': before, 'source': source, 'console': console})))


# ---------------------------------------------------------------- modules after import / from
check('import jso -> json', 'json' in complete('import jso'), complete('import jso')[:5])
check('import xml.d -> the submodules of xml (dom)', 'dom' in complete('import xml.d'), complete('import xml.d')[:5])
check('from os import pa -> path', 'path' in complete('from os import pa'), complete('from os import pa')[:5])
check('from xml import d -> dom (a submodule, not an attribute)', 'dom' in complete('from xml import d'), complete('from xml import d')[:5])
check('import os, jso -> json (the second of a list)', 'json' in complete('import os, jso'), complete('import os, jso')[:5])
check('from col -> collections', 'collections' in complete('from col'), complete('from col')[:5])

# ---------------------------------------------------------------- a package that is installed AFTER the program started
root = tempfile.mkdtemp(prefix='fieldes-complete-')
before_names = complete('import newlyinstalled_pkg')
check('a package that is not installed is not offered', 'newlyinstalled_pkg_x' not in complete('import newlyinstalled'))
pkg = os.path.join(root, 'newlyinstalled_pkg_x')
os.makedirs(pkg)
with open(os.path.join(pkg, '__init__.py'), 'w') as f:
    f.write('VALUE = 1\n')
sys.path.append(root)                       # (what pyenv.activate() does for the folder pip installs into)
time.sleep(0.01)
check('a package put on the path while the program runs is offered at once (as numpy is after `pip install numpy`)', 'newlyinstalled_pkg_x' in complete('import newlyinstalled'),
      complete('import newlyinstalled'))

# ---------------------------------------------------------------- after a dot
check('os.pa -> path, with nothing in the script saying what os is', 'path' in complete('os.pa'), complete('os.pa')[:5])
check('j.du -> dump and dumps when the script says import json as j', {'dump', 'dumps'} <= set(complete('j.du', 'import json as j\n')), complete('j.du', 'import json as j\n')[:5])
check('os.path.jo -> join', 'join' in complete('os.path.jo'), complete('os.path.jo')[:5])
check('dumps after from json import dumps: no attribute completion invented for a name that is not a module', complete('zzz.q') == [])

# ---------------------------------------------------------------- the console: names of its own
words = complete('pri', console=True)
check('console: pri -> print (a built-in)', 'print' in words, words[:5])
support._console['mine']['my_value'] = 3
check('console: my_va -> my_value (what was assigned in the console)', 'my_value' in complete('my_va', console=True), complete('my_va', console=True)[:5])
check('console: my_value.rea -> real (the attributes of an object of the console)', 'real' in complete('my_value.rea', console=True), complete('my_value.rea', console=True)[:5])
support._console['mine'].pop('my_value', None)

# ---------------------------------------------------------------- live output of a command: line by line, as it prints
live = support._Live()
seen = []
code = 'import sys, time\nfor i in range(3):\n    print("step", i)\n    sys.stdout.flush()\n    time.sleep(0.4)\n'
done = []


def run():
    pyenv._run([sys.executable, '-c', code], write=live.write)
    done.append(True)


started = time.time()
thread = threading.Thread(target=run)
thread.start()
while thread.is_alive():
    got = live.take(False)
    if got:
        seen.append((round(time.time() - started, 2), got))
    time.sleep(0.05)
thread.join()
rest = live.take(True)
if rest:
    seen.append((round(time.time() - started, 2), rest))
check('a command that prints over a second gives its lines one after another while it runs, not all at the end', len(seen) >= 3 and seen[0][0] < 0.9, seen)
check('... and the lines are whole and in order', ''.join(g for _t, g in seen).replace('\r', '') == 'step 0\nstep 1\nstep 2\n', ''.join(g for _t, g in seen))
check('a half line waits for its end: nothing of "ab" without a newline goes out', (lambda l: (l.write('ab'), l.take(False))[1] == '')(support._Live()))

print('=' * 70)
print('ALL CHECKS PASSED' if not failures else '%d check(s) failed %s' % (len(failures), failures))
sys.exit(1 if failures else 0)
