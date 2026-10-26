# Solvers of your own

FielDes solves its analyses (static, modal, thermal, flow) in C++, because that is fast. Anything else you can write down as an equation on a mesh you can solve here, in Python, and FielDes draws the result like the result of any analysis. This page is about that: how a solver is written, where it lives, how to change the equations of one, how to bring in a solver somebody else wrote.

- [A first solver](#a-first-solver)
- [Settings > Solver](#settings--solver)
- [What a solver is](#what-a-solver-is)
- [The solvers of the library: heat and elasticity](#the-solvers-of-the-library-heat-and-elasticity)
- [Changing the equations](#changing-the-equations)
- [The analyses that are built in](#the-analyses-that-are-built-in)
- [Bringing in a solver that is not yours](#bringing-in-a-solver-that-is-not-yours)
- [Speed and limits](#speed-and-limits)
- [How it was checked](#how-it-was-checked)

## A first solver

```python
from fieldes import *

plate = box((0, 0, 0), (60, 20, 3))
cold = box((-1, -1, -1), (1, 21, 4))                 # where the plate is held at 20 degrees
hot = box((59, -1, -1), (61, 21, 4))                 # and where it is at 80

result = solve('heat', plate, fixed=[(cold, 20), (hot, 80)], conductivity=0.2)
result                                               # displayed: the temperature, coloured on the plate
print(result.max_temperature)
```

`solve(name, part, element_size=None, **parameters)` meshes `part` with tetrahedra (`mesh_volume`; give `element_size`, or give a mesh you made instead of a part), makes a `Problem` of the mesh, and calls the solver `name` with the problem and the parameters. What the solver gives back is displayed: the part is coloured by the first field it found, the other fields are behind the card's field list, and the elements behind the **Elements** button.

## Settings > Solver

The dialog lists every solver there is, in three kinds:

| Kind | What it is | What you can do |
|---|---|---|
| **(C++)** | The analyses FielDes has built in. | Read their equations. They are compiled and cannot be edited here. |
| **(library)** | Solvers written in Python that come with FielDes (`heat`, `elasticity`). | Read them, and **Make an editable copy** under a name of your own. |
| **(yours)** | Python files in your solvers folder. | Edit the source, **Save and check**, delete. |

The right side shows the equations of the selected solver (one line of Python-like text, which every solver states about itself) and its source. **New solver...** starts from a working steady-conduction solver. **Add from a file...** copies a Python file into your folder and reads it. **Open the folder** opens the folder itself: any `.py` file you put there is read, and read again when it changes, and a file you take away is forgotten. A text with a syntax error is refused with the line of the mistake and nothing is written; a file that fails when it is read is listed with *(error)* and the message.

**The way back.** A solver can be changed beyond repair, so the dialog keeps two ways to undo it. **Reset to default** puts the text the solver started with in the editor: the library solver a copy was made from, the template of a new solver, or the file as you added it (a file you put into the folder by hand has no default of its own, and the button is grey). **Previous version** puts the text the file had before its last save in the editor. Neither writes the file: the text is in the editor, `Ctrl+Z` brings back what was there, and **Save and check** keeps it. The two texts are kept beside the file, as `name.py.default` and `name.py.bak` (the folder reads only `.py` files, and deleting a solver deletes them too). The solvers of the library and the built-in analyses cannot be changed in the dialog at all, so they need no way back.

The folder is `%APPDATA%\FielDes\solvers` on Windows (`~/.config/FielDes/solvers` elsewhere), or the folder in the environment variable `FIELDES_SOLVERS`.

## What a solver is

A function with a decorator, in a `.py` file of the solvers folder:

```python
from fieldes.stdlib.solvers import solver, Sparse, tet_gradients

@solver('heat', description='Steady heat conduction',
        equation='div(conductivity * grad(T)) + source = 0')
def heat(problem, fixed=(), conductivity=1.0, source=0.0):
    K = Sparse(len(problem.nodes))
    load = [0.0] * len(problem.nodes)
    for tet in problem.tets:
        volume, g = tet_gradients(*[problem.nodes[i] for i in tet])
        for a in range(4):
            load[tet[a]] += source * volume / 4.0
            for b in range(4):
                K.add(tet[a], tet[b], conductivity * volume * (g[a][0] * g[b][0] + g[a][1] * g[b][1] + g[a][2] * g[b][2]))
    values = {}
    for region, value in fixed:
        for i in problem.nodes_in(region):
            values[i] = value
    return {'temperature': K.solve(load, values)}
```

This is the structure of any finite-element solver, and the same one OpenFOAM's solvers have: a mesh, a matrix assembled element by element from the equation, boundary values, a solve, fields. Here it is a page of Python.

**What the solver is given**

| | |
|---|---|
| `problem.nodes` | `[(x, y, z), ...]`: every node of the mesh |
| `problem.tets` | `[(a, b, c, d), ...]`: the nodes of every tetrahedron |
| `problem.nodes_in(region)` | the nodes in a body, on a surface selection, or where a field is at most zero (within `tolerance=` mm beyond it; default a twentieth of an element) |
| `problem.boundary_nodes()` | the nodes on the surface of the part |
| `problem.element_size` | about how large an element is (mm) |
| `problem.mesh`, `problem.body`, `problem.parameters` | the `Mesh` (see [meshing.md](meshing.md)), the part, and the parameters of the call |
| `problem.per_tet(values)` | the mean of a nodal field over each tetrahedron |

**The tools**

| | |
|---|---|
| `tet_gradients(p0, p1, p2, p3)` | `(volume, [g0, g1, g2, g3])`: the volume of a tetrahedron and the gradients of its four linear shape functions |
| `Sparse(n)` | a symmetric matrix of `n` rows. `.add(i, j, value)` puts a number in, `.multiply(x)`, `.solve(b, fixed={node: value})` finds `x` with `A x = b` by conjugate gradients, the fixed values taken out |

**What it gives back**: a dictionary of fields, one number per node, `{'temperature': [...]}`, or a pair `(fields, numbers)` with `numbers = {'max_temperature': 80.0}`. A field is `result.fields['temperature']` and `result.temperature`; a number is `result.max_temperature`. If the number of values of a field is not the number of nodes, the call says so.

## The solvers of the library: heat and elasticity

**`heat`**: `div(conductivity * grad(T)) + source = 0`, mm, W, degrees.

| Parameter | |
|---|---|
| `fixed=[(region, temperature), ...]` | where the temperature is given. Required. |
| `conductivity` | W/mm/K: a number, **or a function `f(T, x, y, z)`**. |
| `source` | W/mm3: a number, or a function `f(T, x, y, z)`. |
| `iterations` | the most iterations when a function makes the problem nonlinear (the conductivity of the last iteration is the next one's) |

Gives the field `temperature` and the numbers `max_temperature`, `min_temperature`.

**`elasticity`**: the equations of the static analysis, with small strains: `div(stress) + density * gravity = 0`, `stress = lambda * trace(strain) * I + 2 * mu * strain`, `strain = sym(grad(u))`; mm, N, MPa.

| Parameter | |
|---|---|
| `fixed=[(region, (ux, uy, uz)), ...]` | where the displacement is given (mm); `None` for a direction that is free; one number for all three. Required. |
| `forces=[(region, (fx, fy, fz)), ...]` | the total force (N) on a region, shared by the area of the surface it is on |
| `young` | MPa: a number, **or a function `f(equivalent_strain, x, y, z)`**: the stiffness where the strain is that (the secant modulus, found by iterating) |
| `poisson` | Poisson's ratio |
| `gravity`, `density` | a body force: `gravity=(0, 0, -9810)` mm/s2 with `density=7.85e-9` t/mm3 |

Gives the fields `von_mises`, `displacement`, `ux`, `uy`, `uz` and the numbers `max_von_mises`, `max_displacement`.

```python
bar = box((0, 0, 0), (40, 10, 10))
held = box((-1, -1, -1), (0, 11, 11))
pulled = box((40, -1, -1), (41, 11, 11))
result = solve('elasticity', bar, element_size=2.5,
               fixed=[(held, (0, 0, 0))], forces=[(pulled, (10000, 0, 0))], young=210000)
print(result.max_von_mises, result.max_displacement)          # 100 MPa, 0.019 mm: sigma = F / A, u = sigma L / E
```

## Changing the equations

This is what **Make an editable copy** is for. The copy is a file of your folder with the whole solver in it, equations included. A few changes that are one line each:

```python
# a conductivity that grows with the temperature (copy of heat; or without a copy: the parameter takes a function)
result = solve('heat', plate, fixed=[(cold, 20), (hot, 80)], conductivity=lambda T, x, y, z: 0.2 * (1 + 0.004 * (T - 20)))

# a material that softens with the strain
result = solve('elasticity', bar, fixed=[(held, (0, 0, 0))], forces=[(pulled, (30000, 0, 0))],
               young=lambda eq, x, y, z: 100000 / (1 + 50 * eq))

# a source of heat that is only in one place
result = solve('heat', plate, fixed=[(cold, 20)], source=lambda T, x, y, z: 0.01 if x > 40 else 0.0)
```

The change of the *form* of the equation (a different coupling, another term in the stiffness, a different element) is a change of the copy's source. The text in `equation=` is what the list in the dialog shows: keep it true.

## The analyses that are built in

The dialog shows the equations of the analyses FielDes solves in C++, because they are what the numbers of the result cards come from:

| Analysis | Equations |
|---|---|
| static analysis | `div(sigma) + f = 0`, `sigma = C : eps(u)`, linear tetrahedra (or 10-node) |
| modal analysis | `K phi = omega^2 M phi` |
| thermal analysis | `div(k grad(T)) + q = 0`, with fixed temperature, flux, convection |
| flow analysis | steady incompressible Navier-Stokes, stabilised linear elements |

These are compiled, and they are **not** editable from here: they would not be fast any more if they were Python. To try another law of the material or another source, use the Python copies above (`elasticity` and `heat` are the static and the thermal analysis in Python); to change what the built-in analyses do, the source of FielDes is open.

## Bringing in a solver that is not yours

1. **A Python file** (a colleague's, one from a book): **Add from a file...** in the dialog, or put it in the solvers folder. It has to register a solver with `@solver('name')`; a file that registers none is refused, with the reason.
2. **A program of its own** (OpenFOAM, CalculiX, Elmer): the way is the files. Mesh the part, write the mesh in the format it reads ([meshing.md](meshing.md): `.inp` for Abaqus and CalculiX, `.msh`, a `polyMesh` folder for OpenFOAM, ...), run the program, read the fields it wrote back as one number per node:

```python
import subprocess

@solver('external', description='A solver of another program, through files', equation='whatever the program solves')
def external(problem, program='ccx', job='job'):
    problem.mesh.export(job + '.inp')
    subprocess.run([program, job], check=True)
    values = read_my_results(job + '.dat')              # yours: one number per node, in the order of problem.nodes
    return {'result': values}
```

This is a skeleton and not a finished solver: the reading of the program's results is the part that is yours.

A solver may use anything the Python of FielDes has. The solvers here need only the standard library; a package you import (NumPy, say) has to be installed in FielDes's own Python, in the `runtime` folder.

## Speed and limits

- It is Python, so it is as slow as Python is. A mesh of a few thousand nodes takes seconds; a hundred thousand is for the built-in analyses. A matrix of `n` rows has `n` dictionaries in it.
- The mesh is tetrahedra with linear shape functions. A solver of 10-node elements, hexahedra or quadrilaterals is written the same way from `problem.mesh.cells`, with its own shape functions.
- The solvers of the library are steady: no time stepping. The built-in flow analysis has time, and a solver of your own can step in time itself (a loop, calling `Sparse.solve` once per step).
- `Sparse.solve` is conjugate gradients with a diagonal preconditioner: for a symmetric positive definite matrix. A convection term or a nonsymmetric matrix needs a solver of your own.
- A solver that finds a field the mesh does not have the length for, or does not give back a dictionary, is refused when it is called, with what it should have given.

## How it was checked

`tests/internal/solver_checks.py` (plain Python, a minute): the bar held at two temperatures against the straight line (to what the mesh's own precision allows: the nodes the temperature is given on lie within about 1e-4 mm of the faces); the same with a conductivity that grows with the temperature against the line of Kirchhoff's transform; a bar pulled to a strain against `E * strain` in every node; pulled by a force against `F / A` and `F L / (A E)`; hanging by its weight against `rho g L^2 / 2E`; a softening material against the secant modulus; a solver written from nothing (Poisson's equation) against its parabola; the solvers folder (a copy, a changed file read again, a broken file refused, a file taken away forgotten); the refusals. The numbers are in [validation.md](validation.md).
