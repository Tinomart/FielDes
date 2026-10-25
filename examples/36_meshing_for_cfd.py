# A mesh of the air in a channel round a post, for the solver of another program: Fluent, OpenFOAM, Gmsh ...  Docs: docs/meshing.md.
#
# What to look at: the result card of `mesh` -- press "Elements" and the cells are drawn, coloured by their size (the card's menu changes it to the skewness, the aspect ratio, the
# orthogonal quality): small round the post, large away from it, and a few prism layers on the post itself.  The output lists the cells, the zones and the quality of the mesh;
# the files are in the folder it names.  (About half a minute.)
import os
import tempfile

from fieldes import *

view.set_bounds([-5, -20, -5], [65, 20, 15], auto_scale=True)
view.set_resolution(4)
view.set_quality(8)

# The air: a channel 60 mm long, 30 wide, 10 high, less a post 8 mm across that stands in it (and is taller than the channel, so that its ends are not on the floor and the ceiling).
channel = box((0, -15, 0), (60, 15, 10))
post = cylinder(4, 14, (20, 0, -2))
air = difference(channel, post)

mesh = mesh_volume(
    air,
    # 0.8 mm at the post, growing by 0.25 mm for every mm from it, up to 3 mm
    element_size=graded_size(post, wall_size=0.8, growth_rate=1.25, max_size=3.0),
    # three layers of prisms on the post, the first 0.15 mm thick, each 1.3 times as thick as the one before
    boundary_layers=inflation_layers(post, first_height=0.15, growth_rate=1.3, layers=3),
    # the two ends are the inlet and the outlet; everything else is wall, split where the surface turns by more than 30 degrees
    zones={'inlet': box((-1, -16, -1), (0.01, 16, 11)), 'outlet': box((59.99, -16, -1), (61, 16, 11))},
    feature_angle=30,
)
print(mesh)
print(mesh.quality().report())

out = os.path.join(tempfile.gettempdir(), 'fieldes-meshes')
mesh.export(os.path.join(out, 'channel.msh'))                                                        # Gmsh
mesh.export(os.path.join(out, 'channel.fluent.msh'), format='fluent')                                # Ansys Fluent
mesh.export(os.path.join(out, 'channel_case', 'constant', 'polyMesh'), format='openfoam', scale=0.001)   # OpenFOAM, in metres
mesh.export(os.path.join(out, 'channel.vtu'))                                                        # ParaView
print('Written to', out)

mesh
