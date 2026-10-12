/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include "libfive/tree/tree.hpp"

#include "libfive/render/brep/brep.hpp"

namespace libfive {

// Forward declaration
class Evaluator;
struct BRepSettings;

template <unsigned N> class Region;

class Mesh : public BRep<3> {
public:
    /*
     *  Core render function
     *
     *  Returns nullptr if min_feature is invalid or cancel is set to true
     *  partway through the computation.
     */
    static std::unique_ptr<Mesh> render(
            const Tree& t, const Region<3>& r,
            const BRepSettings& settings);

    /*
     *  Render function that re-uses evaluators
     *  es must be a pointer to at least [settings.workers] Evaluators
     *
     *  Returns nullptr if min_feature is invalid or cancel is set to true
     *  partway through the computation.
     */
    static std::unique_ptr<Mesh> render(
            Evaluator* es, const Region<3>& r,
            const BRepSettings& settings);

    /*
     *  The face of a 2D shape: the part of a plane where the field is
     *  negative, as flat triangles -- a sheet with no thickness, not a body.
     *  The plane is z = 0 when `frame` is null, else the plane through the point
     *  (frame[0..2]) spanned by the two unit vectors u = frame[3..5] and
     *  v = frame[6..8] (the face is seen from the side u x v points to).  The field
     *  is read in that plane only (a 2D shape does not depend on the distance from
     *  its plane).  vars holds the values of the tree's variables.  The face stops
     *  a cell and a half short of the region's edge (so that its outline is closed).
     *
     *  Returns an empty mesh when the plane is outside the region, and nullptr
     *  if cancel is set partway through the computation.  The progress handler
     *  is used as for render().  When `outlines` is given it receives the outline
     *  of the face: closed loops of points in space (the first point is the last)
     */
    static std::unique_ptr<Mesh> renderFlat(
            const Tree& t, const std::map<Tree::Id, float>& vars,
            const Region<3>& r, const double* frame, BRepSettings& settings,
            std::vector<std::vector<Eigen::Vector3f>>* outlines = nullptr);

    /*
     *  Writes the mesh to a file
     */
    bool saveSTL(const std::string& filename) const;

    /*
     *  Merge multiple bodies and write them to a single file
     */
    static bool saveSTL(const std::string& filename,
                        const std::list<const Mesh*>& meshes);

protected:

    /*
     *  Inserts a line into the mesh as a zero-size triangle
     *  (used for debugging)
     */
    void line(const Eigen::Vector3f& a, const Eigen::Vector3f& b);

};

}   // namespace libfive
