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
 * 3DS application menu and settings overlays.
 */

#ifdef __3DS__

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>

#include "utils/filepath.h"
#include "utils/nsoption.h"
#include "utils/useragent.h"
#include "utils/utils.h"

#include "netsurf/plotters.h"

#include "framebuffer/app_menu.h"
#include "framebuffer/findfile.h"
#include "framebuffer/font.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"

extern fbtk_widget_t *fbtk;

#define FB_3DS_MENU_PADDING 8
#define FB_3DS_MENU_ROW_HEIGHT 32
#define FB_3DS_SETTINGS_TITLE_HEIGHT 28

#define FB_3DS_POPUP_PANEL_WIDTH 288
#define FB_3DS_POPUP_ROW_HEIGHT 44
#define FB_3DS_POPUP_ICON_SIZE 24
#define FB_3DS_POPUP_ICON_PAD 14
#define FB_3DS_POPUP_TEXT_PAD 10
#define FB_3DS_POPUP_RADIUS 10
#define FB_3DS_POPUP_SHADOW 3
#define FB_3DS_POPUP_POINTER_H 8
#define FB_3DS_POPUP_POINTER_W 14
#define FB_3DS_POPUP_MARGIN 8
#define FB_3DS_POPUP_PANEL_PAD 4
#define FB_3DS_POPUP_CHECKBOX 20
#define FB_3DS_POPUP_CHECKMARK_WIDTH 4
#define FB_3DS_POPUP_FONT_PX 16
#define FB_3DS_POPUP_SCROLL_STEP 22

#define FB_MENU_COLOUR_TURQUOISE 0xFFD0E040
#define FB_MENU_COLOUR_ORANGE 0xFF008CFF
#define FB_MENU_COLOUR_DISABLED_ICON 0xFFBBBBBB
#define FB_MENU_COLOUR_DISABLED_TEXT 0xFF999999
#define FB_MENU_COLOUR_DIVIDER 0xFFE0E0E0
#define FB_MENU_COLOUR_PANEL_TOP 0xFFF0F0F0
#define FB_MENU_COLOUR_PANEL_BOTTOM 0xFFFFFFFF
#define FB_MENU_COLOUR_SHADOW 0xFFAAAAAA
#define FB_MENU_COLOUR_CHECKBOX_BORDER 0xFFCCCCCC
#define FB_MENU_COLOUR_BORDER FB_COLOUR_BLACK
#define FB_MENU_BORDER_WIDTH 1

#define px_to_pt(x) (((x) * 72) / FBTK_DPI)

enum fb_app_overlay_state {
	FB_APP_OVERLAY_NONE = 0,
	FB_APP_OVERLAY_MENU,
	FB_APP_OVERLAY_SETTINGS,
};

enum fb_menu_icon {
	FB_MENU_ICON_GEAR = 0,
	FB_MENU_ICON_PHONE,
	FB_MENU_ICON_STAR,
	FB_MENU_ICON_DOWNLOAD,
	FB_MENU_ICON_HISTORY,
	FB_MENU_ICON_HELP,
};

enum fb_menu_item_type {
	FB_MENU_ITEM_DISABLED = 0,
	FB_MENU_ITEM_TOGGLE,
};

struct fb_menu_item {
	const char *label;
	enum fb_menu_icon icon;
	enum fb_menu_item_type type;
};

static const struct fb_menu_item fb_menu_items[] = {
	{ "Settings", FB_MENU_ICON_GEAR, FB_MENU_ITEM_DISABLED },
	{ "Request mobile site", FB_MENU_ICON_PHONE, FB_MENU_ITEM_TOGGLE },
	{ "Bookmarks", FB_MENU_ICON_STAR, FB_MENU_ITEM_DISABLED },
	{ "Downloads", FB_MENU_ICON_DOWNLOAD, FB_MENU_ITEM_DISABLED },
	{ "History", FB_MENU_ICON_HISTORY, FB_MENU_ITEM_DISABLED },
	{ "Help", FB_MENU_ICON_HELP, FB_MENU_ITEM_DISABLED },
};

#define FB_MENU_ITEM_COUNT \
	((int)(sizeof(fb_menu_items) / sizeof(fb_menu_items[0])))

struct fb_app_overlay {
	enum fb_app_overlay_state state;
	struct gui_window *gw;

	fbtk_widget_t *menu_wnd;
	fbtk_widget_t *menu_backdrop;
	fbtk_widget_t *menu_popup;

	fbtk_widget_t *settings_top;
	fbtk_widget_t *settings_bottom;
	fbtk_widget_t *mobile_toggle_btn;

	int popup_x;
	int popup_y;
	int popup_w;
	int popup_h;
	int content_h;
	int scroll_y;
	int pointer_x;

	bool scroll_dragging;
	int scroll_drag_start_y;
	int scroll_drag_start_scroll;
};

static struct fb_app_overlay app_overlay = {
	.state = FB_APP_OVERLAY_NONE,
};

static void fb_app_overlay_refresh(void);
static void fb_app_mobile_toggle_update(void);
static void fb_app_settings_save(void);
static void fb_app_menu_layout(struct gui_window *gw);
static void fb_app_menu_clamp_scroll(void);
static void fb_app_overlay_hide_internal(struct gui_window *gw);

