# The interface

- [Top dock](#top-dock)
- [The editor](#the-editor)
- [The output pane](#the-output-pane)
- [The viewport](#the-viewport)
- [The model tree](#the-model-tree)
- [The section card](#the-section-card)
- [Legends and probing](#legends-and-probing)
- [The result card](#the-result-card)
- [Status bar and progress](#status-bar-and-progress)
- [Exporting](#exporting)
- [Keyboard shortcuts](#keyboard-shortcuts)

## Top dock

The bar along the top of the window has **New** (a plus), **Open** and **Import** as large icons at the very left
(`Ctrl+N`, `Ctrl+O` and `Ctrl+I`; hover for the tip; the plus is File → New: a fresh script, asking first when the one you have is
unsaved). A **small arrow** beside Open and Import opens a list of the files used lately: the scripts
you opened (the same list as File → Open recent) and the models you imported (a click imports one again, as Import model
does; *Clear list* empties it; a file that is gone is left out). Then the menus:

| Menu | Holds |
|---|---|
| **File** | New, Open, Open recent, **Open example file** (the small scripts of the `examples` folder next to the program, one for each family of features: open one, run it, change it), Import model, Open as viewer, Load the default script, Revert to saved, Save, Save As, Export STL, Export / copy screenshot, Quit |
| **Edit** | Undo and redo, Find and replace, Go to (definition, line, the editor's tabs), Lines, Breakpoints, Folding, Autocomplete |
| **View** | Origin axes, orientation triad, legends, bounding boxes, Projection, Model tree, Section view, Standard views, Frame all, Zoom to bounds, **Meshing algorithm** (the render options: dual contouring, iso-simplex, hybrid) |
| **Settings** | **Keyboard shortcuts…**, **Blocks folder…** and **Show the blocks folder** (your [custom blocks](blocks.md)), editor text bigger / smaller, Automatically reload changes, Rotation mode, Rotation sensitivity, Zoom center |
| **Help** | The guide, About, **Guided tour** (cards over the program that walk through the basics, offered the first time it starts; see also the [tutorial](tutorial.md)), Shape reference |

Everything else is in the viewport: the model tree, the section card, the legends and the result card.

## The editor

A code editor for the Python script, with:

- syntax colouring; line numbers in a slim gutter; **folding** (the arrows in the gutter), and **sections**: a comment
  that starts with `#SECTION` (or `# SECTION`), with a title after it, folds everything up to the next `#SECTION` comment
  (or the end of the file) when its arrow is clicked; the box that stands for the hidden lines says how many they are. Typing
  `#sec` offers `#SECTION` in the completion. Sections only hide text: the script runs as it is, and one that holds an error,
  or a line you go to, opens by itself. `# %%` and `# region` ... `# endregion` fold the same way;
- **completion** while you type (`Ctrl+Space` forces it) and call tips for arguments. **A function you pick from the list is written complete,
  the same call a right-click menu entry writes**: each argument it cannot do without is a **placeholder** after its name (`select_surface(shape=..., seed=...,`),
  the first one selected so that typing replaces it and `Tab` going on to the next -- the statement does not run until they are all filled in (nor does what is made from it; the rest of the script does)
  (see *Placeholders* under the model tree) -- and **every other argument is written with its default**, so that what exists is in front of you, and what
  you do not need can be kept or deleted. A call that does not fit on a line (88 columns) is written **one argument to a line**, as every nested call
  and list in it. **Hover an argument's name** and the documentation says what it is, with its default. `Ctrl+Alt+Enter` (Edit > Lines) splits the call
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
`box_exact`, a method such as `.max`, or a function from a module that sits next to your script
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

## The viewport

| | |
|---|---|
| **Shift+left-drag** or **middle-drag** | Rotate (turntable; **Y points up** unless you choose Z up in **Settings → Rotation mode**) |
| **Right-drag** | Pan |
| **Left-drag** | Draw a selection rectangle: when you let go, every model that lies **wholly inside** it is selected (a model that is only partly inside is not; a rectangle that holds none leaves the selection alone). With **Ctrl+left-drag** the models inside are added to the selection instead of replacing it. A drag that starts on a surface you can pull (a model in *handles* mode, not locked, alone in the selection) pulls the surface instead, and one that starts on a handle or a gizmo moves that |
| **Right-click** empty space | Make things where the cursor is: **New 3D shape**, **New 2D shape**, **New point**, **New surface**, **New field** and **New custom block** (a menu for each kind, each entry with the kind's icon: every primitive of the library, your [blocks](blocks.md) that need no argument), **Import model…** (a STEP file or a mesh, written as one `import_model` line: see [Importing models](step-import.md)), **Add operation** (*thicken*, *shell*, *smooth*, moving, repeating and combining, the **Lattices** *lattice* and *lattice_surface_conform*, *tessellate* under **Importing**, and the custom blocks that work on a model) and **Add simulation**: **New material**, **New fluid**, **New support or load**, **New thermal condition** and **New flow condition** (the presets of the library written out with their numbers; a condition is written with a placeholder where it acts, for a body, a field or a surface to be dropped on -- **or, with several models selected, for the body and the regions you selected**: see below), then, with a model, the analyses. Choosing one writes the call into the script, with a line showing it, and selects the new model, ready to drag. A new thing is placed where the cursor is (the point of the ray under the cursor that is closest to the origin, as large as about a hundred pixels, and in the plane through the origin that faces you, so y = 0 in a front view; a place outside the render region is moved just inside it, or it would not be drawn) and goes at the end of the script; an operation works on the selected model (or the last one) |
| **Right-click** a model (or a line of the script that defines it, or its row in the model tree: see below) | **center** and **bounding_box** (the middle of the model's box, as a point, and the smallest axis-aligned box that holds it, as a body: library functions of their own, used all the time, so at the top of the menu), **Operation** (the same list, written with that model as its argument, under its definition; with **several models selected** and the one you right-click among them, the operations that combine models work on all of them, in the order you selected them: *union* unites all, *difference* takes every one after the first from the first, *intersection* keeps what they share, *exclude* locks the others as regions of the first (see [Excluded regions](fields.md#excluded-regions-exclude)); **Surfaces → surface_from_bodies** makes the first one's surface where the others meet it, and of one model alone, all of its surface (see [Selecting surfaces](selecting-surfaces.md#by-other-bodies)); the others work on the model you right-clicked), **Simulation** (*static_analysis*, *modal_analysis*, *topology_optimization*, *thermal_analysis*, *fluid_analysis*: the call is written with that model as the part, and a **placeholder for every kind of boundary condition** the analysis takes -- `supports=...`, `loads=...`; `inlets=...`, `outlets=...`, `boundaries=...` -- that you then fill in the model tree or the script; the result is a model of its own, shown when it is solved. **With several models selected** the body is the part and the conditions among them go in the input of their kind: see below), **Delete** (the model, or all the selected ones) and **Select Surface**, which opens the menu of the **surface selection**: the mode, the angle and the radius of a flood fill over the surface from the point you clicked; **Select** writes a `select_surface(...)` line into the script, under the model's definition. See [Selecting surfaces](selecting-surfaces.md) |
| **Wheel** | Zoom (about the cursor or the scene: **Settings → Zoom center**) |
| **Double-click** a model / the background | Frame that model / everything (`Home`) |
| **Click a model** | Select it: its row in the tree and its code in the editor. The keyboard stays in the viewport (also when it was in the editor before), so the single keys that work on the selection — `E`, `R`, `V`, `C`, `I`, `D` — are never typed into the script |
| **Click empty space** | Deselect everything (not with Shift or Ctrl held). The keys that work on the selection then have nothing to work on until you click a model |
| **Ctrl+click** a model | Add it to the selection, or take it out when it is in: select several models one by one |
| **`I`** (viewport or model tree focused) | **Isolate** the selected models: only they are shown, every other model hidden (script edits, undoable); `I` again shows what was shown before. Select another model while isolated and press `I` to isolate that one instead |
| **`D`** (viewport or model tree focused) | **Delete** the selected models from the script: their definitions and the lines that show, hide, edit and lock them go, as one undoable step. If the rest of the script still uses their names it asks once first. Also in the right-click menu of a model (**Delete**; with several models selected and the one you right-click among them, it deletes all of them) |
| **Hover a model** | Highlights it; shows the point under the cursor at the bottom left |

Standard views: front `1`, back `Ctrl+1`, right `3`, left `Ctrl+3`, top `7`, bottom `Ctrl+7`,
isometric `0` (they act while the viewport has keyboard focus — click it first — so they never eat
typing). The triad at the top right is also clickable: click an axis to look along it.

The View menu switches between orthographic and perspective projection and shows or hides the origin axes,
the triad, the legends and the bounding boxes. The scale bar at the bottom shows the scale at the centre of the view.

What is drawn is the mesh of the script's shapes over the region `view.set_bounds(...)` at the
resolution `view.set_resolution(...)`. Imported parts may be meshed at their own resolution
(`roi_resolution`). See [Caching and performance](caching-and-performance.md).

### Right-click anywhere

The viewport's right-click menus are not only the viewport's: **the same menu opens wherever a model is** -- in the viewport,
on a **line of the script** that defines a model, and on its **row in the model tree** -- and means the same there, as if that
model had been right-clicked in the viewport:

- On a line (or row) that defines a model: **Operation**, **Simulation**, **Select Surface** (greyed outside the viewport: the
  selection spreads from the place you click on the surface) and **Delete**, written for that model.
- On a line of the script that defines no model (a blank line, a comment, the imports): the menu of empty space, **New 3D shape**
  and the rest, and what you make is written **under that line** (a line that is indented, opens a block or is in the middle of a
  call would be cut in two, so the new line goes to the end of the script instead). In the empty space of the tree, and in the
  viewport, it goes to the end of the script.
- Selected text keeps the text menu (cut, copy, paste): a right-click inside a selection opens it, anywhere else opens the model's.
- The model tree has no menu of its own: what that menu had is the row's buttons and keys (the eye, gizmo, lock, cache, reimport,
  reset, delete, `V`, `E`, `R`, `C`, `D`).

**Nothing is asked: what a menu entry writes is code, and the code is the place to change it.** A *topology_optimization* is written with its
`volume_fraction`, `element_size`, `iterations` and every other argument it has, each with its default; a *lattice* with its cell, its cell size and
the rest; a `force` with its `vector` -- and where something is **missing** (the region a force acts on, a body for an analysis) the call has a
**placeholder** after the argument's name (`force(region=..., vector=(0, -100, 0), profile=None)`). It is the same call the completion writes,
laid out the same way (one argument to a line when it does not fit). Nothing opens a form: edit the numbers in the code editor, where the call is.
(The visual jobs stay visual: dragging a surface or a gizmo, picking a surface with *Select Surface*.)

**An entry the selection cannot make is greyed out** (no message: it is just not offered). Every selected model is classified once -- a part, a profile, a field, a surface, a point, a lattice cell, a result,
a support, a load, a thermal or a flow condition (each of them a kind of its own: the input of an analysis it goes in), a material or a fluid -- and every entry says what it takes; a selection that has something an entry does not take in it, or
lacks what it needs, does not fit. So *move* or *offset* is greyed out with a force selected; a **Simulation** entry is greyed out only when the selection has something it does not take -- *static_analysis* with a thermal condition or a fluid
among the models (*modal_analysis* takes no load, *thermal_analysis* only thermal conditions, *fluid_analysis* only flow conditions and a fluid), or with a second part or a second material for an
input that takes one (*flow_topology_optimization* takes two parts: the body and its fluid domain); a surface, a field, a point or a lattice cell is no argument of any simulation. **A simulation is never greyed out for lack of something**: with nothing selected (right-click
the empty viewport), with only its conditions or only its material selected, it is written with a placeholder for the part and for everything that is missing; the conditions of **New support or load** and the other **New ... condition**
menus take the surfaces, fields or bodies they act on -- **a condition has no body of its own**: the simulation is given the body. A model that has not run yet has no known kind and fits everything; your own [custom blocks](blocks.md) say nothing about what they take, so they are always offered.

The **Simulation** menu lists *static_analysis*, *modal_analysis*, *topology_optimization*, *thermal_analysis*, *thermal_topology_optimization*,
*fluid_analysis* (a plain analysis: it solves the flow, it does not change a body) and *flow_topology_optimization*. Each is written with the model you right-clicked as the part (the first argument of every simulation; the boundary
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
| square, teal | **2D shape** | a profile (a field that does not depend on z), drawn flat in the z = 0 plane |
| soft disc, green | **field** | a value at every point that is not a body: a distance, a ramp, noise, a stress; not drawn: select it and the [field viewer](#the-field-viewer) opens on it |
| sheet, purple | **surface** | the zero of a field with no body behind it; drawn as a thin sheet |
| cross, amber | **point** | a point: not drawn, its gizmo shows where it is |
| bars, red | **simulation** | a solved analysis or optimisation |
| hatched block, tan | **material** | what a part (or a fluid) is made of: `Material(...)`, steel, aluminium, water ...; an analysis is given one (`material=steel`). A model like the others: it is in the tree, and dragged onto an analysis it becomes its `material=` (or `fluid=`) |
| arrow on a base, orange | **conditions** | what an analysis is given: supports, loads, thermal and flow boundary conditions. Dragged onto an analysis, its **boundary conditions** row or the placeholder of its kind, one or several of them go into the input of their kind, as a list |
| hexagon, gold | **lattice cell** | what a lattice is made of: a cell, a cell map, a graph of beams |
| dashed square, pink | **selection** | a patch picked on a surface |
| open box, teal | **import** | a file read in |
| **f** badge on any of them | **custom block** | the model was made by one of your [blocks](blocks.md) |

Hover a row: the first line of its tooltip says what it is.

**Nesting is the structure of the calls.** The models an operation is made of are its **children**: `thick = offset(plate, 1)`
shows `plate` under `thick`, and `plate = difference(base, hole)` shows `base` and `hole` under `plate`, **in the order the call is
written**. A model that is used by several statements has its row under the *first* one; under each of the others it has a
**shadow**: a half-transparent row with the same icon and name, which is that statement's own reference to the model. What nobody
uses is at the top. Rows are open by default; the arrow closes one. Because the tree is the structure of the calls, **every
nesting, renesting and denesting is an edit of the arguments of a call**, written into the script, never only a move in the tree:

| You | The script |
|---|---|
| nest: drop a model on an operation | the model becomes an argument of the call (see the table below) |
| renest: drag a nested model onto another operation | it leaves the old call's arguments and joins the new one |
| denest: drag a nested model between top-level rows, or into the empty space | it leaves its call's arguments (and its definition moves there, as a top-level model does) |
| reorder: drop a model between the children of an operation | it stands at that place among the call's arguments (`union(a, c, b)`) |
| drag a **shadow** | only that reference moves (or goes): the model, and its other references, stay |
| drag a **locked** model | it is **read-only**, so a drag never moves it: **it is always a copy**, a new shadow, as if Ctrl were held -- the model keeps its row and its place in the script. Locking a model (the lock button, `R`) is how you say that dragging it must only make references |
| **Ctrl**+drag a row or a shadow | a **new shadow**, and **the original does not move**: the model keeps its row and its place in the script, and its name is written into the call you drop it in, where it shows as a shadow. A label with a **+** and a green mark say it is a copy; releasing `Ctrl` mid-drag makes it a move again. A copy only makes sense inside a call: dropped on top level it is refused, as is a model that is an input of the call already. A model that **nothing else uses yet** stays a top-level row, and the call shows a shadow of it: the call's statement gets a `# shadow: name` comment (written by the drop, like the `# hidden:` lines), which says that the call only *holds a reference* to it and is not its first user; drag the model onto that call without Ctrl and the comment goes: the row moves in. **A shadow cannot go above its original** (see below) |

**Boundary conditions.** A simulation has one row of its own for its conditions, **boundary conditions**, with a **placeholder row for every kind it still waits for** (`inlets`,
`outlets`, `boundaries` ...) and the conditions it has under them, in the order the simulation takes them. Each condition is a model of its own with **its own eye**: shown, it draws only
the surface it acts on, in its colour, with its pads or arrows and its label, on the body of the simulation. **The eye of the *boundary conditions* row shows all of them at once, or hides all of them** (it
shows them when they are not all shown). Select several conditions of one kind (Ctrl or Shift) and drop them **all** on the placeholder of their kind, on the simulation, or on this row: they go in as a list
(`boundaries=[wall_1, slip_1]`). **Dropped on the simulation or on this row, conditions of different kinds each go into the input of their kind** (select the whole problem and drop it at once);
on one placeholder only its own kind is taken, and another kind is refused, with the reason.

**A shadow never stands by itself at the top level.** A model that statements only *reference* (a Ctrl+drag put it there: their line says `# shadow: name`) keeps its own row at the top level, with a shadow in each of them. If such a statement is dragged **above** the model's row, or the model's row **below** the statement, the reference would be the first use, so a message asks: the fix (the default) makes the reference inside the statement the real model. Its definition goes directly above that statement, the comment goes, and the separate top-level row is gone; other statements that use it keep a shadow. *Do not show this message again* always does that (Settings → Show hidden messages again brings the message back); Cancel leaves everything as it is.

**A shadow cannot be put above its original.** The tree gives a model its row under the **first statement that uses it** (the first one after its definition, in the script): that is the original; every later use is a shadow. So a shadow can only go into a statement *below* the original. Ctrl+drag a model into a statement that is *above* it, or move a shadow there, and a message explains it: the new use would come first, so it would become the original and the old place would only show a shadow — and the model has to be defined above any use for the script to run. The same happens **indirectly**: a statement that holds a shadow can be dragged above the original, or the original dragged below it. The program works out the tree the edit would give, before it writes anything, and if any shadow would then come first it asks **once**, listing every model it is about (three shadows in one statement are one message, not three). The message offers the easy fix, which is its default: **the first use becomes the original place** (for a copy (Ctrl+drag), the model's definition goes directly above that statement, when nothing in between uses it) **and a reference stays where the original was**. *Do not show this message again* makes it always do that, without asking (**Settings → Show hidden messages again** brings the message back); Cancel leaves everything as it is; one `Ctrl+Z` undoes it. It is the same kind of rule as taking a model out of an operation that cannot do without it: the script is never left broken.

**Taking a model out of a list.** A model that is one of the things in a **list** a call is given -- `loads=[force_1, force_2]`,
`topology_optimization(part, supports, [[push], [pull]])`, `union_all([a, b, c])` -- is taken out of the list, and the call keeps the rest:
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
a click on a row goes to its line. The rows have no bin: a render setting is a line of the code, and it is changed or deleted there.
The corners the program writes are **whole numbers** (a minimum is rounded down and a maximum up, so the region still covers what it
covered), with two decimals only for what is under 1, a part a few millimetres across. **The region grows by itself**: when a new model comes into the
script and reaches out of the region, `view.set_bounds` is rewritten to hold it, with a little room (a script that gets its region from `roi(...)` keeps it, the
new model given to `roi` as a box); a model that was there already and a file that is opened are left as they are, so a region you made small on purpose
stays small.

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
| **Eye** (key `V`) | Show or hide (hidden displays become `# hidden: …` lines). The assembly (an import row) has no eye; each part has one: it adds the part to the script, or toggles it |
| **Double-click a part** | Use that part in the import statement |
| **Gizmo button** (key `E`) | The gizmo is on the screen **the moment a model is selected**: the numbers that move it are a `handles(...)` line that the model gets when it is selected, and the script has to run for them to exist, so until then the gizmo is drawn from where the model is, a little transparent; a press on it is not lost (the drag starts as soon as the real gizmo is there, if the button is still down). Sets when the shape's gizmo is shown, going round **click** (the default: while the shape is selected) → **never** → **always**. Dragging the shape's surfaces works whatever the mode, and the gizmo has priority where it is shown. On a displayed expression such as `sphere(3)` it first gives the expression a name (`sphere_1 = sphere(3)`). Several selected models are in the multi-select state: one shared gizmo moves them all, whatever mode each has when selected alone (the buttons keep showing each one's own mode). Locked models cannot be multi selected. See [Handles](handles.md) |
| **Lock button** (key `R`) | A switch of its own: a locked shape cannot be dragged at all, and keeps its gizmo mode. It writes (and deletes) the line `part = lock(part)` under the definition. On a row that is one of several selected, it locks or unlocks all of them (as `R` does); a selection with a locked model in it cannot be moved or edited |
| **Keys `V`, `R`, `C`, `E`** | Show / hide, lock, render cache, gizmo mode. `V`, `R` and `C` act on all selected models by one rule: if they are not all in the *on* state (shown, locked, cached), they are all put there first; only when they all are, they are all turned off. `E` sends models that are all in one gizmo mode to the next, and models in different modes to click |
| **Cache button** (stack of disks, key `C`) | The **render cache** is on for every shape unless you turn it off: the button writes (or deletes) the line `part = render_cache(part, False)` under its definition. With it on (the default), the finished mesh is kept on disk and shown at once the next time the same shape is rendered; anything that changes about the math makes it mesh again. Blue: on; green: the mesh on screen was read from the cache; amber: the shape cannot be kept (hover). Off until you click it. See [Caching and performance](caching-and-performance.md#the-render-cache) |
| **custom_resolution row** (a property under a variable, or under a part of an import) | A resolution of its own for that one body, in samples per mm, whatever the scene's `view.set_resolution` is: type the number (the line `part = custom_resolution(part, 3)` is written as you type), the bin takes the line away. It is also **Custom resolution** in the right-click menu of the model. Use it when one part needs more detail than the scene should be meshed at -- or less. See [Caching and performance](caching-and-performance.md#resolution-and-quality) |
| **⟳ Reimport** (on an import) | Read the file again (bumps `rev=` in the call). Imported files are also watched: one saved by another program is read again by itself |
| **⟳ Reimport** (on a part) | Back to what the file says: the part's handle edits are deleted and the file read again |
| **↺ Reset** (on an import) | Delete the import's cache and the handle edits of all its parts, and import afresh |
| **Bin** (every row) | **Delete** the row's object: its statement, the line that shows it, the line that hides it and its `handles()`, `expose()`, `lock()` and `render_cache()` lines are removed from the script (one undo restores them). On an import it deletes the parts' variables too; on a part row, the part's variable; on a render setting, its `view.set_…` line (back to the default). If the rest of the script still uses the name, FielDes asks first |
| **Right-click** | The menu of the viewport for that model, as if you had right-clicked it there (see [Right-click anywhere](#right-click-anywhere)); in the empty space under the rows, the menu of empty space. *Select Surface* is greyed there (the selection spreads from the place you click on the surface, so it is started in the viewport). The things the old tree menu did are the row's own buttons and keys: the eye, the gizmo, lock and cache buttons, reimport, reset, the bin, V, E, R, C, D, and a click on the row (goes to the code, frames it) |

Grey rows are hidden, or shown but outside the render region. An amber row is imported but worth a look
(an open mesh, a suspicious size): hover it.

**The tree is quick.** It is there the moment a file is opened (the tree of the last run of that very text is kept), and the
eye, lock and cache buttons and the **bin** change it **at once** -- a deleted model's row is gone, the lines of every row move with
the edit -- without waiting for the script to run, however long that takes. Click as often as you like: all the clicks are made, and only the last text is run. The gizmo
button changes its icon at once, and the lines it writes are known when the run is done. Details and limits:
[Caching and performance](caching-and-performance.md#the-model-tree).

## The section card

`Ctrl+Shift+X` (or **View → Section view**) opens a card at the top right of the viewport. The section view is a viewer **you switch
on** when you want it; it cuts models. It is separate from the [field viewer](#the-field-viewer), which shows fields on a disc: they
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

**Fields have their own card, the field viewer** (next section): select a field model and it opens by itself, whether or not the
section view is on.

## The field viewer

A *field* model (a distance, a ramp, noise, `field_from_body(...)`, anything made from them with arithmetic) is not a body, so
nothing of it is drawn in the viewport. **Select it in the model tree and the field viewer opens**, a card like the section card.
It is independent of the section view: the two can be open together, each with its own plane in the viewport and its own card:

- It shows the field on a **disc**: the value at every point of a plane, **in the section view's own colours and dark lines**: blue
  to cream to orange, with a dark line at every round step of the value, and a heavier one where the field is zero. A field that
  has both signs is cream at its zero (as the section view is at the surface), blue below it and orange above; any other field runs
  from blue at its lowest value to orange at its highest. (The scale is found from the field once, on a coarse grid in 3D, and kept
  while you move the disc.) Hover the disc to read the value. It **cuts no model**, and has none of
  the buttons that are only about models (cut, flip, whole elements).
- The disc starts **where the field is about**: the point or the middle of the body it was made from
  (`distance_to_point(anchor)` starts at `anchor`, `depth_below(part)` at the middle of `part`, a field made of those, such as
  `ramp(distance_to_point(anchor), ...)` or `a * b`, at the first of them); a field made of nothing, such as `x_field()`, starts at the origin.
- **X / Y / Z** chooses which way the disc faces, **Radius** (a slider and a box) sizes it, **Opacity**,
  the colour legend and a flat **2D view** work as on the section card. There is no position slider: nothing keeps the disc
  inside the render region or anywhere else, and the arrows take it wherever the field is worth looking at.
- In the viewport the disc has the section card's **arrow along the way it faces** (drag it to move the disc through the
  field), **two arrows in its plane** (drag one to slide the disc along that axis) and the **dot in the middle**, which moves it
  freely in its plane, as the dot of a model's gizmo does.
- It has **no close button**: it is there while a field is selected, and goes when none is. With **several fields selected**, a
  menu in the card chooses the one it shows.

## The cards

The model tree, the section card, the field viewer and the result card are **cards** floating over the viewport, and every one can
be **dragged anywhere inside it** (by its header, or by any empty place of the card; a click on the header still collapses it) and
**resized from its edges and corners**. A card keeps its place relative to the window when the window changes size, and where you
left it and how large it was are remembered for the next start. A card you never moved or sized keeps its own place and size.

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
  along them, drawn over the fluid (which is drawn like every other result, coloured by its field).

## Status bar and progress

The status bar shows the render state on the left and the region, resolution and quality on the right.
A thin progress bar shows meshing. While a script runs, the output pane shows the statement and the
progress of the operation in it.

## Exporting

- **File → Export STL…** (`Ctrl+E`, `F7`): the shapes in the viewport as STL meshes.
- **Export screenshot…** (`Ctrl+Shift+E`) and **Copy screenshot to clipboard** (`Ctrl+Alt+C`).
- A script can export by itself: `shape.save_stl(path, lower, upper, resolution)`.

## Language

FielDes shows its menus, dialogs, tooltips, the guided tour, the guide, the creation forms and the result legends in **English, Spanish, French, German,
Japanese, Simplified Chinese or Russian**. The default is the language of Windows (the first of the computer's preferred languages that FielDes has);
**Settings → Language** chooses another and offers to **restart now** (the language is read when the program starts; an unsaved script
is asked about, as when closing). The installer asks for the language too.

What stays in English: the names of functions and arguments (they are code), the messages and notes the Python library prints about a script
(an error in your own code reads the same everywhere), the written documentation (`docs/`) and the command line help. A text that has no translation
reads in English, so nothing is ever blank. The translations are plain files, `translations/<code>.json`: see `translations/README.md` for how to
improve one or add a language.

## The guided tour

The first time FielDes starts, a card offers a tour; **Help → Guided tour** starts it at any time. It runs on the program itself, not
on a script: short cards of one or two sentences, 17 steps, about two minutes (among them the section view, and the Ctrl+drag that makes a shadow reference). Each step

- **dims** the window except what it is about, and puts a **glowing frame** round it (and round the line or field it means);
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
- can be **skipped** (*Skip step*), gone back from (*Back*), or left altogether (*Skip the tour*, also on the first card: *No thanks*).

The tour works on a model of its own (a plate with a hole, loaded as a new script: your files are not touched; a question asks first
if the script in the editor has unsaved changes). It builds a field and a second model as it goes, so the later steps have something
to show. When it is done, the model stays for you to play with.

## Keyboard shortcuts

Every command can be rebound in **Settings → Keyboard shortcuts…** (`Ctrl+K Ctrl+S`). The defaults:

| File | |
|---|---|
| New / Open / Save / Save as | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` |
| Import model | `Ctrl+I` |
| Export STL | `Ctrl+E`, `F7` |
| Export / copy screenshot | `Ctrl+Shift+E` / `Ctrl+Alt+C` |
| Quit | `Ctrl+Q` |

| Edit | |
|---|---|
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` |
| Find / replace / next / previous | `Ctrl+F` / `Ctrl+H` / `F3` / `Shift+F3` |
| Next occurrence (multi-cursor) | `Ctrl+D` |
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
| Section view | `Ctrl+Shift+X` |
| Front / back / right / left / top / bottom / iso | `1` / `Ctrl+1` / `3` / `Ctrl+3` / `7` / `Ctrl+7` / `0` |
| Frame all | `Home` |
| Cancel a render | `Esc` |

| Settings | |
|---|---|
| Editor text bigger / smaller | `Ctrl++` / `Ctrl+-` |
| Keyboard shortcuts | `Ctrl+K Ctrl+S` |

| Help | |
|---|---|
| The guide (features and shortcuts) | `Shift+F1` |
| Shape reference | `F1` |
