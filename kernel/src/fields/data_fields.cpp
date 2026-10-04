/*
libfive: a CAD kernel for modeling with implicit functions

Fields from data: scattered samples (inverse-distance weighting of the k
nearest samples, found through a uniform grid) and seeded Perlin noise.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <numeric>
#include <random>

#include "libfive/fields/field_oracles.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_storage.hpp"

namespace libfive {
namespace fields {

namespace {

using V3 = Eigen::Vector3d;

////////////////////////////////////////////////////////////////////////////////
// Scattered samples

class PointData
{
public:
    PointData(std::vector<V3> pts, std::vector<double> vals, int k, double power)
        : pts(std::move(pts)), vals(std::move(vals)), k(std::max(1, k)), power(power)
    {
        lo = hi = this->pts[0];
        for (const auto& p : this->pts) { lo = lo.cwiseMin(p); hi = hi.cwiseMax(p); }
        vmin = *std::min_element(this->vals.begin(), this->vals.end());
        vmax = *std::max_element(this->vals.begin(), this->vals.end());
        // a uniform grid with ~2 samples per cell
        const V3 ext = (hi - lo).cwiseMax(V3::Constant(1e-9));
        const double vol = ext.prod();
        cell = std::cbrt(std::max(vol, 1e-30) * 2.0 / double(this->pts.size()));
        cell = std::max(cell, 1e-6 * ext.maxCoeff());
        for (int a = 0; a < 3; ++a) n[a] = std::max(1, std::min(512, int(std::ceil(ext[a] / cell)) + 1));
        start.assign(size_t(n[0]) * n[1] * n[2] + 1, 0);
        std::vector<size_t> cellOf(this->pts.size());
        for (size_t i = 0; i < this->pts.size(); ++i)
        {
            cellOf[i] = index(cellCoord(this->pts[i]));
            start[cellOf[i] + 1]++;
        }
        for (size_t c = 1; c < start.size(); ++c) start[c] += start[c - 1];
        order.resize(this->pts.size());
        std::vector<size_t> fill(start.begin(), start.end() - 1);
        for (size_t i = 0; i < this->pts.size(); ++i) order[fill[cellOf[i]]++] = int(i);

        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const void* d, size_t m) {
            const unsigned char* c = static_cast<const unsigned char*>(d);
            for (size_t i = 0; i < m; ++i) { h ^= c[i]; h *= 1099511628211ull; }
        };
        for (const auto& p : this->pts) mix(p.data(), sizeof(double) * 3);
        mix(this->vals.data(), sizeof(double) * this->vals.size());
        mix(&k, sizeof(k));
        mix(&power, sizeof(power));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "points#%016llx", (unsigned long long)h);
        key = buf;
    }

    double value(const V3& p) const
    {
        // grow a shell of cells until k samples are found and no closer one
        // can lie outside the searched cells
        std::array<int, 3> c = cellCoord(p);
        std::vector<std::pair<double, int>> best;
        best.reserve(size_t(k) + 8);
        for (int r = 0; r < 1024; ++r)
        {
            for (int dz = -r; dz <= r; ++dz)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx)
                    {
                        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != r) continue;
                        const int x = c[0] + dx, y = c[1] + dy, z = c[2] + dz;
                        if (x < 0 || y < 0 || z < 0 || x >= n[0] || y >= n[1] || z >= n[2]) continue;
                        const size_t id = index({x, y, z});
                        for (size_t q = start[id]; q < start[id + 1]; ++q)
                        {
                            const int i = order[q];
                            best.push_back({(pts[size_t(i)] - p).squaredNorm(), i});
                        }
                    }
            if (int(best.size()) >= k)
            {
                std::partial_sort(best.begin(), best.begin() + k, best.end());
                best.resize(size_t(k));
                // anything outside the searched shells is at least r cells away
                const double reach = r * cell;
                if (best.back().first <= reach * reach || r >= std::max({n[0], n[1], n[2]}))
                    break;
            }
            if (r > std::max({n[0], n[1], n[2]}) + 1) break;
        }
        if (best.empty()) return 0.0;
        double num = 0, den = 0;
        for (const auto& b : best)
        {
            if (b.first < 1e-24) return vals[size_t(b.second)];
            const double w = 1.0 / std::pow(b.first, 0.5 * power);
            num += w * vals[size_t(b.second)];
            den += w;
        }
        return num / den;
    }

    double vmin, vmax;
    std::string key;

private:
    std::array<int, 3> cellCoord(const V3& p) const
    {
        std::array<int, 3> c;
        for (int a = 0; a < 3; ++a)
            c[size_t(a)] = std::max(0, std::min(n[a] - 1, int(std::floor((p[a] - lo[a]) / cell))));
        return c;
    }
    size_t index(const std::array<int, 3>& c) const
    {
        return (size_t(c[2]) * n[1] + c[1]) * n[0] + c[0];
    }

    std::vector<V3> pts;
    std::vector<double> vals;
    int k;
    double power;
    V3 lo, hi;
    double cell = 1;
    int n[3] = {1, 1, 1};
    std::vector<size_t> start;
    std::vector<int> order;
};

////////////////////////////////////////////////////////////////////////////////
// Perlin noise (improved noise, fractal sum of octaves)

class NoiseData
{
public:
    NoiseData(double scale, int octaves, unsigned seed, double gain, double lacunarity)
        : scale(scale), octaves(std::max(1, std::min(12, octaves))), gain(gain),
          lacunarity(lacunarity)
    {
        std::vector<int> p(256);
        std::iota(p.begin(), p.end(), 0);
        std::mt19937 rng(seed);
        std::shuffle(p.begin(), p.end(), rng);
        for (int i = 0; i < 512; ++i) perm[size_t(i)] = p[size_t(i & 255)];
        double a = 1;
        amp = 0;
        for (int o = 0; o < this->octaves; ++o) { amp += a; a *= gain; }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "noise#%.17g#%d#%u#%.17g#%.17g", scale, this->octaves, seed, gain, lacunarity);
        key = buf;
    }

    double value(const V3& p) const
    {
        double sum = 0, a = 1, f = 1.0 / scale;
        for (int o = 0; o < octaves; ++o)
        {
            sum += a * noise(p[0] * f + 17.31 * o, p[1] * f + 5.73 * o, p[2] * f + 29.17 * o);
            a *= gain;
            f *= lacunarity;
        }
        return sum / amp;          // (about -1 .. 1)
    }

    std::string key;

private:
    static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double lerp(double t, double a, double b) { return a + t * (b - a); }
    static double grad(int h, double x, double y, double z)
    {
        const int hh = h & 15;
        const double u = hh < 8 ? x : y, v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : z);
        return ((hh & 1) ? -u : u) + ((hh & 2) ? -v : v);
    }
    double noise(double x, double y, double z) const
    {
        const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
        const int X = int(fx) & 255, Y = int(fy) & 255, Z = int(fz) & 255;
        x -= fx; y -= fy; z -= fz;
        const double u = fade(x), v = fade(y), w = fade(z);
        const int A = perm[size_t(X)] + Y, AA = perm[size_t(A)] + Z, AB = perm[size_t(A + 1)] + Z;
        const int B = perm[size_t(X + 1)] + Y, BA = perm[size_t(B)] + Z, BB = perm[size_t(B + 1)] + Z;
        return lerp(w, lerp(v, lerp(u, grad(perm[size_t(AA)], x, y, z), grad(perm[size_t(BA)], x - 1, y, z)),
                               lerp(u, grad(perm[size_t(AB)], x, y - 1, z), grad(perm[size_t(BB)], x - 1, y - 1, z))),
                       lerp(v, lerp(u, grad(perm[size_t(AA + 1)], x, y, z - 1), grad(perm[size_t(BA + 1)], x - 1, y, z - 1)),
                               lerp(u, grad(perm[size_t(AB + 1)], x, y - 1, z - 1),
                                    grad(perm[size_t(BB + 1)], x - 1, y - 1, z - 1))));
    }

    double scale;
    int octaves;
    double gain, lacunarity, amp = 1;
    std::array<int, 512> perm;
};

////////////////////////////////////////////////////////////////////////////////

template <class Data>
class DataOracle : public OracleStorage<>
{
public:
    DataOracle(std::shared_ptr<const Data> d, float lo, float hi)
        : data(std::move(d)), lo(lo), hi(hi) {}

    // (the interpolation of scattered data costs a sum over the data at each point: remembered by the data's key)
    const std::string* memoKey() const override { return &data->key; }

    void evalInterval(Interval& out) override { out = Interval(lo, hi); }

    void evalPoint(float& out, size_t index) override
    {
        out = float(data->value(points.col(long(index)).matrix().template cast<double>()));
    }

    void checkAmbiguous(Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>,
                                     1, Eigen::Dynamic> /* out */) override {}

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        const V3 p = points.col(0).matrix().template cast<double>();
        const double h = 1e-4 * std::max(1.0, p.norm());
        Eigen::Vector3f g;
        for (int a = 0; a < 3; ++a)
        {
            V3 q = p, r = p;
            q[a] += h;
            r[a] -= h;
            g[a] = float((data->value(q) - data->value(r)) / (2 * h));
        }
        out.push_back(Feature(g));
    }

