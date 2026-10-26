'''
Checks of Settings > Units (python/fieldes/units.py and what uses it): every conversion against a number that is known exactly (an inch is 25.4 mm, a pound-force
4.4482216152605 N, a psi 6894.757 Pa), the units that files are written and read in (STL, 3MF, DXF, FBX, glTF, a mesh of elements), and the key numbers of a result
in the units chosen.

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/units_checks.py
It prints PASS or FAIL for every check, and exits with 1 when one failed (a few seconds).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import time
import traceback
import zipfile

from fieldes import *
from fieldes import units

failures = []
t_all = time.time()


def check(label, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + detail) if detail else ''))
    sys.stdout.flush()
    if not ok:
        failures.append(label)


def near(value, exact, tolerance=1e-9):
    return abs(value - exact) <= tolerance * max(1.0, abs(exact))


def run(label, fn):
    try:
        return fn()
    except Exception:
        check(label + ' (no exception)', False, traceback.format_exc(limit=3).replace('\n', ' | '))
        return None


print('--- the conversions')
units.configure()
check('the defaults are mm, N, MPa, kg, C', units.current() == {'length': 'mm', 'force': 'N', 'stress': 'MPa', 'mass': 'kg', 'temperature': 'C'})
check('nothing is changed at the defaults', units.convert(12.5, 'mm') == (12.5, 'mm') and units.convert(3.0, 'MPa') == (3.0, 'MPa') and units.convert(20.0, 'C')[1] == '°C')

units.configure({'length': 'in', 'force': 'lbf', 'stress': 'psi', 'mass': 'lb', 'temperature': 'F'})
v, u = units.convert(25.4, 'mm')
check('25.4 mm is 1 in', near(v, 1.0) and u == 'in', '%r %r' % (v, u))
v, u = units.convert(25.4 ** 3, 'mm^3')
check('25.4^3 mm^3 is 1 in^3', near(v, 1.0) and u == 'in^3', '%r %r' % (v, u))
v, u = units.convert(25.4 ** 2, 'mm^2')
check('25.4^2 mm^2 is 1 in^2', near(v, 1.0) and u == 'in^2', '%r %r' % (v, u))
v, u = units.convert(25.4, 'mm/s')
check('25.4 mm/s is 1 in/s', near(v, 1.0) and u == 'in/s', '%r %r' % (v, u))
v, u = units.convert(25.4 ** 3, 'mm^3/s')
check('25.4^3 mm^3/s is 1 in^3/s', near(v, 1.0) and u == 'in^3/s', '%r %r' % (v, u))
v, u = units.convert(4.4482216152605, 'N')
check('4.4482216152605 N is 1 lbf', near(v, 1.0) and u == 'lbf', '%r %r' % (v, u))
v, u = units.convert(6.894757293168361, 'MPa')
check('6.894757 MPa is 1000 psi', near(v, 1000.0) and u == 'psi', '%r %r' % (v, u))
v, u = units.convert(0.45359237, 'kg')
check('0.45359237 kg is 1 lb', near(v, 1.0) and u == 'lb', '%r %r' % (v, u))
v, u = units.convert(100.0, 'C')
check('100 C is 212 F', near(v, 212.0) and u == '°F', '%r %r' % (v, u))
v, u = units.convert(4.4482216152605 * 25.4, 'N mm')
check('1 lbf in is 112.98 N mm', near(v, 1.0) and u == 'lbf in', '%r %r' % (v, u))
check('a count, a time and a frequency stay as they are', units.convert(5, '') == (5, '') and units.convert(2.5, 's') == (2.5, 's') and units.convert(50.0, 'Hz') == (50.0, 'Hz'))

units.configure({'length': 'm', 'stress': 'GPa', 'temperature': 'K', 'force': 'kN', 'mass': 't'})
v, u = units.convert(1000.0, 'mm')
check('1000 mm is 1 m', near(v, 1.0) and u == 'm')
v, u = units.convert(210000.0, 'MPa')
check('210000 MPa is 210 GPa', near(v, 210.0) and u == 'GPa')
v, u = units.convert(0.0, 'C')
check('0 C is 273.15 K', near(v, 273.15) and u == 'K')
v, u = units.convert(1000.0, 'N')
check('1000 N is 1 kN', near(v, 1.0) and u == 'kN')
v, u = units.convert(1000.0, 'kg')
check('1000 kg is 1 t', near(v, 1.0) and u == 't')
units.configure({'length': 'um'})
v, u = units.convert(1.0, 'mm')
check('1 mm is 1000 µm', near(v, 1000.0) and u == 'µm')

units.configure({'length': 'in', 'force': 'lbf', 'stress': 'psi'})
text, unit = units.convert_text('(4.4482216152605, 8.896443230521, 0)', 'N')
check('a vector is converted component by component', text == '(1, 2, 0)' and unit == 'lbf', '%r %r' % (text, unit))
check('a text that is no number stays, with its own unit', units.convert_text('yes', 'N') == ('yes', 'N'))
table = units.table()
check('the table holds every unit of the program', set(table) == set(units.NATIVE) and all(r['scale'] > 0 and r['label'] for r in table.values()), str(sorted(table)))
check('the table says how a stress is shown', near(table['MPa']['scale'], 1.0 / 0.006894757293168361) and table['MPa']['label'] == 'psi')
check('the options name every unit', all(len(v) >= 3 for v in units.options().values()) and units.options()['temperature'][0] == ('C', '°C'))

try:
    units.configure({'length': 'furlong'})
    check('an unknown unit is refused', False)
except ValueError as e:
    check('an unknown unit is refused', 'furlong' in str(e), str(e))
try:
    units.configure({'colour': 'red'})
    check('an unknown quantity is refused', False)
except ValueError as e:
    check('an unknown quantity is refused', 'colour' in str(e), str(e))
units.configure()
check('configure() resets', units.current() == units.DEFAULTS)

print('--- the numbers of a result')


class _Result:
    pass


r = _Result()
r.max_von_mises = 6.894757293168361
r.max_displacement = 25.4
r.reaction = (4.4482216152605, 0.0, 0.0)
units.configure({'length': 'in', 'force': 'lbf', 'stress': 'psi'})
rows = {label: (text, unit) for label, text, unit in result_summary(r)}
check('the stress of a result is shown in psi', rows.get('maximum von Mises stress') == ('1000', 'psi'), str(rows))
check('the displacement of a result is shown in in', rows.get('maximum displacement') == ('1', 'in'), str(rows))
check('the reaction of a result is shown in lbf', rows.get('support reaction (x, y, z)') == ('(1, 0, 0)', 'lbf'), str(rows))
native = {label: (text, unit) for label, text, unit in result_summary(r, native=True)}
check('native=True keeps the units of the program (the card converts them itself)', native.get('maximum von Mises stress') == ('6.895', 'MPa') and native.get('maximum displacement') == ('25.4', 'mm'), str(native))
check('the key numbers of the card are the native ones', all(unit in ('MPa', 'mm', 'N', '') for _, _, unit in key_results(r)), str(key_results(r)))
units.configure()

print('--- the files')
folder = tempfile.mkdtemp(prefix='units_checks_')


def stl_extent(path):
    xs = []
    with open(path, 'r', encoding='ascii') as f:
        for line in f:
            if 'vertex' in line:
                xs.append([float(v) for v in line.split()[1:4]])
    return [min(p[i] for p in xs) for i in range(3)], [max(p[i] for p in xs) for i in range(3)]


cube = box((0, 0, 0), (25.4, 25.4, 25.4))

units.configure()
mm_path = run('export_mesh at the defaults', lambda: export_mesh(cube, os.path.join(folder, 'mm.stl'), ascii=True))
if mm_path:
    lo, hi = stl_extent(mm_path)
    check('an STL at the defaults is in millimetres', near(hi[0] - lo[0], 25.4, 0.02), 'extent %.3f' % (hi[0] - lo[0]))

units.configure({'length': 'in'})
in_path = run('export_mesh in inches', lambda: export_mesh(cube, os.path.join(folder, 'in.stl'), ascii=True))
if in_path:
    lo, hi = stl_extent(in_path)
    check('an STL is in inches when inches are chosen', near(hi[0] - lo[0], 1.0, 0.02), 'extent %.4f' % (hi[0] - lo[0]))
cm_path = run('export_mesh with unit=', lambda: export_mesh(cube, os.path.join(folder, 'cm.stl'), ascii=True, unit='cm'))
if cm_path:
    lo, hi = stl_extent(cm_path)
    check('unit= overrides the setting', near(hi[0] - lo[0], 2.54, 0.02), 'extent %.4f' % (hi[0] - lo[0]))

p3 = run('export_mesh 3mf in inches', lambda: export_mesh(cube, os.path.join(folder, 'in.3mf')))
if p3:
    model = zipfile.ZipFile(p3).read('3D/3dmodel.model').decode('utf-8')
    check('a 3MF says inch', 'unit="inch"' in model)
units.configure({'length': 'm'})
p3 = run('export_mesh 3mf in metres', lambda: export_mesh(cube, os.path.join(folder, 'm.3mf')))
if p3:
    model = zipfile.ZipFile(p3).read('3D/3dmodel.model').decode('utf-8')
    xs = [float(v) for v in re.findall(r' x="([-0-9.e+]+)"', model)]
    check('a 3MF says meter and holds metres', 'unit="meter"' in model and near(max(xs) - min(xs), 0.0254, 0.02), 'extent %.5f' % (max(xs) - min(xs)))
units.configure({'length': 'in'})
pd = run('export_mesh dxf in inches', lambda: export_mesh(cube, os.path.join(folder, 'in.dxf')))
if pd:
    text = open(pd, encoding='ascii').read().replace('\r\n', '\n')
    check('a DXF says inches ($INSUNITS 1)', '$INSUNITS\n70\n1\n' in text)
pf = run('export_mesh fbx in inches', lambda: export_mesh(cube, os.path.join(folder, 'in.fbx')))
if pf:
    text = open(pf, encoding='ascii').read()
    check('an FBX says 2.54 centimetres per unit', re.search(r'"UnitScaleFactor", "double", "Number", "",2\.54\b', text) is not None)


def glb_max_x(path):
    data = open(path, 'rb').read()
    n = struct.unpack('<I', data[12:16])[0]
    doc = json.loads(data[20:20 + n].decode('utf-8'))
    return doc['accessors'][0]['max'][0]


units.configure({'length': 'in'})
pg_in = run('export_mesh glb in inches', lambda: export_mesh(cube, os.path.join(folder, 'in.glb')))
units.configure()
pg_mm = run('export_mesh glb at the defaults', lambda: export_mesh(cube, os.path.join(folder, 'mm.glb')))
if pg_in and pg_mm:
    check('a glTF is in metres whatever is chosen', near(glb_max_x(pg_in), glb_max_x(pg_mm), 1e-6) and 0.02 < glb_max_x(pg_in) < 0.03, '%.5f %.5f' % (glb_max_x(pg_in), glb_max_x(pg_mm)))

# a mesh file that does not say is read in the unit chosen
if in_path:
    units.configure({'length': 'in'})
    parts = run('import_model of the inch STL in inches', lambda: import_model(in_path))
    if parts:
        (shape, (lo, hi)) = parts[0]
        check('the STL is read in inches when inches are chosen', near(hi[0] - lo[0], 25.4, 0.02), 'extent %.3f' % (hi[0] - lo[0]))
    parts = run('import_model with file_units', lambda: import_model(in_path, file_units='mm'))
    if parts:
        (shape, (lo, hi)) = parts[0]
        check('file_units= beats the setting', near(hi[0] - lo[0], 1.0, 0.05), 'extent %.3f' % (hi[0] - lo[0]))
    units.configure()
    parts = run('import_model of the inch STL at the defaults', lambda: import_model(in_path))
    if parts:
        (shape, (lo, hi)) = parts[0]
        check('the same file is read in millimetres at the defaults (the setting is part of the cache key)', near(hi[0] - lo[0], 1.0, 0.05), 'extent %.3f' % (hi[0] - lo[0]))

# a mesh of elements: the coordinates of the nodes
def msh_extent(path):
    lines = open(path, encoding='ascii').read().split('\n')
    i = lines.index('$Nodes') + 2
    pts = []
    while lines[i] != '$EndNodes':
        parts = lines[i].split()
        pts.append([float(v) for v in parts[1:4]])
        i += 1
    return [max(p[k] for p in pts) - min(p[k] for p in pts) for k in range(3)]


units.configure({'length': 'm'})
mesh = run('mesh_volume of the cube', lambda: mesh_volume(cube, element_size=12.7))
if mesh is not None:
    path = run('Mesh.export in metres', lambda: mesh.export(os.path.join(folder, 'cube.msh')))
    if path:
        check('a mesh of elements is written in metres when metres are chosen', near(msh_extent(path)[0], 0.0254, 1e-6), 'extent %.6f' % msh_extent(path)[0])
    units.configure()
    path = run('Mesh.export at the defaults', lambda: mesh.export(os.path.join(folder, 'cube_mm.msh')))
    if path:
        check('and in millimetres at the defaults', near(msh_extent(path)[0], 25.4, 1e-6), 'extent %.4f' % msh_extent(path)[0])
    path = run('Mesh.export with scale', lambda: mesh.export(os.path.join(folder, 'cube_scaled.msh'), scale=0.001))
    if path:
        check('scale= beats the setting', near(msh_extent(path)[0], 0.0254, 1e-6))

units.configure()
shutil.rmtree(folder, ignore_errors=True)
print('--- done in {:.1f} s'.format(time.time() - t_all))
if failures:
    print('FAILED: ' + ', '.join(failures))
    sys.exit(1)
print('ALL CHECKS PASS')
