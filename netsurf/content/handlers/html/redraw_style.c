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

/**
 * \file
 * CSS visual effect helpers for HTML redraw (opacity, etc.).
 */

#include "netsurf/css.h"
#include "netsurf/plot_style.h"
#include <libcss/fpmath.h>
#include <libcss/computed.h>

#include "html/redraw_style.h"


css_color
html_redraw_css_color_with_opacity(css_color colour,
		const css_computed_style *style)
{
	css_fixed opacity;
	uint8_t type;
	unsigned int alpha;

	if (style == NULL) {
		return colour;
	}

	type = css_computed_opacity(style, &opacity);
	if (type != CSS_OPACITY_SET || opacity >= INTTOFIX(1)) {
		return colour;
	}

	if (nscss_color_is_transparent(colour)) {
		return colour;
	}

	alpha = (colour >> 24) & 0xff;
	alpha = (unsigned int)(((uint64_t) alpha * (uint64_t) opacity) >>
			CSS_RADIX_POINT);
	if (alpha > 0xff) {
		alpha = 0xff;
	}

	return (colour & 0x00ffffff) | (alpha << 24);
}


colour
html_redraw_colour_with_opacity(colour c, const css_computed_style *style)
{
	unsigned int alpha;
	css_fixed opacity;
	uint8_t type;

	if (style == NULL || c == NS_TRANSPARENT) {
		return c;
	}

	type = css_computed_opacity(style, &opacity);
	if (type != CSS_OPACITY_SET || opacity >= INTTOFIX(1)) {
		return c;
	}

	/* NetSurf colours store alpha in the high byte (see plot_style.h) */
	alpha = (c >> 24) & 0xff;
	alpha = (unsigned int)(((uint64_t) alpha * (uint64_t) opacity) >>
			CSS_RADIX_POINT);
	if (alpha > 0xff) {
		alpha = 0xff;
	}

	return (c & 0x00ffffff) | (alpha << 24);
}


int
html_redraw_scale_border_width(int width, float scale)
{
	int scaled;

	if (width <= 0) {
		return 0;
	}

	scaled = (int) (width * scale);

	return scaled < 1 ? 1 : scaled;
}
