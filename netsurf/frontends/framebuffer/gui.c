/*
 * Copyright 2008, 2014 Vincent Sanders <vince@netsurf-browser.org>
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

#include <stdint.h>
#include <limits.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <nsutils/time.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>

#include "utils/utils.h"
#include "utils/nsoption.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "netsurf/browser_window.h"
#include "netsurf/browser.h"
#include "netsurf/keypress.h"
#include "desktop/browser_history.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "netsurf/misc.h"
#include "netsurf/netsurf.h"
#include "netsurf/plot_style.h"
#include "netsurf/cookie_db.h"
#include "content/fetch.h"

#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/schedule.h"
#include "framebuffer/findfile.h"
#include "framebuffer/image_data.h"
#include "framebuffer/font.h"
#include "framebuffer/clipboard.h"
#include "framebuffer/fetch.h"
#include "framebuffer/bitmap.h"
#include "framebuffer/local_history.h"
#include "3ds.h"


#define NSFB_TOOLBAR_DEFAULT_LAYOUT "blfsrutc"

#ifdef __3DS__
#define FB_3DS_SCREEN_HEIGHT 240
#define FB_3DS_PANE_BG FB_FRAME_COLOUR
#define FB_3DS_TITLE_FONT_HEIGHT 14
#define FB_3DS_TITLE_LABEL_HEIGHT 20
#define FB_3DS_TITLE_BOTTOM_MARGIN 16
#define FB_3DS_TITLE_COLOUR 0xFF666666
#define px_to_pt(x) (((x) * 72) / FBTK_DPI)

/** Action bar contents, bottom of the lower screen. No url entry here. */
#define FB_3DS_ACTIONBAR_LAYOUT "blfsrtc"
#define FB_3DS_ACTION_MAX 8

/** Url bar, top of the lower screen. */
#define FB_3DS_URLBAR_HEIGHT 26
#define FB_3DS_URLBAR_PADDING 2

/** Touch travel needed to reveal or dismiss the url bar. */
#define FB_3DS_URLBAR_GESTURE_PX 32
#endif

fbtk_widget_t *fbtk;

static bool fb_complete = false;

struct gui_window *input_window = NULL;
struct gui_window *search_current_window;
struct gui_window *window_list = NULL;

/* private data for browser user widget */
struct browser_widget_s {
	struct browser_window *bw; /**< The browser window connected to this gui window */
	int scrollx, scrolly; /**< scroll offsets. */

	/* Pending window redraw state. */
	bool redraw_required; /**< flag indicating the foreground loop
			       * needs to redraw the browser widget.
			       */
	bbox_t redraw_box; /**< Area requiring redraw. */
	bool pan_required; /**< flag indicating the foreground loop
			    * needs to pan the window.
			    */
	int panx, pany; /**< Panning required. */
#ifdef __3DS__
	bool user_zoomed; /**< User changed zoom from the default fit-to-width. */
#endif
};

#ifdef __3DS__
static bool fb_history_dismiss_touch;
#endif

#ifdef __3DS__
static void widget_set_scroll(struct gui_window *gw, int sx, int sy);
static void fb_3ds_layout_screens(struct gui_window *gw);
static void fb_3ds_set_urlbar_shown(struct gui_window *gw, bool show);

static struct browser_widget_s *
fb_get_bwidget(struct gui_window *gw)
{
	return fbtk_get_userpw(gw->browser);
}

static int
fb_browser_scroll_viewport_height(struct gui_window *gw)
{
	return fbtk_get_height(gw->browser);
}

static int
fb_browser_pane_origin_y(fbtk_widget_t *widget, struct gui_window *gw,
		struct browser_widget_s *bwidget, int abs_y)
{
	if (widget == gw->browser_top) {
		return abs_y - bwidget->scrolly + FB_3DS_SCREEN_HEIGHT;
	}
	return abs_y - bwidget->scrolly;
}

static int
fb_browser_pane_doc_y(fbtk_widget_t *widget, struct gui_window *gw,
		int widget_y, int scrolly)
{
	if (widget == gw->browser_top) {
		return scrolly - FB_3DS_SCREEN_HEIGHT + widget_y;
	}
	return widget_y + scrolly;
}

static void
fb_browser_request_redraw(struct gui_window *gw)
{
	fbtk_request_redraw(gw->browser_top);
	fbtk_request_redraw(gw->browser);
}

static void
fb_3ds_fill_pane(nsfb_t *nsfb, int x, int y, int w, int h)
{
	nsfb_bbox_t pane;

	pane.x0 = x;
	pane.y0 = y;
	pane.x1 = x + w;
	pane.y1 = y + h;
	nsfb_plot_rectangle_fill(nsfb, &pane, FB_3DS_PANE_BG);
}

static void
fb_3ds_update_top_title_visibility(struct gui_window *gw, int scrolly)
{
	bool show = scrolly == 0;

	if (gw->top_title_label == NULL) {
		return;
	}

	if (gw->top_title_shown == show) {
		return;
	}

	gw->top_title_shown = show;
	fbtk_set_mapping(gw->top_title_label, show);
}

/**
 * The url bar only ever belongs to an unscrolled page; once the document
 * spills onto the top screen there is no room for it.
 */
static void
fb_3ds_update_urlbar_visibility(struct gui_window *gw, int scrolly)
{
	if (scrolly != 0) {
		fb_3ds_set_urlbar_shown(gw, false);
	}
}

static void
fb_3ds_position_top_title(struct gui_window *gw, const char *title)
{
	plot_font_style_t font_style;
	int text_w;
	int win_w;
	int label_y = FB_3DS_SCREEN_HEIGHT - FB_3DS_TITLE_LABEL_HEIGHT -
			FB_3DS_TITLE_BOTTOM_MARGIN;

	if (gw->top_title_label == NULL) {
		return;
	}

	win_w = fbtk_get_width(gw->window);
	font_style.family = PLOT_FONT_FAMILY_SANS_SERIF;
	font_style.size = px_to_pt(FB_3DS_TITLE_FONT_HEIGHT * PLOT_STYLE_SCALE);
	font_style.weight = 400;
	font_style.flags = FONTF_NONE;
	font_style.background = FB_3DS_PANE_BG;
	font_style.foreground = FB_3DS_TITLE_COLOUR;

	if (title == NULL || title[0] == '\0' ||
			fb_font_width(&font_style, title, strlen(title),
					&text_w) != NSERROR_OK) {
		fbtk_set_pos_and_size(gw->top_title_label,
				0, label_y, win_w, FB_3DS_TITLE_LABEL_HEIGHT);
		return;
	}

	fbtk_set_pos_and_size(gw->top_title_label,
			max(0, (win_w - text_w) / 2), label_y,
			min(win_w, text_w + 8), FB_3DS_TITLE_LABEL_HEIGHT);
}

#define FB_3DS_ZOOM_STEP 0.1f
#define FB_3DS_PINCH_STEP 0.015f
#define FB_3DS_PINCH_INTERVAL_MS 80

static struct {
	uint64_t last_apply_ms;
} fb_3ds_zoom;

static float
gui_window_minimum_zoom(struct gui_window *gw)
{
	int viewport_w;
	int content_w;
	int content_h;

	viewport_w = fbtk_get_width(gw->browser);
	if (viewport_w <= 0) {
		return 1.0f;
	}

	if (browser_window_get_extents(gw->bw, false,
			&content_w, &content_h) != NSERROR_OK) {
		return 1.0f;
	}

	if (content_w <= 0) {
		return 1.0f;
	}

	if (content_w <= viewport_w) {
		return 1.0f;
	}

	return (float)viewport_w / (float)content_w;
}

static void
gui_window_fit_width(struct gui_window *gw)
{
	struct browser_widget_s *bwidget = fb_get_bwidget(gw);
	float scale;

	if (gw == NULL || gw->browser == NULL || gw->bw == NULL ||
			bwidget == NULL) {
		return;
	}

	scale = gui_window_minimum_zoom(gw);

	if (fabsf(browser_window_get_scale(gw->bw) - scale) > 0.001f) {
		browser_window_set_scale(gw->bw, scale, true);
	}

	if (bwidget->scrollx != 0) {
		widget_set_scroll(gw, 0, bwidget->scrolly);
	}
}

static void
gui_window_zoom_at_point(struct gui_window *gw, float delta)
{
	struct browser_widget_s *bwidget;
	float old_scale;
	float min_scale;
	float new_scale;
	int fx, fy;
	double ratio;
	int new_scrollx, new_scrolly;

	if (gw == NULL || gw->browser == NULL || gw->bw == NULL) {
		return;
	}

	bwidget = fb_get_bwidget(gw);
	if (bwidget == NULL) {
		return;
	}

	old_scale = browser_window_get_scale(gw->bw);
	if (old_scale < 0.001f) {
		return;
	}

	min_scale = gui_window_minimum_zoom(gw);
	new_scale = old_scale + delta;

	if (new_scale < min_scale) {
		new_scale = min_scale;
	}

	if (fabsf(new_scale - old_scale) < 0.0001f) {
		return;
	}

	bwidget->user_zoomed = true;
	if (fabsf(new_scale - min_scale) < 0.001f) {
		bwidget->user_zoomed = false;
	}

	fx = fbtk_get_width(gw->browser) / 2;
	fy = fb_browser_scroll_viewport_height(gw) / 2;
	ratio = (double)new_scale / (double)old_scale;

	browser_window_set_scale(gw->bw, new_scale, true);

	new_scrollx = (int)(((double)bwidget->scrollx + fx) * ratio - fx + 0.5);
	new_scrolly = (int)(((double)bwidget->scrolly + fy) * ratio - fy + 0.5);

	widget_set_scroll(gw, new_scrollx, new_scrolly);

	if (fabsf(new_scale - min_scale) < 0.001f && bwidget->scrollx != 0) {
		widget_set_scroll(gw, 0, bwidget->scrolly);
	}
}
#endif

static struct gui_drag {
	enum state {
		GUI_DRAG_NONE,
		GUI_DRAG_PRESSED,
		GUI_DRAG_DRAG
	} state;
	int button;
	int x;
	int y;
	bool grabbed_pointer;
} gui_drag;

#ifdef __3DS__
static struct {
	int start_scrollx;
	int start_scrolly;
	int start_screen_x;
	int start_screen_y;
	bool panning;
} touch_pan;

/**
 * Reveal or dismiss the url bar from a vertical touch drag.
 *
 * The drag has to both begin and remain at the top of the document, so a
 * flick that merely scrolls back to the top does not bring the bar out; that
 * takes a further swipe.
 *
 * \param gw The window being dragged in.
 * \param dy Touch travel in pixels since the drag started, positive downwards.
 */
static void
fb_3ds_urlbar_gesture(struct gui_window *gw, int dy)
{
	struct browser_widget_s *bwidget = fb_get_bwidget(gw);

	if (bwidget == NULL || bwidget->scrolly != 0 ||
			touch_pan.start_scrolly != 0) {
		return;
	}

	if (dy >= FB_3DS_URLBAR_GESTURE_PX) {
		fb_3ds_set_urlbar_shown(gw, true);
	} else if (dy <= -FB_3DS_URLBAR_GESTURE_PX) {
		fb_3ds_set_urlbar_shown(gw, false);
	}
}

