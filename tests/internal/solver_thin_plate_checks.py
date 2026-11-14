'''
The static analysis of a thin plate finishes (H193, 2026-11-14).  A steel plate 40 x 24 x t mm, held at one end and loaded across the other, meshed with two ten-node elements across its thickness:
the conjugate-gradient solve with the 3 x 3 block-Jacobi preconditioner needs more iterations the thinner the plate is (they grow with the square of length over thickness), took 78 s at t = 1.2 mm and did
not finish at t = 0.6 mm in 280 s (20,000 iterations).  A solve that stalls goes on with a smoothed-aggregation multigrid (kernel/src/fea/tet_amg.cpp); the number of its iterations does not depend on the
mesh.  The checks: the plates finish within the time given, and the tip deflection is the beam's (Euler-Bernoulli with the shear of Timoshenko) within 8 % -- the ten-node element measured 4 % under it --;
the vibration of a thin strip (LOBPCG, whose preconditioned residuals are one cycle of the multigrid) finishes and gives the beam's first frequency (it took 29 s for 2.4 mm, 118 s for 1.2 mm and could not
finish 0.6 mm with block-Jacobi conjugate gradients as the preconditioner); a thick block, which the block-Jacobi preconditioner solves in a few hundred iterations, still gives the movement of a bar in tension.  Library checks, no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import sys
import time

from fieldes import *

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''), flush=True)
    if not ok:
        bad.append(label)


L, W = 40.0, 24.0


def plate(t, element, h):
    ''' the tip deflection of the plate against the beam's, the number of elements, the seconds '''
    view.set_bounds((-5, -5, -5), (L + 5, W + 5, t + 5), auto_scale=True)
    view.set_resolution(2)
    view.set_quality(8)
    body = box((L, W, t), position=(L / 2, W / 2, t / 2))
    wall = box((1.01, W + 2, t + 2), position=(-0.495, W / 2, t / 2))
    tip = box((1.01, W + 2, t + 2), position=(L + 0.495, W / 2, t / 2))
    E, nu = steel.E, steel.nu
    I, A, G = W * t ** 3 / 12.0, W * t, E / (2.0 * (1.0 + nu))
    theory = L ** 3 / (3.0 * E * I) + L / (5.0 / 6.0 * G * A)
    t0 = time.time()
    result = static_analysis(body, supports=[fixed(wall)], loads=[force(tip, (0, 0, -1.0))], material=steel, element_size=h, element=element, cache=False)
    return abs(result.uz(L, W / 2, t / 2)) / theory, result.elements, time.time() - t0


def main():
    for t, limit in ((1.2, 120.0), (0.6, 300.0)):
        try:
            ratio, elements, seconds = plate(t, 'tet10', t / 2.0)
        except Exception as e:
            check('a ten-node plate of {} mm thickness is solved'.format(t), False, str(e)[:160])
            continue
        check('a ten-node plate of {} mm thickness ({:,} elements, two across) is solved in {:.0f} s (at most {:.0f})'.format(t, elements, seconds, limit), seconds < limit, '')
        check('... and its tip is the beam\'s within 8 % ({:+.1f} %)'.format(100.0 * (ratio - 1.0)), abs(ratio - 1.0) < 0.08, '')
    # the vibration of the same plates (LOBPCG, its residuals preconditioned by one cycle of the multigrid): the first frequency against the beam's (Euler-Bernoulli: 0.5596 sqrt(E / 12 rho) t / L^2)
    import math
    Lb, Wb = 40.0, 10.0
    for t, limit in ((1.2, 60.0), (0.6, 240.0)):
        view.set_bounds((-5, -5, -5), (Lb + 5, Wb + 5, t + 5), auto_scale=True)
        view.set_resolution(2)
        view.set_quality(8)
        strip = box((Lb, Wb, t), position=(Lb / 2, Wb / 2, t / 2))
        grip = fixed(box((1.01, Wb + 2, t + 2), position=(-0.495, Wb / 2, t / 2)))
        theory = 0.5596 * math.sqrt(steel.E / (12.0 * steel.density)) * t / Lb ** 2
        t0 = time.time()
        try:
            modes = modal_analysis(strip, supports=[grip], material=steel, modes=2, element_size=t / 2.0, element='tet10', cache=False)
        except Exception as e:
            check('the vibration of a ten-node strip of {} mm thickness is solved'.format(t), False, str(e)[:160])
            continue
        seconds = time.time() - t0
        f = float(modes.frequencies[0])
        check('the vibration of a ten-node strip of {} mm thickness is solved in {:.0f} s (at most {:.0f}; 118 s for 1.2 mm with block-Jacobi, none for 0.6)'.format(t, seconds, limit), seconds < limit, '')
        check('... and its first frequency is the beam\'s within 6 % ({:.1f} Hz against {:.1f}: {:+.1f} %)'.format(f, theory, 100.0 * (f / theory - 1.0)), abs(f / theory - 1.0) < 0.06, '')
        f2 = float(modes.frequencies[1])
        check('... and its second, the second bending, is the beam\'s (6.267 times the first) within 6 % ({:.1f} Hz against {:.1f}: {:+.1f} %)'.format(f2, 6.267 * theory, 100.0 * (f2 / (6.267 * theory) - 1.0)), abs(f2 / (6.267 * theory) - 1.0) < 0.06, '')
    # a block that block-Jacobi solves without help: the bar in tension
    view.set_bounds((-5, -5, -5), (45, 15, 15), auto_scale=True)
    view.set_resolution(2)
    view.set_quality(8)
    bar = box((40, 10, 10), position=(20, 5, 5))
    t0 = time.time()
    result = static_analysis(bar, supports=[fixed(box((1.01, 12, 12), position=(-0.495, 5, 5)))], loads=[force(box((1.01, 12, 12), position=(40.495, 5, 5)), (1000.0, 0, 0))], material=steel, element_size=2.5, cache=False)
    theory = 1000.0 * 40.0 / (steel.E * 100.0)
    got = abs(result.ux(40.0, 5, 5))
    check('a bar in tension still gives its movement ({:.4g} mm against {:.4g}, {:.1f} s)'.format(got, theory, time.time() - t0), abs(got / theory - 1.0) < 0.05, '')
    print('=' * 70)
    print('{} check(s) failed'.format(len(bad)) if bad else 'ALL CHECKS PASSED', bad)
    sys.exit(1 if bad else 0)


main()
