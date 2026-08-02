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

#include <libnsfb.h>
#include <libnsfb_event.h>

#include "utils/filepath.h"
#include "utils/nsoption.h"
#include "utils/useragent.h"
#include "utils/utils.h"

#include "framebuffer/app_menu.h"
#include "framebuffer/findfile.h"
#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"

extern fbtk_widget_t *fbtk;

#define FB_3DS_MENU_PADDING 8
#define FB_3DS_MENU_ROW_HEIGHT 32
#define FB_3DS_SETTINGS_TITLE_HEIGHT 28

enum fb_app_overlay_state {
	FB_APP_OVERLAY_NONE = 0,
	FB_APP_OVERLAY_MENU,
	FB_APP_OVERLAY_SETTINGS,
};

struct fb_app_overlay {
	enum fb_app_overlay_state state;
	struct gui_window *gw;

	fbtk_widget_t *menu_wnd;
	fbtk_widget_t *settings_top;
	fbtk_widget_t *settings_bottom;
	fbtk_widget_t *mobile_toggle_btn;
};

static struct fb_app_overlay app_overlay = {
	.state = FB_APP_OVERLAY_NONE,
};

static void fb_app_overlay_refresh(void);
static void fb_app_mobile_toggle_update(void);
static void fb_app_settings_save(void);

static void
fb_app_overlay_hide_internal(struct gui_window *gw)
{
	if (app_overlay.menu_wnd != NULL) {
		fbtk_set_mapping(app_overlay.menu_wnd, false);
	}
	if (app_overlay.settings_top != NULL) {
		fbtk_set_mapping(app_overlay.settings_top, false);
	}
	if (app_overlay.settings_bottom != NULL) {
		fbtk_set_mapping(app_overlay.settings_bottom, false);
	}

	app_overlay.state = FB_APP_OVERLAY_NONE;

	if (gw != NULL) {
		fb_gui_repaint_browser(gw);
	}
	fb_gui_flush_display();
}

static int
fb_app_settings_open_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	if (cbi->event->type != NSFB_EVENT_KEY_UP) {
		return 0;
	}

	return fb_app_settings_present(app_overlay.gw);
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
	if (app_overlay.menu_wnd != NULL) {
		fbtk_set_zorder(app_overlay.menu_wnd, INT_MIN);
		fbtk_request_redraw(app_overlay.menu_wnd);
	}
	if (app_overlay.settings_top != NULL) {
		fbtk_set_zorder(app_overlay.settings_top, INT_MIN);
		fbtk_request_redraw(app_overlay.settings_top);
	}
	if (app_overlay.settings_bottom != NULL) {
		fbtk_set_zorder(app_overlay.settings_bottom, INT_MIN);
		fbtk_request_redraw(app_overlay.settings_bottom);
	}
	fbtk_redraw(fbtk);
	fb_gui_flush_display();
}

static nserror
fb_app_menu_init(struct gui_window *gw)
{
	fbtk_widget_t *parent = gw->window;
	fbtk_widget_t *button;
	int win_w = fbtk_get_width(parent);
	int win_h = fbtk_get_height(parent);
	int furniture_width = nsoption_int(fb_furniture_size);
	int bar_h = (gw->toolbar != NULL) ? fbtk_get_height(gw->toolbar) : 0;
	int page_y = fbtk_get_absy(gw->browser);
	int page_h = max(1, win_h - furniture_width - bar_h - page_y);
	int button_w = win_w - FB_3DS_MENU_PADDING * 2;
	int button_y = FB_3DS_MENU_PADDING;

	if (app_overlay.menu_wnd != NULL) {
		return NSERROR_OK;
	}

	app_overlay.menu_wnd = fbtk_create_window(parent, 0, page_y, win_w, page_h,
						  FB_FRAME_COLOUR);
	if (app_overlay.menu_wnd == NULL) {
		return NSERROR_NOMEM;
	}

	button = fbtk_create_text_button(app_overlay.menu_wnd,
					 FB_3DS_MENU_PADDING,
					 button_y,
					 button_w,
					 FB_3DS_MENU_ROW_HEIGHT,
					 FB_COLOUR_WHITE,
					 FB_COLOUR_BLACK,
					 fb_app_settings_open_click,
					 NULL);
	fbtk_set_text(button, "Settings");

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

	app_overlay.mobile_toggle_btn = NULL;
	app_overlay.gw = NULL;
	app_overlay.state = FB_APP_OVERLAY_NONE;

	return NSERROR_OK;
}

#endif /* __3DS__ */
