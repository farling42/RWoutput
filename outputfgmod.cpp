#include "outputfgmod.h"

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QBitmap>
#include <QImage>
#include <QPainter>
#include <QStaticText>
#include <QString>
#include <QTemporaryDir>
#include <QXmlStreamWriter>

#include "xmlelement.h"
#include <quazip/quazip.h>
#include <quazip/quazipfile.h>
#include <quazip/JlCompress.h>

#define DEBUG_LEVEL 6

static int topic_id = 0;
static int image_max_width = -1;
static bool apply_reveal_mask = true;

static QMap<QString,const XmlElement*> all_topics;
static QMap<QString,QStaticText> category_pin_of_topic;
static QMap<QString,const XmlElement*> topics_for_sections;

extern bool show_full_link_tooltip;
extern bool show_full_map_pin_tooltip;
//const QString map_pin_title_default("___ %1 ___");
//const QString map_pin_description_default("%1");
//const QString map_pin_gm_directions_default("---  GM DIRECTIONS  ---\n%1");
extern QString map_pin_title;
extern QString map_pin_description;
extern QString map_pin_gm_directions;


static void write_para_children(QXmlStreamWriter &stream, XmlElement *parent, const QString &classname,
                                const QString &prefix_label = QString(), const QString &prefix_bodytext = QString());

// A routine that will convert UTF-8 to ISO-8859-1 characters properly.
static QTextCodec *codec = QTextCodec::codecForName("ISO 8859-1");

static void write_characters(QXmlStreamWriter &stream, const QString &str)
{
    if (codec->canEncode(str))
    {
        stream.writeCharacters(str);
    }
    else
    {
        QString val;
        for (auto ch : str)
        {
            if (codec->canEncode(ch))
                val.append(ch);
            else
                val.append("?");    // uncodeable character
        }
        stream.writeCharacters(val);
    }
}

static void write_start_category(QXmlStreamWriter &stream)
{
    stream.writeStartElement("category");
    stream.writeAttribute("name", "");
    stream.writeAttribute("mergeid", "");
    stream.writeAttribute("baseicon", "4");
    stream.writeAttribute("decalicon", "1");
}

static QString topic_name(const XmlElement *topic)
{
    QString name = topic->attribute("public_name");
    QString prefix = topic->attribute("prefix");
    QString suffix = topic->attribute("suffix");
    if (!prefix.isEmpty()) name.prepend(prefix + " - ");
    if (!suffix.isEmpty()) name.append(" (" + suffix + ")");
    return name;
}

/**
 * @brief output_gumbo_children
 * Takes the GUMBO tree and calls the relevant methods of QXmlStreamWriter
 * to reproduce it in XHTML.
 * @param node
 */

static void output_gumbo_children(QXmlStreamWriter &stream, const GumboNode *parent)
{
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
            write_characters(stream, (pos < 0) ? text : text.left(pos));
        }
            break;
        case GUMBO_NODE_CDATA:
            stream.writeCDATA(node->v.text.text);
            //if (top) qDebug() << "GUMBO_NODE_CDATA:" << node->v.text.text;
            break;
        case GUMBO_NODE_COMMENT:
            stream.writeComment(node->v.text.text);
            //if (top) qDebug() << "GUMBO_NODE_COMMENT:" << node->v.text.text;
            break;

        case GUMBO_NODE_ELEMENT:
        {
            const QString tag = gumbo_normalized_tagname(node->v.element.tag);
            stream.writeStartElement(tag);
            //if (top) qDebug() << "GUMBO_NODE_ELEMENT:" << tag;
            GumboAttribute **attributes = reinterpret_cast<GumboAttribute**>(node->v.element.attributes.data);
            for (unsigned count = node->v.element.attributes.length; count > 0; --count)
            {
                const GumboAttribute *attr = *attributes++;
                stream.writeAttribute(attr->name, attr->value);
            }
            output_gumbo_children(stream, node);
            stream.writeEndElement();
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

static bool write_html(QXmlStreamWriter &stream, bool use_fixed_title, const QString &sntype, const QByteArray &data)
{
    // Put the children of the BODY into this frame.
    GumboOutput *output = gumbo_parse(data);
    if (output == nullptr)
    {
        return false;
    }

    stream.writeStartElement("details");
    stream.writeAttribute("class", sntype.toLower() + "Details");

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
        stream.writeStartElement("style");
        stream.writeAttribute("type", "text/css");
        output_gumbo_children(stream, style);  // it should only be text
        stream.writeEndElement();
    }

    if (use_fixed_title)
    {
        stream.writeStartElement("summary");
        stream.writeAttribute("class", sntype + "Summary");
        write_characters(stream, sntype);
        stream.writeEndElement(); // summary
    }
    else
    {
        const GumboNode *title = head ? find_named_child(head, "title") : nullptr;
        if (title)
        {
            stream.writeStartElement("summary");
            stream.writeAttribute("class", sntype.toLower() + "Summary");
            output_gumbo_children(stream, title);  // it should only be text
            stream.writeEndElement(); // summary
        }
    }

    if (body)
    {
#ifdef DUMP_CHILDREN
        dump_children("BODY", body);
#endif
        output_gumbo_children(stream, body);
    }

    stream.writeEndElement();  // details

    // Get GUMBO to release all the memory
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return true;
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
        if (!result.isEmpty()) result.append("\n\n");
        result.append(map_pin_description.arg(description));
    }
    if (!gm_directions.isEmpty())
    {
        if (!result.isEmpty()) result.append("\n\n");
        result.append(map_pin_gm_directions.arg(gm_directions));
    }
    return result;
}

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

