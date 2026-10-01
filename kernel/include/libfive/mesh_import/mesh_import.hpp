/*
libfive: a CAD kernel for modeling with implicit functions

Triangle-mesh import (STL, binary or ASCII, and Wavefront OBJ).

The mesh is turned into an exact signed distance field and wrapped in an
Oracle, so it behaves like any other libfive shape: it can be unioned,
cut, offset, shelled or blended with the rest of a script.

    distance  closest point on the triangles, found with a bounding volume
              hierarchy (exact, not a voxel approximation)
    sign      the angle-weighted pseudo-normal of the closest feature
              (Baerentzen & Aanaes), which is exact for closed shells; where
              it can't be trusted -- the closest feature lies on a hole or
              crack of the mesh, or the point may be inside a different,
              overlapping shell -- the generalised winding number (Jacobson
              et al. 2013, with the hierarchical dipole approximation of
              Barill et al. 2018), which degrades gracefully for holes

Before that the triangles are cleaned: duplicate vertices are welded,
degenerate and duplicate triangles are dropped, each connected shell is
given a consistent winding and shells that are inside out are flipped.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <cstdint>
#include <string>

#include "libfive/tree/tree.hpp"

namespace libfive {
namespace mesh {

struct MeshImportInfo
{
    size_t triangles = 0;          // after cleaning
    size_t vertices = 0;           // after welding
    size_t components = 0;         // connected shells
    size_t boundaryEdges = 0;      // edges used by a single triangle
    size_t nonManifoldEdges = 0;   // edges used by more than two triangles
    size_t reoriented = 0;         // triangles whose winding was flipped
    size_t dropped = 0;            // degenerate or duplicate triangles removed
    bool watertight = false;       // no boundary and no non-manifold edges
    bool windingSign = false;      // winding number used where needed (see above)
    double lower[3] = {0, 0, 0};   // bounds, after scaling
    double upper[3] = {0, 0, 0};
};

/*
 *  Loads an .stl or .obj file, multiplying every coordinate by `scale`.
 *  On failure `ok` is false, `error` says why and the returned Tree is
 *  invalid.
 */
Tree importMeshTree(const std::string& path, double scale,
                    MeshImportInfo& info, bool& ok, std::string& error);

/*
 *  The same from triangles already in memory (for formats read elsewhere,
 *  e.g. PLY or 3MF in the Python bindings): `xyz` holds 3 * vertex_count
 *  coordinates, `tri` 3 * tri_count vertex indices.
 */
Tree meshTreeFromArrays(const float* xyz, size_t vertex_count,
                        const uint32_t* tri, size_t tri_count, double scale,
                        MeshImportInfo& info, bool& ok, std::string& error);

}   // namespace mesh
}   // namespace libfive
