/*
libfive: a CAD kernel for modeling with implicit functions

Static linear-elastic finite element analysis of implicit shapes.

The shape is voxelized into a regular grid of 8-node (trilinear) hexahedral
elements; each element's stiffness is scaled by the fraction of it that
lies inside the shape (sampled from the shape's own field), so boundaries
are not a pure staircase.  Supports and loads are given as regions (other
shapes): a support fixes every grid node of the part inside its region, a
load spreads a total force over the part's surface nodes inside its region.
K u = f is solved matrix-free with a Jacobi-preconditioned conjugate
gradient method, one element stiffness matrix shared by all elements.

Results (displacements, von Mises stress, ...) live on the grid's nodes and
are exposed as libfive Trees (an interpolating Oracle leaf), so they can be
displayed, probed and combined with other shapes mathematically.

Units are whatever the inputs use consistently -- e.g. mm, N and MPa
(E in MPa gives stresses in MPa and displacements in mm).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/tree/tree.hpp"
#include "libfive/tree/content_key.hpp"

namespace libfive {
namespace fea {

/*  Results on the grid's nodes (node (i, j, k) at lo + h * (i, j, k))  */
struct Result
{
    // Which result this is, never given out twice (the key of its fields: an address can be reused
    // once a result is freed, and then two different results would share a key)
    uint64_t serial = nextContentSerial();
    Eigen::Vector3d lo = Eigen::Vector3d::Zero();
    double h = 1;
    int nx = 0, ny = 0, nz = 0;             // node counts
    // The result was solved on linear tetrahedra (six to a cell, see Element):
    // inside a cell it is linear in the tetrahedron that holds the point, not
    // trilinear in the cell
    bool tetrahedra = false;

    enum Field { VON_MISES = 0, DISPLACEMENT, UX, UY, UZ,
                 SXX, SYY, SZZ, SXY, SYZ, SZX, MAX_PRINCIPAL, MIN_PRINCIPAL,
                 STRAIN_ENERGY, FIELD_COUNT };
    std::vector<float> fields[FIELD_COUNT];  // nx * ny * nz each
    float minValue[FIELD_COUNT] = {0};       // over the part's nodes
    float maxValue[FIELD_COUNT] = {0};

    size_t index(int i, int j, int k) const { return (size_t(k) * ny + j) * nx + i; }

    // Statistics
    int elements = 0, nodes = 0, dofs = 0, fixedNodes = 0, loadedNodes = 0;
    int looseElements = 0;                  // material not connected to any support: left out
    int iterations = 0;
    double residual = 0, seconds = 0;
    double volume = 0, compliance = 0;
    Eigen::Vector3d totalLoad = Eigen::Vector3d::Zero();
    Eigen::Vector3d reaction = Eigen::Vector3d::Zero();
};

// What a cell of the voxel grid is made of.
//   Tet       six linear tetrahedra (every cell cut the same way along its main
//             diagonal, so neighbouring cells match); the default.  Constant
//             strain in each: stiff in bending -- refine, or use Hex
//   Hex       one trilinear hexahedron with incompatible (Wilson-Taylor) modes:
//             bends without spurious shear and represents pure bending exactly
//   HexBasic  the plain trilinear hexahedron: shear-locks in bending
enum class Element { Tet = 0, Hex = 1, HexBasic = 2 };

