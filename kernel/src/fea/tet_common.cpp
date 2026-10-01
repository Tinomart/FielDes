/*
libfive: a CAD kernel for modeling with implicit functions

What the analyses on the body-fitted tetrahedral mesh share; see tet_common.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "tet_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace libfive {
namespace fea {
namespace tet {

namespace {

// Small problems run on the calling thread: waking the pool a dozen times per conjugate-gradient
// iteration costs more than the work itself
void pfor(size_t n, size_t serialBelow, const std::function<void(size_t, size_t)>& fn, size_t minChunk)
{
    if (n < serialBelow)
    {
        if (n) fn(0, n);
        return;
    }
    parallelRange(n, fn, minChunk);
}

const size_t kSerialRows = 6000;        // block rows
const size_t kSerialDofs = 20000;       // entries

}   // anonymous namespace

Mat6 elasticity(double E, double nu)
{
    const double lambda = E * nu / ((1 + nu) * (1 - 2 * nu));
    const double mu = E / (2 * (1 + nu));
    Mat6 D = Mat6::Zero();
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j) D(i, j) = lambda;
        D(i, i) = lambda + 2 * mu;
        D(i + 3, i + 3) = mu;
    }
    return D;
}

TetGeom geometryOf(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    Eigen::Matrix4d M;
    M.row(0) << 1.0, a.x(), a.y(), a.z();
    M.row(1) << 1.0, b.x(), b.y(), b.z();
    M.row(2) << 1.0, c.x(), c.y(), c.z();
    M.row(3) << 1.0, d.x(), d.y(), d.z();
    const Eigen::Matrix4d C = M.inverse();          // N_i = C(0,i) + C(1,i) x + C(2,i) y + C(3,i) z
    TetGeom t;
    t.vol = std::abs(M.determinant()) / 6.0;
    for (int i = 0; i < 4; ++i)
        for (int a2 = 0; a2 < 3; ++a2) t.g[i][a2] = C(1 + a2, i);
    return t;
}

Mat6x12 strainMatrix(const TetGeom& t)
{
    Mat6x12 B = Mat6x12::Zero();
    for (int i = 0; i < 4; ++i)
    {
        const double dx = t.g[i][0], dy = t.g[i][1], dz = t.g[i][2];
        const int c = 3 * i;
        B(0, c) = dx;
        B(1, c + 1) = dy;
        B(2, c + 2) = dz;
        B(3, c) = dy; B(3, c + 1) = dx;
        B(4, c + 1) = dz; B(4, c + 2) = dy;
        B(5, c) = dz; B(5, c + 2) = dx;
    }
    return B;
}

Vec6 strainOf(const TetGeom& t, const double u[12])
{
    Vec6 e = Vec6::Zero();
    for (int i = 0; i < 4; ++i)
    {
        const double dx = t.g[i][0], dy = t.g[i][1], dz = t.g[i][2];
        const double ux = u[3 * i], uy = u[3 * i + 1], uz = u[3 * i + 2];
        e[0] += dx * ux;
        e[1] += dy * uy;
        e[2] += dz * uz;
        e[3] += dy * ux + dx * uy;
        e[4] += dz * uy + dy * uz;
        e[5] += dz * ux + dx * uz;
    }
    return e;
}

uint64_t fnv(uint64_t h, const void* data, size_t bytes)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

void Bsr::matvec(const double* x, double* y) const
{
    pfor(n, kSerialRows, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
        {
            double a0 = 0, a1 = 0, a2 = 0;
            for (uint32_t e = rowPtr[i]; e < rowPtr[i + 1]; ++e)
            {
                const double* B = &val[size_t(e) * 9];
                const double* xv = x + 3 * size_t(col[e]);
                a0 += B[0] * xv[0] + B[1] * xv[1] + B[2] * xv[2];
                a1 += B[3] * xv[0] + B[4] * xv[1] + B[5] * xv[2];
                a2 += B[6] * xv[0] + B[7] * xv[1] + B[8] * xv[2];
            }
            y[3 * i] = a0;
            y[3 * i + 1] = a1;
            y[3 * i + 2] = a2;
        }
    }, 64);
}

size_t Bsr::diag(size_t row) const
{
    return size_t(std::lower_bound(col.begin() + rowPtr[row], col.begin() + rowPtr[row + 1], uint32_t(row)) - col.begin());
}

void buildPattern(const TetMesh& mesh, Assembly& A)
{
    const size_t nv = mesh.pos.size(), nt = mesh.tets.size();
    A.geom.resize(nt);
    parallelRange(nt, [&](size_t b0, size_t b1) {
        for (size_t t = b0; t < b1; ++t)
        {
            const auto& v = mesh.tets[t];
            A.geom[t] = geometryOf(mesh.pos[size_t(v[0])], mesh.pos[size_t(v[1])], mesh.pos[size_t(v[2])],
                                   mesh.pos[size_t(v[3])]);
        }
    }, 256);
    A.vtStart.assign(nv + 1, 0);
    A.vtList.assign(4 * nt, 0);
    for (const auto& t : mesh.tets)
        for (int p = 0; p < 4; ++p) A.vtStart[size_t(t[size_t(p)]) + 1]++;
    for (size_t i = 0; i < nv; ++i) A.vtStart[i + 1] += A.vtStart[i];
    {
        std::vector<uint32_t> cursor(A.vtStart.begin(), A.vtStart.end() - 1);
        for (size_t t = 0; t < nt; ++t)
            for (int p = 0; p < 4; ++p) A.vtList[cursor[size_t(mesh.tets[t][size_t(p)])]++] = uint32_t(t);
    }
    A.K.n = nv;
    std::vector<std::vector<uint32_t>> nb(nv);
    parallelRange(nv, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
        {
            auto& v = nb[i];
            v.reserve(size_t(A.vtStart[i + 1] - A.vtStart[i]) * 3 + 1);
            for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
                for (int p = 0; p < 4; ++p) v.push_back(uint32_t(mesh.tets[A.vtList[e]][size_t(p)]));
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        }
    }, 64);
    A.K.rowPtr.assign(nv + 1, 0);
    for (size_t i = 0; i < nv; ++i) A.K.rowPtr[i + 1] = A.K.rowPtr[i] + uint32_t(nb[i].size());
    A.K.col.resize(A.K.rowPtr[nv]);
    for (size_t i = 0; i < nv; ++i) std::copy(nb[i].begin(), nb[i].end(), A.K.col.begin() + A.K.rowPtr[i]);
    A.K.val.assign(size_t(A.K.rowPtr[nv]) * 9, 0.0);
}

void assemble(const TetMesh& mesh, const Mat6& D, const std::vector<double>* scale, Assembly& A)
{
    const size_t nv = mesh.pos.size();
    // Row by row (no two threads write one block): each tetrahedron at a vertex adds its
    // stiffness rows there
    parallelRange(nv, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
        {
            for (uint32_t e = A.K.rowPtr[i]; e < A.K.rowPtr[i + 1]; ++e)
                std::fill(A.K.val.begin() + std::ptrdiff_t(size_t(e) * 9), A.K.val.begin() + std::ptrdiff_t(size_t(e) * 9 + 9), 0.0);
            for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
            {
                const size_t t = A.vtList[e];
                const auto& tv = mesh.tets[t];
                int li = 0;
                for (int p = 0; p < 4; ++p) if (size_t(tv[size_t(p)]) == i) li = p;
                const Mat6x12 B = strainMatrix(A.geom[t]);
                const double s = A.geom[t].vol * (scale ? (*scale)[t] : 1.0);
                const Eigen::Matrix<double, 3, 12> rows = s * (B.block<6, 3>(0, 3 * li).transpose() * D * B);
                for (int lj = 0; lj < 4; ++lj)
                {
                    const uint32_t j = uint32_t(tv[size_t(lj)]);
                    const auto pos = std::lower_bound(A.K.col.begin() + A.K.rowPtr[i], A.K.col.begin() + A.K.rowPtr[i + 1], j) -
                                     A.K.col.begin();
                    double* blk = &A.K.val[size_t(pos) * 9];
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c) blk[3 * r + c] += rows(r, 3 * lj + c);
                }
            }
        }
    }, 16);
}

void buildPreconditioner(const std::vector<unsigned char>& fixed, Assembly& A)
{
    const size_t nv = A.K.n;
    A.inv.assign(nv * 9, 0.0);
    parallelRange(nv, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
        {
            const size_t pos = A.K.diag(i);
            Eigen::Matrix3d B;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    B(r, c) = (fixed[3 * i + size_t(r)] || fixed[3 * i + size_t(c)]) ? (r == c ? 1.0 : 0.0)
                                                                                    : A.K.val[pos * 9 + size_t(3 * r + c)];
            const Eigen::Matrix3d Bi = B.inverse();
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) A.inv[i * 9 + size_t(3 * r + c)] = Bi(r, c);
        }
    }, 64);
}

void applyOperator(const Assembly& A, const std::vector<unsigned char>& fixed, const std::vector<double>& x,
                   std::vector<double>& y, std::vector<double>& xm)
{
    const size_t n = 3 * A.K.n;
    xm.resize(n);
    pfor(n, kSerialDofs, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i) xm[i] = fixed[i] ? 0.0 : x[i];
    }, 8192);
    A.K.matvec(xm.data(), y.data());
    pfor(n, kSerialDofs, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i) if (fixed[i]) y[i] = x[i];
    }, 8192);
}

namespace {

void precondition(const Assembly& A, const double* r, double* z)
{
    pfor(A.K.n, kSerialRows, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
        {
            const double* M = &A.inv[i * 9];
            const double* rv = r + 3 * i;
            z[3 * i] = M[0] * rv[0] + M[1] * rv[1] + M[2] * rv[2];
            z[3 * i + 1] = M[3] * rv[0] + M[4] * rv[1] + M[5] * rv[2];
            z[3 * i + 2] = M[6] * rv[0] + M[7] * rv[1] + M[8] * rv[2];
        }
    }, 128);
}

double dotProduct(const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() < kSerialDofs)
    {
        double s = 0;
        for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
        return s;
    }
    return parallelTotal(a.size(), [&](size_t b0, size_t b1) {
        double s = 0;
        for (size_t i = b0; i < b1; ++i) s += a[i] * b[i];
        return s;
    });
}

}   // anonymous namespace

bool cgSolve(const Assembly& A, const std::vector<unsigned char>& fixed, const std::vector<double>& b,
             std::vector<double>& u, double tolerance, int maxIterations, const std::atomic<bool>* cancel,
             const std::function<void(double, const std::string&)>& progress, CgResult& out, std::string& error)
{
    const size_t n = 3 * A.K.n;
    std::vector<double> r(n), z(n), p(n), Ap(n), scratch(n);
    if (u.size() != n) u.assign(n, 0.0);
    for (size_t i = 0; i < n; ++i) if (fixed[i]) u[i] = 0.0;
    applyOperator(A, fixed, u, Ap, scratch);
    for (size_t i = 0; i < n; ++i) r[i] = fixed[i] ? 0.0 : b[i] - Ap[i];
    std::vector<double> bm(n);
    for (size_t i = 0; i < n; ++i) bm[i] = fixed[i] ? 0.0 : b[i];
    const double fnorm = std::sqrt(dotProduct(bm, bm));
    out.iterations = 0;
    out.residual = 0;
    if (!(fnorm > 0)) return true;
    double rel = std::sqrt(dotProduct(r, r)) / fnorm;
    if (rel < tolerance)
    {
        out.residual = rel;
        return true;
    }
    precondition(A, r.data(), z.data());
    p = z;
    double rz = dotProduct(r, z);
    const double rel0 = std::max(rel, 1e-300);
    int it = 0;
    for (it = 0; it < maxIterations; ++it)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled";
            return false;
        }
        applyOperator(A, fixed, p, Ap, scratch);
        const double pAp = dotProduct(p, Ap);
        if (!(pAp > 0))
        {
            error = "the supports don't hold the part: fix more of it, or in more directions";
            return false;
        }
        const double alpha = rz / pAp;
        pfor(n, kSerialDofs, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
            {
                u[i] += alpha * p[i];
                r[i] -= alpha * Ap[i];
            }
        }, 8192);
        rel = std::sqrt(dotProduct(r, r)) / fnorm;
        if (it % 20 == 0)
        {
            if (progress)
            {
                // (how far the residual has fallen, on a log scale, towards the tolerance)
                const double frac = std::max(0.0, std::min(1.0, std::log(rel0 / std::max(rel, 1e-300)) /
                                                                   std::log(rel0 / std::max(tolerance, 1e-300))));
                progress(frac, "solving: iteration " + std::to_string(it));
            }
            if (std::getenv("FIELDES_FEA_DEBUG")) fprintf(stderr, "[tetfea]   iteration %d residual %.3e\n", it, rel);
        }
        if (!std::isfinite(rel))
        {
            error = "the solver diverged";
            return false;
        }
        if (rel < tolerance)
        {
            ++it;
            break;
        }
        precondition(A, r.data(), z.data());
        const double rzNew = dotProduct(r, z);
        const double beta = rzNew / rz;
        rz = rzNew;
        pfor(n, kSerialDofs, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i) p[i] = z[i] + beta * p[i];
        }, 8192);
    }
    out.iterations = it;
    out.residual = rel;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// LOBPCG

bool lobpcg(const Assembly& A, const std::vector<unsigned char>& fixed, const std::vector<double>& mass, int count,
            int maxIterations, double tolerance, const std::atomic<bool>* cancel, std::vector<double>& lambda,
            std::vector<std::vector<double>>& vectors, int& iterations, std::string& error)
{
    using Mat = Eigen::MatrixXd;
    using Vec = Eigen::VectorXd;
    const size_t n = 3 * A.K.n;
    size_t nFree = 0;
    for (size_t i = 0; i < n; ++i) nFree += fixed[i] ? 0 : 1;
    if (nFree < size_t(count))
    {
        error = "the part has fewer free directions than modes asked for";
        return false;
    }
    const int m = int(std::min<size_t>(size_t(count) + 2, nFree));
    const Eigen::Map<const Vec> M(mass.data(), long(n));

    auto applyK = [&](const Mat& S, Mat& AS) {
        AS.resize(long(n), S.cols());
        for (long j = 0; j < S.cols(); ++j)
        {
            A.K.matvec(S.col(j).data(), AS.col(j).data());
            for (size_t i = 0; i < n; ++i) if (fixed[i]) AS(long(i), j) = 0.0;
        }
    };
    // S -> S U^-1 (and AS likewise) so that S^T M S = I; false if the columns are dependent
    auto orthonormalize = [&](Mat& S, Mat& AS) {
        Mat G = S.transpose() * (M.asDiagonal() * S);
        G = 0.5 * (G + G.transpose());
        Eigen::LLT<Mat> llt(G);
        if (llt.info() != Eigen::Success) return false;
        const Mat U = llt.matrixU();
        const Mat Uinv = U.triangularView<Eigen::Upper>().solve(Mat::Identity(U.rows(), U.cols()));
        S = S * Uinv;
        AS = AS * Uinv;
        return true;
    };

    // A deterministic random start in the free directions
    Mat X(long(n), m);
    uint64_t seed = 88172645463325252ull;
    for (long j = 0; j < m; ++j)
        for (size_t i = 0; i < n; ++i)
        {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            X(long(i), j) = fixed[i] ? 0.0 : (double(seed % 2000001) / 1000000.0 - 1.0);
        }
    Mat AX;
    applyK(X, AX);
    if (!orthonormalize(X, AX))
    {
        error = "modal analysis: could not start (the free part has too few directions)";
        return false;
    }
    Mat P, AP;
    bool haveP = false;
    Vec lam(m);
    int it = 0;
    // (an eigenvalue is right to the square of the residual: 1e-5 is plenty, and pushing the block to
    // round-off ruins its conditioning)
    const double tol = std::max(tolerance, 1e-5);
    for (it = 0; it < maxIterations; ++it)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled";
            return false;
        }
        for (long j = 0; j < m; ++j) lam[j] = X.col(j).dot(AX.col(j));
        const Mat R = AX - (M.asDiagonal() * X) * lam.asDiagonal();
        std::vector<long> active;
        bool done = true;
        for (long j = 0; j < m; ++j)
        {
            const double rel = R.col(j).norm() / std::max(AX.col(j).norm(), 1e-300);
            if (rel > tol)
            {
                active.push_back(j);
                if (j < count) done = false;
            }
        }
        if (std::getenv("FIELDES_FEA_DEBUG") && it % 10 == 0)
            fprintf(stderr, "[lobpcg] iteration %d, %zu active, lambda0 %.6g\n", it, active.size(), lam[0]);
        if (done) break;
        // The preconditioned residuals of the pairs still moving: K^-1 r, approximately, by a few
        // conjugate-gradient iterations on the stiffness matrix itself (block Jacobi alone is far
        // too weak for the lowest modes of a fine mesh)
        Mat W(long(n), long(active.size()));
        {
            static const int inner = std::getenv("FIELDES_FEA_MODAL_INNER") ? std::atoi(std::getenv("FIELDES_FEA_MODAL_INNER")) : 15;
            std::vector<double> rr(n), ww;
            for (size_t c = 0; c < active.size(); ++c)
            {
                for (size_t i = 0; i < n; ++i) rr[i] = R(long(i), active[c]);
                ww.assign(n, 0.0);
                CgResult cr;
                std::string err;
                if (!cgSolve(A, fixed, rr, ww, 1e-3, inner, nullptr, std::function<void(double, const std::string&)>(), cr, err))
                    precondition(A, rr.data(), ww.data());
                for (size_t i = 0; i < n; ++i) W(long(i), long(c)) = fixed[i] ? 0.0 : ww[i];
            }
        }
        Mat AW;
        applyK(W, AW);
        // unit columns (in the mass norm), so the block stays well conditioned as the pairs converge
        for (long c = 0; c < W.cols(); ++c)
        {
            const double nrm = std::sqrt(std::max(0.0, W.col(c).dot(M.asDiagonal() * W.col(c))));
            if (nrm > 1e-300)
            {
                W.col(c) /= nrm;
                AW.col(c) /= nrm;
            }
        }
        if (haveP)
            for (long c = 0; c < P.cols(); ++c)
            {
                const double nrm = std::sqrt(std::max(0.0, P.col(c).dot(M.asDiagonal() * P.col(c))));
                if (nrm > 1e-300)
                {
                    P.col(c) /= nrm;
                    AP.col(c) /= nrm;
                }
            }
        bool ok = false;
        Mat S, AS;
        for (int attempt = 0; attempt < 2 && !ok; ++attempt)
        {
            const bool withP = haveP && attempt == 0;
            const long k = m + W.cols() + (withP ? P.cols() : 0);
            S.resize(long(n), k);
            AS.resize(long(n), k);
            S.leftCols(m) = X;
            AS.leftCols(m) = AX;
            S.middleCols(m, W.cols()) = W;
            AS.middleCols(m, W.cols()) = AW;
            if (withP)
            {
                S.rightCols(P.cols()) = P;
                AS.rightCols(P.cols()) = AP;
            }
            ok = orthonormalize(S, AS);
            if (!ok) haveP = false;
        }
        if (!ok)
        {
            error = "modal analysis: the eigensolver lost its search directions";
            return false;
        }
        Mat H = S.transpose() * AS;
        H = 0.5 * (H + H.transpose());
        Eigen::SelfAdjointEigenSolver<Mat> es(H);
        if (es.info() != Eigen::Success)
        {
            error = "modal analysis: the subspace eigenproblem failed";
            return false;
        }
        // (K is positive definite: a Ritz value that is not is round-off wrecking the block: keep what we have)
        if (!std::isfinite(es.eigenvalues()[0]) || es.eigenvalues()[0] <= 0.0) break;
        const Mat C = es.eigenvectors().leftCols(m);
        const long k = S.cols();
        X = S * C;
        AX = AS * C;
        if (k > m)
        {
            P = S.rightCols(k - m) * C.bottomRows(k - m);
            AP = AS.rightCols(k - m) * C.bottomRows(k - m);
            haveP = true;
        }
    }
    iterations = it;
    for (long j = 0; j < m; ++j) lam[j] = X.col(j).dot(AX.col(j));
    lambda.assign(size_t(count), 0.0);
    vectors.assign(size_t(count), std::vector<double>(n, 0.0));
    for (int j = 0; j < count; ++j)
    {
        lambda[size_t(j)] = lam[j];
        for (size_t i = 0; i < n; ++i) vectors[size_t(j)][i] = X(long(i), j);
    }
    return true;
}

}   // namespace tet
}   // namespace fea
}   // namespace libfive
