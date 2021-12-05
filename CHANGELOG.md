# 3.8
Create placeholder note for the category of each top-level topic.

Default option for "Add NAV panel as header" is OFF, to prevent pollution of the Obsidian graph view (Breadcrumb links do not pollute that graph).

Images and other embedded files now have an external link following them using the text of the annotation or "external link" as the label.

Create annotations properly on each line, appearing after a semi-colon.

Tag_Multi_Domain snippets will now contain tags (using #) instead of plain text.

Internally create strings instead of using QTextStream.

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
