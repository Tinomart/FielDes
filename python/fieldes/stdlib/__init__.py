# Import this module first to break dependency loop, since Shape injects
# functions from fieldes.stdlib.csg
import fieldes.shape

from fieldes.stdlib.csg import *
from fieldes.stdlib.shapes import *
from fieldes.stdlib.text import *
from fieldes.stdlib.transforms import *
from fieldes.stdlib.handles import gizmo, handles, lock
from fieldes.stdlib.instances import instance
from fieldes.stdlib.symmetries import symmetry_planes, symmetry_image, symmetry_images, symmetry_count
from fieldes.stdlib.render_cache import *
from fieldes.stdlib.custom_resolution import *
from fieldes.stdlib.cad_import import *
from fieldes.stdlib.excluded import *
from fieldes.stdlib.tessellated_import import *
from fieldes.stdlib.mesh_import import *
from fieldes.stdlib.importing import *
from fieldes.stdlib.exporting import *
from fieldes.stdlib.meshing import *
from fieldes.stdlib.fea import *
from fieldes.stdlib.optimization_measures import *
from fieldes.stdlib.boundary_conditions import *
from fieldes.stdlib.selection import *
from fieldes.stdlib.features import *
from fieldes.stdlib.snapping import *
from fieldes.stdlib.thermal import *
from fieldes.stdlib.fluid import *
from fieldes.stdlib.parametric import *
from fieldes.stdlib.manufacturing import *
from fieldes.stdlib.fields import *
from fieldes.stdlib.fieldargs import *
from fieldes.stdlib.regression import *
from fieldes.stdlib.surfaces import *
from fieldes.stdlib.points import *
from fieldes.stdlib.curves import *
from fieldes.stdlib.profile import *
from fieldes.stdlib.measure import *
from fieldes.stdlib.lattices import *
from fieldes.stdlib.conformal import *
from fieldes.stdlib.content_cache import cache_info, clear_caches

# The key numbers of every result, for the result card (see fieldes.stdlib.exporting)
from fieldes.stdlib.exporting import _install_key_results
_install_key_results()

# What every operation does with a shape that has an excluded region (see fieldes.stdlib.excluded)
import fieldes.stdlib.excluded as _excluded
_excluded.install()

# progress(fraction, text): your own long computations on the progress bar
from fieldes.run_progress import progress
