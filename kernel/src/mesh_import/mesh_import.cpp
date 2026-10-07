/*
libfive: a CAD kernel for modeling with implicit functions

Triangle-mesh import; see mesh_import.hpp for an overview.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Eigen/Eigen>
#include <boost/container/small_vector.hpp>

#include "libfive/mesh_import/mesh_import.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_storage.hpp"
#include "libfive/tree/content_key.hpp"
#include "libfive/eval/feature.hpp"

namespace libfive {
namespace mesh {

namespace {

using V3 = Eigen::Vector3d;
using Tri = std::array<uint32_t, 3>;

constexpr double PI = 3.14159265358979323846;

////////////////////////////////////////////////////////////////////////////////
// Reading

bool readAll(const std::string& path, std::string& data, std::string& error)
{
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f)
    {
        error = "could not open '" + path + "'";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    data = ss.str();
    return true;
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

// Locale-independent number parsing (strtod would follow the C locale)
bool parseDouble(const char*& p, const char* end, double& out)
{
    while (p < end && isSpace(*p)) ++p;
    if (p < end && *p == '+') ++p;
    auto r = std::from_chars(p, end, out);
    if (r.ec != std::errc())
    {
        return false;
    }
    p = r.ptr;
    return true;
}

bool sameWord(const char* w, size_t n, const char* kw)
{
    if (std::strlen(kw) != n)
    {
        return false;
    }
    for (size_t i=0; i < n; ++i)
    {
        if (std::tolower(static_cast<unsigned char>(w[i])) != kw[i])
        {
            return false;
        }
    }
    return true;
}

bool parseStl(const std::string& d, std::vector<V3>& verts,
              std::vector<Tri>& tris, std::string& error)
{
    auto binary = [&](uint32_t n) {
        verts.clear();
        tris.clear();
        verts.reserve(3ull * n);
        tris.reserve(n);
        for (uint32_t i=0; i < n; ++i)
        {
            const char* t = d.data() + 84 + 50ull * i + 12;
            Tri tri;
            for (int k=0; k < 3; ++k)
            {
                float c[3];
                std::memcpy(c, t + 12 * k, 12);
                tri[k] = static_cast<uint32_t>(verts.size());
                verts.emplace_back(c[0], c[1], c[2]);
            }
            tris.push_back(tri);
        }
    };

    uint32_t n = 0;
    if (d.size() >= 84)
    {
        std::memcpy(&n, d.data() + 80, 4);
        // Binary files are recognised by their size (a binary header may
        // itself start with "solid", like an ASCII file)
        if (84 + 50ull * n == d.size())
        {
            binary(n);
            return true;
        }
    }

    // ASCII: every "vertex x y z", three per facet
    const char* p = d.data();
    const char* end = p + d.size();
    size_t count = 0;
    while (p < end)
    {
        while (p < end && isSpace(*p)) ++p;
        const char* w = p;
        while (p < end && !isSpace(*p)) ++p;
        if (sameWord(w, p - w, "vertex"))
        {
            double c[3];
            for (int k=0; k < 3; ++k)
            {
                if (!parseDouble(p, end, c[k]))
                {
                    error = "malformed ASCII STL file (a vertex without three numbers)";
                    return false;
                }
            }
            verts.emplace_back(c[0], c[1], c[2]);
            if (++count % 3 == 0)
            {
                tris.push_back({static_cast<uint32_t>(count - 3),
                                static_cast<uint32_t>(count - 2),
                                static_cast<uint32_t>(count - 1)});
            }
        }
    }
    if (!tris.empty())
    {
        return true;
    }

    // Binary files with trailing bytes after the last triangle
    if (n > 0 && d.size() >= 84 + 50ull * n)
    {
        binary(n);
        return true;
    }
    error = "not a readable STL file (no triangles found)";
    return false;
}

bool parseObj(const std::string& d, std::vector<V3>& verts,
              std::vector<Tri>& tris, std::string& error)
{
    std::vector<std::array<long long, 3>> faces;
    std::vector<long long> idx;
    const char* p = d.data();
    const char* end = p + d.size();
    size_t line = 0;
    while (p < end)
    {
        const char* eol = static_cast<const char*>(std::memchr(p, '\n', end - p));
        if (!eol)
        {
            eol = end;
        }
        const char* q = p;
        p = eol + 1;
        ++line;

        while (q < eol && (*q == ' ' || *q == '\t')) ++q;
        if (eol - q < 2 || !(q[1] == ' ' || q[1] == '\t'))
        {
            continue;
        }
        if (q[0] == 'v')
        {
            q += 2;
            double c[3];
            for (int k=0; k < 3; ++k)
            {
                if (!parseDouble(q, eol, c[k]))
                {
                    error = "malformed vertex on line " + std::to_string(line) + " of the OBJ file";
                    return false;
                }
            }
            verts.emplace_back(c[0], c[1], c[2]);
        }
        else if (q[0] == 'f')
        {
            q += 2;
            idx.clear();
            while (true)
            {
                while (q < eol && isSpace(*q)) ++q;
                if (q >= eol)
                {
                    break;
                }
                long long i = 0;
                auto r = std::from_chars(q, eol, i);
                if (r.ec != std::errc())
                {
                    error = "malformed face on line " + std::to_string(line) + " of the OBJ file";
                    return false;
                }
                q = r.ptr;
                while (q < eol && !isSpace(*q)) ++q;     // "/texture/normal"
                // 1-based, or negative = relative to the vertices so far
                idx.push_back(i < 0 ? static_cast<long long>(verts.size()) + i : i - 1);
            }
            for (size_t k=1; k + 1 < idx.size(); ++k)
            {
                faces.push_back({idx[0], idx[k], idx[k + 1]});
            }
        }
    }
    for (const auto& f : faces)
    {
        Tri t;
        for (int k=0; k < 3; ++k)
        {
            if (f[k] < 0 || f[k] >= static_cast<long long>(verts.size()))
            {
                error = "the OBJ file has a face referring to a vertex that does not exist";
                return false;
            }
            t[k] = static_cast<uint32_t>(f[k]);
        }
        tris.push_back(t);
    }
    if (tris.empty())
    {
        error = "the OBJ file contains no faces";
        return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Geometry helpers

// Closest point on triangle abc to p (Ericson, Real-Time Collision
// Detection 5.1.5).  feature: 0 = face, 1..3 = vertex k-1, 4..6 = edge
// k-4, where edge k joins vertex k and vertex (k+1)%3
V3 closestOnTriangle(const V3& p, const V3& a, const V3& b, const V3& c, int& feature)
{
    const V3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = ab.dot(ap), d2 = ac.dot(ap);
    if (d1 <= 0 && d2 <= 0) { feature = 1; return a; }

    const V3 bp = p - b;
    const double d3 = ab.dot(bp), d4 = ac.dot(bp);
    if (d3 >= 0 && d4 <= d3) { feature = 2; return b; }

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
    {
        feature = 4;
        return a + (d1 / (d1 - d3)) * ab;
    }

    const V3 cp = p - c;
    const double d5 = ab.dot(cp), d6 = ac.dot(cp);
    if (d6 >= 0 && d5 <= d6) { feature = 3; return c; }

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
    {
        feature = 6;
        return a + (d2 / (d2 - d6)) * ac;
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
    {
        feature = 5;
        return b + ((d4 - d3) / ((d4 - d3) + (d5 - d6))) * (c - b);
    }

    const double denom = 1.0 / (va + vb + vc);
    feature = 0;
    return a + ab * (vb * denom) + ac * (vc * denom);
}

// Signed solid angle of triangle abc (positions relative to the query
// point); Van Oosterom & Strackee
double solidAngle(const V3& a, const V3& b, const V3& c)
{
    const double la = a.norm(), lb = b.norm(), lc = c.norm();
    const double num = a.dot(b.cross(c));
    const double den = la * lb * lc + a.dot(b) * lc + b.dot(c) * la + c.dot(a) * lb;
    return 2.0 * std::atan2(num, den);
}

// Ray / triangle intersection (Moller-Trumbore), t > 0 only
bool rayHits(const V3& o, const V3& dir, const V3& a, const V3& b, const V3& c)
{
    const V3 e1 = b - a, e2 = c - a;
    const V3 h = dir.cross(e2);
    const double det = e1.dot(h);
    if (std::abs(det) < 1e-300)
    {
        return false;
    }
    const double inv = 1.0 / det;
    const V3 s = o - a;
    const double u = inv * s.dot(h);
    if (u < 0 || u > 1)
    {
        return false;
    }
    const V3 q = s.cross(e1);
    const double v = inv * dir.dot(q);
    if (v < 0 || u + v > 1)
    {
        return false;
    }
    return inv * e2.dot(q) > 0;
}

struct ArrayHash
{
    size_t operator()(const std::array<int64_t, 3>& k) const
    {
        uint64_t h = 1469598103934665603ull;
        for (auto v : k)
        {
            h ^= static_cast<uint64_t>(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        }
        return static_cast<size_t>(h);
    }
    size_t operator()(const Tri& k) const
    {
        return (*this)(std::array<int64_t, 3>{k[0], k[1], k[2]});
    }
};

////////////////////////////////////////////////////////////////////////////////
// The cleaned mesh with its search structure

class MeshData
{
public:
    std::vector<V3> v;
    std::vector<Tri> t;
    std::vector<V3> faceNormal;     // unit
    std::vector<V3> vertexNormal;   // angle-weighted pseudo-normals
    std::vector<V3> edgeNormal;     // per (triangle, local edge): sum of the adjacent face normals
    // Where those pseudo-normals cancel (a sliver folded back over its neighbour, a knife edge, a spike:
    // faces that face nearly opposite ways), the side of the closest feature is no longer in them
    std::vector<char> weakVertex;          // per vertex
    std::vector<char> weakEdge;            // per (triangle, local edge)

    // Where the pseudo-normal sign needs checking (see signedDistance)
    bool hasOpen = false;                  // boundary / non-manifold edges exist
    std::vector<char> openEdge;            // per (triangle, local edge)
    std::vector<char> openVertex;          // touches an open edge
    bool overlapping = false;              // some shells' bounding boxes intersect
    std::vector<uint32_t> shellOf;         // per triangle
    std::vector<std::pair<uint32_t, Eigen::AlignedBox3d>> overlapBoxes;

    // A patch of a surface (see patchTreeFromArrays): the distance to it, unsigned
    bool unsignedMode = false;

    // The box of the vertices: nothing outside it is inside the mesh
    Eigen::AlignedBox3d bounds;

    struct Node
    {
        Eigen::AlignedBox3d box;
        int32_t left = -1, right = -1;
        uint32_t start = 0, count = 0;
        V3 center = V3::Zero();     // area-weighted centroid
        V3 normal = V3::Zero();     // sum of area-weighted normals
        double radius = 0;          // bounds every vertex around center
    };
    std::vector<Node> nodes;
    std::vector<uint32_t> order;

    void buildTree()
    {
        order.resize(t.size());
        for (uint32_t i=0; i < order.size(); ++i)
        {
            order[i] = i;
        }
        centroids.resize(t.size());
        for (size_t i=0; i < t.size(); ++i)
        {
            centroids[i] = (v[t[i][0]] + v[t[i][1]] + v[t[i][2]]) / 3.0;
        }
        nodes.clear();
        nodes.reserve(2 * t.size() / 4 + 16);
        build(0, static_cast<uint32_t>(t.size()));
        centroids.clear();
        centroids.shrink_to_fit();
    }

    struct Hit
    {
        double d2 = std::numeric_limits<double>::infinity();
        uint32_t tri = 0;
        int feature = 0;
        V3 cp = V3::Zero();
    };

    // `hint`: a triangle the previous (nearby) point was closest to. Its distance is an upper bound that
    // is almost always the answer, so the search starts with a tight radius and skips nearly every node;
    // the result is the same as without it (a triangle that is nearer still takes over)
    Hit closest(const V3& p, uint32_t hint = std::numeric_limits<uint32_t>::max()) const
    {
        Hit best;
        if (hint < t.size())
        {
            const Tri& tr = t[hint];
            int f;
            const V3 cp = closestOnTriangle(p, v[tr[0]], v[tr[1]], v[tr[2]], f);
            best.d2 = (p - cp).squaredNorm();
            best.tri = hint;
            best.feature = f;
            best.cp = cp;
        }
        // (the box distance of a node is worked out once, when it is pushed)
        struct Item { uint32_t node; double d2; };
        Item stack[128];
        int sp = 0;
        stack[sp++] = {0, 0.0};
        while (sp)
        {
            const Item it = stack[--sp];
            if (it.d2 >= best.d2)
            {
                continue;
            }
            const Node& n = nodes[it.node];
            if (n.left < 0 || sp + 2 > 128)
            {
                // A leaf (or, if the stack is ever full, the whole subtree,
                // whose triangles are contiguous in `order`)
                for (uint32_t i=n.start; i < n.start + n.count; ++i)
                {
                    const uint32_t ti = order[i];
                    const Tri& tr = t[ti];
                    int f;
                    const V3 cp = closestOnTriangle(p, v[tr[0]], v[tr[1]], v[tr[2]], f);
                    const double d2 = (p - cp).squaredNorm();
                    if (d2 < best.d2)
                    {
                        best.d2 = d2;
                        best.tri = ti;
                        best.feature = f;
                        best.cp = cp;
                    }
                }
            }
            else
            {
                const double dl = nodes[n.left].box.squaredExteriorDistance(p);
                const double dr = nodes[n.right].box.squaredExteriorDistance(p);
                // The nearer child is searched first
                if (dl < dr)
                {
                    if (dr < best.d2) stack[sp++] = {static_cast<uint32_t>(n.right), dr};
                    if (dl < best.d2) stack[sp++] = {static_cast<uint32_t>(n.left), dl};
                }
                else
                {
                    if (dl < best.d2) stack[sp++] = {static_cast<uint32_t>(n.left), dl};
                    if (dr < best.d2) stack[sp++] = {static_cast<uint32_t>(n.right), dr};
                }
            }
        }
        return best;
    }

    // Generalised winding number: ~1 inside, ~0 outside
    double winding(const V3& p) const
    {
        const double beta = 3.0;
        double w = 0;
        uint32_t stack[128];
        int sp = 0;
        stack[sp++] = 0;
        while (sp)
        {
            const Node& n = nodes[stack[--sp]];
            const V3 d = n.center - p;
            const double dist = d.norm();
            if (n.left >= 0 && dist > beta * n.radius)
            {
                // Far away: the cluster acts like a dipole
                w += d.dot(n.normal) / (dist * dist * dist);
                continue;
            }
            if (n.left < 0 || sp + 2 > 128)
            {
                for (uint32_t i=n.start; i < n.start + n.count; ++i)
                {
                    const Tri& tr = t[order[i]];
                    w += solidAngle(v[tr[0]] - p, v[tr[1]] - p, v[tr[2]] - p);
                }
            }
            else
            {
                stack[sp++] = n.left;
                stack[sp++] = n.right;
            }
        }
        return w / (4 * PI);
    }

    bool weakFeature(const Hit& h) const
    {
        if (h.feature == 0)
        {
            return false;
        }
        if (h.feature <= 3)
        {
            return weakVertex[t[h.tri][h.feature - 1]] != 0;
        }
        return weakEdge[h.tri * 3 + (h.feature - 4)] != 0;
    }

    V3 pseudoNormal(const Hit& h) const
    {
        if (h.feature == 0)
        {
            return faceNormal[h.tri];
        }
        if (h.feature <= 3)
        {
            return vertexNormal[t[h.tri][h.feature - 1]];
        }
        return edgeNormal[h.tri * 3 + (h.feature - 4)];
    }

    // Signed distance (negative inside) and optionally its gradient.
    // `soft` is set near holes, where the value is blended with the winding
    // number (see below) and may change up to SOFT_LIPSCHITZ times as fast
    // as a true distance.
    static constexpr double SOFT_LIPSCHITZ = 4.0;

    double signedDistance(const V3& p, V3* grad, bool* soft=nullptr, uint32_t* hint=nullptr) const
    {
        const Hit h = closest(p, hint ? *hint : std::numeric_limits<uint32_t>::max());
        if (hint)
        {
            *hint = h.tri;
        }
        const double dist = std::sqrt(h.d2);
        if (unsignedMode)
        {
            if (grad)
            {
                *grad = dist > 1e-9 * scaleRef ? V3((p - h.cp) / dist) : faceNormal[h.tri];
            }
            return dist;
        }

        // The pseudo-normal of the closest feature gives the exact side of
        // that feature's (closed) shell.  It can't be trusted when the point
        // is outside its closest shell but maybe inside an overlapping one
        // (then the winding number decides), nor near holes (below).
        // Where the closest feature's pseudo-normal has cancelled (see weakEdge) the winding number
        // decides: the mesh from a dual-contouring mesher has slivers folded back over their
        // neighbours along sharp edges, and there the sign of the pseudo-normal is noise.
        auto plainSign = [&]() {
            double sign = weakFeature(h) ? (winding(p) > 0.5 ? -1.0 : 1.0)
                                         : ((p - h.cp).dot(pseudoNormal(h)) < 0 ? -1.0 : 1.0);
            if (sign > 0 && overlapping)
            {
                bool check = overlapBoxes.empty();   // too many shells to check one by one
                const uint32_t own = shellOf[h.tri];
                for (const auto& sb : overlapBoxes)
                {
                    if (sb.first != own && sb.second.contains(p))
                    {
                        check = true;
                        break;
                    }
                }
                if (check)
                {
                    sign = winding(p) > 0.5 ? -1.0 : 1.0;
                }
            }
            // The pseudo-normal of the closest feature is the side of ONE patch of the surface.  Where that patch faces the wrong
            // way (a thin wedge folded the wrong way round, a fin of two layers that cancel: the tessellation of a thin free-form
            // part has them), it says "inside" over a whole cone of space outside the part, and the field has a solid there that
            // is not.  Nothing outside the box of the mesh is inside it; and an "inside" is believed only if the winding number of
            // the whole mesh, which a defect of one patch changes nowhere but in the defect, agrees (it is 1 inside a closed shell
            // and 0 outside; a point nearer the surface than a billionth of the size is not judged)
            if (sign < 0)
            {
                if (!bounds.contains(p))
                {
                    return 1.0;
                }
                if (dist > 1e-9 * scaleRef && winding(p) < 0.25)
                {
                    return 1.0;
                }
            }
            return sign;
        };

        // Near a hole, inside and outside are only defined by the winding
        // number, which fades from 1 (inside) to 0 (outside) across the
        // opening; there the value is the distance scaled by (1 - 2w),
        // which closes the hole with a smooth membrane.  To keep the field
        // continuous that is blended in over a band: fully where the
        // closest feature is on the hole's rim, not at all once the rim is
        // twice as far away as the closest feature.
        double blend = 0;
        if (hasOpen)
        {
            bool on_rim = false;
            if (h.feature >= 4)
            {
                on_rim = openEdge[h.tri * 3 + (h.feature - 4)] != 0;
            }
            else if (h.feature >= 1)
            {
                on_rim = openVertex[t[h.tri][h.feature - 1]] != 0;
            }
            if (on_rim)
            {
                blend = 1;
            }
            else if (dist > 0)
            {
                const double d_rim = rimDistance(p, 2 * dist);
                blend = std::min(1.0, std::max(0.0, (2 * dist - d_rim) / dist));
            }
        }

        if (blend > 0)
        {
            const double w = std::min(1.0, std::max(0.0, winding(p)));
            double value = blend * (1.0 - 2.0 * w) * dist;
            if (blend < 1)
            {
                value += (1 - blend) * plainSign() * dist;
            }
            if (soft)
            {
                *soft = true;
            }
            if (grad)
            {
                // The membrane is the level set w = 1/2, so its normal is
                // -grad(w); the distance factor only adds kinks there (along
                // the medial axis of the rim).  Central differences over a
                // step comparable to the distance smooth out the small jumps
                // of the hierarchical approximation.
                const double e = std::max(1e-6 * scaleRef, 0.05 * dist);
                V3 gw;
                for (int a=0; a < 3; ++a)
                {
                    V3 d = V3::Zero();
                    d[a] = e;
                    gw[a] = (winding(p - d) - winding(p + d)) / (2 * e);
                }
                V3 g = gw.norm() > 0 ? V3(gw.normalized()) : faceNormal[h.tri];
                if (blend < 1)
                {
                    const V3 plain = dist > 1e-9 * scaleRef
                        ? V3(plainSign() * (p - h.cp) / dist) : faceNormal[h.tri];
                    g = blend * g + (1 - blend) * plain;
                }
                const double len = g.norm();
                *grad = len > 0 ? V3(g / len) : faceNormal[h.tri];
            }
            return value;
        }

        const double sign = plainSign();
        if (grad)
        {
            if (dist > 1e-9 * scaleRef)
            {
                *grad = sign * (p - h.cp) / dist;
            }
            else
            {
                *grad = faceNormal[h.tri];
            }
        }
        return sign * dist;
    }

    // Open (boundary / non-manifold) edges, with their own search tree
    std::vector<std::pair<V3, V3>> rim;
    struct RimNode
    {
        Eigen::AlignedBox3d box;
        int32_t left = -1, right = -1;
        uint32_t start = 0, count = 0;
    };
    std::vector<RimNode> rimNodes;
    std::vector<uint32_t> rimOrder;

    void buildRimTree()
    {
        rimOrder.resize(rim.size());
        for (uint32_t i=0; i < rimOrder.size(); ++i)
        {
            rimOrder[i] = i;
        }
        rimNodes.clear();
        if (!rim.empty())
        {
            buildRim(0, static_cast<uint32_t>(rim.size()));
        }
    }

    // Distance to the nearest open edge, or `cap` if none is closer
    double rimDistance(const V3& p, double cap) const
    {
        if (rimNodes.empty())
        {
            return cap;
        }
        double best = cap * cap;
        uint32_t stack[128];
        int sp = 0;
        stack[sp++] = 0;
        while (sp)
        {
            const RimNode& n = rimNodes[stack[--sp]];
            if (n.box.squaredExteriorDistance(p) >= best)
            {
                continue;
            }
            if (n.left < 0 || sp + 2 > 128)
            {
                for (uint32_t i=n.start; i < n.start + n.count; ++i)
                {
                    const auto& s = rim[rimOrder[i]];
                    const V3 ab = s.second - s.first;
                    const double l2 = ab.squaredNorm();
                    const double u = l2 > 0 ? std::min(1.0, std::max(0.0, (p - s.first).dot(ab) / l2)) : 0.0;
                    best = std::min(best, (p - (s.first + u * ab)).squaredNorm());
                }
            }
            else
            {
                stack[sp++] = n.left;
                stack[sp++] = n.right;
            }
        }
        return std::sqrt(best);
    }

    int32_t buildRim(uint32_t start, uint32_t count)
    {
        RimNode node;
        node.start = start;
        node.count = count;
        Eigen::AlignedBox3d mids;
        for (uint32_t i=start; i < start + count; ++i)
        {
            const auto& s = rim[rimOrder[i]];
            node.box.extend(s.first);
            node.box.extend(s.second);
            mids.extend(V3((s.first + s.second) / 2));
        }
        const int32_t index = static_cast<int32_t>(rimNodes.size());
        rimNodes.push_back(node);
        const V3 extent = mids.sizes();
        int axis = 0;
        if (extent.y() > extent[axis]) axis = 1;
        if (extent.z() > extent[axis]) axis = 2;
        if (count <= 4 || !(extent[axis] > 0))
        {
            return index;
        }
        const uint32_t half = count / 2;
        std::nth_element(rimOrder.begin() + start, rimOrder.begin() + start + half,
                         rimOrder.begin() + start + count,
                         [&](uint32_t x, uint32_t y) {
                             return rim[x].first[axis] + rim[x].second[axis] <
                                    rim[y].first[axis] + rim[y].second[axis]; });
        const int32_t left = buildRim(start, half);
        const int32_t right = buildRim(start + half, count - half);
        rimNodes[index].left = left;
        rimNodes[index].right = right;
        return index;
    }

    double scaleRef = 1;

private:
    std::vector<V3> centroids;

    int32_t build(uint32_t start, uint32_t count)
    {
        Node node;
        node.start = start;
        node.count = count;
        Eigen::AlignedBox3d cbox;
        double area = 0;
        V3 weighted = V3::Zero();
        for (uint32_t i=start; i < start + count; ++i)
        {
            const Tri& tr = t[order[i]];
            const V3& a = v[tr[0]];
            const V3& b = v[tr[1]];
            const V3& c = v[tr[2]];
            node.box.extend(a);
            node.box.extend(b);
            node.box.extend(c);
            cbox.extend(centroids[order[i]]);
            const V3 an = 0.5 * (b - a).cross(c - a);
            const double ar = an.norm();
            node.normal += an;
            weighted += ar * centroids[order[i]];
            area += ar;
        }
        node.center = area > 0 ? V3(weighted / area) : V3(cbox.center());
        for (uint32_t i=start; i < start + count; ++i)
        {
            const Tri& tr = t[order[i]];
            for (int k=0; k < 3; ++k)
            {
                node.radius = std::max(node.radius, (v[tr[k]] - node.center).norm());
            }
        }

        const int32_t index = static_cast<int32_t>(nodes.size());
        nodes.push_back(node);

        const V3 extent = cbox.sizes();
        int axis = 0;
        if (extent.y() > extent[axis]) axis = 1;
        if (extent.z() > extent[axis]) axis = 2;
        if (count <= 4 || !(extent[axis] > 0))
        {
            return index;   // leaf
        }

        const uint32_t half = count / 2;
        std::nth_element(order.begin() + start, order.begin() + start + half,
                         order.begin() + start + count,
                         [&](uint32_t x, uint32_t y) {
                             return centroids[x][axis] < centroids[y][axis]; });
        const int32_t left = build(start, half);
        const int32_t right = build(start + half, count - half);
        nodes[index].left = left;
        nodes[index].right = right;
        return index;
    }
};

////////////////////////////////////////////////////////////////////////////////
// Cleaning, orientation and set-up

void weld(std::vector<V3>& verts, std::vector<Tri>& tris, double tol)
{
    std::unordered_map<std::array<int64_t, 3>, uint32_t, ArrayHash> map;
    map.reserve(verts.size());
    std::vector<uint32_t> remap(verts.size());
    std::vector<V3> out;
    out.reserve(verts.size() / 3 + 16);
    for (size_t i=0; i < verts.size(); ++i)
    {
        const V3& p = verts[i];
        const std::array<int64_t, 3> key = {std::llround(p.x() / tol),
                                            std::llround(p.y() / tol),
                                            std::llround(p.z() / tol)};
        auto r = map.emplace(key, static_cast<uint32_t>(out.size()));
        if (r.second)
        {
            out.push_back(p);
        }
        remap[i] = r.first->second;
    }
    for (auto& tr : tris)
    {
        for (auto& i : tr)
        {
            i = remap[i];
        }
    }
    verts.swap(out);
}

struct Edges
{
    // For every (triangle, local edge) slot: 3 * other triangle + its local
    // edge when exactly two triangles share the edge, -1 on a boundary
    // edge, -2 on a non-manifold one
    std::vector<int64_t> partner;
    std::vector<char> open;         // per triangle: touches a boundary / non-manifold edge
    size_t boundary = 0;
    size_t nonManifold = 0;

    void build(const std::vector<Tri>& tris)
    {
        struct HE { uint64_t key; uint32_t slot; };
        std::vector<HE> he;
        he.reserve(3 * tris.size());
        for (uint32_t i=0; i < tris.size(); ++i)
        {
            for (uint32_t k=0; k < 3; ++k)
            {
                const uint64_t a = tris[i][k], b = tris[i][(k + 1) % 3];
                he.push_back({(std::min(a, b) << 32) | std::max(a, b), 3 * i + k});
            }
        }
        std::sort(he.begin(), he.end(), [](const HE& x, const HE& y) { return x.key < y.key; });
        partner.assign(3 * tris.size(), -1);
        open.assign(tris.size(), 0);
        boundary = nonManifold = 0;
        for (size_t i=0; i < he.size(); )
        {
            size_t j = i;
            while (j < he.size() && he[j].key == he[i].key) ++j;
            if (j - i == 1)
            {
                boundary++;
                open[he[i].slot / 3] = 1;
            }
            else if (j - i == 2)
            {
                partner[he[i].slot] = he[i + 1].slot;
                partner[he[i + 1].slot] = he[i].slot;
            }
            else
            {
                nonManifold++;
                for (size_t m=i; m < j; ++m)
                {
                    partner[he[m].slot] = -2;
                    open[he[m].slot / 3] = 1;
                }
            }
            i = j;
        }
    }
};

// +1 when the three vertices of a triangle are in the order of their indices (or a cyclic shift of it), -1 when the winding is
// the other way round
int windingParity(const Tri& t)
{
    const int inversions = (t[0] > t[1]) + (t[0] > t[2]) + (t[1] > t[2]);
    return (inversions & 1) ? -1 : 1;
}

// Drops the triangles that have two corners at one vertex, and settles the duplicates: the same three vertices twice with the
// same winding are one triangle; with opposite windings they are a sheet of two layers with no volume between them (a face used
// from both sides, a fold), and both go -- keeping one of them would leave a single sheet standing in space, with an inside on
// one side of it.  Returns how many triangles were removed.
size_t dropDegenerateAndCancelDuplicates(std::vector<Tri>& tris)
{
    struct Group
    {
        int net = 0;
        int64_t firstPositive = -1, firstNegative = -1;
    };
    std::unordered_map<Tri, Group, ArrayHash> groups;
    groups.reserve(tris.size());
    for (size_t i=0; i < tris.size(); ++i)
    {
        const Tri& tr = tris[i];
        if (tr[0] == tr[1] || tr[1] == tr[2] || tr[0] == tr[2])
        {
            continue;
        }
        Tri key = tr;
        std::sort(key.begin(), key.end());
        Group& g = groups[key];
        const int sign = windingParity(tr);
        g.net += sign;
        if (sign > 0 && g.firstPositive < 0) g.firstPositive = static_cast<int64_t>(i);
        if (sign < 0 && g.firstNegative < 0) g.firstNegative = static_cast<int64_t>(i);
    }
    std::vector<char> keep(tris.size(), 0);
    for (const auto& kv : groups)
    {
        const Group& g = kv.second;
        if (g.net > 0) keep[g.firstPositive] = 1;
        else if (g.net < 0) keep[g.firstNegative] = 1;
    }
    std::vector<Tri> kept;
    kept.reserve(tris.size());
    for (size_t i=0; i < tris.size(); ++i)
    {
        if (keep[i])
        {
            kept.push_back(tris[i]);
        }
    }
    const size_t removed = tris.size() - kept.size();
    tris.swap(kept);
    return removed;
}

// A sliver is a triangle whose three corners lie on one line.  The tessellation of a solid makes them where one face splits an
// edge that its neighbour keeps whole: the sliver closes the crack.  Dropped, it would open the crack again, and a shell with a
// hole has no inside to speak of near it.  So the neighbour across the sliver's long edge is split at the sliver's middle vertex
// instead, which keeps the shell closed, and the sliver goes.  A sliver that has no neighbour on that edge, or more than one,
// is only dropped.  Returns how many were dealt with.
size_t splitSlivers(const std::vector<V3>& verts, std::vector<Tri>& tris, double diag)
{
    const double absolute = 1e-18 * diag * diag;
    size_t handled = 0;
    auto edgeKey = [](uint32_t a, uint32_t b) {
        return (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
    };
    const int passes = 8;
    for (int pass=0; pass < passes; ++pass)
    {
        struct Sliver { uint32_t tri, p, q, m; };
        std::vector<Sliver> slivers;
        for (uint32_t i=0; i < tris.size(); ++i)
        {
            const Tri& tr = tris[i];
            const V3 e[3] = {verts[tr[1]] - verts[tr[0]], verts[tr[2]] - verts[tr[1]], verts[tr[0]] - verts[tr[2]]};
            const double len[3] = {e[0].norm(), e[1].norm(), e[2].norm()};
            int k = 0;
            if (len[1] > len[k]) k = 1;
            if (len[2] > len[k]) k = 2;
            const double cross = e[0].cross(e[1]).norm();
            if (cross <= std::max(absolute, 1e-6 * len[k] * len[k]))
            {
                slivers.push_back({i, tr[k], tr[(k + 1) % 3], tr[(k + 2) % 3]});
            }
        }
        if (slivers.empty())
        {
            break;
        }
        std::unordered_map<uint64_t, std::vector<uint32_t>> users;
        users.reserve(2 * tris.size());
        for (uint32_t i=0; i < tris.size(); ++i)
        {
            for (int k=0; k < 3; ++k)
            {
                users[edgeKey(tris[i][k], tris[i][(k + 1) % 3])].push_back(i);
            }
        }
        std::vector<char> dead(tris.size(), 0), touched(tris.size(), 0);
        std::vector<Tri> added;
        const bool last = pass == passes - 1;
        for (const auto& s : slivers)
        {
            if (dead[s.tri])
            {
                continue;
            }
            int other = -1, count = 0;
            for (uint32_t j : users[edgeKey(s.p, s.q)])
            {
                if (j != s.tri)
                {
                    other = static_cast<int>(j);
                    ++count;
                }
            }
            if (count == 1 && !dead[other] && !touched[other])
            {
                // The neighbour (u, v, e) with the edge u -> v the sliver's long edge: it becomes (u, m, e) and (m, v, e)
                const Tri& o = tris[other];
                for (int k=0; k < 3; ++k)
                {
                    if (edgeKey(o[k], o[(k + 1) % 3]) == edgeKey(s.p, s.q))
                    {
                        const uint32_t u = o[k], v = o[(k + 1) % 3], e = o[(k + 2) % 3];
                        added.push_back({u, s.m, e});
                        added.push_back({s.m, v, e});
                        break;
                    }
                }
                dead[s.tri] = dead[other] = 1;
                touched[other] = 1;
                ++handled;
            }
            else if (count != 1 || last)
            {
                dead[s.tri] = 1;                // (nothing to split: it only goes)
                ++handled;
            }
            // (else: the neighbour was split by another sliver this pass: this one waits for the next)
        }
        std::vector<Tri> next;
        next.reserve(tris.size() + added.size());
        for (size_t i=0; i < tris.size(); ++i)
        {
            if (!dead[i])
            {
                next.push_back(tris[i]);
            }
        }
        next.insert(next.end(), added.begin(), added.end());
        tris.swap(next);
    }
    return handled;
}

std::shared_ptr<MeshData> buildMesh(std::vector<V3> verts, std::vector<Tri> tris,
                                    MeshImportInfo& info, std::string& error)
{
    Eigen::AlignedBox3d box;
    for (const auto& p : verts)
    {
        box.extend(p);
    }
    const double diag = box.diagonal().norm();
    if (!(diag > 0) || !std::isfinite(diag))
    {
        error = "the mesh has no extent (all its vertices coincide)";
        return nullptr;
    }

    // Weld duplicated vertices (STL stores every triangle's own copies)
    weld(verts, tris, diag * 1e-9);

    {   // Drop degenerate triangles, settle the duplicates, close the slivers (see the functions)
        info.dropped = dropDegenerateAndCancelDuplicates(tris);
        info.dropped += splitSlivers(verts, tris, diag);
        info.dropped += dropDegenerateAndCancelDuplicates(tris);
    }
    if (tris.empty())
    {
        error = "the mesh has no usable (non-degenerate) triangles";
        return nullptr;
    }
    const size_t n = tris.size();

    // Give every connected shell a consistent winding (keeping the one
    // most of its triangles already have)
    Edges edges;
    edges.build(tris);
    std::vector<int32_t> comp(n, -1);
    std::vector<char> flip(n, 0);
    std::vector<uint32_t> byComp;
    std::vector<size_t> compStart;
    byComp.reserve(n);
    for (uint32_t seed=0; seed < n; ++seed)
    {
        if (comp[seed] >= 0)
        {
            continue;
        }
        const int32_t c = static_cast<int32_t>(compStart.size());
        compStart.push_back(byComp.size());
        comp[seed] = c;
        byComp.push_back(seed);
        size_t flipped = 0;
        for (size_t q=compStart.back(); q < byComp.size(); ++q)
        {
            const uint32_t ti = byComp[q];
            flipped += flip[ti];
            for (uint32_t k=0; k < 3; ++k)
            {
                const int64_t pp = edges.partner[3 * ti + k];
                if (pp < 0)
                {
                    continue;
                }
                const uint32_t u = static_cast<uint32_t>(pp / 3), ku = static_cast<uint32_t>(pp % 3);
                if (comp[u] >= 0)
                {
                    continue;
                }
                // Consistent windings run along a shared edge in opposite
                // directions
                const bool fwd_t = tris[ti][k] < tris[ti][(k + 1) % 3];
                const bool fwd_u = tris[u][ku] < tris[u][(ku + 1) % 3];
                comp[u] = c;
                flip[u] = flip[ti] ^ (fwd_t == fwd_u ? 1 : 0);
                byComp.push_back(u);
            }
        }
        const size_t members = byComp.size() - compStart.back();
        if (2 * flipped > members)
        {
            for (size_t q=compStart.back(); q < byComp.size(); ++q)
            {
                flip[byComp[q]] ^= 1;
            }
        }
    }
    compStart.push_back(byComp.size());
    const size_t ncomp = compStart.size() - 1;

    std::vector<char> changed(n, 0);
    auto flipTri = [&](uint32_t i) {
        std::swap(tris[i][1], tris[i][2]);
        changed[i] ^= 1;
    };
    for (uint32_t i=0; i < n; ++i)
    {
        if (flip[i])
        {
            flipTri(i);
        }
    }

    // Closed shells must enclose positive volume, unless they are cavities
    // (inside an odd number of other closed shells)
    {
        std::vector<char> closed(ncomp, 1);
        std::vector<double> volume(ncomp, 0.0);
        for (uint32_t i=0; i < n; ++i)
        {
            if (edges.open[i])
            {
                closed[comp[i]] = 0;
            }
            const auto& tr = tris[i];
            volume[comp[i]] += verts[tr[0]].dot(verts[tr[1]].cross(verts[tr[2]])) / 6.0;
        }
        if (static_cast<double>(ncomp) * static_cast<double>(n) < 5e7)
        {
            // Per shell: bounding box, and sample vertices (its extremes
            // along each axis plus the first one)
            std::vector<Eigen::AlignedBox3d> cbox(ncomp);
            std::vector<std::array<uint32_t, 7>> samples(ncomp);
            for (size_t c=0; c < ncomp; ++c)
            {
                auto& sm = samples[c];
                sm.fill(tris[byComp[compStart[c]]][0]);
                for (size_t m=compStart[c]; m < compStart[c + 1]; ++m)
                {
                    for (auto vi : tris[byComp[m]])
                    {
                        const V3& p = verts[vi];
                        cbox[c].extend(p);
                        for (int a=0; a < 3; ++a)
                        {
                            if (p[a] < verts[sm[1 + 2 * a]][a]) sm[1 + 2 * a] = vi;
                            if (p[a] > verts[sm[2 + 2 * a]][a]) sm[2 + 2 * a] = vi;
                        }
                    }
                }
            }
            auto inside = [&](const V3& q, size_t o) {
                const V3 dir = V3(0.5773502691, 0.5773519, 0.5773483).normalized();
                int hits = 0;
                for (size_t m=compStart[o]; m < compStart[o + 1]; ++m)
                {
                    const auto& tr = tris[byComp[m]];
                    hits += rayHits(q, dir, verts[tr[0]], verts[tr[1]], verts[tr[2]]);
                }
                return (hits & 1) != 0;
            };
            for (size_t c=0; c < ncomp; ++c)
            {
                if (!closed[c])
                {
                    continue;
                }
                // A shell is a cavity of another only if it lies entirely
                // inside it (shells that merely overlap are both solids)
                int depth = 0;
                for (size_t o=0; o < ncomp; ++o)
                {
                    if (o == c || !closed[o] || !cbox[o].contains(cbox[c]))
                    {
                        continue;
                    }
                    bool all = true;
                    for (auto vi : samples[c])
                    {
                        if (!inside(verts[vi], o))
                        {
                            all = false;
                            break;
                        }
                    }
                    depth += all;
                }
                const bool want_positive = (depth % 2) == 0;
                if ((volume[c] > 0) != want_positive)
                {
                    for (size_t m=compStart[c]; m < compStart[c + 1]; ++m)
                    {
                        flipTri(byComp[m]);
                    }
                }
            }
        }
    }
    edges.build(tris);   // local edge slots moved with the flips

    auto data = std::make_shared<MeshData>();
    data->v = std::move(verts);
    data->t = std::move(tris);
    data->scaleRef = diag;
    const auto& v = data->v;
    const auto& t = data->t;

    // Normals
    data->faceNormal.resize(n);
    for (size_t i=0; i < n; ++i)
    {
        const V3 c = (v[t[i][1]] - v[t[i][0]]).cross(v[t[i][2]] - v[t[i][0]]);
        const double len = c.norm();
        data->faceNormal[i] = len > 0 ? V3(c / len) : V3(0, 0, 1);
    }
    data->vertexNormal.assign(v.size(), V3::Zero());
    std::vector<double> angleSum(v.size(), 0.0);
    for (size_t i=0; i < n; ++i)
    {
        for (int k=0; k < 3; ++k)
        {
            const V3 e1 = v[t[i][(k + 1) % 3]] - v[t[i][k]];
            const V3 e2 = v[t[i][(k + 2) % 3]] - v[t[i][k]];
            const double l1 = e1.norm(), l2 = e2.norm();
            if (l1 > 0 && l2 > 0)
            {
                const double angle = std::acos(std::max(-1.0, std::min(1.0, e1.dot(e2) / (l1 * l2))));
                data->vertexNormal[t[i][k]] += angle * data->faceNormal[i];
                angleSum[t[i][k]] += angle;
            }
        }
    }
    // (a vertex whose normals have mostly cancelled -- the faces around it face nearly opposite ways --
    // has no side to speak of: the winding number decides there)
    data->weakVertex.assign(v.size(), 0);
    for (size_t i=0; i < v.size(); ++i)
    {
        auto& vn = data->vertexNormal[i];
        const double len = vn.norm();
        data->weakVertex[i] = !(len > 0.3 * angleSum[i]);
        vn = len > 0 ? V3(vn / len) : V3(0, 0, 1);
    }
    data->edgeNormal.resize(3 * n);
    data->weakEdge.assign(3 * n, 0);
    for (size_t i=0; i < n; ++i)
    {
        for (int k=0; k < 3; ++k)
        {
            const int64_t pp = edges.partner[3 * i + k];
            data->edgeNormal[3 * i + k] = data->faceNormal[i] +
                (pp >= 0 ? data->faceNormal[pp / 3] : data->faceNormal[i]);
            // (the two faces more than about 145 degrees apart: the sum of two unit normals is shorter than 0.6)
            data->weakEdge[3 * i + k] = data->edgeNormal[3 * i + k].norm() < 0.6;
        }
    }

    // Statistics
    info.triangles = n;
    info.vertices = v.size();
    info.components = ncomp;
    info.boundaryEdges = edges.boundary;
    info.nonManifoldEdges = edges.nonManifold;
    info.watertight = edges.boundary == 0 && edges.nonManifold == 0;
    info.reoriented = 0;
    for (uint32_t i=0; i < n; ++i)
    {
        info.reoriented += changed[i];
    }

    // Where signs need the winding number (see MeshData::signedDistance)
    data->hasOpen = !info.watertight;
    data->openEdge.assign(3 * n, 0);
    data->openVertex.assign(v.size(), 0);
    for (size_t i=0; i < n; ++i)
    {
        for (int k=0; k < 3; ++k)
        {
            if (edges.partner[3 * i + k] < 0)
            {
                data->openEdge[3 * i + k] = 1;
                data->openVertex[t[i][k]] = 1;
                data->openVertex[t[i][(k + 1) % 3]] = 1;
            }
        }
    }
    {
        std::unordered_set<uint64_t> seen;
        for (size_t i=0; i < n; ++i)
        {
            for (int k=0; k < 3; ++k)
            {
                if (edges.partner[3 * i + k] < 0)
                {
                    const uint64_t a = t[i][k], b = t[i][(k + 1) % 3];
                    if (seen.insert((std::min(a, b) << 32) | std::max(a, b)).second)
                    {
                        data->rim.push_back({v[a], v[b]});
                    }
                }
            }
        }
        data->buildRimTree();
    }
    data->shellOf.resize(n);
    for (size_t i=0; i < n; ++i)
    {
        data->shellOf[i] = static_cast<uint32_t>(comp[i]);
    }
    if (ncomp > 5000)
    {
        data->overlapping = true;           // too many shells to sort out
    }
    else if (ncomp > 1)
    {
        std::vector<Eigen::AlignedBox3d> boxes(ncomp);
        for (size_t i=0; i < n; ++i)
        {
            for (auto vi : t[i])
            {
                boxes[comp[i]].extend(v[vi]);
            }
        }
        // Sweep along x to find the shells whose boxes meet another's
        std::vector<uint32_t> byX(ncomp);
        for (uint32_t c=0; c < ncomp; ++c)
        {
            byX[c] = c;
        }
        std::sort(byX.begin(), byX.end(), [&](uint32_t a, uint32_t b) {
            return boxes[a].min().x() < boxes[b].min().x(); });
        std::vector<char> meets(ncomp, 0);
        for (size_t i=0; i < ncomp; ++i)
        {
            for (size_t j=i + 1; j < ncomp &&
                 boxes[byX[j]].min().x() <= boxes[byX[i]].max().x(); ++j)
            {
                if (boxes[byX[i]].intersects(boxes[byX[j]]))
                {
                    meets[byX[i]] = meets[byX[j]] = 1;
                }
            }
        }
        for (uint32_t c=0; c < ncomp; ++c)
        {
            if (meets[c])
            {
                data->overlapping = true;
                data->overlapBoxes.push_back({c, boxes[c]});
            }
        }
        if (data->overlapBoxes.size() > 256)
        {
            data->overlapBoxes.clear();     // checked wholesale instead
        }
    }
    info.windingSign = data->hasOpen || data->overlapping;

    Eigen::AlignedBox3d bounds;
    for (const auto& p : v)
    {
        bounds.extend(p);
    }
    data->bounds = bounds;
    for (int i=0; i < 3; ++i)
    {
        info.lower[i] = bounds.min()[i];
        info.upper[i] = bounds.max()[i];
    }

    data->buildTree();
    return data;
}

////////////////////////////////////////////////////////////////////////////////
// Oracle

class MeshOracle : public OracleStorage<>
{
public:
    explicit MeshOracle(std::shared_ptr<const MeshData> m) : mesh(std::move(m)) {}

    void evalInterval(Interval& out) override
    {
        // The field is an exact distance (1-Lipschitz) except across holes
        const V3 lo = lower.cast<double>(), hi = upper.cast<double>();
        const V3 c = (lo + hi) / 2;
        const double r = (hi - lo).norm() / 2;
        bool soft = false;
        const double d = mesh->signedDistance(c, nullptr, &soft, &hint);
        // Blended near holes the field may change faster than a distance
        const double k = soft ? MeshData::SOFT_LIPSCHITZ : 1.0;
        const double low = mesh->unsignedMode ? std::max(0.0, d - k * r) : d - k * r;
        out = Interval(static_cast<float>(low), static_cast<float>(d + k * r));
    }

    void evalPoint(float& out, size_t index) override
    {
        const V3 p = points.col(index).matrix().cast<double>();
        out = static_cast<float>(mesh->signedDistance(p, nullptr, nullptr, &hint));
    }

    void checkAmbiguous(
            Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>,
                         1, Eigen::Dynamic> /* out */) override
    {
        // Never ambiguous: one gradient per point (see evalFeatures)
    }

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        const V3 p = points.col(0).matrix().cast<double>();
        V3 g;
        mesh->signedDistance(p, &g, nullptr, &hint);
        const Eigen::Vector3f gf = g.cast<float>();
        out.push_back(Feature(gf));
    }

