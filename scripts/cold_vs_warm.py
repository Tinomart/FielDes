'''
Runs the examples with the caches as they are shipped, cold, and warm, and says whether they agree: the check for the class of bug in which a cache hands back what is not
what the program would make now.

    python scripts/cold_vs_warm.py [example ...] [--work FOLDER] [--timeout SECONDS]

Three runs of every example (by default all of examples/*.py), each in its own copy of the examples folder with every cache folder of its own, so that nothing of the
user's is read or written:

    shipped   the examples as they are in the repository -- the caches that are shipped beside the STEP files (the import caches) -- and nothing else
    cold      a copy with every cache deleted: everything is made from the files
    warm      the same folder again, run right after the cold one: everything that the cold run kept is read

Each run is `scripts/run_example.py --digest`: what the script prints (the timing is not compared) and, for every shape it made, a hash of its field at 48 fixed points of
its region.  The three must be the same; a difference is a cache that does not give back what is made now (or a result that is not repeatable), and is listed with the lines
that differ.  The exit code is 1 when anything differs, fails or takes longer than the timeout.  Run it before a release.  FIELDES_DIR (the folder of fieldes.dll) is read as
run_example.py reads it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHES = ('*.fieldes-cache.py', '*.fieldes-cache.trees', '*.fieldes-tessellation')


def clear_import_caches(folder):
    for pattern in CACHES:
        for path in glob.glob(os.path.join(folder, '**', pattern), recursive=True):
            if os.path.isdir(path):
                shutil.rmtree(path, ignore_errors=True)
            else:
                os.remove(path)


def run(examples, name, caches, timeout):
    ''' The lines one example prints, as (lines, seconds, why it failed or None) '''
    env = dict(os.environ)
    for variable, folder in (('FIELDES_FIELD_CACHE_DIR', 'field'), ('FIELDES_RESULT_CACHE_DIR', 'result'), ('FIELDES_RENDER_CACHE_DIR', 'render'),
                             ('FIELDES_SCENE_CACHE_DIR', 'scene')):
        env[variable] = os.path.join(caches, folder)
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    command = [sys.executable, '-X', 'utf8', os.path.join(ROOT, 'scripts', 'run_example.py'), os.path.join(examples, name), '--digest']
    started = time.time()
    try:
        done = subprocess.run(command, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=timeout)
    except subprocess.TimeoutExpired:
        return [], time.time() - started, 'longer than %d s' % timeout
    seconds = time.time() - started
    lines = done.stdout.splitlines()
    if done.returncode != 0:
        tail = [line for line in (done.stdout + done.stderr).splitlines() if line.strip()][-3:]
        return lines, seconds, 'failed: ' + ' | '.join(tail)
    return lines, seconds, None


def comparable(lines):
    ''' What is compared: everything the script prints but the time it took -- the "ok in 1.2 s" line, and the seconds that a result says it took ("(steel, 21.6 s)", "in 3.4 s") '''
    out = []
    for line in lines:
        line = re.sub(r'ok in [\d.]+ s', 'ok in X s', line)
        line = re.sub(r'tessellat(?:ed in|ion kept) \d+(\.\d+)? ?s', 'tessellation X s', line)          # (what the program says about its own cache: made, or kept)
        line = re.sub(r'([,(] ?)\d+(\.\d+)? ?s\b', r'\1X s', line)
        line = re.sub(r'\b(took|in|after) \d+(\.\d+)? ?s\b', r'\1 X s', line)
        out.append(line)
    return out


def main(argv):
    names, work, timeout = [], None, 1800
    args = list(argv[1:])
    while args:
        a = args.pop(0)
        if a == '--work':
            work = args.pop(0)
        elif a == '--timeout':
            timeout = int(args.pop(0))
        elif a in ('-h', '--help'):
            print(__doc__)
            return 0
        else:
            names.append(a if a.endswith('.py') else a + '.py')
    source = os.path.join(ROOT, 'examples')
    if not names:
        names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(source, '*.py')))
    work = os.path.abspath(work or tempfile.mkdtemp(prefix='fieldes-cold-vs-warm-'))
    os.makedirs(work, exist_ok=True)
    for scenario in ('shipped', 'cold'):
        folder = os.path.join(work, scenario)
        shutil.rmtree(folder, ignore_errors=True)
        shutil.copytree(source, os.path.join(folder, 'examples'))
        if scenario == 'cold':
            clear_import_caches(os.path.join(folder, 'examples'))
    print('work folder: %s  (%d examples)' % (work, len(names)))
    bad = 0
    for name in names:
        results = {}
        for scenario, again in (('shipped', False), ('cold', False), ('warm', True)):
            folder = os.path.join(work, 'cold' if scenario == 'warm' else scenario)
            results[scenario] = run(os.path.join(folder, 'examples'), name, os.path.join(folder, 'caches'), timeout)
        times = '  '.join('%s %.0f s' % (k, v[1]) for k, v in results.items())
        problems = ['%s %s' % (k, v[2]) for k, v in results.items() if v[2]]
        if not problems:
            base = comparable(results['cold'][0])
            for scenario in ('shipped', 'warm'):
                other = comparable(results[scenario][0])
                if other != base:
                    problems.append('%s differs from cold:' % scenario)
                    for a, b in zip(base + [''] * len(other), other + [''] * len(base)):
                        if a != b:
                            problems.append('    cold:   %s\n    %s: %s' % (a[:200], scenario, b[:200]))
        if problems:
            bad += 1
            print('DIFFERENT  %-34s %s' % (name, times))
            for p in problems:
                print('    ' + p)
        else:
            print('same       %-34s %s' % (name, times))
        sys.stdout.flush()
    print('%d of %d examples agree' % (len(names) - bad, len(names)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