static void
fb_menu_plot_roundrect_fill(nsfb_t *nsfb, int x0, int y0, int x1, int y1,
		int radius, nsfb_colour_t colour)
{
	nsfb_bbox_t rect;
	nsfb_bbox_t corner;

	if (radius < 1) {
		rect.x0 = x0;
		rect.y0 = y0;
		rect.x1 = x1;
		rect.y1 = y1;
		nsfb_plot_rectangle_fill(nsfb, &rect, colour);
		return;
	}

	rect.x0 = x0 + radius;
	rect.y0 = y0;
	rect.x1 = x1 - radius;
	rect.y1 = y1;
	nsfb_plot_rectangle_fill(nsfb, &rect, colour);

	rect.x0 = x0;
	rect.y0 = y0 + radius;
	rect.x1 = x1;
	rect.y1 = y1 - radius;
	nsfb_plot_rectangle_fill(nsfb, &rect, colour);

	corner.x0 = x0;
	corner.y0 = y0;
	corner.x1 = x0 + radius * 2;
	corner.y1 = y0 + radius * 2;
	nsfb_plot_ellipse_fill(nsfb, &corner, colour);

	corner.x0 = x1 - radius * 2;
	corner.y0 = y0;
	corner.x1 = x1;
	corner.y1 = y0 + radius * 2;
	nsfb_plot_ellipse_fill(nsfb, &corner, colour);

	corner.x0 = x0;
	corner.y0 = y1 - radius * 2;
	corner.x1 = x0 + radius * 2;
	corner.y1 = y1;
	nsfb_plot_ellipse_fill(nsfb, &corner, colour);

	corner.x0 = x1 - radius * 2;
	corner.y0 = y1 - radius * 2;
	corner.x1 = x1;
	corner.y1 = y1;
	nsfb_plot_ellipse_fill(nsfb, &corner, colour);
}

static void
fb_menu_plot_panel_background(nsfb_t *nsfb, int x0, int y0, int x1, int y1,
		int radius)
{
	nsfb_bbox_t band;
	int mid_y = y0 + (y1 - y0) / 3;

	fb_menu_plot_roundrect_fill(nsfb, x0, y0, x1, y1, radius,
			FB_MENU_COLOUR_PANEL_BOTTOM);

	band.x0 = x0 + radius / 2;
	band.y0 = y0 + radius / 2;
	band.x1 = x1 - radius / 2;
	band.y1 = mid_y;
	nsfb_plot_rectangle_fill(nsfb, &band, FB_MENU_COLOUR_PANEL_TOP);
}

static void
fb_menu_plot_pointer(nsfb_t *nsfb, int tip_x, int base_y, nsfb_colour_t colour)
{
	int points[] = {
		tip_x, base_y + FB_3DS_POPUP_POINTER_H,
		tip_x - FB_3DS_POPUP_POINTER_W / 2, base_y,
		tip_x + FB_3DS_POPUP_POINTER_W / 2, base_y,
	};

	nsfb_plot_polygon(nsfb, points, 3, colour);
}

static void
fb_menu_plot_roundrect_border(nsfb_t *nsfb, int x0, int y0, int x1, int y1,
		int radius, int bottom_left_x1, int bottom_right_x0)
{
	int width = FB_MENU_BORDER_WIDTH;
	int inner_r = max(0, radius - width);
	nsfb_bbox_t clear;

	fb_menu_plot_roundrect_fill(nsfb, x0, y0, x1, y1, radius,
			FB_MENU_COLOUR_BORDER);
	fb_menu_plot_panel_background(nsfb, x0 + width, y0 + width,
			x1 - width, y1 - width, inner_r);

	if (bottom_right_x0 > bottom_left_x1) {
		clear.x0 = bottom_left_x1;
		clear.y0 = y1 - width;
		clear.x1 = bottom_right_x0;
		clear.y1 = y1;
		nsfb_plot_rectangle_fill(nsfb, &clear, FB_MENU_COLOUR_PANEL_BOTTOM);
	}
}

static void
fb_menu_plot_pointer_border(nsfb_t *nsfb, int tip_x, int base_y,
		nsfb_colour_t colour)
{
	nsfb_bbox_t line;
	nsfb_plot_pen_t pen;

	pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
	pen.stroke_width = FB_MENU_BORDER_WIDTH;
	pen.stroke_colour = colour;
	pen.fill_type = NFSB_PLOT_OPTYPE_NONE;

	line.x0 = tip_x - FB_3DS_POPUP_POINTER_W / 2;
	line.y0 = base_y;
	line.x1 = tip_x;
	line.y1 = base_y + FB_3DS_POPUP_POINTER_H;
	nsfb_plot_line(nsfb, &line, &pen);

	line.x0 = tip_x + FB_3DS_POPUP_POINTER_W / 2;
	line.y0 = base_y;
	line.x1 = tip_x;
	line.y1 = base_y + FB_3DS_POPUP_POINTER_H;
	nsfb_plot_line(nsfb, &line, &pen);
}

static void
fb_menu_plot_line_icon(nsfb_t *nsfb, int cx, int cy, int half,
		nsfb_colour_t colour, int thickness)
{
	nsfb_bbox_t line;
	nsfb_plot_pen_t pen;

	pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
	pen.stroke_width = thickness;
	pen.stroke_colour = colour;
	pen.fill_type = NFSB_PLOT_OPTYPE_NONE;

	line.x0 = cx - half;
	line.y0 = cy;
	line.x1 = cx + half;
	line.y1 = cy;
	nsfb_plot_line(nsfb, &line, &pen);

	line.x0 = cx;
	line.y0 = cy - half;
	line.x1 = cx;
	line.y1 = cy + half;
	nsfb_plot_line(nsfb, &line, &pen);
}

