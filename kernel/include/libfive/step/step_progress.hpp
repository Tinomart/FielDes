/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <string>
#include <vector>

namespace libfive {
namespace step {

/*  How far the STEP import running now has got, for a GUI to show (poll it
 *  from any thread): the stage it is in, how far that stage is (0..1) and
 *  what it is doing; fraction < 0 when no import is running.  Counted, never
 *  predicted, each stage on its own:
 *    0  reading the file: the share of it parsed
 *    1  resolving its faces: faces done of the faces in its solids
 *    2  rebuilding its solids: the solids fully rebuilt of the solids to
 *       rebuild.  A solid counts when it is done, and only then (moved copies
 *       of other solids aren't rebuilt, so they don't count).  With one big
 *       solid among many small ones the bar waits for the big one: it says
 *       what has happened, not when the rest will.  */
struct ImportProgress
{
    double fraction = -1.0;
    std::string text;
    int stage = 0;
};
ImportProgress importProgress();

namespace progress {

// The importer's side: reading the file, resolving its faces, then its solids
void begin(const std::string& path);
void setRead(double fraction);
void setFaces(size_t total);
void faceDone();
void setSolids(size_t count);
void skipSolid(size_t solid);   // a moved copy of another: not rebuilt
void solidStarted(size_t solid);
void solidDone(size_t solid);
void end();

// Which solid / region the calling thread works on: only the labels of the
// debugging output (LIBFIVE_STEP_STAGE_TIMES)
class Scope
{
public:
    Scope(long solid, long region = 0);
    ~Scope();
private:
    long prevSolid, prevRegion;
};
long currentSolid();
long currentRegion();

}   // namespace progress
}   // namespace step
}   // namespace libfive