private:
    std::shared_ptr<const MeshData> mesh;
    // the triangle the last point was closest to (the cells of the renderer ask about points that lie together)
    uint32_t hint = std::numeric_limits<uint32_t>::max();
};

class MeshOracleClause : public OracleClause
{
public:
    explicit MeshOracleClause(std::shared_ptr<const MeshData> m) : mesh(std::move(m)) {}

    std::unique_ptr<Oracle> getOracle() const override
    {
        return std::make_unique<MeshOracle>(mesh);
    }

    std::string name() const override { return "MeshOracle"; }

    // What the mesh is, for keeping across runs: a hash of its triangles (made once, when asked for)
    std::string persistentKey() const override
    {
        std::call_once(keyOnce, [this] {
            // (of the surface, not of the order the mesher wrote it in)
            key = "mesh#" + meshContentKey(mesh->v, mesh->t) + (mesh->unsignedMode ? "#u" : "#s") + "#" +
                  std::to_string(mesh->t.size());
        });
        return key;
    }

private:
    std::shared_ptr<const MeshData> mesh;
    mutable std::once_flag keyOnce;
    mutable std::string key;
};

}   // anonymous namespace

namespace {
// Scales and checks the triangles, then builds the tree (shared by the
// file and array entry points)
Tree finishMesh(std::vector<V3> verts, std::vector<Tri> tris, double scale,
                MeshImportInfo& info, bool& ok, std::string& error);
}

