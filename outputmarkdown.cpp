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

#if 1
typedef QFile OurFile;
#else
typedef LineFile OurFile;
#endif

#undef DUMP_CHILDREN

static const QStringList predefined_styles = { "Normal", "Read_Aloud", "Handout", "Flavor", "Callout" };
static QMap<QString /*style string*/ ,QString /*replacement class name*/> class_of_style;
static QMap<QString,XmlElement*> all_topics;
static QMap<QString,QString> topic_files;   // key=topic_id, value=public_name
static QMap<const XmlElement*,QString> topic_names;

extern QString map_pin_title;
extern QString map_pin_description;
extern QString map_pin_gm_directions;

extern bool show_full_link_tooltip;
extern bool show_full_map_pin_tooltip;

static QString assetsDir("asset-files");
static QString imported_date;
static QString mainPageName;

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
    return result.replace(": ","-").replace(QRegExp("[^\\w-]"), "-");
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
    // Invalid characters for file include *"/\<>:|?
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
    const QString filename = validFilename(topic_files.value(topic->attribute("topic_id")));
    if (category_folders)
    {
        return dirFile(validFilename(topic->attribute("category_name")), filename) + ".md";
    }
    else
    {
        QString dirname = parentDirName(topic);
        if (topic->xmlChild("topic"))
            dirname.append(QDir::separator() + filename);
        return dirFile(dirname, filename) + ".md";
    }
}


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
    return collator.compare(topic_names.value(left), topic_names.value(right)) < 0;
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


static void write_meta_child(QTextStream &stream, const QString &meta_name, const XmlElement *details, const QString &child_name)
{
    XmlElement *child = details->xmlChild(child_name);
    if (child)
    {
        stream << "**" << meta_name << ":** " << child->childString() << "\n\n";
    }

}

static void write_head_meta(QTextStream &stream, const XmlElement *root_elem)
{
    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    if (details == nullptr) return;

    stream << "**Exported from Realm Works:** " << root_elem->attribute("export_date") << "\n\n";

    stream << "**Created By:** RWOutput Tool v" << qApp->applicationVersion() << " on " << imported_date << "\n\n";

    write_meta_child(stream, "Summary",      details, "summary");
    write_meta_child(stream, "Description",  details, "description");
    write_meta_child(stream, "Requirements", details, "requirements");
    write_meta_child(stream, "Credits",      details, "credits");
    write_meta_child(stream, "Legal",        details, "legal");
    write_meta_child(stream, "Other Notes",  details, "other_notes");
}


static QString simple_para_text(XmlElement *p)
{
    // Collect all spans into a single paragraph (without formatting)
    // (get all nested spans; we can't use write_span here)
    QString result;
    const auto spans = p->findChildren<XmlElement*>("span");
    for (auto span : spans)
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


static QString annotationString(const XmlElement *annotation)
{
    //XmlElement *annotation = node->xmlChild("annotation");
    return annotation ? (" *; " + annotation->xmlChild()->fixedText() + "*") : "";
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
            result.append(map_pin_title.arg(title));
    }
    if (!description.isEmpty())
    {
        if (!result.isEmpty()) result.append("\n");
        result.append(map_pin_description.arg(description));
    }
    if (!gm_directions.isEmpty())
    {
        if (!result.isEmpty()) result.append("\n");
        result.append(map_pin_gm_directions.arg(gm_directions));
    }
    return result;
}

/**
 * Read first section from topic.
 *
 * First section - all Multi_Line snippet - contents/gm_directions - p - span
 */