static void
fb_3ds_poll_page_zoom(void)
{
	struct gui_window *gw = window_list;
	u32 down, held;
	float delta = 0.0f;
	uint64_t now;

	if (gw == NULL || gw->browser == NULL || gw->bw == NULL ||
			fb_local_history_is_shown()) {
		return;
	}

	/* Do not change scale while the user is dragging the page. */
	if (gui_drag.state != GUI_DRAG_NONE || touch_pan.panning) {
		return;
	}

	down = hidKeysDown();
	held = hidKeysHeld();

	if (down & KEY_ZL) {
		gui_window_zoom_at_point(gw, -FB_3DS_ZOOM_STEP);
		return;
	}
	if (down & KEY_ZR) {
		gui_window_zoom_at_point(gw, FB_3DS_ZOOM_STEP);
		return;
	}

	if (held & KEY_L) {
		delta -= FB_3DS_PINCH_STEP;
	}
	if (held & KEY_R) {
		delta += FB_3DS_PINCH_STEP;
	}

	if (delta == 0.0f) {
		return;
	}

	nsu_getmonotonic_ms(&now);
	if (now < fb_3ds_zoom.last_apply_ms + FB_3DS_PINCH_INTERVAL_MS) {
		return;
	}

	gui_window_zoom_at_point(gw, delta);
	nsu_getmonotonic_ms(&fb_3ds_zoom.last_apply_ms);
}
#endif


/**
 * Cause an abnormal program termination.
 *
 * \note This never returns and is intended to terminate without any cleanup.
 *
 * \param error The message to display to the user.
 */
static void die(const char *error)
{
	fprintf(stderr, "%s\n", error);
	exit(1);
}


/**
 * Warn the user of an event.
 *
 * \param[in] warning A warning looked up in the message translation table
 * \param[in] detail Additional text to be displayed or NULL.
 * \return NSERROR_OK on success or error code if there was a
 *           faliure displaying the message to the user.
 */
static nserror fb_warn_user(const char *warning, const char *detail)
{
	NSLOG(netsurf, INFO, "%s %s", warning, detail);
	return NSERROR_OK;
}

/* queue a redraw operation, co-ordinates are relative to the window */
static void
fb_queue_redraw(struct fbtk_widget_s *widget, int x0, int y0, int x1, int y1)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);

	bwidget->redraw_box.x0 = min(bwidget->redraw_box.x0, x0);
	bwidget->redraw_box.y0 = min(bwidget->redraw_box.y0, y0);
	bwidget->redraw_box.x1 = max(bwidget->redraw_box.x1, x1);
	bwidget->redraw_box.y1 = max(bwidget->redraw_box.y1, y1);

	if (fbtk_clip_to_widget(widget, &bwidget->redraw_box)) {
		bwidget->redraw_required = true;
		fbtk_request_redraw(widget);
	} else {
		bwidget->redraw_box.y0 = bwidget->redraw_box.x0 = INT_MAX;
		bwidget->redraw_box.y1 = bwidget->redraw_box.x1 = -(INT_MAX);
		bwidget->redraw_required = false;
	}
}

void
fb_gui_repaint_browser(struct gui_window *gw)
{
	if (gw == NULL || gw->browser == NULL) {
		return;
	}

	fb_queue_redraw(gw->browser, 0, 0,
			fbtk_get_width(gw->browser),
			fbtk_get_height(gw->browser));
#ifdef __3DS__
	if (gw->browser_top != NULL) {
		fb_queue_redraw(gw->browser_top, 0, 0,
				fbtk_get_width(gw->browser_top),
				fbtk_get_height(gw->browser_top));
		fbtk_request_redraw(gw->browser_top);
	}
#endif
	fbtk_request_redraw(gw->browser);
	fbtk_redraw(fbtk);
}

void
fb_gui_flush_display(void)
{
	nsfb_t *nsfb;
	nsfb_bbox_t box;

	if (fbtk == NULL) {
		return;
	}

	nsfb = fbtk_get_nsfb(fbtk);
	if (nsfb == NULL) {
		return;
	}

	box.x0 = 0;
	box.y0 = 0;
	box.x1 = fbtk_get_width(fbtk);
	box.y1 = fbtk_get_height(fbtk);
	nsfb_update(nsfb, &box);
}

/* queue a window scroll */
static void
widget_scroll_y(struct gui_window *gw, int y, bool abs)
{
#ifdef __3DS__
	struct browser_widget_s *bwidget = fb_get_bwidget(gw);
#else
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
#endif
	int content_width, content_height;
	int height;

	NSLOG(netsurf, DEEPDEBUG, "window scroll");
	if (abs) {
		bwidget->pany = y - bwidget->scrolly;
	} else {
		bwidget->pany += y;
	}

	browser_window_get_extents(gw->bw, true,
			&content_width, &content_height);

#ifdef __3DS__
	height = fb_browser_scroll_viewport_height(gw);
#else
	height = fbtk_get_height(gw->browser);
#endif

	/* dont pan off the top */
	if ((bwidget->scrolly + bwidget->pany) < 0)
		bwidget->pany = -bwidget->scrolly;

	/* do not pan off the bottom of the content */
	if ((bwidget->scrolly + bwidget->pany) > (content_height - height))
		bwidget->pany = (content_height - height) - bwidget->scrolly;

	if (bwidget->pany == 0)
		return;

	bwidget->pan_required = true;

#ifdef __3DS__
	fb_browser_request_redraw(gw);
#else
	fbtk_request_redraw(gw->browser);
#endif

	if (gw->vscroll != NULL)
		fbtk_set_scroll_position(gw->vscroll, bwidget->scrolly + bwidget->pany);
}

/* queue a window scroll */
static void
widget_scroll_x(struct gui_window *gw, int x, bool abs)
{
#ifdef __3DS__
	struct browser_widget_s *bwidget = fb_get_bwidget(gw);
#else
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
#endif
	int content_width, content_height;
	int width;

	if (abs) {
		bwidget->panx = x - bwidget->scrollx;
	} else {
		bwidget->panx += x;
	}

	browser_window_get_extents(gw->bw, true,
			&content_width, &content_height);

	width = fbtk_get_width(gw->browser);

	/* dont pan off the left */
	if ((bwidget->scrollx + bwidget->panx) < 0)
		bwidget->panx = - bwidget->scrollx;

	/* do not pan off the right of the content */
	if ((bwidget->scrollx + bwidget->panx) > (content_width - width))
		bwidget->panx = (content_width - width) - bwidget->scrollx;

	if (bwidget->panx == 0)
		return;

	bwidget->pan_required = true;

#ifdef __3DS__
	fb_browser_request_redraw(gw);
#else
	fbtk_request_redraw(gw->browser);
#endif

	if (gw->hscroll != NULL)
		fbtk_set_scroll_position(gw->hscroll, bwidget->scrollx + bwidget->panx);
}

#ifdef __3DS__
static void
widget_set_scroll(struct gui_window *gw, int sx, int sy)
{
	struct browser_widget_s *bwidget = fb_get_bwidget(gw);
	int content_width, content_height;
	int width, height;

	browser_window_get_extents(gw->bw, true,
			&content_width, &content_height);

	width = fbtk_get_width(gw->browser);
	height = fb_browser_scroll_viewport_height(gw);

	if (sx < 0)
		sx = 0;
	if (sy < 0)
		sy = 0;
	if (content_width > width && sx > content_width - width)
		sx = content_width - width;
	else if (content_width <= width)
		sx = 0;
	if (content_height > height && sy > content_height - height)
		sy = content_height - height;
	else if (content_height <= height)
		sy = 0;

	if (sx == bwidget->scrollx && sy == bwidget->scrolly)
		return;

	bwidget->scrollx = sx;
	bwidget->scrolly = sy;
	bwidget->panx = 0;
	bwidget->pany = 0;
	bwidget->pan_required = false;
	fb_3ds_update_top_title_visibility(gw, sy);
	fb_3ds_update_urlbar_visibility(gw, sy);
	fb_browser_request_redraw(gw);
}
#endif

static void
fb_pan(fbtk_widget_t *widget,
       struct browser_widget_s *bwidget,
       struct browser_window *bw)
{
	int x;
	int y;
	int width;
	int height;
	nsfb_bbox_t srcbox;
	nsfb_bbox_t dstbox;

	nsfb_t *nsfb = fbtk_get_nsfb(widget);

	height = fbtk_get_height(widget);
	width = fbtk_get_width(widget);

	NSLOG(netsurf, DEEPDEBUG, "panning %d, %d",
			bwidget->panx, bwidget->pany);

	x = fbtk_get_absx(widget);
	y = fbtk_get_absy(widget);

	/* if the pan exceeds the viewport size just redraw the whole area */
	if (bwidget->pany >= height || bwidget->pany <= -height ||
	    bwidget->panx >= width || bwidget->panx <= -width) {

		bwidget->scrolly += bwidget->pany;
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, 0, 0, width, height);

		/* ensure we don't try to scroll again */
		bwidget->panx = 0;
		bwidget->pany = 0;
		bwidget->pan_required = false;
		return;
	}

	if (bwidget->pany < 0) {
		/* pan up by less then viewport height */
		srcbox.x0 = x;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width;
		srcbox.y1 = srcbox.y0 + height + bwidget->pany;

		dstbox.x0 = x;
		dstbox.y0 = y - bwidget->pany;
		dstbox.x1 = dstbox.x0 + width;
		dstbox.y1 = dstbox.y0 + height + bwidget->pany;

		/* move part that remains visible up */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrolly += bwidget->pany;
		fb_queue_redraw(widget, 0, 0, width, - bwidget->pany);

	} else if (bwidget->pany > 0) {
		/* pan down by less then viewport height */
		srcbox.x0 = x;
		srcbox.y0 = y + bwidget->pany;
		srcbox.x1 = srcbox.x0 + width;
		srcbox.y1 = srcbox.y0 + height - bwidget->pany;

		dstbox.x0 = x;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width;
		dstbox.y1 = dstbox.y0 + height - bwidget->pany;

		/* move part that remains visible down */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrolly += bwidget->pany;
		fb_queue_redraw(widget, 0, height - bwidget->pany,
				width, height);
	}

	if (bwidget->panx < 0) {
		/* pan left by less then viewport width */
		srcbox.x0 = x;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width + bwidget->panx;
		srcbox.y1 = srcbox.y0 + height;

		dstbox.x0 = x - bwidget->panx;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width + bwidget->panx;
		dstbox.y1 = dstbox.y0 + height;

		/* move part that remains visible left */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, 0, 0, -bwidget->panx, height);

	} else if (bwidget->panx > 0) {
		/* pan right by less then viewport width */
		srcbox.x0 = x + bwidget->panx;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width - bwidget->panx;
		srcbox.y1 = srcbox.y0 + height;

		dstbox.x0 = x;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width - bwidget->panx;
		dstbox.y1 = dstbox.y0 + height;

		/* move part that remains visible right */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, width - bwidget->panx, 0,
				width, height);
	}

	bwidget->pan_required = false;
	bwidget->panx = 0;
	bwidget->pany = 0;
}

static void
fb_redraw(fbtk_widget_t *widget,
	  struct gui_window *gw,
	  struct browser_widget_s *bwidget,
	  struct browser_window *bw)
{
	int x;
	int y;
	int caret_x, caret_y, caret_h;
	struct rect clip;
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = &fb_plotters
	};
	nsfb_t *nsfb = fbtk_get_nsfb(widget);
	nsfb_bbox_t box;
#ifdef __3DS__
	int origin_y;
#endif

	x = fbtk_get_absx(widget);
	y = fbtk_get_absy(widget);

	/* Use a local copy: both 3DS panes share one bwidget and both call
	 * fb_redraw during the same redraw pass. */
	box = bwidget->redraw_box;
	box.y0 += y;
	box.y1 += y;
	box.x0 += x;
	box.x1 += x;

	clip.x0 = box.x0;
	clip.y0 = box.y0;
	clip.x1 = box.x1;
	clip.y1 = box.y1;

#ifdef __3DS__
	{
		int pane_w = fbtk_get_width(widget);
		int pane_h = fbtk_get_height(widget);

		box.x0 = min(box.x0, x);
		box.y0 = min(box.y0, y);
		box.x1 = max(box.x1, x + pane_w);
		box.y1 = max(box.y1, y + pane_h);
	}
