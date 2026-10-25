/*
libfive: a CAD kernel for modeling with implicit functions

Steady-state heat conduction in implicit shapes, on the same voxel grid as
the static analysis (fea.hpp): 8-node trilinear hexahedra, each element's
conductivity scaled by the fraction of it inside the shape.  Boundary
conditions are given as regions (other shapes):
  - a fixed temperature at the part's nodes inside a region,
  - a heat input (a total power) spread over the part's surface nodes
    inside a region,
  - a heat generation (a total power) spread over the part's volume inside
    a region,
  - convection to an ambient temperature over the part's exposed surface
    inside a region (a heat transfer coefficient).
K T = q is solved matrix-free by Jacobi-preconditioned conjugate gradients.

Results (temperature, heat flux) live on the grid's nodes in an fea::Result
(fields 0 temperature, 1 heat flux magnitude, 2-4 its x / y / z parts) and
become libfive Trees through fea::fieldTree.

Units: whatever the inputs use consistently -- e.g. mm, W, degrees C (or K):
conductivity in W / (mm K), heat transfer coefficients in W / (mm^2 K), heat
flux in W / mm^2.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/tree/tree.hpp"
#include "libfive/fea/fea.hpp"

namespace libfive {
namespace fea {

class ThermalProblem
{
public:
    enum Field { TEMPERATURE = 0, HEAT_FLUX = 1, QX = 2, QY = 3, QZ = 4 };

    ThermalProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi,
                   double h, double conductivity);
    ~ThermalProblem();

    /*  What a cell of the grid is made of (Element in fea.hpp; the default,
     *  six tetrahedra, or one trilinear hexahedron); before prepare  */
    void setElement(Element e) { m_element = e; }
    Element element() const { return m_element; }

    void addTemperature(const Tree& region, double value);
    void addHeat(const Tree& region, double power);
    void addGeneration(const Tree& region, double power);
    void addConvection(const Tree& region, double coefficient, double ambient);

    /*  Voxelizes and resolves the boundary conditions (solve does it when
     *  needed); false with a message if they can't be  */
    bool prepare(std::string& error);
    /*  A hash of the prepared problem (grid, fill, boundary conditions)  */
    uint64_t hash() const;

    /*  Solves; false with a message if the problem can't be solved as given  */
    bool solve(int maxIterations, double tolerance, std::string& error);

    std::shared_ptr<const Result> result() const { return m_result; }

    // After solve: statistics
    int elements = 0, nodes = 0, iterations = 0;
    double residual = 0, seconds = 0;
    double heatIn = 0;              // the heat inputs and generation, W
    double heatOutFixed = 0;        // through the fixed-temperature regions
    double heatOutConvection = 0;   // to the ambient

private:
    friend struct ResultIO;
    struct Prepared;
    /*  Solves K(mult, conv) x = b for the free nodes (x: the start, and
     *  the solution); the elements' conductivities are scaled by mult, conv
     *  is each unknown's convection conductance (h A).
     *  progress(iteration, relative residual) every 16 iterations.  */
    bool pcg(const std::vector<double>& mult, const std::vector<double>& conv,
             const std::vector<double>& b, std::vector<double>& x,
             int maxIterations, double tolerance, int& its, double& rel,
             std::string& error, const std::function<void(int, double)>& progress);
    void conduct(const std::vector<double>& mult, const std::vector<double>& conv,
                 const std::vector<double>& x, std::vector<double>& y) const;

    Tree m_shape;
    Eigen::Vector3d m_lo;
    double m_h, m_k;
    Element m_element = Element::Tet;
    int m_ex = 0, m_ey = 0, m_ez = 0;

    struct Temperature { Tree region; double value; };
    struct Heat { Tree region; double power; bool volume; };
    struct Convection { Tree region; double coefficient, ambient; };
    std::vector<Temperature> m_temperatures;
    std::vector<Heat> m_heats;
    std::vector<Convection> m_convections;

    std::unique_ptr<Prepared> m_prep;
    std::shared_ptr<Result> m_result;
};

}   // namespace fea
}   // namespace libfive
