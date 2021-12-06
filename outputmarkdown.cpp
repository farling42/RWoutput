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

#define TIME_CONVERSION
#define IGNORE_GUMBO_TABLE
#undef  GUMBO_TABLE

#include "outputmarkdown.h"

#include <QBitmap>
#include <QBuffer>
#include <QCollator>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QApplication>
#include <QStyle>
#include <QStaticText>
#include <future>
#include <QDateTime>
#ifdef TIME_CONVERSION
#include <QElapsedTimer>
#endif

#include "gumbo.h"
#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

#include "xmlelement.h"
#include "linefile.h"
#include "linkage.h"

static bool apply_reveal_mask = true;
static bool sort_by_prefix = true;
static QCollator collator;
static bool category_folders = true;
static bool obsidian_links = false;
static bool show_leaflet_pins = true;
static bool show_nav_panel = true;
static bool nav_at_start = true;
static int  max_index_level = 99;

#undef DUMP_CHILDREN

static const QStringList predefined_styles = { "Normal", "Read_Aloud", "Handout", "Flavor", "Callout" };
static QHash<QString /*style string*/ ,QString /*replacement class name*/> class_of_style;
static QHash<QString,QString> topic_files;   // key=topic_id, value=public_name
static QHash<const XmlElement*,QString> topic_full_name;

extern QString map_pin_title;
extern QString map_pin_description;
extern QString map_pin_gm_directions;

extern bool show_full_link_tooltip;
extern bool show_full_map_pin_tooltip;

static QString assetsDir("asset-files");
static QString imported_date;
static QString mainPageName;
static const QString newline("\n");
static const int newlinelen = newline.length();

#define DUMP_LEVEL 0

static inline const QString validFilename(const QString &string)
{
    // full character list from https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    static const QRegExp invalid_chars("[<>:\"/\\|?*]");
    QString result(string);
    return result.replace(invalid_chars,"_");
}


static inline const QString validTag(const QString &string)
{
    // Tag can only contain letters (case sensitive), digits, underscore and dash
    // "/" is allowed for nested tags
    QString result = string;
    // First replace is for things like "Region: Geographical", renaming it to just "Region-Geographical" rather than "Region--Geographical"
   // return result.replace(": ","-").replace(QRegExp("[^\\w-]"), "-");
    return result.replace(QRegExp("[^\\w-]"), "-");
}


static const QString dirFile(const QString &dirname, const QString &filename)
{
    // Each component of dirname must already have been processed by validFilename
    QDir dir(dirname);
    if (!dir.exists()) {
        //qDebug() << "Creating directory: " << dirname;
        if (!QDir::current().mkpath(dirname)) {
            qWarning() << "Failed to create directory: " << dirname;
            // Store at the top level instead
            return filename;
        }
    }
    return dirname + QDir::separator() + validFilename(filename);
}


static const QString parentDirName(const XmlElement *topic)
{
    XmlElement *parent = qobject_cast<XmlElement*>(topic->parent());
    if (parent && parent->objectName() == "topic")
        return parentDirName(parent) + QDir::separator() + topic_files.value(parent->attribute("topic_id"));
    else
        return validFilename(topic->attribute("category_name"));
}


static const QString topicDirFile(const XmlElement *topic)
{
    // If the topic has children, then create a folder named after this note,
    // and put the note in it.
    // This is a parent topic, so create a folder to hold it.
    const QString filename = topic_files.value(topic->attribute("topic_id"));
    if (category_folders)
    {
        return dirFile(validFilename(topic->attribute("category_name")), filename) + ".md";
    }
    else
    {
        QString dirname = parentDirName(topic);
        if (topic->xmlChild("topic"))       // has at least one child
            dirname += QDir::separator() + filename;
        return dirFile(dirname, filename) + ".md";
    }
}


// Sort topics, first by prefix, and then by topic name
static bool sort_topics(const XmlElement *left, const XmlElement *right)
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
    return collator.compare(topic_full_name.value(left), topic_full_name.value(right)) < 0;
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
    QStringList files{"obsidian.css"};

    for (auto filename : files)
    {
        QFile destfile(filename);
        if (destfile.exists()) destfile.remove();
        QFile::copy(":/" + filename, destfile.fileName());
        // Qt copies the file and makes it read-only!
        destfile.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    }

    QFile styles("obsidian.css");
    if (styles.open(QFile::WriteOnly|QFile::Text|QFile::Append))
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


static const QString write_meta_child(const QString &meta_name, const XmlElement *details, const QString &child_name)
{
    QString result;
    XmlElement *child = details->xmlChild(child_name);
    if (!child) return "";
    return "**" + meta_name + ":** " + child->childString() + "\n\n";
}

static const QString write_head_meta(const XmlElement *root_elem)
{
    QString result;
    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    if (details == nullptr) return result;

    // From "2021-12-04T17:51:29Z" (ISO 8601) to something readable
    QDateTime stamp = QDateTime::fromString(root_elem->attribute("export_date"), Qt::ISODate);
    result += "**Exported from Realm Works:** " + stamp.toString(QLocale::system().dateTimeFormat()) + "\n\n";

    result += "**Created By:** RWOutput Tool v" + qApp->applicationVersion() + " on " + imported_date + "\n\n";

    result += write_meta_child("Summary",      details, "summary");
    result += write_meta_child("Description",  details, "description");
    result += write_meta_child("Requirements", details, "requirements");
    result += write_meta_child("Credits",      details, "credits");
    result += write_meta_child("Legal",        details, "legal");
    result += write_meta_child("Other Notes",  details, "other_notes");
    return result;
}


