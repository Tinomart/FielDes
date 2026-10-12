'''
FielDes apps: a script that people who never open the editor can run.

An engineer writes the script once and declares what the person who runs it may choose -- a radius, a model file, where to save the
result.  FielDes shows those inputs as a form (a window of its own, with a Run button, a log and the files that were made), so an
artist or a designer opens the app, fills in the form and presses Run.  The script is an ordinary FielDes script:

    from fieldes import *

    app.title('Bracket maker', 'Makes the bracket for a pipe and saves it as a mesh')
    radius = app.number('Radius', 5, min=1, max=20, unit='mm')
    material = app.choice('Material', ['steel', 'aluminium'])
    model = app.file('Model', filter='STEP files (*.step *.stp)')
    target = app.save_as('Export path', 'bracket.stl')

    part = import_model(model)
    ...
    export_mesh(part, target)

In the editor the inputs are just their defaults: `app.number('Radius', 5)` is 5, so the script runs as it always did, and the script is
tried and changed there.  File > Export as app... bundles it with the custom blocks it uses and the files it needs into one
`.fieldesapp` file; opening that file (a double click) shows the form.  The same file runs without a window:

    FielDes run bracket.fieldesapp --Radius 7 --Material aluminium

Every input is written with a plain value -- a number, a text, a list -- at the top of the script, because the form is read from the
text of the script (it is there before the script has run).  The name of an input is what the form shows; `--Radius`, `--radius` and
`--RADIUS` are the same option, and so are `Model path`, `ModelPath` and `model_path`.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import os
import re

__all__ = ['number', 'text', 'flag', 'choice', 'file', 'save_as', 'folder', 'title', 'asset', 'output', 'in_app', 'running', 'viewport']


class InputError(ValueError):
    ''' A value that does not fit its input: the message says which input and why '''
    def __init__(self, label, why):
        ValueError.__init__(self, '%s: %s' % (label, why))
        self.label = label
        self.why = why


# The parameters of every kind of input, in order: the form is read from the text of the script with these (see app_package.inputs_of)
SIGNATURES = {
    'number': ('name', 'default', 'min', 'max', 'step', 'unit', 'help'),
    'text': ('name', 'default', 'help'),
    'flag': ('name', 'default', 'help'),
    'choice': ('name', 'options', 'default', 'help'),
    'file': ('name', 'default', 'filter', 'help'),
    'save_as': ('name', 'default', 'filter', 'help'),
    'folder': ('name', 'default', 'help'),
}
DEFAULTS = {
    'number': {'default': 0, 'min': None, 'max': None, 'step': None, 'unit': '', 'help': ''},
    'text': {'default': '', 'help': ''},
    'flag': {'default': False, 'help': ''},
    'choice': {'default': None, 'help': ''},
    'file': {'default': '', 'filter': '', 'help': ''},
    'save_as': {'default': 'result', 'filter': '', 'help': ''},
    'folder': {'default': '', 'help': ''},
}

# What the host (the runner, `FielDes run`) tells a run: the values of the inputs by key, the folder of the unpacked app, the folder a
# relative output path means.  None while the script is run in the editor, where every input is its default
_state = {'values': None, 'root': None, 'outputs': None, 'assets': {}, 'running': False, 'fields': []}
_seen = {}          # key -> input as this run declared it (the host compares it with what the text of the script says)
_outputs = []       # [{'label', 'path'}]: the files this run says it makes (save_as inputs and output() calls), in order


def key_of(label):
    ''' The name an option goes by: letters and digits of the label, in lower case ('Model path' -> 'modelpath') '''
    return re.sub(r'[\W_]+', '', str(label).casefold())


def begin(values, root=None, outputs=None, assets=None, running=True):
    ''' (the host) A run as an app starts: `values` are the inputs by key, already checked (see app_package.check).  `running` is False
        for a run that only makes the picture of the app's viewport (see app.viewport): the person has not pressed Run '''
    _state['values'] = dict(values)
    _state['root'] = root
    _state['outputs'] = outputs
    _state['assets'] = dict(assets or {})
    _state['running'] = bool(running)
    _state['fields'] = []
    _seen.clear()
    del _outputs[:]


def end():
    ''' (the host) Back to the editor's way: every input is its default '''
    _state['values'] = None
    _state['root'] = None
    _state['outputs'] = None
    _state['assets'] = {}
    _state['running'] = False


