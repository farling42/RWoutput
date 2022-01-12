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

#ifndef OUTPUTMARKDOWN_H
#define OUTPUTMARKDOWN_H

#include <QString>
class XmlElement;

extern const QString map_pin_title_default;
extern const QString map_pin_description_default;
extern const QString map_pin_gm_directions_default;

extern QString map_pin_title;
extern QString map_pin_description;
extern QString map_pin_gm_directions;


void toMarkdown(const XmlElement *root_elem,
                int  max_image_width,
                bool create_leaflet_pins,
                bool use_reveal_mask,
                bool folders_by_category,
                bool obsidian_links,
                bool create_nav_panel,
                bool tag_for_each_prefix,
                bool tag_for_each_suffix,
                bool graph_connections,
                bool mark_dice_rolls,
                bool mark_html_dice_rolls,
                bool do_statblocks,
                bool do_5e_statblocks,
                bool add_admonition_gmdir,
                bool add_adminition_rwstyle,
                bool add_frontmatter_labeled_text,
                bool add_frontmatter_numeric,
                bool add_initiative_tracker,
                bool permit_table_extended);

#endif // OUTPUTMARKDOWN_H
