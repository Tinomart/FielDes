/*
libfive: a CAD kernel for modeling with implicit functions

Copyright (C) 2020  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once
#include <atomic>

#include "libfive/tree/tree.hpp"
#include "libfive/tree/key.hpp"

namespace libfive {

struct TreeNonaryOp {
    Opcode::Opcode op;
};
struct TreeUnaryOp {
    Opcode::Opcode op;
    // This is mutable so that the destructor can swap the tree's lhs
    // out from a variant pointed to by a "const Data*" pointer.
    Tree lhs;
};
struct TreeBinaryOp {
    Opcode::Opcode op;
    Tree lhs;
    Tree rhs;
};
struct TreeConstant {
    float value;
};
struct TreeOracle {
    std::unique_ptr<const OracleClause> oracle;
};
struct TreeRemap {
    Tree x;
    Tree y;
    Tree z;
    Tree t;
};
struct TreeApply {
    Tree target;
    Tree value;

    Tree t;
};
/*  The field f evaluated at the point (x, y, z): a function application.
 *
 *  f is a tree of its own whose X, Y and Z are its arguments (they are not the X, Y and Z of the tree this node is in), so the point
 *  can be anywhere and f is not copied for it -- a Deck (eval/deck.hpp) compiles f once and every call to it runs that one tape.  A
 *  TreeRemap with the same arguments is the same field, written as a substitution: it is expanded into a copy of f for each use (and
 *  that is what is wanted when f is small or used once, since an expanded copy is shortened by the renderer's pruning along with
 *  the rest of the tape). */
struct TreeCall {
    Tree x;
    Tree y;
    Tree z;
    Tree f;
};
struct TreeInvalid {
    // No members
};
using TreeDataVariant = std::variant<
        TreeNonaryOp,
        TreeUnaryOp,
        TreeBinaryOp,
        TreeConstant,
        TreeOracle,
        TreeRemap,
        TreeApply,
        TreeCall,
        TreeInvalid>;

/*  TreeData is a wrapper struct around the TreeDataVariant.
 *
 *  It's a wrapper struct to make recursive variants work, since TreeData
 *  had to be forward-declared to be used in Tree.
 *
 *  The struct includes a reference count, to track ownership and destruciton.
 *  This is the same idea as boost::intrusive_ptr, but that doesn't quite
 *  work for our use cases because the top-level Tree destructor wants to use
 *  the heap rather than the stack; otherwise, destroying deep trees can cause
 *  a stack overflow.
 *
 *  For more information on the homebrew shared pointer, see tree.hpp. */
struct TreeData : public TreeDataVariant
{
    TreeData(TreeDataVariant&& v)
        : TreeDataVariant(std::move(v))
        , flags(compute_flags())
        , serial(next_serial())
    { /* Nothing to do here */ }

    /*  Returns the opcode of this clause */
    Opcode::Opcode op() const;

    /*  Returns the floating-point value if this is a constant,
     *  throwing an exception otherwise. */
    float value() const;
    struct ValueException : public std::exception {
        const char* what() const throw () override {
            return "Accessed value of non-constant Tree";
        }
    };

    struct PowValueException : public std::exception {
        const char* what() const throw () override {
            return "Power and nth-root argument must be a constant integer";
        }
    };

    /*  Returns left and right-hand Tree references.
     *  Throws a ChildException if the requested branch is missing. */
    const Tree& lhs() const;
    const Tree& rhs() const;
    struct ChildException : public std::exception {
        const char* what() const throw () override {
            return "Accessed missing child";
        }
    };

    /*  Returns the underlying OracleClause.  If this isn't a TreeOracle,
     *  throws an OracleException. */
    const OracleClause& oracle_clause() const;

    /*  Returns a freshly-baked Oracle from the given clause.
     *  If this isn't a TreeOracle, throws an exception. */
    std::unique_ptr<Oracle> build_oracle() const;

    struct OracleException : public std::exception {
        const char* what() const throw () {
            return "Accessed oracle of non-oracle Tree";
        }
    };

    struct RemapException : public std::exception {
        const char* what() const throw () override {
            return "Tried to do an operation which requires flattened tree";
        }
    };

    struct ApplyException : public std::exception {
        const char* what() const throw () override {
            return "Can only apply with a variable as first argument";
        }
    };

    /*  The call, if this is a TreeCall.  Throws a CallException otherwise. */
    const TreeCall& call() const;
    struct CallException : public std::exception {
        const char* what() const throw () override {
            return "Accessed the call of a Tree that is not a call";
        }
    };

    struct InvalidException : public std::exception {
        const char* what() const throw () override {
            return "Invalid tree";
        }
    };

    using Key = TreeDataKey;
    Key key() const;

    mutable std::atomic_uint32_t refcount = 0;

    // Flags are accumulated up through the tree, and used to do things
    // like lazily flattening remap operations when needed.
    //
    // These flags are a pure function of the tree data, serving as
    // a cache; they could be computed by walking the tree, but it's
    // nice to have this information available with O(1).
    //
    // HAS_REMAP means "is not a plain tree of operations yet": it has a remap, an apply or a call in it, and flatten() makes it
    // plain.  HAS_SUBST is the part of that which is substitution (a remap or an apply), HAS_CALL the part that is a call: the
    // Deck keeps calls, so what it is made from has HAS_CALL but not HAS_SUBST.
    //
    // HAS_XYZ and HAS_ORACLE say that the value depends on the coordinates of the tree it is in.  A call depends on them through
    // its arguments only: the field it calls has coordinates of its own.
    enum {
        TREE_FLAG_HAS_REMAP=(1<<0),
        TREE_FLAG_HAS_ORACLE=(1<<1),
        TREE_FLAG_HAS_XYZ=(1<<2),
        TREE_FLAG_HAS_CALL=(1<<3),
        TREE_FLAG_HAS_SUBST=(1<<4),
    };
    const uint32_t flags;

    // The order in which the nodes were made (a number that grows with every one).  Where the optimizer has to put terms in SOME order (a sum is
    // added up term by term, a product multiplied factor by factor, and float arithmetic is not associative) it uses this and not the node's
    // ADDRESS: addresses change from one run to the next (the heap layout does), and with them the rounding of the field's values in the last
    // bits -- the same field gave slightly different numbers in different processes.  The order the nodes are made in is the same on every run of
    // the same script.
    const uint64_t serial;

    static uint64_t next_serial()
    {
        static std::atomic<uint64_t> counter{0};
        return ++counter;
    }

protected:
    /*  Used to calculate flags during the constructor, after the variant
     *  is assigned but before the constructor completes. */
    uint32_t compute_flags() const;
};

}   // namespace libfive
