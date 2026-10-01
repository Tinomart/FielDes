/*
libfive: a CAD kernel for modeling with implicit functions

This is new code, added to give libfive a small, dependency-free STEP
(ISO 10303-21) reader.  It does not attempt to be a general EXPRESS
interpreter -- it just tokenizes the "DATA;" section of a STEP Part 21
file into a table of generic entities (#id = NAME(args);), which the
rest of libfive/src/step/ interprets for the specific entity types
needed to build B-rep solids (points, curves, surfaces, and topology).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace libfive {
namespace step {

// A single value within an entity's argument list.  STEP argument lists
// are recursive (an argument can be a list of arguments), so this is a
// recursive variant.
struct Value;
using ValueList = std::vector<Value>;

// Reference to another entity, e.g. "#123"
struct Ref { int id; };

// Unset ("$") or derived ("*") value
enum class Omitted { UNSET, DERIVED };

struct Value
{
    std::variant<double, std::string, Ref, Omitted, ValueList> v;

    bool isNumber() const;
    bool isString() const;
    bool isRef() const;
    bool isList() const;

    double asNumber(double fallback = 0.0) const;
    std::string asString() const;
    int asRef(int fallback = -1) const;
    const ValueList& asList() const;
};

// One entity instance, e.g. "#12=CARTESIAN_POINT('',(1.,2.,3.));"
//
// STEP also allows "complex entity" instances that stack several typed
// records into one instance, e.g.
//   #50=(BOUNDED_SURFACE()B_SPLINE_SURFACE(3,3,...)
//        B_SPLINE_SURFACE_WITH_KNOTS(...)RATIONAL_B_SPLINE_SURFACE((...))
//        SURFACE());
// Each keyword's argument list is stored under its own name in `aspects`,
// so callers can look up whichever aspect (e.g. RATIONAL_B_SPLINE_SURFACE)
// they care about regardless of whether the instance was simple or complex.
struct Entity
{
    int id = -1;
    // Primary type name (the first/only keyword for a simple instance)
    std::string type;
    // All (keyword -> args) pairs; a simple instance has exactly one
    std::map<std::string, ValueList> aspects;

    const ValueList* find(const std::string& keyword) const;
};

// The full set of entities parsed from a STEP file's DATA section,
// keyed by their '#' id.
class Document
{
public:
    // Parses `path`.  Returns false (with `error` set) on failure to
    // open or tokenize the file; a file with zero entities is not an
    // error at this layer (that's reported by the higher-level solid
    // builder, which knows what "empty" means for a B-rep).
    bool load(const std::string& path, std::string& error);

    const Entity* get(int id) const;

    // All entities of a given primary type, e.g. "MANIFOLD_SOLID_BREP"
    std::vector<const Entity*> ofType(const std::string& type) const;

    // Every entity, by id
    const std::map<int, Entity>& all() const { return entities; }

    // Radians per unit of the file's plane-angle unit (1 for radians,
    // pi/180 for a file declaring degrees). Angle-valued fields -- a
    // CONICAL_SURFACE's semi-angle, a TRIMMED_CURVE's parameter on a circle
    // -- are written in this unit and must be scaled before use.
    double planeAngleFactor() const;

private:
    std::map<int, Entity> entities;
    mutable bool angleFactorCached = false;
    mutable double angleFactor = 1.0;
};

}  // namespace step
}  // namespace libfive