#endif

	nsfb_claim(nsfb, &box);

#ifdef __3DS__
	{
		int pane_w = fbtk_get_width(widget);
		int pane_h = fbtk_get_height(widget);

		fb_3ds_fill_pane(nsfb, x, y, pane_w, pane_h);

		if (widget == gw->browser_top && bwidget->scrolly == 0) {
			nsfb_update(nsfb, &box);
			return;
		}
	}
#endif

#ifdef __3DS__
	origin_y = fb_browser_pane_origin_y(widget, gw, bwidget, y);
	browser_window_redraw(bw,
			x - bwidget->scrollx,
			origin_y,
			&clip, &ctx);
#else
	browser_window_redraw(bw,
			x - bwidget->scrollx,
			y - bwidget->scrolly,
			&clip, &ctx);
#endif

	if (fbtk_get_caret(widget, &caret_x, &caret_y, &caret_h)) {
		/* This widget has caret, so render it */
		nsfb_bbox_t line;
		nsfb_plot_pen_t pen;

		line.x0 = x - bwidget->scrollx + caret_x;
#ifdef __3DS__
		line.y0 = fb_browser_pane_origin_y(widget, gw, bwidget, y) + caret_y;
		line.x1 = x - bwidget->scrollx + caret_x;
		line.y1 = fb_browser_pane_origin_y(widget, gw, bwidget, y) +
			caret_y + caret_h;
#else
		line.y0 = y - bwidget->scrolly + caret_y;
		line.x1 = x - bwidget->scrollx + caret_x;
		line.y1 = y - bwidget->scrolly + caret_y + caret_h;
#endif

		pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
		pen.stroke_width = 1;
		pen.stroke_colour = 0xFF0000FF;

		nsfb_plot_line(nsfb, &line, &pen);
	}

	nsfb_update(fbtk_get_nsfb(widget), &box);

#ifdef __3DS__
	if (widget == gw->browser) {
#endif
	bwidget->redraw_box.y0 = bwidget->redraw_box.x0 = INT_MAX;
	bwidget->redraw_box.y1 = bwidget->redraw_box.x1 = INT_MIN;
	bwidget->redraw_required = false;
#ifdef __3DS__
	}
#endif
}

static int
fb_browser_window_redraw(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget;

#ifdef __3DS__
	if (widget == gw->browser_top) {
		fbtk_request_redraw(gw->browser);
		return 0;
	}
#endif

	bwidget = fbtk_get_userpw(widget);
	if (bwidget == NULL) {
		NSLOG(netsurf, INFO,
		      "browser widget from widget %p was null", widget);
		return -1;
	}

	if (bwidget->pan_required) {
#ifdef __3DS__
		bwidget->scrolly += bwidget->pany;
		bwidget->scrollx += bwidget->panx;
		bwidget->panx = 0;
		bwidget->pany = 0;
		bwidget->pan_required = false;
		fb_3ds_update_top_title_visibility(gw, bwidget->scrolly);
		fb_3ds_update_urlbar_visibility(gw, bwidget->scrolly);
#else
		fb_pan(widget, bwidget, gw->bw);
#endif
	}

	if (bwidget->redraw_required) {
#ifdef __3DS__
		fb_redraw(gw->browser_top, gw, bwidget, gw->bw);
#endif
		fb_redraw(widget, gw, bwidget, gw->bw);
	} else {
		bwidget->redraw_box.x0 = 0;
		bwidget->redraw_box.y0 = 0;
		bwidget->redraw_box.x1 = fbtk_get_width(widget);
		bwidget->redraw_box.y1 = fbtk_get_height(widget);
#ifdef __3DS__
		bwidget->redraw_box.x1 = fbtk_get_width(gw->browser_top);
		bwidget->redraw_box.y1 = fbtk_get_height(gw->browser_top);
		fb_redraw(gw->browser_top, gw, bwidget, gw->bw);
		bwidget->redraw_box.x0 = 0;
		bwidget->redraw_box.y0 = 0;
		bwidget->redraw_box.x1 = fbtk_get_width(widget);
		bwidget->redraw_box.y1 = fbtk_get_height(widget);
#endif
		fb_redraw(widget, gw, bwidget, gw->bw);
	}
	return 0;
}

static int fb_browser_window_destroy(fbtk_widget_t *widget,
		fbtk_callback_info *cbi)
{
	struct browser_widget_s *browser_widget;

	if (widget == NULL) {
		return 0;
	}

	/* Free private data */
	browser_widget = fbtk_get_userpw(widget);
	free(browser_widget);

	return 0;
}

static void
framebuffer_surface_iterator(void *ctx, const char *name, enum nsfb_type_e type)
{
	const char *arg0 = ctx;

	fprintf(stderr, "%s: %s\n", arg0, name);
}

static enum nsfb_type_e fetype = NSFB_SURFACE_COUNT;
static const char *fename;
static int febpp;
static int fewidth;
static int feheight;
static const char *feurl;

static void
framebuffer_pick_default_fename(void *ctx, const char *name, enum nsfb_type_e type)
{
	if (type < fetype) {
		fename = name;
	}
}

static bool
process_cmdline(int argc, char** argv)
{
	int opt;
	int option_index;
	static struct option long_options[] = {
		{0, 0, 0,  0 }
	}; /* no long options */

	NSLOG(netsurf, INFO, "argc %d, argv %p", argc, argv);

	nsfb_enumerate_surface_types(framebuffer_pick_default_fename, NULL);

	febpp = 16;

	fewidth = nsoption_int(window_width);
	if (fewidth <= 0) {
		fewidth = 320;
	}
	feheight = nsoption_int(window_height);
	if (feheight <= 0) {
#ifdef __3DS__
		feheight = FB_3DS_SCREEN_HEIGHT * 2;
#else
		feheight = 240;
#endif
	}

	if ((nsoption_charp(homepage_url) != NULL) && 
	    (nsoption_charp(homepage_url)[0] != '\0')) {
		feurl = nsoption_charp(homepage_url);
	} else {
		feurl = NETSURF_HOMEPAGE;
	}

	while((opt = getopt_long(argc, argv, "f:b:w:h:",
				 long_options, &option_index)) != -1) {
		switch (opt) {
		case 'f':
			fename = optarg;
			break;

		case 'b':
			febpp = atoi(optarg);
			break;

		case 'w':
			fewidth = atoi(optarg);
			break;

		case 'h':
			feheight = atoi(optarg);
			break;

		default:
			fprintf(stderr,
				"Usage: %s [-f frontend] [-b bpp] [-w width] [-h height] <url>\n",
				argv[0]);
			return false;
		}
	}

	if (optind < argc) {
		feurl = argv[optind];
	}

	if (nsfb_type_from_name(fename) == NSFB_SURFACE_NONE) {
		if (strcmp(fename, "?") != 0) {
			fprintf(stderr,
				"%s: Unknown surface `%s`\n", argv[0], fename);
		}
		fprintf(stderr, "%s: Valid surface names are:\n", argv[0]);
		nsfb_enumerate_surface_types(framebuffer_surface_iterator, argv[0]);
		return false;
	}

	return true;
}

/**
 * Set option defaults for framebuffer frontend
 *
 * @param defaults The option table to update.
 * @return error status.
 */
static nserror set_defaults(struct nsoption_s *defaults)
{
	/* Set defaults for absent option strings */
	nsoption_setnull_charp(cookie_file, strdup("~/.netsurf/Cookies"));
	nsoption_setnull_charp(cookie_jar, strdup("~/.netsurf/Cookies"));

	if (nsoption_charp(cookie_file) == NULL ||
	    nsoption_charp(cookie_jar) == NULL) {
		NSLOG(netsurf, INFO, "Failed initialising cookie options");
		return NSERROR_BAD_PARAMETER;
	}

	/* set system colours for framebuffer ui */
	nsoption_set_colour(sys_colour_ActiveBorder, 0x00000000);
	nsoption_set_colour(sys_colour_ActiveCaption, 0x00ddddcc);
	nsoption_set_colour(sys_colour_AppWorkspace, 0x00eeeeee);
	nsoption_set_colour(sys_colour_Background, 0x00aa0000);
	nsoption_set_colour(sys_colour_ButtonFace, 0x00dddddd);
	nsoption_set_colour(sys_colour_ButtonHighlight, 0x00cccccc);
	nsoption_set_colour(sys_colour_ButtonShadow, 0x00bbbbbb);
	nsoption_set_colour(sys_colour_ButtonText, 0x00000000);
	nsoption_set_colour(sys_colour_CaptionText, 0x00000000);
	nsoption_set_colour(sys_colour_GrayText, 0x00777777);
	nsoption_set_colour(sys_colour_Highlight, 0x00ee0000);
	nsoption_set_colour(sys_colour_HighlightText, 0x00000000);
	nsoption_set_colour(sys_colour_InactiveBorder, 0x00000000);
	nsoption_set_colour(sys_colour_InactiveCaption, 0x00ffffff);
	nsoption_set_colour(sys_colour_InactiveCaptionText, 0x00cccccc);
	nsoption_set_colour(sys_colour_InfoBackground, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_InfoText, 0x00000000);
	nsoption_set_colour(sys_colour_Menu, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_MenuText, 0x00000000);
	nsoption_set_colour(sys_colour_Scrollbar, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_ThreeDDarkShadow, 0x00555555);
	nsoption_set_colour(sys_colour_ThreeDFace, 0x00dddddd);
	nsoption_set_colour(sys_colour_ThreeDHighlight, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_ThreeDLightShadow, 0x00999999);
	nsoption_set_colour(sys_colour_ThreeDShadow, 0x00777777);
	nsoption_set_colour(sys_colour_Window, 0x00aaaaaa);
	nsoption_set_colour(sys_colour_WindowFrame, 0x00000000);
	nsoption_set_colour(sys_colour_WindowText, 0x00000000);

	return NSERROR_OK;
}


/**
 * Ensures output logging stream is correctly configured
 */
static bool nslog_stream_configure(FILE *fptr)
{
        /* set log stream to be non-buffering */
	setbuf(fptr, NULL);

	return true;
}

static void framebuffer_run(void)
{
	nsfb_event_t event;
	int timeout; /* timeout in miliseconds */

	while (fb_complete != true) {
		/* run the scheduler and discover how long to wait for
		 * the next event.
		 */
		timeout = schedule_run();

		/* if redraws are pending do not wait for event,
		 * return immediately
		 */
		if (fbtk_get_redraw_pending(fbtk))
			timeout = 0;
	
		// timeout = min(timeout,20);
		timeout = 0;

		// printf("TIMEOUT: %d\n",timeout);

		if (fbtk_event(fbtk, &event, timeout)) {
			if ((event.type == NSFB_EVENT_CONTROL) &&
			    (event.value.controlcode ==  NSFB_CONTROL_QUIT))
				fb_complete = true;
		}

#ifdef __3DS__
		fb_3ds_poll_page_zoom();
#endif

		fbtk_redraw(fbtk);
	}
}

static void gui_quit(void)
{
	NSLOG(netsurf, INFO, "gui_quit");

	urldb_save_cookies(nsoption_charp(cookie_jar));

	framebuffer_finalise();
}

