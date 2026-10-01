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

    void addSupport(const Tree& region, bool x, bool y, bool z);
    /*  A load in a load case (0, 1, ...): topology optimization makes the part stiff for each
     *  case on its own; a static analysis applies every load together  */
    void addForce(const Tree& region, Eigen::Vector3d total, int loadCase = 0);
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
    bool modal(int count, double density, int maxIterations, double tolerance, std::string& error);
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
        double move = 0.2;
        double minStiffness = 1e-3;     // of the full material, for "void"
        int extrude = -1;               // 0 / 1 / 2: the design is constant along x / y / z
        int iterations = 60;
        int solverIterations = 20000;
        double tolerance = 1e-5;
        std::vector<Tree> keep, avoid;
    };
    bool optimize(const TopOpt& settings, std::string& error, const std::atomic<bool>* cancel = nullptr);
    /*  After optimize: the compliance at each iteration, and the density (0..1) as a field on the
     *  mesh (each tetrahedron's density averaged at the nodes by volume) for meshFieldTree(., 0)  */
    const std::vector<double>& complianceHistory() const { return m_history; }
    std::shared_ptr<const MeshResult> densityResult() const { return m_densityResult; }
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
    Tree m_shape;
    Eigen::Vector3d m_lo, m_hi;
    double m_h, m_E, m_nu;

    struct Support { Tree region; bool fix[3]; };
    struct Force { Tree region; Eigen::Vector3d total; int loadCase; };
    std::vector<Support> m_supports;
    std::vector<Force> m_forces;
    Eigen::Vector3d m_gravity = Eigen::Vector3d::Zero();
    double m_density = 0;
    Tree m_temperature = Tree::invalid();
    double m_alpha = 0, m_reference = 0;

    // Prepared
    bool m_prepared = false;
    std::shared_ptr<TetMesh> m_mesh;
    std::vector<unsigned char> m_fixedDof;      // per DOF (3 * vertex + axis)
    std::vector<double> m_force;                // per DOF: the loads, gravity and thermal expansion together
    std::vector<std::vector<double>> m_caseForce;   // per load case, when there are several
    std::vector<double> m_thermalStrain;        // per tetrahedron: alpha (T - reference)
    int m_looseElements = 0, m_fixedNodes = 0, m_loadedNodes = 0;
    Eigen::Vector3d m_totalLoad = Eigen::Vector3d::Zero();
    uint64_t m_hash = 0;
    std::shared_ptr<MeshResult> m_result;

    bool m_noLoads = false;                     // prepare without loads (modal)
    std::vector<float> m_topDensity;            // per tetrahedron
    std::vector<double> m_history;
    std::shared_ptr<MeshResult> m_densityResult;
    std::vector<double> m_frequencies;
    std::vector<std::shared_ptr<MeshResult>> m_modes;
};

}   // namespace fea
}   // namespace libfive
