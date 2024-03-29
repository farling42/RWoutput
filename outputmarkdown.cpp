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

#include "outputmarkdown.h"

#include <Qt>
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
#include <QStack>
#include <QStyle>
#include <QSettings>
#include <QStaticText>
#include <QTextStream>
#include <future>
#include <QDateTime>
#include <QRegularExpression>
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
static QCollator collator;              // allow alphanumeric sorting to do proper number comparisons
static bool category_folders = true;
static bool use_wikilinks = false;
static bool show_leaflet_pins = true;
static bool show_nav_panel = true;
static bool nav_at_start = true;
static int  max_index_level = 99;
static bool create_prefix_tag = false;
static bool create_suffix_tag = false;
static int  image_max_width   = -1;
static int  gumbofilenumber = 0;
static bool connections_as_graph=true;
static bool detect_dice_rolls=false;
static bool detect_html_dice_rolls=false;
static bool create_statblocks=true;
static bool create_5e_statblocks=false;
static bool use_admonition_gmdir=false;
static bool use_admonition_style=false;
static bool frontmatter_labeled_text=true;
static bool frontmatter_numeric=true;
static bool frontmatter_prefix_suffix=true;
static bool initiative_tracker=true;
static bool use_table_extended=false;
static bool create_por_link=true;

#undef DUMP_CHILDREN

static QHash<QString,QString> topic_filename;               // key=topic_id/plot_id, value=<valid filename for this topic/plot>
static QHash<const XmlElement*,QString> topic_full_name;    // key=topic_id, value=<prefix+public_name+suffix>
static QHash<QString,QString> tag_full_name;                // key=tag_id, value=name attribute of <domain> or <domain_global>

extern QString map_pin_title;
extern QString map_pin_description;
extern QString map_pin_gm_directions;

extern bool show_full_link_tooltip;
extern bool show_full_map_pin_tooltip;

static const QString oldAssetsDir("asset-files");
static const QString assetsDir("zz_asset-files");
static const QString templatesDir("templates");

static const QString newline("\n");
static const QString frontmatterMarker("---\n");
static const QString codeblock("```");
static const QString YAMLLIST("  - ");

static QString mainPageName;
static QString imported_date;
static QMap<QString,QString> global_names;      // key=<any *_id>, value=<"name of key_id">  - tag, facet, category, partition, topic, plot
#define DUMP_LEVEL 0


static QString RW_LINE_BREAK("<rwbr>");
static QChar   RW_LEFT_BRACKET  = QChar(8704);
static QChar   RW_RIGHT_BRACKET = QChar(8705);
static QString RW_ESCAPE_BAR{"&#124;"};


template<class T>const QString setJoin(QSet<T> &value, const QString &joiner) {
    const QStringList list = value.values();
    return list.join(joiner);
}


struct ExportLink;
struct ExportLink
{
    ExportLink(const QString target_id, int start, int length) :
        target_id(target_id), start(start), length(length) {}
    QString target_id;
    int start, length;
};
typedef QList<ExportLink> ExportLinks;
static ExportLinks NO_LINKS;

static inline void sortLinks(ExportLinks &list)
{
    // Get entries in order starting with HIGHEST start, and proceeding to LOWEST start.
    std::sort(list.begin(), list.end(), [](ExportLink left, ExportLink right) { return right.start < left.start; });
    // Remove duplicates
    QMutableListIterator<ExportLink> it(list);
    while (it.hasNext()) {
        ExportLink &item = it.next();
        if (it.hasNext() && item.start == it.peekNext().start)
            it.remove();
    }
}


static inline const QString validFilename(const QString &string)
{
    // full character list from https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    static const QRegularExpression invalid_chars("[<>:\"/\\|?*]", QRegularExpression::UseUnicodePropertiesOption);
    if (!invalid_chars.isValid()) qWarning() << "validFilename has invalid regexp";
    QString result(string);
    return result.replace(invalid_chars,"_");
}


static inline QString quotes(const QString &value)
{
    static const QChar QUOTE('"');
    return QUOTE + value + QUOTE;
}


static const QString snippetLabel(const XmlElement *elem)
{
    QString result;
    result = elem->attribute("label");   // For Labeled_Text with manual label
    if (!result.isEmpty()) return result;

    result = elem->attribute("facet_name");
    if (!result.isEmpty()) return result;

    return global_names.value(elem->attribute("facet_id"));
}


static inline const QString validTag(const QString &string)
{
    // Tag can only contain letters (case sensitive), digits, underscore and dash
    // "/" is allowed for nested tags

    // First replace is for things like "Region: Geographical", renaming it to just "Region-Geographical" rather than "Region--Geographical"
    // return result.replace(": ","-").replace(QRegularExpression("[^\\w-]"), "-");
    // QRegularExpression documentation says that "\w" - Matches a word character (QChar::isLetterOrNumber(), QChar::isMark(), or '_').
    // isLetterOrNumber matches Letter_* or Number_*  (Number_DecimalDigit (Nd), Number_Letter (Nl), Number_Other (No)
    // isMark           matches Mark_NonSpacing (Mn), Mark_SpacingCombining (Mc), Mark_Enclosing (Me)  (unicode class name)
    //
    // Officially, Obsidian only allows letters, numbers, and the symbols _ (underscore), - (dash)
    // reserving the use of / (forward slash) for nested tags.
    //
    // Unofficially, some other characters appear to be allowed
    //
    // Symbol_Other (°), Symbol_Modifier, Symbol_Currency (£), Symbol_Math
    //
    static const QRegularExpression invalid_chars("[^\\w°£¬]", QRegularExpression::UseUnicodePropertiesOption);
    if (!invalid_chars.isValid()) qWarning() << "validTag has invalid regexp";
    QString result(string);
    return result.replace(invalid_chars, "-");
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
    XmlElement *parent = topic->parent();
    if (parent && parent->objectName() == "topic")
        return parentDirName(parent) + QDir::separator() + topic_filename.value(parent->attribute("topic_id"));
    else
        return validFilename(global_names.value(topic->attribute("category_id")));
}


static const QString topicDirFile(const XmlElement *topic)
{
    // If the topic has children, then create a folder named after this note,
    // and put the note in it.
    // This is a parent topic, so create a folder to hold it.
    const QString filename = topic_filename.value(topic->attribute("topic_id"));
    if (category_folders)
    {
        return dirFile(validFilename(global_names.value(topic->attribute("category_id"))), filename) + ".md";
    }
    else
    {
        QString dirname = parentDirName(topic);
        if (topic->xmlChild("topic"))       // has at least one child
            dirname += QDir::separator() + filename;
        return dirFile(dirname, filename) + ".md";
    }
}


/**
 * @brief xmlChildren
 * Find all descendents WITHIN THIS TOPIC with the matching name.
 * @param name
 * @return
 */
inline QList<const XmlElement *> topicDescendents(const XmlElement *parent, const QString &name)
{
    QList<const XmlElement*>result;
    if (parent->objectName() == name) result.append(parent);

    foreach (const auto &child, parent->xmlChildren())
    {
        if (child->objectName() != "topic") result.append(topicDescendents(child, name));
    }
    return result;
}


static inline const QString mermaid_node_raw(const QString &name, const QString &label, bool internal_link=true)
{
    QString result = name + "([" + quotes(label) + "])";
    if (internal_link) result += "; class " + name + " internal-link";
    return result;
}


static inline const QString mermaid_node(const QString &topic_id)
{
    return mermaid_node_raw(topic_id, topic_filename.value(topic_id));
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
    //QStringList files{"realmworks.css"};

    const QDir snippetsDir(".obsidian/snippets/");
    if (!snippetsDir.exists()) QDir::current().mkpath(snippetsDir.path());

    foreach (const auto &fileinfo, QDir(":/markdown/").entryInfoList({"*.css"}))
    {
        QString pathname = fileinfo.absoluteFilePath();
        QString filename = fileinfo.fileName();

        // Put file into snippets directory
        QFile destfile{snippetsDir.filePath(filename)};
        if (destfile.exists()) destfile.remove();

        // Qt copies the file and makes it read-only!
        QFile::copy(pathname, destfile.fileName());
        destfile.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);

        // Put a copy into the base directory too, for convenience.
        QFile basefile{QDir::current().filePath(filename)};
        if (basefile.exists()) basefile.remove();
        // Qt copies the file and makes it read-only!
        QFile::copy(pathname, basefile.fileName());
        basefile.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    }
}


static inline QString addStyle(const QString &from, bool is_bold)
{
    if (is_bold)
        return "**" + from + "**";
    else
        return from;
}


static inline QString capitalise(const QString &from)
{
    QString result{from};
    if (!result.isEmpty()) result.replace(0, 1, from.front().toUpper());
    return result;
}


static inline QString heading(int level, const QString &title)
{
    return QString(level,QChar('#')) + ' ' + title + newline;
}


static QString template_partition(int level, const XmlElement *partition)
{
    QString result = heading(level, partition->attribute("name"));
    result.reserve(1000);
    static const QString MARKER_START{"{{ "};
    static const QString MARKER_FINISH{" }}"};

    for (auto &child: partition->xmlChildren())
    {
        QString elem = child->objectName();
        if (elem == "facet_global" || elem == "facet")
        {
            QString name = child->attribute("name");
            QString fctype = child->attribute("type");
            if (fctype == "Hybrid_Tag" || fctype == "Tag_Standard")
            {
                QString tagname = validTag(global_names.value(child->attribute("domain_id")));
                QString tag_id   = child->attribute("tag_id");  // only with Hybrid_Tag
                if (!tag_id.isEmpty()) tagname.append('/' + validTag(global_names.value(tag_id)));

                //QString is_lock  = child->attribute("is_lock_domain");
                //QString is_multi = child->attribute("is_multi_tag");

                result += addStyle(name + ':', child->attribute("label_style") == "Bold") +
                        ' ' + MARKER_START + "#" + tagname + MARKER_FINISH + newline;
            }
            else if (fctype == "Labeled_Text" || fctype == "Numeric" || fctype == "Tag_Multi_Domain")
            {
                result += addStyle(name + ':', child->attribute("label_style") == "Bold") +
                        ' ' + MARKER_START + name.toLower() + MARKER_FINISH + newline;
            }
            else if (fctype == "Multi_Line")
            {
                result += addStyle(name + ':', true) +
                        ' ' + MARKER_START + name.toLower() + MARKER_FINISH + newline;
            }
            else if (fctype == "Date_Game")
            {
                QString format = child->attribute("date_format");

                result += addStyle(name + ':', child->attribute("label_style") == "Bold") +
                        ' ' + MARKER_START + format + MARKER_FINISH + newline;
            }
            else if (fctype == "Date_Range")
            {
                QString format = child->attribute("date_format");

                result += addStyle(name + ':', child->attribute("label_style") == "Bold") +
                        ' ' + MARKER_START + format + MARKER_FINISH + newline;
            }
            else if (fctype == "Picture" || fctype == "Smart_Image" || fctype == "Portfolio" || fctype == "Statblock")
            {
                //QString size  = child->attribute("thumbnail_size");  // Picture + Smart_Image
                result += addStyle(name + ':', child->attribute("label_style") == "Bold") +
                        ' ' + MARKER_START + fctype + ':' + name.toLower() + MARKER_FINISH + newline;
            }
            else
            {
                result += MARKER_START + fctype.toLower() + MARKER_FINISH + newline;
                qDebug() << "Unsupported facet type:" << fctype;
            }
        }
        else if (elem == "partition" || elem == "partition_global")
        {
            result += template_partition(level+1, child);
        }
        else if (elem == "description" || elem == "summary" || elem == "purpose")
        {
            QString contents{child->childString()};
            if (!contents.isEmpty())
            {
                result += "%% " + capitalise(elem) + ": " + contents + " %%" + newline;
            }
            else
                qDebug() << "no text for partition" << elem;
        }
        else
            qDebug() << "Unsupported element in partition:" << elem;

        // Maybe have some children?
        if (elem == "partition" || elem == "partition_global")
            ;
        else
            for (auto node: child->xmlChildren())
            {
                QString childtype = node->objectName();
                if (childtype == "description" || childtype == "summary" || childtype == "purpose")
                {
                    QString contents{node->childString()};
                    if (!contents.isEmpty())
                    {
                        result += "%% " + capitalise(childtype) + ": " + contents + " %%" + newline;
                    }
                    else
                        qDebug() << "no text for partition" << childtype;
                }
                else if (!childtype.isEmpty())
                    qDebug() << "Unexpected child of facet:" << elem << ">" << childtype;
            }
    }
    // Ensure blank line after the partition
    if (!result.endsWith("\n\n")) result += newline;
    return newline + result;
}


static void write_template(const XmlElement *category)
{
    QFile outfile(dirFile(templatesDir, validFilename(category->attribute("name"))) + ".md");
    if (!outfile.open(QFile::WriteOnly))
    {
        qWarning() << "Failed to create template file for category" << category->attribute("name");
        return;
    }
    QTextStream stream(&outfile);

    stream << heading(1, category->attribute("name"));

    for (auto &child: category->xmlChildren())
    {
        QString elem = child->objectName();
        if (elem == "partition" || elem == "partition_global")
        {
            stream << template_partition(2, child);
        }
        else if (elem == "description" || elem == "summary" || elem == "purpose")
        {
            QString contents{child->childString()};
            if (!contents.isEmpty())
            {
                stream << "%% " + capitalise(elem) + ": " + contents + " %%" + newline;
            }
            else
                qDebug() << "no text for category" << elem;
        }
        else if (elem == "category" || elem == "category_global")
            ;  // We don't care about nested categories
        else
            qWarning() << "Unsupported element in category:" << child->objectName();
    }

    outfile.close();
}


static void write_category_templates(const XmlElement *structure)
{
    foreach (const auto &cat, structure->findChildren<XmlElement*>("category_global"))
        write_template(cat);

    foreach (const auto &cat, structure->findChildren<XmlElement*>("category"))
        write_template(cat);
}


static const QString write_meta_child(const QString &meta_name, const XmlElement *details, const QString &child_name)
{
    XmlElement *child = details->xmlChild(child_name);
    if (!child) return "";
    return "**" + meta_name + ":** " + child->childString() + "\n\n";
}