/* called back when click in browser window */
static int
fb_browser_window_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	browser_mouse_state mouse;
	int x = cbi->x + bwidget->scrollx;
	int y = fb_browser_pane_doc_y(widget, gw, cbi->y, bwidget->scrolly);
	uint64_t time_now;
	static struct {
		enum { CLICK_SINGLE, CLICK_DOUBLE, CLICK_TRIPLE } type;
		uint64_t time;
	} last_click;

	if (cbi->event->type != NSFB_EVENT_KEY_DOWN &&
	    cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

#ifdef __3DS__
	if (fb_local_history_is_shown()) {
		if (cbi->event->type == NSFB_EVENT_KEY_UP &&
		    fb_history_dismiss_touch) {
			fb_history_dismiss_touch = false;
			return 1;
		}
		if (widget == gw->browser_top &&
		    cbi->event->type == NSFB_EVENT_KEY_DOWN &&
		    cbi->event->value.keycode == NSFB_KEY_MOUSE_1) {
			fb_history_dismiss_touch = true;
			fb_local_history_hide();
			return 1;
		}
		return 0;
	}
#endif

	NSLOG(netsurf, DEEPDEBUG, "browser window clicked at %d,%d",
			cbi->x, cbi->y);

	switch (cbi->event->type) {
	case NSFB_EVENT_KEY_DOWN:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_MOUSE_1:
#ifdef __3DS__
			touch_pan.start_scrollx = bwidget->scrollx;
			touch_pan.start_scrolly = bwidget->scrolly;
			touch_pan.start_screen_x = fbtk_get_absx(widget) + cbi->x;
			touch_pan.start_screen_y = fbtk_get_absy(widget) + cbi->y;
			touch_pan.panning = false;
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 1;
			gui_drag.x = x;
			gui_drag.y = y;
#else
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_1, x, y);
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 1;
			gui_drag.x = x;
			gui_drag.y = y;
#endif
			break;

		case NSFB_KEY_MOUSE_3:
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_2, x, y);
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 2;
			gui_drag.x = x;
			gui_drag.y = y;
			break;

		case NSFB_KEY_MOUSE_4:
			/* scroll up */
			if (browser_window_scroll_at_point(gw->bw,
							   x, y,
							   0, -100) == false)
				widget_scroll_y(gw, -100, false);
			break;

		case NSFB_KEY_MOUSE_5:
			/* scroll down */
			if (browser_window_scroll_at_point(gw->bw,
							   x, y,
							   0, 100) == false)
				widget_scroll_y(gw, 100, false);
			break;

		default:
			break;

		}

		break;
	case NSFB_EVENT_KEY_UP:

		mouse = 0;
		nsu_getmonotonic_ms(&time_now);

		switch (cbi->event->value.keycode) {
		case NSFB_KEY_MOUSE_1:
#ifdef __3DS__
			if (touch_pan.panning) {
				if (gui_drag.grabbed_pointer) {
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}
				touch_pan.panning = false;
				gui_drag.state = GUI_DRAG_NONE;
				break;
			}
			gui_drag.state = GUI_DRAG_NONE;
			mouse = BROWSER_MOUSE_CLICK_1;
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_1, x, y);
#else
			if (gui_drag.state == GUI_DRAG_DRAG) {
				/* End of a drag, rather than click */

				if (gui_drag.grabbed_pointer) {
					/* need to ungrab pointer */
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}

				gui_drag.state = GUI_DRAG_NONE;

				/* Tell core */
				browser_window_mouse_track(gw->bw, 0, x, y);
				break;
			}
			/* This is a click;
			 * clear PRESSED state and pass to core */
			gui_drag.state = GUI_DRAG_NONE;
			mouse = BROWSER_MOUSE_CLICK_1;
#endif
			break;

		case NSFB_KEY_MOUSE_3:
			if (gui_drag.state == GUI_DRAG_DRAG) {
				/* End of a drag, rather than click */
				gui_drag.state = GUI_DRAG_NONE;

				if (gui_drag.grabbed_pointer) {
					/* need to ungrab pointer */
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}

				/* Tell core */
				browser_window_mouse_track(gw->bw, 0, x, y);
				break;
			}
			/* This is a click;
			 * clear PRESSED state and pass to core */
			gui_drag.state = GUI_DRAG_NONE;
			mouse = BROWSER_MOUSE_CLICK_2;
			break;

		default:
			break;

		}

		/* Determine if it's a double or triple click, allowing
		 * 0.5 seconds (500ms) between clicks
		 */
		if ((time_now < (last_click.time + 500)) &&
		    (cbi->event->value.keycode != NSFB_KEY_MOUSE_4) &&
		    (cbi->event->value.keycode != NSFB_KEY_MOUSE_5)) {
			if (last_click.type == CLICK_SINGLE) {
				/* Set double click */
				mouse |= BROWSER_MOUSE_DOUBLE_CLICK;
				last_click.type = CLICK_DOUBLE;

			} else if (last_click.type == CLICK_DOUBLE) {
				/* Set triple click */
				mouse |= BROWSER_MOUSE_TRIPLE_CLICK;
				last_click.type = CLICK_TRIPLE;
			} else {
				/* Set normal click */
				last_click.type = CLICK_SINGLE;
			}
		} else {
			last_click.type = CLICK_SINGLE;
		}

		if (mouse) {
			browser_window_mouse_click(gw->bw, mouse, x, y);
		}

		last_click.time = time_now;

		break;
	default:
		break;

	}
	return 1;
}

/* called back when movement in browser window */
static int
fb_browser_window_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	browser_mouse_state mouse = 0;
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	int x = cbi->x + bwidget->scrollx;
	int y = fb_browser_pane_doc_y(widget, gw, cbi->y, bwidget->scrolly);

#ifdef __3DS__
	if (fb_local_history_is_shown()) {
		return 0;
	}
#endif

#ifdef __3DS__
	if (gui_drag.state == GUI_DRAG_PRESSED && gui_drag.button == 1) {
		int screen_x = fbtk_get_absx(widget) + cbi->x;
		int screen_y = fbtk_get_absy(widget) + cbi->y;
		int dx = screen_x - touch_pan.start_screen_x;
		int dy = screen_y - touch_pan.start_screen_y;

		if (!touch_pan.panning &&
				(abs(dx) > 5 || abs(dy) > 5)) {
			touch_pan.panning = true;
			gui_drag.grabbed_pointer = fbtk_tgrab_pointer(widget);
		}

		if (touch_pan.panning) {
			fb_3ds_urlbar_gesture(gw, dy);
			widget_set_scroll(gw,
					touch_pan.start_scrollx - dx,
					touch_pan.start_scrolly - dy);
			return 0;
		}
	}

	browser_window_mouse_track(gw->bw, mouse, x, y);
	return 0;
#else

	if (gui_drag.state == GUI_DRAG_PRESSED &&
			(abs(x - gui_drag.x) > 5 ||
			 abs(y - gui_drag.y) > 5)) {
		/* Drag started */
		if (gui_drag.button == 1) {
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_DRAG_1,
					gui_drag.x, gui_drag.y);
		} else {
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_DRAG_2,
					gui_drag.x, gui_drag.y);
		}
		gui_drag.grabbed_pointer = fbtk_tgrab_pointer(widget);
		gui_drag.state = GUI_DRAG_DRAG;
	}

	if (gui_drag.state == GUI_DRAG_DRAG) {
		/* set up mouse state */
		mouse |= BROWSER_MOUSE_DRAG_ON;

		if (gui_drag.button == 1)
			mouse |= BROWSER_MOUSE_HOLDING_1;
		else
			mouse |= BROWSER_MOUSE_HOLDING_2;
	}

	browser_window_mouse_track(gw->bw, mouse, x, y);

	return 0;
#endif
}


static int
fb_browser_window_input(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	static fbtk_modifier_type modifier = FBTK_MOD_CLEAR;
	int ucs4 = -1;

	NSLOG(netsurf, INFO, "got value %d", cbi->event->value.keycode);

	switch (cbi->event->type) {
	case NSFB_EVENT_KEY_DOWN:
		switch (cbi->event->value.keycode) {

		case NSFB_KEY_DELETE:
			browser_window_key_press(gw->bw, NS_KEY_DELETE_RIGHT);
			break;

		case NSFB_KEY_PAGEUP:
			if (browser_window_key_press(gw->bw,
					NS_KEY_PAGE_UP) == false)
				widget_scroll_y(gw, -fbtk_get_height(
						gw->browser), false);
			break;

		case NSFB_KEY_PAGEDOWN:
			if (browser_window_key_press(gw->bw,
					NS_KEY_PAGE_DOWN) == false)
				widget_scroll_y(gw, fbtk_get_height(
						gw->browser), false);
			break;

		case NSFB_KEY_RIGHT:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				/* CTRL held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LINE_END) == false)
					widget_scroll_x(gw, INT_MAX, true);

			} else if (modifier & FBTK_MOD_RSHIFT ||
					modifier & FBTK_MOD_LSHIFT) {
				/* SHIFT held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_WORD_RIGHT) == false)
					widget_scroll_x(gw, fbtk_get_width(
						gw->browser), false);

			} else {
				/* no modifier */
				if (browser_window_key_press(gw->bw,
						NS_KEY_RIGHT) == false)
					widget_scroll_x(gw, 100, false);
			}
			break;

		case NSFB_KEY_LEFT:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				/* CTRL held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LINE_START) == false)
					widget_scroll_x(gw, 0, true);

			} else if (modifier & FBTK_MOD_RSHIFT ||
					modifier & FBTK_MOD_LSHIFT) {
				/* SHIFT held */
				if (browser_window_key_press(gw->bw,
						NS_KEY_WORD_LEFT) == false)
					widget_scroll_x(gw, -fbtk_get_width(
						gw->browser), false);

			} else {
				/* no modifier */
				if (browser_window_key_press(gw->bw,
						NS_KEY_LEFT) == false)
					widget_scroll_x(gw, -100, false);
			}
			break;

		case NSFB_KEY_UP:
			if (browser_window_key_press(gw->bw,
					NS_KEY_UP) == false)
				widget_scroll_y(gw, -100, false);
			break;

		case NSFB_KEY_DOWN:
			if (browser_window_key_press(gw->bw,
					NS_KEY_DOWN) == false)
				widget_scroll_y(gw, 100, false);
			break;

		case NSFB_KEY_MINUS:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, -0.1, false);
			}
			break;

		case NSFB_KEY_EQUALS: /* PLUS */
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, 0.1, false);
			}
			break;

		case NSFB_KEY_0:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				browser_window_set_scale(gw->bw, 1.0, true);
			}
			break;

		case NSFB_KEY_RSHIFT:
			modifier |= FBTK_MOD_RSHIFT;
			break;

		case NSFB_KEY_LSHIFT:
			modifier |= FBTK_MOD_LSHIFT;
			break;

		case NSFB_KEY_RCTRL:
			modifier |= FBTK_MOD_RCTRL;
			break;

		case NSFB_KEY_LCTRL:
			modifier |= FBTK_MOD_LCTRL;
			break;

		case NSFB_KEY_y:
		case NSFB_KEY_z:
			if (cbi->event->value.keycode == NSFB_KEY_z &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL) &&
					(modifier & FBTK_MOD_RSHIFT ||
					 modifier & FBTK_MOD_LSHIFT)) {
				/* Z pressed with CTRL and SHIFT held */
				browser_window_key_press(gw->bw, NS_KEY_REDO);
				break;

			} else if (cbi->event->value.keycode == NSFB_KEY_z &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL)) {
				/* Z pressed with CTRL held */
				browser_window_key_press(gw->bw, NS_KEY_UNDO);
				break;

			} else if (cbi->event->value.keycode == NSFB_KEY_y &&
					(modifier & FBTK_MOD_RCTRL ||
					 modifier & FBTK_MOD_LCTRL)) {
				/* Y pressed with CTRL held */
				browser_window_key_press(gw->bw, NS_KEY_REDO);
				break;
			}
			/* Z or Y pressed but not undo or redo; */
			/* Fall through */

		default:
			ucs4 = fbtk_keycode_to_ucs4(cbi->event->value.keycode,
						    modifier);
			if (ucs4 != -1)
				browser_window_key_press(gw->bw, ucs4);
			break;
		}
		break;

	case NSFB_EVENT_KEY_UP:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_RSHIFT:
			modifier &= ~FBTK_MOD_RSHIFT;
			break;

		case NSFB_KEY_LSHIFT:
			modifier &= ~FBTK_MOD_LSHIFT;
			break;

		case NSFB_KEY_RCTRL:
			modifier &= ~FBTK_MOD_RCTRL;
			break;

		case NSFB_KEY_LCTRL:
			modifier &= ~FBTK_MOD_LCTRL;
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	return 0;
}

