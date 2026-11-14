'''
Python library of FielDes, built on the libfive CAD kernel
Copyright (C) 2021  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

import ctypes
import numbers
import tempfile
import subprocess

from fieldes.ffi import (lib, libfive_region_t, libfive_interval_t,
                         libfive_vec3_t, libfive_tree)

# The number of an opcode and the number of arguments it takes, by its name (see Shape.new)
_OP_INFO = {}


def _combined_kind(shapes):
    ''' The kind of what is made of these shapes by arithmetic: a number is a number ('const'), fields combine into a
        field, and anything else -- a body, or a shape of no known kind -- makes a body (None) '''
    kinds = [s._kind for s in shapes]
    if all(k == 'const' for k in kinds):
        return 'const'
    if all(k in ('field', 'const') for k in kinds):
        return 'field'
    return None


# (the numbers an arithmetic step is given are wrapped into trees: one tree for each number, kept -- a body of points makes fifty thousand steps and most of them multiply by a few numbers (1/3, 0.5, 3 ...).
# Only the step itself ever sees the wrapped number and nothing there changes it, so it can be shared; Shape.wrap, which gives a new one each time because its caller may go on to set its kind, is not shared)
_OPERANDS = {}


def _operand(t):
    c = t.__class__
    if (c is float or c is int) and t == t and t != 0:
        key = (c, t)
        s = _OPERANDS.get(key)
        if s is None:
            s = Shape.wrap(t)
            if len(_OPERANDS) < 4096:
                _OPERANDS[key] = s
        return s
    return Shape.wrap(t)


def _wrapped(f, whole=True):
    ''' Decorator function which calls Shape.wrap on every argument
        in f(self, *args), then calls f as usual.  The result is a field when all it is made of are fields (and
        numbers); with whole=False, as the first of them is (a remapped body is a body)
    '''
    def g(*args):
        args = [a if a.__class__ is Shape else _operand(a) for a in args]
        out = f(*args)
        kind = 'const'
        for s in (args if whole else args[:1]):
            k = s._kind
            if k == 'const':
                continue
            if k == 'field':
                kind = 'field'
            else:
                kind = None
                break
        out._kind = kind
        for a in args:                  # (a field made of fields is about what the first of them is about)
            o = a._field_origin
            if o is not None:
                out._field_origin = o
                break
        return out
    return g

class Shape:
    # What this is, for the model tree's icons and colours (fieldes.kinds): 'field' for what is made to be a field
    # (a distance, a ramp, noise ...), 'const' for a number made into a tree, 'point' / 'surface' for those, and None
    # for anything else, which is a body (3D, or 2D when it has no z)
    _kind = None
    # Where a field made from a point or a body is "about" (the point, the body, or three numbers): the field viewer starts there
    _field_origin = None

    def __init__(self, ptr):
        ''' Builds a Shape from a raw pointer.

            It is unlikely that you want to call this by hand; consider using
            X/Y/Z, the @shape decorator, or functions in fieldes.stdlib instead
        '''
        if not isinstance(ptr, int):
            raise TypeError(
                "Must call Shape.__init__ with a c_void_p returned " +
                "from a FielDes library function.  It is unlikely "
                "that you want to call this by hand; consider using " +
                "Shape.X() (and Y and Z), or the @shape decorator")
        self.ptr = ptr

    def __del__(self):
        lib.libfive_tree_delete(self.ptr)

    @classmethod
    def new(cls, op, *args):
        # (the number of an opcode and how many arguments it takes are asked of the library once for each name: a body of points makes fifty thousand of these, and the two questions were three
        # calls into the library each time)
        info = _OP_INFO.get(op)
        if info is None:
            name = op
            if isinstance(op, str):
                op = op.encode('ascii')
            if isinstance(op, bytes):
                op = lib.libfive_opcode_enum(op)
            if op == -1:
                raise RuntimeError("Invalid opcode")
            num_args = lib.libfive_opcode_args(op)
            if isinstance(name, str):
                _OP_INFO[name] = (op, num_args)
        else:
            op, num_args = info
        if len(args) != num_args:
            raise RuntimeError("Error: opcode {} takes {} arguments"
                    .format(op, num_args))
        if num_args == 0:
            return cls(lib.libfive_tree_nullary(op))
        elif num_args == 1:
            return cls(lib.libfive_tree_unary(op, args[0]))
        elif num_args == 2:
            return cls(lib.libfive_tree_binary(op, args[0], args[1]))

    @classmethod
    def wrap(cls, t):
        ''' Ensures that the input argument is a Shape, converting constants
        '''
        # (a shape is what most things are: asked first, as a plain class -- the check for a number goes through an abstract base class and costs ten times as much)
        if isinstance(t, cls):
            return t
        elif isinstance(t, numbers.Number):
            out = cls(lib.libfive_tree_const(t))
            out._kind = 'const'
            return out
        elif getattr(type(t), '_as_field', None) is not None:
            return cls.wrap(t._as_field())          # (a result of an analysis or an optimisation: its field -- see fea.FieldResult)
        else:
            raise RuntimeError("Cannot convert {} into a Shape".format(t))

    @classmethod
    def X(cls):
        return cls.new('var-x')
    @classmethod
    def Y(cls):
        return cls.new('var-y')
    @classmethod
    def Z(cls):
        return cls.new('var-z')

    def __repr__(self):
        return "<shape@0x{:x}>".format(self.ptr)

    def __str__(self):
        s = lib.libfive_tree_print(self.ptr)
        try:
            return ctypes.c_char_p(s).value.decode('ascii')
        finally:
            lib.libfive_free_str(ctypes.cast(s, ctypes.c_char_p))

    def __bool__(self):
        raise RuntimeError("Cannot check truthiness of Shape")

    @_wrapped
    def __add__(self, other):
        return Shape.new('add', self.ptr, other.ptr)

    @_wrapped
    def __radd__(self, other):
        return Shape.new('add', other.ptr, self.ptr)

    @_wrapped
    def __sub__(self, other):
        return Shape.new('sub', self.ptr, other.ptr)

    @_wrapped
    def __rsub__(self, other):
        return Shape.new('sub', other.ptr, self.ptr)

    @_wrapped
    def __mul__(self, other):
        return Shape.new('mul', self.ptr, other.ptr)

    @_wrapped
    def __rmul__(self, other):
        return Shape.new('mul', other.ptr, self.ptr)

    @_wrapped
    def __mod__(self, other):
        return Shape.new('mod', self.ptr, other.ptr)

    @_wrapped
    def __rmod__(self, other):
        return Shape.new('mod', other.ptr, self.ptr)

    @_wrapped
    def __pow__(self, other):
        return Shape.new('pow', self.ptr, other.ptr)

    @_wrapped
    def __rpow__(self, other):
        return Shape.new('pow', other.ptr, self.ptr)

    @_wrapped
    def __truediv__(self, other):
        return Shape.new('div', self.ptr, other.ptr)

    def __neg__(self):
        return self._unary('neg')

    def __pos__(self):
        return self

    @_wrapped
    def __rtruediv__(self, other):
        return Shape.new('div', other.ptr, self.ptr)

    @staticmethod
    def _remapped(self, x_, y_, z_):
        return Shape(lib.libfive_tree_remap(
            self.ptr, x_.ptr, y_.ptr, z_.ptr))

    def remap(self, x_, y_, z_):
        ''' Performs the remapping f(x_, y_, z_)
        '''
        args = [Shape.wrap(a) for a in (self, x_, y_, z_)]
        out = Shape._remapped(*args)
        out._kind = self._kind if self._kind in ('field', 'const') else None
        return out

    def __eq__(self, other):
        raise RuntimeError("Shape does not support equality comparisons")

    @classmethod
    def var(cls):
        return cls.new('var-free')

    def with_constant_vars(self):
        return Shape.new('const-var', self.ptr)

    def lock(self):
        return self.with_constant_vars()

    def optimized(self):
        return Shape(lib.libfive_tree_optimized(self.ptr));

    def _unary(self, op):
        out = Shape.new(op, self.ptr)
        out._kind = self._kind if self._kind in ('field', 'const') else None
        out._field_origin = self._field_origin
        return out

    def sqrt(self):
        return self._unary('sqrt')

    @_wrapped
    def pow(self, other):
        return Shape.new('pow', self.ptr, other.ptr)

    def sin(self):
        return self._unary('sin')
    def cos(self):
        return self._unary('cos')
    def tan(self):
        return self._unary('tan')
    def asin(self):
        return self._unary('asin')
    def acos(self):
        return self._unary('acos')
    def atan(self):
        return self._unary('atan')
    def exp(self):
        return self._unary('exp')
    def log(self):
        return self._unary('log')
    def square(self):
        return self._unary('square')
    def abs(self):
        return self._unary('abs')
    def __abs__(self):
        return self._unary('abs')

    @_wrapped
    def atan2(self, other):
        return Shape.new('atan2', self.ptr, other.ptr)

    @_wrapped
    def nth_root(self, other):
        return Shape.new('nth-root', self.ptr, other.ptr)

    @_wrapped
    def nanfill(self, other):
        return Shape.new('nanfill', self.ptr, other.ptr)

    @_wrapped
    def min(self, other):
        return Shape.new('min', self.ptr, other.ptr)

    @_wrapped
    def max(self, other):
        return Shape.new('max', self.ptr, other.ptr)

    def _display(self):
        ''' What is drawn for this shape: itself, except where it is of a kind that has no body to draw -- a 2D shape
            (drawn flat), a surface (a thin sheet), a point (a small ball), a field (nothing: the section viewer shows it): see
            fieldes.stdlib.points '''
        from fieldes.stdlib import points
        return points.displayed(self)

    def distance(self, resolution=None):
        ''' The signed distance from every point to the surface of this body (negative inside): its exact distance, whatever made the body -- a union, a smoothing, a blend and the rest have fields
            that are bounds or sums, and this is the value (H190: "ALL BODIES ARE ALWAYS TRUE DISTANCE FIELDS ... as a part of the type itself").  See fieldes.stdlib.fields.exact_of.  What
            is drawn is the body's own field, whose surface is the same '''
        from fieldes.stdlib.fields import exact_of
        return exact_of(self, resolution)

    def __call__(self, x, y, z):
        ''' The value of this field at a point.

            With three numbers: the number it has there.  With shapes (fields of x, y, z, or numbers among them): the field
            that has, at every point, the value this one has at the point they give -- f(x + 3, y, z) is f moved by 3.  This
            is a call: this field is kept where it is and evaluated at the point, not copied for each place it is asked at
            (remap is the same field written as a substitution, which is copied into every use).
        '''
        if all([isinstance(c, numbers.Number) for c in [x, y, z]]):
            return lib.libfive_tree_eval_f(self.ptr, libfive_vec3_t(x, y, z))
        args = [Shape.wrap(c) for c in (x, y, z)]
        out = Shape(lib.libfive_tree_call(self.ptr, args[0].ptr, args[1].ptr, args[2].ptr))
        out._kind = self._kind if self._kind in ('field', 'const') else None
        return out

    def partial(self, axis, oracle_step=0.0, max_call_depth=1):
        ''' The partial derivative of this field with respect to x (axis 0), y (1) or z (2), as a field of its own: the symbolic derivative, by
            the rules of calculus -- not a difference of two calls, whose intervals over a cell of the renderer's are as many times too wide as
            the cell is wider than the step (a field built from such a gradient makes the renderer refine a whole band round the surface).
            A native field inside (a mesh's distance, noise) has no formula: with `oracle_step` above 0 its partial is the central difference over
            that step; without it, None is returned.  None too for a field whose calls are nested deeper than `max_call_depth` (default 1: calls of plain
            fields): the derivative of a call is three calls of partials, and costs several times what the field does for every level of nesting -- a
            field made from the gradient of another, a Newton walk, the result of a remedy that was made from remedies.  Its gradient is read by differences.
        '''
        ptr = lib.libfive_tree_partial(self.ptr, int(axis), float(oracle_step), int(max_call_depth))
        if not ptr:
            return None
        out = Shape(ptr)
        out._kind = 'field' if self._kind in ('field', 'const') else None
        return out

    def call(self, fn, *args, **kwargs):
        ''' Calls the given function with self as the first argument.
            This is convenient to chain transforms without deep call trees.
        '''
        return fn(self, *args, **kwargs)

    @_wrapped
    def compare(self, other):
        return Shape.new('compare', self.ptr, other.ptr)

    def save_stl(self, filename, xyz_min=(-10,-10,-10), xyz_max=(10,10,10),
                 resolution=10, quality=8):
        ''' Converts this Shape into a mesh and saves it as an STL file.

            xyz_min/max are three-element lists of corner positions
            resolution is the reciprocal of minimum feature size
                (larger values = higher resolution = slower)
            quality is the negative order of magnitude of maximum error
        '''
        region = libfive_region_t(*[libfive_interval_t(a, b) for a, b
                                    in zip(xyz_min, xyz_max)])
        # (the C function reads trees until a null pointer: without that terminator it read whatever lay after
        # the array, and crashed now and then)
        ptr_array = [self.ptr, None]
        trees = (libfive_tree * len(ptr_array))(*ptr_array)
        lib.libfive_tree_save_meshes(trees, region, resolution, quality,
                                   filename.encode('ascii'))

    def get_mesh(self, xyz_min=(-10,-10,-10), xyz_max=(10,10,10),
                 resolution=10, algorithm='dc'):
        ''' Converts this Shape into a mesh, and returns that mesh in
            the form of a face-vertex mesh.

        Options are the same as in save_stl(). `algorithm` selects the
        meshing algorithm: 'dc' (dual contouring, default, fastest),
        'simplex', or 'hybrid'. The latter two are slower but more
        robust for STEP-imported oracle shapes, where dual contouring
        has been found to fragment meshes even when the underlying
        field is provably correct at every queried point.

        Returns:
        verts - List of (x, y, z) vertices
        tris - List of (a, b, c) indices into verts
        '''
        region = libfive_region_t(*[libfive_interval_t(a, b) for a, b
                                    in zip(xyz_min, xyz_max)])
        if algorithm == 'dc':
            mesh_p = lib.libfive_tree_render_mesh(self.ptr, region, resolution)
        else:
            algo_id = {'dc': 0, 'simplex': 1, 'hybrid': 2}[algorithm]
            mesh_p = lib.libfive_tree_render_mesh_algo(self.ptr, region, resolution,
                                                        algo_id, 1e-8, 8)
        mesh = mesh_p[0]
        tris = []
        for i in range(mesh.tri_count):
            f = mesh.tris[i]
            tris.append((f.a, f.b, f.c))
        verts = []
        for i in range(mesh.vert_count):
            v = mesh.verts[i]
            verts.append((v.x, v.y, v.z))
        lib.libfive_mesh_delete(mesh_p)
        return verts, tris

