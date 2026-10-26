# A FielDes app with a viewport: the person sets up a load by dragging, then runs the analysis.
#
# `app.viewport(...)` gives the app window a picture of the models you name, that the person can look around in (the mouse, as in the editor)
# and -- the models that are not locked -- drag by their gizmo and their surfaces.  There is no model tree and no menu in it.  What is
# dragged is written into the numbers of this script (the `var()`s of the gizmo below), which runs again for the picture: so the load point
# below is a dot (the middle point of a gizmo): click it, its arrows show, drag one -- the load follows, and the arrows on the beam are drawn
# where the force is.  The names are drawn over the models, as the labels of boundary conditions are.
#
# A picture is made when the app is opened, when an input changes and when something has been dragged, with `app.running()` False; the work
# -- the analysis, the files -- is under `if app.running():` and happens when the person presses Run.  (In the editor `app.running()` is False too:
# File > Preview as app, then Run, tries the whole app.)
#
# `lock()` is what keeps a model where it is: the beam is locked, so the only thing the person can move is the load point.
from fieldes import *

app.title('Beam load case', 'Drag the load point along the beam, choose the force and the material, press Run for the stresses', '1.0')

force_n = app.number('Force', 300, min=10, max=5000, unit='N', help='Pulling down at the load point')
material = app.choice('Material', ['steel', 'aluminium'], 'steel')
report = app.save_as('Report', 'beam_load_case.txt', 'Text files (*.txt)', 'The key numbers of the analysis, as text')

view.set_bounds((-10, -15, -15), (75, 35, 25), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

beam = difference(box((0, 0, 0), (60, 20, 10)), cylinder(3, 20, (30, 10, -5)))
beam = lock(beam)

# The load point: a point with a gizmo.  Its dot is always there; click it and drag.
pull = point(55, 10, 10,
    position=(var(0), var(0), var(0)),
)

# The beam is held at its left end; the load pulls down where the point is (a block round it: the force is spread over the beam's surface in it)
support = fixed(box((-1, -1, -1), (1, 21, 11)))
load = force(move(box((-6, -11, -6), (6, 11, 6)), pull), (0, 0, -force_n))

body = beam
if app.running():
    result = static_analysis(beam, supports=[support], loads=[load], material=steel if material == 'steel' else aluminium, element_size=3)
    print('Safety factor %.1f, maximum stress %.0f MPa' % (result.safety_factor, result.max_von_mises))
    export_results(result, report)
    body = result

# A field in the viewport is an icon (with its name over it) where the object it was made from is -- here the load point: click the icon and the viewer of
# the field opens, a disc through it, with a radius slider.  This one is the distance from the load point.
distance = distance_to_point(pull)
app.viewport({'Beam': body, 'Load point': pull, 'Distance': distance}, support, load)

# The viewport is a setting of the app: in the editor it draws nothing and has no row in the model tree.  What the editor shows is what is written
# below (the models are in the tree as they are, with their eyes); File > Preview as app shows the viewport the way the person sees it.
body
pull
support
load
distance
