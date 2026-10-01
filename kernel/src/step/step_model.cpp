/*
libfive: a CAD kernel for modeling with implicit functions
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/step/step_model.hpp"
#include "libfive/step/step_progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace libfive {
namespace step {

// Moller-Trumbore ray-triangle intersection. Returns true (with t, the
// ray parameter of the hit) only for a genuine forward (t > eps) hit
// strictly inside the triangle -- eps excludes both a degenerate
// parallel ray and a hit essentially at the ray's own origin (which
// would otherwise let a query point sitting near-exactly on the
// surface itself double-count or miss a crossing).
/*  The closed shells bounding a solid entity: a MANIFOLD_SOLID_BREP's one
 *  shell, or a BREP_WITH_VOIDS' outer shell and the shells of its cavities
 *  (each an ORIENTED_CLOSED_SHELL wrapping a CLOSED_SHELL)  */
static std::vector<const ValueList*> solidShells(const Document& doc, const Entity* e)
{
    std::vector<int> ids;
    auto* mb = e->find("MANIFOLD_SOLID_BREP");
    if (mb && mb->size() >= 2 && (*mb)[1].isRef()) ids.push_back((*mb)[1].asRef());
    if (auto* bv = e->find("BREP_WITH_VOIDS")) {
        if (ids.empty() && bv->size() >= 2 && (*bv)[1].isRef()) ids.push_back((*bv)[1].asRef());
        if (bv->size() >= 3 && (*bv)[2].isList()) {
            for (auto& v : (*bv)[2].asList()) {
                if (!v.isRef()) continue;
                int shell = v.asRef();
                if (const Entity* ve = doc.get(shell)) {
                    if (auto* oa = ve->find("ORIENTED_CLOSED_SHELL")) {
                        if (oa->size() >= 3 && (*oa)[2].isRef()) shell = (*oa)[2].asRef();
                    }
                }
                ids.push_back(shell);
            }
        }
    }
    std::vector<const ValueList*> out;
    for (int id : ids) {
        const Entity* shellE = doc.get(id);
        // CLOSED_SHELL(name, cfs_faces) -- index 0 is the (usually blank)
        // name string, index 1 is the actual face list.
        auto* shellArgs = shellE ? shellE->find("CLOSED_SHELL") : nullptr;
        if (shellArgs && shellArgs->size() >= 2) out.push_back(shellArgs);
    }
    return out;
}

static Solid resolveSolid(const Document& doc, int manifoldSolidBrepId,
                           int& facesResolved, int& facesSkipped)
{
    Solid solid;
    const Entity* e = doc.get(manifoldSolidBrepId);
    if (!e) return solid;
    const auto shells = solidShells(doc, e);
    if (shells.empty()) return solid;

    bool dbgSkip = std::getenv("FIELDES_STEP_DEBUG_SKIPPED_FACES") != nullptr;
    bool dbgIds = std::getenv("FIELDES_STEP_DEBUG_FACE_IDS") != nullptr;
    int localSkipped = 0;
    size_t shellFaces = 0;
    // (A solid with cavities: the faces of its cavities' shells are part of
    // its boundary too -- the reconstruction decides inside / outside by
    // crossings of all of them.)
    for (const ValueList* shellArgs : shells)
    for (auto& faceRef : (*shellArgs)[1].asList()) {
        shellFaces++;
        Face f = resolveFace(doc, faceRef.asRef());
        progress::faceDone();
        if (dbgIds) {
            fprintf(stderr, "[dbg face id] solid=#%d faceIdx=%zu entity=#%d\n",
                    manifoldSolidBrepId, solid.faces.size() + localSkipped, faceRef.asRef());
        }
        if (f.valid()) {
            solid.faces.push_back(std::move(f));
            facesResolved++;
        } else {
            facesSkipped++;
            localSkipped++;
            if (dbgSkip) {
                const Entity* fe = doc.get(faceRef.asRef());
                fprintf(stderr, "[dbg skipped face] solid=#%d faceEntity=#%d type=%s\n",
                        manifoldSolidBrepId, faceRef.asRef(), fe ? fe->type.c_str() : "?");
            }
        }
    }
    if (dbgSkip) {
        fprintf(stderr, "[dbg skipped face] solid=#%d total shell faces=%zu resolved=%zu skipped=%d\n",
                manifoldSolidBrepId, shellFaces, solid.faces.size(), localSkipped);
    }

    for (size_t fi = 0; fi < solid.faces.size(); fi++) {
        auto& f = solid.faces[fi];
        solid.boundMin = solid.boundMin.cwiseMin(f.boundMin);
        solid.boundMax = solid.boundMax.cwiseMax(f.boundMax);
    }
    for (auto& f : solid.faces) {
        for (auto& loop : f.loops) loop.makeBox();
    }
    auto tree = std::make_shared<FaceBoxTree>();
    tree->build(solid.faces);
    solid.faceTree = tree;
    return solid;
}

