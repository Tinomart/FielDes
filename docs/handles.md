# Handles: editing by dragging

Every edit you make with the mouse is **written into the script**. There is no hidden state: drag a face,
and a number in your code changes; delete the line, and the part is what the file says again. Two ways of
editing a shape by dragging work **together**:

- **Handles**: hover a surface of the shape and drag it. This is **always there**, whatever else is set, for every
  shape that has numbers to pull (selecting a shape gives it its numbers when it has none yet).
- **The gizmo**: move arrows, rotation rings and scale squares at the shape, which move, turn and scale the whole of
  it. Where the two meet the **gizmo has priority** — which is why it can be turned off, in case it gets in the way.
  The **gizmo button** in the model tree row of every shape and part (and the key `E`) sets **when the gizmo is shown**:

| Gizmo mode | |
|---|---|
| **click** (the default) | the gizmo shows while the shape is selected — click it and it appears |
| **never** | the gizmo is never shown |
| **always** | the gizmo is shown on the shape whether it is selected or not |

`E` goes round **click → never → always → click**. A **lock button** beside the gizmo button is a separate switch: a
locked shape cannot be dragged at all, neither by the gizmo nor by its surfaces, and keeps its gizmo mode, which is
there again when you unlock it.

Selecting a shape makes it ready to be dragged: it gets the numbers its gizmo moves it by (a `handles(...)` line, in
the mode it is in) and, when it has none, the numbers that place its surfaces (an `expose(...)` line), if they are few.
A shape with many (an imported part) has **Make its surfaces draggable** in its right-click menu in the model tree.
A primitive made from the viewport's right-click menu is selected as it is made, so it is ready at once.

**Keys.** With the viewport (or the model tree) focused, `E` sets the gizmo mode of the selected models (round click,
never, always), `R` locks or unlocks them, `V` shows or hides them, `C` turns their render cache on or off, `I`
isolates them and `D` deletes them (**View → Edit mode**). Click a model first to select it; a click in the viewport
leaves the keyboard in the viewport, so the keys keep working, and a click on empty space deselects everything. They
can be rebound in **Settings → Keyboard shortcuts**.

**Several models selected.** Every key works on all of them, by one rule: if they are not all in the *on* state
of a toggle (shown, locked, cached), the key puts them all there first; only when they all are does it turn
them all off. For the gizmo mode, models that are all in one mode all go to the next; models in different modes all go
to click, where the round starts. A mixed selection is never changed model by model.

Two or more selected models are in the **multi-select state**. Whatever gizmo mode each has when it is selected alone,
all of them are moved by **one shared gizmo**, at the middle of them: its arrows and its centre dot move them all
together, and nothing is pulled by its surface. (Turning and scaling are one model at a time.) The shared gizmo is shown
unless every selected model is set to *never*. A model that has no numbers to move it by gets a gizmo line when it
joins the selection, in the mode it is in. `E` changes each model's own mode, which the tree's buttons show, for when it
is selected alone: the viewport shows the shared gizmo until the selection is one model, and then every model has its
own mode again.

**A locked model cannot be in a selection of several.** Adding one (Ctrl+click, a Shift range, a rectangle) leaves it
out, with a warning that locked shapes cannot be multi selected (with a *do not show this message again* box;
**Settings → Show hidden messages again** brings it back). A locked model on its own can be selected, which is how it is
unlocked. `R` on several models locks them, and they leave the selection.

**The dot in the middle of a gizmo** drags the model freely: it follows the cursor in the plane through the gizmo
that faces the camera (all the selected models together, for the shared gizmo).