################################################################################

def field_shape(ptr):
    ''' A field a solver calculated (a stress, a temperature, a speed, a density ...), made from the tree `ptr`: a FIELD, not a body -- a value at
        every point of space, which is not drawn, is not a part, and has an icon (that opens the field viewer) when it is displayed '''
    out = Shape(ptr)
    out._kind = 'field'
    return out


def shape(f):
    ''' Decorator which converts a target function into a Shape
    '''
    return f(Shape.X(), Shape.Y(), Shape.Z())


################################################################################
# A BODY

def _pure(other):
    ''' Whether `other` is a number or a body: what arithmetic on a body is still a body with (a body with a field is a field: a value at every point, which is not a body) '''
    if isinstance(other, numbers.Number):
        return True
    return isinstance(other, Shape) and (other._kind == 'const' or type(other) is Body)


def become_body(out):
    ''' `out`, a shape that something has just made, is a BODY when it is one -- of the kind 'solid' or 'lattice' (a 2D shape is drawn flat and a field, a point, a curve, a surface are what they are):
        it is made one in place, the same object. See Body '''
    if type(out) is Shape and out._kind in (None, 'solid', 'lattice'):
        from fieldes.kinds import kind_of
        if kind_of(out) in ('solid', 'lattice'):
            out.__class__ = Body
    return out