static void
fb_menu_plot_icon(nsfb_t *nsfb, enum fb_menu_icon icon, int x, int y,
		nsfb_colour_t colour)
{
	int cx = x + FB_3DS_POPUP_ICON_SIZE / 2;
	int cy = y + FB_3DS_POPUP_ICON_SIZE / 2;
	nsfb_bbox_t rect;
	nsfb_plot_pen_t pen;

	pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
	pen.stroke_width = 2;
	pen.stroke_colour = colour;
	pen.fill_type = NFSB_PLOT_OPTYPE_NONE;

	switch (icon) {
	case FB_MENU_ICON_GEAR:
		rect.x0 = cx - 7;
		rect.y0 = cy - 7;
		rect.x1 = cx + 7;
		rect.y1 = cy + 7;
		nsfb_plot_ellipse(nsfb, &rect, colour);
		rect.x0 = cx - 3;
		rect.y0 = cy - 3;
		rect.x1 = cx + 3;
		rect.y1 = cy + 3;
		nsfb_plot_ellipse_fill(nsfb, &rect, colour);
		fb_menu_plot_line_icon(nsfb, cx, cy - 9, 3, colour, 2);
		fb_menu_plot_line_icon(nsfb, cx, cy + 9, 3, colour, 2);
		fb_menu_plot_line_icon(nsfb, cx - 9, cy, 3, colour, 2);
		fb_menu_plot_line_icon(nsfb, cx + 9, cy, 3, colour, 2);
		break;

	case FB_MENU_ICON_PHONE:
		rect.x0 = cx - 5;
		rect.y0 = cy - 8;
		rect.x1 = cx + 5;
		rect.y1 = cy + 8;
		nsfb_plot_rectangle(nsfb, &rect, 2, colour, false, false);
		rect.x0 = cx - 2;
		rect.y0 = cy + 5;
		rect.x1 = cx + 2;
		rect.y1 = cy + 7;
		nsfb_plot_rectangle_fill(nsfb, &rect, colour);
		break;

	case FB_MENU_ICON_STAR:
	{
		int points[] = {
			cx, cy - 8,
			cx + 3, cy - 1,
			cx + 8, cy - 1,
			cx + 4, cy + 3,
			cx + 6, cy + 8,
			cx, cy + 5,
			cx - 6, cy + 8,
			cx - 4, cy + 3,
			cx - 8, cy - 1,
			cx - 3, cy - 1,
		};

		nsfb_plot_polygon(nsfb, points, 10, colour);
		break;
	}

	case FB_MENU_ICON_DOWNLOAD:
	{
		nsfb_bbox_t line;

		line.x0 = cx;
		line.y0 = cy - 7;
		line.x1 = cx;
		line.y1 = cy + 2;
		nsfb_plot_line(nsfb, &line, &pen);
		line.x0 = cx - 4;
		line.y0 = cy - 1;
		line.x1 = cx;
		line.y1 = cy + 4;
		nsfb_plot_line(nsfb, &line, &pen);
		line.x0 = cx + 4;
		line.y0 = cy - 1;
		line.x1 = cx;
		line.y1 = cy + 4;
		nsfb_plot_line(nsfb, &line, &pen);
		rect.x0 = cx - 6;
		rect.y0 = cy + 5;
		rect.x1 = cx + 6;
		rect.y1 = cy + 7;
		nsfb_plot_rectangle_fill(nsfb, &rect, colour);
		break;
	}

	case FB_MENU_ICON_HISTORY:
		rect.x0 = cx - 7;
		rect.y0 = cy - 7;
		rect.x1 = cx + 7;
		rect.y1 = cy + 7;
		nsfb_plot_ellipse(nsfb, &rect, colour);
		{
			nsfb_bbox_t line;

			line.x0 = cx;
			line.y0 = cy;
			line.x1 = cx;
			line.y1 = cy - 5;
			nsfb_plot_line(nsfb, &line, &pen);
			line.x0 = cx;
			line.y0 = cy;
			line.x1 = cx + 4;
			line.y1 = cy + 2;
			nsfb_plot_line(nsfb, &line, &pen);
		}
		break;

	case FB_MENU_ICON_HELP:
		rect.x0 = cx - 7;
		rect.y0 = cy - 7;
		rect.x1 = cx + 7;
		rect.y1 = cy + 7;
		nsfb_plot_ellipse(nsfb, &rect, colour);
		{
			nsfb_bbox_t line;

			line.x0 = cx - 1;
			line.y0 = cy - 4;
			line.x1 = cx + 3;
			line.y1 = cy - 4;
			nsfb_plot_line(nsfb, &line, &pen);
			line.x0 = cx;
			line.y0 = cy - 1;
			line.x1 = cx;
			line.y1 = cy + 2;
			nsfb_plot_line(nsfb, &line, &pen);
		}
		rect.x0 = cx - 1;
		rect.y0 = cy + 5;
		rect.x1 = cx + 1;
		rect.y1 = cy + 7;
		nsfb_plot_rectangle_fill(nsfb, &rect, colour);
		break;
	}
}

