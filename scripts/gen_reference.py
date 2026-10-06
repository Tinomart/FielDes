'''
Writes docs/reference.md: every function and class of the FielDes library, with its signature and its
documentation, from the library itself.  Run it after changing a docstring:

    python scripts/gen_reference.py

(needs the built fieldes.dll: set FIELDES_DIR to the folder holding it).
'''
import importlib, inspect, os, sys, textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'python'))

MODULES = [
    ('shapes', 'Primitive shapes', 'fieldes.stdlib.shapes'),
    ('csg', 'Combining shapes (CSG)', 'fieldes.stdlib.csg'),
    ('transforms', 'Moving, rotating, scaling, deforming', 'fieldes.stdlib.transforms'),
    ('text', 'Text', 'fieldes.stdlib.text'),
    ('cad_import', 'Importing STEP models', 'fieldes.stdlib.cad_import'),
    ('tessellated_import', 'Importing STEP models exactly (almost all free-form)', 'fieldes.stdlib.tessellated_import'),
    ('mesh_import', 'Importing triangle meshes', 'fieldes.stdlib.mesh_import'),
    ('handles', 'Handles: editing shapes by dragging', 'fieldes.stdlib.handles'),
    ('points', 'Points and surfaces', 'fieldes.stdlib.points'),
    ('fields', 'Fields', 'fieldes.stdlib.fields'),
    ('regression', 'Regressions and data', 'fieldes.stdlib.regression'),
    ('surfaces', 'Surfaces and offsets', 'fieldes.stdlib.surfaces'),
    ('lattices', 'Lattices', 'fieldes.stdlib.lattices'),
    ('conformal', 'Lattices that follow a surface', 'fieldes.stdlib.conformal'),
    ('selection', 'Selecting surfaces', 'fieldes.stdlib.selection'),
    ('fea', 'Structural analysis and topology optimization', 'fieldes.stdlib.fea'),
    ('boundary_conditions', 'Seeing the boundary conditions', 'fieldes.stdlib.boundary_conditions'),
    ('thermal', 'Thermal analysis and thermal topology optimization', 'fieldes.stdlib.thermal'),
    ('fluid', 'Fluid flow analysis', 'fieldes.stdlib.fluid'),
    ('content_cache', 'Caching', 'fieldes.stdlib.content_cache'),
    ('render_cache', 'Keeping rendered meshes (render cache)', 'fieldes.stdlib.render_cache'),
]


def signature(obj):
    try:
        return str(inspect.signature(obj))
    except (TypeError, ValueError):
        return '(...)'


def doc_of(obj):
    d = inspect.getdoc(obj) or ''
    return d.strip()


def describe(name, obj, level='###'):
    out = []
    kind = 'class' if inspect.isclass(obj) else 'function'
    out.append('%s `%s%s`' % (level, name, signature(obj) if kind == 'function' else ''))
    d = doc_of(obj)
    out.append('')
    out.append(d if d else '*(no description yet)*')
    out.append('')
    if kind == 'class':
        for mname, m in inspect.getmembers(obj):
            if mname.startswith('_') or not callable(m):
                continue
            if getattr(m, '__qualname__', '').split('.')[0] != obj.__name__:
                continue
            out.append('#### `%s.%s%s`' % (name, mname, signature(m)))
            out.append('')
            out.append(doc_of(m) or '*(no description yet)*')
            out.append('')
    return out


def main():
    lines = ['# Library reference', '',
             'Every public function and class of the FielDes library, generated from its docstrings',
             '(`python scripts/gen_reference.py`).  `from fieldes import *` brings all of them in.', '']
    lines.append('Contents: ' + ' | '.join('[%s](#%s)' % (title, title.lower().replace(' ', '-').replace(':', '')
                                                          .replace(',', '').replace('(', '').replace(')', ''))
                                             for _, title, _ in MODULES))
    lines.append('')
    seen = set()
    for short, title, modname in MODULES:
        mod = importlib.import_module(modname)
        lines.append('## ' + title)
        lines.append('')
        intro = inspect.getdoc(mod)
        if intro:
            lines.append(intro)
            lines.append('')
        names = [n for n in dir(mod) if not n.startswith('_')]
        for n in sorted(names):
            obj = getattr(mod, n)
            if not (inspect.isfunction(obj) or inspect.isclass(obj)):
                continue
            if getattr(obj, '__module__', None) != modname or (modname, n) in seen:
                continue
            seen.add((modname, n))
            lines.extend(describe(n, obj))
    with open(os.path.join(ROOT, 'docs', 'reference.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines).rstrip() + '\n')
    print('wrote docs/reference.md:', len(seen), 'entries')


if __name__ == '__main__':
    main()