static inline void get_summary(const QString &topic_id, QString &description, QString &gm_directions)
{
    // First section - all Multi_Line snippet - contents/gm_directions - p - span
    const XmlElement *topic = all_topics.value(topic_id);
    if (!topic) return;

    const XmlElement *section = topic->xmlChild("section");
    if (!section) return;

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

#if 0
static void write_attributes(QTextStream &stream, const XmlElement *elem, const QString &classname)
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
#endif

static QString internal_link(const QString &topic_id, const QString &public_name)
{
    QString label      = public_name;
    QString topic_file = topic_files.value(topic_id);
    if (topic_file.isEmpty()) topic_file = validFilename(topic_id);

    if (obsidian_links)
    {
        if (public_name.isEmpty() || topic_file == label)
            return QString("[[%1]]").arg(topic_file);
        else
            return QString("[[%1|%2]]").arg(topic_file, label);
    }
    else
    {
        if (public_name.isEmpty() || topic_file == label)
            return QString("[%1]").arg(topic_file);
        else
            return QString("[%1](%2)").arg(topic_file, label);
    }
}

static QString topic_link(const XmlElement *topic)
{
    return internal_link(topic->attribute("topic_id"), topic_names.value(topic));
}

static void write_span(QTextStream &stream, XmlElement *elem, const LinkageList &links, const QString &classname)
{
    Q_UNUSED(classname)
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
            stream << internal_link(link_text, text);
        else
            stream << text;
    }
    else
    {
        // Only put in span if we really require it
#if 0
        bool in_element = elem->objectName() != "span" || elem->hasAttribute("style") || !classname.isEmpty();
        if (in_element)
        {
            stream->writeStartElement(elem->objectName());
            write_attributes(stream, elem, classname);
        }
#endif
        // All sorts of HTML can appear inside the text
        for (auto child: elem->xmlChildren())
        {
            write_span(stream, child, links, QString());
        }
#if 0
        if (in_element) stream << "</" << elem->objectName() << ">";
#endif
    }
}


static void write_para(QTextStream &stream, XmlElement *elem, const QString &classname, const LinkageList &links,
                       const QString &label, const QString &bodytext, const QString &orig_listtype)
{
    QString listtype = orig_listtype;
#if DEBUG_LEVEL > 5
    qDebug() << "....paragraph";
#endif
    if (elem->isFixedString())
    {
        write_span(stream, elem, links, classname);
        return;
    }

    bool close = false;
    QString sntype = elem->objectName();
    if (sntype == "snippet" || sntype == "p")
        stream << "\n";
    else if (sntype == "ul")
        listtype = "\n- ";
    else if (sntype == "ol")
        listtype = "\n+ ";
    else if (sntype == "li")
        stream << listtype;
    else if (sntype == "table" || sntype == "tbody" || sntype == "tr" || sntype == "td")
    {
        // Always inline table elements
        stream << "<" << elem->objectName() << ">";
        close=true;
    }
    else
    {
        //// stream->writeStartElement(sntype);
        //// write_attributes(stream, elem, classname);
        qDebug() << "write_para: element = " << sntype;
        //stream << "<" << elem->objectName() << ">";
        //close=true;
    }
    // If there is no label, then set the class on the paragraph element,
    // otherwise we will put the text inside a span with the given class.
    bool class_set = false;
    if (!label.isEmpty())
    {
        stream << "**" << label << "**: ";
    }
    else if (!classname.isEmpty())
    {
        // class might already have been written by calling writeAttributes
        //if (elem->objectName() == "snippet") stream->writeAttribute("class", classname);
        class_set = true;
    }

    if (!bodytext.isEmpty())
    {
        stream << bodytext;
    }
    for (auto child : elem->xmlChildren())
    {
        if (child->objectName() == "span")
            write_span(stream, child, links, class_set ? QString() : classname);
        else if (child->objectName() != "tag_assign")
            // Ignore certain children
            write_para(stream, child, classname, links, /*no prefix*/QString(), QString(), listtype);
    }
    // Anything to put AFTER the child elements?
    if (sntype == "snippet" || sntype == "p" || sntype == "ul" || sntype == "ol")
        stream << "\n";
    else if (close)
        stream << "</" << elem->objectName() << ">";
}


static void write_para_children(QTextStream &stream, XmlElement *parent, const QString &classname, const LinkageList &links,
                                const QString &prefix_label = QString(), const QString &prefix_bodytext = QString())
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children";
#endif

    QString first_text = prefix_bodytext;
    if (classname.startsWith("annotation")) {
        if (!first_text.isEmpty()) first_text += "\n";
        first_text += "*annotation:* ";
    }
    bool first = true;
    for (auto para: parent->xmlChildren())
    {
        if (first)
            write_para(stream, para, classname, links, /*prefix*/prefix_label, first_text, QString());
        else
            write_para(stream, para, classname, links, /*no prefix*/QString(), QString(), QString());
        first = false;
    }
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

