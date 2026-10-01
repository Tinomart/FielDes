/*
libfive: a CAD kernel for modeling with implicit functions

The numbers of a tree that are geometry, made variables so they can be dragged;
see expose.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libfive/tree/expose.hpp"
#include "libfive/tree/data.hpp"

namespace libfive {

namespace {

bool isConstant(const Tree& t)
{
    return std::get_if<TreeConstant>(t.get()) != nullptr;
}

// A constant that places a surface (not a "nothing here" sentinel of a union)
bool isGeometry(const Tree& t)
{
    const auto c = std::get_if<TreeConstant>(t.get());
    return c && !std::isnan(c->value) && std::fabs(c->value) < 1e6f;
}

/*
 *  What a node is, for finding where a surface lacks a number: `kind` 0 is anything, 1 a constant, 2 an
 *  expression that is a sum of coordinates times constants (a plane's distance, or a coordinate
 *  itself); `positioned` says the expression already has a number that places it (a subtraction of one of
 *  the constants counted).  A plane through the origin, or a sphere's or a cylinder's axis through it, has
 *  no number in the tree at all -- x - 0 is written x -- and gets one here.
 */
struct Info
{
    char kind = 0;
    bool positioned = false;
};

Info infoOf(const TreeData* d, const std::unordered_map<const TreeData*, Info>& info)
{
    Info out;
    if (std::get_if<TreeConstant>(d))
    {
        out.kind = 1;
    }
    else if (auto n = std::get_if<TreeNonaryOp>(d))
    {
        if (n->op == Opcode::VAR_X || n->op == Opcode::VAR_Y || n->op == Opcode::VAR_Z) out.kind = 2;
    }
    else if (auto u = std::get_if<TreeUnaryOp>(d))
    {
        if (u->op == Opcode::OP_NEG) out = info.at(u->lhs.get());
    }
    else if (auto b = std::get_if<TreeBinaryOp>(d))
    {
        const Info l = info.at(b->lhs.get()), r = info.at(b->rhs.get());
        if (b->op == Opcode::OP_ADD || b->op == Opcode::OP_SUB)
        {
            if (l.kind && r.kind)
            {
                out.kind = std::max(l.kind, r.kind);
                out.positioned = l.positioned || r.positioned;
                if (b->op == Opcode::OP_SUB &&
                    ((isGeometry(b->lhs) && !isConstant(b->rhs)) || (isGeometry(b->rhs) && !isConstant(b->lhs))))
                {
                    out.positioned = true;
                }
            }
        }
        else if (b->op == Opcode::OP_MUL)
        {
            if (l.kind == 1 && r.kind) { out.kind = r.kind; out.positioned = r.positioned; }
            else if (r.kind == 1 && l.kind) { out.kind = l.kind; out.positioned = l.positioned; }
        }
        else if (b->op == Opcode::OP_DIV)
        {
            if (r.kind == 1 && l.kind) { out.kind = l.kind; out.positioned = l.positioned; }
        }
    }
    return out;
}

/*
 *  Takes the tree apart leaves first and puts it back together, asking `place` for what to put where a
 *  geometry constant is (a heap-allocated walk: trees can be too deep to recurse on).  `rebuild` false
 *  leaves the nodes alone (the walk is only to find the constants).
 */
Tree walk(const Tree& root0, bool rebuild, const std::function<Tree(const Tree&)>& place)
{
    // (a tree with remaps is taken as the plain tree they make)
    const Tree root = (root0.get()->flags & TreeData::TREE_FLAG_HAS_REMAP) ? root0.flatten() : root0;
    std::unordered_map<const TreeData*, Tree> memo;
    std::unordered_map<const TreeData*, Info> info;
    std::vector<std::pair<Tree, bool>> stack;
    stack.push_back({root, false});
    // A plane through the origin (or the centre of a sphere or a cylinder's axis there) is put at a
    // number: x becomes x - c with c = 0 for it
    // (one number to a node: a plane shared by several unions or intersections -- as the cubes
    // of an imported part share theirs -- is one plane, not one for each of them)
    std::unordered_map<const TreeData*, Tree> placed;
    auto positionedAt = [&](const Tree& child, const Tree& now) -> Tree {
        const Info i = info.at(child.get());
        if (i.kind == 2 && !i.positioned)
        {
            const auto it = placed.find(child.get());
            if (it != placed.end()) return it->second;
            const Tree at = Tree::binary(Opcode::OP_SUB, now, place(Tree(0.0f)));
            placed.emplace(child.get(), at);
            return at;
        }
        return now;
    };
    while (!stack.empty())
    {
        const auto top = stack.back();
        stack.pop_back();
        const Tree& node = top.first;
        if (memo.count(node.get())) continue;
        const auto un = std::get_if<TreeUnaryOp>(node.get());
        const auto bi = std::get_if<TreeBinaryOp>(node.get());
        if (!top.second && (un || bi))
        {
            stack.push_back({node, true});
            // (the right child goes first so that the left one is taken first)
            if (bi && !memo.count(bi->rhs.get())) stack.push_back({bi->rhs, false});
            const Tree& first = un ? un->lhs : bi->lhs;
            if (!memo.count(first.get())) stack.push_back({first, false});
            continue;
        }
        info.emplace(node.get(), infoOf(node.get(), info));
        if (un)
        {
            Tree a = memo.at(un->lhs.get());
            if (un->op == Opcode::OP_SQUARE || un->op == Opcode::OP_ABS) a = positionedAt(un->lhs, a);
            memo.emplace(node.get(), (rebuild && a != un->lhs) ? Tree::unary(un->op, a) : node);
        }
        else if (bi)
        {
            Tree a = memo.at(bi->lhs.get()), b = memo.at(bi->rhs.get());
            // one side of a subtraction is a geometry constant and the other is not a constant at all
            if (bi->op == Opcode::OP_SUB)
            {
                if (isGeometry(bi->lhs) && !isConstant(bi->rhs)) a = place(bi->lhs);
                else if (isGeometry(bi->rhs) && !isConstant(bi->lhs)) b = place(bi->rhs);
            }
            // the sides of a union or an intersection that are planes with no number
            else if (bi->op == Opcode::OP_MAX || bi->op == Opcode::OP_MIN)
            {
                a = positionedAt(bi->lhs, a);
                b = positionedAt(bi->rhs, b);
            }
            const bool same = (a == bi->lhs) && (b == bi->rhs);
            memo.emplace(node.get(), (rebuild && !same) ? Tree::binary(bi->op, a, b) : node);
        }
        else
        {
            memo.emplace(node.get(), node);       // (a leaf, or an oracle: as it is)
        }
    }
    return memo.at(root.get());
}

}   // anonymous namespace

std::vector<float> exposableConstants(const Tree& t)
{
    std::vector<float> out;
    if (!t.get()) return out;
    walk(t, false, [&out](const Tree& c) {
        out.push_back(std::get_if<TreeConstant>(c.get())->value);
        return c;
    });
    return out;
}

Tree exposeConstants(const Tree& t, const std::vector<Tree>& with)
{
    if (!t.get()) return Tree::invalid();
    size_t used = 0;
    bool overrun = false;
    const Tree out = walk(t, true, [&](const Tree& c) {
        if (used >= with.size())
        {
            overrun = true;
            return c;
        }
        return with[used++];
    });
    return (overrun || used != with.size()) ? Tree::invalid() : out;
}

}   // namespace libfive