class StaticProblem
{
public:
    /*  The part, the grid (covering lo..hi with elements of size h) and the
     *  material (Young's modulus, Poisson's ratio)  */
    StaticProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi,
                  double h, double E, double nu);

    /*  Fix the part's nodes inside `region` (per axis)  */
    void setElement(Element e) { m_element = e; m_prepared = false; }
    Element element() const { return m_element; }

    void addSupport(const Tree& region, bool x, bool y, bool z);

    /*  A total force spread evenly over the part's surface nodes inside
     *  `region` (if the region only holds interior nodes, over those).
     *  loadCase: for topology optimization, loads in different cases act
     *  one case at a time (each case solved on its own, the part made stiff
     *  for all of them: the sum of their compliances is minimised); a
     *  static analysis applies every load together.  Gravity acts in every
     *  case.  */
    void addForce(const Tree& region, Eigen::Vector3d total, int loadCase = 0);
    int loadCases() const;

    /*  A body load: acceleration g (e.g. (0, 0, -9810) mm/s^2) on material
     *  of the given density (e.g. 7.85e-9 t/mm^3 for steel)  */
    void setGravity(Eigen::Vector3d g, double density);

    /*  Thermal expansion: the part at the temperature field `temperature`
     *  (a Tree, e.g. a thermal analysis' result, or a constant), expanding
     *  by alpha (1/K) per degree above `reference` -- the strain alpha
     *  (T - reference) in every direction; where it is held, it is
     *  stressed.  Static analysis only (not topology optimization).  */
    void setThermal(const Tree& temperature, double alpha, double reference);

    /*  Voxelizes the part and resolves the supports and loads (fast);
     *  false with a message if the problem can't be solved as given  */
    bool prepare(std::string& error);

    /*  A fingerprint of the prepared problem (for caching solves)  */
    uint64_t hash() const { return m_hash; }

    /*  Solves; false with a message on failure.  `cancel` (optional) aborts  */
    bool solve(int maxIterations, double tolerance, std::string& error,
               const std::atomic<bool>* cancel=nullptr);

    std::shared_ptr<const Result> result() const { return m_result; }

    /*  Modal analysis: the part's `count` lowest natural frequencies (Hz,
     *  with mm, t / mm^3 and MPa) and their mode shapes, held by the
     *  supports -- K phi = omega^2 M phi with a lumped mass matrix (the
     *  material's density), by subspace iteration on the static solver.
     *  No loads are needed.  Each mode's shape is a Result (fields UX, UY,
     *  UZ, DISPLACEMENT; scaled to a largest movement of 1).  */
    bool modal(int count, double density, int maxIterations, double tolerance,
               std::string& error);
    const std::vector<double>& frequencies() const { return m_frequencies; }
    std::shared_ptr<const Result> mode(int i) const
    {
        return (i >= 0 && size_t(i) < m_modes.size()) ? m_modes[size_t(i)] : nullptr;
    }

    /*  Topology optimization (SIMP with a density filter and optimality-
     *  criteria updates): finds the stiffest distribution of material that
     *  uses volumeFraction of the part, for the supports and loads given.
     *  Elements inside `keep` regions (and those holding supported or loaded
     *  nodes) stay solid, elements inside `avoid` regions stay empty.  */
    struct TopOpt
    {
        double volumeFraction = 0.3;
        double penalty = 3.0;
        double filterRadius = 0;        // 0: 1.5 element sizes
        double move = 0.2;
        double minStiffness = 1e-3;     // of the full material, for "void"
        int extrude = -1;               // 0 / 1 / 2: the design is constant along
                                        //   x / y / z (extruded, or machinable
                                        //   from one side through)
        int iterations = 60;
        int solverIterations = 20000;
        double tolerance = 1e-5;
        std::vector<Tree> keep, avoid;
    };
    bool optimize(const TopOpt& settings, std::string& error,
                  const std::atomic<bool>* cancel=nullptr);
    /*  After optimize: the density (0..1) of each element (all elements, i
     *  fastest), the compliance at each iteration, and the density as a
     *  Result on the element centres (field 0) for fieldTree  */
    const std::vector<float>& density() const { return m_topDensity; }
    const std::vector<double>& complianceHistory() const { return m_history; }
    std::shared_ptr<const Result> densityResult() const { return m_densityResult; }

    /*  Grid size (elements along each axis)  */
    int ex() const { return m_ex; }
    int ey() const { return m_ey; }
    int ez() const { return m_ez; }
    int activeElements() const { return int(m_active.size()); }

    /*  What a cell of the grid is made of: six tetrahedra (Tet) or one
     *  hexahedron  */
    int elementsPerCell() const { return m_element == Element::Tet ? 6 : 1; }
    /*  After solve(): a result field's value in each element the solver used,
     *  as that element has it: the stress of a tetrahedron (constant in it) or
     *  of a hexahedron at its centre, from the solved displacements -- not
     *  the nodal averages of the Result -- and for the displacement fields the
     *  value at the element's centre.  The cells with a fraction > 0 in order
     *  (i fastest), elementsPerCell() values each (a tetrahedron t of a cell
     *  is the one whose corners are TetCell's, see fea.cpp).  False before
     *  a solve.  */
    bool elementValues(int field, std::vector<float>& out) const;

    /*  The grid's corner and element size, and how much of each element
     *  (i fastest, then j, then k) lies inside the part: 0 = no element  */
    const Eigen::Vector3d& gridLower() const { return m_lo; }
    double elementSize() const { return m_h; }
    const std::vector<float>& elementFractions() const { return m_fraction; }

