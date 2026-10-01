/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once
#include <QIcon>

namespace FielDes {

/*  The line-art icons of the top dock (Open, Import) and the application mark, drawn at run time  */
namespace Icons
{
    QIcon open();
    QIcon importFile();
    QIcon logo(int size);           // the FielDes mark (the window icon)
}

}   // namespace FielDes
