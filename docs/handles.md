# Handles: editing by dragging

Every edit you make with the mouse is **written into the script**. There is no hidden state: drag a face,
and a number in your code changes; delete the line, and the part is what the file says again. Three ways of
editing a shape by dragging exist, and the **handles button** in the model tree row of every shape and part
cycles through them — `gizmo → handles → lock` — so they never get in each other's way.

A shape nobody has touched yet is a *not draggable yet* lock; the first click on its button gives it the
**gizmo**. The handles step is skipped for a shape that has no numbers to drag.

**Keys.** With the viewport (or the model tree) focused, `M` goes to the next mode of the selected model, and
`G`, `H` and `L` pick the gizmo, the handles and the lock directly (**View → Edit mode**). Click a model first to
select it; they can be rebound in **Settings → Keyboard shortcuts**.

- [`var()`: a number you can drag](#var-a-number-you-can-drag)
- [Gizmo mode](#gizmo-mode)
- [Handles mode: drag a surface](#handles-mode-drag-a-surface)
- [Primitives and displayed expressions](#primitives-and-displayed-expressions)
- [Lock mode](#lock-mode)
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

## Gizmo mode

The part's own **move arrows, rotation rings and scale squares** (Shift: all three axes at once), at the
centre of the part's bounding box. Works on every shape and every imported part. It writes, under the
shape's definition:

```python
stand = handles(stand, move=(var(0), var(0), var(0)),
                rotate=(var(0), var(0), var(0)),
                scale=(var(1), var(1), var(1)), mode='gizmo')
```

and what you drag is written into those `var()` numbers. The shape is `stand` scaled about its centre,
rotated (x, then y, then z; degrees) and moved. `about=` sets another centre. The placement stays in every
mode. An imported part keeps its link to the STEP file through `handles()`, so `exclude()` (the exact
surface) follows it.

## Handles mode: drag a surface

**Hover a surface** of the shape and drag it: the numbers that place that surface change — a plane's
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
- A part that is too large to expose (over 6 000 surface numbers — typical of parts full of free-form
  faces) is refused with a message. It can still be moved with the gizmo.

## Primitives and displayed expressions

A bare expression such as the default script's `sphere(1)` has no variable to hang a line under. Its row in
the model tree has the handles button all the same: the first click gives the expression a name — a free
one, built from the function's name —

```python
sphere_1 = sphere(1)
sphere_1
```

and, once the script has run again, writes the gizmo line (`sphere_1 = handles(sphere_1, ...)`) under it. A
click or two more (or `H`) gives it its `expose(...)` line; then hover the sphere and drag its surface: the
numbers in the list change. A list of shapes (`[part for part, _ in
model]`) and a model coloured by a field (`colored(...)`, `result.show(...)`) have no button.

## Lock mode

Nothing is draggable: neither the gizmo nor a surface. Use it when the viewport is for looking, so a slip
of the mouse cannot change the design. The placement numbers stay in the script.

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
