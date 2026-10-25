/*
libfive: a CAD kernel for modeling with implicit functions

The design of a density-based topology optimization on a tetrahedral mesh, whatever physics it designs for: one density to each tetrahedron
(or to each line of an extrusion), smoothed by a filter (of the design only: the elements that are held do not count in it), sharpened towards 0 and 1 by a projection that steepens as the run goes on, kept
symmetric about the planes the whole problem is symmetric about, held where a region says so (keep, avoid, and what the physics holds), moved
by the method of moving asymptotes on the sensitivities of MEASURES (topopt.hpp: the formulas of them are the objective and the constraints),
with the steps controlled by what the design is worth (the objective plus a penalty on each constraint it breaks) and the best design given
back.

The physics gives what it alone knows -- the `Physics` below: the measures of a density with their sensitivities (an adjoint solve, an
eigenvector ...), the elements that it holds solid, whether the problem is symmetric about a plane, and what is drawn while it runs.  The
structural analysis (tetfea.cpp) is one; the flow is another.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/fea/tetmesh.hpp"
#include "libfive/fea/topopt.hpp"
#include "libfive/tree/tree.hpp"

namespace libfive {
namespace fea {
namespace topopt {

/*  A plane the design is kept symmetric about: axis 0, 1, 2 = x, y, z at the coordinate `at`; atCentre: the plane through the middle of the part's
 *  extent along that axis.  (In the planes that were used: atCentre says that the plane was found, not asked for.)  */
struct Mirror
{
    int axis = 2;
    double at = 0;
    bool atCentre = true;
};

/*  What every density-based design is asked for, whatever the physics  */
struct DesignSettings
{
    /*  The objective (the first output, minimised) and the constraints (each kept when it is 0 or less) as formulas of the measures  */
    Program program;
    std::string objectiveLabel;         // what the objective is called, for the picture that is drawn as it goes
    double objectiveSign = 1.0;         // -1 when the objective is a number that is made as large as it can be (the program minimises its negative)
    double startDensity = 0.3;          // the density every element of the design starts with
    /*  How far a density may move in the first iteration (a share of its range; the cap shrinks to a twentieth by the last).  0: as many iterations ask for --
     *  from 0.5 for a few to 0.2 for a hundred.  A physics whose measures change a lot with a little material (a flow: the friction of an element at the edge
     *  of a body) asks for less: a large step overshoots and the design swings (and breaks its symmetry)  */
    double moveStart = 0;
    double filterRadius = 0;            // 0: 1.5 element sizes
    int extrude = -1;                   // 0 / 1 / 2: the design is constant along x / y / z
    int iterations = 100;
    std::vector<Tree> keep, avoid;
    /*  Set when the design space is bigger than the part (the part grown outwards): the part itself.  The volume fraction is then of the part's
     *  volume, the design starts as the part (the rest of the space nearly empty) and material may be put outside it where that makes the design
     *  better  */
    Tree origin = Tree::invalid();
    /*  The design to start from, when the design space is a new one made round a design that was found in a smaller one: the field's value at the
     *  centre of each element is the value of the design variable it starts with (not under 0.02, so that nothing starts as a hole it could never
     *  be filled from).  Not valid: every element starts as `startDensity` (outside the part: 0.02)  */
    Tree startField = Tree::invalid();
    /*  With `startField`: where it is known (negative inside) -- the design space it was found in.  The field goes on beyond its mesh as the value at
     *  the edge of it, and that is not a design there: an element outside this region starts as one outside the part does, nearly empty.  Not valid:
     *  the field is known everywhere  */
    Tree startDomain = Tree::invalid();
    /*  With `origin`: an element outside the part counts for the frontier only when it is farther than this from it (the value of `origin` there,
     *  which is the distance when the field is one)  */
    double frontierDistance = 0;
    /*  With `origin`: where the edge of the space cannot be moved outwards -- it is held in by something the space is not made larger against (the
     *  place of a support or a load, a limit on how far the design may reach): an element whose centre is inside this region (negative) does not
     *  count for the frontier.  Not valid: every element of the edge counts  */
    Tree frontierBlocked = Tree::invalid();
    /*  With `origin`: the run stops as soon as the design reaches the edge of its space -- material (a density above a tenth) in an element on the
     *  edge that lies outside the part --, at the end of the iteration, and gives back what a run in a larger space needs to go on from there
     *  (DesignResult::resume, designNodal).  The space follows the design: it is never a wall the design stands against while the run goes on  */
    bool stopAtEdge = false;
    /*  A run that goes on from one that stopped (DesignResult::resume): the iteration it went on from, the sharpening's stage and how long it had
     *  been held up, the size of the step, the scale of the objective.  `iterations` is the length of the whole run, as it was.  Empty: a new run  */
    std::vector<double> resume;
    /*  How crisp the design is: its density is projected towards 0 and 1 with a step that grows steeper to this number; 1: not at all  */
    double sharpness = 16.0;
    /*  Mirror symmetry: the design is kept symmetric about these planes; symmetryAuto: look for the planes the whole problem is symmetric about  */
    std::vector<Mirror> mirrors;
    bool symmetryAuto = false;
    /*  The sharpening goes up a little every iteration, from where it would have started to three quarters of the run, instead of doubling at a few steps.  A design that is still grey when the sharpening doubles loses (or gains) a large share of its volume at once -- by a quarter, a half, in a flow -- and its shape snaps; a little at a time the constraints keep up.  The step control then compares designs whose sharpening differs by a fifth or less as the same  */
    bool smoothSharpening = false;
};