Tree importMeshTree(const std::string& path, double scale,
                    MeshImportInfo& info, bool& ok, std::string& error)
{
    ok = false;
    info = MeshImportInfo();

    std::string data;
    if (!readAll(path, data, error))
    {
        return Tree::invalid();
    }

    std::string ext = std::filesystem::u8path(path).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<V3> verts;
    std::vector<Tri> tris;
    bool parsed = false;
    if (ext == ".obj")
    {
        parsed = parseObj(data, verts, tris, error);
    }
    else if (ext == ".stl")
    {
        parsed = parseStl(data, verts, tris, error);
    }
    else
    {
        error = "unsupported mesh format '" + ext + "' (use .stl or .obj)";
        return Tree::invalid();
    }
    data.clear();
    data.shrink_to_fit();
    if (!parsed)
    {
        return Tree::invalid();
    }
    return finishMesh(std::move(verts), std::move(tris), scale, info, ok, error);
}

Tree meshTreeFromArrays(const float* xyz, size_t vertex_count,
                        const uint32_t* tri, size_t tri_count, double scale,
                        MeshImportInfo& info, bool& ok, std::string& error)
{
    ok = false;
    info = MeshImportInfo();
    if (!xyz || !tri || !vertex_count || !tri_count)
    {
        error = "the mesh has no triangles";
        return Tree::invalid();
    }
    std::vector<V3> verts(vertex_count);
    for (size_t i=0; i < vertex_count; ++i)
    {
        verts[i] = V3(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]);
    }
    std::vector<Tri> tris(tri_count);
    for (size_t i=0; i < tri_count; ++i)
    {
        for (int k=0; k < 3; ++k)
        {
            if (tri[3 * i + k] >= vertex_count)
            {
                error = "a triangle refers to a vertex that does not exist";
                return Tree::invalid();
            }
            tris[i][k] = tri[3 * i + k];
        }
    }
    return finishMesh(std::move(verts), std::move(tris), scale, info, ok, error);
}