class Body(Shape):
    ''' A BODY: a 3D shape.  What makes it a different type from a field is its FIELD -- a field is any value at every point of space (a ramp, a stress, noise); the field of a body is always
        the TRUE SIGNED DISTANCE to its surface (negative inside, 0 on the surface, the distance to the surface outside), whatever made the body: a box, a union, a difference with a blend, a smoothing,
        an offset, a scale that is not the same in every direction, an optimized part, an import (the user, 2026-11-12 and 2026-11-13: "A Field can be anything arbitrary, a body is always TRUE
        SIGNED DISTANCE").  The type keeps it, and no operation can give a body that is not a distance: everything the library returns that is a body is a Body, made so by one funnel at the
        library's door (fieldes.stdlib.body_law), not by each operation.

        `body.field` is that distance, and it is what every reading of a body is: `body(x, y, z)`, `evaluate`, `sample_grid`, `field_range`, the field viewers, and a function that makes a field from
        a body (`distance_to_surface`, `normal_field`, `wall_thickness` ...).  The distance has no formula for the inside of a union, a difference, a smoothing (the nearest point of the surface may
        lie on a crease: a search), and the numbers of its var()s change without a run of the script, so `field` is made from the surface of the body, finely meshed, and the kernel's exact
        distance to those triangles -- made the first time something reads it, for the numbers the body has then, and kept (see fields.exact_of).  `ptr`, the tree of the body as it is made,
        is what is MESHED (the viewport, the exports, the analyses) and what the library builds the next body from: its zero set is the surface, which is all a drawing and a boolean need, and
        it is the same surface as the zero set of `field`. '''

    @property
    def field(self):
        ''' The field of this body: its signed distance (see the class).  Made the first time it is read, for the numbers the script's var()s have then, and kept with the body '''
        from fieldes.stdlib.fields import exact_of, _vars_key
        key = _vars_key()
        memo = self.__dict__.get('_field_memo')
        if memo is not None and memo[0] == self.ptr and memo[1] == key:          # (the tree too: a copy of the attributes of another body, with a tree of its own, is not read from the other's memo)
            return memo[2]
        field = exact_of(self)
        self._field_memo = (self.ptr, key, field)
        return field

    def __call__(self, x, y, z):
        if all(isinstance(c, numbers.Number) for c in (x, y, z)):
            return lib.libfive_tree_eval_f(self.field.ptr, libfive_vec3_t(x, y, z))
        return become_body(Shape.__call__(self, x, y, z))

    def __neg__(self):
        # (the complement of a body: its distance is the negative of the body's -- exact, and it needs no extent, which the complement of a body has none of)
        out = become_body(self._unary('neg'))
        if type(out) is Body:
            from fieldes.stdlib.transforms import _lazy_once
            out._distance_of = _lazy_once(lambda: Shape._unary(self.field, 'neg'))
        return out

    def remap(self, x_, y_, z_):
        return become_body(Shape.remap(self, x_, y_, z_))


