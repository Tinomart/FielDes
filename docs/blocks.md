# Custom blocks

A **block** is a function of your own that is there in every script, like `sphere()` or `lattice()`. You write it once, in
a few lines, in a Python file of the **blocks folder**; from then on any script you open in FielDes can call it — with its
call tip, completion, a place in the right-click menus and a mark in the model tree.

```python
# blocks/my_blocks.py
def perforate(body, hole_radius=2.0, spacing=10.0):
    ''' Drills a grid of round holes right through a body, along z '''
    holes = repeat(cylinder_z(hole_radius, 100000, (0, 0, -50000)), (spacing, spacing, 0))
    return difference(body, holes)
```

```python
# any script, anywhere
plate = box_exact((0, 0, 0), (60, 40, 6))
drilled = perforate(plate, 2.5, 10)
drilled
```

That is all there is to it: no import, no registration, no manifest. (`examples/20_custom_blocks.py` uses the four blocks of
`blocks/sample_blocks.py`.)

- [The folder](#the-folder)
- [What a block is](#what-a-block-is)
- [Using blocks](#using-blocks)
- [Fields in blocks](#fields-in-blocks)
- [Errors](#errors)
- [Blocks and the library](#blocks-and-the-library)

## The folder

The blocks folder is `blocks` next to FielDes (the repository's `blocks/` in a checkout, `blocks` in the portable folder). It
comes with `sample_blocks.py` (four blocks to start from) and a `README.md`. **Settings → Blocks folder…** chooses another
folder — one you keep in a shared place, say — and **Settings → Show the blocks folder** opens it in the file manager. The
choice is remembered; the environment variable `FIELDES_BLOCKS` sets it for runs outside the application
(`scripts/run_example.py` reads the folder too).

The folder is **watched**: save a block file and every script that uses one of its blocks runs again, with the new version —
nothing to restart, nothing to reload by hand. A script that does not use a block is left alone.

## What a block is

- **Every public function** in a `.py` file of the folder is a block, named as the function is. A name that starts with an
  underscore (`_helper`) is private to its file; so is a file whose name starts with one (`_notes.py`).
- A block is written with **the whole library at hand**: `sphere`, `lattice`, `ramp`, `Shape`, `Material` ... without
  importing them. It may use the blocks of the files **before it** (files are read in alphabetical order), and plain Python
  — loops, classes, helper functions, `math`.
- The **first line of the docstring** is what the call tip, the completion list and the menus say about the block.
- A block may return **anything the library makes**: a shape, a field, a point, a surface, a lattice cell, a material, an
  analysis result. The model tree shows it with the icon of what it made, plus a small **f** badge for the block.
- A name that is a function of the library is refused (`union` cannot be redefined); a name used by two files is refused
  for the second one. Both are said in the output.

## Using blocks

| Where | What |
|---|---|
| **The script** | call a block like any function; **Ctrl+click** (or `F12`) its name opens the file at its definition, in a tab |
| **The editor** | completion and call tips (`Ctrl+Space`); the tip is the signature and the first line of the docstring |
| **Right-click empty space → Add operation → Custom blocks** | blocks whose **first argument is a model** and that have defaults for all the others: the call is written for the selected model |
| **Right-click empty space → New custom block** | blocks that need **no** argument: the call is written, and the model selected, ready to drag |
| **The model tree** | a model a block made has the block's **f** badge; its tooltip names the block |

A block's own arguments are written with their defaults, so the menu entry runs at once; change them in the script.

## Fields in blocks

Every number of a block can be a field, like every number of the library — a block passes its arguments on to library
functions, and they take fields (see [Fields everywhere](fields.md#fields-everywhere)):

```python
drilled = perforate(plate, hole_radius=ramp(x_field(), (0, 60), (1.0, 3.5)), spacing=10)    # bigger holes towards +x
```

A block whose own code *computes* with a number (`if hole_radius > 3: ...`, `math.sqrt(hole_radius)`) needs a number: say so in
its docstring. The library's `is_field(x)` tells a field from a number when a block has to treat them differently.

## Errors

A block file that cannot be read — a syntax error, an exception while it runs, a name clash — is named in the script's
**output** with the reason (`custom block file my_blocks.py: SyntaxError: ... (line 7)`), and the other files still work. An
error inside a block when it is *called* is a normal traceback of the script, with the block's file and line in it.

## Blocks and the library

Blocks are functions of the same kind as the library's: `blocks.py` in `python/fieldes` loads them (`fieldes.blocks`), the
script runner puts them into the namespace every script runs in, the editor's completion tables list them, and
`menu_catalog.py` offers them to the menus. Nothing is installed anywhere, and a block file is ordinary Python you can also
run, test and keep in version control like any other.