static QString get_elem_string(XmlElement *elem)
{
    if (!elem) return QString();

    // RW puts \r\n as a line terminator, rather than just \n
    return elem->childString().replace("&#xd;\n","\n");
}

// Put image into separate for of MOD
static int write_image(QXmlStreamWriter &stream, const QString &image_name, const QByteArray &orig_data, XmlElement *mask_elem,
                       const QString &filename, const QString &class_name, XmlElement *annotation,
                       const QString &usemap = QString(), const QList<XmlElement*> pins = QList<XmlElement*>())
{
    QBuffer buffer;
    QString format = filename.split(".").last();
    int divisor = 1;
    const int pin_size = 20;
    bool in_buffer = false;

    stream.writeStartElement("p");

    stream.writeStartElement("figure");

    stream.writeStartElement("figcaption");
    if (annotation)
        write_para_children(stream, annotation, "annotation " + class_name, image_name);
    else
        write_characters(stream, image_name);
    stream.writeEndElement();  // figcaption

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

    stream.writeStartElement("img");
    if (!usemap.isEmpty()) stream.writeAttribute("usemap", "#" + usemap);
    stream.writeAttribute("alt", image_name);
    if (in_buffer)
        stream.writeAttribute("src", QString("data:image/%1;base64,%2").arg(format)
                               .arg(QString::fromLatin1(buffer.data().toBase64())));
    else
        stream.writeAttribute("src", QString("data:image/%1;base64,%2").arg(format)
                               .arg(QString::fromLatin1(orig_data.toBase64())));
    stream.writeEndElement();  // img

    if (!pins.isEmpty())
    {
        // Create the clickable MAP on top of the map
        stream.writeStartElement("map");
        stream.writeAttribute("name", usemap);
        for (auto pin : pins)
        {
            int x = pin->attribute("x").toInt() / divisor;
            int y = pin->attribute("y").toInt() / divisor - pin_size;
            stream.writeStartElement("area");
            stream.writeAttribute("shape", "rect");
            stream.writeAttribute("coords", QString("%1,%2,%3,%4")
                                   .arg(x).arg(y)
                                   .arg(x + pin_size).arg(y + pin_size));

            // Build up a tooltip from the text configured on the pin
            QString pin_name = pin->attribute("pin_name");
            QString description = get_elem_string(pin->xmlChild("description"));
            QString gm_directions = get_elem_string(pin->xmlChild("gm_directions"));
            QString link = pin->attribute("topic_id");
            // OPTION - use first section of topic if no description or gm_directions is provided
            if (show_full_map_pin_tooltip && (description.isEmpty() || gm_directions.isEmpty()) && !link.isEmpty())
            {
                // Read topic summary from first section
                get_summary(link, description, gm_directions);
            }
            QString title = build_tooltip(pin_name, description, gm_directions);
            if (!title.isEmpty()) stream.writeAttribute("title", title);

            //if (!link.isEmpty()) write_topic_href(stream, link, false);

            stream.writeEndElement();  // area
        }
        stream.writeEndElement();  // map
    }

    stream.writeEndElement();  // figure
    stream.writeEndElement();  // p
    return divisor;
}

// Put ext object into a separate file to be added to the MOD
static void write_ext_object(QXmlStreamWriter &stream, const QString &obj_name, const QByteArray &data,
                             const QString &filename, const QString &class_name, XmlElement *annotation)
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
    stream.writeStartElement("linklist");
    stream.writeStartElement("link");

    stream.writeAttribute("class", "imagewindow");
    stream.writeAttribute("recordname", "image." + obj_name);
    write_characters(stream, "name of external object");
    // TODO - annotation as writeCharacters?
    if (annotation) write_para_children(stream, annotation, "annotation " + class_name);

    stream.writeEndElement(); // link
    stream.writeEndElement(); // linklist
}

