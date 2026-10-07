// Included by surface_cells.cpp, inside its anonymous namespace: the cells of a surface laid out by a GLOBAL solve, on a scaffold
// that cannot have a hole.
//
// CLOSED AND MADE OF QUADS BY CONSTRUCTION.  A layout that grows over the surface and mends what it leaves is never sure to be
// closed, and a lattice read off a field vertex by vertex leaves holes wherever a witness is missing.  So the layout is built so
// that it CANNOT be open, and is checked:
//
//   scaffold     the surface is covered by a closed triangle mesh made from the field alone (marching tetrahedra on a grid
//                refined towards the surface: every triangle edge is shared by exactly two triangles whatever the field is,
//                so the mesh is closed; its vertices are moved onto the surface and take its normal from the gradient, and
//                where the surface turns sharply they are moved onto the edge).  It is only the connection between points that
//                are on the surface: nothing is measured on it.
//   the field    a direction (a 4-RoSy field) and a lattice point at every vertex, solved over the whole surface on a
//                hierarchy of coarser and coarser vertex graphs, as in the quad remeshers; a sharp edge is a line the field
//                follows.  This decides how the cells LOOK.
//   squares      every triangle lies in one square of the lattice the field makes: that square is its label.
//   regions      the regions of triangles that have the same label.  A partition of a closed surface into connected regions is
//                closed whatever the labels are: the boundary between two regions is edges of the scaffold, every such edge has a
//                region on each side, and a corner is a vertex where three regions meet.  A sliver is joined to a neighbour, a
//                region with six corners or more is cut in two along a line between two of its corners.
//   cells        a region with k corners is k quads (a corner, the middle of the side after it, the middle of the region, the
//                middle of the side before it); the middle of a side is one node for the two regions that have the side, so the
//                quads fit edge to edge.  The nodes are then moved over the surface until the cells are even.
//   the check    every cell has four different corners and every edge is on two cells (one only on the edge of the scaffold).
//
// A bad field makes ugly cells, never a hole and never an overlap.  The scaffold is closed exactly where the surface is (a surface
// cut by the box or by a region has an edge there, and only there).  What comes out is what the rest takes: nodes on the surface,
// cells of four nodes, and the line each edge follows.

// ---------------------------------------------------------------------------------------------------------------------
// The cubes the surface passes through

struct NearCubes
{
    std::vector<std::array<int, 3>> cubes;      // the lower corner of every cube (in steps of h from `origin`)
    V3 origin = V3::Zero();
    double h = 0;
};

// The scaffold of a PATCH of a surface (a selection): the surface is the zero set of the field, as for any sheet, and only the part of it by the
// patch is covered -- the cubes within `slack` of the patch (where the field `region` is below `slack`), and then the scaffold itself, CUT AT the
// outline of the patch, where `region` is zero.  Nothing about what lies behind the surface is looked at: a surface has no thickness
struct PatchMask
{
    Field* region = nullptr;
    double slack = 0.0;
};