- [`var()`: a number you can drag](#var-a-number-you-can-drag)
- [The gizmo](#the-gizmo)
- [Handles: drag a surface](#handles-drag-a-surface)
- [Primitives and displayed expressions](#primitives-and-displayed-expressions)
- [Lock](#lock)
- [Reimport and hot reload](#reimport-and-hot-reload)
- [Limits](#limits)

## `var()`: a number you can drag

```python
r = var(3)
ball = sphere(r)
```

Wrap a literal number in `var(...)` and hover the shape it affects: the surface highlights, and dragging it
rewrites the number in the script, live. FielDes works out which numbers place the surface under the cursor
(a radius, a plane's position, a box's size), so any shape built from `var()` numbers can be dragged
natively. `var()` needs a constant argument (`var(3)`, not `var(r + 1)`).

The numbers are ordinary Python values in an ordinary script: copy them, rename them, put them in a function.

## The gizmo

The part's own **move arrows, rotation rings and scale squares** (Shift: all three axes at once), at the
centre of the part's bounding box. Works on every shape and every imported part. It writes, under the
shape's definition:

```python
stand = handles(stand, move=(var(0), var(0), var(0)),
                rotate=(var(0), var(0), var(0)),
                scale=(var(1), var(1), var(1)))
```

and what you drag is written into those `var()` numbers. The shape is `stand` scaled about its centre,
rotated (x, then y, then z; degrees) and moved. `about=` sets another centre. `mode=` says when the gizmo is
shown: `'click'` (the default, left out), `'never'` or `'always'`; the placement stays whatever the mode.
(`mode='gizmo'` and `mode='handles'`, from before surfaces could always be dragged, are refused.) An imported part keeps its link to the STEP file through `handles()`, so `exclude()` (the exact
surface) follows it.

## Handles: drag a surface

**Hover a surface** of the shape and drag it, whatever the gizmo's mode: the numbers that place that surface change — a plane's
position, a cylinder's radius, the faces of a box. A shape made with `var()` numbers has them already.

For a shape written with **plain numbers** — a primitive, or a part imported from STEP, which is itself made
of such primitives — FielDes writes, under the definition,

```python
bracket = expose(bracket, [var(6), var(40), var(12.5), …])
```

where the list holds the numbers that place the part's surfaces, in order, as `var()`s with their current
values. Dragging a face then edits one of them. `expose()` is the same shape if you change nothing, to the
last float digit.

Rules:

- Only numbers that **place** a surface are exposed. Orientations and the placement of the whole part are
  not — a dragged face moves and neither turns nor takes the rest of the part along. Use the gizmo to move or
  rotate the whole part.
- A plane through the origin has no number in the shape's expression; FielDes gives it one (a 0), so even
  it can be dragged.
- If the part changes in the STEP file so that the number of exposed values differs, `expose()` raises an
  error telling you to **Reimport** the part.
- A part with more than 120 surface numbers is not exposed by being selected (that would be pages of script):
  its menu has **Make its surfaces draggable**. A part that is too large to expose at all (over 6 000 surface
  numbers — typical of parts full of free-form faces) is refused with a message. It can still be moved with the
  gizmo.
- An **excluded** shape (`exclude()`) cannot be dragged by its surfaces (that would move its locked part too); use
  its gizmo.

## Primitives and displayed expressions

The sphere a new file starts with is what the right-click menu's *New primitive* makes: named, with its numbers
exposed once it is selected, so its handles work and it is a model of its own in a selection of several:

```python
sphere_1 = sphere(1)
sphere_1 = expose(sphere_1, [
    var(0.0), var(0.0), var(0.0), var(1.0),
])
sphere_1
```

A bare expression such as `sphere(3)` has no variable to hang a line under. Selecting it (or pressing the gizmo
button) gives the expression a name — a free one, built from the function's name — `sphere_1 = sphere(3)` and a line
`sphere_1` under it; and, once the script has run again, the gizmo line (`sphere_1 = handles(sphere_1, ...)`) and its
`expose(...)` line under it. Then hover the sphere and drag its surface: the numbers in the list change. A list of shapes
(`[part for part, _ in model]`) and a model coloured by a field (`colored(...)`, an analysis result) have no button.

## Lock

```python
bracket = lock(bracket)
```

A locked shape cannot be dragged: neither the gizmo nor a surface. Use it when the viewport is for looking, so a
slip of the mouse cannot change the design. The lock button (or `R`) writes that line under the shape's
definition and deletes it again; it does not touch `handles()` or `expose()`, so the gizmo mode and the placement
numbers stay in the script and are there again when it is unlocked.
`handles(mode='lock')`, from before the lock was a switch of its own, is refused: use `lock()`.

## Reimport and hot reload

| | |
|---|---|
| **⟳ on an import** (model tree) | Reimport: read the STEP file again. The `rev=` of the `import_step_parts` call goes up, which is part of the cache key. |
| **⟳ on a part** | Back to what the file says: the part's `handles()` and `expose()` lines are deleted and the file is read again. |
| **Bin on an import** | Reset: delete the import's cache and the handle edits of all its parts; import afresh. |
| **Hot reload** | The imported files are watched. Save the STEP file from your CAD program and the script re-runs by itself; the status bar says "Reloading: part.step changed on disk". |

Nothing is stored anywhere but in the script (and the content-addressed import cache next to the STEP file).

## Limits

- Dragging works on what is **displayed** and meshed. A shape that is too coarse or outside the render region
  has nothing to grab — frame it and raise the resolution.
- Editing is disabled while you drag, so the script text and the drag never fight.
- Dragging needs the shape to have draggable numbers: `var()`s, or numbers exposed by `expose()`.
- Everything a drag writes can be undone with `Ctrl+Z`.
