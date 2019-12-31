#include "gentextdocument.h"

#define TIME_CONVERSION

#include <QTextDocument>
#include <QTextFrame>
#include <QTextCursor>
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
#include "linkage.h"
#ifdef TIME_CONVERSION
#include <QElapsedTimer>
#endif

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


static QString write_attributes(const XmlElement *elem, const QString &style)
{
    QString result;
    QTextStream stream(&result);
    bool done_style = false;

    for (auto attr : elem->attributes())
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




static QString write_span(const XmlElement *elem, const QString &style)
{
    QString result;
#if DEBUG_LEVEL > 5
    qDebug() << "....writeSpan" << elem->objectName() << elem->isFixedString();
#endif
    if (elem->isFixedString())
    {
        QString text = elem->fixedText();
        // TODO - the span containing the link might have style or class information!
        // Check to see if the fixed text should be replaced with a link.
        QString link_text = links.find(text);
        if (!link_text.isNull())
        {
            text = QString("<a href='#%1'>%2</a>").arg(link_text).arg(text);
        }
        if (style.isEmpty())
            result.append(text);
        else
            result.append(QString("<span style='%1'>%2</span>").arg(style).arg(text));
    }
    else
    {
        // Only put in span if we really require it
        bool in_element = elem->objectName() != "span" || elem->hasAttribute("style") || !style.isEmpty();
        if (in_element)
        {
            result.append(QString("<%1 %2>").arg(elem->objectName()).arg(write_attributes(elem, style)));
        }

        // All sorts of HTML can appear inside the text
        for (auto child: elem->xmlChildren())
        {
            result.append(write_span(child, /*no additional style*/QString()));
        }
        if (in_element)
        {
            result.append(QString("</%1>").arg(elem->objectName()));
        }
    }
    return result;
}


static void write_para(QTextCursor &cursor, const XmlElement *elem, const QString &style,
                       const QString &label, const QString &bodytext)
{
#if DEBUG_LEVEL > 5
    qDebug() << "....writePara" << elem->objectName();
#endif
    if (elem->isFixedString())
    {
        // No fixed text will have a "snippet_style_type"
        cursor.insertHtml(write_span(elem, style));
        return;
    }

    // TODO "<p class='RWEnumerated'>...

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

    if (tag == "p")
    {
        QString p_class = elem->attribute("class");
        QTextBlockFormat format = cursor.blockFormat();
        if (p_class == "RWDefault")
            format.setIndent(0);
        else if (p_class == "RWEnumerated")
            format.setTextIndent(20);
        cursor.setBlockFormat(format);
    }
    else
        cursor.insertHtml(QString("<%1 %2>").arg(tag).arg(write_attributes(elem, styletext)));  // TODO

    // If there is no label, then set the class on the paragraph element,
    // otherwise we will put the text inside a span with the given class.
    if (!label.isEmpty())
    {
        cursor.insertHtml(QString("<b>%1: </b>").arg(label));
    }

    if (!bodytext.isEmpty())
    {
        cursor.insertHtml(bodytext + " ");
    }

    for (auto child : elem->xmlChildren())
    {
        if (child->objectName() == "span")
        {
            cursor.insertHtml(write_span(child, styletext.isEmpty() ? style : QString()));
        }
        else if (child->objectName() != "tag_assign")
            // Ignore certain children
            write_para(cursor, child, styletext, /*no prefix*/QString(), QString());
    }

    if (tag == "p")
    {
        cursor.insertBlock();
        if (elem->attribute("class") == "RWEnumerated")
        {
            QTextBlockFormat format = cursor.blockFormat();
            format.setTextIndent(0);
            cursor.setBlockFormat(format);
        }
    }
    else
        cursor.insertHtml(QString("</%1>").arg(tag));  // terminate the paragraph
}


static void write_para_children(QTextCursor &cursor, const XmlElement *parent, const QString &classname,
                                const QString &prefix_label = QString(), const QString &prefix_bodytext = QString())
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children" << parent->objectName();
#endif
    QString txt_style = classname;
    if (parent->objectName() == "annotation")
    {
        txt_style = ANNOTATION_STYLE;
    }

    bool first = true;
    for (auto para: parent->xmlChildren())
    {
        if (first)
            write_para(cursor, para, txt_style, /*prefix*/prefix_label, prefix_bodytext);
        else
            write_para(cursor, para, txt_style, /*no prefix*/QString(), QString());
        first = false;
    }
}

/*
 * Return the divisor for the map's size
 */