static void
fb_update_back_forward(struct gui_window *gw)
{
	struct browser_window *bw = gw->bw;

	fbtk_set_bitmap(gw->back,
			(browser_window_back_available(bw)) ?
			&left_arrow : &left_arrow_g);
	fbtk_set_bitmap(gw->forward,
			(browser_window_forward_available(bw)) ?
			&right_arrow : &right_arrow_g);
}

/* left icon click routine */
static int
fb_leftarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = gw->bw;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	if (browser_window_back_available(bw))
		browser_window_history_back(bw, false);

	fb_update_back_forward(gw);

	return 1;
}

/* right arrow icon click routine */
static int
fb_rightarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = gw->bw;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	if (browser_window_forward_available(bw))
		browser_window_history_forward(bw, false);

	fb_update_back_forward(gw);
	return 1;

}

/* reload icon click routine */
static int
fb_reload_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	browser_window_reload(bw, true);
	return 1;
}

/* stop icon click routine */
static int
fb_stop_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	browser_window_stop(bw);
	return 0;
}

static int
fb_osk_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	map_osk();

	return 0;
}

/* close browser window icon click routine */
static int
fb_close_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	fb_complete = true;

	return 0;
}

static int
fb_scroll_callback(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;

	switch (cbi->type) {
	case FBTK_CBT_SCROLLY:
		widget_scroll_y(gw, cbi->y, true);
		break;

	case FBTK_CBT_SCROLLX:
		widget_scroll_x(gw, cbi->x, true);
		break;

	default:
		break;
	}
	return 0;
}

static int
fb_url_enter(void *pw, char *text)
{
	struct gui_window *gw = pw;
	nsurl *url;
	nserror error;

	error = nsurl_create(text, &url);
	if (error != NSERROR_OK) {
		fb_warn_user("Errorcode:", messages_get_errorcode(error));
	} else {
		browser_window_navigate(gw->bw, url, NULL, BW_NAVIGATE_HISTORY,
				NULL, NULL, NULL);
		nsurl_unref(url);
	}

#ifdef __3DS__
	fb_3ds_set_urlbar_shown(gw, false);
#endif

	return 0;
}

static int
fb_url_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	framebuffer_set_cursor(&caret_image);
	return 0;
}

static int
set_ptr_default_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	framebuffer_set_cursor(&pointer_image);
	return 0;
}

static int
fb_localhistory_btn_clik(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	fb_local_history_present(gw, gw->bw);

	return 0;
}


#ifndef __3DS__
/** Create a toolbar window and populate it with buttons. 
 *
 * The toolbar layout uses a character to define buttons type and position:
 * b - back
 * l - local history
 * f - forward
 * s - stop 
 * r - refresh
 * u - url bar expands to fit remaining space
 * t - throbber/activity indicator
 * c - close the current window
 *
 * The default layout is "blfsrut" there should be no more than a
 * single url bar entry or behaviour will be undefined.
 *
 * @param gw Parent window 
 * @param toolbar_height The height in pixels of the toolbar
 * @param padding The padding in pixels round each element of the toolbar
 * @param frame_col Frame colour.
 * @param toolbar_layout A string defining which buttons and controls
 *                       should be added to the toolbar. May be empty
 *                       string to disable the bar..
 * 
 */
static fbtk_widget_t *
create_toolbar(struct gui_window *gw, 
	       int toolbar_height, 
	       int padding, 
	       colour frame_col,
	       const char *toolbar_layout)
{
	fbtk_widget_t *toolbar;
	fbtk_widget_t *widget;

	int xpos; /* The position of the next widget. */
	int xlhs = 0; /* extent of the left hand side widgets */
	int xdir = 1; /* the direction of movement + or - 1 */
	const char *itmtype; /* type of the next item */

	if (toolbar_layout == NULL) {
		toolbar_layout = NSFB_TOOLBAR_DEFAULT_LAYOUT;
	}

	NSLOG(netsurf, INFO, "Using toolbar layout %s", toolbar_layout);

	itmtype = toolbar_layout;

	/* check for the toolbar being disabled */
	if ((*itmtype == 0) || (*itmtype == 'q')) {
		return NULL;
	}

	toolbar = fbtk_create_window(gw->window, 0, 0, 0, 
				     toolbar_height, 
				     frame_col);

	if (toolbar == NULL) {
		return NULL;
	}

	fbtk_set_handler(toolbar, 
			 FBTK_CBT_POINTERENTER, 
			 set_ptr_default_move, 
			 NULL);


	xpos = padding;

	/* loop proceeds creating widget on the left hand side until
	 * it runs out of layout or encounters a url bar declaration
	 * wherupon it works backwards from the end of the layout
	 * untill the space left is for the url bar
	 */
	while ((itmtype >= toolbar_layout) && 
	       (*itmtype != 0) && 
	       (xdir !=0)) {

		NSLOG(netsurf, INFO, "toolbar adding %c", *itmtype);


		switch (*itmtype) {

		case 'b': /* back */
			widget = fbtk_create_button(toolbar, 
						    (xdir == 1) ? xpos : 
						     xpos - left_arrow.width, 
						    padding, 
						    left_arrow.width, 
						    -padding, 
						    frame_col, 
						    &left_arrow, 
						    fb_leftarrow_click, 
						    gw);
			gw->back = widget; /* keep reference */
			break;

		case 'l': /* local history */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1) ? xpos : 
						     xpos - history_image.width,
						    padding,
						    history_image.width,
						    -padding,
						    frame_col,
						    &history_image,
						    fb_localhistory_btn_clik,
						    gw);
			gw->history = widget;
			break;

		case 'f': /* forward */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - right_arrow.width,
						    padding,
						    right_arrow.width,
						    -padding,
						    frame_col,
						    &right_arrow,
						    fb_rightarrow_click,
						    gw);
			gw->forward = widget;
			break;

		case 'c': /* close the current window */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - stop_image_g.width,
						    padding,
						    stop_image_g.width,
						    -padding,
						    frame_col,
						    &stop_image_g,
						    fb_close_click,
						    gw->bw);
			gw->close = widget;
			break;

		case 's': /* stop  */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - stop_image.width,
						    padding,
						    stop_image.width,
						    -padding,
						    frame_col,
						    &stop_image,
						    fb_stop_click,
						    gw->bw);
			gw->stop = widget;
			break;

		case 'r': /* reload */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - reload.width,
						    padding,
						    reload.width,
						    -padding,
						    frame_col,
						    &reload,
						    fb_reload_click,
						    gw->bw);
			gw->reload = widget;
			break;

		case 't': /* throbber/activity indicator */
			widget = fbtk_create_bitmap(toolbar,
						    (xdir == 1)?xpos : 
						     xpos - throbber0.width,
						    padding,
						    throbber0.width,
						    -padding,
						    frame_col, 
						    &throbber0);
			gw->throbber = widget;
			break;


		case 'u': /* url bar*/
			if (xdir == -1) {
				/* met the u going backwards add url
				 * now we know available extent 
				 */ 

				widget = fbtk_create_writable_text(toolbar,
						   xlhs,
						   padding,
						   xpos - xlhs,
						   -padding,
						   FB_COLOUR_WHITE,
						   FB_COLOUR_BLACK,
						   true,
						   fb_url_enter,
						   gw);

				fbtk_set_handler(widget, 
						 FBTK_CBT_POINTERENTER, 
						 fb_url_move, gw->bw);

				gw->url = widget; /* keep reference */

				/* toolbar is complete */
				xdir = 0;
				break;
			}
			/* met url going forwards, note position and
			 * reverse direction 
			 */
			itmtype = toolbar_layout + strlen(toolbar_layout);
			xdir = -1;
			xlhs = xpos;
			xpos = (2 * fbtk_get_width(toolbar));
			widget = toolbar;
			break;

		default:
			widget = NULL;
			xdir = 0;
			NSLOG(netsurf, INFO,
			      "Unknown element %c in toolbar layout",
			      *itmtype);
		        break;

		}

		if (widget != NULL) {
			xpos += (xdir * (fbtk_get_width(widget) + padding));
		}

		NSLOG(netsurf, INFO, "xpos is %d", xpos);

		itmtype += xdir;
	}

	fbtk_set_mapping(toolbar, true);

	return toolbar;
}


/** Resize a toolbar.
 *
 * @param gw Parent window
 * @param toolbar_height The height in pixels of the toolbar
 * @param padding The padding in pixels round each element of the toolbar
 * @param toolbar_layout A string defining which buttons and controls
 *                       should be added to the toolbar. May be empty
 *                       string to disable the bar.
 */
static void
resize_toolbar(struct gui_window *gw,
	       int toolbar_height,
	       int padding,
	       const char *toolbar_layout)
{
	fbtk_widget_t *widget;

	int xpos; /* The position of the next widget. */
	int xlhs = 0; /* extent of the left hand side widgets */
	int xdir = 1; /* the direction of movement + or - 1 */
	const char *itmtype; /* type of the next item */
	int x = 0, y = 0, w = 0, h = 0;

	if (gw->toolbar == NULL) {
		return;
	}

	if (toolbar_layout == NULL) {
		toolbar_layout = NSFB_TOOLBAR_DEFAULT_LAYOUT;
	}

	itmtype = toolbar_layout;

	if (*itmtype == 0) {
		return;
	}

	fbtk_set_pos_and_size(gw->toolbar, 0, 0, 0, toolbar_height);

	xpos = padding;

	/* loop proceeds creating widget on the left hand side until
	 * it runs out of layout or encounters a url bar declaration
	 * wherupon it works backwards from the end of the layout
	 * untill the space left is for the url bar
	 */
	while (itmtype >= toolbar_layout && xdir != 0) {

		switch (*itmtype) {
		case 'b': /* back */
			widget = gw->back;
			x = (xdir == 1) ? xpos : xpos - left_arrow.width;
			y = padding;
			w = left_arrow.width;
			h = -padding;
			break;

		case 'l': /* local history */
			widget = gw->history;
			x = (xdir == 1) ? xpos : xpos - history_image.width;
			y = padding;
			w = history_image.width;
			h = -padding;
			break;

		case 'f': /* forward */
			widget = gw->forward;
			x = (xdir == 1) ? xpos : xpos - right_arrow.width;
			y = padding;
			w = right_arrow.width;
			h = -padding;
			break;

		case 'c': /* close the current window */
			widget = gw->close;
			x = (xdir == 1) ? xpos : xpos - stop_image_g.width;
			y = padding;
			w = stop_image_g.width;
			h = -padding;
			break;

		case 's': /* stop  */
			widget = gw->stop;
			x = (xdir == 1) ? xpos : xpos - stop_image.width;
			y = padding;
			w = stop_image.width;
			h = -padding;
			break;

		case 'r': /* reload */
			widget = gw->reload;
			x = (xdir == 1) ? xpos : xpos - reload.width;
			y = padding;
			w = reload.width;
			h = -padding;
			break;

		case 't': /* throbber/activity indicator */
			widget = gw->throbber;
			x = (xdir == 1) ? xpos : xpos - throbber0.width;
			y = padding;
			w = throbber0.width;
			h = -padding;
			break;


		case 'u': /* url bar*/
			if (xdir == -1) {
				/* met the u going backwards add url
				 * now we know available extent
				 */
				widget = gw->url;
				x = xlhs;
				y = padding;
				w = xpos - xlhs;
				h = -padding;

				/* toolbar is complete */
				xdir = 0;
				break;
			}
			/* met url going forwards, note position and
			 * reverse direction
			 */
			itmtype = toolbar_layout + strlen(toolbar_layout);
			xdir = -1;
			xlhs = xpos;
			w = fbtk_get_width(gw->toolbar);
			xpos = 2 * w;
			widget = gw->toolbar;
			break;

		default:
			widget = NULL;
		        break;

		}

		if (widget != NULL) {
			if (widget != gw->toolbar)
				fbtk_set_pos_and_size(widget, x, y, w, h);
			xpos += xdir * (w + padding);
		}

		itmtype += xdir;
	}
}
#endif

