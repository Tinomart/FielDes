# What expose() makes of the starting sphere (a sphere at the origin: are the zero centre numbers kept?)
from fieldes import *
from fieldes.stdlib.handles import exposed_values
for name, s in (('sphere(1)', sphere(1)), ('sphere(1, (0, 0, 0))', sphere(1, (0, 0, 0))), ('sphere(5, (-30, 0, -19.5))', sphere(5, (-30, 0, -19.5)))):
    print(name, '->', exposed_values(s))
