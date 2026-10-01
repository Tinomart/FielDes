'''
Regressions: turn measured data into fields.

A regression is fitted to (x, y) data and can then be applied to numbers or
to any field -- typically a distance field -- to get a field that drives an
operation (a lattice thickness, an offset, a blend):

    from fieldes import *

    ball = sphere(30)
    # wall thickness measured / required at several depths below the skin
    t_of_depth = fit([(0, 2.0), (5, 1.4), (15, 0.9), (30, 0.6)], model='poly', degree=2)
    print(t_of_depth)                  # equation and goodness of fit (R^2)
    thickness = t_of_depth(depth_below(ball))      # a field
    lat = lattice(ball, 'gyroid', cell_size=8, thickness=thickness, skin=1.5)

Models (fit(..., model=...)):
    'linear'        a + b x
    'poly'          polynomial of the given degree (default 2)
    'exp'           a e^(b x)                 (y > 0)
    'exp_offset'    a e^(b x) + c
    'power'         a x^b                     (x > 0, y > 0)
    'log'           a + b ln x                (x > 0)
    'logistic'      c + L / (1 + e^(-k (x - x0)))
    'interp'        straight lines through the points
    'spline'        smooth natural cubic spline through the points
    'pchip'         smooth, shape-preserving cubic through the points
                    (no overshoot between points -- good for thicknesses)
    'auto'          the best of the parametric models (by corrected AIC)

By default a regression holds its end values outside the data range
(clamp=True), so a field never runs off to unphysical values; pass
clamp=False to extrapolate.

Also:
    fit_csv(path, x_column, y_column, ...)      fit data from a CSV file
    fit_field(points, values, degree)           3D polynomial field from
                                                scattered samples (x, y, z) -> v
    interpolate_field(points, values)           a field through scattered
                                                samples (radial basis functions)

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import bisect
import csv
import math
import numbers

from fieldes.shape import Shape

__all__ = ['fit', 'fit_csv', 'Regression', 'fit_field', 'interpolate_field',
           'FieldFit']

X, Y, Z = Shape.X, Shape.Y, Shape.Z


################################################################################
# Small dense linear algebra (no numpy needed)

def _solve(A, b):
    ''' Solves A x = b (A square, lists) by Gaussian elimination with partial
        pivoting; raises ValueError if singular '''
    n = len(A)
    M = [list(map(float, A[i])) + [float(b[i])] for i in range(n)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        if abs(M[p][c]) < 1e-300:
            raise ValueError('singular system')
        M[c], M[p] = M[p], M[c]
        piv = M[c][c]
        row = M[c]
        for r in range(c + 1, n):
            f = M[r][c] / piv
            if f:
                Mr = M[r]
                for k in range(c, n + 1):
                    Mr[k] -= f * row[k]
    x = [0.0] * n
    for r in range(n - 1, -1, -1):
        s = M[r][n] - sum(M[r][k] * x[k] for k in range(r + 1, n))
        x[r] = s / M[r][r]
    return x


def _lstsq(rows, y, ridge=0.0):
    ''' Least squares: min |A p - y|^2 (+ ridge |p|^2), via normal equations '''
    m = len(rows[0])
    AtA = [[0.0] * m for _ in range(m)]
    Aty = [0.0] * m
    for r, v in zip(rows, y):
        for i in range(m):
            ri = r[i]
            Aty[i] += ri * v
            row = AtA[i]
            for j in range(i, m):
                row[j] += ri * r[j]
    for i in range(m):
        for j in range(i):
            AtA[i][j] = AtA[j][i]
        AtA[i][i] += ridge * max(AtA[i][i], 1e-30) + 1e-14 * max(AtA[i][i], 1e-30)
    return _solve(AtA, Aty)


def _levenberg_marquardt(f, p0, xs, ys, iters=200):
    ''' Minimizes sum (f(x, p) - y)^2 over p (finite-difference Jacobian) '''
    p = list(map(float, p0))
    lam = 1e-3

    def sse(q):
        try:
            s = 0.0
            for x, y in zip(xs, ys):
                d = f(x, q) - y
                s += d * d
            return s if s == s else float('inf')
        except (OverflowError, ValueError, ZeroDivisionError):
            return float('inf')

    cur = sse(p)
    for _ in range(iters):
        J = []
        r = []
        for x, y in zip(xs, ys):
            fx = f(x, p)
            row = []
            for k in range(len(p)):
                h = 1e-6 * max(1.0, abs(p[k]))
                q = list(p)
                q[k] += h
                try:
                    row.append((f(x, q) - fx) / h)
                except (OverflowError, ValueError, ZeroDivisionError):
                    row.append(0.0)
            J.append(row)
            r.append(y - fx)
        m = len(p)
        JtJ = [[sum(J[i][a] * J[i][b] for i in range(len(J))) for b in range(m)] for a in range(m)]
        Jtr = [sum(J[i][a] * r[i] for i in range(len(J))) for a in range(m)]
        improved = False
        for _ in range(12):
            A = [[JtJ[a][b] + (lam * (JtJ[a][a] + 1e-12) if a == b else 0.0)
                  for b in range(m)] for a in range(m)]
            try:
                step = _solve(A, Jtr)
            except ValueError:
                lam *= 10
                continue
            q = [p[k] + step[k] for k in range(m)]
            new = sse(q)
            if new < cur:
                rel = (cur - new) / max(cur, 1e-300)
                p, cur = q, new
                lam = max(lam / 3, 1e-12)
                improved = True
                break
            lam *= 10
        if not improved or rel < 1e-12:
            break
    return p


################################################################################
# Piecewise cubics -> fields

def _clamp_s(t, lo, hi):
    return t.max(lo).min(hi) if isinstance(t, Shape) else min(max(t, lo), hi)


class _Piecewise:
    ''' A piecewise cubic through knots t[i] with P_i(s) = y_i + b_i s +
        c_i s^2 + d_i s^3 on [t_i, t_{i+1}] (s = t - t_i), continuous '''

    def __init__(self, t, y, b, c, d):
        self.t, self.y, self.b, self.c, self.d = t, y, b, c, d

    def _poly(self, i, s):
        return self.y[i] + s * (self.b[i] + s * (self.c[i] + s * self.d[i]))

    def value(self, v, clamp):
        t = self.t
        n = len(t) - 1
        if not isinstance(v, Shape):
            x = float(v)
            if x <= t[0]:
                return self.y[0] + (0.0 if clamp else self.b[0] * (x - t[0]))
            if x >= t[n]:
                h = t[n] - t[n - 1]
                end = self._poly(n - 1, h)
                if clamp:
                    return end
                slope = self.b[n - 1] + h * (2 * self.c[n - 1] + 3 * h * self.d[n - 1])
                return end + slope * (x - t[n])
            i = bisect.bisect_right(t, x) - 1
            return self._poly(i, x - t[i])
        # field: y0 + sum_i (P_i(clamp(v - t_i, 0, h_i)) - y_i), which is
        # continuous, exact on every segment and holds the end values
        out = Shape.wrap(self.y[0])
        for i in range(n):
            s = (v - t[i]).max(0).min(t[i + 1] - t[i])
            out = out + s * (self.b[i] + s * (self.c[i] + s * self.d[i]))
        if not clamp:
            h = t[n] - t[n - 1]
            slope = self.b[n - 1] + h * (2 * self.c[n - 1] + 3 * h * self.d[n - 1])
            out = out + self.b[0] * (v - t[0]).min(0) + slope * (v - t[n]).max(0)
        return out


def _linear_pieces(t, y):
    n = len(t) - 1
    b = [(y[i + 1] - y[i]) / (t[i + 1] - t[i]) for i in range(n)]
    return _Piecewise(t, y, b, [0.0] * n, [0.0] * n)


def _natural_spline(t, y):
    n = len(t) - 1
    if n == 1:
        return _linear_pieces(t, y)
    h = [t[i + 1] - t[i] for i in range(n)]
    # second derivatives M (natural: M0 = Mn = 0), tridiagonal system
    A = [[0.0] * (n - 1) for _ in range(n - 1)]
    r = [0.0] * (n - 1)
    for i in range(1, n):
        k = i - 1
        A[k][k] = 2 * (h[i - 1] + h[i])
        if k > 0:
            A[k][k - 1] = h[i - 1]
        if k < n - 2:
            A[k][k + 1] = h[i]
        r[k] = 6 * ((y[i + 1] - y[i]) / h[i] - (y[i] - y[i - 1]) / h[i - 1])
    M = [0.0] + _solve(A, r) + [0.0]
    b, c, d = [], [], []
    for i in range(n):
        b.append((y[i + 1] - y[i]) / h[i] - h[i] * (2 * M[i] + M[i + 1]) / 6)
        c.append(M[i] / 2)
        d.append((M[i + 1] - M[i]) / (6 * h[i]))
    return _Piecewise(t, y, b, c, d)


def _pchip(t, y):
    ''' Fritsch-Carlson monotone cubic Hermite interpolation '''
    n = len(t) - 1
    if n == 1:
        return _linear_pieces(t, y)
    h = [t[i + 1] - t[i] for i in range(n)]
    delta = [(y[i + 1] - y[i]) / h[i] for i in range(n)]
    m = [0.0] * (n + 1)
    for i in range(1, n):
        if delta[i - 1] * delta[i] <= 0:
            m[i] = 0.0
        else:
            w1 = 2 * h[i] + h[i - 1]
            w2 = h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / delta[i - 1] + w2 / delta[i])

    def end(h0, h1, d0, d1):
        e = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1)
        if e * d0 <= 0:
            return 0.0
        if d0 * d1 <= 0 and abs(e) > abs(3 * d0):
            return 3 * d0
        return e
    m[0] = end(h[0], h[1], delta[0], delta[1])
    m[n] = end(h[n - 1], h[n - 2], delta[n - 1], delta[n - 2])
    b, c, d = [], [], []
    for i in range(n):
        b.append(m[i])
        c.append((3 * delta[i] - 2 * m[i] - m[i + 1]) / h[i])
        d.append((m[i] + m[i + 1] - 2 * delta[i]) / (h[i] * h[i]))
    return _Piecewise(t, y, b, c, d)


################################################################################

def _fmt(v):
    return '{:.6g}'.format(v)


class Regression:
    ''' A fitted regression.  Call it on a number (-> number) or on a field
        (-> field): reg(3.0), reg(depth_below(part)).

        Attributes: model, params, r2 (coefficient of determination), rmse,
        equation (text), domain (min and max x of the data), clamp. '''

    def __init__(self, model, fn_num, fn_field, params, xs, ys, equation, clamp,
                 n_params):
        self.model = model
        self._num = fn_num
        self._field = fn_field
        self.params = params
        self.x = list(xs)
        self.y = list(ys)
        self.domain = (min(xs), max(xs))
        self.clamp = clamp
        self.equation = equation
        self.n_params = n_params
        pred = [self._num(x) for x in xs]
        mean = sum(ys) / len(ys)
        ss_res = sum((p - y) ** 2 for p, y in zip(pred, ys))
        ss_tot = sum((y - mean) ** 2 for y in ys)
        self.sse = ss_res
        self.rmse = math.sqrt(ss_res / len(ys))
        self.r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else 1.0
        n, k = len(ys), max(1, n_params)
        if ss_res <= 0:
            self.aicc = -float('inf')
        else:
            self.aicc = n * math.log(ss_res / n) + 2 * k + (
                2 * k * (k + 1) / (n - k - 1) if n - k - 1 > 0 else float('inf'))

    def __call__(self, v):
        if isinstance(v, Shape):
            return self._field(v)
        if isinstance(v, numbers.Number):
            return self._num(float(v))
        return [self(e) for e in v]

    def on(self, field):
        ''' The regression applied to a field (same as calling it) '''
        return self._field(Shape.wrap(field))

    def residuals(self):
        ''' The data's y minus the fitted curve at each x (what the fit leaves unexplained) '''
        return [y - self._num(x) for x, y in zip(self.x, self.y)]

    def table(self, n=11):
        ''' (x, y) samples of the fitted curve across the data range '''
        a, b = self.domain
        return [(a + (b - a) * i / (n - 1), self._num(a + (b - a) * i / (n - 1)))
                for i in range(n)]

    def __repr__(self):
        return '<Regression {}: y = {}  (R^2 = {:.4f}, RMSE = {:.4g}, n = {}, x in [{}, {}]{})>'.format(
            self.model, self.equation, self.r2, self.rmse, len(self.x),
            _fmt(self.domain[0]), _fmt(self.domain[1]),
            ', held constant outside' if self.clamp else ', extrapolated')


