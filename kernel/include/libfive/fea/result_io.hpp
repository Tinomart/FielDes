/*
 *  FielDes: solved analyses kept on disk (the result cache).
 *
 *  A solved problem -- its mesh, every result field, the modes, the density of every iteration of an
 *  optimisation, the steps of a flow -- is written to one file and read back into a problem that answers
 *  every query as the solved one did (fields, numbers, elements, streamlines), without meshing or solving.
 *  The file holds the results only: a loaded problem cannot be solved again.
 *
 *  The results' serials (the keys of their fields, see Result::serial) are kept in the file, and
 *  assignSerials() makes them a function of the problem's hash and a salt the caller derives from the
 *  whole problem, so the fields of a problem solved in one session and read back in another have the same
 *  keys: what was rendered of them is found in the render cache again.
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this file,
 *  You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace libfive {
namespace fea {

class TetProblem;
class TetThermalProblem;
class TetFlowProblem;
class StaticProblem;
class ThermalProblem;

struct ResultIO
{
    /*  false when the file cannot be written (or the problem is not solved)  */
    static bool save(const TetProblem& p, const std::string& path);
    static bool save(const TetThermalProblem& p, const std::string& path);
    static bool save(const TetFlowProblem& p, const std::string& path);
    static bool save(const StaticProblem& p, const std::string& path);
    static bool save(const ThermalProblem& p, const std::string& path);

    /*  null when the file is missing, of another kind, or not readable as written  */
    static std::unique_ptr<TetProblem> loadTet(const std::string& path);
    static std::unique_ptr<TetThermalProblem> loadTetThermal(const std::string& path);
    static std::unique_ptr<TetFlowProblem> loadTetFlow(const std::string& path);
    static std::unique_ptr<StaticProblem> loadStatic(const std::string& path);
    static std::unique_ptr<ThermalProblem> loadThermal(const std::string& path);

    /*  The serials of every result the problem holds, from its hash and the salt (a salt of 0 leaves them)  */
    static void assignSerials(TetProblem& p, uint64_t salt);
    static void assignSerials(TetThermalProblem& p, uint64_t salt);
    static void assignSerials(TetFlowProblem& p, uint64_t salt);
    static void assignSerials(StaticProblem& p, uint64_t salt);
    static void assignSerials(ThermalProblem& p, uint64_t salt);
};

}   // namespace fea
}   // namespace libfive
