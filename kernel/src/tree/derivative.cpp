/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>

#include "libfive/tree/derivative.hpp"
#include "libfive/tree/data.hpp"

namespace libfive {

namespace {

// (a derivative that is empty is 0: a term that is 0 is left out of every sum and product, so the tree stays as small as the field's own)
using Term = std::optional<Tree>;

struct Key {
    const TreeData* node;
    int axis;
    bool operator==(const Key& o) const { return node == o.node && axis == o.axis; }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        return std::hash<const void*>()(k.node) * 3u + size_t(k.axis);
    }
};

Term plus(const Term& a, const Term& b) {
    if (!a) return b;
    if (!b) return a;
    return Term(*a + *b);
}

Term minus(const Term& a, const Term& b) {
    if (!b) return a;
    if (!a) return Term(-*b);
    return Term(*a - *b);
}

Term times(const Term& a, const Tree& k) {
    if (!a) return Term();
    return Term(k * *a);
}

// A quotient whose divisor can be 0 where its numerator is too: sqrt(0) is 0 and has da = 0 as well, and the 0 / 0 of its rule is NaN, which then goes into every
// field made from the gradient (the exact distance to a box is a sqrt of a sum of squares that is exactly 0 inside it).  The limit that is meant there is 0: where the
// value is NaN it is read as 0.  A numerator that is not 0 over a divisor of 0 is an infinite slope (the sqrt at 0), which is true, and is left.
Tree finite(const Tree& t) {
    return nanfill(t, Tree(0.0));
}

class Deriver {
public:
    explicit Deriver(float oracle_step) : oracle_step(oracle_step) { /* Nothing to do here */ }

    // Set when a node was met that has no derivative (an oracle with no step, a tree that was not flat)
    bool failed = false;

    Term of(const Tree& root, int axis) {
        run(root, axis);
        return memo.at(Key{root.get(), axis});
    }

    // How deep calls are nested in the tree: 0 for a tree with no call, 1 for one whose calls call plain trees, and so on (a call's arguments count, and so does the field that
    // it calls).  A function of the node alone, kept; a callee is a tree of its own and is walked when a call is met.
    int depth(const Tree& root) {
        const auto hit = depths.find(root.get());
        if (hit != depths.end()) {
            return hit->second;
        }
        for (const TreeData* n : root.walk()) {
            if (depths.count(n)) {
                continue;
            }
            int d = 0;
            if (auto u = std::get_if<TreeUnaryOp>(n)) {
                d = depths.at(u->lhs.get());
            } else if (auto b = std::get_if<TreeBinaryOp>(n)) {
                d = std::max(depths.at(b->lhs.get()), depths.at(b->rhs.get()));
            } else if (auto c = std::get_if<TreeCall>(n)) {
                d = 1 + std::max(std::max(depths.at(c->x.get()), depths.at(c->y.get())),
                                 std::max(depths.at(c->z.get()), depth(c->f)));
            }
            depths.emplace(n, d);
        }
        return depths.at(root.get());
    }

private:
    float oracle_step;
    std::unordered_map<const TreeData*, int> depths;
    // The derivative of every node reached, for each axis.  It is a function of the node alone (a node means the same in every tree that holds it),
    // so a node shared by many, and the field that many calls call, is derived once.
    std::unordered_map<Key, Term, KeyHash> memo;
    // The weights of a min or a max: (1 where the first is smaller, 1 where it is larger), made once for the three axes
    std::unordered_map<const TreeData*, std::pair<Tree, Tree>> weights;

    // The derivative of every node of the tree `root`, leaves to root (a call's callee is a tree of its own: see call())
    void run(const Tree& root, int axis) {
        if (memo.count(Key{root.get(), axis})) {
            return;
        }
        for (const TreeData* n : root.walk()) {
            const Key k{n, axis};
            if (memo.count(k)) {
                continue;
            }
            Term d = node(n, axis);
            memo.emplace(k, std::move(d));
        }
    }