def _pairs(x, y):
    if y is None:
        if isinstance(x, dict):
            items = sorted(x.items())
        else:
            items = [tuple(p) for p in x]
        xs = [float(p[0]) for p in items]
        ys = [float(p[1]) for p in items]
    else:
        xs = [float(v) for v in x]
        ys = [float(v) for v in y]
    if len(xs) != len(ys):
        raise ValueError('fit: x and y have different lengths')
    if len(xs) < 2:
        raise ValueError('fit: need at least 2 data points')
    return xs, ys


def _clamped(v, lo, hi, clamp):
    if not clamp:
        return v
    if isinstance(v, Shape):
        return v.max(lo).min(hi)
    return min(max(v, lo), hi)


def fit(x, y=None, model='poly', degree=2, clamp=True):
    ''' Fits a regression to data and returns a Regression, which can be
        called on numbers or fields.

        Data: fit([(x0, y0), (x1, y1), ...]) or fit(xs, ys) or fit({x: y}).
        model: 'linear', 'poly' (with degree), 'exp', 'exp_offset', 'power',
        'log', 'logistic', 'interp', 'spline', 'pchip' or 'auto'. '''
    xs, ys = _pairs(x, y)
    lo, hi = min(xs), max(xs)
    model = model.lower()

    if model == 'auto':
        best = None
        for m in ('linear', 'poly2', 'poly3', 'exp', 'exp_offset', 'power', 'log', 'logistic'):
            try:
                if m.startswith('poly'):
                    if len(xs) <= int(m[4:]) + 1:
                        continue
                    r = fit(xs, ys, 'poly', int(m[4:]), clamp)
                else:
                    r = fit(xs, ys, m, degree, clamp)
            except (ValueError, OverflowError, ZeroDivisionError):
                continue
            if best is None or r.aicc < best.aicc:
                best = r
        if best is None:
            raise ValueError('fit: no model could be fitted to this data')
        return best

    if model in ('interp', 'linear_interp', 'spline', 'pchip'):
        order = sorted(range(len(xs)), key=lambda i: xs[i])
        t = [xs[i] for i in order]
        v = [ys[i] for i in order]
        for i in range(len(t) - 1):
            if t[i + 1] <= t[i]:
                raise ValueError('fit: interpolating models need distinct x values')
        pw = {'interp': _linear_pieces, 'linear_interp': _linear_pieces,
              'spline': _natural_spline, 'pchip': _pchip}[model](t, v)
        name = {'linear_interp': 'interp'}.get(model, model)
        eq = '{} through {} points'.format(
            {'interp': 'straight lines', 'spline': 'natural cubic spline',
             'pchip': 'monotone cubic'}[name], len(t))
        return Regression(name, lambda q: pw.value(q, clamp), lambda f: pw.value(f, clamp),
                          {'knots': t, 'values': v}, xs, ys, eq, clamp, len(t))

    if model in ('linear', 'poly', 'polynomial'):
        deg = 1 if model == 'linear' else int(degree)
        if len(xs) < deg + 1:
            raise ValueError('fit: a degree-{} polynomial needs at least {} points'.format(deg, deg + 1))
        # in a scaled variable for conditioning
        m = (lo + hi) / 2
        s = (hi - lo) / 2 or 1.0
        rows = [[((xv - m) / s) ** k for k in range(deg + 1)] for xv in xs]
        c = _lstsq(rows, ys)

        def poly(u):
            out = c[deg]
            for k in range(deg - 1, -1, -1):
                out = out * u + c[k]
            return out

        # raw coefficients (for the equation text)
        raw = [0.0] * (deg + 1)
        for k in range(deg + 1):
            # c_k ((x - m)/s)^k
            for j in range(k + 1):
                raw[j] += c[k] * math.comb(k, j) * (-m) ** (k - j) / s ** k
        terms = []
        for k in range(deg, -1, -1):
            if k == 0:
                terms.append(_fmt(raw[0]))
            elif k == 1:
                terms.append('{} x'.format(_fmt(raw[1])))
            else:
                terms.append('{} x^{}'.format(_fmt(raw[k]), k))
        eq = ' + '.join(terms).replace('+ -', '- ')
        name = 'linear' if deg == 1 else 'poly{}'.format(deg)
        return Regression(name,
                          lambda q: poly((_clamped(q, lo, hi, clamp) - m) / s),
                          lambda f: poly((_clamped(f, lo, hi, clamp) - m) / s),
                          {'coefficients': raw}, xs, ys, eq, clamp, deg + 1)

    if model == 'exp':
        if min(ys) <= 0:
            raise ValueError("fit: model 'exp' needs positive y values (try 'exp_offset')")
        la, b = _lstsq([[1.0, xv] for xv in xs], [math.log(v) for v in ys])
        a = math.exp(la)
        a, b = _levenberg_marquardt(lambda xv, p: p[0] * math.exp(p[1] * xv), (a, b), xs, ys)
        return Regression('exp', lambda q: a * math.exp(b * _clamped(q, lo, hi, clamp)),
                          lambda f: a * (b * _clamped(f, lo, hi, clamp)).exp(),
                          {'a': a, 'b': b}, xs, ys,
                          '{} e^({} x)'.format(_fmt(a), _fmt(b)), clamp, 2)

    if model == 'exp_offset':
        # start from an exponential through the data shifted below its minimum
        span = (max(ys) - min(ys)) or 1.0
        best = None
        for c0 in (min(ys) - 0.1 * span, min(ys) - span, max(ys) + 0.1 * span, max(ys) + span):
            sgn = 1.0 if c0 < min(ys) else -1.0
            try:
                la, b = _lstsq([[1.0, xv] for xv in xs], [math.log(sgn * (v - c0)) for v in ys])
            except ValueError:
                continue
            p = _levenberg_marquardt(lambda xv, q: q[0] * math.exp(q[1] * xv) + q[2],
                                     (sgn * math.exp(la), b, c0), xs, ys)
            e = sum((p[0] * math.exp(p[1] * xv) + p[2] - v) ** 2 for xv, v in zip(xs, ys))
            if best is None or e < best[0]:
                best = (e, p)
        if best is None:
            raise ValueError("fit: could not fit 'exp_offset'")
        a, b, c = best[1]
        return Regression('exp_offset', lambda q: a * math.exp(b * _clamped(q, lo, hi, clamp)) + c,
                          lambda f: a * (b * _clamped(f, lo, hi, clamp)).exp() + c,
                          {'a': a, 'b': b, 'c': c}, xs, ys,
                          '{} e^({} x) + {}'.format(_fmt(a), _fmt(b), _fmt(c)).replace('+ -', '- '),
                          clamp, 3)

    if model == 'power':
        if min(xs) <= 0 or min(ys) <= 0:
            raise ValueError("fit: model 'power' needs positive x and y values")
        la, b = _lstsq([[1.0, math.log(xv)] for xv in xs], [math.log(v) for v in ys])
        a, b = _levenberg_marquardt(lambda xv, p: p[0] * xv ** p[1], (math.exp(la), b), xs, ys)
        return Regression('power', lambda q: a * _clamped(q, lo, hi, clamp) ** b,
                          lambda f: a * (b * _clamped(f, lo, hi, clamp).max(1e-30).log()).exp(),
                          {'a': a, 'b': b}, xs, ys,
                          '{} x^{}'.format(_fmt(a), _fmt(b)), clamp, 2)

    if model == 'log':
        if min(xs) <= 0:
            raise ValueError("fit: model 'log' needs positive x values")
        a, b = _lstsq([[1.0, math.log(xv)] for xv in xs], ys)
        return Regression('log', lambda q: a + b * math.log(_clamped(q, lo, hi, clamp)),
                          lambda f: a + b * _clamped(f, lo, hi, clamp).max(1e-30).log(),
                          {'a': a, 'b': b}, xs, ys,
                          '{} + {} ln x'.format(_fmt(a), _fmt(b)).replace('+ -', '- '), clamp, 2)

    if model == 'logistic':
        if len(xs) < 4:
            raise ValueError("fit: model 'logistic' needs at least 4 points")
        order = sorted(range(len(xs)), key=lambda i: xs[i])
        y0, y1 = ys[order[0]], ys[order[-1]]
        x0 = (lo + hi) / 2
        k0 = 8.0 / ((hi - lo) or 1.0)
        best = None
        for sk in (1.0, -1.0):
            p = _levenberg_marquardt(
                lambda xv, q: q[3] + q[0] / (1 + math.exp(-q[1] * (xv - q[2]))),
                (y1 - y0 if sk > 0 else y0 - y1, sk * k0, x0, min(y0, y1) if sk > 0 else max(y0, y1) - abs(y1 - y0)),
                xs, ys)
            e = sum((p[3] + p[0] / (1 + math.exp(-p[1] * (xv - p[2]))) - v) ** 2
                    for xv, v in zip(xs, ys))
            if best is None or e < best[0]:
                best = (e, p)
        L, k, xm, c = best[1]
        return Regression('logistic',
                          lambda q: c + L / (1 + math.exp(-k * (_clamped(q, lo, hi, clamp) - xm))),
                          lambda f: c + L / (1 + (-k * (_clamped(f, lo, hi, clamp) - xm)).exp()),
                          {'L': L, 'k': k, 'x0': xm, 'c': c}, xs, ys,
                          '{} + {} / (1 + e^(-{} (x - {})))'.format(_fmt(c), _fmt(L), _fmt(k), _fmt(xm)),
                          clamp, 4)

    raise ValueError("fit: unknown model '{}'".format(model))


