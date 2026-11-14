'''
The body law: every body is a Body, and the field of a Body is its true signed distance.

The user, 2026-11-12 and 2026-11-13 (capitals): "ANY OPERATION THAT PRODUCED A BODY HAS TO FUNDAMENTALLY NOT BE ABLE TO PRODUCE ANYTHING THAT IS NOT A TRUE DISTANCE FIELD ... AS A PART OF THE TYPE
ITSELF"; "THE BODIES FIELD HAS TO BE A TRUE SIGNED DISTANCE FIELD. That is the DIFFERENCE BETWEEN A FIELD AND A BODY. A Field can be anything arbitrary, a body is always TRUE SIGNED DISTANCE."

The type is fieldes.shape.Body.  This module is the one funnel that makes it hold for everything the library does -- no operation is named here, and an operation that is written tomorrow is
covered without a line about it:

    * what a script calls is the library's functions (`from fieldes import *`) and the methods of a body (`body.translate(...)`).  Every one of them is wrapped once, here, when the library is
      loaded, and the wrapper does three things:
        - what comes back that is a body is made a Body (in place, the same object: become_body);
        - the bodies that come in are given to the function as the plain shapes they are inside the library -- their tree, which is what the library builds the next body from: a union is the
          smaller of two zero sets, and that is all a boolean needs; the surface of the result is what the operation means.  They are Bodies again the moment the call is over;
        - a function that READS the field of a body -- evaluate, sample_grid, field_range, and every function that makes a FIELD from a body (distance_to_surface, depth_below, normal_field,
          wall_thickness ...: a function the library tags `_makes == 'field'`) -- is given `body.field`, the distance, in its place.  A function says it reads fields with `fn._reads_field = True`
          next to its definition, as it says what it takes with `fn._takes`.
    * the library's own calls of one another, inside its modules, are not wrapped (the same as for fieldes.stdlib.placement and fieldes.stdlib.excluded): inside, a body is its tree.

What is not a body is left as it is: a field (a ramp, a stress, noise: values), a number, a point, a curve, a surface, a 2D shape -- and the arithmetic of a body with a field, which is a field.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import functools
import inspect

from fieldes.shape import Shape, Body, become_body

WRAPPED = []                # (the names of what is wrapped, for the check that nothing is left out: tests/internal/body_field_checks.py)
_SKIPPED = ('progress',)    # (a function that is no operation on shapes: it reports to the progress bar)


def _promote(value):
    ''' Everything in the result of a call that is a body is made one (in place); the result is handed back as it is '''
    t = type(value)
    if t is Shape:
        become_body(value)
    elif t is tuple or t is list:
        for item in value:
            ti = type(item)
            if ti is Shape:
                become_body(item)
            elif ti is tuple or ti is list:
                _promote(item)
    return value


def _demote(values, flipped):
    ''' The bodies among `values` (and in the lists and tuples among them) are given as the plain shapes they are inside the library, until the call is over '''
    for v in values:
        t = type(v)
        if t is Body:
            v.__class__ = Shape
            flipped.append(v)
        elif t is list or t is tuple:
            for w in v:
                if type(w) is Body:
                    w.__class__ = Shape
                    flipped.append(w)


def _field_of(v, flipped):
    ''' A body where a function reads fields: its field.  (The field of a body that is its own distance is the body itself: it is given as a plain shape for the call) '''
    if type(v) is not Body:
        return v
    f = v.field
    if type(f) is Body:
        f.__class__ = Shape
        flipped.append(f)
    return f


def law(fn):
    ''' `fn` under the body law (see the top of this file) '''
    makes_field = getattr(fn, '_makes', None) == 'field'
    reads = makes_field or bool(getattr(fn, '_reads_field', False))

    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        flipped = []
        if reads:
            args = tuple(_field_of(a, flipped) for a in args)
            if kwargs:
                kwargs = {k: _field_of(v, flipped) for k, v in kwargs.items()}
        else:
            _demote(args, flipped)
            if kwargs:
                _demote(kwargs.values(), flipped)
        try:
            out = fn(*args, **kwargs)
        finally:
            for b in flipped:
                b.__class__ = Body
        if not makes_field:
            _promote(out)
        return out

    wrapper._body_law = True
    return wrapper


def _wrappable(name, fn):
    return (inspect.isfunction(fn) and not name.startswith('_') and name not in _SKIPPED and not inspect.isgeneratorfunction(fn)
            and not getattr(fn, '_body_law', False))


def install(namespace):
    ''' Puts every function of `namespace` (what `from fieldes import *` brings in) under the law, and the methods of a Body that are the library's functions.  Done last, after the wrappers of
        the placement and of the excluded regions (it is the outermost: what a script calls gets the result made a body after everything else has been done to it) '''
    for name, fn in list(namespace.items()):
        if _wrappable(name, fn):
            namespace[name] = law(fn)
            WRAPPED.append(name)
    return WRAPPED


def install_methods():
    ''' `body.translate(...)`, `body.union(...)`, `body.offset(...)`: the methods shape.py binds from the library (when it has loaded the library, at its end) are under the law for a Body; the ones the
        class itself defines -- its arithmetic, `field` -- stay '''
    for name, fn in list(Shape.__dict__.items()):
        if name in Body.__dict__ or name.startswith('__'):
            continue
        if inspect.isfunction(fn) and getattr(fn, '__module__', '').startswith('fieldes.stdlib') and not getattr(fn, '_body_law', False):
            setattr(Body, name, law(fn))