static const QString write_head_meta(const XmlElement *root_elem)
{
    QString result;
    result.reserve(1000);

    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    if (details == nullptr) return result;

    // From "2021-12-04T17:51:29Z" (ISO 8601) to something readable
    QDateTime stamp = QDateTime::fromString(root_elem->attribute("export_date"), Qt::ISODate);
    result += "**Exported from Realm Works:** " + stamp.toString(QLocale::system().dateTimeFormat()) + "\n\n";

    result += "**Created By:** " + qApp->applicationName() + " v" + qApp->applicationVersion() + " on " + imported_date + "\n\n";

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
    result.reserve(result.length() + 100);

    // Escape any [ characters
    // Replace non-break-spaces with normal spaces (RW puts nbsp whenever more than one space is required in some text)
    // Occasionally there is a zero-width-space, which we'll assume should be no space at all.

    // get_content_text is now putting internal links before this is called, so we need to handle [[ specially.
    // &forall; = \u8704 (U+2200) - to get [[ passed the first replace
    return result.replace("\[","\\[").replace('*',"\\*").replace('~',"\\~").replace(RW_LEFT_BRACKET, '[').replace(RW_RIGHT_BRACKET, ']');
}

#ifdef TOOLTIP
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
    result.reserve(1000);

    if (!title.isEmpty())
    {
        if (description.isEmpty() && gm_directions.isEmpty())
            result = title;
        else
            result = map_pin_title.arg(title);
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
#endif


static inline QString createMarkdownLink(const QString &filename, const QString &label, int max_width=-1)
{
    Q_UNUSED(max_width)
    // Label is always present
    // The filename needs spaces replaced by URL syntax.
    return QString("[%1](%2)").arg(label.isEmpty() ? filename : label, QString(filename).replace(" ", "%20"));
}


static inline QString createWikilink(const QString &filename, const QString &label, int max_width=-1)
{
    QString extra;
    if (max_width>0) extra=QString("|%1").arg(max_width);
    if (label.isEmpty() || filename == label)
        return QString("[[%1%2]]").arg(filename, extra);
    else
        return QString("[[%1|%2%3]]").arg(filename, label, extra);
}


static inline QString createLink(const QString &filename, const QString &label, int max_width=-1)
{
    if (use_wikilinks)
        return createWikilink(filename, label, max_width);
    else
        return createMarkdownLink(filename, label, max_width);
}


static inline QString internal_link(const QString &topic_id, const QString &label = QString(), int max_width=-1)
{
    QString filename = topic_filename.value(topic_id);
    if (filename.isEmpty()) filename = validFilename(topic_id);
    return createLink(filename, label, max_width);
}


static inline QString topic_link(const XmlElement *topic)
{
    return internal_link(topic->attribute("topic_id"), topic_full_name.value(topic));
}


static QString xmlValue(const XmlElement *node, const QStringList fields, const QString &attributename)
{
    const XmlElement *find = node;
    for (auto &field: fields)
    {
        find = node->xmlChild(field);
        if (!find) return QString();
    }
    return find->attribute(attributename);
}


static inline const QString getGumboAttribute(const GumboNode *node, const QString &name)
{
    GumboAttribute **attributes = reinterpret_cast<GumboAttribute**>(node->v.element.attributes.data);
    for (unsigned count = node->v.element.attributes.length; count > 0; --count)
    {
        const GumboAttribute *attr = *attributes++;
        if (name == attr->name) return attr->value;
    }
    return QString();
}


/*
 * Text enhancements
 */
struct TextStyle {
private:
    bool is_empty=true;
    bool bold=false;
    bool italic=false;
    bool strikethru=false;
    bool underline=false;
    bool superscript=false;
    bool subscript=false;

    void clear()
    {
        bold=false;
        italic=false;
        strikethru=false;
        underline=false;
        superscript=false;
        subscript=false;
        is_empty=true;
    }

public:
    TextStyle() {};
    static const TextStyle NULL_STYLE;

    static TextStyle fromStyle(const QString &details)
    {
        TextStyle result;
        result.decodeStyle(details);
        return result;
    }
    static TextStyle fromNode(const QString &nodename)
    {
        TextStyle result;
        if (nodename == "sup")
        {
            result.superscript = true;
            result.is_empty    = false;
        }
        else if (nodename == "sub")
        {
            result.subscript = true;
            result.is_empty  = false;
        }
        else if (nodename == "b")
        {
            result.bold     = true;
            result.is_empty = false;
        }
        else if (nodename == "i")
        {
            result.italic   = true;
            result.is_empty = false;
        }
        return result;
    }
    bool operator==(const TextStyle &other)
    {
        return bold==other.bold &&
                italic==other.italic &&
                subscript==other.subscript &&
                superscript==other.superscript &&
                underline==other.underline &&
                strikethru==other.strikethru;
    }
    TextStyle operator+(const TextStyle &other) const
    {
        TextStyle result = *this;
        if (other.bold)        result.bold        = true;
        if (other.italic)      result.italic      = true;
        if (other.subscript)   result.subscript   = true;
        if (other.superscript) result.superscript = true;
        if (other.underline)   result.underline   = true;
        if (other.strikethru)  result.strikethru  = true;
        result.setIsEmpty();
        return result;
    }
    TextStyle &operator+=(const TextStyle &other)
    {
        if (other.bold)        bold        = true;
        if (other.italic)      italic      = true;
        if (other.subscript)   subscript   = true;
        if (other.superscript) superscript = true;
        if (other.underline)   underline   = true;
        if (other.strikethru)  strikethru  = true;
        setIsEmpty();
        return *this;
    }
    TextStyle operator-(const TextStyle &other) const
    {
        TextStyle result = *this;
        if (other.bold)        result.bold        = false;
        if (other.italic)      result.italic      = false;
        if (other.subscript)   result.subscript   = false;
        if (other.superscript) result.superscript = false;
        if (other.underline)   result.underline   = false;
        if (other.strikethru)  result.strikethru  = false;
        result.setIsEmpty();
        return result;
    }
    TextStyle &operator-=(const TextStyle &other)
    {
        if (other.bold)        bold        = false;
        if (other.italic)      italic      = false;
        if (other.subscript)   subscript   = false;
        if (other.superscript) superscript = false;
        if (other.underline)   underline   = false;
        if (other.strikethru)  strikethru  = false;
        setIsEmpty();
        return *this;
    }
    const QString toString() const
    {
        QStringList result;
        if (bold)        result.append("bold");
        if (italic)      result.append("italic");
        if (strikethru)  result.append("strikethrough");
        if (underline)   result.append("underline");
        if (superscript) result.append("superscript");
        if (subscript)   result.append("subscript");
        return result.join(",");
    };
    bool isEmpty() const { return is_empty; };

private:
    void setIsEmpty()
    {
        is_empty = !(bold || italic || strikethru || underline || superscript || subscript);
    }
    void decodeStyle(const QString &style)
    {
        // It could be a NODE name rather than a style
        if (style == "sup")
        {
            superscript = true;
            return;
        }
        else if (style == "sub")
        {
            subscript = true;
            return;
        }
        foreach (const auto &part, style.split(";"))
        {
            const auto bits = part.split(":");
            if (bits.length() != 2) continue;
            const auto values = bits.last().split(" ");
            const auto attr = bits.first();
            if (attr == "font-weight")
            {
                foreach (const auto &value, values)
                {
                    if (value == "bold") bold = true;
                    //else qWarning() << "Unknown element in font-weight: " << value;
                }
            }
            else if (attr == "font-style")
            {
                // normal|italic|oblique|initial|inherit
                foreach (const auto &value, values)
                {
                    if (value == "italic") italic = true;
                    //else qWarning() << "Unknown element in font-style: " << value;
                }
            }
            else if (attr == "text-decoration" || attr == "text-decoration-style")
            {
                // solid|double|dotted|dashed|wavy|initial|inherit
                foreach (const auto &value, values)
                {
                    if      (value == "line-through") strikethru = true;
                    else if (value == "underline")    underline  = true;
                    //else if (value == "none") ;
                    //else qWarning() << "Unknown element in text-decoration: " << value;
                }
            }
            else if (attr != "color" &&
                     attr != "font-family" &&
                     attr != "background-color" &&
                     attr != "font-size")
            {
                ; //qWarning() << "Unknown element of style: " << bits.first();
            }
        }
        setIsEmpty();
    };
    friend class TextStyleManager;
};
const TextStyle TextStyle::NULL_STYLE;

typedef QHash<QString,TextStyle> GumboStyles;


class TextStyleManager
{
public:
    TextStyle currentStyle() const
    {
        return this->current;
    }
    /**
     * @brief start
     * Put into @result all the markers required to define the styles given in @style
     * @param result
     * @param style
     */
    void start(QString &result, TextStyle style)
    {
        current = TextStyle::NULL_STYLE;
        change(result, style);
    }
    /**
     * @brief finish
     * Put into @result all the markers required to unset all the currently active styles.
     * @param result
     */
    void finish(QString &result)
    {
        change(result, TextStyle::NULL_STYLE);
    };
    /**
     * @brief change
     * Add flags to @result to change the current styling to match @style.
     * @param result
     * @param style
     */
    void change(QString &result, const TextStyle &tostyle)
    {
        if (current==tostyle) return;

        // Remove any elements no longer required
        if (!current.is_empty)
        {
            // Which things to switch off
            // Ensure space (if any) is AFTER the close
            bool space = result.endsWith(' ');
            if (space) result.truncate(result.length()-1);
            removeFlag(result, current.subscript,   tostyle.subscript,   "</sub>", "<sub>");
            removeFlag(result, current.superscript, tostyle.superscript, "</sup>", "<sup>");
            removeFlag(result, current.underline,   tostyle.underline,   "</u>",   "<u>");
            removeFlag(result, current.strikethru,  tostyle.strikethru,  "~~",     "~~");
            removeFlag(result, current.bold,        tostyle.bold,        "**",     "**",   false);
            removeFlag(result, current.italic,      tostyle.italic,      "*",      "*",    false);
            if (space) result += ' ';
        }
        // Add any elements not currently present
        if (!tostyle.is_empty)
        {
            // Which things to switch on (opposite order to OFF)
            // Only set flag in current if switching on
            addFlag(result, current.italic,      tostyle.italic,      "*");
            addFlag(result, current.bold,        tostyle.bold,        "**");
            addFlag(result, current.strikethru,  tostyle.strikethru,  "~~");
            addFlag(result, current.underline,   tostyle.underline,   "<u>");
            addFlag(result, current.superscript, tostyle.superscript, "<sup>");
            addFlag(result, current.subscript,   tostyle.subscript,   "<sub>");
        }

        // Remember the current style
        current = tostyle;
    }

private:
    TextStyle current;

    inline void addFlag(QString &result, const bool &from, const bool &to, const QString &starttag)
    {
        if (!from && to)
        {
            result += starttag;
        }
    }
    inline void removeFlag(QString &result, const bool &from, const bool &to, const QString &endtag, const QString &starttag, const bool optimise=true) const
    {
        if (from && !to)
        {
            // Doesn't cancel bold+italic properly
            if (optimise && result.endsWith(starttag))
                // There is no text between the start and end of this formatting, so remove the START indicator
                result.truncate(result.length() - starttag.length());
            else
                result.append(endtag);
        }
    }
};


GumboStyles getStyles(const GumboNode *node)
{
    GumboStyles result;
    if (!node) return result;
    // Check the attribute type="text/css"
    if (getGumboAttribute(node, "type") != "text/css") return result;

    GumboNode **children = reinterpret_cast<GumboNode**>(node->v.element.children.data);
    for (unsigned count = node->v.element.children.length; count > 0; --count)
    {
        const GumboNode *node = *children++;
        if (node->type == GUMBO_NODE_TEXT)
        {
            // each line is of the form:
            // .cs1157FFE2{text-align:right;text-indent:0pt;margin:0pt 0pt 0pt 0pt}
            QString body(node->v.text.text);
            foreach (const auto &oneline, body.trimmed().split("\n", QString::SkipEmptyParts))
            {
                // Some files have more than one style on a single line
                foreach (const auto &line, oneline.trimmed().split("}", QString::SkipEmptyParts))
                {
                    // remove trailing "}"
                    QStringList parts = line.split("{");
                    if (parts.length() != 2) {
                        qWarning() << "Invalid syntax in GUMBO style: " << line;
                    }
                    result.insert(parts.first().mid(1), TextStyle::fromStyle(parts.last()));
                }
            }
        }
    }
    return result;
}


static const QString hlabel(const QString &label)
{
    return "**" + label + "**: ";
}


static const QString escape_bracket(const QString &input)
{
    QString result = input;
    result.replace('[', RW_LEFT_BRACKET).replace(']', RW_RIGHT_BRACKET);
    return result;
}

/**
 * @brief replace_dice
 * Examine @string for any patterns that might be dice rolls, and replace with `dice: <expr>`
 * @param string
 */
static void replace_dice(QString &string)
{
    // regex101.com says the following is correct:   ([^\w]|^)(\d*[dD]\d+(\s*[+-]\s*(\d*[dD]\d+|\d+))*)($|[^\w])
    // the REGEXP has to check that letters don't immediately precede or follow the match
    // but we have to escape each backslash
    static const QRegularExpression dice_regexp("(^|[^\\w])(\\d*[dD]\\d+(\\s*[+-]\\s*(\\d*[dD]\\d+|\\d+))*)($|[^\\w])", QRegularExpression::UseUnicodePropertiesOption);
  //  static const QRegularExpression dice_regexp("(^|[^\\w])\\d*[dD]\\d+(\\s*[+-]\\s*(\\d*[dD]\\d+|\\d+))*($|[^\\w])", QRegularExpression::UseUnicodePropertiesOption|QRegularExpression::DontCaptureOption);
    if (!dice_regexp.isValid()) qDebug() << "dice_regexp is invalid:" << dice_regexp.errorString();
    QRegularExpressionMatchIterator it = dice_regexp.globalMatch(string);
    if (!it.hasNext()) return;

    // The iterator only runs forward, so get the pairs into reverse order
    struct Pair { int start, last; };
    QStack<Pair> pairs;
    pairs.reserve(10);
    while (it.hasNext())
    {
        // Match #2 contains the actual expression, rather than the check markers either side
        // which avoid letters touching the front or back of the suspected dice string.
        QRegularExpressionMatch match = it.next();
        if (match.lastCapturedIndex() < 2)
        {
            qWarning() << "replace_dice didn't find two matching expressions: which it always should!";
            continue;
        }
        pairs.append(Pair{match.capturedStart(2), match.capturedEnd(2) });
    }
    // Update the string starting at the end and working backwards
    while (!pairs.isEmpty())
    {
        Pair set = pairs.pop();
        string.insert(set.last, '`');       // do this FIRST, before the string is shifted right
        string.insert(set.start, "`dice: ");
    }
}

/**
 * @brief getTextChildren
 * Return the string built from all the text elements, ignoring all formatting.
 * @param node
 * @param allowWhitespace
 * @return
 */
static const QString getTextChildren(const GumboNode *node, bool allowWhitespace)
{
    QString result;

    switch (node->type)
    {
    case GUMBO_NODE_WHITESPACE:
        if (!allowWhitespace) break;
        [[fallthrough]];
    case GUMBO_NODE_TEXT:
    case GUMBO_NODE_CDATA:
        result += QString(node->v.text.text);
        break;

    case GUMBO_NODE_ELEMENT:
    {
        const QString tag = gumbo_normalized_tagname(node->v.element.tag);
        allowWhitespace = (tag=="p" || tag=="span");
        GumboNode **children = reinterpret_cast<GumboNode**>(node->v.element.children.data);
        for (unsigned count = node->v.element.children.length; count > 0; --count)
        {
            result += getTextChildren(*children++, allowWhitespace);
        }
    }
        break;

    default:
        break;
    }

    return result;
}


static const GumboNode *getGumboChild(const GumboNode *parent, const QString &name)
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

/**
 * @brief textContent
 * Extract only the text elements from the given node tree (just like in JS Node::textContent)
 * @param node
 * @return
 */
static const QString textContent(const QString &source, bool dice=true)
{
    // If no HTML, then simply return the original text.
    if (source.indexOf(">") < 0) return source;

    QString result;
    GumboOutput *output = gumbo_parse(source.toUtf8());
    if (output) {
        if (output->root)
        {
            auto body = getGumboChild(output->root, "body");
            if (body)
                result = getTextChildren(body, false);
            else
                qWarning() << "textContent: No body found in" << source;
        }
        // Get GUMBO to release all the memory
        gumbo_destroy_output(&kGumboDefaultOptions, output);
    }
    if (dice && detect_dice_rolls) replace_dice(result);
    return result;
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
    result.reserve(1000);
    if (!image_name.isEmpty()) result += heading(3, image_name);

    if (show_leaflet_pins && !pins.isEmpty())
    {
        int height = image_size.height();
        // The leaflet plugin for Obsidian uses latitude/longitude, so (y,x)
        result += newline + codeblock + "leaflet" + newline;
        if (!image_name.isEmpty()) result += "id: " + image_name + newline;
        result += "image: [[" + filename + "]]\n";
        if (height > 1500) result += "height: " + mapCoord(height * 2) + "px\n";   // double the scaled height seems to work
        result += "draw: false\n";
        result += "showAllMarkers: true\n";
        result += "preserveAspect: true\n";   // added to Leaflet in 4.4.0
        result += "bounds:\n";
        result += YAMLLIST + "[0, 0]\n";
        result += YAMLLIST + "[" + mapCoord(height) + ", " + mapCoord(image_size.width()) + "]\n";

        // Create the clickable MAP on top of the map
        foreach (const auto &pin, pins)
        {
            int x = pin->attribute("x").toInt() / divisor;
            int y = pin->attribute("y").toInt() / divisor - pin_size;

            // Build up a tooltip from the text configured on the pin
            QString link = pin->attribute("topic_id");
#ifdef TOOLTIP
            QString pin_name = pin->attribute("pin_name");
            QString description = get_elem_string(pin->xmlChild("description"));
            QString gm_directions = get_elem_string(pin->xmlChild("gm_directions"));
            QString tooltip = build_tooltip(pin_name, description, gm_directions);
#endif
            result += "marker: default, " + mapCoord(height-y) + ", " + mapCoord(x) + ",";
            if (link.isEmpty())
            {
                // No link, but explicit tooltip
                result += ",unknown";
            }
            else
            {
                result += internal_link(link);
                //if (tooltip != link_name) result += ", " + quotes(tooltip.replace("\"","'"));
            }
            result += newline;
        }
        result += codeblock;
    }
    else
    {
        // No pins required
        result += "!" + createLink(filename, image_name, (image_size.width() > image_max_width) ? image_max_width : -1);
    }
    // Create a link to open the file externally, either using the annotation as a link, or just a hard-coded string
    result += newline + createLink(filename, annotation.isEmpty() ? "open outside" : annotation) + newline;
    return result;
}


static bool write_binary_file(const QString &filename, const QByteArray &data)
{
    QFile file(dirFile(assetsDir, filename));
    if (!file.open(QFile::WriteOnly))
    {
        qWarning() << "writeExtObject: failed to open file for writing:" << filename;
        return false;
    }
    file.write (data);
    file.close();
    return true;
}


/**
 * @brief write_ext_object
 * No newline at the end
 * @param obj_name
 * @param data
 * @param filename
 * @param annotation
 * @return
 */
static const QString write_ext_object(const QString &obj_name, const QByteArray &data, const QString &filename, const QString &annotation)
{
    Q_UNUSED(obj_name)

    // Write the asset data to an external file
    if (!write_binary_file(filename, data))
    {
        return QString();
    }

    QString result;
    result.reserve(1000);
    if (!obj_name.isEmpty()) result += heading(3, obj_name);
    result += "!" + createLink(filename, filename);

    // Create a link to open the file externally, either using the annotation as a link, or just a hard-coded string
    result += createLink(filename, annotation.isEmpty() ? "open outside" : annotation);
    return result;
}


static inline QString tag_string(const QString &domain, const QString &label)
{
    return validTag(domain) + '/' + validTag(label);
}


static const QString getTags(const QString &prefix, const XmlElement *node, bool withnl=true)
{
    const auto tag_nodes = node->xmlChildren("tag_assign");
    if (tag_nodes.length() == 0) return QString();

    QStringList tags;
    foreach (const auto &tag_assign, tag_nodes)
    {
        QString tag = tag_full_name.value(tag_assign->attribute("tag_id"));
        // Don't include:
        //   Export/<any tag>  <- always present on every single topic during export.
        //   Import/<any tag>  <- quite often present on imports.
        //   Utility/Empty     <- auto-added by Realm Works durin quick topic creation
        if (!(tag.isEmpty() || tag.startsWith("Export/") || tag.startsWith("Import/") || tag == "Utility/Empty"))
                tags.append("#" + tag);
    }
    QString result;
    result.reserve(100);
    if (tags.length() > 0)
    {
        result += tags.join(" ");
        if (withnl) result = prefix + result + newline;
    }
    return result;
}


static TextStyle inlineStyle(const GumboNode *node, const GumboStyles &cssStyles)
{
    TextStyle result;

    // if node has class="..." use it if it is in the decoded cssStyles
    const QString nodeclass = getGumboAttribute(node, "class");
    if (!nodeclass.isEmpty() && cssStyles.contains(nodeclass))
    {
        result = result + cssStyles[nodeclass];
    }
    // If node has style="..." use it
    const QString nodestyle = getGumboAttribute(node, "style");
    if (!nodestyle.isEmpty())
    {
        result = result + TextStyle::fromStyle(nodestyle);
    }
    // probably new span, so cancel old span
    return result;
}


/**
 * @brief decode_gumbo
 * Takes the GUMBO tree and calls the relevant methods of QTextStream
 * to reproduce it in Markdown.
 * @param node
 */

static const QString decode_gumbo(const GumboNode *parent, const GumboStyles &cssStyles, TextStyleManager &styleManager, int nestedTableCount=0, const QString &listtype=QString(), const bool allowWhitespace=false)
{
    QString result;
    result.reserve(1000);

    // The current style, if any, that is in effect at this node level.
    // (child levels might change it independently of this style)
    TextStyle original_style;
    bool original_set  = false;
    bool style_changed = false;
    bool new_style_change=false;
    int start_length;

    GumboNode **children = reinterpret_cast<GumboNode**>(parent->v.element.children.data);
    for (unsigned count = parent->v.element.children.length; count > 0; --count)
    {
        const GumboNode *node = *children++;

        switch (node->type)
        {
        case GUMBO_NODE_TEXT:
        case GUMBO_NODE_CDATA:
        {
            if (style_changed)
            {
                styleManager.change(result, original_style);
                style_changed=false;
            }
            QString text(node->v.text.text);
            int pos = text.lastIndexOf(" - created with Hero Lab");
            if (pos >= 0) text = text.left(pos);
            // Escape any text in square brackets
            result += doEscape(text);
        }
            break;

        case GUMBO_NODE_WHITESPACE:
            if (allowWhitespace)
            {
                if (style_changed)
                {
                    styleManager.change(result, original_style);
                    style_changed=false;
                }
                result += QString(node->v.text.text);
            }
            break;

        case GUMBO_NODE_COMMENT:
            break;

        case GUMBO_NODE_ELEMENT:
        {
            const QString tag = gumbo_normalized_tagname(node->v.element.tag);

            // Paragraphs are handled as having stand-alone formatting.
            if (tag == "p")
            {
                TextStyleManager para_style;  // each paragraph should have its own style information, starting from NO STYLING
                QString paragraph;
                para_style.start(paragraph, inlineStyle(node, cssStyles));
                paragraph += decode_gumbo(node, cssStyles, para_style, nestedTableCount, listtype, /*allowWhitespace*/ true);
                para_style.finish(paragraph);

                // Check for line with only formatting and white space!
                static const QRegularExpression markup("[\\*~ ]+$", QRegularExpression::UseUnicodePropertiesOption);
                if (!markup.match(paragraph, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption).hasMatch())
                    result += paragraph + newline + newline;  // Blank line after each paragraph

                // Skip the rest of the GUMBO_NODE_ELEMENT processing
                break;
            }

            // Not relevant to <p>, we can cancel any previous styling.
            // class and style attributes are "Global Attributes" which can be used on any HTML element.
            TextStyle tag_style = inlineStyle(node, cssStyles);
            if (!tag_style.isEmpty())
            {
                start_length = result.length();
                if (!original_set)
                {
                    original_style = styleManager.currentStyle();
                    original_set   = true;
                }
                styleManager.change(result, original_style + tag_style);
                new_style_change=!style_changed;
                style_changed=true;
            }
            else if (style_changed)
            {
                styleManager.change(result, original_style);
                style_changed=false;
                new_style_change=false;
            }

            if (tag == "span")
            {
                // Some span are inside lists rather than paragraphs!
                // RW sometimes puts additional styling around individual spaces.
                QString span = decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, /*allowWhitespace*/ true);
                if (new_style_change && span == QStringLiteral(" "))
                {
                    // new_style_change is so that we don't "cancel" a style change that is continuing from a previous span/element.
                    // Get the style manager in sync for removing the formatting.
                    styleManager.change(result, original_style);
                    style_changed = false;
                    // Remove the start/end styling from the result string.
                    result.truncate(start_length);
                }
                result += span;
            }
            else if (tag == "br")
            {
                result += RW_LINE_BREAK;  // Replaced in write_html by calling patch_gumbo
            }
            else if (tag == "sup" || tag == "sub" || tag == "b" || tag == "i")
            {
                if (!original_set)
                {
                    original_style = styleManager.currentStyle();
                    original_set   = true;
                }
                styleManager.change(result, original_style + TextStyle::fromNode(tag));
                style_changed=true;

                result += decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, allowWhitespace);
            }
            else if (tag == "hr")
            {
                // Blank line required before the markdown --- indicator
                result += "\n---\n";
            }
            else if (tag == "a")
            {
                QString label = decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, allowWhitespace);
                result += createMarkdownLink(/*filename*/ getGumboAttribute(node, "href"), /*label*/ label);
            }
            else if (tag == "img")
            {
                QString src = getGumboAttribute(node, "src");
                QString alt = getGumboAttribute(node, "alt");
                if (alt.isEmpty()) alt = src;
                if (src.startsWith("data:"))
                {
                    // Write out the link to an assets-file.
                    // data:image/jpeg;base64,...lots of data...
                    int base64 = src.indexOf(";base64,");
                    if (base64 > 0)
                    {
                        QByteArray buffer = QByteArray::fromBase64(src.mid(base64+8).toLatin1());
                        int slash = src.indexOf("/");
                        QString extension = src.mid(slash+1, base64-slash-1);
                        QString filename = QString("gumbodatafile%1.%2").arg(gumbofilenumber++).arg(extension);
                        result += write_image("", buffer, /*mask*/nullptr, filename, /*no annotation*/ QString());
                    }
                }
                else
                {
                    // Build a link to the external URL
                    result += "!" + createMarkdownLink(/*filename*/ src, /*label*/ alt);
                }
            }
            // Table support - just use normal HTML
            else if (nestedTableCount==0 && tag == "table")
            {
                // remove line break from last line
                QString table = decode_gumbo(node, cssStyles, styleManager, nestedTableCount+1, listtype, /*allowWhitespace*/ false).trimmed();

                // Need to add |---|---| line to tell markdown that it is a table
                int break1 = table.indexOf('\n');
                int barCount = (break1 < 0) ? table.count('|') : table.midRef(0,break1).count('|');
                if (barCount==0)
                {
                    qWarning() << "No bars found in table";
                }
                else
                {
                    bool mark_tx = (use_table_extended && table.contains("||"));
                    if (break1 < 0)
                    {
                        if (use_table_extended)
                            mark_tx = true;
                        else
                        {
                            // Only one line in the table, so put a fake line in front of it.
                            int count = barCount;
                            QString header = "|";
                            while (--count) header += " |";
                            break1 = header.length();    // set to index of the line break we are about to add.
                            table.prepend(header + newline);
                        }
                    }
                    // Create the line of columns
                    QString columns = "|";
                    while (--barCount) columns += "---|";
                    table.insert(break1+1, columns + newline);

                    // Maybe need to include marker for "Table Extended" plugin
                    if (mark_tx) result += newline + "-tx-";
                    // Count finished, so we can replace the RW_ESCAPE_BAR with "\|" - required to have image links working
                    table.replace(RW_ESCAPE_BAR, "\\|");
                    result += newline + table + newline + newline;
                }
            }
            else if (nestedTableCount==1 && tag == "tr")
            {
                QString row = decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, allowWhitespace);
                // end of line for table row
                result += "| " + row.trimmed() + "\n";
            }
            else if (nestedTableCount==1 && tag == "td")
            {
                // TD contains text directly, so allow whitespace within it
                // If colspan is set and "Table Extended" is being used, then add extra "|" at the end.
                int colspan=1;
                if (use_table_extended) {
                    QString attr = getGumboAttribute(node, "colspan");
                    if (!attr.isEmpty()) colspan = attr.toInt();
                }
                QString cell = decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, /*allowWhitespace*/ true);
                // TODO - do we need to detect double \n
                result += ' ' + cell.trimmed().replace(RW_LINE_BREAK,"<br>").replace("\n\n","<br>").replace("\n","<br>").replace("|",RW_ESCAPE_BAR) + ' ' + QString(colspan, QChar('|'));
            }
            else if (nestedTableCount==1 && tag == "tbody")
            {
                result += decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, allowWhitespace);
            }
            else if (tag == "ul")
            {
                QString newlisttype = listtype;
                if (newlisttype.isEmpty())
                    newlisttype = "- ";
                else
                {
                    result += newline;   // presumably we are still inside another ol/ul, so terminate the text of the <li>
                    newlisttype.prepend("  ");
                    // Ensure this sublist is a BULLET list
                    newlisttype.replace("1.", "-");
                }
                result += decode_gumbo(node, cssStyles, styleManager, nestedTableCount, newlisttype, allowWhitespace);
                // Blank line after last line of nested list only
                if (listtype.isEmpty()) result += newline;
            }
            else if (tag == "ol")
            {
                QString newlisttype = listtype;
                if (newlisttype.isEmpty())
                    newlisttype = "1. ";
                else
                {
                    result += newline;   // presumably we are still inside another ol/ul, so terminate the text of the <li>
                    newlisttype.prepend("  ");
                    // Ensure this sublist is a NUMBERED list
                    newlisttype.replace("-", "1.");
                }
                result += decode_gumbo(node, cssStyles, styleManager, nestedTableCount, newlisttype, allowWhitespace);
                // Blank line after last line of nested list only
                if (listtype.isEmpty()) result += newline;
            }
            else if (tag == "li")
            {
                result += listtype + decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, allowWhitespace);
                // If this is the <li> after a nested list, then we might end up with too many \n.
                if (!result.endsWith(newline)) result += newline;
            }
            else if (tag.length() == 2 && tag[0] == 'h' && tag[1].isDigit())
            {
                result += heading(tag.midRef(1).toInt(), decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, allowWhitespace).trimmed());
            }
            else
            {
                qDebug() << "GUMBO_NODE_ELEMENT(start): not converting " << QString("<%1>").arg(tag);

                if (tag == "table") nestedTableCount++;

                bool local_allowWhitespace = allowWhitespace;
                // Don't take whitespace immediately after a nested table.
                if (tag == "table" || tag == "thead" || tag == "tbody" || tag == "tr")
                    local_allowWhitespace = false;
                else if (tag == "td")
                    local_allowWhitespace = true;

                // Put back the original tag, includes all its attributes.
                QStringList parts{tag};
                GumboAttribute **attributes = reinterpret_cast<GumboAttribute**>(node->v.element.attributes.data);
                for (unsigned count = node->v.element.attributes.length; count > 0; --count)
                {
                    const GumboAttribute *attr = *attributes++;
                    parts.append(QString("%1=%2").arg(attr->name, quotes(attr->value)));
                }
                result += "<" + parts.join(' ') + ">";
                result += decode_gumbo(node, cssStyles, styleManager, nestedTableCount, listtype, local_allowWhitespace);
                result += "</" + tag + ">";
            }
            // end of GUMBO_NODE_ELEMENT
        }
            break;

        case GUMBO_NODE_DOCUMENT:
            break;

        case GUMBO_NODE_TEMPLATE:
            break;
        }
    }
    // Cancel any final style if one is currently being processed.
    if (style_changed) styleManager.change(result, original_style);

    return result.replace("\u00a0"," ").replace("\u200b","");
}