#ifdef __3DS__

/** One button of the 3DS action bar. */
struct fb_3ds_action_item {
	struct fbtk_bitmap *image;
	fbtk_callback click; /**< NULL for a passive indicator. */
	void *pw;
	fbtk_widget_t **slot; /**< Where the window keeps the widget. */
};

/**
 * Expand an action bar layout string into the items it names.
 *
 * Uses the same letters as the desktop toolbar layout except that the url
 * entry ('u') is not part of the action bar and is skipped.
 *
 * \param gw The window the actions belong to.
 * \param layout Layout string or NULL for the default.
 * \param items Array of at least FB_3DS_ACTION_MAX items to fill in.
 * \return The number of items filled in.
 */
static int
fb_3ds_collect_actions(struct gui_window *gw, const char *layout,
		struct fb_3ds_action_item *items)
{
	int count = 0;

	if (layout == NULL) {
		layout = FB_3DS_ACTIONBAR_LAYOUT;
	}

	for (; (*layout != 0) && (count < FB_3DS_ACTION_MAX); layout++) {
		struct fb_3ds_action_item *item = items + count;

		item->click = NULL;
		item->pw = NULL;

		switch (*layout) {
		case 'b': /* back */
			item->image = &left_arrow;
			item->click = fb_leftarrow_click;
			item->pw = gw;
			item->slot = &gw->back;
			break;

		case 'l': /* local history */
			item->image = &history_image;
			item->click = fb_localhistory_btn_clik;
			item->pw = gw;
			item->slot = &gw->history;
			break;

		case 'f': /* forward */
			item->image = &right_arrow;
			item->click = fb_rightarrow_click;
			item->pw = gw;
			item->slot = &gw->forward;
			break;

		case 's': /* stop */
			item->image = &stop_image;
			item->click = fb_stop_click;
			item->pw = gw->bw;
			item->slot = &gw->stop;
			break;

		case 'r': /* reload */
			item->image = &reload;
			item->click = fb_reload_click;
			item->pw = gw->bw;
			item->slot = &gw->reload;
			break;

		case 't': /* throbber/activity indicator */
			item->image = &throbber0;
			item->slot = &gw->throbber;
			break;

		case 'c': /* close the current window */
			item->image = &stop_image_g;
			item->click = fb_close_click;
			item->pw = gw->bw;
			item->slot = &gw->close;
			break;

		default:
			NSLOG(netsurf, INFO,
			      "Skipping element %c in action bar layout",
			      *layout);
			continue;
		}

		count++;
	}

	return count;
}

/**
 * Spread the action bar items evenly across the bar.
 *
 * \param gw The window owning the action bar.
 * \param items The items to place.
 * \param count Number of items.
 * \param create Create the widgets rather than repositioning them.
 */
static void
fb_3ds_place_actions(struct gui_window *gw, struct fb_3ds_action_item *items,
		int count, bool create)
{
	int bar_w = fbtk_get_width(gw->toolbar);
	int bar_h = fbtk_get_height(gw->toolbar);
	int i;

	for (i = 0; i < count; i++) {
		struct fbtk_bitmap *image = items[i].image;
		int slot_x = (bar_w * i) / count;
		int slot_w = ((bar_w * (i + 1)) / count) - slot_x;
		int x = slot_x + max(0, (slot_w - image->width) / 2);
		int y = max(0, (bar_h - image->height) / 2);

		if (!create) {
			fbtk_set_pos_and_size(*items[i].slot, x, y,
					image->width, image->height);
			continue;
		}

		if (items[i].click == NULL) {
			*items[i].slot = fbtk_create_bitmap(gw->toolbar,
					x, y,
					image->width, image->height,
					FB_FRAME_COLOUR, image);
		} else {
			*items[i].slot = fbtk_create_button(gw->toolbar,
					x, y,
					image->width, image->height,
					FB_FRAME_COLOUR, image,
					items[i].click, items[i].pw);
		}
	}
}

/**
 * Create the action bar along the bottom edge of the lower screen.
 *
 * \param gw The window to attach the bar to.
 * \param bar_height Height of the bar in pixels.
 * \param layout Layout string or NULL for the default.
 */
static void
create_3ds_action_bar(struct gui_window *gw, int bar_height,
		const char *layout)
{
	struct fb_3ds_action_item items[FB_3DS_ACTION_MAX];
	int count;

	if ((layout != NULL) && ((*layout == 0) || (*layout == 'q'))) {
		gw->toolbar = NULL;
		return;
	}

	gw->toolbar = fbtk_create_window(gw->window, 0, 0, 0, bar_height,
					 FB_FRAME_COLOUR);
	if (gw->toolbar == NULL) {
		return;
	}

	fbtk_set_handler(gw->toolbar, FBTK_CBT_POINTERENTER,
			 set_ptr_default_move, NULL);

	count = fb_3ds_collect_actions(gw, layout, items);
	fb_3ds_place_actions(gw, items, count, true);

	fbtk_set_mapping(gw->toolbar, true);
}

/**
 * Create the url bar which sits above the page on the lower screen.
 *
 * The bar starts out hidden; it is revealed by a downwards swipe on an
 * unscrolled page.
 */
static void
create_3ds_urlbar(struct gui_window *gw)
{
	gw->urlbar = fbtk_create_window(gw->window, 0, FB_3DS_SCREEN_HEIGHT,
					0, FB_3DS_URLBAR_HEIGHT,
					FB_FRAME_COLOUR);
	if (gw->urlbar == NULL) {
		return;
	}

	fbtk_set_handler(gw->urlbar, FBTK_CBT_POINTERENTER,
			 set_ptr_default_move, NULL);

	gw->url = fbtk_create_writable_text(gw->urlbar,
					    FB_3DS_URLBAR_PADDING,
					    FB_3DS_URLBAR_PADDING,
					    -FB_3DS_URLBAR_PADDING,
					    -FB_3DS_URLBAR_PADDING,
					    FB_COLOUR_WHITE,
					    FB_COLOUR_BLACK,
					    true,
					    fb_url_enter,
					    gw);

	fbtk_set_handler(gw->url, FBTK_CBT_POINTERENTER, fb_url_move, gw->bw);

	gw->urlbar_shown = false;
	fbtk_set_mapping(gw->urlbar, false);
}

/**
 * Lay out both screens.
 *
 * The top screen is one browser pane. The lower screen is, top to bottom,
 * the optional url bar, the rest of the page, the action bar and the status
 * bar.
 */
static void
fb_3ds_layout_screens(struct gui_window *gw)
{
	int furniture_width = nsoption_int(fb_furniture_size);
	int win_w = fbtk_get_width(gw->window);
	int win_h = fbtk_get_height(gw->window);
	int bar_h = (gw->toolbar != NULL) ? fbtk_get_height(gw->toolbar) : 0;
	int url_h = gw->urlbar_shown ? FB_3DS_URLBAR_HEIGHT : 0;
	int page_y = FB_3DS_SCREEN_HEIGHT + url_h;
	int page_h = max(1, win_h - furniture_width - bar_h - page_y);

	fbtk_set_pos_and_size(gw->browser_top, 0, 0,
			      win_w, FB_3DS_SCREEN_HEIGHT);

	if (gw->urlbar != NULL) {
		fbtk_set_pos_and_size(gw->urlbar, 0, FB_3DS_SCREEN_HEIGHT,
				      win_w, FB_3DS_URLBAR_HEIGHT);
		fbtk_set_pos_and_size(gw->url,
				      FB_3DS_URLBAR_PADDING,
				      FB_3DS_URLBAR_PADDING,
				      -FB_3DS_URLBAR_PADDING,
				      -FB_3DS_URLBAR_PADDING);
	}

	fbtk_set_pos_and_size(gw->browser, 0, page_y, win_w, page_h);

	if (gw->toolbar != NULL) {
		fbtk_set_pos_and_size(gw->toolbar, 0,
				      win_h - furniture_width - bar_h,
				      win_w, bar_h);
	}

	fbtk_set_pos_and_size(gw->status, 0, win_h - furniture_width,
			      win_w, furniture_width);

	if (gw->top_title_label != NULL) {
		fb_3ds_position_top_title(gw, browser_window_get_title(gw->bw));
	}
}

static void
fb_3ds_set_urlbar_shown(struct gui_window *gw, bool show)
{
	if ((gw->urlbar == NULL) || (gw->urlbar_shown == show)) {
		return;
	}

	gw->urlbar_shown = show;
	fbtk_set_mapping(gw->urlbar, show);

	if (!show) {
		/* drops the text caret along with the focus */
		fbtk_set_focus(gw->browser);
	}

	fb_3ds_layout_screens(gw);
	fb_browser_request_redraw(gw);
}
#endif

/** Routine called when "stripped of focus" event occours for browser widget.
 *
 * @param widget The widget reciving "stripped of focus" event.
 * @param cbi The callback parameters.
 * @return The callback result.
 */
static int
fb_browser_window_strip_focus(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	fbtk_set_caret(widget, false, 0, 0, 0, NULL);

	return 0;
}

static void
fb_set_browser_pane_handlers(struct gui_window *gw, fbtk_widget_t *widget,
		bool primary)
{
	fbtk_set_handler(widget, FBTK_CBT_REDRAW, fb_browser_window_redraw, gw);
	if (primary) {
		fbtk_set_handler(widget, FBTK_CBT_DESTROY,
				fb_browser_window_destroy, gw);
	}
	fbtk_set_handler(widget, FBTK_CBT_INPUT, fb_browser_window_input, gw);
	fbtk_set_handler(widget, FBTK_CBT_CLICK, fb_browser_window_click, gw);
	fbtk_set_handler(widget, FBTK_CBT_STRIP_FOCUS,
			fb_browser_window_strip_focus, gw);
	fbtk_set_handler(widget, FBTK_CBT_POINTERMOVE,
			fb_browser_window_move, gw);
}

static void
create_browser_widget(struct gui_window *gw, int browser_y, int furniture_width)
{
	struct browser_widget_s *browser_widget;
	browser_widget = calloc(1, sizeof(struct browser_widget_s));

	gw->browser = fbtk_create_user(gw->window,
				       0,
				       browser_y,
				       -furniture_width,
				       -furniture_width,
				       browser_widget);

	fb_set_browser_pane_handlers(gw, gw->browser, true);
}