#ifdef THREADED
std::mutex image_mutex;
#endif

static void write_image(QTextCursor &cursor, const QString &image_name, const QByteArray &orig_data, const XmlElement *mask_elem,
                       const QString &filename, const QString &class_name, const XmlElement *annotation)
{
#if DEBUG_LEVEL > 4
    qDebug() << "....writeImage: image" << image_name << ", file" << filename << ", size" << orig_data.size();
#endif

    QBuffer buffer;
    QString format = filename.split(".").last();
    int divisor = 1;
    bool in_buffer = false;

    if (annotation)
        write_para_children(cursor, annotation, class_name, image_name);
    else
    {
        cursor.insertHtml(QString("<b>Image: %1</b>").arg(image_name));
        cursor.insertBlock();
    }

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

    if (!divisor)
        cursor.insertHtml(QString("<img src='data:image/%1;base64,").arg(format) + (in_buffer ? buffer.data() : orig_data).toBase64() + "'>");
}

static void write_ext_object(QTextCursor &cursor, const QString &obj_name, const QByteArray &data,
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

        cursor.insertHtml(QString("<b>%1: </b>").arg(obj_name));
        cursor.insertHtml(QString("<a download='%3' href='data:%1;base64,%2'></a>").arg(mime_type).arg(QString::fromLatin1(data.toBase64())).arg(filename));
        cursor.insertBlock();

        if (annotation)
        {
            write_para_children(cursor, annotation, class_name);
        }
    }
#else
    Q_UNUSED(data)
    Q_UNUSED(filename)
    if (annotation)
    {
        write_para_children(cursor, annotation, class_name, /*prefix*/ obj_name);
    }
#endif
}


static QByteArray get_body(const QByteArray &source)
{
    int start  = source.indexOf("<body>");
    int finish = source.indexOf("</body>");
    if (start < 0 || finish < 0) return source;
    start += 6;
    return source.mid(start, finish-start);
}


