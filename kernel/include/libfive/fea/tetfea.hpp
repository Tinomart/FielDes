/*
libfive: a CAD kernel for modeling with implicit functions

Static linear-elastic FEA of an implicit shape on a body-fitted tetrahedral
mesh (see tetmesh.hpp): linear tetrahedra (constant strain in each) whose
boundary is the shape's surface.  A stiffness matrix is assembled from the
elements' own geometry -- no two need be alike -- and K u = f is solved with a
block-Jacobi preconditioned conjugate gradient method.

Supports fix the mesh's nodes inside a region.  A load is a total force spread
over the boundary triangles inside a region by area (over the nodes inside it
if the region holds no surface).  Gravity acts on each element's volume;
thermal expansion from a temperature field at each element's centre.

Results: displacements at the nodes; stress in every element (constant in it,
exactly as the element has it) and at the nodes as the volume-weighted average
of the elements meeting there.  Fields are read anywhere by finding the
tetrahedron that holds the point (linear inside it), so they are Trees like any
other and can be displayed, probed and combined mathematically.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/fea/fea.hpp"
#include "libfive/fea/tetmesh.hpp"
#include "libfive/fea/density_design.hpp"
#include "libfive/fea/topopt.hpp"
#include "libfive/tree/tree.hpp"

namespace libfive {
namespace fea {

/*  Finds the tetrahedron that holds a point  */
class TetLocator
{
public:
    explicit TetLocator(const TetMesh& mesh);

    /*  The tetrahedron holding p and p's barycentric coordinates in it.  A point
     *  outside the mesh gets the nearest tetrahedron found around it with the
     *  coordinates clamped inside it (so a field is continued as its nearest
     *  value); -1 if the mesh has nothing near p.  */
    int locate(const Eigen::Vector3d& p, double lambda[4]) const;
    /*  The tetrahedron that holds p, or -1 when none does (a point outside the mesh): no nearest one  */
    int locateInside(const Eigen::Vector3d& p, double lambda[4]) const;

    /*  The gradients of tetrahedron t's four barycentric coordinates (rows)  */
    void gradients(int t, Eigen::Matrix<double, 4, 3>& g) const;

    /*  Calls fn(t) for every tetrahedron whose cell overlaps the box; false if
     *  the box reaches outside the mesh's extent  */
    template <class F>
    bool forTetsIn(const Eigen::Vector3d& lower, const Eigen::Vector3d& upper, F&& fn) const;

    const TetMesh& mesh() const { return *m_mesh; }

private:
    void cellRange(const Eigen::Vector3d& lower, const Eigen::Vector3d& upper, int lo[3], int hi[3]) const;

    const TetMesh* m_mesh;
    Eigen::Vector3d m_lo = Eigen::Vector3d::Zero(), m_hi = Eigen::Vector3d::Zero();
    double m_cell = 1;
    int m_n[3] = {1, 1, 1};
    std::vector<uint32_t> m_start, m_list;              // per cell: its tetrahedra (CSR)
    struct Inv { double m[9]; double p0[3]; };
    std::vector<Inv> m_inv;                             // per tetrahedron: barycentric coordinates from a point

    size_t cellIndex(int i, int j, int k) const { return (size_t(k) * m_n[1] + j) * m_n[0] + i; }
};

/*  A solved analysis  */
struct MeshResult
{
    uint64_t serial = nextContentSerial();      // (see Result::serial)
    std::shared_ptr<const TetMesh> mesh;
    std::shared_ptr<const TetLocator> locator;
    // Per vertex (Result::Field): displacements as solved, stresses as the
    // volume-weighted average of the elements meeting there
    std::vector<float> fields[Result::FIELD_COUNT];
    float minValue[Result::FIELD_COUNT] = {0};
    float maxValue[Result::FIELD_COUNT] = {0};
    // Per tetrahedron: its own stress (xx yy zz xy yz zx) and strain energy density
    std::vector<std::array<float, 7>> elementStress;

    int elements = 0, nodes = 0, dofs = 0, fixedNodes = 0, loadedNodes = 0, looseElements = 0;
    int iterations = 0;
    double residual = 0, seconds = 0, volume = 0, compliance = 0;
    Eigen::Vector3d totalLoad = Eigen::Vector3d::Zero();
    Eigen::Vector3d reaction = Eigen::Vector3d::Zero();
    double heatIn = 0, heatOutFixed = 0, heatOutConvection = 0;         // (thermal analysis: W)

    /*  A field's value in each tetrahedron as it has it: its own stress, or
     *  the displacement at its centre  */
    void elementValues(int field, std::vector<float>& out) const;
};

/*  A result field as a Tree: linear interpolation of the nodal values inside
 *  the tetrahedron that holds the point  */
