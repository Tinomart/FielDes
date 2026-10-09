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

class TetProblem
{
public:
    TetProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi, double h, double E, double nu);

    /*  A support.  loadCase >= 0: it holds the part only in that load case (topology optimization against several sets of
     *  boundary conditions, each with its own supports); -1: in every case  */
    void addSupport(const Tree& region, bool x, bool y, bool z, int loadCase = -1);
    /*  A load in a load case (0, 1, ...): topology optimization makes the part stiff for each
     *  case on its own; a static analysis applies every load together  */
    void addForce(const Tree& region, Eigen::Vector3d total, int loadCase = 0);
    /*  The same, with the total spread over the region's surface in proportion to a field (taken at the centres of the boundary
     *  triangles; not negative) instead of evenly by area: a pressure that is not the same everywhere  */
    void addForceProfile(const Tree& region, Eigen::Vector3d total, int loadCase, const Tree& profile);

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

    /*  Topology optimization (SIMP with a density filter and optimality-criteria updates): finds
     *  the stiffest distribution of material that uses volumeFraction of the part, for the
     *  supports and loads given.  Elements inside `keep` regions (and those holding supported or
     *  loaded nodes) stay solid, elements inside `avoid` regions stay empty.  */
    struct TopOpt
    {
        double volumeFraction = 0.3;
        double penalty = 3.0;
        double filterRadius = 0;        // 0: 1.5 element sizes
        double minStiffness = 1e-3;     // of the full material, for "void"
        int extrude = -1;               // 0 / 1 / 2: the design is constant along x / y / z
        int iterations = 100;
        int solverIterations = 20000;
        double tolerance = 1e-5;
        std::vector<Tree> keep, avoid;
        /*  Set when the design space is bigger than the part (the part grown outwards): the part itself.  The volume
         *  fraction is then of the part's volume, the design starts as the part (the rest of the space nearly empty) and
         *  material may be put outside it where that makes the design stiffer  */
        Tree origin = Tree::invalid();
        /*  How crisp the design is: its density is projected towards 0 and 1 with a step that grows steeper to this number
         *  (see optimize); 1: not at all, the density the filter makes  */
        double sharpness = 16.0;
        /*  Mirror symmetry: the design is kept symmetric about these planes (axis 0, 1, 2 = x, y, z at the coordinate `at`; atCentre: the
         *  plane through the middle of the part's extent along that axis).  symmetryAuto: look for the planes the part, its supports,
         *  its loads and its keep / avoid regions are all symmetric about, and keep the design symmetric about them -- a symmetric
         *  problem otherwise breaks its symmetry (the mesh of a symmetric part is never exactly symmetric, and a small difference
         *  grows: one of two redundant members takes the other's material)  */
        struct Mirror { int axis = 2; double at = 0; bool atCentre = true; };
        std::vector<Mirror> mirrors;
        bool symmetryAuto = false;
    };
    bool optimize(const TopOpt& settings, std::string& error, const std::atomic<bool>* cancel = nullptr);
    /*  After optimize: the planes the design was kept symmetric about (found or asked for), `at` as it was resolved  */
    const std::vector<TopOpt::Mirror>& mirrorsUsed() const { return m_mirrorsUsed; }
    /*  After optimize: the compliance at each iteration, and the density (0..1) as a field on the
     *  mesh (each tetrahedron's density averaged at the nodes by volume) for meshFieldTree(., 0)  */
    const std::vector<double>& complianceHistory() const { return m_history; }
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
    Tree m_shape;
    Eigen::Vector3d m_lo, m_hi;
    double m_h, m_E, m_nu;

    struct Support { Tree region; bool fix[3]; int loadCase = -1; };
    struct Force { Tree region; Eigen::Vector3d total; int loadCase; Tree profile = Tree::invalid(); };
    std::vector<Support> m_supports;
    std::vector<Force> m_forces;
    Eigen::Vector3d m_gravity = Eigen::Vector3d::Zero();
    double m_density = 0;
    Tree m_temperature = Tree::invalid();
    double m_alpha = 0, m_reference = 0;
    Tree m_stiffnessField = Tree::invalid(), m_densityField = Tree::invalid(), m_expansionField = Tree::invalid();

    // Prepared
    bool m_prepared = false;
    std::shared_ptr<TetMesh> m_mesh;
    std::vector<unsigned char> m_fixedDof;      // per DOF (3 * vertex + axis)
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
    std::vector<float> m_topDensity;            // per tetrahedron
    std::vector<double> m_history;
    std::shared_ptr<MeshResult> m_densityResult;
    std::vector<std::vector<float>> m_densityHistory;   // per iteration: the density at the nodes
    std::vector<double> m_frequencies;
    std::vector<std::shared_ptr<MeshResult>> m_modes;
};

/*  The optimisation a C-API handle (libfive_tetfea*, as a pointer) holds, shared: what keeps it alive while a viewer still draws
 *  the steps of its result  */
std::shared_ptr<TetProblem> tetProblemOf(void* handle);

}   // namespace fea
}   // namespace libfive
