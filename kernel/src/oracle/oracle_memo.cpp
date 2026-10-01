/*
libfive: a CAD kernel for modeling with implicit functions

The memory of what an oracle has answered; see oracle_memo.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cstring>
#include <vector>

#include "libfive/oracle/oracle_memo.hpp"

namespace libfive {

// All memos, by key.  Each holds up to kPerMemo points and all of them together
// kTotal (about 30 bytes a point: a memory budget of a few hundred MB); when the
// total is over, the memo used least recently is dropped.
struct OracleMemoRegistry
{
    static const size_t kPerMemo = 12000000;
    static const size_t kTotal = 24000000;

    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<OracleMemo>> memos;
    uint64_t clock = 0;

    static OracleMemoRegistry& instance()
    {
        static OracleMemoRegistry r;
        return r;
    }

    size_t entries()
    {
        size_t n = 0;
        for (auto& m : memos)
            for (auto& s : m.second->m_shards)
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                n += s.map.size();
            }
        return n;
    }

    void trim()
    {
        while (entries() > kTotal && memos.size() > 1)
        {
            auto oldest = memos.begin();
            for (auto it = memos.begin(); it != memos.end(); ++it)
                if (it->second->m_used < oldest->second->m_used) oldest = it;
            memos.erase(oldest);
        }
    }
};

OracleMemo::OracleMemo() {}

std::shared_ptr<OracleMemo> OracleMemo::get(const std::string& key)
{
    auto& reg = OracleMemoRegistry::instance();
    std::lock_guard<std::mutex> lock(reg.mutex);
    auto it = reg.memos.find(key);
    if (it == reg.memos.end())
    {
        reg.trim();
        it = reg.memos.emplace(key, std::make_shared<OracleMemo>()).first;
    }
    it->second->m_used = ++reg.clock;
    return it->second;
}

OracleMemo::Key OracleMemo::keyOf(const Eigen::Vector3f& p)
{
    Key k;
    std::memcpy(&k.a, &p.x(), sizeof(uint32_t));
    std::memcpy(&k.b, &p.y(), sizeof(uint32_t));
    std::memcpy(&k.c, &p.z(), sizeof(uint32_t));
    return k;
}

bool OracleMemo::lookup(const Eigen::Vector3f& p, float& value)
{
    const Key k = keyOf(p);
    Shard& s = m_shards[Hash()(k) % kShards];
    std::lock_guard<std::mutex> lock(s.mutex);
    const auto it = s.map.find(k);
    if (it == s.map.end()) return false;
    value = it->second;
    return true;
}

void OracleMemo::store(const Eigen::Vector3f& p, float value)
{
    const Key k = keyOf(p);
    Shard& s = m_shards[Hash()(k) % kShards];
    std::lock_guard<std::mutex> lock(s.mutex);
    // (a memo that is full stops taking points; the oracle answers those itself)
    if (s.map.size() * kShards >= OracleMemoRegistry::kPerMemo) return;
    s.map.emplace(k, value);
}

void OracleMemo::clear()
{
    auto& reg = OracleMemoRegistry::instance();
    std::lock_guard<std::mutex> lock(reg.mutex);
    reg.memos.clear();
}

size_t OracleMemo::entries()
{
    auto& reg = OracleMemoRegistry::instance();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return reg.entries();
}

size_t OracleMemo::memos()
{
    auto& reg = OracleMemoRegistry::instance();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return reg.memos.size();
}

}   // namespace libfive
