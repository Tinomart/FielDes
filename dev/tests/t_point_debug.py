import sys
from fieldes import *
from fieldes.kinds import kind_of
print('start', flush=True)
p = point(1, 2, 3)
print('made', flush=True)
del p
print('deleted once', flush=True)
for i in range(5):
    q = point(i, 2, 3)
    del q
print('loop ok', flush=True)
b = Point(1, 2, 3)._ball(0.5)
print('ball ok', flush=True)
del b
print('ball deleted', flush=True)
