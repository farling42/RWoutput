#include "xmlelement.h"

#include <QDebug>
#include <QMetaEnum>

static int dump_indentation = 0;

void XmlElement::dump_tree() const
{
    QString indentation(dump_indentation, QChar(QChar::Space));

    QString line;

    line = "<" + objectName();
    for (auto iter : p_attributes)
    {
        line.append(" " + iter.name + "=\"" + iter.value + "\"");
    }
    QList<XmlElement*> child_items = findChildren<XmlElement*>(QString(), Qt::FindDirectChildrenOnly);
    bool is_empty = (p_text.isEmpty() && children().count() == 0);
    line.append(is_empty ? "/>" : ">");
    qDebug().noquote().nospace() << indentation << line;

    if (!p_text.isEmpty())
        qDebug().noquote().nospace() << indentation << p_text;

    dump_indentation += 3;
    foreach (XmlElement *child, child_items)
    {
        child->dump_tree();
    }
    dump_indentation -= 3;
    if (!is_empty)
        qDebug().noquote().nospace() << indentation << "</" << objectName() << ">";
}


XmlElement::XmlElement(QObject *parent) :
    QObject(parent)
{

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
                QString body = reader->text().toString();
                if (body.startsWith("<p class"))
                {
                    // Convert what was previously translated XML into new XmlElement objects
                    QXmlStreamReader subreader(body);
                    if (subreader.readNextStartElement())
                    {
                        while (!subreader.atEnd())
                        {
                            new XmlElement(&subreader, this);
                        }
                    }
                }
                else
                    p_text.append(reader->text());
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
            setObjectName("StartDocument");
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
    root_element->dump_tree();
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


void XmlElement::writeAttributes(QXmlStreamWriter *stream) const
{
    for (auto attr : p_attributes)
    {
        stream->writeAttribute(attr.name, attr.value);
    }
}


void XmlElement::writeSpan(QXmlStreamWriter *stream, const LinkageList &links) const
{
    // Splan is replaced by "<a href=" element instead
    for (auto link : links)
    {
        // Ignore case during the test
        if (link.name.compare(p_text, Qt::CaseInsensitive) == 0)
        {
            stream->writeStartElement("a");
            stream->writeAttribute("href", "#" + link.id);
            stream->writeCharacters(p_text);  // case might be different
            stream->writeEndElement();  // a
            return;
        }
    }

    // not a replacement
    stream->writeStartElement("span");
    writeAttributes(stream);
    stream->writeCharacters(p_text);

    for (auto span: xmlChildren("span"))
    {
        span->writeSpan(stream, links);
    }
    stream->writeEndElement(); // span
}


void XmlElement::writePara(QXmlStreamWriter *stream, const LinkageList &links, const QString &prefix) const
{
    stream->writeStartElement("p");
    writeAttributes(stream);
    if (!prefix.isEmpty())
    {
        stream->writeStartElement("span");
        stream->writeAttribute("class", "RWSnippet");
        stream->writeAttribute("style", "font-style:bold");
        stream->writeCharacters(prefix + ": ");
        stream->writeEndElement();
    }
    for (auto child : xmlChildren())
    {
        if (child->objectName() == "span")
            child->writeSpan(stream, links);
        else if (child->objectName() == "p")
            child->writePara(stream, links);
    }
    stream->writeEndElement();  // p
}


void XmlElement::writeParaChildren(QXmlStreamWriter *stream, const LinkageList &links, const QString &prefix) const
{
    bool first = true;
    for (XmlElement *para: xmlChildren("p"))
    {
        para->writePara(stream, links, first ? prefix : QString());
        first = false;
    }
}


void XmlElement::writeSnippet(QXmlStreamWriter *stream, const LinkageList &links) const
{
    QString sn_type = attribute("type");
    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        for (XmlElement *contents: xmlChildren("contents"))
            contents->writeParaChildren(stream, links);
        for (XmlElement *gm_directions: xmlChildren("gm_directions"))
            gm_directions->writeParaChildren(stream, links);
    }
    else if (sn_type == "Labeled_Text")
    {
        for (XmlElement *contents: xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            contents->writeParaChildren(stream, links, attribute("facet_name"));  // has its own 'p'
        }
    }
    else if (sn_type == "Picture" ||
             sn_type == "PDF" ||
             sn_type == "Audio" ||
             sn_type == "Video")
    {
        // ext_object child, asset grand-child
        for (XmlElement *ext_object: xmlChildren("ext_object"))
        {
            for (XmlElement *asset: ext_object->xmlChildren("asset"))
            {
                for (XmlElement *contents: asset->xmlChildren("contents"))
                {
                    QString format = asset->attribute("filename").split(".").last();
                    stream->writeStartElement("img");
                    stream->writeAttribute("src", QString("data:image/%1;base64,%2").arg(format).arg(contents->p_text));
                    stream->writeAttribute("alt", ext_object->attribute("name"));
                    stream->writeEndElement();
                }
            }
        }
    }
    else if (sn_type == "Smart_Image")
    {
        // ext_object child, asset grand-child
        for (XmlElement *smart_image: xmlChildren("smart_image"))
        {
            for (XmlElement *asset: smart_image->xmlChildren("asset"))
            {
                for (XmlElement *contents: asset->xmlChildren("contents"))
                {
                    QString format = asset->attribute("filename").split(".").last();
                    stream->writeStartElement("img");
                    stream->writeAttribute("src", QString("data:image/%1;base64,%2").arg(format).arg(contents->p_text));
                    stream->writeAttribute("alt", smart_image->attribute("name"));
                    stream->writeEndElement();
                }
            }
            // maybe loads of "map_pin"
            //   attributes pin_name  topic_id  x   y
            // optional child "description"
        }
    }
    // Multi_Line - handled when contained <contents> is processed.
    // Labeled_Text
    // Numeric
    // Date_Game
    // Date_Range
    // Tag_Standard
    // Tag_Multi_Domain
    // Hybrid_Tag
    // Foreign
    // Statblock
    // Portfolio
    // Picture - handled by asset
    // Smart_Image
    // Rich_Text
    // PDF   - handled by asset
    // Audio - handled by asset
    // Video - handled by asset
    // HTML  - handled by asset
}


