import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_fd17.py'), encoding='utf-8').read().split("print('drag per iteration')")[0].split("print(design)")[0])
print(design)
for k, st in enumerate(design.flow.steps):
    print('step %2d it %2d: nonlinear its %2d residual %.2e converged %s max speed %.3f drop %.3g force x %.4g' % (
        k, st.iteration if st.iteration is not None else -1, st.iterations, st.residual, st.converged, st.max_speed, st.pressure_drop, st.wall_force[0]))