static int write_image(QTextStream &stream, const QString &image_name, const QByteArray &orig_data, XmlElement *mask_elem,
                       const QString &orig_filename, const QString &class_name, XmlElement *annotation, const LinkageList &links,
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

    if (show_leaflet_pins && !pins.isEmpty())
    {
        int height = image_size.height();
        // The leaflet plugin for Obsidian uses latitude/longitude, so (y,x)
        stream << "\n```leaflet\n";
        stream << "id: " << image_name << "\n";
        stream << "image: [[" << filename << "]]\n";
        if (height > 1500) stream << "height: " << mapCoord(height * 2) << "px\n";   // double the scaled height seems to work
        stream << "draw: false\n";
        stream << "showAllMarkers: true\n";
        stream << "bounds:\n";
        stream << "    - [0, 0]\n";
        stream << "    - [" << mapCoord(height) << ", " << mapCoord(image_size.width()) << "]\n";

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

            stream << "marker: default, " << mapCoord(height-y) << ", " << mapCoord(x) << ",";
            if (link.isEmpty())
            {
                // No link, but explicit tooltip
                stream << ",unknown";
            }
            else
            {
                stream << internal_link(link, "");
                //if (tooltip != link_name) stream << ", \"" << tooltip.replace("\"","'") << "\"";
            }
            stream << "\n";
        }
        stream << "```\n";
    }
    else
    {
        // No pins required
        stream << "![" << image_name << "!](" << filename.replace(" ","%20") << ")";
        if (annotation) {
            write_para_children(stream, annotation, "annotation " + class_name, links);
        }
        stream << "\n";
    }

    return divisor;
}


static void write_ext_object(QTextStream &stream, const QString &obj_name, const QByteArray &data,
                             const QString &filename, const QString &class_name, XmlElement *annotation, const LinkageList &links)
{
    Q_UNUSED(obj_name)
        // Write the asset data to an external file
        QFile file(dirFile(assetsDir, filename));
        if (!file.open(QFile::WriteOnly))
        {
            qWarning() << "writeExtObject: failed to open file for writing:" << filename;
            return;
        }
        file.write (data);
        file.close();

        QString temp = filename;
        stream << "![" << filename << "!](" << temp.replace(" ","%20") << ")";
        if (annotation) {
            write_para_children(stream, annotation, "annotation " + class_name, links);
        }
        stream << "\n";
}

/**
 * @brief output_gumbo_children
 * Takes the GUMBO tree and calls the relevant methods of QTextStream
 * to reproduce it in Markdown.
 * @param node
 */

