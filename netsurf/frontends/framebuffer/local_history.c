/*
 * Copyright 2017 Vincent Sanders <vince@netsurf-browser.org>
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
 * Implementation of framebuffer local history manager.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>

#include "utils/log.h"
#include "utils/nsoption.h"
#include "netsurf/keypress.h"
#include "netsurf/plotters.h"
#include "netsurf/browser_window.h"
#include "desktop/local_history.h"

#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "fbtk/widget.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/corewindow.h"
#include "framebuffer/local_history.h"
#include "framebuffer/schedule.h"

extern fbtk_widget_t *fbtk;

struct fb_local_history_window {
	struct fb_corewindow core;

	struct local_history_session *session;
};

static struct fb_local_history_window *local_history_window = NULL;
static fbtk_widget_t *local_history_parent = NULL;
static struct gui_window *local_history_gw = NULL;


static void
fb_local_history_repaint_cb(void *pw)
{
	struct gui_window *gw = pw;

	if (gw == NULL) {
		return;
	}

	fb_gui_repaint_browser(gw);
	fb_gui_flush_display();
}


bool
fb_local_history_is_shown(void)
{
	return local_history_window != NULL &&
	       local_history_window->core.wnd != NULL &&
	       local_history_window->core.wnd->mapped;
}


static void
fb_local_history_refresh(struct fb_local_history_window *lhw)
{
	fbtk_set_zorder(lhw->core.wnd, INT_MIN);
	fbtk_request_redraw(lhw->core.wnd);
	fbtk_request_redraw(lhw->core.drawable);
	lhw->core.cb_table->invalidate(
			(struct core_window *)lhw,
			NULL);
	fbtk_redraw(fbtk);
	fb_gui_flush_display();
}


static nserror
fb_local_history_hide_internal(struct gui_window *gw)
{
	if (!fb_local_history_is_shown()) {
		return NSERROR_OK;
	}

	fbtk_set_mapping(local_history_window->core.wnd, false);
	fb_gui_repaint_browser(gw);
	fb_gui_flush_display();

	return NSERROR_OK;
}


/**
 * callback for mouse action on local history window
 */
static nserror
fb_local_history_mouse(struct fb_corewindow *fb_cw,
		    browser_mouse_state mouse_state,
		    int x, int y)
{
	struct fb_local_history_window *lhw;
	nserror err;
	struct gui_window *gw;

	lhw = (struct fb_local_history_window *)fb_cw;
	gw = local_history_gw;

	err = local_history_mouse_action(lhw->session, mouse_state,
			x + fb_cw->scrollx, y + fb_cw->scrolly);

	if (mouse_state != BROWSER_MOUSE_HOVER && gw != NULL) {
		fb_local_history_hide_internal(gw);
		if (err == NSERROR_OK) {
			browser_window_schedule_reformat(gw->bw);
			framebuffer_schedule(0, fb_local_history_repaint_cb, gw);
			framebuffer_schedule(100, fb_local_history_repaint_cb, gw);
		}
	}

	return NSERROR_OK;
}


/**
 * callback for keypress on local history window
 */
static nserror
fb_local_history_key(struct fb_corewindow *fb_cw, uint32_t nskey)
{
	struct fb_local_history_window *lhw;

	lhw = (struct fb_local_history_window *)fb_cw;

	if (local_history_keypress(lhw->session, nskey)) {
		return NSERROR_OK;
	}
	return NSERROR_NOT_IMPLEMENTED;
}


/**
 * callback on draw event for local history window
 */
static nserror
fb_local_history_draw(struct fb_corewindow *fb_cw, struct rect *r)
{
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = &fb_plotters
	};
	struct fb_local_history_window *lhw;
	struct rect clip;

	(void)r;

	lhw = (struct fb_local_history_window *)fb_cw;

	clip.x0 = 0;
	clip.y0 = 0;
	clip.x1 = fbtk_get_width(lhw->core.drawable);
	clip.y1 = fbtk_get_height(lhw->core.drawable);

	return local_history_redraw(lhw->session,
				    fbtk_get_absx(lhw->core.drawable) - fb_cw->scrollx,
				    fbtk_get_absy(lhw->core.drawable) - fb_cw->scrolly,
				    &clip, &ctx);
}


