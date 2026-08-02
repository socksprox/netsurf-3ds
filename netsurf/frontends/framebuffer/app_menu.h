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

#ifndef NETSURF_FB_APP_MENU_H
#define NETSURF_FB_APP_MENU_H

#ifdef __3DS__

#include <stdbool.h>

#include "utils/errors.h"

struct gui_window;

/**
 * Toggle the application menu overlay.
 */
nserror fb_app_menu_present(struct gui_window *gw);

/**
 * Open the settings screen.
 */
nserror fb_app_settings_present(struct gui_window *gw);

/**
 * Hide any open menu or settings overlay.
 */
nserror fb_app_menu_hide(void);

/**
 * Release menu and settings resources.
 */
nserror fb_app_menu_destroy(void);

/**
 * Whether the menu or settings overlay is visible.
 */
bool fb_app_menu_is_shown(void);

#endif /* __3DS__ */

#endif /* NETSURF_FB_APP_MENU_H */
