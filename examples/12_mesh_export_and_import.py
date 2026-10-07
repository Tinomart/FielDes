# Triangle meshes: export a part as STL, read it back in.
#
# import_model() turns an STL / OBJ / PLY / 3MF / glTF file into a field: the exact signed distance to
# its triangles (the mesh is cleaned first, holes are closed smoothly).  So a mesh from anywhere
# is a shape like any other: cut it, fill it with a lattice, analyse it.  Here the imported stand
# is exported with save_stl and read back.
#
import os
from fieldes import *

parts = import_model("step/MobileStand.step")
stand, (lo, hi) = parts[0]

view.set_bounds(*roi(parts))
view.set_resolution(4)
view.set_quality(8)

os.makedirs("meshes", exist_ok=True)
path = "meshes/mobile_stand.stl"
if not os.path.exists(path):
    stand.save_stl(path, lo, hi, resolution=4)       # STL carries no units: millimetres

mesh, (mlo, mhi) = import_model(path, file_units="mm")[0]
mesh
print("mesh size (mm):", [round(h - l, 1) for l, h in zip(mlo, mhi)])