static QString doEscape(const QString &original)
{
    QString result = original;
    return result.replace("[","\\[");
}


/**
 * @brief build_tooltip
 * Builds the complete string for a popup tooltip.
 *
 * @param title
 * @param description
 * @param gm_directions
 * @return
 */
static inline QString build_tooltip(const QString &title, const QString &description, const QString &gm_directions)
{
    QString result;

    if (!title.isEmpty())
    {
        if (description.isEmpty() && gm_directions.isEmpty())
            result = title;
        else
            result += map_pin_title.arg(title);
    }
    if (!description.isEmpty())
    {
        if (!result.isEmpty()) result += newline;
        result += map_pin_description.arg(description);
    }
    if (!gm_directions.isEmpty())
    {
        if (!result.isEmpty()) result += newline;
        result += map_pin_gm_directions.arg(gm_directions);
    }
    return result;
}


static QString internal_link(const QString &topic_id, const QString &label = QString())
{
    QString filename = topic_files.value(topic_id);
    if (filename.isEmpty()) filename = validFilename(topic_id);

    if (obsidian_links)
    {
        if (label.isEmpty() || filename == label)
            return QString("[[%1]]").arg(filename);
        else
            return QString("[[%1|%2]]").arg(filename, label);
    }
    else
    {
        if (label.isEmpty() || filename == label)
            return QString("[%1]").arg(filename);
        else
            return QString("[%1](%2)").arg(filename, label);
    }
}

static QString topic_link(const XmlElement *topic)
{
    return internal_link(topic->attribute("topic_id"), topic_full_name.value(topic));
}

static const QString write_span(XmlElement *elem, const LinkageList &links)
{
    QString result;
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
            result += internal_link(link_text, doEscape(text)) + " ";  // adjacent links don't have spaces between them!
        else
            result += doEscape(text);
    }
    else
    {
        // Only put in span if we really require it
        // All sorts of HTML can appear inside the text
        for (auto child: elem->xmlChildren())
        {
            result += write_span(child, links);
        }
    }
    return result;
}


static const QString hlabel(const QString &label)
{
    return "**" + label + "**: ";
}


static const QString write_para(XmlElement *elem, const LinkageList &links, const QString &orig_listtype)
{
    QString result;
    QString listtype = orig_listtype;
#if DEBUG_LEVEL > 5
    qDebug() << "....paragraph";
#endif
    if (elem->isFixedString())
    {
        return write_span(elem, links);
    }

    bool close = false;
    QString sntype = elem->objectName();
    if (sntype == "snippet")
        ;
    else if (sntype == "p")
        result += newline;
    else if (sntype == "ul")
        listtype = "\n- ";
    else if (sntype == "ol")
        listtype = "\n+ ";
    else if (sntype == "li")
        result += listtype;
    else if (sntype == "table" || sntype == "tbody" || sntype == "tr" || sntype == "td")
    {
        // Always inline table elements
        result += "<" + elem->objectName() + ">";
        close=true;
    }
    else
    {
        qDebug() << "write_para: element = " << sntype;
        //result += "<" + elem->objectName() + ">";
        //close=true;
    }
    for (auto child : elem->xmlChildren())
    {
        if (child->objectName() == "span")
            result += write_span(child, links);
        else if (child->objectName() != "tag_assign")
            // Ignore certain children
            result += write_para(child, links, listtype);
    }
    // Anything to put AFTER the child elements?
    if (sntype == "snippet" || sntype == "p" || sntype == "ul" || sntype == "ol")
        result += newline;
    else if (close)
        result += "</" + elem->objectName() + ">";

    return result;
}


static const QString write_para_children(XmlElement *parent, const LinkageList &links)
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children";
#endif
    QString result;
    for (auto child: parent->xmlChildren())
    {
        result += write_para(child, links, QString());
    }
    // Remove leading/trailing white space, which includes line breaks
    return result.trimmed();
}


static QString get_elem_string(XmlElement *elem)
{
    if (!elem) return QString();

    // RW puts \r\n as a line terminator, rather than just \n
    return elem->childString().replace("&#xd;\n","\n");
}


static QString mapCoord(int coord)
{
    return QString::number(coord / 10.0, 'f', 1);
}


/*
 * Return the divisor for the map's size
 */

