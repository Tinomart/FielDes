import inspect, sys
import fieldes.stdlib as S
import fieldes.shape as sh
seen = {}
for modname in ['csg','shapes','text','transforms','handles','render_cache','cad_import','tessellated_import','mesh_import','fea','boundary_conditions','selection','thermal','fluid','fields','regression','surfaces','lattices','conformal']:
    mod = sys.modules.get('fieldes.stdlib.' + modname)
    if mod is None:
        __import__('fieldes.stdlib.' + modname); mod = sys.modules['fieldes.stdlib.' + modname]
    names = []
    for n, f in vars(mod).items():
        if n.startswith('_') or not inspect.isfunction(f) or f.__module__ != mod.__name__:
            continue
        try:
            sig = str(inspect.signature(f))
        except Exception:
            sig = '(?)'
        names.append('%s%s' % (n, sig[:70]))
    print('== %s (%d)' % (modname, len(names)))
    for x in names:
        print('  ' + x)
