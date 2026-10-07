# Troubleshooting

| Symptom | Likely cause and what to do |
|---|---|
| **FielDes closed by itself** | A crash leaves a report in `%LOCALAPPDATA%\FielDes\FielDes\crash` (a text file with the exception and the places the program was at): send it with the script that was open. `dev/tools/crash_symbols.py` turns it into function names with the build's map files |
| **The viewport is empty** | Only top-level *expression statements* that evaluate to shapes are drawn; an assignment alone is not (it only appears in the model tree — click its eye). Check the region (`view.set_bounds`) contains the shape, and that it is not hidden (`# hidden:` lines, the eye). A model outside the render region is greyed in the tree. |
| **Thin parts or a lattice are missing or ragged** | The resolution is too coarse for the thinnest feature. Raise `view.set_resolution` (a wall wants about three samples across it), or use `roi_resolution(model)` for imports, which names parts it cannot render properly. |
| **Meshing takes minutes** | The resolution is too fine for the region. Press `Esc` to cancel the render, lower `view.set_resolution`, or shrink `view.set_bounds` to the part you are looking at. |
| **A traceback in the output pane** | The script's own error; the offending line is underlined. What ran before the error is still shown. |
| **`FileNotFoundError` for a relative path** | Relative paths are relative to the script's folder. Save the script first (an unsaved script has no folder), or use an absolute path. |
| **A STEP part is red or grey on the model** | That is the B-spline fit deviation, not an error: see [Free-form faces](step-import.md#free-form-b-spline-faces). Use `exclude()` or `auto_exclude=True` where the exact surface matters. |
| **"this part could not be imported"** | The reconstruction could not resolve that solid; its `FailedPart` says why when used. The other parts of the file are fine. Its row in the model tree is marked. |
| **A part is 1000× too small or large** | Units: the STEP file's declared units are converted to `units='mm'`. For STL/OBJ/PLY pass `file_units='m'` (or `'in'`). See [Units](step-import.md#units). |
| **The import looks stale after editing the STEP file** | Imported files are watched and re-read when saved by another program. If it does not happen, press **⟳ Reimport** on the import, or the **bin** to reset it (deletes the cache). |
| **A strange result that goes away when the cache is deleted** | Delete `<file>.fieldes-cache.py` and `.fieldes-cache.trees/` next to the STEP file, or `clear_caches()` for the in-memory ones, and report it. |
| **`expose()` raises "…numbers to expose…"** | The part changed in the STEP file since the `expose()` line was written. Press **⟳ Reimport** on the part (it deletes its `handles()`/`expose()` lines). |
| **"Cannot make … draggable" (too many numbers to write into the script)** | Parts with thousands of free-form faces have too many surface numbers (more than 6 000) to write into a script. Use the gizmo, or edit the part in your CAD program. |
| **An analysis raises `FeaError`** | The message says why: no support touching the part, supports that do not hold it in place, a thermal problem with nothing fixing the temperature. Display the support and load regions with the part to check they touch it. |
| **The peak stress changes a lot when `element_size` is halved** | The mesh is too coarse, or there is a stress singularity (a sharp re-entrant corner, a point load). Refine, and load over a region. |
| **The program does not start ("missing DLL")** | Use the portable folder as a whole (the DLLs next to `FielDes.exe`), not the exe alone. The folder includes the MSVC runtime; if you still see the error, install the *Microsoft Visual C++ 2015–2022 Redistributable (x64)*. |
| **"FielDes cannot load its Python library"** | `FielDes.exe` looks for `runtime\python3` and `python\fieldes` around itself. Keep the folder structure intact (see [Building](building.md#the-portable-folder)). |

## Reading the timing

Start FielDes from a terminal with `FIELDES_TIMING=1` to see where render and analysis time goes on stderr
(the lines start with `[fieldes]` and `[bar-render]`).

## Reporting

Please report problems with the script, the output pane text and (for an import) the STEP file if you may
share it: [CONTRIBUTING](../CONTRIBUTING.md).
