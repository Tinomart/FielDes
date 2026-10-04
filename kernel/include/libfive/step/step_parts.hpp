/*
libfive: a CAD kernel for modeling with implicit functions

The parts of an imported STEP file: one libfive Tree per placed solid (the
reconstruction, step_reconstruct.hpp), with its bounds and name.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <string>
#include <vector>

#include "libfive/step/step_model.hpp"
#include "libfive/tree/tree.hpp"

namespace libfive {
namespace step {

/*  What a renderer needs to choose a resolution for a part (in the part's
 *  own units; a placed copy scales them by its unit):
 *    detail      its smallest significant feature: the smaller of its thinnest
 *                side and the smallest radius (cylinder, cone, sphere, torus
 *                tube) among faces that are not a negligible share of its
 *                area -- a sheet's thickness, a pin's radius, a fillet's
 *    areaFlat    surface area of its planar faces
 *    areaCurved  surface area of all the others
 *  0 when unknown.  */
struct SolidMetrics
{
    double detail = 0, areaFlat = 0, areaCurved = 0;
};
SolidMetrics solidMetrics(const Solid& solid);
/*  The same from a tessellation of the solid that is there already (its triangles carry the face they
 *  belong to, see TessMesh)  */
struct TessMesh;
SolidMetrics solidMetricsFromMesh(const Solid& solid, const TessMesh& mesh);

struct StepPart
{
    Tree tree;
    Vec3 boundMin, boundMax;
    // Non-empty when this solid could not be reconstructed: `tree` is then
    // a meaningless placeholder (Tree(1e9)) that must not be used as real
    // geometry -- callers must check this field per part.
    std::string error;
    // Which occurrence of which solid this is (assembly path or solid name)
    std::string name;
    // Where the part's B-spline faces were approximated, and how well: the
    // fitted face's relative error (deviation / face size) on and near each
    // one, fading to 0 a little away from it; invalid if nothing was
    // approximated noticeably (placed like `tree`)
    Tree marker = Tree::invalid();
    // Which solid of the file (index into Model::solids) and which of its
    // instances this is -- what an exact region needs to mesh it again
    int solid = -1;
    int instance = -1;
    // Resolution hints (see SolidMetrics), in the delivered units
    double detail = 0, areaFlat = 0, areaCurved = 0;
};

/*  The placed copies of one solid's part (`local` in the solid's own
 *  coordinates): the first instance goes to `first`, further instances
 *  (repeated components of an assembly) to `extra`  */
void emitInstances(const Solid& solid, const Tree& local, const std::string& error,
                   StepPart& first, std::vector<StepPart>& extra,
                   const Tree& marker = Tree::invalid(), int solidIndex = -1,
                   const SolidMetrics& metrics = SolidMetrics());

}  // namespace step
}  // namespace libfive
