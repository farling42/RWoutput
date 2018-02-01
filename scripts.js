includeHTML = function() {
    var z, i, j, elmnt, file, xhttp, parser, doc, body;
    z = document.getElementsByTagName("*");
    for (i = 0; i < z.length; i++) {
        elmnt = z[i];
        file = elmnt.getAttribute("include-html");
        if (file) {
            xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function() {
                if (this.readyState == 4) {
                    if (this.status == 200) {
                        parser = new DOMParser();
                        doc = parser.parseFromString(this.responseText, "text/html");
                        body = doc.getElementsByTagName("body");
                        for (j=0; j < body.length; j++)
                        {
                            elmnt.innerHTML = elmnt.innerHTML + body[j].innerHTML;
                        }
                    }
                    if (this.status == 404) {elmnt.innerHTML = "Page not found.";}
                    elmnt.removeAttribute("include-html");
                    includeHTML();
                }
            }
            xhttp.open("GET", file, true);
            xhttp.send();
            return;
        }
    }
};



function toggleNavVis(item) {
  var i, list, checked;
  checked = item.getAttribute('checked') !== 'true';
  list = item.parentElement.getElementsByTagName('details');
  for (i=0; i < list.length; i++) {
     list[i].open = checked;
  }
  if (checked)
    item.textContent = 'Collapse All'
  else
    item.textContent = 'Expand All'
  item.setAttribute('checked', checked);
};
