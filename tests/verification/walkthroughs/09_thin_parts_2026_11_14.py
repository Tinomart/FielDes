# THIN PARTS, 2026-11-14 (H193): what the program does with a plate that is a millimetre thick -- meshes it without losing it, solves it in seconds, draws the result in colour, says when linear tetrahedra are too stiff for it.
# About one minute to run.  Open the file (File > Open, or drag it onto the window); the console (bottom left) prints the numbers, the viewport shows the two models.
#
#  A  THE PLATE, TEN-NODE (the model `plate_result`, y = 0 .. 24).  A steel plate 40 x 24 x 1.2 mm, held at the left end, 1 N pulling down at the right end, solved with ten-node tetrahedra two across its thickness
#     (75,616 of them).  WHAT TO DO: nothing; read the console line "A: tip ...".  YOU MUST SEE: in about 8 seconds the result: the plate in colour (von Mises stress, red at the wall, blue at the tip, legend 0 - 6.7 MPa),
#     the result card with the residual curve (flat for the first 100 iterations of the plain solver, then a straight fall to 1e-7: the multigrid taking over), the tip 4 % under what beam theory with shear gives
#     (the console prints both).  Press "Elements": the outline of the elements is NOT drawn at the first view -- they are 4 pixels across or less, the outline would cover the colours -- and comes back when you zoom
#     in on a few millimetres of the plate.  (Before this day the solve took 78 seconds, and a plate of 0.6 mm did not finish.)
#
#  B  THE VIBRATION (console only).  The first two natural frequencies of the same plate cut to a strip 10 mm wide, 40 mm long, held at one end, against the beam's: 611.5 and 3832.6 Hz.  YOU MUST SEE: "B: ..." with
#     619.6 and 3861.2 Hz (+1.3 % and +0.7 %).
#
#  C  THE SAME PLATE WITH THE DEFAULTS (console only).  No element, no element size.  YOU MUST SEE: the line "static_analysis: the body has walls about 1.2 mm thick, 1.8 elements ... use the 10-node tetrahedron (mesh_tet10 for a mesh, element='tet10' for an analysis)" printed, and
#     "C: tip ..." about 21 % under the beam's: two linear tetrahedra across a plate are too stiff in bending, and the note says so instead of leaving the number to be believed.
#
#  D  A SHELL BOX, MESHED (the model `shell_mesh`, y = 50 .. 90).  A box 60 x 40 x 25 mm with walls 1.2 mm thick and an open top.  WHAT TO DO: turn the view and look into the box.  YOU MUST SEE: all four walls and the
#     floor meshed (the volume within a per cent of the exact: the console prints both), tetrahedra about as thick as the wall across it, none missing -- the default element size of a body this thin used to be 1.5 mm
#     and lose the walls.  The `modes` and `by_default` results are in the tree, hidden: tick their eye to look at them.
import math

from fieldes import *

view.set_bounds([-5, -5, -5], [65, 105, 30], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

L, W, t = 40.0, 24.0, 1.2
E, nu = steel.E, steel.nu
I, A, G = W * t ** 3 / 12.0, W * t, E / (2.0 * (1.0 + nu))
theory = L ** 3 / (3.0 * E * I) + L / (5.0 / 6.0 * G * A)          # (the tip of a cantilever: bending and shear, for a load of 1 N)

plate = box((L, W, t), position=(L / 2, W / 2, t / 2))
wall = box((1.01, W + 2, t + 2), position=(-0.495, W / 2, t / 2))
tip = box((1.01, W + 2, t + 2), position=(L + 0.495, W / 2, t / 2))

# A: ten-node tetrahedra, two across the thickness
plate_result = static_analysis(plate, supports=[fixed(wall)], loads=[force(tip, (0, 0, -1.0))], material=steel, element_size=t / 2.0, element='tet10')
print('A: tip {:.4f} mm, beam theory {:.4f} mm ({:+.1f} %)'.format(abs(plate_result.uz(L, W / 2, t / 2)), theory, 100.0 * (abs(plate_result.uz(L, W / 2, t / 2)) / theory - 1.0)))

# B: the vibration of a strip of it
Ws = 10.0
f1 = 0.5596 * math.sqrt(E / (12.0 * steel.density)) * t / L ** 2
strip = box((L, Ws, t), position=(L / 2, 50.0, t / 2))
grip = fixed(box((1.01, Ws + 2, t + 2), position=(-0.495, 50.0, t / 2)))
modes = modal_analysis(strip, supports=[grip], material=steel, modes=2, element_size=t / 2.0, element='tet10')
print('B: frequencies {:.1f} and {:.1f} Hz, beam theory {:.1f} and {:.1f} Hz'.format(float(modes.frequencies[0]), float(modes.frequencies[1]), f1, 6.267 * f1))

# C: the defaults (linear tetrahedra): the note is printed by the analysis
by_default = static_analysis(plate, supports=[fixed(wall)], loads=[force(tip, (0, 0, -1.0))], material=steel, cache=False)
print('C: tip {:.4f} mm, beam theory {:.4f} mm ({:+.1f} %)'.format(abs(by_default.uz(L, W / 2, t / 2)), theory, 100.0 * (abs(by_default.uz(L, W / 2, t / 2)) / theory - 1.0)))

# D: a shell box, meshed at the default size
outer = box((60, 40, 25), position=(30, 70, 12.5))
inner = box((57.6, 37.6, 25), position=(30, 70, 13.7))
shell = difference(outer, inner)
shell_mesh = mesh_tet4(shell)
exact = 60 * 40 * 25 - 57.6 * 37.6 * 23.8
print('D: {:,} tetrahedra, volume {:.0f} mm3 against {:.0f} mm3 ({:+.1f} %)'.format(shell_mesh.cell_count(), shell_mesh.volume(), exact, 100.0 * (shell_mesh.volume() / exact - 1.0)))

plate_result
shell_mesh