void XmlElement::writeSection(QXmlStreamWriter *stream, const LinkageList &links) const
{
    int prev_header_level = header_level;

    // Start with HEADER for the section
    stream->writeTextElement(QString("H%1").arg(++header_level), attribute("name"));

    // Write snippets
    for (XmlElement *snippet: xmlChildren("snippet"))
    {
        snippet->writeSnippet(stream, links);
    }

    // Write following sections
    for (XmlElement *section: xmlChildren("section"))
    {
        section->writeSection(stream, links);
    }

    header_level = prev_header_level;
}


void XmlElement::writeTopic(QXmlStreamWriter *stream) const
{
    int prev_header_level = header_level;

    // Start with HEADER for the topic
    stream->writeStartElement(QString("H%1").arg(++header_level));
    stream->writeAttribute("id", attribute("topic_id"));
    stream->writeCharacters(attribute("public_name"));
    stream->writeEndElement();

    // Process <linkage> first, to ensure we can remap strings
    LinkageList links;
    for (XmlElement *link: xmlChildren("linkage"))
    {
        if (link->attribute("direction") != "Inbound")
        {
            links.append(Linkage(link->attribute("target_name"), link->attribute("target_id")));
        }
    }

    // Process all <sections>, applying the linkage for this topic
    for (XmlElement *section: xmlChildren("section"))
    {
        section->writeSection(stream, links);
    }
    // Process <tag_assigns>
    // Process all child topics
    for (XmlElement *topic: xmlChildren("topic"))
    {
        topic->writeTopic(stream);
    }

    header_level = prev_header_level;
}


QString XmlElement::toHtml() const
{
    QString result;
    QXmlStreamWriter stream(&result);
    stream.setAutoFormatting(true);
    stream.setAutoFormattingIndent(2);
    if (this->objectName() == "output")
    {
        stream.writeStartElement("html");
        for (XmlElement *child: xmlChildren())
        {
            if (child->objectName() == "definition")
            {
                stream.writeStartElement("head");
                for (XmlElement *header: child->xmlChildren())
                {
                    if (header->objectName() == "details")
                    {
                        stream.writeTextElement("title", header->attribute("name"));
                    }
                }
                stream.writeEndElement();  // head
            }
            else if (child->objectName() == "contents")
            {
                stream.writeStartElement("body");
                for (XmlElement *topic: child->xmlChildren("topic"))
                {
                    topic->writeTopic(&stream);
                }
                stream.writeEndElement(); // body
            }
        }
        stream.writeEndElement(); // html
    }
    return result;
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