static const QString write_image(const QString &image_name, const QByteArray &orig_data, XmlElement *mask_elem,
                       const QString &orig_filename, const QString &annotation,
                       const QString &usemap = QString(), const QList<XmlElement*> pins = QList<XmlElement*>())
{
    Q_UNUSED(usemap)
    QBuffer buffer;
    QString filename = orig_filename;
    QString format = filename.split(".").last();
    int divisor = 1;
    const int pin_size = 20;
    bool in_buffer = false;

    // Need to decode image, to get size
    QImage image = QImage::fromData(orig_data, qPrintable(format));
    QSize image_size = image.size();

    // See if possible image conversion is required
    bool bad_format = (format == "bmp" || format == "tif" || format == "tiff");
    if (mask_elem != nullptr || bad_format)
    {
        if (bad_format)
        {
            format = "png";
            in_buffer = true;
            int last = filename.lastIndexOf(".");
            filename = filename.mid(0,last) + ".png";
        }

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
                qWarning() << "Image size differences for" << orig_filename << ": image =" << image.size() << ", mask =" << mask.size();
            }
            // Apply the mask to the original picture
            QPainter painter(&image);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.drawPixmap(0, 0, pixmap);

            //format = "png";   // not always better (especially if was JPG)
            in_buffer = true;
        } /* mask */

        // Do we need to put it in the buffer?
        if (in_buffer)
        {
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, qPrintable(format));
            buffer.close();
            in_buffer = true;
        }
    }

    // Put it into a separate file
    QFile file(dirFile(assetsDir, filename));
    if (!file.open(QFile::WriteOnly))
    {
        qWarning() << "writeExtObject: failed to open file for writing:" << filename;
        return 0;
    }
    file.write (in_buffer ? buffer.data() : orig_data);
    file.close();

    QString result;
    if (!image_name.isEmpty()) result += "### " + image_name;

    if (show_leaflet_pins && !pins.isEmpty())
    {
        int height = image_size.height();
        // The leaflet plugin for Obsidian uses latitude/longitude, so (y,x)
        result += "\n```leaflet\n";
        result += "id: " + image_name + newline;
        result += "image: [[" + filename + "]]\n";
        if (height > 1500) result += "height: " + mapCoord(height * 2) + "px\n";   // double the scaled height seems to work
        result += "draw: false\n";
        result += "showAllMarkers: true\n";
        result += "bounds:\n";
        result += "    - [0, 0]\n";
        result += "    - [" + mapCoord(height) + ", " + mapCoord(image_size.width()) + "]\n";

        // Create the clickable MAP on top of the map
        for (auto pin : pins)
        {
            int x = pin->attribute("x").toInt() / divisor;
            int y = pin->attribute("y").toInt() / divisor - pin_size;

            // Build up a tooltip from the text configured on the pin
            QString pin_name = pin->attribute("pin_name");
            QString description = get_elem_string(pin->xmlChild("description"));
            QString gm_directions = get_elem_string(pin->xmlChild("gm_directions"));
            QString link = pin->attribute("topic_id");
            QString tooltip = build_tooltip(pin_name, description, gm_directions);

            result += "marker: default, " + mapCoord(height-y) + ", " + mapCoord(x) + ",";
            if (link.isEmpty())
            {
                // No link, but explicit tooltip
                result += ",unknown";
            }
            else
            {
                result += internal_link(link);
                //if (tooltip != link_name) result += ", \"" + tooltip.replace("\"","'") + "\"";
            }
            result += newline;
        }
        result += "```\n";
    }
    else
    {
        // No pins required
        result += "![" + image_name + "!](" + filename.replace(" ","%20") + ")\n";
        // Create a link to open the file externally, either using the annotation as a link, or just a hard-coded string
        if (!annotation.isEmpty())
            result += "[" + annotation + "](" + filename.replace(" ","%20") + ")\n";
        else
            result += "[open outside](" + filename.replace(" ","%20") + ")\n";
    }

    return result;
}


static const QString write_ext_object(const QString &obj_name, const QByteArray &data, const QString &filename, const QString &annotation)
{
    Q_UNUSED(obj_name)

    QString result;
    // Write the asset data to an external file
    QFile file(dirFile(assetsDir, filename));
    if (!file.open(QFile::WriteOnly))
    {
        qWarning() << "writeExtObject: failed to open file for writing:" << filename;
        return result;
    }
    file.write (data);
    file.close();

    if (!obj_name.isEmpty()) result += "### " + obj_name;
    QString valid_filename = filename;
    valid_filename.replace(" ","%20");

    result += "![" + filename + "!](" + valid_filename + ")\n";

    // Create a link to open the file externally, either using the annotation as a link, or just a hard-coded string
    if (!annotation.isEmpty())
        result += "[" + annotation + "](" + valid_filename + ")\n";
    else
        result += "[open outside](" + valid_filename + ")\n";

    return result;
}


static const QString getTags(const XmlElement *node, bool wrapped=true)
{
    auto tag_nodes = node->xmlChildren("tag_assign");
    if (tag_nodes.length() == 0) return "";

    QStringList tags;
    for (auto tag : tag_nodes)
    {
        QString tag_name    = tag->attribute("tag_name");
        QString domain_name = tag->attribute("domain_name");
        // Don't include:
        //   Export/<any tag>  <- always present on every single topic during export.
        //   Utility/Empty     <- auto-added by Realm Works durin quick topic creation
        if (!domain_name.isEmpty() &&
            domain_name != "Export" &&
            (domain_name != "Utility" || tag_name != "Empty)"))
        {
            tags.append("#" + validTag(domain_name) + "/" + validTag(tag_name));
        }
    }
    if (tags.length() == 0)
        return "";
    else if (wrapped)
        return tags.join(" ") + newline;
    else
        return tags.join(" ");
}


/**
 * @brief output_gumbo_children
 * Takes the GUMBO tree and calls the relevant methods of QTextStream
 * to reproduce it in Markdown.
 * @param node
 */

