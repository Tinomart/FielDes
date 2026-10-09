/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <map>
#include <mutex>
#include <shared_mutex>

#include "libfive/eval/default_vars.hpp"

namespace libfive {

namespace {

std::shared_mutex& lock()
{
    static std::shared_mutex m;
    return m;
}

std::map<Tree::Id, float>& table()
{
    static std::map<Tree::Id, float> t;
    return t;
}

}   // anonymous namespace

void DefaultVars::set(Tree::Id var, float value)
{
    std::unique_lock<std::shared_mutex> guard(lock());
    table()[var] = value;
}

void DefaultVars::clear()
{
    std::unique_lock<std::shared_mutex> guard(lock());
    table().clear();
}

bool DefaultVars::find(Tree::Id var, float& value)
{
    std::shared_lock<std::shared_mutex> guard(lock());
    const auto it = table().find(var);
    if (it == table().end()) return false;
    value = it->second;
    return true;
}

bool DefaultVars::empty()
{
    std::shared_lock<std::shared_mutex> guard(lock());
    return table().empty();
}

void DefaultVars::forEach(const std::function<void(Tree::Id, float)>& f)
{
    std::shared_lock<std::shared_mutex> guard(lock());
    for (const auto& v : table()) f(v.first, v.second);
}

}   // namespace libfive
