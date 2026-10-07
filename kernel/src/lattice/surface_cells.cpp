/*
libfive: a CAD kernel for modeling with implicit functions

A strut lattice, or a periodic surface, laid on a surface made from the field alone; see surface_cells.hpp (fieldCells).

The surface is where the body's field is zero, its normal the field's gradient: nothing else of the body is used -- no mesh of
the body, no distance made from a mesh.  The cells are laid out as ONE MESH OF QUADS, one quad to a cell, in one of two ways (see
surface_quads.inl and quad_layout.inl), chosen by whether the surface goes on past the region it is wanted in:

    a body         (the surface ends inside its box) a CLOSED mesh that covers ALL of the surface, made from a graph of points of the
                   surface: a direction field and a lattice position field over the points, lattice vertices, a face for every
                   three regions that meet, k quads for a face of k corners, then the nodes are evened out over the surface.  Every
                   separate piece of the surface is mapped and checked on its own; a piece too small for cells of this size is
                   sampled finer by itself; what cannot be mapped refuses the whole layout, with the place named
    a sheet        (a surface with no body, cut by the box) a scaffold of marching tetrahedra with the edge the box makes, the same
                   direction and position fields over it, regions of triangles that lie in one square of the lattice, k quads for a
                   region of k corners; the layout has an edge where the box cuts it, and only there

What is in this file besides is what the cells carry: how deep a layer goes from each corner, the edges of every cell as lines on
the surface (a Coons patch of them maps a unit cell onto the cell), the beams of the unit cell over the cells, and the periodic
surface over them.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <functional>
#include <queue>
#include <random>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "libfive/eval/eval_deriv_array.hpp"
#include "libfive/lattice/surface_cells.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_storage.hpp"
#include "libfive/run_progress.hpp"

namespace libfive {
namespace lattice {

using V3 = Eigen::Vector3d;

namespace {

////////////////////////////////////////////////////////////////////////////////
// The field and its gradient

////////////////////////////////////////////////////////////////////////////////
// The bar of the script while cells are laid out.  A whole call of lattice_surface_conform is ONE operation with ONE bar.  The layout goes
// through a fixed list of steps (below), in the order they run; every step has a share of the bar, and inside its share the bar counts what
// the step has really done: levels, rounds, or field evaluations against the number the step is known to need.  The shares say how much of
// a typical call a step takes (percent of a typical call, measured with FIELDES_SC_STAGES=1 on the bracket and the pan -- body -- and on two wavy sheets; another body spends its
// time somewhat differently, so a step can end early or late), the bar never goes back, and it is full when the call is over.

struct StageStep
{
    const char* name;
    double share;           // of the whole call (the list is scaled to add up to 1)
};

// a closed body, in the order the steps run
const StageStep kBodySteps[] = {
    {"sampling the surface", 1.3},
    {"putting the samples on the surface", 8.0},
    {"finding sharp edges and corners", 1.4},
    {"choosing the cell centres", 0.4},
    {"evening out the cell centres", 9.9},
    {"joining the samples into a surface", 33.3},
    {"finding the neighbours of the cell centres", 1.8},
    {"choosing the directions of the rows", 1.1},
    {"placing the rows of cells", 2.0},
    {"placing the corners of the cells", 2.9},
    {"building the faces", 1.0},
    {"cutting the faces into cells", 5.1},
    {"checking the cells", 0.9},
    {"evening out the cells", 15.7},
    {"straightening the rows of cells", 12.9},
    {"measuring how deep the layers can be", 0.7},
    {"making the struts of the cells", 0.4},
};

// a sheet (a surface with no body behind it, cut by the region)
const StageStep kSheetSteps[] = {
    {"making the scaffold of the surface", 5.0},
    {"finding sharp edges and corners", 1.5},
    {"coarsening the scaffold", 4.0},
    {"choosing the directions of the rows", 33.0},
    {"placing the rows of cells", 36.0},
    {"placing the corners of the cells", 0.6},
    {"finding the cells", 18.0},
    {"cutting the cells", 2.0},
    {"measuring how deep the layers can be", 0.2},
    {"making the struts of the cells", 0.7},
};

struct StageState
{
    std::unique_ptr<libfive::run_progress::Task> task;
    const StageStep* steps = nullptr;
    size_t count = 0;
    std::vector<double> begin;      // where each step starts on the bar (0 .. total)
    double total = 1.0;
    int index = -1;                 // the furthest step of the list the layout has reached
    bool mapped = false;            // the stage running is that step (not one that is done over, or none of the list)
    int pass = 1;                   // the layout is made again from the start when the first map needs cutting apart: a second pass
    double base = 0.0, overall = 0.0;   // where the bar was when this pass began, and where it is now
    std::string what, lastDetail;
    double done = 0.0, expect = 0.0, shown = -1.0;
    std::chrono::steady_clock::time_point since;
};
thread_local StageState g_stage;

// FIELDES_SC_STAGES=1: how long each stage took, and the last thing it said (to measure the shares above)
void stageLog()
{
    static const bool on = std::getenv("FIELDES_SC_STAGES") != nullptr;
    if (!on || g_stage.what.empty()) return;
    std::fprintf(stderr, "[stage] %s: %.2f s | %s\n", g_stage.what.c_str(),
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - g_stage.since).count(), g_stage.lastDetail.c_str());
}

template <size_t N>
void stagePlan(const StageStep (&steps)[N])
{
    g_stage.task.reset();
    g_stage.task.reset(new libfive::run_progress::Task("laying out the cells"));
    g_stage.steps = steps;
    g_stage.count = N;
    g_stage.begin.assign(N + 1, 0.0);
    for (size_t i = 0; i < N; ++i) g_stage.begin[i + 1] = g_stage.begin[i] + steps[i].share;
    g_stage.total = g_stage.begin[N];
    g_stage.index = -1;
    g_stage.mapped = false;
    g_stage.pass = 1;
    g_stage.base = g_stage.overall = 0.0;
    g_stage.what.clear();
    g_stage.lastDetail.clear();
    g_stage.task->set(0.0, "laying out the cells");
}

void stageSet(double fraction, const std::string& detail = std::string())
{
    if (!g_stage.task) return;
    fraction = std::min(1.0, std::max(0.0, fraction));
    if (detail.empty() && fraction < 1.0 && fraction - g_stage.shown < 0.004) return;
    g_stage.shown = fraction;
    if (!detail.empty()) g_stage.lastDetail = detail;
    std::string text;
    double overall = 0.0;                           // (a stage done over holds the bar where it is)
    if (g_stage.mapped && g_stage.index >= 0)
    {
        const size_t i = size_t(g_stage.index);
        // (a second pass has the rest of the bar: it goes on from where the first one left it)
        overall = g_stage.base + (1.0 - g_stage.base) * (g_stage.begin[i] + g_stage.steps[i].share * fraction) / g_stage.total;
        text = std::string(g_stage.pass > 1 ? "attempt " + std::to_string(g_stage.pass) + ", " : std::string()) +
               "step " + std::to_string(i + 1) + " of " + std::to_string(g_stage.count) + ": " + g_stage.what;
    }
    else
        text = g_stage.what + " (once more)";
    if (!detail.empty()) text += ": " + detail;
    g_stage.overall = std::max(g_stage.overall, overall);
    g_stage.task->set(overall, text);
}

void stageBegin(const std::string& what, double expectedEvaluations = 0.0)
{
    stageLog();
    if (!g_stage.task) return;                      // (no layout is running its bar)
    int found = -1;
    for (size_t i = 0; i < g_stage.count; ++i)
        if (what == g_stage.steps[i].name)
        {
            found = int(i);
            break;
        }
    if (found == 0 && g_stage.index > 0)            // the first step again: the layout begins over
    {
        g_stage.pass++;
        g_stage.base = g_stage.overall;
        g_stage.index = 0;
    }
    g_stage.mapped = found >= 0 && found >= g_stage.index;
    if (g_stage.mapped) g_stage.index = found;
    g_stage.what = what;
    g_stage.lastDetail.clear();
    g_stage.done = 0.0;
    g_stage.expect = expectedEvaluations;
    g_stage.shown = -1.0;
    g_stage.since = std::chrono::steady_clock::now();
    stageSet(0.0);
}

// from the field, which counts every point it evaluates: the stage that said how many it needs shows how many are done
void stageTick(double evaluations)
{
    if (!g_stage.task || !(g_stage.expect > 0)) return;
    g_stage.done += evaluations;
    stageSet(std::min(0.995, g_stage.done / g_stage.expect));
}

void stageEnd()
{
    stageLog();
    g_stage.what.clear();
    g_stage.task.reset();
}

struct StageGuard
{
    ~StageGuard() { stageEnd(); }
};

class Field
{
public:
    // The field is evaluated by several threads, each with an evaluator of its own (a Deck holds its oracles: one Deck per thread).  A batch of points is cut into runs of
    // ArrayEvaluator::N points, the way a single evaluator would take it, and the threads take the runs one after another, so that every point gets exactly the value and the
    // gradient it would get from one thread alone.  FIELDES_SC_THREADS=n sets the number of threads (1: one thread)
    Field(const Tree& t, const std::map<Tree::Id, float>& vars) : tree_(t.optimized()), vars_(vars), e(tree_, vars)
    {
        unsigned n = std::thread::hardware_concurrency();
        if (const char* env = std::getenv("FIELDES_SC_THREADS")) n = unsigned(std::max(1, std::atoi(env)));
        threads_ = int(std::min(16u, std::max(1u, n)));
    }
    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;
    ~Field() { stopWorkers(); }

    // how many points the field has been evaluated at so far (for the developer)
    unsigned long long evaluated() const { return evaluated_.load(); }

    void eval(const std::vector<V3>& pts, std::vector<double>& f, std::vector<V3>& g)
    {
        evalRaw(pts, f, g);
        // Where two parts of the field are equal (a point exactly on a flat face of a box is where the distance to it, min(0,
        // the largest of three numbers), has its two parts both zero), the gradient is that of whichever part the evaluator
        // meets first -- and that is not the same in every run, so that the points on a face had a gradient of zero in one and
        // not in the next.  A point that has lost its gradient gets it from differences of the field round it.
        std::vector<size_t> lost;
        for (size_t k = 0; k < pts.size(); ++k)
            if (std::isfinite(f[k]) && !(g[k].norm() > 1e-9)) lost.push_back(k);
        if (lost.empty()) return;
        const double h = 0.01;
        std::vector<V3> around;
        around.reserve(6 * lost.size());
        for (size_t k : lost)
            for (int axis = 0; axis < 3; ++axis)
            {
                V3 d = V3::Zero();
                d[axis] = h;
                around.push_back(pts[k] + d);
                around.push_back(pts[k] - d);
            }
        std::vector<double> fa;
        std::vector<V3> ga;
        evalRaw(around, fa, ga);
        for (size_t j = 0; j < lost.size(); ++j)
        {
            const double* v = &fa[6 * j];
            if (!std::isfinite(v[0] + v[1] + v[2] + v[3] + v[4] + v[5])) continue;
            g[lost[j]] = V3((v[0] - v[1]) / (2 * h), (v[2] - v[3]) / (2 * h), (v[4] - v[5]) / (2 * h));
        }
    }

private:
    struct Job
    {
        const V3* pts = nullptr;
        double* f = nullptr;
        V3* g = nullptr;
        size_t n = 0, chunks = 0;
        std::atomic<size_t> next{0}, done{0};
    };

    static void runChunk(DerivArrayEvaluator& ev, const Job& job, size_t c)
    {
        const size_t N = ArrayEvaluator::N;
        const size_t s = c * N;
        const size_t cnt = std::min(N, job.n - s);
        for (size_t k = 0; k < cnt; ++k) ev.set(job.pts[s + k].cast<float>(), k);
        const auto d = ev.derivs(cnt);
        for (size_t k = 0; k < cnt; ++k)
        {
            job.g[s + k] = V3(double(d(0, k)), double(d(1, k)), double(d(2, k)));
            job.f[s + k] = double(d(3, k));
        }
    }

    void work(DerivArrayEvaluator& ev, Job& job)
    {
        for (;;)
        {
            const size_t c = job.next.fetch_add(1);
            if (c >= job.chunks) return;
            runChunk(ev, job, c);
            if (job.done.fetch_add(1) + 1 == job.chunks)
            {
                std::lock_guard<std::mutex> lock(m_);
                cvDone_.notify_all();
            }
        }
    }

    void startWorkers()
    {
        if (started_) return;
        started_ = true;
        // (the evaluators are made one after another here, the way the renderer makes its own: the tree is already optimized)
        for (int i = 1; i < threads_; ++i)
        {
            try
            {
                extra_.push_back(std::make_unique<DerivArrayEvaluator>(tree_, vars_));
            }
            catch (...)
            {
                break;
            }
        }
        for (auto& ev : extra_)
        {
            DerivArrayEvaluator* p = ev.get();
            workers_.emplace_back([this, p] {
                uint64_t seen = 0;
                for (;;)
                {
                    std::shared_ptr<Job> job;
                    {
                        std::unique_lock<std::mutex> lock(m_);
                        cvWork_.wait(lock, [&] { return quit_ || generation_ != seen; });
                        if (quit_) return;
                        seen = generation_;
                        job = job_;
                    }
                    if (job) work(*p, *job);
                }
            });
        }
    }

    void stopWorkers()
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            quit_ = true;
        }
        cvWork_.notify_all();
        for (auto& w : workers_) w.join();
        workers_.clear();
    }

    void evalRaw(const std::vector<V3>& pts, std::vector<double>& f, std::vector<V3>& g)
    {
        f.resize(pts.size());
        g.resize(pts.size());
        evaluated_ += pts.size();
        stageTick(double(pts.size()));
        const size_t N = ArrayEvaluator::N;
        const size_t chunks = (pts.size() + N - 1) / N;
        auto job = std::make_shared<Job>();
        job->pts = pts.data();
        job->f = f.data();
        job->g = g.data();
        job->n = pts.size();
        job->chunks = chunks;
        if (threads_ <= 1 || chunks < 3)
        {
            for (size_t c = 0; c < chunks; ++c) runChunk(e, *job, c);
            return;
        }
        startWorkers();
        {
            std::lock_guard<std::mutex> lock(m_);
            job_ = job;
            ++generation_;
        }
        cvWork_.notify_all();
        work(e, *job);
        std::unique_lock<std::mutex> lock(m_);
        cvDone_.wait(lock, [&] { return job->done.load() == job->chunks; });
    }

    Tree tree_;
    std::map<Tree::Id, float> vars_;
    DerivArrayEvaluator e;
    int threads_ = 1;
    bool started_ = false;
    std::vector<std::unique_ptr<DerivArrayEvaluator>> extra_;
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable cvWork_, cvDone_;
    std::shared_ptr<Job> job_;
    uint64_t generation_ = 0;
    bool quit_ = false;
    std::atomic<unsigned long long> evaluated_{0};
};

////////////////////////////////////////////////////////////////////////////////
// Onto the surface

// Points moved along the gradient to where the field is zero; ok[i] when they got there, n[i] the normal there
void projectToSurface(Field& F, std::vector<V3>& p, std::vector<V3>& n, std::vector<char>& ok, double maxStep, double tol)
{
    ok.assign(p.size(), 0);
    n.assign(p.size(), V3::Zero());
    std::vector<size_t> active(p.size());
    std::iota(active.begin(), active.end(), size_t(0));
    std::vector<V3> pts, g;
    std::vector<double> f;
    for (int it = 0; it < 14 && !active.empty(); ++it)
    {
        pts.clear();
        for (size_t a : active) pts.push_back(p[a]);
        F.eval(pts, f, g);
        std::vector<size_t> next;
        for (size_t k = 0; k < active.size(); ++k)
        {
            const size_t a = active[k];
            const double gn = g[k].norm();
            if (!(gn > 1e-12) || !std::isfinite(f[k])) continue;
            n[a] = g[k] / gn;
            const double dist = f[k] / gn;
            if (std::abs(dist) < tol)
            {
                ok[a] = 1;
                continue;
            }
            p[a] -= n[a] * std::copysign(std::min(std::abs(dist), maxStep), dist);
            next.push_back(a);
        }
        active.swap(next);
    }
}

////////////////////////////////////////////////////////////////////////////////
// The nodes

struct GNode
{
    V3 p = V3::Zero(), n = V3::Zero();  // where it is, and the normal there
    double t = 0;                       // how far the layers reach from it (along `nl`, the way they go): 0 until measured
    V3 nl = V3::Zero();                 // the way the layers go from it: its normal, but at a crease the middle of the two faces'
                                        // (a mitre, so that the layers of the two faces meet); zero until set
};

// A polyline with its length: a point at a fraction of the way along it
struct Poly
{
    std::vector<V3> p;
    std::vector<double> s;          // the length up to each point
    double len = 0;

    Poly() {}
    explicit Poly(std::vector<V3> pts) : p(std::move(pts))
    {
        s.assign(p.size(), 0.0);
        for (size_t i = 1; i < p.size(); ++i) s[i] = s[i - 1] + (p[i] - p[i - 1]).norm();
        len = s.empty() ? 0.0 : s.back();
    }
    V3 at(double u) const
    {
        if (p.size() == 1 || len < 1e-12) return p.front();
        const double x = std::min(1.0, std::max(0.0, u)) * len;
        const size_t i = size_t(std::upper_bound(s.begin(), s.end(), x) - s.begin());
        if (i == 0) return p.front();
        if (i >= p.size()) return p.back();
        const double l = s[i] - s[i - 1];
        const double t = l > 1e-12 ? (x - s[i - 1]) / l : 0.0;
        return p[i - 1] + t * (p[i] - p[i - 1]);
    }
};

// The edges between nodes: the points a step passed, from the lower node to the higher
using PathMap = std::unordered_map<uint64_t, std::vector<V3>>;

uint64_t edgeKey(int a, int b)
{
    return (uint64_t(uint32_t(std::min(a, b))) << 32) | uint32_t(std::max(a, b));
}

// the edge from node a to node b, with the two nodes themselves as its ends
Poly pathPoly(const PathMap& paths, const std::vector<GNode>& nodes, int a, int b)
{
    std::vector<V3> pts;
    const auto it = paths.find(edgeKey(a, b));
    if (it != paths.end())
    {
        pts = it->second;
        if (a > b) std::reverse(pts.begin(), pts.end());
    }
    if (pts.size() < 2) pts = {nodes[size_t(a)].p, nodes[size_t(b)].p};
    pts.front() = nodes[size_t(a)].p;
    pts.back() = nodes[size_t(b)].p;
    return Poly(std::move(pts));
}

////////////////////////////////////////////////////////////////////////////////
// The cells

struct Cell
{
    int c[4];       // the corners [s][t] = c[s + 2t]: a, a + s, a + t, a + s + t (seen from outside, t a quarter turn on from s)
};

// How far a layer reaches from each of the nodes: the depth asked for; one cell for a layer standing out of the surface;
// and for one under it (the lattice that fills a body) as far as the body goes -- found by following the field from the
// node inwards along the normal until it leaves the body, at most `maxDepth`
void measureThickness(Field& F, std::vector<GNode>& nodes, const std::vector<int>& ids, double depth, double cell,
                      double heightDir, double maxDepth)
{
    if (depth > 0)
    {
        for (int i : ids) nodes[size_t(i)].t = depth;
        return;
    }
    if (heightDir > 0)
    {
        for (int i : ids) nodes[size_t(i)].t = cell;
        return;
    }
    // The depth of the body under a node is twice the deepest the field goes along the normal (its distance from the
    // surface, as far as the field is one) -- the way out is not measured: a normal that runs along a face of the body
    // (the field stays a hair inside the whole way) would never leave, and the layer would be many times too deep.
    const size_t N = ids.size();
    std::vector<double> deepest(N, 0.0), thick(N, 0.0);
    std::vector<char> left(N, 0);
    const double step = cell / 8.0;
    const int steps = int(std::ceil(maxDepth / step));
    const double tol = 0.01 * cell;
    // (how deep the field must have gone for a body to have been found: a hundredth of a cell is a hair, a wall thinner than the first
    // step of a cell's eighth is not, and is what a thin shell is)
    const double seen = 0.0025 * cell;
    // The ways along the normal the field is read at: first in finer and finer steps up to a tenth of a cell, so that a wall thinner than
    // an eighth of a cell (a shell, a thin plate: the layer is its thickness) is found as well, then every eighth of a cell.  A wall that
    // the first step already jumped over looked like no body at all, and its layer was given the whole reach, three cells deep, standing
    // out into the air on the other side
    std::vector<double> ways;
    for (double w = cell / 160.0; w < 0.99 * step; w *= 2.0) ways.push_back(w);
    for (int s = 1; s <= steps; ++s) ways.push_back(std::min(maxDepth, s * step));
    std::vector<V3> pts, g;
    std::vector<double> f;
    std::vector<size_t> active(N);
    std::iota(active.begin(), active.end(), size_t(0));
    for (size_t s = 0; s < ways.size() && !active.empty(); ++s)
    {
        pts.clear();
        for (size_t a : active) pts.push_back(nodes[size_t(ids[a])].p + heightDir * nodes[size_t(ids[a])].nl * ways[s]);
        F.eval(pts, f, g);
        std::vector<size_t> next;
        for (size_t k = 0; k < active.size(); ++k)
        {
            const size_t a = active[k];
            const double gn = g[k].norm();
            const double d = gn > 1e-12 && std::isfinite(f[k]) ? -f[k] / gn : 0.0;
            if (d > deepest[a]) deepest[a] = d;
            // (out of the body: the way in has turned round, and is on its way back to zero, after it has been well inside;
            // there the distance falls as fast as the way goes, so the way plus the distance is the whole depth)
            // (at the finest ways a normal that is tilted from the true one is still a long way from "back at zero": the hair `tol` is
            // taken as a quarter of the way there)
            if (deepest[a] > 4.0 * seen && (f[k] >= -std::min(tol, 0.25 * ways[s]) || d < 0.6 * deepest[a]))
            {
                left[a] = 1;
                thick[a] = ways[s] + d;
            }
            else
                next.push_back(a);
        }
        active.swap(next);
    }
    size_t whole = 0;
    for (size_t i = 0; i < N; ++i)
    {
        // (a body that goes on past the reach: the reach; one that ends: twice its deepest, never more than the reach)
        const double t = left[i] ? thick[i] : maxDepth;
        if (!left[i]) ++whole;
        nodes[size_t(ids[i])].t = std::min(maxDepth, std::max(t, 0.0));
    }
    if (std::getenv("FIELDES_SC_STATS"))
        std::fprintf(stderr, "[conform] depth of the body under %zu of %zu corners found; %zu given the whole reach of %.2f mm (the field goes on inside, or no body was found along the normal)\n",
                     N - whole, N, whole, maxDepth);
}

// A depth measured along one normal can be far off where the normal is not true (at a crease, on a facet): each node's
// depth is the middle one of its own and its neighbours' (the nodes that share a cell side with it)
void smoothThickness(std::vector<GNode>& nodes, const std::vector<Cell>& cells, const std::vector<int>& ids)
{
    std::vector<std::vector<int>> nbr(nodes.size());
    for (const Cell& c : cells)
    {
        const int e[4][2] = {{c.c[0], c.c[1]}, {c.c[2], c.c[3]}, {c.c[0], c.c[2]}, {c.c[1], c.c[3]}};
        for (const auto& x : e)
            if (x[0] != x[1])
            {
                nbr[size_t(x[0])].push_back(x[1]);
                nbr[size_t(x[1])].push_back(x[0]);
            }
    }
    std::vector<double> out(ids.size());
    for (size_t k = 0; k < ids.size(); ++k)
    {
        const size_t i = size_t(ids[k]);
        std::sort(nbr[i].begin(), nbr[i].end());
        nbr[i].erase(std::unique(nbr[i].begin(), nbr[i].end()), nbr[i].end());
        std::vector<double> v = {nodes[i].t};
        for (int j : nbr[i])
            if (nodes[size_t(j)].t > 0) v.push_back(nodes[size_t(j)].t);
        std::sort(v.begin(), v.end());
        out[k] = v[(v.size() - 1) / 2];
    }
    for (size_t k = 0; k < ids.size(); ++k) nodes[size_t(ids[k])].t = out[k];
}

// The four edges of a cell as lines along the surface, and its corners
struct CellGeo
{
    Poly e0, e1, f0, f1;        // a to b, c to d, a to c, b to d
    V3 q[4];
};

CellGeo cellGeo(const std::vector<GNode>& nodes, const PathMap& paths, const Cell& c)
{
    CellGeo g;
    g.e0 = pathPoly(paths, nodes, c.c[0], c.c[1]);
    g.e1 = pathPoly(paths, nodes, c.c[2], c.c[3]);
    g.f0 = pathPoly(paths, nodes, c.c[0], c.c[2]);
    g.f1 = pathPoly(paths, nodes, c.c[1], c.c[3]);
    for (int i = 0; i < 4; ++i) g.q[i] = nodes[size_t(c.c[i])].p;
    return g;
}

// A point of a cell at (s, t) in 0..1, found from its four edges (a Coons patch): on a flat cell the bilinear point
// between the corners, on a cell that bends round a lip a point that stays by the surface
V3 coons(const CellGeo& g, double s, double t)
{
    return (1 - t) * g.e0.at(s) + t * g.e1.at(s) + (1 - s) * g.f0.at(t) + s * g.f1.at(t) -
           ((1 - s) * (1 - t) * g.q[0] + s * (1 - t) * g.q[1] + (1 - s) * t * g.q[2] + s * t * g.q[3]);
}

////////////////////////////////////////////////////////////////////////////////
// The cells, laid out as a mesh of quads.  TWO layouts, with different logic, for the two kinds of surface:
//
//   body    the surface of a body is CLOSED (it ends inside its box): the cells are one closed mesh of quads that covers all of it, made from a graph
//           of points of the surface, and every piece of the surface is mapped or the layout is refused (surface_quads.inl)
//   sheet   a surface that is only a surface -- a field that is zero on a sheet with no body behind it, which goes on past the box it is wanted in --
//           has an edge where the box cuts it, and only there: the cells are laid out over a scaffold of it that has that edge (quad_layout.inl)

namespace sheet {
#include "quad_layout.inl"
}

namespace body {
#include "surface_quads.inl"
}

////////////////////////////////////////////////////////////////////////////////
// Beams from the cells

struct PtKey
{
    int64_t a, b, c, d;
    bool operator==(const PtKey& o) const { return a == o.a && b == o.b && c == o.c && d == o.d; }
    bool operator<(const PtKey& o) const { return std::tie(a, b, c, d) < std::tie(o.a, o.b, o.c, o.d); }
};
struct PtKeyHash
{
    size_t operator()(const PtKey& k) const
    {
        return size_t(uint64_t(k.a) * 1099511628211ull ^ uint64_t(k.b) * 73856093ull ^ uint64_t(k.c) * 19349663ull ^
                      uint64_t(k.d) * 83492791ull);
    }
};

bool beamsFromCells(Field& F, const std::vector<GNode>& nodes, const std::vector<Cell>& cells,
                    const std::vector<CellGeo>& geos, double cell, const std::vector<std::array<V3, 2>>& unitBeams,
                    int layers, double inset, double heightDir, double lift, std::vector<V3>& outNodes,
                    std::vector<std::array<int, 2>>& outBeams, SurfaceBeamsInfo& binfo)
{
    outNodes.clear();
    outBeams.clear();
    const double S = 1024.0;
    // Which point of the grid a point of a cell is: the same for the cells that meet there -- a point on a corner column
    // belongs to the node, one on a side to the two nodes of the side, one inside to the cell alone
    auto keyOf = [&](size_t ci, const V3& q, int layer, bool withHeight) {
        const Cell& c = cells[ci];
        const bool sEnd = q[0] < 1e-6 || q[0] > 1 - 1e-6, tEnd = q[1] < 1e-6 || q[1] > 1 - 1e-6;
        const int64_t hq = withHeight ? int64_t(std::llround(S * (double(layer) + q[2]))) : 0;
        const int s = q[0] > 0.5 ? 1 : 0, t = q[1] > 0.5 ? 1 : 0;
        if (sEnd && tEnd) return PtKey{c.c[s + 2 * t], -1, -1, hq};
        if (sEnd || tEnd)
        {
            int i0, i1;
            double par;
            if (sEnd)
            {
                i0 = c.c[s];
                i1 = c.c[s + 2];
                par = q[1];
            }
            else
            {
                i0 = c.c[2 * t];
                i1 = c.c[2 * t + 1];
                par = q[0];
            }
            if (i0 == i1) return PtKey{i0, -1, -1, hq};
            if (i0 > i1)
            {
                std::swap(i0, i1);
                par = 1.0 - par;
            }
            return PtKey{i0, i1, int64_t(std::llround(S * par)), hq};
        }
        return PtKey{-1 - int64_t(ci), int64_t(std::llround(S * q[0])), int64_t(std::llround(S * q[1])), hq};
    };

    // Where a point of a cell lies on the surface: the grid's nodes are on it, the points between them are found from the
    // cell's edges (which run along it) and carried onto it along the field's gradient.  They are found once for the
    // surface position (the same for every layer, and for the cells that share a side).
    std::unordered_map<PtKey, size_t, PtKeyHash> surfIndex;
    std::vector<V3> surfPos, surfNormal;
    std::vector<size_t> surfOwner;          // the cell each of them is a point of
    std::vector<char> surfCorner;           // (a corner of a cell keeps the node's lift normal)
    for (size_t ci = 0; ci < cells.size(); ++ci)
    {
        const Cell& c = cells[ci];
        const GNode* r[2][2] = {{&nodes[size_t(c.c[0])], &nodes[size_t(c.c[2])]}, {&nodes[size_t(c.c[1])], &nodes[size_t(c.c[3])]}};
        for (const auto& ub : unitBeams)
            for (int e = 0; e < 2; ++e)
            {
                const V3& q = ub[size_t(e)];
                const PtKey sk = keyOf(ci, q, 0, false);
                if (surfIndex.count(sk)) continue;
                V3 nn = V3::Zero();
                for (int s = 0; s < 2; ++s)
                    for (int t = 0; t < 2; ++t) nn += (s ? q[0] : 1 - q[0]) * (t ? q[1] : 1 - q[1]) * r[s][t]->nl;
                surfIndex[sk] = surfPos.size();
                surfPos.push_back(coons(geos[ci], q[0], q[1]));
                surfOwner.push_back(ci);
                surfNormal.push_back(nn.norm() > 1e-9 ? V3(nn.normalized()) : r[0][0]->nl);
                surfCorner.push_back((q[0] < 1e-6 || q[0] > 1 - 1e-6) && (q[1] < 1e-6 || q[1] > 1 - 1e-6) ? 1 : 0);
            }
    }
    {
        std::vector<V3> moved = surfPos, normals;
        std::vector<char> ok;
        projectToSurface(F, moved, normals, ok, 0.5 * cell, 1e-4);
        std::vector<size_t> failed;
        for (size_t i = 0; i < surfPos.size(); ++i)
            if (ok[i] && (moved[i] - surfPos[i]).norm() < 0.6 * cell)         // (a point that went further is on another sheet)
            {
                surfPos[i] = moved[i];
                if (!surfCorner[i]) surfNormal[i] = normals[i];
            }
            else
                failed.push_back(i);
        // A point of a cell is ON the surface, always.  The interior of a cell is interpolated from its four edges, so a cell that
        // is bent (round a hole, over a rim) has points off the surface that the short reach above does not bring back.  They get a
        // longer reach; and a point that still does not get there is the nearest corner of its own cell, which is a node of the
        // layout and so on the surface.  There is no way for a point to stay where it is.
        size_t longer = 0, corner = 0;
        if (!failed.empty())
        {
            std::vector<V3> m2, n2;
            for (size_t i : failed) m2.push_back(surfPos[i]);
            std::vector<char> ok2;
            projectToSurface(F, m2, n2, ok2, cell, 1e-4);
            for (size_t k = 0; k < failed.size(); ++k)
            {
                const size_t i = failed[k];
                if (ok2[k] && (m2[k] - surfPos[i]).norm() < 1.5 * cell && n2[k].dot(surfNormal[i]) > 0.3)
                {
                    surfPos[i] = m2[k];
                    if (!surfCorner[i]) surfNormal[i] = n2[k];
                    ++longer;
                }
                else
                {
                    const Cell& c = cells[surfOwner[i]];
                    int best = c.c[0];
                    for (int s = 1; s < 4; ++s)
                        if ((nodes[size_t(c.c[s])].p - surfPos[i]).squaredNorm() < (nodes[size_t(best)].p - surfPos[i]).squaredNorm()) best = c.c[s];
                    surfPos[i] = nodes[size_t(best)].p;
                    surfNormal[i] = nodes[size_t(best)].nl;
                    ++corner;
                }
            }
        }
        if (std::getenv("FIELDES_SC_STATS") && !failed.empty())
            std::fprintf(stderr, "[conform] %zu of %zu points of the cells were off the surface after the short reach: %zu came back with a longer one, %zu went to a corner of their cell\n",
                         failed.size(), surfPos.size(), longer, corner);
    }
    std::unordered_map<PtKey, int, PtKeyHash> ids;
    std::vector<std::pair<PtKey, V3>> pts;
    std::vector<V3> ptBase, ptDir;                  // each point's line: the surface point it stands on, and the way it goes (its reach is
    std::vector<double> ptReach;                    // the length of the line, signed by the way the layers go)
    std::unordered_set<uint64_t> seen;
    std::vector<std::array<int, 2>> beamList;
    for (size_t ci = 0; ci < cells.size(); ++ci)
    {
        const Cell& c = cells[ci];
        const GNode* r[2][2] = {{&nodes[size_t(c.c[0])], &nodes[size_t(c.c[2])]}, {&nodes[size_t(c.c[1])], &nodes[size_t(c.c[3])]}};   // [s][t]
        ++binfo.cells;
        for (int layer = 0; layer < layers; ++layer)
        {
            auto place = [&](const V3& q) {
                const PtKey key = keyOf(ci, q, layer, true);
                const auto it = ids.find(key);
                if (it != ids.end()) return it->second;
                // on the surface under it, and up along the normal there by the layer's depth between the four corners
                const size_t si = surfIndex.at(keyOf(ci, q, 0, false));
                double t = 0;
                for (int s = 0; s < 2; ++s)
                    for (int tt = 0; tt < 2; ++tt) t += (s ? q[0] : 1 - q[0]) * (tt ? q[1] : 1 - q[1]) * r[s][tt]->t;
                // (a layer thinner than its struts keeps them inside as far as it can)
                const double ins = std::min(inset, 0.3 * t);
                const double h = heightDir * (lift + ins + (layer + q[2]) * (t - 2.0 * ins) / layers);
                const V3 x = surfPos[si] + h * surfNormal[si];
                const int id = int(pts.size());
                ids.emplace(key, id);
                pts.emplace_back(key, x);
                ptBase.push_back(surfPos[si]);
                ptDir.push_back(surfNormal[si]);
                ptReach.push_back(h);
                return id;
            };
            for (const auto& ub : unitBeams)
            {
                const int a = place(ub[0]), b = place(ub[1]);
                if (a == b) continue;
                const uint64_t bk = (uint64_t(uint32_t(std::min(a, b))) << 32) | uint32_t(std::max(a, b));
                if (seen.insert(bk).second) beamList.push_back({a, b});
            }
        }
    }
    if (beamList.empty()) return false;
    // A layer inside the body stays inside it, one that stands out of it stays out: a point that came out on the wrong side (at
    // a cell with a bent corner, a rim, a hole) is taken back along its own line as far as it is on the right side, found by
    // halving.  No strut can end in the air of a hole.  (Not for layers that are lifted: they were put partly on the other side of
    // the surface on purpose, and the caller cuts them there.)
    if (heightDir != 0 && lift == 0)
    {
        std::vector<V3> at(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) at[i] = pts[i].second;
        std::vector<double> f;
        std::vector<V3> g;
        F.eval(at, f, g);
        auto wrong = [&](double v) { return heightDir < 0 ? v > 0.02 : v < -0.02; };
        std::vector<size_t> bad;
        for (size_t i = 0; i < pts.size(); ++i)
            if (std::isfinite(f[i]) && wrong(f[i])) bad.push_back(i);
        if (!bad.empty())
        {
            std::vector<double> lo(bad.size(), 0.0), hi(bad.size(), 1.0);
            for (int it = 0; it < 10; ++it)
            {
                std::vector<V3> mid(bad.size());
                for (size_t k = 0; k < bad.size(); ++k) mid[k] = ptBase[bad[k]] + 0.5 * (lo[k] + hi[k]) * ptReach[bad[k]] * ptDir[bad[k]];
                std::vector<double> fm;
                std::vector<V3> gm;
                F.eval(mid, fm, gm);
                for (size_t k = 0; k < bad.size(); ++k)
                {
                    if (std::isfinite(fm[k]) && wrong(fm[k])) hi[k] = 0.5 * (lo[k] + hi[k]);
                    else lo[k] = 0.5 * (lo[k] + hi[k]);
                }
            }
            for (size_t k = 0; k < bad.size(); ++k) pts[bad[k]].second = ptBase[bad[k]] + lo[k] * ptReach[bad[k]] * ptDir[bad[k]];
            if (std::getenv("FIELDES_SC_STATS")) std::fprintf(stderr, "[conform] %zu of %zu points of the layers were out of the material and were taken back\n", bad.size(), pts.size());
        }
    }
    // (in an order that depends on the grid alone)
    std::vector<size_t> order(pts.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return pts[x].first < pts[y].first; });
    std::vector<int> remap(pts.size());
    for (size_t i = 0; i < order.size(); ++i)
    {
        remap[order[i]] = int(i);
        outNodes.push_back(pts[order[i]].second);
    }
    for (auto& b : beamList)
    {
        int x = remap[size_t(b[0])], y = remap[size_t(b[1])];
        if (x > y) std::swap(x, y);
        outBeams.push_back({x, y});
    }
    std::sort(outBeams.begin(), outBeams.end());
    binfo.nodes = outNodes.size();
    binfo.beams = outBeams.size();
    return true;
}

// The way the layers go from each node: the middle of the normals of the cells that meet there (a cell's normal at a corner
// is its way along s crossed with its way along t).  On a smooth surface that is the node's normal; at a crease, where the
// node has the normal of the face beyond it, the cells of both faces count, and the layers of the two meet in a mitre.
void liftNormals(std::vector<GNode>& nodes, const PathMap& paths, const std::vector<Cell>& cells)
{
    std::vector<V3> sum(nodes.size(), V3::Zero());
    for (const Cell& c : cells)
    {
        const Poly e0 = pathPoly(paths, nodes, c.c[0], c.c[1]), e1 = pathPoly(paths, nodes, c.c[2], c.c[3]);
        const Poly f0 = pathPoly(paths, nodes, c.c[0], c.c[2]), f1 = pathPoly(paths, nodes, c.c[1], c.c[3]);
        auto startDir = [](const Poly& p) { return p.p.size() >= 2 ? V3(p.p[1] - p.p[0]) : V3(V3::Zero()); };
        auto endDir = [](const Poly& p) {
            const size_t n = p.p.size();
            return n >= 2 ? V3(p.p[n - 1] - p.p[n - 2]) : V3(V3::Zero());
        };
        struct Corner
        {
            int node;
            V3 ds, dt;
        };
        const Corner corner[4] = {{c.c[0], startDir(e0), startDir(f0)},
                                  {c.c[1], endDir(e0), startDir(f1)},
                                  {c.c[2], startDir(e1), endDir(f0)},
                                  {c.c[3], endDir(e1), endDir(f1)}};
        for (const Corner& k : corner)
        {
            if (!(k.ds.norm() > 1e-9) || !(k.dt.norm() > 1e-9)) continue;           // (a side that is a point)
            V3 n = k.ds.normalized().cross(k.dt.normalized());
            if (n.norm() < 0.3) continue;                                          // (the two sides along one line)
            n.normalize();
            if (n.dot(nodes[size_t(k.node)].n) < -0.2) continue;                  // (turned over)
            sum[size_t(k.node)] += n;
        }
    }
    for (size_t i = 0; i < nodes.size(); ++i)
        nodes[i].nl = sum[i].norm() > 1e-6 ? V3(sum[i].normalized()) : nodes[i].n;
}

// THE MATERIAL IS FILLED ONCE.  A body with side='inside' is filled from its surface inward, as deep as the body is under each cell: on a thin body (the skin of a part, a plate)
// the far end of that column is a point of the opposite face, and the cells of that face fill the same slab from the other side -- the lattice would be doubled.  A cell is
// left out only when NOTHING is lost by it: when every one of a grid of points of its column that lies in the material is also in the column of another cell that stays (a
// cell that covers a point for one that was left out is itself kept for good).  Of two cells that cover each other the one on the smaller surface is the first to go, and on a
// surface that is one piece (a plate) the one that faces away from a fixed direction.  A thick body is not touched: its columns end inside the material, where there is no cell,
// and what a cell covers nobody else does.  The column of a cell is the hexahedron of its four corners and the four points a layer's depth below them along the lifted normals
size_t dropRedundantLayers(Field& F, std::vector<GNode>& nodes, std::vector<Cell>& cells, double cell)
{
    const size_t C = cells.size();
    if (C == 0) return 0;
    std::vector<int> par(nodes.size());
    std::iota(par.begin(), par.end(), 0);
    std::function<int(int)> find = [&](int x) {
        while (par[size_t(x)] != x)
        {
            par[size_t(x)] = par[size_t(par[size_t(x)])];
            x = par[size_t(x)];
        }
        return x;
    };
    for (const Cell& c : cells)
        for (int m = 1; m < 4; ++m) par[size_t(find(c.c[0]))] = find(c.c[m]);
    struct Column
    {
        V3 H[8];                                    // the corners, index = u + 2 v + 4 w (w = 1: the far end of the layer)
        V3 lo, hi, mid, normal;
        double size = 0, depth = 0, area = 0;
        int piece = 0;
    };
    std::vector<Column> col(C);
    std::map<int, double> pieceArea;
    for (size_t k = 0; k < C; ++k)
    {
        Column& f = col[k];
        V3 nl = V3::Zero();
        double t = 0;
        for (int m = 0; m < 4; ++m)
        {
            const GNode& g = nodes[size_t(cells[k].c[m])];
            const V3 d = g.nl.norm() > 1e-9 ? V3(g.nl.normalized()) : V3(g.n.normalized());
            f.H[m] = g.p;                                         // (u = m & 1, v = m >> 1, w = 0)
            f.H[m + 4] = g.p - g.t * d;
            nl += d;
            t += 0.25 * g.t;
        }
        f.normal = nl.norm() > 1e-12 ? V3(nl.normalized()) : V3(0, 0, 1);
        f.depth = t;
        f.lo = f.hi = f.H[0];
        for (int m = 1; m < 8; ++m)
        {
            f.lo = f.lo.cwiseMin(f.H[m]);
            f.hi = f.hi.cwiseMax(f.H[m]);
        }
        f.mid = 0.5 * (f.lo + f.hi);
        const V3 a = f.H[3] - f.H[0], b = f.H[2] - f.H[1];
        f.area = 0.5 * a.cross(b).norm();
        f.size = std::sqrt(std::max(f.area, 1e-12));
        f.piece = find(cells[k].c[0]);
        pieceArea[f.piece] += f.area;
    }
    // is the point inside the column?  (the trilinear map of the hexahedron, inverted by Newton's method)
    auto inside = [&](const Column& g, const V3& p) {
        if ((p.array() < g.lo.array() - 0.05 * g.size).any() || (p.array() > g.hi.array() + 0.05 * g.size).any()) return false;
        V3 x(0.5, 0.5, 0.5);
        for (int it = 0; it < 14; ++it)
        {
            V3 X = V3::Zero();
            Eigen::Matrix3d J = Eigen::Matrix3d::Zero();
            for (int k = 0; k < 8; ++k)
            {
                const int ku = k & 1, kv = (k >> 1) & 1, kw = (k >> 2) & 1;
                const double fu = ku ? x[0] : 1 - x[0], fv = kv ? x[1] : 1 - x[1], fw = kw ? x[2] : 1 - x[2];
                const double du = ku ? 1.0 : -1.0, dv = kv ? 1.0 : -1.0, dw = kw ? 1.0 : -1.0;
                X += fu * fv * fw * g.H[k];
                J.col(0) += du * fv * fw * g.H[k];
                J.col(1) += fu * dv * fw * g.H[k];
                J.col(2) += fu * fv * dw * g.H[k];
            }
            const V3 r = X - p;
            if (r.squaredNorm() < 1e-10 * g.size * g.size) break;
            if (!(std::abs(J.determinant()) > 1e-12 * g.size * g.size * g.size)) return false;
            x -= J.inverse() * r;
            x = x.cwiseMax(V3::Constant(-0.5)).cwiseMin(V3::Constant(1.5));
        }
        const double tol = 0.04;
        return x[0] >= -tol && x[0] <= 1 + tol && x[1] >= -tol && x[1] <= 1 + tol && x[2] >= -tol && x[2] <= 1 + tol;
    };
    const V3 ref = V3(0.3, 0.5, 0.8).normalized();
    // the order the cells are tried in for leaving out: the smallest surface piece first, and on equal pieces the cell that faces away from `ref`
    std::vector<size_t> order(C);
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const double pa = pieceArea[col[a].piece], pb = pieceArea[col[b].piece];
        if (std::abs(pa - pb) > 1e-9 * std::max(pa, pb)) return pa < pb;
        return col[a].normal.dot(ref) < col[b].normal.dot(ref);
    });
    // where the columns are: a hash of their middles, a cell of three cell sizes (a column is never more than that across)
    body::PointHash H(3.0 * cell);
    for (size_t k = 0; k < C; ++k) H.add(int(k), col[k].mid);
    // the points of every column that could be left out: a grid of three by three by three, those in the material
    std::vector<size_t> candidates;
    std::vector<V3> pts;
    for (size_t k : order)
    {
        if (col[k].depth <= 0.3 * col[k].size) continue;
        candidates.push_back(k);
    }
    for (size_t k : candidates)
    {
        const Column& f = col[k];
        for (double w : {1.0 / 6, 0.5, 5.0 / 6})
            for (double v : {1.0 / 6, 0.5, 5.0 / 6})
                for (double u : {1.0 / 6, 0.5, 5.0 / 6})
                {
                    V3 p = V3::Zero();
                    for (int m = 0; m < 8; ++m)
                        p += ((m & 1) ? u : 1 - u) * (((m >> 1) & 1) ? v : 1 - v) * (((m >> 2) & 1) ? w : 1 - w) * f.H[m];
                    pts.push_back(p);
                }
    }
    std::vector<double> fv;
    std::vector<V3> gv;
    if (!pts.empty()) F.eval(pts, fv, gv);
    std::vector<char> dropped(C, 0), keepForGood(C, 0);
    for (size_t ci = 0; ci < candidates.size(); ++ci)
    {
        const size_t k = candidates[ci];
        if (keepForGood[k]) continue;
        std::vector<size_t> cover;
        bool all = true;
        size_t any = 0;
        for (size_t s = 0; s < 27 && all; ++s)
        {
            const size_t at = ci * 27 + s;
            if (!(fv[at] < 0.0)) continue;                         // (not in the material: nothing to cover there)
            ++any;
            bool got = false;
            H.near(pts[at], 1, [&](int j) {
                if (got || size_t(j) == k || dropped[size_t(j)]) return;
                if (inside(col[size_t(j)], pts[at]))
                {
                    got = true;
                    cover.push_back(size_t(j));
                }
            });
            if (!got) all = false;
        }
        if (!all || any == 0) continue;
        dropped[k] = 1;
        for (size_t j : cover) keepForGood[j] = 1;
    }
    std::vector<Cell> kept;
    for (size_t k = 0; k < C; ++k)
        if (!dropped[k]) kept.push_back(cells[k]);
    const size_t gone = C - kept.size();
    cells.swap(kept);
    return gone;
}

////////////////////////////////////////////////////////////////////////////////
// The cells of a surface, made once

struct CellSet
{
    std::vector<GNode> nodes;
    PathMap paths;
    std::vector<Cell> cells;
    std::vector<CellGeo> geos;
    V3 usedDirection = V3::Zero();
    double typical = 0;             // how deep the layers are, typically
    std::string warning;            // what the user should be told about the layout (a map whose topology is not certain, holes that were closed)
};

// The cells over the surface of the field -- one closed mesh of quads -- how deep a layer goes from each corner, and the edges of
// every cell
// Which layout lays the cells out is TOLD, not guessed from the field: 0 a body (a closed surface: one closed mesh of quads that covers all
// of it), 1 a sheet (a surface that is only a surface, cut at the box it is wanted in), 2 a patch of a surface (a selection: the sheet
// layout over the part of the surface by the field `region`, which is negative where the patch is).  A selection is tiled as the surface it
// is, whatever the rest of the surface of the thing it was picked on does
bool makeCells(Field& F, const V3& lo, const V3& hi, const V3& directionIn, int gridOffset, int layout, Field* region, double cell, double depth,
               double heightDir, CellSet& out, std::string& error)
{
    const bool say = std::getenv("FIELDES_SC_STATS") != nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    auto lap = [&](const char* what) {
        if (say)
            std::fprintf(stderr, "[conform] %s: %.2f s\n", what,
                         std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    };
    std::string note;
    out.usedDirection = V3::Zero();
    if (layout != 0)
    {
        sheet::PatchMask mask;
        if (layout == 2)
        {
            if (!region)
            {
                error = "a patch of a surface needs the field that says where the patch is";
                return false;
            }
            mask.region = region;
            mask.slack = cell / 6.0;                    // (the edge of the scaffold follows the patch's to within half a step of its grid)
        }
        if (say) std::fprintf(stderr, "[conform] the %s layout\n", layout == 2 ? "patch (the sheet layout over a patch of a surface)" : "sheet");
        stagePlan(kSheetSteps);
        sheet::growQuads(F, lo, hi, cell, directionIn, out.nodes, out.paths, out.cells, out.usedDirection, note, mask);
    }
    else
    {
        if (say) std::fprintf(stderr, "[conform] the body layout\n");
        stagePlan(kBodySteps);
        body::growQuads(F, lo, hi, cell, directionIn, gridOffset, out.nodes, out.paths, out.cells, out.usedDirection, note, out.warning);
    }
    const double maxDepth = 3.0 * cell;
    lap("laid out");
    if (!note.empty())
    {
        error = note;
        return false;
    }
    if (out.nodes.empty())
    {
        error = layout == 2 ? "the selected surface is smaller than a cell of this size (or too narrow for one): use a smaller cell_size"
                            : "no surface was found in the region";
        return false;
    }
    if (out.cells.empty())
    {
        error = "no cell was made: the surface is smaller than a cell";
        return false;
    }
    liftNormals(out.nodes, out.paths, out.cells);
    // how deep the layers go from the corners of the cells
    std::vector<int> corners;
    {
        std::vector<char> mark(out.nodes.size(), 0);
        for (const Cell& c : out.cells)
            for (int q = 0; q < 4; ++q) mark[size_t(c.c[q])] = 1;
        for (size_t i = 0; i < out.nodes.size(); ++i)
            if (mark[i]) corners.push_back(int(i));
    }
    stageBegin("measuring how deep the layers can be");
    measureThickness(F, out.nodes, corners, depth, cell, heightDir, maxDepth);
    smoothThickness(out.nodes, out.cells, corners);
    if (heightDir < 0 && !std::getenv("FIELDES_SC_NODEDUP"))                 // (for the developer: the topology of the whole map is measured with this set)
    {
        const size_t dropped = dropRedundantLayers(F, out.nodes, out.cells, cell);
        if (say && dropped) std::fprintf(stderr, "[conform] %zu cells whose layers are all filled by other cells were left out (the material is filled once)\n", dropped);
    }
    lap("thickness");
    out.geos.assign(out.cells.size(), CellGeo());
    for (size_t i = 0; i < out.cells.size(); ++i) out.geos[i] = cellGeo(out.nodes, out.paths, out.cells[i]);

    // (for the developer: the layout written to a file, FIELDES_SC_DUMP=path: the nodes with their normals, the cells' corners)
    if (const char* dump = std::getenv("FIELDES_SC_DUMP"))
        if (FILE* fp = std::fopen(dump, "w"))
        {
            std::fprintf(fp, "nodes %zu\n", out.nodes.size());
            for (const GNode& g : out.nodes)
                std::fprintf(fp, "%.4f %.4f %.4f %.4f %.4f %.4f %.3f\n", g.p[0], g.p[1], g.p[2], g.n[0], g.n[1], g.n[2], g.t);
            std::fprintf(fp, "cells %zu\n", out.cells.size());
            for (size_t i = 0; i < out.cells.size(); ++i)
                std::fprintf(fp, "%d %d %d %d\n", out.cells[i].c[0], out.cells[i].c[1], out.cells[i].c[2], out.cells[i].c[3]);
            std::fclose(fp);
        }
    std::vector<double> ts;
    for (const Cell& c : out.cells)
        for (int q = 0; q < 4; ++q) ts.push_back(out.nodes[size_t(c.c[q])].t);
    std::nth_element(ts.begin(), ts.begin() + ts.size() / 2, ts.end());
    out.typical = ts[ts.size() / 2];
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// A periodic surface (TPMS) laid on the cells

const double kTwoPi = 6.283185307179586;

// The periodic functions, in radians, one period every 2 pi; the same ones as the library's TPMS (lattices.py).  Returns the
// value and puts the derivatives with respect to a, b and c in `g`.
double tpmsGrad(int kind, double a, double b, double c, V3& g)
{
    const double sa = std::sin(a), sb = std::sin(b), sc = std::sin(c), ca = std::cos(a), cb = std::cos(b), cc = std::cos(c);
    const double s2a = 2 * sa * ca, s2b = 2 * sb * cb, s2c = 2 * sc * cc;
    const double c2a = ca * ca - sa * sa, c2b = cb * cb - sb * sb, c2c = cc * cc - sc * sc;
    switch (kind)
    {
    case 1:                                                                                       // Schwarz P
        g = V3(-sa, -sb, -sc);
        return ca + cb + cc;
    case 2:                                                                                       // diamond
        g = V3(ca * sb * sc + ca * cb * cc - sa * sb * cc - sa * cb * sc,
               sa * cb * sc - sa * sb * cc + ca * cb * cc - ca * sb * sc,
               sa * sb * cc - sa * cb * sc - ca * sb * sc + ca * cb * cc);
        return sa * sb * sc + sa * cb * cc + ca * sb * cc + ca * cb * sc;
    case 3:                                                                                       // Neovius
        g = V3(-3.0 * sa - 4.0 * sa * cb * cc, -3.0 * sb - 4.0 * ca * sb * cc, -3.0 * sc - 4.0 * ca * cb * sc);
        return 3.0 * (ca + cb + cc) + 4.0 * ca * cb * cc;
    case 4:                                                                                       // Lidinoid
        g = V3(0.5 * (2.0 * c2a * cb * sc + s2b * cc * ca - s2c * sa * sb) + s2a * (c2b + c2c),
               0.5 * (-s2a * sb * sc + 2.0 * c2b * cc * sa + s2c * ca * cb) + s2b * (c2a + c2c),
               0.5 * (s2a * cb * cc - s2b * sc * sa + 2.0 * c2c * ca * sb) + s2c * (c2a + c2b));
        return 0.5 * (s2a * cb * sc + s2b * cc * sa + s2c * ca * sb) - 0.5 * (c2a * c2b + c2b * c2c + c2c * c2a) + 0.15;
    case 5:                                                                                       // split P
        g = V3(1.1 * (2.0 * c2a * sc * cb + s2b * ca * cc - s2c * sb * sa) + 0.4 * s2a * (c2b + c2c) + 0.8 * s2a,
               1.1 * (-s2a * sc * sb + 2.0 * c2b * sa * cc + s2c * ca * cb) + 0.4 * s2b * (c2a + c2c) + 0.8 * s2b,
               1.1 * (s2a * cc * cb - s2b * sa * sc + 2.0 * c2c * sb * ca) + 0.4 * s2c * (c2a + c2b) + 0.8 * s2c);
        return 1.1 * (s2a * sc * cb + s2b * sa * cc + s2c * sb * ca) - 0.2 * (c2a * c2b + c2b * c2c + c2c * c2a) -
               0.4 * (c2a + c2b + c2c);
    case 6:                                                                                       // IWP
        g = V3(-2.0 * sa * (cb + cc) + 2.0 * s2a, -2.0 * sb * (ca + cc) + 2.0 * s2b, -2.0 * sc * (cb + ca) + 2.0 * s2c);
        return 2.0 * (ca * cb + cb * cc + cc * ca) - (c2a + c2b + c2c);
    case 7:                                                                                       // FRD
        g = V3(-4.0 * sa * cb * cc + 2.0 * s2a * (c2b + c2c), -4.0 * ca * sb * cc + 2.0 * s2b * (c2a + c2c),
               -4.0 * ca * cb * sc + 2.0 * s2c * (c2a + c2b));
        return 4.0 * ca * cb * cc - (c2a * c2b + c2b * c2c + c2c * c2a);
    case 8:                                                                                       // Fischer-Koch S
        g = V3(-2.0 * s2a * sb * cc - sa * c2b * sc + ca * cb * c2c, c2a * cb * cc - 2.0 * ca * s2b * sc - sa * sb * c2c,
               -c2a * sb * sc + ca * c2b * cc - 2.0 * sa * cb * s2c);
        return c2a * sb * cc + ca * c2b * sc + sa * cb * c2c;
    default:                                                                                      // gyroid
        g = V3(ca * cb - sc * sa, -sa * sb + cb * cc, -sb * sc + cc * ca);
        return sa * cb + sb * cc + sc * ca;
    }
}

struct MapCell
{
    V3 q[4];                // the corners a, b, c, d
    V3 ctl[4][3];           // the edges a-b, c-d, a-c, b-d, each as a quadratic curve through its two ends and middle
    V3 nl[4];               // the normals at the corners
    double th[4];           // how deep the layer goes at the corners
    double ls[4];           // how long the edges are at the corners, on the average (the size of a cell there)
    V3 lo, hi;              // a box round it
    int nid[4] = {0, 0, 0, 0};              // (the nodes it is made of)
    bool pre = false;       // `Jc` and `cc` are set
    Eigen::Matrix3d Jc;     // the inverse of the map's Jacobian in the middle of the cell
    V3 cc;                  // the middle of the cell (s = t = w = 0.5)
};

V3 bezier(const V3* c, double u)
{
    const double v = 1.0 - u;
    return v * v * c[0] + 2.0 * u * v * c[1] + u * u * c[2];
}

V3 bezierSlope(const V3* c, double u)
{
    return 2.0 * (1.0 - u) * (c[1] - c[0]) + 2.0 * u * (c[2] - c[1]);
}

// The cells as a map: (s, t) over the surface of a cell (a Coons patch of its four edges), w from the surface along the
// normal through the depth of the layer.  A point is given the (s, t, w) of the cell it is in; the TPMS is the periodic
// function of those.
class CellMapData
{
public:
    CellMapData(const CellSet& cs, double cellSize, double heightDir, double lift, int layers, int kind, double thickness, int style,
                double offset, double skin)
        : hd(heightDir), lift(lift), L(std::max(1, layers)), kind(kind), thickness(thickness), style(style), offset(offset), skin(skin),
          cellSize(cellSize)
    {
        // the size of a cell at a node: the middle of the lengths of the edges that meet there.  The same for every cell that
        // has the node, so the field's scale does not jump where cells meet
        std::vector<double> edgeSum(cs.nodes.size(), 0.0);
        std::vector<int> edgeCount(cs.nodes.size(), 0);
        for (const Cell& c : cs.cells)
        {
            const int ee[4][2] = {{c.c[0], c.c[1]}, {c.c[2], c.c[3]}, {c.c[0], c.c[2]}, {c.c[1], c.c[3]}};
            for (const auto& x : ee)
                if (x[0] != x[1])
                {
                    const double len = (cs.nodes[size_t(x[0])].p - cs.nodes[size_t(x[1])].p).norm();
                    for (int i : {x[0], x[1]})
                    {
                        edgeSum[size_t(i)] += len;
                        ++edgeCount[size_t(i)];
                    }
                }
        }
        for (size_t ci = 0; ci < cs.cells.size(); ++ci)
        {
            const Cell& c = cs.cells[ci];
            MapCell m;
            for (int k = 0; k < 4; ++k) m.nid[k] = c.c[k];
            for (int k = 0; k < 4; ++k)
            {
                const GNode& g = cs.nodes[size_t(c.c[k])];
                m.q[k] = g.p;
                m.nl[k] = g.nl.norm() > 0.5 ? g.nl : g.n;
                m.th[k] = g.t;
                const size_t ni = size_t(c.c[k]);
                m.ls[k] = edgeCount[ni] > 0 ? edgeSum[ni] / double(edgeCount[ni]) : cellSize;
            }
            const int e[4][2] = {{0, 1}, {2, 3}, {0, 2}, {1, 3}};
            for (int k = 0; k < 4; ++k)
            {
                const Poly pl = pathPoly(cs.paths, cs.nodes, c.c[e[k][0]], c.c[e[k][1]]);
                const V3 p0 = m.q[e[k][0]], p2 = m.q[e[k][1]], pm = pl.at(0.5);
                m.ctl[k][0] = p0;
                m.ctl[k][1] = 2.0 * pm - 0.5 * (p0 + p2);
                m.ctl[k][2] = p2;
            }
            // a box round the cell and the layer above and below it
            m.lo = V3::Constant(1e300);
            m.hi = V3::Constant(-1e300);
            const double ss[5] = {-0.05, 0.25, 0.5, 0.75, 1.05};
            const double ww[4] = {-0.6, 0.0, 1.0, 1.6};
            for (double s : ss)
                for (double t : ss)
                    for (double w : ww)
                    {
                        const V3 x = map(m, s, t, w);
                        m.lo = m.lo.cwiseMin(x);
                        m.hi = m.hi.cwiseMax(x);
                    }
            m.lo = (m.lo.array() - 0.05 * cellSize).matrix();
            m.hi = (m.hi.array() + 0.05 * cellSize).matrix();
            {
                Eigen::Matrix3d J0;
                m.cc = mapJ(m, 0.5, 0.5, 0.5, J0);
                if (std::abs(J0.determinant()) > 1e-9)
                {
                    m.Jc = J0.inverse();
                    m.pre = true;
                }
            }
            cells.push_back(m);
        }
        order.resize(cells.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
        if (!cells.empty()) build(0, int(cells.size()));
        // a name for what it holds: the cells and everything that shapes the field
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const void* data, size_t n) {
            const unsigned char* c = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < n; ++i)
            {
                h ^= c[i];
                h *= 1099511628211ull;
            }
        };
        for (const MapCell& m : cells)
        {
            mix(m.q, sizeof(m.q));
            mix(m.ctl, sizeof(m.ctl));
            mix(m.nl, sizeof(m.nl));
            mix(m.th, sizeof(m.th));
        }
        const double par[8] = {hd, double(L), double(kind), thickness, double(style), offset, skin, lift};
        mix(par, sizeof(par));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "cellmap#%016llx#%zu", (unsigned long long)h, cells.size());
        key = buf;
        // What the render cache keeps the field by: the same hash, of the cells rounded to a thousandth of a millimetre (the
        // normals to a ten-thousandth).  A lattice laid out again has the last digits of its floats anywhere and is the same
        // lattice; a layout that was changed has other cells and so another key: the cache never gives the mesh of other cells.
        uint64_t hq = 1469598103934665603ull;
        auto mixq = [&hq](double x, double scale) {
            const long long v = std::llround(x * scale);
            const unsigned char* c = reinterpret_cast<const unsigned char*>(&v);
            for (size_t i = 0; i < sizeof(v); ++i)
            {
                hq ^= c[i];
                hq *= 1099511628211ull;
            }
        };
        for (const MapCell& m : cells)
        {
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k < 3; ++k) mixq(m.q[i][k], 1000.0);
            for (int e = 0; e < 4; ++e)
                for (int i = 0; i < 3; ++i)
                    for (int k = 0; k < 3; ++k) mixq(m.ctl[e][i][k], 1000.0);
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k < 3; ++k) mixq(m.nl[i][k], 10000.0);
            for (int i = 0; i < 4; ++i) mixq(m.th[i], 1000.0);
        }
        for (double x : par) mixq(x, 1.0e6);
        std::snprintf(buf, sizeof(buf), "cellmapq#%016llx#%zu", (unsigned long long)hq, cells.size());
        persist = buf;
    }

    V3 map(const MapCell& m, double s, double t, double w) const
    {
        const V3 C = (1 - t) * bezier(m.ctl[0], s) + t * bezier(m.ctl[1], s) + (1 - s) * bezier(m.ctl[2], t) +
                     s * bezier(m.ctl[3], t) -
                     ((1 - s) * (1 - t) * m.q[0] + s * (1 - t) * m.q[1] + (1 - s) * t * m.q[2] + s * t * m.q[3]);
        const double T = (1 - s) * (1 - t) * m.th[0] + s * (1 - t) * m.th[1] + (1 - s) * t * m.th[2] + s * t * m.th[3];
        V3 N = (1 - s) * (1 - t) * m.nl[0] + s * (1 - t) * m.nl[1] + (1 - s) * t * m.nl[2] + s * t * m.nl[3];
        const double nn = N.norm();
        if (nn > 1e-9) N /= nn;
        return C + hd * (lift + w * T) * N;
    }

    // The map and its derivatives with respect to s, t and w (the columns of J)
    V3 mapJ(const MapCell& m, double s, double t, double w, Eigen::Matrix3d& J) const
    {
        const V3 E0 = bezier(m.ctl[0], s), E1 = bezier(m.ctl[1], s), F0 = bezier(m.ctl[2], t), F1 = bezier(m.ctl[3], t);
        const V3 E0s = bezierSlope(m.ctl[0], s), E1s = bezierSlope(m.ctl[1], s);
        const V3 F0t = bezierSlope(m.ctl[2], t), F1t = bezierSlope(m.ctl[3], t);
        const V3 B = (1 - s) * (1 - t) * m.q[0] + s * (1 - t) * m.q[1] + (1 - s) * t * m.q[2] + s * t * m.q[3];
        const V3 Bs = (1 - t) * (m.q[1] - m.q[0]) + t * (m.q[3] - m.q[2]);
        const V3 Bt = (1 - s) * (m.q[2] - m.q[0]) + s * (m.q[3] - m.q[1]);
        const V3 C = (1 - t) * E0 + t * E1 + (1 - s) * F0 + s * F1 - B;
        const V3 Cs = (1 - t) * E0s + t * E1s - F0 + F1 - Bs;
        const V3 Ct = -E0 + E1 + (1 - s) * F0t + s * F1t - Bt;
        const double T = (1 - s) * (1 - t) * m.th[0] + s * (1 - t) * m.th[1] + (1 - s) * t * m.th[2] + s * t * m.th[3];
        const double Ts = (1 - t) * (m.th[1] - m.th[0]) + t * (m.th[3] - m.th[2]);
        const double Tt = (1 - s) * (m.th[2] - m.th[0]) + s * (m.th[3] - m.th[1]);
        const V3 R = (1 - s) * (1 - t) * m.nl[0] + s * (1 - t) * m.nl[1] + (1 - s) * t * m.nl[2] + s * t * m.nl[3];
        const V3 Rs = (1 - t) * (m.nl[1] - m.nl[0]) + t * (m.nl[3] - m.nl[2]);
        const V3 Rt = (1 - s) * (m.nl[2] - m.nl[0]) + s * (m.nl[3] - m.nl[1]);
        const double rn = std::max(R.norm(), 1e-9);
        const V3 N = R / rn;
        const V3 Ns = (Rs - N * N.dot(Rs)) / rn;
        const V3 Nt = (Rt - N * N.dot(Rt)) / rn;
        J.col(0) = Cs + hd * (w * Ts * N + (lift + w * T) * Ns);
        J.col(1) = Ct + hd * (w * Tt * N + (lift + w * T) * Nt);
        J.col(2) = hd * T * N;
        return C + hd * (lift + w * T) * N;
    }

    struct Hit
    {
        int cell = -1;
        double s = 0, t = 0, w = 0;
        Eigen::Matrix3d Jinv;       // ds, dt, dw over dx, dy, dz
        double T = 0;               // how deep the layer is there
        double Ls = 0, Lt = 0;      // how long a step of s and of t is
    };

    // the (s, t, w) of a point in a cell, by Newton's method (from `start`); false if the point is not in the cell's map
    bool invert(const MapCell& m, const V3& p, Hit& h, V3 start = V3(0.5, 0.5, 0.5)) const
    {
        // (the first step of the loop below from the middle of the cell, for the price of one product)
        if (m.pre && start[0] == 0.5 && start[1] == 0.5 && start[2] == 0.5 &&
            (m.Jc * (p - m.cc)).cwiseAbs().maxCoeff() > 4.0)
            return false;
        V3 x = start;
        Eigen::Matrix3d J;
        bool converged = false;
        int clamped = 0;
        for (int it = 0; it < 12; ++it)
        {
            const V3 r = p - mapJ(m, x[0], x[1], x[2], J);
            if (r.squaredNorm() < 4e-6)
            {
                converged = true;
                break;
            }
            const double det = J.determinant();
            if (!(std::abs(det) > 1e-12)) return false;
            V3 d = J.inverse() * r;
            const double big = d.cwiseAbs().maxCoeff();
            // a point far from this cell (its box is only a loose bound: a deep wedge of a cell has a big one): the first
            // step from the middle already goes several cells away, or keeps running into the limits.  Most cells that a
            // box lets through end here, in one step instead of twelve.
            if (it == 0 && big > 4.0) return false;
            if (big > 0.5) d *= 0.5 / big;
            x += d;
            if ((x.array() < -0.7).any() || (x.array() > 1.7).any())
            {
                if (++clamped >= 2) return false;
                x = x.cwiseMax(V3::Constant(-0.7)).cwiseMin(V3::Constant(1.7));
            }
        }
        if (!converged) return false;
        if (x[0] < -0.02 || x[0] > 1.02 || x[1] < -0.02 || x[1] > 1.02 || x[2] < -0.6 || x[2] > 1.6) return false;
        if (!(std::abs(J.determinant()) > 1e-12)) return false;
        h.s = x[0];
        h.t = x[1];
        h.w = x[2];
        h.Jinv = J.inverse();
        h.T = (1 - x[0]) * (1 - x[1]) * m.th[0] + x[0] * (1 - x[1]) * m.th[1] + (1 - x[0]) * x[1] * m.th[2] +
              x[0] * x[1] * m.th[3];
        h.Ls = J.col(0).norm();
        h.Lt = J.col(1).norm();
        return true;
    }

    bool lookup(const V3& p, Hit& best) const
    {
        if (cells.empty()) return false;
        double bestScore = 1e300;
        bool found = false;
        int stack[128];
        int top = 0;
        stack[top++] = 0;
        while (top)
        {
            const Node& n = nodes[size_t(stack[--top])];
            if ((p.array() < n.lo.array()).any() || (p.array() > n.hi.array()).any()) continue;
            if (n.left < 0)
            {
                for (int k = n.first; k < n.first + n.count; ++k)
                {
                    const MapCell& m = cells[size_t(order[size_t(k)])];
                    if ((p.array() < m.lo.array()).any() || (p.array() > m.hi.array()).any()) continue;
                    Hit h;
                    if (!invert(m, p, h)) continue;
                    // (the cell that holds the point most inside it: where two meet, either gives the same answer)
                    const double score = std::max(std::abs(h.s - 0.5), std::abs(h.t - 0.5)) + 0.05 * std::abs(h.w - 0.5);
                    if (score < bestScore)
                    {
                        bestScore = score;
                        h.cell = order[size_t(k)];
                        best = h;
                        found = true;
                    }
                }
            }
            else if (top + 2 <= 128)
            {
                stack[top++] = n.left;
                stack[top++] = n.right;
            }
        }
        return found;
    }

    // how far a point is from the boxes round the cells (0 inside one)
    double boxDistance(const V3& p, V3* away = nullptr) const
    {
        if (cells.empty()) return 1e9;
        double best = 1e300;
        V3 dir = V3::Zero();
        int stack[128];
        int top = 0;
        stack[top++] = 0;
        while (top)
        {
            const Node& n = nodes[size_t(stack[--top])];
            const V3 d0 = (n.lo - p).cwiseMax(p - n.hi).cwiseMax(V3::Zero());
            if (d0.norm() >= best) continue;
            if (n.left < 0)
            {
                for (int k = n.first; k < n.first + n.count; ++k)
                {
                    const MapCell& m = cells[size_t(order[size_t(k)])];
                    const V3 d = (m.lo - p).cwiseMax(p - m.hi).cwiseMax(V3::Zero());
                    const double dist = d.norm();
                    if (dist < best)
                    {
                        best = dist;
                        dir = p - 0.5 * (m.lo + m.hi);
                    }
                }
            }
            else if (top + 2 <= 128)
            {
                stack[top++] = n.left;
                stack[top++] = n.right;
            }
        }
        if (away) *away = dir;
        return best;
    }

    struct Eval
    {
        double f = 1.0;
        V3 g = V3(0, 0, 1);
        bool hit = false;
        Hit h;
    };

    // The field at a point: negative in the solid part of the layers, with its direction of growth
    Eval evaluate(const V3& p) const
    {
        Eval e;
        Hit h;
        if (!lookup(p, h))
        {
            V3 away;
            const double d = boxDistance(p, &away);
            e.f = 0.05 * cellSize + std::min(d, 1e6);
            e.g = away.norm() > 1e-9 ? V3(away.normalized()) : V3(0, 0, 1);
            return e;
        }
        e.hit = true;
        e.h = h;
        const MapCell* cell0 = &cells[size_t(h.cell)];
        const double dmin = std::min(h.w, 1.0 - h.w) * h.T;            // how far into the layer from its nearer face
        const double R = -dmin;
        // the way "away from the layer's faces" points: along the normal, the nearer face's way
        const V3 dw = h.Jinv.row(2).transpose();
        const V3 faceDir = (h.w < 0.5 ? -1.0 : 1.0) * (dw.norm() > 1e-12 ? V3(dw.normalized()) : V3(0, 0, 1));
        if (R > 0.0)
        {
            e.f = R + 0.0;
            e.g = faceDir;
            return e;
        }
        // the periodic function of the cell's own coordinates, one period for each cell on the surface and for each layer
        const double a0 = kTwoPi * h.s, b0 = kTwoPi * h.t, c0 = kTwoPi * h.w * double(L);
        V3 gt;
        const double g0 = tpmsGrad(kind, a0, b0, c0, gt);
        const V3 scaled(gt[0], gt[1], gt[2] * double(L));
        const V3 gp = kTwoPi * (h.Jinv.transpose() * scaled);
        const double gl = gp.norm();
        // How fast the function changes per mm, for turning it into about a distance: from the size of the cell there and
        // the depth of the layer (both found between the corners, so they are the same on both sides of a face between
        // two cells) rather than from this cell's own map (whose slope is not the neighbour's), so that the walls are not
        // thicker on one side of a face than the other.  The cell's size along s and along t is taken as one, so that cells
        // that are turned a quarter against each other agree.
        const double ell = std::max(1e-6, (1 - h.s) * (1 - h.t) * cell0->ls[0] + h.s * (1 - h.t) * cell0->ls[1] +
                                              (1 - h.s) * h.t * cell0->ls[2] + h.s * h.t * cell0->ls[3]);
        const double ellw = std::max(1e-6, h.T / double(L));
        const double slope = kTwoPi * std::sqrt((gt[0] * gt[0] + gt[1] * gt[1]) / (ell * ell) + gt[2] * gt[2] / (ellw * ellw));
        const double k = kTwoPi / cellSize;
        const double eps = 0.15 * k;
        const double d = g0 / std::sqrt(slope * slope + eps * eps);           // about the distance (mm) to the surface
        const V3 dir = gl > 1e-12 ? V3(gp / gl) : V3(0, 0, 1);
        double ft;
        V3 fg;
        if (style == 0)                 // sheet: walls `thickness` thick
        {
            ft = std::abs(d) - 0.5 * thickness;
            fg = (d >= 0 ? 1.0 : -1.0) * dir;
        }
        else                            // network: the solid on one side
        {
            const double sgn = style == 1 ? 1.0 : -1.0;
            ft = sgn * d - offset;
            fg = sgn * dir;
        }
        double inner = ft;
        V3 innerG = fg;
        if (skin > 0 && dmin - skin < inner)
        {
            inner = dmin - skin;
            innerG = -faceDir;
        }
        if (R >= inner)
        {
            e.f = R;
            e.g = faceDir;
        }
        else
        {
            e.f = inner;
            e.g = innerG;
        }
        return e;
    }

    double lipschitz() const { return 3.0; }
    std::string key;                // a name for the data (a hash of the cells): the same within one run
    std::string persist;            // what the render cache keeps it by: a hash of the cells rounded to a thousandth of a millimetre

private:
    struct Node
    {
        V3 lo, hi;
        int left = -1, right = -1, first = 0, count = 0;
    };

    int build(int first, int count)
    {
        const int id = int(nodes.size());
        nodes.push_back(Node());
        V3 lo = V3::Constant(1e300), hi = V3::Constant(-1e300), clo = lo, chi = hi;
        for (int k = first; k < first + count; ++k)
        {
            const MapCell& m = cells[size_t(order[size_t(k)])];
            lo = lo.cwiseMin(m.lo);
            hi = hi.cwiseMax(m.hi);
            const V3 c = 0.5 * (m.lo + m.hi);
            clo = clo.cwiseMin(c);
            chi = chi.cwiseMax(c);
        }
        nodes[size_t(id)].lo = lo;
        nodes[size_t(id)].hi = hi;
        if (count <= 4)
        {
            nodes[size_t(id)].first = first;
            nodes[size_t(id)].count = count;
            return id;
        }
        int axis = 0;
        const V3 ext = chi - clo;
        if (ext[1] > ext[axis]) axis = 1;
        if (ext[2] > ext[axis]) axis = 2;
        const int mid = first + count / 2;
        std::nth_element(order.begin() + first, order.begin() + mid, order.begin() + first + count, [&](int i, int j) {
            const MapCell& a = cells[size_t(i)];
            const MapCell& b = cells[size_t(j)];
            return (a.lo[axis] + a.hi[axis]) < (b.lo[axis] + b.hi[axis]);
        });
        const int l = build(first, mid - first);
        const int r = build(mid, first + count - mid);
        nodes[size_t(id)].left = l;
        nodes[size_t(id)].right = r;
        return id;
    }

    std::vector<MapCell> cells;
    std::vector<int> order;
    std::vector<Node> nodes;
    double hd, lift;
    int L, kind;
    double thickness;
    int style;
    double offset, skin, cellSize;
};

class CellMapOracle : public OracleStorage<>
{
public:
    explicit CellMapOracle(std::shared_ptr<const CellMapData> d) : data(std::move(d)) {}

    void evalInterval(Interval& out) override
    {
        const V3 lo = lower.cast<double>(), hi = upper.cast<double>();
        const V3 c = 0.5 * (lo + hi);
        const double r = 0.5 * (hi - lo).norm();
        const CellMapData::Eval e = data->evaluate(c);
        const double k = data->lipschitz();
        if (e.hit)
        {
            // (a box that reaches past the sides of the cell it is in may hold what lies outside the layers: the upper end is open)
            const double room = std::min(std::min(e.h.s, 1.0 - e.h.s) * e.h.Ls, std::min(e.h.t, 1.0 - e.h.t) * e.h.Lt);
            const double top = r > 0.8 * room ? 1e9 : e.f + k * r;
            out = Interval(float(e.f - k * r), float(top));
        }
        else
        {
            const double d = data->boxDistance(c);
            if (d > 1.05 * r + 1e-6)
                out = Interval(float(std::max(0.0, d - r)), 1e9f);
            else
                out = Interval(-1e9f, 1e9f);
        }
    }

    void evalPoint(float& out, size_t index) override
    {
        out = float(data->evaluate(points.col(index).matrix().cast<double>()).f);
    }

    void checkAmbiguous(Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>, 1, Eigen::Dynamic> /* out */) override {}

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        const CellMapData::Eval e = data->evaluate(points.col(0).matrix().cast<double>());
        out.push_back(Feature(Eigen::Vector3f(e.g.cast<float>())));
    }