static void
fb_menu_plot_thick_segment(nsfb_t *nsfb, int x0, int y0, int x1, int y1,
		int thickness, nsfb_colour_t colour)
{
	int dx = x1 - x0;
	int dy = y1 - y0;
	int len = max(abs(dx), abs(dy));
	int half = thickness / 2;
	int px;
	int py;
	int points[8];

	if (len == 0) {
		return;
	}

	px = (-dy * half) / len;
	py = (dx * half) / len;

	points[0] = x0 + px;
	points[1] = y0 + py;
	points[2] = x0 - px;
	points[3] = y0 - py;
	points[4] = x1 - px;
	points[5] = y1 - py;
	points[6] = x1 + px;
	points[7] = y1 + py;

	nsfb_plot_polygon(nsfb, points, 4, colour);
}

static void
fb_menu_plot_checkbox(nsfb_t *nsfb, int x, int y, bool checked)
{
	nsfb_bbox_t box;
	int t = FB_3DS_POPUP_CHECKMARK_WIDTH;

	box.x0 = x;
	box.y0 = y;
	box.x1 = x + FB_3DS_POPUP_CHECKBOX;
	box.y1 = y + FB_3DS_POPUP_CHECKBOX;
	nsfb_plot_rectangle_fill(nsfb, &box, FB_COLOUR_WHITE);
	nsfb_plot_rectangle(nsfb, &box, 1, FB_MENU_COLOUR_CHECKBOX_BORDER,
			false, false);

	if (!checked) {
		return;
	}

	fb_menu_plot_thick_segment(nsfb,
			x + 4, y + 10,
			x + 9, y + 15,
			t, FB_MENU_COLOUR_ORANGE);
	fb_menu_plot_thick_segment(nsfb,
			x + 8, y + 15,
			x + 16, y + 6,
			t, FB_MENU_COLOUR_ORANGE);
}

static void
fb_menu_plot_text(nsfb_t *nsfb, int x, int y, const char *text,
		nsfb_colour_t colour)
{
	plot_font_style_t font_style;
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = &fb_plotters,
	};

	if (text == NULL) {
		return;
	}

	font_style.family = PLOT_FONT_FAMILY_SANS_SERIF;
	font_style.size = px_to_pt(FB_3DS_POPUP_FONT_PX * PLOT_STYLE_SCALE);
	font_style.weight = 400;
	font_style.flags = FONTF_NONE;
	font_style.background = 0;
	font_style.foreground = colour;

	ctx.plot->text(&ctx, &font_style, x, y, text, strlen(text));
}

static int
fb_menu_visible_height(void)
{
	return app_overlay.popup_h - FB_3DS_POPUP_PANEL_PAD * 2;
}

static int
fb_menu_max_scroll(void)
{
	int visible = fb_menu_visible_height();

	return max(0, app_overlay.content_h - visible);
}

static void
fb_app_menu_clamp_scroll(void)
{
	if (app_overlay.scroll_y < 0) {
		app_overlay.scroll_y = 0;
	}
	if (app_overlay.scroll_y > fb_menu_max_scroll()) {
		app_overlay.scroll_y = fb_menu_max_scroll();
	}
}

static void
fb_app_menu_repaint_exposed_page(struct gui_window *gw)
{
	int exposed_x0;
	int exposed_y0;
	int exposed_x1;
	int exposed_y1;

	if (gw == NULL || app_overlay.menu_popup == NULL) {
		return;
	}

	exposed_x0 = fbtk_get_absx(gw->browser);
	exposed_y0 = fbtk_get_absy(gw->browser);
	exposed_x1 = exposed_x0 + fbtk_get_width(gw->browser);
	exposed_y1 = fbtk_get_absy(app_overlay.menu_popup) - FB_3DS_POPUP_SHADOW;

	if (exposed_y1 > exposed_y0) {
		fb_gui_repaint_rect(gw, exposed_x0, exposed_y0,
				exposed_x1, exposed_y1);
	}
}

static void
fb_app_menu_layout(struct gui_window *gw)
{
	fbtk_widget_t *parent = gw->window;
	int page_x = fbtk_get_absx(gw->browser);
	int page_y = fbtk_get_absy(gw->browser);
	int page_w = fbtk_get_width(gw->browser);
	int page_h = fbtk_get_height(gw->browser);
	int menu_btn_cx;
	int max_panel_h;
	int panel_body_h;

	app_overlay.popup_w = FB_3DS_POPUP_PANEL_WIDTH;
	app_overlay.content_h = FB_MENU_ITEM_COUNT * FB_3DS_POPUP_ROW_HEIGHT;
	max_panel_h = page_h - FB_3DS_POPUP_MARGIN * 2 - FB_3DS_POPUP_POINTER_H;
	panel_body_h = min(app_overlay.content_h + FB_3DS_POPUP_PANEL_PAD * 2,
			max_panel_h);
	app_overlay.popup_h = panel_body_h + FB_3DS_POPUP_POINTER_H;

	if (gw->menu != NULL) {
		menu_btn_cx = fbtk_get_absx(gw->menu) + fbtk_get_width(gw->menu) / 2;
	} else {
		menu_btn_cx = page_x + page_w - FB_3DS_POPUP_MARGIN -
				FB_3DS_POPUP_POINTER_W / 2;
	}

	app_overlay.popup_x = menu_btn_cx - app_overlay.popup_w +
			FB_3DS_POPUP_POINTER_W / 2;
	app_overlay.popup_x = max(page_x + FB_3DS_POPUP_MARGIN,
			app_overlay.popup_x);
	app_overlay.popup_x = min(page_x + page_w - app_overlay.popup_w -
			FB_3DS_POPUP_MARGIN, app_overlay.popup_x);

	app_overlay.pointer_x = menu_btn_cx - app_overlay.popup_x;
	app_overlay.pointer_x = max(FB_3DS_POPUP_RADIUS + FB_3DS_POPUP_POINTER_W,
			app_overlay.pointer_x);
	app_overlay.pointer_x = min(app_overlay.popup_w - FB_3DS_POPUP_RADIUS,
			app_overlay.pointer_x);

	app_overlay.popup_y = page_y + page_h - FB_3DS_POPUP_MARGIN -
			app_overlay.popup_h;

	fbtk_set_pos_and_size(app_overlay.menu_wnd, page_x, page_y, page_w, page_h);
	fbtk_set_pos_and_size(app_overlay.menu_backdrop, 0, 0, page_w, page_h);
	fbtk_set_pos_and_size(app_overlay.menu_popup,
			app_overlay.popup_x - page_x,
			app_overlay.popup_y - page_y,
			app_overlay.popup_w,
			app_overlay.popup_h);

	fb_app_menu_clamp_scroll();
}

