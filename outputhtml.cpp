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

//#define ALWAYS_SAVE_EXT_FILES
//#define THREADED
#define TIME_CONVERSION

#include "outputhtml.h"

#include <QBitmap>
#include <QBuffer>
#include <QCollator>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QApplication>
#include <QStyle>
#include <QStaticText>
#include <future>
#ifdef TIME_CONVERSION
#include <QElapsedTimer>
#endif

#include "gumbo.h"
#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

#include "xmlelement.h"
#include "linefile.h"
#include "linkage.h"

static int image_max_width = -1;
static bool apply_reveal_mask = true;
static bool always_show_index = false;
static bool in_single_file = false;
static bool sort_by_prefix = true;
static QCollator collator;

#if 1
typedef QFile OurFile;
#else
typedef LineFile OurFile;
#endif

#undef DUMP_CHILDREN

static const QStringList predefined_styles = { "Normal", "Read_Aloud", "Handout", "Flavor", "Callout" };
static QMap<QString /*style string*/ ,QString /*replacement class name*/> class_of_style;
static QMap<QString,QStaticText> category_pin_of_topic;
static QMap<QString,XmlElement*> all_topics;

const QString map_pin_title_default("___ %1 ___");
const QString map_pin_description_default("%1");
const QString map_pin_gm_directions_default("---  GM DIRECTIONS  ---\n%1");

QString map_pin_title(map_pin_title_default);
QString map_pin_description(map_pin_description_default);
QString map_pin_gm_directions(map_pin_gm_directions_default);

#define DUMP_LEVEL 0

// Sort topics, first by prefix, and then by topic name
static bool sort_all_topics(const XmlElement *left, const XmlElement *right)
{
    if (sort_by_prefix)
    {
        // items with prefix come before items without prefix
        const QString &left_prefix  = left->attribute("prefix");
        const QString &right_prefix = right->attribute("prefix");
        if (left_prefix != right_prefix)
        {
            if (right_prefix.isEmpty())
            {
                // left prefix is not empty, so must come first
                return true;
            }
            if (left_prefix.isEmpty())
            {
                // right prefix is not empty, so must come first
                return false;
            }
            return collator.compare(left_prefix, right_prefix) < 0;
        }
    }
    // Both have the same prefix
    return collator.compare(left->attribute("public_name"), right->attribute("public_name")) < 0;
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
    QStringList files{"theme.css","scripts.js"};

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


static void write_meta_child(QXmlStreamWriter *stream, const QString &meta_name, const XmlElement *details, const QString &child_name)
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


static void write_head_meta(QXmlStreamWriter *stream, const XmlElement *root_elem)
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

#if 0
    stream->writeStartElement("meta");
    stream->writeAttribute("http-equiv", "Content-Type");
    stream->writeAttribute("content", "text/html");
    stream->writeAttribute("charset", "utf-8");
    stream->writeEndElement(); // meta
#else
    stream->writeStartElement("meta");
    stream->writeAttribute("charset", "utf-8");
    stream->writeEndElement(); // meta
#endif

    stream->writeStartElement("meta");
    stream->writeAttribute("name", "generator");
    stream->writeAttribute("content", qApp->applicationName() + " " + qApp->applicationVersion());
    stream->writeEndElement();

    if (in_single_file)
    {
        stream->writeStartElement("style");

        // Put the style sheet in the same file
        QFile theme(":/theme.css");
        if (theme.open(QFile::ReadOnly|QFile::Text))
        {
            stream->writeCharacters(theme.readAll());
        }

        // Also include all the locally found styles
        for (auto iter = class_of_style.begin(); iter != class_of_style.end(); iter++)
        {
            if (!predefined_styles.contains(iter.value()))
            {
                stream->writeCharacters("." + iter.value() + " {\n" + iter.key() + "\n}\n");
            }
        }
        stream->writeEndElement(); // style
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


static void write_attributes(QXmlStreamWriter *stream, const XmlElement *elem, const QString &classname)
{
    QStringList class_names;
    if (!classname.isEmpty()) class_names.append(classname);
    for (auto attr : elem->attributes())
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


static void write_topic_href(QXmlStreamWriter *stream, const QString &topic)
{
    if (in_single_file)
        stream->writeAttribute("href", "#" + topic);
    else
        stream->writeAttribute("href", topic + ".xhtml");
}


static void write_span(QXmlStreamWriter *stream, XmlElement *elem, const LinkageList &links, const QString &classname)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....span";
#endif

    if (elem->isFixedString())
    {
        // TODO - the span containing the link might have style or class information!
        // Check to see if the fixed text should be replaced with a link.
        const QString text = elem->fixedText();
        const QString link_text = links.find(text);
        if (!link_text.isNull())
        {
            stream->writeStartElement("a");
            write_topic_href(stream, link_text);
            stream->writeCharacters(text);  // case might be different
            stream->writeEndElement();  // a
            return;
        }
        stream->writeCharacters(text);
    }
    else
    {
        // Only put in span if we really require it
        bool in_element = elem->objectName() != "span" || elem->hasAttribute("style") || !classname.isEmpty();
        if (in_element)
        {
            stream->writeStartElement(elem->objectName());
            write_attributes(stream, elem, classname);
        }

        // All sorts of HTML can appear inside the text
        for (auto child: elem->xmlChildren())
        {
            write_span(stream, child, links, QString());
        }
        if (in_element) stream->writeEndElement(); // span
    }
}


static void write_para(QXmlStreamWriter *stream, XmlElement *elem, const QString &classname, const LinkageList &links,
                       const QString &label, const QString &bodytext)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....paragraph";
#endif

    if (elem->isFixedString())
    {
        write_span(stream, elem, links, classname);
        return;
    }

    if (elem->objectName() == "snippet")
    {
        stream->writeStartElement("p");
    }
    else
    {
        stream->writeStartElement(elem->objectName());
        write_attributes(stream, elem, classname);
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
            write_span(stream, child, links, class_set ? QString() : classname);
        else if (child->objectName() != "tag_assign")
            // Ignore certain children
            write_para(stream, child, classname, links, /*no prefix*/QString(), QString());
    }
    stream->writeEndElement();  // p
}


static void write_para_children(QXmlStreamWriter *stream, XmlElement *parent, const QString &classname, const LinkageList &links,
                                const QString &prefix_label = QString(), const QString &prefix_bodytext = QString())
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children";
#endif

    bool first = true;
    for (auto para: parent->xmlChildren())
    {
        if (first)
            write_para(stream, para, classname, links, /*prefix*/prefix_label, prefix_bodytext);
        else
            write_para(stream, para, classname, links, /*no prefix*/QString(), QString());
        first = false;
    }
}