static void write_span(QXmlStreamWriter &stream, XmlElement *elem, const QString &classname)
{
    if (elem->isFixedString())
    {
#if DEBUG_LEVEL > 5
    qDebug() << "....span (simple)" << elem->fixedText();
#endif

        // TODO - the span containing the link might have style or class information!
        // Check to see if the fixed text should be replaced with a link.
        const QString text = elem->fixedText();
#if 0
        const QString link_text = links.find(text);
        if (!link_text.isNull())
        {
            stream.writeStartElement("a");
            write_topic_href(stream, link_text);
            write_characters(stream, text);  // case might be different
            stream.writeEndElement();  // a
            return;
        }
#endif
        write_characters(stream, text);
        if (stream.hasError())
        {
            qCritical() << "*** STREAM HAS AN ERROR";
        }
    }
    else
    {
#if DEBUG_LEVEL > 5
    qDebug() << "....span (complex)" << classname;
#endif

        // Only put in span if we really require it
        bool in_element = elem->objectName() != "span" || elem->hasAttribute("style") || !classname.isEmpty();
        if (in_element)
        {
            //stream.writeStartElement(elem->objectName());
            //write_attributes(stream, elem, classname);
            stream.writeStartElement("i");
            write_characters(stream, " (");
        }

        // All sorts of HTML can appear inside the text
        for (auto child: elem->xmlChildren())
        {
            write_span(stream, child, QString());
        }
        if (in_element)
        {
            write_characters(stream, ")");
            stream.writeEndElement();
            //stream.writeEndElement(); // span
        }
    }
}

static void write_para(QXmlStreamWriter &stream, XmlElement *elem, const QString &classname,
                       const QString &label, const QString &bodytext)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....paragraph" << classname;
#endif

    if (elem->isFixedString())
    {
        write_span(stream, elem, classname);
        return;
    }

    if (elem->objectName() == "snippet")
    {
        stream.writeStartElement("p");
    }
    else
    {
        stream.writeStartElement(elem->objectName());
        //write_attributes(stream, elem, classname);
    }
    // If there is no label, then set the class on the paragraph element,
    // otherwise we will put the text inside a span with the given class.
    bool class_set = false;
    if (!label.isEmpty())
    {
        stream.writeStartElement("b");
        write_characters(stream, label + ": ");
        stream.writeEndElement();
    }
    else if (!classname.isEmpty())
    {
        // class might already have been written by calling writeAttributes
        if (elem->objectName() == "snippet") stream.writeAttribute("class", classname);
        class_set = true;
    }

    if (!bodytext.isEmpty())
    {
        write_characters(stream, bodytext);
    }

    for (auto child : elem->xmlChildren())
    {
        if (child->objectName() == "span")
            write_span(stream, child, class_set ? QString() : classname);
        else if (child->objectName() != "tag_assign")
            // Ignore certain children
            write_para(stream, child, classname, /*no prefix*/QString(), QString());
    }
    stream.writeEndElement();  // p
}

static void write_para_children(QXmlStreamWriter &stream, XmlElement *parent, const QString &classname,
                                const QString &prefix_label, const QString &prefix_bodytext)
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children" << classname;
#endif

    bool first = true;
    for (auto para: parent->xmlChildren())
    {
        if (first)
            write_para(stream, para, classname, /*prefix*/prefix_label, prefix_bodytext);
        else
            write_para(stream, para, classname, /*no prefix*/QString(), QString());
        first = false;
    }
}