static int
fb_menu_popup_redraw(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	nsfb_t *nsfb = fbtk_get_nsfb(widget);
	nsfb_bbox_t bbox;
	int panel_x0;
	int panel_y0;
	int panel_x1;
	int panel_y1;
	int shadow;
	int row;
	int pointer_x;
	int pointer_base_y;
	int pointer_left;
	int pointer_right;

	(void)cbi;

	fbtk_get_bbox(widget, &bbox);
	nsfb_claim(nsfb, &bbox);

	panel_x0 = bbox.x0;
	panel_y0 = bbox.y0;
	panel_x1 = bbox.x1;
	panel_y1 = bbox.y1 - FB_3DS_POPUP_POINTER_H;

	for (shadow = FB_3DS_POPUP_SHADOW; shadow > 0; shadow--) {
		fb_menu_plot_roundrect_fill(nsfb,
				panel_x0 + shadow,
				panel_y0 + shadow,
				panel_x1 + shadow,
				panel_y1,
				FB_3DS_POPUP_RADIUS,
				FB_MENU_COLOUR_SHADOW);
	}

	pointer_x = bbox.x0 + app_overlay.pointer_x;
	pointer_base_y = bbox.y1 - FB_3DS_POPUP_POINTER_H;
	pointer_left = pointer_x - FB_3DS_POPUP_POINTER_W / 2;
	pointer_right = pointer_x + FB_3DS_POPUP_POINTER_W / 2;
	fb_menu_plot_roundrect_border(nsfb, panel_x0, panel_y0, panel_x1, panel_y1,
			FB_3DS_POPUP_RADIUS, pointer_left, pointer_right);
	fb_menu_plot_pointer(nsfb, pointer_x, pointer_base_y,
			FB_MENU_COLOUR_PANEL_BOTTOM);

	{
		nsfb_bbox_t content_clip;

		content_clip.x0 = panel_x0 + FB_MENU_BORDER_WIDTH;
		content_clip.y0 = panel_y0 + FB_3DS_POPUP_PANEL_PAD;
		content_clip.x1 = panel_x1 - FB_MENU_BORDER_WIDTH;
		content_clip.y1 = panel_y1 - FB_3DS_POPUP_PANEL_PAD;
		nsfb_plot_set_clip(nsfb, &content_clip);
	}

	for (row = 0; row < FB_MENU_ITEM_COUNT; row++) {
		const struct fb_menu_item *item = fb_menu_items + row;
		int row_y = panel_y0 + FB_3DS_POPUP_PANEL_PAD +
				row * FB_3DS_POPUP_ROW_HEIGHT -
				app_overlay.scroll_y;
		int icon_x = panel_x0 + FB_3DS_POPUP_ICON_PAD;
		int icon_y = row_y + (FB_3DS_POPUP_ROW_HEIGHT -
				FB_3DS_POPUP_ICON_SIZE) / 2;
		int text_x = icon_x + FB_3DS_POPUP_ICON_SIZE + FB_3DS_POPUP_TEXT_PAD;
		int text_y = row_y + FB_3DS_POPUP_ROW_HEIGHT - 14;
		nsfb_colour_t icon_colour = FB_MENU_COLOUR_TURQUOISE;
		nsfb_colour_t text_colour = FB_COLOUR_BLACK;
		nsfb_bbox_t divider;

		if (row_y + FB_3DS_POPUP_ROW_HEIGHT < panel_y0 + FB_3DS_POPUP_PANEL_PAD ||
				row_y > panel_y1 - FB_3DS_POPUP_PANEL_PAD) {
			continue;
		}

		if (item->type == FB_MENU_ITEM_DISABLED) {
			icon_colour = FB_MENU_COLOUR_DISABLED_ICON;
			text_colour = FB_MENU_COLOUR_DISABLED_TEXT;
		}

		fb_menu_plot_icon(nsfb, item->icon, icon_x, icon_y, icon_colour);
		fb_menu_plot_text(nsfb, text_x, text_y, item->label, text_colour);

		if (item->type == FB_MENU_ITEM_TOGGLE) {
			int check_x = panel_x1 - FB_3DS_POPUP_ICON_PAD -
					FB_3DS_POPUP_CHECKBOX;
			int check_y = row_y + (FB_3DS_POPUP_ROW_HEIGHT -
					FB_3DS_POPUP_CHECKBOX) / 2;

			fb_menu_plot_checkbox(nsfb, check_x, check_y,
					nsoption_bool(fb_mobile_site));
		}

		divider.x0 = panel_x0 + FB_3DS_POPUP_ICON_PAD;
		divider.y0 = row_y + FB_3DS_POPUP_ROW_HEIGHT - 1;
		divider.x1 = panel_x1 - FB_3DS_POPUP_ICON_PAD;
		divider.y1 = row_y + FB_3DS_POPUP_ROW_HEIGHT;
		if (row < FB_MENU_ITEM_COUNT - 1) {
			nsfb_plot_rectangle_fill(nsfb, &divider, FB_MENU_COLOUR_DIVIDER);
		}
	}

	nsfb_plot_set_clip(nsfb, NULL);

	fb_menu_plot_pointer_border(nsfb, pointer_x, pointer_base_y,
			FB_MENU_COLOUR_BORDER);

	nsfb_update(nsfb, &bbox);

	return 0;
}

