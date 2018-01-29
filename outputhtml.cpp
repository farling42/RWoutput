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

#include "outputhtml.h"

#include <QBitmap>
#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmap>

#include "xmlelement.h"

static int image_max_width = -1;
static bool separate_topic_files = false;
static bool apply_reveal_mask = true;
static QXmlStreamWriter *stream = 0;

struct Linkage {
    QString name;
    QString id;
    Linkage(const QString &name, const QString &id) : name(name), id(id) {}
};
typedef QList<Linkage> LinkageList;


#define DUMP_LEVEL 0


/**
 * @brief OutputHtml::toHtml
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


void writeAttributes(XmlElement *elem)
{
    for (auto attr : elem->p_attributes)
    {
        if (attr.name != "class")
        {
            stream->writeAttribute(attr.name, attr.value);
        }
    }
}


void writeSpan(XmlElement *elem, const LinkageList &links, const QString &classname)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....span";
#endif

    if (elem->isFixedString())
    {
        // TODO - the span containing the link might have style or class information!
        // Check to see if the fixed text should be replaced with a link.
        for (auto link : links)
        {
            // Ignore case during the test
            if (link.name.compare(elem->fixedText(), Qt::CaseInsensitive) == 0)
            {
                stream->writeStartElement("a");
                if (separate_topic_files)
                    stream->writeAttribute("href", link.id + ".xhtml");
                else
                    stream->writeAttribute("href", "#" + link.id);
                stream->writeCharacters(elem->fixedText());  // case might be different
                stream->writeEndElement();  // a
                return;
            }
        }
        stream->writeCharacters(elem->fixedText());
        return;
    }

    // Only put in span if we really require it
    bool in_element = elem->objectName() != "span" || elem->hasAttribute("style") || !classname.isEmpty();
    if (in_element)
    {
        stream->writeStartElement(elem->objectName());
        writeAttributes(elem);
        if (!classname.isEmpty()) stream->writeAttribute("class", classname);
    }

    // All sorts of HTML can appear inside the text
    for (auto child: elem->xmlChildren())
    {
        writeSpan(child, links, QString());
    }
    if (in_element) stream->writeEndElement(); // span
}


void writePara(XmlElement *elem, const QString &classname, const LinkageList &links,
               const QString &label, const QString &bodytext)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....paragraph";
#endif

    if (elem->isFixedString())
    {
        writeSpan(elem, links, classname);
        return;
    }

    if (elem->objectName() == "snippet")
    {
        stream->writeStartElement("p");
    }
    else
    {
        stream->writeStartElement(elem->objectName());
        writeAttributes(elem);
    }
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
        stream->writeCharacters(bodytext);
    }
    for (auto child : elem->xmlChildren())
    {
        if (child->objectName() == "span")
            writeSpan(child, links, class_set ? QString() : classname);
        else if (child->objectName() != "tag_assign")
            // Ignore certain children
            writePara(child, classname, links, /*no prefix*/QString(), QString());
    }
    stream->writeEndElement();  // p
}


void writeParaChildren(XmlElement *parent, const QString &classname, const LinkageList &links,
                       const QString &prefix_label = QString(), const QString &prefix_bodytext = QString())
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children";
#endif

    bool first = true;
    for (auto para: parent->xmlChildren())
    {
        if (first)
            writePara(para, classname, links, /*prefix*/prefix_label, prefix_bodytext);
        else
            writePara(para, classname, links, /*no prefix*/QString(), QString());
        first = false;
    }
}

/*
 * Return the divisor for the map's size
 */