def in_app():
    ''' True when the script is run as an app (by the app window or `FielDes run`), False in the editor.  Write files only when it is: an
        edit of the script runs it again and again in the editor, and it should only show the result there:

            if app.in_app():
                export_mesh(part, target) '''
    return _state['values'] is not None


def running():
    ''' True when the person pressed Run (or the app runs from the command line): the work is to be done.  False while the script only makes the
        picture of the app's viewport -- when the app is opened, an input changes, a model was dragged -- and in the editor.  Put what takes
        long, and what writes files, under it:

            if app.running():
                result = static_analysis(part, ...)
                export_results(result, target) '''
    return bool(_state['running'])


def _is_whole(v):
    return isinstance(v, int) and not isinstance(v, bool)


def _bool(raw, label):
    if isinstance(raw, bool):
        return raw
    s = str(raw).strip().casefold()
    if s in ('1', 'true', 'yes', 'y', 'on', 'ja', 'oui', 'si', 'sí'):
        return True
    if s in ('0', 'false', 'no', 'n', 'off', 'nein', 'non', ''):
        return False
    raise InputError(label, 'say yes or no (it is %r)' % (raw,))


def spec_of(kind, args, kwargs):
    ''' The input a call declares: {'kind', 'label', ...} with every parameter of the kind (see SIGNATURES) '''
    names = SIGNATURES[kind]
    if len(args) > len(names):
        raise TypeError('app.%s() takes at most %d arguments (%s)' % (kind, len(names), ', '.join(names)))
    spec = dict(DEFAULTS[kind])
    for n, v in zip(names, args):
        spec[n] = v
    for n, v in kwargs.items():
        if n not in names:
            raise TypeError('app.%s() has no argument %r (it has: %s)' % (kind, n, ', '.join(names)))
        spec[n] = v
    if 'name' not in spec or not str(spec['name']).strip():
        raise TypeError('app.%s() needs the name of the input as its first argument' % kind)
    spec['label'] = str(spec.pop('name')).strip()
    spec['kind'] = kind
    spec['key'] = key_of(spec['label'])
    if kind == 'choice':
        options = spec['options']
        if isinstance(options, (str, bytes)) or not len(options):
            raise TypeError('app.choice(%r, options): the options are a list of two or more texts' % spec['label'])
        spec['options'] = [str(o) for o in options]
        if spec['default'] is None:
            spec['default'] = spec['options'][0]
        spec['default'] = str(spec['default'])
        if spec['default'] not in spec['options']:
            raise TypeError('app.choice(%r, ...): the default %r is not one of the options' % (spec['label'], spec['default']))
    if kind == 'number':
        if spec['default'] is None or isinstance(spec['default'], bool) or not isinstance(spec['default'], (int, float)):
            raise TypeError('app.number(%r, default): the default is a number' % spec['label'])
        spec['integer'] = _is_whole(spec['default']) and all(spec[k] is None or _is_whole(spec[k]) for k in ('min', 'max', 'step'))
    return spec


def coerce(spec, raw):
    ''' The value of an input from what the person gave (a text from the command line, anything from the form): raises InputError
        when it does not fit '''
    kind, label = spec['kind'], spec['label']
    if kind == 'number':
        try:
            if isinstance(raw, bool):
                raise ValueError
            v = float(str(raw).strip().replace(',', '.')) if isinstance(raw, str) else float(raw)
        except (TypeError, ValueError):
            raise InputError(label, 'is a number (it is %r)' % (raw,))
        if v != v or v in (float('inf'), float('-inf')):
            raise InputError(label, 'is a number (it is %r)' % (raw,))
        if spec.get('integer'):
            if abs(v - round(v)) > 1e-9:
                raise InputError(label, 'is a whole number (it is %g)' % v)
            v = int(round(v))
        lo, hi = spec.get('min'), spec.get('max')
        if lo is not None and v < lo:
            raise InputError(label, '%g is below the smallest value, %g' % (v, lo))
        if hi is not None and v > hi:
            raise InputError(label, '%g is above the largest value, %g' % (v, hi))
        return v
    if kind == 'flag':
        return _bool(raw, label)
    if kind == 'choice':
        s = str(raw).strip()
        for o in spec['options']:
            if o == s:
                return o
        for o in spec['options']:
            if o.casefold() == s.casefold():
                return o
        raise InputError(label, 'is one of: %s (it is %r)' % (', '.join(spec['options']), s))
    s = '' if raw is None else str(raw).strip()
    if kind == 'text':
        return '' if raw is None else str(raw)
    if kind == 'file':
        if not s:
            raise InputError(label, 'choose a file')
        if not os.path.isfile(s):
            raise InputError(label, 'there is no file %s' % s)
        return os.path.abspath(s)
    if kind == 'folder':
        if not s:
            raise InputError(label, 'choose a folder')
        s = _resolve_output(s)
        return s
    if kind == 'save_as':
        if not s:
            raise InputError(label, 'say where to save it')
        s = _resolve_output(s)
        folder_ = os.path.dirname(s)
        if folder_ and not os.path.isdir(folder_):
            try:
                os.makedirs(folder_, exist_ok=True)
            except OSError as e:
                raise InputError(label, 'the folder %s cannot be made (%s)' % (folder_, e.strerror or e))
        return s
    return raw


