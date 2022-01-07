![Latest Release Download Count](https://img.shields.io/github/downloads/farling42/RWoutput/latest/RWoutput-installer.exe)

# RWoutput
Tool to convert a Realm Works Output or Export file into other formats.

** NOTE: This tool will NEVER be able to export "protected" content, such as material sold on the Content Market. **

### Instructions for Use

1. Within Realm Works, from the "Share" tab of the ribbon select "Manage Exports" and enter some data, then do one of the following depending upon which output you want:

    - For Markdown, select "Full Export". This will generate a file in your "Realm Works\Export" folder whose name ends with ".rwexport".

    - For other formats, select "Compact Output". This will generate a file in your "Realm Works\Output" folder whose name ends with ".rwoutput".

2. Launch the RWoutput tool, press the "LOAD rwoutput FILE" button and select the file created in step 1.

3. If a separate web page is desired for each topic, then ensure that the "Separate Topic Files" option is selected.

4. If you want to restrict the width of images to something more suitable for web pages, then choose the width in the "Max. Width (pixels)" menu. Optionally choose the "apply reveal mask" if your export should hide unrevealed parts of maps.

5. Choose one of the output options:

    - Create HTML file(s) will create XHTML files containing lots of formatting. (Word does not support importing XHTML files, see HTML4 below).

    - Create PDF file - experimental, and might not always work.

    - Print - print the output to a printer, you could choose the "print to PDF" printer on windows 10 for a more reliable PDF output.

    - Create HTML4 file - this is the intermediate file for printing and PDF generation; so why not have it available. This format can also be imported into Word (and possibly other editors).

    - Create Markdown - which will prompt you for a directory into which the markdown files will be placed.
