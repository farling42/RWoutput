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
#include <QApplication>
//#include <QElapsedTimer>
#include <future>

#include "gumbo.h"
#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

#include "xmlelement.h"

static int image_max_width = -1;
static bool apply_reveal_mask = true;
static bool always_show_index = false;
static bool in_single_file = false;

struct Linkage {
    QString name;
    QString id;
    Linkage(const QString &name, const QString &id) : name(name), id(id) {}
};
typedef QList<Linkage> LinkageList;

static const QStringList predefined_styles = { "Normal", "Read_Aloud", "Handout", "Flavor", "Callout" };
static QMap<QString /*style string*/ ,QString /*replacement class name*/> class_of_style;

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

    QFile styles("localStyles.css");
    if (styles.open(QFile::WriteOnly|QFile::Text))
    {
        QTextStream ts(&styles);
        for (auto iter = class_of_style.begin(); iter != class_of_style.end(); iter++)
        {
            if (!predefined_styles.contains(iter.value()))
            {
                ts << "." + iter.value() + " {\n" + iter.key() + "\n}\n";
            }
        }
    }
}


static void write_local_styles(QXmlStreamWriter *stream)
{
    //stream->writeStartElement("style");
    for (auto iter = class_of_style.begin(); iter != class_of_style.end(); iter++)
    {
        if (!predefined_styles.contains(iter.value()))
        {
            stream->writeCharacters("." + iter.value() + " {\n" + iter.key() + "\n}\n");
        }
    }
    //stream->writeEndElement();
}

static void write_meta_child(QXmlStreamWriter *stream, const QString &meta_name, XmlElement *details, const QString &child_name)
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


static void write_head_meta(QXmlStreamWriter *stream, XmlElement *root_elem)
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

    write_meta_child(stream, "summary",      details, "summary");
    write_meta_child(stream, "description",  details, "description");
    write_meta_child(stream, "requirements", details, "requirements");
    write_meta_child(stream, "credits",      details, "credits");
    write_meta_child(stream, "legal",        details, "legal");
    write_meta_child(stream, "other_notes",  details, "other_notes");
}


static void start_file(QXmlStreamWriter *stream)
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

    stream->writeStartElement("meta");
    stream->writeAttribute("name", "generator");
    stream->writeAttribute("content", qApp->applicationName() + " " + qApp->applicationVersion());
    stream->writeEndElement();

    if (in_single_file)
    {
        // Put the style sheet in the same file
        QFile theme(":/theme.css");
        stream->writeStartElement("style");
        if (theme.open(QFile::ReadOnly|QFile::Text))
        {
            stream->writeCharacters(theme.readAll());
        }
        write_local_styles(stream);
        stream->writeEndElement();
    }
    else
    {
        // External style sheet
        stream->writeStartElement("link");
        stream->writeAttribute("rel", "stylesheet");
        stream->writeAttribute("type", "text/css");
        stream->writeAttribute("href", "theme.css");
        stream->writeEndElement();  // link

        // Locally generated styles
        stream->writeStartElement("link");
        stream->writeAttribute("rel", "stylesheet");
        stream->writeAttribute("type", "text/css");
        stream->writeAttribute("href", "localStyles.css");
        stream->writeEndElement();  // link
    }

    // Caller needs to do writeEndElement for "head" and "html"
}


static void writeAttributes(QXmlStreamWriter *stream, XmlElement *elem, const QString &classname)
{
    QStringList class_names;
    if (!classname.isEmpty()) class_names.append(classname);
    for (auto attr : elem->p_attributes)
    {
        if (attr.name == "style")
        {
            class_names.append(class_of_style.value(attr.value));
        }
        else if (attr.name != "class")  // ignore RWdefault, RWSnippet, RWLink
        {
            stream->writeAttribute(attr.name, attr.value);
        }
    }
    if (!class_names.isEmpty())
    {
        stream->writeAttribute("class", class_names.join(" "));
    }
}