def _resolve_output(path):
    ''' A relative output path means the folder the host chose (the Documents folder of the form, the shell's folder for the command line) '''
    base = _state['outputs']
    if base and not os.path.isabs(path):
        return os.path.normpath(os.path.join(base, path))
    return os.path.abspath(path) if _state['values'] is not None else path


def _declare(kind, args, kwargs):
    spec = spec_of(kind, args, kwargs)
    _seen[spec['key']] = spec
    values = _state['values']
    if values is not None and spec['key'] in values:
        raw = values[spec['key']]
    else:
        raw = spec['default']
        if kind == 'file' and values is not None and not str(raw).strip():
            raise InputError(spec['label'], 'choose a file')
    if kind == 'file' and values is None:
        value = raw                                   # (in the editor a file is just where the default says: nothing is checked)
    elif kind in ('save_as', 'folder') and values is None:
        value = raw
    else:
        value = coerce(spec, raw)
    if kind == 'save_as':
        _outputs.append({'label': spec['label'], 'path': os.path.abspath(value) if value else value})
    return value


def number(name, default=0, min=None, max=None, step=None, unit='', help=''):
    ''' A number the person chooses: a whole number when `default` is one (and min, max, step are), else a decimal.

            radius = app.number('Radius', 5, min=1, max=20, unit='mm')

        `unit` is shown after the field, `help` under it.  A value outside min..max is refused before the script runs. '''
    return _declare('number', (name, default, min, max, step, unit, help), {})


def text(name, default='', help=''):
    ''' A text the person types (a name, a label to engrave).  Returns it as it is. '''
    return _declare('text', (name, default, help), {})


def flag(name, default=False, help=''):
    ''' A yes / no choice (a check box).  On the command line: `--Hollow`, `--no-Hollow` or `--Hollow yes`. '''
    return _declare('flag', (name, default, help), {})


def choice(name, options, default=None, help=''):
    ''' One of a few texts: `material = app.choice('Material', ['steel', 'aluminium'])`.  `default` is the first one when it is left out. '''
    return _declare('choice', (name, options, default, help), {})


def file(name, default='', filter='', help=''):
    ''' A file the person picks (it must exist when the app runs), returned as its full path:

            model = app.file('Model', filter='STEP files (*.step *.stp);;All files (*)')

        `default` can be a file that comes with the app, `app.asset('step/bracket.step')`.  The runner can run the app again by itself
        when this file changes (Watch). '''
    return _declare('file', (name, default, filter, help), {})


def save_as(name, default='result', filter='', help=''):
    ''' Where the result is saved: a path the person can change, returned as a full path (a relative one means the Documents folder, or
        the folder the command was started in).  The runner lists it under *Outputs* once the script has written it:

            export_mesh(part, app.save_as('Export path', 'bracket.stl')) '''
    return _declare('save_as', (name, default, filter, help), {})


def folder(name, default='', help=''):
    ''' A folder the person picks (a relative path is made under the Documents folder), returned as its full path '''
    return _declare('folder', (name, default, help), {})


def title(text, description='', version=''):
    ''' The name of the app, what it does, and its version: shown at the top of its window and in the list of recent apps.
        Written once, with plain texts, at the top of the script. '''
    return text


