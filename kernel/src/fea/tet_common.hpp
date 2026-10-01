/*
libfive: a CAD kernel for modeling with implicit functions

What the analyses on the body-fitted tetrahedral mesh share: the linear
tetrahedron's geometry, a sparse matrix of 3 x 3 blocks assembled from the
elements, a preconditioned conjugate gradient solver, and a block eigensolver
(LOBPCG) for vibration modes.

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

#include "libfive/fea/fea.hpp"
#include "libfive/fea/tetmesh.hpp"

namespace libfive {
namespace fea {
namespace tet {

using Vec3 = Eigen::Vector3d;
using Mat6 = Eigen::Matrix<double, 6, 6>;
using Vec6 = Eigen::Matrix<double, 6, 1>;
using Mat6x12 = Eigen::Matrix<double, 6, 12>;

Mat6 elasticity(double E, double nu);

// A tetrahedron's gradients of its four linear shape functions, and its volume
struct TetGeom
{
    double g[4][3];
    double vol;
};
TetGeom geometryOf(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d);

// The strain-displacement matrix (engineering shear strains), and the strain of nodal displacements ue
Mat6x12 strainMatrix(const TetGeom& t);
Vec6 strainOf(const TetGeom& t, const double ue[12]);

uint64_t fnv(uint64_t h, const void* data, size_t bytes);

// A sparse symmetric matrix of 3 x 3 blocks, one block row per vertex
struct Bsr
{
    size_t n = 0;                               // block rows
    std::vector<uint32_t> rowPtr, col;
    std::vector<double> val;                    // 9 per block, row-major
    void matvec(const double* x, double* y) const;
    size_t diag(size_t row) const;              // the index of the diagonal block of a row
};

struct Assembly
{
    std::vector<TetGeom> geom;
    std::vector<uint32_t> vtStart, vtList;      // each vertex's tetrahedra (CSR)
    Bsr K;
    std::vector<double> inv;                    // per vertex: the inverse of its diagonal block, held directions as identity
};

/*  The elements' geometry and the matrix's block pattern  */
void buildPattern(const TetMesh& mesh, Assembly& A);
/*  The stiffness matrix, each element's stiffness times scale[t] (all 1 if null)  */
void assemble(const TetMesh& mesh, const Mat6& D, const std::vector<double>* scale, Assembly& A);
/*  The block-Jacobi preconditioner from the assembled matrix and the held directions  */
void buildPreconditioner(const std::vector<unsigned char>& fixed, Assembly& A);

/*  y = the operator on x, with the held directions replaced by the identity (it stays positive definite)  */
void applyOperator(const Assembly& A, const std::vector<unsigned char>& fixed, const std::vector<double>& x,
                   std::vector<double>& y, std::vector<double>& scratch);

struct CgResult
{
    int iterations = 0;
    double residual = 0;
};

/*  Preconditioned conjugate gradients for K u = b, u the starting guess and the answer; the
 *  held directions of b are ignored.  progress(fraction, text) now and then.  False with a
 *  message if the matrix is singular, the solve diverges or is cancelled.  */
bool cgSolve(const Assembly& A, const std::vector<unsigned char>& fixed, const std::vector<double>& b,
             std::vector<double>& u, double tolerance, int maxIterations, const std::atomic<bool>* cancel,
             const std::function<void(double, const std::string&)>& progress, CgResult& out, std::string& error);

/*  The `count` lowest eigenpairs of K x = lambda M x (M diagonal, `mass` per DOF; the held
 *  directions are left out): locally optimal block preconditioned conjugate gradients.  */
bool lobpcg(const Assembly& A, const std::vector<unsigned char>& fixed, const std::vector<double>& mass, int count,
            int maxIterations, double tolerance, const std::atomic<bool>* cancel, std::vector<double>& lambda,
            std::vector<std::vector<double>>& vectors, int& iterations, std::string& error);

}   // namespace tet
}   // namespace fea
}   // namespace libfive
