# FielDes apps: a script that anyone can run

**Custom blocks are for FielDes users. Apps are for everyone else.** An engineer writes a script once -- a part that
is made to fit a pipe, a lattice for a model, a batch of exports -- and says which things the person who uses it may
choose: a radius, a model file, where to save the result. FielDes turns that into **one file** (`bracket_maker.fieldesapp`).
Whoever opens it sees **a form with those inputs, a Run button, a log and the files that were made** -- no editor, no
model tree, no code -- and presses Run.

```
 ┌ Bracket maker  1.0 ───────────────────────────────────────────┐
 │ Hollows a part out by a wall thickness and saves it as a mesh   │
 │                                                                 │
 │  Model            [ C:\parts\bracket.step ]      [Browse...]    │
 │  Mode             [ Hollow ▾ ]                                  │
 │  Wall thickness   [ 3.00 mm ]                                   │
 │  Export path      [ C:\Users\me\Documents\bracket.stl ] [Save as...]
 │                                                                 │
 │  [ Run ]   ☐ Run again when a file changes                      │
 │  Done in 2.8 s.                                                 │
 │  Outputs: Export path  C:\...\bracket.stl  6.9 MB  [Open] [Show in folder]
 └─────────────────────────────────────────────────────────────────┘
```

- [The three roles](#the-three-roles)
- [Write the script: declare the inputs](#write-the-script-declare-the-inputs)
- [A viewport: look at it, drag it](#a-viewport-look-at-it-drag-it)
- [Try it: Preview as app](#try-it-preview-as-app)
- [Export it: one file](#export-it-one-file)
- [Open it: a double click](#open-it-a-double-click)
- [Run it without a window](#run-it-without-a-window)
- [Watch: run again when a file changes](#watch-run-again-when-a-file-changes)
- [What is inside a `.fieldesapp`](#what-is-inside-a-fieldesapp)
- [Limits](#limits)

## The three roles

| | Who | Does | Sees |
|---|---|---|---|
| **Engineer** | writes the script | declares inputs with `app.`, previews, exports | the editor, as always |
| **User** | an artist, a designer, a colleague | opens the app file, fills in the form, presses Run | the form, the log, the outputs |
| **A program** | a build server, a batch file | `FielDes run bracket.fieldesapp --Radius 7` | the console |

The script is an **ordinary FielDes script**: everything in this documentation works in it. The only difference is that
the numbers and files a person should choose are not typed in the code but asked for with `app.`.

## Write the script: declare the inputs

```python
from fieldes import *

app.title('Bracket maker', 'Makes the bracket for a pipe and saves it as a mesh', '1.0')

radius   = app.number('Radius', 5, min=1, max=20, unit='mm')
material = app.choice('Material', ['steel', 'aluminium'])
model    = app.file('Model', filter='STEP files (*.step *.stp)')
target   = app.save_as('Export path', 'bracket.stl')

part = import_model(model)[0][0]
...
export_mesh(part, target)
```

| Call | Is | The form shows | Returns |
|---|---|---|---|
| `app.title(name, description, version)` | the heading | the title bar and the top of the window | the name |
| `app.number(name, default, min=, max=, step=, unit=, help=)` | a number | a number box (whole numbers when `default`, `min`, `max` and `step` are whole) | an `int` or a `float` |
| `app.text(name, default, help=)` | a text | a line of text | the text |
| `app.flag(name, default, help=)` | yes / no | a check box | `True` or `False` |
| `app.choice(name, options, default, help=)` | one of a few | a drop-down | the chosen option (a text) |
| `app.file(name, default, filter, help=)` | a file to read | a path and **Browse...** (a file can be dropped on the window) | the full path; it must exist |
| `app.save_as(name, default, filter, help=)` | where to save the result | a path and **Save as...** | the full path; the folder is made |
| `app.folder(name, default, help=)` | a folder | a path and **Choose...** | the full path |
| `app.asset('step/part.step')` | a file that comes along inside the app | nothing | the full path of that file |
| `app.output('report.txt', label)` | a file the script makes | nothing; listed under **Outputs** | the full path |
| `app.in_app()` | whether this is an app run | nothing | `True` as an app, `False` in the editor |
| `app.running()` | whether the person pressed Run | nothing | `True` for Run and the command line; `False` for a picture of the viewport and in the editor |
| `app.viewport(models, ...)` | a picture the app shows | a viewport beside the form | a statement of its own: [see below](#a-viewport-look-at-it-drag-it) |

**Rules that keep it simple:**

- **Write the inputs with plain values** -- numbers, texts, lists -- and **at the top level** of the script, not inside a
  function or a loop. The form is read from the *text* of the script, so it is there before the script has run, and
  `app.number('Radius', compute_default())` cannot be shown (FielDes says so). `app.asset('...')` as the default of a file
  is fine.
- **The name is what the form shows.** On the command line `--Radius`, `--radius` and `--RADIUS` are the same option, and so are
  `Model path`, `ModelPath` and `Model_path`. Two inputs must not have the same name.
- **A relative output path means the user's Documents folder** (or the folder the command was started in, for `FielDes run`):
  `app.save_as('Export path', 'bracket.stl')` shows `C:\Users\me\Documents\bracket.stl`, which the user can change.
- **Every value is checked before the script runs**: a number outside `min`..`max`, a file that is not there, a choice that is not
  one of the options. The message is written under the input, in red, and the script does not start.
- **In the editor, an input is its default.** `app.number('Radius', 5)` is `5`, `app.file('Model', 'step/part.step')` is that
  path: the script runs as any script does, and you work on it there. The Run button's values only exist when it is run as an app.
- **Write files only when it is an app run:** in the editor every change of the script runs it again, and it should only *show*
  the result there. `if app.in_app(): export_mesh(part, target)` does both: the editor shows the part, the app window writes the file.
- What the script **prints** goes to the log. A mistake in the script shows the error and its line in the log, in red.

## A viewport: look at it, drag it

An app can show a picture. **It is the app creator's choice** -- an app that only exports a file has none -- and what is in it is whatever the creator gives
`app.viewport(...)`, as a statement of its own:

```python
body = beam
if app.running():
    result = static_analysis(beam, supports=[support], loads=[load], material=steel, element_size=3)
    body = result

app.viewport({'Beam': body, 'Load point': pull}, support, load)
```

- **The models.** A dict `{'name': model}` (a list, or one model, shows them without names), then, if you like, more models without a name: a body, a point, an
  analysis result, a condition (`force(...)`, `fixed(...)`: drawn on the body with its arrows and pads, as in the editor). The viewport is on the right of the form; the
  mouse works as in the editor (rotate, pan, zoom, the standard views of the triad).
- **The names** are drawn over the models, in the style of the labels of boundary conditions (a small card, over the top of the model; over a point, over its dot).
  `labels=False` leaves them out.
- **Dragging.** Click a model to select it: its gizmo shows, and its surfaces can be pulled as in the editor, **if the model has numbers to drag** -- `gizmo(...)`,
  `handles(...)` or `var()`s -- **and is not locked**. `part = lock(part)` is the creator's switch: a locked model can be looked at and selected, never moved. There is
  no model tree and no context menu, and nothing else can be changed in the picture. What is dragged is written into the numbers of the app's script (the person never
  sees them), and the script runs again for the picture, so everything that depends on them follows -- the arrows of a load, a region, a lattice. **Reset the picture** (under
  the viewport, when something was dragged) takes it all back. The edits are not saved in the app file.
- **A point is its dot** -- the middle point of a gizmo, always drawn while the point is shown. Click it to select the point, then drag the dot (freely, in the plane
  facing you) or an arrow. That makes a point the simplest thing to give a person to place: a load, a start, a centre.
- **The picture is made on its own:** when the app is opened, a moment after an input changes, and after something was dragged. Those runs have `app.running()`
  `False`: put what takes long and what writes files under `if app.running():`, which only the Run button (and the command line) makes `True`. The result of Run is shown
  in the viewport too (as the `body` above), until the next picture. In the editor the script runs as always and `app.running()` is `False`.
- **The region** of the picture is the script's: `view.set_bounds(...)` (and `view.set_resolution(...)`) as in any script; a model dragged out of it is not drawn.
- `examples/25_app_beam_load_case.py` is a complete app of this kind: drag the load point along the beam, choose the force, press Run.

## Try it: Preview as app

**File → Preview as app** opens the window that the user will see, on the script as it is now, as a program of its own beside
the editor. Change the script and **save it** (or press Preview again): the app window reads the new version by itself, keeps what was
typed into the form, and shows the new inputs. This is the loop of building an app: edit, save, look.

## Export it: one file

**File → Export as app...** bundles, into one `.fieldesapp` file:

- the **script** (as it is in the editor, saved or not);
- the **custom blocks** it uses -- only those, and the blocks they use -- from your blocks folder;
- the **files it needs**: every file named in `app.asset('...')`, every relative path in the script that names a file next to it
  (`"step/bracket.step"`), and the Python modules next to the script that it imports.

A window says what went in (the inputs, the blocks, the files, the size) and **what is wrong**: a script that declares no inputs, an
input that is not a plain value, a file that is not there, a path that exists only on your computer (`C:\cad\part.step` -- make it an
input with `app.file`, or bring it along with `app.asset`). Send the file; nothing else is needed on the other computer but FielDes.

The first time you export an app, FielDes asks whether a double click on a `.fieldesapp` file should open FielDes. **Settings → Open
.fieldesapp files with FielDes** does the same at any time.

From a command line or a build script: `FielDes export script.py -o bracket_maker.fieldesapp`.

## Open it: a double click

A `.fieldesapp` file opens in the app window. **Run** starts the script with the values in the form; the button becomes **Stop**,
the bar shows how far the script has got, and when it is done the **Outputs** list has the files the script made, with
**Open** and **Show in folder**. Closing the window while the app runs asks first.

The form **remembers** what was chosen last time, per app. A file dropped on the window goes into the first file input (the one whose
filter has its extension). `Ctrl+Return` runs. File → **Read the app again** (F5) re-reads the file.

The windows are programs of their own: you can have several apps open, and the editor beside them.

An app opens with these options after its file: `FielDes bracket.fieldesapp --Radius 7 --Material aluminium --run` fills in the form
and presses Run (without `--run` it waits for you).

## Run it without a window

```
FielDes run bracket_maker.fieldesapp --Mode Hollow --Wall_thickness 2.5 --Export_path C:\parts\bracket.stl
```

Inputs are written `--name value` (or `--name=value`); a yes / no input is `--Hollow`, `--no-Hollow` or `--Hollow yes`; inputs left
out have their defaults. `FielDes run app --help` lists the inputs of that app. The command prints what the script prints, and
at the end the files that were made. The exit code says how it went: **0** done, **1** the script failed (the message says at which
line), **2** the command line was wrong (an input that does not exist, a value that does not fit).

| Option | |
|---|---|
| `--watch` | run again whenever an input file or the app itself changes; `Ctrl+C` ends it |
| `--log FILE` | write what is printed to FILE as well |
| `--quiet` | print only errors |
| `--folder DIR` | a relative output path means DIR (the folder the command was started in by default) |

`FielDes.exe` is a window program: a command typed at a Windows prompt does not wait for it. In a batch file or a build script use
**`fieldes.cmd`**, in the folder FielDes is installed in (`fieldes run app.fieldesapp --Radius 7`): it waits, passes on the exit code, and its
output goes where you redirected it (`> log.txt`, `| findstr ...`, or `--log FILE`). On Linux `fieldes` (the command the installers make) is a
normal command. The library itself runs apps too, with nothing of the window in it: `python -m fieldes run app.fieldesapp --Radius 7`
(`export` and `info` as well). `fieldes info app.fieldesapp` says what an app asks for.

## Watch: run again when a file changes

Tick **Run again when a file changes** (or `--watch`), and the app runs by itself:

- when **a file chosen for an input changes** -- save the model from your CAD program, and the new part is processed and the output
  is written again, in real time;
- when **the app file itself is replaced** -- an engineer exporting a new version: the form is read again (what was typed stays) and the app
  runs. (Without Watch the form is still read again, and nothing runs until you press Run.)

A change while the app is running runs it again when it is done; several changes together make one run.

## What is inside a `.fieldesapp`

A zip file. Anything that opens zip files shows:

| | |
|---|---|
| `manifest.json` | the title, the version, the inputs (the form), which files came along, the FielDes that made it |
| `app.py` | the script |
| `blocks/*.py` | the custom blocks it uses |
| `files/...` | the files it reads, in the places it reads them from: the app runs with `files` as its working folder |

It is unpacked into the user's cache folder (`%LOCALAPPDATA%\FielDes\FielDes\apps` on Windows, `~/.cache/FielDes/apps` on Linux),
once per version of the file, and run from there; older versions of the same app are removed. While it runs, only **its own** blocks
are there (not the blocks of the computer), so an app does the same everywhere.

## Limits

- The form is read from the text of the script: inputs are plain values at the top level (see the rules above).
- Without `app.viewport(...)` the window shows the log and the files that were made, **not a 3D picture**: an app is for producing results
  (export, analysis, report), and a viewport is something the creator chooses to add. To look at a result of an app that has none, open the file it wrote --
  **Open** next to it does.
- An app does not reach outside the computer it runs on: a path on the engineer's computer (`C:\cad\...`) is not there on another;
  bring files along with `app.asset`, or ask for them with `app.file`.
- Scripts that need an editor's hand (a dragged `var()`, a placeholder `...`, a breakpoint) are not apps: in an app `var(x)` is
  the number `x`, and the script runs from the beginning to the end.
- An app of a newer FielDes than the one that opens it says so and does not run.