private:
    std::shared_ptr<const Data> data;
    float lo, hi;
};

template <class Data>
class DataClause : public OracleClause
{
public:
    DataClause(std::shared_ptr<const Data> d, float lo, float hi, std::string name)
        : data(std::move(d)), lo(lo), hi(hi), label(std::move(name)) {}
    std::unique_ptr<Oracle> getOracle() const override
    {
        return std::make_unique<DataOracle<Data>>(data, lo, hi);
    }
    std::string name() const override { return label; }
    std::string contentKey() const override { return data->key; }
    std::string persistentKey() const override { return data->key; }    // (a hash of the data)

private:
    std::shared_ptr<const Data> data;
    float lo, hi;
    std::string label;
};

}   // anonymous namespace

Tree pointCloudField(const std::vector<Eigen::Vector3d>& points,
                     const std::vector<double>& values, int k, double power)
{
    if (points.empty() || points.size() != values.size()) return Tree(0.0f);
    auto d = std::make_shared<const PointData>(points, values, k, power);
    return Tree(std::make_unique<DataClause<PointData>>(
        d, float(d->vmin), float(d->vmax), "PointCloudField"));
}

Tree noiseField(double scale, int octaves, unsigned seed, double gain, double lacunarity)
{
    auto d = std::make_shared<const NoiseData>(std::max(1e-9, scale), octaves, seed, gain, lacunarity);
    return Tree(std::make_unique<DataClause<NoiseData>>(d, -1.1f, 1.1f, "NoiseField"));
}

}   // namespace fields
}   // namespace libfive
