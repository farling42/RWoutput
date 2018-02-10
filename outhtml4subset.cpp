#include "outhtml4subset.h"

#include <QTextStream>
#include <QBitmap>
#include <QBuffer>
#include <QCollator>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QApplication>
#include <QProgressDialog>
#include <future>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

#define DEBUG_LEVEL 0

static int image_max_width = -1;
static bool apply_reveal_mask = true;
static bool sort_by_prefix = true;
static QCollator collator;

// Some predefined Styles
static const QString FLAVOR_STYLE{"background-color: rgb(239,212,210);"};
static const QString READ_ALOUD_STYLE{"background-color: rgb(209,223,242);"};
static const QString HANDOUT_STYLE{"background-color: rgb(232,225,217);"};
static const QString CALLOUT_STYLE{"background-color: rgb(190,190,190);"};
static const QString ANNOTATION_STYLE{"font-style: italic; margin-left: 10px;"};

struct Linkage {
    QString name;
    QString id;
    Linkage(const QString &name, const QString &id) : name(name), id(id) {}
};
typedef QList<Linkage> LinkageList;
static LinkageList links;

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


QString writeAttributes(const XmlElement *elem, const QString &style)
{
    QString result;
    QTextStream stream(&result);
    bool done_style = false;

    for (auto attr : elem->p_attributes)
    {
        // Ignore attributes which won't do anything in the HTML4 subset.
        if (attr.name == "style")
        {
            stream << QString("%1='%2; %3' ").arg(attr.name).arg(attr.value).arg(style);
            done_style = true;
        }
        else if (attr.name != "class" &&  // ignore RWdefault, RWSnippet, RWLink
                attr.name != "facet_name" &&
                attr.name != "type" &&
                !attr.name.startsWith("canonical") &&
                !attr.name.startsWith("gregorian") &&
                !attr.name.startsWith("display"))
        {
            stream << QString("%1='%2' ").arg(attr.name).arg(attr.value);
        }
    }
    if (!done_style && !style.isEmpty()) stream << QString("style='%1;'").arg(style);
    return result;
}




static void writeSpan(QTextStream &stream, const XmlElement *elem, const QString &style)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....writeSpan" << elem->objectName() << elem->isFixedString();
#endif
    if (elem->isFixedString())
    {
        QString text = elem->fixedText();
        // TODO - the span containing the link might have style or class information!
        // Check to see if the fixed text should be replaced with a link.
        for (auto link : links)
        {
            // Ignore case during the test
            if (link.name.compare(text, Qt::CaseInsensitive) == 0)
            {
                text = QString("<a href='#%1'>%2</a>").arg(link.id).arg(text);
                break;
            }
        }
        if (style.isEmpty())
            stream << text;
        else
            stream << QString("<span style='%1'>%2</span>").arg(style).arg(text);
    }
    else
    {
        // Only put in span if we really require it
        bool in_element = elem->objectName() != "span" || elem->hasAttribute("style") || !style.isEmpty();
        if (in_element)
        {
            stream << QString("<%1 %2>").arg(elem->objectName()).arg(writeAttributes(elem, style));
        }

        // All sorts of HTML can appear inside the text
        for (auto child: elem->xmlChildren())
        {
            writeSpan(stream, child, /*no additional style*/QString());
        }
        if (in_element) stream << QString("</%1>").arg(elem->objectName());
    }
}


static void writePara(QTextStream &stream, const XmlElement *elem, const QString &style,
               const QString &label, const QString &bodytext)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....writePara" << elem->objectName();
#endif
    if (elem->isFixedString())
    {
        // No fixed text will have a "snippet_style_type"
        writeSpan(stream, elem, style);
        return;
    }

    QString styletext;
    if (style == "Read_Aloud")
        styletext = READ_ALOUD_STYLE;
    else if (style == "Handout") // Message
        styletext = HANDOUT_STYLE;
    else if (style == "Flavor")
        styletext = FLAVOR_STYLE;
    else if (style == "Callout")
        styletext = CALLOUT_STYLE;
    else if (label.isEmpty() && bodytext.isEmpty())
        // No prefix label, so apply directly to the paragraph
        styletext = style;

    QString tag = elem->objectName();
    if (tag == "snippet" ||
            tag == "date_range" ||
            tag == "game_date")
    {
        tag = "p";
    }
