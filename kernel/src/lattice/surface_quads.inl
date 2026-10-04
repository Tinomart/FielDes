// Included by surface_cells.cpp, inside its anonymous namespace: the cells of a surface, as ONE CLOSED MESH OF QUADS, made from the field alone.
//
// CLOSED AND MADE OF QUADS BY CONSTRUCTION, and checked.  No triangle mesh is made: the surface is a cloud of points with a neighbour graph
// (a hop joins two points that the surface really joins), and everything is read off that.
//
//   samples      points of the surface a third of a spacing apart, found by projecting the centres of the cubes the surface passes through
//   carriers     evenly spread points of the surface (a Poisson choice, then Lloyd steps over the samples): one per h = s / 2.5
//   the graph    two points are neighbours if the SURFACE joins them, not if they are close in space (a hop is accepted if its middle lies on the
//                surface or the chord, put onto the surface, is a connected curve of about its length): a wall between two points is never crossed.
//                A way over the graph is walked with a frame carried by the least rotation at every hop, so a neighbour's tangent plane is
//                UNFOLDED onto ours round any edge, however many
//   the field    a direction (a 4-RoSy field: each carrier turns towards its neighbours' directions carried to its plane; sharp edges are lines it
//                follows) and then a lattice position (each carrier takes the lattice point its neighbours imply, rounded to whole spacings)
//   the lattice  vertices are clusters of carriers that share a lattice point (seeded by the best centred one, no chains), put where the lattice
//                point is by walking along the surface.  Two vertices are joined if their REGIONS touch: every point of the surface graph belongs to
//                the vertex nearest to it over the surface, and the edges are read off where the regions meet, on the dense points of the surface
//                (not off the lattice field, whose noise would decide the topology)
//   the faces    the neighbours of a vertex in order round it (angles of the unfolded ways): every directed edge is on exactly ONE face, so the
//                faces close up whatever the edges are
//   the quads    a face with k corners is k quads (a corner, the middle of the side after it, the middle of the face, the middle of the side
//                before it); the middle of a side is half way along the way over the surface, the middle of a face the mean of its corners unfolded,
//                brought to the surface; sides are shared, so the quads fit edge to edge.  s = 2 cell: the cells are `cell` wide
//   the check    every quad has four different corners and every edge is on exactly two quads: a layout that is not is not used
//
// Every node of the result is a point of the surface.  What comes out is what the rest of the file takes: nodes, cells of four nodes, and the lines
// the edges of the cells follow.

using Mat3 = Eigen::Matrix3d;

inline V3 inPlane(const V3& v, const V3& n) { return v - v.dot(n) * n; }
inline V3 unit0(const V3& v)
{
    const double L = v.norm();
    return L > 1e-12 ? V3(v / L) : V3(V3::Zero());
}
inline double rnd(double x) { return std::floor(x + 0.5); }

// How many samples span a carrier spacing h (the sampling step is h / this: 3 by default; FIELDES_SC_DELTA sets another, for the developer)
// (for the developer: a number from the environment, or the default)
inline double envNumber(const char* name, double byDefault)
{
    const char* v = std::getenv(name);
    return v ? std::atof(v) : byDefault;
}

inline double deltaDivisor()
{
    static const double d = std::getenv("FIELDES_SC_DELTA") ? std::max(2.0, std::atof(std::getenv("FIELDES_SC_DELTA"))) : 3.0;
    return d;
}

// The least rotation that takes the unit vector nf to nt
Mat3 leastRotation(const V3& nf, const V3& nt)
{
    const V3 k = nf.cross(nt);
    const double s = k.norm();
    if (s < 1e-9) return Mat3::Identity();
    const double c = nf.dot(nt);
    const V3 u = k / s;
    const double cc = 1.0 - c;
    Mat3 R;
    R << c + cc * u[0] * u[0], cc * u[0] * u[1] - s * u[2], cc * u[0] * u[2] + s * u[1],
         cc * u[0] * u[1] + s * u[2], c + cc * u[1] * u[1], cc * u[1] * u[2] - s * u[0],
         cc * u[0] * u[2] - s * u[1], cc * u[1] * u[2] + s * u[0], c + cc * u[2] * u[2];
    return R;
}

// ---------------------------------------------------------------------------------------------------------------------
// Points in a hash of cubes

class PointHash
{
public:
    explicit PointHash(double size) : size_(size) {}

    void add(int idx, const V3& p) { cells_[key(p, 0, 0, 0)].push_back(idx); }

    // fn(index) for every point in the 27 (or more) cubes round p, in a fixed order
    template <class Fn>
    void near(const V3& p, int reach, Fn fn) const
    {
        for (int dx = -reach; dx <= reach; ++dx)
            for (int dy = -reach; dy <= reach; ++dy)
                for (int dz = -reach; dz <= reach; ++dz)
                {
                    const auto it = cells_.find(key(p, dx, dy, dz));
                    if (it == cells_.end()) continue;
                    for (int i : it->second) fn(i);
                }
    }

private:
    uint64_t key(const V3& p, int dx, int dy, int dz) const
    {
        const int64_t x = int64_t(std::floor(p[0] / size_)) + dx + (1 << 20);
        const int64_t y = int64_t(std::floor(p[1] / size_)) + dy + (1 << 20);
        const int64_t z = int64_t(std::floor(p[2] / size_)) + dz + (1 << 20);
        return (uint64_t(x & 0x1FFFFF) << 42) | (uint64_t(y & 0x1FFFFF) << 21) | uint64_t(z & 0x1FFFFF);
    }

    double size_;
    std::unordered_map<uint64_t, std::vector<int>> cells_;
};

// ---------------------------------------------------------------------------------------------------------------------
// The cubes the surface passes through (a narrow band found by refining a coarse grid)

struct NearCubes
{
    std::vector<std::array<int, 3>> cubes;      // the lower corner of every cube (in steps of h from `origin`)
    V3 origin = V3::Zero();
    double h = 0;
};

NearCubes nearCubes(Field& F, const V3& lo, const V3& hi, double h0, int levels, bool say, int shift, const std::vector<V3>* nearPts = nullptr, double nearR = 0.0)
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
    // (where the grid of samples starts: a few different places, so that a layout that is refused can be made again from other samples)
    static const double start[4][3] = {{0.1373, 0.0719, 0.1131}, {0.3571, 0.4413, 0.2269}, {0.5821, 0.1937, 0.7019}, {0.8123, 0.6671, 0.4443}};
    const double* st = start[((shift % 4) + 4) % 4];
    const V3 origin = lo + V3(st[0] * v0, st[1] * v0, st[2] * v0);
    // (with `nearPts`: only the cubes within `nearR` of one of them are looked at: a small piece of the surface that is being sampled finer costs what it is, not what the box round it holds)
    PointHash HN(nearPts ? nearR + 0.8660254 * v0 : 1.0);
    if (nearPts)
        for (size_t k = 0; k < nearPts->size(); ++k) HN.add(int(k), (*nearPts)[k]);
    auto wanted = [&](const V3& c, double size) {
        if (!nearPts) return true;
        bool hit = false;
        HN.near(c, 1, [&](int k) {
            if (!hit && ((*nearPts)[size_t(k)] - c).norm() <= nearR + 0.8660254 * size) hit = true;
        });
        return hit;
    };
    auto centre = [&](const Vox& v, double s) -> V3 { return origin + V3((v[0] + 0.5) * s, (v[1] + 0.5) * s, (v[2] + 0.5) * s); };
    std::vector<Vox> cur;
    {
        const int nx = int(std::floor(ext[0] / v0)) + 1, ny = int(std::floor(ext[1] / v0)) + 1, nz = int(std::floor(ext[2] / v0)) + 1;
        std::vector<V3> pts;
        std::vector<Vox> all;
        std::vector<double> f;
        std::vector<V3> g;
        auto flush = [&]() {
            if (pts.empty()) return;
            F.eval(pts, f, g);
            for (size_t k = 0; k < pts.size(); ++k)
            {
                const double gn = g[k].norm();
                if (!std::isfinite(f[k]) || !(gn > 1e-12)) continue;
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
                    const V3 c0 = centre({i, j, k}, v0);
                    if (!wanted(c0, v0)) continue;
                    pts.push_back(c0);
                    all.push_back({i, j, k});
                }
            if (pts.size() >= 40000) flush();
        }
        flush();
    }
    stageSet(1.0 / double(deepest + 1), "level 1 of " + std::to_string(deepest + 1));
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
                        const V3 cw = centre(w, s);
                        if (!wanted(cw, s)) continue;
                        kids.push_back(w);
                        pts.push_back(cw);
                    }
        std::vector<double> f;
        std::vector<V3> g;
        F.eval(pts, f, g);
        cur.clear();
        for (size_t i = 0; i < kids.size(); ++i)
        {
            const double gn = g[i].norm();
            if (!std::isfinite(f[i]) || !(gn > 1e-12)) continue;
            if (std::abs(f[i]) <= reach * s * gn) cur.push_back(kids[i]);
        }
        stageSet(double(k + 1) / double(deepest + 1), "level " + std::to_string(k + 1) + " of " + std::to_string(deepest + 1));
    }
    out.cubes = std::move(cur);
    out.origin = origin;
    out.h = size.back();
    if (say) std::fprintf(stderr, "[quads] %zu cubes of %.2f mm that the surface may pass through\n", out.cubes.size(), out.h);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The surface: projection of points onto it

class Surf
{
public:
    // `step`: the step of the central differences that give the gradient
    Surf(Field& f, double step) : F(f), step_(step) {}

    // The value and the gradient at points: the gradient by CENTRAL DIFFERENCES of the value, never the evaluator's own derivative.  Where two
    // parts of the field are equal (a point exactly on an edge of a box) the evaluator's derivative is that of whichever part it meets first, which
    // is not the same in every run; the layout would then differ from run to run.  Differences of the value do not.
    void valueAndGradient(const std::vector<V3>& pts, std::vector<double>& f, std::vector<V3>& g)
    {
        std::vector<V3> unused;
        F.eval(pts, f, unused);
        g.assign(pts.size(), V3::Zero());
        const size_t chunk = 20000;
        std::vector<V3> around;
        std::vector<double> fa;
        for (size_t s = 0; s < pts.size(); s += chunk)
        {
            const size_t c = std::min(chunk, pts.size() - s);
            around.clear();
            around.reserve(6 * c);
            for (size_t k = 0; k < c; ++k)
                for (int axis = 0; axis < 3; ++axis)
                {
                    V3 d = V3::Zero();
                    d[axis] = step_;
                    around.push_back(pts[s + k] + d);
                    around.push_back(pts[s + k] - d);
                }
            F.eval(around, fa, unused);
            for (size_t k = 0; k < c; ++k)
            {
                const double* v = &fa[6 * k];
                g[s + k] = V3((v[0] - v[1]) / (2 * step_), (v[2] - v[3]) / (2 * step_), (v[4] - v[5]) / (2 * step_));
            }
        }
    }

    // Each point moved along the gradient to where the field is zero (at most `rounds` Newton steps), then the unit normal there and whether it got within `tol` of the surface.  A point whose
    // Newton step is shorter than a twentieth of the step the gradient is taken with is settled (the step is below the noise of the gradient): it is not evaluated again, which is nearly every
    // point after the second step -- the field of an imported part costs a good deal to evaluate.  With `fOut` / `gOut` the value and the gradient at the points' final places are handed back, so
    // that a caller does not evaluate them a second time
    void project(std::vector<V3>& p, std::vector<V3>& n, std::vector<char>& ok, int rounds, double tol, std::vector<double>* fOut = nullptr, std::vector<V3>* gOut = nullptr, bool report = false)
    {
        n.assign(p.size(), V3(0, 0, 1));
        ok.assign(p.size(), 0);
        std::vector<double> fAll(p.size(), 0.0);
        std::vector<V3> gAll(p.size(), V3::Zero());
        std::vector<size_t> active(p.size());
        std::iota(active.begin(), active.end(), size_t(0));
        const double settled = 0.05 * step_;
        std::vector<V3> sub;
        std::vector<double> f;
        std::vector<V3> g;
        for (int r = 0; r < rounds && !active.empty(); ++r)
        {
            sub.resize(active.size());
            for (size_t k = 0; k < active.size(); ++k) sub[k] = p[active[k]];
            valueAndGradient(sub, f, g);
            std::vector<size_t> moved;
            for (size_t k = 0; k < active.size(); ++k)
            {
                const size_t i = active[k];
                fAll[i] = f[k];
                gAll[i] = g[k];
                const double g2 = g[k].squaredNorm();
                if (!(g2 >= 1e-12) || !std::isfinite(f[k])) continue;                     // (no gradient to follow: it stays)
                if (std::abs(f[k]) / std::sqrt(g2) < settled) continue;                   // (settled)
                p[i] -= (f[k] / g2) * g[k];
                moved.push_back(i);
            }
            active.swap(moved);
            if (report) stageSet(double(r + 1) / double(rounds), "step " + std::to_string(r + 1) + " of at most " + std::to_string(rounds));
        }
        if (!active.empty())                                                             // (the points that moved in the last step: where are they now?)
        {
            sub.resize(active.size());
            for (size_t k = 0; k < active.size(); ++k) sub[k] = p[active[k]];
            valueAndGradient(sub, f, g);
            for (size_t k = 0; k < active.size(); ++k)
            {
                fAll[active[k]] = f[k];
                gAll[active[k]] = g[k];
            }
        }
        for (size_t k = 0; k < p.size(); ++k)
        {
            const double gn = gAll[k].norm();
            if (!(gn > 1e-9) || !std::isfinite(fAll[k])) continue;
            n[k] = gAll[k] / gn;
            ok[k] = std::abs(fAll[k]) / gn < tol;
        }
        if (fOut) *fOut = std::move(fAll);
        if (gOut) *gOut = std::move(gAll);
    }

    Field& F;

private:
    double step_;
};

// ---------------------------------------------------------------------------------------------------------------------
// Distances between points, as the field's frame sees them (carriers: a Poisson-like choice, evened by Lloyd steps)

double bendOf(double c) { return std::max(0.0, (0.9 - c) / 0.9); }

// The distance of the sample q (normal m) from the site p (normal n): the distance, a charge for leaving the tangent plane of the site (a different
// sheet) and a charge for turning
double pdistIso(const V3& p, const V3& n, const V3& q, const V3& m, double s)
{
    const V3 v = q - p;
    const double L = v.norm();
    const double c = std::abs(v.dot(n));
    const double b = bendOf(n.dot(m));
    return L + 4.0 * std::max(0.0, c - 0.25 * L) * std::max(0.0, 1.0 - b) + 0.5 * s * b;
}

// On one sheet: facing about the same way, and q in the tangent plane of p (not across a thin gap)
bool compatible(const V3& p, const V3& n, const V3& q, const V3& m)
{
    if (n.dot(m) < 0.5) return false;
    const V3 d = q - p;
    const double L = d.norm();
    if (L < 1e-9) return true;
    return std::abs(d.dot(n)) < 0.52 * L;
}

void fineSamples(Field& F, Surf& S, const V3& lo, const V3& hi, double delta, bool say, int shift, std::vector<V3>& P, std::vector<V3>& N, const std::vector<V3>* nearPts = nullptr, double nearR = 0.0)
{
    const NearCubes nc = nearCubes(F, lo, hi, delta, 2, say, shift, nearPts, nearR);
    std::vector<V3> pts;
    pts.reserve(nc.cubes.size());
    for (const auto& c : nc.cubes) pts.push_back(nc.origin + V3((c[0] + 0.5) * nc.h, (c[1] + 0.5) * nc.h, (c[2] + 0.5) * nc.h));
    std::vector<V3> nrm;
    std::vector<char> ok;
    stageBegin("putting the samples on the surface");
    S.project(pts, nrm, ok, 5, 0.02 * delta, nullptr, nullptr, true);
    stageSet(1.0);
    PointHash H(delta);
    for (size_t k = 0; k < pts.size(); ++k)
    {
        if (!ok[k]) continue;
        bool dup = false;
        H.near(pts[k], 1, [&](int i) {
            if (!dup && (P[size_t(i)] - pts[k]).norm() < 0.45 * delta && compatible(P[size_t(i)], N[size_t(i)], pts[k], nrm[k])) dup = true;
        });
        if (!dup)
        {
            H.add(int(P.size()), pts[k]);
            P.push_back(pts[k]);
            N.push_back(nrm[k]);
        }
    }
}

void sitesFrom(const std::vector<V3>& P, const std::vector<V3>& N, double s, std::vector<V3>& sp, std::vector<V3>& sn)
{
    std::vector<int> order(P.size());
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(1);
    std::shuffle(order.begin(), order.end(), rng);
    PointHash H(s);
    for (int i : order)
    {
        bool bad = false;
        H.near(P[size_t(i)], 1, [&](int j) {
            if (!bad && pdistIso(sp[size_t(j)], sn[size_t(j)], P[size_t(i)], N[size_t(i)], s) < 0.9 * s) bad = true;
        });
        if (!bad)
        {
            H.add(int(sp.size()), P[size_t(i)]);
            sp.push_back(P[size_t(i)]);
            sn.push_back(N[size_t(i)]);
        }
    }
}