    Term child(const Tree& t, int axis) const {
        return memo.at(Key{t.get(), axis});
    }

    Term node(const TreeData* n, int axis) {
        if (auto u = std::get_if<TreeUnaryOp>(n)) {
            return unary(n, *u, axis);
        } else if (auto b = std::get_if<TreeBinaryOp>(n)) {
            return binary(n, *b, axis);
        } else if (auto c = std::get_if<TreeCall>(n)) {
            return call(*c, axis);
        } else if (auto o = std::get_if<TreeNonaryOp>(n)) {
            switch (o->op) {
                case Opcode::VAR_X: return axis == 0 ? Term(Tree(1.0)) : Term();
                case Opcode::VAR_Y: return axis == 1 ? Term(Tree(1.0)) : Term();
                case Opcode::VAR_Z: return axis == 2 ? Term(Tree(1.0)) : Term();
                default:            return Term();      // (a free variable: constant in the coordinates)
            }
        } else if (std::get_if<TreeConstant>(n)) {
            return Term();
        } else if (std::get_if<TreeOracle>(n)) {
            return oracle(n, axis);
        }
        // (a remap, an apply or an invalid tree: the tree was not flat)
        failed = true;
        return Term();
    }

    Term unary(const TreeData* n, const TreeUnaryOp& u, int axis) {
        const Term da = child(u.lhs, axis);
        if (!da) {
            return Term();
        }
        const Tree& a = u.lhs;
        const Tree t(n);
        switch (u.op) {
            case Opcode::OP_SQUARE: return Term((Tree(2.0) * a) * *da);
            case Opcode::OP_SQRT:   return Term(finite(*da / (Tree(2.0) * t)));
            case Opcode::OP_NEG:    return Term(-*da);
            case Opcode::OP_SIN:    return Term(cos(a) * *da);
            case Opcode::OP_COS:    return Term(-(sin(a) * *da));
            case Opcode::OP_TAN:    return Term((Tree(1.0) + square(t)) * *da);
            case Opcode::OP_ASIN:   return Term(finite(*da / sqrt(Tree(1.0) - square(a))));
            case Opcode::OP_ACOS:   return Term(finite(-(*da / sqrt(Tree(1.0) - square(a)))));
            case Opcode::OP_ATAN:   return Term(*da / (Tree(1.0) + square(a)));
            case Opcode::OP_EXP:    return Term(t * *da);
            case Opcode::OP_LOG:    return Term(finite(*da / a));
            case Opcode::OP_RECIP:  return Term(finite(-(square(t) * *da)));
            case Opcode::OP_ABS:    return Term(compare(a, Tree(0.0)) * *da);
            case Opcode::CONST_VAR: return Term(da->with_const_vars());
            default:
                failed = true;
                return Term();
        }
    }