static void write_snippet(QTextCursor &cursor, const XmlElement *snippet)
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
        if (!gm.isEmpty())
        {
            // Remember the main frame, so we can switch back to it!
            QTextFrame *current_frame = cursor.currentFrame();

            // Delete the current block (we are replacing it with a frame) - TODO doesn't work
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.removeSelectedText();

            // Create a frame for the GM directions
            QTextFrameFormat frame;
            // TODO: background-color: floralwhite; border: 1px solid tan;
            frame.setBorderBrush(QColor("tan"));
            frame.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            frame.setBackground(QColor("floralwhite"));
            frame.setMargin(0);
            cursor.insertFrame(frame);
            for (auto gm_directions: gm)
                write_para_children(cursor, gm_directions, sn_style);
            // Remove the trailing block since we don't need it in the frame
            // (otherwise there is a blank line at the end of the GM snippet section)
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.removeSelectedText();

            // Switch back to the main frame
            cursor = current_frame->lastCursorPosition();
        }

        QList<XmlElement *> children = snippet->xmlChildren("contents");
        if (!children.isEmpty())
        {
            for (auto contents: children)
                write_para_children(cursor, contents, sn_style);
        }
    }
    else if (sn_type == "Labeled_Text")
    {
        for (auto contents: snippet->xmlChildren("contents"))
        {
            // TODO: BOLD label needs to be put inside <p class="RWDefault"> not in front of it
            write_para_children(cursor, contents, sn_style, /*prefix*/snippet->snippetName());  // has its own 'p'
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
                    //writeExtObject(doc, ext_object->attribute("name"), contents->p_byte_data,
                    //               filename, sn_style, annotation);
                    if (annotation)
                    {
                        QString cont;
                        cont.append("<p style='font-style:large'><b><u>"); // TODO
                        write_para_children(cursor, annotation, sn_type, /*prefix*/ "Portfolio");
                        cont.append("</u></b>");
                        cursor.insertHtml(cont);
                    }

                    // Put in markers for statblock
                    QByteArray store = contents->byteData();
                    QBuffer buffer(&store);
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
                                    cursor.insertHtml("<div style='border: 1px solid tan; padding: 2px; margin 5px; background-color: azure;'>" + get_body(file.readAll()) + "</div>");
                                    cursor.insertBlock();
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
                        write_image(cursor, ext_object->attribute("name"), contents->byteData(),
                                   /*mask*/nullptr, filename, sn_style, annotation);
                    }
                    else if (filename.endsWith(".html") ||
                             filename.endsWith(".htm")  ||
                             filename.endsWith(".rtf"))
                    {
                        QTextCharFormat orig_format = cursor.charFormat();
                        QTextCharFormat format = orig_format;
                        format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
                        format.setFontWeight(QFont::Bold);
                        QFont font = format.font();
                        font.setPointSize(font.pointSize() + 2);
                        format.setFont(font);
                        cursor.setCharFormat(format);
                        cursor.insertText(sn_type);
                        cursor.setCharFormat(orig_format);

                        //if (annotation) writeParaChildren(doc, annotation, sn_type, image_name);
                        cursor.insertHtml("<div>"+ get_body(contents->byteData()) + "</div>");
                        cursor.insertBlock();
                    }
                    else
                    {
                        write_ext_object(cursor, ext_object->attribute("name"), contents->byteData(), filename, sn_style, annotation);
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

            write_image(cursor, smart_image->attribute("name"), contents->byteData(),
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
                    if (!link.isEmpty()) writeTopicHref(doc, link);

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
            write_para_children(cursor, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        //else
        //    write_para(cursor, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Date_Range")
    {
        XmlElement *date       = snippet->xmlChild("date_range");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (date == nullptr) return;

        QString bodytext = "From: " + date->attribute("display_start") + " To: " + date->attribute("display_end");
        if (annotation)
            write_para_children(cursor, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        //else
        //    write_para(cursor, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
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
            write_para_children(cursor, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(cursor, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    else if (sn_type == "Numeric")
    {
        XmlElement *contents   = snippet->xmlChild("contents");
        XmlElement *annotation = snippet->xmlChild("annotation");
        if (contents == nullptr) return;

        const QString &bodytext = contents->childString();
        if (annotation)
            write_para_children(cursor, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(cursor, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
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
            write_para_children(cursor, annotation, sn_style, /*prefix*/snippet->snippetName(), bodytext);
        else
            write_para(cursor, snippet, sn_style, /*prefix*/snippet->snippetName(), bodytext);
    }
    // Hybrid_Tag
}


static void write_section(QTextCursor &cursor, const XmlElement *section, int level)
{
    // Start with HEADER for the section
    cursor.insertHtml(QString("<h%1>%2</h%1>").arg(level).arg(section->attribute("name")));
    cursor.insertBlock();

    // Write snippets
    for (auto snippet: section->xmlChildren("snippet"))
    {
        write_snippet(cursor, snippet);
    }

    // Write nested sections
    for (auto subsection: section->xmlChildren("section"))
    {
        write_section(cursor, subsection, level+1);
    }
}


static void write_topic(QTextCursor &cursor, const XmlElement *topic, bool reset)
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
            links.add(link->attribute("target_name"), link->attribute("target_id"));
        }
    }

    // HTML4 allows an anchor to be defined with "<a name='id'>"
    QString title;

    static QTextBlockFormat default_format;
    if (reset)
        default_format = cursor.blockFormat();
    else
        cursor.setBlockFormat(default_format);

    QTextBlockFormat format = default_format;
    format.setAlignment(Qt::AlignCenter);
    format.setPageBreakPolicy(QTextBlockFormat::PageBreak_AlwaysBefore);
    cursor.setBlockFormat(format);
    // TODO: gainsboro background colour
    title.append(QString("<h1><a name='%1'>").arg(topic->attribute("topic_id")));
    if (topic->hasAttribute("prefix")) title.append(QString("%1 - ").arg(topic->attribute("prefix")));
    title.append(topic->attribute("public_name"));
    if (topic->hasAttribute("suffix")) title.append(QString(" (%1)").arg(topic->attribute("suffix")));
    title.append("</a></h1>");
    cursor.insertHtml(title);
    cursor.insertBlock();
    cursor.setBlockFormat(default_format);

    // Maybe some aliases (in own section for smaller font?)
    for (auto alias : topic->xmlChildren("alias"))
    {
        cursor.insertHtml("<i>" + alias->attribute("name") + "</i>");
        cursor.insertBlock();
    }

    // Process all <sections>, applying the linkage for this topic
    for (auto section: topic->xmlChildren("section"))
    {
        write_section(cursor, section, /*level*/ 2);
    }

    // Provide summary of links to child topics
    auto child_topics = topic->xmlChildren("topic");
    std::sort(child_topics.begin(), child_topics.end(), sort_all_topics);

    if (!child_topics.isEmpty())
    {
        QString children;
        cursor.insertHtml("<h2>Child Topics</h2>");
        QTextBlockFormat non_list_format = cursor.blockFormat();
        QTextList *list = nullptr;
        for (auto child: child_topics)
        {
            if (!list)
                list = cursor.insertList(QTextListFormat::ListDisc);
            else
                cursor.insertBlock();
            cursor.insertHtml(QString("<li><a href='#%1'>%2</a>\n").arg(child->attribute("topic_id")).arg(child->attribute("public_name")));
        }
        // End the list (switch to a normal block format) - but this puts in an extra blank line! TODO
        cursor.insertBlock();
        cursor.setBlockFormat(non_list_format);
    }
}

static void write_first_page(QTextCursor &cursor, const XmlElement *root_elem)
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

    QTextBlockFormat basic_format = cursor.blockFormat();
    cursor.insertHtml(QString("<h1 align='center' style='margin: 100px;'>%1</h1>").arg(details->attribute("name")));
    cursor.insertBlock();

    basic_format.setBottomMargin(10);
    cursor.setBlockFormat(basic_format);
    cursor.insertHtml(QString("<b>Exported on</b> %1").arg(QDateTime::fromString(root_elem->attribute("export_date"), Qt::ISODate).toLocalTime().toString(Qt::SystemLocaleLongDate)));
    cursor.insertBlock();

    cursor.insertHtml(QString("<b>Generated by RWoutput tool on</b> %1").arg(QDateTime::currentDateTime().toString(Qt::SystemLocaleLongDate)));
    cursor.insertBlock();

    if (details->hasAttribute("summary"))
    {
        cursor.insertHtml("<h2>Summary</h2>");
        cursor.insertBlock();
        cursor.insertHtml(details->attribute("summary"));
        cursor.insertBlock();
    }

    if (details->hasAttribute("description"))
    {
        cursor.insertHtml("<h2>Description</h2>");
        cursor.insertBlock();
        cursor.insertHtml(details->attribute("description"));
        cursor.insertBlock();
    }

    if (details->hasAttribute("requirements"))
    {
        cursor.insertHtml("<h2>Requirements</h2>");
        cursor.insertBlock();
        cursor.insertHtml(details->attribute("requirements"));
        cursor.insertBlock();
    }

    if (details->hasAttribute("credits"))
    {
        cursor.insertHtml("<h2>Credits</h2>");
        cursor.insertBlock();
        cursor.insertHtml(details->attribute("credits"));
        cursor.insertBlock();
    }

    if (details->hasAttribute("legal"))
    {
        cursor.insertHtml("<h2>Legal</h2>");
        cursor.insertBlock();
        cursor.insertHtml(details->attribute("summary"));
        cursor.insertBlock();
    }

    if (details->hasAttribute("other_notes"))
    {
        cursor.insertHtml("<h2>Other Notes</h2>");
        cursor.insertBlock();
        cursor.insertHtml(details->attribute("other_notes"));
        cursor.insertBlock();
    }
}

/**
 * @brief genTextDocument
 * This routine puts the supplied XmlElement tree into the supplied QTextDocument using the HTML 4
 * subset that is supported by Qt.
 * This writes directly into a QTextDocument because QXmlStreamWriter would put closing element tags everywhere.
 * @param doc the QTextDocument into which the result should be placed
 * @param root a pointer to the root XmlElement for the source to be converted into a QTextDocument
 * @param max_image_width maximum width of an image
 * @param use_reveal_mask whether the reveal mask of the images should be used
 */
void genTextDocument(QTextDocument &doc,
                    const XmlElement *root,
                    int max_image_width,
                    bool use_reveal_mask)
{
#ifdef TIME_CONVERSION
    QElapsedTimer timer;
    timer.start();
#endif
    QTextCursor cursor(&doc);

    image_max_width   = max_image_width;
    apply_reveal_mask = use_reveal_mask;
    collator.setNumericMode(true);

    write_first_page(cursor, root);

    auto topics = root->findChildren<XmlElement*>("topic");
    QProgressDialog pbar("Generating QTextDocument", QString(), 0, topics.count());
    pbar.show();

    pbar.setLabelText("Sorting topics");
    std::sort(topics.begin(), topics.end(), sort_all_topics);
    pbar.setLabelText(QString("Generating QTextDocument for %1 topics").arg(topics.count()));

    int count = 0;
    for (auto topic : topics)
    {
        pbar.setValue(++count);
        write_topic(cursor, topic, /*reset*/ (count == 1));
        qApp->processEvents();
    }
#ifdef TIME_CONVERSION
    qInfo() << "TIME TO GENERATE HTML =" << timer.elapsed() << "milliseconds";
#endif
}
