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

#define PRINT_ON_LOAD
//#define DEBUG_XMLELEMENT_CONSTRUCTOR

static int dump_indentation = 0;


void XmlElement::dump_tree() const
{
    QString indentation(dump_indentation, QChar(QChar::Space));

    if (!p_fixed_text.isEmpty())
    {
        // A simple fixed string
        qDebug().noquote().nospace() << indentation << p_fixed_text;
    }
    else
    {
        // A proper XML element
        QString line = "<" + objectName();
        for (auto iter : p_attributes)
        {
            line.append(" " + iter.name + "=\"" + iter.value + "\"");
        }
        QList<XmlElement*> child_items = findChildren<XmlElement*>(QString(), Qt::FindDirectChildrenOnly);

        if (child_items.count() == 0)
        {
            // No contents, so a terminated start element
            qDebug().noquote().nospace() << indentation << line << "/>";
        }
        else
        {
            // Terminate the opening of the parent element
            qDebug().noquote().nospace() << indentation << line << ">";

            dump_indentation += 3;
            foreach (XmlElement *child, child_items)
            {
                child->dump_tree();
            }
            dump_indentation -= 3;

            // Close the parent element
            qDebug().noquote().nospace() << indentation << "</" << objectName() << ">";
        }
    }
}

/**
 * @brief XmlElement::XmlElement
 * Create a simple fixed-string element in the XmlElement tree.
 * @param fixed_text
 * @param parent
 */

XmlElement::XmlElement(const QString &fixed_text, QObject *parent) :
    QObject(parent),
    p_fixed_text(fixed_text)
{
#ifdef DEBUG_XMLELEMENT_CONSTRUCTOR
    qDebug() << "XmlElement(string) =" << fixed_text;
#endif
}

/**
 * @brief XmlElement::create_children_from_gumbo
 * Read all the GUMBO nodes, looking for TEXT and ELEMENT nodes to convert to XmlElements.
 * @param node
 */

void XmlElement::createGumboChildren(GumboNode *node)
{
    GumboVector *children = &node->v.element.children;
    for (unsigned i=0; i<children->length; i++)
    {
        GumboNode *child = static_cast<GumboNode*>(children->data[i]);
        switch (child->type)
        {
        case GUMBO_NODE_TEXT:
            //qDebug() << "XmlElement(Gumbo) : GUMBO_NODE_TEXT = " << child->v.text.text;
            new XmlElement(child->v.text.text, this);
            break;

        case GUMBO_NODE_ELEMENT:
            //qDebug() << "XmlElement(Gumbo) : GUMBO_NODE_ELEMENT";
            new XmlElement(child, this);
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
 * Creates an XmlElement tree from the HTML embedded within the text part of an element.
 * @param node the GumboNode to be decoded
 * @param parent
 */
XmlElement::XmlElement(GumboNode *node, QObject *parent) :
    QObject(parent)
{
    setObjectName(gumbo_normalized_tagname(node->v.element.tag));

#ifdef DEBUG_XMLELEMENT_CONSTRUCTOR
    qDebug() << "XmlElement(gumbo ) =" << objectName();
#endif

    // Collect up all the attributes
    const GumboVector *attribs = &node->v.element.attributes;
    for (unsigned i=0; i<attribs->length; i++)
    {
        GumboAttribute *attr = static_cast<GumboAttribute*>(attribs->data[i]);
        p_attributes.append(Attribute(attr->name, attr->value));
        //qDebug() << "    attribute:" << at->name << "=" << at->value;
    }

    createGumboChildren(node);
}

/**
 * @brief XmlElement::XmlElement
 * Read the next XML element from the RW export file
 * @param reader
 * @param parent
 */

XmlElement::XmlElement(QXmlStreamReader *reader, QObject *parent) :
    QObject(parent)
{
    // Extract common data from this XML element
    setObjectName(reader->name().toString());

#ifdef DEBUG_XMLELEMENT_CONSTRUCTOR
    qDebug() << "XmlElement(reader)" << objectName();
#endif

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
                        createGumboChildren(body_node);
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

/**
 * @brief XmlElement::readTree
 * Read an entire export file into a single tree of XmlElements
 * @param device
 * @return
 */
XmlElement *XmlElement::readTree(QIODevice *device)
{
    XmlElement *root_element = nullptr;
    QXmlStreamReader reader;
    reader.setDevice(device);

    // Move to the start of the first element
    if (reader.readNextStartElement())
    {
        root_element = new XmlElement(&reader, nullptr);
    }

    if (reader.hasError())
    {
        qWarning() << "Failed to parse XML in structure file: line" <<
                      reader.lineNumber() << ", column" <<
                      reader.columnNumber() << "error:" <<
                      reader.errorString();
    }
#ifdef PRINT_ON_LOAD
    else
    {
        root_element->dump_tree();
    }
#endif
    return root_element;
}

/**
 * @brief XmlElement::childString
 * Finds the single child element which is a fixed string, and returns that value.
 * @return
 */
QString XmlElement::childString() const
{
    for (auto child : children())
    {
        XmlElement *elem = qobject_cast<XmlElement*>(child);
        if (elem && elem->isFixedString())
        {
            return elem->fixedText();
        }
    }
    return QString();
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


QString XmlElement::snippetName() const
{
    return hasAttribute("facet_name") ? attribute("facet_name") : attribute("label");
}
