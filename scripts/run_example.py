'''
Runs a FielDes script without the application: prints what it prints, reports errors and the time.

    python scripts/run_example.py examples/05_static_analysis.py

The script runs with its own folder as the working directory (so "step/Bracket.step" resolves).  The
viewport calls (view.set_bounds, ...) are remembered but not drawn, and var(3) is the plain number;
the last expression is not displayed.  FIELDES_DIR names the folder holding fieldes.dll (the built
application's folder); the Python package is found next to this script's folder.
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
        for node in tree.body:
            code = compile(ast.Module([node], []), path, 'exec')
            exec(code, namespace)
    except Exception:
        traceback.print_exc()
        print('FAILED after %.1f s' % (time.time() - started))
        return 1
    from fieldes import view
    print('ok in %.1f s; view settings: %s' % (time.time() - started, getattr(view, 'settings', {})))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
