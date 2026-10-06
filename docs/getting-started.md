# Getting started

## Running FielDes

**Portable folder (Windows x64).** Unzip it anywhere and run `FielDes.exe`. The folder contains the
application, its libraries, a private Python runtime, the Python library, the examples and these
documents. Nothing is installed and nothing is written outside your user settings and the cache files
next to the STEP files you import.

**From source.** See [Building from source](building.md).

FielDes always opens maximised, so the model has the room. The first time, a card offers the **guided tour** (about two
minutes, on the program itself; every step and the whole tour can be skipped; **Help → Guided tour** starts it again; the
written version is the [tutorial](tutorial.md)); afterwards the split between editor and viewport and the recent files are
remembered.

## The window

```
┌───────────────────────────────────────────────────────────────────────────┐
│ [Open] [Import] │  File  Edit  View  Settings  Help                       │  top dock
├──────────────────────────┬────────────────────────────────────────────────┤
│  editor (the script)     │  viewport                                      │
│                          │   ┌ model tree ┐            ┌ section ┐ triad  │
│                          │   └────────────┘            └─────────┘        │
│                          │                     ┌ result card / legends ┐  │
├──────────────────────────┤                     └───────────────────────┘  │
│  output                  │  scale bar        cursor position              │
├──────────────────────────┴────────────────────────────────────────────────┤
│  status: render state · region · resolution · quality                     │
└───────────────────────────────────────────────────────────────────────────┘
```

The script is always the model. Every button in the viewport edits the script, so everything can be
undone (`Ctrl+Z`) and read back. The dark cards floating on the viewport (model tree, section card,
legends, result card) belong to the model; the dark top dock and status bar frame the window, and the
editor is on the light side.

See [The interface](interface.md) for every part of it.

## Your first model

Replace the script with:

```python
from fieldes import *

view.set_bounds([-12, -12, -12], [12, 12, 12])
view.set_resolution(8)
view.set_quality(8)

r = var(4)
ball = sphere(r)
hole = cylinder_z(1.5, 30, (0, 0, -15))
difference(ball, hole)
```

- The **last expression** is displayed. Variables that hold shapes (`ball`, `hole`) appear in the model
  tree; the eye shows or hides them (hidden ones get a `# hidden:` comment line).
- `var(4)` makes the number **draggable**: hover the sphere and drag its surface, or use the gizmo. The
  number in the script changes as you drag.
- `view.set_bounds`, `view.set_resolution` and `view.set_quality` say where and how finely the
  viewport meshes the shape. They are plain statements in the script.

## Making things from the menu

Right-click empty space in the viewport: **New 3D shape**, **New 2D shape**, **New point**, **New surface** and **New field** write a
call into the script where you clicked, and select the new model; **Add operation** works on the selected model. The model tree
shows each kind of thing with its own icon, and nests the models an operation is made of under it; drag a row onto an
operation to make it one of its inputs. See [The interface](interface.md#the-model-tree).

## Every number can be a field

Anywhere a number goes, a field can go:

```python
anchor = point(10, 4, 0)
swell  = ramp(distance_to_point(anchor), (0, 20), (2.0, 0.3))     # 2 mm at the anchor, 0.3 mm 20 mm away
offset(sphere(6), swell)
```

See [Fields everywhere](fields.md#fields-everywhere), and [Custom blocks](blocks.md) for your own functions that are there in
every script.

## Importing a STEP file

**File → Import model…** (`Ctrl+I`), or drag a `.step`/`.stp` file onto the window. FielDes writes

```python
# Imported model: part.step
part = import_step_parts(r"C:\path\to\part.step")
view.set_bounds(*roi(part))
view.set_resolution(roi_resolution(part))
view.set_quality(8)
```

with a variable named after the file. Once it has run, the parts appear under the import in the model tree
and, one named line each, in the script and the viewport. The eye of the import shows or hides every part. See [Importing STEP files](step-import.md).

## Running a script without the window

```
python scripts/run_example.py examples/05_static_analysis.py
```

runs it with the Python library and the kernel only (set `FIELDES_DIR` to the folder with `fieldes.dll`
if it is not found): prints appear in the console, nothing is drawn.

## Where to go next

- The [tutorial](tutorial.md): the idea, the controls that matter, habits that pay off.
- Open the scripts in `examples/` in order; each is commented (01–13 and 15 start from an imported part; 14 and 16–21 need no file).
- [Scripting](scripting.md) for the language of shapes and fields.
- [Analysis](analysis.md) and [Lattices](lattices.md) for the two big toolboxes.
