/*
libfive: a CAD kernel for modeling with implicit functions

Units and assembly placements of a STEP file's solids (see
step_assembly.hpp).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <cmath>
#include <functional>
#include <set>

#include "libfive/step/step_assembly.hpp"
#include "libfive/step/step_geometry.hpp"

namespace libfive {
namespace step {

namespace {

bool endsWith(const std::string& s, const std::string& tail)
{
    return s.size() >= tail.size() &&
           s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

/*  Millimetres per unit of a LENGTH_UNIT entity, 0 if it isn't one  */
double lengthUnitMM(const Document& doc, int id, int depth=0)
{
    const Entity* e = doc.get(id);
    if (!e || depth > 8) return 0;
    if (auto* si = e->find("SI_UNIT"))
    {
        if (si->size() < 2 || (*si)[1].asString() != "METRE") return 0;
        static const std::map<std::string, double> prefix = {
            {"EXA", 1e21}, {"PETA", 1e18}, {"TERA", 1e15}, {"GIGA", 1e12},
            {"MEGA", 1e9}, {"KILO", 1e6}, {"HECTO", 1e5}, {"DECA", 1e4},
            {"DECI", 100.0}, {"CENTI", 10.0}, {"MILLI", 1.0}, {"MICRO", 1e-3},
            {"NANO", 1e-6}, {"PICO", 1e-9}, {"FEMTO", 1e-12}, {"ATTO", 1e-15}};
        if (!(*si)[0].isString()) return 1000.0;       // $: plain metres
        auto itr = prefix.find((*si)[0].asString());
        return itr == prefix.end() ? 1000.0 : itr->second;
    }
    if (auto* cb = e->find("CONVERSION_BASED_UNIT"))
    {
        if (!e->find("LENGTH_UNIT") || cb->size() < 2) return 0;
        if (const Entity* m = doc.get((*cb)[1].asRef()))
        {
            if (auto* ma = m->find("LENGTH_MEASURE_WITH_UNIT"))
            {
                if (ma->size() >= 2 && (*ma)[0].isNumber())
                {
                    const double inner = lengthUnitMM(doc, (*ma)[1].asRef(), depth + 1);
                    if (inner > 0 && (*ma)[0].asNumber() > 0)
                    {
                        return (*ma)[0].asNumber() * inner;
                    }
                }
            }
        }
        static const std::map<std::string, double> named = {
            {"INCH", 25.4}, {"FOOT", 304.8}, {"YARD", 914.4}, {"MILE", 1609344.0},
            {"MILLIMETRE", 1.0}, {"MILLIMETER", 1.0}, {"CENTIMETRE", 10.0},
            {"CENTIMETER", 10.0}, {"METRE", 1000.0}, {"METER", 1000.0}};
        std::string n = (*cb)[0].asString();
        for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
        auto itr = named.find(n);
        return itr == named.end() ? 0 : itr->second;
    }
    return 0;
}

/*  Millimetres per length unit of a representation context, 0 if it
 *  declares none  */
double contextUnitMM(const Document& doc, int contextId)
{
    const Entity* ctx = doc.get(contextId);
    if (!ctx) return 0;
    auto* units = ctx->find("GLOBAL_UNIT_ASSIGNED_CONTEXT");
    if (!units || units->empty() || !(*units)[0].isList()) return 0;
    for (const Value& u : (*units)[0].asList())
    {
        const double mm = lengthUnitMM(doc, u.asRef());
        if (mm > 0) return mm;
    }
    return 0;
}

/*  A rigid map in millimetres: p -> R p + t  */
struct Rigid
{
    Eigen::Matrix3d R = Eigen::Matrix3d::Identity();
    Eigen::Vector3d t = Eigen::Vector3d::Zero();
    Rigid then(const Rigid& outer) const
    {
        return Rigid{outer.R * R, outer.R * t + outer.t};
    }
};