static void write_snippet(QXmlStreamWriter &stream, XmlElement *snippet)
{
    QString sn_type = snippet->attribute("type");
    QString sn_style = snippet->attribute("style"); // Read_Aloud, Callout, Flavor, Handout
#if DEBUG_LEVEL > 3
    qDebug() << "..snippet" << sn_type;
#endif

    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        for (auto gm_directions: snippet->xmlChildren("gm_directions"))
            write_para_children(stream, gm_directions, "gm_directions " + sn_style);

        for (auto contents: snippet->xmlChildren("contents"))
            write_para_children(stream, contents, "contents " + sn_style);
    }
    else if (sn_type == "Labeled_Text")
    {
        for (auto contents: snippet->xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            write_para_children(stream, contents, "contents " + sn_style, /*prefix*/snippet->snippetName());  // has its own 'p'
        }
    }
    else if (sn_type == "Portfolio")    // put in NPC section
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
                                   filename, sn_style, annotation);

                    // Put in markers for statblock
                    QByteArray store = contents->byteData();
                    QBuffer buffer(&store);
                    QuaZip zip(&buffer);
                    if (zip.open(QuaZip::mdUnzip))
                    {
                        stream.writeStartElement("section");
                        stream.writeAttribute("class", "portfolioListing");
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
                        stream.writeEndElement(); // section[class="portfolioListing"]
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
                                   /*mask*/nullptr, filename, sn_style, annotation);
                    }
                    else if (filename.endsWith(".html") ||
                             filename.endsWith(".htm")  ||
                             filename.endsWith(".rtf"))
                    {
                        stream.writeComment("Decoded " + filename);
                        write_html(stream, true, sn_type, contents->byteData());
                    }
                    else
                    {
                        write_ext_object(stream, ext_object->attribute("name"), contents->byteData(),
                                       filename, sn_style, annotation);
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

            write_image(stream, smart_image->attribute("name"), contents->byteData(),
                        mask, filename, sn_style, annotation, usemap, pins);
        }
    }
    else if (sn_type == "Date_Game")
    {
        XmlElement *date       = snippet->xmlChild("game_date");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = date->attribute("display");
        if (annotation)
            write_para_children(stream, annotation, "annotation " + sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Date_Range")
    {
        XmlElement *date       = snippet->xmlChild("date_range");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = "From: " + date->attribute("display_start") + " To: " + date->attribute("display_end");
        if (annotation)
            write_para_children(stream, annotation, "annotation" + sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Tag_Standard")
    {
        XmlElement *tag        = snippet->xmlChild("tag_assign");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (tag == nullptr) return;

        QString bodytext = tag->attribute("tag_name");
        if (annotation)
            write_para_children(stream, annotation, "annotation" + sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Numeric")
    {
        XmlElement *contents   = snippet->xmlChild("contents");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (contents == nullptr) return;

        const QString &bodytext = contents->childString();
        if (annotation)
            write_para_children(stream, annotation, "annotation" + sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);

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
            write_para_children(stream, annotation, "annotation" + sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    // Hybrid_Tag
}


static QString section_name(QList<int> levels, const QString &name)
{
    QStringList result;
    result.reserve(levels.size());
    for (int value : levels)
        result.append(QString::number(value));
    return result.join(".") + ": " + name;
}

static void write_section(QXmlStreamWriter &stream, const XmlElement *section, QList<int> levels)
{
#if DEBUG_LEVEL > 3
    qDebug() << ".section" << section->attribute("name");
#endif
    stream.writeTextElement("h", section_name(levels, section->attribute("name")));
    // write snippets
    for (auto snippet: section->xmlChildren("snippet"))
    {
        write_snippet(stream, snippet);
    }
    // write sub-sections
    QList<int> sublevels = levels;
    sublevels.append(0);
    for (auto subsection: section->xmlChildren("section"))
    {
        ++sublevels.last();
        write_section(stream, subsection, sublevels);
    }
}

static void write_topic(QXmlStreamWriter &stream, const XmlElement *topic)
{
    topic_id++;
#if DEBUG_LEVEL > 3
    qDebug() << "topic" << topic->attribute("topic_id");
#endif
    stream.writeStartElement(topic->attribute("topic_id"));

    stream.writeStartElement("name");
    stream.writeAttribute("type", "string");
    write_characters(stream, topic_name(topic));
    stream.writeEndElement(); // name

    stream.writeStartElement("text");
    stream.writeAttribute("type", "formattedtext");

    // Write out the topic's text here
    QList<int> levels = {0};
    for (auto section: topic->xmlChildren("section"))
    {
        ++levels[0];
        write_section(stream, section, levels);
    }

    // Write list of child topics
    auto child_topics = topic->xmlChildren("topic");
    if (!child_topics.isEmpty())
    {
        stream.writeTextElement("h", "Child Topics");

        stream.writeStartElement("linklist");
        for (auto child : child_topics)
        {
            stream.writeStartElement("link");
            stream.writeAttribute("class", "encounter");
            stream.writeAttribute("recordname", "encounter." + child->attribute("topic_id"));
            write_characters(stream, topic_name(child));
            stream.writeEndElement();   // link
        }
        stream.writeEndElement();  // linklist
    }

    stream.writeEndElement(); // text
    stream.writeEndElement();  // topic_id
}

// db.xml
//    <?xml version="1.0" encoding="iso-8859-1"?>
//    <root version="3.0" release="1|3.5E:14|CoreRPG:3">
//       <battle>
//    		<category name="" mergeid="" baseicon="4" decalicon="1">
//    			<id-00001>

static bool create_db(const QTemporaryDir &dir, const XmlElement *root_elem)
{
    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    if (details == nullptr) return false;

    QFile file(dir.filePath("db.xml"));
    if (!file.open(QFile::WriteOnly|QFile::Text))
    {
        qCritical() << "Failed to create DB file" << file.fileName();
        return false;
    }

    QXmlStreamWriter stream(&file);
    stream.setAutoFormatting(true);
    stream.setAutoFormattingIndent(2);
    stream.setCodec("ISO 8859-1");
    stream.writeStartDocument();

    stream.writeStartElement("root");
    stream.writeAttribute("version", "3.0");    // Fantasy Grounds version (of this file format)
    stream.writeAttribute("release", details->attribute("version"));

#if 0
    stream.writeStartElement("battle");
    stream.writeEndElement();   // battle
#endif

    stream.writeStartElement("encounter");
    write_start_category(stream);

    // Write each topic as a separate encounter
    for (XmlElement *topic : root_elem->findChildren<XmlElement*>("topic"))
        write_topic(stream, topic);

    stream.writeEndElement();   // category
    stream.writeEndElement();   // encounter

#if 0
    stream.writeStartElement("image");
    stream.writeEndElement();   // image

    stream.writeStartElement("item");
    stream.writeEndElement();   // item

    stream.writeStartElement("lists");
    // battle
    // encounter
    // imagewindow
    // item
    // npc
    // treasureparcels
    stream.writeEndElement();   // lists

    stream.writeStartElement("npc");
    stream.writeEndElement();   // npc

    stream.writeStartElement("treasureparcels");
    stream.writeEndElement();   // treasureparcels
#endif

    stream.writeEndElement();   // root
    stream.writeEndDocument();
    return true;
}

// DEFINITION.XML
// <?xml version="1.0" encoding="iso-8859-1"?>
// <root version="3.0" release="1|3.5E:14|CoreRPG:3">
//    <name>BASIC-1: A Learning Time</name>
//    <category></category>
//    <author>Adventureaweek.com</author>
//    <ruleset>PFRPG</ruleset>
// </root>

static bool create_definition(const QTemporaryDir &dir, const XmlElement *root_elem)
{
    // Get RW definition block
    // output -> definition -> details ->
    //    summary
    //    definition
    //    requirements
    //    credits
    //    legal
    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    if (details == nullptr)
    {
        qCritical() << "Failed to find <details> in RWoutput";
        return false;
    }

    QFile file(dir.filePath("definition.xml"));
    if (!file.open(QFile::WriteOnly|QFile::Text))
    {
        qCritical() << "Failed to create definition file" << file.fileName();
        return false;
    }
    QXmlStreamWriter stream(&file);
    stream.setAutoFormatting(true);
    stream.setAutoFormattingIndent(2);
    stream.setCodec("ISO 8859-1");
    stream.writeStartDocument();

    stream.writeStartElement("root");
    stream.writeAttribute("version", "3.0");    // Fantasy Grounds version (of this file format)
    stream.writeAttribute("release", details->attribute("version"));

    stream.writeTextElement("name", details->attribute("name"));
    stream.writeTextElement("category", "");
    stream.writeTextElement("author", details->attribute("credits"));
    QString game = root_elem->attribute("game_system_id");
    if (game == "4")
        stream.writeTextElement("ruleset", "PFRPG");
    else
        stream.writeTextElement("ruleset", "");

    stream.writeEndElement();   // root
    stream.writeEndDocument();
    return true;
}


void toFgMod(const QString &path,
             const XmlElement *root_elem,
             const QMap<QString,const XmlElement*> section_topics,
             bool use_reveal_mask)
{
    image_max_width   = 2048;
    apply_reveal_mask = use_reveal_mask;
    topics_for_sections = section_topics;

    QString curpath = QDir::currentPath();

    // put files in temporary directory
    QTemporaryDir tempdir;
    if (!tempdir.isValid())
    {
        qWarning() << "Failed to create temporary directory";
        return;
    }
    QDir::setCurrent(tempdir.path());

    // create definition.xml
    create_definition(tempdir, root_elem);

    // create db.xml
    create_db(tempdir, root_elem);

    // zip up the contents into a file with .mod extension
    QDir::setCurrent(curpath);
    QuaZipFile zipfile(path);
    if (!JlCompress::compressDir(/*file*/ path, /*dir*/ tempdir.path()))
    {
        qCritical() << "Failed to create .MOD file" << path << "from" << tempdir.path();
    }
}
