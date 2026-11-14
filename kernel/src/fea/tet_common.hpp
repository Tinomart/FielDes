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
    std::vector<TurnConstraint> turns;          // the restraints of turning (H192): springs on the mean turn of a set of nodes, part of the operator
    std::vector<double> coords;                 // the nodes' positions, x y z each (for the rigid-body modes of the multigrid; empty = no multigrid)
};

/*  y += the springs on the turns of the nodes (H192), applied to x: for each constraint 1/2 k (coef . u - target)^2, the matrix k coef coef^T  */
void addTurnOperator(const Assembly& A, const double* x, double* y);

/*  Values at the nodes from values that are constant in each tetrahedron (a stress, a heat flux): the plane through the values of the elements round a node -- a least-squares fit of a + b.x to
 *  them, weighted by volume, taken AT the node (superconvergent patch recovery, Zienkiewicz and Zhu) -- instead of their average (H199, the user, 2026-11-13: the colours of a result "smeared").  An average
 *  at a node of a surface takes in the deeper elements of the patch, which carry less of a bending stress, and every node does it by its own mix of them: a field that is a plane, as the stress of a
 *  beam is, comes out low and the lower the more deep elements a node has.  The plane gives such a field back exactly, at the surface too.  The patch is two rings of elements -- the node's and its
 *  neighbours' --: one ring keeps the oscillation of the size of an element that a linear tetrahedron's stress has.  A node whose patch cannot fix a plane (fewer than four elements, or all
 *  on a plane) takes its own ring, and last their average; a value never leaves the range of its patch by more than the range itself.
 *  values: `channels` numbers for each tetrahedron (row by row); out: `channels` numbers for each vertex  */
void patchRecovery(const TetMesh& mesh, const Assembly& A, const std::vector<double>& values, int channels, std::vector<double>& out);

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
    std::vector<float> history;         // (the residual, relative to the load, at the start and after every iteration: the curve of the convergence that the result card draws)
};

/*  Smoothed-aggregation algebraic multigrid (tet_amg.cpp): one V-cycle is a preconditioner that is symmetric and positive definite.  It is built from the assembled matrix, the held
 *  directions and the nodes' positions (the six rigid-body modes of each aggregate of nodes are the tentative prolongator), and is for the problems that the block-Jacobi
 *  preconditioner cannot solve in a reasonable number of iterations: plates and shells of tetrahedra, where the number of iterations grows with (length / thickness)^2.
 *  Null with a message when the matrix has the springs of the turns, when it cannot be coarsened or when memory is short.  */
struct Amg;
std::shared_ptr<Amg> buildAmg(const Assembly& A, const std::vector<unsigned char>& fixed, std::string& error);
/*  z = one cycle on r (both of the matrix's size)  */
void applyAmg(Amg& m, const double* r, double* z);
std::string amgSummary(const Amg& m);

/*  Preconditioned conjugate gradients for K u = b, u the starting guess and the answer; the
 *  held directions of b are ignored.  progress(fraction, text) now and then.  False with a
 *  message if the matrix is singular, the solve diverges or is cancelled.  With `multigrid` (and
 *  A.coords set) a solve that stalls under the block-Jacobi preconditioner -- after 100 iterations
 *  the rate of the last 50 says that more than 300 more are needed -- goes on with the multigrid's.  */
bool cgSolve(const Assembly& A, const std::vector<unsigned char>& fixed, const std::vector<double>& b,
             std::vector<double>& u, double tolerance, int maxIterations, const std::atomic<bool>* cancel,
             const std::function<void(double, const std::string&)>& progress, CgResult& out, std::string& error,
             bool multigrid = false);

/*  The `count` lowest eigenpairs of K x = lambda M x (M diagonal, `mass` per DOF; the held
 *  directions are left out): locally optimal block preconditioned conjugate gradients.  */
bool lobpcg(const Assembly& A, const std::vector<unsigned char>& fixed, const std::vector<double>& mass, int count,
            int maxIterations, double tolerance, const std::atomic<bool>* cancel, std::vector<double>& lambda,
            std::vector<std::vector<double>>& vectors, int& iterations, std::string& error);

/*  The surface of a design: of the part where the value `d` at the nodes is above `level` -- it is linear in each tetrahedron, so the surface is exact: marching tetrahedra for what runs through the
 *  mesh and, with `caps`, the faces of the mesh's boundary cut where the value crosses the level.  Triangles, three vertices each (x y z),
 *  counter-clockwise seen from outside, appended to `triangles`  */
void liveSurface(const TetMesh& mesh, const std::vector<float>& d, double level, bool caps, std::vector<float>& triangles);
/*  The same surface as an indexed mesh: the vertices that coincide are one (x y z each; three vertex numbers per triangle)  */
void liveSurfaceIndexed(const TetMesh& mesh, const std::vector<float>& d, double level, bool caps, std::vector<float>& verts,
                        std::vector<uint32_t>& tris);

}   // namespace tet
}   // namespace fea
}   // namespace libfive
