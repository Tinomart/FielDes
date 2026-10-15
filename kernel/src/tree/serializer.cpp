/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2018  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <iostream>
#include <map>
#include <cassert>

#include "libfive/tree/serializer.hpp"
#include "libfive/tree/tree.hpp"
#include "libfive/oracle/oracle_clause.hpp"

namespace libfive {

const uint8_t Serializer::END_OF_ITEM = 0xFF;

void Serializer::run(Archive& a)
{
    static_assert(Opcode::LAST_OP <= 254, "Too many opcodes");

    for (const auto& s : a.shapes)
    {
        serializeShape(s);
    }
}

void Serializer::serializeTree(const Tree& t)
{
    // (remaps are expanded; calls are kept: a field that is called is written once, before the calls, and they point at it)
    const Tree flat = t.flatten(true, Tree::Calls::Keep);      // (kept: walk() of a tree with remaps points into a flattened copy)
    for (auto& n : flat.walk())
    {
        // Skip this id, as it has already been stored
        if (ids.find(n) != ids.end())
        {
            continue;
        }
        if (n->op() == Opcode::ORACLE)
        {
            for (auto& d : n->oracle_clause().dependencies())
            {
                serializeTree(d);
            }
        }
        else if (n->op() == Opcode::CALL)
        {
            serializeTree(n->call().f);
        }
        out.put(n->op());
        ids.insert({ n, (uint32_t)ids.size() });

        // Write constants as raw bytes
        if (n->op() == Opcode::CONSTANT) {
            serializeBytes(n->value());
        } else if (n->op ()== Opcode::ORACLE) {
            serializeString(n->oracle_clause().name());
            OracleClause::serialize(n->oracle_clause().name(), &n->oracle_clause(), *this);
        } else if (n->op() == Opcode::CALL) {
            // the field that is called, then z, y and x (the order the binary operations write theirs in, reversed)
            const TreeCall& c = n->call();
            serializeBytes(ids.at(c.f.get()));
            serializeBytes(ids.at(c.z.get()));
            serializeBytes(ids.at(c.y.get()));
            serializeBytes(ids.at(c.x.get()));
            continue;
        }

        switch (Opcode::args(n->op())) {
            case 2:  serializeBytes(ids.at(n->rhs().get())); // FALLTHRU
            case 1:  serializeBytes(ids.at(n->lhs().get())); // FALLTHRU
            default: break;
        }
    }
}

void Serializer::serializeShape(const Archive::Shape& s)
{
    // 'T' indicate a fully-serialized tree;
    // 't' indicates an id pointing to an earlier tree.
    const bool already_stored = ids.find(s.tree.id()) != ids.end();
    out.put(already_stored ? 't' : 'T');

    serializeString(s.name);
    serializeString(s.doc);

    if (already_stored)
    {
        serializeBytes(ids.at(s.tree.id()));
    }
    else
    {
        serializeTree(s.tree);
        out.put(END_OF_ITEM);
    }
    for (auto& v : s.vars)
    {
        auto a = ids.find(v.first);
        if (a == ids.end())
        {
            std::cerr << "Archive::serialize: named variable not found.";
        }
        else
        {
            serializeString(v.second);
            serializeBytes(a->second);
        }
    }
    out.put(END_OF_ITEM);
}

void Serializer::serializeString(const std::string& s)
{
    out.put('"');
    for (auto& c : s)
    {
        if (c == '"' || c == '\\')
        {
            out.put('\\');
        }
        out.put(c);
    }
    out.put('"');
}

}   // namespace libfive
