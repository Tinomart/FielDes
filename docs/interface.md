# The interface

- [Top dock](#top-dock)
- [The editor](#the-editor)
- [The output pane](#the-output-pane)
- [The viewport](#the-viewport)
- [The model tree](#the-model-tree)
- [The section card](#the-section-card)
- [Legends and probing](#legends-and-probing)
- [The result card](#the-result-card)
- [Windows of their own](#windows-of-their-own)
- [Status bar and progress](#status-bar-and-progress)
- [Exporting](#exporting)
- [Units](#units)
- [Solvers of your own](#solvers-of-your-own)
- [Validation and accuracy](#validation-and-accuracy)
- [Keyboard shortcuts](#keyboard-shortcuts)

## Top dock

The bar along the top of the window has **New** (a plus), **Open** and **Import** as large icons at the very left
(`Ctrl+N`, `Ctrl+O` and `Ctrl+I`; hover for the tip; the plus is File → New: a fresh script, asking first when the one you have is
unsaved). A **small arrow** beside Open and Import opens a list of the files used lately: the scripts
you opened (the same list as File → Open recent) and the models you imported (a click imports one again, as Import model
does; *Clear list* empties it; a file that is gone is left out). Then the menus:

| Menu | Holds |
|---|---|
| **File** | New, Open, Open recent, **Open example file** (the small scripts of the `examples` folder next to the program, one for each family of features: open one, run it, change it), Import model, Open as viewer, Load the default script, Revert to saved, Save, Save As, Export mesh, **Export as app** and **Preview as app** (a script that people who never open the editor can run: see [Apps](apps.md)), Export / copy screenshot, Quit |
| **Primitives** | The primitives, each under its kind (New 3D shape, New 2D shape, New point ...), and the primitives of your own; **Manage primitives...** chooses which shapes are primitives and adds your own (see [Your own primitives](#your-own-primitives)) |
| **Edit** | Undo and redo, Find and replace, Go to (definition, line, the editor's tabs), Lines, Breakpoints, Folding, Autocomplete |
| **View** | Origin axes, orientation triad, legends, bounding boxes, Projection, Model tree, **Console** (the cursor in the line Python is typed in under the output, `Ctrl+\``), Section view, Standard views, Frame all, Zoom to bounds, **Meshing algorithm** (the render options: dual contouring, iso-simplex, hybrid) |
| **Settings** | **Keyboard shortcuts…**, **Blocks folder…** and **Show the blocks folder** (your [custom blocks](blocks.md)), editor text bigger / smaller, **Theme…** (light or dark, and the colours by hand), Automatically reload changes, Rotation mode, Rotation sensitivity, Zoom center, **Navigation** (how the mouse turns, pans and zooms the view: presets, and **Custom...**), **Units...** (the units numbers are shown in: see [Units](#units)), **Solver...** (the equations of the analyses, and solvers of your own: see [Solvers of your own](#solvers-of-your-own)) |
| **Help** | The guide, About, **Report a bug** (a new GitHub issue in your browser with the version and your system filled in; your script only if you choose to copy it; known problems are in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md)), **Guided tour** (cards over the program that walk through the basics, offered the first time it starts; see also the [tutorial](tutorial.md)), Shape reference, **Validation and accuracy** (what every kind of analysis was checked against and the accuracy that was found: see [Validation and accuracy](#validation-and-accuracy)) |

Everything else is in the viewport: the model tree, the section card, the legends and the result card.

### Your own primitives

*Primitives ▸ Manage primitives...* is where the list of primitives is yours. Select a model in the model tree and the dialog lists, for its kind (3D shapes, 2D shapes, lines, surfaces, fields: the kinds whose menu is a list; *New point* and a symmetry are single buttons with nothing nested under them, so they have no section and cannot be made primitives), every shape the menus have, built in or your own:
a tick makes it a primitive (it is shown under *Primitives*), no tick puts it with the *Parametric shapes* -- a built-in primitive can be taken out of *Primitives* this way too. **Add selection** makes the selected model a primitive of your own, under the name you give it:
everything of the script that the model is made of -- the models it uses, the functions and the variables it needs -- is kept in **a file of its own**, `<name>.py` in the `primitives` folder of your data folder (a function that makes the model: a block like any other, which the editor
completes and goes to the definition of with Ctrl+click), and picking it from any *Primitives* menu writes **one line** into the script, `name_1 = name()`. The model is one object, in the script and in the model tree, and what it is made of stays in its file -- as the geometry of an imported part stays in the
cache files of the import --; it is moved by its gizmo and its faces are dragged like those of any model. What you chose is kept in **one file of yours**, `primitives.json` in your data folder (`%APPDATA%\FielDes` on Windows, `~/.local/share/FielDes` on Linux), and the primitives in the folder `primitives` next to it,
so your primitives are there in every script you open, whether or not the script you made them in still exists (a script that calls one needs its file, as a script that imports a file needs that file; removing a primitive deletes its file). A profile (a shape made of points) is a *Parametric shape*, not a primitive.

## The editor

A code editor for the Python script, with:

- syntax colouring; line numbers in a slim gutter; **folding** (the arrows in the gutter), and **sections**: a comment
  that starts with `#SECTION` (or `# SECTION`), with a title after it, folds everything up to the next `#SECTION` comment
  (or the end of the file) when its arrow is clicked; the box that stands for the hidden lines says how many they are. Typing
  `#sec` offers `#SECTION` in the completion. Sections only hide text: the script runs as it is, and one that holds an error,
  or a line you go to, opens by itself. `# %%` and `# region` ... `# endregion` fold the same way;
- **objects as blocks.** A model's definition (`part = box(...)`) and everything that goes on with it -- `part = handles(...)`, `part = lock(part)`, a resolution of its own,
  and the line that shows it (`part` on a line of its own, or `# hidden: part` for a model that is not shown) -- is **one block**: one arrow in the gutter, on the first line of the definition, folds or opens
  all of those lines together, however many lines the definition itself runs over (the closing bracket included). Each such block is told from the next by its
  **background**: every second object has a slightly different shade of the editor's grey (lighter in the dark theme, darker in the light one), so where one object's definition starts and ends
  is seen at once. Numbers, `var(...)`s and the plain functions of Python are not objects and are not shaded;
- **completion** while you type (`Ctrl+Space` forces it) and call tips for arguments. **A function you pick from the list is written complete,
  the same call a right-click menu entry writes**: each argument it cannot do without is a **placeholder** after its name (`select_surface(shape=..., seed=...,`),
  the first one selected so that typing replaces it and `Tab` going on to the next -- the statement does not run until they are all filled in (nor does what is made from it; the rest of the script does)
  (see *Placeholders* under the model tree) -- and **every other argument is written with its default**, so that what exists is in front of you, and what
  you do not need can be kept or deleted. A call with two arguments or more is written **one argument to a line**, however short it is (a call or a list inside
  it stays on one line when it fits, 88 columns; `handles(...)` is the one exception: one long line). **Hover an argument's name** and the documentation says what it is, with its default. `Ctrl+Alt+Enter` (Edit > Lines) splits the call
  the cursor is in over lines, `Ctrl+Alt+J` joins it onto one;
- **go to definition** (`Ctrl`+click or `F12`), also into other files — see [Tabs](#tabs-the-script-and-the-files-it-uses) — and go to line (`Ctrl+G`);
- **find** (`Ctrl+F`), replace (`Ctrl+H`), with case, whole-word and regular-expression options;
- **multiple cursors** (`Alt`+click, or `Ctrl+D` for the next occurrence; `Esc` returns to one);
- line commands: toggle comment (`Ctrl+/`), duplicate (`Ctrl+Shift+D`), delete (`Ctrl+Shift+K`), move (`Alt+↑/↓`);
- **breakpoints**: click the left edge of the gutter or press `F9`. The script then runs up to the
  statement holding the breakpoint and stops; what was built so far is shown and a **Continue** button
  (`F8`) runs on to the next one. A breakpoint belongs to **the code on its line, not to the line's number**: it moves with
  that code when lines are added or deleted above it, when the line is moved (`Alt+↑/↓`), and when you press `Enter` in front of
  it (the code goes down, the breakpoint with it); typing in the line, or clearing its text, leaves it where it is; deleting the
  line removes it (an undo brings it back, and a line that is cut and pasted somewhere else takes it along). Breakpoints
  **are remembered per file**: close the
  file (or the program) and they are back, on the lines that still hold the text they were set on, when you open it again.
  Typing in the script is not interrupted by a stop at a breakpoint: the text cursor stays where it is, and the view
  follows the stop only when you are not in the script;
- **pause, run again, terminate**: three buttons sit at the bottom right of the editor (also under **Edit**):
  - **⏸ / ▶ pause / continue** (`F6`). A running script *freezes* where it is and goes on from there when continued -- nothing
    is lost or recomputed. It cannot be frozen at any instant (a program stopped in the middle of its memory handling or of the
    interpreter's own work could take the whole application down with it), so it waits at the next point where it reports
    progress: an iteration of a solver or an optimisation, a step of a STEP import (its parts, faces, solids), the start of the next
    statement. The bar says *Pausing...* until it has, then *Paused*; a step that reports nothing (a long meshing, say) runs to its end
    first. At a breakpoint the same button is **▶ continue** (`F8`). A script that finishes before it reaches a pause point simply
    finishes. Editing the script while it is paused lets the paused run go on to its end and starts the new one;
  - **↻ run again** (`Ctrl+Shift+F5`) runs the script from the beginning: a run in flight (or paused) is terminated first. Results
    that are in the caches come back at once, as with any run;
  - **● terminate** (the red dot, `Shift+F5`): the long solvers (static, modal, thermal and flow analyses, the optimisations) give up
    at their next iteration, and the script is interrupted at its next line; nothing of that run is shown (the viewport keeps what
    the last run that finished made) and the result line says it was terminated. A step that cannot be interrupted -- a STEP or mesh
    import, the cell layout of a lattice, a meshing -- finishes first; the bar says *Terminating the script...* until it has;
- text size `Ctrl++` / `Ctrl+-` (also on the numeric keypad; remembered; **Settings → Editor text bigger / smaller**).

### Tabs: the script and the files it uses

`Ctrl`+click (or `F12`) on a name opens the place where it is defined. In the script itself the cursor
simply moves there. When the name is defined **in another file** — a library function such as
`box`, a method such as `.max`, or a function from a module that sits next to your script
(`from bracket import make_bracket`) — the file opens in a **new tab** above the editor, at the
definition, and the name is selected. From there `F12` goes on: a name defined in that file moves the cursor,
one it imports opens the next tab. FielDes reads the files and follows the imports, nothing is run.

Only the **first tab**, the one with the play mark (▶) and the bold name, is the script that is run and
rendered. The other tabs are there for editing: they have a slightly darker page, a note under the text
says *Editing only: this file is not rendered*, and what you change in them does not run by itself.

- **Save** (`Ctrl+S`) saves the tab you are in. Saving a file of your own (a module next to the script) runs the
  script again, with the new file: the script's folder is searched for modules, and they are loaded afresh at every
  run. The files of FielDes's own library (the `python/fieldes` folder) can be edited too, but Python reads
  them when FielDes starts, so what you change there applies after the next start.
- A `•` after a tab's name means unsaved changes. Closing the tab (its **×**, or `Ctrl+W`) and quitting
  ask about them.
- `Alt+←` goes back to the script; `Ctrl+Page Down` and `Ctrl+Page Up` move between the tabs.
- Opening another script closes the extra tabs. The text size is shared by all of them.

The script runs **statement by statement** after every edit (shortly after you stop typing). While it
runs, the output pane shows which statement (`3/12`), and a thin bar shows how far the operation in it
has got — a STEP import's stages, an analysis's iterations, a loop's items. A run that is slow to mesh
can be cancelled with `Esc`.

Editing is disabled while you drag something in the viewport, so the drag and the text never fight.

**Automatically reload changes** (Settings menu) reloads the script when the file changes on disk.
**Open as viewer…** opens a file and keeps it reloading, to watch a script written in another editor.

## The output pane

Below the editor: what the script printed (`print(...)`), the value of the last expression when it is
not a shape, import and analysis summaries, warnings, and the traceback of an error — with the
offending line underlined in the editor. The error's text is red, everything else dark grey.

That pane is a **terminal**, and it is always there. The output is above and the line Python is typed in (`>>>`) is at its foot (see *The console*, below); the warnings of a run are lines of the output, and
**Fix All** -- when they have a fix -- is in the header of the pane, beside the run buttons. The bar between the editor and the pane is a splitter: drag it to make the pane higher or lower
(the height is remembered).

### The console

The line at the foot of the output pane (`>>>`; **View → Console**, or `Ctrl+\``, puts the cursor in it) is a console: its answers are shown in the output, under what the run printed. What is typed is **Python, run among
the names of the script that ran last and of the library** (`from fieldes import *`, as a script has it): `bracket.bounds`, `volume_of(bracket)`, `evaluate(top, (0, 0, 10))`, `help(select_surface)`,
`print(...)`, or a block of several lines -- like a notebook, the value of the last expression is shown. `Enter` runs it, `Shift+Enter` is a new line, `Up` and `Down` go through what was run before.
Printed text is shown in the text colour, a value in the accent colour, an error as its traceback in red.

It runs on a thread of its own, so a long computation does not stop the window. The names of the script are a **copy**: what you assign or change in the console is the console's own (it is
kept between commands) and the script, its model tree and the viewport are not touched -- put it in the script if it is to stay. The console reads the script as of its last run; run it again to
bring its names up to date.

## The viewport

| | |
|---|---|
| **Shift+left-drag** or **middle-drag** | Rotate (turntable; **Z points up** (the axis of the library's own shapes) unless you choose Y up in **Settings → Rotation mode**) |
| **Right-drag** | Pan. For hands that have no right or middle button to spare, **Settings → Navigation** adds a second set of gestures -- *Blender style*: also **Shift+middle-drag** pans; *Touchpad*: also **Alt+left-drag** pans; *Left drag turns*: a drag of the left button on empty space turns the view and **Alt+left-drag** pans (a click still selects, and **Ctrl+left-drag** still draws the selection rectangle); **Custom...** chooses the gesture of each of turning, panning and zooming -- a mouse button with Shift, Ctrl or Alt held -- and what is not chosen stays as it is for everyone. The default (*FielDes*) is the one above and stays the default; the others only add to it |
| **Left-drag** | Draw a selection rectangle: when you let go, every model that lies **wholly inside** it is selected (a model that is only partly inside is not; a rectangle that holds none leaves the selection alone). With **Ctrl+left-drag** the models inside are added to the selection instead of replacing it. A drag that starts on a surface you can pull (a model in *handles* mode, not locked, alone in the selection) pulls the surface instead, and one that starts on a handle or a gizmo moves that. With **Settings → Navigation → Left drag turns** the drag of the left button on empty space turns the view instead, and the rectangle is drawn with **Ctrl+left-drag** |
| **Right-click** empty space | Make things where the cursor is: **New 3D shape**, **New 2D shape**, **New point**, **New surface**, **New field** and **New custom block** (a menu for each kind, each entry with the kind's icon: every primitive of the library, your [blocks](blocks.md) that need no argument), **Import model…** (a STEP file or a mesh, written as one `import_model` line: see [Importing models](step-import.md)), **Add operation** (*thicken*, *shell*, *smooth*, moving, repeating and combining, *tessellate* under **Importing**, and the custom blocks that work on a model), the lattices -- **Periodic lattices** (*lattice* and *lattice_surface_conform*) and **Non-periodic lattices** (*random_points*, *voronoi_lattice*, *delaunay_lattice* and *surface_lattice*) -- under **Engineering**, and **Add simulation**: **New material**, **New fluid**, **New support or load**, **New thermal condition** and **New flow condition** (the presets of the library written out with their numbers; a condition is written with a placeholder where it acts, for a body, a field or a surface to be dropped on -- **or, with several models selected, for the body and the regions you selected**: see below), then, with a model, the analyses. Choosing one writes the call into the script, with a line showing it, and selects the new model, ready to drag. A new thing is placed where the cursor is (the point of the ray under the cursor that is closest to the origin, as large as about a hundred pixels, and in the plane through the origin that faces you, so y = 0 in a front view -- except a 2D shape, which lies in the plane z = 0 and is made where the ray under the cursor meets it, in any view; a place outside the render region is moved just inside it, or it would not be drawn) and goes at the end of the script; an operation works on the selected model (or the last one) |
| **Right-click** a model (or a line of the script that defines it, or its row in the model tree: see below) | **center** and **bounding_box** (the middle of the model's box, as a point, and the smallest axis-aligned box that holds it, as a body: library functions of their own, used all the time, so at the top of the menu), **Operation** (the same list, written with that model as its argument, under its definition; with **several models selected** and the one you right-click among them, the operations that combine models work on all of them, in the order you selected them: *union* unites all, *difference* takes every one after the first from the first, *intersection* keeps what they share, *exclude* locks the others as regions of the first (see [Excluded regions](fields.md#excluded-regions-exclude)); **Surfaces → surface_from_bodies** makes the first one's surface where the others meet it, and of one model alone, all of its surface (see [Selecting surfaces](selecting-surfaces.md#by-other-bodies)); the others work on the model you right-clicked), **Engineering** (a category for each domain -- *Structural*, *Thermal*, *Fluid* -- with *static_analysis*, *modal_analysis*, *topology_optimization*, *thermal_analysis*, *fluid_analysis* ...: the call is written with that model as the part, and a **placeholder for every kind of boundary condition** the analysis takes -- `supports=...`, `loads=...`; `inlets=...`, `outlets=...`, `boundaries=...` -- that you then fill in the model tree or the script; the result is a model of its own, shown when it is solved. **With several models selected** the body is the part and the conditions among them go in the input of their kind: see below), **Delete** (the model, or all the selected ones) and **Select Surface**, which opens the menu of the **surface selection**: the mode, the angle and the radius of a flood fill over the surface from the point you clicked; **Select** writes a `select_surface(...)` line into the script, under the model's definition. See [Selecting surfaces](selecting-surfaces.md) |
| **Wheel** | Zoom (about the cursor or the scene: **Settings → Zoom center**) |
| **Shift+wheel** or **Ctrl+wheel** | **Scale** the point the mouse is on (selected or not), else what is selected: every selected model by its gizmo's scale numbers (a point by its scale), a notch is 10 %, written to the script when the wheel rests. With nothing under the mouse or selected that can be scaled, the wheel zooms |
| **Hold Shift** on a selected model | The gizmo changes: the move arrows become **flat squares** that move the model along a plane, and the three scale squares become **one gold ring** round the middle that scales all three axes together: drag it outward to grow, inward to shrink; a faint ring stays at the size you started from and the factor is shown while you drag. **The rotation rings turn in steps of 5 degrees** while Shift is held (the angle that results is a multiple of 5, whatever it was before) |
| **Double-click** a model / the background | Frame that model / everything (`Home`) |
| **Click a model** | Select it: its row in the tree and its code in the editor. The keyboard stays in the viewport (also when it was in the editor before), so the single keys that work on the selection — `E`, `R`, `V`, `C`, `I`, `D` — are never typed into the script |
| **Click empty space** | Deselect everything (not with Shift or Ctrl held). The keys that work on the selection then have nothing to work on until you click a model |
| **Ctrl+click** a model | Add it to the selection, or take it out when it is in: select several models one by one |
| **`I`** (viewport or model tree focused) | **Isolate** the selected models: only they are shown, every other model hidden (script edits, undoable); `I` again shows what was shown before. Select another model while isolated and press `I` to isolate that one instead |
| **`D`** or **`Delete`** (viewport or model tree focused) | **Delete** the selected models from the script: their definitions and the lines that show, hide, edit and lock them go, as one undoable step. If the rest of the script still uses their names it asks once first. Also in the right-click menu of a model (**Delete**; with several models selected and the one you right-click among them, it deletes all of them) |
| **`Shift+D`** (viewport or model tree focused) | **Copy** the selected models: their statements are written again under them with free names, as one undoable step (a copy that is its own model, changed independently; a profile or a line is copied with its own points) |
| **`Ctrl+E`** (viewport or model tree focused) | **Extrude** the selected 2D shape (a circle, a rectangle, a triangle, a polygon, a profile): `extrude(shape, 0, size)` is written under it -- *Operation → Extruding → extrude* on the selection -- with the size a round number of the shape's own size. The two end faces of the solid -- the ones parallel to the shape -- are then draggable (the length of the extrusion: pull the far end, or the end the shape stands on, from where the shape is drawn over it, away from its edge), and so are its walls; the 2D shape underneath keeps priority at its edge. **A 2D shape and a path (a curve) selected together** write `path_extrude(shape, path)`: the shape swept along the path. A selection that is neither gets a word of why not |
| **`Ctrl+D`** (viewport or model tree focused) | **Link** the selected models: under each, `copy = instance(model)` -- a block that **is** the model and follows every change of it, and can only be **moved and rotated** (its gizmo has no scale and its surfaces cannot be dragged). The model is held by reference (`# shadow: model`), always, locked or not, so it keeps its own row and the copy shows a shadow of it. Also *Operation → Repeating → instance*. **Every model is cloned with `Ctrl+D`**, the same way: a profile or a line as the same outline through the images of its points, with its points drawn (a point dragged or double clicked on the clone, or a point put on its outline, is the original's, and every clone changes with it), a point, a material, the conditions or the result of an analysis (nothing is solved again) as themselves, an import by its parts, expressions without a name after they are named. In the editor `Ctrl+D` is still the next occurrence |
| **Hover a model** | Highlights it; shows the point under the cursor at the bottom left |

Standard views: front `1`, back `Ctrl+1`, right `3`, left `Ctrl+3`, top `7`, bottom `Ctrl+7`,
isometric `0` (they act while the viewport has keyboard focus — click it first — so they never eat
typing). **The pane that has the keyboard has a thin blue frame**, and the status bar says **Keys: viewport** or **Keys: editor**: the single keys of the viewport are letters in the script
editor, so this is what decides what a key does (hover the label for the list of the viewport's keys, as they are bound now). The triad at the top right is also clickable: click an axis to look along it.

The View menu switches between orthographic and perspective projection and shows or hides the origin axes,
the triad, the legends and the bounding boxes. The scale bar at the bottom shows the scale at the centre of the view.

What is drawn is the mesh of the script's shapes over the region `view.set_bounds(...)` at the
resolution `view.set_resolution(...)`. Imported parts may be meshed at their own resolution
(`roi_resolution`). See [Caching and performance](caching-and-performance.md).

### Snapping one body onto another

Make the faces, edges and points that are to meet on **two bodies** (right-click a body: *Select Surface*, the corners and edges, the centre of a face or of a circle -- see [Selecting surfaces](selecting-surfaces.md#corners-and-edges)),
select all of them -- at least one on each body, the ones on the body that is to move first -- and press **`S`** (viewport or model tree focused; it is also the *Snap* entry of the View menu).
**Nothing new is made: the body is moved**, as the gizmo moves it -- its own `position=` and `rotation=` are written (as `var()` numbers the gizmo then drags), so the mesh that is drawn is carried to its new place at once, and one undo
takes it back. The faces, edges and points that were made on that body move with it. See [Snapping a body onto another](scripting.md#snapping-a-body-onto-another) for what each kind of feature does.
A selection that cannot be snapped says why instead: none of its features is on a body (a point of the scene is no body), all of them are on one body, they are on more than two, or the body is locked or the image of a symmetry.

### Right-click anywhere

The viewport's right-click menus are not only the viewport's: **the same menu opens wherever a model is** -- in the viewport,
on a **line of the script** that defines a model, and on its **row in the model tree** -- and means the same there, as if that
model had been right-clicked in the viewport:

- **Three parts: Primitives, Parametric shapes and Operations.** **Primitives** hold the most basic, low-level representation of each kind of thing, each under its kind (*New 3D shape*, *New 2D shape*, *New 1D shape*,
  *New point*, *New surface*, *New field*): a sphere, a box, a cylinder, a cone, a torus, a pyramid, a prism; a circle, a rectangle, a triangle, a polygon; a polyline, a spline; a point; a plane, a sphere surface, a cylinder
  surface; a distance, a radial, a z and a noise field. **Parametric shapes** hold the more complicated ones -- shapes made with parameters that are not the most basic of their kind, though they need no model to be made: a rounded box,
  a ring, a wave, noise, your own low-level logic (*low_level_body*, *low_level_field*), a custom block -- by kind in the same way. **Operations** are what is done to a model: the operation groups (*Offsets and walls*,
  *Rounding*, *Moving* ...); **Engineering** is a menu of its own: the analyses of every domain, and the two lattice categories, **Periodic lattices** and **Non-periodic lattices**. What makes a primitive is being the most basic form of its kind, not how few arguments it needs. (`menu_catalog.BASIC` lists them; the quick operations *center*
  and *bounding_box* are below the three.)
- **Python** is a menu of its own, next to Engineering: the most basic features of the language as blocks of plain code -- *variable*, *if*, *if / else*, *for loop (a count)*, *for loop (a list)*,
  *while loop*, *function (def)*, *class*, *list*, *list from a loop*, *dictionary*, *try / except*, *with (a file)*, *import*, *print* -- written **as code, at the insertion point of the tree**
  (see [The insertion point](#the-insertion-point)): no `name =` in front, no line that shows it, because it is no model. The code is edited in the code editor; the model tree shows the block and what is in it
  ([Python in the tree](#python-in-the-tree)).
- **The menu is made of what is valid, and the same wherever it is opened.** A **category** (a submenu, at any depth: *Operations*, *Extruding* inside it, *Periodic lattices*, *Primitives → New 2D shape* ...) is in the menu
  **when there is at least one valid entry in it, and is not in it when there is none** -- it is not greyed out, it is left out -- and that is worked out from the entries themselves, by asking whether
  what is selected is something the entry takes, not from a list of categories. So a **surface** selected shows *New 2D shape* with *profile* (the outline of that surface) whether you right-click it in the
  viewport, on its row in the tree or on its line in the script, and *Extruding* is not in the menu of a solid and is in the menu of a 2D shape (it holds something valid there). An entry that takes a second model
  is valid when the pair it would be written with -- the selected model and the last other model of the script -- is what it takes (with no other model in the script the second is a placeholder, `...`, and the entry is valid all the same). **What an entry takes is declared by the library function itself**
  (`fn._takes = takes(...)`, with the kinds of `fieldes.kinds`: `SHAPES`, `BODIES`), the menu reads it from there, and a function that declares nothing takes any shape: so `smooth`, `round_edges`, `fillet`, `shell_inside`,
  `shell_outside`, `shell_exact`, `lattice` and `random_points` take a body (not a surface, a curve, a point or a field), `extrude` a 2D shape, a foam its points -- and a new function is covered by one line on it, with
  no list of names in the menus.
- On a line (or row) that defines a model: **Primitives**, **Parametric shapes**, **Operations**, **Engineering** (which holds **Periodic lattices** and **Non-periodic lattices** with the analyses), the quick operations (*center*, *bounding_box*), **Import model...**, **Select Surface** (greyed outside the viewport: the
  selection spreads from the place you click on the surface) and **Delete**, written for that model. The two lattice categories are sections of their own
  (not groups of the operations): **Periodic lattices** holds the operations that fill a model with cells that repeat and a menu for every family of cells, each entry making that cell;
  **Non-periodic lattices** the three steps of a foam (random points, then a Voronoi or Delaunay lattice made of them) and a random net over the surface (see
  [Lattices](lattices.md#in-the-menus-and-the-model-tree)); both are in **Engineering**, on empty space too.
- **A right-click on a model has the *Primitives* of empty space and *Import model...*** (Primitives → New 3D shape, New 2D shape, New point ...), **wherever it is opened**: made at the place you clicked on its surface in
  the viewport, at the middle of the model from its row or its line -- a model can be all there is under the cursor, and somewhere to put a point is not only empty space. **A point made from the menu is exactly where
  the cursor was**: on the surface under it, else in the plane through the origin that faces
  the view (on the work plane while its view is open), not rounded to a grid and not moved into the drawn region (a dot is drawn wherever it is).
- On a line of the script that defines no model (a blank line, a comment, the imports): the menu of empty space, **Primitives**,
  **Parametric shapes** and **Operations**, and what you make is written **under that line** (a line that is indented, opens a block or is in the middle of a
  call would be cut in two, so the new line goes to the end of the script instead). In the empty space of the tree, and in the
  viewport, it goes to the end of the script.
- Selected text keeps the text menu (cut, copy, paste): a right-click inside a selection opens it, anywhere else opens the model's.
- The model tree has no menu of its own: what that menu had is the row's buttons and keys (the eye, gizmo, lock, cache, reimport,
  reset, delete, `V`, `E`, `R`, `C`, `D`).

**Nothing is asked: what a menu entry writes is code, and the code is the place to change it.** A *topology_optimization* is written for the analysis you right-clicked, with `constraints=...` to fill in, and with its
`iterations` and every other argument it has, each with its default; a *lattice* with its cell, its cell size and
the rest; a `force` with its `vector` -- and where something is **missing** (the region a force acts on, a body for an analysis) the call has a
**placeholder** after the argument's name (`force(region=..., vector=(0, -100, 0), profile=None)`). It is the same call the completion writes,
laid out the same way (one argument to a line). Nothing opens a form: edit the numbers in the code editor, where the call is.
**A model of the wrong kind is refused**, in a placeholder or on a call, with the reason in the drop's note: a 2D shape dropped where `path_extrude` takes its path (*The path of path_extrude() takes a curve or a point: 'rounded_rectangle_1' is a 2D shape, so it does not go there*), a material on an `offset`. What a place takes is what the library says of the call -- the kinds the menus offer an entry by; where it says nothing, the run says if a model does not fit.
(The visual jobs stay visual: dragging a surface or a gizmo, picking a surface with *Select Surface*.)

**An entry the selection cannot make is greyed out** (no message: it is just not offered). Every selected model is classified once -- a part, a profile, a field, a surface, a point, a lattice cell, a result,
a support, a load, a thermal or a flow condition (each of them a kind of its own: the input of an analysis it goes in), a material or a fluid -- and every entry says what it takes; a selection that has something an entry does not take in it, or
lacks what it needs, does not fit. So *move* or *offset* is greyed out with a force selected; an **Engineering** entry is greyed out only when the selection has something it does not take -- *static_analysis* with a thermal condition or a fluid
among the models (*modal_analysis* takes no load, *thermal_analysis* only thermal conditions, *fluid_analysis* only flow conditions and a fluid), or with a second part or a second material for an
input that takes one (*fluid_analysis* takes two parts: the body and its fluid domain); a surface, a field, a point or a lattice cell is no argument of any simulation. **An operation or a simulation is never greyed out, or left out of the menu, for lack of something to work on**: the menu of empty space has every operation and the whole *Lattice* menu even in a script that has no model (only *center*, *bounding_box* and *Select Surface*, which are about a model, need one), and an operation with nothing selected is written with `...` where the model goes -- on the last model of the script when that is what it takes. A simulation too, with nothing selected (right-click
the empty viewport), with only its conditions or only its material selected, it is written with a placeholder for the part and for everything that is missing; the conditions of **New support or load** and the other **New ... condition**
menus take the surfaces, fields or bodies they act on -- **a condition has no body of its own**: the simulation is given the body. A model that has not run yet has no known kind and fits everything; your own [custom blocks](blocks.md) say nothing about what they take, so they are always offered.

The **Engineering** menu starts with *topology_optimization*, on its own (it designs for an analysis of any domain -- static, modal, thermal or flow -- so it is in no category), and then has a category for each domain -- **Structural**, **Thermal**, **Fluid**, **Manufacturing** (its constraints and *manufacturing_analysis* and *manufacturing_optimization*: see [Manufacturing](manufacturing.md)) and **Mesh** (*mesh_volume* and *mesh_surface*: a body as a mesh of elements for another program's solver, see [Meshing](meshing.md)), each in the colour and with the icon of its domain in the model tree -- and lists in them *static_analysis*, *modal_analysis*, *thermal_analysis* (*topology_optimization* of its result reshapes the part for its temperature),
*fluid_analysis* (the flow around a body in a domain, as it is; *topology_optimization* of its result reshapes the body); and, in the menu itself, under them, *parametric_optimization* -- it works over an analysis of any domain, so it is in no category -- which is written for the result you right-clicked, with `variables=...` and `constraints=...` to fill in (see [Parametric optimization](analysis.md#parametric-optimization)). Each analysis is written with the model you right-clicked as the part (the first argument of every simulation; the boundary
conditions have no body in them) -- or, when you right-clicked the empty viewport or selected only conditions, with `shape=...` in its place -- with **`parameter_name=...` for everything the call cannot do without** -- **one input for each kind of boundary condition it takes**, all of them
required -- and the library's own default for every other argument. A menu never makes up a region, a number, a text
or a call of its own to put there:

```python
static_analysis_1 = static_analysis(
    plate,
    supports=...,
    loads=...,
    material=steel,
    element_size=None,
    ...
)
static_analysis_1
```

A flow is written the same way, with a placeholder for **each** of `inlets`, `outlets` and `boundaries` (the walls and the slips); a thermal analysis with one for each of `fixed_temperatures`, `heat_inputs`,
`heat_generations` and `convections`. The line does not run until the placeholders are filled (the rest of the script does): make the conditions
(**New support or load**, **New thermal condition**, **New flow condition**: each is a model of its own, with an eye, and **its own row**) and drop them -- one, or all the ones of a kind at once --
on the placeholder rows of the model tree, or write them. Select the model **together with** its conditions first and they are written in the input of their kind.

## The model tree

The card at the top left of the viewport: one row per model the script makes; the parts of a STEP file are listed under
it. **Render settings** at the top lists the script's `view.set_*` statements and the numbers they have; a click on a row goes to its line, where the numbers are changed.

**Every kind of thing has its own icon and colour**, in the tree and in the right-click menus that make them:

| Icon | Kind | Is |
|---|---|---|
| cube, blue | **3D shape** | a body: its field is negative inside |
| square, teal | **2D shape** | a profile (a field that does not depend on z), drawn as a flat face in the z = 0 plane, with no thickness, its **edges blue**; pulled by its edges. A **profile** is a 2D shape made of points, in the plane of its first three: click an edge to put a point there, double-click a point to make it a corner or a spline point -- see [Profiles](profiles.md) |
| curve, light | **1D shape** | a `curve` through points: a blue line in the viewport, the path of `path_extrude`, the outline of a `profile`; as a field the distance to it. Select it by a click on its line; press the line and drag to put a point on it |
| soft disc, green | **field** | a value at every point that is not a body: a distance, a ramp, noise, a stress; not drawn: it has an icon in the viewport (with its name over it) and an eye in the tree, and selecting it opens its [field viewer](#the-field-viewer) |
| sheet, purple | **surface** | the zero of a field with no body behind it; drawn as the sheet itself, with no thickness |
| cross, amber | **point** | a point: a dot in the viewport (the middle point of a gizmo), always there while the point is shown (its eye); click the dot to select it, and its full gizmo shows |
| bars, red | **simulation** | a solved analysis or optimisation |
| hatched block, tan | **material** | what a part (or a fluid) is made of: `Material(...)`, steel, aluminium, water ...; an analysis is given one (`material=steel`). A model like the others: it is in the tree, and dragged onto an analysis it becomes its `material=` (or `fluid=`) |
| arrow on a base, orange | **conditions** | what an analysis is given: supports, loads, thermal and flow boundary conditions. Dragged onto an analysis, its **boundary conditions** row or the placeholder of its kind, one or several of them go into the input of their kind, as a list |
| hexagon, gold | **lattice cell** | what a lattice is made of: a cell (a strut cell, a TPMS, a planar pattern, a custom cell), a cell map, a graph of beams |
| small cube of struts, rose | **lattice** | a body filled with cells: what `lattice()` and `lattice_surface_conform()` make |
| scattered dots, orange | **point set** | what `random_points()` makes: the points a foam is made around (`voronoi_lattice`, `delaunay_lattice`). Drawn in the viewport, each as the dot of a gizmo without its handles |
| dashed square, pink | **selection** | a patch picked on a surface |
| open box, teal | **import** | a file read in |
| **f** badge on any of them | **custom block** | the model was made by one of your [blocks](blocks.md) |

Hover a row: the first line of its tooltip says what it is.

**Nesting is the structure of the calls.** The models an operation is made of are its **children**: `thick = offset(plate, 1)`
shows `plate` under `thick`, and `plate = difference(base, hole)` shows `base` and `hole` under `plate`, **in the order the call is
written**. A model has its row under the statement that uses it **only when it stands directly above it**: every row between the two
must be one of that statement's own inputs. Any other row between them -- another model, a variable, a line that makes nothing, a loop or a
function -- separates them: the model keeps its own row where it is made, and the statement shows a **shadow** of it. A model that is used by
several statements stands in one place, and each of the other statements has a **shadow**: a half-transparent row with the same icon and name, which is that
statement's own reference to the model. A loop, an `if`, a `with` or a `try` is a user as a statement is (the models directly above it that it
reads are inside its row); a `def` or a class is not a user but separates the rows. The rows of the tree therefore follow the order of the
script. What nobody uses is at the top. Rows are open by default; the arrow closes one. Because the tree is the structure of the calls, **every
nesting, renesting and denesting is an edit of the arguments of a call**, written into the script, never only a move in the tree:

| You | The script |
|---|---|
| nest: drop a model on an operation | the model becomes an argument of the call (see the table below) |
| renest: drag a nested model onto another operation | it leaves the old call's arguments and joins the new one |
| denest: drag a nested model between top-level rows, or into the empty space | it leaves its call's arguments (and its definition moves there, as a top-level model does) |
| reorder: drop a model between the children of an operation | it stands at that place among the call's arguments (`union(a, c, b)`) |
| drag a **shadow** | only that reference moves (or goes): the model, and its other references, stay |
| drag a **locked** model | it is **read-only**, so a drag never moves it: **it is always a copy**, a new shadow, as if Ctrl were held -- the model keeps its row and its place in the script. Locking a model (the lock button, `R`) is how you say that dragging it must only make references |
| **Ctrl**+drag a row or a shadow | a **new shadow** -- **that one row**, whatever else is selected: it is dragged at once, and becomes the selection -- and **the original does not move**: the model keeps its row and its place in the script, and its name is written into the call you drop it in, where it shows as a shadow. A label with a **+** and a green mark say it is a copy; releasing `Ctrl` mid-drag makes it a move again. A copy only makes sense inside a call: dropped on top level it is refused, as is a model that is an input of the call already. A model that **nothing else uses yet** stays a top-level row, and the call shows a shadow of it: the call's statement gets a `# shadow: name` comment (written by the drop, like the `# hidden:` lines), which says that the call only *holds a reference* to it and is not its first user; drag the model onto that call without Ctrl and the comment goes: the row moves in. **A shadow cannot go above its original** (see below) |

**Arguments are rows too.** Under every model, the arguments its call is written with that are not models -- `radius = 4`, `center = (3, 0, 0)`, `steps = 2` -- are **dim rows of their own, in the order of the call**, so that the tree reads like the code. **A click on one selects its value in the code editor**, ready to type over, as a click on a placeholder does -- for a number written as `var(5)` (the draggable form) it selects **the number inside**, so typing `9` gives `var(9)` and the number stays draggable. A statement whose only children are its arguments starts **closed** (as the tree looked before it showed them); the arrow opens it.

**The placement rows.** Every object that has a gizmo shows four more rows under it, always, the first of its arguments: **`position`**, **`rotation`**, **`scale`** and **`alpha`** (how opaque it is drawn; a symmetry has no scale) -- the arguments its gizmo changes (see [Placement](scripting.md#placement-where-an-object-is-how-it-is-turned-how-big-and-how-see-through)). The ones the call writes show their values; the ones it does not write are **dimmed and show their defaults** (`position = (0, 0, 0)`, `scale = 1`), and their tooltip says so. A click on a dimmed row writes the four into the call (the others with their defaults) and selects that number in the code editor, ready to be typed over; from then on the gizmo drags the numbers that are there.

**Sections.** A comment **`# SECTION name`** (`# %% name` and `# region name` fold the same way) is a section, and it is a row of the tree: the models defined below it, up to the next section, stand **under its row**, and the arrow closes it. A section is a chapter of the script, so **nesting stops at its border**: a model made in one section and used in a later one has its row where it is made, and the later statement shows a shadow of it (a section that held nothing but models used further down would otherwise be empty). To start a section **move the mouse into the gap between two rows** -- or **under the last row**, at the end of the tree: a small **+ in a circle** shows in the middle of the tree; **click the circle** (a click anywhere else in the gap does nothing, so a stray click makes no section) and `# SECTION Section 1` is written above that row (above the models it is made of, too) and its **name is already open for typing** -- the comment follows every key, `Enter` keeps it, `Esc` gives the old name back. **Double-click a section's name to rename it** the same way. The bin of a section row deletes its heading only (the models stay, and join the section above); dropping a model **above or below a section's row** moves it into the neighbouring section, as a drop between two rows does. The heading of a section is never moved along with the model below it.

**Closing rows is remembered, and it is the editor's folding too.** What you close in the tree is kept **for each file** and comes back when the file is opened. Closing a statement's row folds its lines in the code editor -- **a model and its whole setup**: the line that defines a variable folds together with the `x = handles(x, ...)` and `x = lock(x)` that go on with it, with one arrow for all of them -- closing a section folds the section, and **folding in the editor's gutter closes the row** -- the same in both directions, kept together. **A model starts closed -- in the tree and in the code alike**: a model that is new, or one in a file that is opened and that you have not opened or closed yourself, has its row closed and its lines folded; the editor's folds are set to the rows after every change of the tree, so the two are always one state, whichever you change. **A row that holds a placeholder is never closed**: that is where you have to write, so it starts open (and its lines unfolded) whatever was saved, and a collapse of it is undone at once -- and the lines your text cursor is in stay open, so that a model you are typing is not folded away under your hands (it closes with the next change once the cursor has left).

**Properties.** A **result** (an analysis, an optimisation) has more to give than its picture. **Move the mouse over its row: a small teal tab comes out of the right edge of the tree**, level with the row. Click it and a **drawer** opens beside it, listing what the result has -- its **fields** (`von_mises`, `displacement`, `ux` ..., `pressure` ...) and its **numbers** (`max_von_mises`, `safety_factor`, `reaction`, `drag_force` ...) with their values. **Click one and it is written under the statement as a model of its own**: `von_mises_1 = result.von_mises` -- a **field** (every field a solver calculates is one, not a body: it is shown with the field viewer, sliced, used in other models), a number is a line you can use. A property is **not made of the result**: its row has no child for it and no shadow of it, as an operation's has for the models it is made of. The drawer's last button writes the result's **key numbers as a text file**. The tab is there once the analysis has run.

**Boundary conditions.** A simulation has one row of its own for its conditions, **boundary conditions**, with a **placeholder row for every kind it still waits for** (`inlets`,
`outlets`, `boundaries` ...) and the conditions it has under them, in the order the simulation takes them. Each condition is a model of its own with **its own eye**: shown, it draws only
the surface it acts on, in its colour, with its pads or arrows and its label, on the body of the simulation. **The eye of the *boundary conditions* row shows all of them at once, or hides all of them** (it
shows them when they are not all shown). Select several conditions of one kind (Ctrl or Shift) and drop them **all** on the placeholder of their kind, on the simulation, or on this row: they go in as a list
(`boundaries=[wall_1, slip_1]`). **Dropped on the simulation or on this row, conditions of different kinds each go into the input of their kind** (select the whole problem and drop it at once);
on one placeholder only its own kind is taken, and another kind is refused, with the reason.

**A shadow never stands by itself at the top level.** A model that statements only *reference* (a Ctrl+drag put it there: their line says `# shadow: name`) keeps its own row at the top level, with a shadow in each of them. If such a statement is dragged **above** the model's row, or the model's row **below** the statement, the reference would be the first use, so a message asks: the fix (the default) makes the reference inside the statement the real model. Its definition goes directly above that statement, the comment goes, and the separate top-level row is gone; other statements that use it keep a shadow. *Do not show this message again* always does that (Settings → Show hidden messages again brings the message back); Cancel leaves everything as it is.

**A shadow cannot be put above its original.** The tree gives a model its row under the **first statement that uses it** when it stands directly above it (otherwise it keeps its own row where it is made, and every use shows a shadow): that row is the original; every later use is a shadow. So a shadow can only go into a statement *below* the original. Ctrl+drag a model into a statement that is *above* it, or move a shadow there, and a message explains it: the new use would come first, so it would become the original and the old place would only show a shadow — and the model has to be defined above any use for the script to run. The same happens **indirectly**: a statement that holds a shadow can be dragged above the original, or the original dragged below it. The program works out the tree the edit would give, before it writes anything, and if any shadow would then come first it asks **once**, listing every model it is about (three shadows in one statement are one message, not three). The message offers the easy fix, which is its default: **the first use becomes the original place** (for a copy (Ctrl+drag), the model's definition goes directly above that statement, when nothing in between uses it) **and a reference stays where the original was**. *Do not show this message again* makes it always do that, without asking (**Settings → Show hidden messages again** brings the message back); Cancel leaves everything as it is; one `Ctrl+Z` undoes it. It is the same kind of rule as taking a model out of an operation that cannot do without it: the script is never left broken.

**Taking a model out of a list.** A model that is one of the things in a **list** a call is given -- `loads=[force_1, force_2]`,
`topology_optimization([push_case, pull_case], constraints=[...])`, `union_all([a, b, c])` -- is taken out of the list, and the call keeps the rest:
the trash button of the `force_1` row under `static_analysis_1` leaves `loads=[force_2]`, not nothing. Taking out the **last**
model of a list leaves a **placeholder** in it (`loads=...`, never an empty `[]`, which would only fail later): the analysis
and its other inputs stay, and the statement waits there, as at every placeholder, until something is written -- drop a model on the
placeholder's row. A list inside a list (`loads=[[a, b], [c]]`, load cases) goes whole when its last model does.

**Taking a model out of a call it cannot do without: a placeholder, not a deletion.** `union`, `difference` and the other operations that
take any number of models just lose that argument, and a model in a list keeps the rest (see above). When the call cannot do without the
model it loses -- an operation with fixed inputs such as `offset(plate, 1.0)`, the last model it has, a model it uses inside an expression or
inside another call -- **a placeholder takes its place: `offset(..., 1.0)`**. Nothing is deleted, nothing is asked, and `Ctrl+Z` undoes it:
moving models around never costs you a statement.

**Placeholders.** `...` written where a call is given an argument is a placeholder (`...` anywhere else -- the body of a stub function,
`x[..., 0]` -- is just Python). A statement that has one **does not run, and does not throw an error**: it is skipped, and so is every statement that uses a name
it would have made -- **the rest of the script runs and is drawn** (the part, the render settings, everything that does not wait for it) -- and the
result line and the strip under the editor say which line is waiting for what. **Continue is not offered** (no `F8`, no button): the statement runs by
itself the moment every placeholder in it is filled in, because every edit runs the script again. In the editor a placeholder is drawn **bold amber -- not red: it is no error, it is what has to be done for the script to run**.
**Under the model tree a note lists them in amber -- the statement and the argument each is for -- and a click on one selects it in the code editor**,
where what goes there is written (hover the argument's name to read what it is); in the model tree the statement stays, with a **placeholder row**
under it for each one, named by its argument (a click on it does the same). A model **dropped on that row takes the placeholder's place at once** (on the
statement's row: the first one). Placeholders come from the creation menus and from autocompletion alike -- **whenever something is missing, a placeholder is
written for it, after the argument's name** (`union(a, b=...)`, `force(region=..., vector=...)`) -- and from taking a model out of a call that cannot do without it.

**Replacing a model.** Drop a model **on a model of its kind that is one of a call's arguments** and it **takes that one's place** in the call,
no menu: a part on a part, a force on a force, a field on a field, a support on a support (a load on a support is refused: they stand in
different inputs of an analysis). The model it replaces is not deleted: if the call was its first user it goes out to the top level (or
to the next statement that uses it). Dropped on a **shadow**, only that reference goes -- the model the shadow stands for stays where it is.
The dragged model leaves the call it came from as it always does (a placeholder there if that call needs it; with `Ctrl` it is a copy and stays).
Dropping on an operation still adds the model to it, as in the table below: replacing is for what cannot take the model in -- a part, a force.

**Deleting a shadow.** A shadow has a bin button too, and `D` works on it: the reference is taken out of its call by the same rule
(the model is not deleted).

**Drag and drop.** Drag a row (the selected models go with it); with `Ctrl` held it is a copy:

| Drop | Does |
|---|---|
| **a field on an operation** | the field takes the place of **a number** the operation is written with — *anywhere a number goes, a field goes*: drag `swell` onto `thick = offset(plate, 1.0)` and it becomes `offset(plate, swell)`. It takes the first plain number of the call that is no count (not one inside a tuple, such as a corner or a centre; not a whole number such as 
 or count: a field has a different value at every point, so it cannot say how many); where there is none, the drop is refused and says why |
| **a point on an operation** | the point takes the place of **a position** the call is written with: drag `anchor` onto `d = distance_to_point((5, 5, 5))` and it becomes `distance_to_point(anchor)`. It takes the first position written as a tuple of numbers that is a position (not a size, a direction, a scale or a range), or the other point the call is given. A flat shape's position has two coordinates: `circle(3, (20, 50))` becomes `circle(3, (anchor.x, anchor.y))`. A point reads as its coordinates wherever a position goes, so the same works when you write it |
| **on a placeholder row** | the model takes the placeholder's place, at once (on the statement's row: its first placeholder). A placeholder for an input of conditions (`inlets=...`) takes **all the conditions dropped, as a list** -- all of one kind, or the drop is refused and says why |
| **conditions on a simulation** (or on its *boundary conditions* row) | each goes into the input of its kind, as a list: a placeholder is replaced by the list, a list that is there gets them as more items, a single one written becomes a list with them. Conditions of different kinds each go to their own input, all in one edit |
| **on a model of a call (or on a shadow) that cannot take it in** | the model of the same kind **replaces** it in that call -- see *Replacing a model* above |
| **on an operation** | **no menu, ever**: the dragged model becomes one more argument of an operation that takes any number (`union`, `difference`, `intersection`, ...), after the last one. An operation that works on exactly one model (`offset(plate, 1.0)`) gives that place to the dragged model; with several fixed places the drop is refused, with the reason, because nothing says which to replace. A model that is defined below the operation moves up, with what it is made of. A model that was an argument of another call leaves that one (with `Ctrl`, it stays: a reference is added) |
| **between the children of an operation** (or a shadow) | the model stands there among its arguments. An operation that takes exactly its models cannot be given another this way (the drop is refused, and says why); it can have its arguments reordered |
| **between two top-level rows** | the model stands there in the script: its statements (its definition, the lines that show, hide, lock and edit it, and the comments right above it) move, and what it is made of moves with it if that is defined later. A nested model also leaves the call it was in. Refused, with the reason in a note under the tree, if something would then be used before it is defined |
| **in the empty space** | to the end of the script |

The tree draws a blue frame round the operation a drop would go into, or a line where it would stand (green for a copy), and the dragged
rows follow the mouse as a label. Where a drop is refused, the label has a **red note with the reason** next to it (the forbidden cursor alone would not say), and the reason is also written under the tree if you let go. The tree answers **at once**: the rows move (and the script text changes) before the
script has run again; the run that follows confirms it (or, if the script then fails, shows the error). This
works for rows that are nested as for those that are not. Each drop is **one edit of the script**, undone by one `Ctrl+Z`. A model that is changed again further down (`x = f(x)`) cannot be dragged; nor can a
model made of the one it would be dropped on (that would be a cycle).

**Rename.** Double-click a variable's name (or select it and press `F2`) and type the new name: **the script follows every key you
type** (a name that is no name yet, such as an empty one or one that starts with a digit, waits for the next key). `Enter` or leaving
the field keeps it. Every use of the
variable in the script changes with it — the definition, the lines that show, hide, edit and lock it, the `# hidden:`
comments — and nothing else: strings, keyword arguments of the same name, attributes, and a parameter or local variable of the
same name inside a function are left alone. `Esc` gives the old name back. A name that is no name, a Python keyword, a name the script already
uses or a function of the library is refused, with the reason in a note under the tree (and the name the variable had stays).
The keys you type are one step of `Ctrl+Z`.

**Render settings.** Open the row: the region (the two corners of `view.set_bounds`), the resolution and the quality, as the script has them
(or the defaults, dimmed, when it has not set them: the strip under the editor offers the line). **They are read here and written in the code editor**:
a click on a row **selects its number in the code, marked for replacement** (the region: both corners), as a click on a variable or an argument does, so the next thing you type replaces it. The rows have no bin: a render setting is a line of the code, and it is changed or deleted there.
The corners the program writes are **whole numbers** (a minimum is rounded down and a maximum up, so the region still covers what it
covered), with two decimals only for what is under 1, a part a few millimetres across. **The region follows the models** (*Auto scale*, on by default and written in the call as
`auto_scale=True`; the row of that name in *Render settings* switches it, as `auto_scale=False` does; it also sets each model's own resolution): the numbers of `view.set_bounds` are rewritten in the code editor, live,
as you work. When a model reaches out of the region -- you drag it, pull one of its faces, type a bigger number, add a model -- the region grows to hold it, with a little room (a tenth
of the models' size, at least 1 mm); while a model's extent cannot be found the region is not shrunk (it might be what holds that model); when the models have become much smaller than the region, or the big one is gone, it shrinks to them. With no model left it
stays as it is (there is nothing to fit it to), and a file that is just opened is only ever grown, never shrunk, so a region you made large on purpose is kept.
**A drag past the edge is not clipped:** while you pull a gizmo handle or a surface the model is drawn whole (the region grows live, to hold where the model will be), and the grown region is written **together with the numbers of the drag, in one run, when you let go** -- there is no second run a few seconds later that fits the region.
A script that gets its region from `roi(...)` keeps it, the models' box given to `roi`. With Auto scale **off** the region is exactly what the script says, and what
is outside it is not drawn.

**The tree is read from the text of the script; it is not made by running it.** Every statement that makes a model has its row a
moment after the last key you typed -- whatever the script is doing: calculating for minutes, waiting at a breakpoint or a
placeholder, or failed. So the tree always shows what the code says, and a variable whose statement never ran, or threw an error, is
not a "ghost" in it: a statement that threw an error gets a red cross row and the error in the note; the statements below it keep
their rows. What only a run can say -- what exactly a statement made (the icon of its kind), how far it reaches (the extent the
viewport frames and the region of interest is made from) -- is laid over the rows as the statements finish; until then a row's kind
is a guess from the name of its function, and a run corrects it. A line that is being typed and does not parse leaves the tree as
it was (the editor says what is wrong). **Opening a file** (or a new script, or the guided tour) shows the new script's rows at
once and empties the viewport, so what the old script made is never left standing while the new one runs, or when it does not run.

| Element | Does |
|---|---|
| **Click a row** | Select the model in the viewport, scroll the editor to its code, frame the camera on it |
| **Ctrl+click** / **Shift+click** a row | Select several rows, as in a file list: Ctrl+click adds a row or takes it out, Shift+click selects every row from the one clicked before to this one. The selected models are lit up in the viewport; the editor and the camera stay where they are. The order you select them in matters for the operations that combine models (the first is what *difference* subtracts from) |
| **Eye** (key `V`) | Show or hide (hidden displays become `# hidden: …` lines). The assembly (an import row) has no eye; each part has one: it adds the part to the script, or toggles it. A profile, a polyline or a spline is shown and hidden together with the points that only it is made of (the points that go with it when it is deleted); a point that another statement uses stays as it is |
| **Double-click a part** | Use that part in the import statement |
| **Gizmo button** (key `E`) | The gizmo is on the screen **the moment a model is selected**: the numbers that move it are the `position`, `rotation` and `scale` arguments that the call of the model gets when it is selected, and the script has to run for them to exist, so until then the gizmo is drawn from where the model is, a little transparent; a press on it is not lost (the drag starts as soon as the real gizmo is there, if the button is still down). Sets when the shape's gizmo is shown, going round **click** (the default: while the shape is selected) → **never** → **always**. Dragging the shape's surfaces works whatever the mode, and the gizmo has priority where it is shown. On a displayed expression such as `sphere(3)` it first gives the expression a name (`sphere_1 = sphere(3)`). Several selected models are in the multi-select state: one shared gizmo moves them all, whatever mode each has when selected alone (the buttons keep showing each one's own mode). Locked models cannot be multi selected. See [Handles](handles.md) |
| **Lock button** (key `R`) | A switch of its own: a locked shape cannot be dragged at all, and keeps its gizmo mode. It writes (and deletes) the line `part = lock(part)` under the definition. On a row that is one of several selected, it locks or unlocks all of them (as `R` does); a selection with a locked model in it cannot be moved or edited |
| **Keys `V`, `R`, `C`, `E`** | Show / hide, lock, render cache, gizmo mode. `V`, `R` and `C` act on all selected models by one rule: if they are not all in the *on* state (shown, locked, cached), they are all put there first; only when they all are, they are all turned off. `E` sends models that are all in one gizmo mode to the next, and models in different modes to click |
| **Cache button** (stack of disks, key `C`) | The **render cache** is on for every shape unless you turn it off: the button writes (or deletes) the line `part = render_cache(part, False)` under its definition. With it on (the default), the finished mesh is kept on disk and shown at once the next time the same shape is rendered; anything that changes about the math makes it mesh again. Blue: on; green: the mesh on screen was read from the cache; amber: the shape cannot be kept (hover). Off until you click it. See [Caching and performance](caching-and-performance.md#the-render-cache) |
| **custom_resolution row** (a property under a variable, or under a part of an import) | A resolution of its own for that one body -- the number the scene's `view.set_resolution` is, for this body alone (it is made finer for a small body as the scene's is): the row reads `custom_resolution = 3`, a click on it selects the number of the line `part = custom_resolution(part, 3)` in the code, ready to be typed over, like any argument; the bin takes the line away. It is also **Custom resolution** in the right-click menu of the model. Use it when one part needs more detail than the scene should be meshed at -- or less. See [Caching and performance](caching-and-performance.md#resolution-and-quality) |
| **⟳ Reimport** (on an import) | Read the file again (bumps `rev=` in the call). Imported files are also watched: one saved by another program is read again by itself |
| **⟳ Reimport** (on a part) | Back to what the file says: the part's handle edits are deleted and the file read again |
| **↺ Reset** (on an import) | Delete the import's cache and the handle edits of all its parts, and import afresh |
| **Bin** (every row) | **Delete** the row's object: its statement, the line that shows it, the line that hides it and its `handles()`, `lock()`, `render_cache()` and `place()` calls are removed from the script (one undo restores them). On an import it deletes the parts' variables too; on a part row, the part's variable; on a render setting, its `view.set_…` line (back to the default). If the rest of the script still uses the name, FielDes asks first |
| **Right-click** | The menu of the viewport for that model, as if you had right-clicked it there (see [Right-click anywhere](#right-click-anywhere)); in the empty space under the rows, the menu of empty space. *Select Surface* is greyed there (the selection spreads from the place you click on the surface, so it is started in the viewport). The things the old tree menu did are the row's own buttons and keys: the eye, the gizmo, lock and cache buttons, reimport, reset, the bin, V, E, R, C, D, and a click on the row (goes to the code, frames it) |

Grey rows are hidden, or shown but outside the render region. An amber row is imported but worth a look
(an open mesh, a suspicious size): hover it.

**The tree is quick.** It is there the moment a file is opened (the tree of the last run of that very text is kept), and the
eye, lock and cache buttons and the **bin** change it **at once** -- a deleted model's row is gone, the lines of every row move with
the edit -- without waiting for the script to run, however long that takes. Click as often as you like: all the clicks are made, and only the last text is run. The gizmo
button changes its icon at once, and the lines it writes are known when the run is done. Details and limits:
[Caching and performance](caching-and-performance.md#the-model-tree).

### Python in the tree

The tree is a picture of the script, and the script is Python: **an `if`, a loop (`for`, `while`), `with`, `try`, a function (`def`), a class and a `match` at the top level are rows of their own**, a violet tile with the kind in two
letters (*if*, *fo*, *wh*, *wi*, *tr*, *df*, *cl*, *ma*), between the model rows in the order of the script (and inside a section, if they stand in one). What is **in** a block stands under it:

| Row | Is |
|---|---|
| full violet dot | a variable the block assigns (`b = ...`), a loop's variable, a parameter of a function |
| hollow dot, dim | a name the header only reads (`if size > 5` shows `size`) |
| a line, then a name | a branch: `else`, `except ...`, `finally`, `case ...`; its statements are under it |
| a tile and a header | a block inside the block (`elif`, a loop in a loop, a method in a class), down to eight levels |
| a statement | any other statement, shortened (`return cylinder(r, h, ...)`, `print(error)`, `self.size = 4`) |

**Everything under a block is locked here**: it cannot be dragged, renamed or dropped on, and editing Python is done in the **code editor**, never in the tree (the tree is the interaction suite of FielDes blocks). A **click**
on a name selects it in the code, ready to be replaced by typing; a click on a statement or a branch selects it; a click on the block goes to it. A block **at the top level** can be dragged above or below any other row
of the top level -- a model, a section's row, another block -- and its lines move in the script (with the comment lines right above it and the blank lines under it), as one undoable step, to structure the code; it is
refused with the reason where it would end inside another block or inside a model. Models made inside a block are variables of the block, not model rows ([Known issues](../KNOWN_ISSUES.md)).

**A move cannot break the script.** The library reads the script and knows what every statement of the top level makes and what it needs (what a function needs is counted where it is called, not where it is
defined). A drop that would make a statement read a name before it is made -- a call above its `def`, a loop above the variable it counts to -- or read it from another line shows the question **"This would break the script"**
with the easy fix: the statements it goes with -- those that use what it makes, or what it uses -- are moved **along** with it, in the order they have, as one undo step. Cancel leaves everything as it is. ("Do not show this message again" always moves them along.)
A drop whose fix would put everything back where it was -- a `def` dropped below the very statement that calls it -- is no move at all: it is refused, with a note in the tree that says which statements it has to stay in the order of.

### The insertion point

What is added from the tree -- a new shape, a condition, a simulation, a block from the **Python** menu, a paste -- goes to the **insertion point**. The line between two top-level rows, where the **+** that starts a section shows,
is clickable: **a press on the line itself** (a few pixels each side of it, away from the circle) puts the point there -- a **white line** lights up along the gap as soon as the cursor is on it (under the **+**, never over it), and
a **cyan line with a small arrow** marks the point: faint while it is only the default (the end of the
script), solid once it is set -- and puts the **text cursor of the code editor** on that line, with the keyboard in the editor. The two are one: **the cursor you move in the editor moves the point** (inside a statement the point is after
the statement, on the first column of its first line before it, between statements above the next one), and what is added is written above the row the point is above, with the cursor going under it, so the next thing added lands
under the last. Operations still go under the model they work on. Where no point has been set (a script just opened) things go to the end of the script, as they always did.

## The section card

`Shift+X` (or **View → Section view**; the key acts while the viewport -- or the model tree -- has the keyboard, so it never types over the editor) opens a card at the top right of the viewport. The section view is a viewer **you switch
on** when you want it; it cuts models. It is separate from the [field viewer](#the-field-viewer), which shows fields on discs: they
work on different things, and **both can be open at the same time** (the field viewer's card then sits below the section card).

- **X / Y / Z** picks the plane's normal; the slider, the position box, or the blue arrow on the plane in
  the viewport moves it; **Flip** swaps the kept side.
- **Cut model** clips the geometry at the plane (the cut faces are drawn flat).
- **Field** paints the plane with the signed distance field around the model: blue inside, brown outside,
  iso-lines, and the black contour is the surface. Hover the plane to read the distance under the mouse.
- Inside a model shown with an **analysis result**, the plane shows the result instead (stress,
  displacement, temperature …), deformed with the model.
- **Whole elements** appears when the plane cuts a part shown with analysis elements: it keeps the
  elements on the kept side whole instead of cutting them (a cut element looks smaller than it is).
- **Opacity** and **Colours** (automatic or a manual ± range) tune the plane; **2D view** shows the plane
  as a flat plot.

Every pixel of the plane is the field itself evaluated at that point — nothing is resampled onto a grid.

**Fields have their own card, the field viewer** (next section): select a field -- by its icon in the viewport or its row -- and its viewer opens, whether or not the
section view is on.

## The work plane

A **work plane** is a plane to draw on with a pen tool -- points, lines and splines, as in a vector drawing program, with no constraints or dimensions: the numbers of a point are its coordinates, in the script. Nothing about the plane is written into your script, only what you make on it (points, lines, shapes) is, as ordinary
models with ordinary numbers. Three keys, all of which act while the viewport (or the model tree) has the keyboard:

| Key | |
|---|---|
| `Shift+W` (or **View → Work plane**) | opens and closes the **work plane view**: the plane is drawn with a grid, and its card is shown. A plane is made when there is none |
| `Ctrl+Shift+W` (or **View → New work plane**; **works wherever the keyboard is**, in the code editor too, and gives it to the viewport) | makes a **new** plane and opens its view: the plane of the **selected 2D shape** (a profile, a circle, a shape from a surface ...) -- or of three or more selected points -- where it is; **a selected surface** (a face of a body picked in the viewport) gets its own plane, through the middle of the face and **turned to face you**, and a surface that is not flat gets the plane that covers the most of it (the plane most of it faces); with nothing selected, **parallel to the camera view, through the origin** |
| `W` (or **View → Set view to work plane**) | turns the camera to **look straight at** the plane. It only moves the camera: it never changes the script |

The card (top right, below the section card when that is open) has three buttons that say what a click draws -- **Line**, **Spline**, **Point**, each with a picture of what it makes -- a line that says whether you are drawing, and the plane's settings: **Grid** on and off, **Snap to grid** and **Snap to points**, the **grid step** (automatic, from how large the picture is, or a number), the plane's
**offset** along its normal, and the buttons **Set view to work plane** (as `W`), **Set work plane to view** (the plane made parallel to the view through the origin) and **New work plane**. The cursor's place on the plane is
read out under them. **Turning the camera (the middle mouse button, or Shift + left drag) leaves the work plane view**, and so does closing the card; the status bar says so when the camera did it, and how to open the view again; the plane is kept for the next time you open it.

**Symmetry.** What you put down on the plane can be made at its mirror images too: that is [the symmetry](#the-symmetry), a model of its own. With the work plane's view open, `Shift+S` makes one on the plane
-- its axis the plane's normal, its first plane along the plane's u axis -- and **a point, a line, a spline or a profile you put down is put down at its images too**. The lines the symmetry's planes make in the plane are a thing to
snap to (with *Snap to points* on): a point put down near one lands **on** it -- on the grid's steps along it when the plane snaps -- so what you draw on both sides of a plane **meets on it**. **A shape that is closed through the
symmetry is made into one**: draw half a circle from one point of a plane to another, and its image on the other side closes it -- the two are joined where they meet and written as **one closed profile** (one that meets only at
one end is one line). It is made the moment the stroke comes back to a plane of the symmetry -- the click that puts the last point on it closes it, as a click on the first point does.

**What it does.** While its view is open, **the places of new models are on the plane**, snapped (right-click → *New ...*, a profile made of new points is made in the plane's own axes, not horizontal), and the points you drag --
by their dot -- move in the plane and land on the grid (or on a point that is there). When **not drawing**, a **2D shape that lies in the plane** is moved about in it by pressing it, in steps of the grid: a profile moves all its points together, any other
2D shape moves through its own move numbers.

**What a click does.** The three buttons -- **Line**, **Spline** and **Point** -- pick what a click draws; `E` goes round them. **None of them on is not drawing**, and there is no button for it: the view **opens in Line**, so a click draws at once, and with none on clicks work as they do
everywhere -- things are selected, moved, turned and scaled. Click the button that is on to turn it off, or **right-click in the viewport** (a right click that does not move; dragging with the right button still pans). `E` then brings back the kind of drawing that was on before it was stopped. You can tell which you are in: while drawing the pointer is a crosshair and a **marker shows the point a click will put down**
-- a **square** for a line (a corner), a **round point with a ring** for a spline, a plain **dot** for a point, as the points of the script look -- the points already put down have the same marks, and the line under the buttons says what a click does. Not
drawing there is no marker, no readout of the cursor's place, and the pointer is the usual arrow. In a drawing mode **every click puts a point down**, also on a point that is already there -- the line attaches to it, it does not select it -- and
that is what not drawing is for. A **double click on a point** of a profile goes from a corner to a spline point to a round corner and back, drawing or not (on a line it changes between a corner and a spline point).

- **Line**: every click puts down a point of a line; the line through them follows the cursor. **A click on the first point closes it, and a shape is made out of it**: a profile with hard corners. `Enter` or a double click
  ends it open, as a `polyline`. `Backspace` takes the last point back, `Esc` drops the whole line.
- **Spline**: the same with a smooth curve through the points; closed, a profile with spline points (`smooth=True`); open, a `spline`.
- **Point**: every click puts a point on the plane.

Nothing of a line or a spline is written until it ends, and then it is one step (one undo): the points, each a model of its own with its gizmo, and the `profile(...)`, `polyline(...)` or `spline(...)` through them. (Shift and Ctrl keep their
meaning in every mode: Shift turns the view, Ctrl adds to the selection.) See [Profiles](profiles.md).

## The symmetry

A **symmetry** is a model of the script, like a point or a surface: **mirror planes through a centre**. `mirror = symmetry_planes()` is one plane, the plane x = 0; with `planes=3` there are three planes through an axis, 60 degrees
apart; `kind='across'` adds the plane across the axis, and `kind='cube'` is the nine planes of a cube. **`kind='turn'` has no planes: `copies=3` is a three-way symmetry** -- three copies of what you make, 120 degrees
apart round the axis, **nothing mirrored** -- and `copies=5`, `7` ... up to 24 are the five-way, seven-way ... ones (a table with three legs, a fan, a gear). It is not the same as mirror planes: two planes at right angles make four
copies, two of them mirror images, at the corners of a rectangle; `copies=4` makes four turned ones, at the corners of a square. Make one from the right-click menu (**Primitives ▸ New symmetry**), or press **`Shift+S`** while there is none in the script: it is made at
the place of the work plane while its view is open (its axis the plane's normal), else it is the mirror x = 0.

**Select a symmetry in the model tree and it is the one in use.** Its planes are drawn in magenta, its card opens in the corner of the viewport, and **everything you make from then on is made at its images too**: a body, a point, a
surface, a field -- and a profile, a line or a spline (through the images of its points), and what an operation makes of them: a `path_extrude`, a union, a shell. **All the images of a model are one model**, written under it:

```python
box_1 = box((8, 4, 0), (26, 14, 8))
box_1
box_1_images = symmetry_images(box_1, symmetry_1)  # shadow: box_1, symmetry_1
box_1_images
```

`symmetry_1` is the symmetry. A symmetry of two planes at right angles has three images (the turn between them and the two mirror images), a cube's nine planes forty-seven -- and still it is **one row in the tree and one line in the script**:
the original is the only model with numbers, the images are only what the symmetry makes of it, so nothing is stored twice. (`only=(1, 3)` lists the images that are there: an image that would be the model itself, or another image -- a ball
whose centre is on a plane -- is not made.) Each image is still drawn, meshed and moved as a shape of its own, so **dragging the original moves every image with it as you drag, with nothing meshed again**; select the row and all
the images are selected. **The images follow both**: change the original and they change the way a mirror shows it -- and **move the symmetry with its gizmo and every image of it moves with it**. **They have no numbers of their own: drag
the gizmo shown at the first image, or the surfaces of any of them, and the original moves** -- the other way, as a mirror shows it (an arrow moves the original the opposite way across the plane, a ring turns it the opposite way). The images
of a model that goes are deleted with it. **The images are written when the model is made**, under it: select a symmetry first, then make things. A profile, a line or a spline is made of its points, which are models, so each point has its images (`symmetry_image(point, symmetry_1, 3)`) and the stroke goes through them. **What an operation makes is given its images too**, `path_extrude_1_images = symmetry_images(path_extrude_1, symmetry_1,
distinct=True)`: `distinct` leaves out the images that would be the model itself or another image (a tube along a path in a plane of the symmetry has none in that plane); what is made of
images already is not given more. The images of a path extrude are drawn the way it is -- from the numbers of its path, so they follow a dragged point at once -- **and what a model was made from is mirrored with it, whatever the operation** (a statement `leg = path_extrude(profile, path)` knows the models it read: the tree shows them as its children): every image comes with its own copy of the profiles and lines the model was made from -- the blue curve and the outline of a path extrude's profile, the profile of an `extrude`, through a chain of offsets, unions and moves -- and a dot for each of their points, lying on the image as the original's lie on the original. They are not models (no statement, no row in the tree: the profile and the path are the only ones with numbers), and **a dot of any image's is pressed and dragged -- it is the original's point that moves, and every image follows**; a double click on one changes the original point between a corner and a spline point; change the number of copies on the card and the images follow. (Draw the path and the profile with the symmetry off, `Shift+S`: what a model is made from is mirrored with it, and drawn with the symmetry in use it would also have images of its own, one on top of the other.) Pulling the surface of an image changes the model it is the image of, never the symmetry. (A surface or a field has a model for each image,
`symmetry_image(model, symmetry_1, 3)` -- 3 is the number of the image in the symmetry; a shape whose halves meet on a plane closes through their images into one profile.)

Because the symmetry is a model, it is easy to keep track of: it has a row in the tree, it can be hidden, locked or deleted, there can be **several** (select the one to use), and the script says which images belong to which.

**What is in use, exactly.** *In use* is a state of the session, not of the file: a symmetry stays in use when you click away from it (its card and planes stay on the screen), until you stop it (the card's ✕, `Shift+S`) or select another; when a file is opened nothing is in use, the images in it still follow their models, and a symmetry has to be selected again to make more. **A hidden symmetry (its eye off) is not in use**, and neither is one that was deleted: what you make then has no images. **A line that you type in the script yourself is not given images**: they are written when a model is made from the viewport or the menus. Undoing the making of a model removes the model and its images in one step.

**The card** (top right) changes the symmetry that is selected: **Planes** -- *Axis* (planes through one line), *Axis + across* (and the plane across it), *Cube*, or *Turns* (copies turned about the axis, no planes); **Count** -- how many planes round the
axis (1 to 8), or, for *Turns*, **Copies** (1 to 8, or a number up to 24); **Axis** -- along
x, y or z, the work plane's normal, up in the view (the first plane then splits the picture into left and right), or across the selected 2D shape; **Turn** -- how far the first plane is turned about the axis (not for *Turns*: copies turned about
an axis do not care where a first plane is); **Centre** -- where the planes cross (the origin, the middle of what is selected, the work plane's origin). The count and the arrangement are written into the call (`planes=` for planes, `copies=` for
turns: changing the arrangement moves the number to the argument it belongs to); where it is and how it is turned are the numbers of its gizmo. **In use** switches the images off and on
(`Shift+S` does the same; the planes are drawn pale while it is off, and the ✕ of the card stops using it).

**Its gizmo is along its own axes.** A symmetry made with `Shift+S` while the work plane's view is open has its axis along the plane's normal and its first plane across the plane (perpendicular to it), centred on the
plane's origin -- and **its gizmo is turned with it**: the arrows and the rings are along the plane's own axes (its u, its v, its normal), however the camera was turned when the plane was made, so it is moved and turned in the
plane's terms. A symmetry that lies along the world's axes keeps the world's gizmo. **While the work plane's view is open with its grid snapping, the symmetry's centre lands on the grid** as it is moved: by its dot, its plane squares
or an arrow along one of the plane's axes.

**How an image was made.** Select the images of a model -- `leg_images`, `box_1_images`, or any one of their shapes in the viewport -- and **the symmetry they were made in becomes the symmetry in use**: its planes are drawn, its
card shows, and what you make next is made at its images too, so you can add to the model the way it was made. (A symmetry that is selected itself is the one in use; the one in use stays when nothing is selected.)

## The field viewer

A *field* model (a distance, a ramp, noise, `field_from_body(...)`, anything made from them with arithmetic) is not a body, so no surface of it is drawn.
**Every field has an icon in the viewport and a viewer of its own**:

- **The icon.** A shown field is a small green field icon in the scene, with **its name over it in a card** (the style of the labels of boundary
  conditions), so that where there are several fields you tell at a glance which is which. It sits **where the field is about**: the place of the
  object it was made from (`distance_to_point(anchor)` at `anchor`, `depth_below(part)` at the middle of `part`; a field made of those, such as
  `ramp(distance_to_point(anchor), ...)` or `a * b`, at the first of them) -- a field made of nothing, such as `x_field()`, at the origin.
  **Drag the icon** to put it somewhere else: it is positionable, in the plane facing you, and the disc goes with it. When a point's dot is
  where the icon would be -- a field made from a point starts on it -- the icon is drawn beside the dot, so the dot can be clicked to select the point (and
  show its gizmo); several icons that would be on top of each other are moved apart in the same way.
- **The disc has arrows of its own while its viewer is open**, as the section plane has: an **arrow along the way it faces** (drag it to move the disc through the field),
  **two arrows in its plane** (drag one to slide the disc along that axis) and the **dot in the middle**, which moves it freely in its plane, as the dot of a
  model's gizmo does. Nothing keeps the disc inside the render region or anywhere else. (A field has no model gizmo: nothing of it is moved in the script, the viewer is.)
- **Show and hide.** A field has an **eye** in the model tree, like every model: when it is off, the icon and the viewer of the field are not in the scene
  at all. (A field is shown when the script displays it -- the eye writes that line, and a field made from the menu is displayed at once. A field
  that only feeds other models, such as the cell size of a lattice, stays hidden until you turn its eye on.)
- **Select it** by clicking its icon, or its row in the model tree (the two are one selection; Ctrl adds). The icon lights up and **its viewer opens
  where the icon is**. Click the icon again, or select something else, to close it. Several fields can be open at once, each with its own plane.
- **The viewer is a disc through the field.** It shows the value at every point of a plane **in the section view's own colours and dark lines**: blue to cream to orange,
  with a dark line at every round step of the value, and a heavier one where the field is zero. (The scale is found from the field once and kept while you move the
  disc.) Hover the disc to read the value. It **cuts no model**.
- **The card** (like the section card) edits the viewer of the field that is selected: **X / Y / Z** chooses which way the disc faces, **Radius** (a slider and a box)
  sizes it, and **Opacity**, the colour legend and a flat **2D view** work as on the section card. There is no position slider. With several fields open, a menu
  in the card chooses the one it is about. It has no close button: it is there while a viewer is open.
- It is independent of the section view: the two can be open together, each with its own plane and its own card.

The same icons are in an [app's viewport](apps.md#a-viewport-look-at-it-drag-it): a field given to `app.viewport({'Stress': field})` is an icon with its name
over it, and a click on it opens its viewer.

## The cards

The model tree, the section card, the field viewer and the result card are **cards** floating over the viewport, and every one can
be **dragged anywhere inside it** (by its header, or by any empty place of the card; a click on the header still collapses it) and
**resized from its edges and corners**. A card keeps its place relative to the window when the window changes size, and where you
left it and how large it was are remembered for the next start. A card you never moved or sized keeps its own place and size.

## Windows of their own

Any major element of the window can be pulled out of it, to work on more than one screen: the **code editor**, the **console and output**, the **viewport**, the **model tree**, and the cards -- the **section**, the **field viewer**, the **work plane**, the **symmetry**,
the **result** and the **live view** of an optimisation. Each has a small button in its **top right corner** (a card with a close button: just left of it). Pressed, it takes the element out into a window of its own, which can be put on another screen and sized like any window,
and **nothing stands in its place**: the panes round it take its room, so a window that is out saves the room it had (the viewport takes the whole width when the editor is out, the editor when the viewport is). Pressed again -- the button is in the corner of the
element, in its window --, or with that window closed, the element goes back to where it came from, and the panes give the room back.

- The window of an element keeps the **shortcuts** of the window it came from: the commands of the main window work in it, and so do the keys of the pane it belongs to.
- Its **place and size are remembered**. When FielDes ends with elements pulled out they are put back first and **pulled out again at the next start**; the fact is cleared as it is read, so a start that crashed does not repeat itself.
- A card that is pulled out is placed by its window, not by the rules of [The cards](#the-cards): it is not dragged, collapsed or sized there until it is put back.
- The viewport is a graphics widget: in a window of its own it gets a graphics context of its own and the picture is drawn again there; the cards that float over it go with it unless they are pulled out themselves.

## Legends and probing

A model coloured by a field (`colored(shape, field)`, the fit-deviation shading of an import, …) gets a
**legend** at the bottom right: the colour map with its range. **Hover** the model to read the field's
value at the surface point under the cursor. Close a legend with its ×: its probing stops with it.
**View → Show legends** brings them back.

## The result card

Shown at the bottom right when an analysis result is displayed (a result stated on its own, like any shape):

- the **field** drop-down (von Mises, displacement, each component, principal stresses, strain energy;
  temperature and heat flux; speed, pressure, vorticity, ...) above the colour bar — hover the part to read
  the value under the cursor;
- the **key numbers** under it: what the result found that matters (maximum stress and displacement, safety factor, reactions; drag and lift of a flow and their
  coefficients, pressure drop; temperatures; natural frequencies) -- the same numbers `export_results(result, "numbers.txt")` writes;
- a short note under the bar says what the values are: **Nodal average** (the elements' values averaged at
  each node: peaks are lower than the elements' own), **Element values**, or **Solver values at nodes**;
  hover it for the explanation;
- the **step** rows — one step back, play / pause, one step forward, then how it runs and how fast, on one
  row; the step's name on the next; the slider below them across the card's whole width — the same for every analysis: a static result steps the load from 5 % to
  100 %, a mode the phase of its vibration, a flow its solver iterations (the Stokes start to the converged
  flow) or, in time, its stored times (fields and streamlines), a topology optimisation its iterations (the
  part as it was after each one, meshed the first time it is shown; a flow optimisation the body and the flow
  around it). The button beside play sets how it runs — round and round (↻), back and forth (⇄), once to the
  end (→) — and the drop-down its speed: ×⅛, ×¼, ×½, ×1 (ten steps a second), ×2, ×4;
- the **deformation** slider (`1:1` = true size) — the part is drawn deformed;
- **Elements**: draw the tetrahedra (or hexahedra) the solver used, each with its own value (the
  stress in a tetrahedron is constant in it); hover one to read it. The line below says how many there
  are. The result line and the safety factor use the elements' peak. The elements carry the result's own
  values, so the button is there at the result's own step (the full load, the last time);
- **Flow** (a fluid result): the streamlines from the inlets, coloured by the speed, with particles moving
  along them, drawn over the fluid (which is drawn like every other result, coloured by its field); its
  **Opacity** slider lowers how opaque the fluid is drawn, so that the body in it shows.

**What you choose on the card stays.** The field shown, the deformation (as a multiple of the result's own automatic one),
Elements, Flow and the opacity are kept by the window, not by one result: the next run's result, and every picture of an
optimisation that is running (a new one after every iteration), is drawn the way you set the last one. A field the new result
does not have is left alone (it shows its own first one).

## Status bar and progress

The status bar shows the render state on the left and, on the right, **where the keyboard is** (*Keys: viewport* or *Keys: editor*, the pane that has the keyboard also has a frame), the region, the resolution and the quality.
A thin progress bar shows meshing. While a script runs, the output pane shows the statement and the
progress of the operation in it.

## Exporting

- **File → Export mesh…** (`F7`, `Ctrl+Alt+E`): the shapes in the viewport as one mesh, in the format of the file you name -- **STL, OBJ, PLY, 3MF, glTF (`.glb`, `.gltf`), FBX, OFF, DXF**.
- **Right-click a model (in the viewport or on its row) → Export:**
  - a **body**: **Mesh** in any of those formats (the surface at the scene's resolution);
  - a **field**: **Field on a grid** (VTK image data `.vti`, or a CSV table) -- the field sampled on a regular grid for other programs;
  - a **result**: **Finite-element model** (Abaqus / CalculiX `.inp`, Nastran `.bdf`, Gmsh `.msh`) and **Results** (VTK `.vtu`, CSV tables, **key numbers as text** `.txt`). The model and its mesh are those of a static analysis; the others export their key numbers and their fields.

  The file is asked for, then written in the background by the same functions a script calls, so the window stays alive.
- **Export screenshot…** (`Ctrl+Shift+E`) and **Copy screenshot to clipboard** (`Ctrl+Alt+C`).
- A script exports by itself with `export_mesh(part, "part.stl")`, `export_fea(result, "bracket.inp")`, `export_results(result, "bracket.vtu")` and `export_field(field, "stress.vti")` -- see [Exporting and importing files](meshes.md#exporting-and-importing-meshes).
- **Key numbers on the screen:** the result card prints the numbers that matter under its field drop-down -- the maximum stress, displacement and safety factor of a static analysis, the drag and lift (and their coefficients), flows and pressure drop of a flow, the temperatures of a thermal analysis, the natural frequencies of a modal analysis -- and the same numbers are the **Export key numbers as text** button of the result's property drawer.

## Theme: light, dark and your own colours

**Settings → Theme…** opens a small window (it stays open while you work, and what you change shows at once): **Colours** chooses the **Light** set -- a pale window, white bars, a
paper-white editor -- or the **Dark** set -- a dark teal window, the editor in Solarized's darkest base. The viewport and the cards on it (model tree, section card, result
card) are dark in both. Under it the eight colours of the set are listed -- **Window**, **Bars and buttons**, **Editor**, **Line numbers**, **Lines**, **Text**, **Quiet text** and
**Selection and focus** -- each as a swatch: a click on it chooses another colour, **Default** next to it takes the colour back to the set's own (it is enabled only for a colour you
chose), and **Reset colours** takes all of the set that is on back. The two sets keep their own choices, so a colour you changed in Light does not leak into Dark. The shades that
follow from the eight (the border of a field, a pressed button, the selected text, the page of a file that is not run, the scroll bar handles) are worked out from them, so a
choice of yours never leaves a part of the window in the colours of the other set. The choice is kept in the settings and is there the next time FielDes starts.

## Language

FielDes shows its menus, dialogs, tooltips, the guided tour, the guide, the creation forms and the result legends in **English, Spanish, French, German,
Japanese, Simplified Chinese or Russian**. The default is the language of Windows (the first of the computer's preferred languages that FielDes has);
**Settings → Language** chooses another and offers to **restart now** (the language is read when the program starts; an unsaved script
is asked about, as when closing). The installer asks for the language too.

What stays in English: the names of functions and arguments (they are code), the messages and notes the Python library prints about a script
(an error in your own code reads the same everywhere), the written documentation (`docs/`) and the command line help. A text that has no translation
reads in English, so nothing is ever blank. The translations are plain files, `translations/<code>.json`: see `translations/README.md` for how to
improve one or add a language.

## Units

**Settings → Units...** chooses the units that numbers are shown in: **Length** (mm, cm, m, in, ft, µm), **Force** (N, kN, lbf, kgf), **Stress and pressure** (MPa, GPa, kPa, Pa, bar, psi, ksi), **Mass** (kg, g, t, lb) and **Temperature** (°C, K, °F) --
on the ruler, in the cards and colour bars of results, in the value under the cursor, on the work plane (its step, offset and readout) and in the radius of a surface selection -- and the length unit that **mesh files are written in**
(and read in, when the file does not say what its numbers mean). The button **Millimetres, newtons, megapascals** (its tip: *Back to the units the program works in*) sets millimetres, newtons, megapascals, kilograms and degrees Celsius again. The choice is kept in the settings.

What does not change: **the script and the solvers always work in mm, N, MPa, kg and °C**, so that a script means the same on every machine -- a size typed in a script is in millimetres whatever is chosen here.
For the Python side (`fieldes.units`, `export_mesh(..., unit='in')`) see [Units](scripting.md#units).

## Solvers of your own

**Settings → Solver...** shows the equations the analyses solve, and is where solvers of your own are added. The list holds the analyses FielDes has built in (*built in, solved in C++*: their equations are shown, to read), the solvers of the library
(*of the library: make a copy to change it*: `heat` and `elasticity`, written in Python) and the Python files of your solvers folder. A solver is a function that is given the mesh and gives back fields; `solve('name', part, ...)` runs it and draws
what it found like any result. **New solver...** starts from a working one, **Add from a file...** brings in a solver written by somebody else, **Make an editable copy** copies a solver of the library so that its equations can be changed,
**Save and check** writes the file and reads it again (the line under the editor says *Saved and read* or what is wrong), **Open the folder** shows the solvers folder, and **Delete** removes a file of yours after asking. Because a solver can be changed beyond repair, **Reset to default** puts the text it started with -- the library solver it was copied from, the template of a new one, or the file as it was added -- back in the editor (nothing is written until *Save and check*, and `Ctrl+Z` brings back what was there), and **Previous version** puts back the text the file had before its last save (it is kept as `name.py.bak`).
See [Solvers of your own](solvers.md).

## Validation and accuracy

**Help → Validation and accuracy** opens a window with the contents of [the validation page](validation.md) on the left and the page itself on the right: what every kind of analysis (static, modal, thermal, flow, the optimisations, manufacturing,
meshing for another program, CAD, solvers of your own) was checked against, the accuracy that was found, and what is not validated.

## The guided tour

The first time FielDes starts, a **welcome card** says in a few lines what FielDes is (field driven design) and offers the tour: **Start the guided tour** (orange)
or the quieter **Skip the guided tour**. **Help → Guided tour** starts it at any time, **where you stopped**: the chapter and the step you were at are kept, so the tour goes on from there and not from the start. **Leaving it -- on the first card, or at any step -- ends with a card that says where it is found again** (the Help menu, with its entry framed, and the chapter it goes on at), so it is never lost. It runs on the program itself, not on a script.

The tour is in **twelve chapters**, one after the other. **Every chapter ends with a card that says what comes next** (*Next up: chapter 2*, and what that chapter is about) **and asks whether to go on or to explore on your own** (*Continue* or *Explore on my own*; the second leaves the tour at the next chapter and shows where it lives). The **Chapters** button on every card lists the twelve (a tick for the ones that are done, the one you are in in bold) and goes to the one you choose, and the place in the tour -- chapter and step -- is a small line above the title of the card. The chapters:

1. **How FielDes works: the script and fields.** The script is the model; changing a number changes the design; every shape is a field (the section view shows it); a field in place of a number, and what that means for design (move a point and the thickness moves with it); an analysis is a field too.
2. **Navigation, gizmos and shortcuts.** The viewport follows the model; the standard views (a click on a point of the little coordinate axis in the corner of the viewport); your own navigation (Settings → Navigation); panes pulled out into windows of their own (the button in the corner of a pane pulls it out, and the same button puts it back); the gizmo, and the one you get with Shift; the keys worth knowing.
3. **Modelling with objects and the model tree.** Dragging a surface, the tree, models inside models, renaming, making things, a shape from a formula (`low_level_body`, then the formula changed), your primitives, placeholders, sections, a model used in two places (Ctrl+drag), the points, edges and faces a right-click on a body makes (**Point at the corner**, **Select Surface**), **snapping one body onto another (S)**, copying or linking a model (Shift+D, Ctrl+D), the property tab of a result.
4. **Work planes, 2D shapes, paths and extrusions.** Drawing on a work plane, stopping and starting again, pulling a point out of an edge, corners, lines through points and a sweep along one.
5. **Symmetry.** A mirror, editing the original or an image, switching the symmetry off and on (Shift+S).
6. **The special features, in short.** Lattices, simulations and optimization (shown running), flows, manufacturing, apps, and custom functions from a block.
7. **Build a lattice.** Fill a block with a lattice from the right-click menu, with a field as the thickness of its walls; choose another cell; make the cells smaller; a **foam** that does not repeat (`random_points` spread through the block, then `voronoi_lattice` round them); and a **cell of your own** (`custom_tpms`: change its equation, then fill the block with it).
8. **Build a static analysis.** Select the two end faces of a beam with the right-click menu (**Select Surface**), hold one end (`fixed`), pull the other (`force`, with its placeholder), choose a material, run `static_analysis` on the beam, the support, the load and the material, and change the load.
9. **Build a flow analysis.** Choose water (**Engineering → Materials → New fluid**), select the two end faces of the water with **Select Surface**, make an inlet (with its speed) and an outlet of them, solve `fluid_analysis` round a post (the placeholder for the boundaries typed over), and widen the post.
10. **Build a topology optimization.** `topology_optimization` on the analysis of chapter 8, typing over what it should do, and allowing less material.
11. **Build a manufacturing optimization.** The rule of a 3D printer (`fff_constraint`), how far a wall may lean, `manufacturing_analysis` that colours where the rule is broken, and `manufacturing_optimization` that changes the part until it is kept.
12. **Build a FielDes app.** A number written as an input (`app.number`), File → Preview as app, File → Export as app.

The steps of chapters 7 to 12 work the way you work: **select the models, right-click, choose the entry** (Engineering → Structural → static_analysis, say), type over a placeholder or a number. Each starts from the model the menus write, so it works whether or not you did the step before it, and **Show me** does it for you with a pretend hand -- Ctrl+click on the rows, right-click, the entries one after the other, the typing -- and then puts everything back so that you can do it yourself. The steps that need more than the plate -- a field, a reference, a result, a beam, a channel -- **work on a small model of their own**, so that only what the step is about is there, and the tree shows only the rows it is about; your model is back when the step is left. **Every step has a state of its own, and starts from it**: what you changed or deleted in a step -- a number, a row, a name, a section -- is put back when the step is left, so the next one is whole (the steps that build something one after the other keep what the step before made, inside their chapter). Each step

- **dims** the window except what it is about, and puts a **glowing frame** round it (and round the line or field it means) -- yellow while there is
  something to do, and **green** (the frames, the arrow, the card's border and its button) as soon as the step is done, so nothing says there is more to do;
- shows the **script and the picture as one thing**: the lines of the script a step is about stay lit and framed in the editor even
  while the step is about the viewport or the tree, and every line that **changes or moves** -- typed, or edited by a drag in the
  viewport, by a drop or a rename in the tree, by a menu -- glows for a moment and fades (lines that only moved down because one
  was put above them do not glow);
- puts the card **beside** it and draws an **arrow** to it;
- lets you do **only that one thing**: a click anywhere else makes the frames flare, and the keyboard is held back, except where the
  step is about typing;
- can be done **for you**: **Show me** moves a pretend cursor and sends the program the same mouse events a hand does (types a number
  into the script, drags a surface and a gizmo, clicks a row, opens the right-click menu, switches on the section view and drags its
  plane, drags a field onto an operation, renames, edits a render setting) so that you see what it should look like. **It shows it once
  and then puts everything back as it was** before you pressed the button, so that you can do it yourself: the step does not count it
  as done and does not move on;
- has **a state of its own**: the script, what is selected and whether the section view is on are, for each step, what the steps
  before left the first time it is opened, and the same again whenever the step is opened later (**Back** included). Go back to *Change
  a number* and the number is as it was, ready to be changed;
- can be **skipped** (*Skip step*), gone back from (*Back*), or left altogether (*Skip the tour*, on the first card *Skip the guided tour*): the last card then says where the tour is found again.

The tour works on a model of its own (a plate with a hole, loaded as a new script: your files are not touched; a question asks first
if the script in the editor has unsaved changes). It builds a field, a second model and a small analysis as it goes, so the later steps have something
to show. When it is done, the model stays for you to play with.

## Keyboard shortcuts

Every command can be rebound in **Settings → Keyboard shortcuts…** (`Ctrl+K Ctrl+S`). The defaults:

| File | |
|---|---|
| New / Open / Save / Save as | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` |
| Import model | `Ctrl+I` |
| Export STL | `F7`, `Ctrl+Alt+E` |
| Extrude the selected 2D shape (viewport or tree focused); a 2D shape and a curve selected together make a `path_extrude` of the shape along the curve | `Ctrl+E` |
| Export / copy screenshot | `Ctrl+Shift+E` / `Ctrl+Alt+C` |
| Quit | `Ctrl+Q` |

| Edit | |
|---|---|
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` |
| Find / replace / next / previous | `Ctrl+F` / `Ctrl+H` / `F3` / `Shift+F3` |
| Next occurrence (multi-cursor); with the viewport or the tree focused, link the selected models | `Ctrl+D` |
| Go to definition / line | `F12` / `Ctrl+G` |
| Back to the rendered script / next tab / previous tab / close tab | `Alt+←` / `Ctrl+Page Down` / `Ctrl+Page Up` / `Ctrl+W` |
| Toggle comment | `Ctrl+/` |
| Duplicate / delete / move lines | `Ctrl+Shift+D` / `Ctrl+Shift+K` / `Alt+↑ ↓` |
| Breakpoint / continue | `F9` / `F8` |
| Pause / continue the running script | `F6` |
| Run the script again from the beginning | `Ctrl+Shift+F5` |
| Terminate the running script | `Shift+F5` |
| Fold at cursor / fold all / unfold all | `Ctrl+Shift+[` / `Ctrl+K Ctrl+0` / `Ctrl+K Ctrl+J` |
| Autocomplete | `Ctrl+Space` |

| View | |
|---|---|
| Model tree | `Ctrl+Shift+T` |
| Section view | `Shift+X` (viewport focused) |
| Copy / link the selected models (viewport or tree focused) | `Shift+D` / `Ctrl+D` |
| Snap the first body onto the second by the selected faces, edges and points (viewport or tree focused) | `S` |
| Work plane view: open / close, new work plane, set the view to it | `Shift+W`, `W` (viewport focused); `Ctrl+Shift+W` anywhere |
| Use the symmetry that is selected, or stop (with none in the script: make one) | `Shift+S` (viewport focused) |
| In the work plane view: next kind of drawing (line, spline, point; after stopping: the one that was on) / stop drawing / end a line / take a point back / cancel | `E` / right click / `Enter` or a double click / `Backspace` / `Esc` |
| Front / back / right / left / top / bottom / iso | `1` / `Ctrl+1` / `3` / `Ctrl+3` / `7` / `Ctrl+7` / `0` |
| Frame all | `Home` |
| Leave the mode you are in, one step at a time: the line being drawn, the kind of drawing, the work plane view, the section view; in the terminal's line, back to the editor. With none of them on: cancel a render | `Esc` |

| Settings | |
|---|---|
| Editor text bigger / smaller | `Ctrl++` / `Ctrl+-` |
| Keyboard shortcuts | `Ctrl+K Ctrl+S` |

| Help | |
|---|---|
| The guide (features and shortcuts) | `Shift+F1` |
| Shape reference | `F1` |
