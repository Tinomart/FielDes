# The next update (night of 2026-10-06): fields everywhere, custom blocks, a typed and nested model tree

Written while you slept. **Nothing is committed or pushed**; everything is in the working tree for you to review
(`git status` / `git diff` in the repo, or just run `dist\FielDes\FielDes.exe`, which is deployed with everything below).

## Round 5 (2026-10-07): the same menu everywhere, simulations, fields you can see

| You asked for | It is | Try it |
|---|---|---|
| The tutorial should show the **code** that changes beside the viewport | The lines a step is about stay lit and framed in the editor while the step is about the viewport or the tree; every line that changes or moves (typed, dragged, dropped, renamed) glows for a moment, found by a line diff (lines that only shift down do not glow); the editor scrolls to it | **Help -> Guided tour**, step *Drag a surface*: watch the plate line |
| A field is 3D: selecting it opens the **section viewer** on it | A field is not drawn any more; selecting it in the tree opens the section card, painting a plane through the render region with the field's value at every point (hover reads it, move the plane through the field); the card closes when the selection leaves a field, if the selection opened it | Select swell in the tour, or any field |
| A point could not be passed to `distance_to_point` in the tree | A point dropped on an operation takes the place of a position written as a tuple (not a size, direction or scale); 2D centres become `(p.x, p.y)`. A point is accepted wherever a position goes. I also checked every function for what a drop does: a field no longer takes a **count** (`array_x(b, 3, 12)` -> the 12), with the reason when there is nothing to take | Drag `anchor` onto a `distance_to_point((..))` row |
| Field arithmetic, field * field too | It worked for `+ - * / **` between fields already (a number is a field); `2 ** field` was missing. Now also as functions that always make a field (`multiply_fields` ...), in **Operation -> Field math** | `multiply_fields(a, b)` or `a * b` |
| `field_from_body` | The values of a body as a field: not a body, not drawn, may be multiplied. A body itself keeps true scale -- I did **not** forbid `body * 2` (the library multiplies bodies internally); it is documented instead | `field_from_body(plate) * 2` |
| The right-click menu in the **text editor** and the **model tree** (and the tree's own menu removed) | Right-clicking a line that defines a model, or its row, opens the viewport's menu for it; on a line with no model, the menu of empty space, and what you make goes under that line (to the end of the script where the line is indented or inside a call). Selected text keeps the text menu. *Select Surface* is greyed outside the viewport (it needs the place you click on the surface) | Right-click a line of the script |
| **Simulations** in the menu | **Simulation** in a model's menu and **Add simulation** in the empty-space menu: static, modal, topology optimization, thermal, flow, written with the model as the part and a first set of supports/loads made of boxes on its ends | Right-click a model -> Simulation |


## Round 6 (2026-10-07): separate viewers, typing writes the code, the tour puts things back

| You asked for | It is | Try it |
|---|---|---|
| The section viewer is **not disabled** while the field viewer is active (and in the tour) | Two planes in the viewport, two cards: the section view is the user's (`Ctrl+Shift+X`) and cuts models; the field viewer opens when a field is selected and shows it on a disc. Both at once; the field card stacks below the section card. New tour step **Cut a section** | Switch on the section view, then select a field |
| **Rename** and **render settings** write the code in real time | Every key types into the script (rename: the variable is renamed with the name so far; settings: the `view.set_*` call is rewritten). One number / one name = one undo step. A name that cannot be yet waits; Esc / an invalid final name gives the old one back | Double-click a name and type |
| "Show me" **shows once and resets** | The script, selection and section view are put back after the demonstration; the step is not marked done | **Help -> Guided tour**, any step, **Show me** |
| **Back** puts the state back; each step has its own state | The state of a step (script, selection, section) is saved when the step is first opened and put back whenever it is opened again | Do *Change a number*, go on, press **Back** |
| The last tour step should **show the dropdown of the example files** | It draws the File menu beside the card with **Open example file** framed and an arrow to it, like the blocks step | **Help -> Guided tour**, the last step |
| No **delete button** on the region | The two region rows have no bin (resolution and quality keep theirs) | **Render settings** |
| **Low-level fields** in Python, as libfive's Scheme `define-shape` / `remap-shape` | Yes: `Shape.X()/Y()/Z()`, the operators and `.min .max .sin .cos ...`, `.remap(x', y', z')`. `examples/21_low_level.py` (cube, twist, ball + torus with a smooth minimum, gyroid) and a section in `docs/scripting.md` | **File -> Open example file -> 21** |

## Round 2 (your feedback of the morning): the guided tour, the tree, Linux

| You asked for | It is | Try it |
|---|---|---|
| A tutorial that **guides the user**, not a script | A **guided tour** on the live program: the first time FielDes starts a card offers it (**Help → Guided tour** later). 14 steps of one or two short sentences; each dims the window except what it is about, glows round it, points at it with an arrow and puts the card beside it, and lets you do only that (the rest of the window and the keyboard are held back). **Show me** does the step with a pretend cursor, through the same mouse events a hand makes. **Skip step**, **Back**, **Skip the tour** everywhere. The old welcome script and *Help → Tutorial* are gone; the tour loads a small model of its own. | `Help → Guided tour`; in tests `tour start|goto N|next|showme|skip|state` (`dev/automation/auto_tour*.txt`) |
| Drag and drop of **nested** rows, **instant** | The drag is done by the tree itself from the mouse events (no Qt drag and drop: it is the same code for a hand and for a test), nested rows included (`win_nested.py`). The tree updates **before** the script has run (the new rows and the new script text come from the edit); the script then runs at once (no 250 ms wait), and nothing is written into the script while the button is down (that was what made a drop wait). | drag `x3` onto `more` in `dev/tests/win_nested.py` |
| A **Linux build** (WSL) | `scripts/build-linux.sh [--package]` → `~/FielDes-linux/run.sh` and `dist/FielDes-linux-x64.tar.gz` (Fedora and Ubuntu package lists inside; Clipper2 is built once). | see below for what was tried |
| **Render settings editable** in the tree | Open *Render settings*: region, resolution and quality are number fields; Enter / leaving writes the `view.set_*` statement (adds one when the script has none); Esc reverts; nonsense is refused with a note. | the tour's step 12 |
| **Rename** by double-clicking a name | Double-click (or F2): every use changes, scope-aware (strings, keyword arguments, attributes, a function's own variables are left alone; keywords, used names and library functions are refused). | `dev/tests/t_rename.py`, `win_rename.py` |

| The tree **is** the structure of the calls: nesting, renesting, denesting always change arguments | Dropping on an operation adds/replaces an argument; dragging a nested model out of its call (to top level or onto another operation) takes it out of that call's arguments; dropping between the children of an operation puts it at that place among the arguments (children are listed in argument order). | `dev/automation/auto_nesting.txt` |
| A semi-transparent **shadow** of a model under every other statement that uses it | A model has its real row under the first user; each other user has a faded shadow row = its own reference. Dragging a shadow moves only that reference (the main variable stays); it disappears when denested. Shadows have the delete button and work with `D`. | `auto_shadowdel.txt` |
| **Fixed inputs** can be denested, with a warning, default = delete the operation | `offset(plate, 1.0)` without `plate` cannot work: the question (default **Delete**, with *Do not show this message again*; Settings -> Show hidden messages again) says what goes; what is made of the deleted operation loses it in turn; one undo brings it all back. Operations that take any number of models (`union`, ...) just lose the argument. | drag `a@t` onto `v` in `win_nesting.py` |
| **Ctrl+drag** creates a shadow reference | Adds the model as an argument at the dropped spot and leaves it where it was (green frame, "+" label); refused at top level and for a model that is an input already. | `treedrop d > u > on ctrl` |
| Ctrl+drag must **not move** the model: the new shadow goes at the drop spot | The tree is rebuilt from the script and a model's row goes under the *first* statement that uses it, so a copy into an earlier statement used to take the row away from the old place. Position decides, as you said: the real variable is the first use after its definition. A copy into a statement **below** the original just adds a shadow there. A copy into a statement **above** it cannot be a shadow (it would become the original), so a message explains why and offers the fix, which is its default: the drop spot becomes the original place (the definition goes directly above it) and a reference stays at the old one; *Do not show again* does it without asking. The same message covers a statement that holds shadows being dragged above the original (or the original below it): the tree the edit would give is worked out before anything is written, and every shadow that would come first is listed in ONE message. No `ref()` function: names are plain. A regular drag still moves (the row follows the script). | `auto_nesting.txt`, `dev/tests/t_arg_edits.py` |
| **No menu** on a drop | A drop on an operation is added (`union`...) or takes the place of the only model it works on; a field takes the first plain number; the rest is refused with a red note at the mouse that says why. | any drop |
| Rename box: the old name showed through | The editor is opaque now. | double-click a name |
| Tour: last step **Custom function from block**; a crash | The menu is drawn by the tour as a picture with the card beside it (a real popup hid the card and took clicks). The crash (Windows event log, 11:53 pm): the *Show me* of the render-settings step kept a pointer to a field that the tree had deleted when it was built again; every demo now looks its widget up each time. | `Help -> Guided tour` |
| The **gizmo** shows **at once** when a model is selected | It used to wait for the script to run with the `handles(...)` line the selection writes. It is drawn immediately now from the model's box centre (the same pivot the real one gets), a little transparent; the real one replaces it; a press on it is kept and the drag starts when the real one is ready. | select any model |
| **`#SECTION`** comments fold | `#SECTION Title` (or `# SECTION`): an arrow in the gutter folds up to the next one (a box says "... N lines"); blue bold header; typing `#sec` completes to `#SECTION `; an error inside a folded section opens it. | `auto_sections.txt` |