protected:
    Tree m_shape;
    Eigen::Vector3d m_lo;
    double m_h, m_E, m_nu;
    Element m_element = Element::Tet;
    int m_ex = 0, m_ey = 0, m_ez = 0;       // elements per axis

    struct Support { Tree region; bool fix[3]; };
    struct Force { Tree region; Eigen::Vector3d total; int loadCase; };
    std::vector<Support> m_supports;
    std::vector<Force> m_forces;
    Eigen::Vector3d m_gravity = Eigen::Vector3d::Zero();
    double m_density = 0;
    Tree m_temperature = Tree::invalid();
    double m_alpha = 0, m_reference = 0;
    std::vector<double> m_thermalStrain;    // per active element: alpha (T - reference)

    // Prepared data
    bool m_prepared = false;
    std::vector<float> m_fraction;          // per element (all), 0 = empty
    std::vector<int> m_active;              // active element indices
    std::vector<int> m_dofOfNode;           // node -> first DOF, -1 if unused
    std::vector<char> m_fixed;              // per DOF
    std::vector<double> m_force;            // per DOF (every load together)
    std::vector<std::vector<double>> m_caseForce;   // per load case, when there are several
    int m_dofs = 0;
    int m_fixedNodes = 0, m_loadedNodes = 0;
    int m_looseElements = 0;
    uint64_t m_hash = 0;

    std::shared_ptr<Result> m_result;

    // Modal analysis
    bool m_noLoads = false;                 // prepare without loads (modal)
    std::vector<double> m_frequencies;
    std::vector<std::shared_ptr<Result>> m_modes;

    // Topology optimization
    std::vector<double> m_elemScale;        // per active element, empty = 1
    std::vector<double> m_lastU;            // warm start (quick solves)
    bool m_quick = false;                   // skip the result fields
    std::vector<float> m_topDensity;
    std::vector<double> m_history;
    std::shared_ptr<Result> m_densityResult;

    size_t elem(int i, int j, int k) const { return (size_t(k) * m_ey + j) * m_ex + i; }
    size_t node(int i, int j, int k) const { return (size_t(k) * (m_ey + 1) + j) * (m_ex + 1) + i; }
};

/*  A result field as a Tree: trilinear interpolation of the nodal values
 *  (outside the grid, the nearest grid value)  */
Tree fieldTree(std::shared_ptr<const Result> result, int field);

/*  (Shared by the solvers: the static solver's thread pool.)  fn(begin,
 *  end) over [0, n) on all cores; the sum of fn(begin, end) over [0, n)  */
void parallelRange(size_t n, const std::function<void(size_t, size_t)>& fn, size_t minChunk=256);
double parallelTotal(size_t n, const std::function<double(size_t, size_t)>& fn);

}   // namespace fea
}   // namespace libfive
