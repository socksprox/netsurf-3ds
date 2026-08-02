/*
 * Copyright 2026 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NETSURF_HTML_REDRAW_STYLE_H
#define NETSURF_HTML_REDRAW_STYLE_H

#include <libcss/types.h>
#include <libcss/computed.h>
#include "netsurf/types.h"

/**
 * Apply element opacity to a CSS ARGB colour.
 */
css_color html_redraw_css_color_with_opacity(css_color colour,
		const css_computed_style *style);

/**
 * Apply element opacity to a NetSurf plot colour.
 */
colour html_redraw_colour_with_opacity(colour c,
		const css_computed_style *style);

/**
 * Scale a border width for redraw.
 *
 * A border the author asked for stays at least one device pixel wide, so
 * hairline borders do not vanish when the page is scaled below 1.0.
 */
int html_redraw_scale_border_width(int width, float scale);

#endif
