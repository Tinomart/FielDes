'''
Units: what the program shows its numbers in, and what the files it writes are in.

A script and the solvers have ONE set of units and Settings > Units does not change it: lengths in millimetres, forces in newtons, stresses and pressures in
megapascals, masses in kilograms, temperatures in degrees Celsius (so that a script means the same on every machine).  The choice made there is about the
other side of the numbers:

    shown      the ruler, the key numbers of a result and its colour bars, the value under the cursor, the work plane and the radius of a surface selection
    written    export_mesh() and Mesh.export() write their coordinates in the length unit (`scale=` of Mesh.export and `unit=` of export_mesh override it);
               the formats that state their unit (3MF, DXF, FBX) say it
    read       a mesh file that does not say what its numbers mean (STL, OBJ, PLY, OFF) is read in the length unit, unless import_model(..., file_units=...)
               says otherwise

Files of the analyses (export_fea, export_results, export_field) stay in millimetres, newtons and megapascals: a solver reads a consistent set, and the
file says nothing about its units.  The key numbers of export_results(..., '.txt') are in the shown units, each with its unit written next to it.

    from fieldes import units
    units.configure({'length': 'in', 'stress': 'psi'})     # the rest stay at their defaults
    units.convert(25.4, 'mm')                              # (1.0, 'in')
    units.convert(6.894757, 'MPa')                         # (1000.0, 'psi')

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import re

__all__ = ['DEFAULTS', 'LENGTH', 'FORCE', 'STRESS', 'MASS', 'TEMPERATURE', 'configure', 'current', 'options', 'table', 'conversion', 'convert', 'convert_text',
           'shown_name', 'length_mm', 'export_unit', 'export_scale', 'import_mm', 'check_length', 'threemf_unit', 'dxf_unit']

# Millimetres in one unit of length
LENGTH = {'mm': 1.0, 'cm': 10.0, 'm': 1000.0, 'in': 25.4, 'ft': 304.8, 'um': 0.001}
# Newtons in one unit of force
FORCE = {'N': 1.0, 'kN': 1000.0, 'lbf': 4.4482216152605, 'kgf': 9.80665}
# Megapascals in one unit of stress or pressure
STRESS = {'MPa': 1.0, 'GPa': 1000.0, 'kPa': 0.001, 'Pa': 1e-6, 'bar': 0.1, 'psi': 0.006894757293168361, 'ksi': 6.894757293168361}
# Kilograms in one unit of mass
MASS = {'kg': 1.0, 'g': 0.001, 't': 1000.0, 'lb': 0.45359237}
# A temperature in degrees Celsius -> shown = celsius * scale + offset
TEMPERATURE = {'C': (1.0, 0.0), 'K': (1.0, 273.15), 'F': (1.8, 32.0)}

DEFAULTS = {'length': 'mm', 'force': 'N', 'stress': 'MPa', 'mass': 'kg', 'temperature': 'C'}

# How a unit is written when it is shown
_SHOWN = {'um': 'µm', 'C': '°C', 'F': '°F'}

_TABLES = {'length': LENGTH, 'force': FORCE, 'stress': STRESS, 'mass': MASS, 'temperature': TEMPERATURE}

# The units the program works in (the ones the solvers and the scripts have): each is converted to the shown ones
NATIVE = ('mm', 'mm^2', 'mm^3', 'mm/s', 'mm^3/s', 'MPa', 'N', 'N mm', 'kg', 'C')

# What 3MF and DXF call the units they can state
_THREEMF = {'um': 'micron', 'mm': 'millimeter', 'cm': 'centimeter', 'm': 'meter', 'in': 'inch', 'ft': 'foot'}
_DXF = {'in': 1, 'ft': 2, 'mm': 4, 'cm': 5, 'm': 6, 'um': 13}

_active = dict(DEFAULTS)


def shown_name(unit):
    ''' A unit as it is written on a screen: um is the micrometre sign, C and F carry a degree sign '''
    return _SHOWN.get(unit, unit)


def options():
    ''' What can be chosen: {quantity: [(name, how it is shown)]} -- the order is the order of the menus '''
    return {q: [(name, shown_name(name)) for name in t] for q, t in _TABLES.items()}


def current():
    ''' The units chosen: {'length': 'mm', 'force': 'N', 'stress': 'MPa', 'mass': 'kg', 'temperature': 'C'} '''
    return dict(_active)


def configure(choice=None):
    ''' Chooses the units numbers are shown and files are written in.  Whatever `choice` leaves out is its default (so configure() resets).
        Raises ValueError for a quantity or a unit that is not known.  Returns the choice '''
    new = dict(DEFAULTS)
    for quantity, name in (choice or {}).items():
        if quantity not in DEFAULTS:
            raise ValueError('units: {!r} is not a quantity ({})'.format(quantity, ', '.join(DEFAULTS)))
        if name in (None, ''):
            continue
        if name not in _TABLES[quantity]:
            raise ValueError('units: {!r} is not a unit of {} ({})'.format(name, quantity, ', '.join(_TABLES[quantity])))
        new[quantity] = name
    _active.clear()
    _active.update(new)
    return current()


_LENGTH_UNIT = re.compile(r'^mm(?:\^([23]))?(/s)?$')


def conversion(native):
    ''' How a number in a unit the program works in is shown now: (scale, offset, label) -- shown = number * scale + offset -- or None for a unit that is
        not converted (a count, a time, a frequency).  The units that are converted are those of NATIVE: lengths (mm, mm^2, mm^3, mm/s, mm^3/s), stress and
        pressure (MPa), force (N), the product of force and length (N mm), mass (kg) and temperature (C) '''
    u = (native or '').strip()
    length, force, stress, mass, temperature = (_active[q] for q in ('length', 'force', 'stress', 'mass', 'temperature'))
    m = _LENGTH_UNIT.match(u)
    if m:
        power = int(m.group(1) or 1)
        label = shown_name(length) + ('^%d' % power if power > 1 else '') + (m.group(2) or '')
        return LENGTH[length] ** -power, 0.0, label
    if u == 'MPa':
        return 1.0 / STRESS[stress], 0.0, shown_name(stress)
    if u == 'N':
        return 1.0 / FORCE[force], 0.0, shown_name(force)
    if u == 'N mm':
        return 1.0 / (FORCE[force] * LENGTH[length]), 0.0, '%s %s' % (shown_name(force), shown_name(length))
    if u == 'kg':
        return 1.0 / MASS[mass], 0.0, shown_name(mass)
    if u == 'C':
        scale, offset = TEMPERATURE[temperature]
        return scale, offset, shown_name(temperature)
    return None


def table():
    ''' conversion() of every unit of NATIVE, as {unit: {'scale', 'offset', 'label'}}: what the window is given to convert what it draws '''
    out = {}
    for u in NATIVE:
        scale, offset, label = conversion(u)
        out[u] = {'scale': scale, 'offset': offset, 'label': label}
    return out


def convert(value, native):
    ''' (the number as it is shown, the unit as it is shown) for a number in a unit the program works in; the number itself for a unit that is not converted '''
    c = conversion(native)
    if c is None:
        return value, native
    return value * c[0] + c[1], c[2]


def convert_text(text, native):
    ''' The same for a number written as text -- '12.5', or '(1, 2, 3)' for a vector --: (text, unit) as they are shown.  A text that is no number stays as it is,
        with its own unit '''
    c = conversion(native)
    if c is None:
        return text, native
    scale, offset, label = c
    try:
        if text.startswith('('):
            parts = [float(v) for v in text.strip('()').split(',')]
            return '(' + ', '.join('%.4g' % (v * scale + offset) for v in parts) + ')', label
        return '%.4g' % (float(text) * scale + offset), label
    except ValueError:
        return text, native


def length_mm():
    ''' Millimetres in one unit of the length chosen '''
    return LENGTH[_active['length']]


def export_unit():
    ''' The name of the length unit files are written in (the one chosen) '''
    return _active['length']


def export_scale():
    ''' The factor the coordinates of a file are multiplied by to be in the length unit chosen: 1 for millimetres, 0.001 for metres '''
    return 1.0 / LENGTH[_active['length']]


def import_mm():
    ''' Millimetres in the unit a mesh file is read in when it does not say (the length unit chosen) '''
    return LENGTH[_active['length']]


def check_length(unit):
    ''' A name of a length unit; raises ValueError when it is not one '''
    if unit not in LENGTH:
        raise ValueError('unknown length unit {!r}; use one of {}'.format(unit, ', '.join(LENGTH)))
    return unit


def threemf_unit(unit):
    ''' What a 3MF file calls a length unit ('millimeter', 'inch' ...) '''
    return _THREEMF[check_length(unit)]


def dxf_unit(unit):
    ''' The $INSUNITS code of a DXF file for a length unit (4 millimetres, 1 inches ...) '''
    return _DXF[check_length(unit)]
