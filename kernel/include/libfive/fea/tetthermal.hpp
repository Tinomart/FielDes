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

    /*  Meshes the part and resolves the boundary conditions; false with a message if
     *  the problem can't be solved as given  */
    bool prepare(std::string& error);
    uint64_t hash() const { return m_hash; }
    bool solve(int maxIterations, double tolerance, std::string& error,
               const std::atomic<bool>* cancel = nullptr);

    std::shared_ptr<const MeshResult> result() const { return m_result; }
    std::shared_ptr<const TetMesh> mesh() const { return m_mesh; }

private:
    Tree m_shape;
    Eigen::Vector3d m_lo, m_hi;
    double m_h, m_k;

    struct Temperature { Tree region; double value; };
    struct Heat { Tree region; double watts; bool volume; };
    struct Convection { Tree region; double coefficient, ambient; };
    std::vector<Temperature> m_temperatures;
    std::vector<Heat> m_heats;
    std::vector<Convection> m_convections;

    // Prepared
    bool m_prepared = false;
    std::shared_ptr<TetMesh> m_mesh;
    std::vector<unsigned char> m_fixed;         // per vertex: held at m_fixedValue
    std::vector<double> m_fixedValue;
    std::vector<double> m_source;               // per vertex: heat in (W)
    struct ConvTri { int a, b, c; double area, h, ambient; };
    std::vector<ConvTri> m_conv;
    double m_heatIn = 0;
    int m_looseElements = 0;
    uint64_t m_hash = 0;
    std::shared_ptr<MeshResult> m_result;
};

}   // namespace fea
}   // namespace libfive