Tree meshFieldTree(std::shared_ptr<const MeshResult> result, int field);

namespace tet { struct QuadMesh; }

class TetProblem
{
public:
    TetProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi, double h, double E, double nu);

    /*  A support.  loadCase >= 0: it holds the part only in that load case (topology optimization against several sets of
     *  boundary conditions, each with its own supports); -1: in every case.  `displacement`: where the held directions are held AT -- 0 (the
     *  default) the part does not move there, another number is a displacement the support imposes (mm); a static analysis solves for the
     *  rest of the part, a modal analysis holds the part still (the displacement means nothing to a vibration), and a topology optimization
     *  refuses it  */
    void addSupport(const Tree& region, bool x, bool y, bool z, int loadCase = -1, Eigen::Vector3d displacement = Eigen::Vector3d::Zero());
    /*  A load in a load case (0, 1, ...): topology optimization makes the part stiff for each
     *  case on its own; a static analysis applies every load together  */
    void addForce(const Tree& region, Eigen::Vector3d total, int loadCase = 0);
    /*  The same, with the total spread over the region's surface in proportion to a field (taken at the centres of the boundary
     *  triangles; not negative) instead of evenly by area: a pressure that is not the same everywhere  */
    void addForceProfile(const Tree& region, Eigen::Vector3d total, int loadCase, const Tree& profile);

    /*  The supports and loads act on the body only: a node farther outside it than `slack` is never held, and a boundary face with a corner farther
     *  outside it is never loaded.  For a design space that is bigger than the part (the part grown outwards, with holes where the supports and loads
     *  are): the walls the space has round such a hole lie on the edge of the region, and are not the part's surface -- a support that held them would
     *  anchor material to a place that is not on the part, a load that took them would carry a share of the force off the part.  The body is a field,
     *  negative inside (its distance, where it is one).  Before prepare  */
    void setConditionBody(const Tree& body, double slack);

    /*  The supports hold only nodes on the surface of the mesh (a corner of a boundary face), as the loads act only on boundary faces: a support is the
     *  FACE where the part meets what holds it, not the slab of the part behind it that its region happens to take in.  For a design that may grow:
     *  material it puts against that slab, or round the edge of the face, would be clamped to nodes that are held and give it a stiffness the
     *  support does not give (it curled round the plate's edge, past the plane of the face, for nothing but that).  A support that finds no such
     *  node holds the nodes it did.  Before prepare  */
    void setConditionSurface(bool on) { m_conditionSurface = on; }

    /*  Material properties as fields of space (each is evaluated at the centre of every tetrahedron):
     *    setStiffnessField  Young's modulus in MPa -- E given to the constructor is then only what the stiffness matrix is
     *                       made with, each element's stiffness being E(centre) / E of it
     *    setDensityField    the density in t / mm^3: the weight of gravity, the mass of a modal analysis
     *    setExpansionField  the thermal expansion coefficient in 1 / K  */
    void setStiffnessField(const Tree& E);
    void setDensityField(const Tree& density);
    void setExpansionField(const Tree& alpha);
    void setGravity(Eigen::Vector3d g, double density);
    void setThermal(const Tree& temperature, double alpha, double reference);
    /*  The element: 1 (the default), the linear tetrahedron; 2, the 10-node tetrahedron on the same mesh (a node in the middle of every edge:
     *  quadratic displacement, linear stress in each element).  The result is on the nodes, cut into linear tetrahedra, eight to an element:
     *  MeshResult::mesh is that mesh, MeshResult::elements the number of 10-node elements.  For static and modal analysis; not for topology
     *  optimization  */
    void setOrder(int order);

    /*  Meshes the part and resolves the supports and loads; false with a
     *  message if the problem can't be solved as given  */
    bool prepare(std::string& error);

    /*  Modal analysis: the part's `count` lowest natural frequencies (Hz, with mm, t / mm^3 and
     *  MPa) and mode shapes, held by the supports -- K phi = omega^2 M phi with a lumped mass
     *  matrix (the material's density), by locally optimal block preconditioned conjugate
     *  gradients.  No loads are needed.  Each mode's shape is a MeshResult (fields UX, UY, UZ,
     *  DISPLACEMENT; scaled to a largest movement of 1).  */
    bool modal(int count, double density, int maxIterations, double tolerance, std::string& error,
               const std::atomic<bool>* cancel = nullptr);
    const std::vector<double>& frequencies() const { return m_frequencies; }

    /*  A load case that is a modal analysis: the supports of the case hold the part and nothing loads it; the optimisation reads
     *  its frequencies.  Before prepare  */
    void markModalCase(int loadCase);

    /*  Topology optimization (SIMP with a density filter and the method of moving asymptotes): finds the distribution of material
     *  that is best for what is asked -- an objective and constraints that are formulas of MEASURES of the analyses (see topopt.hpp
     *  and `measures`).  Elements inside `keep` regions (and those holding supported or loaded nodes) stay solid, elements inside
     *  `avoid` regions stay empty.  */
    struct TopOpt : topopt::DesignSettings
    {
        using Mirror = topopt::Mirror;
        /*  A number read from the design that the formulas use, with its sensitivity to the density of every element.  loadCase: the
         *  analysis it is read in (the cases are numbered as the loads and the supports give them).  VOLUME is the share of the part's
         *  volume that the design uses (0 .. 1; the part is the design space, or the part it was grown from)  */
        enum Kind { VOLUME = 0, COMPLIANCE = 1, MAX_DISPLACEMENT = 2, MAX_VON_MISES = 3, FREQUENCY = 4, DISPLACEMENT_AT = 5 };
        struct Measure
        {
            int kind = VOLUME;
            int loadCase = 0;
            int index = 0;                  // FREQUENCY: the mode (0 the lowest); DISPLACEMENT_AT: the component 0 / 1 / 2, or 3 for the magnitude
            double a = 0, b = 0, c = 0;     // DISPLACEMENT_AT: the point
        };
        std::vector<Measure> measures;
        /*  What every density design has -- the objective and the constraints as formulas of the measures, the density it starts with, the filter,
         *  the extrusion, the iterations, the keep / avoid regions, the part it was grown from, how crisp it gets, the symmetry -- is in
         *  topopt::DesignSettings (density_design.hpp)  */
        double density = 0;                 // the material's density, t / mm^3: the mass of a frequency
        double penalty = 3.0;
        double minStiffness = 1e-3;     // of the full material, for "void"
        int solverIterations = 20000;
        double tolerance = 1e-5;
    };
    bool optimize(const TopOpt& settings, std::string& error, const std::atomic<bool>* cancel = nullptr);
    /*  After optimize: the planes the design was kept symmetric about (found or asked for), `at` as it was resolved  */
    const std::vector<TopOpt::Mirror>& mirrorsUsed() const { return m_mirrorsUsed; }
    /*  After optimize: the objective at each iteration, and the density (0..1) as a field on the
     *  mesh (each tetrahedron's density averaged at the nodes by volume) for meshFieldTree(., 0)  */
    const std::vector<double>& complianceHistory() const { return m_history; }
    /*  ... and every measure at each iteration (baseHistory()[k][measure]), and every output of the program (outputHistory()[k][output]:
     *  the objective, then the constraints)  */
    const std::vector<std::vector<double>>& baseHistory() const { return m_baseHistory; }
    const std::vector<std::vector<double>>& outputHistory() const { return m_outputHistory; }
    std::shared_ptr<const MeshResult> densityResult() const { return m_densityResult; }
    /*  The density as a field after iteration k (0-based; one per compliance value): how the design was found,
     *  step by step  */
    size_t densityHistoryCount() const { return m_densityHistory.size(); }
    std::shared_ptr<const MeshResult> densityResultAt(size_t k) const;
    /*  The design after iteration k as a surface -- indexed triangles, counter-clockwise from outside -- where its density is
     *  above `level`: the part as it was then, for the result card to draw when that step is shown (made from the density at the
     *  nodes, exactly as the optimised part is cut from it, in a few tens of milliseconds)  */
    bool densitySurface(size_t k, double level, std::vector<float>& verts, std::vector<uint32_t>& tris) const;
    /*  How many separate pieces the optimised design is in when it is cut at the density `threshold` (the density at
     *  the nodes, as the optimised part is cut from it): the part is where it is above the level, and a link that is only
     *  just there -- less than `margin` above it -- is too thin to count.  Specks under 2 % of the body are not
     *  counted (0 when there is no result)  */
    int pieces(double threshold, double margin = 0.0) const;
    /*  The density at the nodes above which the optimised part has the volume the design has: the density is partly grey, so a cut at
     *  one half keeps less or more than the design is, and the volume above a level is counted exactly (the field is linear in each
     *  tetrahedron), not on a grid that cannot see a wall thinner than its spacing.  0.5 when there is no result  */
    double cutThreshold() const;
    /*  The highest density that the design has in an element outside the part (see TopOpt::origin) that lies on the edge of the design
     *  space: how far the design wants to reach, when it is as far as the space goes.  0 when the space is the part  */
    double frontierDensity() const { return m_frontier; }
    /*  After optimize with TopOpt::stopAtEdge: the run stopped because the design reached the edge of its space.  resumeState() is what a run in
     *  a larger space goes on with (TopOpt::resume) -- empty when the run went to its end --, and designResult() the design it stopped at, the
     *  variable of the optimisation (not the density it is projected to) as a field on the mesh, for meshFieldTree(., 0): TopOpt::startField  */
    const std::vector<double>& resumeState() const { return m_resume; }
    std::shared_ptr<const MeshResult> designResult() const { return m_designResult; }
    /*  The surface of the optimised design where its density at the nodes is `level`: indexed triangles, counter-clockwise from outside,
     *  as densitySurface() has them for an iteration -- of the design that is given back  */
    bool finalSurface(double level, std::vector<float>& verts, std::vector<uint32_t>& tris) const;
    std::shared_ptr<const MeshResult> mode(int i) const
    {
        return (i >= 0 && size_t(i) < m_modes.size()) ? m_modes[size_t(i)] : nullptr;
    }
    uint64_t hash() const { return m_hash; }
    bool solve(int maxIterations, double tolerance, std::string& error,
               const std::atomic<bool>* cancel = nullptr);

    std::shared_ptr<const MeshResult> result() const { return m_result; }
    std::shared_ptr<const TetMesh> mesh() const { return m_mesh; }

