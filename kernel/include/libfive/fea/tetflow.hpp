/*
libfive: a CAD kernel for modeling with implicit functions

Steady, incompressible, laminar flow of a fluid through an implicit shape (the fluid domain: the
field is negative where the fluid is) on the same body-fitted tetrahedral mesh as the structural and
thermal analyses (see tetmesh.hpp).

The equations are the steady Navier-Stokes equations in millimetres, tonnes, seconds, newtons and
megapascals:

    rho (u . grad) u - mu laplace u + grad p = rho g,     div u = 0

discretised with linear tetrahedra for both the velocity and the pressure (P1/P1) and stabilised
the usual way (Tezduyar: SUPG for the convection, PSPG for the pressure, a grad-div (LSIC) term,
and a backflow term on the outlets); every stabilisation term is residual-based, so the discrete
equations are consistent with the continuous ones.  The viscous term is the Laplacian form
mu (grad w : grad u), whose natural ("do-nothing") outlet condition mu du/dn - p n = -p_out n is
satisfied by a developed (Poiseuille) profile: an outlet lets a developed flow leave undisturbed
at the pressure given.  The wall force uses the full stress -p I + mu (grad u + grad u^T).

Boundary conditions are regions (other shapes), on the boundary triangles of the mesh:
  addInlet      a velocity into the domain (a direction and a mean speed, or a flow rate) with a
                uniform profile, or the developed one (the exact fully developed profile of the
                inlet's cross-section: -laplace phi = 1 on the inlet patch, phi = 0 on its rim)
  addOutlet     the pressure there (the do-nothing condition)
  addWall       a wall, no-slip, moving with the velocity given; every boundary triangle that is
                in no region is a wall at rest
  addSlip       a symmetry plane / a slip wall: the normal velocity is zero, the tangential free

The velocity at a node of several kinds of triangle: an explicit wall wins, then a wall at rest,
then an inlet, then slip, so the rim of an inlet is at rest (no-slip) and the inlet's speed or flow
rate is matched exactly on the discrete flux through the inlet's triangles.

The nonlinearity is solved from the Stokes solution by Picard (Oseen) iterations and then Newton's
method; each linear system is solved directly (sparse LU) when it is small enough, else by
BiCGSTAB with an incomplete LU.  Results (a MeshResult, fields 0 to 7): the speed, the three
velocity components, the pressure, the total pressure p + rho |u|^2 / 2, the shear rate and the
vorticity, at the nodes (the velocity and pressure as solved; the derived ones from each element's
constant gradient, averaged at the nodes by volume).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/fea/tetfea.hpp"

namespace libfive {
namespace fea {

struct FlowIlut;                // (the block incomplete LU of the flow's linear solves: tetflow.cpp)

class TetFlowProblem
{
public:
    enum Field { SPEED = 0, VX, VY, VZ, PRESSURE, TOTAL_PRESSURE, SHEAR_RATE, VORTICITY, FLOW_FIELD_COUNT };
    enum Profile { UNIFORM = 0, DEVELOPED = 1 };

    /*  The fluid domain (the shape), the region to mesh, the element size, and the fluid
     *  (density in t/mm^3, dynamic viscosity in MPa s)  */
    TetFlowProblem(const Tree& domain, Eigen::Vector3d lo, Eigen::Vector3d hi, double h, double density,
                   double viscosity);
    ~TetFlowProblem();

    /*  direction: the flow direction (zero: along the inlet's inward normal); speed: the mean speed over
     *  the inlet (mm/s; <= 0: the direction's length); flowRate: mm^3/s (> 0 overrides the speed)  */
    void addInlet(const Tree& region, Eigen::Vector3d direction, double speed, double flowRate, int profile);
    void addOutlet(const Tree& region, double pressure);
    void addWall(const Tree& region, Eigen::Vector3d velocity);
    void addSlip(const Tree& region);
    /*  A body force per unit mass (gravity, mm/s^2): f = rho g  */
    void setBodyForce(Eigen::Vector3d g);

    struct Options
    {
        bool stokes = false;            // leave the convection out (creeping flow)
        int nonlinearIterations = 60;   // Picard / Newton iterations at most
        double relaxation = 1.0;        // of the Picard update
        int directLimit = 12000;        // unknowns: a direct solve up to here (small), iterative above (and direct if that fails)
        int linearIterations = 4000;    // of the iterative solver
        double linearTolerance = 1e-9;
        bool newton = true;             // Newton's method once Picard has brought the residual down
    };
    void setOptions(const Options& o) { m_opt = o; m_prepared = false; }
    const Options& options() const { return m_opt; }

    /*  Meshes the domain and resolves the boundary conditions; false with a message if the
     *  problem can't be solved as given  */
    bool prepare(std::string& error);
    uint64_t hash() const { return m_hash; }
    bool solve(double tolerance, std::string& error, const std::atomic<bool>* cancel = nullptr);

    std::shared_ptr<const MeshResult> result() const { return m_result; }
    std::shared_ptr<const TetMesh> mesh() const { return m_mesh; }

    /*  The flow in time: from the Stokes flow at t = 0 (an impulsive start), `steps` steps of `dt` seconds, each a
     *  nonlinear solve of the equations with the time derivative (backward Euler, the consistent mass matrix, the
     *  time term in the stabilisation).  Every storeEvery-th step (and the last) is kept: step(k) with its time,
     *  fields and numbers; result() is the last.  */
    bool solveTransient(double dt, int steps, int storeEvery, double tolerance, std::string& error,
                        const std::atomic<bool>* cancel = nullptr);
    /*  The steps of a solve: of a steady solve, the flow after each nonlinear iteration (the Stokes start
     *  first; iteration 0, 1, ...), so the flow can be seen developing; of a flow in time, every stored time
     *  (iteration -1).  The last step is the result itself.  */
    struct Step
    {
        double time = 0;
        int iteration = -1;
        std::shared_ptr<const MeshResult> result;
        struct Stats* stats = nullptr;      // (filled below; see stepStats)
    };
    size_t stepCount() const { return m_steps.size(); }
    double stepTime(size_t k) const { return k < m_steps.size() ? m_steps[k].time : 0.0; }
    int stepIteration(size_t k) const { return k < m_steps.size() ? m_steps[k].iteration : -1; }
    std::shared_ptr<const MeshResult> stepResult(size_t k) const { return k < m_steps.size() ? m_steps[k].result : nullptr; }

    /*  Streamlines of a result (the last one, or a stored step): from each seed, the path a particle takes
     *  (forward, or backward against the flow) by Runge-Kutta steps of half an element, up to maxTime seconds and
     *  maxPoints points, until it leaves the fluid or comes to rest.  Each point: x y z, the speed there, the time
     *  since the seed.  */
    void streamlines(std::shared_ptr<const MeshResult> res, const std::vector<Eigen::Vector3d>& seeds, double maxTime,
                     int maxPoints, bool backward, std::vector<std::vector<std::array<double, 5>>>& out) const;
    /*  Up to n points spread over the inlets (a little inside the fluid), to seed streamlines from  */
    std::vector<Eigen::Vector3d> inletSeeds(int n) const;

    struct Stats
    {
        double inletFlow = 0, outletFlow = 0;           // mm^3/s, into / out of the domain
        double wallFlow = 0;                            // net flow in through the walls (a moving wall; the rounded rim of an inlet)
        double imbalance = 0;                           // |in + through the walls - out| / in
        double pressureDrop = 0;                        // mean inlet pressure - mean outlet pressure (MPa)
        double maxSpeed = 0;                            // mm/s
        double dissipation = 0;                         // viscous dissipation, N mm / s (= mW)
        double reynolds = 0;                            // rho U D_h / mu (U: mean inlet speed, D_h: inlet's)
        double cellReynolds = 0;                        // max |u| h / nu over the elements
        double hydraulicDiameter = 0;                   // 4 V / S of the domain (S: the walls)
        double elementsAcross = 0;                      // hydraulicDiameter / h
        Eigen::Vector3d wallForce = Eigen::Vector3d::Zero();    // on all walls, N
        int nonlinearIterations = 0;
        double residual = 0;
        bool converged = false;
        double seconds = 0;
        int linearSolver = 0;                           // 0 direct, 1 iterative, 2 both (the iterative one failed once)
        int elements = 0, nodes = 0, unknowns = 0;
    };
    const Stats& stats() const { return m_stats; }
    const Stats& stepStats(size_t k) const { return k < m_stepStats.size() ? m_stepStats[k] : m_stats; }
    const std::vector<double>& inletFlows() const { return m_inletFlows; }
    const std::vector<double>& outletFlows() const { return m_outletFlows; }
    const std::vector<Eigen::Vector3d>& wallForces() const { return m_wallForces; }    // per explicit wall
    /*  The message of the last prepare / solve that is a warning, not an error  */
    const std::string& warning() const { return m_warning; }

    /*  For the flow topology optimisation: a Brinkman friction alpha per tetrahedron (a force
     *  -alpha u per volume; empty: none).  Must be set before solve; prepare is unchanged.  */
    void setFriction(std::vector<double> alpha) { m_alpha = std::move(alpha); }
    /*  After a solve: the velocity at the nodes (3 per node) and the pressure  */
    const std::vector<double>& velocity() const { return m_u; }
    const std::vector<double>& pressure() const { return m_p; }


    /*  Shape optimisation of a BODY in the flow, on the optimiser every density-based design shares (density_design.hpp): one density per tetrahedron (1 solid, 0 fluid),
     *  filtered, sharpened, kept symmetric, moved by the method of moving asymptotes on the MEASURES of the flow -- the force on the body along the
     *  flow (drag) and across it (lift), the volume of the body as a share of its own -- with the objective and the constraints as formulas of
     *  them (`program`).  The friction of an element is alpha_max times its density; the force on the body, F = sum_e alpha_e int u dV, is the
     *  momentum the fluid loses in it; the sensitivity of each force to every element's density is the exact discrete adjoint's (one adjoint solve
     *  per direction, on the factorisation of the flow's own).  The results are those of optimize(): the drag and lift of every iteration (the last
     *  of the final body), the design as a level set (mm, positive inside the body: the density above one half, scaled by the filter radius), the flows
     *  as steps  */
    enum DensityMeasure { MEASURE_DRAG = 0, MEASURE_LIFT = 1, MEASURE_VOLUME = 2 };
    struct DensityOpt : topopt::DesignSettings
    {
        Tree body = Tree::invalid();                    // the body the design starts as (solid where negative); its volume is the volume the share is of
        Tree region = Tree::invalid();                  // where material may be (invalid: anywhere in the fluid domain)
        Eigen::Vector3d flowDir = Eigen::Vector3d::Zero(), liftDir = Eigen::Vector3d::Zero();      // the flow direction (zero: the inlets' mean) and the lift direction (zero: perpendicular to it in the plane of the domain's two long axes)
        double darcy = 0.1;                             // the solid's permeability relative to the element: its friction is mu / (darcy h^2)
        std::vector<int> measures;                      // the kind (DensityMeasure) of each measure the program reads, in the program's order
    };
    bool optimizeDensity(const DensityOpt& settings, std::string& error, const std::atomic<bool>* cancel = nullptr);
    /*  After optimizeDensity: every measure, and every output of the program, at each kept iteration  */
    const std::vector<std::vector<double>>& baseHistory() const { return m_baseHistory; }
    const std::vector<std::vector<double>>& outputHistory() const { return m_outputHistory; }
    /*  After optimizeDensity: the volume of the body the design started as (mm^3), and the volume of the design (the density integrated)  */
    double bodyVolume() const { return m_bodyVolume; }
    double designVolume() const { return m_designVolume; }
    /*  After optimizeDensity: in how many pieces the final body is (a piece under a fiftieth of the largest is a speck and is not counted)  */
    int pieces() const { return m_pieces; }
    /*  After optimize: the planes the design was kept symmetric about (`found`: by itself, not asked for)  */
    struct MirrorUsed { int axis; double at; bool found; };
    const std::vector<MirrorUsed>& mirrorsUsed() const { return m_mirrorsUsed; }
    int meshMirrors() const { return m_meshMirrors; }
    /*  After optimize: the drag and the lift (N) at each iteration (the last of the final body), the design as a
     *  field on the mesh (field 0: each tetrahedron's rho averaged at the nodes by volume) for meshFieldTree(., 0),
     *  and the directions used  */
    const std::vector<double>& dragHistory() const { return m_history; }
    const std::vector<double>& liftHistory() const { return m_dropHistory; }
    std::shared_ptr<const MeshResult> densityResult() const { return m_densityResult; }
    Eigen::Vector3d flowDirection() const { return m_flowDir; }
    Eigen::Vector3d liftDirection() const { return m_liftDir; }
    /*  The design as a field after iteration k (0-based; one per drag value)  */
    size_t densityHistoryCount() const { return m_densityHistory.size(); }
    std::shared_ptr<const MeshResult> densityResultAt(size_t k) const;
    /*  How the optimisation ended: the steps it took back (each halved the step), and why it stopped --
     *  0 the iteration limit, 1 the objective stopped improving, 2 no step shortens the objective any more,
     *  3 no sensitivity left  */
    int optStepsBack() const { return m_optRejected; }
    int optStop() const { return m_optStop; }