static void writeTopicHref(QXmlStreamWriter *stream, const QString &topic)
{
    if (in_single_file)
        stream->writeAttribute("href", "#" + topic);
    else
        stream->writeAttribute("href", topic + ".xhtml");
}


static void writeSpan(QXmlStreamWriter *stream, XmlElement *elem, const LinkageList &links, const QString &classname)
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
                writeTopicHref(stream, link.id);
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
        writeAttributes(stream, elem, classname);
    }

    // All sorts of HTML can appear inside the text
    for (auto child: elem->xmlChildren())
    {
        writeSpan(stream, child, links, QString());
    }
    if (in_element) stream->writeEndElement(); // span
}


static void writePara(QXmlStreamWriter *stream, XmlElement *elem, const QString &classname, const LinkageList &links,
               const QString &label, const QString &bodytext)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....paragraph";
#endif

    if (elem->isFixedString())
    {
        writeSpan(stream, elem, links, classname);
        return;
    }

    if (elem->objectName() == "snippet")
    {
        stream->writeStartElement("p");
    }
    else
    {
        stream->writeStartElement(elem->objectName());
        writeAttributes(stream, elem, classname);
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
        // class might already have been written by calling writeAttributes
        if (elem->objectName() == "snippet") stream->writeAttribute("class", classname);
        class_set = true;
    }

    if (!bodytext.isEmpty())
    {
        stream->writeCharacters(bodytext);
    }
    for (auto child : elem->xmlChildren())
    {
        if (child->objectName() == "span")
            writeSpan(stream, child, links, class_set ? QString() : classname);
        else if (child->objectName() != "tag_assign")
            // Ignore certain children
            writePara(stream, child, classname, links, /*no prefix*/QString(), QString());
    }
    stream->writeEndElement();  // p
}


static void writeParaChildren(QXmlStreamWriter *stream, XmlElement *parent, const QString &classname, const LinkageList &links,
                       const QString &prefix_label = QString(), const QString &prefix_bodytext = QString())
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children";
#endif

    bool first = true;
    for (auto para: parent->xmlChildren())
    {
        if (first)
            writePara(stream, para, classname, links, /*prefix*/prefix_label, prefix_bodytext);
        else
            writePara(stream, para, classname, links, /*no prefix*/QString(), QString());
        first = false;
    }
}

/*
 * Return the divisor for the map's size
 */

std::mutex image_mutex;

