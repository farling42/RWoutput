#include "xmlelement.h"

#include <QDebug>
#include <QMetaEnum>

void XmlElement::dump_tree(int indent)
{
    QString indentation(indent, QChar(QChar::Space));

    QString line;

    line = "<" + p_element_title;
    for (QXmlStreamAttribute attr : p_attributes)
    {
        line.append(" " + attr.name() + "=\"" + attr.value() + "\"");
    }
    QList<XmlElement*> child_items = findChildren<XmlElement*>(QString(), Qt::FindDirectChildrenOnly);
    bool is_empty = (p_text.isEmpty() && children().count() == 0);
    line.append(is_empty ? "/>" : ">");
    qDebug().noquote().nospace() << indentation << line;

    if (!p_text.isEmpty())
        qDebug().noquote().nospace() << indentation << p_text;

    foreach (XmlElement *child, child_items)
    {
        child->dump_tree(indent+3);
    }
    if (!is_empty)
        qDebug().noquote().nospace() << indentation << "</" << p_element_title << ">";
}


XmlElement::XmlElement(QObject *parent) :
    QObject(parent)
{

}


XmlElement::XmlElement(QXmlStreamReader *reader, QObject *parent) :
    QObject(parent)
{
    static int indent = 0;
    QString indentation(indent, ' ');

    // Extract common data from this XML element
    p_element_title = reader->name().toString();
    p_attributes    = reader->attributes();
    //p_namespace_uri = reader->namespaceUri().toString();

    p_public_name = p_attributes.value("public_name").toString();
    p_revealed    = p_attributes.value("is_revealed") == "true";
    if (p_element_title == "topic")   p_topic_id       = reader->attributes().value("topic_id").toString();
    if (p_element_title == "snippet") p_snippet_type   = reader->attributes().value("type").toString();
    if (p_element_title == "asset")   p_asset_filename = reader->attributes().value("filename").toString();
    if (p_element_title == "section") p_section_name   = reader->attributes().value("name").toString();
    if (p_element_title == "linkage")
    {
        p_linkage_id = reader->attributes().value("target_id").toString();
        p_linkage_name = reader->attributes().value("target_name").toString();
        p_linkage_direction = reader->attributes().value("direction").toString();
    }

    bool after_children = false;

    // Now read the rest of this element
    while (!reader->atEnd())
    {
        switch (reader->readNext())
        {
        case QXmlStreamReader::StartElement:
            //qDebug().noquote() << indentation << "StartElement:" << reader->name();
            // The start of a child
            //indent += 3;
            new XmlElement(reader, this);
            //indent -= 3;
            after_children = true;
            break;

        case QXmlStreamReader::EndElement:
            //qDebug().noquote() << indentation << "EndElement" << reader->name();
            // The end of this element
            return;

        case QXmlStreamReader::Characters:
            // Add the characters to the end of the text for this element.
            if (!after_children && !reader->isWhitespace())
            {
                //qDebug().noquote() << indentation << "Characters:" << reader->text();
                p_text.append(reader->text().trimmed());
            }
            break;

        case QXmlStreamReader::Comment:
            //qDebug().noquote() << indentation << "Comment:" << reader->text();
            break;
        case QXmlStreamReader::EntityReference:
            //qDebug().noquote() << indentation << "EntityReference:" << reader->name();
            break;
        case QXmlStreamReader::ProcessingInstruction:
            //qDebug().noquote() << indentation << "ProcessingInstruction";
            break;
        case QXmlStreamReader::NoToken:
            //qDebug().noquote() << indentation << "NoToken";
            break;
        case QXmlStreamReader::Invalid:
            //qDebug().noquote() << indentation << "Invalid:" << reader->errorString();
            break;
        case QXmlStreamReader::StartDocument:
            //qDebug().noquote() << indentation << "StartDocument:" << reader->documentVersion();
            p_element_title = "StartDocument";
            break;
        case QXmlStreamReader::EndDocument:
            //qDebug().noquote() << indentation << "EndDocument";
            return;
        case QXmlStreamReader::DTD:
            //qDebug().noquote() << indentation << "DTD:" << reader->text();
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
    //root_element->dump_tree(0);
    return root_element;
}

/**
 * @brief XmlElement::toHtml
 * Output this element and all its children.
 *
 * Children with linkage elements need special handling.
 * The "target_id" of the linkage element should be used as the HTML anchor identifier.
 * The "target_name" attribute of the linkage element is the text which will match one of the spans
 * within the "contents" section of the one of the section->snippet->contents children.
 * An "Outbound" linkage indicates that the "span" within the "contents" needs to be a HTML link.
 *   <a href="#<target_id>">...</a>
 * An "Inbound" linkage indicates that the "span" within the "contents" needs to be a HTML anchor.
 *   <H1 id="<target_id>">...</H1>
 * @return
 */
static int header_level = 0;

QString XmlElement::toHtml() const
{
    //qDebug() << "XmlElement::toHtml" << p_element_title;
    int prev_header_level = header_level;

    QString result;
    if (p_element_title == "topic")
    {
        result.append(QString("<H%1 id=\"%3\">%2</H%1>").arg(++header_level).arg(p_public_name).arg(p_topic_id));
    }
    else if (p_element_title == "section")
    {
        result.append(QString("<H%1>%2</H%1>").arg(++header_level).arg(p_section_name));
    }
    else if (p_element_title == "contents")
    {
        const XmlElement *parent_elem = qobject_cast<const XmlElement*>(parent());
        if (parent_elem == nullptr)
        {
            qDebug() << "XmlElement::toHtml - contents has no parent element";
        }
        else if (parent_elem->p_element_title == "snippet")
        {
            if (parent_elem->p_snippet_type == "Multi_Line")
            {
                // Snippet text already contains the <p class="RWDefault"> class
                //result.append("<p>");
                result.append(p_text);
                //result.append("</p>");
            }
            else
            {
                qDebug() << "XmlElement::toHtml - no support for snippet type" <<
                            parent_elem->p_snippet_type;
            }
        }
        else if (parent_elem->p_element_title == "asset")
        {
            QString format = parent_elem->p_asset_filename.split(".").last();
            result.append(QString("<img src=\"data:image/%1;base64,%2\">").arg(format).arg(p_text));
        }
        else if (parent_elem->p_element_title == "output")
        {
            ; // no problem, since the main contents under <output> is called <contents>
        }
        else
        {
            qDebug() << "XmlElement::toHtml - parent of contents is not snippet, it is" <<
                        parent_elem->p_element_title;
        }
    }

    for (const QObject *obj : children())
    {
        const XmlElement *elem = qobject_cast<const XmlElement*>(obj);
        if (elem)
        {
            // Linkages need to be handled inline since they affect the result collected so far.
            if (elem->p_element_title == "linkage" && elem->p_linkage_direction != "Inbound")
            {
                QString from_pattern = QString(">%1<").arg(elem->p_linkage_name);
                QString to_pattern   = QString("><a href=\"#%1\">%2</a><").arg(elem->p_linkage_id).arg(elem->p_linkage_name);
                result = result.replace(from_pattern, to_pattern);
            }
            else
            {
                result.append(elem->toHtml());
            }
        }
    }

    header_level = prev_header_level;
    return result;
}