def asset(path):
    ''' A file that is bundled into the app: `model = import_model(app.asset('step/bracket.step'))`.  A relative path is read from
        the folder of the script (and is the same inside the app); File > Export as app... copies the file in.  Returns the full path. '''
    assets = _state['assets']
    if _state['values'] is not None:
        if path in assets:
            return assets[path]
        if not os.path.isabs(path):
            return os.path.abspath(path)
        return path
    return path


def output(path, label=''):
    ''' A file the script makes that the runner lists under *Outputs* when it is there afterwards (a report, a drawing):

            report = app.output('report.txt')

        A relative path means the Documents folder (or the folder the command was started in).  Returns the full path. '''
    full = _resolve_output(str(path)) if _state['values'] is not None else str(path)
    if _state['values'] is not None:
        folder_ = os.path.dirname(full)
        if folder_ and not os.path.isdir(folder_):
            try:
                os.makedirs(folder_, exist_ok=True)
            except OSError:
                pass
    _outputs.append({'label': label or os.path.basename(full), 'path': os.path.abspath(full)})
    return full


def _is_field(model):
    try:
        from fieldes.kinds import kind_of
        return kind_of(model) == 'field'
    except Exception:
        return False


def _viewport_fields():
    ''' (the host) The fields the last app.viewport(...) was given, as [(name, field)] '''
    return list(_state['fields'])


class _Viewport:
    ''' What app.viewport(...) returns: what the viewport of an app window shows.  In the editor it is only a setting (the model tree has no row
        for it: the models are in it as they are, shown by their own eyes); File > Preview as app shows the viewport '''
    _is_viewport = True

    def __init__(self, models, labels):
        self.models, self.labels = models, labels

    def _display(self):
        if not in_app():
            return []
        shown = []
        fields = []
        for name, model in self.models:
            if _is_field(model):
                # (a field is not drawn: the viewport shows its icon, with this name over it, and the viewer of the field when the icon is clicked)
                fields.append((name or 'field %d' % (len(fields) + 1), model))
                continue
            d = model._display() if hasattr(model, '_display') else model
            items = list(d) if isinstance(d, (list, tuple)) and not hasattr(d, 'xyz') else [d]
            for item in items:
                try:
                    item._app_shown = True
                    item._app_name = name if self.labels else ''
                except Exception:
                    pass
                shown.append(item)
        _state['fields'] = fields
        return shown

    def __repr__(self):
        return '<shape@0x0>'        # (read by the editor's result line as "a shape": nothing is printed for it)


def viewport(models=None, *more, labels=True):
    ''' The app window shows a viewport: a picture of what you give it, that the person can look around in and -- the models that are not
        locked -- drag by their gizmo and their handles.  Write it as a statement of its own:

            app.viewport({'Bracket': part, 'Load point': pull}, support, load)

        `models` is a dict of name -> model (the name is drawn over the model, in the style of the labels of boundary conditions), or a list of
        models (no names); the models after it (`more`) are shown without a name.  A condition (`force(...)`, `fixed(...)`) is a model: it is
        drawn on the body as in the editor.  Whatever a model has numbers to drag -- `gizmo()`, `handles()`, `var()` -- the person can drag,
        unless it is `lock()`ed: click a model to select it and its gizmo shows; there is no model tree and no menu.  A model that is dragged
        writes its new numbers into the script the app runs, which runs again for the picture (with app.running() False), and the numbers are
        there when Run is pressed.  A point is its dot: always drawn, click it to select it.  A **field** among them has an icon (with its name over it) at the place of the object it
        was made from; click the icon and the field is shown on a plane inside the field's bounds, with a slider for its position.

        It is a setting of the app: the editor draws nothing for it and the model tree has no row for it -- the models are in the tree as they are,
        shown by their eyes; File > Preview as app shows the viewport.  `labels=False` leaves the names out. '''
    if models is None:
        pairs = []
    elif isinstance(models, dict):
        pairs = [(str(k), v) for k, v in models.items()]
    else:
        pairs = [('', v) for v in (models if isinstance(models, (list, tuple)) else [models])]
    pairs += [('', v) for v in more]
    _state['fields'] = []
    return _Viewport(pairs, bool(labels))