static QString get_elem_string(XmlElement *elem)
{
    if (!elem) return QString();

    // RW puts \r\n as a line terminator, rather than just \n
    return elem->childString().replace("&#xd;\n","\n");
}

static QString simple_para_text(XmlElement *p)
{
    // Collect all spans into a single paragraph (without formatting)
    // (get all nested spans; we can't use write_span here)
    QString result;
    for (auto span : p->findChildren<XmlElement*>("span"))
    {
        for (auto child : span->xmlChildren())
        {
            if (child->isFixedString())
            {
                result.append(child->fixedText());
            }
        }
    }
    return result;
}

/*
 * Return the divisor for the map's size
 */

#ifdef THREADED
std::mutex image_mutex;
#endif

static int write_image(QXmlStreamWriter *stream, const QString &image_name, const QByteArray &orig_data, XmlElement *mask_elem,
                       const QString &filename, const QString &class_name, XmlElement *annotation, const LinkageList &links,
                       const QString &usemap = QString(), const QList<XmlElement*> pins = QList<XmlElement*>())
{
    QBuffer buffer;
    QString format = filename.split(".").last();
    int divisor = 1;
    const int pin_size = 20;
    bool in_buffer = false;

    stream->writeStartElement("p");

    stream->writeStartElement("figure");

    stream->writeStartElement("figcaption");
    if (annotation)
        write_para_children(stream, annotation, "annotation " + class_name, links, image_name);
    else
        stream->writeCharacters(image_name);
    stream->writeEndElement();  // figcaption

    // See if possible image conversion is required
    bool bad_format = (format == "bmp" || format == "tif");
    if (mask_elem != nullptr || image_max_width > 0 || bad_format || !pins.isEmpty())
    {
#ifdef THREADED
        // Only one thread at a time can use QImage
        std::unique_lock<std::mutex> lock{image_mutex};
#endif

        QImage image = QImage::fromData(orig_data, qPrintable(format));
        if (bad_format)
        {
            format = "png";
            in_buffer = true;
        }

        if (mask_elem != nullptr || (image_max_width > 0 && image.width() > image_max_width))
        {
            // Apply mask, if supplied
            if (mask_elem && apply_reveal_mask)
            {
                // If the mask is empty, then don't use it
                // (if the image is JPG, the mask isn't necessarily JPG
                QImage mask = QImage::fromData(mask_elem->byteData());

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
            if (image_max_width > 0)
            {
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
            }
            //format = "png";   // not always better (especially if was JPG)
            in_buffer = true;
        } /* mask or scaling */

        // Add some graphics to show where PINS will be
        if (!pins.isEmpty())
        {
            // Set desired colour of the marker
            QPainter painter(&image);
            painter.setPen(QPen(Qt::red));

            // Set correct font size
            QFont font(painter.font());
            font.setPixelSize(pin_size-1);
            painter.setFont(font);

            // Create string once
            static QStaticText default_pin_text;
            static bool first_time = true;
            if (first_time)
            {
                first_time = false;
                /* UNICODE : 1F4CC = map marker (push pin) */
                /* original = bottom-left corner */
                uint pin_char = 0x1f4cd;
                default_pin_text.setText(QString::fromUcs4(&pin_char, 1));
                default_pin_text.prepare(QTransform(), font);
            }
            // TODO - select pin appropriate to the type of topic to which it is linked!
            // The "category_name" attribute of each "topic" element is what needs to be matched to a pin_text
            for (XmlElement *pin : pins)
            {
                const QString topic_name = pin->attribute("topic_id");
                if (!topic_name.isEmpty() && !category_pin_of_topic.contains(topic_name))
                {
                    QString category;
                    if (all_topics.contains(topic_name))
                        category = all_topics[topic_name]->attribute("category_name");
                    else
                        category = "..generic..";

                    // Find the category of the named topic (if any).
                    uint pin_char = 0x1f4cc;    // round pin
                    QStaticText cat_pin;
                    cat_pin.setText(QString::fromUcs4(&pin_char, 1));
                    cat_pin.prepare(QTransform(), font);
                    category_pin_of_topic.insert(topic_name, cat_pin);
                }

                painter.setPen(QPen(Qt::blue));
                const QStaticText &pin_text = topic_name.isEmpty() ? default_pin_text : category_pin_of_topic.value(topic_name);
                painter.drawStaticText(pin->attribute("x").toInt() / divisor,
                                       pin->attribute("y").toInt() / divisor - pin_text.size().height(),
                                       pin_text);
            }

            // Tell the next bit to read from the buffer
            in_buffer = true;
        } /* pins */

        // Do we need to put it in the buffer?
        if (in_buffer)
        {
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, qPrintable(format));
            buffer.close();
            in_buffer = true;
        }
    }

    stream->writeStartElement("img");
    if (!usemap.isEmpty()) stream->writeAttribute("usemap", "#" + usemap);
    stream->writeAttribute("alt", image_name);
    if (in_buffer)
        stream->writeAttribute("src", QString("data:image/%1;base64,%2").arg(format)
                               .arg(QString::fromLatin1(buffer.data().toBase64())));
    else
        stream->writeAttribute("src", QString("data:image/%1;base64,%2").arg(format)
                               .arg(QString::fromLatin1(orig_data.toBase64())));
    stream->writeEndElement();  // img

    if (!pins.isEmpty())
    {
        // Create the clickable MAP on top of the map
        stream->writeStartElement("map");
        stream->writeAttribute("name", usemap);
        for (auto pin : pins)
        {
            int x = pin->attribute("x").toInt() / divisor;
            int y = pin->attribute("y").toInt() / divisor - pin_size;
            stream->writeStartElement("area");
            stream->writeAttribute("shape", "rect");
            stream->writeAttribute("coords", QString("%1,%2,%3,%4")
                                   .arg(x).arg(y)
                                   .arg(x + pin_size).arg(y + pin_size));

            // Build up a tooltip from the text configured on the pin
            QString pin_name = pin->attribute("pin_name");
            QString description = get_elem_string(pin->xmlChild("description"));
            QString gm_directions = get_elem_string(pin->xmlChild("gm_directions"));
            QString link = pin->attribute("topic_id");
            QString title;
            // OPTION - use first section of topic if no description or gm_directions is provided
            if ((description.isEmpty() || gm_directions.isEmpty()) && !link.isEmpty())
            {
                // First section - all Multi_Line snippet - contents/gm_directions - p - span
                const XmlElement *topic = all_topics.value(link);
                const XmlElement *section = topic ? topic->xmlChild("section") : nullptr;
                if (section)
                {
                    bool add_desc = description.isEmpty();
                    bool add_gm   = gm_directions.isEmpty();
                    bool first_contents = true;
                    bool first_gm = true;
                    for (auto snippet : section->xmlChildren("snippet"))
                    {
                        if (snippet->attribute("type") != "Multi_Line") continue;

                        if (add_desc)
                        {
                            if (XmlElement *contents = snippet->xmlChild("contents"))
                            {
                                // <p class="RWDefault"><span class="RWSnippet">text</span></p>
                                for (auto p : contents->xmlChildren("p"))
                                {
                                    QString text = simple_para_text(p);
                                    if (!text.isEmpty())
                                    {
                                        if (first_contents)
                                            first_contents = false;
                                        else
                                            description.append("\n\n");
                                        description.append(text);
                                    }
                                }
                            }
                        }
                        if (add_gm)
                        {
                            bool first = true;
                            if (XmlElement *gm_dir = snippet->xmlChild("gm_directions"))
                            {
                                // <p class="RWDefault"><span class="RWSnippet">text</span></p>
                                for (auto p : gm_dir->xmlChildren("p"))
                                {
                                    QString text = simple_para_text(p);
                                    if (!text.isEmpty())
                                    {
                                        if (first_gm)
                                            first_gm = false;
                                        else
                                            gm_directions.append("\n\n");
                                        gm_directions.append(text);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (!pin_name.isEmpty())
            {
                if (description.isEmpty() && gm_directions.isEmpty())
                    title = pin_name;
                else
                    title.append(map_pin_title.arg(pin_name));
            }
            if (!description.isEmpty())
            {
                if (!title.isEmpty()) title.append("\n\n");
                title.append(map_pin_description.arg(description));
            }
            if (!gm_directions.isEmpty())
            {
                if (!title.isEmpty()) title.append("\n\n");
                title.append(map_pin_gm_directions.arg(gm_directions));
            }
            if (!title.isEmpty()) stream->writeAttribute("title", title);

            if (!link.isEmpty()) write_topic_href(stream, link);

            stream->writeEndElement();  // area
        }
        stream->writeEndElement();  // map
    }

    stream->writeEndElement();  // figure
    stream->writeEndElement();  // p
    return divisor;
}


static void write_ext_object(QXmlStreamWriter *stream, const QString &obj_name, const QByteArray &data,
                             const QString &filename, const QString &class_name, XmlElement *annotation, const LinkageList &links)
{
    // Don't inline objects which are more than 5MB in size
    if (data.length() < 5*1000*1000)
    {
#ifdef ALWAYS_SAVE_EXT_FILES
        // TESTING ONLY: Write the asset data to an external file
        QFile file(filename);
        if (!file.open(QFile::WriteOnly))
        {
            qWarning() << "writeExtObject: failed to open file for writing:" << filename;
            return;
        }
        file.write (data);
        file.close();
#endif

        QString mime_type = "binary/octet-stream";
        QString filetype = filename.split(".").last();
        if (filetype == "pdf") mime_type = "application/pdf";

        stream->writeStartElement("p");

        stream->writeStartElement("span");
        stream->writeAttribute("class", "snippet_label");
        stream->writeCharacters(obj_name + ": ");
        stream->writeEndElement();  // span

        stream->writeStartElement("span");
        stream->writeStartElement("a");
        stream->writeAttribute("download", filename);
        stream->writeAttribute("href", QString("data:%1;base64,%2").arg(mime_type).arg(QString::fromLatin1(data.toBase64())));
        stream->writeCharacters(filename);
        stream->writeEndElement();  // a
        stream->writeEndElement();  // span

        if (annotation) write_para_children(stream, annotation, "annotation " + class_name, links);
        stream->writeEndElement();  // p
    }
    else
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

        if (annotation) write_para_children(stream, annotation, "annotation " + class_name, links);
        stream->writeEndElement();  // p
    }
}

/**
 * @brief output_gumbo_children
 * Takes the GUMBO tree and calls the relevant methods of QXmlStreamWriter
 * to reproduce it in XHTML.
 * @param node
 */

static void output_gumbo_children(QXmlStreamWriter *stream, const GumboNode *parent, bool top=false)
{
    Q_UNUSED(top)
    GumboNode **children = reinterpret_cast<GumboNode**>(parent->v.element.children.data);
    for (unsigned count = parent->v.element.children.length; count > 0; --count)
    {
        const GumboNode *node = *children++;
        switch (node->type)
        {
        case GUMBO_NODE_TEXT:
        {
            const QString text(node->v.text.text);
            //if (top) qDebug() << "GUMBO_NODE_TEXT:" << text;
            int pos = text.lastIndexOf(" - created with Hero Lab");
            stream->writeCharacters((pos < 0) ? text : text.left(pos));
        }
            break;
        case GUMBO_NODE_CDATA:
            stream->writeCDATA(node->v.text.text);
            //if (top) qDebug() << "GUMBO_NODE_CDATA:" << node->v.text.text;
            break;
        case GUMBO_NODE_COMMENT:
            stream->writeComment(node->v.text.text);
            //if (top) qDebug() << "GUMBO_NODE_COMMENT:" << node->v.text.text;
            break;

        case GUMBO_NODE_ELEMENT:
        {
            const QString tag = gumbo_normalized_tagname(node->v.element.tag);
            stream->writeStartElement(tag);
            //if (top) qDebug() << "GUMBO_NODE_ELEMENT:" << tag;
            GumboAttribute **attributes = reinterpret_cast<GumboAttribute**>(node->v.element.attributes.data);
            for (unsigned count = node->v.element.attributes.length; count > 0; --count)
            {
                const GumboAttribute *attr = *attributes++;
                stream->writeAttribute(attr->name, attr->value);
            }
            output_gumbo_children(stream, node);
            stream->writeEndElement();
        }
            break;

        case GUMBO_NODE_WHITESPACE:
            //if (top) qDebug() << "GUMBO_NODE_WHITESPACE";
            break;
        case GUMBO_NODE_DOCUMENT:
            //if (top) qDebug() << "GUMBO_NODE_DOCUMENT";
            break;
        case GUMBO_NODE_TEMPLATE:
            //if (top) qDebug() << "GUMBO_NODE_TEMPLATE:" << gumbo_normalized_tagname(node->v.element.tag);
            break;
        }
    }
}


static const GumboNode *find_named_child(const GumboNode *parent, const QString &name)
{
    GumboNode **children = reinterpret_cast<GumboNode**>(parent->v.element.children.data);
    for (unsigned count = parent->v.element.children.length; count > 0; --count)
    {
        const GumboNode *child = *children++;
        if (child->type == GUMBO_NODE_ELEMENT &&
                gumbo_normalized_tagname(child->v.element.tag) == name)
        {
            return child;
        }
    }
    return nullptr;
}

#ifdef DUMP_CHILDREN
static void dump_children(const QString &from, const GumboNode *parent)
{
    QStringList list;
    GumboNode **children = reinterpret_cast<GumboNode**>(parent->v.element.children.data);
    for (int count = parent->v.element.children.length; count > 0; --count)
    {
        const GumboNode *node = *children++;
        switch (node->type)
        {
        case GUMBO_NODE_ELEMENT:
            list.append("element: " + QString(gumbo_normalized_tagname(node->v.element.tag)));
            break;
        case GUMBO_NODE_TEXT:
            list.append("text: " + QString(node->v.text.text));
            break;
        case GUMBO_NODE_CDATA:
            list.append("cdata: " + QString(node->v.text.text));
            break;
        case GUMBO_NODE_COMMENT:
            list.append("comment: " + QString(node->v.text.text));
            break;
        case GUMBO_NODE_WHITESPACE:
            list.append("whitespace");
            break;
        case GUMBO_NODE_DOCUMENT:
            list.append("document");
            break;
        case GUMBO_NODE_TEMPLATE:
            list.append("template: " + QString(gumbo_normalized_tagname(node->v.element.tag)));
            break;
        }
    }
    qDebug() << "   write_html:" << from << gumbo_normalized_tagname(parent->v.element.tag) << "has children" << list.join(", ");
}
#endif

static bool write_html(QXmlStreamWriter *stream, bool use_fixed_title, const QString &sntype, const QByteArray &data)
{
    // Put the children of the BODY into this frame.
    GumboOutput *output = gumbo_parse(data);
    if (output == nullptr)
    {
        return false;
    }

    stream->writeStartElement("details");
    stream->writeAttribute("class", sntype.toLower() + "Details");

#ifdef DUMP_CHILDREN
    dump_children("ROOT", output->root);
#endif

    const GumboNode *head = find_named_child(output->root, "head");
    const GumboNode *body = find_named_child(output->root, "body");

#ifdef DUMP_CHILDREN
    dump_children("HEAD", head);
#endif

    // Maybe we have a CSS that we can put inline.
    const GumboNode *style = find_named_child(head, "style");
#ifdef HANDLE_POOR_RTF
    if (!style) style = get_gumbo_child(body, "style");
#endif
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
        const GumboNode *title = head ? find_named_child(head, "title") : nullptr;
        if (title)
        {
            stream->writeStartElement("summary");
            stream->writeAttribute("class", sntype.toLower() + "Summary");
            output_gumbo_children(stream, title);  // it should only be text
            stream->writeEndElement(); // summary
        }
    }

    if (body)
    {
#ifdef DUMP_CHILDREN
        dump_children("BODY", body);
#endif
        output_gumbo_children(stream, body, /*top*/true);
    }

    stream->writeEndElement();  // details

    // Get GUMBO to release all the memory
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return true;
}


static void write_snippet(QXmlStreamWriter *stream, XmlElement *snippet, const LinkageList &links)
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
            write_para_children(stream, gm_directions, "gm_directions " + sn_style, links);

        for (auto contents: snippet->xmlChildren("contents"))
            write_para_children(stream, contents, "contents " + sn_style, links);
    }
    else if (sn_type == "Labeled_Text")
    {
        for (auto contents: snippet->xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            write_para_children(stream, contents, "contents " + sn_style, links, /*prefix*/snippet->snippetName());  // has its own 'p'
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
                    write_ext_object(stream, ext_object->attribute("name"), contents->byteData(),
                                   filename, sn_style, annotation, links);

                    // Put in markers for statblock
                    QByteArray store = contents->byteData();
                    QBuffer buffer(&store);
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
                    {
                        write_image(stream, ext_object->attribute("name"), contents->byteData(),
                                   /*mask*/nullptr, filename, sn_style, annotation, links);
                    }
                    else if (filename.endsWith(".html") ||
                             filename.endsWith(".htm")  ||
                             filename.endsWith(".rtf"))
                    {
                        stream->writeComment("Decoded " + filename);
                        write_html(stream, true, sn_type, contents->byteData());
                    }
                    else
                    {
                        write_ext_object(stream, ext_object->attribute("name"), contents->byteData(),
                                       filename, sn_style, annotation, links);
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

            int divisor = write_image(stream, smart_image->attribute("name"), contents->byteData(),
                                     mask, filename, sn_style, annotation, links, usemap, pins);
        }
    }
    else if (sn_type == "Date_Game")
    {
        XmlElement *date       = snippet->xmlChild("game_date");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = date->attribute("display");
        if (annotation)
            write_para_children(stream, annotation, "annotation " + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Date_Range")
    {
        XmlElement *date       = snippet->xmlChild("date_range");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = "From: " + date->attribute("display_start") + " To: " + date->attribute("display_end");
        if (annotation)
            write_para_children(stream, annotation, "annotation" + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Tag_Standard")
    {
        XmlElement *tag        = snippet->xmlChild("tag_assign");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (tag == nullptr) return;

        QString bodytext = tag->attribute("tag_name");
        if (annotation)
            write_para_children(stream, annotation, "annotation" + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Numeric")
    {
        XmlElement *contents   = snippet->xmlChild("contents");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (contents == nullptr) return;

        const QString &bodytext = contents->childString();
        if (annotation)
            write_para_children(stream, annotation, "annotation" + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);

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
            write_para_children(stream, annotation, "annotation" + sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext);
    }
    // Hybrid_Tag
}


static void write_section(QXmlStreamWriter *stream, XmlElement *section, const LinkageList &links, int level)
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
        write_snippet(stream, snippet, links);
    }

    // Write following sections
    for (auto subsection: section->xmlChildren("section"))
    {
        write_section(stream, subsection, links, level+1);
    }
}


static void write_topic_body(QXmlStreamWriter *stream, const QString &main_tag, const XmlElement *topic, bool allinone)
{
    // The RWoutput file puts the "true name" as the public_name of the topic.
    // All other names are listed as aliases (with no attributes).
    // If the RW topic has a "true name" defined then the actual name of the topic
    // is reported as an alias.

#if DUMP_LEVEL > 1
    qDebug() << ".topic" << topic->objectName() << ":" << topic->attribute("public_name");
#endif
    // Start with HEADER for the topic
    stream->writeStartElement(allinone ? "summary" : "header");

    stream->writeAttribute("class", "topicHeader");
    stream->writeAttribute("id", topic->attribute("topic_id"));
    if (topic->hasAttribute("prefix")) stream->writeAttribute("topic_prefix", topic->attribute("prefix"));
    if (topic->hasAttribute("suffix")) stream->writeAttribute("topic_suffix", topic->attribute("suffix"));
    stream->writeCharacters(topic->attribute("public_name"));

    auto aliases = topic->xmlChildren("alias");
    // Maybe some aliases (in own section for smaller font?)
    if (!aliases.isEmpty())
    {
        stream->writeEndElement();  // summary/header
        stream->writeStartElement("header");
        stream->writeAttribute("class", "nameAliasHeader");
    }
    for (auto alias : aliases)
    {
        stream->writeStartElement("p");
        stream->writeAttribute("class", "nameAlias");
        stream->writeCharacters(alias->attribute("name"));
        stream->writeEndElement(); // p
    }
    stream->writeEndElement();  // header for aliases OR the summary/header for topic title

    if (always_show_index)
    {
        stream->writeStartElement("nav");
        stream->writeAttribute("include-html", "index.xhtml");
        stream->writeEndElement();  // nav
    }

    stream->writeStartElement("section"); // for all the RW sections
    stream->writeAttribute("class", "topicBody");

    // Process <linkage> first, to ensure we can remap strings
    LinkageList links;
    for (auto link: topic->xmlChildren("linkage"))
    {
        if (link->attribute("direction") != "Inbound")
        {
            links.add(link->attribute("target_name"), link->attribute("target_id"));
        }
    }

    // Process all <sections>, applying the linkage for this topic
    for (auto section: topic->xmlChildren("section"))
    {
        write_section(stream, section, links, /*level*/ 1);
    }

    stream->writeEndElement();  // section (for RW sections)

    // Provide summary of links to child topics
    auto child_topics = topic->xmlChildren("topic");
    if (!child_topics.isEmpty())
    {
        std::sort(child_topics.begin(), child_topics.end(), sort_all_topics);

        stream->writeStartElement("footer");
        stream->writeAttribute("class", "topicFooter");

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
            write_topic_href(stream, child->attribute("topic_id"));
            stream->writeCharacters(child->attribute("public_name"));
            stream->writeEndElement();   // a
            stream->writeEndElement(); // li
        }
        stream->writeEndElement(); // ul
        stream->writeEndElement(); // footer

        if (in_single_file)
        {
            for (auto child_topic: child_topics)
            {
                stream->writeStartElement("details");
                stream->writeAttribute("class", "mainTopic");
                stream->writeAttribute("open", "open");
                write_topic_body(stream, main_tag, child_topic, allinone);
                stream->writeEndElement();
            }
        }
    }
}

/**
 * @brief write_link
 * Writes one of the navigation links that might appear in the footer of each page
 * @param stream
 * @param name
 * @param topic
 * @param override
 */
static void write_link(QXmlStreamWriter *stream, const QString &name, const XmlElement *topic, const QString &override = QString())
{
    stream->writeStartElement("td");
    stream->writeAttribute("class", "nav" + name);
    if (topic && topic->objectName() == "topic")
    {
        stream->writeStartElement("a");
        write_topic_href(stream, topic->attribute("topic_id"));
        stream->writeCharacters(topic->attribute("public_name"));
        stream->writeEndElement();
    }
    else if (!override.isEmpty())
    {
        stream->writeStartElement("a");
        write_topic_href(stream, override);
        stream->writeCharacters(name);
        stream->writeEndElement();
    }
    else
    {
        stream->writeCharacters(name);
    }
    stream->writeEndElement(); // TD
}


static void write_topic_file(const XmlElement *topic, const XmlElement *up, const XmlElement *prev, const XmlElement *next)
{
#if DUMP_LEVEL > 1
    qDebug() << ".topic" << topic->objectName() << ":" << topic->attribute("public_name");
#endif

    // Create a new file for this topic
    OurFile topic_file(topic->attribute("topic_id") + ".xhtml");
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

    write_topic_body(stream, "header", topic, /*allinone*/ false);

    // Include the INDEX file
    if (always_show_index)
    {
        stream->writeStartElement("script");
        stream->writeAttribute("type", "text/javascript");
        stream->writeCharacters("includeHTML();");
        stream->writeEndElement();
    }

    // Footer
    // PREV     TOP     UP    NEXT
    stream->writeStartElement("footer");
    stream->writeStartElement("table");
    stream->writeAttribute("class", "navTable");
    stream->writeStartElement("tr");

    write_link(stream, "Prev", prev);
    write_link(stream, "Home", nullptr, "index");
    write_link(stream, "Up",   up,      "index");  // If up is not defined, use the index file
    write_link(stream, "Next", next);

    stream->writeEndElement(); // tr
    stream->writeEndElement(); // table
    stream->writeEndElement(); // footer

    // Complete the individual topic file
    stream->writeEndElement(); // body

    stream->writeEndElement(); // html

    stream = nullptr;
}

/*
 * Write entries into the INDEX file
 */

static void write_topic_to_index(QXmlStreamWriter *stream, XmlElement *topic, int level)
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
    write_topic_href(stream, topic->attribute("topic_id"));
    stream->writeCharacters(topic->attribute("public_name"));
    stream->writeEndElement();   // a

    if (has_kids)
    {
        stream->writeEndElement();  // summary

        // next level for the children
        stream->writeStartElement(QString("ul"));
        stream->writeAttribute("class", QString("summary%1").arg(level));

        std::sort(child_topics.begin(), child_topics.end(), sort_all_topics);

        for (auto child_topic: child_topics)
        {
            write_topic_to_index(stream, child_topic, level+1);
        }

        stream->writeEndElement();   // ul
        stream->writeEndElement();  // details
    }

    stream->writeEndElement();   // li
}


static void write_separate_index(const XmlElement *root_elem)
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
                QMultiMap<QString,XmlElement*> categories;
                for (auto topic: child->xmlChildren("topic"))
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
                    std::sort(topics.begin(), topics.end(), sort_all_topics);

                    for (auto topic: topics)
                    {
                        write_topic_to_index(&stream, topic, 1);
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


static void write_first_page(QXmlStreamWriter *stream, const XmlElement *root_elem)
{
    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    XmlElement *contents   = root_elem->xmlChild("contents");

    if (definition == nullptr || contents == nullptr || details == nullptr)
    {
        qWarning() << "Invalid structure inside RWoutput file";
        qDebug() << "root_elem  =" << root_elem->objectName();
        qDebug() << "definition =" << definition;
        qDebug() << "contents   =" << contents;
        qDebug() << "details    =" << details;
        return;
    }

    stream->writeStartElement("h1");
    stream->writeAttribute("align", "center");
    stream->writeAttribute("style", "margin: 100px;");
    stream->writeCharacters(details->attribute("name"));
    stream->writeEndElement();

    stream->writeStartElement("p");
    stream->writeStartElement("b");
    stream->writeCharacters("Exported on ");
    stream->writeEndElement(); // b
    stream->writeCharacters(QDateTime::fromString(root_elem->attribute("export_date"), Qt::ISODate).toLocalTime().toString(Qt::SystemLocaleLongDate));
    stream->writeEndElement(); // p

    stream->writeStartElement("p");
    stream->writeStartElement("b");
    stream->writeCharacters("Generated by RWoutput tool on ");
    stream->writeEndElement(); // b
    stream->writeCharacters(QDateTime::currentDateTime().toString(Qt::SystemLocaleLongDate));
    stream->writeEndElement(); // p

    if (details->hasAttribute("summary"))
    {
        stream->writeTextElement("h2", "Summary");
        stream->writeTextElement("p", details->attribute("summary"));
    }

    if (details->hasAttribute("description"))
    {
        stream->writeTextElement("h2", "Description");
        stream->writeTextElement("p", details->attribute("description"));
    }

    if (details->hasAttribute("requirements"))
    {
        stream->writeTextElement("h2", "Requirements");
        stream->writeTextElement("p", details->attribute("requirements"));
    }

    if (details->hasAttribute("credits"))
    {
        stream->writeTextElement("h2", "Credits");
        stream->writeTextElement("p", details->attribute("credits"));
    }

    if (details->hasAttribute("legal"))
    {
        stream->writeTextElement("h2", "Legal");
        stream->writeTextElement("p", details->attribute("legal"));
    }

    if (details->hasAttribute("other_notes"))
    {
        stream->writeTextElement("h2", "Other Notes");
        stream->writeTextElement("p", details->attribute("other_notes"));
    }
}




#ifdef THREADED
static void write_topics(QList<XmlElement*> list, int first, int last)
{
    //qDebug() << "THREAD for" << first << "to" << last << ".";
    for (auto it = first; it < last; ++it)
    {
        //qDebug() << "topic" << it;
        writeTopicFile(list.at(it));
    }
}
#endif


static void write_child_topics(XmlElement *parent)
{
    QList<XmlElement*> children = parent->findChildren<XmlElement*>("topic", Qt::FindDirectChildrenOnly);
    int last = children.count()-1;
    for (int pos=0; pos<children.count(); pos++)
    {
        write_topic_file(children[pos],
                         /*up*/parent,
                         /*prev*/(pos>0) ? children[pos-1] : nullptr,
                         /*next*/(pos<last) ? children[pos+1] : nullptr);
        write_child_topics(children[pos]);
    }
}


/**
 * @brief toHtml
 * Generate HTML 5 (XHTML) representation of the supplied XmlElement tree
 * @param path
 * @param root_elem
 * @param max_image_width
 * @param separate_files
 * @param use_reveal_mask
 * @param index_on_every_page
 */
void toHtml(const QString &path,
            const XmlElement *root_elem,
            int max_image_width,
            bool separate_files,
            bool use_reveal_mask,
            bool index_on_every_page)
{
#ifdef TIME_CONVERSION
    QElapsedTimer timer;
    timer.start();
#endif

    image_max_width   = max_image_width;
    apply_reveal_mask = use_reveal_mask;
    always_show_index = index_on_every_page;
    in_single_file    = !separate_files;
    collator.setNumericMode(true);

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
    category_pin_of_topic.clear();

    // To help get category for pins on each individual topic,
    // get the topic_id of every single topic in the file.
    for (auto topic: root_elem->findChildren<XmlElement*>("topic"))
    {
        all_topics.insert(topic->attribute("topic_id"), topic);
    }

    // Write out the individual TOPIC files now:
    // Note use of findChildren to find children at all levels,
    // whereas xmlChildren returns only direct children.
    if (separate_files)
    {
        write_support_files();
        write_separate_index(root_elem);

        // A separate file for every single topic
#ifdef THREADED
        auto topics = root_elem->findChildren<XmlElement*>("topic");
        // This method speeds up the output of multiple files by creating separate
        // threads to handle each CHUNK of topics.
        // (Unfortunately, it only takes 4 seconds to write out the S&S campaign,
        //  so reducing this to 2.3 seconds doesn't help that much.)
        // Although on the first run, it is a lot slower (presumably due to O/S
        // not caching the files originally.
        unsigned max_threads = 32; //std::thread::hardware_concurrency();
        qDebug() << "Concurrency =" << max_threads;
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
        write_child_topics(root_elem->findChild<XmlElement*>("contents"));
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
        OurFile single_file(path);
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

        write_first_page(&stream, root_elem);

        // All topics in a single file, grouped by category,
        // nesting child topics inside the parent topic.
        auto children = contents->xmlChildren("topic");
        std::sort(children.begin(), children.end(), sort_all_topics);

        for (auto topic : children)
        {
            stream.writeStartElement("details");
            stream.writeAttribute("class", "mainTopic");
            stream.writeAttribute("open", "open");
            write_topic_body(&stream, "summary", topic, /*allinone*/ true);
            stream.writeEndElement();  // details
        }

        // Write footer for single file
        // Complete the individual topic file
        stream.writeEndElement(); // body
        stream.writeEndElement(); // html
    }

#ifdef TIME_CONVERSION
    qInfo() << "TIME TO GENERATE HTML =" << timer.elapsed() << "milliseconds";
#endif
}