static int writeImage(QXmlStreamWriter *stream, const QString &image_name, const QByteArray &orig_data, XmlElement *mask_elem,
               const QString &filename, const QString &class_name, XmlElement *annotation, const LinkageList &links,
               const QString &usemap = QString())
{
    return 1;

    // Only one thread at a time can use QImage
    std::unique_lock<std::mutex> lock{image_mutex};

    QBuffer buffer;
    QString format = filename.split(".").last();
    int divisor = 1;

    stream->writeStartElement("p");

    stream->writeStartElement("figure");

    stream->writeStartElement("figcaption");
    if (annotation)
        writeParaChildren(stream, annotation, "annotation " + class_name, links, image_name);
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


static void writeExtObject(QXmlStreamWriter *stream, const QString &obj_name, const QByteArray &data,
                    const QString &filename, const QString &class_name, XmlElement *annotation, const LinkageList &links)
{
    // Write the asset data to an external file
    QFile file(filename);
    if (!file.open(QFile::WriteOnly))
    {
        qWarning() << "writeExtObject: failed to open file for writing:" << filename;
        return;
    }
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

    if (annotation) writeParaChildren(stream, annotation, "annotation " + class_name, links);
    stream->writeEndElement();  // p
}

/**
 * @brief output_gumbo_children
 * Takes the GUMBO tree and calls the relevant methods of QXmlStreamWriter
 * to reproduce it in XHTML.
 * @param node
 */

static void output_gumbo_children(QXmlStreamWriter *stream, GumboNode *parent)
{
    GumboVector *children = &parent->v.element.children;
    for (unsigned i=0; i<children->length; i++)
    {
        GumboNode *node = static_cast<GumboNode*>(children->data[i]);
        switch (node->type)
        {
        case GUMBO_NODE_TEXT:
            //qDebug() << "GUMBO_NODE_TEXT:";
        {
            const QString text(node->v.text.text);
            int pos = text.lastIndexOf(" - created with Hero Lab");
            stream->writeCharacters((pos < 0) ? text : text.left(pos));
        }
            break;
        case GUMBO_NODE_CDATA:
            //qDebug() << "GUMBO_NODE_CDATA:";
            stream->writeCDATA(node->v.text.text);
            break;
        case GUMBO_NODE_COMMENT:
            //qDebug() << "GUMBO_NODE_COMMENT:";
            stream->writeComment(node->v.text.text);
            break;

        case GUMBO_NODE_ELEMENT:
            //qDebug() << "GUMBO_NODE_ELEMENT:";
            stream->writeStartElement(gumbo_normalized_tagname(node->v.element.tag));
        {
            const GumboVector *attribs = &node->v.element.attributes;
            for (unsigned i=0; i<attribs->length; i++)
            {
                GumboAttribute *at = static_cast<GumboAttribute*>(attribs->data[i]);
                stream->writeAttribute(at->name, at->value);
            }
        }
            output_gumbo_children(stream, node);
            stream->writeEndElement();
            break;

        case GUMBO_NODE_WHITESPACE:
            //qDebug() << "GUMBO_NODE_WHITESPACE";
            break;
        case GUMBO_NODE_DOCUMENT:
            //qDebug() << "GUMBO_NODE_DOCUMENT";
            break;
        case GUMBO_NODE_TEMPLATE:
            //qDebug() << "GUMBO_NODE_TEMPLATE";
            break;
        }
    }
}


static GumboNode *get_gumbo_child(GumboNode *parent, const QString &name)
{
    GumboVector *children = &parent->v.element.children;
    for (unsigned i=0; i<children->length; i++)
    {
        GumboNode *node = static_cast<GumboNode*>(children->data[i]);
        if (node->type == GUMBO_NODE_ELEMENT)
        {
            const QString tag = gumbo_normalized_tagname(node->v.element.tag);
            if (tag == name) return node;
        }
    }
    return nullptr;
}


std::mutex gumbo_mutex;

static bool write_html(QXmlStreamWriter *stream, bool use_fixed_title, const QString &sntype, const QByteArray &data)
{
    // Stop other tasks calling gumbo_parse
    std::unique_lock<std::mutex> {gumbo_mutex};

    // Put the children of the BODY into this frame.
    GumboOutput *output = gumbo_parse(data);
    if (output == 0)
    {
        return false;
    }

    stream->writeStartElement("details");
    stream->writeAttribute("class", sntype.toLower() + "Details");

    GumboNode *head = get_gumbo_child(output->root, "head");

    // Maybe we have a CSS that we can put inline.
    GumboNode *style = get_gumbo_child(head, "style");
    if (style)
    {
        stream->writeStartElement("style");
        stream->writeAttribute("type", "text/css");
        output_gumbo_children(stream, style);  // it should only be text
        stream->writeEndElement();
    }

    if (use_fixed_title)
    {
        stream->writeStartElement("summary");
        stream->writeAttribute("class", sntype + "Summary");
        stream->writeCharacters(sntype);
        stream->writeEndElement(); // summary
    }
    else
    {
        GumboNode *title = head ? get_gumbo_child(head, "title") : nullptr;
        if (title)
        {
            stream->writeStartElement("summary");
            stream->writeAttribute("class", sntype.toLower() + "Summary");
            output_gumbo_children(stream, title);  // it should only be text
            stream->writeEndElement(); // summary
        }
    }

    GumboNode *body = get_gumbo_child(output->root, "body");
    if (body) output_gumbo_children(stream, body);

    stream->writeEndElement();  // details

    // Get GUMBO to release all the memory
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return true;
}


static void writeSnippet(QXmlStreamWriter *stream, XmlElement *snippet, const LinkageList &links)
{
    QString sn_type = snippet->attribute("type");
    QString sn_style = snippet->attribute("style"); // Read_Aloud, Callout, Flavor, Handout
#if DUMP_LEVEL > 3
    qDebug() << "...snippet" << sn_type;
#endif

    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        for (auto gm_directions: snippet->xmlChildren("gm_directions"))
            writeParaChildren(stream, gm_directions, "gm_directions " + sn_style, links);

        for (auto contents: snippet->xmlChildren("contents"))
            writeParaChildren(stream, contents, "contents " + sn_style, links);
    }
    else if (sn_type == "Labeled_Text")
    {
        for (auto contents: snippet->xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            writeParaChildren(stream, contents, "contents " + sn_style, links, /*prefix*/snippet->snippetName());  // has its own 'p'
        }
    }
    else if (sn_type == "Portfolio")
    {
        // As for other EXT OBJECTS, but with unzipping involved to get statblocks
        XmlElement *annotation = snippet->xmlChild("annotation");
        for (auto ext_object: snippet->xmlChildren("ext_object"))
        {
            for (auto asset: ext_object->xmlChildren("asset"))
            {
                QString filename = asset->attribute("filename");
                XmlElement *contents = asset->xmlChild("contents");
                if (contents)
                {
                    writeExtObject(stream, ext_object->attribute("name"), contents->p_byte_data,
                                   filename, sn_style, annotation, links);

                    // Put in markers for statblock
                    QBuffer buffer(&contents->p_byte_data);
                    QuaZip zip(&buffer);
                    if (zip.open(QuaZip::mdUnzip))
                    {
                        stream->writeStartElement("section");
                        stream->writeAttribute("class", "portfolioListing");
                        for (bool more=zip.goToFirstFile(); more; more=zip.goToNextFile())
                        {
                            if (zip.getCurrentFileName().startsWith("statblocks_html/"))
                            {
                                QuaZipFile file(&zip);
                                if (!file.open(QuaZipFile::ReadOnly) ||
                                        !write_html(stream, false, sn_type, file.readAll()))
                                {
                                    qWarning() << "GUMBO failed to parse" << zip.getCurrentFileName();
                                }
                            }
                        }
                        stream->writeEndElement(); // section[class="portfolioListing"]
                    }
                }
            }
        }

    }
    else if (sn_type == "Picture" ||
             sn_type == "PDF" ||
             sn_type == "Audio" ||
             sn_type == "Video" ||
             sn_type == "Statblock" ||
             sn_type == "Foreign" ||
             sn_type == "Rich_Text")
    {
        // TODO: if filename ends with .html then we can put it inline.

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
                        writeImage(stream, ext_object->attribute("name"), contents->p_byte_data,
                                   /*mask*/nullptr, filename, sn_style, annotation, links);
                    else
                        writeExtObject(stream, ext_object->attribute("name"), contents->p_byte_data,
                                       filename, sn_style, annotation, links);

                    if (filename.endsWith(".html") || filename.endsWith(".htm") ||filename.endsWith(".rtf"))
                    {
                        write_html(stream, true, sn_type, contents->p_byte_data);
                    }
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

            int divisor = writeImage(stream, smart_image->attribute("name"), contents->p_byte_data,
                                     mask, filename, sn_style, annotation, links, usemap);

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
                    if (!link.isEmpty()) writeTopicHref(stream, link);

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
            writeParaChildren(stream, annotation, "annotation " + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Date_Range")
    {
        XmlElement *date       = snippet->xmlChild("date_range");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = "From: " + date->attribute("display_start") + " To: " + date->attribute("display_end");
        if (annotation)
            writeParaChildren(stream, annotation, "annotation" + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Tag_Standard")
    {
        XmlElement *tag        = snippet->xmlChild("tag_assign");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (tag == nullptr) return;

        QString bodytext = tag->attribute("tag_name");
        if (annotation)
            writeParaChildren(stream, annotation, "annotation" + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Numeric")
    {
        XmlElement *contents   = snippet->xmlChild("contents");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (contents == nullptr) return;

        const QString &bodytext = contents->childString();
        if (annotation)
            writeParaChildren(stream, annotation, "annotation" + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);

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
            writeParaChildren(stream, annotation, "annotation" + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
    }
    // Hybrid_Tag
}


static void writeSection(QXmlStreamWriter *stream, XmlElement *section, const LinkageList &links, int level)
{
#if DUMP_LEVEL > 2
    qDebug() << "..section" << section->attribute("name");
#endif

    // Start with HEADER for the section
    stream->writeStartElement(QString("h%1").arg(level+1));
    stream->writeAttribute("class", QString("section%1").arg(level));
    stream->writeCharacters(section->attribute("name"));
    stream->writeEndElement();

    // Write snippets
    for (auto snippet: section->xmlChildren("snippet"))
    {
        writeSnippet(stream, snippet, links);
    }

    // Write following sections
    for (auto subsection: section->xmlChildren("section"))
    {
        writeSection(stream, subsection, links, level+1);
    }
}


static void writeTopicBody(QXmlStreamWriter *stream, XmlElement *topic)
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
    for (auto section: topic->xmlChildren("section"))
    {
        writeSection(stream, section, links, /*level*/ 1);
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
        stream->writeEndElement(); // h2

        stream->writeStartElement("ul");
        stream->writeAttribute("class", "childTopicsList");
        for (auto child: child_topics)
        {
            stream->writeStartElement("li");
            stream->writeAttribute("class", "childTopicsEntry");

            stream->writeStartElement("a");
            writeTopicHref(stream, child->attribute("topic_id"));
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
            writeTopicBody(stream, child_topic);
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
    QXmlStreamWriter *stream = &topic_stream;

    start_file(stream);
    stream->writeTextElement("title", topic->attribute("public_name"));
    stream->writeEndElement(); // head

    stream->writeStartElement("body");

    writeTopicBody(stream, topic);

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

static void writeTopicToIndex(QXmlStreamWriter *stream, XmlElement *topic, int level)
{
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
    writeTopicHref(stream, topic->attribute("topic_id"));
    stream->writeCharacters(topic->attribute("public_name"));
    stream->writeEndElement();   // a

    if (has_kids)
    {
        stream->writeEndElement();  // summary

        // next level for the children
        stream->writeStartElement(QString("ul"));
        stream->writeAttribute("class", QString("summary%1").arg(level));

        std::sort(child_topics.begin(), child_topics.end(), sort_by_public_name);

        for (auto child_topic: child_topics)
        {
            writeTopicToIndex(stream, child_topic, level+1);
        }

        stream->writeEndElement();   // ul
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
    QXmlStreamWriter stream(&out_file);

    if (root_elem->objectName() == "output")
    {
        start_file(&stream);
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
                        stream.writeTextElement("title", header->attribute("name"));
                    }
                }
                write_head_meta(&stream, root_elem);
                stream.writeEndElement();  // head
            }
            else if (child->objectName() == "contents")
            {
                stream.writeStartElement("body");

                // A top-level button to collapse/expand the entire list
                const QString expand_all   = "Expand All";
                const QString collapse_all = "Collapse All";

                stream.writeStartElement("button");
                stream.writeAttribute("onclick", "toggleNavVis(this)");
                stream.writeAttribute("checked", "false");
                stream.writeCharacters(expand_all);
                stream.writeEndElement();

                stream.writeStartElement("script");
                stream.writeAttribute("type", "text/javascript");
                // This script isn't found when index.html is loaded into the NAV panel
                // of children; so this script is duplicated inside scripts.js
                stream.writeCharacters("function toggleNavVis(item) {\n"
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
                stream.writeEndElement();
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
                    stream.writeStartElement("details");
                    // If we are displaying the index on each page,
                    // then it is better to have categories not expanded.
                    if (!always_show_index)
                    {
                        stream.writeAttribute("open", "true");
                    }
                    stream.writeAttribute("class", "indexCategory");

                    stream.writeStartElement("summary");
                    stream.writeCharacters(cat);
                    stream.writeEndElement();  // summary

                    stream.writeStartElement(QString("ul"));
                    stream.writeAttribute("class", QString("summary1"));

                    // Organise topics alphabetically
                    auto topics = categories.values(cat);
                    std::sort(topics.begin(), topics.end(), sort_by_public_name);

                    for (auto topic: topics)
                    {
                        writeTopicToIndex(&stream, topic, 1);
                    }
                    stream.writeEndElement();   // ul
                    stream.writeEndElement();  // details
                }
#else
                // Outer wrapper for summary page

                for (auto topic: main_topics)
                {
                    writeTopicToIndex(topic);
                }
                // Outer wrapper for summary page
#endif

                stream.writeEndElement(); // body
            }
        }
        stream.writeEndElement(); // html
    }
}


static void write_topics(QList<XmlElement*> list, int first, int last)
{
    qDebug() << "THREAD for" << first << "to" << last << ".";
    for (auto it = first; it < last; ++it)
    {
        qDebug() << "topic" << it;
        writeTopicFile(list.at(it));
    }
}


void toHtml(const QString &path,
            XmlElement *root_elem,
            int max_image_width,
            bool separate_files,
            bool use_reveal_mask,
            bool index_on_every_page)
{
    //QElapsedTimer timer;
    //timer.start();

    image_max_width   = max_image_width;
    apply_reveal_mask = use_reveal_mask;
    always_show_index = index_on_every_page;
    in_single_file    = !separate_files;

    // Get a full list of the individual STYLE attributes of every single topic,
    // with a view to putting them into the CSS instead.
    QSet<QString> styles_set;
    for (auto child : root_elem->findChildren<XmlElement*>())
    {
        if (child->hasAttribute("style"))
        {
            styles_set.insert(child->attribute("style"));
        }
    }
    class_of_style.clear();
    int stylenumber=1;
    for (auto style: styles_set)
    {
        if (predefined_styles.contains(style))
            class_of_style.insert(style, style);
        else
            class_of_style.insert(style, QString("rwStyle%1").arg(stylenumber++));
    }

    // Write out the individual TOPIC files now:
    // Note use of findChildren to find children at all levels,
    // whereas xmlChildren returns only direct children.
    if (separate_files)
    {
        write_support_files();
        writeIndex(root_elem);

        // A separate file for every single topic
        auto topics = root_elem->findChildren<XmlElement*>("topic");
#if 0
        // This method speeds up the output of multiple files by creating separate
        // threads to handle each CHUNK of topics.
        // (Unfortunately, it only takes 4 seconds to write out the S&S campaign,
        //  so reducing this to 2.3 seconds doesn't help that much.)
        // Although on the first run, it is a lot slower (presumably due to O/S
        // not caching the files originally.
        qDebug() << "Concurrency =" << std::thread::hardware_concurrency();
        unsigned max_threads = std::thread::hardware_concurrency();
        std::vector<std::future<void>> jobs;
        int last  = topics.size();
        int step  = (last + max_threads - 1) / max_threads;
        int first = 0;
        for (unsigned i=0; i<max_threads; i++)
        {
            jobs.push_back(std::async(std::launch::async, write_topics,
                                      topics, first, qMin(last,first + step)));
            first += step;
        }
        for (unsigned i=0; i<max_threads; i++)
            jobs[i].get();
#else
        for (auto topic : topics)
        {
            writeTopicFile(topic);
        }
#endif
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
        QXmlStreamWriter stream(&single_file);

        start_file(&stream);
        stream.writeTextElement("title", details->attribute("name"));
        write_head_meta(&stream, root_elem);
        stream.writeEndElement(); // head

        stream.writeStartElement("body");

        // All topics in a single file, nesting child topics
        // inside the parent topic.
        for (auto topic : contents->xmlChildren("topic"))
        {
            writeTopicBody(&stream, topic);
        }

        // Write footer for single file
        // Complete the individual topic file
        stream.writeEndElement(); // body
        stream.writeEndElement(); // html
    }

    //qInfo() << "TIME TO GENERATE HTML =" << timer.elapsed() << "milliseconds";
}