def fit_csv(path, x_column=0, y_column=1, model='poly', degree=2, clamp=True,
            delimiter=None):
    ''' Fits a regression to two columns of a CSV file.  Columns are given
        by index or by header name; rows that aren't numbers are skipped. '''
    with open(path, newline='', encoding='utf-8-sig') as f:
        text = f.read()
    if delimiter is None:
        try:
            delimiter = csv.Sniffer().sniff(text[:2048], delimiters=',;\t ').delimiter
        except csv.Error:
            delimiter = ','
    rows = list(csv.reader(text.splitlines(), delimiter=delimiter))
    header = rows[0] if rows else []

    def col(c):
        if isinstance(c, int):
            return c
        names = [h.strip().lower() for h in header]
        if c.strip().lower() not in names:
            raise ValueError('fit_csv: no column {!r} (columns: {})'.format(c, header))
        return names.index(c.strip().lower())
    ix, iy = col(x_column), col(y_column)
    xs, ys = [], []
    for r in rows:
        try:
            xv, yv = float(r[ix]), float(r[iy])
        except (ValueError, IndexError):
            continue
        xs.append(xv)
        ys.append(yv)
    return fit(xs, ys, model=model, degree=degree, clamp=clamp)


################################################################################
# Fields from scattered 3D data

def _monomials(degree):
    out = []
    for total in range(degree + 1):
        for i in range(total, -1, -1):
            for j in range(total - i, -1, -1):
                out.append((i, j, total - i - j))
    return out


