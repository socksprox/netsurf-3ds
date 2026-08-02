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
 * CSS outline rendering for HTML boxes.
 */

#include <stdbool.h>

#include "utils/errors.h"
#include "netsurf/plotters.h"
#include "netsurf/css.h"
#include "netsurf/plot_style.h"
#include <libcss/computed.h>
#include <libcss/fpmath.h>

#include "css/utils.h"

#include "html/box.h"
#include "html/private.h"
#include "html/redraw_style.h"


static plot_style_t plot_style_outline = {
	.fill_type = PLOT_OP_TYPE_NONE,
	.stroke_type = PLOT_OP_TYPE_SOLID,
};


static plot_operation_type_t
html_outline_stroke_type(enum css_border_style_e style)
{
	switch (style) {
	case CSS_BORDER_STYLE_DOTTED:
		return PLOT_OP_TYPE_DOT;
	case CSS_BORDER_STYLE_DASHED:
		return PLOT_OP_TYPE_DASH;
	default:
		return PLOT_OP_TYPE_SOLID;
	}
}


static int
html_outline_width_px(const css_computed_style *style,
		const css_unit_ctx *unit_len_ctx,
		float scale)
{
	css_fixed length;
	css_unit unit;
	uint8_t type;
	int width;

	type = css_computed_outline_width(style, &length, &unit);
	if (type != CSS_BORDER_WIDTH_WIDTH) {
		return 0;
	}

	width = FIXTOINT(css_unit_len2device_px(style, unit_len_ctx,
			length, unit));

	return html_redraw_scale_border_width(width, scale);
}


bool
html_redraw_outline(struct box *box,
		int x, int y,
		int padding_width, int padding_height,
		int border_left, int border_top,
		int border_right, int border_bottom,
		const struct rect *clip,
		float scale,
		const css_unit_ctx *unit_len_ctx,
		const struct redraw_context *ctx)
{
	enum css_border_style_e style;
	css_color css_col;
	colour col;
	int ow;
	struct rect rect;
	struct rect outline;
	nserror res;

	if (box->style == NULL) {
		return true;
	}

	style = css_computed_outline_style(box->style);
	if (style == CSS_BORDER_STYLE_NONE ||
	    style == CSS_BORDER_STYLE_HIDDEN) {
		return true;
	}

	ow = html_outline_width_px(box->style, unit_len_ctx, scale);
	if (ow <= 0) {
		return true;
	}

	css_computed_outline_color(box->style, &css_col);
	css_col = html_redraw_css_color_with_opacity(css_col, box->style);
	col = nscss_color_to_ns(css_col);
	if (col == NS_TRANSPARENT) {
		return true;
	}

	/* Outline follows the border edge, drawn outward (CSS 2.1). */
	outline.x0 = x - border_left - ow;
	outline.y0 = y - border_top - ow;
	outline.x1 = x + padding_width + border_right + ow;
	outline.y1 = y + padding_height + border_bottom + ow;

	rect.x0 = outline.x0 < clip->x0 ? clip->x0 : outline.x0;
	rect.y0 = outline.y0 < clip->y0 ? clip->y0 : outline.y0;
	rect.x1 = outline.x1 > clip->x1 ? clip->x1 : outline.x1;
	rect.y1 = outline.y1 > clip->y1 ? clip->y1 : outline.y1;

	if (rect.x0 >= rect.x1 || rect.y0 >= rect.y1) {
		return true;
	}

	plot_style_outline.stroke_type = html_outline_stroke_type(style);
	plot_style_outline.stroke_colour = col;
	plot_style_outline.stroke_width = (ow << PLOT_STYLE_RADIX);

	res = ctx->plot->rectangle(ctx, &plot_style_outline, &rect);

	return res == NSERROR_OK;
}
