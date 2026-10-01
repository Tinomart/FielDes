/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/step/step_progress.hpp"

#include <algorithm>
#include <mutex>

namespace libfive {
namespace step {

namespace {

struct SolidState
{
    int state = 0;                  // 0 waiting, 1 being rebuilt, 2 done, 3 a copy (not rebuilt)
};

std::mutex g_mutex;
bool g_running = false;
int g_stage = 0;
double g_read = 0.0;
size_t g_faces = 0, g_facesDone = 0;
std::string g_file;
std::vector<SolidState> g_solids;

thread_local long t_solid = -1;
thread_local long t_region = 0;

}   // namespace

ImportProgress importProgress()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ImportProgress p;
    if (!g_running) return p;
    const size_t slash = g_file.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? g_file : g_file.substr(slash + 1);
    p.stage = g_stage;
    if (g_stage == 0) {
        p.fraction = g_read;
        p.text = "Reading " + name;
        return p;
    }
    if (g_stage == 1) {
        p.fraction = g_faces ? double(g_facesDone) / double(g_faces) : 0.0;
        p.text = "Reading " + name + ": " + std::to_string(g_facesDone) + " of " +
                 std::to_string(g_faces) + " faces";
        return p;
    }
    // The solids fully rebuilt, of those to rebuild
    size_t nDone = 0, nRebuilt = 0;
    for (const auto& s : g_solids) {
        if (s.state == 3) continue;
        nRebuilt++;
        if (s.state == 2) nDone++;
    }
    p.fraction = nRebuilt ? double(nDone) / double(nRebuilt) : 1.0;
    p.text = "Importing " + name + ": " + std::to_string(nDone) + " of " +
             std::to_string(nRebuilt) + " solids rebuilt";
    if (nRebuilt < g_solids.size())
        p.text += " (+" + std::to_string(g_solids.size() - nRebuilt) +
                  (g_solids.size() - nRebuilt == 1 ? " copy)" : " copies)");
    return p;
}

namespace progress {

void begin(const std::string& path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_running = true;
    g_stage = 0;
    g_read = 0.0;
    g_faces = g_facesDone = 0;
    g_file = path;
    g_solids.clear();
}

void setRead(double fraction)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running && g_stage == 0) g_read = std::max(g_read, std::min(1.0, fraction));
}

void setFaces(size_t total)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_running) return;
    g_stage = 1;
    g_faces = total;
    g_facesDone = 0;
}

void faceDone()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running && g_stage == 1 && g_facesDone < g_faces) g_facesDone++;
}

void setSolids(size_t count)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_solids.assign(count, SolidState());
    g_stage = 2;
}

void skipSolid(size_t solid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (solid < g_solids.size()) g_solids[solid].state = 3;
}

void solidStarted(size_t solid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (solid < g_solids.size()) g_solids[solid].state = 1;
}

void solidDone(size_t solid)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (solid < g_solids.size() && g_solids[solid].state != 3) g_solids[solid].state = 2;
}

void end()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_running = false;
}

Scope::Scope(long solid, long region)
    : prevSolid(t_solid), prevRegion(t_region)
{
    t_solid = solid;
    t_region = region;
}

Scope::~Scope()
{
    t_solid = prevSolid;
    t_region = prevRegion;
}

long currentSolid() { return t_solid; }
long currentRegion() { return t_region; }

}   // namespace progress
}   // namespace step
}   // namespace libfive