#ifdef __3DS__
static void
create_3ds_browser_panes(struct gui_window *gw)
{
	struct browser_widget_s *browser_widget;
	int win_h = fbtk_get_height(gw->window);

	browser_widget = calloc(1, sizeof(struct browser_widget_s));

	gw->browser_top = fbtk_create_user(gw->window,
					    0,
					    0,
					    0,
					    -(win_h - FB_3DS_SCREEN_HEIGHT),
					    browser_widget);
	gw->browser = fbtk_create_user(gw->window,
				       0,
				       FB_3DS_SCREEN_HEIGHT,
				       0,
				       0,
				       browser_widget);

	fb_set_browser_pane_handlers(gw, gw->browser_top, false);
	fb_set_browser_pane_handlers(gw, gw->browser, true);

	gw->top_title_label = fbtk_create_text(gw->window,
			0,
			FB_3DS_SCREEN_HEIGHT - FB_3DS_TITLE_LABEL_HEIGHT -
					FB_3DS_TITLE_BOTTOM_MARGIN,
			fbtk_get_width(gw->window),
			FB_3DS_TITLE_LABEL_HEIGHT,
			FB_3DS_PANE_BG,
			FB_3DS_TITLE_COLOUR,
			false);
	gw->top_title_shown = true;
	fbtk_set_mapping(gw->top_title_label, true);
}
#endif

static void
resize_browser_widget(struct gui_window *gw, int x, int y,
		int width, int height)
{
	fbtk_set_pos_and_size(gw->browser, x, y, width, height);
	browser_window_schedule_reformat(gw->bw);
}

static void
create_normal_browser_window(struct gui_window *gw, int furniture_width)
{
#ifndef __3DS__
	fbtk_widget_t *widget;
	fbtk_widget_t *toolbar;
#endif
	int statusbar_width = 0;
	int toolbar_height = nsoption_int(fb_toolbar_size);

	NSLOG(netsurf, INFO, "Normal window");

	gw->window = fbtk_create_window(fbtk, 0, 0, 0, 0, 0);

	statusbar_width = nsoption_int(toolbar_status_size) *
		fbtk_get_width(gw->window) / 10000;

#ifdef __3DS__
	create_3ds_action_bar(gw, toolbar_height,
			      nsoption_charp(fb_toolbar_layout));
	create_3ds_urlbar(gw);
	create_3ds_browser_panes(gw);
#else
	/* toolbar */
	toolbar = create_toolbar(gw, 
				 toolbar_height, 
				 2, 
				 FB_FRAME_COLOUR, 
				 nsoption_charp(fb_toolbar_layout));
	gw->toolbar = toolbar;

	/* set the actually created toolbar height */
	if (toolbar != NULL) {
		toolbar_height = fbtk_get_height(toolbar);
	} else {
		toolbar_height = 0;
	}
#endif

	/* status bar */
	gw->status = fbtk_create_text(gw->window,
				      0,
				      fbtk_get_height(gw->window) - furniture_width,
#ifdef __3DS__
				      fbtk_get_width(gw->window),
#else
				      statusbar_width,
#endif
				      furniture_width,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
	fbtk_set_handler(gw->status, FBTK_CBT_POINTERENTER, set_ptr_default_move, NULL);

	NSLOG(netsurf, INFO, "status bar %p at %d,%d", gw->status,
	      fbtk_get_absx(gw->status), fbtk_get_absy(gw->status));

#ifndef __3DS__
	/* create horizontal scrollbar */
	gw->hscroll = fbtk_create_hscroll(gw->window,
					  statusbar_width,
					  fbtk_get_height(gw->window) - furniture_width,
					  fbtk_get_width(gw->window) - statusbar_width - furniture_width,
					  furniture_width,
					  FB_SCROLL_COLOUR,
					  FB_FRAME_COLOUR,
					  fb_scroll_callback,
					  gw);

	/* fill bottom right area */

	if (nsoption_bool(fb_osk) == true) {
		widget = fbtk_create_text_button(gw->window,
						 fbtk_get_width(gw->window) - furniture_width,
						 fbtk_get_height(gw->window) - furniture_width,
						 furniture_width,
						 furniture_width,
						 FB_FRAME_COLOUR, FB_COLOUR_BLACK,
						 fb_osk_click,
						 NULL);
		widget = fbtk_create_button(gw->window,
				fbtk_get_width(gw->window) - furniture_width,
				fbtk_get_height(gw->window) - furniture_width,
				furniture_width,
				furniture_width,
				FB_FRAME_COLOUR,
				&osk_image,
				fb_osk_click,
				NULL);
	} else {
		widget = fbtk_create_fill(gw->window,
					  fbtk_get_width(gw->window) - furniture_width,
					  fbtk_get_height(gw->window) - furniture_width,
					  furniture_width,
					  furniture_width,
					  FB_FRAME_COLOUR);

		fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, set_ptr_default_move, NULL);
	}

	gw->bottom_right = widget;

	/* create vertical scrollbar */
	gw->vscroll = fbtk_create_vscroll(gw->window,
					  fbtk_get_width(gw->window) - furniture_width,
					  toolbar_height,
					  furniture_width,
					  fbtk_get_height(gw->window) - toolbar_height - furniture_width,
					  FB_SCROLL_COLOUR,
					  FB_FRAME_COLOUR,
					  fb_scroll_callback,
					  gw);
#else
	gw->hscroll = NULL;
	gw->vscroll = NULL;
	gw->bottom_right = NULL;
#endif

#ifdef __3DS__
	fb_3ds_layout_screens(gw);
#else
	/* browser widget */
	create_browser_widget(gw, toolbar_height, furniture_width);
#endif

	/* Give browser_window's user widget input focus */
	fbtk_set_focus(gw->browser);
}

static void
resize_normal_browser_window(struct gui_window *gw, int furniture_width)
{
	bool resized;
#ifdef __3DS__
	struct fb_3ds_action_item items[FB_3DS_ACTION_MAX];
	int count;
#else
	int width, height;
	int statusbar_width;
	int toolbar_height = (gw->toolbar != NULL) ?
			fbtk_get_height(gw->toolbar) : 0;
#endif

	/* Resize the main window widget */
	resized = fbtk_set_pos_and_size(gw->window, 0, 0, 0, 0);
	if (!resized)
		return;

#ifdef __3DS__
	fb_3ds_layout_screens(gw);

	if (gw->toolbar != NULL) {
		count = fb_3ds_collect_actions(gw,
				nsoption_charp(fb_toolbar_layout), items);
		fb_3ds_place_actions(gw, items, count, false);
	}

	browser_window_schedule_reformat(gw->bw);
#else
	width = fbtk_get_width(gw->window);
	height = fbtk_get_height(gw->window);
	statusbar_width = nsoption_int(toolbar_status_size) * width / 10000;

	resize_toolbar(gw, toolbar_height, 2,
			nsoption_charp(fb_toolbar_layout));
	fbtk_set_pos_and_size(gw->status,
			0, height - furniture_width,
			statusbar_width,
			furniture_width);
	fbtk_reposition_hscroll(gw->hscroll,
			statusbar_width, height - furniture_width,
			width - statusbar_width - furniture_width,
			furniture_width);
	fbtk_set_pos_and_size(gw->bottom_right,
			width - furniture_width, height - furniture_width,
			furniture_width, furniture_width);
	fbtk_reposition_vscroll(gw->vscroll,
			width - furniture_width,
			toolbar_height, furniture_width,
			height - toolbar_height - furniture_width);
	resize_browser_widget(gw,
			0, toolbar_height,
			width - furniture_width,
			height - furniture_width - toolbar_height);
#endif
}

static void gui_window_add_to_window_list(struct gui_window *gw)
{
	gw->next = NULL;
	gw->prev = NULL;

	if (window_list == NULL) {
		window_list = gw;
	} else {
		window_list->prev = gw;
		gw->next = window_list;
		window_list = gw;
	}
}

static void gui_window_remove_from_window_list(struct gui_window *gw)
{
	struct gui_window *list;

	for (list = window_list; list != NULL; list = list->next) {
		if (list != gw)
			continue;

		if (list == window_list) {
			window_list = list->next;
			if (window_list != NULL)
				window_list->prev = NULL;
		} else {
			list->prev->next = list->next;
			if (list->next != NULL) {
				list->next->prev = list->prev;
			}
		}
		break;
	}
}


static struct gui_window *
gui_window_create(struct browser_window *bw,
		struct gui_window *existing,
		gui_window_create_flags flags)
{
	struct gui_window *gw;

	gw = calloc(1, sizeof(struct gui_window));

	if (gw == NULL)
		return NULL;

	/* associate the gui window with the underlying browser window
	 */
	gw->bw = bw;

	create_normal_browser_window(gw, nsoption_int(fb_furniture_size));

	/* map and request redraw of gui window */
	fbtk_set_mapping(gw->window, true);

	/* Add it to the window list */
	gui_window_add_to_window_list(gw);

	return gw;
}

static void
gui_window_destroy(struct gui_window *gw)
{
	gui_window_remove_from_window_list(gw);

	fbtk_destroy_widget(gw->window);

	free(gw);
}


/**
 * Invalidates an area of a framebuffer browser window
 *
 * \param g The netsurf window being invalidated.
 * \param rect area to redraw or NULL for the entire window area
 * \return NSERROR_OK on success or appropriate error code
 */
static nserror
fb_window_invalidate_area(struct gui_window *g, const struct rect *rect)
{
#ifdef __3DS__
	struct browser_widget_s *bwidget = fb_get_bwidget(g);

	if (rect != NULL) {
		int scrolly = bwidget->scrolly;
		int top_doc_start = scrolly > FB_3DS_SCREEN_HEIGHT ?
				scrolly - FB_3DS_SCREEN_HEIGHT : 0;
		int bottom_h = fbtk_get_height(g->browser);

		if (scrolly > 0 && rect->y1 > top_doc_start &&
				rect->y0 < scrolly) {
			int wy0 = max(rect->y0, top_doc_start) - scrolly +
					FB_3DS_SCREEN_HEIGHT;
			int wy1 = min(rect->y1, scrolly) - scrolly +
					FB_3DS_SCREEN_HEIGHT;

			if (wy1 > wy0) {
				fb_queue_redraw(g->browser_top,
						rect->x0 - bwidget->scrollx,
						wy0,
						rect->x1 - bwidget->scrollx,
						wy1);
			}
		}

		if (rect->y1 > scrolly &&
				rect->y0 < scrolly + bottom_h) {
			int wy0 = max(rect->y0, scrolly) - scrolly;
			int wy1 = min(rect->y1, scrolly + bottom_h) - scrolly;

			if (wy1 > wy0) {
				fb_queue_redraw(g->browser,
						rect->x0 - bwidget->scrollx,
						wy0,
						rect->x1 - bwidget->scrollx,
						wy1);
			}
		}
	} else {
		fb_queue_redraw(g->browser_top,
				0, 0,
				fbtk_get_width(g->browser_top),
				fbtk_get_height(g->browser_top));
		fb_queue_redraw(g->browser,
				0, 0,
				fbtk_get_width(g->browser),
				fbtk_get_height(g->browser));
	}
#else
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	if (rect != NULL) {
		fb_queue_redraw(g->browser,
				rect->x0 - bwidget->scrollx,
				rect->y0 - bwidget->scrolly,
				rect->x1 - bwidget->scrollx,
				rect->y1 - bwidget->scrolly);
	} else {
		fb_queue_redraw(g->browser,
				0,
				0,
				fbtk_get_width(g->browser),
				fbtk_get_height(g->browser));
	}
#endif
	return NSERROR_OK;
}

static bool
gui_window_get_scroll(struct gui_window *g, int *sx, int *sy)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	*sx = bwidget->scrollx;
	*sy = bwidget->scrolly;

	return true;
}

/**
 * Set the scroll position of a framebuffer browser window.
 *
 * Scrolls the viewport to ensure the specified rectangle of the
 *   content is shown. The framebuffer implementation scrolls the contents so
 *   the specified point in the content is at the top of the viewport.
 *
 * \param gw gui_window to scroll
 * \param rect The rectangle to ensure is shown.
 * \return NSERROR_OK on success or apropriate error code.
 */
