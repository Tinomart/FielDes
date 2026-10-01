/*
libfive: a CAD kernel for modeling with implicit functions

B-spline patches of imported STEP faces; see step_bspline.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "libfive/step/step_bspline.hpp"

namespace libfive {
namespace step {

namespace {

constexpr int MAXP = 15;    // highest degree handled

// Knot span index for u (The NURBS Book A2.1), clamped to the domain
int findSpan(int n, int p, double u, const std::vector<double>& U)
{
    if (u >= U[n + 1])
    {
        // The last non-empty span
        int k = n;
        while (k > p && U[k] >= U[k + 1]) --k;
        return k;
    }
    if (u <= U[p])
    {
        int k = p;
        while (k < n && U[k] >= U[k + 1]) ++k;
        return k;
    }
    int low = p, high = n + 1, mid = (low + high) / 2;
    while (u < U[mid] || u >= U[mid + 1])
    {
        if (u < U[mid]) high = mid;
        else low = mid;
        mid = (low + high) / 2;
    }
    return mid;
}

double safeDiv(double a, double b)
{
    return b != 0 ? a / b : 0.0;
}

// Non-zero basis functions and their first derivatives at u (A2.3, n = 1)
void basis(int i, double u, int p, const std::vector<double>& U, double N[2][MAXP + 1])
{
    double ndu[MAXP + 1][MAXP + 1];
    double left[MAXP + 1], right[MAXP + 1];
    ndu[0][0] = 1.0;
    for (int j = 1; j <= p; ++j)
    {
        left[j] = u - U[i + 1 - j];
        right[j] = U[i + j] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r)
        {
            ndu[j][r] = right[r + 1] + left[j - r];
            const double temp = safeDiv(ndu[r][j - 1], ndu[j][r]);
            ndu[r][j] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        ndu[j][j] = saved;
    }
    for (int j = 0; j <= p; ++j)
    {
        N[0][j] = ndu[j][p];
    }
    for (int r = 0; r <= p; ++r)
    {
        double d = 0.0;
        if (p > 0)
        {
            if (r >= 1) d += safeDiv(ndu[r - 1][p - 1], ndu[p][r - 1]);
            if (r <= p - 1) d -= safeDiv(ndu[r][p - 1], ndu[p][r]);
        }
        N[1][r] = d * p;
    }
}

// Closest point on triangle abc to p, as barycentric weights (b1, b2) of b
// and c (Ericson, Real-Time Collision Detection 5.1.5)

}   // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

BSplinePatch::BSplinePatch(const Surface& s)
{
    if (s.kind != SurfaceKind::BSpline || s.degreeU < 1 || s.degreeV < 1 ||
        s.degreeU > MAXP || s.degreeV > MAXP ||
        s.nCtrlU <= s.degreeU || s.nCtrlV <= s.degreeV ||
        int(s.ctrl.size()) != s.nCtrlU * s.nCtrlV ||
        int(s.knotsU.size()) != s.nCtrlU + s.degreeU + 1 ||
        int(s.knotsV.size()) != s.nCtrlV + s.degreeV + 1)
    {
        return;
    }
    pU = s.degreeU;
    pV = s.degreeV;
    nU = s.nCtrlU;
    nV = s.nCtrlV;
    KU = s.knotsU;
    KV = s.knotsV;
    rational = !s.weights.empty() && s.weights.size() == s.ctrl.size();
    Pw.resize(s.ctrl.size());
    for (size_t i = 0; i < s.ctrl.size(); ++i)
    {
        const double w = rational ? s.weights[i] : 1.0;
        Pw[i] << w * s.ctrl[i].x(), w * s.ctrl[i].y(), w * s.ctrl[i].z(), w;
    }
    setup();
}

void BSplinePatch::setup()
{
    m_ok = false;
    T.clear();
    nodes.clear();
    u0 = KU[pU];
    u1 = KU[nU];
    v0 = KV[pV];
    v1 = KV[nV];
    if (!(u1 > u0) || !(v1 > v0))
    {
        return;
    }
    for (const auto& q : Pw)
    {
        if (!q.allFinite() || !(q.w() > 0)) return;
    }

    // Tessellation: a few samples per knot span, within limits
    auto spans = [](const std::vector<double>& K, int p, int n) {
        int m = 0;
        for (int i = p; i < n; ++i) if (K[i + 1] > K[i]) ++m;
        return std::max(1, m);
    };
    gu = std::min(64, std::max(12, 6 * spans(KU, pU, nU)));
    gv = std::min(64, std::max(12, 6 * spans(KV, pV, nV)));
    P.resize(size_t(gu + 1) * (gv + 1));
    UV.resize(P.size());
    Eigen::AlignedBox3d box;
    for (int i = 0; i <= gu; ++i)
    {
        for (int j = 0; j <= gv; ++j)
        {
            const double u = u0 + (u1 - u0) * i / gu, v = v0 + (v1 - v0) * j / gv;
            const size_t k = size_t(i) * (gv + 1) + j;
            eval(u, v, P[k]);
            UV[k] = Eigen::Vector2d(u, v);
            box.extend(P[k]);
        }
    }
    if (!box.min().allFinite() || !box.max().allFinite())
    {
        return;
    }
    lo = box.min();
    hi = box.max();
    scale = std::max((hi - lo).norm(), 1e-12);
    tessErr = 0;
    for (int i = 0; i < gu; ++i)
    {
        for (int j = 0; j < gv; ++j)
        {
            const int a = i * (gv + 1) + j, b = (i + 1) * (gv + 1) + j;
            const int c = (i + 1) * (gv + 1) + j + 1, d = i * (gv + 1) + j + 1;
            for (const auto& tri : {std::array<int, 3>{a, b, c}, std::array<int, 3>{a, c, d}})
            {
                const Vec3 n = (P[tri[1]] - P[tri[0]]).cross(P[tri[2]] - P[tri[0]]);
                if (n.norm() > 1e-14 * scale * scale)
                {
                    T.push_back(tri);
                }
            }
            // How far the flat quad strays from the surface
            Vec3 mid;
            eval(u0 + (u1 - u0) * (i + 0.5) / gu, v0 + (v1 - v0) * (j + 0.5) / gv, mid);
            const Vec3 avg = (P[a] + P[b] + P[c] + P[d]) / 4;
            tessErr = std::max(tessErr, (mid - avg).norm());
        }
    }
    if (T.empty())
    {
        return;
    }

    // Whether the patch closes on itself, and how fast its parameters run
    {
        const double pad = 1e-7 * scale;
        closedU = closedV = true;
        for (int j = 0; j <= gv; ++j)
        {
            if ((P[size_t(j)] - P[size_t(gu) * (gv + 1) + j]).norm() > pad) closedU = false;
        }
        for (int i = 0; i <= gu; ++i)
        {
            if ((P[size_t(i) * (gv + 1)] - P[size_t(i) * (gv + 1) + gv]).norm() > pad) closedV = false;
        }
        double sumU = 0, sumV = 0;
        for (int i = 0; i < gu; ++i)
            for (int j = 0; j <= gv; ++j)
                sumU += (P[size_t(i + 1) * (gv + 1) + j] - P[size_t(i) * (gv + 1) + j]).norm();
        for (int i = 0; i <= gu; ++i)
            for (int j = 0; j < gv; ++j)
                sumV += (P[size_t(i) * (gv + 1) + j + 1] - P[size_t(i) * (gv + 1) + j]).norm();
        speedU = std::max(sumU / double(gu * (gv + 1)) * gu / (u1 - u0), 1e-12);
        speedV = std::max(sumV / double((gu + 1) * gv) * gv / (v1 - v0), 1e-12);
    }

    std::vector<Vec3> centroids(T.size());
    for (size_t k = 0; k < T.size(); ++k)
    {
        centroids[k] = (P[T[k][0]] + P[T[k][1]] + P[T[k][2]]) / 3;
    }
    order.resize(T.size());
    for (size_t k = 0; k < T.size(); ++k) order[k] = int(k);
    nodes.clear();
    build(0, int(T.size()), centroids);
    m_ok = true;
}

int BSplinePatch::build(int start, int count, std::vector<Vec3>& centroids)
{
    return buildInto(nodes, order, start, count, centroids);
}

int BSplinePatch::buildInto(std::vector<Node>& ns, std::vector<int>& ord, int start, int count,
                            std::vector<Vec3>& centroids)
{
    Node node;
    node.start = start;
    node.count = count;
    Eigen::AlignedBox3d cbox;
    for (int k = start; k < start + count; ++k)
    {
        const auto& tri = T[ord[k]];
        for (int v : tri) node.box.extend(P[v]);
        cbox.extend(centroids[ord[k]]);
    }
    const int index = int(ns.size());
    ns.push_back(node);
    const Vec3 ext = cbox.sizes();
    int axis = 0;
    if (ext.y() > ext[axis]) axis = 1;
    if (ext.z() > ext[axis]) axis = 2;
    if (count <= 4 || !(ext[axis] > 0))
    {
        return index;
    }
    const int half = count / 2;
    std::nth_element(ord.begin() + start, ord.begin() + start + half, ord.begin() + start + count,
                     [&](int x, int y) { return centroids[x][axis] < centroids[y][axis]; });
    const int l = buildInto(ns, ord, start, half, centroids);
    const int r = buildInto(ns, ord, start + half, count - half, centroids);
    ns[index].left = l;
    ns[index].right = r;
    return index;
}

void BSplinePatch::eval(double u, double v, Vec3& S, Vec3* Su, Vec3* Sv) const
{
    u = std::min(u1, std::max(u0, u));
    v = std::min(v1, std::max(v0, v));
    const int su = findSpan(nU - 1, pU, u, KU);
    const int sv = findSpan(nV - 1, pV, v, KV);
    double Nu[2][MAXP + 1], Nv[2][MAXP + 1];
    basis(su, u, pU, KU, Nu);
    basis(sv, v, pV, KV, Nv);
    Eigen::Vector4d A = Eigen::Vector4d::Zero(), Au = A, Av = A;
    for (int i = 0; i <= pU; ++i)
    {
        const int row = (su - pU + i) * nV + (sv - pV);
        for (int j = 0; j <= pV; ++j)
        {
            const Eigen::Vector4d& q = Pw[row + j];
            A += (Nu[0][i] * Nv[0][j]) * q;
            if (Su) Au += (Nu[1][i] * Nv[0][j]) * q;
            if (Sv) Av += (Nu[0][i] * Nv[1][j]) * q;
        }
    }
    const double w = A.w();
    S = A.head<3>() / w;
    if (Su) *Su = (Au.head<3>() - Au.w() * S) / w;
    if (Sv) *Sv = (Av.head<3>() - Av.w() * S) / w;
}

Vec3 BSplinePatch::normal(double u, double v) const
{
    const double uc = 0.5 * (u0 + u1), vc = 0.5 * (v0 + v1);
    // At a collapsed edge or pole Su x Sv vanishes: look just inside
    for (double f : {0.0, 1e-6, 1e-4, 1e-2, 5e-2})
    {
        Vec3 S, Su, Sv;
        eval(u + f * (uc - u), v + f * (vc - v), S, &Su, &Sv);
        const Vec3 n = Su.cross(Sv);
        const double len = n.norm();
        if (len > 1e-12 * (Su.norm() * Sv.norm() + 1e-300) && std::isfinite(len))
        {
            return n / len;
        }
    }
    return Vec3(0, 0, 1);
}

void BSplinePatch::evalWrapped(double u, double v, Vec3& S, Vec3* Su, Vec3* Sv) const
{
    if (closedU && (u < u0 || u > u1))
    {
        const double period = u1 - u0;
        u = u0 + std::fmod(u - u0, period);
        if (u < u0) u += period;
    }
    if (closedV && (v < v0 || v > v1))
    {
        const double period = v1 - v0;
        v = v0 + std::fmod(v - v0, period);
        if (v < v0) v += period;
    }
    eval(u, v, S, Su, Sv);
}

void BSplinePatch::turnRates(double& rateU, double& rateV) const
{
    rateU = rateV = 1.0;
    if (!m_ok) return;
    const int nj = gv + 1;
    std::vector<Vec3> N(P.size());
    for (size_t k = 0; k < P.size(); ++k) N[k] = normal(UV[k].x(), UV[k].y());
    auto turn = [](const Vec3& a, const Vec3& b) { return std::acos(std::max(-1.0, std::min(1.0, a.dot(b)))); };
    std::vector<double> ru, rv;
    const double du = (u1 - u0) / gu, dv = (v1 - v0) / gv;
    for (int i = 0; i < gu; ++i)
        for (int j = 0; j <= gv; ++j) ru.push_back(turn(N[size_t(i) * nj + j], N[size_t(i + 1) * nj + j]) / du);
    for (int i = 0; i <= gu; ++i)
        for (int j = 0; j < gv; ++j) rv.push_back(turn(N[size_t(i) * nj + j], N[size_t(i) * nj + j + 1]) / dv);
    auto upper = [](std::vector<double>& x) {
        if (x.empty()) return 1.0;
        const size_t k = x.size() * 3 / 4;
        std::nth_element(x.begin(), x.begin() + long(k), x.end());
        return x[k];
    };
    rateU = upper(ru);
    rateV = upper(rv);
}

Vec3 BSplinePatch::normalWrapped(double u, double v) const
{
    if (closedU && (u < u0 || u > u1))
    {
        const double period = u1 - u0;
        u = u0 + std::fmod(u - u0, period);
        if (u < u0) u += period;
    }
    if (closedV && (v < v0 || v > v1))
    {
        const double period = v1 - v0;
        v = v0 + std::fmod(v - v0, period);
        if (v < v0) v += period;
    }
    return normal(u, v);
}

double BSplinePatch::refine(const Vec3& p, double& u, double& v) const
{
    const double du = u1 - u0, dv = v1 - v0;
    u = std::min(u1, std::max(u0, u));
    v = std::min(v1, std::max(v0, v));
    Vec3 S, Su, Sv;
    eval(u, v, S, &Su, &Sv);
    double f = (S - p).squaredNorm();
    double lambda = 1e-3;
    const double fDone = 1e-24 * scale * scale;     // a point on the surface to a trillionth of its size
    for (int it = 0; it < 80 && f > fDone; ++it)
    {
        // Derivatives with respect to the parameters scaled to 0..1, so that
        // a step means the same thing along both
        const Vec3 Ss = Su * du, St = Sv * dv;
        const Vec3 R = S - p;
        const double g0 = Ss.dot(R), g1 = St.dot(R);
        const double h00 = Ss.dot(Ss), h01 = Ss.dot(St), h11 = St.dot(St);
        const double floorH = 1e-12 * (h00 + h11) + 1e-300;
        bool moved = false, settled = false;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const double a = h00 * (1 + lambda) + floorH, d = h11 * (1 + lambda) + floorH;
            const double det = a * d - h01 * h01;
            if (!(det > 0)) { lambda *= 8; continue; }
            const double ds = -(d * g0 - h01 * g1) / det;
            const double dt = -(a * g1 - h01 * g0) / det;
            double nu = std::min(u1, std::max(u0, u + ds * du));
            double nv = std::min(v1, std::max(v0, v + dt * dv));
            if (nu == u && nv == v) { lambda *= 8; continue; }
            Vec3 S2, Su2, Sv2;
            eval(nu, nv, S2, &Su2, &Sv2);
            const double f2 = (S2 - p).squaredNorm();
            if (f2 < f)
            {
                const double gain = f - f2;
                const double move = std::max(std::abs(nu - u) / du, std::abs(nv - v) / dv);
                u = nu; v = nv; S = S2; Su = Su2; Sv = Sv2; f = f2;
                lambda = std::max(lambda * 0.2, 1e-14);
                moved = true;
                if (move < 1e-13 || gain < 1e-15 * f) settled = true;
                break;
            }
            lambda *= 8;
        }
        if (!moved || settled) break;
    }
    return f;
}

void BSplinePatch::feet(const Vec3& p, const Eigen::Vector2d* near, std::vector<Foot>& out) const
{
    out.clear();
    if (!m_ok) return;
    const double du = u1 - u0, dv = v1 - v0;
    struct Start { double u, v; };
    std::vector<Start> starts;
    if (near)
    {
        double nu = near->x(), nv = near->y();
        if (closedU) { nu = u0 + std::fmod(nu - u0, du); if (nu < u0) nu += du; }
        if (closedV) { nv = v0 + std::fmod(nv - v0, dv); if (nv < v0) nv += dv; }
        starts.push_back({nu, nv});
    }
    // Every grid point that is nearer to p than its neighbours: the
    // surface's separate places near p, each the start of a refinement
    {
        const int nj = gv + 1;
        std::vector<double> d2(P.size());
        for (size_t k = 0; k < P.size(); ++k) d2[k] = (P[k] - p).squaredNorm();
        std::vector<std::pair<double, size_t>> minima;
        for (int i = 0; i <= gu; ++i)
        {
            for (int j = 0; j <= gv; ++j)
            {
                const size_t k = size_t(i) * nj + j;
                bool lowest = true;
                for (int a = std::max(0, i - 1); a <= std::min(gu, i + 1) && lowest; ++a)
                    for (int b = std::max(0, j - 1); b <= std::min(gv, j + 1); ++b)
                    {
                        const size_t q = size_t(a) * nj + b;
                        if (q != k && (d2[q] < d2[k] || (d2[q] == d2[k] && q < k))) { lowest = false; break; }
                    }
                if (lowest) minima.push_back({d2[k], k});
            }
        }
        std::sort(minima.begin(), minima.end());
        for (size_t m = 0; m < minima.size() && m < 6; ++m)
        {
            starts.push_back({UV[minima[m].second].x(), UV[minima[m].second].y()});
        }
    }

    for (const Start& s : starts)
    {
        double u = s.u, v = s.v;
        const double f = refine(p, u, v);
        Foot foot;
        foot.u = u;
        foot.v = v;
        foot.dist = std::sqrt(f);
        bool dup = false;
        for (auto& o : out)
        {
            double a = std::abs(o.u - foot.u), b = std::abs(o.v - foot.v);
            if (closedU) a = std::min(a, std::abs(a - du));
            if (closedV) b = std::min(b, std::abs(b - dv));
            if (a <= 1e-7 * du && b <= 1e-7 * dv) { dup = true; break; }
        }
        if (!dup) out.push_back(foot);
    }
    std::stable_sort(out.begin(), out.end(), [](const Foot& a, const Foot& b) { return a.dist < b.dist; });
}

BSplinePatch::Foot BSplinePatch::closest(const Vec3& p, const Eigen::Vector2d* near) const
{
    std::vector<Foot> f;
    feet(p, near, f);
    return f.empty() ? Foot() : f.front();
}

void BSplinePatch::rayHits(const Vec3& p, const Vec3& d, std::vector<Eigen::Vector3d>& out) const
{
    out.clear();
    const Vec3 inv(1.0 / d.x(), 1.0 / d.y(), 1.0 / d.z());
    auto boxHit = [&](const Eigen::AlignedBox3d& b) {
        double t0 = 0, t1 = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 3; ++i)
        {
            const double pad = 1e-9 * scale;
            double ta = (b.min()[i] - pad - p[i]) * inv[i];
            double tb = (b.max()[i] + pad - p[i]) * inv[i];
            if (std::isnan(ta) || std::isnan(tb))
            {
                if (p[i] < b.min()[i] - pad || p[i] > b.max()[i] + pad) return false;
                continue;
            }
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        return true;
    };

    std::vector<Eigen::Vector3d> raw;
    int stack[128];
    int sp = 0;
    stack[sp++] = 0;
    while (sp)
    {
        const Node& n = nodes[stack[--sp]];
        if (!boxHit(n.box)) continue;
        if (n.left < 0 || sp + 2 > 128)
        {
            for (int k = n.start; k < n.start + n.count; ++k)
            {
                const auto& t = T[order[k]];
                const Vec3 e1 = P[t[1]] - P[t[0]], e2 = P[t[2]] - P[t[0]];
                const Vec3 h = d.cross(e2);
                const double det = e1.dot(h);
                if (std::abs(det) < 1e-300) continue;
                const double f = 1.0 / det;
                const Vec3 s = p - P[t[0]];
                const double b1 = f * s.dot(h);
                // A little slack: the refinement below decides
                if (b1 < -1e-6 || b1 > 1 + 1e-6) continue;
                const Vec3 q = s.cross(e1);
                const double b2 = f * d.dot(q);
                if (b2 < -1e-6 || b1 + b2 > 1 + 1e-6) continue;
                const double tt = f * e2.dot(q);
                if (tt <= 0) continue;
                const Eigen::Vector2d uv = UV[t[0]] * (1 - b1 - b2) + UV[t[1]] * b1 + UV[t[2]] * b2;
                raw.push_back(Eigen::Vector3d(tt, uv.x(), uv.y()));
            }
        }
        else
        {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }

    // Refine each crossing on the exact surface: S(u, v) = p + t d
    const double tol = 1e-9 * scale;
    for (auto h : raw)
    {
        double t = h.x(), u = h.y(), v = h.z();
        bool converged = false;
        for (int it = 0; it < 12; ++it)
        {
            Vec3 S, Su, Sv;
            eval(u, v, S, &Su, &Sv);
            const Vec3 F = S - (p + t * d);
            if (F.norm() < tol)
            {
                converged = true;
                break;
            }
            Eigen::Matrix3d J;
            J.col(0) = Su;
            J.col(1) = Sv;
            J.col(2) = -d;
            const Eigen::Vector3d step = J.partialPivLu().solve(-F);
            if (!step.allFinite()) break;
            u = std::min(u1, std::max(u0, u + step.x()));
            v = std::min(v1, std::max(v0, v + step.y()));
            t += step.z();
        }
        if (!converged)
        {
            // Keep the triangle's crossing unless the refinement showed it
            // is really past the patch's edge
            Vec3 S;
            eval(u, v, S);
            if ((S - (p + t * d)).norm() > std::max(4 * tessErr, 1e-7 * scale))
            {
                t = h.x(); u = h.y(); v = h.z();
            }
        }
        if (!(t > 0)) continue;
        bool dup = false;
        for (const auto& o : out)
        {
            if (std::abs(o.x() - t) < 1e-7 * scale + 1e-12 &&
                std::abs(o.y() - u) < 1e-6 * (u1 - u0) && std::abs(o.z() - v) < 1e-6 * (v1 - v0))
            {
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back(Eigen::Vector3d(t, u, v));
    }
}

}   // namespace step
}   // namespace libfive
