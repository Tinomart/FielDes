# Custom blocks

Put a `.py` file in this folder and every function in it becomes a **block**: a function that is there in every
script you open in FielDes, like `sphere()` or `lattice()`, with its call tip, completion and menu entry.

```python
# blocks/my_blocks.py
def perforate(body, hole_radius=2.0, spacing=10.0):
    ''' Drills a grid of round holes right through a body, along z '''
    holes = repeat(cylinder_z(hole_radius, 100000, (0, 0, -50000)), (spacing, spacing, 0))
    return difference(body, holes)
```

```python
# any script
plate = box_exact((0, 0, 0), (60, 40, 6))
drilled = perforate(plate, 2.5, 10)
drilled
```

- **No imports and no registration.** A block is written with the whole library at hand, and may use the blocks of the files
  before it (files are read in alphabetical order). A name that starts with an underscore is private to its file.
- **Save and it is there.** Scripts that use a block run again when its file is saved; the editor completes it, shows its
  call tip (the first line of its docstring) and Ctrl+click goes to its definition.
- **Any kind of result.** A block returns a shape, a field, a point, a surface, a lattice cell, a material -- anything the
  library makes -- and the model tree shows it with that kind's icon and a small block mark.
- **Menus.** A block whose first argument is a model, with defaults for the others, is in the right-click menu under
  *Add operation > Custom blocks*; a block that needs no argument is under *New custom block*.
- **Fields work.** Every number in a block can be a field: `perforate(plate, hole_radius=ramp(x_field(), (0, 60), (1, 3)))`.
- **The folder** is this one by default (next to FielDes); *Settings > Blocks folder...* chooses another, for instance one
  you keep in a shared place. A file that cannot be read is named in the script's output with the reason.

`sample_blocks.py` has four to start from: `perforate`, `rounded`, `light_core`, `bracket`.