/*  The frame of an AXIS2_PLACEMENT_3D, its origin scaled to millimetres  */
bool frameOf(const Document& doc, int id, double unitMM, Rigid& out)
{
    bool ok = false;
    const Placement pl = resolvePlacement(doc, id, &ok);
    if (!ok) return false;
    out.R.col(0) = pl.xAxis;
    out.R.col(1) = pl.yAxis;
    out.R.col(2) = pl.zAxis;
    out.t = pl.origin * unitMM;
    return true;
}

}   // anonymous namespace

Tree placeTree(const Tree& t, const SolidInstance& inst, bool distance)
{
    const Eigen::Matrix3d& L = inst.linear;
    if (L.isIdentity(1e-12) && inst.offset.isZero(1e-12))
    {
        return t;
    }
    const Eigen::Matrix3d Li = L.inverse();
    const Eigen::Vector3d c = -Li * inst.offset;
    const Tree xyz[3] = {Tree::X(), Tree::Y(), Tree::Z()};
    Tree q[3] = {Tree(0.0f), Tree(0.0f), Tree(0.0f)};
    const double big = Li.cwiseAbs().maxCoeff();
    for (int r = 0; r < 3; ++r)
    {
        bool first = true;
        for (int k = 0; k < 3; ++k)
        {
            const double a = Li(r, k);
            if (std::abs(a) <= 1e-12 * big) continue;
            const Tree term = (a == 1.0) ? xyz[k] : xyz[k] * float(a);
            q[r] = first ? term : q[r] + term;
            first = false;
        }
        if (c[r] != 0) q[r] = first ? Tree(float(c[r])) : q[r] + float(c[r]);
    }
    const double s = std::cbrt(std::abs(L.determinant()));
    // remap() is lazy; flatten so the result prints as a plain tree (the
    // import cache stores it that way) and B-spline leaves get moved
    Tree out = t.remap(q[0], q[1], q[2]).flatten();
    return (s == 1.0 || !distance) ? out : out * float(s);
}

void placeBox(const Eigen::Vector3d& a, const Eigen::Vector3d& b,
              const SolidInstance& inst, Eigen::Vector3d& lo, Eigen::Vector3d& hi)
{
    for (int k = 0; k < 8; ++k)
    {
        const Eigen::Vector3d corner((k & 1) ? b.x() : a.x(),
                                     (k & 2) ? b.y() : a.y(),
                                     (k & 4) ? b.z() : a.z());
        const Eigen::Vector3d p = inst.linear * corner + inst.offset;
        lo = lo.cwiseMin(p);
        hi = hi.cwiseMax(p);
    }
}

double fileLengthUnitMM(const Document& doc)
{
    for (const Entity* ctx : doc.ofType("GLOBAL_UNIT_ASSIGNED_CONTEXT"))
    {
        const double mm = contextUnitMM(doc, ctx->id);
        if (mm > 0) return mm;
    }
    return 1.0;
}