NearCubes nearCubes(Field& F, const V3& lo, const V3& hi, double h0, int levels, bool say, const PatchMask& mask = PatchMask())
{
    using Vox = std::array<int, 3>;
    NearCubes out;
    const V3 ext = hi - lo;
    double v0 = h0 * std::pow(2.0, levels);
    auto count = [&](double v) { return (std::floor(ext[0] / v) + 1) * (std::floor(ext[1] / v) + 1) * (std::floor(ext[2] / v) + 1); };
    while (count(v0) > 3e6) v0 *= 1.3;
    const int deepest = std::max(1, int(std::lround(std::log2(v0 / h0))));
    std::vector<double> size(size_t(deepest) + 1);
    for (int k = 0; k <= deepest; ++k) size[size_t(k)] = v0 / std::pow(2.0, k);
    // (a cube the surface passes through has its middle within half its diagonal of the surface; with room for a field that is
    // not a distance)
    const double reach = 1.25 * 0.8660254;
    const V3 origin = lo + V3(0.1373 * v0, 0.0719 * v0, 0.1131 * v0);
    auto centre = [&](const Vox& v, double s) -> V3 { return origin + V3((v[0] + 0.5) * s, (v[1] + 0.5) * s, (v[2] + 0.5) * s); };
    std::vector<Vox> cur;
    {
        const int nx = int(std::floor(ext[0] / v0)) + 1, ny = int(std::floor(ext[1] / v0)) + 1, nz = int(std::floor(ext[2] / v0)) + 1;
        std::vector<V3> pts;
        std::vector<Vox> all;
        std::vector<double> f, fr;
        std::vector<V3> g, gr;
        auto flush = [&]() {
            if (pts.empty()) return;
            F.eval(pts, f, g);
            if (mask.region) mask.region->eval(pts, fr, gr);
            for (size_t k = 0; k < pts.size(); ++k)
            {
                const double gn = g[k].norm();
                if (!std::isfinite(f[k]) || !(gn > 1e-12)) continue;
                if (mask.region && !(fr[k] <= reach * v0 + mask.slack)) continue;
                if (std::abs(f[k]) <= reach * v0 * gn) cur.push_back(all[k]);
            }
            pts.clear();
            all.clear();
        };
        for (int i = 0; i < nx; ++i)
        {
            for (int j = 0; j < ny; ++j)
                for (int k = 0; k < nz; ++k)
                {
                    pts.push_back(centre({i, j, k}, v0));
                    all.push_back({i, j, k});
                }
            if (pts.size() >= 40000) flush();
        }
        flush();
    }
    for (int k = 1; k <= deepest; ++k)
    {
        const double s = size[size_t(k)];
        std::vector<V3> pts;
        std::vector<Vox> kids;
        for (const Vox& v : cur)
            for (int a = 0; a < 2; ++a)
                for (int b = 0; b < 2; ++b)
                    for (int c = 0; c < 2; ++c)
                    {
                        const Vox w = {2 * v[0] + a, 2 * v[1] + b, 2 * v[2] + c};
                        kids.push_back(w);
                        pts.push_back(centre(w, s));
                    }
        std::vector<double> f, fr;
        std::vector<V3> g, gr;
        F.eval(pts, f, g);
        if (mask.region) mask.region->eval(pts, fr, gr);
        cur.clear();
        for (size_t i = 0; i < kids.size(); ++i)
        {
            const double gn = g[i].norm();
            if (!std::isfinite(f[i]) || !(gn > 1e-12)) continue;
            if (mask.region && !(fr[i] <= reach * s + mask.slack)) continue;
            if (std::abs(f[i]) <= reach * s * gn) cur.push_back(kids[i]);
        }
        stageSet(double(k + 1) / double(deepest + 1), "level " + std::to_string(k + 1) + " of " + std::to_string(deepest + 1) + " of the cubes the surface passes through");
    }
    out.cubes = std::move(cur);
    out.origin = origin;
    out.h = size.back();
    if (say) std::fprintf(stderr, "[quads] %zu cubes of %.2f mm that the surface may pass through\n", out.cubes.size(), out.h);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The scaffold: a closed triangle mesh of the surface (marching tetrahedra)

struct TriMesh
{
    std::vector<V3> p, n;                       // on the surface, and its normal there
    std::vector<std::array<int, 3>> tri;        // counterclockwise seen from outside
};

// Every cube is cut into six tetrahedra that share its diagonal (the same cut in every cube, so that the faces of neighbouring
// cubes agree).  A tetrahedron whose corners are not all on one side of the surface gets one triangle or two, with its corners on
// the edges that cross.  A crossing is one vertex for every tetrahedron that has that edge, so the triangles fit exactly: every
// edge of the mesh is the edge of two triangles.
TriMesh marchingTetrahedra(Field& F, const NearCubes& nc, bool say, const PatchMask& mask = PatchMask())
{
    TriMesh m;
    const double h = nc.h;
    std::unordered_map<uint64_t, int> cornerId;
    std::vector<V3> cpos;
    auto cornerKey = [](int i, int j, int k) {
        const uint64_t B = 1u << 20;
        return ((uint64_t(i + B)) << 42) | ((uint64_t(j + B)) << 21) | uint64_t(k + B);
    };
    for (const auto& c : nc.cubes)
        for (int dx = 0; dx < 2; ++dx)
            for (int dy = 0; dy < 2; ++dy)
                for (int dz = 0; dz < 2; ++dz)
                {
                    const uint64_t key = cornerKey(c[0] + dx, c[1] + dy, c[2] + dz);
                    if (cornerId.emplace(key, int(cpos.size())).second)
                        cpos.push_back(nc.origin + h * V3(c[0] + dx, c[1] + dy, c[2] + dz));
                }
    std::vector<double> cf;
    {
        std::vector<V3> g;
        F.eval(cpos, cf, g);
    }
    // (inside is f < 0; a corner at exactly zero is outside, so that every corner has one side)
    auto inside = [&](int c) { return cf[size_t(c)] < 0.0; };
    static const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    std::map<std::pair<int, int>, int> vertexOf;
    std::vector<V3> raw;
    auto vertexOn = [&](int a, int b) -> int {
        const std::pair<int, int> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        const auto it = vertexOf.find(key);
        if (it != vertexOf.end()) return it->second;
        const double fa = cf[size_t(key.first)], fb = cf[size_t(key.second)];
        double t = fa / (fa - fb);
        t = std::min(0.98, std::max(0.02, t));
        raw.push_back(cpos[size_t(key.first)] + t * (cpos[size_t(key.second)] - cpos[size_t(key.first)]));
        const int id = int(raw.size()) - 1;
        vertexOf[key] = id;
        return id;
    };
    for (const auto& c : nc.cubes)
        for (const auto& pm : perms)
        {
            int d[3] = {0, 0, 0};
            int corner[4];
            corner[0] = cornerId.at(cornerKey(c[0], c[1], c[2]));
            for (int s = 0; s < 3; ++s)
            {
                d[pm[s]] = 1;
                corner[s + 1] = cornerId.at(cornerKey(c[0] + d[0], c[1] + d[1], c[2] + d[2]));
            }
            int in[4], nin = 0;
            for (int s = 0; s < 4; ++s)
            {
                in[s] = inside(corner[s]) ? 1 : 0;
                nin += in[s];
            }
            if (nin == 0 || nin == 4) continue;
            if (nin == 1 || nin == 3)
            {
                const int lone = nin == 1 ? 1 : 0;                       // (the corner on its own side)
                int a = -1;
                for (int s = 0; s < 4; ++s)
                    if (in[s] == lone) a = s;
                int o[3], k = 0;
                for (int s = 0; s < 4; ++s)
                    if (s != a) o[k++] = s;
                m.tri.push_back({vertexOn(corner[a], corner[o[0]]), vertexOn(corner[a], corner[o[1]]), vertexOn(corner[a], corner[o[2]])});
            }
            else
            {
                int i0 = -1, i1 = -1, o0 = -1, o1 = -1;
                for (int s = 0; s < 4; ++s)
                    if (in[s]) (i0 < 0 ? i0 : i1) = s;
                    else (o0 < 0 ? o0 : o1) = s;
                const int v1 = vertexOn(corner[i0], corner[o0]), v2 = vertexOn(corner[i0], corner[o1]);
                const int v3 = vertexOn(corner[i1], corner[o1]), v4 = vertexOn(corner[i1], corner[o0]);
                m.tri.push_back({v1, v2, v3});
                m.tri.push_back({v1, v3, v4});
            }
        }
    // the vertices onto the surface (the ones that do not get there stay where the edge crosses it), and the normals
    std::vector<V3> pts = raw, nn;
    std::vector<char> ok;
    projectToSurface(F, pts, nn, ok, h, 1e-4);
    for (size_t i = 0; i < pts.size(); ++i)
        if (!ok[i] || (pts[i] - raw[i]).norm() > 0.75 * h) pts[i] = raw[i];
    if (mask.region)
    {
        // The patch: the scaffold is cut AT the outline of the patch, where the field `region` is zero.  A triangle that crosses it is cut
        // there: the places where its edges cross are new vertices (one for every edge, shared by the triangles that have it, and put onto the
        // surface), and the part that is inside is kept.  So the scaffold ends ON the outline, not a step short of it or past it, and the
        // cells that are laid out over it end there.  The vertices that no triangle has left are dropped and the rest numbered again.
        std::vector<double> fr;
        std::vector<V3> gr;
        mask.region->eval(pts, fr, gr);
        std::map<std::pair<int, int>, int> crossing;
        std::vector<V3> extra;                       // (where the edges cross, before they are put onto the surface)
        auto crossAt = [&](int in, int out) {
            const std::pair<int, int> key = in < out ? std::make_pair(in, out) : std::make_pair(out, in);
            const auto it = crossing.find(key);
            if (it != crossing.end()) return it->second;
            const double ri = fr[size_t(in)], ro = fr[size_t(out)];
            double t = ri / (ri - ro);               // (ri <= 0 < ro)
            t = std::min(0.98, std::max(0.02, t));
            const int id = int(pts.size()) + int(extra.size());
            extra.push_back(pts[size_t(in)] + t * (pts[size_t(out)] - pts[size_t(in)]));
            crossing[key] = id;
            return id;
        };
        std::vector<std::array<int, 3>> kept;
        for (const auto& t : m.tri)
        {
            int in[3];
            for (int s = 0; s < 3; ++s) in[s] = fr[size_t(t[size_t(s)])] <= 0.0 ? 1 : 0;
            const int count = in[0] + in[1] + in[2];
            if (count == 3)
                kept.push_back(t);
            else if (count == 1)
            {
                const int s = in[0] ? 0 : (in[1] ? 1 : 2);
                const int u = t[size_t(s)], v = t[size_t((s + 1) % 3)], w = t[size_t((s + 2) % 3)];
                kept.push_back({u, crossAt(u, v), crossAt(u, w)});
            }
            else if (count == 2)
            {
                const int s = !in[0] ? 0 : (!in[1] ? 1 : 2);
                const int w = t[size_t(s)], u = t[size_t((s + 1) % 3)], v = t[size_t((s + 2) % 3)];      // (w is outside)
                const int xvw = crossAt(v, w), xwu = crossAt(u, w);
                kept.push_back({u, v, xvw});
                kept.push_back({u, xvw, xwu});
            }
        }
        if (!extra.empty())
        {
            std::vector<V3> moved = extra, nrm;
            std::vector<char> okm;
            projectToSurface(F, moved, nrm, okm, h, 1e-4);
            for (size_t k = 0; k < extra.size(); ++k)
                if (okm[k] && (moved[k] - extra[k]).norm() <= 0.75 * h) extra[k] = moved[k];
            pts.insert(pts.end(), extra.begin(), extra.end());
        }
        m.tri.swap(kept);
        std::vector<int> number(pts.size(), -1);
        int count = 0;
        for (const auto& t : m.tri)
            for (int k = 0; k < 3; ++k)
                if (number[size_t(t[size_t(k)])] < 0) number[size_t(t[size_t(k)])] = count++;
        std::vector<V3> packed(size_t(count), V3::Zero());
        for (size_t i = 0; i < pts.size(); ++i)
            if (number[i] >= 0) packed[size_t(number[i])] = pts[i];
        for (auto& t : m.tri)
            for (int k = 0; k < 3; ++k) t[size_t(k)] = number[size_t(t[size_t(k)])];
        pts.swap(packed);
    }
    std::vector<double> fv;
    std::vector<V3> gv;
    F.eval(pts, fv, gv);
    m.p = pts;
    m.n.resize(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) m.n[i] = gv[i].norm() > 1e-12 ? V3(gv[i].normalized()) : V3(0, 1, 0);
    // counterclockwise seen from outside: first as the normals say, then made consistent over the mesh (an edge is walked once
    // each way) -- the way round of every connected piece is the one that most of its triangles have by their normals
    std::vector<std::vector<int>> trisOfEdge;
    std::unordered_map<uint64_t, int> edgeIndex;
    auto ek = [](int a, int b) { return (uint64_t(uint32_t(std::min(a, b))) << 32) | uint32_t(std::max(a, b)); };
    for (size_t t = 0; t < m.tri.size(); ++t)
        for (int s = 0; s < 3; ++s)
        {
            const uint64_t key = ek(m.tri[t][size_t(s)], m.tri[t][size_t((s + 1) % 3)]);
            auto it = edgeIndex.find(key);
            if (it == edgeIndex.end())
            {
                it = edgeIndex.emplace(key, int(trisOfEdge.size())).first;
                trisOfEdge.emplace_back();
            }
            trisOfEdge[size_t(it->second)].push_back(int(t));
        }
    std::vector<int> piece(m.tri.size(), -1);
    int pieces = 0;
    auto directed = [&](size_t t, int a, int b) {
        for (int s = 0; s < 3; ++s)
            if (m.tri[t][size_t(s)] == a && m.tri[t][size_t((s + 1) % 3)] == b) return true;
        return false;
    };
    for (size_t t0 = 0; t0 < m.tri.size(); ++t0)
    {
        if (piece[t0] >= 0) continue;
        std::vector<size_t> stack = {t0}, members;
        piece[t0] = pieces;
        while (!stack.empty())
        {
            const size_t t = stack.back();
            stack.pop_back();
            members.push_back(t);
            for (int s = 0; s < 3; ++s)
            {
                const int a = m.tri[t][size_t(s)], b = m.tri[t][size_t((s + 1) % 3)];
                for (int u : trisOfEdge[size_t(edgeIndex[ek(a, b)])])
                {
                    if (size_t(u) == t || piece[size_t(u)] >= 0) continue;
                    piece[size_t(u)] = pieces;
                    if (directed(size_t(u), a, b)) std::swap(m.tri[size_t(u)][1], m.tri[size_t(u)][2]);         // (walked the same way: turned over)
                    stack.push_back(size_t(u));
                }
            }
        }
        double agree = 0;
        for (size_t t : members)
        {
            const V3 nrm = (m.p[size_t(m.tri[t][1])] - m.p[size_t(m.tri[t][0])]).cross(m.p[size_t(m.tri[t][2])] - m.p[size_t(m.tri[t][0])]);
            agree += nrm.dot(m.n[size_t(m.tri[t][0])] + m.n[size_t(m.tri[t][1])] + m.n[size_t(m.tri[t][2])]) > 0 ? 1.0 : -1.0;
        }
        if (agree < 0)
            for (size_t t : members) std::swap(m.tri[t][1], m.tri[t][2]);
        ++pieces;
    }
    if (say)
    {
        size_t boundary = 0, nonManifold = 0;
        for (const auto& e : trisOfEdge)
        {
            if (e.size() == 1) ++boundary;
            else if (e.size() > 2) ++nonManifold;
        }
        std::fprintf(stderr, "[quads] scaffold: %zu triangles, %zu vertices, %d pieces; %zu edges on one triangle only (the edge of the box), %zu on more than two\n",
                     m.tri.size(), m.p.size(), pieces, boundary, nonManifold);
    }
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------
// Creases of the scaffold

struct CreaseInfo
{
    std::vector<char> is;           // the vertex is on a crease
    std::vector<V3> n2, t;          // the normal of the face beyond (n is the one of the face it was found over), and the way the crease runs
    size_t count = 0;
};

// Where the surface turns sharply between the two ends of an edge of the scaffold, a crease passes between them.  It is found by
// halving (every middle point carried onto the surface, and given to the end whose normal it has: a turn that goes on being a
// turn as the ends close in is a crease, one that dies away is a bend), and the nearer end of the edge is MOVED onto it.  Only
// positions change: the mesh stays as closed as it was.  The vertex then has the two normals and the direction of the crease.
CreaseInfo snapCreases(Field& F, TriMesh& m, double h, double cosCrease)
{
    CreaseInfo ci;
    const size_t N = m.p.size();
    ci.is.assign(N, 0);
    ci.n2.assign(N, V3::Zero());
    ci.t.assign(N, V3::Zero());
    std::set<std::pair<int, int>> edges;
    for (const auto& t : m.tri)
        for (int s = 0; s < 3; ++s)
        {
            const int a = t[size_t(s)], b = t[size_t((s + 1) % 3)];
            edges.insert({std::min(a, b), std::max(a, b)});
        }
    std::vector<std::pair<int, int>> cand;
    std::vector<V3> lo, hi, nlo, nhi;
    for (const auto& e : edges)
    {
        const double dot = m.n[size_t(e.first)].dot(m.n[size_t(e.second)]);
        if (dot > cosCrease || dot < -0.3) continue;
        cand.push_back(e);
        lo.push_back(m.p[size_t(e.first)]);
        hi.push_back(m.p[size_t(e.second)]);
        nlo.push_back(m.n[size_t(e.first)]);
        nhi.push_back(m.n[size_t(e.second)]);
    }
    std::vector<char> alive(cand.size(), 1);
    for (int it = 0; it < 12 && !cand.empty(); ++it)
    {
        std::vector<V3> mids(cand.size()), chord(cand.size()), mn;
        std::vector<char> mok;
        for (size_t k = 0; k < cand.size(); ++k) chord[k] = mids[k] = 0.5 * (lo[k] + hi[k]);
        projectToSurface(F, mids, mn, mok, 0.5 * h, 1e-4);
        for (size_t k = 0; k < cand.size(); ++k)
        {
            if (!alive[k]) continue;
            if (!mok[k] || (mids[k] - chord[k]).norm() > 0.45 * h)
            {
                alive[k] = 0;
                continue;
            }
            if (mn[k].dot(nlo[k]) >= mn[k].dot(nhi[k]))
            {
                lo[k] = mids[k];
                nlo[k] = mn[k];
            }
            else
            {
                hi[k] = mids[k];
                nhi[k] = mn[k];
            }
        }
    }
    std::vector<double> bestD(N, 1e300);
    for (size_t k = 0; k < cand.size(); ++k)
    {
        if (!alive[k] || (hi[k] - lo[k]).norm() > 0.2 * h || nlo[k].dot(nhi[k]) > 0.5 * (1.0 + cosCrease) || nlo[k].cross(nhi[k]).norm() < 0.4) continue;
        const V3 c = 0.5 * (lo[k] + hi[k]);
        const int ends[2] = {cand[k].first, cand[k].second};
        const int v = (m.p[size_t(ends[0])] - c).squaredNorm() <= (m.p[size_t(ends[1])] - c).squaredNorm() ? ends[0] : ends[1];
        const double d = (m.p[size_t(v)] - c).squaredNorm();
        if (d >= bestD[size_t(v)]) continue;
        bestD[size_t(v)] = d;
        ci.is[size_t(v)] = 1;
        ci.n2[size_t(v)] = nhi[k];
        ci.t[size_t(v)] = nlo[k].cross(nhi[k]).normalized();
        m.p[size_t(v)] = c;
        m.n[size_t(v)] = nlo[k];
    }
    for (size_t v = 0; v < N; ++v) ci.count += ci.is[v] ? 1 : 0;
    return ci;
}

// ---------------------------------------------------------------------------------------------------------------------
// The levels

struct QLevel
{
    std::vector<V3> p, n;                                   // where, and the normal there
    std::vector<double> weight;                             // how many samples each stands for
    std::vector<std::vector<std::pair<int, double>>> adj;   // neighbours, and how strongly they are tied
    std::vector<int> parent;                                // in the next coarser level (-1 on the coarsest)
    std::vector<V3> q;                                      // the direction (a unit vector in the tangent plane)
    std::vector<V3> o;                                      // the lattice point
    std::vector<char> fixed;                                // on a crease: the direction is fq and the lattice point is on
    std::vector<V3> fq, fc;                                 // the line through fc along fq
    std::vector<V3> n2;                                     // on a crease: the normal of the other face (n is the one of the first)
};

// the normal a vertex has towards another point: on a crease, that of the face the other point is on
inline V3 nFor(const QLevel& L, size_t i, const V3& other)
{
    if (L.n2.empty() || L.n2[i].squaredNorm() < 0.25) return L.n[i];
    return L.n[i].dot(other) >= L.n2[i].dot(other) ? L.n[i] : L.n2[i];
}

// the quarter turns of two directions that bring them nearest: `a` is one of the four of q0 and `b` of q1, with a . b near 1
inline void rosyAlign(const V3& q0, const V3& n0, const V3& q1, const V3& n1, V3& a, V3& b)
{
    const V3 A[2] = {q0, n0.cross(q0)}, B[2] = {q1, n1.cross(q1)};
    double best = -1;
    int ba = 0, bb = 0;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
        {
            const double s = std::abs(A[i].dot(B[j]));
            if (s > best)
            {
                best = s;
                ba = i;
                bb = j;
            }
        }
    a = A[ba];
    b = B[bb] * (A[ba].dot(B[bb]) >= 0 ? 1.0 : -1.0);
}

inline V3 inPlane(const V3& v, const V3& n)
{
    return v - n.dot(v) * n;
}

// the lattice point (of the lattice with the point `o` and the axes q and n x q, spacing rho) nearest to x, in the tangent plane
inline V3 nearestLattice(const V3& x, const V3& o, const V3& q, const V3& n, double rho)
{
    const V3 t = n.cross(q);
    const V3 d = x - o;
    return o + rho * (std::round(d.dot(q) / rho) * q + std::round(d.dot(t) / rho) * t);
}

// the same set of points, with fixed order: a shuffle that does not depend on the library
std::vector<int> shuffled(int n, uint64_t seed)
{
    std::vector<int> a(size_t(std::max(0, n)));
    std::iota(a.begin(), a.end(), 0);
    uint64_t s = seed * 6364136223846793005ull + 1442695040888963407ull;
    for (int i = n - 1; i > 0; --i)
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        const int j = int((s >> 33) % uint64_t(i + 1));
        std::swap(a[size_t(i)], a[size_t(j)]);
    }
    return a;
}

// pairs of neighbours that agree about their normal are one vertex of the next level
QLevel coarsen(const QLevel& fine, std::vector<int>& childToParent)
{
    const int N = int(fine.p.size());
    std::vector<int> mate(size_t(N), -1);
    const std::vector<int> order = shuffled(N, 7);
    for (int i : order)
    {
        if (mate[size_t(i)] >= 0) continue;
        int best = -1;
        double bestScore = -1e300;
        for (const auto& e : fine.adj[size_t(i)])
        {
            const int j = e.first;
            if (mate[size_t(j)] >= 0 || j == i) continue;
            const double nd = fine.n[size_t(i)].dot(fine.n[size_t(j)]);
            if (nd < 0.6) continue;
            const double score = e.second * (nd - 0.5) - 0.0 * (fine.p[size_t(i)] - fine.p[size_t(j)]).norm();
            if (score > bestScore || (score == bestScore && j < best))
            {
                bestScore = score;
                best = j;
            }
        }
        if (best >= 0)
        {
            mate[size_t(i)] = best;
            mate[size_t(best)] = i;
        }
    }
    childToParent.assign(size_t(N), -1);
    QLevel up;
    for (int i = 0; i < N; ++i)
    {
        if (childToParent[size_t(i)] >= 0) continue;
        const int id = int(up.p.size());
        const int j = mate[size_t(i)];
        childToParent[size_t(i)] = id;
        if (j >= 0)
        {
            childToParent[size_t(j)] = id;
            const double wi = fine.weight[size_t(i)], wj = fine.weight[size_t(j)];
            up.p.push_back((wi * fine.p[size_t(i)] + wj * fine.p[size_t(j)]) / (wi + wj));
            const V3 nn = wi * fine.n[size_t(i)] + wj * fine.n[size_t(j)];
            up.n.push_back(nn.norm() > 1e-9 ? V3(nn.normalized()) : fine.n[size_t(i)]);
            up.weight.push_back(wi + wj);
            const int src = fine.fixed[size_t(i)] ? i : (fine.fixed[size_t(j)] ? j : -1);
            up.fixed.push_back(src >= 0 ? 1 : 0);
            up.fq.push_back(src >= 0 ? fine.fq[size_t(src)] : V3(V3::Zero()));
            up.fc.push_back(src >= 0 ? fine.fc[size_t(src)] : V3(V3::Zero()));
            up.n2.push_back(src >= 0 ? fine.n2[size_t(src)] : V3(V3::Zero()));
        }
        else
        {
            up.p.push_back(fine.p[size_t(i)]);
            up.n.push_back(fine.n[size_t(i)]);
            up.weight.push_back(fine.weight[size_t(i)]);
            up.fixed.push_back(fine.fixed[size_t(i)]);
            up.fq.push_back(fine.fq[size_t(i)]);
            up.fc.push_back(fine.fc[size_t(i)]);
            up.n2.push_back(fine.n2[size_t(i)]);
        }
    }
    const int M = int(up.p.size());
    up.adj.assign(size_t(M), {});
    {
        std::vector<std::map<int, double>> acc(static_cast<size_t>(M));
        for (int i = 0; i < N; ++i)
            for (const auto& e : fine.adj[size_t(i)])
            {
                const int a = childToParent[size_t(i)], b = childToParent[size_t(e.first)];
                if (a != b) acc[size_t(a)][b] += e.second;
            }
        for (int a = 0; a < M; ++a)
            for (const auto& kv : acc[size_t(a)]) up.adj[size_t(a)].push_back({kv.first, kv.second});
    }
    up.parent.assign(size_t(M), -1);
    return up;
}

// ---------------------------------------------------------------------------------------------------------------------
// The solves

// one sweep of the orientation field; returns how far the directions moved (the mean of 1 - cos of the change)
double orientationSweep(QLevel& L, const std::vector<int>& order)
{
    double moved = 0;
    for (int i : order)
    {
        const auto& nb = L.adj[size_t(i)];
        if (nb.empty() || L.fixed[size_t(i)]) continue;
        const V3 ni = L.n[size_t(i)];
        V3 sum = L.q[size_t(i)];
        double wsum = 0;
        for (const auto& e : nb)
        {
            V3 a, b;
            rosyAlign(sum, ni, L.q[size_t(e.first)], nFor(L, size_t(e.first), ni), a, b);
            sum = a * wsum + b * e.second;
            sum = inPlane(sum, ni);
            wsum += e.second;
            const double nrm = sum.norm();
            if (nrm > 1e-12) sum /= nrm;
        }
        if (sum.norm() > 1e-9)
        {
            const V3 old = L.q[size_t(i)];
            const V3 nq = sum.normalized();
            // (the change is measured between the nearest of the quarter turns)
            V3 a, b;
            rosyAlign(old, ni, nq, ni, a, b);
            moved += 1.0 - a.dot(b);
            L.q[size_t(i)] = nq;
        }
    }
    return moved / double(std::max<size_t>(1, order.size()));
}

// one sweep of the position field; returns the mean distance the lattice points moved, in units of rho
double positionSweep(QLevel& L, const std::vector<int>& order, double rho)
{
    double moved = 0;
    for (int i : order)
    {
        const auto& nb = L.adj[size_t(i)];
        if (nb.empty()) continue;
        const V3 ni = L.n[size_t(i)], qi = L.q[size_t(i)], pi = L.p[size_t(i)];
        const V3 oi = L.o[size_t(i)];
        V3 sum = V3::Zero();
        double wsum = 0;
        for (const auto& e : nb)
        {
            const size_t j = size_t(e.first);
            V3 A, Aj;
            const V3 nij = nFor(L, size_t(i), L.n[j]), nj = nFor(L, j, nij);
            rosyAlign(qi, nij, L.q[j], nj, A, Aj);
            const V3 Bj = nj.cross(Aj);
            const V3 d = oi - L.o[j];
            const double x = d.dot(Aj) / rho, y = d.dot(Bj) / rho;
            const V3 cand = L.o[j] + rho * (std::round(x) * Aj + std::round(y) * Bj);
            sum += e.second * cand;
            wsum += e.second;
        }
        V3 np = sum / wsum;
        if (!L.fixed[size_t(i)]) np -= ni * ni.dot(np - pi);
        if (L.fixed[size_t(i)])
        {
            // (on a crease: the lattice point stays on the line, at the place along it the neighbours ask for)
            const V3 fq = L.fq[size_t(i)], fc = L.fc[size_t(i)];
            const double tp = (pi - fc).dot(fq);
            double ta = (np - fc).dot(fq);
            ta -= rho * std::round((ta - tp) / rho);
            np = fc + fq * ta;
        }
        else
            np = nearestLattice(pi, np, qi, ni, rho);
        moved += (np - oi).norm() / rho;
        L.o[size_t(i)] = np;
    }
    return moved / double(std::max<size_t>(1, order.size()));
}

// ---------------------------------------------------------------------------------------------------------------------
// The layout

void growQuads(Field& F, const V3& loIn, const V3& hiIn, double cell, const V3& directionIn,
               std::vector<GNode>& nodes, PathMap& paths, std::vector<Cell>& cells, V3& firstDirection, std::string& note,
               const PatchMask& mask = PatchMask())
{
    const bool say = std::getenv("FIELDES_SC_STATS") != nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    // (the bar: when a stage ends, the next one begins)
    auto nextStage = [&](const std::string& done) {
        static const char* const table[][2] = {
            {"scaffold", "finding sharp edges and corners"}, {"creases", "coarsening the scaffold"}, {"levels", "choosing the directions of the rows"},
            {"orientation", "placing the rows of cells"}, {"position", "placing the corners of the cells"}, {"lattice", "finding the cells"},
            {"regions", "cutting the cells"}};
        for (const auto& row : table)
            if (done == row[0]) stageBegin(row[1]);
    };
    auto lap = [&](const char* what) {
        nextStage(what);
        if (say)
            std::fprintf(stderr, "[quads] %s: %.2f s\n", what,
                         std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    };
    stageBegin("making the scaffold of the surface");
    // (the regions are laid out at twice the cell size, and every region is split once into quads: the cells are `cell` wide, and
    // there are only quads in the end)
    const double rho = 2.0 * cell;
    const double hgrid = cell / 3.0;                              // (the scaffold: its edges are a third of a cell)
    // (the sheet is laid out over a margin of two cells round the region it is wanted in: the edge of the layout is ragged, and the region must lie well inside it)
    const V3 lo = loIn - V3::Constant(2.0 * cell), hi = hiIn + V3::Constant(2.0 * cell);

    // ---- the scaffold: a closed triangle mesh of the surface
    const NearCubes nc = nearCubes(F, lo, hi, hgrid, 2, say, mask);
    TriMesh mesh = marchingTetrahedra(F, nc, say, mask);
    lap("scaffold");
    if (mesh.tri.size() < 8) return;
    const int N = int(mesh.p.size());
    const size_t T = mesh.tri.size();
    // the sharp edges: vertices moved onto them (positions only: the mesh stays closed)
    CreaseInfo creases = snapCreases(F, mesh, hgrid, 0.8);
    if (say) std::fprintf(stderr, "[quads] %zu vertices of the scaffold were moved onto sharp edges\n", creases.count);
    lap("creases");

    // ---- the graph of the vertices: the edges of the mesh
    QLevel L0;
    L0.p = mesh.p;
    L0.n = mesh.n;
    L0.weight.assign(size_t(N), 1.0);
    L0.fixed.assign(size_t(N), 0);
    L0.fq.assign(size_t(N), V3::Zero());
    L0.fc.assign(size_t(N), V3::Zero());
    L0.n2.assign(size_t(N), V3::Zero());
    for (int v = 0; v < N; ++v)
        if (creases.is[size_t(v)])
        {
            L0.fixed[size_t(v)] = 1;
            L0.fq[size_t(v)] = creases.t[size_t(v)];
            L0.fc[size_t(v)] = mesh.p[size_t(v)];
            L0.n2[size_t(v)] = creases.n2[size_t(v)];
        }
    L0.adj.assign(size_t(N), {});
    {
        std::vector<std::vector<int>> nb(static_cast<size_t>(N));
        for (const auto& t : mesh.tri)
            for (int s = 0; s < 3; ++s)
            {
                nb[size_t(t[size_t(s)])].push_back(t[size_t((s + 1) % 3)]);
                nb[size_t(t[size_t((s + 1) % 3)])].push_back(t[size_t(s)]);
            }
        for (int i = 0; i < N; ++i)
        {
            std::sort(nb[size_t(i)].begin(), nb[size_t(i)].end());
            nb[size_t(i)].erase(std::unique(nb[size_t(i)].begin(), nb[size_t(i)].end()), nb[size_t(i)].end());
            for (int j : nb[size_t(i)]) L0.adj[size_t(i)].push_back({j, 1.0});
        }
    }

    // ---- levels
    std::vector<QLevel> levels;
    levels.push_back(std::move(L0));
    std::vector<std::vector<int>> toParent;
    while (levels.back().p.size() > 80 && levels.size() < 24)
    {
        std::vector<int> c2p;
        QLevel up = coarsen(levels.back(), c2p);
        if (up.p.size() * 100 > levels.back().p.size() * 92) break;
        toParent.push_back(std::move(c2p));
        levels.push_back(std::move(up));
    }
    if (say)
    {
        std::fprintf(stderr, "[quads] %zu levels:", levels.size());
        for (const QLevel& l : levels) std::fprintf(stderr, " %zu", l.p.size());
        std::fprintf(stderr, "\n");
    }
    lap("levels");

    // ---- orientation, from the coarsest level down
    V3 axis = directionIn.norm() > 0 ? V3(directionIn.normalized()) : V3(1, 0, 0);
    auto anyTangent = [&](const V3& n) -> V3 {
        V3 t = inPlane(axis, n);
        if (t.norm() < 0.3) t = inPlane(V3(0, 0, 1), n);
        if (t.norm() < 0.3) t = inPlane(V3(0, 1, 0), n);
        return t.normalized();
    };
    for (QLevel& l : levels)
    {
        l.q.assign(l.p.size(), V3::Zero());
        l.o.assign(l.p.size(), V3::Zero());
    }
    {
        QLevel& top = levels.back();
        for (size_t i = 0; i < top.p.size(); ++i)
        {
            const V3 f = top.fixed[i] ? V3(inPlane(top.fq[i], top.n[i])) : V3(V3::Zero());
            top.q[i] = f.norm() > 1e-6 ? V3(f.normalized()) : anyTangent(top.n[i]);
        }
    }
    const int sweepsCoarse = 60, sweepsFine = 8, sweepsFinest = 24;
    // (the bar counts the work of the sweeps: a point updated in a sweep is one unit, over all levels)
    double sweepWork = 0.0, sweepDone = 0.0;
    for (size_t k = 0; k < levels.size(); ++k)
        sweepWork += double(levels[k].p.size()) * double(k + 1 == levels.size() ? sweepsCoarse : (k == 0 ? sweepsFinest : sweepsFine));
    for (int li = int(levels.size()) - 1; li >= 0; --li)
    {
        QLevel& l = levels[size_t(li)];
        if (li + 1 < int(levels.size()))
        {
            const QLevel& up = levels[size_t(li) + 1];
            for (size_t i = 0; i < l.p.size(); ++i)
            {
                const V3 pq = l.fixed[i] ? l.fq[i] : up.q[size_t(toParent[size_t(li)][i])];
                V3 t = inPlane(pq, l.n[i]);
                l.q[i] = t.norm() > 1e-6 ? V3(t.normalized()) : anyTangent(l.n[i]);
            }
        }
        const std::vector<int> order = shuffled(int(l.p.size()), 11 + uint64_t(li));
        const int sweeps = li == int(levels.size()) - 1 ? sweepsCoarse : (li == 0 ? sweepsFinest : sweepsFine);
        double last = 0;
        for (int s = 0; s < sweeps; ++s)
        {
            last = orientationSweep(l, order);
            sweepDone += double(l.p.size());
            stageSet(sweepDone / sweepWork, "level " + std::to_string(int(levels.size()) - li) + " of " + std::to_string(levels.size()) + ", sweep " + std::to_string(s + 1) + " of " + std::to_string(sweeps));
        }
        if (say && li == 0) std::fprintf(stderr, "[quads] orientation: last sweep moved the directions by %.5f (1 - cos)\n", last);
    }
    lap("orientation");
    if (say)
    {
        // THE CHECK on the field: the triangles round which the four directions do not come back to themselves (a singularity of
        // the field: a vertex of the lattice with three or five squares round it, or more), and how many of them are on a crease
        const QLevel& Z0 = levels[0];
        auto shift = [&](int i, int j) {
            const V3 A[2] = {Z0.q[size_t(i)], Z0.n[size_t(i)].cross(Z0.q[size_t(i)])};
            const V3 B[2] = {Z0.q[size_t(j)], Z0.n[size_t(j)].cross(Z0.q[size_t(j)])};
            double best = -1;
            int ba = 0, bb = 0;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y)
                {
                    const double s = std::abs(A[x].dot(B[y]));
                    if (s > best)
                    {
                        best = s;
                        ba = x;
                        bb = y;
                    }
                }
            const int k = bb + (A[ba].dot(B[bb]) < 0 ? 2 : 0);
            return ((k - ba) % 4 + 4) % 4;
        };
        size_t singular = 0, singularOnCrease = 0;
        for (size_t t = 0; t < T; ++t)
        {
            const int a = mesh.tri[t][0], b = mesh.tri[t][1], c = mesh.tri[t][2];
            const int sum = shift(a, b) + shift(b, c) + shift(c, a);
            if (sum % 4 != 0)
            {
                ++singular;
                if (Z0.fixed[size_t(a)] || Z0.fixed[size_t(b)] || Z0.fixed[size_t(c)]) ++singularOnCrease;
            }
        }
        std::fprintf(stderr, "[quads] orientation field: %zu of %zu triangles are singular (%zu of them at a crease)\n", singular, T, singularOnCrease);
    }

    // ---- position
    sweepDone = 0.0;
    for (int li = int(levels.size()) - 1; li >= 0; --li)
    {
        QLevel& l = levels[size_t(li)];
        if (li + 1 < int(levels.size()))
        {
            const QLevel& up = levels[size_t(li) + 1];
            for (size_t i = 0; i < l.p.size(); ++i)
            {
                V3 o = up.o[size_t(toParent[size_t(li)][i])];
                if (l.fixed[i])
                {
                    const double tp = (l.p[i] - l.fc[i]).dot(l.fq[i]);
                    double ta = (o - l.fc[i]).dot(l.fq[i]);
                    ta -= rho * std::round((ta - tp) / rho);
                    l.o[i] = l.fc[i] + l.fq[i] * ta;
                    continue;
                }
                o -= l.n[i] * l.n[i].dot(o - l.p[i]);
                l.o[i] = nearestLattice(l.p[i], o, l.q[i], l.n[i], rho);
            }
        }
        else
            for (size_t i = 0; i < l.p.size(); ++i)
                l.o[i] = l.fixed[i] ? V3(l.fc[i] + l.fq[i] * (rho * std::round((l.p[i] - l.fc[i]).dot(l.fq[i]) / rho))) : l.p[i];
        const std::vector<int> order = shuffled(int(l.p.size()), 23 + uint64_t(li));
        const int sweeps = li == int(levels.size()) - 1 ? sweepsCoarse : (li == 0 ? sweepsFinest : sweepsFine);
        double last = 0;
        for (int s = 0; s < sweeps; ++s)
        {
            last = positionSweep(l, order, rho);
            sweepDone += double(l.p.size());
            stageSet(sweepDone / sweepWork, "level " + std::to_string(int(levels.size()) - li) + " of " + std::to_string(levels.size()) + ", sweep " + std::to_string(s + 1) + " of " + std::to_string(sweeps));
        }
        if (say && li == 0) std::fprintf(stderr, "[quads] position: last sweep moved the lattice points by %.4f cells\n", last);
    }
    lap("position");

    // ---- the lattice the field makes: vertices (the vertices of the mesh whose lattice points are the same point) and the steps
    // between them
    const QLevel& Z = levels[0];
    std::vector<int> grp(static_cast<size_t>(N));
    std::iota(grp.begin(), grp.end(), 0);
    auto find = [&](int a) {
        while (grp[size_t(a)] != a)
        {
            grp[size_t(a)] = grp[size_t(grp[size_t(a)])];
            a = grp[size_t(a)];
        }
        return a;
    };
    struct Witness
    {
        int i, j, X, Y;
    };
    std::vector<Witness> witnesses;
    size_t consistent = 0, seen = 0;
    for (int i = 0; i < N; ++i)
        for (const auto& e : Z.adj[size_t(i)])
        {
            const int j = e.first;
            if (j < i) continue;
            V3 A, Aj;
            const V3 nij = nFor(Z, size_t(i), Z.n[size_t(j)]), nj = nFor(Z, size_t(j), nij);
            rosyAlign(Z.q[size_t(i)], nij, Z.q[size_t(j)], nj, A, Aj);
            const V3 Bj = nj.cross(Aj);
            const V3 d = Z.o[size_t(i)] - Z.o[size_t(j)];
            const int X = int(std::lround(d.dot(Aj) / rho)), Y = int(std::lround(d.dot(Bj) / rho));
            const double r = (d - rho * (X * Aj + Y * Bj)).norm() / rho;
            ++seen;
            if (r < 0.3)
            {
                ++consistent;
                witnesses.push_back({i, j, X, Y});
            }
        }
    if (say)
        std::fprintf(stderr, "[quads] %zu of %zu edges of the mesh agree about the lattice (%.1f %%)\n", consistent, seen,
                     100.0 * double(consistent) / double(std::max<size_t>(1, seen)));
    for (const Witness& w : witnesses)
        if (w.X == 0 && w.Y == 0 && (Z.o[size_t(w.i)] - Z.o[size_t(w.j)]).norm() < 0.35 * rho)
        {
            const int a = find(w.i), b = find(w.j);
            if (a != b) grp[size_t(b)] = a;
        }
    std::vector<int> classOf(static_cast<size_t>(N), -1);
    std::vector<V3> cpos, cmean;
    std::vector<int> ccount;
    {
        std::vector<int> id(static_cast<size_t>(N), -1);
        for (int i = 0; i < N; ++i)
        {
            const int r = find(i);
            if (id[size_t(r)] < 0)
            {
                id[size_t(r)] = int(cpos.size());
                cpos.push_back(V3::Zero());
                cmean.push_back(V3::Zero());
                ccount.push_back(0);
            }
            const int c = id[size_t(r)];
            classOf[size_t(i)] = c;
            cpos[size_t(c)] += Z.o[size_t(i)];
            cmean[size_t(c)] += Z.p[size_t(i)];
            ++ccount[size_t(c)];
        }
        for (size_t c = 0; c < cpos.size(); ++c)
        {
            cpos[c] /= double(ccount[c]);
            cmean[c] /= double(ccount[c]);
        }
    }
    const int C = int(cpos.size());
    std::vector<std::vector<std::pair<int, V3>>> cadj(static_cast<size_t>(C));
    {
        auto link = [&](int a, int b) {
            for (const auto& e : cadj[size_t(a)])
                if (e.first == b) return;
            const V3 d = cpos[size_t(b)] - cpos[size_t(a)];
            if (d.norm() > 1e-9) cadj[size_t(a)].push_back({b, V3(d.normalized())});
        };
        for (const Witness& w : witnesses)
            if (std::abs(w.X) + std::abs(w.Y) == 1)
            {
                const int a = classOf[size_t(w.i)], b = classOf[size_t(w.j)];
                if (a == b) continue;
                link(a, b);
                link(b, a);
            }
    }
    if (say)
    {
        size_t steps = 0;
        for (const auto& v : cadj) steps += v.size();
        std::fprintf(stderr, "[quads] the lattice: %d vertices, %zu steps\n", C, steps / 2);
    }
    lap("lattice");

    // ---- the square every triangle lies in.  A vertex lies in one square of its lattice (the one on the side of its lattice point
    // that it is on); the middle of that square is a point in space.  Vertices next to each other whose squares have the same
    // middle lie in one square.  A triangle lies in the square, among those of its three vertices, that is nearest the middle it
    // would have by the lattice of its nearest vertex.
    std::vector<V3> vcentre(static_cast<size_t>(N), V3::Zero());
    for (int v = 0; v < N; ++v)
    {
        if (Z.fixed[size_t(v)]) continue;                          // (a vertex on a crease lies on the edge of two squares: it takes none)
        const V3 A = Z.q[size_t(v)], B = Z.n[size_t(v)].cross(A);
        const V3 d = Z.p[size_t(v)] - Z.o[size_t(v)];
        vcentre[size_t(v)] = Z.o[size_t(v)] + 0.5 * rho * ((d.dot(A) >= 0 ? 1.0 : -1.0) * A + (d.dot(B) >= 0 ? 1.0 : -1.0) * B);
    }
    std::vector<int> sqClass(static_cast<size_t>(N));
    std::vector<V3> sqCentre;
    {
        std::vector<int> spar(static_cast<size_t>(N));
        std::iota(spar.begin(), spar.end(), 0);
        auto sroot = [&](int a) {
            while (spar[size_t(a)] != a)
            {
                spar[size_t(a)] = spar[size_t(spar[size_t(a)])];
                a = spar[size_t(a)];
            }
            return a;
        };
        for (const Witness& w : witnesses)
            if (!Z.fixed[size_t(w.i)] && !Z.fixed[size_t(w.j)] && (vcentre[size_t(w.i)] - vcentre[size_t(w.j)]).norm() < 0.3 * rho)
            {
                const int a = sroot(w.i), b = sroot(w.j);
                if (a != b) spar[size_t(b)] = a;
            }
        std::map<int, int> ids;
        std::vector<int> count;
        for (int v = 0; v < N; ++v)
        {
            if (Z.fixed[size_t(v)])
            {
                sqClass[size_t(v)] = -1;
                continue;
            }
            const int r = sroot(v);
            const auto it = ids.emplace(r, int(ids.size()));
            if (it.second)
            {
                sqCentre.push_back(V3::Zero());
                count.push_back(0);
            }
            const int c = it.first->second;
            sqClass[size_t(v)] = c;
            sqCentre[size_t(c)] += vcentre[size_t(v)];
            ++count[size_t(c)];
        }
        for (size_t c = 0; c < sqCentre.size(); ++c) sqCentre[c] /= double(count[c]);
        if (say) std::fprintf(stderr, "[quads] %zu squares found among the vertices\n", sqCentre.size());
    }
    std::vector<std::array<int, 4>> squareOf(T);
    for (size_t t = 0; t < T; ++t)
    {
        const auto& tr = mesh.tri[t];
        const V3 cen = (mesh.p[size_t(tr[0])] + mesh.p[size_t(tr[1])] + mesh.p[size_t(tr[2])]) / 3.0;
        int a = -1;
        for (int s = 0; s < 3; ++s)
            if (!Z.fixed[size_t(tr[size_t(s)])] &&
                (a < 0 || (mesh.p[size_t(tr[size_t(s)])] - cen).squaredNorm() < (mesh.p[size_t(a)] - cen).squaredNorm()))
                a = tr[size_t(s)];
        if (a < 0)
        {
            squareOf[t] = {-1, 0, 0, 0};                             // (all three on creases: it takes the square of a neighbour, below)
            continue;
        }
        const V3 A = Z.q[size_t(a)], B = Z.n[size_t(a)].cross(A);
        const V3 d = cen - Z.o[size_t(a)];
        const V3 want = Z.o[size_t(a)] + 0.5 * rho * ((d.dot(A) >= 0 ? 1.0 : -1.0) * A + (d.dot(B) >= 0 ? 1.0 : -1.0) * B);
        int best = sqClass[size_t(a)];
        double bestD = (sqCentre[size_t(best)] - want).squaredNorm();
        for (int s = 0; s < 3; ++s)
        {
            const int c = sqClass[size_t(tr[size_t(s)])];
            if (c < 0) continue;
            const double dd = (sqCentre[size_t(c)] - want).squaredNorm();
            if (dd < bestD)
            {
                bestD = dd;
                best = c;
            }
        }
        squareOf[t] = {best, 0, 0, 0};
    }

    // ---- the regions: triangles of one square that are in one piece
    std::unordered_map<uint64_t, std::vector<int>> trisOfEdge;
    auto ek = [](int a, int b) { return (uint64_t(uint32_t(std::min(a, b))) << 32) | uint32_t(std::max(a, b)); };
    auto dk = [](int a, int b) { return (uint64_t(uint32_t(a)) << 32) | uint32_t(b); };
    for (size_t t = 0; t < T; ++t)
        for (int s = 0; s < 3; ++s) trisOfEdge[ek(mesh.tri[t][size_t(s)], mesh.tri[t][size_t((s + 1) % 3)])].push_back(int(t));
    // (a triangle that has only vertices on creases lies in the square of a triangle beside it)
    for (int pass = 0; pass < 6; ++pass)
    {
        bool any = false;
        for (size_t t = 0; t < T; ++t)
        {
            if (squareOf[t][0] >= 0) continue;
            for (int s = 0; s < 3 && squareOf[t][0] < 0; ++s)
                for (int u : trisOfEdge[ek(mesh.tri[t][size_t(s)], mesh.tri[t][size_t((s + 1) % 3)])])
                    if (squareOf[size_t(u)][0] >= 0)
                    {
                        squareOf[t] = squareOf[size_t(u)];
                        break;
                    }
            any = any || squareOf[t][0] < 0;
        }
        if (!any) break;
    }
    for (size_t t = 0; t < T; ++t)
        if (squareOf[t][0] < 0) squareOf[t] = {0, 0, 0, 0};
    std::vector<int> region(T, -1);
    int R = 0;
    {
        std::map<std::array<int, 4>, int> label;
        std::vector<int> lab(T);
        for (size_t t = 0; t < T; ++t)
        {
            const auto it = label.emplace(squareOf[t], int(label.size()));
            lab[t] = it.first->second;
        }
        std::vector<int> par(T);
        std::iota(par.begin(), par.end(), 0);
        auto root = [&](int a) {
            while (par[size_t(a)] != a)
            {
                par[size_t(a)] = par[size_t(par[size_t(a)])];
                a = par[size_t(a)];
            }
            return a;
        };
        for (const auto& kv : trisOfEdge)
            for (size_t x = 0; x + 1 < kv.second.size(); ++x)
                if (lab[size_t(kv.second[x])] == lab[size_t(kv.second[x + 1])])
                {
                    const int a = root(kv.second[x]), b = root(kv.second[x + 1]);
                    if (a != b) par[size_t(b)] = a;
                }
        std::map<int, int> ids;
        for (size_t t = 0; t < T; ++t)
        {
            const int r = root(int(t));
            const auto it = ids.emplace(r, int(ids.size()));
            region[t] = it.first->second;
        }
        R = int(ids.size());
    }
    std::vector<double> triArea(T);
    for (size_t t = 0; t < T; ++t)
        triArea[t] = 0.5 * (mesh.p[size_t(mesh.tri[t][1])] - mesh.p[size_t(mesh.tri[t][0])]).cross(mesh.p[size_t(mesh.tri[t][2])] - mesh.p[size_t(mesh.tri[t][0])]).norm();
    if (say) std::fprintf(stderr, "[quads] %d regions of triangles that lie in one square\n", R);

    // ---- the corners and the cells.  Slivers are joined to the region they share most boundary with; a region whose boundary is
    // not one closed line of chains (an island in another, a region with less than three corners) is joined to its neighbour: the
    // regions only grow, none is ever removed from the surface, so nothing is left uncovered.
    struct Chain
    {
        int region, from, to;            // the region on its left, and the nodes (vertices of the mesh where three regions meet)
        double length;
        std::vector<int> verts;
    };
    std::vector<std::vector<std::vector<Chain>>> cyclesOf;       // per region: its closed lines, each a list of chains in order
    std::vector<int> nodeCluster;                                // per mesh vertex: the node it is (after short chains are contracted)
    std::vector<std::vector<int>> corners;                       // per region: the nodes round it, counterclockwise
    std::vector<std::vector<std::vector<int>>> sideVerts;        // per region, per corner: the vertices of its line to the next corner
    int splitRounds = 0;
    for (int iter = 0; iter < 24; ++iter)
    {
        // slivers
        {
            std::vector<double> area(static_cast<size_t>(R), 0.0);
            for (size_t t = 0; t < T; ++t) area[size_t(region[t])] += triArea[t];
            if (say && iter == 0)
            {
                std::vector<double> sorted = area;
                std::sort(sorted.begin(), sorted.end());
                size_t slivers = 0;
                for (double a : sorted) slivers += a < 0.2 * rho * rho ? 1 : 0;
                std::fprintf(stderr, "[quads] region areas (mm2): smallest %.2f, quartiles %.1f %.1f %.1f, largest %.1f; %zu of %d are slivers (under %.1f)\n", sorted.front(),
                             sorted[sorted.size() / 4], sorted[sorted.size() / 2], sorted[3 * sorted.size() / 4], sorted.back(), slivers, R, 0.2 * rho * rho);
            }
            std::map<std::pair<int, int>, double> shared;
            for (const auto& kv : trisOfEdge)
                if (kv.second.size() == 2 && region[size_t(kv.second[0])] != region[size_t(kv.second[1])])
                {
                    const int u = int(kv.first >> 32), v = int(kv.first & 0xffffffffu);
                    const double len = (mesh.p[size_t(u)] - mesh.p[size_t(v)]).norm();
                    const int a = region[size_t(kv.second[0])], b = region[size_t(kv.second[1])];
                    shared[{a, b}] += len;
                    shared[{b, a}] += len;
                }
            std::vector<std::vector<std::pair<int, double>>> nbrs(static_cast<size_t>(R));
            for (const auto& kv : shared) nbrs[size_t(kv.first.first)].push_back({kv.first.second, kv.second});
            std::vector<int> into(static_cast<size_t>(R));
            std::iota(into.begin(), into.end(), 0);
            std::vector<int> order(static_cast<size_t>(R));
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](int a, int b) { return area[size_t(a)] != area[size_t(b)] ? area[size_t(a)] < area[size_t(b)] : a < b; });
            auto top = [&](int a) {
                while (into[size_t(a)] != a) a = into[size_t(a)];
                return a;
            };
            for (int r : order)
            {
                if (area[size_t(r)] >= 0.2 * rho * rho) break;
                int best = -1;
                double bestLen = 0;
                for (const auto& e : nbrs[size_t(r)])
                    if (top(e.first) != r && e.second > bestLen)
                    {
                        bestLen = e.second;
                        best = e.first;
                    }
                if (best >= 0) into[size_t(r)] = top(best);
            }
            std::map<int, int> ids;
            for (size_t t = 0; t < T; ++t)
            {
                const int r = top(region[t]);
                const auto it = ids.emplace(r, int(ids.size()));
                region[t] = it.first->second;
            }
            R = int(ids.size());
        }
        // the edge of the surface, the vertices round which three regions meet
        std::vector<char> onEdge(static_cast<size_t>(N), 0);
        for (const auto& kv : trisOfEdge)
            if (kv.second.size() == 1)
            {
                onEdge[size_t(kv.first >> 32)] = 1;
                onEdge[size_t(kv.first & 0xffffffffu)] = 1;
            }
        std::vector<std::set<int>> labelsAt(static_cast<size_t>(N));
        for (size_t t = 0; t < T; ++t)
            for (int s = 0; s < 3; ++s) labelsAt[size_t(mesh.tri[t][size_t(s)])].insert(region[t]);
        std::vector<char> isNode(static_cast<size_t>(N), 0);
        for (int v = 0; v < N; ++v) isNode[size_t(v)] = labelsAt[size_t(v)].size() + (onEdge[size_t(v)] ? 1 : 0) >= 3 ? 1 : 0;
        // the directed edges of the boundary of every region (the region on the left)
        std::unordered_map<uint64_t, int> regionOfDir;
        for (size_t t = 0; t < T; ++t)
            for (int s = 0; s < 3; ++s) regionOfDir[dk(mesh.tri[t][size_t(s)], mesh.tri[t][size_t((s + 1) % 3)])] = region[t];
        std::vector<std::map<int, std::vector<int>>> outs(static_cast<size_t>(R));
        for (const auto& kv : regionOfDir)
        {
            const int u = int(kv.first >> 32), v = int(kv.first & 0xffffffffu);
            const auto back = regionOfDir.find(dk(v, u));
            if (back == regionOfDir.end() || back->second != kv.second) outs[size_t(kv.second)][u].push_back(v);
        }
        for (auto& m : outs)
            for (auto& kv : m) std::sort(kv.second.begin(), kv.second.end());
        // the closed lines of every region, cut into chains at the nodes
        cyclesOf.assign(static_cast<size_t>(R), {});
        bool changed = false;
        std::vector<int> mergeInto(static_cast<size_t>(R), -1);
        for (int r = 0; r < R; ++r)
        {
            std::set<uint64_t> used;
            for (const auto& kv : outs[size_t(r)])
                for (int v0 : kv.second)
                {
                    if (used.count(dk(kv.first, v0))) continue;
                    std::vector<int> loop = {kv.first};
                    int u = kv.first, v = v0;
                    bool closed = false;
                    for (int guard = 0; guard < 400000; ++guard)
                    {
                        used.insert(dk(u, v));
                        if (v == kv.first)
                        {
                            closed = true;
                            break;
                        }
                        loop.push_back(v);
                        const auto it = outs[size_t(r)].find(v);
                        int nx = -1;
                        if (it != outs[size_t(r)].end())
                        {
                            // (where the region touches itself the line turns the way that keeps the region on its left)
                            const V3 nv = mesh.n[size_t(v)];
                            double bestAng = -1e300;
                            for (int w : it->second)
                            {
                                if (used.count(dk(v, w))) continue;
                                const V3 in = mesh.p[size_t(v)] - mesh.p[size_t(u)], outd = mesh.p[size_t(w)] - mesh.p[size_t(v)];
                                const double ang = std::atan2(in.cross(outd).dot(nv), in.dot(outd));
                                if (ang > bestAng)
                                {
                                    bestAng = ang;
                                    nx = w;
                                }
                            }
                        }
                        if (nx < 0) break;
                        u = v;
                        v = nx;
                    }
                    if (!closed) continue;
                    // rotate to start at a node
                    size_t start = loop.size();
                    for (size_t k = 0; k < loop.size(); ++k)
                        if (isNode[size_t(loop[k])])
                        {
                            start = k;
                            break;
                        }
                    if (start == loop.size())
                    {
                        // (a line without a corner: the region is inside another, or all round another: it joins that one)
                        const auto across = regionOfDir.find(dk(loop[1 % loop.size()], loop[0]));
                        if (across != regionOfDir.end() && across->second != r) mergeInto[size_t(r)] = across->second;
                        continue;
                    }
                    std::rotate(loop.begin(), loop.begin() + long(start), loop.end());
                    std::vector<Chain> chains;
                    Chain cur;
                    cur.region = r;
                    cur.from = loop[0];
                    cur.length = 0;
                    cur.verts = {loop[0]};
                    for (size_t k = 1; k <= loop.size(); ++k)
                    {
                        const int vk = loop[k % loop.size()];
                        cur.length += (mesh.p[size_t(vk)] - mesh.p[size_t(cur.verts.back())]).norm();
                        cur.verts.push_back(vk);
                        if (isNode[size_t(vk)])
                        {
                            cur.to = vk;
                            chains.push_back(cur);
                            cur = Chain();
                            cur.region = r;
                            cur.from = vk;
                            cur.length = 0;
                            cur.verts = {vk};
                        }
                    }
                    cyclesOf[size_t(r)].push_back(std::move(chains));
                }
            // a region with several lines is not a disc: the regions inside its inner lines join it
            if (cyclesOf[size_t(r)].size() > 1 && mergeInto[size_t(r)] < 0)
            {
                size_t longest = 0;
                for (size_t k = 1; k < cyclesOf[size_t(r)].size(); ++k)
                    if (cyclesOf[size_t(r)][k].size() > cyclesOf[size_t(r)][longest].size()) longest = k;
                for (size_t k = 0; k < cyclesOf[size_t(r)].size(); ++k)
                    if (k != longest && !cyclesOf[size_t(r)][k].empty())
                    {
                        const Chain& c0 = cyclesOf[size_t(r)][k][0];
                        const auto across = regionOfDir.find(dk(c0.verts[1], c0.verts[0]));
                        if (across != regionOfDir.end() && across->second != r && mergeInto[size_t(across->second)] < 0)
                            mergeInto[size_t(across->second)] = r;
                    }
            }
        }
        // the short chains are contracted: the nodes at their ends are one node
        std::vector<int> npar(static_cast<size_t>(N));
        std::iota(npar.begin(), npar.end(), 0);
        auto nroot = [&](int a) {
            while (npar[size_t(a)] != a)
            {
                npar[size_t(a)] = npar[size_t(npar[size_t(a)])];
                a = npar[size_t(a)];
            }
            return a;
        };
        for (const auto& cyc : cyclesOf)
            for (const auto& chains : cyc)
                for (const Chain& c : chains)
                    if (c.length < 0.45 * rho)
                    {
                        const int a = nroot(c.from), b = nroot(c.to);
                        if (a != b) npar[size_t(b)] = a;
                    }
        nodeCluster.assign(static_cast<size_t>(N), -1);
        for (int v = 0; v < N; ++v)
            if (isNode[size_t(v)]) nodeCluster[size_t(v)] = nroot(v);
        // the corners of every region
        corners.assign(static_cast<size_t>(R), {});
        sideVerts.assign(static_cast<size_t>(R), {});
        for (int r = 0; r < R; ++r)
        {
            std::vector<int> all;
            std::vector<std::vector<int>> real;           // (per chain: its vertices if it joins two different corners)
            if (!cyclesOf[size_t(r)].empty())
            {
                size_t longest = 0;
                for (size_t k = 1; k < cyclesOf[size_t(r)].size(); ++k)
                    if (cyclesOf[size_t(r)][k].size() > cyclesOf[size_t(r)][longest].size()) longest = k;
                for (const Chain& c : cyclesOf[size_t(r)][longest])
                {
                    all.push_back(nodeCluster[size_t(c.from)]);
                    real.push_back(nodeCluster[size_t(c.from)] != nodeCluster[size_t(c.to)] ? c.verts : std::vector<int>());
                }
            }
            std::vector<int> cr;
            std::vector<std::vector<int>> sv;
            for (size_t k = 0; k < all.size(); ++k)
            {
                if (cr.empty() || cr.back() != all[k])
                {
                    cr.push_back(all[k]);
                    sv.emplace_back();
                }
                if (sv.back().empty() && !real[k].empty()) sv.back() = real[k];
            }
            while (cr.size() > 1 && cr.front() == cr.back())
            {
                cr.pop_back();
                sv.pop_back();
            }
            corners[size_t(r)] = cr;
            sideVerts[size_t(r)] = std::move(sv);
            if (mergeInto[size_t(r)] < 0 && cr.size() < 3 && !cyclesOf[size_t(r)].empty()) mergeInto[size_t(r)] = -2;      // (to be joined below)
        }
        // regions that have to join another: the neighbour they share most boundary with
        for (int r = 0; r < R; ++r)
            if (mergeInto[size_t(r)] == -2)
            {
                std::map<int, double> sh;
                for (const auto& chains : cyclesOf[size_t(r)])
                    for (const Chain& c : chains)
                        for (size_t k = 0; k + 1 < c.verts.size(); ++k)
                        {
                            const auto across = regionOfDir.find(dk(c.verts[k + 1], c.verts[k]));
                            if (across != regionOfDir.end() && across->second != r)
                                sh[across->second] += (mesh.p[size_t(c.verts[k])] - mesh.p[size_t(c.verts[k + 1])]).norm();
                        }
                int best = -1;
                double bl = 0;
                for (const auto& kv : sh)
                    if (kv.second > bl)
                    {
                        bl = kv.second;
                        best = kv.first;
                    }
                mergeInto[size_t(r)] = best;
            }
        size_t nodeless = 0, islands = 0, fewCorners = 0, merges = 0;
        for (int r = 0; r < R; ++r)
        {
            if (mergeInto[size_t(r)] >= 0) ++merges;
            if (corners[size_t(r)].size() < 3) ++fewCorners;
            if (cyclesOf[size_t(r)].empty()) ++nodeless;
            if (cyclesOf[size_t(r)].size() > 1) ++islands;
        }
        if (say) std::fprintf(stderr, "[quads]   round %d: %d regions, %zu to join another (%zu have no line with a corner, %zu have several lines, %zu have fewer than three corners)\n", iter, R, merges, nodeless, islands, fewCorners);
        for (int r = 0; r < R; ++r)
            if (mergeInto[size_t(r)] >= 0) changed = true;
        // A region with six corners or more is where several singularities of the field fall into one square: a star of thin
        // cells.  It is cut along a line between two of its own corners (a path over its own triangles, through vertices that
        // belong to it alone) into a region of four corners and the rest, again and again.  The cut ends in nodes that are there,
        // so no neighbour gets a corner, and the cut is a line between two regions: the partition is as closed as it was.
        if (!changed && splitRounds < 4)
        {
            std::vector<std::vector<int>> trisAt(static_cast<size_t>(N));
            for (size_t t = 0; t < T; ++t)
                for (int s = 0; s < 3; ++s) trisAt[size_t(mesh.tri[t][size_t(s)])].push_back(int(t));
            std::vector<int> vReg(static_cast<size_t>(N), -2);          // the region all the triangles of a vertex are in (-1: several)
            for (int v = 0; v < N; ++v)
                for (int t : trisAt[size_t(v)])
                {
                    if (vReg[size_t(v)] == -2) vReg[size_t(v)] = region[size_t(t)];
                    else if (vReg[size_t(v)] != region[size_t(t)]) vReg[size_t(v)] = -1;
                }
            std::vector<double> regionArea(static_cast<size_t>(R), 0.0);
            for (size_t t = 0; t < T; ++t) regionArea[size_t(region[t])] += triArea[t];
            int newLabels = R;
            size_t splits = 0;
            for (int r = 0; r < R; ++r)
            {
                const size_t K = corners[size_t(r)].size();
                if (K < 6 || sideVerts[size_t(r)].size() != K) continue;
                std::vector<int> rep(K, -1);
                bool haveAll = true;
                for (size_t i = 0; i < K; ++i)
                {
                    if (sideVerts[size_t(r)][i].empty()) haveAll = false;
                    else rep[i] = sideVerts[size_t(r)][i].front();
                }
                if (!haveAll) continue;
                // (the cuts that would cut off four corners, those whose length is nearest one lattice step first)
                std::vector<std::pair<double, size_t>> tries;
                for (size_t i = 0; i < K; ++i)
                {
                    const double d = (mesh.p[size_t(rep[i])] - mesh.p[size_t(rep[(i + 3) % K])]).norm();
                    if (d >= 0.6 * rho) tries.push_back({std::fabs(d - rho), i});
                }
                std::sort(tries.begin(), tries.end());
                for (size_t ti = 0; ti < tries.size() && ti < 4; ++ti)
                {
                    const int a = rep[tries[ti].second], b = rep[(tries[ti].second + 3) % K];
                    // the shortest path from a to b over vertices that belong to this region alone
                    std::unordered_map<int, double> dist;
                    std::unordered_map<int, int> before;
                    using QI = std::pair<double, int>;
                    std::priority_queue<QI, std::vector<QI>, std::greater<QI>> heap;
                    dist[a] = 0;
                    heap.push({0.0, a});
                    bool found = false;
                    while (!heap.empty())
                    {
                        const QI top = heap.top();
                        heap.pop();
                        if (top.first > dist[top.second]) continue;
                        if (top.second == b)
                        {
                            found = true;
                            break;
                        }
                        for (const auto& e : levels[0].adj[size_t(top.second)])
                        {
                            const int w = e.first;
                            if (w != b && vReg[size_t(w)] != r) continue;
                            const double nd = top.first + (mesh.p[size_t(w)] - mesh.p[size_t(top.second)]).norm();
                            const auto it = dist.find(w);
                            if (it == dist.end() || nd < it->second)
                            {
                                dist[w] = nd;
                                before[w] = top.second;
                                heap.push({nd, w});
                            }
                        }
                    }
                    if (!found) continue;
                    std::vector<int> path = {b};
                    while (path.back() != a) path.push_back(before[path.back()]);
                    std::reverse(path.begin(), path.end());
                    if (path.size() < 3) continue;
                    // the triangles on the left of the path, and the part of the region they make
                    std::unordered_set<uint64_t> onPath;
                    for (size_t k = 0; k + 1 < path.size(); ++k) onPath.insert(ek(path[k], path[k + 1]));
                    std::vector<int> seeds, rightSeeds;
                    for (size_t k = 0; k + 1 < path.size(); ++k)
                        for (int t : trisAt[size_t(path[k])])
                            for (int s = 0; s < 3; ++s)
                            {
                                const int u = mesh.tri[size_t(t)][size_t(s)], v = mesh.tri[size_t(t)][size_t((s + 1) % 3)];
                                if (region[size_t(t)] != r) continue;
                                if (u == path[k] && v == path[k + 1]) seeds.push_back(t);
                                if (v == path[k] && u == path[k + 1]) rightSeeds.push_back(t);
                            }
                    std::vector<char> inPart(T, 0);
                    std::vector<int> stack;
                    double partArea = 0;
                    for (int t : seeds)
                        if (!inPart[size_t(t)])
                        {
                            inPart[size_t(t)] = 1;
                            stack.push_back(t);
                        }
                    while (!stack.empty())
                    {
                        const int t = stack.back();
                        stack.pop_back();
                        partArea += triArea[size_t(t)];
                        for (int s = 0; s < 3; ++s)
                        {
                            const uint64_t key = ek(mesh.tri[size_t(t)][size_t(s)], mesh.tri[size_t(t)][size_t((s + 1) % 3)]);
                            if (onPath.count(key)) continue;
                            for (int u : trisOfEdge[key])
                                if (region[size_t(u)] == r && !inPart[size_t(u)])
                                {
                                    inPart[size_t(u)] = 1;
                                    stack.push_back(u);
                                }
                        }
                    }
                    bool separates = true;
                    for (int t : rightSeeds)
                        if (inPart[size_t(t)]) separates = false;
                    if (!separates || partArea < 0.5 * rho * rho || regionArea[size_t(r)] - partArea < 0.5 * rho * rho) continue;
                    for (size_t t = 0; t < T; ++t)
                        if (inPart[t]) region[t] = newLabels;
                    ++newLabels;
                    ++splits;
                    break;
                }
            }
            if (say) std::fprintf(stderr, "[quads]   round %d: %zu regions with six corners or more cut in two along a line between two corners\n", iter, splits);
            if (splits > 0)
            {
                R = newLabels;
                ++splitRounds;
                continue;
            }
        }
        if (!changed) break;
        std::vector<int> into(static_cast<size_t>(R));
        std::iota(into.begin(), into.end(), 0);
        auto top = [&](int a) {
            while (into[size_t(a)] != a)
            {
                into[size_t(a)] = into[size_t(into[size_t(a)])];
                a = into[size_t(a)];
            }
            return a;
        };
        for (int r = 0; r < R; ++r)
            if (mergeInto[size_t(r)] >= 0)
            {
                const int a = top(r), b = top(mergeInto[size_t(r)]);
                if (a != b) into[size_t(b)] = a;
            }
        std::map<int, int> ids;
        for (size_t t = 0; t < T; ++t)
        {
            const auto it = ids.emplace(top(region[t]), int(ids.size()));
            region[t] = it.first->second;
        }
        R = int(ids.size());
    }
    {
        size_t k3 = 0, k4 = 0, kmore = 0, kless = 0;
        for (int r = 0; r < R; ++r)
        {
            const size_t k = corners[size_t(r)].size();
            if (k == 4) ++k4;
            else if (k == 3) ++k3;
            else if (k > 4) ++kmore;
            else ++kless;
        }
        if (say)
            std::fprintf(stderr, "[quads] %d regions: %zu with four corners, %zu with three, %zu with more, %zu with fewer than three\n", R, k4, k3, kmore, kless);
    }
    lap("regions");

    // the vertices of the scaffold that every region has: what a node made in the middle of a region is put on, if the middle is
    // not on the surface (a region that goes round a hole has its middle in the air of the hole)
    std::vector<std::vector<int>> regionVerts(static_cast<size_t>(R));
    for (size_t t = 0; t < T; ++t)
        for (int s = 0; s < 3; ++s) regionVerts[size_t(region[t])].push_back(mesh.tri[t][size_t(s)]);
    for (auto& rv : regionVerts)
    {
        std::sort(rv.begin(), rv.end());
        rv.erase(std::unique(rv.begin(), rv.end()), rv.end());
    }

    // ---- nodes: the vertex of the lattice the squares round a corner share (or the middle of the vertices of the node), on the
    // surface
    std::map<int, int> nodeIndex;
    std::vector<V3> npos, nnrm;
    {
        std::map<int, std::vector<int>> members;
        for (int v = 0; v < N; ++v)
            if (nodeCluster[size_t(v)] >= 0) members[nodeCluster[size_t(v)]].push_back(v);
        std::vector<V3> pts, mean;
        std::vector<int> key;
        for (const auto& kv : members)
        {
            V3 m = V3::Zero(), nsum = V3::Zero();
            for (int v : kv.second)
            {
                m += mesh.p[size_t(v)];
                nsum += mesh.n[size_t(v)];
            }
            m /= double(kv.second.size());
            key.push_back(kv.first);
            mean.push_back(m);
            nnrm.push_back(nsum.norm() > 1e-9 ? V3(nsum.normalized()) : V3(0, 1, 0));
        }
        pts.resize(key.size());
        // (the lattice vertex that is nearest the middle of the vertices where the regions meet, if it is near: else that middle.
        // A lattice vertex is one node at most: two nodes at one place would be a side of no length)
        struct Pair
        {
            double d;
            size_t node;
            int vertex;
        };
        std::vector<Pair> pairs;
        const double reach = 0.35 * rho * 0.35 * rho;
        for (size_t i = 0; i < key.size(); ++i)
            for (int c = 0; c < C; ++c)
            {
                const double dd = (cpos[size_t(c)] - mean[i]).squaredNorm();
                if (dd < reach) pairs.push_back({dd, i, c});
            }
        std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
            if (a.d != b.d) return a.d < b.d;
            return a.node != b.node ? a.node < b.node : a.vertex < b.vertex;
        });
        std::vector<int> latticeOf(key.size(), -1);
        std::vector<char> latticeTaken(static_cast<size_t>(std::max(C, 0)), 0);
        for (const Pair& pr : pairs)
            if (latticeOf[pr.node] < 0 && !latticeTaken[size_t(pr.vertex)])
            {
                latticeOf[pr.node] = pr.vertex;
                latticeTaken[size_t(pr.vertex)] = 1;
            }
        for (size_t i = 0; i < key.size(); ++i)
        {
            pts[i] = latticeOf[i] >= 0 ? cpos[size_t(latticeOf[i])] : mean[i];
            nodeIndex[key[i]] = int(i);
        }
        npos = pts;
        std::vector<V3> before = pts, nn;
        std::vector<char> ok;
        projectToSurface(F, pts, nn, ok, 0.5 * cell, 1e-4);
        size_t moved = 0;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (ok[i] && (pts[i] - before[i]).norm() < 0.5 * rho)
            {
                npos[i] = pts[i];
                if (nn[i].dot(nnrm[i]) > 0.3) nnrm[i] = nn[i];
            }
            else
            {
                npos[i] = mean[i];
                ++moved;
            }
        }
        if (say) std::fprintf(stderr, "[quads] %zu nodes (%zu left at the middle of their vertices)\n", npos.size(), moved);
    }

    // ---- the cells: a region with four corners is a quad, with three a triangle, with more a quad fan round a middle node
    std::vector<int> nodeOf(npos.size(), -1);
    auto nodeFor = [&](size_t idx) {
        if (nodeOf[idx] < 0)
        {
            GNode g;
            g.p = npos[idx];
            g.n = nnrm[idx];
            nodes.push_back(g);
            nodeOf[idx] = int(nodes.size()) - 1;
        }
        return nodeOf[idx];
    };
    std::map<uint64_t, int> edgeCount;
    std::vector<std::array<int, 2>> edgeList;
    auto addCell = [&](int a, int b, int c, int d) {
        // (the quad round as a, b, c, d: s from a to b, t from a to d)
        Cell cl{{a, b, d, c}};
        cells.push_back(cl);
        const int ring[4] = {a, b, c, d};
        for (int k = 0; k < 4; ++k)
        {
            const int x = ring[k], y = ring[(k + 1) % 4];
            if (x == y) continue;
            if (++edgeCount[edgeKey(x, y)] == 1) edgeList.push_back({std::min(x, y), std::max(x, y)});
        }
    };
    size_t holesLeft = 0, smallPieces = 0;
    // Quads only.  A region with k corners becomes k quads, one at each corner: the corner, the middle of the side after it, the
    // middle of the region, the middle of the side before it.  The middle of a side is one node for the two regions that have
    // the side, so what was a closed partition is a closed mesh of quads still (the first step of a Catmull-Clark subdivision:
    // every face of any polygon mesh becomes quads).  A region with four corners is four cells in a block, like the lattice
    // round it; a region with three or five has a vertex in the middle with three or five cells round it: where the lattice has
    // its singularities, as every quad mesh of a closed surface must.
    std::map<std::pair<int, int>, int> sideNode;
    std::vector<int> toPlace;                                   // nodes made here: put onto the surface in one go
    std::vector<int> placeRegion;                               // (and for the middle of a region, the region it is in)
    std::vector<int> placeHint;                                 // (for the middle of a side, the vertex half way along its line)
    std::vector<std::array<int, 2>> placeEnds;                  // (and the nodes at its ends)
    std::vector<std::vector<int>> placeLine;                    // (and for the middle of a side, the vertices of its line)
    std::vector<std::vector<int>> aroundRegion(static_cast<size_t>(R));     // per region: the nodes round it (corners and middles of sides)
    auto middleNode = [&](const std::vector<int>& ids, int inRegion) {
        V3 c = V3::Zero(), n = V3::Zero();
        for (int id : ids)
        {
            c += nodes[size_t(id)].p;
            n += nodes[size_t(id)].n;
        }
        c /= double(ids.size());
        GNode g;
        g.p = c;
        g.n = n.norm() > 1e-9 ? V3(n.normalized()) : V3(0, 1, 0);
        nodes.push_back(g);
        toPlace.push_back(int(nodes.size()) - 1);
        placeRegion.push_back(inRegion);
        placeHint.push_back(-1);
        placeEnds.push_back({-1, -1});
        placeLine.emplace_back();
        return int(nodes.size()) - 1;
    };
    // The middle of a side is the straight middle of its two corners put onto the surface, if that comes to rest near the line the
    // two regions share; else the vertex half way along that line (the line is on the surface, so the middle is, and it cannot be
    // across a hole or a slot from the corners).
    auto sideMiddle = [&](int a, int b, const std::vector<int>& line) {
        const std::pair<int, int> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        const auto it = sideNode.find(key);
        if (it != sideNode.end()) return it->second;
        const int m = middleNode({a, b}, -1);
        placeEnds.back() = {a, b};
        if (line.size() >= 3)
        {
            double total = 0;
            for (size_t k = 0; k + 1 < line.size(); ++k) total += (mesh.p[size_t(line[k + 1])] - mesh.p[size_t(line[k])]).norm();
            double run = 0;
            size_t pick = line.size() / 2;
            double bestGap = 1e300;
            for (size_t k = 1; k + 1 < line.size(); ++k)
            {
                run += (mesh.p[size_t(line[k])] - mesh.p[size_t(line[k - 1])]).norm();
                const double gap = std::fabs(run - 0.5 * total);
                if (gap < bestGap)
                {
                    bestGap = gap;
                    pick = k;
                }
            }
            placeHint.back() = line[pick];
            placeLine.back() = line;
        }
        sideNode[key] = m;
        return m;
    };
    for (int r = 0; r < R; ++r)
    {
        const std::vector<int>& cr = corners[size_t(r)];
        if (cyclesOf[size_t(r)].empty())
        {
            // (a closed piece of the surface that lies inside one square of the lattice: it has no line and so no corner, and is
            // smaller than two cells across -- a bump or a speck of the field -- it gets no cell)
            ++smallPieces;
            continue;
        }
        if (cr.size() < 3)
        {
            ++holesLeft;
            continue;
        }
        std::vector<int> nd;
        for (int c : cr) nd.push_back(nodeFor(size_t(nodeIndex.at(c))));
        const size_t K = nd.size();
        const int centre = middleNode(nd, r);
        std::vector<int> mid(K);
        const std::vector<std::vector<int>>& sv = sideVerts[size_t(r)];
        static const std::vector<int> noLine;
        for (size_t i = 0; i < K; ++i) mid[i] = sideMiddle(nd[i], nd[(i + 1) % K], sv.size() == K ? sv[i] : noLine);
        for (size_t i = 0; i < K; ++i) addCell(nd[i], mid[i], centre, mid[(i + K - 1) % K]);
        aroundRegion[size_t(r)] = nd;
        aroundRegion[size_t(r)].insert(aroundRegion[size_t(r)].end(), mid.begin(), mid.end());
    }
    {
        // (the nodes made here, onto the surface)
        std::vector<V3> pts;
        for (int id : toPlace) pts.push_back(nodes[size_t(id)].p);
        const std::vector<V3> before = pts;
        std::vector<V3> nn;
        std::vector<char> ok;
        projectToSurface(F, pts, nn, ok, 0.5 * cell, 1e-4);
        // A node is ON the surface, always.  The middle of a region that goes round a hole is in the air of the hole: if it does not
        // go back onto the surface within a short reach, it is a vertex of the scaffold (which is on the surface) of the region it
        // is the middle of, or of the line of its side.  There is no case in which a node stays where it was made.
        // And no side has no length: the middle of a side is a quarter of the way between its corners from both of them (else the
        // vertex of its line that is furthest from both); the middle of a region is a fifth of a cell from every node round it
        // (else the vertex of the region that is clearest of them).  A quad with two nodes at one place is a triangle.
        std::unordered_map<uint64_t, std::vector<int>> vgrid;
        const double vgs = 2.0 * hgrid;
        auto vkey = [&](int64_t x, int64_t y, int64_t z) {
            return uint64_t(x) * 73856093ull ^ uint64_t(y) * 19349663ull ^ uint64_t(z) * 83492791ull;
        };
        bool gridMade = false;
        size_t snapped = 0, inRegion = 0, onLine = 0;
        auto nearestOf = [&](int r, const V3& at) {
            int best = -1;
            double bestD = 1e300;
            for (int v : regionVerts[size_t(r)])
            {
                const double d = (mesh.p[size_t(v)] - at).squaredNorm();
                if (d < bestD)
                {
                    bestD = d;
                    best = v;
                }
            }
            return best;
        };
        auto validProjection = [&](size_t k) {
            return ok[k] && (pts[k] - before[k]).norm() < 0.35 * cell && nn[k].dot(nodes[size_t(toPlace[k])].n) > 0.5;
        };
        auto putAt = [&](size_t k, int v) {
            GNode& g = nodes[size_t(toPlace[k])];
            g.p = mesh.p[size_t(v)];
            g.n = mesh.n[size_t(v)];
        };
        auto nearestVertexOfScaffold = [&](size_t k) {
            const GNode& g = nodes[size_t(toPlace[k])];
            if (!gridMade)
            {
                for (int v = 0; v < N; ++v)
                    vgrid[vkey(int64_t(std::floor(mesh.p[size_t(v)][0] / vgs)), int64_t(std::floor(mesh.p[size_t(v)][1] / vgs)),
                               int64_t(std::floor(mesh.p[size_t(v)][2] / vgs)))].push_back(v);
                gridMade = true;
            }
            int best = -1;
            double bestD = 1e300;
            for (int ring = 1; ring <= 12 && best < 0; ++ring)
            {
                const int64_t cx = int64_t(std::floor(g.p[0] / vgs)), cy = int64_t(std::floor(g.p[1] / vgs)), cz = int64_t(std::floor(g.p[2] / vgs));
                for (int64_t dx = -ring; dx <= ring; ++dx)
                    for (int64_t dy = -ring; dy <= ring; ++dy)
                        for (int64_t dz = -ring; dz <= ring; ++dz)
                        {
                            const auto it = vgrid.find(vkey(cx + dx, cy + dy, cz + dz));
                            if (it == vgrid.end()) continue;
                            for (int v : it->second)
                            {
                                if (mesh.n[size_t(v)].dot(g.n) < 0.3) continue;
                                const double d = (mesh.p[size_t(v)] - g.p).squaredNorm();
                                if (d < bestD)
                                {
                                    bestD = d;
                                    best = v;
                                }
                            }
                        }
            }
            return best < 0 ? 0 : best;
        };
        // the middles of the sides first (they are among the nodes round a region)
        for (size_t k = 0; k < toPlace.size(); ++k)
        {
            if (placeRegion[k] >= 0) continue;
            GNode& g = nodes[size_t(toPlace[k])];
            const V3 pa = nodes[size_t(placeEnds[k][0])].p, pb = nodes[size_t(placeEnds[k][1])].p;
            const double need = 0.25 * (pa - pb).norm();
            auto apart = [&](const V3& p) { return (p - pa).norm() >= need && (p - pb).norm() >= need; };
            const int hint = placeHint[k];
            // (the straight middle of the corners put onto the surface, if it comes to rest near the line the two regions share)
            if (validProjection(k) && (hint < 0 || (mesh.p[size_t(hint)] - pts[k]).norm() < 0.5 * cell) && apart(pts[k]))
            {
                g.p = pts[k];
                g.n = nn[k];
                continue;
            }
            if (hint >= 0)
            {
                int pick = apart(mesh.p[size_t(hint)]) ? hint : -1;
                if (pick < 0)
                {
                    double bestClear = -1;
                    for (int v : placeLine[k])
                    {
                        const V3& q = mesh.p[size_t(v)];
                        const double cl = std::min((q - pa).norm(), (q - pb).norm());
                        if (cl > bestClear + 1e-9)
                        {
                            bestClear = cl;
                            pick = v;
                        }
                    }
                }
                if (pick >= 0)
                {
                    putAt(k, pick);
                    ++onLine;
                    continue;
                }
            }
            // (a line too short to have a vertex of its own)
            if (validProjection(k))
            {
                g.p = pts[k];
                g.n = nn[k];
                continue;
            }
            putAt(k, nearestVertexOfScaffold(k));
            ++snapped;
        }
        // the middles of the regions
        for (size_t k = 0; k < toPlace.size(); ++k)
        {
            const int reg = placeRegion[k];
            if (reg < 0) continue;
            GNode& g = nodes[size_t(toPlace[k])];
            const std::vector<int>& around = aroundRegion[size_t(reg)];
            auto clearance = [&](const V3& p) {
                double c = 1e300;
                for (int id : around) c = std::min(c, (nodes[size_t(id)].p - p).norm());
                return c;
            };
            const double want = 0.2 * cell;
            if (validProjection(k))
            {
                // (the middle of a region must come to rest on the region: on the surface near it, not on the far side of a hole)
                const int closeV = nearestOf(reg, pts[k]);
                if (closeV >= 0 && (mesh.p[size_t(closeV)] - pts[k]).norm() < 0.5 * cell && clearance(pts[k]) >= want)
                {
                    g.p = pts[k];
                    g.n = nn[k];
                    continue;
                }
            }
            // (the vertex of the region that is nearest to the middle of its corners and clear of the nodes round it; if none is
            // clear, the clearest)
            int pick = -1, clearest = -1;
            double bestD = 1e300, bestClear = -1;
            for (int v : regionVerts[size_t(reg)])
            {
                const double cl = clearance(mesh.p[size_t(v)]);
                if (cl > bestClear)
                {
                    bestClear = cl;
                    clearest = v;
                }
                if (cl >= want)
                {
                    const double d = (mesh.p[size_t(v)] - before[k]).squaredNorm();
                    if (d < bestD)
                    {
                        bestD = d;
                        pick = v;
                    }
                }
            }
            if (pick < 0) pick = clearest;
            if (pick >= 0)
            {
                putAt(k, pick);
                ++inRegion;
                continue;
            }
            putAt(k, nearestVertexOfScaffold(k));
            ++snapped;
        }
        if (say)
            std::fprintf(stderr, "[quads] %zu of %zu middle nodes could not be projected: %zu went to the middle vertex of their line, %zu to a vertex of their region, %zu to the nearest vertex of the scaffold\n",
                         snapped + inRegion + onLine, toPlace.size(), onLine, inRegion, snapped);
    }
    // (the outline of the surface -- the scaffold's own boundary -- and the nearest point on it: for the rule that the outer nodes lie on it, and for
    // every node to stay inside it)
    std::vector<std::array<V3, 2>> outline;
    {
        std::unordered_map<uint64_t, int> uses;
        for (const auto& t : mesh.tri)
            for (int s = 0; s < 3; ++s) ++uses[edgeKey(t[size_t(s)], t[size_t((s + 1) % 3)])];
        for (const auto& kv : uses)
            if (kv.second == 1) outline.push_back({mesh.p[size_t(kv.first >> 32)], mesh.p[size_t(kv.first & 0xffffffffu)]});
        std::sort(outline.begin(), outline.end(), [](const std::array<V3, 2>& a, const std::array<V3, 2>& b) {
            for (int k = 0; k < 3; ++k)
            {
                if (a[0][k] != b[0][k]) return a[0][k] < b[0][k];
                if (a[1][k] != b[1][k]) return a[1][k] < b[1][k];
            }
            return false;
        });
    }
    auto onOutline = [&](const V3& q) {
        V3 best = q;
        double bestD = 1e300;
        for (const auto& seg : outline)
        {
            const V3 d = seg[1] - seg[0];
            const double len2 = d.squaredNorm();
            const double u = len2 > 1e-18 ? std::min(1.0, std::max(0.0, (q - seg[0]).dot(d) / len2)) : 0.0;
            const V3 x = seg[0] + u * d;
            const double dd = (x - q).squaredNorm();
            if (dd < bestD)
            {
                bestD = dd;
                best = x;
            }
        }
        return best;
    };
    // ---- CELLS AS BIG AS ASKED.  Where the layout stretches its rows (a narrow neck of the surface, a tight bend) a cell is left with a side
    // much longer than a cell, and the struts of its unit cell run across it as long chords, in the air over the curve.  A side longer than
    // one and a half cells is cut: a ROW of cells is inserted through it -- the chord of the quad mesh that crosses the side (it goes from a
    // quad to the one across its opposite side, and on, to the edge of the surface or round to where it began) -- every side the row crosses
    // gets a node in the middle, put onto the surface, and every quad of the row becomes two.  A quad mesh stays a quad mesh, every edge
    // stays on two cells (or on one, at the edge of the surface); the relaxation below then evens the sizes
    {
        const double longest = 1.5 * cell;
        const size_t cap = 3 * cells.size() + 64;
        size_t inserted = 0, rounds = 0;
        std::set<uint64_t> blocked;             // (sides of a row that touches a cell that is no quad: that row is left alone)
        auto ringOf = [](const Cell& c) { return std::array<int, 4>{c.c[0], c.c[1], c.c[3], c.c[2]}; };
        auto makeCell = [](int a, int b, int c, int d) { return Cell{{a, b, d, c}}; };
        auto rebuildEdges = [&]() {
            edgeCount.clear();
            edgeList.clear();
            for (const Cell& cl : cells)
            {
                const auto r = ringOf(cl);
                for (int k = 0; k < 4; ++k)
                {
                    const int x = r[size_t(k)], y = r[size_t((k + 1) % 4)];
                    if (x == y) continue;
                    if (++edgeCount[edgeKey(x, y)] == 1) edgeList.push_back({std::min(x, y), std::max(x, y)});
                }
            }
        };
        for (; rounds < 80; ++rounds)
        {
            // the longest side
            int ea = -1, eb = -1;
            double worstLen = longest;
            for (const auto& e : edgeList)
            {
                if (blocked.count(edgeKey(e[0], e[1]))) continue;
                const double len = (nodes[size_t(e[0])].p - nodes[size_t(e[1])].p).norm();
                if (len > worstLen)
                {
                    worstLen = len;
                    ea = e[0];
                    eb = e[1];
                }
            }
            if (ea < 0 || cells.size() > cap) break;
            // the row through it
            std::unordered_map<uint64_t, std::vector<int>> cellsOfEdge;
            std::vector<char> usable(cells.size(), 1);
            for (size_t ci = 0; ci < cells.size(); ++ci)
            {
                const auto r = ringOf(cells[ci]);
                for (int k = 0; k < 4; ++k)
                    if (r[size_t(k)] == r[size_t((k + 1) % 4)]) usable[ci] = 0;           // (a cell with a repeated corner is no quad: no row goes through it)
                if (!usable[ci]) continue;
                for (int k = 0; k < 4; ++k) cellsOfEdge[edgeKey(r[size_t(k)], r[size_t((k + 1) % 4)])].push_back(int(ci));
            }
            std::set<uint64_t> row;
            std::vector<uint64_t> queue = {edgeKey(ea, eb)};
            row.insert(queue.back());
            while (!queue.empty())
            {
                const uint64_t key = queue.back();
                queue.pop_back();
                const auto it = cellsOfEdge.find(key);
                if (it == cellsOfEdge.end()) continue;
                for (int ci : it->second)
                {
                    const auto r = ringOf(cells[size_t(ci)]);
                    for (int k = 0; k < 4; ++k)
                        if (edgeKey(r[size_t(k)], r[size_t((k + 1) % 4)]) == key)
                        {
                            const uint64_t across = edgeKey(r[size_t((k + 2) % 4)], r[size_t((k + 3) % 4)]);
                            if (row.insert(across).second) queue.push_back(across);
                        }
                }
            }
            // (a row that reaches a cell with a repeated corner would leave a node on one side of it only: it is left alone)
            bool bad = false;
            for (size_t ci = 0; ci < cells.size() && !bad; ++ci)
                if (!usable[ci])
                {
                    const auto r = ringOf(cells[ci]);
                    for (int k = 0; k < 4; ++k)
                        if (r[size_t(k)] != r[size_t((k + 1) % 4)] && row.count(edgeKey(r[size_t(k)], r[size_t((k + 1) % 4)]))) bad = true;
                }
            if (bad)
            {
                blocked.insert(row.begin(), row.end());
                continue;
            }
            // a node in the middle of every side of the row
            std::map<uint64_t, int> middleOf;
            std::vector<V3> at;
            std::vector<int> made;
            for (uint64_t key : row)
            {
                const int a = int(key >> 32), b = int(key & 0xffffffffu);
                GNode g;
                g.p = 0.5 * (nodes[size_t(a)].p + nodes[size_t(b)].p);
                const V3 nn = nodes[size_t(a)].n + nodes[size_t(b)].n;
                g.n = nn.norm() > 1e-9 ? V3(nn.normalized()) : nodes[size_t(a)].n;
                nodes.push_back(g);
                middleOf[key] = int(nodes.size()) - 1;
                made.push_back(int(nodes.size()) - 1);
                at.push_back(g.p);
            }
            // the quads of the row, each split by the nodes of its sides (a quad that the row crosses twice, in four)
            std::vector<Cell> next;
            std::vector<int> centreOf;
            for (size_t ci = 0; ci < cells.size(); ++ci)
            {
                const auto r = ringOf(cells[ci]);
                int m[4] = {-1, -1, -1, -1};
                for (int k = 0; k < 4; ++k)
                {
                    const auto it = middleOf.find(edgeKey(r[size_t(k)], r[size_t((k + 1) % 4)]));
                    if (it != middleOf.end() && usable[ci]) m[k] = it->second;
                }
                const bool s02 = m[0] >= 0 && m[2] >= 0, s13 = m[1] >= 0 && m[3] >= 0;
                if (s02 && s13)
                {
                    GNode g;
                    g.p = 0.25 * (nodes[size_t(r[0])].p + nodes[size_t(r[1])].p + nodes[size_t(r[2])].p + nodes[size_t(r[3])].p);
                    g.n = nodes[size_t(r[0])].n;
                    nodes.push_back(g);
                    const int q = int(nodes.size()) - 1;
                    made.push_back(q);
                    at.push_back(g.p);
                    next.push_back(makeCell(r[0], m[0], q, m[3]));
                    next.push_back(makeCell(m[0], r[1], m[1], q));
                    next.push_back(makeCell(q, m[1], r[2], m[2]));
                    next.push_back(makeCell(m[3], q, m[2], r[3]));
                }
                else if (s02)
                {
                    next.push_back(makeCell(r[0], m[0], m[2], r[3]));
                    next.push_back(makeCell(m[0], r[1], r[2], m[2]));
                }
                else if (s13)
                {
                    next.push_back(makeCell(r[1], m[1], m[3], r[0]));
                    next.push_back(makeCell(m[1], r[2], r[3], m[3]));
                }
                else
                    next.push_back(cells[ci]);
            }
            cells.swap(next);
            // (the new nodes, onto the surface)
            std::vector<V3> before = at, nn;
            std::vector<char> ok;
            projectToSurface(F, at, nn, ok, 0.5 * cell, 1e-4);
            for (size_t k = 0; k < made.size(); ++k)
                if (ok[k] && (at[k] - before[k]).norm() < 0.3 * cell)
                {
                    nodes[size_t(made[k])].p = at[k];
                    if (nn[k].dot(nodes[size_t(made[k])].n) > 0.3) nodes[size_t(made[k])].n = nn[k];
                }
            if (mask.region && !outline.empty())
            {
                // (the middle of a side that crosses a bay of the outline is beyond the patch: it is put on the outline)
                std::vector<V3> now;
                for (int id : made) now.push_back(nodes[size_t(id)].p);
                std::vector<double> fr;
                std::vector<V3> gr;
                mask.region->eval(now, fr, gr);
                for (size_t k = 0; k < made.size(); ++k)
                    if (fr[k] > 0.0) nodes[size_t(made[k])].p = onOutline(nodes[size_t(made[k])].p);
            }
            rebuildEdges();
            ++inserted;
        }
        if (say && inserted) std::fprintf(stderr, "[quads] %zu rows of cells inserted where a side was longer than one and a half cells: %zu cells\n", inserted, cells.size());
    }
    // ---- THE OUTER NODES LIE ON THE OUTLINE.  Where the surface ends (the outline of a patch, the edge a box cuts), the cells end, and every
    // node on an edge that only one cell has -- an outer node -- is ON that edge of the surface: put onto the nearest point of the scaffold's
    // own boundary, and kept there by the relaxation below.  The sides between two outer nodes follow the outline too (see the lines of the
    // edges, at the end).  A rule of the layout, not an approximation: a lattice never reaches past the surface it is laid on, nor stops short
    // of its edge by a node
    std::vector<char> outerNode(nodes.size(), 0);
    if (!outline.empty())
    {
        for (const auto& e : edgeList)
            if (edgeCount[edgeKey(e[0], e[1])] == 1) outerNode[size_t(e[0])] = outerNode[size_t(e[1])] = 1;
        size_t snappedOuter = 0;
        double farthest = 0;
        for (size_t i = 0; i < nodes.size(); ++i)
            if (outerNode[i])
            {
                const V3 x = onOutline(nodes[i].p);
                farthest = std::max(farthest, (x - nodes[i].p).norm());
                nodes[i].p = x;
                ++snappedOuter;
            }
        if (say) std::fprintf(stderr, "[quads] %zu outer nodes put on the outline of the surface (the farthest moved %.2f mm)\n", snappedOuter, farthest);
    }
    // ---- relaxation: the nodes are moved over the surface so that the cells are of one size and their corners are square: every
    // node goes part of the way to where its sides would be a cell long and it would be in the middle of its neighbours, and back
    // onto the surface; a node on a sharp edge goes along the edge only (so that the edge stays), and no move is made that folds a
    // cell (turns a corner of it the wrong way) or throws a node to another face.  Only positions change: the mesh stays closed and
    // made of quads, and a node stays on the surface.
    {
        const size_t M = nodes.size();
        std::vector<std::vector<int>> nbr(M);
        for (const auto& e : edgeList)
        {
            nbr[size_t(e[0])].push_back(e[1]);
            nbr[size_t(e[1])].push_back(e[0]);
        }
        std::vector<std::array<int, 4>> ring(cells.size());
        std::vector<std::vector<int>> cellsOf(M);
        for (size_t c = 0; c < cells.size(); ++c)
        {
            ring[c] = {cells[c].c[0], cells[c].c[1], cells[c].c[3], cells[c].c[2]};
            for (int id : ring[c]) cellsOf[size_t(id)].push_back(int(c));
        }
        // the way round a cell, seen from outside, in the units of a cell: positive at a corner that turns as the cell does
        double orient = 0;
        auto corner = [&](const std::array<int, 4>& r, int k) {
            const V3& cur = nodes[size_t(r[size_t(k)])].p;
            const V3& nx = nodes[size_t(r[size_t((k + 1) % 4)])].p;
            const V3& pv = nodes[size_t(r[size_t((k + 3) % 4)])].p;
            return (nx - cur).cross(pv - cur).dot(nodes[size_t(r[size_t(k)])].n) / (cell * cell);
        };
        for (const auto& r : ring)
            for (int k = 0; k < 4; ++k) orient += corner(r, k) > 0 ? 1.0 : -1.0;
        const double turn = orient >= 0 ? 1.0 : -1.0;
        // (the worst corner of the cells a node is in, with its sign)
        auto worstCorner = [&](size_t i) {
            double w = 1e300;
            for (int c : cellsOf[i])
                for (int k = 0; k < 4; ++k) w = std::min(w, turn * corner(ring[size_t(c)], k));
            return w;
        };
        const int relaxIterations = 20;
        const double wSpring = 0.1, wMean = 0.15;       // (how far a node goes towards a cell-long side, and towards the middle of its neighbours)
        std::vector<char> pinned(M, 0);
        std::vector<V3> along(M, V3::Zero());
        for (size_t i = 0; i < M; ++i)
        {
            int across = -1;
            double least = 0.75;
            for (int j : nbr[i])
                if (nodes[i].n.dot(nodes[size_t(j)].n) < least)
                {
                    least = nodes[i].n.dot(nodes[size_t(j)].n);
                    across = j;
                }
            if (across < 0) continue;
            pinned[i] = 1;
            const V3 t = nodes[i].n.cross(nodes[size_t(across)].n);
            along[i] = t.norm() > 0.3 ? V3(t.normalized()) : V3::Zero();
        }
        for (size_t i = 0; i < M; ++i)
            if (outerNode[i])
            {
                pinned[i] = 1;                  // (an outer node stays where it was put: on the outline)
                along[i] = V3::Zero();
            }
        size_t movable = 0, edgeNodes = 0;
        for (size_t i = 0; i < M; ++i)
        {
            movable += pinned[i] ? 0 : 1;
            edgeNodes += pinned[i] && along[i].norm() > 0.5 ? 1 : 0;
        }
        size_t moved = 0, folds = 0;
        for (int it = 0; it < relaxIterations; ++it)
        {
            std::vector<V3> pts;
            std::vector<size_t> who;
            for (size_t i = 0; i < M; ++i)
            {
                if (nbr[i].empty()) continue;
                if (pinned[i] && along[i].norm() < 0.5) continue;
                V3 spring = V3::Zero(), mean = V3::Zero();
                for (int j : nbr[i])
                {
                    const V3 e = nodes[size_t(j)].p - nodes[i].p;
                    const double len = e.norm();
                    mean += nodes[size_t(j)].p;
                    if (len > 1e-9) spring += e * (1.0 - cell / len);
                }
                spring /= double(nbr[i].size());
                mean /= double(nbr[i].size());
                V3 d = wSpring * spring + wMean * (mean - nodes[i].p);
                if (pinned[i]) d = along[i] * d.dot(along[i]);
                pts.push_back(nodes[i].p + d);
                who.push_back(i);
            }
            const std::vector<V3> before = pts;
            std::vector<V3> nn;
            std::vector<char> ok;
            projectToSurface(F, pts, nn, ok, 0.5 * cell, 1e-4);
            // (no node is moved beyond the outline of the surface)
            std::vector<double> beyond(pts.size(), -1.0);
            if (mask.region && !pts.empty())
            {
                std::vector<V3> gr;
                mask.region->eval(pts, beyond, gr);
            }
            for (size_t k = 0; k < who.size(); ++k)
            {
                const size_t i = who[k];
                if (!ok[k] || (pts[k] - nodes[i].p).norm() > 0.3 * cell || nn[k].dot(nodes[i].n) < (pinned[i] ? 0.9 : 0.8) || beyond[k] > 0.0) continue;
                const V3 oldP = nodes[i].p, oldN = nodes[i].n;
                const double oldWorst = worstCorner(i);
                nodes[i].p = pts[k];
                nodes[i].n = nn[k];
                const double newWorst = worstCorner(i);
                if (newWorst <= 0.01 && newWorst < oldWorst)
                {
                    nodes[i].p = oldP;
                    nodes[i].n = oldN;
                    ++folds;
                    continue;
                }
                ++moved;
            }
        }
        if (say)
            std::fprintf(stderr, "[quads] relaxed: %zu nodes move over their faces, %zu go along a sharp edge, the rest stay; %zu moves made, %zu refused for folding a cell\n",
                         movable, edgeNodes, moved, folds);
    }
    if (say)
    {
        // THE CHECK on the shape: sides that are no length at all (two nodes at one place: a quad that is a triangle)
        size_t shortSides = 0;
        for (const auto& e : edgeList)
            if ((nodes[size_t(e[0])].p - nodes[size_t(e[1])].p).norm() < 0.1 * cell) ++shortSides;
        std::fprintf(stderr, "[quads] sides shorter than a tenth of a cell: %zu of %zu\n", shortSides, edgeList.size());
    }
    if (say)
    {
        // THE CHECK on the nodes: how far each is from the surface (the field there over its gradient)
        std::vector<V3> at, g;
        std::vector<double> f;
        for (const GNode& nd : nodes) at.push_back(nd.p);
        F.eval(at, f, g);
        size_t off = 0;
        double worst = 0;
        for (size_t i = 0; i < f.size(); ++i)
        {
            const double d = g[i].norm() > 1e-12 ? std::abs(f[i]) / g[i].norm() : 0.0;
            worst = std::max(worst, d);
            if (d > 0.1 * hgrid) ++off;
        }
        std::fprintf(stderr, "[quads] nodes more than a tenth of a grid step off the surface: %zu of %zu (the worst %.3f mm)\n", off, nodes.size(), worst);
    }
    {
        // THE CHECK.  Every cell has four corners that are four different nodes (a quad, never a triangle), and every edge is on
        // two cells -- or on one, where the surface itself ends (the box).  The layout above cannot fail this; if it ever does,
        // the layout is not used and the run says so, rather than going on with a mesh that is open or has a triangle in it.
        size_t one = 0, two = 0, many = 0, degenerate = 0;
        for (const auto& kv : edgeCount)
        {
            if (kv.second == 1) ++one;
            else if (kv.second == 2) ++two;
            else ++many;
        }
        for (const Cell& c : cells)
        {
            int ids[4] = {c.c[0], c.c[1], c.c[2], c.c[3]};
            std::sort(ids, ids + 4);
            if (std::unique(ids, ids + 4) - ids < 4) ++degenerate;
        }
        // the edge of the surface: the scaffold's own boundary edges (the box); with none of those, no edge may be on one cell only
        size_t scaffoldBoundary = 0;
        for (const auto& kv : trisOfEdge)
            if (kv.second.size() == 1) ++scaffoldBoundary;
        if (say)
            std::fprintf(stderr, "[quads] %zu cells, all of four different corners but %zu; every edge: %zu on one cell only (the edge of the surface), %zu on two, %zu on more; %zu regions left out\n",
                         cells.size(), degenerate, one, two, many, holesLeft, smallPieces);
        if (degenerate > 0 || many > 0 || holesLeft > 0 || (scaffoldBoundary == 0 && one > 0))
        {
            char buf[320];
            std::snprintf(buf, sizeof buf,
                          "the cell layout is not a closed mesh of quads (%zu cells with a repeated corner, %zu edges on more than two cells, %zu on one, %zu regions left out): it is not used",
                          degenerate, many, one, holesLeft);
            note = buf;
            if (say) std::fprintf(stderr, "[quads] %s\n", buf);
        }
    }
    // the line of every edge: the chord, carried onto the surface at three points
    {
        std::vector<V3> pts;
        for (const auto& e : edgeList)
            for (int k = 1; k <= 3; ++k)
                pts.push_back(nodes[size_t(e[0])].p + 0.25 * k * (nodes[size_t(e[1])].p - nodes[size_t(e[0])].p));
        const std::vector<V3> before = pts;
        std::vector<V3> nn;
        std::vector<char> ok;
        projectToSurface(F, pts, nn, ok, 0.5 * cell, 1e-4);
        for (size_t i = 0; i < edgeList.size(); ++i)
        {
            std::vector<V3> line = {nodes[size_t(edgeList[i][0])].p};
            // (a side between two outer nodes that only one cell has runs along the outline of the surface: its points are put on it)
            const bool alongOutline = !outline.empty() && outerNode[size_t(edgeList[i][0])] && outerNode[size_t(edgeList[i][1])] &&
                                      edgeCount[edgeKey(edgeList[i][0], edgeList[i][1])] == 1;
            for (int k = 0; k < 3; ++k)
            {
                const size_t at = 3 * i + size_t(k);
                const V3 chord = before[at];
                const V3 q = ok[at] && (pts[at] - chord).norm() < 0.3 * cell ? pts[at] : chord;
                line.push_back(alongOutline ? onOutline(q) : q);
            }
            line.push_back(nodes[size_t(edgeList[i][1])].p);
            paths[edgeKey(edgeList[i][0], edgeList[i][1])] = std::move(line);
        }
    }
    firstDirection = axis;
    lap("cells");
}
