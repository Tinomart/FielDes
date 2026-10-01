/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <Eigen/Eigen>

#include "libfive/oracle/oracle.hpp"
#include "libfive/oracle/oracle_memo.hpp"
#include "libfive/eval/eval_array_size.hpp"
#include "libfive/render/brep/default_new_delete.hpp"

namespace libfive {

template <int N=LIBFIVE_EVAL_ARRAY_SIZE>
class OracleStorage : public Oracle
{
public:
    void set(const Eigen::Vector3f& p, size_t index=0) override
    {
        points.col(index) = p;
    }

    void set(const Eigen::Vector3f& _lower,
             const Eigen::Vector3f& _upper) override
    {
        lower = _lower;
        upper = _upper;
    }

    /*
     *  A block of points, each answered by evalPoint -- or, when the oracle names what it
     *  computes (memoKey), taken from the memory of what it has answered before (see
     *  oracle_memo.hpp): the same value, exactly, without computing it again.
     */
    void evalArray(
            Eigen::Block<Eigen::Array<float, Eigen::Dynamic, LIBFIVE_EVAL_ARRAY_SIZE,
                                      Eigen::RowMajor>, 1, Eigen::Dynamic> out) override
    {
        evalArrayMemo(out);
    }

    /*
     *  Inefficient-but-correct implementation of evalDerivs
     *  (delegates work to evalFeatures)
     */
    void evalDerivs(
            Eigen::Block<Eigen::Array<float, 3, Eigen::Dynamic>,
                         3, 1, true> out, size_t index=0) override
    {
        Eigen::Vector3f before = points.col(0);
        points.col(0) = points.col(index);

        boost::container::small_vector<Feature, 4> fs;
        evalFeatures(fs);
        assert(fs.size() > 0);
        out = fs[0].deriv;

        points.col(0) = before;
    }

    /*  Make an aligned new operator, as this class has Eigen structs
     *  inside of it (which are aligned for SSE) */
    ALIGNED_OPERATOR_NEW_AND_DELETE(OracleStorage)

protected:
    /*  What this oracle computes, if it is costly enough to remember its answers: a key that
     *  names the oracle and everything it depends on (its OracleClause's contentKey), or null  */
    virtual const std::string* memoKey() const { return nullptr; }

    void evalArrayMemo(
            Eigen::Block<Eigen::Array<float, Eigen::Dynamic, LIBFIVE_EVAL_ARRAY_SIZE,
                                      Eigen::RowMajor>, 1, Eigen::Dynamic> out)
    {
        const std::string* key = memoKey();
        if (!key || key->empty())
        {
            for (unsigned i = 0; i < out.cols(); ++i) evalPoint(out(i), i);
            return;
        }
        if (!m_memo) m_memo = OracleMemo::get(*key);
        for (unsigned i = 0; i < out.cols(); ++i)
        {
            const Eigen::Vector3f p = points.col(i).matrix();
            if (!m_memo->lookup(p, out(i)))
            {
                evalPoint(out(i), i);
                m_memo->store(p, out(i));
            }
        }
    }

    /* Local storage for set(Vector3f) */
    Eigen::Array<float, 3, N> points;

    /* Local storage for set(Interval) */
    Eigen::Vector3f lower;
    Eigen::Vector3f upper;

private:
    std::shared_ptr<OracleMemo> m_memo;
};

}   // namespace libfive