namespace {
Tree finishMesh(std::vector<V3> verts, std::vector<Tri> tris, double scale,
                MeshImportInfo& info, bool& ok, std::string& error)
{
    if (!(scale > 0) || !std::isfinite(scale))
    {
        error = "invalid scale factor";
        return Tree::invalid();
    }
    for (auto& p : verts)
    {
        if (!p.allFinite())
        {
            error = "the mesh contains invalid (NaN or infinite) coordinates";
            return Tree::invalid();
        }
        p *= scale;
    }

    auto mesh = buildMesh(std::move(verts), std::move(tris), info, error);
    if (!mesh)
    {
        return Tree::invalid();
    }
    ok = true;
    return Tree(std::make_unique<MeshOracleClause>(mesh));
}
}   // anonymous namespace

Tree patchTreeFromArrays(const float* xyz, size_t vertex_count, const uint32_t* tri, size_t tri_count,
                         const uint8_t* selected, MeshImportInfo& info, bool& ok, std::string& error)
{
    ok = false;
    info = MeshImportInfo();
    if (!xyz || !tri || !selected || !vertex_count || !tri_count)
    {
        error = "the patch has no triangles";
        return Tree::invalid();
    }
    // (only the vertices of the selected triangles: the box of the patch is theirs)
    std::vector<V3> verts;
    std::vector<int64_t> renumbered(vertex_count, -1);
    std::vector<Tri> tris;
    for (size_t i=0; i < tri_count; ++i)
    {
        if (!selected[i]) continue;
        Tri t;
        for (int k=0; k < 3; ++k)
        {
            const uint32_t vi = tri[3 * i + k];
            if (vi >= vertex_count)
            {
                error = "a triangle refers to a vertex that does not exist";
                return Tree::invalid();
            }
            if (renumbered[vi] < 0)
            {
                const V3 p(xyz[3 * vi], xyz[3 * vi + 1], xyz[3 * vi + 2]);
                if (!p.allFinite())
                {
                    error = "the surface contains invalid (NaN or infinite) coordinates";
                    return Tree::invalid();
                }
                renumbered[vi] = static_cast<int64_t>(verts.size());
                verts.push_back(p);
            }
            t[k] = static_cast<uint32_t>(renumbered[vi]);
        }
        tris.push_back(t);
    }
    if (tris.empty())
    {
        error = "no triangle is selected";
        return Tree::invalid();
    }
    auto mesh = buildMesh(std::move(verts), std::move(tris), info, error);
    if (!mesh)
    {
        return Tree::invalid();
    }
    mesh->unsignedMode = true;
    ok = true;
    return Tree(std::make_unique<MeshOracleClause>(mesh));
}

}   // namespace mesh
}   // namespace libfive
