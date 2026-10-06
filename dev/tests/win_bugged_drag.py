# (the user's saved file: a union that only references a top-level sphere; dragging the union above it, or the sphere below it, asks and merges)
from fieldes import *

view.set_bounds([-10, -10, -10], [10, 10, 10])
view.set_quality(8)
view.set_resolution(10)

sphere_1 = sphere(1)
sphere_1 = expose(sphere_1, [
    var(0.0), var(0.0), var(0.0), var(1.0),
])
sphere_1 = handles(sphere_1, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
# hidden: sphere_1

sphere_2 = sphere(2.5, (-6.8, -3.4, 5.2))
sphere_2 = expose(sphere_2, [
    var(-6.8), var(-3.4), var(5.2), var(2.5),
])
sphere_2 = handles(sphere_2, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
# hidden: sphere_2
sphere_3 = sphere(2.5, (-1, 5.8, 5))
sphere_3 = expose(sphere_3, [
    var(-1.0), var(5.8), var(5.0), var(2.5),
])
sphere_3 = handles(sphere_3, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
# hidden: sphere_3
union_1 = union(sphere_1, sphere_3, sphere_2)  # shadow: sphere_2
union_1 = handles(union_1, move=(var(0), var(-0.682683), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
union_1