static const QString output_gumbo_children(const GumboNode *parent, bool top=false)
{
    QString result;
    Q_UNUSED(top)
    GumboNode **children = reinterpret_cast<GumboNode**>(parent->v.element.children.data);
    bool term_img=false;
    for (unsigned count = parent->v.element.children.length; count > 0; --count)
    {
        const GumboNode *node = *children++;
        switch (node->type)
        {
        case GUMBO_NODE_TEXT:
        case GUMBO_NODE_CDATA:
        {
            QString text(node->v.text.text);
            //if (top) qDebug() << "GUMBO_NODE_TEXT:" << text;
            int pos = text.lastIndexOf(" - created with Hero Lab");
            if (pos >= 0) text = text.left(pos);
            // Escape any text in square brackets
            result += text.replace("[","\\[");
        }
            break;
        case GUMBO_NODE_COMMENT:
            //stream->writeComment(node->v.text.text);
            //if (top) qDebug() << "GUMBO_NODE_COMMENT:" << node->v.text.text;
            break;

        case GUMBO_NODE_ELEMENT:
        {
            const QString tag = gumbo_normalized_tagname(node->v.element.tag);
            bool close = false;
            QString href;
#if 1
            // See what to put before the text
            if (tag == "p")
                result += newline;
            else if (tag == "br")
                result += "\n\n";
            else if (tag == "b")
                result += "**";
            else if (tag == "i")
                result += "*";
            else if (tag == "hr")
                result += "\n---\n\n";
            else if (tag == "a") {
                GumboAttribute **attributes = reinterpret_cast<GumboAttribute**>(node->v.element.attributes.data);
                for (unsigned count = node->v.element.attributes.length; count > 0; --count)
                {
                    const GumboAttribute *attr = *attributes++;
                    //qDebug() << "GUMBO: a with attribute = " << attr->name;
                    if (QString(attr->name) == "href")
                        href = attr->value;
                    // what about style?
                }
                result += "[";
            }
            else if (tag == "img")
            {
                QString src, alt;
                GumboAttribute **attributes = reinterpret_cast<GumboAttribute**>(node->v.element.attributes.data);
                for (unsigned count = node->v.element.attributes.length; count > 0; --count)
                {
                    const GumboAttribute *attr = *attributes++;
                    if (QString(attr->name) == "src")
                        src = attr->value;
                    else if (QString(attr->name) == "alt")
                        alt = attr->value;
                    // what about width and height and style?
                }
                if (alt.isEmpty()) alt = src;
                if (src.startsWith("data:"))
                {
                    // Write out the link to an assets-file.
                    // data:image/jpeg;base64,...lots of data...
                    int base64 = src.indexOf(";base64,");
                    if (base64 > 0)
                    {
                        static int gumbofilenumber = 0;
                        QByteArray buffer = QByteArray::fromBase64(src.mid(base64+8).toLatin1());
                        int slash = src.indexOf("/");
                        QString extension = src.mid(slash+1, base64-slash-1);
                        QString filename = QString("gumbodatafile%1.%2").arg(gumbofilenumber++).arg(extension);
                        result += write_ext_object("", buffer, filename, /*annotation*/ QString());
                    }
                }
                else
                {
                    result +="![" + alt + "!](" + src + " \"";
                    term_img = true;
                }
            }
#ifdef IGNORE_GUMBO_TABLE
            else if (tag == "tr" || tag == "table" || tag == "tbody" || tag == "td")
                ;
#else
#ifndef GUMBO_TABLE
            // Table support - just use normal HTML
            else if (tag == "tr")
                result += newline;
            else if (tag == "td")
                result += "|";
            else if (tag == "table" || tag == "tbody")
                ;  // do nothing with spans (we might need to get any style from it
#endif
#endif
            else if (tag == "span")
                ;  // do nothing with spans (we might need to get any style from it
            else if (tag == "sup" || tag == "sub") {
                result += "<" + tag + ">";
                close=true;
            }
            else if (tag == "span")
                ; // ignore span
            else {
                result += "<" + tag + ">";
                close=true;
                qDebug() << "GUMBO_NODE_ELEMENT(start): unsupported = " << tag;
            }
#else
            stream->writeStartElement(tag);
            //if (top) qDebug() << "GUMBO_NODE_ELEMENT:" << tag;
            GumboAttribute **attributes = reinterpret_cast<GumboAttribute**>(node->v.element.attributes.data);
            for (unsigned count = node->v.element.attributes.length; count > 0; --count)
            {
                const GumboAttribute *attr = *attributes++;
                stream->writeAttribute(attr->name, attr->value);
            }
#endif
            result += output_gumbo_children(node);

            // Now, see if we need to terminate this node
            if (tag == "p")
                result += newline;
            else if (tag == "b")
                result += "**";
            else if (tag == "i")
                result += "*";
            else if (tag == "a")
                result += "](" + href + ")";
            else if (tag == "img" && term_img)
            {
                result += "\")";
                term_img = false;
            }
            else if (tag == "span")
                ;  // ignore span
#ifdef IGNORE_GUMBO_TABLE
            else if (tag == "tr" || tag == "table" || tag == "tbody")
                ;
            else if (tag == "td")
                result += newline;    // normally the end of a row
#else
#ifndef GUMBO_TABLE
            else if (tag == "tr")
                result += "|\n";   // end of line for table row
            else if (tag == "table")
                result += "\n\n";
#endif
#endif
            else if (close)
                result += "</" + tag + ">";
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
    return result;
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
    qDebug() << "   dump_children:" << from << gumbo_normalized_tagname(parent->v.element.tag) << "has children" << list.join(", ");
}
#endif

static const QString write_html(bool use_fixed_title, const QString &sntype, const QByteArray &data)
{
    QString result;

    // Put the children of the BODY into this frame.
    GumboOutput *output = gumbo_parse(data);
    if (output == nullptr) return result;

#ifdef DUMP_CHILDREN
    dump_children("ROOT", output->root);
#endif

    const GumboNode *head = find_named_child(output->root, "head");
    const GumboNode *body = find_named_child(output->root, "body");

#ifdef DUMP_CHILDREN
    dump_children("HEAD", head);
#endif

    result += "\n---\n\n# " + sntype;
    if (use_fixed_title)
    {
        // Nothing else in the header
        result += newline;
    }
    else
    {
        const GumboNode *title = head ? find_named_child(head, "title") : nullptr;
        // title should only be text
        if (title) result += ": " + output_gumbo_children(title) + newline;
    }

    // Maybe we have a CSS that we can put inline.
#ifdef HANDLE_POOR_RTF
    const GumboNode *style = find_named_child(head, "style");
    if (!style) style = get_gumbo_child(body, "style");
#endif

#if 0
    if (style)
    {
        result += "\n```\ngumbo-style: {";
        result += output_gumbo_children(style);  // it should only be text
        result += "}\n```\n\n";
    }
#endif
    if (body)
    {
#ifdef DUMP_CHILDREN
        dump_children("BODY", body);
#endif
        result += output_gumbo_children(body, /*top*/true);
    }

    // Get GUMBO to release all the memory
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return result;
}


static inline const QString annotationText(XmlElement *snippet, const LinkageList &links, bool wrapped = true)
{
    XmlElement *annotation = snippet->xmlChild("annotation");
    if (!annotation) return "";

    // Remove newline from either end of paragraph
    QString result = write_para_children(annotation, links);
    if (!wrapped)
        return result;
    else
        return " ; " + result;
}


static const QString write_snippet(XmlElement *snippet, const LinkageList &links)
{
    QString result;
    QString sn_type = snippet->attribute("type");
    QString sn_style = snippet->attribute("style"); // Read_Aloud, Callout, Flavor, Handout
#if DUMP_LEVEL > 3
    qDebug() << "...snippet" << sn_type;
#endif

    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        if (auto gm_directions = snippet->xmlChild("gm_directions"))
            result += write_para_children(gm_directions, links);

        if (auto contents = snippet->xmlChild("contents"))
            result += write_para_children(contents, links);

        // Multi_Line has no annotation
        result += newline + getTags(snippet) + newline;
    }
    else if (sn_type == "Labeled_Text")
    {
        if (auto contents = snippet->xmlChild("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            // Remove newline from either end of paragraph
            result += hlabel(snippet->snippetName()) + write_para_children(contents, links);  // has its own 'p'
        }
        // Labeled_Text has no annotation
        result += newline + getTags(snippet) + newline;
    }
    else if (sn_type == "Portfolio")
    {
        // As for other EXT OBJECTS, but with unzipping involved to get statblocks
        if (auto ext_object = snippet->xmlChild("ext_object"))
        {
            if (auto asset = ext_object->xmlChild("asset"))
            {
                QString filename = asset->attribute("filename");
                XmlElement *contents = asset->xmlChild("contents");
                if (contents)
                {
                    result += write_ext_object(ext_object->attribute("name"), contents->byteData(), filename, annotationText(snippet, links, false));
                    result += getTags(snippet) + newline;

                    // Put in markers for statblock
                    QByteArray store = contents->byteData();
                    QBuffer buffer(&store);
                    QuaZip zip(&buffer);
                    if (zip.open(QuaZip::mdUnzip))
                    {
                        // Need to convert this HTML into markup
                        for (bool more=zip.goToFirstFile(); more; more=zip.goToNextFile())
                        {
                            if (zip.getCurrentFileName().startsWith("statblocks_html/"))
                            {
                                QuaZipFile file(&zip);
                                if (!file.open(QuaZipFile::ReadOnly))
                                    qWarning() << "Failed to open file from zip: " << zip.getCurrentFileName();
                                else
                                {
                                    QString temp = write_html(false, sn_type, file.readAll());
                                    if (temp.isEmpty()) qWarning() << "GUMBO failed to parse" << zip.getCurrentFileName();
                                    result += temp;
                                }
                            }
                        }
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
        if (auto ext_object = snippet->xmlChild("ext_object"))
        {
            if (auto asset = ext_object->xmlChild("asset"))
            {
                if (auto contents = asset->xmlChild("contents"))
                {
                    QString filename = asset->attribute("filename");
                    auto annotation = annotationText(snippet, links, false);
                    if (sn_type == "Picture")
                    {
                        result += write_image(ext_object->attribute("name"), contents->byteData(), /*mask*/nullptr, filename, annotation);
                    }
                    else if (filename.endsWith(".html") ||
                             filename.endsWith(".htm")  ||
                             filename.endsWith(".rtf"))
                    {
                        result += write_ext_object(ext_object->attribute("name"), contents->byteData(), filename, annotation);
                        result += write_html(true, sn_type, contents->byteData());
                    }
                    else
                    {
                        result += write_ext_object(ext_object->attribute("name"), contents->byteData(), filename, annotation);
                    }
                    result += getTags(snippet) + newline;
                }
            }
        }
    }
    else if (sn_type == "Smart_Image")
    {
        // ext_object child, asset grand-child
        if (auto smart_image = snippet->xmlChild("smart_image"))
        {
            XmlElement *asset = smart_image->xmlChild("asset");
            XmlElement *mask  = smart_image->xmlChild("subset_mask");
            if (asset == nullptr) return result;

            QString filename = asset->attribute("filename");
            XmlElement *contents = asset->xmlChild("contents");
            if (contents == nullptr) return result;

            QString usemap;
            QList<XmlElement*> pins = smart_image->xmlChildren("map_pin");
            if (!pins.isEmpty()) usemap = "map-" + asset->attribute("filename");

            result += write_image(smart_image->attribute("name"), contents->byteData(), mask, filename, annotationText(snippet, links, false), usemap, pins);
        }
        result += newline + getTags(snippet) + newline;
    }
    else if (sn_type == "Date_Game")
    {
        if (auto date = snippet->xmlChild("game_date"))
        {
            result += "**" + snippet->snippetName() + "**: " + date->attribute("display");
            result += annotationText(snippet, links) + newline + getTags(snippet) + newline;
        }
    }
    else if (sn_type == "Date_Range")
    {
        if (auto date = snippet->xmlChild("date_range"))
        {
            result += hlabel(snippet->snippetName()) + "From: " + date->attribute("display_start") + " To: " + date->attribute("display_end");
            result += annotationText(snippet, links) + newline + getTags(snippet) + newline;
        }
    }
    else if (sn_type == "Tag_Standard")
    {
        QStringList tags;
        for (auto tag: snippet->xmlChildren("tag_assign"))
            tags.append(tag->attribute("tag_name"));
        if (tags.length() > 0)
        {
            // In non-tag text before showing all connected tags
            result += hlabel(snippet->snippetName()) + tags.join(", ");
            result += annotationText(snippet, links) + newline + getTags(snippet) + newline;
        }
    }
    else if (sn_type == "Numeric")
    {
        if (auto contents = snippet->xmlChild("contents"))
        {
            result += hlabel(snippet->snippetName()) + contents->childString();
            result += annotationText(snippet, links) + newline + getTags(snippet) + newline;
        }
    }
    else if (sn_type == "Tag_Multi_Domain")
    {
        QString tags = getTags(snippet, /*wrapped*/false);   // tags will be on the same line as this snippet
        if (!tags.isEmpty())
        {
            result += hlabel(snippet->snippetName()) + tags;
            result += annotationText(snippet, links) + newline + newline;
        }
    }
    // Hybrid_Tag

    return result;
}


static const QString write_section(XmlElement *section, const LinkageList &links, int level)
{
    QString result;
#if DUMP_LEVEL > 2
    qDebug() << "..section" << section->attribute("name");
#endif

    // Start with HEADER for the section (H1 used for topic title)
    result += newline + QString(level+1,'#') + ' ' + section->attribute("name") + newline;

    // Write snippets
    for (auto snippet: section->xmlChildren("snippet"))
    {
        result += write_snippet(snippet, links);
    }

    // Write following sections
    for (auto subsection: section->xmlChildren("section"))
    {
        result += write_section(subsection, links, level+1);
    }
    return result;
}

/**
 * @brief startFile
 * Opens the file, and fills in the first parts of the Obsidian Frontmatter.
 */
static void startFile(QTextStream &stream)
{
    stream.setCodec("UTF-8");

    // The first part of the FRONTMATTER
    stream << "---\n";
    stream << "ImportedOn: " << imported_date << newline;

}

/**
 * @brief write_link
 * Writes one of the navigation links that might appear in the footer of each page
 * @param topic
 * @param override
 */
static QString nav_link(const XmlElement *topic, const QString &override = QString())
{
    QString result;
    if (topic && topic->objectName() == "topic")
        result = topic_link(topic);
    else if (!override.isEmpty())
        result = internal_link(override);
    else
        result = "---";
    // Escape vertical bars to work with table format
    return result.replace("|","\\|");
}

static const QMap<QString,QString> nature_mapping{
    { "Arbitrary", "Arbitrary Connection To"},
    { "Generic",   "Simple Connection To"},
    { "Union",     "Family Relationship To-Union With" },
    { "Parent_To_Offspring", "Family Relationship To-Immediate Ancestor Of"},
    { "Offspring_To_Parent", "Family Relationship To-Offspring Of" },
    { "Master_To_Minion", "Comprises Or Encompasses" },
    { "Minion_To_Master", "Belongs To Or Within" },
    { "Public_Attitude_Towards", "Public Attitude Towards" },
    { "Private_Attitude_Towards", "Private Attitude Towards" }
};

static QString relationship(XmlElement *connection)
{
    QString nature    = connection->attribute("nature");
    QString qualifier = connection->attribute("qualifier");
    QString attitude  = connection->attribute("attitude");
    QString rating    = connection->attribute("rating");

    QString result = nature_mapping.value(nature);
    if (result.isEmpty()) result = "Unknown Relationship";

    if (!qualifier.isEmpty())
    {
        if (nature == "Master_To_Minion")
        {
            auto quals = qualifier.split(" / ");
            if (quals.length() > 1) qualifier = quals[0];
        }
        else if (nature == "Minion_To_Master")
        {
            auto quals = qualifier.split(" / ");
            if (quals.length() > 1) qualifier = quals[1];
        }
        result += "-" + qualifier;
    }
    else if (!attitude.isEmpty())
        result += "-" + attitude;
    else if (!rating.isEmpty())
        result += "-" + rating;

    return result;
}

static void write_topic_file(const XmlElement *topic, const XmlElement *parent, const XmlElement *prev, const XmlElement *next)
{
#if DUMP_LEVEL > 1
    qDebug() << ".topic" << topic->objectName() << ":" << topic_names.value(topic);
#endif
    QString category_name = topic->attribute("category_name");

    // Create a new file for this topic
    QFile topic_file(topicDirFile(topic));
    if (!topic_file.open(QFile::WriteOnly|QFile::Text))
    {
        qWarning() << "Failed to open output file for topic" << topic_file.fileName();
        return;
    }

    // Switch output to the new stream.
    QTextStream topic_stream(&topic_file);
    QTextStream &stream = topic_stream;

    //
    // Start of FRONTMATTER
    //
    startFile(stream);

    // Aliases belong in the metadata at the start of the file
    auto aliases = topic->xmlChildren("alias");
    if (!aliases.isEmpty())
    {
        stream << "Aliases:\n";
        for (auto alias : aliases)
        {
            stream << "  - " << alias->attribute("name") << newline;
        }
    }
    stream << "Tags: Category/" << validTag(category_name) << newline;

    if (parent) {
        // Don't tell Breadcrumbs about the main page!
        QString link = (parent->objectName() == "topic") ? topic_files.value(parent->attribute("topic_id")) : validFilename(category_name);
        stream << "parent:\n  - " << link << "\nup:\n  - " << link << newline;
    }
    if (prev)
    {
        QString link = topic_files.value(prev->attribute("topic_id"));
        stream << "prev:\n  - " << link << newline;
    }
    if (next)
    {
        QString link = topic_files.value(next->attribute("topic_id"));
        stream << "next:\n  - " << link << newline;
        //stream << "same:\n  - " << link << newline;
    }
    const auto children = topic->xmlChildren("topic");
    if (children.length() > 0)
    {
        stream << "down:\n";
        for (auto child: topic->xmlChildren("topic"))
        {
            stream << "  - " << topic_files.value(child->attribute("topic_id")) << newline;
        }
    }
    stream << "RWtopicId: " << topic->attribute("topic_id") << newline;

    // Connections
    for (auto child : topic->xmlChildren("connection"))
    {
        // Remove spaces from tag
        stream << relationship(child).replace(" ", "") << ": " << internal_link(child->attribute("target_id"), child->attribute("target_name")) << newline;
    }
    stream << "---\n";

    //
    // End of FRONTMATTER
    //

    if (show_nav_panel && nav_at_start)
    {
        stream << "\n| Up | Prev | Next | Home |\n";
        stream << "|----|------|------|------|\n";
        stream << "| "  << nav_link(parent, category_name);  // If up is not defined, use the index file
        stream << " | " << nav_link(prev);
        stream << " | " << nav_link(next);
        stream << " | " << nav_link(nullptr, mainPageName);  // Always points to top
        stream << " |\n\n";
    }

    stream << "# " << topic_full_name.value(topic) << newline;

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
        stream << write_section(section, links, /*level*/ 1);
    }

    // Provide summary of links to child topics
    auto child_topics = topic->xmlChildren("topic");
    if (!child_topics.isEmpty())
    {
        stream << "\n\n---\n## Governed Content\n";

        std::sort(child_topics.begin(), child_topics.end(), sort_topics);
        for (auto child: child_topics)
        {
            stream << "- " << topic_link(child) << newline;
        }
    }

    auto connections = topic->xmlChildren("connection");
    if (connections.length() > 0)
    {
        stream << "\n\n---\n## Connections\n";
        for (auto connection : connections)
        {
            // Remove spaces from tag
            stream << relationship(connection) << ": " << internal_link(connection->attribute("target_id"), connection->attribute("target_name"));
            XmlElement *annot = connection->xmlChild("annotation");
            if (annot) stream << " ; " << annot->xmlChild()->fixedText();
            stream << newline;
        }
    }

    // If any tags are defined at the topic level, then add them now
    QString topic_tags = getTags(topic, /*wrapped*/ false);
    if (topic_tags.length() > 0)
    {
        stream << "\n\n---\n## Tags\n" << topic_tags << newline;
    }

    if (show_nav_panel && !nav_at_start)
    {
        //stream << "\n---\n";
        stream << "\n| Up | Prev | Next | Home |\n";
        stream << "|----|------|------|------|\n";
        stream << "| "  << nav_link(parent, category_name);  // If up is not defined, use the category
        stream << " | " << nav_link(prev);
        stream << " | " << nav_link(next);
        stream << " | " << nav_link(nullptr, mainPageName);  // Always points to top
        stream << " |\n";
    }
}

/*
 * Write entries into the INDEX file
 */

static void write_topic_to_index(QTextStream &stream, XmlElement *topic, int level)
{
    stream << QString(level * 2, ' ') << "- " << topic_link(topic) << newline;

    auto child_topics = topic->xmlChildren("topic");
    if (!child_topics.isEmpty())
    {
        std::sort(child_topics.begin(), child_topics.end(), sort_topics);
        if (level < max_index_level)
        {
            for (auto child_topic: child_topics)
            {
                write_topic_to_index(stream, child_topic, level+1);
            }
        }
    }
}


static void write_separate_index(const XmlElement *root_elem)
{
    if (root_elem->objectName() != "output") return;

    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    mainPageName           = details    ? details->attribute("name") : "Table of Contents";
    topic_files.insert(mainPageName, validFilename(mainPageName));

    QFile out_file(mainPageName + ".md");
    if (!out_file.open(QFile::WriteOnly|QFile::Text))
    {
        qWarning() << "Failed to find file" << out_file.fileName();
        return;
    }
    QTextStream stream(&out_file);


    //
    // start of FRONTMATTER
    //
    startFile(stream);
    stream << "---\n";
    //
    // end of FRONTMATTER
    //

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
                    stream << "\n# <center>" << header->attribute("name") << "</center>\n";
                }
            }
            stream << write_head_meta(root_elem);
        }
        else if (child->objectName() == "contents")
        {
            // A top-level button to collapse/expand the entire list
            const QString expand_all   = "Expand All";
            const QString collapse_all = "Collapse All";

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
                stream << "# " << internal_link(cat) << newline;

                // Organise topics alphabetically
                auto topics = categories.values(cat);
                std::sort(topics.begin(), topics.end(), sort_topics);

                for (auto topic: topics)
                {
                    write_topic_to_index(stream, topic, 0);
                }
            }
        }
    }
}


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


static void write_category_files(const XmlElement *tree)
{
    // Only use top-level topics - not nested topics
    XmlElement *contents = tree->xmlChild("contents");
    if (contents == nullptr)
    {
        qWarning() << "Failed to find contents node for generating category files";
        return;
    }

    QMultiMap<QString,XmlElement*> categories;
    for (auto topic: contents->xmlChildren("topic"))
    {
        categories.insert(topic->attribute("category_name"), topic);
    }
    QStringList category_names(categories.keys());
    category_names.removeDuplicates();
    category_names.sort();

    foreach (const auto catname, category_names)
    {
        QString filename = dirFile(validFilename(catname), validFilename(catname)) + ".md";
        QFile file(filename);
        // If it already exists, then don't change it;
#if 0
        if (file.exists())
        {
            qDebug() << "Placeholder file already exists for category " << catname;
            return;
        }
#endif
        if (!file.open(QFile::WriteOnly))
        {
            qWarning() << "writeExtObject: failed to open file for writing:" << filename;
            return;
        }
        QTextStream stream(&file);

        //
        // Start of FRONTMATTER
        //
        startFile(stream);

        // frontmatter for folder information
        stream << "up:\n  - " << mainPageName << newline;
        stream << "down:\n";
        auto topics = categories.values(catname);
        std::sort(topics.begin(), topics.end(), sort_topics);
        for (auto topic: topics)
        {
            if (topic->attribute("category_name") == catname)
            {
                stream << "  - " << topic_files.value(topic->attribute("topic_id")) << newline;
            }
        }
        stream << "same:\n";
        foreach (const QString other_cat, category_names)
        {
            if (other_cat != catname) stream << "  - " << validFilename(other_cat) << newline;
        }
        stream << "---\n";
        stream << "# " << catname << newline;
        file.close();
    }
}

/**
 * @brief toHtml
 * Generate Markdown representation of the supplied XmlElement tree
 * @param root_elem
 * @param create_leaflet_pins  Add interactive map with map pins using Leaflet plugin
 * @param use_reveal_mask
 * @param folders_by_category  IF true, stores pages in folders named after category; if false then store pages based on topic hierarchy
 * @param do_obsidian_links
 */
void toMarkdown(const XmlElement *root_elem,
                bool create_leaflet_pins,
                bool use_reveal_mask,
                bool folders_by_category,
                bool do_obsidian_links,
                bool create_nav_panel)
{
#ifdef TIME_CONVERSION
    QElapsedTimer timer;
    timer.start();
#endif
    Q_UNUSED(use_reveal_mask)
    apply_reveal_mask = false; //use_reveal_mask;
    category_folders  = folders_by_category;
    obsidian_links    = do_obsidian_links;
    show_leaflet_pins = create_leaflet_pins;
    show_nav_panel    = create_nav_panel;
    collator.setNumericMode(true);

    imported_date = QDateTime::currentDateTime().toString(QLocale::system().dateTimeFormat());

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
    int stylenumber=1;
    for (auto style: styles_set)
    {
        if (predefined_styles.contains(style))
            class_of_style.insert(style, style);
        else
            class_of_style.insert(style, QString("rwStyle%1").arg(stylenumber++));
    }

    // To help get category for pins on each individual topic,
    // get the topic_id of every single topic in the file.
    for (auto topic: root_elem->findChildren<XmlElement*>("topic"))
    {
        // Filename contains the FULL topic name including prefix and suffix
        QString fullname;
        const QString prefix = topic->attribute("prefix");
        const QString suffix = topic->attribute("suffix");
        if (!prefix.isEmpty()) fullname += prefix + " - ";
        fullname += topic->attribute("public_name");
        if (!suffix.isEmpty()) fullname += " (" + suffix + ")";
        topic_full_name.insert(topic, fullname);
        QString vfn = validFilename(fullname);
        if (vfn != fullname) qWarning() << "Filename (" << vfn << ") different for " << fullname;
        topic_files.insert(topic->attribute("topic_id"), validFilename(fullname));
    }

    // Write out the individual TOPIC files now:
    // Note use of findChildren to find children at all levels,
    // whereas xmlChildren returns only direct children.
    write_support_files();
    write_separate_index(root_elem);
    write_category_files(root_elem);

    // A separate file for every single topic
    write_child_topics(root_elem->findChild<XmlElement*>("contents"));

#ifdef TIME_CONVERSION
    qInfo() << "TIME TO GENERATE HTML =" << timer.elapsed() << "milliseconds";
#endif

    // Tidy up memory
    class_of_style.clear();
    topic_full_name.clear();
    topic_files.clear();
}
