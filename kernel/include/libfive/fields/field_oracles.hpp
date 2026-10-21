/*
libfive: a CAD kernel for modeling with implicit functions

Analysis fields derived from another shape's field: its gradient (surface
normals, overhang angles), the local wall thickness and the mean curvature.
Each is a new field (a Tree) evaluated through an oracle that runs the
original shape's evaluator.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <map>
#include <vector>
#include <Eigen/Eigen>

#include "libfive/tree/tree.hpp"

namespace libfive {
namespace fields {

/*  mode 0..2: x / y / z of the unit normal (the normalized gradient);
 *  3: the gradient's length; 4..6: x / y / z of the raw gradient  */
Tree gradientField(const Tree& t, int mode);

/*  Local wall thickness (mm): the length of the chord through the material
 *  along the local normal, through the point (or, outside, the nearest wall
 *  it points at).  Capped at maxThickness.  */
Tree thicknessField(const Tree& t, double maxThickness);

/*  Mean curvature (1/mm, positive where convex), from the divergence of the
 *  unit normal with central differences of step h  */
Tree curvatureField(const Tree& t, double h);

/*  A field through scattered samples: inverse-distance weighting (with the
 *  given power) of the k nearest samples  */
Tree pointCloudField(const std::vector<Eigen::Vector3d>& points,
                     const std::vector<double>& values, int k, double power);

/*  Seeded Perlin noise (fractal sum of octaves), about -1 .. 1; `scale` is
 *  the size of the largest features  */
Tree noiseField(double scale, int octaves, unsigned seed, double gain, double lacunarity);
/*  Cellular (Worley) noise at one scale: mode 0 the distance to the nearest point of a jittered grid, 1 the distance from a wall between cells (F2 - F1), 2 the distance to
 *  the second nearest; about -1 .. 1  */
Tree cellularField(double scale, unsigned seed, int mode, double jitter);

/*  The gusset under the overhangs of a part, as a field of its own (lattice_field.cpp): negative where material is added, `cap` far from any.  A lattice is laid over the part in
 *  the build direction: the frame (`origin`, `axes`: three unit vectors, the last the direction material is built up in), `n` nodes along each axis a `pitch` apart.  The material
 *  under a point is added when a disc, as wide as `tanSlope` times its height above the point, fits inside the part at that height (the first height at which the part is above
 *  the point, and up to `reach` higher), and the point is at most `reach` under the part and at least `minZ` up the frame's third axis.  `negate` does the same to the air (the part's
 *  field read the other way round), and the frame's heights between `forceLo` and `forceHi` are held as "not part".  `stats` (8 numbers, if not null): 0 the status (0 = made),
 *  1 the volume of the gusset (mm^3, counted on the nodes), 2 the nodes, 3 the nodes the part was evaluated at, 4 and 5 the seconds of the sampling and of the whole, 6 the nodes of the
 *  part, 7 the memory (MB).  An invalid tree when it cannot be made (stats[0] says why).  */
struct OverhangLatticeParams
{
    double origin[3];
    double axes[9];
    int n[3];
    double pitch;
    double tanSlope;
    double reach;
    double minZ;
    bool negate;
    double forceLo, forceHi;
    int mode = 0;         // (0: the gusset / undercut fill / closing by the sign of `tanSlope`; 2: the DRAFT -- `tanSlope` the tangent of the angle, `reach` the width of the band round the part that is looked at, `minZ` the top of the part up the third axis;
                          // 3: the WALLS; 5: the CORNERS by the rolling ball, `minZ` its radius; 6 / 7: the ACCESS FILL -- the air that a ball-end tool of radius `minZ` cannot touch coming along the third axis is filled, from its top only (6) or from
                          // either end (7); the shaft widens by `tanSlope` (the tangent of a draft; a tiny number for a straight shaft) for every unit it goes out; `reach` is the band of the part that is looked at and where the field stops)
};
Tree overhangLattice(const Tree& part, const std::map<Tree::Id, float>& vars,
                     const OverhangLatticeParams& params, double* stats, std::vector<uint8_t>* checkOut = nullptr);

/*  The CORRECTION lattice (lattice_field.cpp): what `field` differs from `part` by, sampled once at the nodes of a lattice -- `origin`, `axes`, `n`, `pitch` as above -- at the nodes where
 *  `field` is within `reach` of its surface (with the pruning of the mesher), 0 everywhere else.  The part plus it is `field` there (trilinear between the nodes) and the part's own
 *  field elsewhere.  `stats` as above.  An invalid tree when it cannot be made.  */
Tree bakeLattice(const Tree& field, const Tree& part, const std::map<Tree::Id, float>& vars,
                 const OverhangLatticeParams& params, double* stats, Tree* zone = nullptr);

}   // namespace fields
}   // namespace libfive
