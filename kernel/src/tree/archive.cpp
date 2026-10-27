/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <iostream>
#include <cassert>

#include "libfive/tree/archive.hpp"
#include "libfive/tree/serializer.hpp"
#include "libfive/tree/deserializer.hpp"

namespace libfive
{
void Archive::addShape(Tree tree, std::string name, std::string doc,
                       std::map<Tree::Id, std::string> vars)
{
    Shape s {
        tree, name, doc, vars
    };

    shapes.push_back(s);
}

////////////////////////////////////////////////////////////////////////////////

void Archive::serialize(std::ostream& out)
{
    Serializer(out).run(*this);
}

bool Archive::serialize(std::ostream& out, const std::map<Tree::Id, float>& constants)
{
    Serializer s(out);
    s.constants = &constants;
    s.run(*this);
    return !s.unbound;
}

Archive Archive::deserialize(std::istream& data)
{
    return Deserializer(data).run();
}


}   // namespace libfive