Also fixed on the way: selecting a model whose extents the tree only estimates (one with a gizmo in it) zoomed the camera out 20×
(`View::focusOn` now frames the drawn mesh); dropping a model on an operation that has it as an input no longer offers to add it
again; a name being typed survives the rebuild of the tree that a selection causes.

## What you asked for, and where it is

| You asked for | It is | Try it |
|---|---|---|
| Anything that was a plain value takes a **field** | Every size / radius / thickness / spacing / blend of the library (98 numeric slots checked: a constant field gives exactly the number's result), graded lattice **cell size**, and in the analyses: `Material(E / density / conductivity / expansion = field)`, `force(..., profile=field)`, `fixed_temperature(region, field)`, `convection` with fields, heat profiles. | examples **18** and **19** (no STEP file needed); `docs/fields.md#fields-everywhere`, `docs/analysis.md#fields-in-analyses` |
| **Custom blocks**, simple, in house | A `.py` file in the blocks folder: every public function is in every script (no import), with call tips, completion, go-to-definition, menu entries, hot reload (a script that uses a block runs again when the file is saved). Settings → **Blocks folder…** / **Show the blocks folder**. | `blocks/sample_blocks.py`, example **20**, `docs/blocks.md` |
| **Types** clearly defined, coloured icons | 3D shape (blue cube), 2D shape (teal square), field (green disc), surface (purple sheet), point (amber cross), simulation (red bars), conditions (orange load arrow), lattice cell (gold hexagon), selection (pink), import (teal box); an **f** badge for what a block made. In the tree and in the menus. | `docs/interface.md#the-model-tree` |
| Make **points, surfaces, 2D and 3D shapes** from the context menu | Right-click empty space: *New 3D shape / 2D shape / point / surface / field / custom block* + *Add operation*. A point is a model (a ball, with a gizmo) that goes wherever a coordinate goes; a surface is drawn as a thin sheet; a 2D shape is drawn flat. | right-click in the viewport |
| **Nesting and drag and drop** in the tree; operations nest their variables | **Drag a field onto an operation and it replaces a number** (`offset(plate, 1.0)` -> `offset(plate, swell)`), the way "anywhere a number goes, a field goes" is done with the mouse. The models an operation is made of are its children (under the first statement that uses them; the others show a dim *uses x* row). Drag a row onto an operation = an input of it (replace one / add to `union`...); between rows = move it in the script, with what it is made of. Each is one undoable edit. | drag in the tree |
| A **tutorial**, a better **README**, **images** | `docs/tutorial.md`, **Help → Tutorial** (a new welcome script), the in-app guide has the new features, README rewritten, new screenshots (`docs/images`). | |

## Decisions I made without you (all easy to change)

- **A field is a Shape.** No new type: a number-or-field is whatever a library function already did with tree arithmetic. Where
  a function *computed* with the number (counts, tolerances), it refuses a field with a message instead of failing obscurely.
- **Graded lattice cell size** is made by blending lattices a factor of two apart (hat weights in log2), up to a factor of 16
  over the part. It is a field like any other (no meshing inside).
- **Analyses: tetrahedral elements only** for fields (the voxel elements and the **flow solver** refuse them, and say so). The
  kernel reads each field at the centre of every tetrahedron (stiffness, density, conductivity, expansion), at the nodes
  (held temperature), at the boundary triangles (convection h and ambient, load / heat profiles). I checked: constant fields
  = numbers; a stiffness falling to a tenth = beam theory (x1.42 against x1.39); conductivity falling to a tenth = Fourier
  (x2.559 against x2.558); a load spread in proportion to x = 0.55 of the tip load (theory 0.55), evenly = 0.38 (0.375).
  A load's *total* stays the vector you give; the profile only says how it is shared.
- **Menu names:** *New primitive* is gone; the empty-space menu has one entry per kind (a kind with one entry, *point*, is that
  entry). The old automation scripts under `dev/automation` were updated to the new names.
- **Drag and drop semantics:** on an operation = input (the menu offers *Replace x with y* for each input, *Add y as another
  input* for `union`-like functions; with exactly one way it just happens); between rows = move the statements (definition,
  the lines that show / hide / lock / edit it, the comments right above it). It refuses, with the reason in a note under the
  tree, when something would be used before it is defined, when the model is changed again further down (`x = f(x)`), or when
  it would make a cycle. A drop that arrives while the script is still running its last edit waits for the new scene.
- **Two real bugs found while testing, fixed:** (1) a 2D shape lost its kind through `handles()`/`expose()`; (2) selecting a
  model made the tree write gizmo/expose lines even for models nobody can see, and a gizmo line on an upstream model changed
  the number count of a downstream model's `expose()` line (5 -> 22 numbers), which then raised an error. Now only shown
  models are prepared, and a downstream `expose()` line that the new numbers make stale is removed (also when a drop changes a
  model's inputs).
- **The blocks folder** is `blocks/` next to FielDes (repo root in dev, `dist\FielDes\blocks` in the portable folder, copied by
  `dev/deploy.ps1` and `scripts/build-windows.ps1 -Package`). Your choice in Settings is stored as `blocks-folder`.

## Not done / limits (said plainly)

- The **flow solver** (`fluid_analysis`, `flow_topology_optimization`) takes numbers only: an inlet profile or a viscosity field
  would be a change in `tetflow.cpp` (2600 lines) that I did not make. It refuses a field with a clear message.
- Real **mouse** drags in the tree could not be driven by my test harness: it sends the same drag-and-drop events a drag
  produces (`treedrop` in the automation) into the real tree widget, and the Qt drag start itself is Qt's. Worth a quick try by hand.
- A **cell size field** changes by at most a factor of 16; a bigger range says so.
- The model tree's nesting is by *first use*; a model used by two operations is nested under the first, the second shows it dim.
- **Only one sample STEP part is shipped** (round 5): `examples/step/PivotBearingSupportBracket.STEP`, in the repository (`.gitignore`
  un-ignores that one name), in `dist` (`dev/deploy.ps1`), in the Windows package (`scripts/build-windows.ps1`) and in the
  Linux package (the rsync rules of `scripts/build-linux.sh`, checked with a dry run only: the Linux package was **not**
  rebuilt, it still has the 01:42 build, and will be rebuilt when you allow the push). Its GrabCAD origin and unknown terms
  are stated in `NOTICE.md` and `examples/step/README.md`. Examples 02, 03, 04, 06, 07, 09, 11, 12, 13 still need files of their own.
- I did not **commit or push** anything (standing rule). To look at the whole change: `git status`.

## How I verified it (everything is in `dev/`)

- Python: `dev/tests/t_field_slots.py` (98 slots), `t_kinds.py`, `t_blocks.py`, `t_menu_catalog.py`, `t_solver_fields.py`
  (constants vs numbers, beam theory, Fourier, topology optimization, modal, thermal, refusals), `t_readme_snippets.py` (every code
  snippet of the README / tutorial / docs runs), `t_excluded*.py` (every function of the library is classified for excluded shapes),
  `t_smooth.py`, `t_cell_custom.py`, `t_scene_tree.py` (types, owners, inputs, numbers the tree is told). Run with
  `dev/run_regress.sh t_x.py ...`. Last full run: all passed (`dev/logs/regress2.txt`).
- The real window (automation + screenshots, `dev/automation/auto_*.txt`, grabs in `dev/grabs`): the tree with typed icons and
  nesting (`win_tree.py`), drops on operations / between rows / at the end / refused (`auto_tree.txt`), the menus and the blocks folder
  chosen at run time and the **file watcher** re-running the script when a block file changes (`auto_blocks.txt`), a selection chain
  that used to break the script (`auto_chain.txt`), a field dropped on `offset(plate, 1.0)` becoming `offset(plate, swell)` with
  `swell` moved above it (`auto_fielddrop.txt`). `dev/automation/run_final.ps1` runs all four window tests in a row; the
  screenshots in `docs/images` come from `auto_shot_overview.txt` and `auto_shot_analysis.txt`.