// Each sample belongs to the nearest compatible site; a site goes to the middle of its samples, back onto the surface; a sample no site can have
// becomes a site (so nothing is left uncovered); a site with no sample goes
void lloyd(Surf& S, const std::vector<V3>& P, const std::vector<V3>& N, std::vector<V3>& sp, std::vector<V3>& sn, double s, int rounds)
{
    const double delta = s / deltaDivisor();
    for (int it = 0; it < rounds; ++it)
    {
        stageSet(double(it) / double(rounds), "round " + std::to_string(it + 1) + " of " + std::to_string(rounds));
        PointHash H(s);
        for (size_t j = 0; j < sp.size(); ++j) H.add(int(j), sp[j]);
        std::vector<int> owner(P.size(), -1);
        std::vector<double> away(P.size(), 0.0);
        for (size_t i = 0; i < P.size(); ++i)
        {
            double best = 1e9;
            int bj = -1;
            H.near(P[i], 2, [&](int j) {
                if ((sp[size_t(j)] - P[i]).norm() < 2.0 * s)
                {
                    const double d = pdistIso(sp[size_t(j)], sn[size_t(j)], P[i], N[i], s);
                    if (d < best)
                    {
                        best = d;
                        bj = j;
                    }
                }
            });
            owner[i] = bj;
            away[i] = best;
        }
        // a sample no site has, or one too far from its site: a new site (the farthest first)
        std::vector<int> loose;
        for (size_t i = 0; i < P.size(); ++i)
            if (owner[i] < 0 || away[i] > 0.85 * s) loose.push_back(int(i));
        std::stable_sort(loose.begin(), loose.end(), [&](int a, int b) {
            const double ka = owner[size_t(a)] >= 0 ? -away[size_t(a)] : -1e9, kb = owner[size_t(b)] >= 0 ? -away[size_t(b)] : -1e9;
            return ka < kb;
        });
        PointHash H2(s);
        for (size_t j = 0; j < sp.size(); ++j) H2.add(int(j), sp[j]);
        int added = 0;
        for (int i : loose)
        {
            bool clash = false;
            H2.near(P[size_t(i)], 1, [&](int j) {
                if (!clash && pdistIso(sp[size_t(j)], sn[size_t(j)], P[size_t(i)], N[size_t(i)], s) < 0.7 * s) clash = true;
            });
            if (!clash)
            {
                H2.add(int(sp.size()), P[size_t(i)]);
                sp.push_back(P[size_t(i)]);
                sn.push_back(N[size_t(i)]);
                ++added;
            }
        }
        if (added) continue;
        std::vector<V3> sum(sp.size(), V3::Zero());
        std::vector<int> cnt(sp.size(), 0);
        for (size_t i = 0; i < P.size(); ++i)
            if (owner[i] >= 0)
            {
                sum[size_t(owner[i])] += P[i];
                ++cnt[size_t(owner[i])];
            }
        std::vector<int> keep;
        std::vector<V3> mid;
        for (size_t j = 0; j < sp.size(); ++j)
            if (cnt[j] > 0)
            {
                keep.push_back(int(j));
                mid.push_back(sum[j] / double(cnt[j]));
            }
        std::vector<V3> nrm;
        std::vector<char> ok;
        std::vector<V3> proj = mid;
        S.project(proj, nrm, ok, 5, 0.02 * delta);
        std::vector<V3> np, nn;
        double moved = 0;
        for (size_t k = 0; k < keep.size(); ++k)
        {
            const size_t j = size_t(keep[k]);
            if (ok[k])
            {
                np.push_back(proj[k]);
                nn.push_back(nrm[k]);
            }
            else
            {
                np.push_back(sp[j]);
                nn.push_back(sn[j]);
            }
            moved += (np.back() - sp[j]).norm();
        }
        moved /= double(std::max<size_t>(1, keep.size()));
        sp = std::move(np);
        sn = std::move(nn);
        if (moved < 0.01 * s) break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Sharp edges and corners: samples ON them
//
// A sample is the centre of a cube the surface passes through, put on the surface at the nearest point.  Next to a sharp edge it lands on one face or the other, up to a sample step
// from the edge, and nothing of the graph is ON the edge: a lattice line that is laid along the edge finds only nodes beside it, so the vertices, the sides between them and the
// cells round them are all a little off, and the outline of a part is ragged by a fraction of a cell.  At a corner (three faces) it is worse: a lattice cannot go round a cone point
// unless the point IS a lattice point.  So the samples near an edge or a corner are moved onto it, the way dual contouring finds its vertices: the planes of the neighbouring samples
// (a point and a normal each) are intersected in the least-squares sense; the directions in which the normals do not span anything (along an edge, everywhere on a flat face) are left
// out, so a sample on a flat face does not move, one by an edge goes to the edge line (the nearest point of it) and one by a corner goes to the corner.  The result is kept only if
// the field says it is ON the surface (a fillet, whose planes meet in the air, is not an edge).  Samples that land within half a step of each other are one node.
struct FeatureInfo
{
    std::vector<char> kind;     // for every sample: 0 on a face, 1 on a sharp edge, 2 at a corner
    std::vector<V3> dir;        // for an edge sample: the direction of the edge
};

void snapToFeatures(Surf& S, std::vector<V3>& P, std::vector<V3>& N, double delta, bool say, FeatureInfo& info)
{
    const size_t n = P.size();
    info.kind.assign(n, 0);
    info.dir.assign(n, V3::Zero());
    if (n < 8 || std::getenv("FIELDES_SC_NOSNAP")) return;
    const double rho = 1.5 * delta;             // the samples that tell what the surface is like here
    const double tau = 0.06;                    // a direction counts when it holds this share of the normals
    PointHash H(rho);
    for (size_t i = 0; i < n; ++i) H.add(int(i), P[i]);
    std::vector<V3> cand(n), candN(n), candDir(n);
    std::vector<char> kind(n, 0);
    std::vector<int> who;
    for (size_t i = 0; i < n; ++i)
    {
        std::vector<int> nbrs;
        H.near(P[i], 1, [&](int j) {
            if ((P[size_t(j)] - P[i]).norm() <= rho) nbrs.push_back(j);
        });
        if (nbrs.size() < 5) continue;
        Mat3 A = Mat3::Zero();
        V3 b = V3::Zero();
        for (int j : nbrs)
        {
            const V3 nj = N[size_t(j)].normalized();
            A += nj * nj.transpose();
            b += nj * nj.dot(P[size_t(j)] - P[i]);
        }
        Eigen::SelfAdjointEigenSolver<Mat3> es(A);
        const V3 ev = es.eigenvalues();         // ascending
        const double m = double(nbrs.size());
        int kept = 0;
        for (int k = 0; k < 3; ++k)
            if (ev[k] >= tau * m) ++kept;
        if (kept < 2) continue;
        V3 move = V3::Zero();
        for (int k = 0; k < 3; ++k)
            if (ev[k] >= tau * m)
            {
                const V3 vk = es.eigenvectors().col(k);
                move += (vk.dot(b) / ev[k]) * vk;
            }
        if (move.norm() > 0.8 * delta) continue;
        // the normal there: the middle of the faces' (two for an edge, three for a corner)
        const V3 c1 = N[i].normalized();
        V3 c2 = c1;
        double md = 2.0;
        for (int j : nbrs)
        {
            const V3 nj = N[size_t(j)].normalized();
            if (c1.dot(nj) < md)
            {
                md = c1.dot(nj);
                c2 = nj;
            }
        }
        // A wall, a rib or a gap thinner than the look radius has its two sheets in the neighbourhood: normals that point against the two faces (against either of them).  The planes of the
        // two sheets cannot be told apart from an edge's (the fit lands in the middle of the top of a rib, which IS on the surface, and passes the test below), and the nodes that come out
        // are no edge: such a sample stays where it is
        bool opposed = false;
        for (int j : nbrs)
        {
            const V3 nj = N[size_t(j)].normalized();
            if (nj.dot(c1) < -0.7 || nj.dot(c2) < -0.7) opposed = true;
        }
        if (opposed) continue;
        V3 nm = c1 + c2;
        if (kept == 3)
        {
            V3 c3 = c1;
            double bd = 2.0;
            for (int j : nbrs)
            {
                const V3 nj = N[size_t(j)].normalized();
                const double d = std::max(c1.dot(nj), c2.dot(nj));
                if (d < bd)
                {
                    bd = d;
                    c3 = nj;
                }
            }
            nm += c3;
        }
        cand[i] = P[i] + move;
        candN[i] = nm.norm() > 1e-6 ? V3(nm.normalized()) : c1;
        candDir[i] = kept == 2 ? V3(es.eigenvectors().col(0)) : V3(V3::Zero());
        kind[i] = kept == 3 ? 2 : 1;
        who.push_back(int(i));
    }
    // on the surface?  (a point that the field puts further from it than a thirtieth of a step is not on an edge: the planes met in the air)
    std::vector<V3> pts;
    for (int i : who) pts.push_back(cand[size_t(i)]);
    std::vector<double> fv;
    std::vector<V3> gv;
    if (!pts.empty()) S.valueAndGradient(pts, fv, gv);
    std::vector<char> snapped(n, 0);
    for (size_t k = 0; k < who.size(); ++k)
    {
        const double gn = gv[k].norm();
        if (!(gn > 1e-9) || !std::isfinite(fv[k]) || std::abs(fv[k]) / gn > 0.03 * delta) continue;
        snapped[size_t(who[k])] = 1;
    }
    // corners first, then edges, each kept unless another one is within half a step
    std::vector<int> order;
    for (int pass = 2; pass >= 1; --pass)
        for (int i : who)
            if (snapped[size_t(i)] && kind[size_t(i)] == pass) order.push_back(i);
    std::vector<char> drop(n, 0);
    PointHash HK(0.45 * delta);
    size_t merged = 0;
    for (int i : order)
    {
        bool near = false;
        HK.near(cand[size_t(i)], 1, [&](int j) {
            if (!near && (cand[size_t(j)] - cand[size_t(i)]).norm() < 0.45 * delta) near = true;
        });
        if (near)
        {
            drop[size_t(i)] = 1;
            ++merged;
            continue;
        }
        HK.add(i, cand[size_t(i)]);
    }
    std::vector<V3> P2, N2;
    FeatureInfo f2;
    size_t edges = 0, corners = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (drop[i]) continue;
        if (snapped[i])
        {
            P2.push_back(cand[i]);
            N2.push_back(candN[i]);
            f2.kind.push_back(kind[i]);
            f2.dir.push_back(candDir[i]);
            (kind[i] == 2 ? corners : edges) += 1;
        }
        else
        {
            P2.push_back(P[i]);
            N2.push_back(N[i]);
            f2.kind.push_back(0);
            f2.dir.push_back(V3::Zero());
        }
    }
    if (say) std::fprintf(stderr, "[features] %zu of %zu samples were by a sharp edge or a corner and are now ON it: %zu edge nodes, %zu corner nodes (%zu merged into their neighbours)\n", edges + corners + merged, n, edges, corners, merged);
    P.swap(P2);
    N.swap(N2);
    info = std::move(f2);
}

// ---------------------------------------------------------------------------------------------------------------------
// The surface graph, and ways over it with a frame carried along

struct Rec
{
    double d = 0;           // the length of the way
    Mat3 R = Mat3::Identity();  // takes vectors of the node's tangent plane into the source's
    V3 t = V3::Zero();      // the node's place in the source's plane
    int pk = -1;            // the node before it on the way
};

struct Ways
{
    std::vector<int> ids;       // the nodes reached, in the order they were settled
    std::vector<Rec> rec;
    mutable std::unordered_map<int, int> at;
    mutable bool built = false;

    const Rec* find(int node) const
    {
        if (!built)
        {
            for (size_t i = 0; i < ids.size(); ++i) at[ids[i]] = int(i);
            built = true;
        }
        const auto it = at.find(node);
        return it == at.end() ? nullptr : &rec[size_t(it->second)];
    }
};

class SGraph
{
public:
    static constexpr double kSmooth = 0.1;      // a hop whose middle is this near the surface (as a share of the hop) lies on it
    static constexpr double kStep = 0.6;        // the chord put onto the surface: no step longer than this share of the hop ...
    static constexpr double kTotal = 1.4;       // ... and the whole way no longer than this

    SGraph(Surf& s, double delta, double reach = 1.6, double facing = -0.3)
        : S(s), delta_(delta), reach_(reach * delta), facing_(facing), H(reach * delta) {}

    std::vector<V3> P, N;
    std::vector<std::vector<int>> adj;
    std::vector<char> edgeMark;                 // nodes that addBridges put ON a sharp edge (they are edge nodes like the samples that were moved onto one)
    // (for the developer: where the pairs of nodes that addBridges looks at drop out)
    std::array<int, 12> bridgeStats{};

    // put points on the graph, joined to the nodes there by the hops that hold; returns the first new node number
    int addAll(const std::vector<V3>& pts, const std::vector<V3>& nrm)
    {
        const int base = int(P.size());
        for (size_t k = 0; k < pts.size(); ++k)
        {
            H.add(int(P.size()), pts[k]);
            P.push_back(pts[k]);
            N.push_back(nrm[k]);
            adj.emplace_back();
        }
        edgeMark.resize(P.size(), 0);
        std::vector<std::pair<int, int>> cand;
        for (int i = base; i < int(P.size()); ++i)
            H.near(P[size_t(i)], 1, [&](int j) {
                if (j < i && (P[size_t(j)] - P[size_t(i)]).norm() <= reach_ && N[size_t(j)].dot(N[size_t(i)]) > facing_) cand.push_back({j, i});
            });
        const std::vector<char> good = connected(cand);
        for (size_t k = 0; k < cand.size(); ++k)
            if (good[k])
            {
                adj[size_t(cand[k].first)].push_back(cand[k].second);
                adj[size_t(cand[k].second)].push_back(cand[k].first);
            }
        return base;
    }

    // Does the surface really join the two ends of each hop?  Where the chord lies on the surface (the middle is within a tenth of the hop of it)
    // yes.  Otherwise (a bend, or a wall between) three points of the chord are put onto the surface: a real bend gives a connected curve of about
    // the hop's length, a wall gives a jump from one sheet to the other
    std::vector<char> connected(const std::vector<std::pair<int, int>>& pairs)
    {
        std::vector<char> res(pairs.size(), 1);
        if (pairs.empty()) return res;
        // (a big batch of hops is a stage of its own: the middle of every hop is read from the field, seven values to a hop)
        if (pairs.size() > 20000) stageBegin("joining the samples into a surface", 7.0 * double(pairs.size()));
        std::vector<V3> mids(pairs.size());
        std::vector<double> length(pairs.size());
        for (size_t k = 0; k < pairs.size(); ++k)
        {
            mids[k] = 0.5 * (P[size_t(pairs[k].first)] + P[size_t(pairs[k].second)]);
            length[k] = (P[size_t(pairs[k].first)] - P[size_t(pairs[k].second)]).norm();
        }
        std::vector<double> f;
        std::vector<V3> g;
        S.valueAndGradient(mids, f, g);
        std::vector<size_t> sus;
        for (size_t k = 0; k < pairs.size(); ++k)
        {
            const double gn = g[k].norm();
            if (!(gn >= 1e-9) || !std::isfinite(f[k]) || std::abs(f[k]) / gn > kSmooth * length[k]) sus.push_back(k);
        }
        if (sus.empty()) return res;
        std::vector<V3> pts;
        for (size_t k : sus)
            for (double t : {0.25, 0.5, 0.75})
                pts.push_back(P[size_t(pairs[k].first)] + t * (P[size_t(pairs[k].second)] - P[size_t(pairs[k].first)]));
        std::vector<V3> nrm;
        std::vector<char> ok;
        S.project(pts, nrm, ok, 4, 0.02 * delta_);
        for (size_t n = 0; n < sus.size(); ++n)
        {
            const size_t k = sus[n];
            V3 way[5] = {P[size_t(pairs[k].first)], pts[3 * n], pts[3 * n + 1], pts[3 * n + 2], P[size_t(pairs[k].second)]};
            bool good = ok[3 * n] && ok[3 * n + 1] && ok[3 * n + 2];
            if (good)
            {
                double mx = 0, sum = 0;
                for (int m = 0; m < 4; ++m)
                {
                    const double d = (way[m] - way[m + 1]).norm();
                    mx = std::max(mx, d);
                    sum += d;
                }
                good = mx <= kStep * length[k] && sum <= kTotal * length[k];
            }
            res[k] = good ? 1 : 0;
        }
        return res;
    }

    // THE SURFACE DOES NOT STOP WHERE THE HOPS STOP.  A hop is left out where the two ends face away from each other (a sharp edge: no sample sits ON
    // the edge, the chord from one face to the other cuts through the body) or where they are further apart than the reach (the samples left a void),
    // and then the graph has a slit or a void there although the surface goes on, and the regions on the two sides do not touch.  Two nodes that are
    // near each other (within `gap`) and have no common neighbour (and are not neighbours) get a node between them where the surface joins them:
    // across a void in the samples of one smooth sheet, at the middle of the two; across an edge, ON the edge, at the point where the tangent planes
    // of the two meet (near the middle of the two), with the bisector of the two normals as its normal, so that it faces about the same way as both
    // sides and the hops to both hold.  The point is put onto the surface, and kept only if it did not have to move far (else the surface does not run
    // between the two: a slot between two plates, a thin wall, whose two sheets face opposite ways, is left alone).  Every hop stays within the reach,
    // so every node of the graph has a neighbour all round it wherever the surface is.  Returns the number of nodes added
    int addBridges(double gap)
    {
        const int n0 = int(P.size());
        PointHash HB(gap);
        for (int i = 0; i < n0; ++i) HB.add(i, P[size_t(i)]);
        std::vector<int> mark(size_t(n0), -1);
        int stamp = 0;
        // are the two nodes neighbours, or do they have a neighbour in common?
        auto close = [&](int a, int b) {
            ++stamp;
            for (int u : adj[size_t(a)]) mark[size_t(u)] = stamp;
            if (mark[size_t(b)] == stamp) return true;
            for (int v : adj[size_t(b)])
                if (mark[size_t(v)] == stamp) return true;
            return false;
        };
        std::vector<V3> pts, nrm;
        std::vector<std::array<V3, 2>> sides;                               // the normals of the two nodes each point joins
        std::vector<char> isEdge;
        PointHash HC(0.5 * delta_);
        for (int i = 0; i < n0; ++i)
            HB.near(P[size_t(i)], 1, [&](int j) {
                if (j <= i) return;
                const V3 pi = P[size_t(i)], pj = P[size_t(j)];
                const double d = (pj - pi).norm();
                if (d > gap) return;
                ++bridgeStats[0];
                const V3 ni = N[size_t(i)].normalized(), nj = N[size_t(j)].normalized();
                const double dp = ni.dot(nj);
                if (dp <= -0.97)
                {
                    ++bridgeStats[1];
                    return;
                }                                    // the two sheets of a wall
                if (dp > 0.5 && d <= reach_)
                {
                    ++bridgeStats[2];
                    return;
                }                        // one sheet, within reach: the hop was left out for a reason
                if (close(i, j))
                {
                    ++bridgeStats[3];
                    return;
                }
                const V3 bis = ni + nj;
                if (bis.norm() < 1e-6) return;
                const V3 M = 0.5 * (pi + pj);
                V3 c = M;
                if (dp <= 0.5)
                {
                    const double r1 = ni.dot(pi - M), r2 = nj.dot(pj - M), det = 1.0 - dp * dp;
                    c = M + ((r1 - dp * r2) / det) * ni + ((r2 - dp * r1) / det) * nj;
                    if ((c - pi).norm() > reach_ || (c - pj).norm() > reach_)
                    {
                        ++bridgeStats[4];
                        return;
                    }
                }
                bool nearOne = false;
                HC.near(c, 1, [&](int k) {
                    if ((pts[size_t(k)] - c).norm() < 0.5 * delta_) nearOne = true;
                });
                HB.near(c, 1, [&](int k) {
                    if ((P[size_t(k)] - c).norm() < 0.25 * delta_) nearOne = true;
                });
                if (nearOne)
                {
                    ++bridgeStats[5];
                    return;
                }
                HC.add(int(pts.size()), c);
                pts.push_back(c);
                nrm.push_back(bis.normalized());
                sides.push_back({ni, nj});
                isEdge.push_back(dp <= 0.5);
                ++bridgeStats[dp <= 0.5 ? 7 : 6];
            });
        if (pts.empty()) return 0;
        const std::vector<V3> before = pts;
        std::vector<V3> pn;
        std::vector<char> ok;
        S.project(pts, pn, ok, 5, 0.02 * delta_);
        std::vector<V3> keep, keepN;
        std::vector<char> keepEdge;
        for (size_t k = 0; k < pts.size(); ++k)
        {
            if (!ok[k] || (pts[k] - before[k]).norm() > 0.4 * delta_)
            {
                ++bridgeStats[8];
                continue;
            }
            // the surface where the point landed must face the way the two nodes do: a point in the void of one sheet faces as both do; a point on an edge
            // faces between its two faces (its normal lies in the wedge the two normals span); a point on the wall of a thin rib between two nodes does
            // neither
            const V3 nk = pn[k].normalized();
            const double a = nk.dot(sides[k][0]), b = nk.dot(sides[k][1]);
            if (isEdge[k])
            {
                const double dpn = sides[k][0].dot(sides[k][1]), det = 1.0 - dpn * dpn;
                const double ca = (a - dpn * b) / det, cb = (b - dpn * a) / det;
                const V3 rest = nk - ca * sides[k][0] - cb * sides[k][1];
                if (ca < -0.15 || cb < -0.15 || rest.norm() > 0.35)
                {
                    ++bridgeStats[9];
                    continue;
                }
            }
            else if (std::min(a, b) < 0.7)
            {
                ++bridgeStats[9];
                continue;
            }
            ++bridgeStats[10];
            {
                keep.push_back(pts[k]);
                keepN.push_back(nrm[k]);
                keepEdge.push_back(isEdge[k]);
            }
        }
        if (keep.empty()) return 0;
        const int first = addAll(keep, keepN);
        for (size_t k = 0; k < keep.size(); ++k) edgeMark[size_t(first) + k] = keepEdge[k];
        return int(keep.size());
    }

    // The separate pieces of the graph: for every node the number of its piece
    std::vector<int> pieces(int& count) const
    {
        std::vector<int> comp(P.size(), -1);
        count = 0;
        for (size_t a = 0; a < P.size(); ++a)
        {
            if (comp[a] >= 0) continue;
            comp[a] = count;
            std::vector<int> stack = {int(a)};
            while (!stack.empty())
            {
                const int i = stack.back();
                stack.pop_back();
                for (int j : adj[size_t(i)])
                    if (comp[size_t(j)] < 0)
                    {
                        comp[size_t(j)] = count;
                        stack.push_back(j);
                    }
            }
            ++count;
        }
        return comp;
    }

    // A piece whose samples all lie at ONE place (within `pointTol`, the accuracy a sample is put on the surface with) is a POINT of the surface, a corner
    // where samples from several sides landed: it has no area, so there is nothing of it to put cells on
    bool isPoint(const std::vector<int>& comp, int count, std::vector<char>& point, double pointTol) const
    {
        std::vector<V3> lo(size_t(count), V3::Constant(1e300)), hi(size_t(count), V3::Constant(-1e300));
        for (size_t k = 0; k < P.size(); ++k)
        {
            lo[size_t(comp[k])] = lo[size_t(comp[k])].cwiseMin(P[k]);
            hi[size_t(comp[k])] = hi[size_t(comp[k])].cwiseMax(P[k]);
        }
        point.assign(size_t(count), 0);
        bool any = false;
        for (int c = 0; c < count; ++c)
            if ((hi[size_t(c)] - lo[size_t(c)]).norm() <= pointTol)
            {
                point[size_t(c)] = 1;
                any = true;
            }
        return any;
    }


    // Shortest ways from src, up to `limit` long (or until every node of `targets` is reached); for each node reached: the length, the rotation
    // that takes its tangent plane into src's, and its place in src's plane
    Ways unfold(int src, double limit, const std::vector<int>* targets = nullptr)
    {
        if (stamp_.size() < P.size())
        {
            stamp_.resize(P.size(), 0);
            doneStamp_.resize(P.size(), 0);
            best_.resize(P.size(), 0.0);
        }
        ++cur_;
        Ways w;
        std::unordered_map<int, int> slot;
        std::unordered_set<int> want;
        if (targets)
            for (int t : *targets) want.insert(t);
        using Item = std::tuple<double, int, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
        stamp_[size_t(src)] = cur_;
        best_[size_t(src)] = 0.0;
        heap.push(Item(0.0, src, -1));
        while (!heap.empty())
        {
            const double d = std::get<0>(heap.top());
            const int k = std::get<1>(heap.top());
            const int pk = std::get<2>(heap.top());
            heap.pop();
            if (doneStamp_[size_t(k)] == cur_) continue;
            Rec r;
            r.d = d;
            r.pk = pk;
            if (pk >= 0)
            {
                const Rec& rp = w.rec[size_t(slot[pk])];
                const V3& npk = N[size_t(pk)];
                V3 v = P[size_t(k)] - P[size_t(pk)];
                v -= v.dot(npk) * npk;
                r.t = rp.t + rp.R * v;
                r.R = rp.R * leastRotation(N[size_t(k)], npk);
            }
            doneStamp_[size_t(k)] = cur_;
            slot[k] = int(w.ids.size());
            w.ids.push_back(k);
            w.rec.push_back(r);
            if (targets)
            {
                want.erase(k);
                if (want.empty()) break;
            }
            for (int j : adj[size_t(k)])
            {
                if (doneStamp_[size_t(j)] == cur_) continue;
                const double nd = d + (P[size_t(k)] - P[size_t(j)]).norm();
                if (nd <= limit && (stamp_[size_t(j)] != cur_ || nd < best_[size_t(j)]))
                {
                    stamp_[size_t(j)] = cur_;
                    best_[size_t(j)] = nd;
                    heap.push(Item(nd, j, k));
                }
            }
        }
        return w;
    }

    // The NODE of the surface reached from node src by going the vector v (in src's tangent plane) ALONG the surface, round any edge on the way: the
    // node whose unfolded place is nearest v.  A node of the graph is a point of the surface by construction; where v reaches beyond the surface's
    // own extent (a lattice point past an edge or a tip) it is the node nearest the end
    int walkNode(int src, const V3& v)
    {
        const double L = v.norm();
        const Ways w = unfold(src, 1.6 * L + 3.0 * reach_);
        double best = 1e18;
        int bk = src;
        for (size_t i = 0; i < w.ids.size(); ++i)
        {
            const double e = (w.rec[i].t - v).squaredNorm();
            if (e < best)
            {
                best = e;
                bk = w.ids[i];
            }
        }
        return bk;
    }

    // The point half way along the way from the source to node k (the way found by unfold)
    V3 middle(const Ways& w, int k) const
    {
        std::vector<int> way = {k};
        while (true)
        {
            const Rec* r = w.find(way.back());
            if (!r || r->pk < 0) break;
            way.push_back(r->pk);
        }
        std::reverse(way.begin(), way.end());
        std::vector<double> seg;
        double total = 0;
        for (size_t m = 0; m + 1 < way.size(); ++m)
        {
            seg.push_back((P[size_t(way[m])] - P[size_t(way[m + 1])]).norm());
            total += seg.back();
        }
        const double half = 0.5 * total;
        double acc = 0;
        for (size_t m = 0; m < seg.size(); ++m)
        {
            if (acc + seg[m] >= half)
            {
                const double u = seg[m] > 1e-12 ? (half - acc) / seg[m] : 0.0;
                return P[size_t(way[m])] + u * (P[size_t(way[m + 1])] - P[size_t(way[m])]);
            }
            acc += seg[m];
        }
        return P[size_t(k)];
    }

    Surf& S;

private:
    double delta_, reach_, facing_;
    PointHash H;
    std::vector<uint32_t> stamp_, doneStamp_;
    std::vector<double> best_;
    uint32_t cur_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------------
// The field on the carriers

struct Nbr
{
    int j;
    Mat3 R;     // takes j's tangent plane into i's
    V3 t;       // the place of j in i's plane
};

struct Pair
{
    int j;
    Mat3 R;
    V3 t;
    V3 a, b;    // j's axes carried into i's plane and turned (by quarter turns) to agree with i's first axis
};

// For every carrier the carriers whose way over the surface is shorter than `limit`
std::vector<std::vector<Nbr>> neighboursOf(SGraph& G, int firstNode, int n, double limit)
{
    std::vector<std::vector<Nbr>> nbr(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const Ways w = G.unfold(firstNode + i, limit);
        for (size_t k = 0; k < w.ids.size(); ++k)
        {
            const int j = w.ids[k] - firstNode;
            if (j >= 0 && j < n && j != i) nbr[size_t(i)].push_back({j, w.rec[k].R, w.rec[k].t});
        }
    }
    return nbr;
}

// The natural directions of a smooth surface: where it curves more one way than the other (a cylinder, a torus, an ellipsoid, a fillet) the quads of a good map run ALONG the strongest
// curvature and across it -- round the cylinder and along its axis, round the tube of the torus and round the ring -- and the map then has no irregular vertex inside that stretch.  A solver that
// starts from one global direction and only smooths finds those directions only by luck (the same torus gave 98 to 139 irregular vertices, depending on the direction it was started from).  Here they
// are read from the field: the Hessian (the difference of the gradients 0.5 spacings either side, along the three axes) seen in the tangent plane and divided by the gradient is the shape operator;
// its eigenvector of the largest eigenvalue is the direction.  `weight` says how sure that is: the difference of the two principal curvatures times the lattice spacing, from 0.05 (nothing:
// a plane or a sphere, whose directions are free) to 0.3 (a surface that bends clearly within one lattice spacing).  Next to a sharp edge the gradient jumps and the Hessian means nothing: there
// the weight is 0 (the edge itself sets the direction)
void curvatureDirections(Surf& S, const std::vector<V3>& sp, const std::vector<V3>& sn, double eps, double s, std::vector<V3>& dir, std::vector<double>& weight)
{
    const size_t n = sp.size();
    dir.assign(n, V3::Zero());
    weight.assign(n, 0.0);
    if (n == 0) return;
    std::vector<V3> pts;
    pts.reserve(6 * n);
    for (size_t i = 0; i < n; ++i)
        for (int a = 0; a < 3; ++a)
        {
            V3 d = V3::Zero();
            d[a] = eps;
            pts.push_back(sp[i] + d);
            pts.push_back(sp[i] - d);
        }
    std::vector<double> f;
    std::vector<V3> g;
    S.valueAndGradient(pts, f, g);
    for (size_t i = 0; i < n; ++i)
    {
        const V3 nrm = sn[i].norm() > 1e-9 ? V3(sn[i].normalized()) : V3(0, 0, 1);
        double gn = 0;
        bool edge = false;
        for (int k = 0; k < 6; ++k)
        {
            const double l = g[6 * i + size_t(k)].norm();
            gn += l / 6.0;
            if (!(l > 1e-9) || g[6 * i + size_t(k)].dot(nrm) < 0.9 * l) edge = true;       // (the normal turns by more than 25 degrees within half a spacing: an edge is near)
        }
        if (edge || !(gn > 1e-9)) continue;
        Mat3 H;
        for (int a = 0; a < 3; ++a) H.col(a) = (g[6 * i + size_t(2 * a)] - g[6 * i + size_t(2 * a + 1)]) / (2.0 * eps);
        H = 0.5 * (H + H.transpose());
        V3 t1 = inPlane(V3(1, 0, 0), nrm);
        if (t1.squaredNorm() < 0.09) t1 = inPlane(V3(0, 1, 0), nrm);
        t1 = unit0(t1);
        const V3 t2 = nrm.cross(t1);
        const double a11 = t1.dot(H * t1) / gn, a12 = t1.dot(H * t2) / gn, a22 = t2.dot(H * t2) / gn;
        const double r = std::sqrt(0.25 * (a11 - a22) * (a11 - a22) + a12 * a12);
        const double th = 0.5 * std::atan2(2.0 * a12, a11 - a22);
        dir[i] = std::cos(th) * t1 + std::sin(th) * t2;
        weight[i] = std::max(0.0, std::min(1.0, (2.0 * r * s - 0.05) / 0.25));
    }
}

// The four-fold direction field: each carrier turns towards the average of its neighbours' directions carried into its plane
std::vector<V3> orientationField(const std::vector<V3>& sn, const std::vector<std::vector<Nbr>>& nbr, const std::vector<char>& isFixed,
                                 const std::vector<V3>& fixedDir, const V3& axis, int sweeps, const std::vector<V3>* prior = nullptr, const std::vector<double>* priorW = nullptr)
{
    const size_t n = sn.size();
    std::vector<V3> q(n);
    for (size_t i = 0; i < n; ++i)
    {
        V3 v = inPlane(axis, sn[i]);
        if (v.squaredNorm() < 0.09) v = inPlane(V3(0, 0, 1), sn[i]);
        if (v.squaredNorm() < 0.09) v = inPlane(V3(0, 1, 0), sn[i]);
        q[i] = unit0(v);
    }
    for (size_t i = 0; i < n; ++i)
        if (isFixed[i]) q[i] = unit0(inPlane(fixedDir[i], sn[i]));
    const double priorStrength = envNumber("FIELDES_SC_PRIORS", 0.6);
    // (where the surface clearly curves more one way, the field starts along that)
    if (prior)
        for (size_t i = 0; i < n; ++i)
            if (!isFixed[i] && (*priorW)[i] > 0.3)
            {
                const V3 v = inPlane((*prior)[i], sn[i]);
                if (v.squaredNorm() > 1e-12) q[i] = unit0(v);
            }
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(3);
    std::shuffle(order.begin(), order.end(), rng);
    for (int sw = 0; sw < sweeps; ++sw)
    {
        double change = 0;
        for (int i : order)
        {
            if (isFixed[size_t(i)]) continue;
            V3 acc = V3::Zero();
            for (const Nbr& nb : nbr[size_t(i)])
            {
                const V3 qj = nb.R * q[size_t(nb.j)];
                const V3 tj = sn[size_t(i)].cross(qj);
                const V3 cand[4] = {qj, tj, -qj, -tj};
                V3 best = qj;
                double bd = -2.0;
                for (const V3& r : cand)
                {
                    const double d = r.dot(q[size_t(i)]);
                    if (d > bd)
                    {
                        bd = d;
                        best = r;
                    }
                }
                acc += (isFixed[size_t(nb.j)] ? 3.0 : 1.0) * best;
            }
            // the pull towards the natural direction of the surface (one of the four ways of a four-fold direction, the one nearest to where the field is), as strong as
            // a share of the neighbours' together, by how sure the direction is
            if (prior && (*priorW)[size_t(i)] > 0.0)
            {
                const V3 e = inPlane((*prior)[size_t(i)], sn[size_t(i)]);
                if (e.squaredNorm() > 1e-12)
                {
                    const V3 e0 = unit0(e), e1 = sn[size_t(i)].cross(e0);
                    const V3 cand[4] = {e0, e1, -e0, -e1};
                    V3 best = e0;
                    double bd = -2.0;
                    for (const V3& r : cand)
                    {
                        const double d = r.dot(q[size_t(i)]);
                        if (d > bd)
                        {
                            bd = d;
                            best = r;
                        }
                    }
                    acc += priorStrength * double(nbr[size_t(i)].size()) * (*priorW)[size_t(i)] * best;
                }
            }
            const V3 v = inPlane(acc, sn[size_t(i)]);
            if (v.squaredNorm() > 1e-12)
            {
                const V3 nq = unit0(v);
                change += 1.0 - std::abs(nq.dot(q[size_t(i)]));
                q[size_t(i)] = nq;
            }
        }
        if (change < 1e-7 * double(n)) break;
    }
    return q;
}

std::vector<std::vector<Pair>> preparePairs(const std::vector<V3>& sn, const std::vector<V3>& q, const std::vector<std::vector<Nbr>>& nbr)
{
    std::vector<std::vector<Pair>> out(sn.size());
    for (size_t i = 0; i < sn.size(); ++i)
        for (const Nbr& nb : nbr[i])
        {
            const V3 qj = nb.R * q[size_t(nb.j)];
            const V3 tj = sn[i].cross(qj);
            const V3 cand[4] = {qj, tj, -qj, -tj};
            V3 best = qj;
            double bd = -2.0;
            for (const V3& a : cand)
            {
                const double d = a.dot(q[i]);
                if (d > bd)
                {
                    bd = d;
                    best = a;
                }
            }
            out[i].push_back({nb.j, nb.R, nb.t, best, sn[i].cross(best)});
        }
    return out;
}

// dl[i] = (the lattice point nearest carrier i) - (carrier i), a vector in i's tangent plane.  Each sweep a carrier takes from every neighbour the
// lattice point that neighbour implies (the neighbour's, carried into our plane, plus the offset rounded to whole spacings) and snaps to the nearest
// lattice point of the lattice through their mean
// Where the lattice goes: for every carrier the offset (in its tangent plane) to the nearest lattice point.  A carrier by a sharp edge (`edgeAt` set: the place on the edge) has its
// direction along the edge and the lattice COORDINATE ACROSS the edge fixed so that the edge lies ON a lattice line: no cell then straddles the edge (a cell across an edge is
// bent round it, folded over when the edge is sharp), and the rows of cells on the two sides meet along it
// A carrier by a CORNER (`pinned`) has its lattice point AT the corner, `pinDl` away: a lattice cannot go round a cone point unless the point is one of its own, and with the corner a
// lattice point the lattices on its faces agree about where the lines across each edge go
std::vector<V3> positionField(const std::vector<V3>& sn, const std::vector<V3>& q, const std::vector<std::vector<Pair>>& pairs, double s, int sweeps,
                              const std::vector<char>& onEdge, const std::vector<double>& edgeOff, const std::vector<char>& pinned, const std::vector<V3>& pinDl)
{
    const size_t n = sn.size();
    std::vector<V3> dl(n, V3::Zero());
    const bool grow = envNumber("FIELDES_SC_POSGROW", 1.0) > 0.5;                         // the lattice is grown from a seed first
    const bool near = envNumber("FIELDES_SC_POSNEAR", 1.0) > 0.5;                         // close neighbours count more than far ones
    const double h = s / 2.5;
    // How much a neighbour's word counts: the way over the surface to it is carried in a flat plane, and the error of that grows with the distance (a surface that bends, a torus of
    // radius five spacings, is off by a fifth of a spacing at one carrier spacing and by half at two); so a neighbour at half a carrier spacing counts four times a neighbour at 1.3
    auto weight = [&](const Pair& p) { return near ? 1.0 / (0.25 + (p.t.norm() / h) * (p.t.norm() / h)) : 1.0; };
    // One carrier's lattice point from the neighbours whose flag is set: their points carried into its plane and moved by whole spacings to the one nearest the carrier's own, averaged,
    // and the lattice through that mean snapped to.  Returns false when no neighbour counted
    auto solve = [&](int i, const std::vector<char>* usable, V3& out) {
        if (pinned[size_t(i)])
        {
            out = pinDl[size_t(i)];
            return true;
        }
        V3 acc = V3::Zero();
        double wsum = 0;
        const V3 di = dl[size_t(i)];
        for (const Pair& p : pairs[size_t(i)])
        {
            if (usable && !(*usable)[size_t(p.j)]) continue;
            const V3 u = p.t + p.R * dl[size_t(p.j)];
            const V3 d = di - u;
            const double X = rnd(d.dot(p.a) / s), Y = rnd(d.dot(p.b) / s);
            const double w = weight(p);
            acc += w * (u + s * (X * p.a + Y * p.b));
            wsum += w;
        }
        if (wsum <= 0) return false;
        V3 v = acc / wsum;
        v -= v.dot(sn[size_t(i)]) * sn[size_t(i)];
        const V3 ti = sn[size_t(i)].cross(q[size_t(i)]);
        const double m = rnd(-v.dot(q[size_t(i)]) / s), k = rnd(-v.dot(ti) / s);
        V3 nw = v + s * (m * q[size_t(i)] + k * ti);
        if (onEdge[size_t(i)]) nw += (edgeOff[size_t(i)] - nw.dot(ti)) * ti;            // (the edge is a lattice line)
        out = nw;
        return true;
    };
    if (grow)
    {
        // the lattice grows from a seed: carriers by an edge first (their lattice line is known), then outwards over the neighbours, each taking its lattice from the ones already placed,
        // the nearest first.  Where the ways round a loop of carriers do not fit (a singularity, a handle) the lattices meet at a seam; the sweeps below move the seam to where it costs least
        std::vector<char> placed(n, 0), queued(n, 0);
        std::vector<int> queue;
        std::vector<int> starts;
        for (size_t i = 0; i < n; ++i)
            if (pinned[i]) starts.push_back(int(i));
        for (size_t i = 0; i < n; ++i)
            if (onEdge[i] && !pinned[i]) starts.push_back(int(i));
        for (size_t i = 0; i < n; ++i)
            if (!onEdge[i] && !pinned[i]) starts.push_back(int(i));
        for (int st : starts)
        {
            if (queued[size_t(st)]) continue;
            queue.assign(1, st);
            queued[size_t(st)] = 1;
            for (size_t head = 0; head < queue.size(); ++head)
            {
                const int i = queue[head];
                V3 nw;
                if (solve(i, &placed, nw)) dl[size_t(i)] = nw;
                placed[size_t(i)] = 1;
                std::vector<std::pair<double, int>> next;
                for (const Pair& p : pairs[size_t(i)])
                    if (!queued[size_t(p.j)])
                    {
                        queued[size_t(p.j)] = 1;
                        next.push_back({p.t.norm(), p.j});
                    }
                std::sort(next.begin(), next.end());
                for (const auto& nx : next) queue.push_back(nx.second);
            }
        }
    }
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(5);
    std::shuffle(order.begin(), order.end(), rng);
    for (int sw = 0; sw < sweeps; ++sw)
    {
        double moved = 0;
        for (int i : order)
        {
            V3 nw;
            if (!solve(i, nullptr, nw)) continue;
            moved += (nw - dl[size_t(i)]).norm();
            dl[size_t(i)] = nw;
        }
        if (moved < 1e-4 * s * double(n)) break;
    }
    return dl;
}

struct Vert
{
    V3 pos, n;          // where it is (a node of the surface graph) and the normal there
    int node = -1;      // the node
};

// Vertices: clusters of carriers that share a lattice point.
//
// A cluster is SEEDED by the carrier whose lattice point lies nearest to it (the most certain), and takes the carriers within a spacing and a bit of
// it whose lattice point, carried along the surface into the seed's plane, is within `tol` of a spacing of the seed's.  Nothing is merged by chains
// (a matches b, b matches c ...): where the phase of the lattice drifts, chains glue different lattice points together; a cluster here cannot be
// wider than the tolerance.  The vertex is the NODE of the surface graph that the seed's lattice point falls on, found by walking along the surface;
// so it is on the surface by construction, never a free point that has to be brought to it.  Two clusters that fall on the same node are one vertex.
// (How the vertices are JOINED is not read from the lattice field: see tripleFaces.  And where the surface is more complicated than the lattice can
// show, vertices are ADDED: see refineVertices.)
//
// CORNERS and EDGES.  A corner of the part (`forced`: nodes of the graph that are ON a corner) is always a vertex, put first; the vertices that would come within half a spacing of
// it give way to it.  A cluster whose seed carrier is by a sharp edge (`edgeCarrier`) has its lattice point ON the edge line; its vertex is the nearest node ON the edge
// (`edgeNode`) rather than the nearest node, which may be a node of a face beside the edge (the lattice line is straight, a rim that bends away from it is not)
void latticeVertices(SGraph& G, int cnode, const std::vector<V3>& sn, const std::vector<V3>& dl, double s, double tol, std::vector<Vert>& verts, const std::vector<int>& forced,
                     const std::vector<char>& edgeCarrier, const std::vector<char>& edgeNode)
{
    const int n = int(sn.size());
    std::vector<int> cluster(size_t(n), -1);
    std::vector<int> seeds;
    std::vector<int> byCertainty(static_cast<size_t>(n));
    std::iota(byCertainty.begin(), byCertainty.end(), 0);
    std::stable_sort(byCertainty.begin(), byCertainty.end(), [&](int a, int b) { return dl[size_t(a)].squaredNorm() < dl[size_t(b)].squaredNorm(); });
    for (int r : byCertainty)
    {
        if (cluster[size_t(r)] >= 0) continue;
        const int cid = int(seeds.size());
        seeds.push_back(r);
        cluster[size_t(r)] = cid;
        const V3 dr = dl[size_t(r)];
        const Ways w = G.unfold(cnode + r, 1.1 * s);
        for (size_t k = 0; k < w.ids.size(); ++k)
        {
            const int i = w.ids[k] - cnode;
            if (i < 0 || i >= n || cluster[size_t(i)] >= 0) continue;
            const V3 u = w.rec[k].t + w.rec[k].R * dl[size_t(i)] - dr;
            if (u.squaredNorm() < (tol * s) * (tol * s)) cluster[size_t(i)] = cid;
        }
    }
    verts.clear();
    std::vector<char> accepted(G.P.size(), 0);
    for (int f : forced)
    {
        bool close = false;
        const Ways w = G.unfold(f, 0.5 * s);
        for (int id : w.ids)
            if (accepted[size_t(id)])
            {
                close = true;
                break;
            }
        if (close) continue;
        accepted[size_t(f)] = 1;
        Vert v;
        v.node = f;
        v.pos = G.P[size_t(f)];
        v.n = G.N[size_t(f)];
        verts.push_back(v);
    }
    for (size_t c = 0; c < seeds.size(); ++c)
    {
        int node = G.walkNode(cnode + seeds[c], dl[size_t(seeds[c])]);
        if (edgeCarrier[size_t(seeds[c])] && !edgeNode[size_t(node)])
        {
            const V3 dr = dl[size_t(seeds[c])];
            const Ways w = G.unfold(cnode + seeds[c], dr.norm() + 0.6 * s);
            double best = 0.5 * s;
            for (size_t k = 0; k < w.ids.size(); ++k)
                if (size_t(w.ids[k]) < edgeNode.size() && edgeNode[size_t(w.ids[k])])
                {
                    const double e = (w.rec[k].t - dr).norm();
                    if (e < best)
                    {
                        best = e;
                        node = w.ids[k];
                    }
                }
        }
        // Two lattice points are not closer than half a spacing: where the field's noise has made one into two, only the more certain one is kept.
        // (The regions that the vertices divide the surface into have to be wider than the hop of the graph, or where regions meet cannot be read.)
        bool close = accepted[size_t(node)] != 0;
        if (!close)
        {
            const Ways w = G.unfold(node, 0.5 * s);
            for (int id : w.ids)
                if (accepted[size_t(id)])
                {
                    close = true;
                    break;
                }
        }
        if (close) continue;
        accepted[size_t(node)] = 1;
        Vert v;
        v.node = node;
        v.pos = G.P[size_t(node)];
        v.n = G.N[size_t(node)];
        verts.push_back(v);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The lattice as a map: neighbours in order round each vertex, faces traced

using EdgeSet = std::set<std::pair<int, int>>;

// Which vertex every node of the surface graph is nearest to, by the way over the surface (a wave of shortest ways from all the vertices at once):
// the surface divided into one connected REGION for every vertex.  label[k] is the vertex, dist[k] how far the way is
void partition(SGraph& G, const std::vector<int>& vnode, std::vector<int>& label, std::vector<double>& dist)
{
    const size_t N = G.P.size();
    dist.assign(N, 1e300);
    label.assign(N, -1);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    for (size_t v = 0; v < vnode.size(); ++v)
    {
        const size_t k = size_t(vnode[v]);
        dist[k] = 0.0;
        label[k] = int(v);
        heap.push(Item(0.0, int(k)));
    }
    while (!heap.empty())
    {
        const double d = heap.top().first;
        const size_t k = size_t(heap.top().second);
        heap.pop();
        if (d > dist[k]) continue;
        for (int j : G.adj[k])
        {
            const double nd = d + (G.P[k] - G.P[size_t(j)]).norm();
            if (nd < dist[size_t(j)])
            {
                dist[size_t(j)] = nd;
                label[size_t(j)] = label[k];
                heap.push(Item(nd, j));
            }
        }
    }
}

// The pieces the boundary of each region is made of (the nodes of the region that have a neighbour in another region, joined where they are
// neighbours).  A region that is a DISC has one; a region that goes round a hole or a handle has two or more; a region that is a whole piece of
// the surface has none
std::vector<int> boundaryPieces(SGraph& G, const std::vector<int>& label, size_t V)
{
    const size_t N = G.P.size();
    std::vector<int> par(N);
    std::iota(par.begin(), par.end(), 0);
    auto find = [&](int a) {
        while (par[size_t(a)] != a)
        {
            par[size_t(a)] = par[size_t(par[size_t(a)])];
            a = par[size_t(a)];
        }
        return a;
    };
    std::vector<char> onBoundary(N, 0);
    for (size_t k = 0; k < N; ++k)
        for (int j : G.adj[k])
            if (label[k] >= 0 && label[size_t(j)] >= 0 && label[k] != label[size_t(j)]) onBoundary[k] = 1;
    for (size_t k = 0; k < N; ++k)
        if (onBoundary[k])
            for (int j : G.adj[k])
                if (onBoundary[size_t(j)] && label[size_t(j)] == label[k]) par[size_t(find(int(k)))] = find(j);
    std::vector<std::set<int>> roots(V);
    for (size_t k = 0; k < N; ++k)
        if (onBoundary[k]) roots[size_t(label[k])].insert(find(int(k)));
    std::vector<int> pieces(V);
    for (size_t v = 0; v < V; ++v) pieces[v] = int(roots[v].size());
    return pieces;
}

// Pairs of regions that touch along MORE THAN ONE arc (round a thin handle, say): the neighbour graph has one edge between two regions, so it
// cannot show two contacts; the topology of the surface needs a vertex between them.  For each such pair returns a node of the smaller arc (the
// one farthest from its own vertex), where a vertex is wanted
std::vector<int> doubleContacts(SGraph& G, const std::vector<int>& label, const std::vector<double>& dist, const std::vector<char>& isVertex)
{
    const size_t N = G.P.size();
    std::map<std::pair<int, int>, std::vector<int>> contact;
    for (size_t k = 0; k < N; ++k)
        for (int j : G.adj[k])
            if (size_t(j) > k && label[k] >= 0 && label[size_t(j)] >= 0 && label[k] != label[size_t(j)])
            {
                std::vector<int>& c = contact[{std::min(label[k], label[size_t(j)]), std::max(label[k], label[size_t(j)])}];
                c.push_back(int(k));
                c.push_back(j);
            }
    std::vector<int> wanted;
    for (auto& pr : contact)
    {
        std::vector<int>& nodes = pr.second;
        std::sort(nodes.begin(), nodes.end());
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        if (nodes.size() < 4) continue;
        std::unordered_map<int, int> at;
        for (size_t i = 0; i < nodes.size(); ++i) at[nodes[i]] = int(i);
        std::vector<int> par(nodes.size());
        std::iota(par.begin(), par.end(), 0);
        auto find = [&](int a) {
            while (par[size_t(a)] != a)
            {
                par[size_t(a)] = par[size_t(par[size_t(a)])];
                a = par[size_t(a)];
            }
            return a;
        };
        for (size_t i = 0; i < nodes.size(); ++i)
            for (int j : G.adj[size_t(nodes[i])])
            {
                const auto it = at.find(j);
                if (it != at.end()) par[size_t(find(int(i)))] = find(it->second);
            }
        std::map<int, std::vector<int>> arcs;
        for (size_t i = 0; i < nodes.size(); ++i) arcs[find(int(i))].push_back(nodes[i]);
        if (arcs.size() < 2) continue;
        // the smaller arcs (all but the largest) each want a vertex
        size_t biggest = 0;
        int big = -1;
        for (const auto& a : arcs)
            if (a.second.size() > biggest)
            {
                biggest = a.second.size();
                big = a.first;
            }
        for (const auto& a : arcs)
        {
            if (a.first == big || a.second.size() < 2) continue;
            int best = -1;
            double bd = -1;
            for (int k : a.second)
                if (!isVertex[size_t(k)] && dist[size_t(k)] > bd)
                {
                    bd = dist[size_t(k)];
                    best = k;
                }
            if (best >= 0) wanted.push_back(best);
        }
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    return wanted;
}

// TOPOLOGY-DRIVEN REFINEMENT.  The lattice field decides where the vertices go (how the cells LOOK); it cannot know whether the surface is more
// complicated than a lattice of that spacing can show (a handle narrower than a cell, a thin tube, a small separate piece).  Where it is, a vertex
// whose region is not a disc (its boundary is not one piece) cannot have the cell structure of a disc round it, and two regions that touch along
// two separate arcs cannot be joined by one edge; the map would not have the topology of the surface.  So: every region that is not a disc gets
// another vertex, at the point of it farthest from its vertex, every second arc between two regions gets one, and the surface is divided again;
// and every separate piece of the surface gets at least four vertices (fewer cannot be the corners of a closed map of quads).  This repeats until
// every region is a disc and every pair touches once, which it must once the vertices are close enough.  Returns how many vertices were added
int refineVertices(SGraph& G, std::vector<int>& vnode, std::vector<Vert>& verts, const std::vector<int>& comp, int ncomp, const std::vector<char>& point, bool say)
{
    const size_t N = G.P.size();
    std::vector<int> size(size_t(ncomp), 0);
    std::vector<int> first(size_t(ncomp), -1);
    for (size_t k = 0; k < N; ++k)
    {
        ++size[size_t(comp[k])];
        if (first[size_t(comp[k])] < 0) first[size_t(comp[k])] = int(k);
    }
    // Every piece of the surface gets cells, however small: it gets at least four vertices (a piece too small for the sampling is sampled finer by the caller).  The one
    // exception is a POINT (a piece whose samples all lie at one place: a corner where samples from several sides landed): it has no area, so there is nothing of it
    // to put cells on
    std::vector<char> debris(point.begin(), point.end());
    int leftOut = 0;
    for (int c = 0; c < ncomp; ++c)
        if (debris[size_t(c)]) ++leftOut;
    {
        std::vector<int> keepNodes;
        std::vector<Vert> keepVerts;
        for (size_t i = 0; i < vnode.size(); ++i)
            if (!debris[size_t(comp[size_t(vnode[i])])])
            {
                keepNodes.push_back(vnode[i]);
                keepVerts.push_back(verts[i]);
            }
        vnode.swap(keepNodes);
        verts.swap(keepVerts);
    }
    std::vector<char> isVertex(N, 0);
    for (int n : vnode) isVertex[size_t(n)] = 1;
    auto addVertex = [&](int node) {
        isVertex[size_t(node)] = 1;
        vnode.push_back(node);
        Vert v;
        v.node = node;
        v.pos = G.P[size_t(node)];
        v.n = G.N[size_t(node)];
        verts.push_back(v);
    };
    int added = 0;
    for (int round = 0; round < 60; ++round)
    {
        // a piece of the surface with no vertex at all gets one
        std::vector<int> have(size_t(ncomp), 0);
        for (int n : vnode) ++have[size_t(comp[size_t(n)])];
        int changed = 0;
        for (int c = 0; c < ncomp; ++c)
            if (have[size_t(c)] == 0 && !debris[size_t(c)] && size[size_t(c)] >= 1 && !isVertex[size_t(first[size_t(c)])])
            {
                addVertex(first[size_t(c)]);
                ++added;
                ++changed;
            }
        if (changed) continue;
        std::vector<int> label;
        std::vector<double> dist;
        partition(G, vnode, label, dist);
        const size_t V = vnode.size();
        const std::vector<int> pieces = boundaryPieces(G, label, V);
        std::vector<int> farthest(V, -1);
        std::vector<double> farDist(V, -1.0);
        for (size_t k = 0; k < N; ++k)
            if (label[k] >= 0 && !isVertex[k] && dist[k] > farDist[size_t(label[k])])
            {
                farDist[size_t(label[k])] = dist[k];
                farthest[size_t(label[k])] = int(k);
            }
        std::vector<int> wanted;
        for (size_t v = 0; v < V; ++v)
            if ((pieces[v] != 1 || have[size_t(comp[size_t(vnode[v])])] < 4) && farthest[v] >= 0) wanted.push_back(farthest[v]);
        for (int k : doubleContacts(G, label, dist, isVertex)) wanted.push_back(k);
        std::sort(wanted.begin(), wanted.end());
        wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
        if (wanted.empty()) break;
        for (int k : wanted)
        {
            addVertex(k);
            ++added;
        }
    }
    if (say) std::fprintf(stderr, "[regions] %d vertices added so that every region is a disc, every pair of regions touches once and every piece of the surface has four; %d pieces given no vertex here (points: no area; or too few nodes for this cell size, sampled finer by the caller)\n", added, leftOut);
    return added;
}

// The middle of a face ON the surface: the mean of its corners' places, unfolded about a point; the point moves to the node of the surface nearest
// that mean; repeated until it stays.  Returns a node (never a lattice vertex)
int surfaceCentre(SGraph& G, const std::vector<int>& corners, double limit, const std::vector<char>& isVertexNode)
{
    int c = corners[0];
    for (int it = 0; it < 8; ++it)
    {
        double lim = limit;
        Ways w;
        while (true)
        {
            w = G.unfold(c, lim);
            bool all = true;
            for (int v : corners)
                if (!w.find(v)) all = false;
            if (all || lim > 12.0 * limit) break;
            lim *= 1.6;
        }
        V3 m = V3::Zero();
        int cnt = 0;
        for (int v : corners)
            if (const Rec* r = w.find(v))
            {
                m += r->t;
                ++cnt;
            }
        m /= double(std::max(1, cnt));
        double best = 1e18;
        int bk = c;
        for (size_t k = 0; k < w.ids.size(); ++k)
        {
            if (isVertexNode[size_t(w.ids[k])]) continue;
            const double e = (w.rec[k].t - m).squaredNorm();
            if (e < best)
            {
                best = e;
                bk = w.ids[k];
            }
        }
        if (bk == c) break;
        c = bk;
    }
    return c;
}

// How strongly every pair of regions touches: the hops of the surface graph that cross from one region to the other
std::map<std::pair<int, int>, int> contactCounts(SGraph& G, const std::vector<int>& vnode)
{
    std::vector<int> label;
    std::vector<double> dist;
    partition(G, vnode, label, dist);
    std::map<std::pair<int, int>, int> crossing;
    for (size_t k = 0; k < G.P.size(); ++k)
        for (int j : G.adj[k])
            if (size_t(j) > k && label[k] >= 0 && label[size_t(j)] >= 0 && label[k] != label[size_t(j)])
                ++crossing[{std::min(label[k], label[size_t(j)]), std::max(label[k], label[size_t(j)])}];
    return crossing;
}

// THE FACES OF THE LATTICE, read off the division into regions.  The regions are the cells of a Voronoi diagram on the surface, and the lattice is
// its DUAL: a vertex for every region, a TRIANGLE for every point where three regions meet.  Nothing here is a guess about directions, and nothing is
// clustered: which three regions meet is a plain fact about the labels of the nodes (a node whose own hop holds all three SEES them meet, and the
// more nodes see it the surer it is), and a set of triangles is a map of the surface when every edge is on two of them and the triangles round
// every vertex make ONE fan.  So the triangles are taken in the order of how many nodes see them meet, and a triangle is taken when it keeps both
// true (every edge on at most two triangles; a vertex whose triangles closed into a fan takes no more).  Where four regions meet at one point
// the four triples all have the same evidence and the constraint decides which two of the four there are (the diagonal that touches most first);
// nothing is decided by the angle of anything.  Triples that no node sees (the junction fell between the nodes) are taken last, from the regions
// that touch each other pairwise, under the same two rules, so they can only fill a hole, never take the place of a triple that was seen.
// Two triangles on an edge that touches less than the four edges round them are one QUAD (a square cell cut by its short diagonal); taking the edge
// away puts the two faces together and the topology does not change.
// The one thing read from the geometry is which way round a triangle goes, and that is not read triangle by triangle: the two faces on an edge run
// along it in opposite directions, which leaves two choices for every connected set of faces, and the choice is the one the signed areas of the
// triangles (in the chart of each, along the surface) agree with most.  What is not consistent -- an edge that is not on exactly two faces, faces
// that cannot all go round the same way, a vertex whose faces are not one fan -- is counted and reported; nothing is cut or capped to hide it.
struct Junctions
{
    std::vector<std::vector<int>> faces;        // the corners of every face, counterclockwise seen from outside
    std::vector<int> rep, evidence;             // for every face: a node where its regions meet, and how many nodes see all its regions at once (0: none does)
    std::vector<int> sizes;                     // how many faces have 3, 4, 5, ... corners (index = corners)
    size_t candidates = 0, candidatesSeen = 0, taken = 0, takenSeen = 0, merged = 0;
    size_t repairRounds = 0, holesBefore = 0, holesAfter = 0;
    size_t filled = 0;
    size_t constraints = 0, conflicts = 0, components = 0;
    size_t edges = 0, edgesOne = 0, edgesTwo = 0, edgesMore = 0, pinched = 0;
    std::vector<int> pinchedAt;                 // the vertices whose faces are not one fan
    std::vector<std::vector<int>> onBad;        // for every edge that is not on exactly two faces: its two ends, then the sizes of the faces on it
    std::vector<std::array<int, 6>> tried;      // every candidate triple in the order it was tried: its three regions, its evidence, its weakest touch, and what became of it (0 taken, 1 an edge was full, 2 the fan of a vertex would not hold)
};

// which way round every face goes: faces on a common edge run along it in opposite directions; each connected set of faces is turned as the sum of the
// signed areas of its faces says
void orientFaces(Junctions& out, const std::vector<double>& area)
{
    const size_t F = out.faces.size();
    std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> onEdge;     // the faces on an edge, and the way each runs along it (+1: from the lower end to the higher)
    for (size_t f = 0; f < F; ++f)
        for (size_t m = 0; m < out.faces[f].size(); ++m)
        {
            const int a = out.faces[f][m], b = out.faces[f][(m + 1) % out.faces[f].size()];
            onEdge[{std::min(a, b), std::max(a, b)}].push_back({int(f), a < b ? 1 : -1});
        }
    std::vector<std::vector<std::pair<int, int>>> relate(F);
    out.constraints = out.conflicts = out.components = 0;
    for (const auto& e : onEdge)
    {
        if (e.second.size() != 2) continue;
        const int f1 = e.second[0].first, f2 = e.second[1].first;
        if (f1 == f2) continue;
        const int rel = -(e.second[0].second * e.second[1].second);
        relate[size_t(f1)].push_back({f2, rel});
        relate[size_t(f2)].push_back({f1, rel});
        ++out.constraints;
    }
    std::vector<int> turn(F, 0), comp(F, -1);
    for (size_t f0 = 0; f0 < F; ++f0)
    {
        if (comp[f0] >= 0) continue;
        const int id = int(out.components++);
        std::vector<int> members, stack = {int(f0)};
        comp[f0] = id;
        turn[f0] = 1;
        while (!stack.empty())
        {
            const int f = stack.back();
            stack.pop_back();
            members.push_back(f);
            for (const auto& r : relate[size_t(f)])
            {
                const int want = turn[size_t(f)] * r.second;
                if (comp[size_t(r.first)] < 0)
                {
                    comp[size_t(r.first)] = id;
                    turn[size_t(r.first)] = want;
                    stack.push_back(r.first);
                }
                else if (turn[size_t(r.first)] != want && f < r.first)
                    ++out.conflicts;
            }
        }
        double vote = 0;
        for (int f : members) vote += turn[size_t(f)] * area[size_t(f)];
        if (vote < 0)
            for (int f : members) turn[size_t(f)] = -turn[size_t(f)];
    }
    for (size_t f = 0; f < F; ++f)
        if (turn[f] < 0) std::reverse(out.faces[f].begin(), out.faces[f].end());
}

// what the faces make: the faces on every edge, the faces by number of corners, the vertices whose faces are not one fan
void auditFaces(Junctions& out)
{
    const size_t F = out.faces.size();
    out.sizes.clear();
    out.edges = out.edgesOne = out.edgesTwo = out.edgesMore = out.pinched = 0;
    out.pinchedAt.clear();
    out.onBad.clear();
    std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> onEdge;
    for (size_t f = 0; f < F; ++f)
    {
        if (out.sizes.size() <= out.faces[f].size()) out.sizes.resize(out.faces[f].size() + 1, 0);
        ++out.sizes[out.faces[f].size()];
        for (size_t m = 0; m < out.faces[f].size(); ++m)
        {
            const int a = out.faces[f][m], b = out.faces[f][(m + 1) % out.faces[f].size()];
            onEdge[{std::min(a, b), std::max(a, b)}].push_back({int(f), a < b ? 1 : -1});
        }
    }
    std::set<int> badVertex;
    for (const auto& e : onEdge)
    {
        ++out.edges;
        if (e.second.size() == 2)
        {
            ++out.edgesTwo;
            continue;
        }
        (e.second.size() == 1 ? out.edgesOne : out.edgesMore) += 1;
        badVertex.insert(e.first.first);
        badVertex.insert(e.first.second);
        std::vector<int> row = {e.first.first, e.first.second};
        for (const auto& fd : e.second) row.push_back(int(out.faces[size_t(fd.first)].size()));
        out.onBad.push_back(row);
    }
    // the faces of a vertex must close up into ONE fan
    std::map<int, std::vector<int>> facesAt;
    for (size_t f = 0; f < F; ++f)
        for (int v : out.faces[f]) facesAt[v].push_back(int(f));
    for (const auto& fa : facesAt)
    {
        if (badVertex.count(fa.first)) continue;
        const std::vector<int>& fs = fa.second;
        std::vector<int> par(fs.size());
        std::iota(par.begin(), par.end(), 0);
        std::function<int(int)> find = [&](int x) {
            while (par[size_t(x)] != x)
            {
                par[size_t(x)] = par[size_t(par[size_t(x)])];
                x = par[size_t(x)];
            }
            return x;
        };
        auto at = [&](int f) { return int(std::lower_bound(fs.begin(), fs.end(), f) - fs.begin()); };
        for (int f : fs)
        {
            const std::vector<int>& c = out.faces[size_t(f)];
            for (size_t m = 0; m < c.size(); ++m)
            {
                if (c[m] != fa.first) continue;
                for (int other : {c[(m + 1) % c.size()], c[(m + c.size() - 1) % c.size()]})
                {
                    const auto& two = onEdge[{std::min(fa.first, other), std::max(fa.first, other)}];
                    for (const auto& fd : two)
                        if (fd.first != f) par[size_t(find(at(f)))] = find(at(fd.first));
                }
            }
        }
        std::set<int> roots;
        for (size_t i = 0; i < fs.size(); ++i) roots.insert(find(int(i)));
        if (roots.size() > 1)
        {
            ++out.pinched;
            out.pinchedAt.push_back(fa.first);
        }
    }
}

// A HOLE that is a plain cycle of edges, each on one face, is the boundary of ONE missing face: where k regions meet at one place the node evidence did not
// give a consistent set of triangles, but the faces round the place are all there, and what is missing is that polygon.  Its corners are the vertices of the
// cycle, in the order the cycle goes, and which way round is fixed by the faces the cycle touches (the new face runs along every edge the other way from
// the face already on it).  Only a hole that is a plain cycle of at most six corners (more regions than that do not meet at one place: such a ring may as
// well be the mouth of a thin tunnel), whose vertices all lie within one junction's reach of each other, is filled; anything else is a defect the map
// is refused for.  (A cycle that is the boundary of a lone triangle already taken is not filled: that would
// put the same triangle on both sides.)  Returns the number of faces added
size_t fillHoles(SGraph& G, const std::vector<int>& vnode, Junctions& out, double reach)
{
    std::map<std::pair<int, int>, int> count;                               // the faces on an edge
    std::map<std::pair<int, int>, int> directed;                            // the edges as the faces run along them
    for (const auto& f : out.faces)
        for (size_t m = 0; m < f.size(); ++m)
        {
            const int a = f[m], b = f[(m + 1) % f.size()];
            ++count[{std::min(a, b), std::max(a, b)}];
            directed[{a, b}] = 1;
        }
    std::map<int, std::vector<int>> next;                                   // the new face runs v -> u along the edge on which an old face runs u -> v
    for (const auto& e : directed)
        if (count[{std::min(e.first.first, e.first.second), std::max(e.first.first, e.first.second)}] == 1) next[e.first.second].push_back(e.first.first);
    std::set<std::array<int, 3>> triangles;
    for (const auto& f : out.faces)
        if (f.size() == 3)
        {
            std::array<int, 3> k = {f[0], f[1], f[2]};
            std::sort(k.begin(), k.end());
            triangles.insert(k);
        }
    std::set<int> visited;
    std::vector<std::vector<int>> added;
    for (const auto& start : next)
    {
        if (visited.count(start.first)) continue;
        std::vector<int> cycle = {start.first};
        bool plain = true, closed = false;
        int cur = start.first;
        while (plain && !closed)
        {
            visited.insert(cur);
            const auto it = next.find(cur);
            if (it == next.end() || it->second.size() != 1)
            {
                plain = false;
                break;
            }
            const int nx = it->second[0];
            if (nx == start.first)
            {
                closed = true;
                break;
            }
            if (std::find(cycle.begin(), cycle.end(), nx) != cycle.end() || cycle.size() >= 6)
            {
                plain = false;
                break;
            }
            cycle.push_back(nx);
            cur = nx;
        }
        if (!plain || !closed || cycle.size() < 3) continue;
        double extent = 0;
        for (int x : cycle)
            for (int y : cycle) extent = std::max(extent, (G.P[size_t(vnode[size_t(x)])] - G.P[size_t(vnode[size_t(y)])]).norm());
        if (extent > reach) continue;
        if (cycle.size() == 3)
        {
            std::array<int, 3> k = {cycle[0], cycle[1], cycle[2]};
            std::sort(k.begin(), k.end());
            if (triangles.count(k)) continue;
        }
        added.push_back(cycle);
    }
    for (const auto& c : added)
    {
        out.faces.push_back(c);
        out.rep.push_back(vnode[size_t(c[0])]);
        out.evidence.push_back(0);
    }
    out.filled += added.size();
    return added.size();
}

Junctions tripleFaces(SGraph& G, const std::vector<int>& vnode, const std::map<std::pair<int, int>, int>& contact, double limit)
{
    Junctions out;
    const size_t N = G.P.size();
    std::vector<int> label;
    std::vector<double> dist;
    partition(G, vnode, label, dist);
    auto touch = [&](int a, int b) {
        const auto it = contact.find({std::min(a, b), std::max(a, b)});
        return it == contact.end() ? 0 : it->second;
    };
    // three regions meet at a point only when each touches each of the others: a node that sees two regions on either side of a third one that is
    // thinner than its reach does not see them meet
    auto touching = [&](int a, int b, int c) { return touch(a, b) > 0 && touch(a, c) > 0 && touch(b, c) > 0; };
    // the triples: every node that sees three or more regions in its own hop sees every three of them meet
    struct Cand
    {
        int evidence = 0, rep = -1;
    };
    std::map<std::array<int, 3>, Cand> cand;
    // the triangulation of a POLYGON of k >= 4 regions that meet: round the polygon in the order in which the regions that are next to each other touch most
    // (every order of up to eight is tried), cut into triangles by the diagonals that touch most (the best triangulation of the polygon)
    std::map<std::vector<int>, std::vector<std::array<int, 3>>> polygons;
    auto triangulate = [&](const std::vector<int>& S) {
        const int k = int(S.size());
        std::vector<int> rest(S.begin() + 1, S.end()), order = S;
        long best = -1;
        do
        {
            long sum = touch(S[0], rest.front()) + touch(rest.back(), S[0]);
            for (size_t m = 0; m + 1 < rest.size(); ++m) sum += touch(rest[m], rest[m + 1]);
            if (sum > best)
            {
                best = sum;
                order.assign(1, S[0]);
                order.insert(order.end(), rest.begin(), rest.end());
            }
        } while (std::next_permutation(rest.begin(), rest.end()));
        std::vector<std::vector<long>> score(size_t(k), std::vector<long>(size_t(k), 0));
        std::vector<std::vector<int>> cut(size_t(k), std::vector<int>(size_t(k), -1));
        for (int len = 2; len < k; ++len)
            for (int i = 0; i + len < k; ++i)
            {
                const int j = i + len;
                long top = -1;
                for (int m = i + 1; m < j; ++m)
                {
                    const long v = score[size_t(i)][size_t(m)] + score[size_t(m)][size_t(j)] + (m - i > 1 ? touch(order[size_t(i)], order[size_t(m)]) : 0) +
                                   (j - m > 1 ? touch(order[size_t(m)], order[size_t(j)]) : 0);
                    if (v > top)
                    {
                        top = v;
                        cut[size_t(i)][size_t(j)] = m;
                    }
                }
                score[size_t(i)][size_t(j)] = top;
            }
        std::vector<std::array<int, 3>> tri;
        std::function<void(int, int)> emit = [&](int i, int j) {
            if (j - i < 2) return;
            const int m = cut[size_t(i)][size_t(j)];
            tri.push_back({order[size_t(i)], order[size_t(m)], order[size_t(j)]});
            emit(i, m);
            emit(m, j);
        };
        emit(0, k - 1);
        return tri;
    };
    for (size_t k = 0; k < N; ++k)
    {
        if (label[k] < 0) continue;
        std::vector<int> seen = {label[k]};
        for (int j : G.adj[k])
            if (label[size_t(j)] >= 0 && std::find(seen.begin(), seen.end(), label[size_t(j)]) == seen.end()) seen.push_back(label[size_t(j)]);
        if (seen.size() < 3) continue;
        std::sort(seen.begin(), seen.end());
        if (seen.size() >= 4 && seen.size() <= 8)
        {
            // Several regions meet here: a POLYGON, which is k - 2 triangles, never any three of its triples (four regions: a square, two triangles that
            // share the diagonal that touches more).  The order round the polygon is the one in which the regions that are next to each other touch most:
            // the sides touch along their whole length, a diagonal only at the corner
            auto it = polygons.find(seen);
            if (it == polygons.end()) it = polygons.emplace(seen, triangulate(seen)).first;
            for (std::array<int, 3> t : it->second)
            {
                std::sort(t.begin(), t.end());
                if (!touching(t[0], t[1], t[2])) continue;
                Cand& c = cand[t];
                ++c.evidence;
                if (c.rep < 0) c.rep = int(k);
            }
            continue;
        }
        for (size_t i = 0; i < seen.size(); ++i)
            for (size_t j = i + 1; j < seen.size(); ++j)
                for (size_t l = j + 1; l < seen.size(); ++l)
                {
                    if (!touching(seen[i], seen[j], seen[l])) continue;
                    Cand& c = cand[{seen[i], seen[j], seen[l]}];
                    ++c.evidence;
                    if (c.rep < 0) c.rep = int(k);
                }
    }
    out.candidatesSeen = cand.size();
    // the triples no node sees: three regions that touch each other pairwise
    std::map<int, std::vector<int>> next;
    for (const auto& c : contact)
    {
        next[c.first.first].push_back(c.first.second);
        next[c.first.second].push_back(c.first.first);
    }
    for (auto& nb : next) std::sort(nb.second.begin(), nb.second.end());
    for (const auto& nb : next)
        for (size_t i = 0; i < nb.second.size(); ++i)
            for (size_t j = i + 1; j < nb.second.size(); ++j)
            {
                const int a = nb.first, b = nb.second[i], c = nb.second[j];
                if (a < b && touch(b, c) > 0)
                {
                    Cand& cd = cand[{a, b, c}];
                    if (cd.rep < 0) cd.rep = vnode[size_t(a)];
                }
            }
    out.candidates = cand.size();
    // best first: the most nodes that see it, then the weakest of its three touches (the strongest first).  The evidence is what the nodes say; the one
    // thing that is certain is that the map must be CLOSED, so where the first choice leaves holes the triples that would fill them are moved up
    // (their priority goes up by one for every round they were turned away at a hole) and the choice is made again, until there is no hole or no round
    // has fewer holes than the best so far.  Nothing but the exact checks (below) decides what is taken, so a different choice is a different closed
    // set of triangles, never a wrong one
    struct Ranked
    {
        int evidence, weakest;
        std::array<int, 3> key;
        int rep;
    };
    std::vector<Ranked> ranked;
    for (const auto& c : cand)
    {
        const auto& t = c.first;
        ranked.push_back({c.second.evidence, std::min(touch(t[0], t[1]), std::min(touch(t[0], t[2]), touch(t[1], t[2]))), t, c.second.rep});
    }
    std::vector<int> boost(ranked.size(), 0);
    auto edgeKey = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
    struct Pick
    {
        std::vector<int> taken;                                             // the triples taken, in the order they were taken
        std::vector<std::array<int, 6>> tried;                              // every triple in the order tried: its three regions, evidence, weakest touch, what became of it
        std::vector<char> status;                                           // per triple: 0 taken, 1 an edge was full, 2 the fan of a vertex would not hold, 3 it would not go round the same way as the triangles it touches
        std::map<std::pair<int, int>, int> onEdge;                          // triangles taken on an edge
        size_t holes = 0;                                                   // edges on one triangle
    };
    auto select = [&]() {
        Pick pick;
        pick.status.assign(ranked.size(), 1);
        std::vector<int> order(ranked.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int x, int y) {
            const Ranked& rx = ranked[size_t(x)];
            const Ranked& ry = ranked[size_t(y)];
            if (rx.evidence + boost[size_t(x)] != ry.evidence + boost[size_t(y)]) return rx.evidence + boost[size_t(x)] > ry.evidence + boost[size_t(y)];
            if (rx.weakest != ry.weakest) return rx.weakest > ry.weakest;
            return rx.key < ry.key;
        });
        std::map<int, std::vector<std::pair<int, int>>> link;               // for every vertex, the edges of the link of its triangles
        // Which way round each triangle goes is not known yet, but two triangles on an edge must run along it in opposite directions, so the triangles
        // taken fall into connected sets whose relative orientations are all fixed; a triangle that would need two of them to go round the opposite
        // ways at once is not taken (the boundary of a solid is orientable).  `dsu` joins triangles with the parity of their relative turn
        std::vector<int> dsu(ranked.size());
        std::iota(dsu.begin(), dsu.end(), 0);
        std::vector<char> turn(ranked.size(), 0);                           // the parity of a triangle's turn against its parent's
        std::function<std::pair<int, int>(int)> root = [&](int x) -> std::pair<int, int> {
            if (dsu[size_t(x)] == x) return {x, 0};
            const auto up = root(dsu[size_t(x)]);
            turn[size_t(x)] = char(turn[size_t(x)] ^ up.second);
            dsu[size_t(x)] = up.first;
            return {up.first, int(turn[size_t(x)])};
        };
        std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> facesOn;    // the triangle (and the way it runs, +1 from the lower end to the higher) on an edge
        // can the edge x-y join the link of v?  (the link must stay paths, or a single closed cycle that is all of it)
        auto fits = [&](int v, int x, int y) {
            const auto it = link.find(v);
            if (it == link.end() || it->second.empty()) return true;
            const std::vector<std::pair<int, int>>& L = it->second;
            std::set<int> reached = {x};
            size_t edgesReached = 0;
            std::vector<int> stack = {x};
            std::vector<char> used(L.size(), 0);
            while (!stack.empty())
            {
                const int p = stack.back();
                stack.pop_back();
                for (size_t e = 0; e < L.size(); ++e)
                {
                    if (used[e] || (L[e].first != p && L[e].second != p)) continue;
                    used[e] = 1;
                    ++edgesReached;
                    const int q = L[e].first == p ? L[e].second : L[e].first;
                    if (reached.insert(q).second) stack.push_back(q);
                }
            }
            if (!reached.count(y)) return true;
            return edgesReached == L.size();                                // closes a cycle: only when nothing else of the link is left out
        };
        for (int i : order)
        {
            const Ranked& r = ranked[size_t(i)];
            const int a = r.key[0], b = r.key[1], c = r.key[2];
            if (pick.onEdge[edgeKey(a, b)] >= 2 || pick.onEdge[edgeKey(a, c)] >= 2 || pick.onEdge[edgeKey(b, c)] >= 2)
            {
                pick.tried.push_back({a, b, c, r.evidence, r.weakest, 1});
                continue;
            }
            if (!fits(a, b, c) || !fits(b, a, c) || !fits(c, a, b))
            {
                pick.status[size_t(i)] = 2;
                pick.tried.push_back({a, b, c, r.evidence, r.weakest, 2});
                continue;
            }
            // the triangle runs a -> b -> c, so +1 along a-b and b-c and -1 along a-c
            const std::pair<int, int> own[3] = {{a, b}, {b, c}, {a, c}};
            const int way[3] = {1, 1, -1};
            std::vector<std::array<int, 3>> ties;                           // (the triangle on the edge, the parity our turn must have against it)
            for (int m = 0; m < 3; ++m)
            {
                const auto it = facesOn.find(edgeKey(own[m].first, own[m].second));
                if (it == facesOn.end()) continue;
                for (const auto& f : it->second) ties.push_back({f.first, way[m] == f.second ? 1 : 0, 0});
            }
            bool twisted = false;
            for (size_t u = 0; u < ties.size() && !twisted; ++u)
            {
                const auto ru = root(ties[u][0]);
                ties[u][2] = ru.second ^ ties[u][1];                         // the parity of our turn against the root of that set
                for (size_t v = 0; v < u; ++v)
                    if (root(ties[v][0]).first == ru.first && ties[v][2] != ties[u][2]) twisted = true;
            }
            if (twisted)
            {
                pick.status[size_t(i)] = 3;
                pick.tried.push_back({a, b, c, r.evidence, r.weakest, 3});
                continue;
            }
            pick.status[size_t(i)] = 0;
            pick.tried.push_back({a, b, c, r.evidence, r.weakest, 0});
            for (const auto& t : ties)
            {
                const auto ro = root(t[0]);
                const auto rn = root(i);
                if (ro.first != rn.first)
                {
                    dsu[size_t(rn.first)] = ro.first;
                    turn[size_t(rn.first)] = char(rn.second ^ ro.second ^ t[1]);
                }
            }
            for (int m = 0; m < 3; ++m) facesOn[edgeKey(own[m].first, own[m].second)].push_back({i, way[m]});
            ++pick.onEdge[edgeKey(a, b)];
            ++pick.onEdge[edgeKey(a, c)];
            ++pick.onEdge[edgeKey(b, c)];
            link[a].push_back({b, c});
            link[b].push_back({a, c});
            link[c].push_back({a, b});
            pick.taken.push_back(i);
        }
        for (const auto& e : pick.onEdge) pick.holes += e.second == 1;
        return pick;
    };
    Pick best = select();
    const size_t firstHoles = best.holes;
    Pick current = best;
    int stalled = 0, rounds = 0;
    while (best.holes > 0 && rounds < 60 && stalled < 12)
    {
        ++rounds;
        for (size_t i = 0; i < ranked.size(); ++i)
        {
            if (current.status[i] == 0) continue;
            const auto& t = ranked[i].key;
            const auto h = [&](int x, int y) {
                const auto it = current.onEdge.find(edgeKey(x, y));
                return it != current.onEdge.end() && it->second == 1;
            };
            if (h(t[0], t[1]) || h(t[0], t[2]) || h(t[1], t[2])) ++boost[i];
        }
        current = select();
        if (current.holes < best.holes)
        {
            best = current;
            stalled = 0;
        }
        else
            ++stalled;
    }
    out.repairRounds = size_t(rounds);
    out.holesBefore = firstHoles;
    out.holesAfter = best.holes;
    out.tried = best.tried;
    std::vector<double> area;
    for (int i : best.taken)
    {
        const Ranked& r = ranked[size_t(i)];
        const int a = r.key[0], b = r.key[1], c = r.key[2];
        ++out.taken;
        out.takenSeen += r.evidence > 0;
        // the signed area of the triangle in the chart of its junction (counterclockwise about the normal is positive)
        const int rep = r.rep >= 0 ? r.rep : vnode[size_t(a)];
        const std::vector<int> targets = {vnode[size_t(a)], vnode[size_t(b)], vnode[size_t(c)]};
        const Ways w = G.unfold(rep, limit, &targets);
        const V3 n = G.N[size_t(rep)];
        V3 t[3];
        for (int m = 0; m < 3; ++m)
        {
            const Rec* rc = w.find(targets[size_t(m)]);
            t[m] = inPlane(rc ? rc->t : G.P[size_t(targets[size_t(m)])] - G.P[size_t(rep)], n);
        }
        area.push_back(0.5 * ((t[1] - t[0]).cross(t[2] - t[0])).dot(n));
        out.faces.push_back({a, b, c});
        out.rep.push_back(rep);
        out.evidence.push_back(r.evidence);
    }
    orientFaces(out, area);
    if (fillHoles(G, vnode, out, limit)) out.edges = 0;
    auditFaces(out);
    return out;
}

// Two triangles that share an edge are one QUAD when that edge touches less than every one of the four round them: a square cell, cut into two by
// a diagonal, has a diagonal that touches least.  Taking the edge away merges the two faces into one (two distinct faces glued along one edge are one
// face: the topology does not change), so this is only a choice of faces, never a risk to the map.  Each triangle is merged at most once; the weakest
// edge first
// NECKS.  A vertex whose faces are two or more fans that meet only at it is a place where the surface is narrower than the spacing of the lattice: a tube, whose two sides the lattice
// cannot tell from a point.  Splitting the vertex would cut the tube and cap both ends, which closes a handle that is really there; the right repair is a TUBE between the two fans.
// Take a face of one fan (v, p0 .. pm) and a face of the other (v, q0 .. qn), both running round v the same way, and replace them by (v, p0, qn), (v, q0, pm) and the polygon
// (p0 .. pm, q0 .. qn): every edge of the faces taken away is on a new face running along it the same way, the two new edges (pm, q0) and (qn, p0) are each on two new faces, the link
// of v is ONE cycle again, and the Euler number goes down by one (the pinched complex counted v once for two cones).  The pair of faces with the shortest new edges is taken, among
// those whose new edges are not already edges of the map and whose corners are all different.  Returns the number of necks closed
size_t bridgeNecks(Junctions& jf, const std::vector<Vert>& verts, size_t maxCorners)
{
    size_t closed = 0;
    for (int round = 0; round < 12; ++round)
    {
        auditFaces(jf);
        if (jf.pinchedAt.empty()) break;
        const std::vector<int> at = jf.pinchedAt;
        bool any = false;
        for (int v : at)
        {
            // the faces at v, and their fans (faces that share an edge at v)
            std::vector<int> fs;
            std::map<int, std::vector<int>> onEdge;                         // the other end of an edge at v -> the faces on it
            for (size_t f = 0; f < jf.faces.size(); ++f)
            {
                const std::vector<int>& c = jf.faces[f];
                for (size_t m = 0; m < c.size(); ++m)
                    if (c[m] == v)
                    {
                        fs.push_back(int(f));
                        onEdge[c[(m + 1) % c.size()]].push_back(int(f));
                        onEdge[c[(m + c.size() - 1) % c.size()]].push_back(int(f));
                    }
            }
            if (fs.size() < 2) continue;
            std::vector<int> par(fs.size());
            std::iota(par.begin(), par.end(), 0);
            std::function<int(int)> find = [&](int x) {
                while (par[size_t(x)] != x)
                {
                    par[size_t(x)] = par[size_t(par[size_t(x)])];
                    x = par[size_t(x)];
                }
                return x;
            };
            auto idx = [&](int f) { return int(std::find(fs.begin(), fs.end(), f) - fs.begin()); };
            for (const auto& e : onEdge)
                for (size_t i = 1; i < e.second.size(); ++i) par[size_t(find(idx(e.second[0])))] = find(idx(e.second[i]));
            std::map<int, std::vector<int>> fans;
            for (size_t i = 0; i < fs.size(); ++i) fans[find(int(i))].push_back(fs[i]);
            if (fans.size() < 2) continue;
            // the edges of the map now (undirected)
            std::set<std::pair<int, int>> edges;
            for (const auto& c : jf.faces)
                for (size_t m = 0; m < c.size(); ++m)
                {
                    const int a = c[m], b = c[(m + 1) % c.size()];
                    edges.insert({std::min(a, b), std::max(a, b)});
                }
            // the corners after v, round the face: p0 .. pm
            auto after = [&](int f) {
                const std::vector<int>& c = jf.faces[size_t(f)];
                size_t t = 0;
                while (c[t] != v) ++t;
                std::vector<int> p;
                for (size_t m = 1; m < c.size(); ++m) p.push_back(c[(t + m) % c.size()]);
                return p;
            };
            const auto first = fans.begin()->second;
            const auto second = std::next(fans.begin())->second;
            double bestCost = 1e300;
            int bestA = -1, bestB = -1;
            for (int fa : first)
                for (int fb : second)
                {
                    const std::vector<int> p = after(fa), q = after(fb);
                    if (p.size() + q.size() > maxCorners) continue;
                    std::set<int> all(p.begin(), p.end());
                    bool simple = true;
                    for (int x : q) simple = simple && all.insert(x).second;
                    if (!simple) continue;
                    const int pm = p.back(), q0 = q.front(), qn = q.back(), p0 = p.front();
                    if (edges.count({std::min(pm, q0), std::max(pm, q0)}) || edges.count({std::min(qn, p0), std::max(qn, p0)})) continue;
                    const double cost = (verts[size_t(pm)].pos - verts[size_t(q0)].pos).norm() + (verts[size_t(qn)].pos - verts[size_t(p0)].pos).norm();
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        bestA = fa;
                        bestB = fb;
                    }
                }
            if (bestA < 0) continue;
            const std::vector<int> p = after(bestA), q = after(bestB);
            jf.faces[size_t(bestA)] = {v, p.front(), q.back()};
            jf.faces[size_t(bestB)] = {v, q.front(), p.back()};
            std::vector<int> tube = p;
            tube.insert(tube.end(), q.begin(), q.end());
            jf.faces.push_back(tube);
            jf.rep.push_back(verts[size_t(tube[0])].node);
            jf.evidence.push_back(0);
            ++closed;
            any = true;
            break;                                                           // (the faces changed: the fans of the other vertices are looked at again)
        }
        if (!any) break;
    }
    auditFaces(jf);
    return closed;
}

// How far a quad of four vertices (in the order round it) is from a square: the largest deviation of a corner from 90 degrees, in degrees, seen along the mean normal (a corner that
// turns the wrong way, a folded or very flat quad: 180)
double squareness(const std::vector<Vert>& verts, const int q[4], double* aspect)
{
    V3 n = V3::Zero();
    for (int k = 0; k < 4; ++k) n += verts[size_t(q[k])].n;
    if (n.norm() < 1e-9) return 180.0;
    n.normalize();
    double worst = 0, lmin = 1e300, lmax = 0;
    int turns = 0;
    for (int k = 0; k < 4; ++k)
    {
        const V3& b = verts[size_t(q[k])].pos;
        const V3 nx = verts[size_t(q[(k + 1) % 4])].pos - b, pv = verts[size_t(q[(k + 3) % 4])].pos - b;
        const V3 a = inPlane(nx, n), c = inPlane(pv, n);
        const double la = a.norm(), lc = c.norm();
        if (la < 1e-12 || lc < 1e-12) return 180.0;
        const double turn = a.cross(c).dot(n);
        turns += turn > 0 ? 1 : -1;                                       // (a convex quad turns the same way at all four corners)
        const double ang = std::acos(std::max(-1.0, std::min(1.0, a.dot(c) / (la * lc)))) * 57.29577951308232;
        worst = std::max(worst, std::abs(ang - 90.0));
        lmin = std::min(lmin, la);
        lmax = std::max(lmax, la);
    }
    if (std::abs(turns) != 4) return 180.0;
    if (aspect) *aspect = lmax / std::max(lmin, 1e-12);
    return worst;
}

void mergeTriangles(Junctions& jf, const std::map<std::pair<int, int>, int>& contact, const std::vector<Vert>& verts)
{
    std::map<int, int> degree;                                              // the faces round every vertex
    for (const auto& f : jf.faces)
        for (int v : f) ++degree[v];
    auto touch = [&](int a, int b) {
        const auto it = contact.find({std::min(a, b), std::max(a, b)});
        return it == contact.end() ? 0 : it->second;
    };
    std::map<std::pair<int, int>, std::vector<int>> onEdge;
    for (size_t f = 0; f < jf.faces.size(); ++f)
        for (size_t m = 0; m < jf.faces[f].size(); ++m)
        {
            const int a = jf.faces[f][m], b = jf.faces[f][(m + 1) % jf.faces[f].size()];
            onEdge[{std::min(a, b), std::max(a, b)}].push_back(int(f));
        }
    std::vector<std::pair<int, std::pair<int, int>>> order;
    for (const auto& e : onEdge)
    {
        if (e.second.size() != 2 || e.second[0] == e.second[1]) continue;
        const std::vector<int>& f1 = jf.faces[size_t(e.second[0])];
        const std::vector<int>& f2 = jf.faces[size_t(e.second[1])];
        if (f1.size() != 3 || f2.size() != 3) continue;
        int x = -1, y = -1;
        for (int v : f1)
            if (v != e.first.first && v != e.first.second) x = v;
        for (int v : f2)
            if (v != e.first.first && v != e.first.second) y = v;
        if (x < 0 || y < 0 || x == y) continue;
        const int w = touch(e.first.first, e.first.second);
        const int sides = std::min(std::min(touch(e.first.first, x), touch(e.first.second, x)), std::min(touch(e.first.first, y), touch(e.first.second, y)));
        // a pair of triangles is a square cut by a diagonal when the contacts say so (the diagonal touches least) or when the four corners are those of a square, seen along the surface;
        // the best squares first.  A vertex must keep three faces (a quad may not leave a vertex with two)
        int ring[4] = {e.first.first, y, e.first.second, x};
        double aspect = 1.0;
        const double dev = squareness(verts, ring, &aspect);
        // (how square the pair has to be: level 0 is the first rule, 1 and 2 (FIELDES_SC_MERGE) take more pairs, level 2 also those at a vertex that has six or more faces round it, whose
        // faces come out skinny however the nodes are moved: one triangle fewer there takes a face from it)
        const char* mergeEnv = std::getenv("FIELDES_SC_MERGE");
        const int level = mergeEnv ? std::atoi(mergeEnv) : 2;
        const bool crowded = std::max(degree[e.first.first], degree[e.first.second]) >= 6;
        const double devLimit = level >= 3 ? 70.0 : level >= 2 && crowded ? 60.0 : level >= 1 ? 45.0 : 32.0;
        const double aspectLimit = level >= 3 ? 4.0 : level >= 2 && crowded ? 3.0 : level >= 1 ? 2.2 : 1.7;
        const bool geometric = dev <= devLimit && aspect <= aspectLimit;
        const bool keeps = degree[e.first.first] >= 4 && degree[e.first.second] >= 4;
        if (!keeps) continue;
        if (w < sides || geometric) order.push_back({int(std::lround(10.0 * dev)), e.first});
    }
    std::sort(order.begin(), order.end());
    std::vector<char> used(jf.faces.size(), 0);
    std::vector<std::vector<int>> faces;
    std::vector<int> rep, evidence;
    for (const auto& o : order)
    {
        const std::vector<int>& two = onEdge[o.second];
        if (used[size_t(two[0])] || used[size_t(two[1])]) continue;
        used[size_t(two[0])] = used[size_t(two[1])] = 1;
        const std::vector<int>& f1 = jf.faces[size_t(two[0])];
        const std::vector<int>& f2 = jf.faces[size_t(two[1])];
        // f1 runs p -> q along the edge and then to x; f2 runs q -> p and then to y: the quad is p, y, q, x
        int p = -1, q = -1, x = -1, y = -1;
        for (size_t m = 0; m < 3; ++m)
        {
            const int u = f1[m], v = f1[(m + 1) % 3];
            if (std::min(u, v) == o.second.first && std::max(u, v) == o.second.second)
            {
                p = u;
                q = v;
                x = f1[(m + 2) % 3];
            }
        }
        for (int v : f2)
            if (v != p && v != q) y = v;
        faces.push_back({p, y, q, x});
        rep.push_back(jf.rep[size_t(two[0])]);
        evidence.push_back(jf.evidence[size_t(two[0])] + jf.evidence[size_t(two[1])]);
        ++jf.merged;
    }
    for (size_t f = 0; f < jf.faces.size(); ++f)
        if (!used[f])
        {
            faces.push_back(jf.faces[f]);
            rep.push_back(jf.rep[f]);
            evidence.push_back(jf.evidence[f]);
        }
    jf.faces.swap(faces);
    jf.rep.swap(rep);
    jf.evidence.swap(evidence);
    auditFaces(jf);
}

// ---------------------------------------------------------------------------------------------------------------------
// The layout

// (for the developer, with FIELDES_SC_STATS: what the finished map looks like as a whole, and what is wrong with it where it is not a map of the
// surface.  Everything is printed in one go, so that one run answers every question about it: the Euler number of what was built (also when the
// layout is refused), pieces of the lattice graph against pieces of the surface, valences, the regions, the junctions, and for every edge that is
// not on exactly two faces what the two regions on it are like and where they are)
void diagnose(SGraph& G, Surf& S, const std::vector<int>& vnode, const Junctions& jf, const std::map<std::pair<int, int>, int>& contact, double s)
{
    const std::vector<std::vector<int>>& faces = jf.faces;
    const size_t V = vnode.size();
    const size_t N = G.P.size();
    EdgeSet edges;
    for (const auto& f : faces)
        for (size_t m = 0; m < f.size(); ++m) edges.insert({std::min(f[m], f[(m + 1) % f.size()]), std::max(f[m], f[(m + 1) % f.size()])});
    std::vector<int> degree(V, 0);
    for (const auto& e : edges)
    {
        ++degree[size_t(e.first)];
        ++degree[size_t(e.second)];
    }
    size_t withEdges = 0;
    for (size_t v = 0; v < V; ++v) withEdges += degree[v] > 0;
    // the pieces of the lattice graph
    std::vector<int> par(V);
    std::iota(par.begin(), par.end(), 0);
    std::function<int(int)> find = [&](int a) {
        while (par[size_t(a)] != a)
        {
            par[size_t(a)] = par[size_t(par[size_t(a)])];
            a = par[size_t(a)];
        }
        return a;
    };
    for (const auto& e : edges) par[size_t(find(e.first))] = find(e.second);
    std::set<int> graphPieces;
    for (size_t v = 0; v < V; ++v)
        if (degree[v] > 0) graphPieces.insert(find(int(v)));
    // the pieces of the surface
    std::vector<int> piece(N, -1);
    std::vector<size_t> pieceNodes;
    for (size_t a = 0; a < N; ++a)
    {
        if (piece[a] >= 0) continue;
        const int id = int(pieceNodes.size());
        pieceNodes.push_back(0);
        std::vector<int> stack = {int(a)};
        piece[a] = id;
        while (!stack.empty())
        {
            const int i = stack.back();
            stack.pop_back();
            ++pieceNodes[size_t(id)];
            for (int j : G.adj[size_t(i)])
                if (piece[size_t(j)] < 0)
                {
                    piece[size_t(j)] = id;
                    stack.push_back(j);
                }
        }
    }
    std::vector<size_t> pieceVertices(pieceNodes.size(), 0);
    for (int n : vnode) ++pieceVertices[size_t(piece[size_t(n)])];
    size_t piecesWithVertices = 0;
    for (size_t p = 0; p < pieceNodes.size(); ++p) piecesWithVertices += pieceVertices[p] > 0;
    const long chi = long(withEdges) - long(edges.size()) + long(faces.size());
    int val[7] = {0, 0, 0, 0, 0, 0, 0};
    for (size_t v = 0; v < V; ++v) ++val[std::min(6, degree[v])];
    std::fprintf(stderr, "[euler] traced map: chi %ld (V %zu, E %zu, F %zu); lattice graph in %zu pieces, surface in %zu pieces that hold vertices (%zu pieces in all); vertices with valence 0:%d 1:%d 2:%d 3:%d 4:%d 5:%d 6+:%d\n",
                 chi, withEdges, edges.size(), faces.size(), graphPieces.size(), piecesWithVertices, pieceNodes.size(), val[0], val[1], val[2], val[3], val[4], val[5], val[6]);
    // the regions
    std::vector<int> label;
    std::vector<double> dist;
    partition(G, vnode, label, dist);
    std::vector<int> regionNodes(V, 0);
    for (size_t k = 0; k < N; ++k)
        if (label[k] >= 0) ++regionNodes[size_t(label[k])];
    std::vector<int> sorted = regionNodes;
    std::sort(sorted.begin(), sorted.end());
    const std::vector<int> pieces = boundaryPieces(G, label, V);
    int bp[4] = {0, 0, 0, 0};
    for (size_t v = 0; v < V; ++v) ++bp[std::min(3, pieces[v])];
    int tiny = 0;
    for (int c : sorted) tiny += c < 10;
    std::fprintf(stderr, "[regionstats] nodes per region: min %d, 10%% %d, median %d, max %d; fewer than 10 nodes: %d of %zu; boundary pieces 0:%d 1:%d 2:%d 3+:%d\n",
                 sorted.empty() ? 0 : sorted.front(), sorted.empty() ? 0 : sorted[sorted.size() / 10], sorted.empty() ? 0 : sorted[sorted.size() / 2],
                 sorted.empty() ? 0 : sorted.back(), tiny, V, bp[0], bp[1], bp[2], bp[3]);
    // the Euler number of every piece of the lattice: its vertices, edges and faces, and the nodes of the piece of the surface it lies on
    {
        std::map<int, std::array<long, 3>> comp;
        std::map<int, size_t> compNodes;
        for (size_t v = 0; v < V; ++v)
            if (degree[v] > 0)
            {
                ++comp[find(int(v))][0];
                compNodes[find(int(v))] = pieceNodes[size_t(piece[size_t(vnode[v])])];
            }
        for (const auto& e : edges) ++comp[find(e.first)][1];
        for (const auto& f : faces) ++comp[find(f[0])][2];
        std::fprintf(stderr, "[components] pieces of the lattice (surface nodes, V, E, F, chi):");
        for (const auto& c : comp) std::fprintf(stderr, " (%zu, %ld, %ld, %ld, %ld)", compNodes[c.first], c.second[0], c.second[1], c.second[2], c.second[0] - c.second[1] + c.second[2]);
        std::fprintf(stderr, "\n");
    }
    // the pieces of the surface: how big each is, and where the defects are
    {
        std::vector<size_t> holesIn(pieceNodes.size(), 0), pinchedIn(pieceNodes.size(), 0);
        for (const auto& row : jf.onBad) ++holesIn[size_t(piece[size_t(vnode[size_t(row[0])])])];
        for (int v : jf.pinchedAt) ++pinchedIn[size_t(piece[size_t(vnode[size_t(v)])])];
        std::vector<size_t> order(pieceNodes.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return pieceNodes[x] > pieceNodes[y]; });
        std::fprintf(stderr, "[pieces] %zu pieces of the surface:", pieceNodes.size());
        for (size_t i = 0; i < order.size() && i < 14; ++i)
        {
            const size_t p = order[i];
            std::fprintf(stderr, " (%zu nodes, %zu vertices, %zu holes, %zu pinched)", pieceNodes[p], pieceVertices[p], holesIn[p], pinchedIn[p]);
        }
        std::fprintf(stderr, "\n");
    }
    // nodes on the border of the graph: the neighbours of a node surround it unless the graph has a hole or an edge there; a gap of more than 150 degrees
    // in the directions to its neighbours, in the tangent plane, makes it a border node
    std::vector<char> border(N, 0);
    for (size_t k = 0; k < N; ++k)
    {
        const V3 n = G.N[k];
        V3 ref = unit0(inPlane(V3(1, 0, 0), n));
        if (ref.squaredNorm() < 1e-12) ref = unit0(inPlane(V3(0, 1, 0), n));
        const V3 tt = n.cross(ref);
        std::vector<double> ang;
        for (int j : G.adj[k])
        {
            const V3 d = inPlane(G.P[size_t(j)] - G.P[k], n);
            if (d.squaredNorm() > 1e-18) ang.push_back(std::atan2(d.dot(tt), d.dot(ref)));
        }
        if (ang.size() < 3)
        {
            border[k] = 1;
            continue;
        }
        std::sort(ang.begin(), ang.end());
        double gap = ang.front() + 6.283185307179586 - ang.back();
        for (size_t m = 0; m + 1 < ang.size(); ++m) gap = std::max(gap, ang[m + 1] - ang[m]);
        border[k] = gap > 2.617993877991494;
    }
    size_t borderNodes = 0;
    for (size_t k = 0; k < N; ++k) borderNodes += border[k];
    // what is beyond a border node: the nearest node that is not joined to it, and why it is not (a hop is left out when the two face away from each
    // other, or when the surface does not run from one to the other), or that there is none
    size_t byFacing = 0, byConnection = 0, beyondReach = 0, nothing = 0, facingBins[4] = {0, 0, 0, 0}, why[7] = {0, 0, 0, 0, 0, 0, 0};
    {
        const double delta = s / 7.5;
        PointHash H(2.2 * delta);
        for (size_t k = 0; k < N; ++k) H.add(int(k), G.P[k]);
        for (size_t k = 0; k < N; ++k)
        {
            if (!border[k]) continue;
            int nearest = -1;
            double best = 1e300;
            H.near(G.P[k], 1, [&](int j) {
                if (j == int(k) || std::find(G.adj[k].begin(), G.adj[k].end(), j) != G.adj[k].end()) return;
                const double d = (G.P[size_t(j)] - G.P[k]).norm();
                if (d < 2.2 * delta && d < best)
                {
                    best = d;
                    nearest = j;
                }
            });
            if (nearest < 0)
                ++nothing;
            else if (best > 1.6 * delta)
                ++beyondReach;
            else if (G.N[size_t(nearest)].dot(G.N[k]) <= -0.3)
            {
                ++byFacing;
                const double dp = G.N[size_t(nearest)].dot(G.N[k]);
                ++facingBins[dp > -0.6 ? 0 : dp > -0.9 ? 1 : dp > -0.97 ? 2 : 3];
                // what the bridging made of this pair: the same steps as addBridges takes
                const V3 pi = G.P[k], pj = G.P[size_t(nearest)];
                const V3 ni = G.N[k].normalized(), nj = G.N[size_t(nearest)].normalized();
                const double dpn = ni.dot(nj);
                bool common = false;
                for (int u : G.adj[k])
                    if (std::find(G.adj[size_t(nearest)].begin(), G.adj[size_t(nearest)].end(), u) != G.adj[size_t(nearest)].end()) common = true;
                if (dpn <= -0.97)
                    ++why[0];
                else if (common)
                    ++why[1];
                else
                {
                    const V3 M = 0.5 * (pi + pj);
                    const double r1 = ni.dot(pi - M), r2 = nj.dot(pj - M), det = 1.0 - dpn * dpn;
                    const V3 c = M + ((r1 - dpn * r2) / det) * ni + ((r2 - dpn * r1) / det) * nj;
                    const double farthest = std::max((c - pi).norm(), (c - pj).norm()) / delta;
                    ++why[farthest <= 1.6 ? 6 : farthest <= 2.0 ? 2 : farthest <= 3.0 ? 3 : farthest <= 5.0 ? 4 : 5];
                }
            }
            else
                ++byConnection;
        }
    }
    std::fprintf(stderr, "[bridge-why] border nodes whose nearest unjoined node faces away: wall %zu, joined through a neighbour %zu; crease point farther than the reach by: up to 0.4 delta %zu, up to 1.4 delta %zu, up to 3.4 delta %zu, more %zu; crease point within reach (a candidate) %zu\n",
                 why[0], why[1], why[2], why[3], why[4], why[5], why[6]);
    std::fprintf(stderr, "[facing] the normals of a border node and the nearest node it faces away from: dot -0.3..-0.6: %zu, -0.6..-0.9: %zu, -0.9..-0.97: %zu, below -0.97: %zu\n", facingBins[0], facingBins[1], facingBins[2], facingBins[3]);
    std::fprintf(stderr, "[holes] %zu of %zu nodes have a gap of more than 150 degrees in the ring of their neighbours; the nearest node not joined to such a node: faces away %zu, hop not on the surface %zu, beyond reach %zu, none within 2.2 reaches %zu\n",
                 borderNodes, N, byFacing, byConnection, beyondReach, nothing);
    // the triangles, and the faces they make
    std::fprintf(stderr, "[triples] %zu candidates (%zu seen by nodes), %zu taken (%zu seen by nodes, %zu completed from touches), %zu pairs merged into quads; repair rounds %zu (edges on one triangle %zu before, %zu after); %zu plain holes filled with a face\n", jf.candidates, jf.candidatesSeen, jf.taken, jf.takenSeen, jf.taken - jf.takenSeen, jf.merged, jf.repairRounds, jf.holesBefore, jf.holesAfter, jf.filled);
    std::fprintf(stderr, "[junctions] %zu faces (corners", faces.size());
    for (size_t k = 3; k < jf.sizes.size(); ++k)
        if (jf.sizes[k]) std::fprintf(stderr, " %zu:%d", k, jf.sizes[k]);
    std::fprintf(stderr, "); %zu edges, on 1 face:%zu, 2 faces:%zu, 3+ faces:%zu; orientation: %zu sets of faces, %zu conflicts in %zu constraints; vertices whose faces are not one fan: %zu of %zu\n",
                 jf.edges, jf.edgesOne, jf.edgesTwo, jf.edgesMore, jf.components, jf.conflicts, jf.constraints, jf.pinched, withEdges);
    // the holes: the edges on one face, in connected pieces; a piece that is a plain cycle is the boundary of ONE missing face
    {
        std::map<int, std::vector<int>> hole;
        for (const auto& row : jf.onBad)
            if (row.size() == 3)
            {
                hole[row[0]].push_back(row[1]);
                hole[row[1]].push_back(row[0]);
            }
        std::map<std::array<int, 3>, int> statusOf;
        for (const auto& t : jf.tried)
        {
            std::array<int, 3> key = {t[0], t[1], t[2]};
            std::sort(key.begin(), key.end());
            statusOf[key] = t[5];
        }
        std::set<int> seenV;
        size_t cycles[8] = {0, 0, 0, 0, 0, 0, 0, 0}, others = 0;
        size_t three[5] = {0, 0, 0, 0, 0};                                  // the plain 3-cycles: no such candidate, an edge full, a fan, orientation, taken(?)
        for (const auto& hv : hole)
        {
            if (seenV.count(hv.first)) continue;
            std::vector<int> stack = {hv.first}, members;
            seenV.insert(hv.first);
            bool plain = true;
            while (!stack.empty())
            {
                const int v = stack.back();
                stack.pop_back();
                members.push_back(v);
                plain = plain && hole[v].size() == 2;
                for (int w : hole[v])
                    if (seenV.insert(w).second) stack.push_back(w);
            }
            if (!plain)
            {
                ++others;
                continue;
            }
            ++cycles[std::min<size_t>(members.size(), 7)];
            if (members.size() <= 5)
            {
                // where the cycle lies: how many nodes the piece of the surface has that holds it, and how big its regions are
                double extent = 0;
                for (int x : members)
                    for (int y : members) extent = std::max(extent, (G.P[size_t(vnode[size_t(x)])] - G.P[size_t(vnode[size_t(y)])]).norm());
                std::fprintf(stderr, "[loopinfo] a %zu-cycle: its piece of the surface has %zu nodes and %zu vertices; nodes in its regions", members.size(), pieceNodes[size_t(piece[size_t(vnode[size_t(members[0])])])],
                             pieceVertices[size_t(piece[size_t(vnode[size_t(members[0])])])]);
                for (int x : members) std::fprintf(stderr, " %d", regionNodes[size_t(x)]);
                std::fprintf(stderr, "; extent %.2f spacings\n", extent / s);
            }
            if (members.size() == 3)
            {
                std::array<int, 3> key = {members[0], members[1], members[2]};
                std::sort(key.begin(), key.end());
                const auto it = statusOf.find(key);
                ++three[it == statusOf.end() ? 0 : size_t(std::min(it->second, 3)) + 1];
            }
        }
        std::fprintf(stderr, "[loops] edges on one face lie in plain cycles of 3:%zu 4:%zu 5:%zu 6:%zu 7+:%zu and in %zu other pieces; the triple that would fill a 3-cycle: not a candidate %zu, rejected for a full edge %zu, for a fan %zu, for orientation %zu\n",
                     cycles[3], cycles[4], cycles[5], cycles[6], cycles[7], others, three[0], three[2], three[3], three[4]);
    }
    // the edges that are not on exactly two faces (up to six): what the two regions on them are like
    std::map<int, std::set<int>> next;
    for (const auto& c : contact)
    {
        next[c.first.first].insert(c.first.second);
        next[c.first.second].insert(c.first.first);
    }
    int shown = 0;
    for (const auto& row : jf.onBad)
    {
        if (shown >= 6) break;
        ++shown;
        const int a = row[0], b = row[1];
        const auto ct = contact.find({std::min(a, b), std::max(a, b)});
        // the pieces of the arc the two regions share: the nodes of a next to b, joined where they are neighbours
        std::vector<int> arc;
        for (size_t k = 0; k < N; ++k)
        {
            if (label[k] != a) continue;
            for (int j : G.adj[k])
                if (label[size_t(j)] == b)
                {
                    arc.push_back(int(k));
                    break;
                }
        }
        std::set<int> inArc(arc.begin(), arc.end()), seen;
        int arcs = 0;
        for (int k : arc)
        {
            if (seen.count(k)) continue;
            ++arcs;
            std::vector<int> stack = {k};
            seen.insert(k);
            while (!stack.empty())
            {
                const int i = stack.back();
                stack.pop_back();
                for (int j : G.adj[size_t(i)])
                    if (inArc.count(j) && !seen.count(j))
                    {
                        seen.insert(j);
                        stack.push_back(j);
                    }
            }
        }
        // the regions that touch both: the junctions (a, b, c) that should exist at the two ends of the arc, and what became of each
        int mutual = 0, nextTo = 0, apart = 0, nowhere = 0;
        for (int c : next[a])
            if (next[b].count(c))
            {
                ++mutual;
                bool any = false, adjacent = false;
                for (const auto& f : faces)
                {
                    int pa = -1, pb = -1, pc = -1;
                    for (size_t m = 0; m < f.size(); ++m)
                    {
                        if (f[m] == a) pa = int(m);
                        if (f[m] == b) pb = int(m);
                        if (f[m] == c) pc = int(m);
                    }
                    if (pa < 0 || pb < 0 || pc < 0) continue;
                    any = true;
                    const int d = std::abs(pa - pb);
                    if (d == 1 || d == int(f.size()) - 1) adjacent = true;
                }
                (adjacent ? nextTo : any ? apart : nowhere) += 1;
            }
        // how thick the body is there: from the vertex of a, along the inward normal, to where the field turns positive again
        const V3 p0 = G.P[size_t(vnode[size_t(a)])], n0 = G.N[size_t(vnode[size_t(a)])];
        std::vector<V3> along;
        for (int k = 1; k <= 30; ++k) along.push_back(p0 - n0 * (0.1 * s * k));
        std::vector<double> fv;
        std::vector<V3> gv;
        S.valueAndGradient(along, fv, gv);
        double thickness = -1;
        for (size_t k = 0; k < fv.size(); ++k)
            if (fv[k] > 0)
            {
                thickness = 0.1 * double(k + 1);
                break;
            }
        int borderA = 0, borderB = 0;
        for (size_t k = 0; k < N; ++k)
            if (border[k] && label[k] == a)
                ++borderA;
            else if (border[k] && label[k] == b)
                ++borderB;
        std::fprintf(stderr, "[badedge] border nodes in the two regions %d,%d; ", borderA, borderB);
        std::fprintf(stderr, "on %zu faces (corners", row.size() - 2);
        for (size_t m = 2; m < row.size(); ++m) std::fprintf(stderr, " %d", row[m]);
        std::fprintf(stderr, "), regions %d,%d: nodes %d/%d, touching hops %d in %d arcs, mutual neighbours %d (in a face with a, b next to each other %d, in a face with a, b apart %d, in no face %d), vertices %.2f spacings apart, thickness %s\n", a, b,
                     regionNodes[size_t(a)], regionNodes[size_t(b)], ct == contact.end() ? 0 : ct->second, arcs, mutual, nextTo, apart, nowhere, (p0 - G.P[size_t(vnode[size_t(b)])]).norm() / s,
                     thickness < 0 ? "more than 3 spacings" : (std::to_string(thickness).substr(0, 4) + " spacings").c_str());
        // every candidate triple that holds both ends of the edge: how many nodes see it, how weakly its three regions touch, whether it was taken, and if not why not
        if (shown <= 6)
            for (size_t m = 0; m < jf.tried.size(); ++m)
            {
                const auto& t = jf.tried[m];
                if ((t[0] != a && t[1] != a && t[2] != a) || (t[0] != b && t[1] != b && t[2] != b)) continue;
                std::fprintf(stderr, "[cand] edge %d-%d: triple (%d %d %d) tried %zu of %zu, seen by %d nodes, weakest touch %d: %s\n", a, b, t[0], t[1], t[2], m + 1, jf.tried.size(), t[3], t[4],
                             t[5] == 0 ? "TAKEN" : t[5] == 1 ? "rejected: an edge already on two triangles" : t[5] == 2 ? "rejected: the triangles round a vertex would not make one fan" : "rejected: it would not go round the same way as the triangles it touches");
            }
        // the junctions nearest the middle of a and b (up to five): which regions they have as corners, in which order, how strongly consecutive corners touch
        if (shown <= 3)
        {
            const V3 mid = 0.5 * (G.P[size_t(vnode[size_t(a)])] + G.P[size_t(vnode[size_t(b)])]);
            std::vector<std::pair<double, size_t>> byDist;
            for (size_t f = 0; f < faces.size(); ++f) byDist.push_back({(G.P[size_t(jf.rep[f])] - mid).norm(), f});
            std::sort(byDist.begin(), byDist.end());
            for (size_t k = 0; k < byDist.size() && k < 5; ++k)
            {
                const std::vector<int>& f = faces[byDist[k].second];
                std::fprintf(stderr, "[near] %.2f spacings from the middle: seen by %d nodes, corners", byDist[k].first / s, jf.evidence[byDist[k].second]);
                for (size_t m = 0; m < f.size(); ++m)
                {
                    const int x = f[m], y = f[(m + 1) % f.size()];
                    const auto cx = contact.find({std::min(x, y), std::max(x, y)});
                    std::fprintf(stderr, " %d%s-(%d)->", x, x == a ? "*a" : x == b ? "*b" : "", cx == contact.end() ? 0 : cx->second);
                }
                std::fprintf(stderr, "\n");
            }
        }
    }
    // (with FIELDES_SC_PICTURE=<path>: everything a picture needs, the nodes with the region each belongs to, the vertices, the faces, the bad edges)
    if (const char* base = std::getenv("FIELDES_SC_PICTURE"))
    {
        char name[700];
        std::snprintf(name, sizeof(name), "%s_%g.json", base, s);
        if (FILE* fp = std::fopen(name, "w"))
        {
            std::fprintf(fp, "{\"s\":%g,\"nodes\":[", s);
            for (size_t k = 0; k < N; ++k) std::fprintf(fp, "%s[%.3f,%.3f,%.3f,%d]", k ? "," : "", G.P[k][0], G.P[k][1], G.P[k][2], label[k]);
            std::fprintf(fp, "],\"verts\":[");
            for (size_t v = 0; v < V; ++v) std::fprintf(fp, "%s[%.3f,%.3f,%.3f]", v ? "," : "", G.P[size_t(vnode[v])][0], G.P[size_t(vnode[v])][1], G.P[size_t(vnode[v])][2]);
            std::fprintf(fp, "],\"faces\":[");
            for (size_t f = 0; f < faces.size(); ++f)
            {
                std::fprintf(fp, "%s[", f ? "," : "");
                for (size_t m = 0; m < faces[f].size(); ++m) std::fprintf(fp, "%s%d", m ? "," : "", faces[f][m]);
                std::fprintf(fp, "]");
            }
            std::fprintf(fp, "],\"reps\":[");
            for (size_t f = 0; f < faces.size(); ++f) std::fprintf(fp, "%s[%.3f,%.3f,%.3f]", f ? "," : "", G.P[size_t(jf.rep[f])][0], G.P[size_t(jf.rep[f])][1], G.P[size_t(jf.rep[f])][2]);
            std::fprintf(fp, "],\"bad\":[");
            for (size_t m = 0; m < jf.onBad.size(); ++m) std::fprintf(fp, "%s[%d,%d,%zu]", m ? "," : "", jf.onBad[m][0], jf.onBad[m][1], jf.onBad[m].size() - 2);
            // the surface round each of the first bad edges, unfolded flat along the surface from the vertex a (x axis towards b): nodes (x, y, region),
            // vertices (number, x, y), junctions (x, y, corners), and the faces' edges
            std::fprintf(fp, "],\"charts\":[");
            for (size_t m = 0; m < jf.onBad.size() && m < 12; ++m)
            {
                const int a = jf.onBad[m][0], b = jf.onBad[m][1];
                const Ways w = G.unfold(vnode[size_t(a)], 4.0 * s);
                const V3 n = G.N[size_t(vnode[size_t(a)])];
                const Rec* tb = w.find(vnode[size_t(b)]);
                V3 ref = unit0(inPlane(tb ? tb->t : G.P[size_t(vnode[size_t(b)])] - G.P[size_t(vnode[size_t(a)])], n));
                if (ref.squaredNorm() < 1e-12) ref = unit0(inPlane(V3(1, 0, 0), n));
                const V3 tt = n.cross(ref);
                std::fprintf(fp, "%s{\"a\":%d,\"b\":%d,\"found\":%d,\"nodes\":[", m ? "," : "", a, b, tb ? 1 : 0);
                for (size_t k = 0; k < w.ids.size(); ++k)
                {
                    // 1: a neighbour belongs to another region (the node is on the border), 2: the node sees three regions (it is at a junction)
                    const int me = label[size_t(w.ids[k])];
                    std::set<int> seen;
                    seen.insert(me);
                    for (int j : G.adj[size_t(w.ids[k])])
                        if (label[size_t(j)] >= 0) seen.insert(label[size_t(j)]);
                    const int flag = (seen.size() >= 2 ? 1 : 0) | (seen.size() >= 3 ? 2 : 0) | (border[size_t(w.ids[k])] ? 4 : 0);
                    std::fprintf(fp, "%s[%.3f,%.3f,%d,%d]", k ? "," : "", w.rec[k].t.dot(ref), w.rec[k].t.dot(tt), me, flag);
                }
                std::fprintf(fp, "],\"verts\":[");
                bool first = true;
                for (size_t v = 0; v < V; ++v)
                    if (const Rec* r = w.find(vnode[v]))
                    {
                        std::fprintf(fp, "%s[%zu,%.3f,%.3f]", first ? "" : ",", v, r->t.dot(ref), r->t.dot(tt));
                        first = false;
                    }
                std::fprintf(fp, "],\"reps\":[");
                first = true;
                for (size_t f = 0; f < faces.size(); ++f)
                    if (const Rec* r = w.find(jf.rep[f]))
                    {
                        std::fprintf(fp, "%s[%.3f,%.3f,%zu,%zu]", first ? "" : ",", r->t.dot(ref), r->t.dot(tt), faces[f].size(), f);
                        first = false;
                    }
                std::fprintf(fp, "]}");
            }
            std::fprintf(fp, "]}\n");
            std::fclose(fp);
        }
    }
}

// (for the developer, with FIELDES_SC_STATS: a fingerprint of a list of vectors, to see between two runs where they first differ)
unsigned long long fingerprint(const std::vector<V3>& v)
{
    unsigned long long h = 1469598103934665603ULL;
    for (const V3& p : v)
        for (int k = 0; k < 3; ++k)
        {
            unsigned long long bits;
            std::memcpy(&bits, &p[k], sizeof(bits));
            h = (h ^ bits) * 1099511628211ULL;
        }
    return h;
}

// ---------------------------------------------------------------------------------------------------------------------
// The layout, piece by piece.  EVERY piece of the surface gets a closed map of quads, or the whole map is refused and says which piece could not; nothing is left out

// A piece of the surface that did not come out as a closed map of quads: where it is, and why not
struct FailedPiece
{
    std::vector<V3> seeds;          // a share of the places of its nodes
    V3 lo = V3::Zero(), hi = V3::Zero();
    size_t nodes = 0;
    std::string why;
};

// The nodes and cells of the maps made so far
struct MapSet
{
    std::vector<GNode> nodes;
    std::vector<Cell> cells;
    std::vector<V3> covered, coveredN;      // the places (and normals) of the graph nodes of the pieces that are mapped: what a map covers
    std::vector<V3> edgePoints;             // the nodes of those that are ON a sharp edge or a corner (exact: the smoothing moves vertices that lie beside an edge onto them)
    size_t repairs = 0;                     // faces that were added to close holes the layout left (in the pieces that are mapped)
    std::vector<V3> repairAt;               // where (the middle of each such face)
};

// HOLES THE LAYOUT LEFT.  Where the regions of the surface do not give a consistent set of triangles (a wall or a neck about as thin as the lattice's own spacing), some edges end up on
// ONE face: a hole in the map.  A hole is a closed loop of such edges, and the map is closed by what it is: one face on that loop, running along every edge of it the way the face
// beside does not.  A loop that visits a vertex twice is cut into the simple loops it is made of.  A face of k corners is k quads, so any loop of three to `maxCorners` corners
// is one face; nothing is taken away and nothing else is changed, and a loop that is the boundary of a lone triangle already taken is not filled (that would put the same
// triangle on both sides).  The checks of the faces are made again, and the faces are kept only if fewer edges are open and none is on more than two faces.  Returns the number of
// faces added (they are the last ones in `jf.faces`); `where` gets the middle of each
size_t closeBoundaries(Junctions& jf, const std::vector<Vert>& verts, std::vector<V3>* where, size_t maxCorners)
{
    std::map<std::pair<int, int>, int> count;
    for (const auto& f : jf.faces)
        for (size_t m = 0; m < f.size(); ++m)
        {
            const int a = f[m], b = f[(m + 1) % f.size()];
            ++count[{std::min(a, b), std::max(a, b)}];
        }
    std::map<int, std::vector<int>> out;                                    // the new face runs b -> a along an edge on which the old face runs a -> b
    size_t boundary = 0;
    for (const auto& f : jf.faces)
        for (size_t m = 0; m < f.size(); ++m)
        {
            const int a = f[m], b = f[(m + 1) % f.size()];
            if (count[{std::min(a, b), std::max(a, b)}] == 1)
            {
                out[b].push_back(a);
                ++boundary;
            }
        }
    if (boundary == 0) return 0;
    // the loops: a walk along the open edges; where it comes back to a vertex it has been at, the loop between is cut out and the walk goes on
    std::vector<std::vector<int>> cycles;
    for (auto& kv : out)
    {
        const int start = kv.first;
        while (!out[start].empty())
        {
            std::vector<int> path = {start};
            std::map<int, int> at = {{start, 0}};
            bool broken = false;
            while (true)
            {
                const int cur = path.back();
                std::vector<int>& next = out[cur];
                if (next.empty())
                {
                    broken = path.size() > 1;                                // (a walk that cannot go on is not a loop: the open edges are not closed)
                    break;
                }
                const int to = next.back();
                next.pop_back();
                const auto it = at.find(to);
                if (it != at.end())
                {
                    cycles.emplace_back(path.begin() + it->second, path.end());
                    for (size_t i = size_t(it->second) + 1; i < path.size(); ++i) at.erase(path[i]);
                    path.resize(size_t(it->second) + 1);
                }
                else
                {
                    at[to] = int(path.size());
                    path.push_back(to);
                }
            }
            if (broken) break;
        }
    }
    if (std::getenv("FIELDES_SC_STATS"))
    {
        std::string sizes;
        for (const auto& c : cycles) sizes += (sizes.empty() ? "" : " ") + std::to_string(c.size());
        size_t left = 0;
        for (const auto& kv : out) left += kv.second.size();
        std::fprintf(stderr, "[repair-diag] %zu open edges, loops of %s corners, %zu open edges not in a loop\n", boundary, sizes.empty() ? "no" : sizes.c_str(), left);
    }
    std::vector<std::vector<int>> fill;
    for (const auto& c : cycles)
    {
        if (c.size() < 3 || c.size() > maxCorners) continue;
        // (a loop that is the boundary of a lone face is a piece that is that one face: a tiny closed surface of three or four lattice vertices.  The face on its other side, the same
        // corners the other way round, closes it: two faces back to back are a closed surface)
        fill.push_back(c);
    }
    if (fill.empty()) return 0;
    auditFaces(jf);
    const size_t openBefore = jf.edgesOne + jf.edgesMore;
    const size_t n0 = jf.faces.size();
    for (const auto& c : fill)
    {
        jf.faces.push_back(c);
        jf.rep.push_back(verts[size_t(c[0])].node);
        jf.evidence.push_back(0);
    }
    auditFaces(jf);
    if (jf.edgesMore > 0 || jf.edgesOne + jf.edgesMore >= openBefore)
    {
        jf.faces.resize(n0);
        jf.rep.resize(n0);
        jf.evidence.resize(n0);
        auditFaces(jf);
        return 0;
    }
    jf.filled += fill.size();
    if (where)
        for (const auto& c : fill)
        {
            V3 m = V3::Zero();
            for (int v : c) m += verts[size_t(v)].pos;
            where->push_back(m / double(c.size()));
        }
    return fill.size();
}

// Do the faces of one piece make ONE closed map: every edge on exactly two faces, the faces of a vertex one fan, all faces going round the same way, every vertex a
// corner, all the faces connected?  Returns "" when they do, else what is wrong
std::string checkPiece(const std::vector<std::vector<int>>& faces, const std::vector<int>& faceIds, const std::vector<int>& verts)
{
    if (faceIds.empty()) return "no face: the regions of its " + std::to_string(verts.size()) + " vertices do not meet three at a time";
    Junctions chk;
    for (int f : faceIds) chk.faces.push_back(faces[size_t(f)]);
    auditFaces(chk);
    if (chk.edgesOne || chk.edgesMore)
        return std::to_string(chk.edgesOne + chk.edgesMore) + " edges not on exactly two faces";
    if (chk.pinched) return std::to_string(chk.pinched) + " vertices whose faces are not one fan";
    Junctions turn = chk;
    orientFaces(turn, std::vector<double>(turn.faces.size(), 0.0));
    if (turn.conflicts) return std::to_string(turn.conflicts) + " faces that cannot go round the same way";
    std::unordered_map<int, int> parent;
    std::function<int(int)> find = [&](int x) {
        int& p = parent[x];
        if (p == 0) p = x + 1;
        int root = p - 1;
        if (root != x)
        {
            root = find(root);
            parent[x] = root + 1;
        }
        return root;
    };
    for (const auto& f : chk.faces)
        for (size_t m = 1; m < f.size(); ++m) parent[find(f[0])] = find(f[m]) + 1;
    std::set<int> roots, used;
    for (const auto& f : chk.faces)
        for (int v : f)
        {
            used.insert(v);
            roots.insert(find(v));
        }
    (void)verts;                                // (a vertex that is not a corner is no defect: a face of the map spans its region)
    if (roots.size() > 1) return "the faces make " + std::to_string(roots.size()) + " separate surfaces";
    return "";
}

// A face of six corners or more makes six or more cells meet at its middle, and cells that are long and thin there: the faces that come out of a closed hole or a bridged neck are of this kind
// (one face for a loop of up to sixteen corners).  Such a face is cut by CHORDS between its own corners into faces of four corners (and a last one of four or five): the chord that is cut
// is the one that is shortest against the three sides it cuts off (a square has a chord of 1.4 sides, a nearly flat run of corners a chord of 3), and only a chord that lies on the surface
// (its middle within a quarter of its length of it) and is not an edge already.  The faces then follow the same rule as all the others: k quads for k corners.  Where no chord is
// possible the face is left as it is.  Faces from firstRepair on are the ones the repairs made; the pieces of a face stay on the side of the list that it was on
size_t splitBigFaces(Junctions& jf, const std::vector<Vert>& verts, Surf& S, size_t& firstRepair)
{
    std::set<std::pair<int, int>> edges;
    for (const auto& f : jf.faces)
        for (size_t m = 0; m < f.size(); ++m)
        {
            const int a = f[m], b = f[(m + 1) % f.size()];
            edges.insert({std::min(a, b), std::max(a, b)});
        }
    size_t cut = 0;
    std::vector<std::vector<int>> faces[2];
    std::vector<int> rep[2], evidence[2];
    for (size_t f = 0; f < jf.faces.size(); ++f)
    {
        const int side = f >= firstRepair ? 1 : 0;
        std::vector<int> cur = jf.faces[f];
        std::vector<std::vector<int>> pieces;
        while (cur.size() >= 6)
        {
            const size_t k = cur.size();
            std::vector<V3> mids(k);
            std::vector<double> len(k);
            for (size_t i = 0; i < k; ++i)
            {
                const V3& pa = verts[size_t(cur[i])].pos;
                const V3& pb = verts[size_t(cur[(i + 3) % k])].pos;
                mids[i] = 0.5 * (pa + pb);
                len[i] = (pa - pb).norm();
            }
            std::vector<double> fv;
            std::vector<V3> gv;
            S.valueAndGradient(mids, fv, gv);
            int best = -1;
            double bestScore = 1e300;
            for (size_t i = 0; i < k; ++i)
            {
                const int a = cur[i], b = cur[(i + 3) % k];
                if (edges.count({std::min(a, b), std::max(a, b)})) continue;
                const double gn = gv[i].norm();
                if (!(gn > 1e-9) || !std::isfinite(fv[i]) || std::abs(fv[i]) / gn > 0.25 * len[i]) continue;
                double sides = 0;
                for (size_t m = 0; m < 3; ++m) sides += (verts[size_t(cur[(i + m) % k])].pos - verts[size_t(cur[(i + m + 1) % k])].pos).norm();
                const double score = len[i] / std::max(sides / 3.0, 1e-12);
                if (score < bestScore)
                {
                    bestScore = score;
                    best = int(i);
                }
            }
            if (best < 0) break;
            const size_t b0 = size_t(best);
            pieces.push_back({cur[b0], cur[(b0 + 1) % k], cur[(b0 + 2) % k], cur[(b0 + 3) % k]});
            edges.insert({std::min(cur[b0], cur[(b0 + 3) % k]), std::max(cur[b0], cur[(b0 + 3) % k])});
            std::vector<int> rest;
            for (size_t m = 3; m <= k; ++m) rest.push_back(cur[(b0 + m) % k]);           // from the far end of the chord round to its near end
            cur = rest;
            ++cut;
        }
        pieces.push_back(cur);
        for (const auto& pc : pieces)
        {
            faces[side].push_back(pc);
            rep[side].push_back(jf.rep[f]);
            evidence[side].push_back(jf.evidence[f]);
        }
    }
    if (cut == 0) return 0;
    jf.faces = faces[0];
    jf.rep = rep[0];
    jf.evidence = evidence[0];
    firstRepair = jf.faces.size();
    for (size_t i = 0; i < faces[1].size(); ++i)
    {
        jf.faces.push_back(faces[1][i]);
        jf.rep.push_back(rep[1][i]);
        jf.evidence.push_back(evidence[1][i]);
    }
    auditFaces(jf);
    return cut;
}

// The layout of the surface inside [loIn, hiIn] at one cell size: samples, graph, lattice, faces, quads.  Every piece of the surface that comes out as a closed map
// of quads is added to `out`; every piece that does not is listed in `failed`.  With `seeds`, only the pieces of the surface that pass within `seedTol` of a seed
// are laid out; if one of them proves to be joined to a place that is already mapped (`done`, within `doneTol`) that map covers it (`joined`), and if one reaches
// within `edge` of the sides of the region it is cut by the region (`clipped`) and nothing is made of it
void mapPass(Field& F, const V3& loIn, const V3& hiIn, double cell, const V3& axis, const std::vector<V3>* seeds, double seedTol, const std::vector<V3>* done, const std::vector<V3>* doneN, double doneTol,
             double edge, int shift, double sampleR, const std::vector<std::pair<int, int>>* cutIn, std::vector<std::pair<int, int>>* cutOut, MapSet& out, std::vector<FailedPiece>& failed, bool& joined,
             bool& clipped, bool& vanished, bool say)
{
    joined = false;
    clipped = false;
    vanished = false;
    const auto t0 = std::chrono::steady_clock::now();
    // (the bar: when a stage ends, the next one begins)
    auto nextStage = [&](const std::string& done) {
        static const char* const table[][2] = {
            {"samples", "finding sharp edges and corners"}, {"edges snapped", "choosing the cell centres"}, {"sites", "evening out the cell centres"},
            {"carriers", "joining the samples into a surface"}, {"graph", "finding the neighbours of the cell centres"},
            {"neighbours", "choosing the directions of the rows"}, {"directions", "placing the rows of cells"},
            {"lattice positions", "placing the corners of the cells"}, {"lattice graph", "building the faces"}, {"faces", "cutting the faces into cells"},
            {"quads", "checking the cells"}};
        if (seeds) return;                                  // (a piece being sampled finer: the stage of the whole stays)
        for (const auto& row : table)
            if (done == row[0]) stageBegin(row[1]);
    };
    auto lap = [&](const char* what) {
        nextStage(what);
        if (say)
            std::fprintf(stderr, "[quads] %s: %.2f s (%.2f million field evaluations so far)\n", what, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), 1e-6 * double(F.evaluated()));
    };
    if (!seeds) stageBegin("sampling the surface");
    const double s = 2.0 * cell;            // the lattice spacing: every lattice face becomes k quads, so the cells are `cell` wide
    const double h = s / 2.5;               // the carriers
    const double delta = h / deltaDivisor();           // the samples
    const V3 lo = loIn - V3::Constant(3.0 * delta), hi = hiIn + V3::Constant(3.0 * delta);
    Surf S(F, 0.02 * delta);

    // ---- samples and carriers
    std::vector<V3> P, N;
    fineSamples(F, S, lo, hi, delta, say, shift, P, N, (seeds && sampleR > 0) ? seeds : nullptr, sampleR);
    lap("samples");
    // Nothing near the seeds at this finer sampling: either the surface is not there (the coarse samples were within the coarse placement tolerance of a field that comes near zero
    // but never crosses it) or it was missed.  The field is read on a grid round the seeds, a sixth of the reach apart: if it keeps ONE sign over it, there is no surface there
    if (seeds && sampleR > 0 && P.empty())
    {
        V3 slo = V3::Constant(1e300), shi = V3::Constant(-1e300);
        for (const V3& q : *seeds)
        {
            slo = slo.cwiseMin(q);
            shi = shi.cwiseMax(q);
        }
        slo -= V3::Constant(sampleR);
        shi += V3::Constant(sampleR);
        const double step = sampleR / 6.0;
        int cnt[3];
        for (int a = 0; a < 3; ++a) cnt[a] = std::min(48, int(std::ceil((shi[a] - slo[a]) / step)) + 1);
        std::vector<V3> gp;
        for (int i = 0; i < cnt[0]; ++i)
            for (int j = 0; j < cnt[1]; ++j)
                for (int k = 0; k < cnt[2]; ++k)
                    gp.push_back(slo + V3((shi[0] - slo[0]) * i / std::max(1, cnt[0] - 1), (shi[1] - slo[1]) * j / std::max(1, cnt[1] - 1), (shi[2] - slo[2]) * k / std::max(1, cnt[2] - 1)));
        std::vector<double> gf;
        std::vector<V3> gg;
        F.eval(gp, gf, gg);
        bool neg = false, pos = false, bad = false;
        for (double v : gf)
        {
            if (!std::isfinite(v)) bad = true;
            else if (v < 0) neg = true;
            else if (v > 0) pos = true;
            else neg = pos = true;
        }
        if (!bad && !(neg && pos))
        {
            vanished = true;
            return;
        }
    }
    // (a piece asked for that has too few samples here to be judged is NOT given up: it is reported, and the caller samples it finer)
    auto tooFew = [&](size_t n, const std::vector<V3>* pts) {
        if (!seeds) return;
        FailedPiece fp;
        fp.seeds = *seeds;
        fp.lo = loIn;
        fp.hi = hiIn;
        if (pts && !pts->empty())
        {
            // the piece itself, as these samples show it
            fp.seeds = *pts;
            fp.lo = V3::Constant(1e300);
            fp.hi = V3::Constant(-1e300);
            for (const V3& q : *pts)
            {
                fp.lo = fp.lo.cwiseMin(q);
                fp.hi = fp.hi.cwiseMax(q);
            }
        }
        fp.nodes = n;
        fp.why = "too few samples (" + std::to_string(n) + ") to be judged at this cell size";
        failed.push_back(fp);
    };
    if (P.size() < 8)
    {
        tooFew(P.size(), nullptr);
        return;
    }
    FeatureInfo feat;
    snapToFeatures(S, P, N, delta, say && !seeds, feat);
    lap("edges snapped");
    if (seeds)
    {
        // only the pieces of the surface that pass through the seeds (the rest of what lies in the region is mapped elsewhere)
        SGraph G0(S, delta);
        G0.addAll(P, N);
        for (int pass = 0; pass < 2; ++pass)
            if (!G0.addBridges(2.6 * delta)) break;
        int count = 0;
        const std::vector<int> comp = G0.pieces(count);
        PointHash HS(seedTol);
        for (size_t k = 0; k < seeds->size(); ++k) HS.add(int(k), (*seeds)[k]);
        std::vector<char> keep(size_t(count), 0);
        for (size_t i = 0; i < G0.P.size(); ++i)
            HS.near(G0.P[i], 1, [&](int k) {
                if ((G0.P[i] - (*seeds)[size_t(k)]).norm() <= seedTol) keep[size_t(comp[i])] = 1;
            });
        if (done)
        {
            PointHash HD(doneTol);
            for (size_t k = 0; k < done->size(); ++k) HD.add(int(k), (*done)[k]);
            for (size_t i = 0; i < G0.P.size() && !joined; ++i)
                if (keep[size_t(comp[i])])
                    HD.near(G0.P[i], 1, [&](int k) {
                        if ((G0.P[i] - (*done)[size_t(k)]).norm() <= doneTol && compatible((*done)[size_t(k)], (*doneN)[size_t(k)], G0.P[i], G0.N[i])) joined = true;
                    });
            if (joined) return;
        }
        std::vector<V3> P2, N2;
        FeatureInfo feat2;
        for (size_t i = 0; i < P.size(); ++i)
            if (keep[size_t(comp[i])])
            {
                P2.push_back(P[i]);
                N2.push_back(N[i]);
                feat2.kind.push_back(feat.kind[i]);
                feat2.dir.push_back(feat.dir[i]);
                for (int a = 0; a < 3; ++a)
                    if (P[i][a] < loIn[a] + edge || P[i][a] > hiIn[a] - edge) clipped = true;
            }
        if (clipped) return;
        P.swap(P2);
        N.swap(N2);
        feat = std::move(feat2);
        if (P.size() < 8)
        {
            tooFew(P.size(), &P);
            return;
        }
    }
    std::vector<V3> sp, sn;
    sitesFrom(P, N, h, sp, sn);
    lap("sites");
    lloyd(S, P, N, sp, sn, h, 12);
    const int nc = int(sp.size());
    if (say && !seeds) std::fprintf(stderr, "[quads] %zu samples, %d carriers\n", P.size(), nc);
    if (say && !seeds)
        std::fprintf(stderr, "[fp] samples %016llx normals %016llx carriers %016llx carrier normals %016llx\n", fingerprint(P), fingerprint(N), fingerprint(sp), fingerprint(sn));
    lap("carriers");

    // ---- the graph
    SGraph G(S, delta);
    G.addAll(P, N);
    {
        // the graph must go where the surface goes: across sharp edges and across voids between the samples (a node put in the middle of a void halves it, so twice)
        for (int pass = 0; pass < 2; ++pass)
        {
            const int added = G.addBridges(2.6 * delta);
            if (say && !seeds) std::fprintf(stderr, "[bridges] pass %d: %d nodes put on sharp edges and in voids (graph of %zu nodes)\n", pass + 1, added, G.P.size());
            if (!added) break;
        }
        if (say && !seeds)
        {
            std::fprintf(stderr, "[bridge-stats] pairs %d: wall %d, one sheet in reach %d, joined %d, edge point too far %d, duplicate %d; candidates: voids %d, edges %d; dropped after projection %d, by the normal test %d; kept %d\n",
                         G.bridgeStats[0], G.bridgeStats[1], G.bridgeStats[2], G.bridgeStats[3], G.bridgeStats[4], G.bridgeStats[5], G.bridgeStats[6], G.bridgeStats[7], G.bridgeStats[8],
                         G.bridgeStats[9], G.bridgeStats[10]);
        }
        P = G.P;
        N = G.N;
    }
    // sharp edges: where neighbouring samples face different ways, the surface has an edge, its direction the cross of the two normals
    std::vector<char> hasCrease(P.size(), 0);
    std::vector<V3> creaseDir(P.size(), V3::Zero());
    std::vector<int> creaseOrder;
    // (the samples that were moved onto an edge know its direction; the graph's first nodes are the samples, bridges come after them)
    for (size_t i = 0; i < feat.kind.size() && i < P.size(); ++i)
        if (feat.kind[i] == 1)
        {
            hasCrease[i] = 1;
            creaseDir[i] = feat.dir[i];
            creaseOrder.push_back(int(i));
        }
    for (size_t i = 0; i < P.size(); ++i)
        for (int j : G.adj[i])
        {
            if (N[i].dot(N[size_t(j)]) < 0.8)
            {
                const V3 t = unit0(N[i].cross(N[size_t(j)]));
                if (t.squaredNorm() > 0.5)
                {
                    if (!hasCrease[i])
                    {
                        hasCrease[i] = 1;
                        creaseDir[i] = t;
                        creaseOrder.push_back(int(i));
                    }
                    if (!hasCrease[size_t(j)])
                    {
                        hasCrease[size_t(j)] = 1;
                        creaseDir[size_t(j)] = t;
                        creaseOrder.push_back(j);
                    }
                }
            }
        }
    const int cn = G.addAll(sp, sn);
    if (cutIn)
        for (const auto& c : *cutIn)
        {
            auto& ea = G.adj[size_t(c.first)];
            auto& eb = G.adj[size_t(c.second)];
            ea.erase(std::remove(ea.begin(), ea.end(), c.second), ea.end());
            eb.erase(std::remove(eb.begin(), eb.end(), c.first), eb.end());
        }
    lap("graph");
    const auto nbr = neighboursOf(G, cn, nc, 1.9 * h);
    if (say && !seeds)
    {
        size_t hops = 0, pairsN = 0;
        std::vector<V3> ts;
        for (const auto& a : G.adj) hops += a.size();
        for (const auto& row : nbr)
            for (const Nbr& b : row)
            {
                ++pairsN;
                ts.push_back(b.t);
            }
        std::fprintf(stderr, "[fp] graph hops %zu, carrier pairs %zu, their places %016llx\n", hops, pairsN, fingerprint(ts));
    }
    lap("neighbours");

    // ---- the field
    std::vector<char> isFixed(size_t(nc), 0);
    std::vector<V3> fixedDir(size_t(nc), V3::Zero());
    std::vector<V3> edgePoint(size_t(nc), V3::Zero());                  // for a carrier by an edge: the nearest place on the edge
    std::vector<char> pinned(size_t(nc), 0);                            // for a carrier by a corner: its lattice point is the corner ...
    std::vector<V3> pinDl(size_t(nc), V3::Zero());                      // ... this far from it, in its plane
    std::vector<int> corners;                                           // the corners of the part (nodes of the graph that are ON a corner), always lattice vertices
    {
        PointHash H(h);
        for (int j = 0; j < nc; ++j) H.add(j, sp[size_t(j)]);
        for (int i : creaseOrder)
            H.near(P[size_t(i)], 1, [&](int j) {
                if (!isFixed[size_t(j)] && (sp[size_t(j)] - P[size_t(i)]).norm() < 0.6 * h)
                {
                    isFixed[size_t(j)] = 1;
                    fixedDir[size_t(j)] = creaseDir[size_t(i)];
                    edgePoint[size_t(j)] = P[size_t(i)];
                }
            });
        if (!std::getenv("FIELDES_SC_NOPIN"))
            for (size_t i = 0; i < feat.kind.size() && i < P.size(); ++i)
                if (feat.kind[i] == 2)
                {
                    corners.push_back(int(i));
                    // (a carrier on a face that meets at the corner has the corner in its plane; one across a thin wall has not.  Every carrier within a carrier spacing counts -- the corner is
                    // the nearest lattice point to all of them, a lattice point being at most half a lattice spacing from any carrier along a line -- and a corner always has one: if none is
                    // that near, the nearest on its faces)
                    int nearest = -1;
                    double nearestD = 1.4 * h;
                    int got = 0;
                    H.near(P[i], 1, [&](int j) {
                        const V3 d = P[i] - sp[size_t(j)];
                        if (std::abs(d.dot(sn[size_t(j)])) >= 0.15 * h) return;
                        if (d.norm() < nearestD)
                        {
                            nearestD = d.norm();
                            nearest = j;
                        }
                        if (!pinned[size_t(j)] && d.norm() < 1.0 * h)
                        {
                            pinned[size_t(j)] = 1;
                            pinDl[size_t(j)] = d - d.dot(sn[size_t(j)]) * sn[size_t(j)];
                            ++got;
                        }
                    });
                    if (!got && nearest >= 0 && !pinned[size_t(nearest)])
                    {
                        const V3 d = P[i] - sp[size_t(nearest)];
                        pinned[size_t(nearest)] = 1;
                        pinDl[size_t(nearest)] = d - d.dot(sn[size_t(nearest)]) * sn[size_t(nearest)];
                    }
                }
    }
    std::vector<V3> naturalDir;
    std::vector<double> naturalW;
    if (!std::getenv("FIELDES_SC_NOPRIOR")) curvatureDirections(S, sp, sn, 0.5 * delta, s, naturalDir, naturalW);
    if (say && !seeds && !naturalW.empty())
    {
        size_t strong = 0;
        for (double w : naturalW) strong += w > 0.3;
        std::fprintf(stderr, "[curvature] %zu of %d carriers have a clear natural direction (the surface bends one way more than the other)\n", strong, nc);
    }
    const std::vector<V3> q = std::getenv("FIELDES_SC_NOPRIOR") ? orientationField(sn, nbr, isFixed, fixedDir, axis, int(envNumber("FIELDES_SC_DIRSWEEPS", 400))) : orientationField(sn, nbr, isFixed, fixedDir, axis, int(envNumber("FIELDES_SC_DIRSWEEPS", 400)), &naturalDir, &naturalW);
    if (say && !seeds) std::fprintf(stderr, "[fp] directions %016llx\n", fingerprint(q));
    lap("directions");
    const auto pairs = preparePairs(sn, q, nbr);
    std::vector<double> edgeOff(size_t(nc), 0.0);                       // the offset across the edge that puts the lattice line on it, within half a spacing
    for (int j = 0; j < nc; ++j)
        if (isFixed[size_t(j)])
        {
            const V3 ti = sn[size_t(j)].cross(q[size_t(j)]);
            double o = (edgePoint[size_t(j)] - sp[size_t(j)]).dot(ti);
            edgeOff[size_t(j)] = o - s * rnd(o / s);
        }
    const std::vector<V3> dl = positionField(sn, q, pairs, s, int(envNumber("FIELDES_SC_POSSWEEPS", 200)), isFixed, edgeOff, pinned, pinDl);
    // (for the developer, with FIELDES_SC_STATS: how well the two fields are solved.  The direction field: for every carrier the mean angle between its direction and its neighbours', each
    // taken to the nearest of the four ways; a carrier above 25 degrees is next to a singularity or is a place where the field is not smooth.  The position field: for every pair of
    // neighbours the distance between the lattice points they imply, after the whole spacings are taken out; above a fifth of a spacing the two lattices do not agree there)
    if (say && !seeds)
    {
        size_t rough = 0, free = 0;
        double sumAngle = 0;
        for (int i = 0; i < nc; ++i)
        {
            if (isFixed[size_t(i)] || nbr[size_t(i)].empty()) continue;
            ++free;
            double acc = 0;
            for (const Nbr& nb : nbr[size_t(i)])
            {
                const V3 qj = nb.R * q[size_t(nb.j)];
                const V3 tj = sn[size_t(i)].cross(qj);
                const V3 cand[4] = {qj, tj, -qj, -tj};
                double bd = -2.0;
                for (const V3& r : cand) bd = std::max(bd, r.dot(q[size_t(i)]));
                acc += std::acos(std::max(-1.0, std::min(1.0, bd))) * 57.29577951308232;
            }
            acc /= double(nbr[size_t(i)].size());
            sumAngle += acc;
            if (acc > 25.0) ++rough;
        }
        size_t pairsAll = 0, pairsBad = 0, carriersBad = 0, pairsNear = 0, pairsNearBad = 0;
        for (int i = 0; i < nc; ++i)
        {
            double sum = 0;
            int cnt = 0;
            for (const Pair& p : pairs[size_t(i)])
            {
                const V3 u = p.t + p.R * dl[size_t(p.j)];
                const V3 d = dl[size_t(i)] - u;
                const double X = rnd(d.dot(p.a) / s), Y = rnd(d.dot(p.b) / s);
                const double res = (d - s * (X * p.a + Y * p.b)).norm() / s;
                sum += res;
                ++cnt;
                ++pairsAll;
                if (res > 0.2) ++pairsBad;
                if (p.t.norm() <= 1.1 * h)
                {
                    ++pairsNear;
                    if (res > 0.2) ++pairsNearBad;
                }
            }
            if (cnt && sum / cnt > 0.15) ++carriersBad;
        }
        std::fprintf(stderr, "[fields] direction field: mean angle to the neighbours %.1f degrees, %zu of %zu free carriers above 25 degrees; position field: %zu of %zu neighbour pairs disagree by more than a fifth of a spacing (of the near ones, within 1.1 carrier spacings: %zu of %zu), %zu of %d carriers by more than 0.15 on average\n",
                     free ? sumAngle / double(free) : 0.0, rough, free, pairsBad, pairsAll, pairsNearBad, pairsNear, carriersBad, nc);
    }
    if (say && !seeds) std::fprintf(stderr, "[fp] lattice positions %016llx\n", fingerprint(dl));
    lap("lattice positions");

    // ---- the lattice: vertices (nodes of the surface graph), made fine enough that every region is a disc, joined where their regions touch
    std::vector<Vert> verts;
    {
        std::vector<char> edgeNode(G.P.size(), 0);                      // the nodes that are ON a sharp edge or a corner
        for (size_t i = 0; i < feat.kind.size() && i < G.P.size(); ++i) edgeNode[i] = feat.kind[i] != 0;
        for (size_t i = 0; i < G.edgeMark.size() && i < G.P.size(); ++i)
            if (G.edgeMark[i]) edgeNode[i] = 1;
        latticeVertices(G, cn, sn, dl, s, 0.3, verts, corners, isFixed, edgeNode);
        if (say && !seeds)
        {
            size_t pinnedN = 0, onEdgeV = 0;
            for (char c : pinned) pinnedN += c != 0;
            for (const Vert& v : verts) onEdgeV += size_t(v.node) < edgeNode.size() && edgeNode[size_t(v.node)];
            std::fprintf(stderr, "[corners] %zu corners of the part, %zu carriers pinned to them; %zu of %zu lattice vertices are ON an edge or a corner\n", corners.size(), pinnedN, onEdgeV, verts.size());
        }
    }
    if (say && !seeds)
    {
        std::vector<V3> vp;
        for (const Vert& v : verts) vp.push_back(v.pos);
        std::fprintf(stderr, "[fp] %zu lattice vertices %016llx\n", verts.size(), fingerprint(vp));
    }
    std::vector<int> vnode;
    for (const Vert& v : verts) vnode.push_back(v.node);
    int ncomp = 0;
    const std::vector<int> comp = G.pieces(ncomp);
    std::vector<char> point;
    G.isPoint(comp, ncomp, point, 0.1 * delta);
    // A piece with fewer nodes than four regions of the lattice's own size hold (4 (s/delta)^2) cannot carry a map at this cell size: its map would be decided by the
    // sampling, not by the surface.  It is NOT left out: it is deferred, and the caller samples it finer, on its own, until it has the nodes to be judged
    const int minPiece = int(4.0 * (s / delta) * (s / delta));
    std::vector<int> compSize(static_cast<size_t>(ncomp), 0);
    for (size_t k = 0; k < G.P.size(); ++k) ++compSize[size_t(comp[k])];
    std::vector<char> deferred(static_cast<size_t>(ncomp), 0);
    std::vector<char> skip = point;
    for (int c = 0; c < ncomp; ++c)
        if (!point[size_t(c)] && compSize[size_t(c)] < minPiece)
        {
            deferred[size_t(c)] = 1;
            skip[size_t(c)] = 1;
        }
    refineVertices(G, vnode, verts, comp, ncomp, skip, say && !seeds);
    std::vector<char> isVertexNode(G.P.size(), 0);
    for (int n : vnode) isVertexNode[size_t(n)] = 1;
    // The middle of a face is never a node ON a sharp edge: an edge is where cells end, and a face whose middle lies on it is squeezed flat against it (the band beside an edge holds only
    // edge nodes now, so the node nearest the mean of a face's corners would be one)
    std::vector<char> noCentre = isVertexNode;
    for (size_t i = 0; i < feat.kind.size() && i < noCentre.size(); ++i)
        if (feat.kind[i] != 0) noCentre[i] = 1;
    for (size_t i = 0; i < G.edgeMark.size() && i < noCentre.size(); ++i)
        if (G.edgeMark[i]) noCentre[i] = 1;
    const std::map<std::pair<int, int>, int> contact = contactCounts(G, vnode);
    lap("lattice graph");

    // ---- faces: a triangle for every three regions that meet (those the most nodes see first, as long as the map stays true), pairs of triangles that make a square merged, all going round the same way
    if (say && !seeds)
    {
        // (for the developer: the Euler number of the PARTITION into regions, read off the graph without any face: regions - pairs that touch along an arc + junctions where three or more
        // regions meet; to be read against the lattice's [components] and the truth)
        std::vector<int> label;
        std::vector<double> dist;
        partition(G, vnode, label, dist);
        const size_t N = G.P.size();
        std::vector<char> junction(N, 0);
        std::vector<std::map<std::pair<int, int>, int>> arcs(static_cast<size_t>(ncomp));
        std::vector<long long> R(static_cast<size_t>(ncomp), 0), E(static_cast<size_t>(ncomp), 0), J(static_cast<size_t>(ncomp), 0);
        auto seen = [&](size_t n) {
            std::set<int> r;
            if (label[n] >= 0) r.insert(label[n]);
            for (int j : G.adj[n])
                if (label[size_t(j)] >= 0) r.insert(label[size_t(j)]);
            return r;
        };
        for (size_t n = 0; n < N; ++n)
            if (label[n] >= 0 && seen(n).size() >= 3) junction[n] = 1;
        for (size_t n = 0; n < N; ++n)
        {
            if (label[n] < 0 || junction[n]) continue;
            for (int j : G.adj[n])
                if (!junction[size_t(j)] && label[size_t(j)] >= 0 && label[size_t(j)] != label[n])
                    ++arcs[size_t(comp[n])][{std::min(label[n], label[size_t(j)]), std::max(label[n], label[size_t(j)])}];
        }
        for (size_t v = 0; v < vnode.size(); ++v) ++R[size_t(comp[size_t(vnode[v])])];
        for (int c = 0; c < ncomp; ++c) E[size_t(c)] = long(arcs[size_t(c)].size());
        std::vector<char> done(N, 0);
        for (size_t n = 0; n < N; ++n)
        {
            if (!junction[n] || done[n]) continue;
            ++J[size_t(comp[n])];
            std::vector<int> stack = {int(n)};
            done[n] = 1;
            while (!stack.empty())
            {
                const int u = stack.back();
                stack.pop_back();
                for (int w : G.adj[size_t(u)])
                    if (junction[size_t(w)] && !done[size_t(w)])
                    {
                        done[size_t(w)] = 1;
                        stack.push_back(w);
                    }
            }
        }
        std::fprintf(stderr, "[voronoi-euler] pieces (surface nodes: regions, arcs, junctions, Euler number):");
        long long total = 0;
        std::vector<int> order(static_cast<size_t>(ncomp));
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int x, int y) { return compSize[size_t(x)] > compSize[size_t(y)]; });
        for (size_t i = 0; i < order.size(); ++i)
        {
            const int c = order[i];
            if (point[size_t(c)] || R[size_t(c)] == 0) continue;
            const long long chi = R[size_t(c)] - E[size_t(c)] + J[size_t(c)];
            total += chi;
            if (i < 14) std::fprintf(stderr, " (%d: %lld, %lld, %lld, %lld)", compSize[size_t(c)], R[size_t(c)], E[size_t(c)], J[size_t(c)], chi);
        }
        std::fprintf(stderr, "; total %lld\n", total);
    }
    Junctions jf = tripleFaces(G, vnode, contact, 2.5 * s);
    mergeTriangles(jf, contact, verts);
    if (say && !seeds)
    {
        size_t tri = 0, quad = 0, other = 0;
        for (const auto& f : jf.faces) (f.size() == 3 ? tri : f.size() == 4 ? quad : other) += 1;
        std::fprintf(stderr, "[faces] %zu triangles, %zu quads, %zu others (of %zu faces)\n", tri, quad, other, jf.faces.size());
    }
    // the holes that are left (loops of open edges) are closed with one face each, so that a piece that would be refused for a defect of a few faces gets a map; the faces added are
    // the last ones, and what comes of them is counted once the pieces are known
    size_t firstRepair = jf.faces.size();
    {
        std::vector<V3> where;
        const size_t added = closeBoundaries(jf, verts, &where, 16);
        if (say && added) std::fprintf(stderr, "[repair] %zu holes in the faces closed with one face each\n", added);
        const size_t necks = bridgeNecks(jf, verts, 16);
        if (say && necks) std::fprintf(stderr, "[repair] %zu necks (a vertex whose faces were two fans) closed with a tube\n", necks);
        if (necks) closeBoundaries(jf, verts, &where, 16);
        const size_t cut = std::getenv("FIELDES_SC_NOSPLIT") ? 0 : splitBigFaces(jf, verts, S, firstRepair);
        if (say && cut) std::fprintf(stderr, "[split] %zu chords cut faces of six corners or more into faces of four and five\n", cut);
    }
    const std::vector<std::vector<int>>& faces = jf.faces;
    lap("faces");
    if (say && !seeds)
    {
        diagnose(G, S, vnode, jf, contact, s);
        std::fprintf(stderr, "[quads] %zu vertices, %zu faces\n", verts.size(), faces.size());
        // (for the developer: faces whose corners face opposite ways reach across a thin wall or a thin gap)
        size_t opp[4] = {0, 0, 0, 0};
        std::vector<std::pair<double, size_t>> worst;
        for (size_t f = 0; f < faces.size(); ++f)
        {
            double md = 1.0;
            for (int a : faces[f])
                for (int b : faces[f]) md = std::min(md, verts[size_t(a)].n.normalized().dot(verts[size_t(b)].n.normalized()));
            if (md < -0.3) ++opp[0];
            if (md < -0.5) ++opp[1];
            if (md < -0.7) ++opp[2];
            if (md < -0.9) ++opp[3];
            if (md < -0.3) worst.push_back({md, f});
        }
        std::sort(worst.begin(), worst.end());
        std::fprintf(stderr, "[face-normals] faces whose corner normals have a dot product below -0.3: %zu, -0.5: %zu, -0.7: %zu, -0.9: %zu, of %zu faces", opp[0], opp[1], opp[2], opp[3], faces.size());
        for (size_t i = 0; i < worst.size() && i < 3; ++i)
        {
            V3 c = V3::Zero();
            for (int a : faces[worst[i].second]) c += verts[size_t(a)].pos;
            c /= double(faces[worst[i].second].size());
            std::fprintf(stderr, "; worst %.2f at (%.1f %.1f %.1f)", worst[i].first, c[0], c[1], c[2]);
        }
        std::fprintf(stderr, "\n");
    }

    // ---- every piece of the surface on its own: does it make one closed map?
    std::vector<std::vector<int>> facesOf(static_cast<size_t>(ncomp)), vertsOf(static_cast<size_t>(ncomp));
    for (size_t f = 0; f < faces.size(); ++f) facesOf[size_t(comp[size_t(vnode[size_t(faces[f][0])])])].push_back(int(f));
    for (size_t i = 0; i < vnode.size(); ++i) vertsOf[size_t(comp[size_t(vnode[i])])].push_back(int(i));
    std::vector<std::string> why(static_cast<size_t>(ncomp));
    std::vector<char> good(size_t(ncomp), 0);
    for (int c = 0; c < ncomp; ++c)
    {
        if (point[size_t(c)]) continue;
        if (deferred[size_t(c)])
        {
            why[size_t(c)] = "too few nodes (" + std::to_string(compSize[size_t(c)]) + ", of the " + std::to_string(minPiece) + " four regions of this size hold) to carry a map at this cell size";
            continue;
        }
        why[size_t(c)] = checkPiece(faces, facesOf[size_t(c)], vertsOf[size_t(c)]);
        good[size_t(c)] = why[size_t(c)].empty();
    }
    auto compOfFace = [&](const std::vector<int>& f) { return comp[size_t(vnode[size_t(f[0])])]; };

    // ---- a piece whose faces make several SEPARATE closed surfaces is several surfaces that the graph joined by a few hops (two surfaces closer together than the graph can tell
    // apart).  The hops between them are listed: the caller cuts them and lays the surface out again.  Surfaces that were really one (a neck) are then pieces with a hole, and fail
    if (cutOut)
    {
        std::vector<char> split(static_cast<size_t>(ncomp), 0);
        bool any = false;
        for (int c = 0; c < ncomp; ++c)
            if (why[size_t(c)].find("separate surfaces") != std::string::npos)
            {
                split[size_t(c)] = 1;
                any = true;
            }
        if (any)
        {
            std::vector<int> par(vnode.size());
            std::iota(par.begin(), par.end(), 0);
            std::function<int(int)> fnd = [&](int x) {
                while (par[size_t(x)] != x)
                {
                    par[size_t(x)] = par[size_t(par[size_t(x)])];
                    x = par[size_t(x)];
                }
                return x;
            };
            std::vector<char> hasFace(vnode.size(), 0);
            for (const auto& f : faces)
                for (size_t m = 0; m < f.size(); ++m)
                {
                    hasFace[size_t(f[m])] = 1;
                    par[size_t(fnd(f[0]))] = fnd(f[m]);
                }
            std::vector<int> label;
            std::vector<double> dist;
            partition(G, vnode, label, dist);
            for (size_t u = 0; u < G.P.size(); ++u)
            {
                if (label[u] < 0 || !split[size_t(comp[u])] || !hasFace[size_t(label[u])]) continue;
                for (int w : G.adj[u])
                {
                    if (w < int(u) || label[size_t(w)] < 0 || !hasFace[size_t(label[size_t(w)])]) continue;
                    if (fnd(label[u]) != fnd(label[size_t(w)])) cutOut->push_back({int(u), w});
                }
            }
        }
    }

    // ---- the quads: every face of k corners into k quads (corner, middle of the side after it, middle of the face, middle of the side before it)
    std::vector<V3> pos;                                    // vertices first (the used ones keep their number), then the middles
    for (const Vert& v : verts) pos.push_back(v.pos);
    std::vector<V3> nrmAt(verts.size());
    std::vector<int> owner(verts.size(), -1);               // the piece every node belongs to
    for (size_t i = 0; i < verts.size(); ++i)
    {
        nrmAt[i] = verts[i].n;
        owner[i] = comp[size_t(vnode[i])];
    }
    std::map<std::pair<int, int>, int> mid;
    std::vector<int> pending;
    {
        std::map<int, std::set<int>> need;
        for (const auto& f : faces)
        {
            if (!good[size_t(compOfFace(f))]) continue;
            for (size_t m = 0; m < f.size(); ++m)
            {
                const int a = f[m], b = f[(m + 1) % f.size()];
                need[std::min(a, b)].insert(std::max(a, b));
            }
        }
        for (const auto& nd : need)
        {
            const int a = nd.first;
            std::vector<int> targets;
            for (int b : nd.second) targets.push_back(vnode[size_t(b)]);
            const Ways w = G.unfold(vnode[size_t(a)], 3.0 * s, &targets);
            for (int b : nd.second)
            {
                V3 p;
                if (w.find(vnode[size_t(b)]))
                    p = G.middle(w, vnode[size_t(b)]);
                else
                    p = 0.5 * (verts[size_t(a)].pos + verts[size_t(b)].pos);
                mid[{a, b}] = int(pos.size());
                pending.push_back(int(pos.size()));
                owner.push_back(comp[size_t(vnode[size_t(a)])]);
                pos.push_back(p);
            }
        }
    }
    std::vector<std::array<int, 4>> quads;                  // (corner, middle of the side after it, middle of the face, middle of the side before it)
    std::vector<int> quadPiece;
    size_t centresMoved = 0;
    for (const auto& f : faces)
    {
        const int pc = compOfFace(f);
        if (!good[size_t(pc)]) continue;
        const size_t k = f.size();
        std::vector<int> corners;
        for (int v : f) corners.push_back(vnode[size_t(v)]);
        int c = surfaceCentre(G, corners, 2.0 * s, noCentre);
        // The middle of the face is the node nearest the mean of its corners.  Nothing in that says the k quads (corner, middle of a side, middle of the face, middle of the side before) come out
        // right, so it is checked: every corner of every quad must turn the right way and be no further than 15 degrees from a right angle... and no spoke from a side's middle to the middle of the face may
        // run through the air (its own middle within a quarter of its length of the surface).  If the nearest node fails that, the nodes within half a spacing of it are tried, and the one whose
        // quads are best is the middle.  A face whose nearest node passes is not touched
        if (!std::getenv("FIELDES_SC_NOCENTRE"))
        {
            std::vector<V3> cornerP(k), sideP(k);
            for (size_t m = 0; m < k; ++m)
            {
                const int a = f[m], nx = f[(m + 1) % k];
                cornerP[m] = verts[size_t(a)].pos;
                sideP[m] = pos[size_t(mid[{std::min(a, nx), std::max(a, nx)}])];
            }
            auto scoreOf = [&](const std::vector<int>& nodesZ, std::vector<double>& score, std::vector<char>& bad, bool withField) {
                std::vector<V3> spokes;
                std::vector<double> fv;
                std::vector<V3> gv;
                if (withField)
                {
                    for (int z : nodesZ)
                        for (size_t m = 0; m < k; ++m) spokes.push_back(0.5 * (G.P[size_t(z)] + sideP[m]));
                    S.valueAndGradient(spokes, fv, gv);
                }
                score.assign(nodesZ.size(), 0.0);
                bad.assign(nodesZ.size(), 0);
                for (size_t zi = 0; zi < nodesZ.size(); ++zi)
                {
                    const V3 zp = G.P[size_t(nodesZ[zi])];
                    const V3 nz = G.N[size_t(nodesZ[zi])].normalized();
                    for (size_t m = 0; m < k; ++m)
                    {
                        const V3 q[4] = {cornerP[m], sideP[m], zp, sideP[(m + k - 1) % k]};
                        for (int t = 0; t < 4; ++t)
                        {
                            const V3 nxv = q[(t + 1) % 4] - q[t], pvv = q[(t + 3) % 4] - q[t];
                            const double ln = nxv.norm() * pvv.norm();
                            if (ln < 1e-12)
                            {
                                bad[zi] = 1;
                                score[zi] += 10.0;
                                continue;
                            }
                            const double turn = nxv.cross(pvv).dot(nz) / ln;
                            if (turn <= 0.1)
                            {
                                bad[zi] = 1;
                                score[zi] += 20.0 * (0.1 - turn) * (0.1 - turn) + 1.0;
                            }
                            double ang = std::atan2(nxv.cross(pvv).dot(nz), nxv.dot(pvv) - nxv.dot(nz) * pvv.dot(nz)) * 57.29577951308232;
                            if (ang < 0) ang += 360.0;
                            const double dev = std::max(0.0, std::abs(ang - 90.0) - 15.0) / 45.0;
                            score[zi] += dev * dev;
                        }
                        if (!withField) continue;
                        const size_t at = zi * k + m;
                        const double gn = gv[at].norm();
                        const double len = (zp - sideP[m]).norm();
                        if (!(gn > 1e-9) || !std::isfinite(fv[at]) || std::abs(fv[at]) / gn > 0.25 * len)
                        {
                            bad[zi] = 1;
                            score[zi] += 5.0;
                        }
                    }
                }
            };
            std::vector<double> sc0;
            std::vector<char> bad0;
            scoreOf({c}, sc0, bad0, true);
            if (bad0[0])
            {
                const Ways around = G.unfold(c, 0.5 * s);
                std::vector<int> cand;
                for (size_t q = 0; q < around.ids.size() && cand.size() < 40; ++q)
                    if (!noCentre[size_t(around.ids[q])]) cand.push_back(around.ids[q]);
                // (the shapes of the quads first -- that needs no reading of the field -- and the field only for the four best)
                std::vector<double> sg;
                std::vector<char> bg;
                scoreOf(cand, sg, bg, false);
                std::vector<size_t> order(cand.size());
                std::iota(order.begin(), order.end(), size_t(0));
                std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return sg[x] < sg[y]; });
                std::vector<int> top;
                for (size_t q = 0; q < order.size() && top.size() < 4; ++q) top.push_back(cand[order[q]]);
                std::vector<double> sc;
                std::vector<char> bad;
                scoreOf(top, sc, bad, true);
                int best = -1;
                for (size_t q = 0; q < top.size(); ++q)
                    if (sc[q] < sc0[0] - 1e-9 && (best < 0 || sc[q] < sc[size_t(best)])) best = int(q);
                if (best >= 0)
                {
                    c = top[size_t(best)];
                    ++centresMoved;
                }
            }
        }
        const int centre = int(pos.size());
        pending.push_back(centre);
        owner.push_back(pc);
        pos.push_back(G.P[size_t(c)]);
        for (size_t m = 0; m < k; ++m)
        {
            const int a = f[m], nx = f[(m + 1) % k], pv = f[(m + k - 1) % k];
            quads.push_back({a, mid[{std::min(a, nx), std::max(a, nx)}], centre, mid[{std::min(a, pv), std::max(a, pv)}]});
            quadPiece.push_back(pc);
        }
    }
    if (say && !seeds && centresMoved) std::fprintf(stderr, "[centre] %zu faces got another node for their middle (the nearest to the mean of the corners gave quads that turn the wrong way or spokes through the air)\n", centresMoved);
    nrmAt.resize(pos.size(), V3(0, 0, 1));
    std::vector<size_t> unprojected(size_t(ncomp), 0);
    {
        std::vector<V3> pp;
        for (int i : pending) pp.push_back(pos[size_t(i)]);
        std::vector<V3> nrm;
        std::vector<char> ok;
        // a node is on the surface when it is within a fiftieth of a cell of it (the cell is what the map is for; a distance of a tenth of the sampling step, which was
        // asked here before, refused maps whose side middles lay a few hundredths of a millimetre off a crease)
        S.project(pp, nrm, ok, 5, 0.02 * cell);
        for (size_t k = 0; k < pending.size(); ++k)
        {
            const size_t i = size_t(pending[k]);
            if (ok[k])
            {
                pos[i] = pp[k];
                nrmAt[i] = nrm[k];
            }
            else
            {
                ++unprojected[size_t(owner[i])];
                nrmAt[i] = nrm[k];
            }
        }
    }
    lap("quads");

    // ---- the check, piece by piece: four different corners, every edge on exactly two quads, every node on the surface
    {
        std::map<std::pair<int, int>, std::pair<int, int>> edgeUse;       // the quads on an edge, and the piece they belong to
        std::vector<size_t> degenerate(size_t(ncomp), 0), open(size_t(ncomp), 0), many(size_t(ncomp), 0);
        for (size_t k = 0; k < quads.size(); ++k)
        {
            const auto& qd = quads[k];
            if (qd[0] == qd[1] || qd[0] == qd[2] || qd[0] == qd[3] || qd[1] == qd[2] || qd[1] == qd[3] || qd[2] == qd[3]) ++degenerate[size_t(quadPiece[k])];
            for (int m = 0; m < 4; ++m)
            {
                const int a = qd[size_t(m)], b = qd[size_t((m + 1) % 4)];
                auto& u = edgeUse[{std::min(a, b), std::max(a, b)}];
                ++u.first;
                u.second = quadPiece[k];
            }
        }
        for (const auto& e : edgeUse)
        {
            if (e.second.first == 1) ++open[size_t(e.second.second)];
            if (e.second.first > 2) ++many[size_t(e.second.second)];
        }
        for (int c = 0; c < ncomp; ++c)
        {
            if (good[size_t(c)] && (degenerate[size_t(c)] || open[size_t(c)] || many[size_t(c)] || unprojected[size_t(c)]))
            {
                good[size_t(c)] = 0;
                why[size_t(c)] = "the cell map was not closed and made of quads (" + std::to_string(degenerate[size_t(c)]) + " cells with a repeated corner, " +
                                 std::to_string(open[size_t(c)]) + " open edges, " + std::to_string(many[size_t(c)]) + " edges on more than two cells, " +
                                 std::to_string(unprojected[size_t(c)]) + " nodes off the surface)";
            }
        }
    }

    // ---- the maps of the pieces that are good: the quads counterclockwise seen from outside, the nodes renumbered
    {
        std::vector<int> positive(size_t(ncomp), 0), negative(size_t(ncomp), 0);
        for (size_t k = 0; k < quads.size(); ++k)
        {
            const auto& qd = quads[k];
            const V3 a = pos[size_t(qd[0])];
            const double sgn = (pos[size_t(qd[1])] - a).cross(pos[size_t(qd[3])] - a).dot(nrmAt[size_t(qd[0])]);
            (sgn >= 0 ? positive : negative)[size_t(quadPiece[k])] += 1;
        }
        std::vector<int> remap(pos.size(), -1);
        for (size_t k = 0; k < quads.size(); ++k)
        {
            const int pc = quadPiece[k];
            if (!good[size_t(pc)]) continue;
            const auto& qd = quads[k];
            int id[4];
            for (int m = 0; m < 4; ++m)
            {
                int& r = remap[size_t(qd[size_t(m)])];
                if (r < 0)
                {
                    r = int(out.nodes.size());
                    GNode g;
                    g.p = pos[size_t(qd[size_t(m)])];
                    g.n = nrmAt[size_t(qd[size_t(m)])];
                    out.nodes.push_back(g);
                }
                id[m] = r;
            }
            const bool flip = negative[size_t(pc)] > positive[size_t(pc)];
            Cell c;
            // corners [s][t] = c[s + 2t]: a, a + s, a + t, a + s + t; the quad (a, next, centre, previous) goes round counterclockwise
            c.c[0] = id[0];
            c.c[1] = flip ? id[3] : id[1];
            c.c[2] = flip ? id[1] : id[3];
            c.c[3] = id[2];
            out.cells.push_back(c);
        }
    }

    for (size_t k = 0; k < G.P.size(); ++k)
        if (good[size_t(comp[k])])
        {
            out.covered.push_back(G.P[k]);
            out.coveredN.push_back(G.N[k]);
            if ((k < feat.kind.size() && feat.kind[k] != 0) || (k < G.edgeMark.size() && G.edgeMark[k])) out.edgePoints.push_back(G.P[k]);
        }

    for (size_t f = firstRepair; f < faces.size(); ++f)
        if (good[size_t(compOfFace(faces[f]))])
        {
            ++out.repairs;
            V3 m = V3::Zero();
            for (int v : faces[f]) m += verts[size_t(v)].pos;
            out.repairAt.push_back(m / double(faces[f].size()));
        }

    // ---- the pieces that did not come out: none is dropped, the caller samples them finer
    for (int c = 0; c < ncomp; ++c)
    {
        if (point[size_t(c)] || good[size_t(c)]) continue;
        FailedPiece fp;
        fp.lo = V3::Constant(1e300);
        fp.hi = V3::Constant(-1e300);
        std::vector<V3> all;
        for (size_t k = 0; k < G.P.size(); ++k)
            if (comp[k] == c)
            {
                all.push_back(G.P[k]);
                fp.lo = fp.lo.cwiseMin(G.P[k]);
                fp.hi = fp.hi.cwiseMax(G.P[k]);
            }
        fp.nodes = all.size();
        const size_t step = std::max<size_t>(1, all.size() / 400);
        for (size_t k = 0; k < all.size(); k += step) fp.seeds.push_back(all[k]);
        fp.why = why[size_t(c)];
        failed.push_back(fp);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Evening the cells out.  The layout decides WHICH cells there are; it does not make them regular.  Every node that is not on a sharp edge is moved over the surface towards the
// middle of its neighbours, a little at a time, and put back onto the surface; a move that would fold a cell or make a corner worse than it is, is not made.  The cells stay the
// same cells (the same four corners, the same neighbours): only the places of the nodes change, so the map stays closed, of quads, and on the surface.  The result is kept only if
// fewer of the cells are bad than before

struct CellQuality
{
    size_t cells = 0, bad = 0;
};

// The angle of a cell at the corner `b`, in degrees from 0 to 360, seen along the surface normal there: `next` is the corner after it, `prev` the corner before it (the ring goes
// counterclockwise seen from outside).  A corner that turns the wrong way (dented, or the cell folded over) is over 180 -- the angle between the two sides alone (an arccosine)
// cannot tell it from the same corner turning the right way, and a smoothing that cannot tell them apart has nothing to push a dented cell out with
inline double cornerAngle(const V3& b, const V3& n, const V3& next, const V3& prev)
{
    const V3 v = next - b, u = prev - b;
    const double s = v.cross(u).dot(n);
    const double c = v.dot(u) - v.dot(n) * u.dot(n);
    double a = std::atan2(s, c) * 57.29577951308232;
    if (a < 0) a += 360.0;
    return a;
}

bool wrappedCell(const std::vector<GNode>& nodes, const Cell& c);

// The normal a corner of a cell is judged along.  At a node exactly ON a sharp edge the normal is one face's, or a mix of the two (the field has no single normal there), and a cell that lies
// on the OTHER face is seen from the side: its corner there would come out at any angle.  So a corner is judged along its node's normal when that agrees with the cell (the cross of its
// diagonals, turned the way its nodes face), and along the cell's own normal when it does not
inline V3 cellOwnNormal(const std::vector<GNode>& nodes, const int ring[4])
{
    V3 meanN = V3::Zero();
    for (int m = 0; m < 4; ++m) meanN += nodes[size_t(ring[m])].n;
    V3 cn = (nodes[size_t(ring[2])].p - nodes[size_t(ring[0])].p).cross(nodes[size_t(ring[3])].p - nodes[size_t(ring[1])].p);
    if (cn.norm() < 1e-12) return V3::Zero();
    cn.normalize();
    if (meanN.norm() > 1e-9 && cn.dot(meanN) < 0) cn = -cn;
    return cn;
}

inline V3 cornerNormal(const std::vector<GNode>& nodes, const int ring[4], int k, const V3& cn)
{
    const V3& nk = nodes[size_t(ring[k])].n;
    if (cn.squaredNorm() < 0.5) return nk;
    return (nk.norm() > 1e-9 && nk.normalized().dot(cn) > 0.9) ? V3(nk.normalized()) : cn;
}

// A cell is bad when a corner is under 40 or over 140 degrees or its longest side is over 2.5 times its shortest (a cell round a thin rim: its sides only)
CellQuality measureCells(const std::vector<GNode>& nodes, const std::vector<Cell>& cells)
{
    CellQuality q;
    q.cells = cells.size();
    for (const Cell& c : cells)
    {
        const int ring[4] = {c.c[0], c.c[1], c.c[3], c.c[2]};
        double lmin = 1e300, lmax = 0, amin = 1e300, amax = 0;
        const bool bent = wrappedCell(nodes, c);
        const V3 cn = bent ? V3(V3::Zero()) : cellOwnNormal(nodes, ring);
        for (int k = 0; k < 4; ++k)
        {
            const V3& a = nodes[size_t(ring[(k + 3) % 4])].p;
            const V3& b = nodes[size_t(ring[k])].p;
            const V3& d = nodes[size_t(ring[(k + 1) % 4])].p;
            const double L = (d - b).norm();
            lmin = std::min(lmin, L);
            lmax = std::max(lmax, L);
            if ((a - b).norm() < 1e-12 || (d - b).norm() < 1e-12)
            {
                amin = 0;
                continue;
            }
            if (bent) continue;
            const double ang = cornerAngle(b, cornerNormal(nodes, ring, k, cn), d, a);
            amin = std::min(amin, ang);
            amax = std::max(amax, ang);
        }
        if (amin < 40.0 || amax > 140.0 || lmax > 2.5 * lmin) ++q.bad;
    }
    return q;
}

// A cell round a thin rim: its corners' normals differ by more than 100 degrees.  It is bent, not flat, and the angles of its corners seen along any one normal mean nothing
bool wrappedCell(const std::vector<GNode>& nodes, const Cell& c)
{
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            if (nodes[size_t(c.c[a])].n.normalized().dot(nodes[size_t(c.c[b])].n.normalized()) < -0.17) return true;
    return false;
}

// The sine of the sharpest corner of a cell seen along the mean normal of its nodes (positive: the corner turns the right way; a folded or flat cell has a corner at or below zero).
// A cell round a thin rim is not judged (1)
double cellTurn(const std::vector<GNode>& nodes, const Cell& c)
{
    if (wrappedCell(nodes, c)) return 1.0;
    const int ring[4] = {c.c[0], c.c[1], c.c[3], c.c[2]};
    V3 n = V3::Zero();
    for (int k = 0; k < 4; ++k) n += nodes[size_t(ring[k])].n;
    if (n.norm() < 1e-9) return 0.0;
    n.normalize();
    double worst = 1e300;
    for (int k = 0; k < 4; ++k)
    {
        const V3& b = nodes[size_t(ring[k])].p;
        const V3 nx = nodes[size_t(ring[(k + 1) % 4])].p - b, pv = nodes[size_t(ring[(k + 3) % 4])].p - b;
        const double L = nx.norm() * pv.norm();
        worst = std::min(worst, L > 1e-300 ? nx.cross(pv).dot(n) / L : -1.0);
    }
    return worst;
}

// How badly shaped a cell is, smoothly (0: nothing to improve): the corners that are further than 15 degrees from a right angle, the more the further (a corner at the limit of
// the "bad" measure, 40 or 140 degrees, costs 0.6), and the longest side against the shortest beyond a ratio of 1.35.  A node is moved only if the cells round it get better by this
double cellEnergy(const std::vector<GNode>& nodes, const Cell& c, double target)
{
    const int ring[4] = {c.c[0], c.c[1], c.c[3], c.c[2]};
    double lmin = 1e300, lmax = 0, e = 0;
    const bool bent = wrappedCell(nodes, c);                // (a cell round a rim is judged by its sides only)
    const V3 cn = bent ? V3(V3::Zero()) : cellOwnNormal(nodes, ring);
    for (int k = 0; k < 4; ++k)
    {
        const V3& a = nodes[size_t(ring[(k + 3) % 4])].p;
        const V3& b = nodes[size_t(ring[k])].p;
        const V3& d = nodes[size_t(ring[(k + 1) % 4])].p;
        const double L = (d - b).norm();
        lmin = std::min(lmin, L);
        lmax = std::max(lmax, L);
        // an edge much shorter than the cell (vertices that have run into each other) or much longer (a spike across a crevice) costs: beyond 0.6 and 1.6 of the size, in the log
        const double lr = std::abs(std::log(std::max(L, 1e-12) / target)) - std::log(1.6);
        if (lr > 0) e += 3.0 * lr * lr;
        if ((a - b).norm() < 1e-12 || (d - b).norm() < 1e-12)
        {
            e += 10.0;
            continue;
        }
        if (bent) continue;
        const double ang = cornerAngle(b, cornerNormal(nodes, ring, k, cn), d, a);
        const double dev = std::max(0.0, std::abs(ang - 90.0) - 15.0) / 45.0;
        e += dev * dev;
    }
    const double r = std::max(0.0, std::log(lmax / std::max(lmin, 1e-12)) - std::log(1.35));
    return e + 2.0 * r * r;
}

// The middle of a cell (the mean of its corners) and its normal (the diagonals' cross product)
struct CellFrame
{
    V3 centre, normal;
};

inline CellFrame cellFrame(const std::vector<GNode>& nodes, const Cell& c)
{
    const V3 p0 = nodes[size_t(c.c[0])].p, p1 = nodes[size_t(c.c[1])].p, p2 = nodes[size_t(c.c[2])].p, p3 = nodes[size_t(c.c[3])].p;      // (corners [s][t] = c[s + 2t]: the ring is 0, 1, 3, 2)
    const V3 n = (p3 - p0).cross(p2 - p1);
    const double ln = n.norm();
    return {0.25 * (p0 + p1 + p2 + p3), ln > 1e-12 ? V3(n / ln) : V3(0, 0, 1)};
}

// Does cell `a` cover a piece of surface that cell `b` covers?  Two of five samples of `a` (its middle and four points 0.7 of the way to its corners) lie inside the footprint of `b`, along
// its normal and close to its plane.  Cells that share a corner are neighbours, and cells that face another way are other places on the surface: neither is meant
inline bool footprintHit(const std::vector<GNode>& nodes, const Cell& a, const CellFrame& fa, const Cell& b, const CellFrame& fb, double target)
{
    const int ra[4] = {a.c[0], a.c[1], a.c[3], a.c[2]};
    const int rb[4] = {b.c[0], b.c[1], b.c[3], b.c[2]};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (ra[i] == rb[j]) return false;
    if (fa.normal.dot(fb.normal) < 0.5) return false;
    const V3 n = fb.normal;
    V3 ex = nodes[size_t(rb[1])].p - nodes[size_t(rb[0])].p;
    ex -= ex.dot(n) * n;
    if (ex.norm() < 1e-12) return false;
    ex.normalize();
    const V3 ey = n.cross(ex);
    double qx[4], qy[4];
    for (int k = 0; k < 4; ++k)
    {
        const V3 r = nodes[size_t(rb[k])].p - fb.centre;
        qx[k] = r.dot(ex);
        qy[k] = r.dot(ey);
    }
    int hit = 0;
    for (int m = 0; m < 5; ++m)
    {
        const V3 s = m == 0 ? fa.centre : V3(fa.centre + 0.7 * (nodes[size_t(ra[m - 1])].p - fa.centre));
        const V3 r = s - fb.centre;
        if (std::abs(r.dot(n)) >= 0.35 * target) continue;
        const double px = r.dot(ex), py = r.dot(ey);
        bool pos = true, neg = true;
        for (int k = 0; k < 4; ++k)
        {
            const int q = (k + 1) % 4;
            const double cr = (qx[q] - qx[k]) * (py - qy[k]) - (qy[q] - qy[k]) * (px - qx[k]);
            if (cr <= 1e-12) pos = false;
            if (cr >= -1e-12) neg = false;
        }
        if (pos || neg) ++hit;
    }
    return hit >= 2;
}

// What is forbidden in a map, counted: edges shorter than 0.3 of the middle edge (vertices that have run into each other), edges longer than twice it (spikes across a crevice), cells
// with a corner that turns the wrong way in their own plane (dented), and cells that cover the same piece of surface as another (two of five samples of one lie inside the footprint of the
// other, along its normal and close to its plane)
struct Defects
{
    size_t collapsed = 0, longEdges = 0, dented = 0, overlapping = 0, offSurface = 0, coincident = 0, cutCorners = 0;
    std::vector<unsigned char> mark;                       // per cell: 1 has a collapsed edge, 2 a long edge, 4 is dented or flipped, 8 overlaps another cell, 16 has an edge through the air, 32 has coincident corners, 64 an edge cutting a corner
    // what is FORBIDDEN (a cell that folds over another or itself, an edge through empty space, two vertices at one place) and what is only ugly (edges much shorter or longer than the rest)
    size_t forbidden() const { return dented + overlapping + offSurface + coincident; }
    size_t total() const { return collapsed + longEdges + cutCorners + forbidden(); }
};

// `S` (optional) is the surface the cells are on: with it, an edge whose middle is off the surface by more than a quarter of its length is counted (it runs through the air)
Defects mapDefects(const std::vector<GNode>& nodes, const std::vector<Cell>& cells, Surf* S = nullptr)
{
    Defects d;
    if (cells.empty()) return d;
    d.mark.assign(cells.size(), 0);
    std::vector<double> all;
    for (const Cell& c : cells)
        for (int k = 0; k < 4; ++k) all.push_back((nodes[size_t(c.c[k])].p - nodes[size_t(c.c[(k + 1) % 4])].p).norm());
    std::nth_element(all.begin(), all.begin() + all.size() / 2, all.end());
    const double target = std::max(all[all.size() / 2], 1e-9);
    std::set<std::pair<int, int>> seen;
    std::vector<V3> centre(cells.size()), normal(cells.size());
    std::vector<V3> edgeMid;                                                       // the middle of every edge (once), for the test against the surface, and the cell it was first met in
    std::vector<double> edgeLen;
    std::vector<size_t> edgeCell;
    for (size_t ci = 0; ci < cells.size(); ++ci)
    {
        const int ring[4] = {cells[ci].c[0], cells[ci].c[1], cells[ci].c[3], cells[ci].c[2]};
        V3 p[4], cen = V3::Zero();
        for (int k = 0; k < 4; ++k)
        {
            p[k] = nodes[size_t(ring[k])].p;
            cen += 0.25 * p[k];
            const int a = ring[k], b = ring[(k + 1) % 4];
            if (seen.insert({std::min(a, b), std::max(a, b)}).second)
            {
                const double L = (p[k] - nodes[size_t(b)].p).norm();
                if (L < 0.1 * target)
                {
                    ++d.coincident;
                    d.mark[ci] |= 32;
                }
                if (L < 0.3 * target)
                {
                    ++d.collapsed;
                    d.mark[ci] |= 1;
                }
                if (L > 2.0 * target)
                {
                    ++d.longEdges;
                    d.mark[ci] |= 2;
                }
                edgeMid.push_back(0.5 * (p[k] + nodes[size_t(b)].p));
                edgeLen.push_back(L);
                edgeCell.push_back(ci);
            }
        }
        centre[ci] = cen;
        V3 n = (p[2] - p[0]).cross(p[3] - p[1]);
        const double ln = n.norm();
        normal[ci] = ln > 1e-12 ? V3(n / ln) : V3(0, 0, 1);
        // a cell round a thin rim (its corners' normals differ by more than 100 degrees) is bent, not dented: the angles of its corners seen along one normal mean nothing, and it is judged
        // by the other tests only
        double minDot = 1.0;
        V3 meanN = V3::Zero();
        for (int a = 0; a < 4; ++a)
        {
            meanN += nodes[size_t(ring[a])].n;
            for (int b = a + 1; b < 4; ++b) minDot = std::min(minDot, nodes[size_t(ring[a])].n.normalized().dot(nodes[size_t(ring[b])].n.normalized()));
        }
        const bool wrapped = minDot < -0.17;
        bool dent = false;
        for (int k = 0; k < 4; ++k)
        {
            const V3 nx = p[(k + 1) % 4] - p[k], pv = p[(k + 3) % 4] - p[k];
            if (nx.cross(pv).dot(normal[ci]) <= 0) dent = true;                    // (the ring runs one way: every corner must turn the way the diagonals say)
        }
        // the whole cell turned over (its sense of going round against the surface's outside)
        if (meanN.norm() > 1e-9 && normal[ci].dot(meanN.normalized()) < -0.3) dent = true;
        if (dent && !wrapped)
        {
            ++d.dented;
            d.mark[ci] |= 4;
        }
    }
    if (S && !edgeMid.empty())
    {
        std::vector<double> f;
        std::vector<V3> g;
        S->valueAndGradient(edgeMid, f, g);
        for (size_t k = 0; k < edgeMid.size(); ++k)
        {
            const double gn = g[k].norm();
            if (!(gn > 1e-9) || !std::isfinite(f[k])) continue;
            const double away = std::abs(f[k]) / gn;
            if (away > 0.5 * target)                                               // the middle of the edge is more than half a cell from the surface: the edge runs through the air
            {
                ++d.offSurface;
                d.mark[edgeCell[k]] |= 16;
            }
            else if (away > 0.25 * edgeLen[k])                                     // it cuts a corner of the surface (a cell with no vertex on a sharp edge it straddles): ugly, not forbidden
            {
                ++d.cutCorners;
                d.mark[edgeCell[k]] |= 64;
            }
        }
    }
    PointHash H(2.0 * target);
    for (size_t ci = 0; ci < cells.size(); ++ci) H.add(int(ci), centre[ci]);
    std::vector<char> over(cells.size(), 0);
    for (size_t ci = 0; ci < cells.size(); ++ci)
    {
        const CellFrame fi{centre[ci], normal[ci]};
        H.near(centre[ci], 1, [&](int cj) {
            if (size_t(cj) == ci || over[ci]) return;
            if (footprintHit(nodes, cells[ci], fi, cells[size_t(cj)], CellFrame{centre[size_t(cj)], normal[size_t(cj)]}, target)) over[ci] = over[size_t(cj)] = 1;
        });
    }
    for (size_t ci = 0; ci < cells.size(); ++ci)
        if (over[ci])
        {
            ++d.overlapping;
            d.mark[ci] |= 8;
        }
    return d;
}

void relaxCells(Field& F, std::vector<GNode>& nodes, const std::vector<Cell>& cells, const std::vector<V3>& edgePts, bool say)
{
    const size_t N = nodes.size();
    if (N == 0 || cells.empty()) return;
    std::vector<std::vector<int>> nb(N), inc(N);
    for (size_t k = 0; k < cells.size(); ++k)
    {
        const int ring[4] = {cells[k].c[0], cells[k].c[1], cells[k].c[3], cells[k].c[2]};
        for (int m = 0; m < 4; ++m)
        {
            const int a = ring[m], b = ring[(m + 1) % 4];
            nb[size_t(a)].push_back(b);
            nb[size_t(b)].push_back(a);
            inc[size_t(a)].push_back(int(k));
        }
    }
    std::vector<double> local(N, 0.0);                      // the node's own scale: the mean length of its edges
    std::vector<char> fixed(N, 0);                          // a node where the surface turns sharply stays where it is
    for (size_t i = 0; i < N; ++i)
    {
        std::sort(nb[i].begin(), nb[i].end());
        nb[i].erase(std::unique(nb[i].begin(), nb[i].end()), nb[i].end());
        for (int j : nb[i])
        {
            local[i] += (nodes[size_t(j)].p - nodes[i].p).norm();
            if (nodes[i].n.dot(nodes[size_t(j)].n) < 0.85) fixed[i] = 1;
        }
        if (!nb[i].empty()) local[i] /= double(nb[i].size());
        if (nb[i].size() < 3) fixed[i] = 1;
    }
    // A node IS on a sharp edge when its neighbours' normals differ from its own to BOTH sides (some towards one face, some towards the other).  A node on a face next to the edge has
    // neighbours that differ from it to one side only (the nodes on the edge): it is not on the edge, it moves over its face (and keeps its normal)
    std::vector<char> crease(N, 0);
    for (size_t i = 0; i < N; ++i)
    {
        if (!fixed[i]) continue;
        if (nb[i].size() < 3)
        {
            crease[i] = 1;
            continue;
        }
        int worstJ = -1;
        double worstDot = 2.0;
        for (int j : nb[i])
        {
            const double d = nodes[i].n.dot(nodes[size_t(j)].n);
            if (d < worstDot)
            {
                worstDot = d;
                worstJ = j;
            }
        }
        V3 axis = nodes[size_t(worstJ)].n - nodes[i].n.dot(nodes[size_t(worstJ)].n) * nodes[i].n;
        if (axis.norm() < 1e-9)
        {
            crease[i] = 1;
            continue;
        }
        axis.normalize();
        bool pos = false, neg = false;
        for (int j : nb[i])
        {
            const double c = (nodes[size_t(j)].n - nodes[i].n).dot(axis);
            if (c > 0.12) pos = true;
            if (c < -0.12) neg = true;
        }
        crease[i] = pos && neg;
    }
    // a node on a sharp edge slides ALONG the edge: its two neighbours on the edge (the ones in the direction of the edge, themselves on it) give the place it moves towards
    std::vector<V3> slideT(N, V3::Zero());
    std::vector<int> slideA(N, -1), slideB(N, -1);
    std::vector<char> freeSlide(N, 0);                      // a node on a sharp edge that has no two neighbours along the edge to slide between: it still slides along the edge, by steps
    for (size_t i = 0; i < N; ++i)
    {
        if (!crease[i] || nb[i].size() < 3) continue;
        int worstJ = -1;
        double worstDot = 2.0;
        for (int j : nb[i])
        {
            const double d = nodes[i].n.dot(nodes[size_t(j)].n);
            if (d < worstDot)
            {
                worstDot = d;
                worstJ = j;
            }
        }
        if (worstJ < 0) continue;
        const V3 na = nodes[size_t(worstJ)].n, ni = nodes[i].n;
        const V3 nb2 = 2.0 * na.dot(ni) * ni - na;                  // (the node's normal is the middle of the two faces' normals)
        V3 tg = na.cross(nb2);
        if (tg.norm() < 0.3) continue;
        tg.normalize();
        slideT[i] = tg;
        freeSlide[i] = 1;
        int a = -1, b = -1;
        double da = 0, db = 0;
        for (int j : nb[i])
        {
            if (!crease[size_t(j)]) continue;
            const V3 dir = nodes[size_t(j)].p - nodes[i].p;
            const double L = dir.norm();
            if (L < 1e-12 || std::abs(dir.dot(tg)) / L < 0.85) continue;
            const double side = dir.dot(tg);
            if (side > 0 && L > da) { da = L; a = j; }
            if (side < 0 && L > db) { db = L; b = j; }
        }
        if (a >= 0 && b >= 0)
        {
            slideT[i] = tg;
            slideA[i] = a;
            slideB[i] = b;
            freeSlide[i] = 0;
        }
    }
    size_t sliding = 0;
    for (size_t i = 0; i < N; ++i)
        if (slideA[i] >= 0) ++sliding;
    double meanLocal = 0;
    for (double l : local) meanLocal += l;
    meanLocal /= double(N);
    // the size the cells should have: the middle one of all the edges
    double target = meanLocal;
    {
        std::vector<double> all;
        for (const Cell& c : cells)
            for (int k = 0; k < 4; ++k) all.push_back((nodes[size_t(c.c[k])].p - nodes[size_t(c.c[(k + 1) % 4])].p).norm());
        std::nth_element(all.begin(), all.begin() + all.size() / 2, all.end());
        if (all[all.size() / 2] > 1e-9) target = all[all.size() / 2];
    }
    const CellQuality before = measureCells(nodes, cells);
    std::vector<GNode> work = nodes;
    Surf S(F, 0.005 * meanLocal);
    // A node ON a sharp edge or at a corner stays where the layout put it.  It is read from the field, not from the normals (at an edge the normal is one face's or the other's, and a
    // node that looks as if it sat on one face is moved into it): the gradient at eight points round the node, 0.05 of a cell away in its tangent plane, turns by more than 30 degrees
    // where the node is by an edge and not at all where it is on a face
    auto readFeature = [&](const std::vector<V3>& at, const std::vector<V3>& nrmAt) {
        const double r = 0.05 * target;
        const size_t M = at.size();
        std::vector<char> res(M, 0);
        std::vector<V3> probes;
        probes.reserve(8 * M);
        for (size_t i = 0; i < M; ++i)
        {
            const V3 nrm = nrmAt[i].norm() > 1e-9 ? V3(nrmAt[i].normalized()) : V3(0, 0, 1);
            V3 u = nrm.cross(std::abs(nrm[0]) < 0.9 ? V3(1, 0, 0) : V3(0, 1, 0));
            u.normalize();
            const V3 w = nrm.cross(u);
            for (int k = 0; k < 8; ++k)
            {
                const double a = 0.7853981633974483 * k;
                probes.push_back(at[i] + r * (std::cos(a) * u + std::sin(a) * w));
            }
        }
        std::vector<double> pf;
        std::vector<V3> pg;
        S.valueAndGradient(probes, pf, pg);
        for (size_t i = 0; i < M; ++i)
        {
            V3 gs[8];
            int cnt = 0;
            for (int k = 0; k < 8; ++k)
            {
                const V3& g = pg[8 * i + size_t(k)];
                const double gn = g.norm();
                if (gn > 1e-9 && std::isfinite(pf[8 * i + size_t(k)])) gs[cnt++] = g / gn;
            }
            if (cnt < 4) continue;
            double md = 2.0;
            for (int a = 0; a < cnt; ++a)
                for (int b = a + 1; b < cnt; ++b) md = std::min(md, gs[a].dot(gs[b]));
            res[i] = md < 0.866;
        }
        return res;
    };
    std::vector<char> onFeature(N, 0);
    size_t frozen = 0;
    if (!std::getenv("FIELDES_SC_NOFREEZE"))
    {
        std::vector<V3> at(N), nr(N);
        for (size_t i = 0; i < N; ++i)
        {
            at[i] = nodes[i].p;
            nr[i] = nodes[i].n;
        }
        onFeature = readFeature(at, nr);
        frozen = size_t(std::count(onFeature.begin(), onFeature.end(), char(1)));
    }
    int rounds = 0;
    size_t movedTotal = 0;
    // Every node is tried with SEVERAL moves, and the one that makes the cells round it best is taken if it makes them better at all: towards the middle of its neighbours, to the place that
    // makes each cell round it a parallelogram, and -- the moves a node stuck between its neighbours needs -- away from a neighbour that is too near (an edge much shorter than the
    // cells) and towards one that is too far.  A node at a sharp edge slides along the edge; a node where the surface merely bends (cells large against the curvature) is no longer
    // frozen: it moves over its own face and must keep its normal
    stageBegin("evening out the cells");
    for (; rounds < 400; ++rounds)
    {
        stageSet(std::min(0.95, double(rounds) / 30.0), "round " + std::to_string(rounds + 1) + " (it ends when the cells stop changing)");
        std::vector<V3> cand;
        std::vector<int> who;
        for (size_t i = 0; i < N; ++i)
        {
            if (onFeature[i]) continue;
            std::vector<V3> tries;
            double happy = 0;                                                            // (a node whose cells are all as good as they get is left alone)
            for (int ci : inc[i]) happy += cellEnergy(work, cells[size_t(ci)], target);
            if (happy < 1e-9) continue;
            if (crease[i] && slideA[i] < 0)
            {
                if (freeSlide[i])
                    for (double u : {-0.6, -0.3, 0.3, 0.6}) tries.push_back(work[i].p + u * local[i] * slideT[i]);
            }
            else if (crease[i])
            {
                const V3 m = 0.5 * (work[size_t(slideA[i])].p + work[size_t(slideB[i])].p);
                const V3 d = (m - work[i].p).dot(slideT[i]) * slideT[i];
                tries.push_back(work[i].p + 0.5 * d);
                for (int j : nb[i])
                {
                    const V3 dir = work[i].p - work[size_t(j)].p;
                    const double L = dir.norm();
                    if (L < 1e-12) continue;
                    const double along = dir.dot(slideT[i]);
                    if (std::abs(along) < 0.5 * L) continue;                                   // (only an edge of the crease itself moves the node along it)
                    const double want = L < 0.7 * target ? 0.8 * target - L : (L > 1.4 * target ? 1.1 * target - L : 0.0);
                    if (want != 0.0) tries.push_back(work[i].p + (along > 0 ? 1.0 : -1.0) * want * slideT[i]);
                }
            }
            else
            {
                V3 m = V3::Zero();
                for (int j : nb[i]) m += work[size_t(j)].p;
                m /= double(nb[i].size());
                V3 pm = V3::Zero();
                for (int ci : inc[i])
                {
                    const int ring[4] = {cells[size_t(ci)].c[0], cells[size_t(ci)].c[1], cells[size_t(ci)].c[3], cells[size_t(ci)].c[2]};
                    int at = 0;
                    for (int q = 0; q < 4; ++q)
                        if (ring[q] == int(i)) at = q;
                    pm += work[size_t(ring[(at + 1) % 4])].p + work[size_t(ring[(at + 3) % 4])].p - work[size_t(ring[(at + 2) % 4])].p;
                }
                pm /= double(std::max<size_t>(1, inc[i].size()));
                auto tangent = [&](V3 d) { return V3(d - d.dot(work[i].n) * work[i].n); };
                tries.push_back(work[i].p + 0.6 * tangent(0.5 * (m + pm) - work[i].p));
                tries.push_back(work[i].p + 0.6 * tangent(m - work[i].p));
                tries.push_back(work[i].p + 0.6 * tangent(pm - work[i].p));
                for (int j : nb[i])
                {
                    const V3 dir = work[i].p - work[size_t(j)].p;
                    const double L = dir.norm();
                    if (L < 1e-12) continue;
                    if (L < 0.7 * target) tries.push_back(work[i].p + tangent(dir / L * (0.9 * target - L)));
                    else if (L > 1.4 * target) tries.push_back(work[i].p - tangent(dir / L * (L - 1.1 * target)));
                }
            }
            for (const V3& t : tries)
            {
                cand.push_back(t);
                who.push_back(int(i));
            }
        }
        if (who.empty()) break;
        std::vector<V3> nrm;
        std::vector<char> ok;
        std::vector<double> fv;
        std::vector<V3> gv;
        S.project(cand, nrm, ok, 4, 1e18, &fv, &gv);
        size_t moved = 0;
        double shift = 0;
        size_t k = 0;
        while (k < who.size())
        {
            const size_t i = size_t(who[k]);
            size_t kEnd = k;
            while (kEnd < who.size() && size_t(who[kEnd]) == i) ++kEnd;
            std::vector<double> turnBefore;
            double energyBefore = 0;
            for (int ci : inc[i])
            {
                turnBefore.push_back(cellTurn(work, cells[size_t(ci)]));
                energyBefore += cellEnergy(work, cells[size_t(ci)], target);
            }
            const V3 oldP = work[i].p, oldN = work[i].n;
            double bestEnergy = energyBefore - 1e-9;
            size_t best = kEnd;
            for (size_t q = k; q < kEnd; ++q)
            {
                const double gn = gv[q].norm();
                if (!(gn > 1e-9) || !std::isfinite(fv[q]) || std::abs(fv[q]) / gn > 0.01 * local[i]) continue;      // not back on the surface
                if ((cand[q] - oldP).norm() > 0.4 * local[i]) continue;
                if (fixed[i] && nrm[q].dot(oldN) < 0.98) continue;                        // (a node on an edge stays on the edge; one on a bend keeps its face's normal)
                work[i].p = cand[q];
                work[i].n = nrm[q];
                bool valid = true;
                for (size_t m = 0; m < inc[i].size() && valid; ++m)
                    if (cellTurn(work, cells[size_t(inc[i][m])]) < std::min(0.17, turnBefore[m]) - 1e-9) valid = false;
                if (valid)
                {
                    double energyAfter = 0;
                    for (int ci : inc[i]) energyAfter += cellEnergy(work, cells[size_t(ci)], target);
                    if (energyAfter < bestEnergy)
                    {
                        bestEnergy = energyAfter;
                        best = q;
                    }
                }
            }
            if (best < kEnd)
            {
                work[i].p = cand[best];
                work[i].n = nrm[best];
                ++moved;
                shift += (cand[best] - oldP).norm() / local[i];
            }
            else
            {
                work[i].p = oldP;
                work[i].n = oldN;
            }
            k = kEnd;
        }
        movedTotal += moved;
        if (moved == 0 || shift / double(moved) < 0.002) break;
    }
    // STRAIGHTENING.  The energy has a dead zone (corners within 15 degrees of a right angle and sides within 1.35 of each other cost nothing), so a row of cells that is nearly straight but
    // rocks back and forth costs nothing and the moves above leave it as it is.  Here every node that is not ON an edge or a corner is drawn towards the mean of its neighbours -- the rows
    // beside a straight row (an edge, or a row already straight) relax into straight lines, elsewhere into smooth ones -- and put back on the surface.  A move is made if it dents no cell,
    // makes no cell worse (by more than a hair), and keeps the node on its own face (its normal); a cell round a thin rim is not touched.  A node whose neighbours move wakes up again
    size_t straightMoves = 0, vertexSnaps = 0;
    int straightRounds = 0;
    if (!std::getenv("FIELDES_SC_NOSTRAIGHT"))
    {
        std::vector<char> skip(N, 0), active(N, 1), slideActive(N, 1);
        for (size_t i = 0; i < N; ++i)
        {
            bool rim = false;
            for (int ci : inc[i])
                if (wrappedCell(work, cells[size_t(ci)])) rim = true;
            if (rim) slideActive[i] = 0;
            if (onFeature[i] || nb[i].size() < 3 || rim)
            {
                skip[i] = 1;
                continue;
            }
        }
        // a move of node i to `np` (normal `nn`) is made when it dents no cell round it, makes none worse by more than a hair; else it is taken back
        auto tryMove = [&](size_t i, const V3& np, const V3& nn, double slack = 0.02) {
            std::vector<double> turnBefore;
            double energyBefore = 0;
            for (int ci : inc[i])
            {
                turnBefore.push_back(cellTurn(work, cells[size_t(ci)]));
                energyBefore += cellEnergy(work, cells[size_t(ci)], target);
            }
            const V3 oldP = work[i].p, oldN = work[i].n;
            work[i].p = np;
            work[i].n = nn;
            bool valid = true;
            for (size_t m = 0; m < inc[i].size() && valid; ++m)
                if (cellTurn(work, cells[size_t(inc[i][m])]) < std::min(0.17, turnBefore[m]) - 1e-9) valid = false;
            if (valid)
            {
                double energyAfter = 0;
                for (int ci : inc[i]) energyAfter += cellEnergy(work, cells[size_t(ci)], target);
                if (energyAfter > energyBefore + slack) valid = false;
            }
            if (!valid)
            {
                work[i].p = oldP;
                work[i].n = oldN;
            }
            return valid;
        };
        // VERTICES BESIDE AN EDGE.  A lattice vertex (the corner every cell lists first) within half a cell of an exact edge node, and not on an edge itself, belongs on it: the lattice line along
        // an edge is straight, and a rim that bends away from it is not, so the lattice point lands a few tenths of a cell beside the rim and the outline is ragged there.  The vertex moves to
        // the nearest edge node when the way to it lies on the surface (the middle of the chord is on it: not across a thin wall, not over a void) and the move is a valid one (no cell dented, none
        // worse by more than a hair); a vertex that has moved is on the edge, and is kept there
        if (!std::getenv("FIELDES_SC_NOVSNAP") && !edgePts.empty())
        {
            PointHash HE(target);
            for (size_t k = 0; k < edgePts.size(); ++k) HE.add(int(k), edgePts[k]);
            std::vector<char> isVertex(N, 0);
            for (const Cell& c : cells) isVertex[size_t(c.c[0])] = 1;
            std::vector<V3> want;
            std::vector<int> who;
            for (size_t i = 0; i < N; ++i)
            {
                if (!isVertex[i] || onFeature[i] || nb[i].size() < 3) continue;
                double best = 0.5 * target;
                int bk = -1;
                HE.near(work[i].p, 1, [&](int k) {
                    const double d = (edgePts[size_t(k)] - work[i].p).norm();
                    if (d < best)
                    {
                        best = d;
                        bk = k;
                    }
                });
                if (bk < 0 || best < 0.02 * target) continue;
                want.push_back(edgePts[size_t(bk)]);
                who.push_back(int(i));
            }
            size_t offSurface = 0, refused = 0;
            const double slack = envNumber("FIELDES_SC_VSNAPTOL", 1.0);          // (the cells round a vertex that moves are evened out by the rounds that follow: a move that dents none may worsen them)
            if (!who.empty())
            {
                std::vector<V3> mids(who.size());
                for (size_t q = 0; q < who.size(); ++q) mids[q] = 0.5 * (want[q] + work[size_t(who[q])].p);
                std::vector<double> mf;
                std::vector<V3> mg;
                S.valueAndGradient(mids, mf, mg);
                for (size_t q = 0; q < who.size(); ++q)
                {
                    const size_t i = size_t(who[q]);
                    const double gn = mg[q].norm();
                    const double len = (want[q] - work[i].p).norm();
                    if (!(gn > 1e-9) || !std::isfinite(mf[q]) || std::abs(mf[q]) / gn > 0.15 * len + 0.005 * target)       // (the way to it is not on the surface)
                    {
                        ++offSurface;
                        continue;
                    }
                    if (tryMove(i, want[q], work[i].n, slack))
                    {
                        ++vertexSnaps;
                        onFeature[i] = 1;
                        skip[i] = 1;
                        for (int j : nb[i]) active[size_t(j)] = 1;
                    }
                    else
                        ++refused;
                }
            }
            if (say) std::fprintf(stderr, "[vsnap] %zu lattice vertices lie within half a cell of an edge node: %zu moved onto it, %zu refused (a cell would be dented or worse), %zu with the way to it off the surface\n", who.size(), vertexSnaps, refused, offSurface);
        }
        stageBegin("straightening the rows of cells");
        for (; straightRounds < 150; ++straightRounds)
        {
            stageSet(double(straightRounds) / 150.0, "round " + std::to_string(straightRounds + 1) + " of at most 150");
            std::vector<V3> cand;
            std::vector<int> who;
            for (size_t i = 0; i < N; ++i)
            {
                if (skip[i] || !active[i]) continue;
                V3 m = V3::Zero();
                for (int j : nb[i]) m += work[size_t(j)].p;
                m /= double(nb[i].size());
                V3 d = m - work[i].p;
                d -= d.dot(work[i].n) * work[i].n;
                const double len = d.norm();
                if (len < 0.003 * local[i])
                {
                    active[i] = 0;
                    continue;
                }
                if (len > 0.5 * local[i]) continue;                                      // (no wobble: the moves above judged it)
                cand.push_back(work[i].p + 0.8 * d);
                who.push_back(int(i));
            }
            size_t moved = 0;
            std::vector<V3> nrm;
            std::vector<char> ok;
            std::vector<double> fv;
            std::vector<V3> gv;
            if (!who.empty()) S.project(cand, nrm, ok, 4, 1e18, &fv, &gv);
            for (size_t q = 0; q < who.size(); ++q)
            {
                const size_t i = size_t(who[q]);
                const double gn = gv[q].norm();
                if (!(gn > 1e-9) || !std::isfinite(fv[q]) || std::abs(fv[q]) / gn > 0.01 * local[i]) continue;      // not back on the surface
                if (nrm[q].dot(work[i].n) < (fixed[i] ? 0.98 : 0.9)) continue;                                      // not on its own face any more
                if ((cand[q] - work[i].p).norm() > 0.5 * local[i]) continue;
                if (tryMove(i, cand[q], nrm[q]))
                {
                    ++moved;
                    for (int j : nb[i]) active[size_t(j)] = 1;
                }
            }
            // Edge nodes on a STRAIGHT stretch (the two neighbours on the edge in line with the node) slide to the middle of the two, so that the rows across the edge are evenly spaced.
            // The middle of two nodes of a straight edge is on the edge; the field and the eight probes say whether it still is (a bent edge, a rim, fails both)
            if (!std::getenv("FIELDES_SC_NOSLIDE"))
            {
                std::vector<V3> sc, snr;
                std::vector<int> sw;
                for (size_t i = 0; i < N; ++i)
                {
                    if (!onFeature[i] || !slideActive[i]) continue;
                    int a = -1, b = -1, cnt = 0;
                    for (int j : nb[i])
                        if (onFeature[size_t(j)])
                        {
                            if (cnt == 0) a = j;
                            else if (cnt == 1) b = j;
                            ++cnt;
                        }
                    if (cnt != 2) continue;
                    const V3 va = work[size_t(a)].p - work[i].p, vb = work[size_t(b)].p - work[i].p;
                    if (va.norm() < 1e-12 || vb.norm() < 1e-12 || va.dot(vb) > -0.985 * va.norm() * vb.norm()) continue;      // (not in line: an edge that turns)
                    const V3 m = 0.5 * (work[size_t(a)].p + work[size_t(b)].p);
                    const double len = (m - work[i].p).norm();
                    if (len < 0.003 * local[i])
                    {
                        slideActive[i] = 0;
                        continue;
                    }
                    if (len > 0.4 * local[i]) continue;
                    sc.push_back(m);
                    snr.push_back(work[i].n);
                    sw.push_back(int(i));
                }
                if (!sc.empty())
                {
                    std::vector<double> sf;
                    std::vector<V3> sg;
                    S.valueAndGradient(sc, sf, sg);
                    const std::vector<char> still = readFeature(sc, snr);
                    for (size_t q = 0; q < sw.size(); ++q)
                    {
                        const size_t i = size_t(sw[q]);
                        const double gn = sg[q].norm();
                        if (!still[q] || !(gn > 1e-9) || !std::isfinite(sf[q]) || std::abs(sf[q]) / gn > 0.004 * local[i]) continue;
                        if (tryMove(i, sc[q], work[i].n))
                        {
                            ++moved;
                            for (int j : nb[i])
                            {
                                slideActive[size_t(j)] = 1;
                                active[size_t(j)] = 1;
                            }
                        }
                    }
                }
            }
            straightMoves += moved;
            if (moved == 0) break;
        }
    }
    const CellQuality after = measureCells(work, cells);
    // the smoothed nodes are kept if no more cells are bad than before
    const bool keep = after.bad <= before.bad;
    if (say)
        std::fprintf(stderr, "[relax] %zu cells, bad %zu (%.1f %%) -> %zu (%.1f %%) after %d rounds, %zu nodes of %zu on sharp edges (%zu of them slide along the edge), %zu nodes read ON an edge or corner and kept in place, %zu straightening moves in %d rounds, %zu vertices moved onto an edge: %s\n", cells.size(),
                     before.bad, 100.0 * double(before.bad) / double(cells.size()), after.bad, 100.0 * double(after.bad) / double(cells.size()), rounds,
                     size_t(std::count(crease.begin(), crease.end(), char(1))), N, sliding, frozen, straightMoves, straightRounds, vertexSnaps, keep ? "kept" : "not kept");
    if (keep) nodes = std::move(work);
}

// ONE attempt at the whole layout, from one placement of the grid the samples are taken on: the cells are in `all`, or `note` says why not
void growAttempt(Field& F, const V3& loIn, const V3& hiIn, double cell, const V3& directionIn, int shift0, bool refineLarge, MapSet& all, V3& firstDirection, std::string& note, bool say)
{
    const V3 axis = directionIn.norm() > 0 ? V3(directionIn.normalized()) : V3(1, 0, 0);
    firstDirection = axis;
    std::vector<FailedPiece> failed;
    bool joined = false, clipped = false, vanished = false;
    std::vector<std::pair<int, int>> cuts;
    mapPass(F, loIn, hiIn, cell, axis, nullptr, 0.0, nullptr, nullptr, 0.0, 0.0, shift0, 0.0, nullptr, &cuts, all, failed, joined, clipped, vanished, say);
    if (!cuts.empty())
    {
        if (say) std::fprintf(stderr, "[cut] %zu hops between the separate surfaces of one piece were cut, and the surface is laid out again\n", cuts.size());
        const std::vector<std::pair<int, int>> use = cuts;
        all = MapSet();
        failed.clear();
        mapPass(F, loIn, hiIn, cell, axis, nullptr, 0.0, nullptr, nullptr, 0.0, 0.0, shift0, 0.0, &use, nullptr, all, failed, joined, clipped, vanished, say);
    }

    // A piece that did not come out as a closed map is sampled finer, on its own, until it does: the cells on it are then smaller than asked (a piece too small for
    // cells of this size cannot have bigger ones).  A piece that proves to be joined to a place that is mapped is covered by that map.  What cannot be resolved is
    // reported, and the whole map is refused: nothing is left out
    const double delta0 = 2.0 * cell / 2.5 / deltaDivisor();
    const int maxHalvings = 12;                 // (a speck of the field that is a thousandth of the cell is still a surface: its box is tiny, so it costs next to nothing)
    const double nodeBudget = 300000.0;         // (a limit of time: a piece of more nodes than this, sampled four times as finely, takes minutes)
    const double minNodes = 4.0 * 2.5 * deltaDivisor() * 2.5 * deltaDivisor();    // (the nodes four regions of the lattice's own size hold: a piece with fewer cannot be judged, so its size is not known from its nodes)
    std::vector<V3> done = all.covered, doneN = all.coveredN;
    std::string unresolved;
    int unresolvedCount = 0;
    for (const FailedPiece& fp0 : failed)
    {
        FailedPiece cur = fp0;                      // the piece as it is known now: at every level it is learned better, and the next level samples only round it
        bool solved = false;
        std::string last = fp0.why;
        if (!refineLarge && double(fp0.nodes) >= minNodes)
        {
            // (a piece that can be judged at this cell size but is not a good map is first tried from other samples, at THIS size; sampled finer its cells would be smaller than asked)
            ++unresolvedCount;
            if (unresolvedCount <= 3)
            {
                char where[160];
                std::snprintf(where, sizeof(where), "%zu nodes near (%.1f, %.1f, %.1f)", fp0.nodes, 0.5 * (fp0.lo[0] + fp0.hi[0]), 0.5 * (fp0.lo[1] + fp0.hi[1]), 0.5 * (fp0.lo[2] + fp0.hi[2]));
                unresolved += std::string(unresolved.empty() ? "" : "; ") + "a piece of " + where + ": " + last;
            }
            continue;
        }
        double known = delta0;                      // the sampling step the piece was last found at (it lies within a step of its nodes)
        // (a piece that can be judged at this cell size may be sampled finer ONCE: cells half as big as asked; a feature that needs smaller cells than that is for the user to decide on)
        const int kLimit = fp0.nodes >= minNodes ? 1 : maxHalvings;
        for (int k = 1; k <= kLimit && !solved; ++k)
        {
            if (double(cur.nodes) >= minNodes && double(cur.nodes) * 4.0 > nodeBudget)
            {
                last += "; sampled twice as finely it would be over " + std::to_string(int(nodeBudget)) + " nodes";
                break;
            }
            const double c2 = cell / std::pow(2.0, k);
            // (a piece that spreads over many steps of the finer sampling -- a leaf of the field a tenth of a millimetre thick and ten across, sampled at a thousandth of a millimetre -- is
            // not looked at: it would be tens of millions of samples for every one of up to twelve halvings, and the run takes an hour)
            if ((cur.hi - cur.lo).maxCoeff() / (2.0 * c2 / 2.5 / deltaDivisor()) > 3000.0)
            {
                last += "; sampled at cell " + std::to_string(c2) + " it would be over 3000 steps across";
                break;
            }
            bool learned = false;
            for (int grow = 0; grow < 4 && !solved && !learned; ++grow)
            {
                const double margin = 2.0 * known * std::pow(2.0, grow);
                // (the box a piece is sampled in may reach a cell and a half beyond the region, as far as a body's own surface may: a body's region is its extent, and a piece that
                // fills it -- a tube, a plate -- would otherwise always be "cut by the box")
                const V3 loLimit = loIn - V3::Constant(1.5 * cell), hiLimit = hiIn + V3::Constant(1.5 * cell);
                const V3 lo = (cur.lo - V3::Constant(margin)).cwiseMax(loLimit), hi = (cur.hi + V3::Constant(margin)).cwiseMin(hiLimit);
                MapSet sub;
                std::vector<FailedPiece> subFailed;
                bool j = false, cl = false, van = false;
                const auto tRefine = std::chrono::steady_clock::now();
                mapPass(F, lo, hi, c2, axis, &cur.seeds, 0.5 * known, &done, &doneN, 0.75 * known, 0.5 * known, shift0, cur.nodes <= 400 ? 1.5 * known : 0.0, nullptr, nullptr, sub, subFailed, j, cl, van, false);
                const V3 mid = 0.5 * (cur.lo + cur.hi);
                if (say)
                    std::fprintf(stderr, "[refine] piece of %zu nodes near (%.2f %.2f %.2f), cell %.4f, box %.3f x %.3f x %.3f: %s (%.1f s)\n", cur.nodes, mid[0], mid[1], mid[2], c2, hi[0] - lo[0], hi[1] - lo[1],
                                 hi[2] - lo[2], j ? "joined to a mapped piece" : cl ? "cut by the box" : !subFailed.empty() ? subFailed.front().why.c_str() : sub.cells.empty() ? "nothing found" : "mapped",
                                 std::chrono::duration<double>(std::chrono::steady_clock::now() - tRefine).count());
                if (van)
                {
                    if (say) std::fprintf(stderr, "[cover] piece of %zu nodes near (%.2f %.2f %.2f) is not a surface: at cell %.4f the field keeps one sign all round it (it came near zero at the coarse sampling and never crossed it)\n", fp0.nodes, mid[0], mid[1], mid[2], c2);
                    solved = true;
                    break;
                }
                if (j)
                {
                    if (say) std::fprintf(stderr, "[cover] piece of %zu nodes near (%.1f %.1f %.1f) is joined to a mapped piece at cell %.4f: that map covers it\n", fp0.nodes, mid[0], mid[1], mid[2], c2);
                    solved = true;
                    break;
                }
                if (cl)
                {
                    if (lo == loLimit && hi == hiLimit)
                    {
                        last = "the piece reaches the edge of the region";
                        break;
                    }
                    continue;                               // cut by the region: a bigger region
                }
                if (subFailed.empty() && !sub.cells.empty())
                {
                    const int base = int(all.nodes.size());
                    all.repairs += sub.repairs;
                    all.repairAt.insert(all.repairAt.end(), sub.repairAt.begin(), sub.repairAt.end());
                    all.edgePoints.insert(all.edgePoints.end(), sub.edgePoints.begin(), sub.edgePoints.end());
                    for (const GNode& g : sub.nodes) all.nodes.push_back(g);
                    for (size_t q = 0; q < sub.covered.size(); ++q)
                    {
                        done.push_back(sub.covered[q]);
                        doneN.push_back(sub.coveredN[q]);
                    }
                    for (Cell c : sub.cells)
                    {
                        for (int m = 0; m < 4; ++m) c.c[m] += base;
                        all.cells.push_back(c);
                    }
                    if (say) std::fprintf(stderr, "[cover] piece of %zu nodes near (%.1f %.1f %.1f) mapped at cell %.4f (%d halvings): %zu quads\n", fp0.nodes, mid[0], mid[1], mid[2], c2, k, sub.cells.size());
                    solved = true;
                    break;
                }
                if (!subFailed.empty())
                {
                    // not yet: learn the piece at this level (where it is, how many nodes) and sample twice as finely round it
                    FailedPiece next = subFailed.front();
                    for (size_t q = 1; q < subFailed.size(); ++q)
                    {
                        next.lo = next.lo.cwiseMin(subFailed[q].lo);
                        next.hi = next.hi.cwiseMax(subFailed[q].hi);
                        next.nodes += subFailed[q].nodes;
                        for (const V3& p : subFailed[q].seeds)
                            if (next.seeds.size() < 400) next.seeds.push_back(p);
                    }
                    last = next.why;
                    cur = next;
                    learned = true;
                }
                else
                    last = "no closed map at cell " + std::to_string(c2);
                break;
            }
            known = 2.0 * c2 / 2.5 / deltaDivisor();
        }
        if (!solved)
        {
            ++unresolvedCount;
            if (unresolvedCount <= 3)
            {
                char where[160];
                std::snprintf(where, sizeof(where), "%zu nodes near (%.1f, %.1f, %.1f)", fp0.nodes, 0.5 * (fp0.lo[0] + fp0.hi[0]), 0.5 * (fp0.lo[1] + fp0.hi[1]), 0.5 * (fp0.lo[2] + fp0.hi[2]));
                unresolved += std::string(unresolved.empty() ? "" : "; ") + "a piece of " + where + ": " + last;
            }
        }
    }
    if (unresolvedCount)
    {
        note = "the cell map does not cover the whole surface, so it is not used: " + std::to_string(unresolvedCount) + " piece(s) of the surface could not be given a closed map of quads (" + unresolved +
               (unresolvedCount > 3 ? "; ..." : "") + ")";
        return;
    }
    if (all.cells.empty())
    {
        note = "the surface gave no lattice at this cell size";
        return;
    }
}

// How a map is connected: the number of its separate surfaces and the Euler number of all of them (vertices - edges + cells)
std::pair<int, long long> mapSignature(const MapSet& m)
{
    std::vector<int> par(m.nodes.size());
    std::iota(par.begin(), par.end(), 0);
    std::function<int(int)> find = [&](int x) {
        while (par[size_t(x)] != x)
        {
            par[size_t(x)] = par[size_t(par[size_t(x)])];
            x = par[size_t(x)];
        }
        return x;
    };
    std::vector<char> used(m.nodes.size(), 0);
    std::set<std::pair<int, int>> edges;
    for (const Cell& c : m.cells)
    {
        const int ring[4] = {c.c[0], c.c[1], c.c[3], c.c[2]};
        for (int k = 0; k < 4; ++k)
        {
            const int a = ring[k], b = ring[(k + 1) % 4];
            used[size_t(a)] = 1;
            par[size_t(find(a))] = find(b);
            edges.insert({std::min(a, b), std::max(a, b)});
        }
    }
    std::set<int> roots;
    long long V = 0;
    for (size_t i = 0; i < used.size(); ++i)
        if (used[i])
        {
            ++V;
            roots.insert(find(int(i)));
        }
    return {int(roots.size()), V - (long long)(edges.size()) + (long long)(m.cells.size())};
}

// What a placement of the samples gave: a closed map of the whole surface, and what it says the surface is
struct Candidate
{
    MapSet map;
    std::pair<int, long long> sig{0, 0};        // the number of separate surfaces and the Euler number (mapSignature)
    bool repaired = false;                      // holes in its faces were closed
    bool finer = false;                         // a piece of it was sampled finer than asked
    bool polished = false;                      // its cells have been evened out (relaxCells) and counted
    size_t forbidden = 0, ugly = 0;             // what is forbidden in it (cells folded over, edges through the air, vertices at one place) and what is only ugly, after the smoothing
};

// How much of the surface lies on features thinner than 0.8 of the cell: from up to 600 of the nodes of the map, the way into the material and the way out are followed (a cell at most) until
// the field changes sign -- the thickness of the wall there, the width of the gap or of the hole -- and the smaller of the two is the size of the feature.  Returns the share of those nodes
// whose feature is under 0.8 of the cell, and puts the smallest size (the second percentile, so that one stray node does not decide) in `smallest`.  A feature thinner than a cell can be
// missing from the map, or extra in it, and the placements of the samples can agree on the wrong answer: the call says so
double thinFeatureShare(Field& F, const std::vector<GNode>& nodes, const std::vector<Cell>& cells, double cell, double& smallest)
{
    smallest = 0.0;
    std::vector<char> used(nodes.size(), 0);
    for (const Cell& c : cells)
        for (int q = 0; q < 4; ++q) used[size_t(c.c[q])] = 1;
    std::vector<size_t> ids;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (used[i]) ids.push_back(i);
    if (ids.empty()) return 0.0;
    const size_t stride = std::max<size_t>(1, ids.size() / 600);
    const double h = 0.04 * cell;
    const int steps = 25;
    std::vector<V3> pts;
    size_t count = 0;
    for (size_t k = 0; k < ids.size(); k += stride)
    {
        const GNode& g = nodes[ids[k]];
        const V3 n = g.n.norm() > 1e-9 ? V3(g.n.normalized()) : V3(0, 0, 1);
        for (int s = 1; s <= steps; ++s)
        {
            pts.push_back(g.p - n * (s * h));
            pts.push_back(g.p + n * (s * h));
        }
        ++count;
    }
    std::vector<double> f;
    std::vector<V3> unused;
    F.eval(pts, f, unused);
    std::vector<double> sizes;
    size_t thin = 0;
    for (size_t w = 0; w < count; ++w)
    {
        double tIn = 1e300, tOut = 1e300;
        for (int s = 1; s <= steps; ++s)
        {
            const size_t base = w * size_t(2 * steps) + size_t(2 * (s - 1));
            if (tIn > 1e299 && f[base] > 0) tIn = s * h;
            if (tOut > 1e299 && f[base + 1] < 0) tOut = s * h;
        }
        const double t = std::min(tIn, tOut);
        if (t < 1e299) sizes.push_back(t);
        if (t < 0.8 * cell) ++thin;
    }
    if (!sizes.empty())
    {
        std::sort(sizes.begin(), sizes.end());
        smallest = sizes[sizes.size() / 50];
    }
    return double(thin) / double(count);
}

// The surface laid out as ONE closed map of quads.  A map is made from several placements of the sample grid and the topology of the results is voted on: a surface the cells
// can resolve gives the same number of surfaces and the same Euler number from every placement, one they cannot (a hole, a wall or a neck about as thin as a cell) gives answers that
// differ with where the samples fall.  Two placements that agree (and no other topology has as many) settle it.  When they do not, a CLOSED MAP THAT COVERS THE WHOLE SURFACE EXISTS
// all the same, and it is delivered, with a warning that says where the doubt is -- a refusal would throw away a map that covers everything.  Only when no placement gave a closed
// map (every piece that is not one is sampled finer on its own first, and holes in the faces are closed) is the layout refused, with an error that names the piece
void growQuads(Field& F, const V3& loIn, const V3& hiIn, double cell, const V3& directionIn, int gridOffset,
               std::vector<GNode>& nodes, PathMap& paths, std::vector<Cell>& cells, V3& firstDirection, std::string& note, std::string& warning)
{
    (void)paths;
    warning.clear();
    const bool say = std::getenv("FIELDES_SC_STATS") != nullptr;
    const char* shiftEnv = std::getenv("FIELDES_SC_SHIFT");                           // (for the developer: overrides grid_offset)
    const int offset = shiftEnv ? std::atoi(shiftEnv) : gridOffset;
    std::vector<Candidate> cands;
    auto polish = [&](Candidate& c) {
        if (c.polished) return;
        Surf checkSurf(F, 0.005 * cell);
        const auto tPolish = std::chrono::steady_clock::now();
        const Defects before = say ? mapDefects(c.map.nodes, c.map.cells, &checkSurf) : Defects();
        if (!std::getenv("FIELDES_SC_NORELAX")) relaxCells(F, c.map.nodes, c.map.cells, c.map.edgePoints, say);
        const Defects after = mapDefects(c.map.nodes, c.map.cells, &checkSurf);
        c.forbidden = after.forbidden();
        c.ugly = after.collapsed + after.longEdges;
        c.polished = true;
        if (say)
        {
            std::fprintf(stderr, "[polish] %zu cells: smoothing and count %.1f s\n", c.map.cells.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - tPolish).count());
            std::fprintf(stderr, "[defects] cells %zu: FORBIDDEN dented/flipped %zu -> %zu, overlapping %zu -> %zu, edge through the air %zu -> %zu, coincident corners %zu -> %zu; ugly: collapsed edges %zu -> %zu, long edges %zu -> %zu, edges cutting a corner %zu -> %zu (before -> after smoothing)\n",
                         c.map.cells.size(), before.dented, after.dented, before.overlapping, after.overlapping, before.offSurface, after.offSurface, before.coincident, after.coincident, before.collapsed,
                         after.collapsed, before.longEdges, after.longEdges, before.cutCorners, after.cutCorners);
        }
    };
    auto deliver = [&](size_t pick) {
        polish(cands[pick]);
        Candidate& c = cands[pick];
        if (const char* defectFile = std::getenv("FIELDES_SC_DEFECTS"))             // (for the developer: which cells are defective, one "cell mark" to a line, for drawing the map)
            if (FILE* fp = std::fopen(defectFile, "w"))
            {
                Surf checkSurf(F, 0.005 * cell);
                const Defects d = mapDefects(c.map.nodes, c.map.cells, &checkSurf);
                for (size_t i = 0; i < d.mark.size(); ++i)
                    if (d.mark[i]) std::fprintf(fp, "%zu %d\n", i, int(d.mark[i]));
                std::fclose(fp);
            }
        std::string w;
        if (c.map.repairs > 0)
        {
            std::string where;
            size_t shown = 0;
            for (const V3& p : c.map.repairAt)
            {
                if (shown >= 3) break;
                char one[90];
                std::snprintf(one, sizeof(one), "%s(%.1f, %.1f, %.1f)", shown ? ", " : "", p[0], p[1], p[2]);
                where += one;
                ++shown;
            }
            w += std::string(w.empty() ? "" : " ") + std::to_string(c.map.repairs) + " face(s) were added where the layout of the surface left a hole (near " + where + "): the cells there are larger or less regular than elsewhere (another grid_offset, or a smaller cell_size, may close them).";
        }
        if (c.finer) w += std::string(w.empty() ? "" : " ") + "A piece of the surface could only be mapped with cells half as big as asked.";
        {
            double smallest = 0.0;
            const double share = thinFeatureShare(F, c.map.nodes, c.map.cells, cell, smallest);
            if (share >= 0.01)
            {
                char said[400];
                std::snprintf(said, sizeof(said), "%.0f %% of the surface lies on walls, gaps or holes thinner than the cell (down to %.3g mm, the cell is %.3g mm): a feature thinner than a cell can be missing from the map, or extra in it, whatever the grid offset. A smaller cell_size resolves it.", 100.0 * share, smallest, cell);
                w += std::string(w.empty() ? "" : " ") + said;
            }
        }
        if (c.forbidden > 0)
            w += std::string(w.empty() ? "" : " ") + std::to_string(c.forbidden) + " cell(s) of the map fold over a neighbour, lie on top of another cell or have an edge through the air: the surface there is finer than the cells (a thin rim, a narrow slot, a tiny hole). A smaller cell_size removes them.";
        // (for the developer: where the holes were closed, FIELDES_SC_REPAIRS=path, one place to a line, for drawing the map)
        if (const char* rp = std::getenv("FIELDES_SC_REPAIRS"))
            if (FILE* fp = std::fopen(rp, "w"))
            {
                for (const V3& p : c.map.repairAt) std::fprintf(fp, "%.4f %.4f %.4f\n", p[0], p[1], p[2]);
                std::fclose(fp);
            }
        warning = w;
        nodes = std::move(c.map.nodes);
        cells = std::move(c.map.cells);
        note.clear();
    };

    // ONE layout, from the sampling grid at the offset asked for.  The surface is first sampled on a grid, and where it has detail about as small as the spacing of the points, where the grid
    // lies decides which points are joined; a part can therefore close with one offset and not with another, or give a different count of holes.  Which offset is the right one is for the
    // user to say (grid_offset, 0 to 3: four fixed grids), who knows the part: the layout does not try the others behind their back.  What it cannot be sure of it says in the warning
    MapSet got;
    std::string n;
    growAttempt(F, loIn, hiIn, cell, directionIn, offset, false, got, firstDirection, n, say);
    if (!n.empty())
    {
        if (n.rfind("the surface gave no lattice", 0) == 0)
        {
            note = n;
            return;
        }
        char advice[420];
        std::snprintf(advice, sizeof(advice), " The layout depends on where the grid of sample points starts and on the size of the cells: try grid_offset=%d (0 to 3 are four different grids) or cell_size about %.3g mm, or change the surface where it is named (a hole, a thin wall or a narrow strip finer than the cells).", (offset + 1) % 4, 0.5 * cell);
        note = n + advice;
        return;
    }
    Candidate c;
    c.sig = mapSignature(got);
    c.repaired = got.repairs > 0;
    c.map = std::move(got);
    cands.push_back(std::move(c));
    if (say) std::fprintf(stderr, "[layout] one layout, grid offset %d: %d separate surfaces, Euler number %lld%s\n", offset, cands[0].sig.first, cands[0].sig.second, cands[0].repaired ? " (holes closed)" : "");
    deliver(0);
}
