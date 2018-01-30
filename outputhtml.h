/*
 * This file is part of the RWout (https://github.com/farling42/RWoutput).
 * Copyright (c) 2018 Martin Smith.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OUTPUTHTML_H
#define OUTPUTHTML_H

#include <QXmlStreamWriter>
class XmlElement;

class OutputHtml
{
public:
    OutputHtml() {}

    static void toHtml(XmlElement *elem, int max_image_width, bool use_reveal_mask,
                       bool index_on_every_page);
};

#endif // OUTPUTHTML_H
