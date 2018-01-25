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


XmlElement::XmlElement(QObject *parent) :
    QObject(parent)
{

}

XmlElement::XmlElement(const QString &fixed_text, QObject *parent) :
    QObject(parent)
{
    setObjectName(FIXED_STRING_NAME);
    p_fixed_text = fixed_text;
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
    bool after_children = false;

    // Now read the rest of this element
    while (!reader->atEnd())
    {
        switch (reader->readNext())
        {
        case QXmlStreamReader::StartElement:
            //qDebug().noquote() << indentation << "StartElement:" << reader->name();
            // The start of a child
            new XmlElement(reader, this);
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
                    body = "<top>" + body + "</top>";
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
                    new XmlElement(reader->text().toString(), this);
            }
            break;

        case QXmlStreamReader::Comment:
            //qDebug().noquote() << indentation << "Comment:" << reader->text();
            break;
        case QXmlStreamReader::EntityReference:
            //qDebug().noquote() << "EntityReference:" << reader->name();
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


void XmlElement::writeSpan(QXmlStreamWriter *stream, const LinkageList &links, const QString &classname) const
{
    //qDebug() << "write span";
    if (objectName() == FIXED_STRING_NAME)
    {
        // Check to see if the fixed text should be replaced with a link.
        for (auto link : links)
        {
            // Ignore case during the test
            if (link.name.compare(p_fixed_text, Qt::CaseInsensitive) == 0)
            {
                stream->writeStartElement("a");
                if (separate_topic_files)
                    stream->writeAttribute("href", link.id + ".html");
                else
                    stream->writeAttribute("href", "#" + link.id);
                stream->writeCharacters(p_fixed_text);  // case might be different
                stream->writeEndElement();  // a
                return;
            }
        }
        stream->writeCharacters(p_fixed_text);
        return;
    }

    // Only put in span if we really require it
    bool in_element = objectName() != "span" || hasAttribute("style") || !classname.isEmpty();
    if (in_element)
    {
        stream->writeStartElement(objectName());
        writeAttributes(stream);
        if (!classname.isEmpty()) stream->writeAttribute("class", classname);
    }

    // All sorts of HTML can appear inside the text
    for (auto child: xmlChildren())
    {
        child->writeSpan(stream, links, QString());
    }
    if (in_element) stream->writeEndElement(); // span
}


void XmlElement::writePara(QXmlStreamWriter *stream, const QString &classname, const LinkageList &links, const QString &label, const QString &bodytext) const
{
    //qDebug() << "write paragraph";
    stream->writeStartElement("p");
    if (objectName() == "p") writeAttributes(stream);
    // If there is no label, then set the class on the paragraph element,
    // otherwise we will put the text inside a span with the given class.
    bool class_set = false;
    if (!label.isEmpty())
    {
        stream->writeStartElement("span");
        stream->writeAttribute("class", "snippet_label");
        stream->writeCharacters(label + ": ");
        stream->writeEndElement();
    }
    else if (!classname.isEmpty())
    {
        stream->writeAttribute("class", classname);
        class_set = true;
    }

    if (!bodytext.isEmpty())
    {
#if 1
        stream->writeCharacters(bodytext);
#else
        bool use_span = !label.isEmpty() && !classname.isEmpty();
        if (use_span)
        {
            stream->writeStartElement("span");
            stream->writeAttribute("class", classname);
        }
        stream->writeCharacters(bodytext);
        if (use_span)
        {
            stream->writeEndElement();
        }
#endif
    }
    for (auto child : xmlChildren("span"))
    {
        child->writeSpan(stream, links, class_set ? QString() : classname);
    }
    stream->writeEndElement();  // p

    for (auto child : xmlChildren("p"))
    {
        child->writePara(stream, classname, links);
    }
}


void XmlElement::writeParaChildren(QXmlStreamWriter *stream, const QString &classname, const LinkageList &links, const QString &first_label, const QString &first_bodytext) const
{
    //qDebug() << "write paragraph children";
    bool first = true;
    for (XmlElement *para: xmlChildren("p"))
    {
        if (first)
            para->writePara(stream, classname, links, first_label, first_bodytext);
        else
            para->writePara(stream, classname, links);
        first = false;
    }
}


QString XmlElement::snippetName() const
{
    return hasAttribute("facet_name") ? attribute("facet_name") : attribute("label");
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
int XmlElement::writeImage(QXmlStreamWriter *stream, const LinkageList &links,
                           const QString &image_name,
                           const QString &base64_data, XmlElement *mask_elem,
                           const QString &filename, XmlElement *annotation, const QString &usemap) const
{
    QString format = filename.split(".").last();
    int divisor = 1;
    stream->writeStartElement("p");

    stream->writeStartElement("figure");

    stream->writeStartElement("figcaption");
    if (annotation)
        annotation->writeParaChildren(stream, "annotation", links, image_name);
    else
        stream->writeCharacters(image_name);
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
                QImage mask = QImage::fromData(QByteArray::fromBase64(mask_elem->childString().toLocal8Bit()), qPrintable(format));
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
    stream->writeAttribute("alt", image_name);
    stream->writeEndElement();  // img

    stream->writeEndElement();  // figure
    stream->writeEndElement();  // p
    return divisor;
}


void XmlElement::writeExtObject(QXmlStreamWriter *stream, const LinkageList &links,
                                const QString &obj_name,
                                const QString &base64_data, const QString &filename, XmlElement *annotation) const
{
    if (!write_base64_file(filename, base64_data)) return;

    stream->writeStartElement("p");

    stream->writeStartElement("span");
    stream->writeAttribute("class", "snippet_label");
    stream->writeCharacters(obj_name + ": ");
    stream->writeEndElement();  // span

    stream->writeStartElement("span");
    stream->writeStartElement("a");
    stream->writeAttribute("href", filename);
    stream->writeCharacters(filename);
    stream->writeEndElement();  // a
    stream->writeEndElement();  // span

    if (annotation) annotation->writeParaChildren(stream, "annotation", links);
    stream->writeEndElement();  // p
}


void XmlElement::writeSnippet(QXmlStreamWriter *stream, const LinkageList &links) const
{
    QString sn_type = attribute("type");
    //qDebug() << "snippet of type" << sn_type;

    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        for (XmlElement *gm_directions: xmlChildren("gm_directions"))
            gm_directions->writeParaChildren(stream, "gm_directions", links);
        for (XmlElement *contents: xmlChildren("contents"))
            contents->writeParaChildren(stream, "contents", links);
    }
    else if (sn_type == "Labeled_Text")
    {
        for (XmlElement *contents: xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            contents->writeParaChildren(stream, "contents", links, snippetName());  // has its own 'p'
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
        XmlElement *annotation = xmlChild("annotation");
        for (XmlElement *ext_object: xmlChildren("ext_object"))
        {
            for (XmlElement *asset: ext_object->xmlChildren("asset"))
            {
                QString filename = asset->attribute("filename");
                XmlElement *contents = asset->xmlChild("contents");
                if (contents)
                {
                    if (sn_type == "Picture")
                        writeImage(stream, links, ext_object->attribute("name"), contents->childString(), nullptr, filename, annotation);
                    else
                        writeExtObject(stream, links, ext_object->attribute("name"), contents->childString(), filename, annotation);
                }
            }
        }
    }
    else if (sn_type == "Smart_Image")
    {
        XmlElement *annotation = xmlChild("annotation");
        // ext_object child, asset grand-child
        for (XmlElement *smart_image: xmlChildren("smart_image"))
        {
            XmlElement *asset = smart_image->xmlChild("asset");
            XmlElement *mask  = smart_image->xmlChild("subset_mask");
            if (asset == nullptr) return;

            QString filename = asset->attribute("filename");
            XmlElement *contents = asset->xmlChild("contents");
            if (contents == nullptr) return;

            QList<XmlElement*> pins = smart_image->xmlChildren("map_pin");
            QString usemap;
            if (!pins.isEmpty()) usemap = "map-" + asset->attribute("filename");

            int divisor = writeImage(stream, links, smart_image->attribute("name"), contents->childString(), mask, filename, annotation, usemap);

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
                    QString title = pin->attribute("pin_name");
                    if (!title.isEmpty()) stream->writeAttribute("title", title);
                    QString link = pin->attribute("topic_id");
                    if (!link.isEmpty()) stream->writeAttribute("href", link + ".html");
                    if (description && !description->childString().isEmpty())
                    {
                        stream->writeAttribute("alt", description->childString());
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

        QString bodytext = date->attribute("display");
        if (annot)
            annot->writeParaChildren(stream, "annotation", links, snippetName(), bodytext);
        else
            writePara(stream, QString(), links, snippetName(), bodytext);
    }
    else if (sn_type == "Date_Range")
    {
        XmlElement *date  = xmlChild("date_range");
        XmlElement *annot = xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = "From: " + date->attribute("display_start") + " To: " + date->attribute("display_end");
        if (annot)
            annot->writeParaChildren(stream, "annotation", links, snippetName(), bodytext);
        else
            writePara(stream, QString(), links, snippetName(), bodytext);
    }
    else if (sn_type == "Tag_Standard")
    {
        XmlElement *tag   = xmlChild("tag_assign");
        XmlElement *annot = xmlChild("annotation");
        if (tag == nullptr) return;

        QString bodytext = tag->attribute("tag_name");
        if (annot)
            annot->writeParaChildren(stream, "annotation", links, snippetName(), bodytext);
        else
            writePara(stream, QString(), links, snippetName(), bodytext);
    }
    else if (sn_type == "Numeric")
    {
        XmlElement *contents = xmlChild("contents");
        XmlElement *annot = xmlChild("annotation");
        if (contents == nullptr) return;

        QString bodytext = contents->childString();
        if (annot)
            annot->writeParaChildren(stream, "annotation", links, snippetName(), bodytext);
        else
            writePara(stream, QString(), links, snippetName(), bodytext);

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
        QString bodytext = values.join("; ");
        if (annot)
            annot->writeParaChildren(stream, "annotation", links, snippetName(), bodytext);
        else
            writePara(stream, QString(), links, snippetName(), bodytext);
    }
    // Hybrid_Tag
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
    // Use the file that is stored in the resource file
    QFile destfile("theme.css");
    if (destfile.exists()) destfile.remove();
    QFile::copy(":/theme.css", destfile.fileName());
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
    if (hasAttribute("prefix")) stream->writeAttribute("topic_prefix", attribute("prefix"));
    if (hasAttribute("suffix")) stream->writeAttribute("topic_suffix", attribute("suffix"));
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