private:
    std::shared_ptr<const CellMapData> data;
};

class CellMapClause : public OracleClause
{
public:
    explicit CellMapClause(std::shared_ptr<const CellMapData> d) : data(std::move(d)) {}
    std::unique_ptr<Oracle> getOracle() const override { return std::make_unique<CellMapOracle>(data); }
    std::string name() const override { return "CellMapTPMS"; }
    std::string contentKey() const override { return data->key; }
    std::string persistentKey() const override { return data->persist; }

private:
    std::shared_ptr<const CellMapData> data;
};

}   // namespace

bool fieldCells(const Tree& body, const std::map<Tree::Id, float>& vars, const V3& lo, const V3& hi,
                const V3& directionIn, int gridOffset, int layout, const Tree* regionTree, double cell, const std::vector<std::array<V3, 2>>& unitBeams, double depth,
                int layersIn, double radiusIn, double heightDir, double lift, std::vector<V3>& outNodes,
                std::vector<std::array<int, 2>>& outBeams, SurfaceBeamsInfo& binfo, V3& usedDirection, std::string& error)
{
    binfo = SurfaceBeamsInfo();
    if (unitBeams.empty() || !(cell > 0))
    {
        error = "the unit cell has no beams, or the cell no size";
        return false;
    }
    Field F(body, vars);
    std::unique_ptr<Field> region;
    if (regionTree) region.reset(new Field(*regionTree, vars));
    CellSet cs;
    StageGuard guard;
    if (!makeCells(F, lo, hi, directionIn, gridOffset, layout, region.get(), cell, depth, heightDir, cs, error)) return false;
    usedDirection = cs.usedDirection;
    binfo.warning = cs.warning;
    const double typical = cs.typical;
    stageBegin("making the struts of the cells");
    // the size of the struts, how deep the layers are in all, how many
    double radius = radiusIn > 0 ? radiusIn : 0.12 * std::min(cell, typical);
    const double inset = (heightDir < 0 && lift == 0) ? radius : 0.0;      // (a layer in the body keeps its struts in it)
    int layers = layersIn;
    if (layers <= 0) layers = std::max(1, int(std::lround((typical - 2.0 * inset) / cell)));
    binfo.radius = radius;
    binfo.thickness = typical;
    binfo.layers = layers;
    if (!beamsFromCells(F, cs.nodes, cs.cells, cs.geos, cell, unitBeams, layers, inset, heightDir, lift, outNodes, outBeams, binfo))
    {
        error = "the unit cell has no beams";
        return false;
    }
    binfo.cellsDropped = 0;
    if (std::getenv("FIELDES_SC_STATS"))
        std::fprintf(stderr, "[conform] beams made\n");
    return true;
}