    Term binary(const TreeData* n, const TreeBinaryOp& b, int axis) {
        const Term da = child(b.lhs, axis);
        const Term db = child(b.rhs, axis);
        if (!da && !db) {
            return Term();
        }
        const Tree& a = b.lhs;
        const Tree& c = b.rhs;
        const Tree t(n);
        switch (b.op) {
            case Opcode::OP_ADD:    return plus(da, db);
            case Opcode::OP_SUB:    return minus(da, db);
            case Opcode::OP_MUL:    return plus(times(da, c), times(db, a));
            case Opcode::OP_DIV: {
                // (da - t db) / c, with t = a / c
                const Term num = minus(da, times(db, t));
                return Term(finite(*num / c));
            }
            case Opcode::OP_MIN:
            case Opcode::OP_MAX: {
                if (da && db && da->id() == db->id()) {
                    return da;
                }
                auto w = weights.find(n);
                if (w == weights.end()) {
                    const Tree cmp = compare(a, c);     // -1 where a < c, 1 where a > c, 0 where they are the same
                    w = weights.emplace(n, std::make_pair((Tree(1.0) - cmp) * Tree(0.5),
                                                          (Tree(1.0) + cmp) * Tree(0.5))).first;
                }
                const bool smaller = (b.op == Opcode::OP_MIN);
                const Tree& wa = smaller ? w->second.first : w->second.second;
                const Tree& wb = smaller ? w->second.second : w->second.first;
                return plus(times(da, wa), times(db, wb));
            }
            case Opcode::OP_ATAN2: {
                const Term num = minus(times(da, c), times(db, a));
                return Term(finite(*num / (square(a) + square(c))));
            }
            case Opcode::OP_POW: {
                const auto* k = std::get_if<TreeConstant>(b.rhs.get());
                if (!k) {
                    failed = true;
                    return Term();
                }
                if (!da || k->value == 0.0f) {
                    return Term();
                }
                if (k->value == 1.0f) {
                    return da;
                }
                if (k->value == 2.0f) {
                    return Term((Tree(2.0) * a) * *da);
                }
                return Term((Tree(double(k->value)) * pow(a, Tree(double(k->value) - 1.0))) * *da);
            }
            case Opcode::OP_NTH_ROOT: {
                const auto* k = std::get_if<TreeConstant>(b.rhs.get());
                if (!k) {
                    failed = true;
                    return Term();
                }
                if (!da) {
                    return Term();
                }
                // a^(1/k): (1/k) a^(1/k - 1) da = t / (k a) da
                return Term(finite(t / (Tree(double(k->value)) * a)) * *da);
            }
            case Opcode::OP_MOD: {
                if (!db) {
                    return da;
                }
                // a - c floor(a / c), where floor(a / c) = (a - t) / c
                return minus(da, times(db, (a - t) / c));
            }
            case Opcode::OP_NANFILL:
                return Term(nanfill(da ? *da : Tree(0.0), db ? *db : Tree(0.0)));
            case Opcode::OP_COMPARE:
                return Term();
            default:
                failed = true;
                return Term();
        }
    }

    // f(x, y, z) as a function of the point p: d/dp = f_u(x, y, z) x' + f_v(x, y, z) y' + f_w(x, y, z) z', the partials of the called field being those
    // in its own coordinates, asked at the same arguments
    Term call(const TreeCall& c, int axis) {
        const Tree* args[3] = {&c.x, &c.y, &c.z};
        Term sum;
        for (int i = 0; i < 3; ++i) {
            const Term dp = child(*args[i], axis);
            if (!dp) {
                continue;
            }
            run(c.f, i);
            const Term fi = memo.at(Key{c.f.get(), i});
            if (!fi) {
                continue;
            }
            sum = plus(sum, Term(fi->call(c.x, c.y, c.z) * *dp));
        }
        return sum;
    }

    // A native field: no formula, so the central difference over the step, a call of the oracle at the two shifted points
    Term oracle(const TreeData* n, int axis) {
        if (!(oracle_step > 0.0f)) {
            failed = true;
            return Term();
        }
        const Tree o(n);
        const double h = oracle_step;
        Tree hi[3] = {Tree::X(), Tree::Y(), Tree::Z()};
        Tree lo[3] = {Tree::X(), Tree::Y(), Tree::Z()};
        hi[axis] = hi[axis] + Tree(h);
        lo[axis] = lo[axis] - Tree(h);
        return Term((o.call(hi[0], hi[1], hi[2]) - o.call(lo[0], lo[1], lo[2])) * Tree(0.5 / h));
    }
};

}   // namespace

Tree partial_derivative(const Tree& t, int axis, float oracle_step, int max_call_depth) {
    if (axis < 0 || axis > 2 || !t.is_valid()) {
        return Tree::invalid();
    }
    const Tree flat = t.flatten(true, Tree::Calls::Keep);
    Deriver d(oracle_step);
    if (max_call_depth >= 0 && d.depth(flat) > max_call_depth) {
        return Tree::invalid();
    }
    const Term out = d.of(flat, axis);
    if (d.failed) {
        return Tree::invalid();
    }
    return out ? *out : Tree(0.0);
}

}   // namespace libfive
