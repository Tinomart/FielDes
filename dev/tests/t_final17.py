# The cached example-17 result: did the real flow of the final step converge, and what does its wall look like?
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_fd17.py'), encoding='utf-8').read().split("print('drag per iteration')")[0].split("print(design)")[0])
f = design.flow
print('real flow: converged %s, %d nonlinear iterations, residual %.2e, %d elements, in %.4g out %.4g, max speed %.3f' % (
    f.converged, f.iterations, f.residual, f.elements, f.inlet_flow, f.outlet_flow, f.max_speed))
m = design.model_flow.steps[-1]
print('last model step: converged %s, residual %.2e' % (m.converged, m.residual))
print(design)