#if 0
    if (tag != "p" &&
            tag != "td" && tag != "tr" && tag != "table" && tag != "tbody" &&
            tag != "ul" && tag != "li" && tag != "ol")
    {
        qDebug() << tag;
    }
#endif

    stream << QString("<%1 %2>").arg(tag).arg(writeAttributes(elem, styletext));

    // If there is no label, then set the class on the paragraph element,
    // otherwise we will put the text inside a span with the given class.
    if (!label.isEmpty())
    {
        stream << QString("<b>%1: </b>").arg(label);
    }

    if (!bodytext.isEmpty())
    {
        stream << bodytext << " ";
    }
    for (auto child : elem->xmlChildren())
    {
        if (child->objectName() == "span")
        {
            writeSpan(stream, child, styletext.isEmpty() ? style : QString());
        }
        else if (child->objectName() != "tag_assign")
            // Ignore certain children
            writePara(stream, child, styletext, /*no prefix*/QString(), QString());
    }
    stream << QString("</%1>\n").arg(tag);  // terminate the paragraph
}


static void writeParaChildren(QTextStream &stream, const XmlElement *parent, const QString &classname,
                       const QString &prefix_label = QString(), const QString &prefix_bodytext = QString())
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children" << parent->objectName();
#endif
    QString useclass = classname;
    if (parent->objectName() == "annotation")
    {
        useclass = ANNOTATION_STYLE;
    }

    bool first = true;
    for (auto para: parent->xmlChildren())
    {
        if (first)
            writePara(stream, para, useclass, /*prefix*/prefix_label, prefix_bodytext);
        else
            writePara(stream, para, useclass, /*no prefix*/QString(), QString());
        first = false;
    }
}


/**
 * @brief tobase64
 * @param data
 * @return
 */
static inline QString to_base64(const QByteArray &data)
{
    // use new LineFile class to insert \n at the appropriate points in the output
    return QString{data.toBase64()};
}


/*
 * Return the divisor for the map's size
 */

#ifdef THREADED
std::mutex image_mutex;
#endif

static int writeImage(QTextStream &stream, const QString &image_name, const QByteArray &orig_data, const XmlElement *mask_elem,
               const QString &filename, const QString &class_name, const XmlElement *annotation)
{
#if DEBUG_LEVEL > 4
    qDebug() << "....writeImage: image" << image_name << ", file" << filename << ", size" << orig_data.size();
#endif

    QBuffer buffer;
    QString format = filename.split(".").last();
    int divisor = 1;
    bool in_buffer = false;

    stream << "<p>";

    if (annotation)
        writeParaChildren(stream, annotation, class_name, image_name);
    else
        stream << QString("<b>Image: %1</b>").arg(image_name);

    // See if possible image conversion is required
    bool bad_format = (format == "bmp" || format == "tif");
    if (mask_elem != nullptr || image_max_width > 0 || bad_format)
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
        }
        // Do we need to put it in the buffer?
        if (in_buffer)
        {
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, qPrintable(format));
            buffer.close();
            in_buffer = true;
        }
    }

    stream << QString("<img src='data:image/%1;base64,").arg(format);
    stream << (in_buffer ? buffer.data() : orig_data).toBase64();
    stream << "'>\n";

    return divisor;
}

static void writeExtObject(QTextStream &stream, const QString &obj_name, const QByteArray &data,
                           const QString &filename, const QString &class_name, XmlElement *annotation)
{
#if 0
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

        stream << "<p>";

        stream << QString("<b>%1: </b>").arg(obj_name);

        stream << QString("<span><a download='%3' href='data:%1;base64,%2'></span>").arg(mime_type).arg(to_base64(data)).arg(filename);

        if (annotation) writeParaChildren(stream, annotation, class_name);
    }
#else
    if (annotation) writeParaChildren(stream, annotation, class_name, /*prefix*/ obj_name);
#endif
}


static QString get_body(const QByteArray &data)
{
    QString source{data};
    int start = source.indexOf("<body>");
    int finish = source.indexOf("</body>");
    if (start < 0 || finish < 0) return source;
    start += 6;
    return source.mid(start, finish-start);
}


