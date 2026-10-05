from fieldes import *

view.set_bounds([-10, -10, -10], [10, 10, 10])
view.set_quality(8)
view.set_resolution(10)

box_exact_centered_1 = box_exact_centered((2, 2, 2), (-0.1, 5.2, 4.8))
box_exact_centered_1 = expose(box_exact_centered_1, [
    var(-0.1), var(1), var(5.2), var(1), var(4.8), var(1),
])
box_exact_centered_1 = handles(box_exact_centered_1, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)), mode='handles')
# hidden: box_exact_centered_1

box_exact_centered_2 = box_exact_centered((2, 2, 2), (3.8, 4.7, 5.5))
box_exact_centered_2 = expose(box_exact_centered_2, [
    var(3.8), var(1), var(4.7), var(1), var(5.5), var(1),
])
box_exact_centered_2 = handles(box_exact_centered_2, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)), mode='handles')
# hidden: box_exact_centered_2

box_centered_1 = box_centered((2, 2, 2), (3.1, 2.3, 3.1))
box_centered_1 = expose(box_centered_1, [
    var(-0.62746), var(4.1), var(-0.244142), var(4.8285), var(2.1), var(6.20347),
])
box_centered_1 = handles(box_centered_1, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)), mode='handles')
difference_1 = difference(box_centered_1, box_exact_centered_1, box_exact_centered_2)
smooth_1 = smooth(difference_1, 0.1, 3)
smooth_1
difference_1
# hidden: box_centered_1