static nserror
gui_window_set_scroll(struct gui_window *gw, const struct rect *rect)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);

	assert(bwidget);

	widget_scroll_x(gw, rect->x0, true);
	widget_scroll_y(gw, rect->y0, true);

	return NSERROR_OK;
}


/**
 * Find the current dimensions of a framebuffer browser window content area.
 *
 * \param gw The gui window to measure content area of.
 * \param width receives width of window
 * \param height receives height of window
 * \return NSERROR_OK on sucess and width and height updated.
 */
static nserror
gui_window_get_dimensions(struct gui_window *gw, int *width, int *height)
{
	*width = fbtk_get_width(gw->browser);
	*height = fbtk_get_height(gw->browser);

	return NSERROR_OK;
}

static void
gui_window_update_extent(struct gui_window *gw)
{
	int w, h;
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);

	browser_window_get_extents(gw->bw, true, &w, &h);

#ifdef __3DS__
	if (bwidget != NULL && !bwidget->user_zoomed) {
		gui_window_fit_width(gw);
	}
#endif

	if (gw->hscroll != NULL) {
		fbtk_set_scroll_parameters(gw->hscroll, 0, w,
				fbtk_get_width(gw->browser), 100);
	}

	if (gw->vscroll != NULL) {
#ifdef __3DS__
		fbtk_set_scroll_parameters(gw->vscroll, 0, h,
				fb_browser_scroll_viewport_height(gw), 100);
#else
		fbtk_set_scroll_parameters(gw->vscroll, 0, h,
				fbtk_get_height(gw->browser), 100);
#endif
	}
}

static void
gui_window_set_status(struct gui_window *g, const char *text)
{
	fbtk_set_text(g->status, text);
}

#ifdef __3DS__
static void
gui_window_set_title(struct gui_window *g, const char *title)
{
	struct browser_widget_s *bwidget;

	if (g->top_title_label == NULL) {
		return;
	}

	if (title == NULL) {
		title = "";
	}

	fb_3ds_position_top_title(g, title);
	fbtk_set_text(g->top_title_label, title);

	bwidget = fb_get_bwidget(g);
	fb_3ds_update_top_title_visibility(g,
			bwidget != NULL ? bwidget->scrolly : 0);
}
#endif

static void
gui_window_set_pointer(struct gui_window *g, gui_pointer_shape shape)
{
	switch (shape) {
	case GUI_POINTER_POINT:
		framebuffer_set_cursor(&hand_image);
		break;

	case GUI_POINTER_CARET:
		framebuffer_set_cursor(&caret_image);
		break;

	case GUI_POINTER_MENU:
		framebuffer_set_cursor(&menu_image);
		break;

	case GUI_POINTER_PROGRESS:
		framebuffer_set_cursor(&progress_image);
		break;

	case GUI_POINTER_MOVE:
		framebuffer_set_cursor(&move_image);
		break;

	default:
		framebuffer_set_cursor(&pointer_image);
		break;
	}
}

static nserror
gui_window_set_url(struct gui_window *g, nsurl *url)
{
	fbtk_set_text(g->url, nsurl_access(url));
	return NSERROR_OK;
}

static void
throbber_advance(void *pw)
{
	struct gui_window *g = pw;
	struct fbtk_bitmap *image;

	switch (g->throbber_index) {
	case 0:
		image = &throbber1;
		g->throbber_index = 1;
		break;

	case 1:
		image = &throbber2;
		g->throbber_index = 2;
		break;

	case 2:
		image = &throbber3;
		g->throbber_index = 3;
		break;

	case 3:
		image = &throbber4;
		g->throbber_index = 4;
		break;

	case 4:
		image = &throbber5;
		g->throbber_index = 5;
		break;

	case 5:
		image = &throbber6;
		g->throbber_index = 6;
		break;

	case 6:
		image = &throbber7;
		g->throbber_index = 7;
		break;

	case 7:
		image = &throbber8;
		g->throbber_index = 0;
		break;

	default:
		return;
	}

	if (g->throbber_index >= 0) {
		fbtk_set_bitmap(g->throbber, image);
		framebuffer_schedule(100, throbber_advance, g);
	}
}

static void
gui_window_start_throbber(struct gui_window *g)
{
	g->throbber_index = 0;
	framebuffer_schedule(100, throbber_advance, g);
}

static void
gui_window_stop_throbber(struct gui_window *gw)
{
	gw->throbber_index = -1;
	fbtk_set_bitmap(gw->throbber, &throbber0);

	fb_update_back_forward(gw);

}

static void
gui_window_remove_caret_cb(fbtk_widget_t *widget)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	int c_x, c_y, c_h;

	if (fbtk_get_caret(widget, &c_x, &c_y, &c_h)) {
		/* browser window already had caret:
		 * redraw its area to remove it first */
		fb_queue_redraw(widget,
				c_x - bwidget->scrollx,
				c_y - bwidget->scrolly,
				c_x + 1 - bwidget->scrollx,
				c_y + c_h - bwidget->scrolly);
	}
}

static void
gui_window_place_caret(struct gui_window *g, int x, int y, int height,
		const struct rect *clip)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	/* set new pos */
	fbtk_set_caret(g->browser, true, x, y, height,
			gui_window_remove_caret_cb);

	/* redraw new caret pos */
	fb_queue_redraw(g->browser,
			x - bwidget->scrollx,
			y - bwidget->scrolly,
			x + 1 - bwidget->scrollx,
			y + height - bwidget->scrolly);
}

static void
gui_window_remove_caret(struct gui_window *g)
{
	int c_x, c_y, c_h;

	if (fbtk_get_caret(g->browser, &c_x, &c_y, &c_h)) {
		/* browser window owns the caret, so can remove it */
		fbtk_set_caret(g->browser, false, 0, 0, 0, NULL);
	}
}

/**
 * process miscellaneous window events
 *
 * \param gw The window receiving the event.
 * \param event The event code.
 * \return NSERROR_OK when processed ok
 */
static nserror
gui_window_event(struct gui_window *gw, enum gui_window_event event)
{
	switch (event) {
	case GW_EVENT_UPDATE_EXTENT:
		gui_window_update_extent(gw);
		break;

#ifdef __3DS__
	case GW_EVENT_NEW_CONTENT:
	{
		struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);

		if (bwidget != NULL) {
			bwidget->user_zoomed = false;
		}
		break;
	}
#endif

	case GW_EVENT_REMOVE_CARET:
		gui_window_remove_caret(gw);
		break;

	case GW_EVENT_START_THROBBER:
		gui_window_start_throbber(gw);
		break;

	case GW_EVENT_STOP_THROBBER:
		gui_window_stop_throbber(gw);
		break;

	default:
		break;
	}
	return NSERROR_OK;
}

static struct gui_window_table framebuffer_window_table = {
	.create = gui_window_create,
	.destroy = gui_window_destroy,
	.invalidate = fb_window_invalidate_area,
	.get_scroll = gui_window_get_scroll,
	.set_scroll = gui_window_set_scroll,
	.get_dimensions = gui_window_get_dimensions,
	.event = gui_window_event,

	.set_url = gui_window_set_url,
#ifdef __3DS__
	.set_title = gui_window_set_title,
#endif
	.set_status = gui_window_set_status,
	.set_pointer = gui_window_set_pointer,
	.place_caret = gui_window_place_caret,
};


static struct gui_misc_table framebuffer_misc_table = {
	.schedule = framebuffer_schedule,

	.quit = gui_quit,
};

/**
 * Entry point from OS.
 *
 * /param argc The number of arguments in the string vector.
 * /param argv The argument string vector.
 * /return The return code to the OS
 */
int
main(int argc, char** argv)
{
	struct browser_window *bw;
	char *options;
	char *messages;
	nsurl *url;
	nserror ret;
	nsfb_t *nsfb;
	struct netsurf_table framebuffer_table = {
		.misc = &framebuffer_misc_table,
		.window = &framebuffer_window_table,
		.clipboard = framebuffer_clipboard_table,
		.fetch = framebuffer_fetch_table,
		.utf8 = framebuffer_utf8_table,
		.bitmap = framebuffer_bitmap_table,
		.layout = framebuffer_layout_table,
	};

	ret = netsurf_register(&framebuffer_table);
	if (ret != NSERROR_OK) {
		die("NetSurf operation table failed registration");
	}

	Result rfsRes = romfsInit();

	respaths = fb_init_resource_path(NETSURF_FB_RESPATH":"NETSURF_FB_FONTPATH);


	/* initialise logging. Not fatal if it fails but not much we
	 * can do about it either.
	 */
	nslog_init(nslog_stream_configure, &argc, argv);
	if(rfsRes){
		NSLOG(netsurf,WARN,"ROMFS: %d",rfsRes);
	}

	/* user options setup */
	ret = nsoption_init(set_defaults, &nsoptions, &nsoptions_default);
	if (ret != NSERROR_OK) {
		die("Options failed to initialise");
	}
	options = filepath_find(respaths, "Choices");
	nsoption_read(options, nsoptions);
	free(options);
	nsoption_commandline(&argc, argv, nsoptions);

#ifdef __3DS__
	/* Top screen is ~96 DPI; needed for correct CSS font sizing. */
	browser_set_dpi(96);
#endif

	/* message init */
	messages = filepath_find(respaths, "Messages");
        ret = messages_add_from_file(messages);
	free(messages);
	if (ret != NSERROR_OK) {
		fprintf(stderr, "Message translations failed to load\n");
	}

	/* common initialisation */
	ret = netsurf_init(NULL);
	if (ret != NSERROR_OK) {
		die("NetSurf failed to initialise");
	}

	/* Override, since we have no support for non-core SELECT menu */
	nsoption_set_bool(core_select_menu, true);

	if (process_cmdline(argc,argv) != true)
		die("unable to process command line.\n");

	nsfb = framebuffer_initialise(fename, fewidth, feheight, febpp);
	if (nsfb == NULL)
		die("Unable to initialise framebuffer");

	framebuffer_set_cursor(&pointer_image);

	if (fb_font_init() == false)
		die("Unable to initialise the font system");

	fbtk = fbtk_init(nsfb);

	fbtk_enable_oskb(fbtk);

	urldb_load_cookies(nsoption_charp(cookie_file));

	/* create an initial browser window */

	NSLOG(netsurf, INFO, "calling browser_window_create");

	ret = nsurl_create(feurl, &url);
	if (ret == NSERROR_OK) {
		ret = browser_window_create(BW_CREATE_HISTORY,
					      url,
					      NULL,
					      NULL,
					      &bw);
		nsurl_unref(url);
	}
	if (ret != NSERROR_OK) {
		fb_warn_user("Errorcode:", messages_get_errorcode(ret));
	} else {
		framebuffer_run();

		browser_window_destroy(bw);
	}

	netsurf_exit();

	if (fb_font_finalise() == false)
		NSLOG(netsurf, INFO, "Font finalisation failed.");

	romfsExit();

	/* finalise options */
	nsoption_finalise(nsoptions, nsoptions_default);

	/* finalise logging */
	nslog_finalise();

	return 0;
}

void gui_resize(fbtk_widget_t *root, int width, int height)
{
	struct gui_window *gw;
	nsfb_t *nsfb = fbtk_get_nsfb(root);

	/* Enforce a minimum */
	if (width < 300)
		width = 300;
	if (height < 200)
		height = 200;

	if (framebuffer_resize(nsfb, width, height, febpp) == false) {
		return;
	}

	fbtk_set_pos_and_size(root, 0, 0, width, height);

	fewidth = width;
	feheight = height;

	for (gw = window_list; gw != NULL; gw = gw->next) {
		resize_normal_browser_window(gw,
				nsoption_int(fb_furniture_size));
	}

	fbtk_request_redraw(root);
}


/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
