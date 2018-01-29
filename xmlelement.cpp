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

#include "xmlelement.h"

#include <QDebug>
#include "gumbo.h"

#undef PRINT_ON_LOAD

static int dump_indentation = 0;

const QString FIXED_STRING_NAME("FIXED-STRING");


void XmlElement::dump_tree() const
{
    QString indentation(dump_indentation, QChar(QChar::Space));

    QString line;

    if (objectName() == FIXED_STRING_NAME)
    {
        qDebug().noquote().nospace() << indentation << p_fixed_text;
        return;
    }

    line = "<" + objectName();
    for (auto iter : p_attributes)
    {
        line.append(" " + iter.name + "=\"" + iter.value + "\"");
    }
    QList<XmlElement*> child_items = findChildren<XmlElement*>(QString(), Qt::FindDirectChildrenOnly);
    bool is_empty = (p_fixed_text.isEmpty() && children().count() == 0);
    line.append(is_empty ? "/>" : ">");
    qDebug().noquote().nospace() << indentation << line;

    if (!p_fixed_text.isEmpty())
        qDebug().noquote().nospace() << indentation << p_fixed_text;

    dump_indentation += 3;
    foreach (XmlElement *child, child_items)
    {
        child->dump_tree();
    }
    dump_indentation -= 3;
    if (!is_empty)
        qDebug().noquote().nospace() << indentation << "</" << objectName() << ">";
}


XmlElement::XmlElement(const QString &fixed_text, QObject *parent) :
    QObject(parent)
{
    setObjectName(FIXED_STRING_NAME);
    p_fixed_text = fixed_text;
}


static void create_children(GumboNode *node, QObject *parent)
{
    GumboVector *children = &node->v.element.children;
    for (unsigned i=0; i<children->length; i++)
    {
        GumboNode *child = static_cast<GumboNode*>(children->data[i]);
        switch (child->type)
        {
        case GUMBO_NODE_TEXT:
        case GUMBO_NODE_ELEMENT:
            new XmlElement(child, parent);
            break;

        case GUMBO_NODE_WHITESPACE:
            //qDebug() << "XmlElement(Gumbo) : ignoring GUMBO_NODE_WHITESPACE";
            break;
        case GUMBO_NODE_DOCUMENT:
            //qDebug() << "XmlElement(Gumbo) : ignoring GUMBO_NODE_DOCUMENT";
            break;
        case GUMBO_NODE_CDATA:
            //qDebug() << "XmlElement(Gumbo) : ignoring GUMBO_NODE_CDATA";
            break;
        case GUMBO_NODE_COMMENT:
            //qDebug() << "XmlElement(Gumbo) : ignoring GUMBO_NODE_COMMENT";
            break;
        case GUMBO_NODE_TEMPLATE:
            //qDebug() << "XmlElement(Gumbo) : ignoring GUMBO_NODE_TEMPLATE";
            break;
        }
    }
}

/**
 * @brief XmlElement::XmlElement
 * Creates an XmlElement tree from the provided GumboNode tree.
 * @param node
 * @param parent
 */
XmlElement::XmlElement(GumboNode *node, QObject *parent) :
    QObject(parent)
{
    if (node->type == GUMBO_NODE_TEXT)
    {
        setObjectName(FIXED_STRING_NAME);
        p_fixed_text = node->v.text.text;
        return;
    }
    // otherwise it is a NODE
    setObjectName(gumbo_normalized_tagname(node->v.element.tag));
    //qDebug() << "XmlElement(gumbo) =" << objectName();

    const GumboVector *attribs = &node->v.element.attributes;
    for (unsigned i=0; i<attribs->length; i++)
    {
        GumboAttribute *at = static_cast<GumboAttribute*>(attribs->data[i]);
        p_attributes.append(Attribute(at->name, at->value));
        //qDebug() << "    attribute:" << at->name << "=" << at->value;
    }

    create_children(node, this);
}


QString XmlElement::childString() const
{
    XmlElement *child = xmlChild(FIXED_STRING_NAME);
    return child ? child->p_fixed_text : QString();
}


