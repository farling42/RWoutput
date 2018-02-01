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
static bool apply_reveal_mask = true;
static QXmlStreamWriter *stream = 0;
static bool always_show_index = false;
static bool in_single_file = false;

struct Linkage {
    QString name;
    QString id;
    Linkage(const QString &name, const QString &id) : name(name), id(id) {}
};
typedef QList<Linkage> LinkageList;


#define DUMP_LEVEL 0

static bool sort_by_public_name(const XmlElement *left, const XmlElement *right)
{
    return left->attribute("public_name") < right->attribute("public_name");
}


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

static void write_support_files()
{
    // Use the files that are stored in the resource file
    QStringList files;
    files << "theme.css" << "scripts.js";

    for (auto filename : files)
    {
        QFile destfile(filename);
        if (destfile.exists()) destfile.remove();
        QFile::copy(":/" + filename, destfile.fileName());
        // Qt copies the file and makes it read-only!
        destfile.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    }
}


static void write_meta_child(const QString &meta_name, XmlElement *details, const QString &child_name)
{
    XmlElement *child = details->xmlChild(child_name);
    if (child)
    {
        stream->writeStartElement("meta");
        stream->writeAttribute("name", meta_name);
        stream->writeAttribute("content", child->childString());
        stream->writeEndElement();
    }

}


static void write_head_meta(XmlElement *root_elem)
{
    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    if (details == nullptr) return;

    stream->writeStartElement("meta");
    stream->writeAttribute("name", "exportDate");
    stream->writeAttribute("content", root_elem->attribute("export_date"));
    stream->writeEndElement();

    stream->writeStartElement("meta");
    stream->writeAttribute("name", "author");
    stream->writeAttribute("content", "RWoutput Tool");
    stream->writeEndElement();

    write_meta_child("summary",      details, "summary");
    write_meta_child("description",  details, "description");
    write_meta_child("requirements", details, "requirements");
    write_meta_child("credits",      details, "credits");
    write_meta_child("legal",        details, "legal");
    write_meta_child("other_notes",  details, "other_notes");
}