static int
fb_menu_hit_row(int local_y)
{
	int row = (local_y - FB_3DS_POPUP_PANEL_PAD + app_overlay.scroll_y) /
			FB_3DS_POPUP_ROW_HEIGHT;

	if (row < 0 || row >= FB_MENU_ITEM_COUNT) {
		return -1;
	}

	if (local_y < FB_3DS_POPUP_PANEL_PAD ||
			local_y >= app_overlay.popup_h - FB_3DS_POPUP_POINTER_H -
			FB_3DS_POPUP_PANEL_PAD) {
		return -1;
	}

	return row;
}

static void
fb_menu_activate_row(int row)
{
	const struct fb_menu_item *item;

	if (row < 0 || row >= FB_MENU_ITEM_COUNT) {
		return;
	}

	item = fb_menu_items + row;

	switch (item->type) {
	case FB_MENU_ITEM_DISABLED:
		break;

	case FB_MENU_ITEM_TOGGLE:
		nsoption_set_bool(fb_mobile_site, !nsoption_bool(fb_mobile_site));
		user_agent_rebuild();
		fb_app_settings_save();
		fbtk_request_redraw(app_overlay.menu_popup);
		break;
	}
}

static int
fb_menu_backdrop_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	(void)widget;

	if (cbi->event->type != NSFB_EVENT_KEY_UP) {
		return 0;
	}

	fb_app_overlay_hide_internal(app_overlay.gw);
	return 0;
}

static int
fb_menu_popup_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	int row;

	(void)widget;

	if (cbi->event->type == NSFB_EVENT_KEY_DOWN) {
		if (app_overlay.state != FB_APP_OVERLAY_MENU) {
			return 0;
		}

		app_overlay.scroll_dragging = true;
		app_overlay.scroll_drag_start_y = cbi->y;
		app_overlay.scroll_drag_start_scroll = app_overlay.scroll_y;
		if (fbtk_tgrab_pointer(widget) == false) {
			app_overlay.scroll_dragging = false;
		}
		return 0;
	}

	if (cbi->event->type != NSFB_EVENT_KEY_UP) {
		return 0;
	}

	if (app_overlay.scroll_dragging) {
		int dy = cbi->y - app_overlay.scroll_drag_start_y;

		app_overlay.scroll_dragging = false;

		if (abs(dy) > 6) {
			fbtk_tgrab_pointer(widget);
			return 0;
		}
	}

	fbtk_tgrab_pointer(widget);

	row = fb_menu_hit_row(cbi->y);
	if (row >= 0) {
		fb_menu_activate_row(row);
	}

	return 0;
}

static int
fb_menu_popup_pointer_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	int new_scroll;

	(void)widget;

	if (!app_overlay.scroll_dragging ||
	    app_overlay.state != FB_APP_OVERLAY_MENU ||
	    app_overlay.menu_popup == NULL) {
		return 0;
	}

	new_scroll = app_overlay.scroll_drag_start_scroll -
			(cbi->y - app_overlay.scroll_drag_start_y);
	if (new_scroll == app_overlay.scroll_y) {
		return 0;
	}

	app_overlay.scroll_y = new_scroll;
	fb_app_menu_clamp_scroll();
	fbtk_request_redraw(app_overlay.menu_popup);

	return 0;
}

static void
fb_app_overlay_hide_internal(struct gui_window *gw)
{
	int restore_x = 0;
	int restore_y = 0;
	int restore_w = 0;
	int restore_h = 0;
	bool restore = false;
	enum fb_app_overlay_state closing_state = app_overlay.state;

	if (app_overlay.menu_popup != NULL && gw != NULL &&
	    app_overlay.state == FB_APP_OVERLAY_MENU) {
		restore_x = fbtk_get_absx(app_overlay.menu_popup) -
				FB_3DS_POPUP_SHADOW;
		restore_y = fbtk_get_absy(app_overlay.menu_popup) -
				FB_3DS_POPUP_SHADOW;
		restore_w = fbtk_get_width(app_overlay.menu_popup) +
				FB_3DS_POPUP_SHADOW * 2;
		restore_h = fbtk_get_height(app_overlay.menu_popup) +
				FB_3DS_POPUP_SHADOW * 2;
		restore = true;
	}

	if (app_overlay.menu_wnd != NULL) {
		fbtk_unmap_without_redraw(app_overlay.menu_wnd);
	}
	if (app_overlay.settings_top != NULL) {
		fbtk_unmap_without_redraw(app_overlay.settings_top);
	}
	if (app_overlay.settings_bottom != NULL) {
		fbtk_unmap_without_redraw(app_overlay.settings_bottom);
	}

	app_overlay.state = FB_APP_OVERLAY_NONE;
	app_overlay.scroll_dragging = false;

	if (restore) {
		fb_gui_repaint_rect(gw, restore_x, restore_y,
				restore_x + restore_w,
				restore_y + restore_h);
	} else if (closing_state == FB_APP_OVERLAY_SETTINGS && gw != NULL) {
		fb_gui_repaint_browser(gw);
		fb_gui_flush_display();
	}
}