static void writeSnippet(QTextStream &stream, const XmlElement *snippet)
{
    QString sn_type = snippet->attribute("type");
    QString sn_style = snippet->attribute("style"); // Read_Aloud, Callout, Flavor, Handout
#if DEBUG_LEVEL > 3
    qDebug() << "...snippet" << sn_type;
#endif

    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        auto gm = snippet->xmlChildren("gm_directions");
        if (!gm.isEmpty()) stream << "<div style='background-color: floralwhite; border: 1px solid tan;'>";
        for (auto gm_directions: gm)
            writeParaChildren(stream, gm_directions, sn_style);
        if (!gm.isEmpty()) stream << "</div>\n";

        for (auto contents: snippet->xmlChildren("contents"))
            writeParaChildren(stream, contents, sn_style);
    }
    else if (sn_type == "Labeled_Text")
    {
        for (auto contents: snippet->xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            writeParaChildren(stream, contents, sn_style, /*prefix*/snippet->snippetName());  // has its own 'p'
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
                    //writeExtObject(stream, ext_object->attribute("name"), contents->p_byte_data,
                    //               filename, sn_style, annotation);

                    stream << "<p style='font-style:large'><b><u>";
                    if (annotation) writeParaChildren(stream, annotation, sn_type, /*prefix*/ "Portfolio");
                    stream << "</u></b>\n";

                    // Put in markers for statblock
                    QBuffer buffer(&contents->p_byte_data);
                    QuaZip zip(&buffer);
                    if (zip.open(QuaZip::mdUnzip))
                    {
                        for (bool more=zip.goToFirstFile(); more; more=zip.goToNextFile())
                        {
                            if (zip.getCurrentFileName().startsWith("statblocks_html/"))
                            {
                                QuaZipFile file(&zip);
                                if (file.open(QuaZipFile::ReadOnly))
                                {
                                    stream << "<div style='border: 1px solid tan; padding: 2px; margin 5px; background-color: azure;'>";
                                    stream << get_body(file.readAll());
                                    stream << "</div>\n";
                                }
                                else
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
                        writeImage(stream, ext_object->attribute("name"), contents->p_byte_data,
                                   /*mask*/nullptr, filename, sn_style, annotation);
                    }
                    else if (filename.endsWith(".html") ||
                             filename.endsWith(".htm")  ||
                             filename.endsWith(".rtf"))
                    {
                        stream << QString("<p style='font-style:large'><b><u>%1</b></u>").arg(sn_type);
                        //if (annotation) writeParaChildren(stream, annotation, sn_type, image_name);

                        stream << "<div>";
                        stream << get_body(contents->p_byte_data);
                        stream << "</div>\n";
                    }
                    else
                    {
                        writeExtObject(stream, ext_object->attribute("name"), contents->p_byte_data, filename, sn_style, annotation);
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

            writeImage(stream, smart_image->attribute("name"), contents->p_byte_data,
                                     mask, filename, sn_style, annotation);
#if 0
            if (!pins.isEmpty())
            {
                // Create the clickable MAP on top of the map
                stream->writeStartElement("map");
                //stream->writeAttribute("name", usemap);
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
#endif

        }
    }
    else if (sn_type == "Date_Game")
    {
        XmlElement *date       = snippet->xmlChild("game_date");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = date->attribute("display");
        if (annotation)
            writeParaChildren(stream, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Date_Range")
    {
        XmlElement *date       = snippet->xmlChild("date_range");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = "From: " + date->attribute("display_start") + " To: " + date->attribute("display_end");
        if (annotation)
            writeParaChildren(stream, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Tag_Standard")
    {
        QList<XmlElement*> tags = snippet->xmlChildren("tag_assign");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (tags.isEmpty()) return;

        QStringList tag_text;
        for (auto tag: tags)
            tag_text.append(tag->attribute("tag_name"));
        QString bodytext = tag_text.join(", ");
        if (annotation)
            writeParaChildren(stream, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Numeric")
    {
        XmlElement *contents   = snippet->xmlChild("contents");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (contents == nullptr) return;

        const QString &bodytext = contents->childString();
        if (annotation)
            writeParaChildren(stream, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);

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
            writeParaChildren(stream, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            writePara(stream, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    // Hybrid_Tag
}


static void writeSection(QTextStream &stream, const XmlElement *section, int level)
{
    // Start with HEADER for the section
    stream << QString("<h%1>%2</h%1>\n").arg(level+1).arg(section->attribute("name"));

    // Write snippets
    for (auto snippet: section->xmlChildren("snippet"))
    {
        writeSnippet(stream, snippet);
    }

    // Write following sections
    for (auto subsection: section->xmlChildren("section"))
    {
        writeSection(stream, subsection, level+1);
    }
}


void writeTopic(QTextStream &stream, const XmlElement *topic)
{
#if DEBUG_LEVEL > 1
    qDebug() << ".topic" << topic->objectName() << ":" << topic->attribute("public_name");
#endif

    // Process <linkage> first, to ensure we can remap strings
    links.clear();
    for (auto link: topic->xmlChildren("linkage"))
    {
        if (link->attribute("direction") != "Inbound")
        {
            links.append(Linkage(link->attribute("target_name"), link->attribute("target_id")));
        }
    }

    // HTML4 allows an anchor to be defined with "<a name='id'>"
    stream << QString("<h1 align='center' style='page-break-before: always; background-color: gainsboro;'><a name='%1'>").arg(topic->attribute("topic_id"));

    if (topic->hasAttribute("prefix")) stream << QString("%1 - ").arg(topic->attribute("prefix"));
    stream << topic->attribute("public_name");
    if (topic->hasAttribute("suffix")) stream << QString(" (%1)").arg(topic->attribute("suffix"));
    stream << "</a></h1>\n";

    auto aliases = topic->xmlChildren("alias");
    // Maybe some aliases (in own section for smaller font?)
    for (auto alias : aliases)
    {
        stream << "<p><i>" << alias->attribute("name") << "</i>\n";
    }

    // Process all <sections>, applying the linkage for this topic
    for (auto section: topic->xmlChildren("section"))
    {
        writeSection(stream, section, /*level*/ 1);
    }

    // Provide summary of links to child topics
    auto child_topics = topic->xmlChildren("topic");
    std::sort(child_topics.begin(), child_topics.end(), sort_all_topics);

    if (!child_topics.isEmpty())
    {
        stream << "<h2>Child Topics</h2>\n";
        stream << "<ul>";
        for (auto child: child_topics)
        {
            stream << QString("<li><a href='#%1'>%2</a>\n").arg(child->attribute("topic_id")).arg(child->attribute("public_name"));
        }
        stream << "</ul>\n";
    }
}

static void write_first_page(QTextStream &stream, const XmlElement *root_elem)
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

    stream << QString("<h1 align='center' style='margin: 100px;'>%1</h1>\n").arg(details->attribute("name"));

    stream << QString("<p><b>Exported on</b> %1\n").arg(QDateTime::fromString(root_elem->attribute("export_date"), Qt::ISODate).toLocalTime().toString(Qt::SystemLocaleLongDate));

    stream << QString("<p><b>Generated by RWoutput tool on</b> %1\n").arg(QDateTime::currentDateTime().toString(Qt::SystemLocaleLongDate));

    if (details->hasAttribute("summary"))
        stream << QString("<h2>Summary</h2>\n<p>%1\n").arg(details->attribute("summary"));

    if (details->hasAttribute("description"))
        stream << QString("<h2>Description</h2>\n<p>%1\n").arg(details->attribute("description"));

    if (details->hasAttribute("requirements"))
        stream << QString("<h2>Requirements</h2>\n<p>%1\n").arg(details->attribute("requirements"));

    if (details->hasAttribute("credits"))
        stream << QString("<h2>Credits</h2>\n<p>%1\n").arg(details->attribute("credits"));

    if (details->hasAttribute("legal"))
        stream << QString("<h2>Legal</h2>\n<p>%1\n").arg(details->attribute("summary"));

    if (details->hasAttribute("other_notes"))
        stream << QString("<h2>Other Notes</h2>\n<p>%1\n").arg(details->attribute("other_notes"));
}


void outHtml4Subset(QTextStream &stream,
                    const XmlElement *root,
                    int max_image_width,
                    bool use_reveal_mask)
{
    image_max_width   = max_image_width;
    apply_reveal_mask = use_reveal_mask;
    collator.setNumericMode(true);

    stream << "<meta http-equiv='Content-Type' content='text/html; charset='utf-8' />\n";

    write_first_page(stream, root);

    auto topics = root->findChildren<XmlElement*>("topic");
    QProgressDialog pbar("Generating HTML4", QString(), 0, topics.count());
    pbar.show();

    std::sort(topics.begin(), topics.end(), sort_all_topics);

    int count = 0;
    for (auto topic : topics)
    {
        pbar.setValue(++count);
        writeTopic(stream, topic);
        qApp->processEvents();
    }
}
