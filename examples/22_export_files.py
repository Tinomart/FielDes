# Exporting: a part as a mesh in every format, an analysis as a finite-element model for other solvers, and its results.
#
# What to look at:
#   * the files, in the folder printed at the end (your temporary folder): the part as STL, OBJ, PLY, 3MF, glTF, FBX, OFF and DXF; the analysis as
#     Abaqus / CalculiX (.inp), Nastran (.bdf) and Gmsh (.msh) models; its results for ParaView (.vtu), as tables (.csv) and as text (.txt) -- the same
#     key numbers the result card shows (delete the folder to write them again);
#   * the model tree: **right-click a model, Export** writes one file at a time, with a dialog for where it goes; the arrow at the end of the
#     result's row opens the drawer of its fields and numbers (a click writes `von_mises_1 = result.von_mises`) and a button for the text file;
#   * the script is divided into sections (`# SECTION ...`): rows of the model tree with an arrow to close them, and a fold in the code editor.
#     `beam` is made in the first section and used in the second: it stays where it is made, and the analysis holds a reference to it.
#
# FBX is Autodesk's format, read and written here without Autodesk's SDK and for meshes only (docs/meshes.md says what that means).
import os
import tempfile
from fieldes import *

view.set_bounds((-5, -10, -10), (65, 30, 20))
view.set_resolution(2)
view.set_quality(8)

# SECTION The part
beam = difference(box((0, 0, 0), (60, 20, 10)), cylinder(3, 20, (30, 10, -5)))
beam

# SECTION The analysis
support = fixed(box((-1, -1, -1), (1, 21, 11)))
load = force(box((59, -1, -1), (61, 21, 11)), (0, 0, -300))
result = static_analysis(beam, supports=[support], loads=[load], material=steel, element_size=3)
result

# SECTION The files
out = os.path.join(tempfile.gettempdir(), "fieldes-exports")
region = ((-1, -1, -1), (61, 21, 11))
for ext in (".stl", ".obj", ".ply", ".3mf", ".glb", ".fbx", ".off", ".dxf"):
    if not os.path.exists(os.path.join(out, "beam" + ext)):
        export_mesh(beam, os.path.join(out, "beam" + ext), bounds=region, resolution=2)
for ext in (".inp", ".bdf", ".msh"):
    if not os.path.exists(os.path.join(out, "beam" + ext)):
        export_fea(result, os.path.join(out, "beam" + ext))
for ext in (".vtu", ".csv", ".txt"):
    if not os.path.exists(os.path.join(out, "beam_results" + ext)):
        export_results(result, os.path.join(out, "beam_results" + ext))
print("files written to", out)
print(open(os.path.join(out, "beam_results.txt"), encoding="utf-8").read())
