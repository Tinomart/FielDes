/*
libfive: a CAD kernel for modeling with implicit functions

Steady-state heat conduction in an implicit shape on a body-fitted tetrahedral
mesh (see tetmesh.hpp and thermal.hpp): linear tetrahedra with a conduction
matrix assembled from each element's own geometry, solved with a
Jacobi-preconditioned conjugate gradient method.

Boundary conditions are regions (other shapes):
  addTemperature   the mesh's nodes inside the region are held at a temperature
  addHeat          a total power spread over the boundary triangles inside it, by area
  addGeneration    a total power generated in the tetrahedra whose centre is inside it, by volume
  addConvection    the boundary triangles inside it exchange heat with an ambient
                   temperature through a coefficient h (W / (mm^2 K))

Results are a MeshResult whose fields are TEMPERATURE, HEAT_FLUX, QX, QY, QZ
(fields 0 to 4, as in the voxel thermal analysis): nodal temperatures, and the heat
flux -k grad T of each element (constant in it) averaged at the nodes by volume.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/fea/density_design.hpp"
#include "libfive/fea/tetfea.hpp"

namespace libfive {
namespace fea {

class TetThermalProblem
{
public:
    TetThermalProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi, double h, double conductivity);

    void addTemperature(const Tree& region, double value);
    void addHeat(const Tree& region, double watts);
    void addGeneration(const Tree& region, double watts);
    void addConvection(const Tree& region, double coefficient, double ambient);

    /*  The same with fields (a Tree that is not valid says: the number).  Each is taken where it acts:
     *    the conductivity (W / (mm K)) at the centre of every tetrahedron -- the one given to the constructor is then only a
     *      number to start from;
     *    a held temperature at the nodes inside its region; the transfer coefficient h and the ambient temperature at the
     *      centre of every boundary triangle of a convection;
     *    a profile weights how a heat input or generation spreads over its region: by area times the profile (by volume times
     *      it for a generation, by it alone over the nodes of a region that holds no surface), not evenly  */
    void setConductivityField(const Tree& k);
    void addTemperatureField(const Tree& region, double value, const Tree& field);
    void addHeatProfile(const Tree& region, double watts, const Tree& profile);
    void addGenerationProfile(const Tree& region, double watts, const Tree& profile);
    void addConvectionFields(const Tree& region, double coefficient, const Tree& coefficientField, double ambient,
                             const Tree& ambientField);

    /*  Meshes the part and resolves the boundary conditions; false with a message if
     *  the problem can't be solved as given  */
    bool prepare(std::string& error);
    uint64_t hash() const { return m_hash; }
    bool solve(int maxIterations, double tolerance, std::string& error,
               const std::atomic<bool>* cancel = nullptr);

    std::shared_ptr<const MeshResult> result() const { return m_result; }
    std::shared_ptr<const TetMesh> mesh() const { return m_mesh; }

    /*  Topology optimization: one density to each tetrahedron of the part (the design space), the conductivity of an element the material's times
     *  kmin + (1 - kmin) rho^p, on the design core every analysis shares (density_design.hpp).  The held temperatures, the heat inputs and the
     *  convecting faces of the part's own surface act as in the analysis; the elements that hold a temperature or bring heat in stay solid, and
     *  material that nothing joins to a held temperature or a convecting face is taken out.  Elements inside `keep` regions stay solid, inside
     *  `avoid` regions empty.  The measures are numbers of the temperature, each with its sensitivity to the density of every element (an adjoint
     *  solve): the peak temperature and the lowest, the peak heat flux, the temperature the heat comes in at, the temperature at a point  */
    enum DensityMeasure { MEASURE_VOLUME = 0, MEASURE_MAX_TEMPERATURE = 1, MEASURE_MIN_TEMPERATURE = 2, MEASURE_MAX_HEAT_FLUX = 3,
                          MEASURE_INPUT_TEMPERATURE = 4, MEASURE_TEMPERATURE_AT = 5 };
    struct DensityMeasureSpec
    {
        int kind = MEASURE_VOLUME;
        double a = 0, b = 0, c = 0;     // MEASURE_TEMPERATURE_AT: the point
    };
    struct DensityOpt : topopt::DesignSettings
    {
        std::vector<DensityMeasureSpec> measures;
        double penalty = 3.0;
        double minConductivity = 1e-3;      // of the full material, for "void"
        int solverIterations = 50000;
        double tolerance = 1e-6;
    };
    bool optimizeDensity(const DensityOpt& settings, std::string& error, const std::atomic<bool>* cancel = nullptr);
    /*  After optimizeDensity: the planes the design was kept symmetric about; the objective, every measure and every output of the program at each
     *  kept iteration; the density (0..1) as a field on the mesh, and after each iteration; the density at the nodes above which the design's volume
     *  is what the design has (the cut), and in how many pieces the part cut there is  */
    const std::vector<topopt::Mirror>& mirrorsUsed() const { return m_mirrorsUsed; }
    const std::vector<double>& designHistory() const { return m_history; }
    const std::vector<std::vector<double>>& baseHistory() const { return m_baseHistory; }
    const std::vector<std::vector<double>>& outputHistory() const { return m_outputHistory; }
    std::shared_ptr<const MeshResult> densityResult() const { return m_densityResult; }
    size_t densityHistoryCount() const { return m_densityHistory.size(); }
    std::shared_ptr<const MeshResult> densityResultAt(size_t k) const;
    double cutThreshold() const;
    int pieces(double threshold, double margin = 0.0) const;
    int stepsBack() const { return m_stepsBack; }
    double designVolume() const { return m_designVolume; }
    /*  After solve: the temperature the heat comes in at -- the free nodes' temperature weighted by the heat that comes in at each  */
    double inputTemperature() const { return m_inputTemperature; }