private:
    friend struct ResultIO;
    struct Inlet { Tree region; Eigen::Vector3d direction; double speed, flowRate; int profile; };
    struct Outlet { Tree region; double pressure; };
    struct Wall { Tree region; Eigen::Vector3d velocity; };

    Tree m_shape;
    Eigen::Vector3d m_lo, m_hi;
    double m_h, m_rho, m_mu;
    Eigen::Vector3d m_g = Eigen::Vector3d::Zero();
    std::vector<Inlet> m_inlets;
    std::vector<Outlet> m_outlets;
    std::vector<Wall> m_walls;
    std::vector<Tree> m_slips;
    Options m_opt;

    // Prepared
    bool m_prepared = false;
    std::shared_ptr<TetMesh> m_mesh;
    std::vector<int> m_faceKind;                // per boundary face: -1 wall at rest, 0 inlet, 1 outlet, 2 wall, 3 slip
    std::vector<int> m_faceItem;                // per boundary face: which inlet / outlet / wall / slip (its index)
    std::vector<double> m_faceArea;
    std::vector<Eigen::Vector3d> m_faceNormal;  // unit, out of the fluid
    std::vector<unsigned char> m_fixed;         // per unknown (4 per node: ux uy uz p)
    std::vector<double> m_fixedValue;
    std::vector<Eigen::Vector3d> m_slipNormal;  // per node: the nodal normal of a slip node with a penalty (zero: none)
    std::vector<Eigen::Vector3d> m_slipNormal2; // ...and a second one at the rounded edge between two slip planes
    std::vector<uint32_t> m_vfStart, m_vfList;  // each node's boundary faces (CSR)
    std::vector<int> m_nodeKind, m_nodeItem;    // per node: the kind of condition that holds it, and its item
    std::vector<double> m_alpha;                // Brinkman friction per tetrahedron (empty: none)
    uint64_t m_hash = 0;
    std::string m_warning;
    double m_inletArea = 0, m_inletPerimeter = 0, m_wallArea = 0, m_volume = 0, m_refSpeed = 0;

    // Solved
    std::vector<double> m_u, m_p;
    std::shared_ptr<MeshResult> m_result;
    Stats m_stats;
    std::vector<double> m_inletFlows, m_outletFlows;
    std::vector<Eigen::Vector3d> m_wallForces;
    bool m_quiet = false;                       // (no progress task of its own: the optimiser has one)
    double m_dtInv = 0;                         // 1 / dt while stepping in time, else 0
    std::vector<double> m_uOld;                 // the velocity of the step before (3 per node) while stepping
    bool m_snapshot = false;                    // keep the result of this solve as a step
    double m_time = 0;                          // the time of the step being solved
    std::vector<Step> m_steps;
    std::vector<Stats> m_stepStats;
    std::shared_ptr<const TetLocator> m_locator;    // one locator for all the results of this mesh
    double m_residualScale = 0;                 // the scaled residual of the first (cold) linear solve: the absolute scale
    // The incomplete LU of the last linear solve, kept while it still works (the matrix of a flow in time, or of an
    // optimisation iteration, changes little from one solve to the next): its dimension, the iterations the solve
    // right after building it took, and the last solve's (-1: not usable)
    std::unique_ptr<FlowIlut> m_ilut;           // the block incomplete LU of the scaled system, kept between solves
    size_t m_ilutN = 0;
    long m_ilutBase = 0, m_ilutIter = -1;
    std::unique_ptr<FlowIlut> m_ilutAdj;        // the same for the matrix of the adjoint (the transpose): a place of its own, so that the factorisation of the flow is not thrown away at every iteration of the optimiser
    size_t m_ilutAdjN = 0;
    long m_ilutAdjBase = 0, m_ilutAdjIter = -1;
    bool m_ilutAdjNewton = false;
    double m_adjointTolerance = 0.0;            // the reduction asked of the optimiser's adjoint solves (0: the solver's own tolerance)
    bool m_ilutNewton = false;                  // whether the kept factorisation was made of a Newton matrix (one of a Picard matrix is a poor preconditioner for it)
    int m_meshMirrors = 0;                      // the planes the mesh was mirrored about (see prepare)
    // The discrete adjoint of the last (Stokes) solve, when the optimiser asks for it: lambda with K^T lambda = -dJ/dU,
    // J the dissipated power; zero at the held unknowns
    bool m_wantAdjoint = false;
    // The force directions the optimiser wants adjoints for (J_k = F . m_objDirs[k]): one adjoint solve for each, lambda_k with K^T lambda_k = -dJ_k / dU
    std::vector<Eigen::Vector3d> m_objDirs;
    std::vector<std::vector<double>> m_adjoints;
    std::vector<std::vector<double>> m_baseHistory, m_outputHistory;
    double m_bodyVolume = 0, m_designVolume = 0;
    int m_pieces = 0;
    Eigen::Vector3d m_flowDir = Eigen::Vector3d::Zero(), m_liftDir = Eigen::Vector3d::Zero();
    std::vector<Eigen::Vector3d> m_inletDir;    // per inlet: the direction its velocity was given along

    // Optimised
    std::vector<double> m_history, m_dropHistory;
    std::shared_ptr<MeshResult> m_densityResult;        // the level set at the nodes (mm, > 0 in the body) as field 0
    std::vector<std::vector<float>> m_densityHistory;   // per iteration: the level set at the nodes
    int m_optRejected = 0, m_optStop = 0;
    std::vector<MirrorUsed> m_mirrorsUsed;      // (see optStepsBack / optStop)

    bool resolveConditions(std::string& error);
    bool developedProfile(const std::vector<char>& inFace, const std::vector<char>& rim, std::vector<double>& phi,
                          std::string& error) const;
};

}   // namespace fea
}   // namespace libfive