int writeImage(const QString &image_name, const QByteArray &orig_data, XmlElement *mask_elem,
               const QString &filename, XmlElement *annotation, const LinkageList &links,
               const QString &usemap = QString())
{
    QBuffer buffer;
    QString format = filename.split(".").last();
    int divisor = 1;

    stream->writeStartElement("p");

    stream->writeStartElement("figure");

    stream->writeStartElement("figcaption");
    if (annotation)
        writeParaChildren(annotation, "annotation", links, image_name);
    else
        stream->writeCharacters(image_name);
    stream->writeEndElement();  // figcaption

    // See if possible image conversion is required
    if (mask_elem != nullptr || image_max_width > 0)
    {
        QImage image = QImage::fromData(orig_data, qPrintable(format));

        if (mask_elem != nullptr || (image_max_width > 0 && image.width() > image_max_width))
        {
            // Apply mask, if supplied
            if (mask_elem && apply_reveal_mask)
            {
                // If the mask is empty, then don't use it
                // (if the image is JPG, the mask isn't necessarily JPG
                QImage mask = QImage::fromData(mask_elem->p_byte_data);

                // Ensure we have a 32-bit image to convert
                image = image.convertToFormat(QImage::Format_RGB32);

                // Create a mask with the correct alpha
                QPixmap pixmap(image.size());
                pixmap.fill(QColor(0, 0, 0, 200));
                pixmap.setMask(QBitmap::fromImage(mask));

                if (image.size() != mask.size())
                {
                    qWarning() << "Image size differences for" << filename << ": image =" << image.size() << ", mask =" << mask.size();
                }
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

            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, qPrintable(format));
            buffer.close();
        }
    }

    stream->writeStartElement("img");
    if (!usemap.isEmpty()) stream->writeAttribute("usemap", "#" + usemap);
    stream->writeAttribute("src", QString("data:image/%1;base64,%2").arg(format)
                           .arg(QString((buffer.data().isEmpty() ? orig_data : buffer.data()).toBase64())));
    stream->writeAttribute("alt", image_name);
    stream->writeEndElement();  // img

    stream->writeEndElement();  // figure
    stream->writeEndElement();  // p
    return divisor;
}


void writeExtObject(const QString &obj_name, const QByteArray &data,
                    const QString &filename, XmlElement *annotation, const LinkageList &links)
{
    // Write the asset data to an external file
    QFile file(filename);
    if (!file.open(QFile::WriteOnly)) return;
    file.write (data);
    file.close();

    // Put a reference to the external file into the HTML output
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

    if (annotation) writeParaChildren(annotation, "annotation", links);
    stream->writeEndElement();  // p
}


