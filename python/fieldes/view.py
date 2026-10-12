'''
How the viewport renders a script: the region, the resolution and the quality.

    view.set_bounds((-10, -10, -10), (10, 10, 10))   # the region meshed (mm); it grows by itself, in this line, when a model
                                                     # reaches out of it -- unless you write auto_scale=False: then it is what you wrote
    view.set_resolution(10)                          # samples per mm
    view.set_quality(8)                              # mesh accuracy, 1 to 10

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
        ''' The region the viewport meshes: its lower and upper corner (mm).  auto_scale (the default): FielDes grows these numbers in the
            script when a model reaches out of the region; False: the region is exactly what is written '''
        settings['bounds'] = (tuple(lo), tuple(hi))
        settings['auto_scale'] = bool(auto_scale)

    def set_resolution(res):
        ''' Samples per mm of the viewport's mesh '''
        settings['resolution'] = float(res)

    def set_quality(q):
        ''' Mesh accuracy of the viewport, 1 to 10 '''
        settings['quality'] = float(q)
