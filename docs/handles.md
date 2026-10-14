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

Selecting a shape makes it ready to be dragged: it gets the numbers its gizmo moves it by (a `gizmo(...)` call, in
the mode it is in) and, when it has none, the numbers that place its surfaces (a `handles(...)` call), if they are no more than
**120**: a bigger one (an imported part can have a thousand) would be pages of script. A model whose surfaces cannot be pulled
has an **orange dot** in the model tree, right after its name, and the dot's tooltip says why: it is a mesh (tessellated: no faces
to pull), or too many numbers place its faces. Its gizmo moves it all the same. Selecting such a model says it under the tree too.
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
together, and nothing is pulled by its surface. (Turning and scaling are one model at a time -- except for **points**: see below.) The shared gizmo is shown
unless every selected model is set to *never*. A model that has no numbers to move it by gets a `gizmo(...)` call when it
joins the selection, in the mode it is in. `E` changes each model's own mode, which the tree's buttons show, for when it
is selected alone: the viewport shows the shared gizmo until the selection is one model, and then every model has its
own mode again.

**Several points selected are scaled and turned one by one.** Select two or more points (Ctrl+click, a Shift range, a rectangle) and their shared gizmo has two rings round the middle of
them all besides the arrows or the dot: the **gold ring scales** every point by its own scale -- pull it out to make each one bigger, push it in to make each smaller; the factor is
shown beside it -- and the **ring in the plane turns** every point by its own rotation (the plane of their profile, or the work plane's while its view is open, else the plane facing the camera);
the angle is shown. **The points stay where they are**: the rings change the scale and the rotation numbers of each point's `gizmo(...)`, not the arrangement of the points -- a profile made of
them keeps its shape, and a sweep along a line gets thicker or twists at every selected point. Points of a profile stay in its plane.

**A locked model can be in a selection of several, but then the whole selection cannot be moved or edited.** Select
them as any models (Ctrl+click, a Shift range, a rectangle): a warning says that *when a locked object is part of a multi
select, the multi selection can no longer be moved or edited* (with a *do not show this message again* box;
**Settings → Show hidden messages again** brings it back), and a line under the model tree says how many are locked. There is
no shared gizmo and nothing is written for the selection until they are unlocked; it can still be combined, shown, hidden,
isolated or deleted. To unlock a handful of parts: select them, press `R` (or the lock button of any of the selected
rows): they are all unlocked, and still selected.

**The dot in the middle of a gizmo** drags the model freely: it follows the cursor in the plane through the gizmo
that faces the camera (all the selected models together, for the shared gizmo).

**A point is that dot, and its rotation and scale mean something.** The rotation of a point turns the direction a spline leaves it (and twists what `path_extrude` sweeps); its **scale** -- a point has one, always uniform, dragged with the one
gold ring round the dot -- is how strongly a spline leaves it and how thick a sweep is at it. **A point of a profile has no arrows**: its dot, pressed and dragged, moves it in the plane of the profile (several points of one profile selected
together have just the dot too), it has one rotation ring (in the plane) when it is a spline point, and the scale handle. See [Profiles](profiles.md).

**The gizmo of a point of a curve faces the way the curve does.** Its blue axis -- the arrow and the ring -- is the direction the curve (the path of a `path_extrude`) takes through the point, and the red and green are
across it. So the **blue ring turns what is swept about the curve** (the twist of a sweep at that point), the other two rings tilt the direction the curve leaves the point by, the blue arrow slides the point along the
curve and the red and green move it across. The numbers written are the same three rotation angles and three move numbers as ever: a turn about one of these axes is the sum of that turn and the one the point already had.
(The gizmo keeps the axes it was pressed with while you drag it.)

A `point(...)` has no surface, so it is drawn as the dot alone, **always** -- whatever the gizmo's mode, selected or not -- while it is shown
(its eye in the model tree: a point whose eye is off draws nothing). **Click the dot to select the point**; then its full gizmo shows (the arrows; `mode='never'`
keeps the dot only) and the dot, or an arrow, drags it. A locked point is a dot that cannot be moved. In an [app](apps.md) the same: `app.viewport({'Load point': p})`
shows the dot, with its name over it.

- [`var()`: a number you can drag](#var-a-number-you-can-drag)
- [The gizmo](#the-gizmo)
- [Copies: Shift+D and Ctrl+D](#copies-shiftd-and-ctrld)
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

The part's own **move arrows, rotation rings and scale squares**, at the centre of the part's bounding box. Works on every shape and every imported part.
**Hold Shift** and the gizmo changes: the three move arrows become three **flat squares** -- each one moves the part **along a plane** (the colour of a square is the axis that is the plane's normal: the blue
square moves it in x and y) -- and the three scale squares are replaced by **one gold ring round the middle**, the uniform scale handle of Blender's scale gizmo (Maya, Unity and Unreal have a cube in the middle instead; here the middle is the dot that moves):
drag it outward to grow the part, inward to shrink it, and **all three axes scale together**. A faint ring stays at the size you started from and the factor is shown while you drag. **The rotation rings turn in whole steps of 5 degrees** while Shift is held: the angle the part ends at is a multiple of 5 (a ring that starts from 3 degrees goes to 5, 10, 15 ... not to 8, 13 ...), and the angle shown beside the ring is the snapped one. (A point has this one handle always.) **Shift + the mouse wheel** scales what is selected without a gizmo
to aim at: every selected model by the scale numbers of its gizmo, all three together (a notch is 10 %), and the numbers are written when the wheel has rested a moment; a model that cannot be scaled leaves the wheel
to zoom. It writes, under the shape's definition:

```python
stand = gizmo(
    stand,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
    scale=(var(1), var(1), var(1)),
)
```

one argument per line, like the arguments of an operation, so each is easy to find and change (the `handles(...)` call is the exception: see below). What you drag is written into those
`var()` numbers. The shape is `stand` scaled about its centre, rotated (x, then y, then z; degrees) and moved. `about=` sets another
centre. `mode=` says when the gizmo is shown: `'click'` (the default, left out), `'never'` or `'always'`; the placement stays
whatever the mode. (`mode='gizmo'` and `mode='handles'`, from before surfaces could always be dragged, are refused.) An imported
part keeps its link to the STEP file through `gizmo()`, so `exclude()` (the exact surface) follows it.

## Copies: Shift+D and Ctrl+D

Two ways to duplicate a model, with the viewport or the model tree focused:

- **`Shift+D` copies**: the model's statements are written again under it with free names (`box_1` becomes `box_2`; copies of several models refer to each other's copies). The copy is a model of its own -- change either and the other stays.
- **`Ctrl+D` links**: under the model, a block of its own that **is** the model:

```python
plate_2 = instance(plate)  # shadow: plate
plate_2 = gizmo(
    plate_2,
    move=(var(0), var(0), var(0)),
    rotate=(var(0), var(0), var(0)),
)
```

  The copy's shape, sizes, colours and every other property are the original's, so **when the original changes the copy changes with it**. All it has of its own is where it is: it can be **moved and rotated** with its gizmo (which has no
  scale knob), and nothing else -- its surfaces cannot be dragged and `gizmo(copy, scale=...)` and `handles(copy, ...)` are refused, because they would change the original's shape. The original is only **referred to**, always (the mark `# shadow: plate`),
  whether it is locked or not: it keeps its own row in the model tree and the copy shows a shadow of it -- taking the original into the copy would move its row away and make it look like what the copy is made of. *Operation → Repeating → instance*
  writes the same block. A profile or a curve cannot be linked (it is made of its points: link those, or copy it with `Shift+D`); a number, a material or the result of an analysis has no shape to follow.

## Handles: drag a surface

**Hover a surface** of the shape and drag it, whatever the gizmo's mode: the numbers that place that surface change — a plane's
position, a cylinder's radius, the faces of a box. A shape made with `var()` numbers has them already.

For a shape written with **plain numbers** — a primitive, or a part imported from STEP, which is itself made
of such primitives — FielDes writes, under the definition,

```python
bracket = handles(bracket, [var(6), var(40), var(12.5), …])
```

where the list holds the numbers that place the part's surfaces, in order, as `var()`s with their current
values. **The call is one line, however many numbers there are** -- the one exception to the layout of the other calls (one argument to a
line): nobody edits those numbers by hand, they are dragged, so the call stays out of the way in the editor. Dragging a face edits one of them.
`handles()` is the same shape if you change nothing, to the last float digit.

Rules:

- **A shape made of a few numbers of its own lists those numbers**, one for each thing you can pull. A `rectangle` has four (its sides: `[x0, y0, x1, y1]`), a `rounded_rectangle` five (the four sides
  and the radius), a `triangle` three (its three sides, each pulled parallel to itself). A side pulled moves that side and no other, the corners of a rounded rectangle stay tangent to its sides whatever you pull, and
  **the gizmo stays at the middle of the shape** -- it is where the shape is after a side is pulled -- without anything jumping when the script runs again (the pivot is an expression of those numbers).
- Only numbers that **place** a surface are in the list. Orientations and the placement of the whole part are
  not — a dragged face moves and neither turns nor takes the rest of the part along. Use the gizmo to move or
  rotate the whole part.
- A plane through the origin has no number in the shape's expression; FielDes gives it one (a 0), so even
  it can be dragged.
- If the part changes in the STEP file so that the number of surface numbers differs, `handles()` raises an
  error telling you to **Reimport** the part.
- A part with more than 120 surface numbers does not get its `handles()` call by being selected (that would be pages of
  script): its menu has **Make its surfaces draggable**. A part that is too large to have handles at all (over 6 000
  surface numbers — typical of parts full of free-form faces) is refused with a message. It can still be moved with the
  gizmo.
- An **excluded** shape (`exclude()`) cannot be dragged by its surfaces (that would move its locked part too); use
  its gizmo.

## Primitives and displayed expressions

A new file is empty. The sphere that the right-click menu's *Primitives → New 3D shape* makes is named, and gets its surface
numbers in a `handles()` call once it is selected, so its handles work and it is a model of its own in a selection of several:

```python
sphere_1 = sphere(1)
sphere_1 = handles(sphere_1, [var(0.0), var(0.0), var(0.0), var(1.0)])
sphere_1
```

A bare expression such as `sphere(3)` has no variable to hang a line under. Selecting it (or pressing the gizmo
button) gives the expression a name — a free one, built from the function's name — `sphere_1 = sphere(3)` and a line
`sphere_1` under it; and, once the script has run again, the gizmo call (`sphere_1 = gizmo(sphere_1, ...)`) and its
`handles(...)` call under it. Then hover the sphere and drag its surface: the numbers in the list change. A list of shapes
(`[part for part, _ in model]`) and a model coloured by a field (`colored(...)`, an analysis result) have no button.

## Lock

```python
bracket = lock(bracket)
```

A locked shape cannot be dragged: neither the gizmo nor a surface. Use it when the viewport is for looking, so a
slip of the mouse cannot change the design. **Any model can be locked**, not only a shape: the result of an analysis,
a condition, a material, a point (`result = lock(result)`: every row of the model tree has the lock button).

**A locked model is read-only, and nothing moves it.** Not a drag, not a menu, not an operation made of it: its line stays
where it is, whatever is made of it. What is made of a locked model holds a **reference** to it (`# shadow: name`, a shadow),
written below its `lock(...)` line: a surface picked on it (right-click → *Select Surface*), a `union` or an analysis chosen from
its menu, a drop into another call -- the locked model stays at the top level and is not taken into the new call. An edit
that would have to move it (making room for what is made of it) is refused, and says why. The lock button (or `R`) writes that line under the shape's
definition and deletes it again; it does not touch `gizmo()` or `handles()`, so the gizmo mode and the placement
numbers stay in the script and are there again when it is unlocked.
`gizmo(mode='lock')`, from before the lock was a switch of its own, is refused: use `lock()`.

A locked model that is selected on its own says why nothing can be dragged, in a line under the model tree; unlocking it
makes it ready (it gets its `gizmo()` call and, up to 120 numbers, its `handles()` call). Imported parts are not locked when
they come in: lock the ones that should stay where the file puts them.

## Reimport and hot reload

| | |
|---|---|
| **⟳ on an import** (model tree) | Reimport: read the STEP file again. The `rev=` of the `import_model` call goes up, which is part of the cache key. |
| **⟳ on a part** | Back to what the file says: the part's `gizmo()` and `handles()` calls are deleted and the file is read again. |
| **Bin on an import** | Reset: delete the import's cache and the handle edits of all its parts; import afresh. |
| **Hot reload** | The imported files are watched. Save the STEP file from your CAD program and the script re-runs by itself; the status bar says "Reloading: part.step changed on disk". |

Nothing is stored anywhere but in the script (and the content-addressed import cache next to the STEP file).

## Limits

- Dragging works on what is **displayed** and meshed. A shape that is too coarse or outside the render region
  has nothing to grab — frame it and raise the resolution.
- Editing is disabled while you drag, so the script text and the drag never fight.
- Dragging needs the shape to have draggable numbers: `var()`s, or numbers placed in a `handles()` call.
- Everything a drag writes can be undone with `Ctrl+Z`.