static inline const QString read_gumbo(const GumboNode *node, const GumboStyles &cssStyles)
{
    TextStyleManager styleManager;
    QString result = decode_gumbo(node, cssStyles, styleManager);
    styleManager.finish(result);
    // GUMBO puts "<br>" in, which need to be "\n" to work with markdown,
    // but we should ignore RW_LINE_BREAK at the end of a line.
    return result.replace(RW_LINE_BREAK + newline, newline).replace(RW_LINE_BREAK, newline);
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


static inline QString stat(XmlElement *node, const QString &prefix, const QString &field, const QString &attribute, const QString &suffix)
{
    if (!field.isEmpty())
    {
        const QStringList parts = field.split("/");
        for (auto &part: parts)
        {
            const QStringList bits = part.split("+");
            if (bits.length() < 3)
                node = node->xmlChild(part);
            else
            {
                // element+attribute+value
                // find version of <element> with <attribute> set to <value>
                const QString &elem = bits[0];
                const QString &attr = bits[1];
                const QString &aval = bits[2];
                auto children = node->xmlChildren(elem);
                node = nullptr;
                foreach (auto choice, children)
                {
                    if (choice->attribute(attr) == aval)
                    {
                        node = choice;
                        break;
                    }
                }
            }
            if (!node) return QString();
        }
    }

    QString result = attribute.isEmpty() ? node->fixedText() : node->attribute(attribute);
    if (result.isEmpty()) return result;
    return prefix + result + suffix;
}

static const QString get_content_text(XmlElement *parent, const ExportLinks &links, bool dice=true);

static inline QString stattext(XmlElement *node)
{
    // Disable dice roll substitution in the text (until the 5e-statblock plugin supports it)
    QString value = get_content_text(node, NO_LINKS, false).trimmed();
    value.replace(QRegExp("[\n]+"), "\\n");
    return value;
}


static const QString INDENT{newline + YAMLLIST};

static inline QString join_list(const QString &label, const QStringList &list)
{
    if (list.isEmpty()) return QString();
    static const QString indent1{QStringLiteral(":\n  - [")};
    static const QString indentN{QStringLiteral("]\n  - [")};
    static const QRegularExpression twocommas("[, ]+,");
    // Plugin fails if two commas appear together
    return label + indent1 + list.join(indentN).replace(twocommas, ",") + ']' + newline;
}

static inline QString st_save(XmlElement *character, const QString &prefix, const QString &abilityname)
{
    // base_mod: text attribute has "0" whilst base and modified has "+0"
    // save_mod: text attribute has "+0"
    const QString base_mod = stat(character, "", "abilityscores/abilityscore+name+" + abilityname + "/abilbonus",   "modified", QString());
    const QString save_mod = stat(character, "", "abilityscores/abilityscore+name+" + abilityname + "/savingthrow", "text",     QString());
    if (base_mod != save_mod)
        return INDENT + prefix + save_mod;

    return QString();
}

/**
 * @brief write_5e_statblock
 * Adds terminating newline
 * @param constxml
 * @return
 */
static const QString write_5e_statblock(const QMap<QString,QByteArray> &image_files, const QByteArray &constxml)
{
    QString finalresult;

    QByteArray xml(constxml);   // convert from const to non-const
    QBuffer buffer(&xml);
    if (!buffer.open(QBuffer::ReadOnly))
    {
        qWarning() << "write_5e_statblock: failed to open XML string buffer";
        return QString();
    }
    XmlElement *tree = XmlElement::readTree(&buffer);
    if (!tree)
    {
        qWarning() << "write_5e_statblock: failed to parse XML in string buffer";
        return QString();
    }

    QList<XmlElement*> children = tree->findChildren<XmlElement*>("character");
    if (children.isEmpty())
    {
        qWarning() << "write_5e_statblock: failed to find <character> in XML string buffer";
        return QString();
    }

    for (XmlElement *character : children)
    {
        QString result;
        QString temp;

        QStringList senses;
        QStringList traits;
        QStringList legendaries;
        QStringList reactions;
        QStringList actions;
        QStringList movements;
        QStringList languages;

        // Set up the suffix which should be ignored on ability names.
        const QString ignorename = " (" + character->attribute("name") + ")";

        XmlElement *imagesparent = character->xmlChild("images");
        if (imagesparent)
        {
            const auto images = imagesparent->xmlChildren("image");
            for (const auto &image: images)
            {
                QString filename = image->attribute("filename");
                if (image_files.contains(filename) &&
                    write_binary_file(filename, image_files.value(filename)))
                {
                    result += "image: [[" + filename + "]]" + newline;
                }
            }
        }
        result += stat(character, "name: ",      QString(),    "name",        newline);
        result += stat(character, "size: ",      "size",       "name",        newline);
        result += stat(character, "race: ",      "race",       "displayname", newline);
        result += stat(character, "type: ",      "types/type", "name",        newline);
        result += stat(character, "subtype: ",   "subrace",    "name",        newline);
        result += stat(character, "alignment: ", "alignment",  "name",        newline);
        result += stat(character, "ac: ",        "armorclass", "ac",          newline);
        result += stat(character, "hp: ",        "health",     "hitpoints",   newline);
        result += stat(character, "hit_dice: ",  "health",     "hitdice",     newline);

        // Add a flag to add Dice Roller markers
        if (detect_dice_rolls) result += "dice: true" + newline;

        temp = stat(character, "", "movement/speed", "value", " ft.");
        if (!temp.isEmpty()) movements.append(temp);
        //
        if (character->xmlChild("abilityscores"))
        {
            // D&D 5E
            result +=
                    stat(character, "stats: [", "abilityscores/abilityscore+name+Strength/abilvalue", "text", QString()) +
                    stat(character, ", ", "abilityscores/abilityscore+name+Dexterity/abilvalue",      "text", QString()) +
                    stat(character, ", ", "abilityscores/abilityscore+name+Constitution/abilvalue",   "text", QString()) +
                    stat(character, ", ", "abilityscores/abilityscore+name+Intelligence/abilvalue",   "text", QString()) +
                    stat(character, ", ", "abilityscores/abilityscore+name+Wisdom/abilvalue",         "text", QString()) +
                    stat(character, ", ", "abilityscores/abilityscore+name+Charisma/abilvalue",       "text", ']' + newline);
            const QString saves =
                    st_save(character, "Str: ", "Strength") +
                    st_save(character, "Dex: ", "Dexterity") +
                    st_save(character, "Con: ", "Constitution") +
                    st_save(character, "Int: ", "Intelligence") +
                    st_save(character, "Wis: ", "Wisdom") +
                    st_save(character, "Cha: ", "Charisma");
            if (!saves.isEmpty()) result += "saves:" + saves + newline;

        }
        else if (character->xmlChild("attributes"))
        {
            // Pathfinder (and SWADE?)
            result +=
                    stat(character, "stats: [", "attributes/attribute+name+Strength/attrvalue", "modified", QString()) +
                    stat(character, ", ", "attributes/attribute+name+Dexterity/attrvalue",      "modified", QString()) +
                    stat(character, ", ", "attributes/attribute+name+Constitution/attrvalue",   "modified", QString()) +
                    stat(character, ", ", "attributes/attribute+name+Intelligence/attrvalue",   "modified", QString()) +
                    stat(character, ", ", "attributes/attribute+name+Wisdom/attrvalue",         "modified", QString()) +
                    stat(character, ", ", "attributes/attribute+name+Charisma/attrvalue",       "modified", ']' + newline);
            const QString saves =
                    stat(character, INDENT + "Con: ", "saves/save+abbr+Fort", "save", QString()) +
                    stat(character, INDENT + "Dex: ",  "saves/save+abbr+Ref", "save", QString()) +
                    stat(character, INDENT + "Wis: ", "saves/save+abbr+Will", "save", QString());
            if (!saves.isEmpty()) result += "saves:" + saves + newline;
        }

        if (auto parent = character->xmlChild("skills"))
        {
            result += "skillsaves:";
            foreach (auto skill, parent->xmlChildren("skill"))
            {
                QString name      = skill->attribute("name");
                QString abilbonus = skill->attribute("abilbonus");
                QString value     = skill->attribute("value");
                // Statblock doesn't include skills where the skill matches the stat bonus.
                // but always seems to include the Perception skill.
                if (abilbonus != value || name == "Perception")
                {
                    if (value.front() != '-') value.prepend('+');
                    result += INDENT + quotes(name) + ": " + value;
                }
            }
            result += newline;
        }

        result += stat(character, "damage_vulnerabilities: ", "damagevulnerabilities", "text",  newline);
        result += stat(character, "damage_resistances: ",     "damageresistances",     "text",  newline);
        result += stat(character, "damage_immunities: ",      "damageimmunities",      "text",  newline);
        result += stat(character, "condition_immunities: ",   "conditionimmunities",   "text",  newline);
        result += stat(character, "cr: ",                     "challengerating",       "value", newline);

        if (auto parent = character->xmlChild("otherspecials"))
        {
            foreach (auto special, parent->xmlChildren("special"))
            {
                QString name = special->attribute("name");
                if (name.endsWith(ignorename)) name.truncate(name.length() - ignorename.length());

                QString value = quotes(name) + ", " + quotes(stattext(special->xmlChild("description")));

                QString type = special->attribute("type");
                if (type == "Action")
                    actions.append(value);
                else if (type == "Legendary")
                    legendaries.append(value);
                else if (type == "Reaction")
                    reactions.append(value);
                else if (type == "Movement")
                    movements.append(name);
                else if (type == "Sense")
                    senses.append(name);
                else if (type == "Special")
                    traits.append(value);
                else if (type == "Language")
                    languages.append(name);
            }
        }

        if (auto parent = character->xmlChild("senses"))
            foreach (auto sense, parent->xmlChildren())
            {
                senses.append(sense->attribute("name"));
            }

        if (auto parent = character->xmlChild("melee"))
        {
            QString reach = stat(character, "reach ", "size/reach", "value", " ft., ");
            foreach (auto weapon, parent->xmlChildren("weapon"))
            {
                // <weapon name="Longsword" categorytext="Melee Weapon" typetext="Slashing" attack="+9" damage="13 (2d8+4) slashing" quantity="1" isproficient="yes">
                // - [Bite, Melee Weapon Attack:+ 15 to hit, reach 15 ft., one target. Hit: 19 (2d10 + 8) piercing damage plus 9 (2d8) acid damage.]
                QString name = weapon->attribute("name");
                if (name.endsWith(ignorename)) name.truncate(name.length() - ignorename.length());

                actions.append(quotes(name) + ", " + quotes(
                                   weapon->attribute("categorytext") + " Attack: " +
                                   weapon->attribute("attack") + " to hit, " +
                                   reach +
                                   "one target. " +   // presumably (since melee)
                                   "Hit: " +
                                   weapon->attribute("damage") + " damage"));
            }
        }
        if (auto parent = character->xmlChild("ranged"))
            foreach (auto weapon, parent->xmlChildren("weapon"))
            {
                // <weapon name="Spell Attack" categorytext="Thrown Weapon" typetext="" attack="+3" damage="As Spell" equipped="mainhand" quantity="1" isproficient="yes">
                //    <rangedattack attack="+5" range="150 ft./600 ft."/>
                // - [Longbow. Ranged Weapon Attack: +6 to hit, range 150/600 ft., one target. Hit: 8 (1d8 + 4) piercing damage.]
                XmlElement *ranged = weapon->xmlChild("rangedattack");
                QString name = weapon->attribute("name");
                if (name.endsWith(ignorename)) name.truncate(name.length() - ignorename.length());

                actions.append(quotes(name) + ", " + quotes(
                                   "Ranged Weapon Attack:" +
                                   weapon->attribute("attack") + " to hit, " +
                                   ranged->attribute("range") +
                                   ", one target. " +   // presumably (since melee)
                                   "Hit: " +
                                   weapon->attribute("damage") + " damage"));
            }

        if (auto parent = character->xmlChild("languages"))
            foreach (auto lang, parent->xmlChildren())
            {
                languages.append(lang->attribute("name"));
            }

        // Lastly, senses has passive perception at the end of the line
        QString passive_perception = stat(character, "Passive Perception ", "skills/skill+name+Perception", "passive", QString());
        if (!passive_perception.isEmpty()) senses.append(passive_perception);

        if (senses.length() > 0)
            result += "senses: " + quotes(senses.join(", ")) + newline;

        result += "languages: " + (languages.isEmpty() ? "--" : languages.join(", ")) + newline;

        // spellclasses/spellclass[name="Rogue" spells="Spontaneous" maxspelllevel="1" cantripcount="2" spellcount="0"]
        // spellslots/spellslot[name="1st" count="2" used="0"]
        // cantrips/spell[name="Mage Hand" class="Rogue" casttime="1 action" range="30 feet" duration="1 minute" dc="12" casterlevel="1"]
        // spellsknown/spell[name="Counterspell" level="3rd" class="Rogue" casttime="1 reaction" range="60 feet" duration="" dc="12" casterlevel="1" componenttext="Somatic" schooltext="Abjuration" castsleft="0" spontaneous="yes"]
        // -->
        // spells:
        // - The archmage is an 18th-level spellcaster. Its spellcasting ability is Intelligence (spell save DC 17, +9 to hit with spell attacks). The archmage can cast disguise self and invisibility at will and has the following wizard spells prepared
        // - Cantrips (at will): fire bolt, light, mage hand, prestidigitation, shocking grasp
        // - 1st level (4 slots): detect magic, identify, mage armor*, magic missile
        // - 2nd level (3 slots): detect thoughts, mirror image, misty step
        QMultiMap<QString,QString> spellnames;
        QMap<QString,QString> spellslots;
        QString spelldesc;
        if (auto parent = character->xmlChild("spellslots"))
            foreach (auto slot, parent->xmlChildren("spellslot"))
            {
                QString count = slot->attribute("count");
                spellslots.insert(slot->attribute("name"), count + (count=="1" ? " slot" : " slots"));
            }
        else if (auto parent = character->xmlChild("spellclasses")) // pathfinder
            foreach (auto spclass, parent->xmlChildren("spellclass"))
            {
                foreach (auto splevel, spclass->xmlChildren("spelllevel"))
                {
                    QString casts = splevel->attribute("maxcasts");
                    if (!casts.isEmpty())    // 0th level don't have casts
                        spellslots.insert(splevel->attribute("level"), casts);
                }
            }

        if (auto parent = character->xmlChild("cantrips"))
            foreach (auto spell, parent->xmlChildren("spell"))
            {
                spellnames.insert(0, spell->attribute("name"));
            }
        if (auto parent = character->xmlChild("spellsknown"))
            foreach (auto spell, parent->xmlChildren("spell"))
            {
                spellnames.insert(spell->attribute("level"), spell->attribute("name"));
            }
        if (auto parent = character->xmlChild("classes"))
            foreach (auto cls, parent->xmlChildren("class"))
            {
                QString casterlevel = cls->attribute("casterlevel");
                if (casterlevel.isEmpty()) continue;
                QStringList parts;

                parts.append("The " + cls->attribute("name") + " is a level " + casterlevel + " caster");

                QString spellattack = cls->attribute("spellattack");
                if (!spellattack.isEmpty())
                {
                    // DND5E
                    QString spellability = cls->attribute("spellability");
                    if (!spellability.isEmpty()) parts.append("Spellcasting Ability " + spellability);
                    if (!spellattack.isEmpty())  parts.append("Spell Attack " + spellattack);
                    QString spellsavedc = cls->attribute("spellsavedc");
                    if (!spellsavedc.isEmpty())  parts.append("Spell Save DC " + spellsavedc);
                }
                else
                {
                    // Pathfinder
                    QString basespelldc = cls->attribute("basespelldc");
                    if (!basespelldc.isEmpty()) parts.append("Base Spell DC " + basespelldc);

                    QString overcomespellresistance = cls->attribute("overcomespellresistance");
                    if (!overcomespellresistance.isEmpty()) parts.append("Overcome SR " + overcomespellresistance);
                }

                if (!spelldesc.isEmpty()) spelldesc += "; ";
                spelldesc += parts.join(", ");
            }
        //  spells:
        //      - <description>
        //      - <spell level>: <spell-list>
        if (!spellnames.isEmpty())
        {
            result += "spells: " + INDENT + spelldesc;
            // first line = spellcasting description, such as "The archmage is an 18th-level spellcaster. Its spellcasting ability is Intelligence (spell save DC 17, +9 to hit with spell attacks). The archmage can cast disguise self and invisibility at will and has the following wizard spells prepared"
            foreach (auto level, spellnames.uniqueKeys())
            {
                QString count = spellslots.value(level);
                if (count.isEmpty()) count = "at will";
                QString lvl = (level == 0) ? "Cantrip" : (level + " level");
                const QStringList spells = spellnames.values(level);
                result += INDENT + lvl + " (" + count + "): " + spells.join(", ");
            }
            result += newline;
        }

        if (!movements.isEmpty())
            result += "speed: " + quotes(movements.join(", ")) + newline;

        //  traits:
        //      - [<trait-name>, <trait-description>]
        //      - ...
        result += join_list("traits", traits);

        //  actions:
        //      - [<trait-name>, <trait-description>]
        //      - ...
        result += join_list("actions", actions);

        //  legendary_actions:
        //      - [<legendary_actions-name>, <legendary_actions-description>]
        //      - ...
        result += join_list("legendary_actions", legendaries);

        //  reactions:
        //      - [<reaction-name>, <reaction-description>]
        //      - ...
        result += join_list("reactions", reactions);

#if 0
name: string
size: string
type: string
subtype: string
alignment: string
ac: number
hp: number
hit_dice: string
speed: string
stats: [number, number, number, number, number, number]
fage_stats: [number, number, number, number, number, number, number, number, number]
saves:
    - <ability-score>: number
skillsaves:
    - <skill-name>: number
damage_vulnerabilities: string
damage_resistances: string
damage_immunities: string
condition_immunities: string
senses: string
languages: string
cr: number
spells:
    - <description>
    - <spell level>: <spell-list>
traits:
    - [<trait-name>, <trait-description>]
    - ...
actions:
    - [<trait-name>, <trait-description>]
    - ...
legendary_actions:
    - [<legendary_actions-name>, <legendary_actions-description>]
    - ...
reactions:
    - [<reaction-name>, <reaction-description>]
    - ...
#endif

        // Put the statblock wrapper on the result
        if (!result.isEmpty())
        {
            result.prepend(codeblock + "statblock" + newline);
            result.append(codeblock + newline);
        }

        finalresult += result;
    } /* for each child */

    return finalresult;
}

/**
 * @brief write_html
 * No newline at the end
 * @param use_fixed_title
 * @param sntype
 * @param data
 * @return
 */
static const QString write_html(bool use_fixed_title, const QString &sntype, const QByteArray &data)
{
    // Put the children of the BODY into this frame.
    GumboOutput *output = gumbo_parse(data);
    if (output == nullptr)
    {
        qWarning() << "Failed to parse HTML data";
        return QString();
    }

#ifdef DUMP_CHILDREN
    dump_children("ROOT", output->root);
#endif
    QString result;
    result.reserve(1000);

    const GumboNode *head = getGumboChild(output->root, "head");
    const GumboNode *body = getGumboChild(output->root, "body");

#ifdef DUMP_CHILDREN
    dump_children("HEAD", head);
#endif

    // Read styles which are used in the statblock
    const GumboNode *style = head ? getGumboChild(head, "style") : nullptr;
    GumboStyles cssStyles = getStyles(style);

    result += "\n---\n## " + sntype;
    if (!use_fixed_title)
    {
        const GumboNode *titlenode = head ? getGumboChild(head, "title") : nullptr;
        if (titlenode)
        {
            QString title = read_gumbo(titlenode, cssStyles).trimmed();

            // Strip HL from name
            int pos = title.lastIndexOf(" - created with Hero Lab");
            if (pos >= 0) title.truncate(pos);
            result += ": " + title;
        }
    }
    result += newline;

#ifdef DUMP_CHILDREN
    dump_children("BODY", body);
#endif
    result += read_gumbo(body ? body : output->root, cssStyles).trimmed();

    // Get GUMBO to release all the memory
    gumbo_destroy_output(&kGumboDefaultOptions, output);

    // We can't automatically detect dice rolls, it will mess up embedded tables,
    // and will make HL portfolio files NOT display the basic damage dice (since it
    // replaces each one with the result of the dice roll).

    if (detect_html_dice_rolls) replace_dice(result);

    return result;
}

/**
 * @brief get_content_text
 * Decodes the HTML contained in the specified @parent.
 * The result is not trimmed() so that tables can be handled properly in Labeled_Text snippets.
 * @param parent
 * @param links
 * @param dice
 * @return
 */
static const QString get_content_text(XmlElement *parent, const ExportLinks &links, bool dice /*=true*/)
{
#if DEBUG_LEVEL > 4
    qDebug() << "...write para-children";
#endif
    QString result;
    result.reserve(1000);
    if (!parent->xmlChild()) return "";

    QString text{parent->childString()};
    text.replace("&#xd;", "\n");    // Get to correct length for fixing links

    // Now substitute any required links
    foreach (const auto &link, links)
    {
        // Replace ONLY the text of the link, keeping the surrounding spans to handle formatting.
        const QString source = text.mid(link.start,link.length);
        const QString label  = textContent(source);
        int pos = source.indexOf(label);
        if (pos >= 0)
            text.replace(link.start+pos, label.length(), escape_bracket(internal_link(link.target_id, label)));
        else
        {
            //qDebug() << "textContent not found in" << source;
            //qDebug() << "       - textContent =" << label;
            // Failed to find the label, so replace everything (and just suffer the problems with formatting)
            text.replace(link.start, link.length, escape_bracket(internal_link(link.target_id, label)));
        }
    }

    if (text.startsWith("<"))
    {
        GumboOutput *output = gumbo_parse(text.toUtf8());
        if (output) {
            if (output->root)
            {
                if (auto body = getGumboChild(output->root, "body"))
                {
                    GumboStyles cssStyles;
                    result += read_gumbo(body, cssStyles);
                }
            }
            // Get GUMBO to release all the memory
            gumbo_destroy_output(&kGumboDefaultOptions, output);
        }
    }
    else
    {
        // No embedded HTML
        result = text;
    }
    if (dice && detect_dice_rolls) replace_dice(result);

    return result;
}


static inline const QString annotationText(const XmlElement *snippet, bool wrapped = true)
{
    // No need to consider TextStyle, since this is called at the SNIPPET level, not the paragraph level
    XmlElement *annotation = snippet->xmlChild("annotation");
    if (!annotation) return "";

    // Remove newline from either end of paragraph
    static const ExportLinks nolinks;
    QString result = get_content_text(annotation, nolinks).trimmed();
    result.replace("&#xd;\n","\n");
    if (!wrapped)
        return result;
    else
        return " ; " + result;
}


static inline QString canonicalTime(const QString &source)
{
    return source;
    // canonical="000000001:50:66:+002018:50:28:0028:000:000:000" gregorian="2018-04-29 00:00:00"
    // canonical="000000001:50:33: 980000:50:07:0000:000:000:000" gregorian="20000-01-01 00:00:00 BCE
    // canonical="000000002:50:66:+002029:50:07:0025:004:002:005" (for Absolom calendar:  "26/01/2029 AR 04:02:05"
    // canonical="00000000b:50:33: 602012:00:00:0018:004:002:005" (for Imperial calendar: "019-397988 IY 04:02:05")
    // canonical="000000001:50:66:+002021:50:76:0013:020:032:055" gregorian="2021-10-14 20:32:55"
    //                               YYYY    ww DDDD:HHH:MMM:SSS
    // Add 1 to DDDD to get real data
    // For BCE, year = 1,000,000 - year
    // month (mm) = 7 x month

    // canonical syntax = "0[0-9a-fA-F]{8}:[0-9]{2}:[0-9]{2}:[ +][0-9]{6}:[0-9]{2}:[0-9]{2}:[0-9]{4}:[0-9]{3}:[0-9]{3}:[0-9]{3}"

    // <calendar_map calendar_uuid="1B00287B-91C6-5D3A-00D0-6A935C3C6BA9">
    //     <calendar_map>
    //         <version>1</version>
    //         <map_type>Overlay</map_type>
    //         <ref_calendar>2</ref_calendar>
    //         <anchor_calendar>1</anchor_calendar>
    //         <ref_date>000000002:50:33: 980000:50:07:0000:000:000:000</ref_date>
    //         <anchor_date>000000001:50:33: 980000:50:07:0000:000:000:000</anchor_date>
    //     </calendar_map>
    // </calendar_map>
    // <calendar_map calendar_uuid="1C00287B-91C6-5D3A-00D0-6A935C3C6BA9">
    //     <calendar_map>
    //         <version>1</version>
    //         <map_type>Overlay</map_type>
    //         <ref_calendar>11</ref_calendar>
    //         <anchor_calendar>1</anchor_calendar>
    //         <ref_date>00000000b:50:33: 600000:00:00:0000:000:000:000</ref_date>
    //         <anchor_date>000000001:50:66:+000011:50:14:0000:000:000:000</anchor_date>
    //     </calendar_map>
    // </calendar_map>
}


static inline QString gregorian(const QString &source)
{
    if (source.isEmpty()) return source;

    // 20000-01-01 00:00:00 BCE
    // 0956-03-18 04:02:05
    // 2018-04-29 00:00:00
    int yearmark = source.indexOf('-')+1;
    int year   = source.midRef(0,yearmark-1).toInt();
    int length = source.length() - yearmark;
    if (source.endsWith(" BCE"))
    {
        year   = -year+1;
        length -= 4;
    }
    QDateTime datetime = QDateTime::fromString(source.mid(yearmark, length), "MM-dd hh:mm:ss").addYears(year-1900);

    //result.setDate(QDate(year, result.month(),))
    return datetime.toString(QLocale::system().dateTimeFormat());    //QLocale::system().toString(datetime);
}

/**
 * @brief encounter_creature
 * Determine how many additional parameters can be found for a creature being added to an ENCOUNTER block.
 * @param character
 * @return
 */
static inline QString encounter_creature(XmlElement *character)
{
    // Remove trailing #<digit> from creature name (if present)
    QString name = character->attribute("name");
    int pos = name.indexOf(" #");
    if (pos > 0) name.truncate(pos);

    QString result{YAMLLIST + name};
    QString part;
    const QString prefix{", "};

    // Both PF1 and DND5E have the same structure in their XML, so let's assume it is the same for other game systems.
    part = xmlValue(character, {"health"}, "hitpoints");
    if (part.isEmpty()) return result;
    result += prefix + part;

    part = xmlValue(character, {"armorclass"}, "ac");
    if (part.isEmpty()) return result;
    result += prefix + part;

    part = xmlValue(character, {"initiative"}, "total");
    if (part.isEmpty()) return result;
    result += prefix + part;

    part =   xmlValue(character, {"xpaward"}, "value");
    if (part.isEmpty()) return result;
    result += prefix + part;

    return result;
}


static const QString write_snippet(XmlElement *snippet)
{
    const QString sn_type     = snippet->attribute("type");
    const QString sn_veracity = snippet->attribute("veracity");
    QString sn_style          = snippet->attribute("style"); // Read_Aloud, Callout, Flavor, Handout  - not const since might get cancelled
    QString endspan{newline};   // Might be </span>
    bool inspan=false;
#if DUMP_LEVEL > 3
    qDebug() << "...snippet" << sn_type;
#endif

    // Get whatever links might be on this snippet.
    // Collect links into a set to remove duplicates.
    ExportLinks links;
    ExportLinks gmlinks;
    foreach (const auto &link, snippet->xmlChildren("link"))
    {
        const QString target_id = link->attribute("target_id");
        foreach (const auto &span_info, link->xmlChildren("span_info"))
        {
            foreach (const auto &span_list, span_info->xmlChildren("span_list"))
            {
                foreach (const auto &span, span_list->xmlChildren("span"))
                {
                    const int start  = span->attribute("start").toInt();
                    const int length = span->attribute("length").toInt();
                    if (span->attribute("directions").toInt() == 1)
                        gmlinks.append(ExportLink(target_id, start, length));   // GM-directions
                    else
                        links.append(ExportLink(target_id, start, length));     // normal content
                }
            }
        }
    }
    // Convert to sorted list
    sortLinks(links);
    sortLinks(gmlinks);

    QString result;
    result.reserve(1000);

    // Put GM-Directions first - which could occur on any snippet
    QString line_prefix;
    auto gm_directions = snippet->xmlChild("gm_directions");
    if (gm_directions && !use_admonition_gmdir)
    {
        QString gmtext = get_content_text(gm_directions, gmlinks).trimmed();
        gmtext.replace("\n\n","<br>\n");  // ensure that multiple paragraphs appears as a single block for the surrounding SPAN
        result += "<span class=\"RWgmDirections\" title=\"GM Directions\">" + gmtext + "</span>" + newline + newline;
    }

    // Possibly set a SPAN on the main snippet, for style and veracity
    if (use_admonition_style)
    {
        if (!sn_style.isEmpty())
        {
            if (sn_style == "Read_Aloud")
                result += "> [!QUOTE]+ Read-Aloud";
            else if (sn_style == "Flavor")
                result += "> [!HINT]+ Flavor";
            else if (sn_style == "Callout")
                result += "> [!INFO]+ Callout";
            else if (sn_style == "Handout")     // Message style
                result += "> [!EXAMPLE]+ Message";
            else
                result += "> [!QUESTION]+ " + sn_style;
            result += newline;
            line_prefix = "> ";
        }
    }
    else
    {
        QStringList classes;
        QStringList titles;
        if (!sn_style.isEmpty())
        {
            classes.append(QString("RW%1").arg(sn_style));
            titles.append(QString("Style: %1").arg(sn_style));
        }
        if (!sn_veracity.isEmpty())
        {
            classes.append(QString("RWveracity-%1").arg(sn_veracity));
            titles.append(QString("Veracity: %1").arg(sn_veracity));
        }
        QString title;
        if (!titles.isEmpty()) title = QString(" title=%1").arg(quotes(titles.join(", ")));
        if (!classes.isEmpty()) {
            result  += "<span class=" + quotes(classes.join(" ")) + title + '>';
            endspan =  "</span>" + newline;
            inspan=true;
        }
    }

    if (gm_directions && use_admonition_gmdir)
    {
        QString gmtext = get_content_text(gm_directions, gmlinks).trimmed();
        // See if this is a GM-only or a normal snippet
        // If there are also contents, then we need to nest the GM directions INSIDE the other contents.
        bool only_gm = (snippet->attribute("purpose") == "Directions_Only");
        line_prefix = only_gm ? "> " : ">> ";
        result += line_prefix + "[!WARNING]+ GM Directions" + newline + line_prefix + gmtext.replace("\n","\n" + line_prefix) + newline;
        // Blank line after GM directions, but might be inside callout block
        line_prefix = "> ";
        if (!only_gm) result += line_prefix;
        result += newline;
    }

    //
    // The rest of the processing depends on the snippet type
    //
    if (sn_type == "Multi_Line")
    {
        // child is either <contents> or <gm_directions> or both
        if (auto contents = snippet->xmlChild("contents"))
        {
            QString text = get_content_text(contents, links).trimmed();
            if (!use_admonition_style && inspan) text.replace("\n\n","<br>\n");
            // Multi_Line has no annotation
            if (!line_prefix.isEmpty()) text.replace("\n","\n"+line_prefix);
            result += line_prefix + text + endspan + getTags(line_prefix, snippet);
        }
    }
    else if (sn_type == "Labeled_Text")
    {
        if (auto contents = snippet->xmlChild("contents"))
        {
            QString text = get_content_text(contents, links);
            result += line_prefix + hlabel(snippetLabel(snippet));
            // If content starts with \n then it might be a table, so add an extra blank line at the end of the table
            if (text.startsWith("\n")) result += "\n\n";
            // Labeled_Text has no annotation
            if (!line_prefix.isEmpty()) text.replace("\n","\n"+line_prefix);
            result += text.trimmed() + endspan + getTags(line_prefix, snippet);
        }
    }
    else if (sn_type == "Portfolio")
    {
        // As for other EXT OBJECTS, but with unzipping involved to get statblocks
        if (auto ext_object = snippet->xmlChild("ext_object"))
        {
            if (auto asset = ext_object->xmlChild("asset"))
            {
                if (auto contents = asset->xmlChild("contents"))
                {
                    if (create_por_link)
                    {
                        result += line_prefix + write_ext_object(ext_object->attribute("name"), contents->byteData(), asset->attribute("filename"), annotationText(snippet, false));
                        result += endspan + getTags(line_prefix, snippet);
                    }

                    if (create_5e_statblocks || create_statblocks || initiative_tracker)
                    {
                        // Put in markers for statblock
                        QByteArray store = contents->byteData();
                        QBuffer buffer(&store);
                        QuaZip zip(&buffer);
                        QMap<QString,QByteArray> image_files;
                        if (zip.open(QuaZip::mdUnzip))
                        {
                            // Put encounter block BEFORE other stat blocks
                            if (initiative_tracker && zip.setCurrentFile("index.xml"))
                            {
                                QuaZipFile indexfile(&zip);
                                if (!indexfile.open(QuaZipFile::ReadOnly))
                                    qWarning() << "Failed to open index file from zip: " << zip.getCurrentFileName();
                                else
                                {
                                    XmlElement *index = XmlElement::readTree(&indexfile);
                                    QString system = index->findChild<XmlElement*>("game")->attribute("folder");
                                    QStringList creatures;
                                    const auto characters = index->findChildren<XmlElement*>("character");
                                    for (auto child : characters)
                                    {
                                        bool minion = child->parent()->objectName() == "minions";

                                        const XmlElement* statblocks = minion ? child->parent()->parent()->xmlChild("statblocks") : child->xmlChild("statblocks");
                                        QString statfilename;
                                        QString imgfilename;
                                        for (auto statblock : statblocks->xmlChildren("statblock"))
                                        {
                                            if (statblock->attribute("format") == "xml") {
                                                statfilename = statblock->attribute("folder") + "/" + statblock->attribute("filename");
                                                break;
                                            }
                                        }
                                        if (!statfilename.isEmpty() && zip.setCurrentFile(statfilename))
                                        {
                                            QuaZipFile statfile(&zip);
                                            if (!statfile.open(QuaZipFile::ReadOnly))
                                                qWarning() << "Failed to open statblock file from zip: " << zip.getCurrentFileName();
                                            else
                                            {
                                                XmlElement *stat = XmlElement::readTree(&statfile);
                                                XmlElement *character = nullptr;
                                                const auto characters = stat->findChildren<XmlElement*>("character");
                                                for (auto onechar : characters)
                                                    if (onechar->attribute("name") == child->attribute("name"))
                                                    {
                                                        character = onechar;
                                                        break;
                                                    }
                                                if (character == nullptr)
                                                {
                                                    qWarning() << "Failed to find character" << child->attribute("name") << "in statblock XML file";
                                                }
                                                else
                                                {
                                                    creatures.append(encounter_creature(character));
                                                }
                                            }
                                        }

                                    }
                                    if (creatures.length() > 0)
                                        result += codeblock + "encounter\ncreatures:\n" + creatures.join(newline) + newline + codeblock + newline + newline;
                                }
                            }

                            // Need to convert this HTML into markup
                            for (bool more=zip.goToFirstFile(); more; more=zip.goToNextFile())
                            {
                                if (create_5e_statblocks && zip.getCurrentFileName().startsWith("images/"))
                                {
                                    QuaZipFile file(&zip);
                                    if (file.open(QuaZipFile::ReadOnly))
                                        image_files.insert(zip.getCurrentFileName().mid(7), file.readAll());
                                }
                                else if (create_5e_statblocks && zip.getCurrentFileName().startsWith("statblocks_xml/"))
                                {
                                    QuaZipFile file(&zip);
                                    if (!file.open(QuaZipFile::ReadOnly))
                                        qWarning() << "Failed to open XML file from zip: " << zip.getCurrentFileName();
                                    else
                                    {
                                        result += write_5e_statblock(image_files, file.readAll());
                                    }
                                }
                                else if (create_statblocks && zip.getCurrentFileName().startsWith("statblocks_html/"))
                                {
                                    // Collect images for later


                                    QuaZipFile file(&zip);
                                    if (!file.open(QuaZipFile::ReadOnly))
                                        qWarning() << "Failed to open file from zip: " << zip.getCurrentFileName();
                                    else
                                    {
                                        QString body = write_html(false, sn_type, file.readAll());
                                        if (body.isEmpty())
                                            qWarning() << "GUMBO failed to parse" << zip.getCurrentFileName();
                                        else
                                        {
#if 0
                                            // THIS WORKS FOR PATHFINDER GENERATED FILES,
                                            // BUT NOT FOR SOME OTHER GAME SYSTEMS (WHERE THEY HAVE STATS ON A SINGLE LINE BETWEEN TWO PARALLEL LINES)
                                            // Replace section headers in portfolio HTML with proper section header.
                                            // Ensure other line breaks have a blank line in front of them.
                                            static const QRegularExpression header("\n---\n([^\n]+)\n---\n", QRegularExpression::UseUnicodePropertiesOption);
                                            body.replace(header, "\n\n### \\1\n");
#endif

                                            // Ensure any --- marker has a blank line in front of it (don't add another if one already there!)
                                            static const QRegularExpression line("([^\n])\n---\n", QRegularExpression::UseUnicodePropertiesOption);
                                            body.replace(line, "\\1\n\n---\n");
                                            if (!line_prefix.isEmpty()) body.replace("\n", "\n"+line_prefix);

                                            // Ensure a blank line after the statblock
                                            result += body + newline + newline;
                                        }
                                    }
                                }
                            } /* for goToNextFile */
                        }
                    } // if (create_5e_statblocks || create_statblocks)
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
        // ext_object child, asset grand-child
        if (auto ext_object = snippet->xmlChild("ext_object"))
        {
            if (auto asset = ext_object->xmlChild("asset"))
            {
                if (auto contents = asset->xmlChild("contents"))
                {
                    QString filename = asset->attribute("filename");
                    auto annotation = annotationText(snippet, false);
                    if (sn_type == "Picture")
                    {
                        result += line_prefix + write_image(ext_object->attribute("name"), contents->byteData(), /*mask*/nullptr, filename, annotation);
                    }
                    else
                    {
                        bool expand = filename.endsWith(".html") || filename.endsWith(".htm") || filename.endsWith(".rtf");

                        if (!expand || create_por_link)
                        {
                            result += line_prefix + write_ext_object(ext_object->attribute("name"), contents->byteData(), filename, annotation) + newline;
                        }

                        if (expand)
                        {
                            // Terminate the write_ext_object with a blank line before putting the explicit markup into the note
                            result += newline + line_prefix + write_html(true, sn_type, contents->byteData());
                        }
                    }
                    result += endspan + getTags(line_prefix, snippet);
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

            result += line_prefix + write_image(smart_image->attribute("name"), contents->byteData(), mask, filename, annotationText(snippet, false), usemap, pins);
            result += endspan + getTags(line_prefix, snippet);
        }
    }
    else if (sn_type == "Date_Game")
    {
        if (auto date = snippet->xmlChild("game_date"))
        {
            QString datestr = gregorian(date->attribute("gregorian"));
            if (datestr.isEmpty()) datestr = canonicalTime(date->attribute("canonical"));
            result += line_prefix + hlabel(snippetLabel(snippet)) + datestr + annotationText(snippet) + endspan + getTags(line_prefix, snippet);
        }
    }
    else if (sn_type == "Date_Range")
    {
        if (auto date = snippet->xmlChild("date_range"))
        {
            QString start,finish;
            start  = gregorian(date->attribute("gregorian_start"));
            finish = gregorian(date->attribute("gregorian_end"));
            if (start.isEmpty())  start  = canonicalTime(date->attribute("canonical_start"));
            if (finish.isEmpty()) finish = canonicalTime(date->attribute("canonical_end"));
            result += line_prefix + hlabel(snippetLabel(snippet)) + "From: " + start + " To: " + finish + annotationText(snippet) + endspan + getTags(line_prefix, snippet);
        }
    }
    else if (sn_type == "Tag_Standard")
    {
        QStringList tags;
        foreach (const auto &tag, snippet->xmlChildren("tag_assign"))
            tags.append(global_names.value(tag->attribute("tag_id")));
        if (tags.length() > 0)
        {
            // In non-tag text before showing all connected tags
            result += line_prefix + hlabel(snippetLabel(snippet)) + tags.join(", ") + annotationText(snippet) + endspan + getTags(line_prefix, snippet);
        }
    }
    else if (sn_type == "Numeric")
    {
        if (auto contents = snippet->xmlChild("contents"))
        {
            result += line_prefix + hlabel(snippetLabel(snippet)) + contents->childString() + annotationText(snippet) + endspan + getTags(line_prefix, snippet);
        }
    }
    else if (sn_type == "Tag_Multi_Domain")
    {
        QString tags = getTags("", snippet, /*withnl*/false);   // tags will be on the same line as this snippet
        if (!tags.isEmpty())
            result += line_prefix + hlabel(snippetLabel(snippet)) + tags + annotationText(snippet) + endspan;
    }
    // Hybrid_Tag

    // Ensure blank line between snippets
    if (!result.endsWith("\n\n"))
        result += newline;

    return result;
}


static const QString write_section(XmlElement *section, int level)
{
#if DUMP_LEVEL > 2
    qDebug() << "..section" << section->attribute("name");
#endif

    QString result;
    result.reserve(1000);

    // Start with HEADER for the section (H1 used for topic title)
    QString sname = section->attribute("name");
    if (sname.isEmpty()) sname = global_names.value(section->attribute("partition_id"));
    result += heading(level+1, sname);

    // Write snippets
    foreach (const auto &snippet, section->xmlChildren("snippet"))
    {
        result += write_snippet(snippet);
    }

    // Write following sections
    foreach (const auto &subsection, section->xmlChildren("section"))
    {
        result += write_section(subsection, level+1);
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
    stream << frontmatterMarker;
    stream << "ImportedOn: " << quotes(imported_date) << newline;

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
    { "Union",     "Union With" },
    { "Parent_To_Offspring", "Immediate Ancestor Of"},
    { "Offspring_To_Parent", "Offspring Of" },
    { "Master_To_Minion", "Comprises Or Encompasses" },
    { "Minion_To_Master", "Belongs To Or Within" },
    { "Public_Attitude_Towards", "Public Attitude Towards" },
    { "Private_Attitude_Towards", "Private Attitude Towards" }
};
static const QMap<QString,QString> nature_attitude{
    { "Arbitrary", ""},
    { "Generic",   ""},
    { "Union",     "" },
    { "Parent_To_Offspring", ""},
    { "Offspring_To_Parent", "" },
    { "Master_To_Minion", "" },
    { "Minion_To_Master", "" },
    { "Public_Attitude_Towards",  "Publicly %1 Towards" },
    { "Private_Attitude_Towards", "Privately %1 Towards" }
};
static const QMap<QString,bool> nature_directional{
    { "Arbitrary", false},
    { "Generic",   false},
    { "Union",     false },
    { "Parent_To_Offspring", true},
    { "Offspring_To_Parent", true },
    { "Master_To_Minion", true },
    { "Minion_To_Master", true },
    { "Public_Attitude_Towards", true },
    { "Private_Attitude_Towards", true }
};
static const QMap<QString,bool> nature_incoming{
    { "Arbitrary", false},
    { "Generic",   false},
    { "Union",     false },
    { "Parent_To_Offspring",      false },
    { "Offspring_To_Parent",      true  },
    { "Master_To_Minion",         false },
    { "Minion_To_Master",         true  },
    { "Public_Attitude_Towards",  false },
    { "Private_Attitude_Towards", false }
};


static QString relationship(const XmlElement *connection)
{
    QString result;
    QString qualifier       = connection->attribute("qualifier");
    const QString attitude  = connection->attribute("attitude");
    const QString rating    = connection->attribute("rating");    // Attitude will always be present
    const QString nature    = connection->attribute("nature");

    // Determine whether to use NATURE or the specific directional QUALIFIER
    if (!qualifier.isEmpty())
    {
        if (qualifier.contains(" / "))
        {
            auto quals = qualifier.split(" / ");
            if (quals.length() > 1)
            {
                if (nature_incoming.value(nature))
                    qualifier = quals[1];
                else
                    qualifier = quals[0];
            }
        }
        result = qualifier;
    }
    else if (!attitude.isEmpty())
        result = nature_attitude.value(nature).arg(attitude);
    else if (!rating.isEmpty())
        result = nature_mapping.value(nature) + "-" + rating;
    else
        result = nature_mapping.value(nature);

    if (result.isEmpty()) result = "Unknown Relationship";

    return result;
}


static void write_topic_file(const XmlElement *topic, const XmlElement *parent, const XmlElement *prev, const XmlElement *next)
{
#if DUMP_LEVEL > 1
    qDebug() << ".topic" << topic->objectName() << ":" << topic_full_name.value(topic);
#endif
    QString category_name = global_names.value(topic->attribute("category_id"));

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

    QString basename = topic->attribute("public_name");
    bool name_alias = (topic_filename.value(topic->attribute("topic_id")) != basename);

    // Aliases belong in the metadata at the start of the file
    auto aliases = topic->xmlChildren("alias");
    if (!aliases.isEmpty() || name_alias)
    {
        QString true_name;
        stream << "Aliases:\n";
        if (name_alias) stream << YAMLLIST << quotes(basename) << newline;
        foreach (const auto &alias, aliases)
        {
            QString name = alias->attribute("name");
            if (alias->attribute("is_true_name") == "true") true_name = name;
            stream << YAMLLIST << quotes(name) << newline;
        }
        if (!true_name.isEmpty()) stream << "True-Name: " << quotes(true_name) << newline;
    }
    stream << "Category: " << quotes(category_name) << newline;
    QStringList tags;
    tags.append(quotes(tag_string("Category", category_name)));

    if (create_prefix_tag || frontmatter_prefix_suffix)
    {
        QString prefix = topic->attribute("prefix");
        if (!prefix.isEmpty())
        {
            if (create_prefix_tag) tags.append(quotes(tag_string("Prefix", prefix)));
            if (frontmatter_prefix_suffix) stream << "Prefix: " << quotes(prefix) << newline;
        }
    }
    if (create_suffix_tag || frontmatter_prefix_suffix)
    {
        QString suffix = topic->attribute("suffix");
        if (!suffix.isEmpty()) {
            if (create_suffix_tag) tags.append(quotes(tag_string("Suffix", suffix)));
            if (frontmatter_prefix_suffix) stream << "Suffix: " << quotes(suffix) << newline;
        }
    }
    stream << "Tags:" << INDENT << tags.join(INDENT) << newline;

    // For each field with a tag_assign, create the field name in the frontmatter, e.g.
    // Class: name
    // School: name
    foreach (const auto &snippet, topicDescendents(topic, "snippet"))
    {
        auto sntype = snippet->attribute("type");
        if (sntype == "Tag_Standard")
        {
            const XmlElement *tag = snippet->xmlChild("tag_assign");
            if (tag) stream << validTag(global_names.value(snippet->attribute("facet_id"))) << ": " << quotes(global_names.value(tag->attribute("tag_id"))) << newline;
        }
        else if (sntype == "Tag_Multi_Domain")
        {
            QStringList tags;
            foreach (const auto &tag, snippet->xmlChildren("tag_assign"))
                tags.append(quotes(tag_full_name.value(tag->attribute("tag_id"))));
            if (tags.length() == 1)
                stream << validTag(snippetLabel(snippet)) << ": " << tags.first() << newline;
            else if (tags.length() > 1)
                stream << validTag(snippetLabel(snippet)) << ": [ " << tags.join(", ") << " ]" << newline;
        }
        else if (frontmatter_labeled_text && sntype == "Labeled_Text")
        {
            if (auto contents = snippet->xmlChild("contents"))
            {
                // No formatting in value!
                QString value = textContent(contents->childString(), /*dice*/ false);

                // If content starts with \n then it might be a table, so add an extra blank line
                if (value.length() < 60 &&
                    !value.startsWith("\n"))   // probably a table, so don't create the variable
                {
                    value = value.trimmed();
                    if (!value.contains("\n"))
                        stream << validTag(snippetLabel(snippet)) << ": " << quotes(value) << newline;
                }
            }

        }
        else if (frontmatter_numeric && sntype == "Numeric")
        {
            if (auto contents = snippet->xmlChild("contents"))
            {
                // No quotes needed, since we are putting in a number
                stream << validTag(snippetLabel(snippet)) << ": " << contents->childString() << newline;
            }
        }
    }

    if (parent) {
        // Don't tell Breadcrumbs about the main page!
        QString link = (parent->objectName() == "topic") ? topic_filename.value(parent->attribute("topic_id")) : validFilename(category_name);
        stream << "parent:\n" + YAMLLIST << quotes(link) << "\nup:\n" + YAMLLIST << quotes(link) << newline;
    }
    if (prev)
    {
        QString link = topic_filename.value(prev->attribute("topic_id"));
        stream << "prev:\n" + YAMLLIST << quotes(link) << newline;
    }
    if (next)
    {
        QString link = topic_filename.value(next->attribute("topic_id"));
        stream << "next:\n" + YAMLLIST << quotes(link) << newline;
        //stream << "same:\n" + YAMLLIST << link << newline;
    }
    const auto children = topic->xmlChildren("topic");
    if (children.length() > 0)
    {
        stream << "down:\n";
        foreach (const auto &child, topic->xmlChildren("topic"))
        {
            stream << YAMLLIST << quotes(topic_filename.value(child->attribute("topic_id"))) << newline;
        }
    }
    stream << "RWtopicId: " << quotes(topic->attribute("topic_id")) << newline;

    // Connections
    foreach (const auto &child, topic->xmlChildren("connection"))
    {
        // Remove spaces from tag
        stream << relationship(child).replace(" ", "") << ": " << quotes(internal_link(child->attribute("target_id"))) << newline;
    }
    stream << frontmatterMarker;

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

    stream << heading(1, topic_full_name.value(topic));

    // Process all <sections>, applying the linkage for this topic
    foreach (const auto &section, topic->xmlChildren("section"))
    {
        // Remove leading and trailing white space (including blank lines)
        // Replace two or more blank lines with just one blank line
        QString text = write_section(section, /*level*/ 1);
        //if (text.contains("\u00a0")) qWarning() << "\nText contains non-break-space at pos "  << text.indexOf("\u00a0") << "\n" << text;
        //if (text.contains("\u200b")) qWarning() << "\nText contains ZERO-width-space at pos " << text.indexOf("\u200b") << "\n" << text;

        stream << text;
    }

    // Provide summary of links to child topics
    auto child_topics = topic->xmlChildren("topic");
    if (!child_topics.isEmpty())
    {
        stream << "---\n## Governed Content\n";

        std::sort(child_topics.begin(), child_topics.end(), sort_topics);
        foreach (const auto &child, child_topics)
        {
            stream << "- " << topic_link(child) << newline;
        }
        stream << newline;  // blank line separator
    }

    auto connections = topic->xmlChildren("connection");
    if (connections.length() > 0)
    {
        stream << "---\n## Connections\n";
        if (!connections_as_graph)
        {
            foreach (const auto &connection, connections)
            {
                // Remove spaces from tag
                stream << relationship(connection) << ": " << internal_link(connection->attribute("target_id")) << annotationText(connection) << newline;
            }
        } else {
            // Now create a MERMAID flowchart
            const QString topic_id    = topic->attribute("topic_id");

            QSet<QString> nodes;
            QSet<QString> relationships;
            QSet<QString> targets;

            foreach (const auto &connection, connections)
            {
                QString source_id         = topic_id;
                QString target_id         = connection->attribute("target_id");
                const QString nature      = connection->attribute("nature");
                const QString annotation  = annotationText(connection, false);  // not const so we can do annotation.replace later

                targets.insert("[[ " + topic_filename.value(target_id) + "]]");

                // Need to be incoming links
                if (nature_incoming.value(nature)) source_id.swap(target_id);

                const QString arrow = nature_directional.value(nature) ? "-->" : "<-->";

                // Add the source and target to the list of nodes
                // (square brackets make the box have rounded ends instead of square)
                nodes.insert(mermaid_node(source_id));
                nodes.insert(mermaid_node(target_id));

                QString label = relationship(connection);
                label.replace("-", newline);
                if (!annotation.isEmpty()) label += newline + annotation;

                if (!label.isEmpty()) label = "-- " + quotes(label) + " ";

                relationships.insert(source_id + label + arrow + target_id);
            }

            if (!nodes.isEmpty() && !relationships.isEmpty())
            {
                stream << codeblock + "mermaid" + newline;
                // graph doesn't allow two-headed arrows
                // TD = topdown, rather than LR = left-to-right
                stream << "flowchart TD" << newline;
                stream << setJoin(nodes, newline) << newline;
                stream << setJoin(relationships, newline) << newline;
                stream << codeblock + newline;

                stream << "%%\nlinks: [ " << setJoin(targets, ", ") << " ]\n%%\n";
            }
        }
        stream << newline;  // blank line separator
    }

    // If any tags are defined at the topic level, then add them now
    QString topic_tags = getTags("", topic, /*withnl*/ false);
    if (topic_tags.length() > 0)
    {
        stream << "\n---\n## Tags\n" << topic_tags << newline;
        stream << newline;  // blank line separator
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

/**
 * @brief write_topic_to_index
 * Write entries into the INDEX file
 * @param stream
 * @param topic
 * @param level
 */
static void write_topic_to_index(QTextStream &stream, XmlElement *topic, int level)
{
    stream << QString(level * 2, ' ') << "- " << topic_link(topic) << newline;
    if (level < max_index_level)
    {
        auto child_topics = topic->xmlChildren("topic");
        if (!child_topics.isEmpty())
        {
            std::sort(child_topics.begin(), child_topics.end(), sort_topics);
            foreach (const auto &child_topic, child_topics)
            {
                write_topic_to_index(stream, child_topic, level+1);
            }
        }
    }
}


static void write_separate_index(const XmlElement *root_elem)
{
    if (root_elem->objectName() != "export") return;

    XmlElement *definition = root_elem->xmlChild("definition");
    XmlElement *details    = definition ? definition->xmlChild("details") : nullptr;
    mainPageName           = details    ? details->attribute("name") : "Table of Contents";
    topic_filename.insert(mainPageName, validFilename(mainPageName));

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
    stream << frontmatterMarker;
    //
    // end of FRONTMATTER
    //

    foreach (const auto &child, root_elem->xmlChildren())
    {
#if DUMP_LEVEL > 0
        qDebug() << "TOP:" << child->objectName();
#endif
        if (child->objectName() == "definition")
        {
            foreach (const auto &header, child->xmlChildren())
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
            // Root level of topics
            QMultiMap<QString,XmlElement*> categories;
            foreach (const auto &topic, child->xmlChildren("topic"))
            {
                categories.insert(global_names.value(topic->attribute("category_id")), topic);
            }

            QStringList unique_keys(categories.uniqueKeys());
            unique_keys.sort();

            foreach (const auto &cat, unique_keys)
            {
                stream << heading(1, internal_link(cat));

                // Organise topics alphabetically
                auto topics = categories.values(cat);
                std::sort(topics.begin(), topics.end(), sort_topics);

                foreach (const auto &topic, topics)
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
    foreach (const auto &topic, contents->xmlChildren("topic"))
    {
        categories.insert(global_names.value(topic->attribute("category_id")), topic);
    }
    QStringList category_names(categories.uniqueKeys());
    category_names.sort();

    foreach (const auto &catname, category_names)
    {
        QString filename = dirFile(validFilename(catname), validFilename(catname)) + ".md";
        QFile file(filename);

        // If it already exists, then don't change it;
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
        stream << "up:\n" + YAMLLIST << quotes(mainPageName) << newline;
        stream << "down:\n";
        auto topics = categories.values(catname);
        std::sort(topics.begin(), topics.end(), sort_topics);
        foreach (const auto &topic, topics)
        {
            if (global_names.value(topic->attribute("category_id")) == catname)
            {
                stream << YAMLLIST << quotes(topic_filename.value(topic->attribute("topic_id"))) << newline;
            }
        }
        stream << "same:\n";
        foreach (const QString &other_cat, category_names)
        {
            if (other_cat != catname) stream << YAMLLIST << quotes(validFilename(other_cat)) << newline;
        }
        stream << frontmatterMarker;
        stream << heading(1, catname);
        file.close();
    }
}


static const XmlElement *findTopicParent(const XmlElement *elem)
{
    QObject const * node = elem;
    while (node && node->objectName() != "topic")
        node = node->parent();
    return qobject_cast<const XmlElement*>(node);
}


static void write_relationships(const XmlElement *root_elem)
{
    const QString folderName("Relationships");

    QMultiMap<QString, const XmlElement*> connections;
    foreach (const auto &connection, root_elem->findChildren<XmlElement*>("connection"))
    {
        QString nature = connection->attribute("nature");
        // Don't include child nodes
        if (nature == "Minion_To_Master" ||
            nature == "Offspring_To_Parent")
            continue;
        connections.insert(nature, connection);
    }

    foreach (const auto &nature, connections.keys())
    {
        QSet<QString> nodes;
        QSet<QString> relationships;

        bool directional = nature_directional.value(nature);
        const QString arrow = directional ? "-->" : "<-->";

        foreach (const auto &connection, connections.values(nature))
        {
            const XmlElement *topic = findTopicParent(connection);
            if (!topic)
            {
                qWarning() << "create_relationships: failed to find topic node for connection!";
                continue;
            }

            const QString topic_id   = topic->attribute("topic_id");
            //const QString topic_name = topic->attribute("public_name");
            const QString target_id  = connection->attribute("target_id");
            const QString annotation = annotationText(connection, false);  // not const so we can do annotation.replace later

            // Add the source and target to the list of nodes
            // (square brackets make the box have rounded ends instead of square)
            nodes.insert(mermaid_node(topic_id));
            nodes.insert(mermaid_node(target_id));

            // If not directional, ensure it only occurs ONCE in the relationships SET
            QString from=topic_id, to=target_id;
            if (!directional && from > to) from.swap(to);

            QString label = relationship(connection);
            label.replace("-", newline);
            if (!annotation.isEmpty()) label += newline + annotation;
            if (!label.isEmpty()) label = "-- " + quotes(label) + " ";

            relationships.insert(from + label + arrow + to);
        }

        if (!nodes.isEmpty() && !relationships.isEmpty())
        {
            QFile file(dirFile(folderName, nature_mapping.value(nature) + ".md"));

            if (!file.open(QFile::WriteOnly|QFile::Text))
            {
                qWarning() << "Failed to create file for relationships";
                return;
            }
            QTextStream stream(&file);
            startFile(stream);
            stream << frontmatterMarker;

            stream << codeblock + "mermaid" + newline;
            // graph doesn't allow two-headed arrows
            // TD = topdown, rather than LR = left-to-right
            stream << "flowchart TD" << newline;
            stream << setJoin(nodes, newline) << newline;
            stream << setJoin(relationships, newline) << newline;
            stream << codeblock + newline;
            file.close();
        }
    }
}


static void write_storyboard(const XmlElement *root_elem)
{
    XmlElement *contents = root_elem->xmlChild("contents");
    if (!contents) return;

    // Collect all the plot ids before we start
    const auto plot_groups = contents->xmlChildren("plot_group");
    foreach (const auto &plot_group, plot_groups)
    {
        foreach (const auto &plot, plot_group->xmlChildren("plot"))
        {
            const QString plot_id   = plot->attribute("plot_id");
            const QString plot_name = plot->attribute("public_name");

            //qDebug() << "PLOT: " << plot_id << " := " << plot_name;
            global_names.insert(plot_id, plot_name);
            topic_filename.insert(plot_id, validFilename(plot_name));
        }
    }

    // Now do the normal work
    foreach (const auto &plot_group, plot_groups)
    {
        const QString group_name = plot_group->attribute("name");

        foreach (const auto &plot, plot_group->xmlChildren("plot"))
        {
            const QString plot_name   = plot->attribute("public_name");
            const QString description = get_elem_string(plot->xmlChild("description"));

            QStringList nodes;
            QStringList links;
            QSet<QString> otherlinks;

            foreach (const auto &node, plot->xmlChildren("node"))
            {
                const QString node_id       = "Node_" + node->attribute("node_id");  // number of node_X
                //const QString description   = get_elem_string(node->xmlChild("description"));
                //const QString gm_directions = get_elem_string(node->xmlChild("gm_directions"));
                QString node_name           = node->attribute("node_name");

                foreach (const auto &edge, node->xmlChildren("edge"))
                {
                    const QString target_node_id = edge->attribute("target_node_id");
                    links.append(node_id + " --> " + "Node_" + target_node_id);
                }
                // We fudge links that have different names to the node name.
                bool real_link=false;
                if (auto link = node->xmlChild("link"))
                {
                    const QString target_id = link->attribute("target_id");  // topic_id
                    if (!target_id.isEmpty())
                    {
                        //qDebug() << "\nNODE: name  = " << node_name;
                        //qDebug() <<   "target_id   = " << global_names.value(target_id);
                        //qDebug() <<   "target_name = " << global_names.value(target_id);
                        QString node_link = topic_filename.value(target_id);
                        if (node_link == node_name)
                            real_link = true;
                        else if (node_name.isEmpty() || node_name.toLower() == global_names.value(target_id).toLower())
                        {
                            // If node has the BASE name (ignoring case), then use the topic name
                            node_name = node_link;
                            real_link = true;
                        }
                        else
                        {
                            QString fake_node = node_id + "a";
                            nodes.append(mermaid_node_raw(fake_node, node_link));
                            links.append(node_id + " -.-> " + fake_node);
                        }
                        otherlinks.insert("[[" + node_link + "]]");
                    }
                }

                // How to get original link_name to be displayed, but the LINK take us to the correct note?
                nodes.append(mermaid_node_raw(node_id, !node_name.isEmpty() ? node_name : "Unnamed", real_link));
            }

            // Create the actual file!
            QFile file(dirFile("Storyboard/" + group_name, plot_name + ".md"));
            if (!file.open(QFile::WriteOnly))
            {
                qWarning() << "Failed to open file for PLOT " << plot_name;
                continue;
            }
            QTextStream stream(&file);

            startFile(stream);
            stream << "Tag: " << quotes("Storyboard") << newline;
            stream << frontmatterMarker;

            stream << heading(1, plot_name);
            if (!description.isEmpty()) stream << description << newline;

            stream << codeblock + "mermaid" + newline;
            stream << "flowchart TD" << newline;
            stream << nodes.join("\n") << newline;
            stream << links.join("\n") << newline;
            stream << codeblock + newline;

            if (!otherlinks.isEmpty())
                stream << "%%links: [ " << setJoin(otherlinks, ", ") << " ]" << newline;
            file.close();
        }
    }
}


static void read_structure(XmlElement *structure)
{
    global_names.clear();
    foreach (const auto &tag, structure->findChildren<XmlElement*>("tag_global"))    // parent is <domain_global>
    {
        const QString tag_id = tag->attribute("tag_id");
        const QString name   = tag->attribute("name");
        tag_full_name.insert(tag_id, tag_string(tag->parent()->attribute("name"), name));
        global_names.insert(tag_id, tag->attribute("name"));
    }
    foreach (const auto &tag, structure->findChildren<XmlElement*>("tag"))   // parent is <domain>
    {
        const QString tag_id = tag->attribute("tag_id");
        const QString name   = tag->attribute("name");
        tag_full_name.insert(tag_id, tag_string(tag->parent()->attribute("name"), name));
        global_names.insert(tag_id, tag->attribute("name"));
    }

    foreach (const auto &cat, structure->findChildren<XmlElement*>("category_global"))
        global_names.insert(cat->attribute("category_id"), cat->attribute("name"));
    foreach (const auto &cat, structure->findChildren<XmlElement*>("category"))
        global_names.insert(cat->attribute("category_id"), cat->attribute("name"));

    foreach (const auto &facet, structure->findChildren<XmlElement*>("facet_global"))
        global_names.insert(facet->attribute("facet_id"), facet->attribute("name"));
    foreach (const auto &facet, structure->findChildren<XmlElement*>("facet"))
        global_names.insert(facet->attribute("facet_id"), facet->attribute("name"));

    foreach (const auto &facet, structure->findChildren<XmlElement*>("partition_global"))
        global_names.insert(facet->attribute("partition_id"), facet->attribute("name"));
    foreach (const auto &facet, structure->findChildren<XmlElement*>("partition"))
        global_names.insert(facet->attribute("partition_id"), facet->attribute("name"));

    foreach (const auto &facet, structure->findChildren<XmlElement*>("domain_global"))
        global_names.insert(facet->attribute("domain_id"), facet->attribute("name"));
    foreach (const auto &facet, structure->findChildren<XmlElement*>("domain"))
        global_names.insert(facet->attribute("domain_id"), facet->attribute("name"));
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
                int  max_image_width,
                bool create_leaflet_pins,
                bool use_reveal_mask,
                bool folders_by_category,
                bool do_obsidian_links,
                bool create_nav_panel,
                bool tag_for_each_prefix,
                bool tag_for_each_suffix,
                bool graph_connections,
                bool mark_dice_rolls,
                bool mark_html_dice_rolls,
                bool do_statblocks,
                bool do_5e_statblocks,
                bool add_admonition_gmdir,
                bool add_admonition_rwstyle,
                bool add_frontmatter_labeled_text,
                bool add_frontmatter_numeric,
                bool add_frontmatter_prefix_suffix,
                bool add_initiative_tracker,
                bool permit_table_extended,
                bool create_category_templates,
                bool link_por_file)
{
#ifdef TIME_CONVERSION
    QElapsedTimer timer;
    timer.start();
#endif
    Q_UNUSED(use_reveal_mask)
    apply_reveal_mask = false; //use_reveal_mask;
    category_folders  = folders_by_category;
    use_wikilinks    = do_obsidian_links;
    show_leaflet_pins = create_leaflet_pins;
    show_nav_panel    = create_nav_panel;
    create_prefix_tag = tag_for_each_prefix,
    create_suffix_tag = tag_for_each_suffix;
    image_max_width   = max_image_width;
    connections_as_graph = graph_connections;
    detect_dice_rolls    = mark_dice_rolls;
    detect_html_dice_rolls = mark_html_dice_rolls;
    create_statblocks = do_statblocks;
    create_5e_statblocks = do_5e_statblocks;
    use_admonition_gmdir = add_admonition_gmdir;
    use_admonition_style = add_admonition_rwstyle;
    frontmatter_labeled_text = add_frontmatter_labeled_text;
    frontmatter_numeric      = add_frontmatter_numeric;
    frontmatter_prefix_suffix = add_frontmatter_prefix_suffix;
    initiative_tracker       = add_initiative_tracker;
    use_table_extended       = permit_table_extended;
    create_por_link          = link_por_file;

    gumbofilenumber   = 0;
    collator.setNumericMode(true);

    imported_date = QDateTime::currentDateTime().toString(QLocale::system().dateTimeFormat());

    read_structure(root_elem->xmlChild("structure"));

    // Patch name of assets directory
    if (QDir(oldAssetsDir).exists() && !QDir(assetsDir).exists())
        QDir::current().rename(oldAssetsDir, assetsDir);

    // To help get category for pins on each individual topic,
    // get the topic_id of every single topic in the file.
    foreach (const auto &topic, root_elem->findChildren<XmlElement*>("topic"))
    {
        // Filename contains the FULL topic name including prefix and suffix
        QString fullname;
        const QString prefix   = topic->attribute("prefix");
        const QString corename = topic->attribute("public_name");
        const QString suffix   = topic->attribute("suffix");

        if (!prefix.isEmpty()) fullname += prefix + " - ";
        fullname += corename;
        if (!suffix.isEmpty()) fullname += " (" + suffix + ")";

        global_names.insert(topic->attribute("topic_id"), corename);
        topic_full_name.insert(topic, fullname);

        QString vfn = validFilename(fullname);
        if (vfn != fullname) qWarning() << "Filename (" << vfn << ") different for " << fullname;
        topic_filename.insert(topic->attribute("topic_id"), validFilename(fullname));
    }

    // Write out the individual TOPIC files now:
    // Note use of findChildren to find children at all levels,
    // whereas xmlChildren returns only direct children.
    if (create_category_templates) write_category_templates(root_elem->xmlChild("structure"));
    write_support_files();
    write_separate_index(root_elem);
    write_category_files(root_elem);

    write_relationships(root_elem);
    write_storyboard(root_elem);

    // A separate file for every single topic
    write_child_topics(root_elem->findChild<XmlElement*>("contents"));

#ifdef TIME_CONVERSION
    qInfo() << "TIME TO GENERATE HTML =" << timer.elapsed() << "milliseconds";
#endif

    // Tidy up memory
    topic_full_name.clear();
    topic_filename.clear();
    global_names.clear();
}
