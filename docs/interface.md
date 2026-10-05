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

The bar along the top of the window has **Open** and **Import** as two large icons at the very left
(`Ctrl+O` and `Ctrl+I`; hover for the tip), then the menus:

| Menu | Holds |
|---|---|
| **File** | New, Open, Open recent, Import model, Open as viewer, Load the default script, Revert to saved, Save, Save As, Export STL, Export / copy screenshot, Quit |
| **Edit** | Undo and redo, Find and replace, Go to (definition, line, the editor's tabs), Lines, Breakpoints, Folding, Autocomplete |
| **View** | Origin axes, orientation triad, legends, bounding boxes, Projection, Model tree, Section view, Standard views, Frame all, Zoom to bounds, **Meshing algorithm** (the render options: dual contouring, iso-simplex, hybrid) |
| **Settings** | **Keyboard shortcuts…**, editor text bigger / smaller, Automatically reload changes, Rotation mode, Rotation sensitivity, Zoom center |
| **Help** | The guide, About, Open the welcome script, Shape reference |

Everything else is in the viewport: the model tree, the section card, the legends and the result card.

## The editor

A code editor for the Python script, with:

- syntax colouring; line numbers in a slim gutter; **folding** (the arrows in the gutter);
- **completion** while you type (`Ctrl+Space` forces it) and call tips for arguments. A function you pick
  from the list gets its brackets, with the cursor inside them (after them when it takes no arguments);
  typing `)` steps over the closing one;
- **go to definition** (`Ctrl`+click or `F12`), also into other files — see [Tabs](#tabs-the-script-and-the-files-it-uses) — and go to line (`Ctrl+G`);
- **find** (`Ctrl+F`), replace (`Ctrl+H`), with case, whole-word and regular-expression options;
- **multiple cursors** (`Alt`+click, or `Ctrl+D` for the next occurrence; `Esc` returns to one);
- line commands: toggle comment (`Ctrl+/`), duplicate (`Ctrl+Shift+D`), delete (`Ctrl+Shift+K`), move (`Alt+↑/↓`);
- **breakpoints**: click the left edge of the gutter or press `F9`. The script then runs up to the
  statement holding the breakpoint and stops; what was built so far is shown and a **Continue** button
  (`F8`) runs on to the next one. Breakpoints follow their line as you edit;
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
| **Right-click** empty space | **New primitive** and **Add operation**: hover either to see the library's functions (every primitive under *New primitive*; *thicken*, *shell*, *smooth*, moving, repeating and combining under *Add operation*). Choosing one writes the call into the script, with a line showing it, and selects the new model. A primitive is placed where the cursor is (the point of the ray under the cursor that is closest to the origin, as large as about a hundred pixels, and in the plane through the origin that faces you, so y = 0 in a front view; a place outside the render region is moved just inside it, or the primitive would not be drawn) and goes at the end of the script; an operation works on the selected model (or the last one) |
| **Right-click** a model | **Operation** (the same list, written with that model as its argument, under its definition; with **several models selected** and the one you right-click among them, the operations that combine models work on all of them, in the order you selected them: *union* unites all, *difference* takes every one after the first from the first, *intersection* keeps what they share, *exclude* locks the others as regions of the first (see [Excluded regions](fields.md#excluded-regions-exclude)); the others work on the model you right-clicked), **Delete** (the model, or all the selected ones) and **Select Surface**, which opens the menu of the **surface selection**: the angle, the mode, the thickness and the radius of a flood fill from the point you clicked; **Select** writes a `select_surface(...)` line into the script, under the model's definition. See [Selecting surfaces](selecting-surfaces.md) |
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

## The model tree

The card at the top left of the viewport: one row per import and per displayed shape; the parts of a STEP
file are listed under it. **Render settings** at the top lists the script's `view.set_*` statements.

The tree fills in **while the script runs**: when a statement takes a while (an import, a smoothing, an analysis), the
variables of the statements before it are in the tree already, and the finished script's tree replaces that one (the
viewport shows the shapes when the script is done). **Opening a file** (or a new script, or the tutorial) empties the
tree and the viewport at once, so what the old script made is never left standing while the new one runs, or when it
does not run.

| Element | Does |
|---|---|
| **Click a row** | Select the model in the viewport, scroll the editor to its code, frame the camera on it |
| **Ctrl+click** / **Shift+click** a row | Select several rows, as in a file list: Ctrl+click adds a row or takes it out, Shift+click selects every row from the one clicked before to this one. The selected models are lit up in the viewport; the editor and the camera stay where they are. The order you select them in matters for the operations that combine models (the first is what *difference* subtracts from) |
| **Eye** (key `V`) | Show or hide (hidden displays become `# hidden: …` lines). On an import it shows or hides every part; on a part it adds the part to the script, or toggles it |
| **Double-click a part** | Use that part in the import statement |
| **Gizmo button** (key `E`) | Sets when the shape's gizmo is shown, going round **click** (the default: while the shape is selected) → **never** → **always**. Dragging the shape's surfaces works whatever the mode, and the gizmo has priority where it is shown. On a displayed expression such as `sphere(3)` it first gives the expression a name (`sphere_1 = sphere(3)`). Several selected models are in the multi-select state: one shared gizmo moves them all, whatever mode each has when selected alone (the buttons keep showing each one's own mode). Locked models cannot be multi selected. See [Handles](handles.md) |
| **Lock button** (key `R`) | A switch of its own: a locked shape cannot be dragged at all, and keeps its gizmo mode. It writes (and deletes) the line `part = lock(part)` under the definition |
| **Keys `V`, `R`, `C`, `E`** | Show / hide, lock, render cache, gizmo mode. `V`, `R` and `C` act on all selected models by one rule: if they are not all in the *on* state (shown, locked, cached), they are all put there first; only when they all are, they are all turned off. `E` sends models that are all in one gizmo mode to the next, and models in different modes to click |
| **Cache button** (stack of disks, key `C`) | The **render cache** is on for every shape unless you turn it off: the button writes (or deletes) the line `part = render_cache(part, False)` under its definition. With it on (the default), the finished mesh is kept on disk and shown at once the next time the same shape is rendered; anything that changes about the math makes it mesh again. Blue: on; green: the mesh on screen was read from the cache; amber: the shape cannot be kept (hover). Off until you click it. See [Caching and performance](caching-and-performance.md#the-render-cache) |
| **⟳ Reimport** (on an import) | Read the file again (bumps `rev=` in the call). Imported files are also watched: one saved by another program is read again by itself |
| **⟳ Reimport** (on a part) | Back to what the file says: the part's handle edits are deleted and the file read again |
| **↺ Reset** (on an import) | Delete the import's cache and the handle edits of all its parts, and import afresh |
| **Bin** (every row) | **Delete** the row's object: its statement, the line that shows it, the line that hides it and its `handles()`, `expose()`, `lock()` and `render_cache()` lines are removed from the script (one undo restores them). On an import it deletes the parts' variables too; on a part row, the part's variable; on a render setting, its `view.set_…` line (back to the default). If the rest of the script still uses the name, FielDes asks first |
| **Right-click** | The same, plus *Go to code*, *Focus camera* and *Delete* |

Grey rows are hidden, or shown but outside the render region. An amber row is imported but worth a look
(an open mesh, a suspicious size): hover it.

## The section card

`Ctrl+Shift+X` (or **View → Section view**) opens a card at the top right of the viewport.

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
