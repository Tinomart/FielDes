'''
Runs a FielDes script without the application: prints what it prints, reports errors and the time.

    python scripts/run_example.py examples/05_static_analysis.py [--digest]

The script runs with its own folder as the working directory (so "step/Bracket.step" resolves).  The
viewport calls (view.set_bounds, ...) are remembered but not drawn, and var(3) is the plain number;
the last expression is not displayed.  FIELDES_DIR names the folder holding fieldes.dll (the built
application's folder); the Python package is found next to this script's folder.  --digest also prints a line for every shape the script made: its name and a hash
of its field at 48 fixed points (scripts/cold_vs_warm.py compares these lines between runs).
'''
import ast
import os
import sys
import time
import traceback

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'python'))
for candidate in (os.environ.get('FIELDES_DIR'), r'C:\dev\fieldes-build\app\Release',
                  os.path.join(ROOT, 'dist', 'FielDes')):
    if candidate and os.path.isdir(candidate):
        os.environ.setdefault('FIELDES_DIR', candidate)
        if hasattr(os, 'add_dll_directory'):
            os.add_dll_directory(candidate)
        break


def main(argv):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors='replace')
        except (AttributeError, ValueError):
            pass
    digest = '--digest' in argv
    argv = [a for a in argv if a != '--digest']
    if len(argv) != 2:
        print(__doc__)
        return 2
    path = os.path.abspath(argv[1])
    os.chdir(os.path.dirname(path))
    source = open(path, encoding='utf-8').read()
    tree = ast.parse(source, path)

    from fieldes import Shape
    namespace = {'__name__': '__main__', '__file__': path, 'var': lambda x: x}
    # (the custom blocks of the blocks folder are there in every script, as they are in the application)
    from fieldes import blocks
    namespace.update(blocks.namespace())
    for file, why in blocks.errors():
        print('custom block file {}: {}'.format(os.path.basename(file), why))
    started = time.time()
    try:
        from fieldes import made_from
        for node in tree.body:
            code = compile(ast.Module([node], []), path, 'exec')
            inputs = made_from.reads(node, namespace)
            exec(code, namespace)
            made_from.note(node, namespace, inputs)
    except Exception:
        traceback.print_exc()
        print('FAILED after %.1f s' % (time.time() - started))
        return 1
    from fieldes import view
    print('ok in %.1f s; view settings: %s' % (time.time() - started, getattr(view, 'settings', {})))
    if digest:
        print_digests(namespace, getattr(view, 'settings', {}))
    return 0


def print_digests(namespace, settings):
    ''' One line for every shape the script made: its name and a hash of its field at 48 fixed points of the view's region (to four significant digits: a result that
        comes back from a cache and one that was solved again are the same to that, and a stale cache is not).  scripts/cold_vs_warm.py compares these lines '''
    import hashlib
    from fieldes import Shape
    (x0, y0, z0), (x1, y1, z1) = settings.get('bounds', ((-50, -50, -50), (50, 50, 50)))
    points = []
    state = 12345
    for _ in range(48):
        values = []
        for _ in range(3):
            state = (state * 1103515245 + 12345) % 2147483648
            values.append(state / 2147483648.0)
        points.append((x0 + (x1 - x0) * values[0], y0 + (y1 - y0) * values[1], z0 + (z1 - z0) * values[2]))
    import ctypes
    from fieldes.ffi import lib
    flat = (ctypes.c_float * (3 * len(points)))(*[c for p in points for c in p])
    for name in sorted(namespace):
        value = namespace[name]
        if name.startswith('_') or not isinstance(value, Shape):
            continue
        try:
            out = (ctypes.c_float * len(points))()
            lib.libfive_tree_eval_points(value.ptr, flat, len(points), out)           # (all the points in one call: a call for each builds the evaluator again)
            numbers = ['%.4g' % v for v in out]
            print('digest %s %s' % (name, hashlib.sha1(' '.join(numbers).encode()).hexdigest()[:16]))
        except Exception as error:                        # (a shape that has no field to read -- a surface, a condition -- is not one to compare)
            print('digest %s unreadable: %s' % (name, type(error).__name__))


if __name__ == '__main__':
    sys.exit(main(sys.argv))