static void start_file()
{
    stream->setAutoFormatting(true);
    stream->setAutoFormattingIndent(2);

    stream->writeStartDocument();
    stream->writeDTD("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">");
    stream->writeStartElement("html");
    stream->writeDefaultNamespace("http://www.w3.org/1999/xhtml");

    // The script element MUST have a separate terminating token
    // for DOMParser::parseFromString() to work properly.
    stream->writeStartElement("script");
    stream->writeAttribute("src", "scripts.js");
    stream->writeCharacters(" ");  // to force generation of terminator
    stream->writeEndElement();

    stream->writeStartElement("head");

    stream->writeStartElement("meta");
    stream->writeAttribute("http-equiv", "Content-Type");
    stream->writeAttribute("content", "text/html");
    stream->writeAttribute("charset", "utf-8");
    stream->writeEndElement(); // meta

    if (in_single_file)
    {
        // Put the style sheet in the same file
        QFile theme(":/theme.css");
        if (theme.open(QFile::ReadOnly|QFile::Text))
        {
            stream->writeStartElement("style");
            stream->writeCharacters(theme.readAll());
            stream->writeEndElement();
        }
    }
    else
    {
        // External style sheet
        stream->writeStartElement("link");
        stream->writeAttribute("rel", "stylesheet");
        stream->writeAttribute("type", "text/css");
        stream->writeAttribute("href", "theme.css");
        stream->writeEndElement();  // link
    }

    // Caller needs to do writeEndElement for "head" and "html"
}


static void writeAttributes(XmlElement *elem)
{
    for (auto attr : elem->p_attributes)
    {
        if (attr.name != "class")
        {
            stream->writeAttribute(attr.name, attr.value);
        }
    }
}


static void writeTopicHref(const QString &topic)
{
    if (in_single_file)
        stream->writeAttribute("href", "#" + topic);
    else
        stream->writeAttribute("href", topic + ".xhtml");
}


static void writeSpan(XmlElement *elem, const LinkageList &links, const QString &classname)
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
                writeTopicHref(link.id);
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


static void writePara(XmlElement *elem, const QString &classname, const LinkageList &links,
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


static void writeParaChildren(XmlElement *parent, const QString &classname, const LinkageList &links,
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

static int writeImage(const QString &image_name, const QByteArray &orig_data, XmlElement *mask_elem,
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


static void writeExtObject(const QString &obj_name, const QByteArray &data,
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


static void writeSnippet(XmlElement *snippet, const LinkageList &links)
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
                    if (!link.isEmpty()) writeTopicHref(link);

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


static int section_level = 0;

static void writeSection(XmlElement *section, const LinkageList &links)
{
#if DUMP_LEVEL > 2
    qDebug() << "..section" << section->attribute("name");
#endif

    // Start with HEADER for the section
    ++section_level;
    stream->writeStartElement(QString("h%1").arg(section_level+1));
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

    --section_level;
}


static void writeTopicBody(XmlElement *topic)
{
#if DUMP_LEVEL > 1
    qDebug() << ".topic" << topic->objectName() << ":" << topic->attribute("public_name");
#endif
    if (in_single_file) stream->writeStartElement("section");

    // Start with HEADER for the topic
    stream->writeStartElement("header");
    stream->writeStartElement(QString("h1"));
    stream->writeAttribute("class", "topic");
    stream->writeAttribute("id", topic->attribute("topic_id"));
    if (topic->hasAttribute("prefix")) stream->writeAttribute("topic_prefix", topic->attribute("prefix"));
    if (topic->hasAttribute("suffix")) stream->writeAttribute("topic_suffix", topic->attribute("suffix"));
    stream->writeCharacters(topic->attribute("public_name"));
    stream->writeEndElement();  // h1
    // Maybe some aliases
    for (auto alias : topic->xmlChildren("alias"))
    {
        stream->writeStartElement("p");
        // The RWoutput file puts the "true name" as the public_name of the topic.
        // All other names are listed as aliases (with no attributes).
        // If the RW topic has a "true name" defined then the actual name of the topic
        // is reported as an alias.
        stream->writeAttribute("class", "nameAlias");
        stream->writeCharacters(alias->attribute("name"));
        stream->writeEndElement(); // p
    }
    stream->writeEndElement();  // header

    if (always_show_index)
    {
        stream->writeStartElement("nav");
        stream->writeAttribute("include-html", "index.xhtml");
        stream->writeEndElement();  // nav
    }

    stream->writeStartElement("section"); // for all the RW sections

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
    section_level = 0;
    for (auto section: topic->xmlChildren("section"))
    {
        writeSection(section, links);
    }

    // Provide summary of links to child topics
    auto child_topics = topic->xmlChildren("topic");
    std::sort(child_topics.begin(), child_topics.end(), sort_by_public_name);

    if (!child_topics.isEmpty())
    {
        stream->writeStartElement("footer");
        stream->writeStartElement("h2");
        stream->writeAttribute("class", "childTopicsHeader");
        stream->writeCharacters("Child Topics");
        stream->writeEndElement();
        stream->writeStartElement("ul");
        for (auto child: child_topics)
        {
            stream->writeStartElement("li");
            stream->writeAttribute("class", "childTopicsEntry");
            stream->writeStartElement("a");
            writeTopicHref(child->attribute("topic_id"));
            stream->writeCharacters(child->attribute("public_name"));
            stream->writeEndElement();   // a
            stream->writeEndElement(); // li
        }
        stream->writeEndElement(); // ul
        stream->writeEndElement(); // footer
    }

    if (in_single_file)
    {
        for (auto child_topic: child_topics)
        {
            writeTopicBody(child_topic);
        }
    }

    stream->writeEndElement();  // section (for RW sections)

    if (in_single_file) stream->writeEndElement(); // outermost <section>
}


static void writeTopicFile(XmlElement *topic)
{
#if DUMP_LEVEL > 1
    qDebug() << ".topic" << topic->objectName() << ":" << topic->attribute("public_name");
#endif

    // Create a new file for this topic
    QFile topic_file(topic->attribute("topic_id") + ".xhtml");
    if (!topic_file.open(QFile::WriteOnly|QFile::Text))
    {
        qWarning() << "Failed to open output file for topic" << topic_file.fileName();
        return;
    }

    // Switch output to the new stream.
    QXmlStreamWriter topic_stream(&topic_file);
    stream = &topic_stream;

    start_file();
    stream->writeTextElement("title", topic->attribute("public_name"));
    stream->writeEndElement(); // head

    stream->writeStartElement("body");

    writeTopicBody(topic);

    // Include the INDEX file
    if (always_show_index)
    {
        stream->writeStartElement("script");
        stream->writeAttribute("type", "text/javascript");
        stream->writeCharacters("includeHTML();");
        stream->writeEndElement();
    }

    // Complete the individual topic file
    stream->writeEndElement(); // body
    stream->writeEndElement(); // html

    stream = 0;
}

/*
 * Write entries into the INDEX file
 */

static void writeTopicToIndex(XmlElement *topic)
{
    static int index_header_level = 0;
    auto child_topics = topic->xmlChildren("topic");
    bool has_kids = !child_topics.isEmpty();

    stream->writeStartElement("li");

    if (has_kids)
    {
        stream->writeStartElement("details");
        stream->writeAttribute("open", "true");
        stream->writeStartElement("summary");
    }
    stream->writeStartElement("a");
    writeTopicHref(topic->attribute("topic_id"));
    stream->writeCharacters(topic->attribute("public_name"));
    stream->writeEndElement();   // a

    if (has_kids)
    {
        stream->writeEndElement();  // summary

        ++index_header_level;

        // next level for the children
        stream->writeStartElement(QString("ul"));
        stream->writeAttribute("class", QString("summary%1").arg(index_header_level));

        std::sort(child_topics.begin(), child_topics.end(), sort_by_public_name);

        for (auto child_topic: child_topics)
        {
            writeTopicToIndex(child_topic);
        }

        stream->writeEndElement();   // ul
        --index_header_level;
        stream->writeEndElement();  // details
    }

    stream->writeEndElement();   // li
}


static void writeIndex(XmlElement *root_elem)
{
    QFile out_file("index.xhtml");
    if (!out_file.open(QFile::WriteOnly|QFile::Text))
    {
        qWarning() << "Failed to find file" << out_file.fileName();
        return;
    }
    QXmlStreamWriter out_stream(&out_file);
    stream = &out_stream;

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
                write_head_meta(root_elem);
                stream->writeEndElement();  // head
            }
            else if (child->objectName() == "contents")
            {
                stream->writeStartElement("body");

                // A top-level button to collapse/expand the entire list
                const QString expand_all   = "Expand All";
                const QString collapse_all = "Collapse All";

                stream->writeStartElement("button");
                stream->writeAttribute("onclick", "toggleNavVis(this)");
                stream->writeAttribute("checked", "false");
                stream->writeCharacters(expand_all);
                stream->writeEndElement();

                stream->writeStartElement("script");
                stream->writeAttribute("type", "text/javascript");
                // This script isn't found when index.html is loaded into the NAV panel
                // of children; so this script is duplicated inside scripts.js
                stream->writeCharacters("function toggleNavVis(item) {\n"
                                        "  var i, list, checked;\n"
                                        "  checked = item.getAttribute('checked') !== 'true';\n"
                                        "  list = document.getElementsByTagName('details');\n"
                                        "  for (i=0; i < list.length; i++) {\n"
                                        "     list[i].open = checked;\n"
                                        "  }\n"
                                        "  if (checked)\n"
                                        "    item.textContent = '" + collapse_all + "'\n"
                                        "  else\n"
                                        "    item.textContent = '" + expand_all + "'\n"
                                        "  item.setAttribute('checked', checked);\n"
                                        "}");
                stream->writeEndElement();
#if 1
                // Root level of topics
                auto main_topics = child->xmlChildren("topic");
                QMultiMap<QString,XmlElement*> categories;
                for (auto topic: main_topics)
                {
                    categories.insert(topic->attribute("category_name"), topic);
                }

                QStringList unique_keys(categories.keys());
                unique_keys.removeDuplicates();
                unique_keys.sort();

                for (auto cat : unique_keys)
                {
                    stream->writeStartElement("details");
                    // If we are displaying the index on each page,
                    // then it is better to have categories not expanded.
                    if (!always_show_index)
                    {
                        stream->writeAttribute("open", "true");
                    }
                    stream->writeAttribute("class", "indexCategory");

                    stream->writeStartElement("summary");
                    stream->writeCharacters(cat);
                    stream->writeEndElement();  // summary

                    stream->writeStartElement(QString("ul"));
                    stream->writeAttribute("class", QString("summary1"));

                    // Organise topics alphabetically
                    auto topics = categories.values(cat);
                    std::sort(topics.begin(), topics.end(), sort_by_public_name);

                    for (auto topic: topics)
                    {
                        writeTopicToIndex(topic);
                    }
                    stream->writeEndElement();   // ul
                    stream->writeEndElement();  // details
                }
#else
                // Outer wrapper for summary page

                for (auto topic: main_topics)
                {
                    writeTopicToIndex(topic);
                }
                // Outer wrapper for summary page
#endif

                stream->writeEndElement(); // body
            }
        }
        stream->writeEndElement(); // html
    }

    stream = 0;
}


void toHtml(const QString &path,
            XmlElement *root_elem,
            int max_image_width,
            bool separate_files,
            bool use_reveal_mask,
            bool index_on_every_page)
{
    image_max_width   = max_image_width;
    apply_reveal_mask = use_reveal_mask;
    always_show_index = index_on_every_page;
    in_single_file    = !separate_files;

    // Write out the individual TOPIC files now:
    // Note use of findChildren to find children at all levels,
    // whereas xmlChildren returns only direct children.
    if (separate_files)
    {
        write_support_files();
        writeIndex(root_elem);

        // A separate file for every single topic
        for (auto topic : root_elem->findChildren<XmlElement*>("topic"))
        {
            writeTopicFile(topic);
        }
    }
    else
    {
        //XmlElement *output     = root_elem;
        XmlElement *definition = root_elem->xmlChild("definition");
        XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
        XmlElement *contents   = root_elem->xmlChild("contents");

        if (definition == nullptr ||
                contents == nullptr || details == nullptr)
        {
            qWarning() << "Invalid structure inside RWoutput file";
            qDebug() << "root_elem  =" << root_elem->objectName();
            qDebug() << "definition =" << definition;
            qDebug() << "contents   =" << contents;
            qDebug() << "details    =" << details;
            return;
        }

        // Write header for single file
        // Create a new file for this topic
        QFile single_file(path);
        if (!single_file.open(QFile::WriteOnly|QFile::Text))
        {
            qWarning() << "Failed to open chosen output file" << single_file.fileName();
            return;
        }

        // Switch output to the new stream.
        QXmlStreamWriter topic_stream(&single_file);
        stream = &topic_stream;

        start_file();
        stream->writeTextElement("title", details->attribute("name"));
        write_head_meta(root_elem);
        stream->writeEndElement(); // head

        stream->writeStartElement("body");

        // All topics in a single file, nesting child topics
        // inside the parent topic.
        for (auto topic : contents->xmlChildren("topic"))
        {
            writeTopicBody(topic);
        }

        // Write footer for single file
        // Complete the individual topic file
        stream->writeEndElement(); // body
        stream->writeEndElement(); // html
    }
}