std::map<int, std::vector<SolidInstance>> solidInstances(const Document& doc,
                                                         const std::vector<int>& solidIds)
{
    const double fileUnit = fileLengthUnitMM(doc);

    // Representations: (name, items, context), flat or as a complex
    // entity's REPRESENTATION aspect
    std::map<int, double> repUnit;                  // rep -> mm per unit
    std::map<int, std::vector<int>> repsOfItem;     // item -> reps listing it
    for (const auto& [id, e] : doc.all())
    {
        for (const auto& [key, args] : e.aspects)
        {
            if (!endsWith(key, "REPRESENTATION") ||
                key == "SHAPE_DEFINITION_REPRESENTATION" ||
                key == "CONTEXT_DEPENDENT_SHAPE_REPRESENTATION" ||
                key == "PROPERTY_DEFINITION_REPRESENTATION" ||
                args.size() < 3 || !args[1].isList() || !args[2].isRef())
            {
                continue;
            }
            const double u = contextUnitMM(doc, args[2].asRef());
            repUnit[id] = u > 0 ? u : fileUnit;
            for (const Value& item : args[1].asList())
            {
                if (item.isRef()) repsOfItem[item.asRef()].push_back(id);
            }
            break;
        }
    }

    // The representations that hold a solid directly: the bottom of every
    // chain of relationships
    std::set<int> solidReps;
    for (int sid : solidIds)
    {
        auto itr = repsOfItem.find(sid);
        if (itr != repsOfItem.end()) solidReps.insert(itr->second.begin(), itr->second.end());
    }

    // Product structure, to tell a relationship's child from its parent
    // and to name occurrences: PRODUCT_DEFINITION_SHAPE -> its definition,
    // product definition -> its representation, relationship -> NAUO
    std::map<int, int> pdsDefinition;
    for (const Entity* e : doc.ofType("PRODUCT_DEFINITION_SHAPE"))
    {
        auto* a = e->find("PRODUCT_DEFINITION_SHAPE");
        if (a && a->size() >= 3) pdsDefinition[e->id] = (*a)[2].asRef();
    }
    std::map<int, std::set<int>> repsOfPD;
    for (const Entity* e : doc.ofType("SHAPE_DEFINITION_REPRESENTATION"))
    {
        auto* a = e->find("SHAPE_DEFINITION_REPRESENTATION");
        if (!a || a->size() < 2) continue;
        auto itr = pdsDefinition.find((*a)[0].asRef());
        if (itr != pdsDefinition.end()) repsOfPD[itr->second].insert((*a)[1].asRef());
    }
    struct Occurrence { int childPD = -1; std::string name; };
    std::map<int, Occurrence> occurrenceOfRel;
    for (const Entity* e : doc.ofType("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION"))
    {
        auto* a = e->find("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION");
        if (!a || a->size() < 2) continue;
        auto itr = pdsDefinition.find((*a)[1].asRef());
        if (itr == pdsDefinition.end()) continue;
        const Entity* nauo = doc.get(itr->second);
        const ValueList* na = nauo ? nauo->find("NEXT_ASSEMBLY_USAGE_OCCURRENCE") : nullptr;
        if (!na) na = nauo ? nauo->find("PRODUCT_DEFINITION_USAGE") : nullptr;
        if (!na || na->size() < 5) continue;
        Occurrence occ;
        occ.childPD = (*na)[4].asRef();
        occ.name = (*na)[1].asString();
        if (occ.name.empty()) occ.name = (*na)[0].asString();
        occurrenceOfRel[(*a)[0].asRef()] = occ;
    }

    // Relationships, child -> parent, each with the child-to-parent map
    struct Up { int parent; Rigid map; std::string name; };
    std::map<int, std::vector<Up>> ups;
    for (const auto& [id, e] : doc.all())
    {
        const ValueList* rr = e.find("REPRESENTATION_RELATIONSHIP");
        if (!rr) rr = e.find("SHAPE_REPRESENTATION_RELATIONSHIP");
        if (!rr || rr->size() < 4) continue;
        int child = (*rr)[2].asRef(), parent = (*rr)[3].asRef();
        if (!repUnit.count(child) || !repUnit.count(parent)) continue;

        std::string name;
        bool swapped = false;
        auto occ = occurrenceOfRel.find(id);
        if (occ != occurrenceOfRel.end())
        {
            name = occ->second.name;
            auto reps = repsOfPD.find(occ->second.childPD);
            if (reps != repsOfPD.end() && !reps->second.count(child) &&
                reps->second.count(parent))
            {
                std::swap(child, parent);       // written parent-first
                swapped = true;
            }
        }
        else if (!e.find("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION") &&
                 solidReps.count(parent) && !solidReps.count(child))
        {
            // A plain relationship between two representations of one shape
            // -- a part's own shape representation and the advanced-brep
            // one that holds its solid -- written with the solid's second
            // (as some exporters do: "Part1", "ADVANCED_BREP_SHAPE_
            // REPRESENTATION"): the one holding the solid is the lower.
            // Read the other way round, the chain from the solid never
            // reached the assembly, and every part sat at the origin.
            std::swap(child, parent);
        }

        Rigid map;
        const ValueList* rt = e.find("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION");
        if (rt && !rt->empty())
        {
            const Entity* te = doc.get((*rt)[0].asRef());
            const ValueList* idt = te ? te->find("ITEM_DEFINED_TRANSFORMATION") : nullptr;
            if (!idt || idt->size() < 4) continue;
            // transform_item_1 lives in rep_1, transform_item_2 in rep_2:
            // the child's frame goes onto the parent's frame
            int childItem = (*idt)[2].asRef(), parentItem = (*idt)[3].asRef();
            if (swapped) std::swap(childItem, parentItem);
            Rigid fc, fp;
            if (!frameOf(doc, childItem, repUnit[child], fc) ||
                !frameOf(doc, parentItem, repUnit[parent], fp))
            {
                continue;
            }
            // p_parent = Fp * Fc^-1 * p_child
            map.R = fp.R * fc.R.transpose();
            map.t = fp.t - map.R * fc.t;
        }
        ups[child].push_back(Up{parent, map, name});
    }

    // Every path from a solid's representation up to a root
    std::map<int, std::vector<SolidInstance>> out;
    for (int sid : solidIds)
    {
        std::vector<SolidInstance> found;
        std::function<void(int, const Rigid&, const std::string&, int, std::set<int>&)> walk =
            [&](int rep, const Rigid& acc, const std::string& name, int depth, std::set<int>& onPath)
        {
            if (found.size() >= 4096) return;
            auto itr = ups.find(rep);
            if (itr == ups.end() || depth > 64)
            {
                SolidInstance inst;
                inst.linear = acc.R;
                inst.offset = acc.t;
                inst.name = name;
                found.push_back(inst);
                return;
            }
            bool any = false;
            for (const Up& up : itr->second)
            {
                if (onPath.count(up.parent)) continue;      // a cycle: ignore
                any = true;
                onPath.insert(up.parent);
                const std::string n = up.name.empty() ? name
                                    : name.empty() ? up.name : up.name + "/" + name;
                walk(up.parent, acc.then(up.map), n, depth + 1, onPath);
                onPath.erase(up.parent);
            }
            if (!any)
            {
                SolidInstance inst;
                inst.linear = acc.R;
                inst.offset = acc.t;
                inst.name = name;
                found.push_back(inst);
            }
        };

        auto reps = repsOfItem.find(sid);
        if (reps == repsOfItem.end())
        {
            SolidInstance inst;
            inst.linear *= fileUnit;
            out[sid].push_back(inst);
            continue;
        }
        for (int rep : reps->second)
        {
            const double u = repUnit[rep];
            std::set<int> onPath{rep};
            const size_t before = found.size();
            walk(rep, Rigid(), "", 0, onPath);
            for (size_t k = before; k < found.size(); ++k)
            {
                found[k].linear *= u;           // own units -> mm, then placed
            }
        }

        // The same placement reached along different relationship paths
        // (e.g. a solid listed in two representations of one product)
        std::vector<SolidInstance> unique;
        for (const auto& f : found)
        {
            bool dup = false;
            for (const auto& g : unique)
            {
                const double scale = std::max(1.0, g.offset.norm());
                if ((f.linear - g.linear).cwiseAbs().maxCoeff() < 1e-9 * g.linear.norm() &&
                    (f.offset - g.offset).norm() < 1e-9 * scale)
                {
                    dup = true;
                    break;
                }
            }
            if (!dup) unique.push_back(f);
        }
        out[sid] = unique;
    }
    return out;
}

}   // namespace step
}   // namespace libfive