private:
    friend struct ResultIO;
    Tree m_shape;
    Eigen::Vector3d m_lo, m_hi;
    double m_h, m_k;

    struct Temperature { Tree region; double value; Tree field = Tree::invalid(); };
    struct Heat { Tree region; double watts; bool volume; Tree profile = Tree::invalid(); };
    struct Convection { Tree region; double coefficient, ambient; Tree coefficientField = Tree::invalid(), ambientField = Tree::invalid(); };
    Tree m_conductivityField = Tree::invalid();
    std::vector<double> m_elementK;             // per tetrahedron: its conductivity (empty without a conductivity field)
    std::vector<Temperature> m_temperatures;
    std::vector<Heat> m_heats;
    std::vector<Convection> m_convections;

    // Prepared
    bool m_prepared = false;
    std::shared_ptr<TetMesh> m_mesh;
    std::vector<unsigned char> m_fixed;         // per vertex: held at m_fixedValue
    std::vector<double> m_fixedValue;
    std::vector<double> m_source;               // per vertex: heat in (W)
    std::vector<double> m_sourceFixed;          // ... of it what a heat input puts in over a surface or at a point (a design holds the elements that carry it; a generation is a condition of the design space)
    struct ConvTri { int a, b, c; double area, h, ambient; };
    std::vector<ConvTri> m_conv;
    double m_heatIn = 0;
    int m_looseElements = 0;
    uint64_t m_hash = 0;
    std::shared_ptr<MeshResult> m_result;
    double m_inputTemperature = 0;

    // The optimisation
    std::vector<topopt::Mirror> m_mirrorsUsed;
    std::vector<double> m_history;
    std::vector<std::vector<double>> m_baseHistory, m_outputHistory;
    std::vector<std::vector<float>> m_densityHistory;   // per iteration: the density at the nodes
    std::vector<float> m_topDensity;                    // per tetrahedron
    std::shared_ptr<MeshResult> m_densityResult;
    double m_designVolume = 0;
    int m_stepsBack = 0;
};

}   // namespace fea
}   // namespace libfive
