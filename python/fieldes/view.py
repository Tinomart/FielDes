'''
How the viewport renders a script: the region, the resolution and the quality.

    view.set_bounds((-10, -10, -10), (10, 10, 10), auto_scale=True)   # the region meshed (mm); FielDes rewrites these numbers, live,
                                                     # to follow the models (grown when one reaches out, shrunk when they get much
                                                     # smaller), and meshes every model at a resolution of its own size
                                                     # -- unless you write auto_scale=False: then it is what you wrote
    view.set_resolution(10)                          # ten samples across the longest side of a model for every unit: a hundred at 10
    view.set_quality(8)                              # mesh accuracy, 1 to 10

The resolution is NORMED by the size of the model: the number says how many samples go across the longest side of the box of every model -- ten for every unit of it -- whatever the size of
the model in mm and whatever is next to it, so scaling a model does not change its number of triangles, and a huge cylinder and a small one beside it have the same.
`resolution_per_mm(size)` says what that is in samples per mm for a model that is `size` mm long.

Inside the application these are read when the script has run; anywhere else
(a plain Python session, a test) they are remembered in `settings` and used by
nothing.
'''

try:
    from _fieldes_host import set_bounds, set_resolution, set_quality   # (provided by the application)
    in_application = True
except ImportError:
    in_application = False
    settings = {}

    def set_bounds(lo, hi, auto_scale=True):
        ''' The region the viewport meshes: its lower and upper corner (mm).  auto_scale (the default): FielDes rewrites these numbers in the
            script to follow the models -- grown when a model reaches out of the region, shrunk when the models get much smaller -- and
            meshes every model at the scene's resolution normed by its own size (see the top of this module); False: the region is exactly what is
            written, and every model is meshed at the scene's resolution as samples per mm '''
        settings['bounds'] = (tuple(lo), tuple(hi))
        settings['auto_scale'] = bool(auto_scale)

    def set_resolution(res):
        ''' The resolution of the viewport's mesh: ten samples across the longest side of a model for every unit (with auto_scale; samples per mm without it) '''
        settings['resolution'] = float(res)

    def set_quality(q):
        ''' Mesh accuracy of the viewport, 1 to 10 '''
        settings['quality'] = float(q)


SAMPLES_ACROSS = 10.0        # (samples across the longest side of a model for every unit of the resolution)


def _scene_resolution():
    ''' The resolution the script has asked for so far: the application's, or what was remembered outside it; None when it has not said '''
    try:
        import _fieldes_host as host
        res = getattr(host, '__resolution', None)
    except ImportError:
        res = globals().get('settings', {}).get('resolution')
    try:
        return float(res) if res else None
    except (TypeError, ValueError):
        return None


def resolution_per_mm(size, res=None):
    ''' The samples per mm that the scene's resolution `res` (the script's, when not given) comes to for a model whose longest side is `size` mm: `res * 10 / size`, at most 2000 samples across.  None when
        the script has not set a resolution.  The viewport norms every model by its own size, and what reads the resolution to do the same work -- an export, a check -- asks here '''
    if res is None:
        res = _scene_resolution()
    if not res:
        return None
    size = float(size)
    if not size > 1e-9:
        return float(res)
    return min(float(res) * SAMPLES_ACROSS / size, 2000.0 / size)
