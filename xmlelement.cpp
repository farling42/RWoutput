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
#include <QElapsedTimer>
#include "gumbo.h"

//#define DUMP_LOADED_TREE
//#define PRINT_XMLELEMENT_CONSTRUCTOR
//#define PRINT_GUMBO
#define PRINT_LOAD_TIME

static int dump_indentation = 0;

/**
 * @brief XmlElement::XmlElement
 * Create a simple fixed-string element in the XmlElement tree.
 * @param fixed_text
 * @param parent
 */

XmlElement::XmlElement(const QByteArray &fixed_text, QObject *parent) :
    QObject(parent),
    p_byte_data(fixed_text),
    is_fixed_text(true)
{
#ifdef PRINT_XMLELEMENT_CONSTRUCTOR
    qDebug().noquote().nospace() << "XmlElement(string)       " << fixed_text;
#endif
}

/**
 * @brief XmlElement::XmlElement
 * Creates an XmlElement from one node in the GUMBO tree of the embedded HTML
 * @param node the GumboNode to be decoded
 * @param parent
 */
XmlElement::XmlElement(const GumboNode *node, QObject *parent) :
    QObject(parent)
{
    setObjectName(gumbo_normalized_tagname(node->v.element.tag));

#ifdef PRINT_XMLELEMENT_CONSTRUCTOR
    qDebug().noquote().nospace() << "XmlElement(gumbo)     <" << objectName() << ">";
#endif

    // Collect up all the attributes
    GumboAttribute **attributes = reinterpret_cast<GumboAttribute**>(node->v.element.attributes.data);
    p_attributes.reserve(int(node->v.element.attributes.length));
    for (unsigned count = node->v.element.attributes.length; count > 0; --count)
    {
        const GumboAttribute *attr = *attributes++;
        p_attributes.append(Attribute(attr->name, attr->value));
#ifdef PRINT_GUMBO
        qDebug() << "GUMBO     " << attr->name << "=" << attr->value;
#endif
    }
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

#ifdef PRINT_XMLELEMENT_CONSTRUCTOR
    qDebug().noquote().nospace() << "XmlElement(reader) <" << objectName() << ">";
#endif

    // Collect up all the attributes
    const QXmlStreamAttributes attributes{reader->attributes()};
    p_attributes.reserve(attributes.size());
    for (auto attr : attributes)
        p_attributes.append(Attribute(attr.name(), attr.value()));

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
                QStringRef text = reader->text();

                //qDebug().noquote() << "Characters:" << text;
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
                    // Convert from BASE64 to BINARY, to reduce memory usage
                    // (is_fixed_text is NOT set, so later we'll convert it to the proper "thing")
                    // (toLocal8Bit applies the QTextCodec to the string, we know it is BASE64
                    //  so we can use toLatin1 - which doesn't do conversion)
                    p_byte_data = QByteArray::fromBase64(text.toLatin1());
                }
                else if (text.left(1) == "<")
                {
                    //
                    // Use the GUMBO library to parse the HTML5 code.
                    //
                    // It uses pointers into the original data, so we have to store
                    // the QByteArray until gumbo_destroy_output is called.
                    //
                    QByteArray buffer(text.toUtf8());
                    GumboOutput *output = gumbo_parse(buffer);
                    // The output will be
                    // <html>
                    //   <head/>
                    //   <body>
                    //     the nodes that we want
#ifdef PRINT_GUMBO
                    qDebug() << "---GUMBO start---";
#endif
                    // Check that HTML has at least 2 children: head and body
                    if (output->root->v.element.children.length >= 2)
                    {
                        const GumboNode *body_node = reinterpret_cast<GumboNode*>(output->root->v.element.children.data[1]);
                        parse_gumbo_nodes(body_node);
                    }
#ifdef PRINT_GUMBO
                    qDebug() << "---GUMBO finish---";
#endif
                    // Get GUMBO to release all the memory
                    gumbo_destroy_output(&kGumboDefaultOptions, output);
                }
                else
                {
                    new XmlElement(text.toString().toUtf8(), this);
                }
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
            qWarning().noquote() << "Invalid:" << reader->errorString();
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
 * @brief XmlElement::create_children_from_gumbo
 * Read all the GUMBO nodes, looking for TEXT and ELEMENT nodes to convert to XmlElements.
 * @param node
 */