void writeSnippet(XmlElement *snippet, const LinkageList &links)
{
    QString sn_type = snippet->attribute("type");
#if DUMP_LEVEL > 3
    qDebug() << "...snippet" << sn_type;
#endif

    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        for (auto gm_directions: snippet->xmlChildren("gm_directions"))
            writeParaChildren(gm_directions, "gm_directions", links);

        for (auto contents: snippet->xmlChildren("contents"))
            writeParaChildren(contents, "contents", links);
    }
    else if (sn_type == "Labeled_Text")
    {
        for (auto contents: snippet->xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            writeParaChildren(contents, "contents", links, /*prefix*/snippet->snippetName());  // has its own 'p'
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
        XmlElement *annotation = snippet->xmlChild("annotation");
        for (auto ext_object: snippet->xmlChildren("ext_object"))
        {
            for (auto asset: ext_object->xmlChildren("asset"))
            {
                QString filename = asset->attribute("filename");
                XmlElement *contents = asset->xmlChild("contents");
                if (contents)
                {
                    if (sn_type == "Picture")
                        writeImage(ext_object->attribute("name"), contents->p_byte_data,
                                   /*mask*/nullptr, filename, annotation, links);
                    else
                        writeExtObject(ext_object->attribute("name"), contents->p_byte_data,
                                       filename, annotation, links);
                }
            }
        }
    }
    else if (sn_type == "Smart_Image")
    {
        XmlElement *annotation = snippet->xmlChild("annotation");
        // ext_object child, asset grand-child
        for (auto smart_image: snippet->xmlChildren("smart_image"))
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

            int divisor = writeImage(smart_image->attribute("name"), contents->p_byte_data,
                                     mask, filename, annotation, links, usemap);

            if (!pins.isEmpty())
            {
                // Create the clickable MAP on top of the map
                stream->writeStartElement("map");
                stream->writeAttribute("name", usemap);
                for (auto pin : pins)
                {
                    stream->writeStartElement("area");
                    stream->writeAttribute("shape", "circle");
                    stream->writeAttribute("coords", QString("%1,%2,%3")
                                           .arg(pin->attribute("x").toInt() / divisor)
                                           .arg(pin->attribute("y").toInt() / divisor)
                                           .arg(10));
                    QString title = pin->attribute("pin_name");
                    if (!title.isEmpty()) stream->writeAttribute("title", title);
                    QString link = pin->attribute("topic_id");
                    if (!link.isEmpty()) stream->writeAttribute("href", link + ".xhtml");

                    XmlElement *description = pin->xmlChild("description");
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
        XmlElement *date       = snippet->xmlChild("game_date");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = date->attribute("display");
        if (annotation)
            writeParaChildren(annotation, "annotation", links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(snippet, /*no class*/QString(), links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Date_Range")
    {
        XmlElement *date       = snippet->xmlChild("date_range");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = "From: " + date->attribute("display_start") + " To: " + date->attribute("display_end");
        if (annotation)
            writeParaChildren(annotation, "annotation", links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(snippet, /*no class*/QString(), links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Tag_Standard")
    {
        XmlElement *tag        = snippet->xmlChild("tag_assign");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (tag == nullptr) return;

        QString bodytext = tag->attribute("tag_name");
        if (annotation)
            writeParaChildren(annotation, "annotation", links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(snippet, /*no class*/QString(), links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Numeric")
    {
        XmlElement *contents   = snippet->xmlChild("contents");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (contents == nullptr) return;

        const QString &bodytext = contents->childString();
        if (annotation)
            writeParaChildren(annotation, "annotation", links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(snippet, /*no class*/QString(), links, /*prefix*/snippet->snippetName(), bodytext);

    }
    else if (sn_type == "Tag_Multi_Domain")
    {
        QList<XmlElement*> tags = snippet->xmlChildren("tag_assign");
        if (tags.isEmpty()) return;

        QStringList values;
        for (auto tag : tags)
        {
            values.append(tag->attribute("domain_name") + ":" + tag->attribute("tag_name"));
        }
        QString bodytext = values.join("; ");

        XmlElement *annotation = snippet->xmlChild("annotation");
        if (annotation)
            writeParaChildren(annotation, "annotation", links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(snippet, /*no class*/QString(), links, /*prefix*/snippet->snippetName(), bodytext);
    }
    // Hybrid_Tag
}


void writeSection(XmlElement *section, const LinkageList &links)
{
#if DUMP_LEVEL > 2
    qDebug() << "..section" << section->attribute("name");
#endif

    int prev_section_level = section_level;

    // Start with HEADER for the section
    ++section_level;
    stream->writeStartElement(QString("h%1").arg(header_level + section_level));
    stream->writeAttribute("class", QString("section%1").arg(section_level));
    stream->writeCharacters(section->attribute("name"));
    stream->writeEndElement();

    // Write snippets
    for (auto snippet: section->xmlChildren("snippet"))
    {
        writeSnippet(snippet, links);
    }

    // Write following sections
    for (auto subsection: section->xmlChildren("section"))
    {
        writeSection(subsection, links);
    }

    section_level = prev_section_level;
}


static void write_generic_css()
{
    // Use the file that is stored in the resource file
    QFile destfile("theme.css");
    if (destfile.exists()) destfile.remove();
    QFile::copy(":/theme.css", destfile.fileName());
    // Qt copies the file and makes it read-only!
    destfile.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
}


static void start_file()
{
    stream->setAutoFormatting(true);
    stream->setAutoFormattingIndent(2);

    stream->writeStartDocument();
    stream->writeDTD("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">");
    stream->writeStartElement("html");
    stream->writeDefaultNamespace("http://www.w3.org/1999/xhtml");

    stream->writeStartElement("head");

    stream->writeStartElement("meta");
    stream->writeAttribute("http-equiv", "Content-Type");
    stream->writeAttribute("content", "text/html");
    stream->writeAttribute("charset", "utf-8");
    stream->writeEndElement(); // meta

    stream->writeStartElement("link");
    stream->writeAttribute("rel", "stylesheet");
    stream->writeAttribute("type", "text/css");
    stream->writeAttribute("href", "theme.css");
    stream->writeEndElement();  // link

    // Caller needs to do writeEndElement for "head" and "html"
}


void writeTopic(XmlElement *topic)
{
#if DUMP_LEVEL > 1
    qDebug() << ".topic" << topic->objectName() << ":" << topic->attribute("public_name");
#endif

    int prev_header_level = header_level;
    QXmlStreamWriter *orig_stream = 0;
    QFile topic_file;

    section_level = 0;

    if (separate_topic_files)
    {
        header_level = 0;
        topic_file.setFileName(topic->attribute("topic_id") + ".xhtml");
        if (!topic_file.open(QFile::WriteOnly))
        {
            qWarning() << "Failed to open output file for topic" << topic_file.fileName();
            return;
        }

        // Switch output to the new stream.
        orig_stream = stream;
        stream = new QXmlStreamWriter(&topic_file);

        start_file();
        stream->writeTextElement("title", topic->attribute("public_name"));
        stream->writeEndElement(); // head

        stream->writeStartElement("body");
    }

    // Start with HEADER for the topic
    stream->writeStartElement(QString("h%1").arg(++header_level));
    stream->writeAttribute("class", "topic");
    if (!separate_topic_files)
    {
        stream->writeAttribute("id", topic->attribute("topic_id"));
    }
    if (topic->hasAttribute("prefix")) stream->writeAttribute("topic_prefix", topic->attribute("prefix"));
    if (topic->hasAttribute("suffix")) stream->writeAttribute("topic_suffix", topic->attribute("suffix"));
    stream->writeCharacters(topic->attribute("public_name"));
    stream->writeEndElement();

    // Process <linkage> first, to ensure we can remap strings
    LinkageList links;
    for (auto link: topic->xmlChildren("linkage"))
    {
        if (link->attribute("direction") != "Inbound")
        {
            links.append(Linkage(link->attribute("target_name"), link->attribute("target_id")));
        }
    }

    // Process all <sections>, applying the linkage for this topic
    for (auto section: topic->xmlChildren("section"))
    {
        writeSection(section, links);
    }

    auto child_topics = topic->xmlChildren("topic");

    if (separate_topic_files)
    {
        // Complete the individual topic file
        stream->writeEndElement(); // body
        stream->writeEndElement(); // html
        header_level = prev_header_level + 1;

        delete stream;
        topic_file.close();

        // Switch output back to the original stream.
        stream = orig_stream;

        // Now write an entry into the main stream.

        stream->writeStartElement("li");
        stream->writeStartElement("a");
        stream->writeAttribute("href", topic_file.fileName());
        stream->writeCharacters(topic->attribute("public_name"));
        stream->writeEndElement();   // a
        stream->writeEndElement();   // li
        // UL not finished until after we do the child topics

        if (!child_topics.isEmpty())
        {
            // next level for the children
            stream->writeStartElement(QString("ul"));
            stream->writeAttribute("class", QString("summary%1").arg(header_level+1));
        }
    }

    // Process <tag_assigns>
    // Process all child topics
    for (auto child_topic: child_topics)
    {
        writeTopic(child_topic);
    }

    if (separate_topic_files && !child_topics.isEmpty())
    {
        stream->writeEndElement();   // ul
    }

    header_level = prev_header_level;
}


void OutputHtml::toHtml(QXmlStreamWriter *out_stream, XmlElement *root_elem,
                        bool multi_page, int max_image_width, bool use_reveal_mask)
{
    image_max_width      = max_image_width;
    separate_topic_files = multi_page;
    apply_reveal_mask    = use_reveal_mask;
    stream               = out_stream;

    write_generic_css();

    if (root_elem->objectName() == "output")
    {
        start_file();
        for (auto child: root_elem->xmlChildren())
        {
#if DUMP_LEVEL > 0
            qDebug() << "TOP:" << child->objectName();
#endif
            if (child->objectName() == "definition")
            {
                for (auto header: child->xmlChildren())
                {
#if DUMP_LEVEL > 0
                    qDebug() << "DEFINITION:" << header->objectName();
#endif
                    if (header->objectName() == "details")
                    {
                        stream->writeTextElement("title", header->attribute("name"));
                    }
                }
                stream->writeEndElement();  // head
            }
            else if (child->objectName() == "contents")
            {
                stream->writeStartElement("body");

                // Outer wrapper for summary page
                if (separate_topic_files)
                {
                    // next level for the children
                    stream->writeStartElement(QString("ul"));
                    stream->writeAttribute("class", QString("summary1"));
                }

                for (auto topic: child->xmlChildren("topic"))
                {
                    writeTopic(topic);
                }

                // Outer wrapper for summary page
                if (separate_topic_files)
                {
                    stream->writeEndElement();   // ul
                }

                stream->writeEndElement(); // body
            }
        }
        stream->writeEndElement(); // html
    }
}