/**
 * Creates the window for the local history view.
 */
static nserror
fb_local_history_init(fbtk_widget_t *parent,
		      struct fb_local_history_window **win_out)
{
	struct fb_local_history_window *ncwin;
	nserror res;

	if ((*win_out) != NULL) {
		return NSERROR_OK;
	}

	ncwin = calloc(1, sizeof(*ncwin));
	if (ncwin == NULL) {
		return NSERROR_NOMEM;
	}

	ncwin->core.draw = fb_local_history_draw;
	ncwin->core.key = fb_local_history_key;
	ncwin->core.mouse = fb_local_history_mouse;

#ifdef __3DS__
	ncwin->core.no_scrollbars = true;
#endif

	res = fb_corewindow_init(parent, &ncwin->core);
	if (res != NSERROR_OK) {
		free(ncwin);
		return res;
	}

	/* Bind browser data only when the overlay is shown. */
	res = local_history_init(ncwin->core.cb_table,
				 (struct core_window *)ncwin,
				 NULL,
				 &ncwin->session);
	if (res != NSERROR_OK) {
		free(ncwin);
		return res;
	}

	fbtk_set_mapping(ncwin->core.wnd, false);

	*win_out = ncwin;

	return NSERROR_OK;
}


/* exported function documented in local_history.h */
nserror fb_local_history_present(struct gui_window *gw,
				 struct browser_window *bw)
{
	nserror res;
	fbtk_widget_t *parent = gw->window;
	int furniture_width = nsoption_int(fb_furniture_size);
	int toolbar_height = 0;
	int pos_x;
	int pos_y;
	int width;
	int height;

	if (fb_local_history_is_shown() && local_history_gw == gw) {
		return fb_local_history_hide_internal(gw);
	}

	if (gw->toolbar != NULL) {
		toolbar_height = fbtk_get_height(gw->toolbar);
	}

	if (local_history_window != NULL &&
	    local_history_parent != parent) {
		fb_local_history_destroy();
	}

	res = fb_local_history_init(parent, &local_history_window);
	if (res != NSERROR_OK) {
		return res;
	}

	local_history_parent = parent;
	local_history_gw = gw;

	pos_x = 0;
	width = fbtk_get_width(parent);

#ifdef __3DS__
	/* Bottom screen only: below toolbar, above status bar. */
	pos_y = FB_3DS_SCREEN_HEIGHT + toolbar_height;
	height = fbtk_get_height(parent) - pos_y - furniture_width;
#else
	pos_y = toolbar_height;
	height = fbtk_get_height(parent) - toolbar_height - furniture_width;
#endif

	if (width < 1) {
		width = 1;
	}
	if (height < 1) {
		height = 1;
	}

	local_history_window->core.scrollx = 0;
	local_history_window->core.scrolly = 0;

	fb_corewindow_resize(&local_history_window->core,
			     pos_x, pos_y, width, height);

	res = local_history_set(local_history_window->session, bw);
	if (res != NSERROR_OK) {
		return res;
	}

	fbtk_set_mapping(local_history_window->core.wnd, true);

	local_history_scroll_to_cursor(local_history_window->session);
	fb_local_history_refresh(local_history_window);

	return res;
}


/* exported function documented in local_history.h */
nserror fb_local_history_hide(void)
{
	return fb_local_history_hide_internal(local_history_gw);
}


/* exported function documented in local_history.h */
nserror fb_local_history_destroy(void)
{
	nserror res;

	if (local_history_window == NULL) {
		return NSERROR_OK;
	}

	res = local_history_fini(local_history_window->session);
	if (res == NSERROR_OK) {
		res = fb_corewindow_fini(&local_history_window->core);
		free(local_history_window);
		local_history_window = NULL;
		local_history_parent = NULL;
		local_history_gw = NULL;
	}

	return res;
}
