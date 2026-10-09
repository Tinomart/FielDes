# Tutorial: your first hour with FielDes

This is a short walk through what FielDes is for, how you work in it, and the few controls that matter most.

> **The program has its own tour.** The first time FielDes starts it offers a **guided tour**: about two minutes of short
> cards over the program itself, each dimming everything but what it is about, pointing at it, and letting you do the one
> thing it asks (or doing it for you once: **Show me**, which puts everything back as it was so that you can do it yourself; **Back**
> does the same: each step has a state of its own). Every step can be skipped, and so can the whole tour; **Help → Guided
> tour** starts it again. This page is the written version, with the reasons.

Follow it with the program open: **Help → Guided tour** loads a small model of the kind it uses, and everything below can
be tried on it.

- [The idea](#the-idea)
- [The window](#the-window)
- [1. The script is the model](#1-the-script-is-the-model)
- [2. Drag, and the script follows](#2-drag-and-the-script-follows)
- [3. Make things with the right mouse button](#3-make-things-with-the-right-mouse-button)
- [4. The model tree](#4-the-model-tree)
- [5. Every number can be a field](#5-every-number-can-be-a-field)
- [6. Your own blocks](#6-your-own-blocks)
- [7. Real parts and real analysis](#7-real-parts-and-real-analysis)
- [The controls that matter](#the-controls-that-matter)
- [Habits that pay off](#habits-that-pay-off)
- [Where next](#where-next)

<p align="center">
  <img src="images/overview.png" width="900" alt="FielDes: the script on the left; the viewport, with the model tree, on the right">
</p>

## The idea

FielDes is built on three ideas.

**The script is the model.** What you see in the viewport is what a Python script says, and nothing else. Every button and
every drag in the viewport is an *edit of that script* — you can read it, undo it with `Ctrl+Z`, keep it, share it, and
change it by hand. There is no hidden state and nothing to "rebuild".

**Everything is a field.** A shape is a number at every point of space (negative inside the body, positive outside). So is a
distance, a ramp, noise, a temperature, the stress an analysis finds. They all combine with ordinary arithmetic, and any of
them can stand wherever a number stands: *offset this part by a thickness that depends on where you are*, *make the cells of
this lattice smaller where the stress is high*, *load this face harder at one end*. Anything can drive anything.

**Direct manipulation, written down.** Drag a surface, or a gizmo, in the viewport, and the numbers in the script change;
change a number in the script, and the viewport follows. You get the immediacy of a modeller and the precision and
repeatability of code.

## The window

| | |
|---|---|
| **Left: the editor** | the script, with colouring, completion (`Ctrl+Space`), call tips, go to definition (`Ctrl+click`, `F12`, also into the library: it opens in a tab), find (`Ctrl+F`). Below it, the **output**: what the script printed, and errors, with the line underlined |
| **Right: the viewport** | the model, drawn by meshing the script's fields. Rotate: `Shift`+left-drag or middle-drag. Pan: right-drag. Zoom: the wheel. Double-click a model to frame it, the background to frame all (`Home`) |
| **Top left of the viewport: the model tree** | one row per thing the script makes, each with the icon of what it is: a 3D shape, a 2D shape, a field, a surface, a point, a simulation ... |
| **Top: the menus** | *File* (open, import, save, export), *Edit*, *View*, *Settings* (shortcuts, the blocks folder), *Help* (this tutorial, the guide `Shift+F1`, the library reference `F1`) |

## 1. The script is the model

The tour's model (**Help → Guided tour**) starts with

```python
from fieldes import *

view.set_bounds([-35, -25, -8], [35, 25, 30])    # the region the viewport meshes
view.set_resolution(6)                           # samples per mm
view.set_quality(8)
```

and builds a plate with a hole. Change `box_exact((-30, -20, 0), (30, 20, var(6)))` — make the plate 12 mm thick — and the
viewport updates a moment after you stop typing. The **last expression** of a script is what is shown; give other things names
(`plate = ...`) and they appear in the tree, where the eye shows or hides each. (A bare name on a line of its own, `plate`,
shows it too.)

**Resolution** is the one setting to know: more samples per mm = a finer picture and a slower one. Start low; raise it for
a look at the detail. `Esc` cancels a render that is taking too long.

## 2. Drag, and the script follows

`var(6)` in the welcome script is a **draggable number**. Hover the top of the plate — it lights up — and drag it: the 6 in the
script changes as you move. Any number you wrap in `var(...)` can be pulled this way.

You do not have to write `var` yourself. Click a model in the viewport (or its row in the tree): FielDes gives it a
**gizmo** — three arrows to move, three rings to rotate, three squares to scale, and a dot in the middle that moves it freely
— and numbers on its surfaces to pull. What it adds is plain script (`plate = handles(plate, move=(var(0), ...))`).

Three keys (with the viewport focused) turn this on and off on the selected models:

| Key | |
|---|---|
| `E` | when the gizmo is shown: while the model is selected → never → always |
| `R` | **lock**: a locked model cannot be dragged at all (`plate = lock(plate)`) |
| `V` | show / hide |

## 3. Make things with the right mouse button

Right-click empty space in the viewport:

<p align="center">
  <img src="images/menu.png" width="520" alt="The right-click menu: New 3D shape, New 2D shape, New point, New surface, New field, New custom block, Add operation">
</p>

- **New 3D shape / 2D shape / point / surface / field** write a call into the script where you clicked — a sphere, a box, a
  circle, a plane, a distance field ... — and select the new model, ready to drag. Each kind has its icon and colour.
- **Add operation** works on the selected model: offsets and walls, rounding, moving, repeating, combining. Right-click a
  *model* for the same list written with that model as its input. With several models selected, *union*, *difference*,
  *intersection* act on all of them, in the order you selected them (hold `Ctrl` to select more than one).

- **Simulation** (on a model): *static_analysis*, *modal_analysis*, *topology_optimization*, *thermal_analysis*, *fluid_analysis*,
  written with that model as the part and a first set of supports and loads laid on its ends, for you to change.

The same menu opens **on a line of the script that defines a model, and on its row in the model tree** -- right-click there and it
is as if you had right-clicked the model in the viewport. (On a line that defines no model it is the menu of empty space, and what
you make goes under that line.)

It is just a quick way to write script. Look at what it wrote, and change it.

## 4. The model tree

The tree shows how the model is built: **the models an operation is made of are its children.**
`thick = offset(plate, 1)` puts `plate` under `thick`; `plate = difference(base, hole)` puts `base` and `hole` under `plate`.
What nothing else uses is at the top. A model that two statements use has its row under the first and a half-transparent
**shadow** under the other: that statement's own reference to it.

<p align="center">
  <img src="images/tree.png" width="420" alt="The model tree: typed icons, nested children, a dim 'uses' row">
</p>

- **Click** a row to select the model: its code lights up in the editor, it is highlighted in the viewport.
  **`Ctrl`+click** adds a row, **`Shift`+click** selects a range. In the viewport, drag a rectangle to select every body inside it.
- **The tree is the structure of the calls**, so moving a row in it always changes arguments. **Drag a row onto an operation**
  to make it one of that operation's inputs (it is added to a `union`; no menu asks anything); drag it out of
  one to take it out of that call; drop it **between the children** of an operation to put it at that place among its arguments.
  **`Ctrl`+drag** puts a new shadow where you drop it and moves nothing (a shadow cannot go above its original: the program says
  so and offers the easy fix). Taking a model
  out of an operation that cannot do without it leaves a placeholder (`...`) in its place: that statement (and what is made from it) waits until something is written,
  and a model dropped on the placeholder's row fills it. All of it is edits of the script: one
  `Ctrl+Z` undoes a drop.
- Rows have buttons: the **eye**, the **gizmo**, the **lock**, the **render cache** (keeps the finished mesh, so a big
  model comes back at once), and a **bin** (deletes the model's statements; `D` does it for the selected models). A shadow has a
  bin too: it takes only that reference out of its call.
- Long scripts: a comment `#SECTION Title` gets a fold arrow in the editor's gutter and folds everything up to the next
  `#SECTION` (type `#sec` and complete it).
- `I` **isolates** the selected models (everything else hidden); `I` again brings the rest back.

## 5. Every number can be a field

This is the part of FielDes that is not like other modellers. Anywhere the script has a number — a size, a radius, a wall
thickness, a spacing — you can write a **field** instead. In the welcome script:

```python
anchor = point(20, 10, 6)                                     # a point: a model, and a value wherever a coordinate goes
swell  = ramp(distance_to_point(anchor), (0, 45), (3.0, 0.5))   # 3 mm at the anchor, falling to 0.5 mm 45 mm away
swollen = offset(drilled, swell)
```

`offset(drilled, 1.0)` would grow the plate by a millimetre everywhere. With a field it grows by an amount that depends on
the place — and the call looks the same. Drag the anchor's gizmo: the swelling follows it. You can also do it with the
mouse: **drag the field's row in the model tree onto the `offset` row**, and it takes the place of the `1.0`.

The fields to start with:

| | |
|---|---|
| `x_field()`, `y_field()`, `z_field()` | the coordinates |
| `distance_to_point(p)`, `distance_to_plane(...)`, `depth_below(shape)` | distances |
| `ramp(field, (a, b), (va, vb))` | map a field linearly (clamped): the workhorse |
| `noise_field(scale)` | organic variation |
| `result.von_mises`, `result.temperature` | the stress or temperature an analysis found |

Every function of the library follows the rule — see [Fields everywhere](fields.md#fields-everywhere) for where, and for the
few numbers (counts, tolerances, the Poisson's ratio) that can only be numbers. A lattice whose cells get smaller towards a
corner is one line: `lattice(body, cell_periodic('gyroid'), cell_size=ramp(x_field(), (0, 80), (16, 6)), thickness=1)`.

## 6. Your own blocks

When you write something you will want again, make it a **block**: put a function in a `.py` file of the blocks folder and it
is there in *every* script, like `sphere()` or `lattice()`, with its call tip, completion and menu entry.

```python
# blocks/my_blocks.py
def perforate(body, hole_radius=2.0, spacing=10.0):
    ''' Drills a grid of round holes right through a body, along z '''
    holes = repeat(cylinder_z(hole_radius, 100000, (0, 0, -50000)), (spacing, spacing, 0))
    return difference(body, holes)
```

**Settings → Blocks folder…** chooses the folder; save the file and the scripts that use it run again. Models made by a block
carry a small **f** in the tree. See [Custom blocks](blocks.md).

## 7. Real parts and real analysis

- **File → Import model…** (`Ctrl+I`) brings in a STEP file (every solid becomes a field; an assembly arrives assembled) or a
  mesh (STL, OBJ, PLY, 3MF, glTF). The script gets the import line, and each part gets a row in the tree.
  See [Importing STEP files](step-import.md).
- **Analysis** is script too: `static_analysis(part, supports=[fixed(region)], loads=[force(region, (0, 0, -200))], material=aluminium)` --
  every kind of condition is an input of its own, and every condition a model of its own, with its own eye in the tree. Supports and loads are *regions* — shapes you place — not
  clicked faces. The result is a field: stated on its own it is drawn on the part, with the **result card** (the field to
  show, the load step, the deformation, the elements); `part - 0.02 * result.von_mises` uses it as geometry.
  Materials and loads take fields too: `Material('graded', ramp(x_field(), (0, 100), (69e3, 7e3)), 0.33)`. See
  [Analysis](analysis.md).
- The **section card** (`Ctrl+Shift+X`) cuts the model and paints the field on the plane; **hover** a coloured model to read
  the value under the cursor. It is a viewer you switch on yourself. Select a *field* in the model tree and the **field viewer**
  opens beside it, showing the field on a disc: the two are separate, and both can be open at once.

The `examples/` folder has a script for each of these. The ones that need no STEP file are 14, 16, 17, 18, 19, 20 and 21.

## The controls that matter

| | |
|---|---|
| `Ctrl+O` / `Ctrl+S` / `Ctrl+Z` / `Ctrl+Y` | open / save / undo / redo (an edit made by the viewport is one undo) |
| `Ctrl+I` | import a model |
| `Ctrl+Space`, `Ctrl+click`, `F12`, `Ctrl+F` | complete, go to definition, find |
| `Shift`+left-drag (or middle-drag), right-drag, wheel | rotate, pan, zoom; `1` front, `3` right, `7` top, `0` isometric, `Home` frame all |
| left-drag a rectangle, `Ctrl`+click, `Shift`+click (tree) | select |
| right-click | make things / operate on a model |
| `E` `R` `V` `C` `I` `D` (viewport focused) | gizmo mode, lock, show / hide, render cache, isolate, delete |
| `Esc` | cancel a slow render |
| `Ctrl+Shift+X`, `Ctrl+Shift+T` | the section card, the model tree |
| `Shift+F1`, `F1` | the guide, the library reference |
| `Ctrl+K Ctrl+S` | every shortcut, rebindable |

## Habits that pay off

- **Name things.** `bracket = ...` rather than one long expression: the tree shows it, the gizmo can hold it, and you can use
  it twice.
- **Keep the resolution low** while you build; raise `view.set_resolution` to look at detail. A model that is slow to mesh
  can be kept by the render cache button.
- **Hide, do not delete,** what you may want back: the eye writes a `# hidden:` comment.
- **Put what you reuse in a block.** A block is the shortest way from "I did this twice" to a function.
- **Let a field do the work.** Before writing a dozen exceptions ("thicker here, thinner there"), ask what single field
  describes the rule: a distance, a ramp, a stress.
- **Read what FielDes wrote.** It writes ordinary script — the quickest way to learn the library is to make something from
  the menu, look at the line, and edit it.

## Where next

| | |
|---|---|
| [The interface](interface.md) | every card, button and key |
| [Scripting](scripting.md) | shapes, arithmetic on fields, view settings, what gets displayed |
| [Fields](fields.md) | the fields of the library, regressions, data as fields |
| [Custom blocks](blocks.md) | your own functions in every script |
| [Handles](handles.md) | the gizmo, native handles, `expose()` |
| [Analysis](analysis.md) | static, modal, thermal, topology optimization, flow |
| [Lattices](lattices.md) | cells, graded and conformal lattices |
| [Importing STEP files](step-import.md) | how a CAD part becomes a field |
| [Library reference](reference.md) | every function, from its docstring |