static void output_gumbo_children(QTextStream &stream, const GumboNode *parent, bool top=false)
{
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
            stream << text.replace("[","\\[");
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
                stream << "\n";
            else if (tag == "br")
                stream << "\n\n";
            else if (tag == "b")
                stream << "**";
            else if (tag == "i")
                stream << "*";
            else if (tag == "hr")
                stream << "\n---\n\n";
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
                stream << "[";
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
                        write_ext_object(stream, "anyoldobjectname", buffer, filename, "gumbo", NULL, LinkageList());
                    }
                }
                else
                {
                    stream <<"![" << alt << "!](" << src << " \"";
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
                stream << "\n";
            else if (tag == "td")
                stream << "|";
            else if (tag == "table" || tag == "tbody")
                ;  // do nothing with spans (we might need to get any style from it
#endif
#endif
            else if (tag == "span")
                ;  // do nothing with spans (we might need to get any style from it
            else if (tag == "sup" || tag == "sub") {
                stream << "<" << tag << ">";
                close=true;
            }
            else if (tag == "span")
                ; // ignore span
            else {
                stream << "<" << tag << ">";
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
            output_gumbo_children(stream, node);

            // Now, see if we need to terminate this node
            if (tag == "p")
                stream << "\n";
            else if (tag == "b")
                stream << "**";
            else if (tag == "i")
                stream << "*";
            else if (tag == "a")
                stream << "](" << href << ")";
            else if (tag == "img" && term_img)
            {
                stream << "\")";
                term_img = false;
            }
            else if (tag == "span")
                ;  // ignore span
#ifdef IGNORE_GUMBO_TABLE
            else if (tag == "tr" || tag == "table" || tag == "tbody")
                ;
            else if (tag == "td")
                stream << "\n";    // normally the end of a row
#else
#ifndef GUMBO_TABLE
            else if (tag == "tr")
                stream << "|\n";   // end of line for table row
            else if (tag == "table")
                stream << "\n\n";
#endif
#endif
            else if (close)
                stream << "</" << tag << ">";
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

static bool write_html(QTextStream &stream, bool use_fixed_title, const QString &sntype, const QByteArray &data)
{
    // Put the children of the BODY into this frame.
    GumboOutput *output = gumbo_parse(data);
    if (output == nullptr)
    {
        return false;
    }

#ifdef DUMP_CHILDREN
    dump_children("ROOT", output->root);
#endif

    const GumboNode *head = find_named_child(output->root, "head");
    const GumboNode *body = find_named_child(output->root, "body");

#ifdef DUMP_CHILDREN
    dump_children("HEAD", head);
#endif

    if (use_fixed_title)
    {
        stream << sntype << "\n---\n\n# " << sntype << "\n";
    }
    else
    {
        const GumboNode *title = head ? find_named_child(head, "title") : nullptr;
        if (title)
        {
            stream << sntype.toLower() + "\n---\n\n# " << sntype << ": ";
            output_gumbo_children(stream, title);  // it should only be text
            stream << "\n";
        }
    }

    // Maybe we have a CSS that we can put inline.
#ifdef HANDLE_POOR_RTF
    const GumboNode *style = find_named_child(head, "style");
    if (!style) style = get_gumbo_child(body, "style");
#endif

#if 0
    if (style)
    {
        stream << "\n```\ngumbo-style: {";
        output_gumbo_children(stream, style);  // it should only be text
        stream << "}\n```\n\n";
    }
#endif
    if (body)
    {
#ifdef DUMP_CHILDREN
        dump_children("BODY", body);
#endif
        output_gumbo_children(stream, body, /*top*/true);
    }

    // Get GUMBO to release all the memory
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return true;
}


static void write_snippet(QTextStream &stream, XmlElement *snippet, const LinkageList &links)
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
                        // Need to convert this HTML into markup
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
                        //stream << "\n\nDecoded " << filename << "\n";
                        write_ext_object(stream, ext_object->attribute("name"), contents->byteData(),
                                       filename, sn_style, annotation, links);
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

            QString usemap;
            QList<XmlElement*> pins = smart_image->xmlChildren("map_pin");
            if (!pins.isEmpty()) usemap = "map-" + asset->attribute("filename");

            write_image(stream, smart_image->attribute("name"), contents->byteData(),
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
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext, QString());
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
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext, QString());
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
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext, QString());
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
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext, QString());

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
            write_para(stream, snippet, sn_style, links, /*prefix*/snippet->snippetName(), bodytext, QString());
    }
    // Hybrid_Tag
}