static int
fb_app_settings_back_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	if (cbi->event->type != NSFB_EVENT_KEY_UP) {
		return 0;
	}

	fb_app_overlay_hide_internal(app_overlay.gw);
	return 0;
}

static int
fb_app_mobile_toggle_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	if (cbi->event->type != NSFB_EVENT_KEY_UP) {
		return 0;
	}

	nsoption_set_bool(fb_mobile_site, !nsoption_bool(fb_mobile_site));
	user_agent_rebuild();
	fb_app_mobile_toggle_update();
	fb_app_settings_save();
	fbtk_request_redraw(app_overlay.settings_bottom);
	fb_gui_flush_display();

	return 0;
}

static void
fb_app_mobile_toggle_update(void)
{
	if (app_overlay.mobile_toggle_btn == NULL) {
		return;
	}

	if (nsoption_bool(fb_mobile_site)) {
		fbtk_set_text(app_overlay.mobile_toggle_btn, "On");
	} else {
		fbtk_set_text(app_overlay.mobile_toggle_btn, "Off");
	}
}

static void
fb_app_settings_save(void)
{
	char *choices;

	choices = filepath_find(respaths, "Choices");
	if (choices != NULL) {
		nsoption_write(choices, NULL, NULL);
		free(choices);
	}
}

static void
fb_app_overlay_refresh(void)
{
	if (app_overlay.state == FB_APP_OVERLAY_MENU &&
	    app_overlay.gw != NULL) {
		fb_app_menu_repaint_exposed_page(app_overlay.gw);
	}
	if (app_overlay.menu_wnd != NULL && app_overlay.menu_popup != NULL) {
		fbtk_set_zorder(app_overlay.menu_wnd, INT_MIN);
		fbtk_request_redraw(app_overlay.menu_popup);
	}
	if (app_overlay.settings_top != NULL) {
		fbtk_set_zorder(app_overlay.settings_top, INT_MIN);
		fbtk_request_redraw(app_overlay.settings_top);
	}
	if (app_overlay.settings_bottom != NULL) {
		fbtk_set_zorder(app_overlay.settings_bottom, INT_MIN);
		fbtk_request_redraw(app_overlay.settings_bottom);
	}
	if (app_overlay.state == FB_APP_OVERLAY_SETTINGS) {
		fbtk_redraw(fbtk);
		fb_gui_flush_display();
	}
}

static nserror
fb_app_menu_init(struct gui_window *gw)
{
	fbtk_widget_t *parent = gw->window;
	int page_x = fbtk_get_absx(gw->browser);
	int page_y = fbtk_get_absy(gw->browser);
	int page_w = max(1, fbtk_get_width(gw->browser));
	int page_h = max(1, fbtk_get_height(gw->browser));

	if (app_overlay.menu_wnd != NULL) {
		return NSERROR_OK;
	}

	app_overlay.menu_wnd = fbtk_create_window(parent, page_x, page_y,
			page_w, page_h, 0);
	if (app_overlay.menu_wnd == NULL) {
		return NSERROR_NOMEM;
	}

	app_overlay.menu_backdrop = fbtk_create_fill(app_overlay.menu_wnd,
			0, 0, page_w, page_h, 0);
	if (app_overlay.menu_backdrop == NULL) {
		return NSERROR_NOMEM;
	}
	fbtk_set_handler(app_overlay.menu_backdrop, FBTK_CBT_CLICK,
			fb_menu_backdrop_click, NULL);

	app_overlay.menu_popup = fbtk_create_user(app_overlay.menu_wnd,
			0, 0, FB_3DS_POPUP_PANEL_WIDTH, 120, NULL);
	if (app_overlay.menu_popup == NULL) {
		return NSERROR_NOMEM;
	}
	fbtk_set_handler(app_overlay.menu_popup, FBTK_CBT_REDRAW,
			fb_menu_popup_redraw, NULL);
	fbtk_set_handler(app_overlay.menu_popup, FBTK_CBT_CLICK,
			fb_menu_popup_click, NULL);
	fbtk_set_handler(app_overlay.menu_popup, FBTK_CBT_POINTERMOVE,
			fb_menu_popup_pointer_move, NULL);

	fbtk_set_mapping(app_overlay.menu_wnd, false);
	return NSERROR_OK;
}