bool fieldCellTpms(const Tree& body, const std::map<Tree::Id, float>& vars, const V3& lo, const V3& hi,
                   const V3& directionIn, int gridOffset, int layout, const Tree* regionTree, double cell, double depth, int layersIn, double heightDir, double lift, int kind, double thickness,
                   int style, double offset, double skin, std::vector<Tree>& out,
                   SurfaceBeamsInfo& binfo, V3& usedDirection, std::string& error)
{
    binfo = SurfaceBeamsInfo();
    if (!(cell > 0))
    {
        error = "the cell has no size";
        return false;
    }
    StageGuard guard;
    Field F(body, vars);
    std::unique_ptr<Field> region;
    if (regionTree) region.reset(new Field(*regionTree, vars));
    CellSet cs;
    if (!makeCells(F, lo, hi, directionIn, gridOffset, layout, region.get(), cell, depth, heightDir, cs, error)) return false;
    usedDirection = cs.usedDirection;
    binfo.warning = cs.warning;
    int layers = layersIn > 0 ? layersIn : std::max(1, int(std::lround(cs.typical / cell)));
    binfo.cells = cs.cells.size();
    binfo.nodes = cs.nodes.size();
    binfo.thickness = cs.typical;
    binfo.layers = layers;
    auto data = std::make_shared<const CellMapData>(cs, cell, heightDir, lift, layers, kind, thickness, style, offset, skin);
    out.clear();
    out.push_back(Tree(std::make_unique<CellMapClause>(data)));
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Selecting a patch of a surface: a flood fill over the surface of a field, from the field alone

namespace {

// The samples of a walk, in a hash of cubes: is there one within r of a point?
class SampleHash
{
public:
    SampleHash(const std::vector<V3>& samples, double cell) : pts_(samples), cell_(cell) {}

    void add(size_t index)
    {
        grid_[key(cellOf(pts_[index]))].push_back(uint32_t(index));
    }

    bool near(const V3& p, double r) const
    {
        const std::array<long, 3> c = cellOf(p);
        const double r2 = r * r;
        for (long dx = -1; dx <= 1; ++dx)
            for (long dy = -1; dy <= 1; ++dy)
                for (long dz = -1; dz <= 1; ++dz)
                {
                    const auto it = grid_.find(key({c[0] + dx, c[1] + dy, c[2] + dz}));
                    if (it == grid_.end()) continue;
                    for (uint32_t i : it->second)
                        if ((pts_[i] - p).squaredNorm() < r2) return true;
                }
        return false;
    }

private:
    std::array<long, 3> cellOf(const V3& p) const
    {
        return {long(std::floor(p.x() / cell_)), long(std::floor(p.y() / cell_)), long(std::floor(p.z() / cell_))};
    }
    static uint64_t key(const std::array<long, 3>& c)
    {
        const uint64_t m = (uint64_t(1) << 21) - 1;
        return ((uint64_t(c[0] + (1 << 20)) & m) << 42) | ((uint64_t(c[1] + (1 << 20)) & m) << 21) | (uint64_t(c[2] + (1 << 20)) & m);
    }

    const std::vector<V3>& pts_;
    double cell_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid_;
};

// Two unit vectors that span the plane normal to n
void tangents(const V3& n, V3& a, V3& b)
{
    const V3 axis = std::abs(n.x()) < 0.6 ? V3(1, 0, 0) : (std::abs(n.y()) < 0.6 ? V3(0, 1, 0) : V3(0, 0, 1));
    a = n.cross(axis).normalized();
    b = n.cross(a).normalized();
}

}   // namespace

bool selectSurfacePatch(const Tree& body, const std::map<Tree::Id, float>& vars, const V3& seed, double angleDegrees, int mode,
                        double maxRadius, double spacing, const V3& lo, const V3& hi, SurfacePatch& out, std::string& error)
{
    out = SurfacePatch();
    if (!(spacing > 0) || !std::isfinite(spacing))
    {
        error = "the spacing of the samples must be positive";
        return false;
    }
    if (!(angleDegrees > 0) || angleDegrees > 90.0)
    {
        error = "the angle is between 0 and 90 degrees";
        return false;
    }
    const V3 diag = hi - lo;
    if (!diag.allFinite() || diag.minCoeff() <= 0 || diag.maxCoeff() / spacing > 1.0e6)
    {
        error = "the part is too big for samples this close (more than a million along its length): raise the spacing";
        return false;
    }
    Field F(body, vars);
    const double s = spacing;
    const double cosLimit = std::cos(angleDegrees * M_PI / 180.0);

    // the seed, moved along the gradient onto the surface
    std::vector<V3> p0{seed}, n0;
    std::vector<char> ok0;
    projectToSurface(F, p0, n0, ok0, diag.norm(), 1e-4 * s);
    if (!ok0[0] || !p0[0].allFinite())
    {
        error = "no surface was found near the seed: put it on, or close to, the part";
        return false;
    }
    out.spacing = s;
    out.seedPoint = p0[0];
    out.seedDistance = (p0[0] - seed).norm();
    out.points.push_back(p0[0]);
    out.normals.push_back(n0[0]);
    const V3 nSeed = n0[0];

    // the lowest and highest corners of what may be reached: a little past the box, which only bounds the search
    const V3 boxLo = lo - V3::Constant(2 * s), boxHi = hi + V3::Constant(2 * s);

    SampleHash hash(out.points, 0.7 * s);
    hash.add(0);
    std::vector<size_t> frontier{0};
    // (the sample every sample was reached from: the smooth walk measures how far the surface has turned along the way back)
    std::vector<size_t> cameFrom{size_t(-1)};
    // How tightly the surface may bend in the smooth mode: `angle` degrees within this length of the walk (a curvature limit that
    // does not depend on how close the samples are: a limit per step would be a limit on the triangles of a mesh, and a rounded
    // skin never turns that much in a step)
    const double kSmoothLength = 10.0;
    const int smoothSteps = std::max(1, int(std::lround(kSmoothLength / s)));
    const double noiseFloor = std::min(angleDegrees, 10.0);       // (the normals of a mesh jump by a few degrees from facet to facet)
    const size_t kMax = 4000000;
    std::vector<V3> cand, cn;
    std::vector<size_t> parent;
    std::vector<char> ok;
    while (!frontier.empty())
    {
        cand.clear();
        parent.clear();
        for (size_t f : frontier)
        {
            const V3 p = out.points[f];
            V3 a, b;
            tangents(out.normals[f], a, b);
            for (int k = 0; k < 8; ++k)
            {
                const double t = k * M_PI / 4.0;
                const V3 q = p + s * (std::cos(t) * a + std::sin(t) * b);
                if (hash.near(q, 0.55 * s)) continue;              // (the surface there is taken already: nothing to look at)
                cand.push_back(q);
                parent.push_back(f);
            }
        }
        if (cand.empty()) break;
        std::vector<V3> start = cand;
        projectToSurface(F, cand, cn, ok, s, 2e-3 * s);
        std::vector<size_t> next;
        for (size_t i = 0; i < cand.size(); ++i)
        {
            if (!ok[i] || !cand[i].allFinite()) continue;
            const V3& q = cand[i];
            // (a point that the walk had to carry far to the surface is past an edge or a gap: the surface does not go on there)
            if ((q - start[i]).norm() > 0.6 * s) continue;
            if ((q.array() < boxLo.array()).any() || (q.array() > boxHi.array()).any()) continue;
            if (maxRadius > 0 && (q - out.seedPoint).norm() > maxRadius) continue;
            const V3& nq = cn[i];
            double c, limit = cosLimit;
            if (mode == 0)
                c = nq.dot(nSeed);
            else
            {
                // the sample about kSmoothLength back along the walk (or the seed, if the walk is not that long yet): the surface may have
                // turned `angle` degrees for every kSmoothLength of the way between them
                size_t a = parent[i];
                int steps = 1;
                while (steps < smoothSteps && cameFrom[a] != size_t(-1))
                {
                    a = cameFrom[a];
                    ++steps;
                }
                const double allowed = std::min(179.0, std::max(noiseFloor, angleDegrees * (steps * s) / kSmoothLength));
                limit = std::cos(allowed * M_PI / 180.0);
                c = nq.dot(out.normals[a]);
            }
            if (!(c >= limit)) continue;
            if (hash.near(q, 0.7 * s)) continue;
            out.points.push_back(q);
            out.normals.push_back(nq);
            cameFrom.push_back(parent[i]);
            hash.add(out.points.size() - 1);
            next.push_back(out.points.size() - 1);
            if (out.points.size() >= kMax)
            {
                out.stopped = true;
                break;
            }
        }
        if (out.stopped) break;
        frontier.swap(next);
    }
    return true;
}

// The distance to the nearest of a set of points, as a field: a kd-tree of the points, so that a point of the field and a box
// of space are answered by looking at the few that are near
namespace {

struct PointCloud
{
    std::vector<V3> p;
    std::vector<V3> n;              // (the normals, for a cloud of oriented points: see orientedPointsTree)
    double sigma = 0;               // (and the width of the blend of their planes)
    struct Node
    {
        V3 lo = V3::Zero(), hi = V3::Zero();
        int left = -1, right = -1;
        uint32_t start = 0, count = 0;
    };
    std::vector<Node> nodes;
    std::vector<uint32_t> order;
    std::string key;

    void build()
    {
        order.resize(p.size());
        std::iota(order.begin(), order.end(), uint32_t(0));
        nodes.clear();
        nodes.reserve(p.size() / 4 + 16);
        make(0, uint32_t(p.size()));
        // (a hash of the points to a thousandth, in the order of the tree: what the cloud is, not how it was found)
        uint64_t h = 1469598103934665603ull;
        std::vector<std::array<int64_t, 3>> r;
        r.reserve(p.size());
        for (const V3& q : p) r.push_back({int64_t(std::llround(q.x() * 1000)), int64_t(std::llround(q.y() * 1000)), int64_t(std::llround(q.z() * 1000))});
        std::sort(r.begin(), r.end());
        for (const auto& a : r)
            for (int64_t v : a)
            {
                h ^= uint64_t(v);
                h *= 1099511628211ull;
            }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "points#%016llx#%zu", static_cast<unsigned long long>(h), p.size());
        key = buf;
    }

    int make(uint32_t start, uint32_t count)
    {
        Node n;
        n.start = start;
        n.count = count;
        n.lo = V3::Constant(1e300);
        n.hi = V3::Constant(-1e300);
        for (uint32_t i = start; i < start + count; ++i)
        {
            n.lo = n.lo.cwiseMin(p[order[i]]);
            n.hi = n.hi.cwiseMax(p[order[i]]);
        }
        const int index = int(nodes.size());
        nodes.push_back(n);
        if (count <= 8) return index;
        const V3 ext = n.hi - n.lo;
        int axis = 0;
        if (ext.y() > ext[axis]) axis = 1;
        if (ext.z() > ext[axis]) axis = 2;
        if (!(ext[axis] > 0)) return index;
        const uint32_t half = count / 2;
        std::nth_element(order.begin() + start, order.begin() + start + half, order.begin() + start + count,
                         [&](uint32_t a, uint32_t b) { return p[a][axis] < p[b][axis]; });
        const int l = make(start, half);
        const int r = make(start + half, count - half);
        nodes[size_t(index)].left = l;
        nodes[size_t(index)].right = r;
        return index;
    }

    static double boxDistance2(const V3& q, const V3& lo, const V3& hi)
    {
        const V3 d = (lo - q).cwiseMax(V3::Zero()).cwiseMax((q - hi).cwiseMax(V3::Zero()));
        return d.squaredNorm();
    }

    // the squared distance to the nearest point, and which
    void nearest(const V3& q, int node, double& best, uint32_t& who) const
    {
        const Node& n = nodes[size_t(node)];
        if (boxDistance2(q, n.lo, n.hi) >= best) return;
        if (n.left < 0)
        {
            for (uint32_t i = n.start; i < n.start + n.count; ++i)
            {
                const double d = (p[order[i]] - q).squaredNorm();
                if (d < best)
                {
                    best = d;
                    who = order[i];
                }
            }
            return;
        }
        const Node& a = nodes[size_t(n.left)];
        const Node& b = nodes[size_t(n.right)];
        const bool first = boxDistance2(q, a.lo, a.hi) <= boxDistance2(q, b.lo, b.hi);
        nearest(q, first ? n.left : n.right, best, who);
        nearest(q, first ? n.right : n.left, best, who);
    }

    // the points within sqrt(r2) of q
    void within(const V3& q, double r2, int node, std::vector<uint32_t>& out) const
    {
        const Node& nd = nodes[size_t(node)];
        if (boxDistance2(q, nd.lo, nd.hi) > r2) return;
        if (nd.left < 0)
        {
            for (uint32_t i = nd.start; i < nd.start + nd.count; ++i)
                if ((p[order[i]] - q).squaredNorm() <= r2) out.push_back(order[i]);
            return;
        }
        within(q, r2, nd.left, out);
        within(q, r2, nd.right, out);
    }

    // the squared distance from a box to the nearest point of the cloud
    void boxNearest(const V3& lo, const V3& hi, int node, double& best) const
    {
        const Node& n = nodes[size_t(node)];
        // (the boxes' own distance: the cloud's node box and the query box)
        const V3 gap = (n.lo - hi).cwiseMax(V3::Zero()).cwiseMax((lo - n.hi).cwiseMax(V3::Zero()));
        if (gap.squaredNorm() >= best) return;
        if (n.left < 0)
        {
            for (uint32_t i = n.start; i < n.start + n.count; ++i)
                best = std::min(best, boxDistance2(p[order[i]], lo, hi));
            return;
        }
        boxNearest(lo, hi, n.left, best);
        boxNearest(lo, hi, n.right, best);
    }
};

class PointCloudOracle : public OracleStorage<>
{
public:
    explicit PointCloudOracle(std::shared_ptr<const PointCloud> c) : cloud(std::move(c)) {}

    void evalInterval(Interval& out) override
    {
        const V3 lo = lower.cast<double>(), hi = upper.cast<double>();
        const V3 c = 0.5 * (lo + hi);
        const double r = 0.5 * (hi - lo).norm();
        double bestBox = std::numeric_limits<double>::infinity();
        cloud->boxNearest(lo, hi, 0, bestBox);
        double best = std::numeric_limits<double>::infinity();
        uint32_t who = 0;
        cloud->nearest(c, 0, best, who);
        out = Interval(float(std::sqrt(bestBox)), float(std::sqrt(best) + r));
    }

    void evalPoint(float& out, size_t index) override
    {
        double best = std::numeric_limits<double>::infinity();
        uint32_t who = 0;
        cloud->nearest(points.col(index).matrix().cast<double>(), 0, best, who);
        out = float(std::sqrt(best));
    }

    void checkAmbiguous(Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>, 1, Eigen::Dynamic> /* out */) override {}

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        const V3 q = points.col(0).matrix().cast<double>();
        double best = std::numeric_limits<double>::infinity();
        uint32_t who = 0;
        cloud->nearest(q, 0, best, who);
        const V3 away = q - cloud->p[who];
        const double len = away.norm();
        out.push_back(Feature(len > 1e-12 ? Eigen::Vector3f((away / len).cast<float>()) : Eigen::Vector3f(0, 0, 0)));
    }

private:
    std::shared_ptr<const PointCloud> cloud;
};