static void write_section(QTextStream &stream, XmlElement *section, const LinkageList &links, int level)
{
#if DUMP_LEVEL > 2
    qDebug() << "..section" << section->attribute("name");
#endif

    // Start with HEADER for the section (H1 used for topic title)
    stream << "\n" << QString(level+1,'#') << ' ' << section->attribute("name") << "\n";

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

/**
 * @brief startFile
 * Opens the file, and fills in the first parts of the Obsidian Frontmatter.
 */
static void startFile(QTextStream &stream)
{
    stream.setCodec("UTF-8");

    // The first part of the FRONTMATTER
    stream << "---\n";
    stream << "ImportedOn: " << imported_date << "\n";

}

/**
 * @brief write_link
 * Writes one of the navigation links that might appear in the footer of each page
 * @param stream
 * @param name
 * @param topic
 * @param override
 */
static QString nav_link(const XmlElement *topic, const QString &override = QString())
{
    QString result;
    if (topic && topic->objectName() == "topic")
        result = topic_link(topic);
    else if (!override.isEmpty())
        result = internal_link(override, override);
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

    //XmlElement *parent = qobject_cast<XmlElement*>(topic->parent());
    //if (parent && parent->objectName() != "topic") parent = NULL;
    QString directory = parent ? topic_names.value(parent) : topic->attribute("category_name");

    // Create a new file for this topic
    //OurFile topic_file(dirFile(topic->attribute("category_name"), topic_files.value(topic->attribute("topic_id")) + ".md"));
    OurFile topic_file(topicDirFile(topic));
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
            stream << "  - " << alias->attribute("name") << "\n";
        }
    }
    stream << "Tags: Category/" << validTag(topic->attribute("category_name")) << "\n";

    if (parent) {
        // Don't tell Breadcrumbs about the main page!
        QString link = (parent->objectName() == "topic") ? topic_files.value(parent->attribute("topic_id")) : mainPageName;
        stream << "parent:\n  - " << link << "\nup:\n  - " << link << "\n";
    }
    if (prev)
    {
        QString link = topic_files.value(prev->attribute("topic_id"));
        stream << "prev:\n  - " << link << "\n";
    }
    if (next)
    {
        QString link = topic_files.value(next->attribute("topic_id"));
        stream << "next:\n  - " << link << "\nsibling:\n  - " << link << "\n";
    }
    const auto children = topic->xmlChildren("topic");
    if (children.length() > 0)
    {
        stream << "down:\n";
        for (auto child: topic->xmlChildren("topic"))
        {
            stream << "  - " << topic_files.value(child->attribute("topic_id")) << "\n";
        }
    }
    stream << "RWtopicId: " << topic->attribute("topic_id") << "\n";

    // Connections
    for (auto child : topic->xmlChildren("connection"))
    {
        // Remove spaces from tag
        stream << relationship(child).replace(" ", "") << ": " << internal_link(child->attribute("target_id"), child->attribute("target_name")) << "\n";
    }
    stream << "---\n";

    //
    // End of FRONTMATTER
    //

    if (show_nav_panel && nav_at_start)
    {
        stream << "\n| Up | Prev | Next | Home |\n";
        stream << "|----|------|------|------|\n";
        stream << "| "  << nav_link(parent,  mainPageName);  // If up is not defined, use the index file
        stream << " | " << nav_link(prev);
        stream << " | " << nav_link(next);
        stream << " | " << nav_link(nullptr, mainPageName);  // Always points to top
        stream << " |\n\n";
    }

    stream << "# <center>" << topic_names.value(topic) << "</center>\n";

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

    // Provide summary of links to child topics
    auto child_topics = topic->xmlChildren("topic");
    if (!child_topics.isEmpty())
    {
        stream << "\n\n---\n## Governed Content\n";

        std::sort(child_topics.begin(), child_topics.end(), sort_all_topics);
        for (auto child: child_topics)
        {
            stream << "- " << topic_link(child) << "\n";
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
            if (annot) stream << " *; " << annot->xmlChild()->fixedText() << "*";
            stream << "\n";
        }
    }

    if (show_nav_panel && !nav_at_start)
    {
        //stream << "\n---\n";
        stream << "\n| Up | Prev | Next | Home |\n";
        stream << "|----|------|------|------|\n";
        stream << "| "  << nav_link(parent,  mainPageName);  // If up is not defined, use the index file
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
    stream << QString(level * 2, ' ') << "- " << topic_link(topic) << "\n";

    auto child_topics = topic->xmlChildren("topic");
    if (!child_topics.isEmpty())
    {
        std::sort(child_topics.begin(), child_topics.end(), sort_all_topics);
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
            write_head_meta(stream, root_elem);
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
                stream << "# " << cat << "\n";

                // Organise topics alphabetically
                auto topics = categories.values(cat);
                std::sort(topics.begin(), topics.end(), sort_all_topics);

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
        all_topics.insert(topic->attribute("topic_id"), topic);
        // Filename contains the FULL topic name including prefix and suffix
        QString fullname;
        const QString prefix = topic->attribute("prefix");
        const QString suffix = topic->attribute("suffix");
        if (!prefix.isEmpty()) fullname += prefix + " - ";
        fullname += topic->attribute("public_name");
        if (!suffix.isEmpty()) fullname += " (" + suffix + ")";
        topic_names.insert(topic, fullname);
        topic_files.insert(topic->attribute("topic_id"), validFilename(fullname));
    }

    // Write out the individual TOPIC files now:
    // Note use of findChildren to find children at all levels,
    // whereas xmlChildren returns only direct children.
    write_support_files();
    write_separate_index(root_elem);

    // A separate file for every single topic
    write_child_topics(root_elem->findChild<XmlElement*>("contents"));

#ifdef TIME_CONVERSION
    qInfo() << "TIME TO GENERATE HTML =" << timer.elapsed() << "milliseconds";
#endif

    // Tidy up memory
    class_of_style.clear();
    all_topics.clear();
    topic_names.clear();
    topic_files.clear();
}
