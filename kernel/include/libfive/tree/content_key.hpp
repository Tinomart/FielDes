/*
libfive: a CAD kernel for modeling with implicit functions

An exact key of what a tree is; see content_key.cpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "libfive/tree/tree.hpp"

namespace libfive {

/*
 *  A key of the tree's structure: two trees built separately -- a script run
 *  again -- that are the same expression have the same key, and two that are
 *  not the same expression do not (up to a 128-bit hash).  Every node counts:
 *  its operation, a constant by all the bits of its value, the way the nodes
 *  are joined, an oracle by its name and content key and the trees it depends
 *  on.  A free variable and an oracle that has no content key are identified
 *  by the object itself, so a copy of them is a different tree (a key that
 *  never matches is the safe answer).
 *
 *  (Printing a tree is not a key: it writes a constant to six digits, an
 *  oracle by its name alone, and every free variable the same.)
 */
std::string treeContentKey(const Tree& t);

/*
 *  The same, for keeping across runs of the program (the render cache keeps
 *  meshes on disk by it): a free variable counts by its value in `vars` (its
 *  address is another one in the next run), an oracle by its persistentKey.
 *  A tree that holds a free variable with no value there, or an oracle that
 *  has no persistent key, has no such key: the text is empty.
 */
std::string treePersistentKey(const Tree& t, const std::map<Tree::Id, float>& vars);

/*
 *  A key of a triangle mesh that does not depend on the order of its vertices and triangles, nor on which corner
 *  of a triangle is written first (the mesher gives the same surface in another order from one run to the next):
 *  every triangle as its three corners, from the smallest, the triangles sorted.  The same surface is the same
 *  key in every run of the program.
 */
std::string meshContentKey(const std::vector<Eigen::Vector3d>& verts,
                           const std::vector<std::array<uint32_t, 3>>& tris);

/*
 *  A number that is never given out twice in this process: what a solved
 *  result is, for the keys of its fields (an address can be reused after the
 *  result is freed, and then two different results would share a key).
 */
uint64_t nextContentSerial();

}   // namespace libfive
