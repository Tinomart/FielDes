#include <iostream>
#include <cassert>

#include "libfive/tree/data.hpp"
#include "libfive/tree/content_key.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/transformed_oracle_clause.hpp"

namespace libfive {

std::string OracleClause::contentKey() const
{
    const std::string own = persistentKey();
    if (own.empty())
    {
        return std::string();
    }
    // (what it holds, by name and hash; and what it is made of: a moved copy of an oracle is its oracle and the three maps)
    std::string out = name() + "|" + own;
    for (const Tree& dep : dependencies())
    {
        out += "|" + (dep.get() ? treeContentKey(dep) : std::string("-"));
    }
    return out;
}

std::unique_ptr<const OracleClause> OracleClause::deserialize(
        const std::string& name, Deserializer& in)
{
    auto itr = installed().find(name);
    if (itr == installed().end())
    {
        std::cerr << "OracleClause::deserialize: no installed \""
                  << name << "\"\n"
                  << "  You may need to call OracleClause::install."
                  << std::endl;
        return nullptr;
    }
    else
    {
        return (*itr).second.second(in);
    }
}

bool OracleClause::serialize(const std::string& name,
                             const OracleClause* clause, Serializer& out)
{
    auto itr = installed().find(name);
    if (itr == installed().end())
    {
        std::cerr << "OracleClause::serialize: no installed \""
                  << name << "\"\n"
                  << "  You may need to call OracleClause::install."
                  << std::endl;
        return false;
    }
    else
    {
        return (*itr).second.first(clause, out);
    }
}

std::unique_ptr<const OracleClause> OracleClause::remap(
            Tree self, Tree X_, Tree Y_, Tree Z_) const
{
    assert(self->op() == Opcode::ORACLE);
    assert(&self->oracle_clause() == this);

    return std::make_unique<TransformedOracleClause>(self, X_, Y_, Z_);
}

}   // namespace libfive
