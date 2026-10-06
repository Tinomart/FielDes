# What happens to a downstream expose() when the model it is made of is exposed and given a gizmo afterwards
from fieldes import *
from fieldes.stdlib.handles import _exposed_count

ball = sphere(8, (60, 15, 10))
rounded = offset(ball, 1.0)
print('numbers of rounded alone:', _exposed_count(rounded))
rounded_e = expose(rounded, [var(60.0), var(15.0), var(10.0), var(8.0), var(1.0)])
ball2 = expose(ball, [var(60.0), var(15.0), var(10.0), var(8.0)])
print('numbers of ball exposed:', _exposed_count(ball2))
ball3 = handles(ball2, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
rounded2 = offset(ball3, 1.0)
print('numbers of offset(exposed + gizmo ball):', _exposed_count(rounded2))
try:
    expose(rounded2, [var(60.0), var(15.0), var(10.0), var(8.0), var(1.0)])
    print('expose with 5 numbers: ok')
except ValueError as e:
    print('expose with 5 numbers: ERROR', e)
