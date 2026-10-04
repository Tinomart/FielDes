/*
libfive: a CAD kernel for modeling with implicit functions

An exact key of what a tree is; see content_key.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libfive/tree/content_key.hpp"
#include "libfive/tree/data.hpp"
#include "libfive/oracle/oracle_clause.hpp"

namespace libfive {

namespace {

// A 128-bit hash: two lanes of a 64-bit mixer with different constants
struct Hash
{
    uint64_t a = 0x243f6a8885a308d3ULL;
    uint64_t b = 0x13198a2e03707344ULL;
};

inline uint64_t finalize(uint64_t x)
{
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

inline void add(Hash& h, uint64_t v)
{
    h.a = finalize(h.a ^ (v + 0x9e3779b97f4a7c15ULL));
    h.b = finalize(h.b + (v ^ 0xd6e8feb86659fd93ULL)) + 0x632be59bd9b4e019ULL;
}

inline void add(Hash& h, const Hash& o)
{
    add(h, o.a);
    add(h, o.b);
}

inline void add(Hash& h, const std::string& s)
{
    add(h, uint64_t(s.size()));
    uint64_t word = 0;
    int n = 0;
    for (unsigned char c : s)
    {
        word = (word << 8) | c;
        if (++n == 8)
        {
            add(h, word);
            word = 0;
            n = 0;
        }
    }
    add(h, word);
}

// The nodes a node is built from, in order
int children(const TreeData* d, const TreeData* out[4])
{
    if (auto u = std::get_if<TreeUnaryOp>(d))
    {
        out[0] = u->lhs.get();
        return 1;
    }
    if (auto b = std::get_if<TreeBinaryOp>(d))
    {
        out[0] = b->lhs.get();
        out[1] = b->rhs.get();
        return 2;
    }
    if (auto r = std::get_if<TreeRemap>(d))
    {
        out[0] = r->x.get();
        out[1] = r->y.get();
        out[2] = r->z.get();
        out[3] = r->t.get();
        return 4;
    }
    if (auto a = std::get_if<TreeApply>(d))
    {
        out[0] = a->target.get();
        out[1] = a->value.get();
        out[2] = a->t.get();
        return 3;
    }
    return 0;
}

// How a key is made: the usual way, or for keeping (treePersistentKey)
struct KeyMode
{
    const std::map<Tree::Id, float>* vars = nullptr;    // set: a key to keep
    bool ok = true;                                     // (a key to keep: false when the tree has none)
};

Hash keyOf(const Tree& root, KeyMode& mode)
{
    // (a heap-allocated walk: trees can be too deep to recurse on)
    std::unordered_map<const TreeData*, Hash> memo;
    std::vector<std::pair<const TreeData*, bool>> stack;
    stack.push_back({root.get(), false});
    while (!stack.empty())
    {
        const auto top = stack.back();
        stack.pop_back();
        const TreeData* d = top.first;
        if (memo.count(d)) continue;
        const TreeData* kid[4] = {nullptr, nullptr, nullptr, nullptr};
        const int n = children(d, kid);
        if (!top.second)
        {
            stack.push_back({d, true});
            for (int i = 0; i < n; ++i)
            {
                if (!memo.count(kid[i])) stack.push_back({kid[i], false});
            }
            continue;
        }

        Hash h;
        if (auto c = std::get_if<TreeConstant>(d))
        {
            add(h, uint64_t(1));
            uint32_t bits;
            const float v = std::isnan(c->value) ? std::numeric_limits<float>::quiet_NaN() : c->value;
            std::memcpy(&bits, &v, sizeof(bits));
            add(h, uint64_t(bits));
        }
        else if (auto o = std::get_if<TreeNonaryOp>(d))
        {
            add(h, uint64_t(2));
            add(h, uint64_t(o->op));
            // (a free variable is the variable itself: its value is not part of the tree)
            if (o->op == Opcode::VAR_FREE)
            {
                if (!mode.vars)
                {
                    add(h, uint64_t(reinterpret_cast<uintptr_t>(d)));
                }
                else
                {
                    // (to keep, its address means nothing: the number it stands for does)
                    const auto v = mode.vars->find(d);
                    if (v == mode.vars->end())
                    {
                        mode.ok = false;
                    }
                    else
                    {
                        uint32_t bits;
                        const float val = v->second;
                        std::memcpy(&bits, &val, sizeof(bits));
                        add(h, uint64_t(bits));
                    }
                }
            }
        }
        else if (auto u = std::get_if<TreeUnaryOp>(d))
        {
            add(h, uint64_t(3));
            add(h, uint64_t(u->op));
            add(h, memo.at(kid[0]));
        }
        else if (auto b = std::get_if<TreeBinaryOp>(d))
        {
            add(h, uint64_t(4));
            add(h, uint64_t(b->op));
            add(h, memo.at(kid[0]));
            add(h, memo.at(kid[1]));
        }
        else if (auto oc = std::get_if<TreeOracle>(d))
        {
            const OracleClause& clause = *oc->oracle;
            add(h, uint64_t(5));
            add(h, clause.name());
            const std::string content = mode.vars ? clause.persistentKey() : clause.contentKey();
            if (!content.empty())
            {
                add(h, uint64_t(6));
                add(h, content);
            }
            else if (mode.vars)
            {
                // (to keep, there is nothing to say what it is: no key)
                mode.ok = false;
            }
            else
            {
                // no content key: it is this very oracle
                add(h, uint64_t(7));
                add(h, uint64_t(reinterpret_cast<uintptr_t>(&clause)));
            }
            for (const Tree& dep : clause.dependencies())
            {
                add(h, dep.get() ? keyOf(dep, mode) : Hash());
            }
        }
        else if (std::get_if<TreeRemap>(d))
        {
            add(h, uint64_t(8));
            for (int i = 0; i < n; ++i) add(h, memo.at(kid[i]));
        }
        else if (std::get_if<TreeApply>(d))
        {
            add(h, uint64_t(9));
            for (int i = 0; i < n; ++i) add(h, memo.at(kid[i]));
        }
        else
        {
            add(h, uint64_t(10));
        }
        memo.emplace(d, h);
    }
    return memo.at(root.get());
}

}   // anonymous namespace

namespace {
std::string keyText(const Hash& h)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  static_cast<unsigned long long>(h.a), static_cast<unsigned long long>(h.b));
    return buf;
}
}   // anonymous namespace

std::string treeContentKey(const Tree& t)
{
    if (!t.get()) return std::string();
    KeyMode mode;
    return keyText(keyOf(t, mode));
}

std::string treePersistentKey(const Tree& t, const std::map<Tree::Id, float>& vars)
{
    if (!t.get()) return std::string();
    KeyMode mode;
    mode.vars = &vars;
    const Hash h = keyOf(t, mode);
    return mode.ok ? keyText(h) : std::string();
}

std::string meshContentKey(const std::vector<Eigen::Vector3d>& verts,
                           const std::vector<std::array<uint32_t, 3>>& tris)
{
    std::vector<std::array<double, 9>> rows;
    rows.reserve(tris.size());
    auto less = [&](uint32_t a, uint32_t b) {
        const auto& p = verts[a];
        const auto& q = verts[b];
        if (p[0] != q[0]) return p[0] < q[0];
        if (p[1] != q[1]) return p[1] < q[1];
        return p[2] < q[2];
    };
    for (const auto& t : tris)
    {
        // (the corner that comes first, then the others in the order of the triangle: its winding is kept)
        int first = 0;
        for (int k = 1; k < 3; ++k)
            if (less(t[size_t(k)], t[size_t(first)])) first = k;
        std::array<double, 9> r;
        for (int k = 0; k < 3; ++k)
        {
            const auto& p = verts[t[size_t((first + k) % 3)]];
            r[size_t(3 * k)] = p[0];
            r[size_t(3 * k + 1)] = p[1];
            r[size_t(3 * k + 2)] = p[2];
        }
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end());
    Hash h;
    add(h, uint64_t(rows.size()));
    for (const auto& r : rows)
    {
        for (double d : r)
        {
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof(bits));
            add(h, bits);
        }
    }
    return keyText(h);
}

uint64_t nextContentSerial()
{
    static std::atomic<uint64_t> next{0};
    return ++next;
}

}   // namespace libfive
