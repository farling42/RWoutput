# 3.14
Newline added after title of ext_object or image

Remove unnecessary ! in front of ]

No line break between ext_object/image and it's annotation link.

Remove excessive blank lines.

Ensure ending of a text format occurs BEFORE the preceding space.

# 3.13
Add detection of text styles to snippet conversion and to embedded HTML file conversion (e.g. statblocks and HL portfolio files).

The handling includes bold, italic, strikethrough, underline, superscript, subscript: 
- Internal links will work with bold, italic, and strikethrough.
- Internal links will not work if inside underline, superscript or subscript text.

It is possible that text containing multiple changes of text style adjacent to each other might not be converted properly.

# 3.10
Attempt to decode tables into markdown format (so that links will work) - tables inside HL portfolio files and statblocks remain as HTML.

Tables with varying number of columns per row (e.g. cells spread across multiple columns) will most likely not be converted properly.

Tables consisting of a single row will be formatted with a thin row above the imported row (since markdown requires a heading row and at least one data row).

Incorporate the "preserveAspect" flag for smart images (courtesy of Leaflet version 4.4.0)

Note that Leaflet version 4.3.7 allows tooltips to appear on map pins without requiring them to be linked to an actual topic :-)

# 3.9
Tags are now also created from Tag_Standard snippets (placed on the following line).

All tags on a Tag_Standard snippet are now created (rather than only the first).

All tags manually added to snippets and/or topics are created (except the automatic Export/<name> and Utility/Empty tags).

# 3.8.1
Fix an issue with the category notes not being created as UTF-8 files.

# 3.8
Create placeholder note for the category of each top-level topic.

Default option for "Add NAV panel as header" is OFF, to prevent pollution of the Obsidian graph view (Breadcrumb links do not pollute that graph).

Images and other embedded files now have an external link following them using the text of the annotation or "external link" as the label.

Create annotations properly on each line, appearing after a semi-colon.

Tag_Multi_Domain snippets will now contain tags (using #) instead of plain text.

Internally create strings instead of using QTextStream.

Note headers no longer have the center HTML tag.

# 3.7
Display annotations on its own line prefixed by "annotation:" in italics (needs more work).

Add connections at the end of each page (and add information into frontmatter for future use).

Reformat various links in frontmatter to use multi-line arrays to avoid needing to handle commas.

Tidy up memory after each run, so that the program can be run several times without polluting other vaults.

# 3.6
Move navigation box to top of each page, thus allowing the Parent line to be removed (with a UI option to disable its creation).

Prefix the tag category with the text "Category/" so that they are grouped better in the tags panel.

Allow any letters (not just latin) into tag names, so should allow accented characters.

Add frontmatter lines to support **Breadcrumbs** plug-in for navigation (https://github.com/SkepticMystic/breadcrumbs)

Topics are created using the FULL name using prefix and suffix when available, to build: "prefix - name ( suffix )"

# 3.5
Smart Images are converted to map pins that are usable with the Leaflet plugin.

Tags in the metadata block don't have a # at the start

# 3.4.1
The HOME and UP boxes in the Navigation pane now uses the name of the main page, rather than "Home".

Ensure that the filename for Notes don't contain invalid characters.

# 3.4

Add aliases in metadata section at the top of each file

Use system date/time format for import date

Put ImportedOn flag into metadata at the top of each file

Put tag into each file that is the name of the topic's category (in metadata block and as a hidden comment, since the metadata tags are ignored)

Add link to parent topic under each topic's title

"Child Topics" renamed to "Governed Content" to match Realm Works

Removed blank lines from Main Page 

Add Navigation bar at the bottom of each page

Two new options in the UI:
- choose to put notes into a single level of folders based on topic Categories, or topic hierarchy (with categories only at the top-level).
- Use \[\[.]] for internal links, or \[.] for all link.