class PointCloudClause : public OracleClause
{
public:
    explicit PointCloudClause(std::shared_ptr<const PointCloud> c) : cloud(std::move(c)) {}
    std::unique_ptr<Oracle> getOracle() const override { return std::make_unique<PointCloudOracle>(cloud); }
    std::string name() const override { return "PointCloudDistance"; }
    std::string contentKey() const override { return cloud->key; }
    std::string persistentKey() const override { return cloud->key; }

private:
    std::shared_ptr<const PointCloud> cloud;
};

// The field of a SURFACE made of oriented points (the samples of a walk over it, each with the normal of the surface there): at a point, its
// height above the surface along the normal -- the mean of (q - p).n over the samples p near it, weighted by how near they are and by how
// little their normal turns from that of the nearest one (so that a sharp edge is not rounded off).  It is zero on the surface, positive
// on the side the normals face.  The surface alone: nothing that lies behind it is in the field, so it has no thickness to find
double orientedValue(const PointCloud& c, const V3& q, V3* grad)
{
    double best = std::numeric_limits<double>::infinity();
    uint32_t who = 0;
    c.nearest(q, 0, best, who);
    const V3 n0 = c.n[who];
    std::vector<uint32_t> near;
    const double R = 3.0 * c.sigma;
    c.within(q, R * R, 0, near);
    double sw = 0, sf = 0;
    V3 sn = V3::Zero();
    for (uint32_t i : near)
    {
        const double a = c.n[i].dot(n0);
        if (!(a > 0)) continue;
        const V3 d = q - c.p[i];
        const double w = std::exp(-d.squaredNorm() / (c.sigma * c.sigma)) * std::pow(a, 6.0);
        sw += w;
        sf += w * d.dot(c.n[i]);
        sn += w * c.n[i];
    }
    if (!(sw > 1e-12))
    {
        if (grad) *grad = n0;
        return (q - c.p[who]).dot(n0);
    }
    if (grad) *grad = sn.norm() > 1e-12 ? V3(sn.normalized()) : n0;
    return sf / sw;
}

