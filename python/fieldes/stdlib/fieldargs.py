'''
Numbers and fields in the same place.

Anything the library takes as a number takes a FIELD as well (a Shape: a value at every point of space -- a distance, a ramp
of one, a stress result, noise ...), as in nTop.  A function that can do arithmetic with what it is given needs nothing;
the few places that must ask a question of a number (is it positive? is it zero?) use these.  A number stays a number, so
nothing is slower for a plain value; a field is worked with as the tree it is.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import functools
import numbers

from fieldes.shape import Shape

__all__ = ['is_field']


def is_field(v):
    ''' Whether v is a field (a Shape) and not a plain number '''
    return isinstance(v, Shape)


def present(v):
    ''' A value was given and it is not zero (a field is taken to be something) '''
    return isinstance(v, Shape) or bool(v)


def positive(v):
    ''' A number above zero -- or a field, which is not known to be anything else '''
    return isinstance(v, Shape) or v > 0


def number_or_field(v, what='a value'):
    ''' v as a float, or kept as the field it is; anything else is a TypeError that says what was expected '''
    if isinstance(v, Shape):
        return v
    if isinstance(v, numbers.Number):
        return float(v)
    raise TypeError('{} is a number or a field (a Shape), not {!r}'.format(what, type(v).__name__))


def lowest(*values):
    ''' The smallest of numbers and fields: a number when all are numbers, else a field '''
    out = values[0]
    for v in values[1:]:
        if isinstance(out, Shape) or isinstance(v, Shape):
            out = Shape.wrap(out).min(v)
        else:
            out = min(out, v)
    return out


def highest(*values):
    ''' The largest of numbers and fields: a number when all are numbers, else a field '''
    out = values[0]
    for v in values[1:]:
        if isinstance(out, Shape) or isinstance(v, Shape):
            out = Shape.wrap(out).max(v)
        else:
            out = max(out, v)
    return out


def nonzero(v):
    ''' v itself when it is a field or a nonzero number, else None -- for a size that "0" turns off '''
    return v if present(v) else None


def _tag_field(v, given):
    if isinstance(v, Shape):
        if not any(v is g for g in given):          # (never the shape that was passed in: it is not ours to relabel)
            v._kind = 'field'
    elif isinstance(v, (tuple, list)):
        for item in v:
            _tag_field(item, given)


def _origin_of(values):
    ''' What a field made from these arguments is "about", for the field viewer to start there: the origin of a field that is
        given (a field made from a point keeps that point), else a point or a body that is given (the model itself: its place is
        looked up when the viewer opens), else a position written as three numbers.  None when the arguments give none '''
    from fieldes.kinds import kind_of
    for a in values:
        if isinstance(a, Shape):
            o = getattr(a, '_field_origin', None)
            if o is not None:
                return o
            if kind_of(a) in ('solid', 'profile', 'point', 'surface'):
                return a
        elif isinstance(a, (tuple, list)) and a:
            if len(a) == 3 and all(isinstance(c, numbers.Number) for c in a):
                return tuple(float(c) for c in a)
            o = _origin_of(a[:1])
            if o is not None:
                return o
    return None


def _set_origin(v, origin):
    if isinstance(v, Shape):
        if getattr(v, '_field_origin', None) is None:
            v._field_origin = origin
    elif isinstance(v, (tuple, list)):
        for item in v:
            _set_origin(item, origin)


def returns_field(fn):
    ''' A function that makes a field (not a body): what it returns is of the kind 'field', which the model tree shows
        with a field's icon and the field viewer shows (a disc coloured by it) when it is selected '''
    @functools.wraps(fn)
    def g(*args, **kwargs):
        out = fn(*args, **kwargs)
        _tag_field(out, list(args) + list(kwargs.values()))
        origin = _origin_of(list(args) + list(kwargs.values()))
        if origin is not None:
            _set_origin(out, origin)
        return out
    return g


def returns_body(fn):
    ''' A function that makes a body out of what it is given, which may be a field: what it returns is not of the kind
        'field' even when it was made from one (a wall round a sheet, a shell) '''
    @functools.wraps(fn)
    def g(*args, **kwargs):
        out = fn(*args, **kwargs)
        if isinstance(out, Shape) and not any(out is a for a in list(args) + list(kwargs.values())):
            out._kind = None
        return out
    return g
