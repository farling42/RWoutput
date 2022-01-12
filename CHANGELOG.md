# 4.17
Add support for the "Table Extended" plugin of Obsidian - to allow headerless tables and column spanning.

Add option to create a template MD file for each category defined in the structure definition.

# 4.16
Ensure that minions are added to 5e-statblocks as well as the main character.

Ensure that statblock generation doesn't fail for SWADE (and other) game systems.

# 4.15
For Pathfinder and D&D5E, the encounter blocks now include HP, AC, Init Modifier, XP.
(The XP is optional for D&D5E, since some portfolios won't include challengerating and XP).
Also fixes Action section of generated statblocks to use quotes to ensure no issues with wierd characters.

# 4.14
First look at generating encounter YAML blocks for use with the "Initiative Tracker" plugin of obsidian.md.

# 4.13
Slight change to CSS for RW styles.

Put </span> at the end of paragraphs with an RW style so that they are formatted in the new "live preview" editor.

Ensure all snippets are followed by a single blank line.

# 4.12
Ensure multiple paragraphs in a single styled snippet need to be broken apart with "<br>" instead of fully separate paragraphs.

Include a small label indicating the style at the start of each style block (not when using Admonition blocks).

Add italic font-style to Flavor and Handout RW styles (to match default settings in RW).

Fix a problem with generating the wrong tags.

Handle duplicated link spans in the RWexport file.

# 4.11.1
When putting Labeled_Text into the frontmatter, ensure that the value doesn't include any formatting.

Fix an issue with Tag_Multi_Domain not being decoded properly.

# 4.11
Provide option to add Labeled_Text and/or Numeric fields to the frontmatter.

Only Labeled_Text fields with a single line of text of less than 30 characters will be included.

# 4.10
Move Obsidian.md options into a separate dialog window (as there are so many options now!)

Allow Admonition plug-in to be enabled separately for GM Directions and RW snippet styles.

Any Labeled_Text snippets which start with a table will now be formatted properly (The table will be put on its own line with a blank line between it and the snippet's label)

Statblock improvements:
- Get STATS appearing properly.
- If Dice Roller is enabled, then set flag for 5e-statblock to do the dice replacements (in 5e-statblock version 2).
- only include Saves and Skills which aren't the same as the base stat modifier.
- Always put Passive Perception into Senses, and Perception into the Skills line (even if same bonus as stat).
- Always put in Languages, using "--" if no languages are defined.
- If ability or weapon/attack name ends with "(creature name)" then remove that part.

# 4.9
Make decoding of Portfolios optional.

# 4.8
Add option to generate ADMONITION blocks for GM Directions as well as RW styles (Read-Aloud, Flavor, Callout and Message).

Add option to generate 5E-STATBLOCK blocks for characters stored in HL portfolio files (formatted properly for DnD 5E and sort-of works for Pathfinder 1E too).

A separate *realmworks-admonition.css* is shipped to make the admonition blocks smaller.

# 4.7
Don't auto-number sections (which was added in 4.3).

If the note filename includes prefix and/or suffix, then add an alias into the note containing just the base name.

Save the selected settings after an export/output, so that the same settings can be used when you next open the application.

Revert the change made in 4.4 to have --- / name / --- on three lines in an HL portfolio file be converted into a section header, since it works for Pathfinder files but not for some other game systems!

# 4.6
Add option to mark dice rolls with `dice: <expr>` (for Obsidian "Dice Roller" plug-in).

There is a separate option for HTML files - this will do this work for HL portfolio and other embedded HTML files (such as statblocks).

Note that Dice Roller replaces the dice expression with the result of a dice roll, so not so easy to read a creature's stat block.


# 4.5
Change all loops to use foreach so that memory allocation isn't performed.

When putting in unsupported tags, include all the attributes on each tag.

When handling nested tables, ensure white space is only collected from TD elements.

Change TextStyle handling in decode_gumbo to work properly.

Single spaces wrapped in formatting should now be handled properly.

Put a copy of obsidian.css into the base directory, for those who want to get the file easily.

Reinstate the creation of the main launch page (which hasn't been getting (re)created since 4.0).

# 4.4
Fix indentation of nested lists. Numbered lists should have "1. " as a prefix not "+ "

Standardise the processing to never put line break before the entered text.

Replace "sections" marked by --- in HL portfolio files with proper markdown section header.

Simplify output_gumbo_children (now called decode_gumbo) so that each element is processed entirely in its own little section of code.

# 4.3.1
Ensure that the directory .obsidian is created if it doesn't already exist (rather than only trying to create the snippets sub-folder)

# 4.3
Fix formatting of embedded tables.

Reduce number of double blank lines being produced.

Reduce amount of whitespace produced when decoding embedded HTML (e.g. portfolios or statblocks).

Fix extraction of date information from RWexport files, and convert them into the user's locale time-date format.

Fix formatting of links.

Add additional "True-Name:" variable in frontmatter.

Ensure that the links for connection graphs are placed into a comment block properly ("%%" on its own line before AND after the variable)

Add SPAN in front of GM-Directions and add SPAN for snippets with RW-STYLE or RW-VERACITY. Add a realmworks CSS file (which must be manually enabled).

Put numbering in front of section headers, which should match the numbering in Realm Works

# 4.2
Mark multiple paragraphs inside a GM Direction correctly so that they appear in a single box.

Remove additional blank lines appearing before paragraphs; since markdown keeps line breaks in paragraphs.

Fix issue with formatting appearing on otherwise empty paragraph lines.

# 4.1
Add Tag of Storyboard to each of the created plots.

Add a %%links variable to each Storyboard identifying the topics linked to from the graph.

Change presentation of GM Directions so that they now appear in an indented box instead of with a GMDIR: prefix.

# 4.0
Convert to using RWexport file for Markdown creation. All processing should work as before. 

Nested tables will have the outer table in markdown, with inner tables using HTML markup.

Create a Storyboard top-level folder containing all your story boards (plots) from Realm Works.

Due to the Mermaid graphs not allowing labels to be defined separately from the link destination, any story linked to a topic using a different name will have an additional node linked by a dotted line to a node with the link inside.

Shortened the names of relationships: so only the qualifier will be used if present; also the public/private relationship text is smaller.

Any links which weren't created with version 3.X should now be created properly.

# 3.18
For each Tag_Standard or Tag_Multi_Domain snippet, put a corresponding line in the frontmatter.
Tag_Multi_Domain will be created as a YAML array, Tag_Standard will be created as a single value variable.
The name of the variable will be a valid obsidian tag (e.g. spaces replaced by "-").

Rename asset-files to zz_asset-files so that it doesn't appear at the top of the File Explorer view (it renames the old directory if it exists).

Provide a new output option "Use Mermaid Graph for Connections" which will replace the textual Connections section of each topic with a mermaid graph of the topic's relationships.

Create a new folder called "Relationships" which contains one note per relationship type; each containing all relationships of that type in the realm (potentially very large graphs).

# 3.17.1
Set QRegularExpression so that it will actually check for UNICODE characters when checking letters (by default it only supports ASCII!)

# 3.17
When decoding GM Directions. they are now put onto their own line before the converted snippet.

There is an option to have each GM Direction prefixed with the string "GMDIR: " in bold+italic.

# 3.16
Add some non-ASCII characters to the valid characters allowed in Tags (°&¬)

Remove formatting markers which don't include any actual text.

Revert change when processing HTML (HL Portfolio files and Statblocks) so that "<br>" is replaced by "\n" again.

# 3.15
Provide better formatting by ensuring that end of format is placed before a preceding space.

Try to keep style flowing from one span to the next in the same paragraph.

# 3.14
Newline added after title of ext_object or image

Remove unnecessary ! in front of ], and use wikilinks style links wherever possible.

No line break between ext_object/image and it's annotation link.

Remove excessive blank lines.

Ensure ending of a text format occurs BEFORE the preceding space.

Handle anchors (external links) in snippets; with supported file extensions being displayed inline.

Add an option to create a tag for every topic prefix and/or suffix (issue #42). Each will be put into the Prefix/ or Suffix/ tag group.

Change the "max. width (pixels)" input to allow any number to be entered. This will be applied to plain images in markdown (but not smart images). (issue #41)

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