private:
    friend struct ResultIO;
    bool solveQuad(int maxIterations, double tolerance, std::string& error, const std::atomic<bool>* cancel);
    Tree m_shape;
    Eigen::Vector3d m_lo, m_hi;
    double m_h, m_E, m_nu;

    struct Support { Tree region; bool fix[3]; int loadCase = -1; Eigen::Vector3d displacement = Eigen::Vector3d::Zero(); };
    struct Force { Tree region; Eigen::Vector3d total; int loadCase; Tree profile = Tree::invalid(); };
    std::vector<Support> m_supports;
    std::vector<Force> m_forces;
    Tree m_conditionBody = Tree::invalid();     // (see setConditionBody)
    double m_conditionSlack = 0;
    bool m_conditionSurface = false;            // (see setConditionSurface)
    Eigen::Vector3d m_gravity = Eigen::Vector3d::Zero();
    double m_density = 0;
    Tree m_temperature = Tree::invalid();
    double m_alpha = 0, m_reference = 0;
    Tree m_stiffnessField = Tree::invalid(), m_densityField = Tree::invalid(), m_expansionField = Tree::invalid();

    // Prepared
    bool m_prepared = false;
    std::shared_ptr<TetMesh> m_mesh;            // (with 10-node elements: their nodes cut into linear tetrahedra, what the results are on)
    int m_order = 1;
    std::shared_ptr<tet::QuadMesh> m_quad;      // (the 10-node elements, when m_order is 2)
    std::vector<unsigned char> m_fixedDof;      // per DOF (3 * vertex + axis)
    std::vector<double> m_prescribed;           // per DOF: the displacement a held direction is held at (empty when every support holds at 0)
    std::vector<double> m_force;                // per DOF: the loads, gravity and thermal expansion together
    std::vector<std::vector<double>> m_caseForce;   // per load case, when there are several
    std::vector<std::vector<unsigned char>> m_caseFixed;    // per load case: the fixed DOFs of its own, when the cases have supports of their own
    std::vector<TopOpt::Mirror> m_mirrorsUsed;
    std::vector<double> m_thermalStrain;        // per tetrahedron: alpha (T - reference)
    std::vector<double> m_scale;                // per tetrahedron: its Young's modulus over E (empty without a stiffness field)
    std::vector<double> m_elementDensity;       // per tetrahedron (empty without a density field)
    int m_looseElements = 0, m_fixedNodes = 0, m_loadedNodes = 0;
    Eigen::Vector3d m_totalLoad = Eigen::Vector3d::Zero();
    uint64_t m_hash = 0;
    std::shared_ptr<MeshResult> m_result;

    bool m_noLoads = false;                     // prepare without loads (modal)
    std::vector<char> m_modalCase;              // per load case: it is a modal analysis (see markModalCase)
    std::vector<float> m_topDensity;            // per tetrahedron
    std::vector<double> m_history;
    std::vector<std::vector<double>> m_baseHistory, m_outputHistory;
    std::shared_ptr<MeshResult> m_densityResult;
    std::vector<std::vector<float>> m_densityHistory;   // per iteration: the density at the nodes
    double m_designVolume = 0;                  // the volume the design given back has (the density integrated over the elements)
    double m_frontier = 0;                      // see frontierDensity()
    std::vector<double> m_resume;               // see resumeState()
    std::shared_ptr<MeshResult> m_designResult; // see designResult() (not kept in a file: it serves the run that goes on from this one)
    std::vector<double> m_frequencies;
    std::vector<std::shared_ptr<MeshResult>> m_modes;
};

/*  The optimisation a C-API handle (libfive_tetfea*, as a pointer) holds, shared: what keeps it alive while a viewer still draws
 *  the steps of its result  */
std::shared_ptr<TetProblem> tetProblemOf(void* handle);

}   // namespace fea
}   // namespace libfive
