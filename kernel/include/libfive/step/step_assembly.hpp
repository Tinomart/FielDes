/*
libfive: a CAD kernel for modeling with implicit functions

Where each solid of a STEP file sits: its length unit and its placements
in the file's assembly structure.

A STEP solid's coordinates are in the units of the representation context
it is defined in -- and one file can mix them (an assembly in millimetres
whose purchased gear was modelled in metres).  Assemblies place each
component's representation inside its parent's with an
ITEM_DEFINED_TRANSFORMATION (via REPRESENTATION_RELATIONSHIP_WITH_
TRANSFORMATION + NEXT_ASSEMBLY_USAGE_OCCURRENCE), possibly many times
(four identical screws) and nested (a motor sub-assembly inside the
machine).  This resolves all of it into, per solid, a list of instances
mapping the solid's own coordinates to millimetres in the top-level frame.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <map>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/step/step_parser.hpp"
#include "libfive/tree/tree.hpp"

namespace libfive {
namespace step {

/*  One placed occurrence of a solid: p = linear * q + offset, q in the
 *  solid's own (file) coordinates, p in millimetres  */
struct SolidInstance
{
    Eigen::Matrix3d linear = Eigen::Matrix3d::Identity();
    Eigen::Vector3d offset = Eigen::Vector3d::Zero();
    std::string name;       // e.g. "Drive:1/Stepper Motor:1/M3x10-Screw:2"
};

/*  Instances of every solid entity (MANIFOLD_SOLID_BREP, ...) by id.  A
 *  solid outside any assembly gets one instance (just its unit scale).  */
std::map<int, std::vector<SolidInstance>> solidInstances(const Document& doc,
                                                         const std::vector<int>& solidIds);

/*  Millimetres per length unit of the file's first declared length unit
 *  (1 if none), i.e. what "the file's own numbers" means  */
double fileLengthUnitMM(const Document& doc);

/*  A shape given in a solid's own coordinates, placed as `inst`:
 *  f'(p) = s f(q) with q = inst^-1(p) and s the instance's scale, so the
 *  result is still a distance in the new units -- or, with distance =
 *  false (a value that isn't a length, e.g. a relative error), f(q)  */
Tree placeTree(const Tree& t, const SolidInstance& inst, bool distance = true);

/*  Grows [lo, hi] to hold the box [a, b] placed as `inst`  */
void placeBox(const Eigen::Vector3d& a, const Eigen::Vector3d& b,
              const SolidInstance& inst, Eigen::Vector3d& lo, Eigen::Vector3d& hi);

}   // namespace step
}   // namespace libfive