XmlElement::XmlElement(QXmlStreamReader *reader, QObject *parent) :
    QObject(parent)
{
    // Extract common data from this XML element
    setObjectName(reader->name().toString());

    //p_namespace_uri = reader->namespaceUri().toString();

    // Transfer all attributes into a map
    const QXmlStreamAttributes attr = reader->attributes();
    p_attributes.reserve(attr.size());
    for (int idx=0; idx<attr.size(); idx++)
        p_attributes.append(Attribute(attr.at(idx).name().toString(), attr.at(idx).value().toString()));

    // Now read the rest of this element
    while (!reader->atEnd())
    {
        switch (reader->readNext())
        {
        case QXmlStreamReader::StartElement:
            //qDebug().noquote() << "StartElement:" << reader->name();
            // The start of a child
            new XmlElement(reader, this);
            break;

        case QXmlStreamReader::EndElement:
            //qDebug().noquote() << "EndElement" << reader->name();
            // The end of this element
            return;

        case QXmlStreamReader::Characters:
            // Add the characters to the end of the text for this element.
            if (!reader->isWhitespace())
            {
                //qDebug().noquote() << "Characters:" << reader->text();
                // Some things shouldn't be converted.
                bool is_binary =
                        (parent->objectName() == "asset" &&
                         (objectName() == "contents"  ||
                          objectName() == "thumbnail" ||
                          objectName() == "summary")) ||
                        (parent->objectName() == "smart_image" &&
                         (objectName() == "subset_mask"  ||
                          objectName() == "superset_mask")) ||
                        (parent->objectName() == "details" &&
                         (objectName() == "cover_art"));

                if (is_binary)
                {
                    p_byte_data = QByteArray::fromBase64(reader->text().toLocal8Bit());
                }
                else if (reader->text().left(1) == "<")
                {
                    //
                    // Use the GUMBO library to parse the HTML5 code.
                    //
                    // It uses pointers into the original data, so we have to store
                    // the QByteArray until gumbo_destroy_output is called.
                    //
                    QByteArray text(reader->text().toUtf8());
                    GumboOutput *output = gumbo_parse(text);
                    // The output will be
                    // <html>
                    //   <head/>
                    //   <body>
                    //     the nodes that we want

                    // Check that HTML has at least 2 children: head and body
                    if (output->root->v.element.children.length >= 2)
                    {
                        GumboNode *body_node = static_cast<GumboNode*>(output->root->v.element.children.data[1]);
                        create_children(body_node, this);
                    }
                    // Get GUMBO to release all the memory
                    gumbo_destroy_output(&kGumboDefaultOptions, output);
                }
                else
                    new XmlElement(reader->text().toString(), this);
            }
            break;

        case QXmlStreamReader::Comment:
            //qDebug().noquote() << "Comment:" << reader->text();
            break;
        case QXmlStreamReader::EntityReference:
            //qDebug().noquote() << "EntityReference:" << reader->name();
            break;
        case QXmlStreamReader::ProcessingInstruction:
            //qDebug().noquote() << "ProcessingInstruction";
            break;
        case QXmlStreamReader::NoToken:
            //qDebug().noquote() << "NoToken";
            break;
        case QXmlStreamReader::Invalid:
            //qDebug().noquote() << "Invalid:" << reader->errorString();
            break;
        case QXmlStreamReader::StartDocument:
            //qDebug().noquote() << "StartDocument:" << reader->documentVersion();
            setObjectName("StartDocument");
            break;
        case QXmlStreamReader::EndDocument:
            //qDebug().noquote() << "EndDocument";
            return;
        case QXmlStreamReader::DTD:
            //qDebug().noquote() << "DTD:" << reader->text();
            break;
        }
    }
}


XmlElement *XmlElement::readTree(QIODevice *device)
{
    XmlElement *root_element = 0;
    QXmlStreamReader reader;
    reader.setDevice(device);

    // Move to the start of the first element
    if (reader.readNextStartElement())
    {
        root_element = new XmlElement(&reader, 0);
    }

    if (reader.hasError())
    {
        qWarning() << "Failed to parse XML in structure file: line" <<
                      reader.lineNumber() << ", column" <<
                      reader.columnNumber() << "error:" <<
                      reader.errorString();
        return root_element;
    }
#ifdef PRINT_ON_LOAD
    root_element->dump_tree();
#endif
    return root_element;
}


bool XmlElement::hasAttribute(const QString &name) const
{
    for (auto attr : p_attributes)
        if (attr.name == name) return true;
    return false;
}


QString XmlElement::attribute(const QString &name) const
{
    for (auto attr : p_attributes)
        if (attr.name == name) return attr.value;
    return QString();
}

/**
 * @brief XmlElement::childrenWithAttributes
 * Find all the child elements which have a specified attribute with a specified value.
 * @param name
 * @param value
 * @return
 */
QList<XmlElement *> XmlElement::childrenWithAttributes(const QString &name, const QString &value) const
{
    QList<XmlElement*> result;
    for (auto child : xmlChildren())
    {
        if (child->attribute(name) == value) result.append(child);
    }
    return result;
}


QString XmlElement::snippetName() const
{
    return hasAttribute("facet_name") ? attribute("facet_name") : attribute("label");
}


bool XmlElement::isFixedString() const
{
    return objectName() == FIXED_STRING_NAME;
}