class FieldFit:
    ''' A fitted 3D field: .field (the Shape), .r2, .rmse, and call it with a
        point for its value. '''

    def __init__(self, field, fn, points, values, kind):
        self.field = field
        self._fn = fn
        self.kind = kind
        pred = [fn(p) for p in points]
        mean = sum(values) / len(values)
        ss_res = sum((a - b) ** 2 for a, b in zip(pred, values))
        ss_tot = sum((v - mean) ** 2 for v in values)
        self.rmse = math.sqrt(ss_res / len(values))
        self.r2 = 1 - ss_res / ss_tot if ss_tot > 0 else 1.0

    def __call__(self, p):
        return self._fn(p)

    def __repr__(self):
        return '<FieldFit {}: R^2 = {:.4f}, RMSE = {:.4g}>'.format(self.kind, self.r2, self.rmse)


def _points_values(points, values):
    if values is None:
        pts = [tuple(float(c) for c in p[:3]) for p in points]
        vals = [float(p[3]) for p in points]
    else:
        pts = [tuple(float(c) for c in p) for p in points]
        vals = [float(v) for v in values]
    if len(pts) != len(vals) or not pts:
        raise ValueError('need matching points and values')
    return pts, vals


def fit_field(points, values=None, degree=2):
    ''' Least-squares polynomial field in x, y, z through scattered samples,
        e.g. measured temperatures or loads at points -> a smooth field.
        points: [(x, y, z), ...] with values [v, ...], or [(x, y, z, v), ...].
        Returns a FieldFit; its .field is the Shape. '''
    pts, vals = _points_values(points, values)
    mons = _monomials(int(degree))
    if len(pts) < len(mons):
        raise ValueError('fit_field: a degree-{} field needs at least {} points'.format(degree, len(mons)))
    lo = [min(p[a] for p in pts) for a in range(3)]
    hi = [max(p[a] for p in pts) for a in range(3)]
    m = [(lo[a] + hi[a]) / 2 for a in range(3)]
    s = [((hi[a] - lo[a]) / 2) or 1.0 for a in range(3)]

    def basis(q):
        u = [(q[a] - m[a]) / s[a] for a in range(3)]
        return [u[0] ** i * u[1] ** j * u[2] ** k for i, j, k in mons]
    c = _lstsq([basis(p) for p in pts], vals, ridge=1e-12)

    def fn(q):
        return sum(ci * b for ci, b in zip(c, basis(q)))
    u = [(v - m[a]) / s[a] for a, v in enumerate((X(), Y(), Z()))]
    field = Shape.wrap(0.0)
    for ci, (i, j, k) in zip(c, mons):
        if ci == 0:
            continue
        term = Shape.wrap(ci)
        for axis, e in ((0, i), (1, j), (2, k)):
            for _ in range(e):
                term = term * u[axis]
        field = field + term
    return FieldFit(field, fn, pts, vals, 'polynomial degree {}'.format(degree))