class OrientedPointsOracle : public OracleStorage<>
{
public:
    explicit OrientedPointsOracle(std::shared_ptr<const PointCloud> c) : cloud(std::move(c)) {}

    void evalInterval(Interval& out) override
    {
        const V3 lo = lower.cast<double>(), hi = upper.cast<double>();
        const V3 mid = 0.5 * (lo + hi);
        const double r = 0.5 * (hi - lo).norm();
        double best = std::numeric_limits<double>::infinity();
        uint32_t who = 0;
        cloud->nearest(mid, 0, best, who);
        // (the height above the surface is at most the distance to the samples that count)
        const double u = 3.0 * cloud->sigma + std::sqrt(best) + r;
        out = Interval(float(-u), float(u));
    }

    void evalPoint(float& out, size_t index) override
    {
        out = float(orientedValue(*cloud, points.col(index).matrix().cast<double>(), nullptr));
    }

    void checkAmbiguous(Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>, 1, Eigen::Dynamic> /* out */) override {}

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        V3 g = V3::Zero();
        orientedValue(*cloud, points.col(0).matrix().cast<double>(), &g);
        out.push_back(Feature(Eigen::Vector3f(g.cast<float>())));
    }

private:
    std::shared_ptr<const PointCloud> cloud;
};

class OrientedPointsClause : public OracleClause
{
public:
    explicit OrientedPointsClause(std::shared_ptr<const PointCloud> c) : cloud(std::move(c)) {}
    std::unique_ptr<Oracle> getOracle() const override { return std::make_unique<OrientedPointsOracle>(cloud); }
    std::string name() const override { return "OrientedPointsSurface"; }
    std::string contentKey() const override { return cloud->key; }
    std::string persistentKey() const override { return cloud->key; }

private:
    std::shared_ptr<const PointCloud> cloud;
};

}   // namespace

Tree orientedPointsTree(const std::vector<V3>& points, const std::vector<V3>& normals, double sigma)
{
    auto cloud = std::make_shared<PointCloud>();
    cloud->p = points;
    cloud->n = normals;
    cloud->sigma = sigma;
    cloud->build();
    // (the points are in the key already; the normals and the width of the blend are what else the field is)
    uint64_t h = 1469598103934665603ull;
    for (const V3& q : normals)
        for (int k = 0; k < 3; ++k)
        {
            h ^= uint64_t(int64_t(std::llround(q[k] * 1000)));
            h *= 1099511628211ull;
        }
    h ^= uint64_t(std::llround(sigma * 1000));
    h *= 1099511628211ull;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "#oriented%016llx", static_cast<unsigned long long>(h));
    cloud->key += buf;
    return Tree(std::make_unique<OrientedPointsClause>(cloud));
}

Tree pointsDistanceTree(const std::vector<V3>& points)
{
    auto cloud = std::make_shared<PointCloud>();
    cloud->p = points;
    cloud->build();
    return Tree(std::make_unique<PointCloudClause>(cloud));
}

}   // namespace lattice
}   // namespace libfive