namespace {
// The padding faceRayHits (step_reconstruct.cpp) gives a face's box
const double kFaceBoxPad = 1e-6;

int buildFaceNode(FaceBoxTree& t, const std::vector<Face>& faces, const std::vector<Vec3>& centre,
                  int start, int count)
{
    FaceBoxTree::Node node;
    node.start = start;
    node.count = count;
    node.lo = Vec3::Constant(1e300);
    node.hi = Vec3::Constant(-1e300);
    Vec3 clo = Vec3::Constant(1e300), chi = Vec3::Constant(-1e300);
    for (int k = start; k < start + count; k++) {
        const int fi = t.order[size_t(k)];
        node.lo = node.lo.cwiseMin(faces[size_t(fi)].boundMin - Vec3::Constant(kFaceBoxPad));
        node.hi = node.hi.cwiseMax(faces[size_t(fi)].boundMax + Vec3::Constant(kFaceBoxPad));
        clo = clo.cwiseMin(centre[size_t(fi)]);
        chi = chi.cwiseMax(centre[size_t(fi)]);
    }
    const int index = int(t.nodes.size());
    t.nodes.push_back(node);
    const Vec3 ext = chi - clo;
    int axis = 0;
    if (ext.y() > ext[axis]) axis = 1;
    if (ext.z() > ext[axis]) axis = 2;
    if (count <= 4 || !(ext[axis] > 0)) return index;
    const int half = count / 2;
    std::nth_element(t.order.begin() + start, t.order.begin() + start + half, t.order.begin() + start + count,
                     [&](int a, int b) { return centre[size_t(a)][axis] < centre[size_t(b)][axis]; });
    const int l = buildFaceNode(t, faces, centre, start, half);
    const int r = buildFaceNode(t, faces, centre, start + half, count - half);
    t.nodes[size_t(index)].left = l;
    t.nodes[size_t(index)].right = r;
    return index;
}
}   // namespace

void FaceBoxTree::build(const std::vector<Face>& faces)
{
    nodes.clear();
    order.clear();
    faceCount = faces.size();
    std::vector<Vec3> centre(faces.size());
    for (size_t i = 0; i < faces.size(); i++) {
        centre[i] = 0.5 * (faces[i].boundMin + faces[i].boundMax);
        if (faces[i].valid()) order.push_back(int(i));
    }
    if (!order.empty()) buildFaceNode(*this, faces, centre, 0, int(order.size()));
}

void FaceBoxTree::rayFaces(const Vec3& p, const Vec3& d, std::vector<int>& out) const
{
    out.clear();
    if (nodes.empty()) return;
    int stack[128];
    int sp = 0;
    stack[sp++] = 0;
    while (sp) {
        const Node& n = nodes[size_t(stack[--sp])];
        // (the slab test faceRayHits makes with a face's own padded box)
        double tmin = 0.0, tmax = 1e18;
        bool miss = false;
        for (int i = 0; i < 3 && !miss; i++) {
            if (std::abs(d[i]) < 1e-14) {
                if (p[i] < n.lo[i] || p[i] > n.hi[i]) miss = true;
            } else {
                double t1 = (n.lo[i] - p[i]) / d[i], t2 = (n.hi[i] - p[i]) / d[i];
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) miss = true;
            }
        }
        if (miss) continue;
        if (n.left < 0 || sp + 2 > 128) {
            for (int k = n.start; k < n.start + n.count; k++) out.push_back(order[size_t(k)]);
        } else {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }
}

ImportResult importStepFile(const std::string& path)
{
    ImportResult result;

    Document doc;
    std::string err;
    if (!doc.load(path, err)) {
        result.error = "Could not read STEP file: " + err;
        return result;
    }

    auto model = std::make_shared<Model>();
    // The solids: MANIFOLD_SOLID_BREPs, then the ones with cavities
    // (BREP_WITH_VOIDS; after, so the others keep their numbers)
    std::vector<const Entity*> solidEntities;
    {
        std::set<int> seen;
        for (auto* e : doc.ofType("MANIFOLD_SOLID_BREP")) {
            if (seen.insert(e->id).second) solidEntities.push_back(e);
        }
        for (auto* e : doc.ofType("BREP_WITH_VOIDS")) {
            if (seen.insert(e->id).second) solidEntities.push_back(e);
        }
    }
    {   // (progress: the faces to resolve)
        size_t faces = 0;
        for (auto* e : solidEntities) {
            for (const ValueList* shellArgs : solidShells(doc, e)) faces += (*shellArgs)[1].asList().size();
        }
        progress::setFaces(faces);
    }
    for (auto* e : solidEntities) {
        Solid solid = resolveSolid(doc, e->id, result.numFacesResolved, result.numFacesSkipped);
        if (!solid.faces.empty()) {
            solid.entityId = e->id;
            auto* args = e->find("MANIFOLD_SOLID_BREP");
            if (!args) args = e->find("BREP_WITH_VOIDS");
            if (args && !args->empty()) solid.name = (*args)[0].asString();
            model->solids.push_back(std::move(solid));
        }
    }

    // Units and assembly placements (see step_assembly.hpp)
    result.fileUnitMM = fileLengthUnitMM(doc);
    std::vector<int> ids;
    for (auto& s : model->solids) ids.push_back(s.entityId);
    auto instances = solidInstances(doc, ids);
    for (auto& s : model->solids) {
        s.instances = instances[s.entityId];
        if (s.instances.empty()) {
            s.instances.emplace_back();
            s.instances.back().linear *= result.fileUnitMM;
        }
    }
    if (std::getenv("FIELDES_STEP_DEBUG_INSTANCES")) {
        for (size_t si = 0; si < model->solids.size(); si++) {
            for (const auto& in : model->solids[si].instances) {
                const auto& L = in.linear;
                fprintf(stderr, "[instance] solid %zu #%d '%s': L=[%g %g %g; %g %g %g; %g %g %g] t=(%g %g %g)\n",
                        si, model->solids[si].entityId, in.name.c_str(),
                        L(0, 0), L(0, 1), L(0, 2), L(1, 0), L(1, 1), L(1, 2), L(2, 0), L(2, 1), L(2, 2),
                        in.offset.x(), in.offset.y(), in.offset.z());
            }
        }
    }

    result.numSolids = static_cast<int>(model->solids.size());

    if (result.numSolids == 0) {
        result.error = "No importable solids found in file "
            "(it may contain only a wireframe/sketch, or use surface "
            "types this importer doesn't support yet)";
        return result;
    }

    result.ok = true;
    result.model = model;
    return result;
}

}  // namespace step
}  // namespace libfive