def _arithmetic_of_a_body(name):
    base = getattr(Shape, name)

    def method(self, other):
        out = base(self, other)
        if _pure(other):
            become_body(out)
        return out
    method.__name__ = name
    return method


# (a body and a number or a body, by arithmetic -- an offset, a scaled field, a union, an intersection -- is a body: its field is its own distance)
for _name in ('__add__', '__radd__', '__sub__', '__rsub__', '__mul__', '__rmul__', '__truediv__', '__rtruediv__', 'min', 'max'):
    setattr(Body, _name, _arithmetic_of_a_body(_name))

# Hotpatch every transform (which take a shape as their first arguments)
# into the Shape as methods, to make things easier to chain
import fieldes.stdlib.transforms as _transforms
for (_name, _f) in _transforms.__dict__.items():
    if callable(_f):
        setattr(Shape, _name, _f)

# Hot-patch a few CSG functions onto the Shape class
import fieldes.stdlib.csg as _csg
for _name in ['union', 'intersection', 'difference', 'offset']:
    setattr(Shape, _name, getattr(_csg, _name))

# What those functions do with a shape that has an excluded region (see fieldes.stdlib.excluded)
from fieldes.stdlib import excluded as _excluded
_excluded.install_methods()

# ... and for a Body, the methods that are the library's functions are under the body law (see fieldes.stdlib.body_law)
from fieldes.stdlib import body_law as _body_law
_body_law.install_methods()
