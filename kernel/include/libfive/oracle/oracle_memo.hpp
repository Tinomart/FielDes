/*
libfive: a CAD kernel for modeling with implicit functions

A memory of what an expensive oracle (a field computed from a mesh, a graph, a
set of data) has answered: its value at each point it was asked about.

Nothing is approximated: a memo returns exactly the value the oracle computed
for exactly that point.  It is kept by the oracle's content key (what it
computes, from what), shared by every instance of it and by every tree rebuilt
with the same key -- so a script run again, or a shape rendered again after a
change that leaves this oracle's inputs alone, asks each point once -- and it
is dropped only when its key is never asked for again (the least recently used
memos go first, within a memory budget).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <Eigen/Eigen>

namespace libfive {

class OracleMemo
{
public:
    /*  The memo of an oracle with this content key (made if there is none)  */
    static std::shared_ptr<OracleMemo> get(const std::string& key);

    /*  Its answer at p, if it has one  */
    bool lookup(const Eigen::Vector3f& p, float& value);
    void store(const Eigen::Vector3f& p, float value);

    /*  Drops every memo (and how many points all of them hold, for a report)  */
    static void clear();
    static size_t entries();
    static size_t memos();

    OracleMemo();

private:
    struct Key
    {
        uint32_t a, b, c;
        bool operator==(const Key& o) const { return a == o.a && b == o.b && c == o.c; }
    };
    struct Hash
    {
        size_t operator()(const Key& k) const
        {
            uint64_t h = 1469598103934665603ull;
            for (uint32_t v : {k.a, k.b, k.c})
            {
                h ^= v;
                h *= 1099511628211ull;
                h ^= h >> 29;
            }
            return size_t(h);
        }
    };
    static Key keyOf(const Eigen::Vector3f& p);

    static const int kShards = 64;
    struct Shard
    {
        std::mutex mutex;
        std::unordered_map<Key, float, Hash> map;
    };
    Shard m_shards[kShards];
    uint64_t m_used = 0;                    // (registry ordering: the least recently used goes first)
    friend struct OracleMemoRegistry;
};

}   // namespace libfive
