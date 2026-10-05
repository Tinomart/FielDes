/*
 *  FielDes: solved analyses kept on disk (the result cache); see result_io.hpp.
 *
 *  One binary file per problem: a header (FDRS, a version, the kind), the problem's settings, its mesh
 *  once, then every result it holds without the mesh (the fields at the nodes, the elements' own values,
 *  the numbers).  Reading builds a problem of the same kind around the data and one locator for all its
 *  results.  Anything odd in the file (a short read, an index past the mesh) refuses the whole file.
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this file,
 *  You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "libfive/fea/result_io.hpp"
#include "libfive/fea/tetfea.hpp"
#include "libfive/fea/tetthermal.hpp"
#include "libfive/fea/tetflow.hpp"
#include "libfive/fea/fea.hpp"
#include "libfive/fea/thermal.hpp"
#include "libfive/tree/content_key.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <type_traits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace libfive {
namespace fea {

namespace {

const char MAGIC[4] = {'F', 'D', 'R', 'S'};
const uint32_t VERSION = 2;
enum Kind : uint32_t { KIND_TET = 1, KIND_TETTHERMAL = 2, KIND_TETFLOW = 3, KIND_HEX = 4, KIND_HEXTHERMAL = 5 };

#ifdef _WIN32
std::wstring wide(const std::string& s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(std::max(n, 1)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(n > 0 ? size_t(n - 1) : 0);
    return w;
}
#endif

struct Writer
{
    std::ofstream f;
    bool ok = true;

    explicit Writer(const std::string& path)
    {
#ifdef _WIN32
        f.open(wide(path).c_str(), std::ios::binary | std::ios::trunc);
#else
        f.open(path, std::ios::binary | std::ios::trunc);
#endif
        ok = f.good();
    }
    void bytes(const void* p, size_t n)
    {
        if (!ok || n == 0) return;
        f.write(static_cast<const char*>(p), std::streamsize(n));
        ok = f.good();
    }
    template <class T> void pod(const T& v) { bytes(&v, sizeof(T)); }
    void u8(bool v) { const uint8_t b = v ? 1 : 0; pod(b); }
    void u32(uint32_t v) { pod(v); }
    void u64(uint64_t v) { pod(v); }
    void i32(int32_t v) { pod(v); }
    void f32(float v) { pod(v); }
    void f64(double v) { pod(v); }
    void str(const std::string& s) { u64(s.size()); bytes(s.data(), s.size()); }
    void vec3(const Eigen::Vector3d& v) { for (int c = 0; c < 3; ++c) f64(v[c]); }
    template <class T> void raw(const std::vector<T>& v)
    {
        static_assert(std::is_trivially_copyable<T>::value, "raw vectors are of plain data");
        u64(v.size());
        bytes(v.data(), v.size() * sizeof(T));
    }
    void vec3s(const std::vector<Eigen::Vector3d>& v) { u64(v.size()); for (const auto& x : v) vec3(x); }
    void floats2(const std::vector<std::vector<float>>& v) { u64(v.size()); for (const auto& x : v) raw(x); }
    void doubles(const std::vector<double>& v) { raw(v); }
    bool close() { if (ok) { f.flush(); ok = f.good(); } f.close(); return ok; }
};

struct Reader
{
    std::ifstream f;
    bool ok = true;
    uint64_t left = 0;          // bytes not yet read: a count that cannot fit is refused before anything is allocated

    explicit Reader(const std::string& path)
    {
#ifdef _WIN32
        f.open(wide(path).c_str(), std::ios::binary | std::ios::ate);
#else
        f.open(path, std::ios::binary | std::ios::ate);
#endif
        ok = f.good();
        if (ok)
        {
            const auto end = f.tellg();
            left = end < 0 ? 0 : uint64_t(end);
            f.seekg(0, std::ios::beg);
            ok = f.good();
        }
    }
    void bytes(void* p, size_t n)
    {
        if (!ok) return;
        if (n == 0) return;
        if (n > left) { ok = false; return; }
        f.read(static_cast<char*>(p), std::streamsize(n));
        ok = f.good() && size_t(f.gcount()) == n;
        left -= n;
    }
    template <class T> T pod() { T v{}; bytes(&v, sizeof(T)); return ok ? v : T{}; }
    bool u8() { return pod<uint8_t>() != 0; }
    uint32_t u32() { return pod<uint32_t>(); }
    uint64_t u64() { return pod<uint64_t>(); }
    int32_t i32() { return pod<int32_t>(); }
    float f32() { return pod<float>(); }
    double f64() { return pod<double>(); }
    uint64_t count(size_t itemBytes)
    {
        const uint64_t n = u64();
        if (!ok) return 0;
        if (itemBytes > 0 && n > left / itemBytes) { ok = false; return 0; }
        return n;
    }
    std::string str()
    {
        const uint64_t n = count(1);
        std::string s(size_t(n), '\0');
        if (n) bytes(&s[0], size_t(n));
        return ok ? s : std::string();
    }
    Eigen::Vector3d vec3()
    {
        Eigen::Vector3d v = Eigen::Vector3d::Zero();
        for (int c = 0; c < 3; ++c) v[c] = f64();
        return v;
    }
    template <class T> void raw(std::vector<T>& v)
    {
        static_assert(std::is_trivially_copyable<T>::value, "raw vectors are of plain data");
        const uint64_t n = count(sizeof(T));
        v.clear();
        if (!ok) return;
        v.resize(size_t(n));
        bytes(v.data(), v.size() * sizeof(T));
        if (!ok) v.clear();
    }
    void vec3s(std::vector<Eigen::Vector3d>& v)
    {
        const uint64_t n = count(3 * sizeof(double));
        v.clear();
        if (!ok) return;
        v.resize(size_t(n));
        for (auto& x : v) x = vec3();
    }
    void floats2(std::vector<std::vector<float>>& v)
    {
        const uint64_t n = count(8);
        v.clear();
        if (!ok) return;
        v.resize(size_t(n));
        for (auto& x : v) raw(x);
    }
    void doubles(std::vector<double>& v) { raw(v); }
};

// ------------------------------------------------------------------ the pieces

void writeHeader(Writer& w, uint32_t kind)
{
    w.bytes(MAGIC, 4);
    w.u32(VERSION);
    w.u32(kind);
}

bool readHeader(Reader& r, uint32_t kind)
{
    char m[4] = {0, 0, 0, 0};
    r.bytes(m, 4);
    if (!r.ok || std::memcmp(m, MAGIC, 4) != 0) return false;
    if (r.u32() != VERSION) return false;
    if (r.u32() != kind) return false;
    return r.ok;
}

void writeMesh(Writer& w, const TetMesh& m)
{
    w.vec3s(m.pos);
    w.raw(m.tets);
    w.raw(m.faces);
    w.raw(m.faceTet);
    w.raw(m.onSurface);
    w.f64(m.h);
    w.u64(m.degenerate);
    w.u64(m.featureVertices);
    w.u64(m.featuresReverted);
}

bool readMesh(Reader& r, TetMesh& m)
{
    r.vec3s(m.pos);
    r.raw(m.tets);
    r.raw(m.faces);
    r.raw(m.faceTet);
    r.raw(m.onSurface);
    m.h = r.f64();
    m.degenerate = size_t(r.u64());
    m.featureVertices = size_t(r.u64());
    m.featuresReverted = size_t(r.u64());
    if (!r.ok) return false;
    const int nv = int(m.pos.size()), nt = int(m.tets.size());
    if (nv <= 0 || nt <= 0) return false;
    for (const auto& t : m.tets)
        for (int i = 0; i < 4; ++i) if (t[size_t(i)] < 0 || t[size_t(i)] >= nv) return false;
    for (const auto& f : m.faces)
        for (int i = 0; i < 3; ++i) if (f[size_t(i)] < 0 || f[size_t(i)] >= nv) return false;
    if (m.faceTet.size() != m.faces.size()) return false;
    for (int t : m.faceTet) if (t < 0 || t >= nt) return false;
    if (!m.onSurface.empty() && m.onSurface.size() != m.pos.size()) return false;
    return true;
}

void writeMeshResult(Writer& w, const MeshResult& R)
{
    w.u64(R.serial);
    for (int k = 0; k < Result::FIELD_COUNT; ++k) w.raw(R.fields[k]);
    for (int k = 0; k < Result::FIELD_COUNT; ++k) { w.f32(R.minValue[k]); w.f32(R.maxValue[k]); }
    w.raw(R.elementStress);
    w.i32(R.elements); w.i32(R.nodes); w.i32(R.dofs); w.i32(R.fixedNodes); w.i32(R.loadedNodes);
    w.i32(R.looseElements); w.i32(R.iterations);
    w.f64(R.residual); w.f64(R.seconds); w.f64(R.volume); w.f64(R.compliance);
    w.vec3(R.totalLoad); w.vec3(R.reaction);
    w.f64(R.heatIn); w.f64(R.heatOutFixed); w.f64(R.heatOutConvection);
}

std::shared_ptr<MeshResult> readMeshResult(Reader& r, const std::shared_ptr<const TetMesh>& mesh,
                                           const std::shared_ptr<const TetLocator>& locator)
{
    auto R = std::make_shared<MeshResult>();
    R->mesh = mesh;
    R->locator = locator;
    R->serial = r.u64();
    const size_t nv = mesh->pos.size(), nt = mesh->tets.size();
    for (int k = 0; k < Result::FIELD_COUNT; ++k)
    {
        r.raw(R->fields[k]);
        if (!R->fields[k].empty() && R->fields[k].size() != nv) return nullptr;
    }
    for (int k = 0; k < Result::FIELD_COUNT; ++k) { R->minValue[k] = r.f32(); R->maxValue[k] = r.f32(); }
    r.raw(R->elementStress);
    if (!R->elementStress.empty() && R->elementStress.size() != nt) return nullptr;
    R->elements = r.i32(); R->nodes = r.i32(); R->dofs = r.i32(); R->fixedNodes = r.i32(); R->loadedNodes = r.i32();
    R->looseElements = r.i32(); R->iterations = r.i32();
    R->residual = r.f64(); R->seconds = r.f64(); R->volume = r.f64(); R->compliance = r.f64();
    R->totalLoad = r.vec3(); R->reaction = r.vec3();
    R->heatIn = r.f64(); R->heatOutFixed = r.f64(); R->heatOutConvection = r.f64();
    return r.ok ? R : nullptr;
}

void writeOptional(Writer& w, const std::shared_ptr<MeshResult>& R)
{
    w.u8(R != nullptr);
    if (R) writeMeshResult(w, *R);
}

// (a result that may be absent: a modal problem has no static result)
bool readOptional(Reader& r, std::shared_ptr<MeshResult>& R, const std::shared_ptr<const TetMesh>& mesh,
                  const std::shared_ptr<const TetLocator>& locator)
{
    R.reset();
    if (!r.u8()) return r.ok;
    R = readMeshResult(r, mesh, locator);
    return R != nullptr;
}

void writeGrid(Writer& w, const Result& R)
{
    w.u64(R.serial);
    w.vec3(R.lo);
    w.f64(R.h);
    w.i32(R.nx); w.i32(R.ny); w.i32(R.nz);
    w.u8(R.tetrahedra);
    for (int k = 0; k < Result::FIELD_COUNT; ++k) w.raw(R.fields[k]);
    for (int k = 0; k < Result::FIELD_COUNT; ++k) { w.f32(R.minValue[k]); w.f32(R.maxValue[k]); }
    w.i32(R.elements); w.i32(R.nodes); w.i32(R.dofs); w.i32(R.fixedNodes); w.i32(R.loadedNodes);
    w.i32(R.looseElements); w.i32(R.iterations);
    w.f64(R.residual); w.f64(R.seconds); w.f64(R.volume); w.f64(R.compliance);
    w.vec3(R.totalLoad); w.vec3(R.reaction);
}

std::shared_ptr<Result> readGrid(Reader& r)
{
    auto R = std::make_shared<Result>();
    R->serial = r.u64();
    R->lo = r.vec3();
    R->h = r.f64();
    R->nx = r.i32(); R->ny = r.i32(); R->nz = r.i32();
    R->tetrahedra = r.u8();
    if (!r.ok || R->nx < 0 || R->ny < 0 || R->nz < 0) return nullptr;
    const size_t n = size_t(R->nx) * size_t(R->ny) * size_t(R->nz);
    for (int k = 0; k < Result::FIELD_COUNT; ++k)
    {
        r.raw(R->fields[k]);
        if (!R->fields[k].empty() && R->fields[k].size() != n) return nullptr;
    }
    for (int k = 0; k < Result::FIELD_COUNT; ++k) { R->minValue[k] = r.f32(); R->maxValue[k] = r.f32(); }
    R->elements = r.i32(); R->nodes = r.i32(); R->dofs = r.i32(); R->fixedNodes = r.i32(); R->loadedNodes = r.i32();
    R->looseElements = r.i32(); R->iterations = r.i32();
    R->residual = r.f64(); R->seconds = r.f64(); R->volume = r.f64(); R->compliance = r.f64();
    R->totalLoad = r.vec3(); R->reaction = r.vec3();
    return r.ok ? R : nullptr;
}

void writeOptionalGrid(Writer& w, const std::shared_ptr<Result>& R)
{
    w.u8(R != nullptr);
    if (R) writeGrid(w, *R);
}

bool readOptionalGrid(Reader& r, std::shared_ptr<Result>& R)
{
    R.reset();
    if (!r.u8()) return r.ok;
    R = readGrid(r);
    return R != nullptr;
}

void writeStats(Writer& w, const TetFlowProblem::Stats& s)
{
    w.f64(s.inletFlow); w.f64(s.outletFlow); w.f64(s.wallFlow); w.f64(s.imbalance); w.f64(s.pressureDrop);
    w.f64(s.maxSpeed); w.f64(s.dissipation); w.f64(s.reynolds); w.f64(s.cellReynolds); w.f64(s.hydraulicDiameter);
    w.f64(s.elementsAcross); w.vec3(s.wallForce);
    w.i32(s.nonlinearIterations); w.f64(s.residual); w.u8(s.converged); w.f64(s.seconds); w.i32(s.linearSolver);
    w.i32(s.elements); w.i32(s.nodes); w.i32(s.unknowns);
}

TetFlowProblem::Stats readStats(Reader& r)
{
    TetFlowProblem::Stats s;
    s.inletFlow = r.f64(); s.outletFlow = r.f64(); s.wallFlow = r.f64(); s.imbalance = r.f64(); s.pressureDrop = r.f64();
    s.maxSpeed = r.f64(); s.dissipation = r.f64(); s.reynolds = r.f64(); s.cellReynolds = r.f64(); s.hydraulicDiameter = r.f64();
    s.elementsAcross = r.f64(); s.wallForce = r.vec3();
    s.nonlinearIterations = r.i32(); s.residual = r.f64(); s.converged = r.u8(); s.seconds = r.f64(); s.linearSolver = r.i32();
    s.elements = r.i32(); s.nodes = r.i32(); s.unknowns = r.i32();
    return s;
}

template <class R>
void setSerial(const std::shared_ptr<R>& r, uint64_t s)
{
    if (r) const_cast<typename std::remove_const<R>::type&>(*r).serial = s;
}

}   // anonymous namespace

// ------------------------------------------------------------------ tetrahedral structural

bool ResultIO::save(const TetProblem& p, const std::string& path)
{
    if (!p.m_mesh) return false;
    Writer w(path);
    if (!w.ok) return false;
    writeHeader(w, KIND_TET);
    w.vec3(p.m_lo); w.vec3(p.m_hi); w.f64(p.m_h); w.f64(p.m_E); w.f64(p.m_nu);
    w.u64(p.m_hash);
    w.i32(p.m_fixedNodes); w.i32(p.m_loadedNodes); w.i32(p.m_looseElements);
    w.vec3(p.m_totalLoad);
    writeMesh(w, *p.m_mesh);
    writeOptional(w, p.m_result);
    writeOptional(w, p.m_densityResult);
    w.floats2(p.m_densityHistory);
    w.doubles(p.m_history);
    w.raw(p.m_topDensity);
    w.doubles(p.m_frequencies);
    w.u64(p.m_modes.size());
    for (const auto& m : p.m_modes) writeOptional(w, m);
    return w.close();
}

std::unique_ptr<TetProblem> ResultIO::loadTet(const std::string& path)
{
    Reader r(path);
    if (!r.ok || !readHeader(r, KIND_TET)) return nullptr;
    const Eigen::Vector3d lo = r.vec3(), hi = r.vec3();
    const double h = r.f64(), E = r.f64(), nu = r.f64();
    if (!r.ok) return nullptr;
    std::unique_ptr<TetProblem> p(new TetProblem(Tree::invalid(), lo, hi, h, E, nu));
    p->m_hash = r.u64();
    p->m_fixedNodes = r.i32(); p->m_loadedNodes = r.i32(); p->m_looseElements = r.i32();
    p->m_totalLoad = r.vec3();
    auto mesh = std::make_shared<TetMesh>();
    if (!readMesh(r, *mesh)) return nullptr;
    p->m_mesh = mesh;
    auto locator = std::make_shared<TetLocator>(*mesh);
    if (!readOptional(r, p->m_result, mesh, locator)) return nullptr;
    if (!readOptional(r, p->m_densityResult, mesh, locator)) return nullptr;
    r.floats2(p->m_densityHistory);
    r.doubles(p->m_history);
    r.raw(p->m_topDensity);
    r.doubles(p->m_frequencies);
    const uint64_t nm = r.count(1);
    if (!r.ok) return nullptr;
    p->m_modes.clear();
    for (uint64_t i = 0; i < nm; ++i)
    {
        std::shared_ptr<MeshResult> m;
        if (!readOptional(r, m, mesh, locator)) return nullptr;
        p->m_modes.push_back(m);
    }
    for (const auto& d : p->m_densityHistory) if (d.size() != mesh->pos.size()) return nullptr;
    if (!r.ok) return nullptr;
    p->m_prepared = true;
    return p;
}

void ResultIO::assignSerials(TetProblem& p, uint64_t salt)
{
    if (!salt) return;
    setSerial(p.m_result, derivedContentSerial(p.m_hash, salt, 1));
    setSerial(p.m_densityResult, derivedContentSerial(p.m_hash, salt, 2));
    for (size_t i = 0; i < p.m_modes.size(); ++i) setSerial(p.m_modes[i], derivedContentSerial(p.m_hash, salt, 100 + i));
}

// ------------------------------------------------------------------ tetrahedral thermal

bool ResultIO::save(const TetThermalProblem& p, const std::string& path)
{
    if (!p.m_mesh || !p.m_result) return false;
    Writer w(path);
    if (!w.ok) return false;
    writeHeader(w, KIND_TETTHERMAL);
    w.vec3(p.m_lo); w.vec3(p.m_hi); w.f64(p.m_h); w.f64(p.m_k);
    w.u64(p.m_hash);
    w.f64(p.m_heatIn); w.i32(p.m_looseElements);
    writeMesh(w, *p.m_mesh);
    writeOptional(w, p.m_result);
    return w.close();
}

std::unique_ptr<TetThermalProblem> ResultIO::loadTetThermal(const std::string& path)
{
    Reader r(path);
    if (!r.ok || !readHeader(r, KIND_TETTHERMAL)) return nullptr;
    const Eigen::Vector3d lo = r.vec3(), hi = r.vec3();
    const double h = r.f64(), k = r.f64();
    if (!r.ok) return nullptr;
    std::unique_ptr<TetThermalProblem> p(new TetThermalProblem(Tree::invalid(), lo, hi, h, k));
    p->m_hash = r.u64();
    p->m_heatIn = r.f64(); p->m_looseElements = r.i32();
    auto mesh = std::make_shared<TetMesh>();
    if (!readMesh(r, *mesh)) return nullptr;
    p->m_mesh = mesh;
    auto locator = std::make_shared<TetLocator>(*mesh);
    if (!readOptional(r, p->m_result, mesh, locator) || !p->m_result) return nullptr;
    if (!r.ok) return nullptr;
    p->m_prepared = true;
    return p;
}

void ResultIO::assignSerials(TetThermalProblem& p, uint64_t salt)
{
    if (!salt) return;
    setSerial(p.m_result, derivedContentSerial(p.m_hash, salt, 1));
}

// ------------------------------------------------------------------ flow

bool ResultIO::save(const TetFlowProblem& p, const std::string& path)
{
    if (!p.m_mesh) return false;
    Writer w(path);
    if (!w.ok) return false;
    writeHeader(w, KIND_TETFLOW);
    w.vec3(p.m_lo); w.vec3(p.m_hi); w.f64(p.m_h); w.f64(p.m_rho); w.f64(p.m_mu); w.vec3(p.m_g);
    w.u64(p.m_hash);
    w.str(p.m_warning);
    writeStats(w, p.m_stats);
    w.doubles(p.m_inletFlows);
    w.doubles(p.m_outletFlows);
    w.vec3s(p.m_wallForces);
    w.raw(p.m_faceKind);
    w.raw(p.m_faceItem);
    w.doubles(p.m_faceArea);
    w.vec3s(p.m_faceNormal);
    w.vec3s(p.m_inletDir);
    w.f64(p.m_inletArea); w.f64(p.m_inletPerimeter); w.f64(p.m_wallArea); w.f64(p.m_volume); w.f64(p.m_refSpeed);
    writeMesh(w, *p.m_mesh);
    writeOptional(w, p.m_result);
    // the steps: a result shared with the final one is written once (as a reference)
    w.u64(p.m_steps.size());
    for (const auto& st : p.m_steps)
    {
        w.f64(st.time);
        w.i32(st.iteration);
        const bool same = st.result && st.result == p.m_result;
        w.u8(same);
        if (!same)
        {
            w.u8(st.result != nullptr);
            if (st.result) writeMeshResult(w, *st.result);
        }
    }
    w.u64(p.m_stepStats.size());
    for (const auto& s : p.m_stepStats) writeStats(w, s);
    w.doubles(p.m_history);
    w.doubles(p.m_dropHistory);
    writeOptional(w, p.m_densityResult);
    w.floats2(p.m_densityHistory);
    w.vec3(p.m_flowDir); w.vec3(p.m_liftDir);
    w.i32(p.m_optRejected); w.i32(p.m_optStop);
    return w.close();
}

std::unique_ptr<TetFlowProblem> ResultIO::loadTetFlow(const std::string& path)
{
    Reader r(path);
    if (!r.ok || !readHeader(r, KIND_TETFLOW)) return nullptr;
    const Eigen::Vector3d lo = r.vec3(), hi = r.vec3();
    const double h = r.f64(), rho = r.f64(), mu = r.f64();
    const Eigen::Vector3d g = r.vec3();
    if (!r.ok) return nullptr;
    std::unique_ptr<TetFlowProblem> p(new TetFlowProblem(Tree::invalid(), lo, hi, h, rho, mu));
    p->m_g = g;
    p->m_hash = r.u64();
    p->m_warning = r.str();
    p->m_stats = readStats(r);
    r.doubles(p->m_inletFlows);
    r.doubles(p->m_outletFlows);
    r.vec3s(p->m_wallForces);
    r.raw(p->m_faceKind);
    r.raw(p->m_faceItem);
    r.doubles(p->m_faceArea);
    r.vec3s(p->m_faceNormal);
    r.vec3s(p->m_inletDir);
    p->m_inletArea = r.f64(); p->m_inletPerimeter = r.f64(); p->m_wallArea = r.f64(); p->m_volume = r.f64();
    p->m_refSpeed = r.f64();
    auto mesh = std::make_shared<TetMesh>();
    if (!readMesh(r, *mesh)) return nullptr;
    p->m_mesh = mesh;
    const size_t nf = mesh->faces.size();
    if (p->m_faceKind.size() != nf || p->m_faceItem.size() != nf || p->m_faceArea.size() != nf ||
        p->m_faceNormal.size() != nf) return nullptr;
    auto locator = std::make_shared<TetLocator>(*mesh);
    p->m_locator = locator;
    if (!readOptional(r, p->m_result, mesh, locator)) return nullptr;
    const uint64_t ns = r.count(1);
    if (!r.ok) return nullptr;
    p->m_steps.clear();
    for (uint64_t k = 0; k < ns; ++k)
    {
        TetFlowProblem::Step st;
        st.time = r.f64();
        st.iteration = r.i32();
        const bool same = r.u8();
        if (same)
        {
            if (!p->m_result) return nullptr;
            st.result = p->m_result;
        }
        else if (r.u8())
        {
            auto res = readMeshResult(r, mesh, locator);
            if (!res) return nullptr;
            st.result = res;
        }
        if (!r.ok) return nullptr;
        p->m_steps.push_back(std::move(st));
    }
    const uint64_t nss = r.count(1);
    if (!r.ok) return nullptr;
    p->m_stepStats.clear();
    for (uint64_t k = 0; k < nss; ++k) p->m_stepStats.push_back(readStats(r));
    r.doubles(p->m_history);
    r.doubles(p->m_dropHistory);
    if (!readOptional(r, p->m_densityResult, mesh, locator)) return nullptr;
    r.floats2(p->m_densityHistory);
    for (const auto& d : p->m_densityHistory) if (d.size() != mesh->pos.size()) return nullptr;
    p->m_flowDir = r.vec3(); p->m_liftDir = r.vec3();
    p->m_optRejected = r.i32(); p->m_optStop = r.i32();
    if (!r.ok) return nullptr;
    p->m_prepared = true;
    return p;
}

void ResultIO::assignSerials(TetFlowProblem& p, uint64_t salt)
{
    if (!salt) return;
    for (size_t k = 0; k < p.m_steps.size(); ++k) setSerial(p.m_steps[k].result, derivedContentSerial(p.m_hash, salt, 10 + k));
    setSerial(p.m_result, derivedContentSerial(p.m_hash, salt, 1));       // (after the steps: the last step is the result)
    setSerial(p.m_densityResult, derivedContentSerial(p.m_hash, salt, 2));
}

// ------------------------------------------------------------------ voxel structural

bool ResultIO::save(const StaticProblem& p, const std::string& path)
{
    if (!p.m_prepared) return false;
    Writer w(path);
    if (!w.ok) return false;
    writeHeader(w, KIND_HEX);
    w.vec3(p.m_lo); w.f64(p.m_h); w.f64(p.m_E); w.f64(p.m_nu);
    w.i32(int(p.m_element)); w.i32(p.m_ex); w.i32(p.m_ey); w.i32(p.m_ez);
    w.u64(p.m_hash);
    w.i32(p.m_dofs); w.i32(p.m_fixedNodes); w.i32(p.m_loadedNodes); w.i32(p.m_looseElements);
    w.raw(p.m_fraction);
    w.raw(p.m_active);
    writeOptionalGrid(w, p.m_result);
    writeOptionalGrid(w, p.m_densityResult);
    w.doubles(p.m_history);
    w.raw(p.m_topDensity);
    w.doubles(p.m_frequencies);
    w.u64(p.m_modes.size());
    for (const auto& m : p.m_modes) writeOptionalGrid(w, m);
    return w.close();
}

std::unique_ptr<StaticProblem> ResultIO::loadStatic(const std::string& path)
{
    Reader r(path);
    if (!r.ok || !readHeader(r, KIND_HEX)) return nullptr;
    const Eigen::Vector3d lo = r.vec3();
    const double h = r.f64(), E = r.f64(), nu = r.f64();
    const int element = r.i32(), ex = r.i32(), ey = r.i32(), ez = r.i32();
    if (!r.ok || h <= 0 || ex <= 0 || ey <= 0 || ez <= 0) return nullptr;
    std::unique_ptr<StaticProblem> p(new StaticProblem(Tree::invalid(), lo, lo + Eigen::Vector3d(ex * h, ey * h, ez * h),
                                                       h, E, nu));
    p->m_element = element == 1 ? Element::Hex : element == 2 ? Element::HexBasic : Element::Tet;
    p->m_ex = ex; p->m_ey = ey; p->m_ez = ez;
    p->m_hash = r.u64();
    p->m_dofs = r.i32(); p->m_fixedNodes = r.i32(); p->m_loadedNodes = r.i32(); p->m_looseElements = r.i32();
    r.raw(p->m_fraction);
    r.raw(p->m_active);
    const size_t ne = size_t(ex) * size_t(ey) * size_t(ez);
    if (!r.ok || p->m_fraction.size() != ne) return nullptr;
    for (int a : p->m_active) if (a < 0 || size_t(a) >= ne) return nullptr;
    if (!readOptionalGrid(r, p->m_result)) return nullptr;
    if (!readOptionalGrid(r, p->m_densityResult)) return nullptr;
    r.doubles(p->m_history);
    r.raw(p->m_topDensity);
    r.doubles(p->m_frequencies);
    const uint64_t nm = r.count(1);
    if (!r.ok) return nullptr;
    p->m_modes.clear();
    for (uint64_t i = 0; i < nm; ++i)
    {
        std::shared_ptr<Result> m;
        if (!readOptionalGrid(r, m)) return nullptr;
        p->m_modes.push_back(m);
    }
    if (!r.ok) return nullptr;
    p->m_prepared = true;
    return p;
}

void ResultIO::assignSerials(StaticProblem& p, uint64_t salt)
{
    if (!salt) return;
    setSerial(p.m_result, derivedContentSerial(p.m_hash, salt, 1));
    setSerial(p.m_densityResult, derivedContentSerial(p.m_hash, salt, 2));
    for (size_t i = 0; i < p.m_modes.size(); ++i) setSerial(p.m_modes[i], derivedContentSerial(p.m_hash, salt, 100 + i));
}

// ------------------------------------------------------------------ voxel thermal

bool ResultIO::save(const ThermalProblem& p, const std::string& path)
{
    Writer w(path);
    if (!w.ok) return false;
    writeHeader(w, KIND_HEXTHERMAL);
    w.vec3(p.m_lo); w.f64(p.m_h); w.f64(p.m_k);
    w.i32(int(p.m_element)); w.i32(p.m_ex); w.i32(p.m_ey); w.i32(p.m_ez);
    w.i32(p.elements); w.i32(p.nodes); w.i32(p.iterations);
    w.f64(p.residual); w.f64(p.seconds); w.f64(p.heatIn); w.f64(p.heatOutFixed); w.f64(p.heatOutConvection);
    writeOptionalGrid(w, p.m_result);
    writeOptionalGrid(w, p.m_densityResult);
    w.doubles(p.m_history);
    return w.close();
}

std::unique_ptr<ThermalProblem> ResultIO::loadThermal(const std::string& path)
{
    Reader r(path);
    if (!r.ok || !readHeader(r, KIND_HEXTHERMAL)) return nullptr;
    const Eigen::Vector3d lo = r.vec3();
    const double h = r.f64(), k = r.f64();
    const int element = r.i32(), ex = r.i32(), ey = r.i32(), ez = r.i32();
    if (!r.ok || h <= 0 || ex <= 0 || ey <= 0 || ez <= 0) return nullptr;
    std::unique_ptr<ThermalProblem> p(new ThermalProblem(Tree::invalid(), lo, lo + Eigen::Vector3d(ex * h, ey * h, ez * h),
                                                         h, k));
    p->m_element = element == 1 ? Element::Hex : element == 2 ? Element::HexBasic : Element::Tet;
    p->m_ex = ex; p->m_ey = ey; p->m_ez = ez;
    p->elements = r.i32(); p->nodes = r.i32(); p->iterations = r.i32();
    p->residual = r.f64(); p->seconds = r.f64(); p->heatIn = r.f64(); p->heatOutFixed = r.f64();
    p->heatOutConvection = r.f64();
    if (!readOptionalGrid(r, p->m_result)) return nullptr;
    if (!readOptionalGrid(r, p->m_densityResult)) return nullptr;
    r.doubles(p->m_history);
    if (!r.ok) return nullptr;
    return p;
}

void ResultIO::assignSerials(ThermalProblem& p, uint64_t salt)
{
    if (!salt) return;
    const uint64_t h = p.hash();
    setSerial(p.m_result, derivedContentSerial(h, salt, 1));
    setSerial(p.m_densityResult, derivedContentSerial(h, salt, 2));
}

}   // namespace fea
}   // namespace libfive
