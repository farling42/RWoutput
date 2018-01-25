#include "xmlelement.h"

#include <QBitmap>
#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QMetaEnum>
#include <QPainter>

static int dump_indentation = 0;

static int image_max_width = -1;
static bool separate_topic_files = false;
static bool apply_reveal_mask = true;

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
                    // The stream reader requires a SINGLE top-level element
                    body = "<top>" + body + "</body";
                    QXmlStreamReader subreader(body);
                    if (subreader.readNextStartElement())
                    {
                        // Don't create an XmlElement for OUR top-level fake element
                        while (!subreader.atEnd())
                        {
                            if (subreader.readNext() == QXmlStreamReader::StartElement)
                            {
                                new XmlElement(&subreader, this);
                            }
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
    //root_element->dump_tree();
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
static int section_level = 0;


void XmlElement::writeAttributes(QXmlStreamWriter *stream) const
{
    for (auto attr : p_attributes)
    {
        if (attr.name != "class")
        {
            stream->writeAttribute(attr.name, attr.value);
        }
    }
}


void XmlElement::writeSpan(QXmlStreamWriter *stream, const LinkageList &links) const
{
    //qDebug() << "write span";
    // Splan is replaced by "<a href=" element instead
    for (auto link : links)
    {
        // Ignore case during the test
        if (link.name.compare(p_text, Qt::CaseInsensitive) == 0)
        {
            stream->writeStartElement("a");
            if (separate_topic_files)
                stream->writeAttribute("href", link.id + ".html");
            else
                stream->writeAttribute("href", "#" + link.id);
            stream->writeCharacters(p_text);  // case might be different
            stream->writeEndElement();  // a
            return;
        }
    }

    // not a replacement
    bool in_span = hasAttribute("style");
    if (in_span)
    {
        stream->writeStartElement("span");
        writeAttributes(stream);
    }
    stream->writeCharacters(p_text);

    for (auto span: xmlChildren("span"))
    {
        span->writeSpan(stream, links);
    }
    if (in_span) stream->writeEndElement(); // span
}


void XmlElement::writePara(QXmlStreamWriter *stream, const LinkageList &links, const QString &prefix, const QString &value) const
{
    //qDebug() << "write paragraph";
    stream->writeStartElement("p");
    if (objectName() == "p") writeAttributes(stream);
    if (!prefix.isEmpty())
    {
        stream->writeStartElement("span");
        stream->writeAttribute("class", "snippetLabel");
        stream->writeCharacters(prefix + ": ");
        stream->writeEndElement();
    }
    if (!value.isEmpty())
    {
        stream->writeCharacters(value);
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


QString XmlElement::snippetName() const
{
    return hasAttribute("facet_name") ? attribute("facet_name") : attribute("label");
}


void XmlElement::writeParaChildren(QXmlStreamWriter *stream, const LinkageList &links, const QString &prefix, const QString &value) const
{
    //qDebug() << "write paragraph children";
    bool first = true;
    for (XmlElement *para: xmlChildren("p"))
    {
        para->writePara(stream, links, first ? prefix : QString(), first ? value : QString());
        first = false;
    }
}


static bool write_base64_file(const QString &filename, const QString &base64_data)
{
    QFile file(filename);
    if (!file.open(QFile::WriteOnly)) return false;
    file.write (QByteArray::fromBase64(base64_data.toLocal8Bit()));
    return true;
}


/*
 * Return the divisor for the map's size
 */
static int write_img(QXmlStreamWriter *stream, const QString &alt_name,
                     const QString &base64_data, XmlElement *mask_elem,
                     const QString &filename, const QString &caption, const QString &usemap = QString())
{
    QString format = filename.split(".").last();
    int divisor = 1;
    stream->writeStartElement("p");

    stream->writeStartElement("figure");

    stream->writeStartElement("figcaption");
    stream->writeCharacters(caption);
    stream->writeEndElement();  // figcaption

    stream->writeStartElement("img");
    if (!usemap.isEmpty()) stream->writeAttribute("usemap", "#" + usemap);

    if (mask_elem == nullptr && image_max_width <= 0)
    {
        // No image conversion required
        stream->writeAttribute("src", QString("data:image/%1;base64,%2").arg(format).arg(base64_data));
    }
    else
    {
        QImage image = QImage::fromData(QByteArray::fromBase64(base64_data.toLocal8Bit()), qPrintable(format));

        if (mask_elem == nullptr && (image_max_width <= 0 || image.width() < image_max_width))
        {
            stream->writeAttribute("src", QString("data:image/%1;base64,%2").arg(format).arg(base64_data));
        }
        else
        {
            // Apply mask, if supplied
            if (mask_elem && apply_reveal_mask)
            {
                // Ensure we have a 32-bit image to convert
                image = image.convertToFormat(QImage::Format_RGB32);

                // Create a mask with the correct alpha
                QPixmap pixmap(image.size());
                pixmap.fill(QColor(0, 0, 0, 200));
                QImage mask = QImage::fromData(QByteArray::fromBase64(mask_elem->p_text.toLocal8Bit()), qPrintable(format));
                pixmap.setMask(QBitmap::fromImage(mask));

                // Apply the mask to the original picture
                QPainter painter(&image);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                painter.drawPixmap(0, 0, pixmap);
            }

            // Reduce width in a binary fashion, so maximum detail is kept.
            int orig_width = image.size().width();
            int new_width  = orig_width;
            while (new_width > image_max_width)
            {
                divisor = divisor << 1;
                new_width = new_width >> 1;
            }
            if (divisor > 1)
            {
                image = image.scaledToWidth(new_width, Qt::SmoothTransformation);
            }

            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, qPrintable(format));
            buffer.close();

            stream->writeAttribute("src", QString("data:image/%1;base64,%2").arg(format).arg(QString(buffer.data().toBase64())));
        }
    }
    stream->writeAttribute("alt", alt_name);
    stream->writeEndElement();  // img

    stream->writeEndElement();  // figure
    stream->writeEndElement();  // p
    return divisor;
}


static void write_ext_object(QXmlStreamWriter *stream, const QString &prefix,
                             const QString &base64_data, const QString &filename, const QString &suffix)
{
    if (!write_base64_file(filename, base64_data)) return;

    stream->writeStartElement("p");
    stream->writeStartElement("span");
    stream->writeAttribute("class", "snippetLabel");
    stream->writeCharacters(prefix + ": ");
    stream->writeEndElement();  // span

    stream->writeStartElement("span");
    stream->writeStartElement("a");
    stream->writeAttribute("href", filename);
    stream->writeCharacters(filename);
    stream->writeEndElement();  // a

    if (!suffix.isEmpty()) stream->writeCharacters(" " + suffix);
    stream->writeEndElement();  // span
    stream->writeEndElement();  // p
}


void XmlElement::writeSnippet(QXmlStreamWriter *stream, const LinkageList &links) const
{
    QString sn_type = attribute("type");
    //qDebug() << "snippet of type" << sn_type;
    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        for (XmlElement *contents: xmlChildren("contents"))
            contents->writeParaChildren(stream, links);
        for (XmlElement *gm_directions: xmlChildren("gm_directions"))
            gm_directions->writeParaChildren(stream, links, "GM");
    }
    else if (sn_type == "Labeled_Text")
    {
        for (XmlElement *contents: xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            contents->writeParaChildren(stream, links, snippetName());  // has its own 'p'
        }
    }
    else if (sn_type == "Picture" ||
             sn_type == "PDF" ||
             sn_type == "Audio" ||
             sn_type == "Video" ||
             sn_type == "Portfolio" ||
             sn_type == "Statblock" ||
             sn_type == "Foreign" ||
             sn_type == "Rich_Text")
    {
        // ext_object child, asset grand-child
        for (XmlElement *ext_object: xmlChildren("ext_object"))
        {
            for (XmlElement *asset: ext_object->xmlChildren("asset"))
            {
                QString filename = asset->attribute("filename");
                XmlElement *contents = asset->xmlChild("contents");
                XmlElement *annotation = asset->xmlChild("annotation");
                if (contents)
                {
                    QString caption = annotation ? (snippetName() + " : " + annotation->p_text) : snippetName();
                    if (sn_type == "Picture")
                        write_img(stream, ext_object->attribute("name"), contents->p_text, nullptr, filename, caption);
                    else
                        write_ext_object(stream, ext_object->attribute("name"), contents->p_text, filename,
                                         (annotation ? annotation->p_text : QString()));
                }
            }
        }
    }
    else if (sn_type == "Smart_Image")
    {
        // ext_object child, asset grand-child
        for (XmlElement *smart_image: xmlChildren("smart_image"))
        {
            XmlElement *asset = smart_image->xmlChild("asset");
            XmlElement *mask  = smart_image->xmlChild("subset_mask");
            if (asset == nullptr) return;

            QString filename = asset->attribute("filename");
            XmlElement *contents = asset->xmlChild("contents");
            XmlElement *annotation = asset->xmlChild("annotation");
            if (contents == nullptr) return;

            QString caption = snippetName();
            if (annotation) caption.append(" : " + annotation->p_text);

            QList<XmlElement*> pins = smart_image->xmlChildren("map_pin");
            QString usemap;
            if (!pins.isEmpty()) usemap = "map-" + asset->attribute("filename");

            int divisor = write_img(stream, smart_image->attribute("name"), contents->p_text, mask, filename, caption, usemap);

            if (!pins.isEmpty())
            {
                // Create the clickable MAP on top of the map
                stream->writeStartElement("map");
                stream->writeAttribute("name", usemap);
                for (auto pin : pins)
                {
                    XmlElement *description = pin->xmlChild("description");
                    stream->writeStartElement("area");
                    stream->writeAttribute("shape", "circle");
                    stream->writeAttribute("coords", QString("%1,%2,%3")
                                           .arg(pin->attribute("x").toInt() / divisor)
                                           .arg(pin->attribute("y").toInt() / divisor)
                                           .arg(10));
                    stream->writeAttribute("href", pin->attribute("topic_id") + ".html");
                    if (description && !description->p_text.isEmpty())
                    {
                        stream->writeAttribute("alt", description->p_text);
                    }
                    stream->writeEndElement();  // area
                }
                stream->writeEndElement();  // map
            }
        }
    }
    else if (sn_type == "Date_Game")
    {
        XmlElement *date  = xmlChild("game_date");
        XmlElement *annot = xmlChild("annotation");
        if (date == nullptr) return;

        QString value = date->attribute("display");
        if (annot)
            annot->writeParaChildren(stream, links, snippetName(), value);
        else
            writePara(stream, links, snippetName(), value);
    }
    else if (sn_type == "Date_Range")
    {
        XmlElement *date  = xmlChild("date_range");
        XmlElement *annot = xmlChild("annotation");
        if (date == nullptr) return;

        QString value = "from " + date->attribute("display_start") + " to " + date->attribute("display_end");
        if (annot)
            annot->writeParaChildren(stream, links, snippetName(), value);
        else
            writePara(stream, links, snippetName(), value);
    }
    else if (sn_type == "Tag_Standard")
    {
        XmlElement *tag   = xmlChild("tag_assign");
        XmlElement *annot = xmlChild("annotation");
        if (tag == nullptr) return;

        QString value = tag->attribute("tag_name");
        if (annot)
            annot->writeParaChildren(stream, links, snippetName(), value);
        else
            writePara(stream, links, snippetName(), value);
    }
    else if (sn_type == "Numeric")
    {
        XmlElement *contents = xmlChild("contents");
        XmlElement *annot = xmlChild("annotation");
        if (contents == nullptr) return;

        QString value = contents->p_text;
        if (annot)
            annot->writeParaChildren(stream, links, snippetName(), value);
        else
            writePara(stream, links, snippetName(), value);

    }
    else if (sn_type == "Tag_Multi_Domain")
    {
        QList<XmlElement*> tags = xmlChildren("tag_assign");
        XmlElement *annot = xmlChild("annotation");
        if (tags.isEmpty()) return;

        QStringList values;
        for (auto tag : tags)
        {
            values.append(tag->attribute("domain_name") + ":" + tag->attribute("tag_name"));
        }
        QString value = values.join("; ");
        if (annot)
            annot->writeParaChildren(stream, links, snippetName(), value);
        else
            writePara(stream, links, snippetName(), value);
    }
    // Hybrid_Tag
    // Smart_Image
}


void XmlElement::writeSection(QXmlStreamWriter *stream, const LinkageList &links) const
{
    //qDebug() << "write section" << attribute("name");

    int prev_section_level = section_level;

    // Start with HEADER for the section
    ++section_level;
    stream->writeStartElement(QString("H%1").arg(header_level + section_level));
    stream->writeAttribute("class", QString("section%1").arg(section_level));
    stream->writeCharacters(attribute("name"));
    stream->writeEndElement();

    //qDebug() << "section" << attribute("name");

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

    section_level = prev_section_level;
}


static void write_generic_css()
{
    QFile file("theme.css");
    if (!file.open(QFile::WriteOnly | QFile::Text)) return;
    QTextStream css(&file);
    css << "*.summary1 {" << endl;
    css << "    font-size: 25px;" << endl;
    css << "}" << endl;
    css << "*.summary2 {" << endl;
    css << "    font-size: 20px;" << endl;
    css << "}" << endl;
    css << endl;
    css << "*.topic {" << endl;
    css << "    font-family: verdana;" << endl;
    css << "    font-size: 20px;" << endl;
    css << "    text-align: center;" << endl;
    css << "}" << endl;
    css << endl;
    css << "*.section1 {" << endl;
    css << "    background-color: lightblue;" << endl;
    css << "}\n" << endl;
    css << "*.snippetLabel {" << endl;
    css << "    font-weight: bold;" << endl;
    css << "}\n" << endl;
}


static void write_generic_header(QXmlStreamWriter *stream)
{
    stream->writeStartElement("meta");
    stream->writeAttribute("charset", "UTF-8");
    stream->writeEndElement();

    stream->writeStartElement("link");
    stream->writeAttribute("rel", "stylesheet");
    stream->writeAttribute("type", "text/css");
    stream->writeAttribute("href", "theme.css");
    stream->writeEndElement();
}


void XmlElement::writeTopic(QXmlStreamWriter *orig_stream) const
{
    //qDebug() << "write topic" << attribute("topic_id");

    int prev_header_level = header_level;
    QXmlStreamWriter *stream = orig_stream;
    QFile topic_file;

    section_level = 0;

    if (separate_topic_files)
    {
        header_level = 0;
        topic_file.setFileName(attribute("topic_id") + ".html");
        if (!topic_file.open(QFile::WriteOnly))
        {
            qWarning() << "Failed to open output file for topic" << topic_file.fileName();
            return;
        }
        stream = new QXmlStreamWriter(&topic_file);
        stream->setAutoFormatting(true);
        stream->setAutoFormattingIndent(2);

        stream->writeStartElement("html");
        stream->writeStartElement("head");

        // Put <meta charset="UTF-8"> into header.
        // Note that lack of a proper "/>" at the end.
        write_generic_header(stream);

        stream->writeTextElement("title", attribute("public_name"));

        stream->writeEndElement(); // head
    }

    //qDebug() << "topic" << attribute("public_name");

    // Start with HEADER for the topic
    stream->writeStartElement(QString("H%1").arg(++header_level));
    stream->writeAttribute("class", "topic");
    if (!separate_topic_files)
    {
        stream->writeAttribute("id", attribute("topic_id"));
    }
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

    if (separate_topic_files)
    {
        stream->writeEndElement(); // html
        header_level = prev_header_level + 1;

        delete stream;
        topic_file.close();
        stream = orig_stream;

        // Now write an entry into the main stream.
        stream->writeStartElement(QString("ul"));
        stream->writeAttribute("class", QString("summary%1").arg(header_level));

        stream->writeStartElement("li");
        stream->writeStartElement("a");
        stream->writeAttribute("href", topic_file.fileName());
        stream->writeCharacters(attribute("public_name"));
        stream->writeEndElement();   // a
        stream->writeEndElement();   // li
        // UL not finished until after we do the child topics
    }

    // Process <tag_assigns>
    // Process all child topics
    for (XmlElement *topic: xmlChildren("topic"))
    {
        topic->writeTopic(stream);
    }

    if (separate_topic_files)
    {
        stream->writeEndElement();   // ul
    }

    header_level = prev_header_level;
}


void XmlElement::toHtml(QXmlStreamWriter &stream, bool multi_page, int max_image_width, bool use_reveal_mask) const
{
    image_max_width      = max_image_width;
    separate_topic_files = multi_page;
    apply_reveal_mask    = use_reveal_mask;

    write_generic_css();

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

                // Put <meta charset="UTF-8"> into header.
                // Note that lack of a proper "/>" at the end.
                write_generic_header(&stream);

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
