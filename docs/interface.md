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
| **Left-drag** | Rotate (turntable; Z or Y up in **Settings → Rotation mode**) |
| **Right-drag** | Pan |
| **Wheel** | Zoom (about the cursor or the scene: **Settings → Zoom center**) |
| **Double-click** a model / the background | Frame that model / everything (`Home`) |
| **Click a model** | Select it: its row in the tree and its code in the editor |
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

| Element | Does |
|---|---|
| **Click a row** | Select the model in the viewport, scroll the editor to its code, frame the camera on it |
| **Eye** | Show or hide (hidden displays become `# hidden: …` lines). On an import it shows or hides every part; on a part it adds the part to the script, or toggles it |
| **Double-click a part** | Use that part in the import statement |
| **Handles button** (keys `M`, `G`, `H`, `L`) | Cycle the way the shape is edited by dragging: gizmo → handles → lock. A shape nobody has touched yet starts at the gizmo. On a displayed expression such as `sphere(3)` it first gives the expression a name (`sphere_1 = sphere(3)`). See [Handles](handles.md) |
| **⟳ Reimport** (on an import) | Read the file again (bumps `rev=` in the call). Imported files are also watched: one saved by another program is read again by itself |
| **⟳ Reimport** (on a part) | Back to what the file says: the part's handle edits are deleted and the file read again |
| **Bin** (on an import) | Reset: delete the import's cache and the handle edits of all its parts, and import afresh |
| **Right-click** | The same, plus *Go to code* and *Focus camera* |

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

Shown at the bottom right when a model is displayed with an analysis result (`result.show()`):

- the **field** drop-down (von Mises, displacement, each component, principal stresses, strain energy; or
  temperature and heat flux) above the colour bar — hover the part to read the value under the cursor;
- a short note under the bar says what the values are: **Nodal average** (the elements' values averaged at
  each node: peaks are lower than the elements' own), **Element values**, or **Solver values at nodes**;
  hover it for the explanation;
- the **deformation** slider (`1:1` = true size) — the part is drawn deformed;
- **Elements**: draw the tetrahedra (or hexahedra) the solver used, each with its own value (the
  stress in a tetrahedron is constant in it); hover one to read it. The line below says how many there
  are. The result line and the safety factor use the elements' peak;
- for a modal result, the **mode** shown.

## Status bar and progress

The status bar shows the render state on the left and the region, resolution and quality on the right.
A thin progress bar shows meshing. While a script runs, the output pane shows the statement and the
progress of the operation in it.

## Exporting

- **File → Export STL…** (`Ctrl+E`, `F7`): the shapes in the viewport as STL meshes (exact regions
  from `exclude()` are included).
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