void XmlElement::parse_gumbo_nodes(const GumboNode *node)
{
    GumboNode **children = reinterpret_cast<GumboNode**>(node->v.element.children.data);
    for (unsigned count = node->v.element.children.length; count > 0; --count)
    {
        const GumboNode *child = *children++;
        switch (child->type)
        {
        case GUMBO_NODE_TEXT:
#ifdef PRINT_GUMBO
            qDebug() << "GUMBO_NODE_TEXT: " << child->v.text.text;
#endif
            new XmlElement(child->v.text.text, this);
            break;

        case GUMBO_NODE_ELEMENT:
#ifdef PRINT_GUMBO
            qDebug() << "GUMBO_NODE_ELEMENT: " << gumbo_normalized_tagname(child->v.element.tag);
#endif
            // Create the child node, then iterate over its children
            (new XmlElement(child, this))->parse_gumbo_nodes(child);
            break;

        case GUMBO_NODE_WHITESPACE:
#ifdef PRINT_GUMBO
            qDebug() << "GUMBO_NODE_WHITESPACE ignored";
#endif
            new XmlElement(child->v.text.text, this);
            break;

        case GUMBO_NODE_DOCUMENT:
#ifdef PRINT_GUMBO
            qDebug() << "GUMBO_NODE_DOCUMENT ignored";
#endif
            break;

        case GUMBO_NODE_CDATA:
#ifdef PRINT_GUMBO
            qDebug() << "GUMBO_NODE_CDATA ignored";
#endif
            break;

        case GUMBO_NODE_COMMENT:
#ifdef PRINT_GUMBO
            qDebug() << "GUMBO_NODE_COMMENT ignored";
#endif
            break;

        case GUMBO_NODE_TEMPLATE:
#ifdef PRINT_GUMBO
            qDebug() << "GUMBO_NODE_TEMPLATE ignored";
#endif
            break;
        }
    }
}

/**
 * @brief XmlElement::readTree
 * Read an entire RWEXPORT file into a single tree of XmlElement nodes
 * @param device
 * @return
 */
XmlElement *XmlElement::readTree(QIODevice *device)
{
    XmlElement *root_element = nullptr;
    QXmlStreamReader reader;
    reader.setDevice(device);

#ifdef PRINT_LOAD_TIME
    QElapsedTimer timer;
    timer.start();
#endif

    // Move to the start of the first element
    if (reader.readNextStartElement())
    {
        root_element = new XmlElement(&reader, nullptr);
    }
#ifdef PRINT_LOAD_TIME
    qDebug() << "FILE READ took" << timer.elapsed() << "milliseconds";
#endif

    if (reader.hasError())
    {
        qWarning() << "Failed to parse XML in structure file: line" <<
                      reader.lineNumber() << ", column" <<
                      reader.columnNumber() << "error:" <<
                      reader.errorString();
    }
#ifdef DUMP_LOADED_TREE
    else
    {
        root_element->dump_tree();
    }
#endif
    return root_element;
}

void XmlElement::dump_tree() const
{
    QString indentation(dump_indentation, QChar(QChar::Space));

    if (isFixedString())
    {
        // A simple fixed string
        qDebug().noquote().nospace() << indentation << fixedText();
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

        if (child_items.count() == 0 && byteData().isEmpty())
        {
            // No contents, so a terminated start element
            qDebug().noquote().nospace() << indentation << line << "/>";
        }
        else
        {
            // Terminate the opening of the parent element
            qDebug().noquote().nospace() << indentation << line << ">";

            if (!byteData().isEmpty())
            {
                qDebug().noquote().nospace() << QString(dump_indentation+3, QChar(QChar::Space)) << "... " << byteData().size() << " bytes of binary data...";
            }

            dump_indentation += 3;
            for (auto child: child_items)
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
 * @brief XmlElement::childString
 * Finds the single child element which is a fixed string, and returns that value.
 * @return
 */
QString XmlElement::childString() const
{
    // 20,092 + 20,466 + 20,289 ms for 900 MB data
    for (auto child : xmlChildren())
    {
        if (child->isFixedString())
        {
            return child->fixedText();
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


const QString &XmlElement::attribute(const QString &name) const
{
    for (const Attribute &attr : p_attributes)
        if (attr.name == name) return attr.value;
    // Return reference to a null string
    static const QString null_string;
    return null_string;
}


QString XmlElement::snippetName() const
{
    return hasAttribute("facet_name") ? attribute("facet_name") : attribute("label");
}