/*  What a kept iteration shows: the physics draws it  */
struct IterationView
{
    int iteration = 0, iterations = 0;
    double objective = 0;               // as the user has it (the sign applied)
    double change = 0;                  // how far a density moved in the step that follows
    double share = 0;                   // the volume of the design, of the part's
    const std::vector<double>& history;             // the objective of each kept iteration
    const std::vector<double>& xPhys;               // the density of each element
    const std::vector<float>& nodal;                // the density at the nodes
    double filterRadius = 0;
};

class Physics
{
public:
    virtual ~Physics() = default;

    virtual int measureCount() const = 0;
    virtual const char* taskName() const { return "optimising"; }
    /*  The volume of the part (the design space, or the part it was grown from, where the design space is bigger): what a share of the volume is of.
     *  Told once, before the first evaluation  */
    virtual void partVolume(double /*total*/) {}

    /*  The elements that stay solid whatever the design does (a load acts there, a support holds it): their `state` is set to 1 (0 design, 1 solid,
     *  2 empty).  `supported`: the elements that hold a support -- what material has to be joined to to be held; left empty when the physics has no
     *  such rule (material that nothing joins to a support is then not taken out)  */
    virtual void fixedElements(std::vector<char>& /*state*/, std::vector<char>& /*supported*/) {}

    /*  Is the whole problem symmetric about the plane, as far as the physics knows it (the part, the loads, the supports ...)?  `mirrored`: the
     *  mirror image of the centre of each element; `regionMismatch`: the share of the volume whose keep / avoid regions differ between a point and
     *  its image (for the physics' own debug line)  */
    virtual bool symmetricAbout(int /*axis*/, double /*at*/, const std::vector<Eigen::Vector3f>& /*mirrored*/, double /*regionMismatch*/,
                                std::string& why)
    {
        why = "the physics does not say";
        return false;
    }

    /*  The measures of a design: their values (values) and the sensitivity of each to the density of every element (sens[measure][element]),
     *  at the density xPhys.  `update`: this is the design of an iteration (not a look at it a little off its place: the physics may move on its
     *  own state then); `iteration`: 0-based  */
    virtual bool evaluate(const std::vector<double>& xPhys, bool update, int iteration, std::vector<double>& values,
                          std::vector<std::vector<double>>& sens, std::string& error) = 0;

    /*  What the physics keeps between iterations that a step taken back has to give back (the factors of a p-norm ...)  */
    virtual std::vector<double> saveState() const { return {}; }
    virtual void restoreState(const std::vector<double>& /*state*/) {}

    /*  Solves as tight as a finite-difference check needs them (on = true) and as they were  */
    virtual void tightSolves(bool /*on*/) {}

    /*  An iteration was kept: for the picture that is drawn as the optimisation goes  */
    virtual void iterationKept(const IterationView& /*view*/) {}
};

/*  What the run found  */
struct DesignResult
{
    std::vector<double> history;                            // the objective of each kept iteration (the sign applied)
    std::vector<std::vector<double>> baseHistory;           // every measure at each
    std::vector<std::vector<double>> outputHistory;         // every output of the program at each (the objective, then the constraints)
    std::vector<std::vector<float>> densityHistory;         // the density at the nodes after each
    std::vector<double> xPhys;                              // the density of each element of the design that is given back
    std::vector<float> nodal;                               // ... at the nodes
    std::vector<Mirror> mirrorsUsed;
    double designVolume = 0;                                // the volume of the design (the density integrated)
    double frontier = 0;                                    // see TetProblem::frontierDensity
    int iterations = 0;                                     // how many iterations ran (the number of the one to go on from, when it stopped)
    int stepsBack = 0;                                      // how many steps made the design worse and were taken back
    /*  Set when the run stopped at the edge of its space (DesignSettings::stopAtEdge): where a run goes on from -- the iteration, the sharpening's
     *  stage, how long it had been held up, the size of the step, the scale of the objective -- and the design it stopped at, the variable of
     *  the optimisation at the nodes (the step already taken: it is the design the next iteration evaluates).  Empty: the run ran to its end  */
    std::vector<double> resume;
    std::vector<float> designNodal;
};

/*  The design.  `vol`: each element's volume; `h`: the mesh's element size; `vtStart` / `vtList`: each vertex's elements (CSR).  False with a message
 *  when it cannot be made (nothing to design, the update fails, cancelled ...)  */
bool runDensityDesign(const TetMesh& mesh, const std::vector<double>& vol, double h, const std::vector<uint32_t>& vtStart,
                      const std::vector<uint32_t>& vtList, const DesignSettings& settings, Physics& physics, DesignResult& result,
                      std::string& error, const std::atomic<bool>* cancel);

}   // namespace topopt
}   // namespace fea
}   // namespace libfive