def interpolate_field(points, values=None, smoothing=0.0, method='rbf', power=2.0):
    ''' A field passing through scattered samples (x, y, z) -> v.
        method='rbf': polyharmonic radial basis functions (smooth, exact at
        the samples unless smoothing > 0; up to a few hundred points).
        method='idw': inverse-distance weighting with the given power
        (no solve; bounded by the sample values). '''
    pts, vals = _points_values(points, values)
    x, y, z = X(), Y(), Z()

    def dist(p):
        return ((x - p[0]).square() + (y - p[1]).square() + (z - p[2]).square()).sqrt()

    if method == 'idw':
        num = Shape.wrap(0.0)
        den = Shape.wrap(0.0)
        for p, v in zip(pts, vals):
            w = 1.0 / (dist(p) ** power + 1e-9) if power != 2 else 1.0 / (
                (x - p[0]).square() + (y - p[1]).square() + (z - p[2]).square() + 1e-9)
            num = num + v * w
            den = den + w
        field = num / den

        def fn(q):
            ws = []
            for p in pts:
                d2 = sum((q[a] - p[a]) ** 2 for a in range(3))
                if d2 < 1e-18:
                    return vals[pts.index(p)]
                ws.append(1.0 / (d2 ** (power / 2) + 1e-9))
            return sum(w * v for w, v in zip(ws, vals)) / sum(ws)
        return FieldFit(field, fn, pts, vals, 'inverse distance (power {})'.format(power))

    if method != 'rbf':
        raise ValueError("interpolate_field: method is 'rbf' or 'idw'")
    n = len(pts)
    if n > 600:
        raise ValueError('interpolate_field: too many points for rbf ({}); use '
                         "method='idw' or fit_field()".format(n))
    # phi(r) = r (biharmonic in 3D) with a linear polynomial part
    A = [[0.0] * (n + 4) for _ in range(n + 4)]
    for i in range(n):
        for j in range(n):
            A[i][j] = math.sqrt(sum((pts[i][a] - pts[j][a]) ** 2 for a in range(3)))
        A[i][i] += smoothing
        row = [1.0, pts[i][0], pts[i][1], pts[i][2]]
        for k in range(4):
            A[i][n + k] = row[k]
            A[n + k][i] = row[k]
    w = _solve(A, list(vals) + [0.0] * 4)
    field = Shape.wrap(w[n]) + w[n + 1] * x + w[n + 2] * y + w[n + 3] * z
    for p, wi in zip(pts, w[:n]):
        if wi:
            field = field + wi * dist(p)

    def fn(q):
        return (w[n] + w[n + 1] * q[0] + w[n + 2] * q[1] + w[n + 3] * q[2] +
                sum(wi * math.sqrt(sum((q[a] - p[a]) ** 2 for a in range(3)))
                    for p, wi in zip(pts, w[:n])))
    return FieldFit(field, fn, pts, vals, 'radial basis functions')