static nserror
fb_app_settings_init(struct gui_window *gw)
{
	fbtk_widget_t *parent = gw->window;
	fbtk_widget_t *button;
	fbtk_widget_t *label;
	int win_w = fbtk_get_width(parent);
	int row_y = FB_3DS_SETTINGS_TITLE_HEIGHT + FB_3DS_MENU_PADDING;
	int toggle_w = 48;
	int label_w = win_w - FB_3DS_MENU_PADDING * 3 - toggle_w;

	if (app_overlay.settings_top != NULL &&
	    app_overlay.settings_bottom != NULL) {
		return NSERROR_OK;
	}

	app_overlay.settings_top = fbtk_create_fill(parent,
						    0, 0,
						    win_w,
						    FB_3DS_SCREEN_HEIGHT,
						    0xFF000000);
	if (app_overlay.settings_top == NULL) {
		return NSERROR_NOMEM;
	}

	app_overlay.settings_bottom = fbtk_create_window(parent,
							 0,
							 FB_3DS_SCREEN_HEIGHT,
							 win_w,
							 FB_3DS_SCREEN_HEIGHT,
							 FB_FRAME_COLOUR);
	if (app_overlay.settings_bottom == NULL) {
		return NSERROR_NOMEM;
	}

	label = fbtk_create_text(app_overlay.settings_bottom,
				 FB_3DS_MENU_PADDING,
				 FB_3DS_MENU_PADDING,
				 win_w - FB_3DS_MENU_PADDING * 2,
				 FB_3DS_SETTINGS_TITLE_HEIGHT,
				 FB_FRAME_COLOUR,
				 FB_COLOUR_BLACK,
				 false);
	fbtk_set_text(label, "Settings");

	label = fbtk_create_text(app_overlay.settings_bottom,
				 FB_3DS_MENU_PADDING,
				 row_y,
				 label_w,
				 FB_3DS_MENU_ROW_HEIGHT,
				 FB_FRAME_COLOUR,
				 FB_COLOUR_BLACK,
				 false);
	fbtk_set_text(label, "Request mobile site");

	app_overlay.mobile_toggle_btn = fbtk_create_text_button(
			app_overlay.settings_bottom,
			win_w - FB_3DS_MENU_PADDING - toggle_w,
			row_y,
			toggle_w,
			FB_3DS_MENU_ROW_HEIGHT,
			FB_COLOUR_WHITE,
			FB_COLOUR_BLACK,
			fb_app_mobile_toggle_click,
			NULL);
	fb_app_mobile_toggle_update();

	button = fbtk_create_text_button(app_overlay.settings_bottom,
					 FB_3DS_MENU_PADDING,
					 FB_3DS_SCREEN_HEIGHT - FB_3DS_MENU_ROW_HEIGHT -
					 FB_3DS_MENU_PADDING,
					 win_w - FB_3DS_MENU_PADDING * 2,
					 FB_3DS_MENU_ROW_HEIGHT,
					 FB_COLOUR_WHITE,
					 FB_COLOUR_BLACK,
					 fb_app_settings_back_click,
					 NULL);
	fbtk_set_text(button, "Back");

	fbtk_set_mapping(app_overlay.settings_top, false);
	fbtk_set_mapping(app_overlay.settings_bottom, false);

	return NSERROR_OK;
}

bool
fb_app_menu_is_shown(void)
{
	return app_overlay.state != FB_APP_OVERLAY_NONE;
}

nserror
fb_app_menu_hide(void)
{
	fb_app_overlay_hide_internal(app_overlay.gw);
	return NSERROR_OK;
}

nserror
fb_app_menu_present(struct gui_window *gw)
{
	nserror res;

	if (app_overlay.state == FB_APP_OVERLAY_MENU &&
	    app_overlay.gw == gw) {
		fb_app_overlay_hide_internal(gw);
		return NSERROR_OK;
	}

	if (app_overlay.state == FB_APP_OVERLAY_SETTINGS) {
		return NSERROR_OK;
	}

	res = fb_app_menu_init(gw);
	if (res != NSERROR_OK) {
		return res;
	}

	app_overlay.gw = gw;
	app_overlay.state = FB_APP_OVERLAY_MENU;
	app_overlay.scroll_y = 0;
	app_overlay.scroll_dragging = false;

	fb_app_menu_layout(gw);

	fbtk_set_mapping(app_overlay.menu_wnd, true);
	fb_app_overlay_refresh();

	return NSERROR_OK;
}

nserror
fb_app_settings_present(struct gui_window *gw)
{
	nserror res;

	res = fb_app_settings_init(gw);
	if (res != NSERROR_OK) {
		return res;
	}

	if (app_overlay.menu_wnd != NULL) {
		fbtk_set_mapping(app_overlay.menu_wnd, false);
	}

	app_overlay.gw = gw;
	app_overlay.state = FB_APP_OVERLAY_SETTINGS;

	fbtk_set_mapping(app_overlay.settings_top, true);
	fbtk_set_mapping(app_overlay.settings_bottom, true);
	fb_app_overlay_refresh();

	return NSERROR_OK;
}

nserror
fb_app_menu_destroy(void)
{
	if (app_overlay.menu_wnd != NULL) {
		fbtk_destroy_widget(app_overlay.menu_wnd);
		app_overlay.menu_wnd = NULL;
	}
	if (app_overlay.settings_top != NULL) {
		fbtk_destroy_widget(app_overlay.settings_top);
		app_overlay.settings_top = NULL;
	}
	if (app_overlay.settings_bottom != NULL) {
		fbtk_destroy_widget(app_overlay.settings_bottom);
		app_overlay.settings_bottom = NULL;
	}

	app_overlay.menu_backdrop = NULL;
	app_overlay.menu_popup = NULL;
	app_overlay.mobile_toggle_btn = NULL;
	app_overlay.gw = NULL;
	app_overlay.state = FB_APP_OVERLAY_NONE;

	return NSERROR_OK;
}

#endif /* __3DS__ */
