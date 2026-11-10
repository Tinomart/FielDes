''' H164, step 1 of the native morph body (the user, 2026-11-09: "ON SMALLER FILES IT SHOULD LEGIT TAKE NO TIME AT ALL. FOR LARGE FILES IT SHOULD BE REAL TIME").

    A plain number that has the methods of an expression (`max`, `min`, `sqrt`, `square`, `sin`, `cos`) and the arithmetic of one, so that the code that builds the patches of a body of points (morph.py) can run
    on numbers instead of building a graph of expression nodes: one Python call for each node, about 7 microseconds, 128,000 nodes for a body of 28 points.  The same arithmetic on these costs a fraction of
    that.  This is the reference the C++ port of the patch evaluation is compared against; it is not used by the viewport.  Only the operations morph.py uses are here '''
import math


class Num(object):
    __slots__ = ('v',)

    def __init__(self, v):
        self.v = float(v)

    # arithmetic with another Num or a plain number
    def __add__(self, o):
        return Num(self.v + (o.v if isinstance(o, Num) else o))
    __radd__ = __add__

    def __sub__(self, o):
        return Num(self.v - (o.v if isinstance(o, Num) else o))

    def __rsub__(self, o):
        return Num((o.v if isinstance(o, Num) else o) - self.v)

    def __mul__(self, o):
        return Num(self.v * (o.v if isinstance(o, Num) else o))
    __rmul__ = __mul__

    def __truediv__(self, o):
        return Num(self.v / (o.v if isinstance(o, Num) else o))

    def __rtruediv__(self, o):
        return Num((o.v if isinstance(o, Num) else o) / self.v)

    def __neg__(self):
        return Num(-self.v)

    # the methods of an expression that morph.py uses
    def max(self, o):
        return Num(max(self.v, o.v if isinstance(o, Num) else o))

    def min(self, o):
        return Num(min(self.v, o.v if isinstance(o, Num) else o))

    def sqrt(self):
        return Num(math.sqrt(max(self.v, 0.0)))

    def square(self):
        return Num(self.v * self.v)

    def sin(self):
        return Num(math.sin(self.v))

    def cos(self):
        return Num(math.cos(self.v))

    def __float__(self):
        return self.v

    def __repr__(self):
        return 'Num(%r)' % self.v


def wrap(v):
    ''' What `Shape.wrap` is for expressions: a number as a Num (a Num stays) '''
    return v if isinstance(v, Num) else Num(v)


# ---- H164 step 2: the same arithmetic RECORDED as a tape of plain operations instead of worked out ------------------------------------------------------------------------------------------------------------
# (the design is in the task H164: Shape::morphDirect already runs a flat tape on doubles at every step of a drag, but the tape is made by walking 128,000 libfive nodes that Python built one call at a time.  Here
# the patch code writes the tape itself: each operation appends (op, a, b) to the tape of the run, a leaf is an INPUT -- one of the numbers of the points, still an expression of the var()s, worked out by the
# viewport -- or a constant.  The ops are the ones of Shape::morphDirect.)

OP_INPUT, OP_CONST, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MIN, OP_MAX, OP_SQRT, OP_SQUARE, OP_NEG, OP_SIN, OP_COS = range(13)


class Tape(object):
    ''' The operations of one run: ops[k] = (op, a, b) -- a and b are the numbers of earlier operations (OP_INPUT: a is the number of the input; OP_CONST: a is the index of the constant in `constants`) '''

    def __init__(self):
        self.ops = []
        self.constants = []
        self._const_index = {}

    def add(self, op, a=0, b=0):
        self.ops.append((op, a, b))
        return len(self.ops) - 1

    def const(self, v):
        key = float(v)
        got = self._const_index.get(key)
        if got is None:
            self.constants.append(key)
            got = self.add(OP_CONST, len(self.constants) - 1)
            self._const_index[key] = got
        return got


_TAPE = [None]


class Tr(object):
    __slots__ = ('i',)

    def __init__(self, i):
        self.i = i

    @staticmethod
    def input(n):
        return Tr(_TAPE[0].add(OP_INPUT, n))

    @staticmethod
    def _of(o):
        return o.i if isinstance(o, Tr) else _TAPE[0].const(o)

    def __add__(self, o):
        return Tr(_TAPE[0].add(OP_ADD, self.i, Tr._of(o)))
    __radd__ = __add__

    def __sub__(self, o):
        return Tr(_TAPE[0].add(OP_SUB, self.i, Tr._of(o)))

    def __rsub__(self, o):
        return Tr(_TAPE[0].add(OP_SUB, Tr._of(o), self.i))

    def __mul__(self, o):
        return Tr(_TAPE[0].add(OP_MUL, self.i, Tr._of(o)))
    __rmul__ = __mul__

    def __truediv__(self, o):
        return Tr(_TAPE[0].add(OP_DIV, self.i, Tr._of(o)))

    def __rtruediv__(self, o):
        return Tr(_TAPE[0].add(OP_DIV, Tr._of(o), self.i))

    def __neg__(self):
        return Tr(_TAPE[0].add(OP_NEG, self.i))

    def max(self, o):
        return Tr(_TAPE[0].add(OP_MAX, self.i, Tr._of(o)))

    def min(self, o):
        return Tr(_TAPE[0].add(OP_MIN, self.i, Tr._of(o)))

    def sqrt(self):
        return Tr(_TAPE[0].add(OP_SQRT, self.i))

    def square(self):
        return Tr(_TAPE[0].add(OP_SQUARE, self.i))

    def sin(self):
        return Tr(_TAPE[0].add(OP_SIN, self.i))

    def cos(self):
        return Tr(_TAPE[0].add(OP_COS, self.i))


def wrap_trace(v):
    return v if isinstance(v, Tr) else Tr(_TAPE[0].const(v))


def run_tape(tape, inputs):
    ''' The values of every operation of a tape for the given values of its inputs (the reference for the viewport's reading of it) '''
    import math
    val = [0.0] * len(tape.ops)
    for k, (op, a, b) in enumerate(tape.ops):
        if op == OP_INPUT:
            val[k] = float(inputs[a])
        elif op == OP_CONST:
            val[k] = tape.constants[a]
        elif op == OP_ADD:
            val[k] = val[a] + val[b]
        elif op == OP_SUB:
            val[k] = val[a] - val[b]
        elif op == OP_MUL:
            val[k] = val[a] * val[b]
        elif op == OP_DIV:
            val[k] = val[a] / val[b]
        elif op == OP_MIN:
            val[k] = min(val[a], val[b])
        elif op == OP_MAX:
            val[k] = max(val[a], val[b])
        elif op == OP_SQRT:
            val[k] = math.sqrt(max(val[a], 0.0))
        elif op == OP_SQUARE:
            val[k] = val[a] * val[a]
        elif op == OP_NEG:
            val[k] = -val[a]
        elif op == OP_SIN:
            val[k] = math.sin(val[a])
        elif op == OP_COS:
            val[k] = math.cos(val[a])
    return val
