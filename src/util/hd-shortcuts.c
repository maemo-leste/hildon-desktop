#include "hd-shortcuts.h"

#include <glib.h>
#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <time.h>
#include <sys/time.h>

#include <matchbox/core/mb-wm.h>
#include <matchbox/mb-wm-config.h>
#include <matchbox/core/mb-wm-object.h>
#include <matchbox/comp-mgr/mb-wm-comp-mgr.h>

#include "hildon-desktop.h"
#include "hd-wm.h"
#include "hd-theme.h"
#include "hd-util.h"
#include "hd-dbus.h"
#include "hd-volume-profile.h"
#include "../launcher/hd-app-mgr.h"
#include "../home/hd-render-manager.h"
#include "hd-transition.h"
#include "hd-orientation-lock.h"
#include "hd-home.h"

#define SHORTCUTS_INI "/usr/share/hildon-desktop/shortcuts.ini"

#define GCONF_SCREENSHOT_PATH "/apps/osso/hildon-desktop/screenshot_path"

MBWindowManager *hd_mb_wm = NULL;

enum {
	KEY_ACTION_TOGGLE_SWITCHER = 1,
	KEY_ACTION_LEFT_BUTTON,
	KEY_ACTION_RIGHT_BUTTON,
	KEY_ACTION_CENTER_BUTTON,
	KEY_ACTION_VKB_BUTTON,
	KEY_ACTION_TOGGLE_NON_COMP_MODE,
	KEY_ACTION_TAKE_SCREENSHOT,
	KEY_ACTION_XTERMINAL,
	KEY_ACTION_TOGGLE_PORTRAITABLE,
	KEY_ACTION_SEND_DBUS,
};

static void take_screenshot(void)
{
	char *path, *filename;
	char *mydocsdir;
	static gchar datestamp[255];
	static time_t secs = 0;
	struct tm *tm = NULL;
	GdkDrawable *window;
	int width, height;
	GdkPixbuf *image;
	GError *error = NULL;
	gboolean ret;
	GConfClient *client;

	/* limit the rate of screenshots to avoid jamming HD when the key
	 * is pressed all the time */
	if (time(NULL) - secs < 5)
		return;

	client = gconf_client_get_default();
	mydocsdir = g_strdup(getenv("MYDOCSDIR"));

	path = gconf_client_get_string(client, GCONF_SCREENSHOT_PATH, NULL);

	if (!path || !*path) {
		if (!mydocsdir) {
			g_warning("Screenshot failed, environment variable MYDOCSDIR missing"
				  "or gconf option \'%s\' not set", GCONF_SCREENSHOT_PATH);
			g_free(path);
			return;
		} else {
			path = g_strdup_printf("%s/.images/Screenshots", mydocsdir);
		}
	}

	g_free(mydocsdir);
	g_object_unref(client);

	g_mkdir_with_parents(path, 0770);

	secs = time(NULL);
	tm = localtime(&secs);
	strftime(datestamp, 255, "%Y%m%d-%H%M%S", tm);

	filename = g_strdup_printf("%s/Screenshot-%s.png", path, datestamp);
	g_free(path);

	window = gdk_get_default_root_window();
	gdk_drawable_get_size(window, &width, &height);
	image = gdk_pixbuf_get_from_drawable(NULL,
					     window, gdk_drawable_get_colormap(window), 0, 0, 0, 0, width, height);
	ret = gdk_pixbuf_save(image, filename, "png", &error, NULL);
	g_object_unref(image);

	if (ret) {
		g_debug("Screenshot '%s' saved.", filename);
	} else if (error) {
		g_warning("%s: Image saving failed: %s", __func__, error->message);
		g_error_free(error);
	}
	g_free(filename);
}

/* Toggle the portrait-capable flag on and off on the topmost window */
static void toggle_portraitable(MBWindowManager * wm)
{
	MBWindowManagerClient *c;
	for (c = wm->stack_top; c; c = c->stacked_below) {
		MBWMClientType c_type = MB_WM_CLIENT_CLIENT_TYPE(c);
		if (c_type == MBWMClientTypeApp || c_type == MBWMClientTypeDesktop) {
			/* actually set the portrait property to the opposite now */
			gboolean new_supports = !hd_comp_mgr_client_supports_portrait(c);
			guint value = new_supports ? 1 : 0;
			mb_wm_util_async_trap_x_errors(wm->xdpy);
			XChangeProperty(wm->xdpy, c->window->xwindow,
					wm->atoms[MBWM_ATOM_HILDON_PORTRAIT_MODE_SUPPORT],
					XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);
			mb_wm_util_async_untrap_x_errors();
		}
	}
}

static void key_binding_func(MBWindowManager * wm, MBWMKeyBinding * binding, void *userdata)
{
	int action;

	action = GPOINTER_TO_INT(userdata);

	switch (action) {
	case KEY_ACTION_TOGGLE_SWITCHER:
		{
			int state = hd_render_manager_get_state();
			int portrait = STATE_IS_PORTRAIT(state);
			/* don't go to the switcher if we are showing a system-modal */
			if (!STATE_IS_TASK_NAV(state) && !hd_wm_has_modal_blockers(hd_mb_wm))
				hd_render_manager_set_state(portrait ? HDRM_STATE_TASK_NAV_PORTRAIT :
							    HDRM_STATE_TASK_NAV);
		}
		break;
	case KEY_ACTION_LEFT_BUTTON:
		if (!hd_wm_has_modal_blockers(hd_mb_wm)) {
			if (STATE_IS_TASK_NAV(hd_render_manager_get_state())) {
				if (STATE_IS_PORTRAIT(hd_render_manager_get_state()))
					hd_render_manager_set_state(HDRM_STATE_LAUNCHER_PORTRAIT);
				else
					hd_render_manager_set_state(HDRM_STATE_LAUNCHER);
			} else if (STATE_IS_LAUNCHER(hd_render_manager_get_state())) {
				if (STATE_IS_PORTRAIT(hd_render_manager_get_state()))
					hd_render_manager_set_state(HDRM_STATE_TASK_NAV_PORTRAIT);
				else
					hd_render_manager_set_state(HDRM_STATE_TASK_NAV);
			} else if (hd_task_navigator_is_empty()) {
				if (STATE_IS_PORTRAIT(hd_render_manager_get_state()))
					hd_render_manager_set_state(HDRM_STATE_LAUNCHER_PORTRAIT);
				else
					hd_render_manager_set_state(HDRM_STATE_LAUNCHER);
			} else {
				if (STATE_IS_PORTRAIT(hd_render_manager_get_state()))
					hd_render_manager_set_state(HDRM_STATE_TASK_NAV_PORTRAIT);
				else
					hd_render_manager_set_state(HDRM_STATE_TASK_NAV);
			}
		}
		break;
	case KEY_ACTION_CENTER_BUTTON:
		if (STATE_IS_APP(hd_render_manager_get_state())) {
			extern MBWindowManager *hd_mb_wm;
			HdCompMgrClient *client;
			HdCompMgr *cmgr = HD_COMP_MGR(hd_mb_wm->comp_mgr);
			client = hd_comp_mgr_get_current_client(cmgr);
			if (client) {
				MBWindowManagerClient *c = MB_WM_COMP_MGR_CLIENT(client)->wm_client;
				mb_wm_client_deliver_message(c, hd_mb_wm->atoms[MBWM_ATOM_MB_GRAB_TRANSFER],
							     CurrentTime, c->window->xwindow, wm->root_win->xwindow, 0,
							     0);
				XSync(wm->xdpy, False);
				XUngrabPointer(wm->xdpy, CurrentTime);
			}
		}
		break;
	case KEY_ACTION_RIGHT_BUTTON:
		{
			if (STATE_IS_APP(hd_render_manager_get_state())) {
				extern MBWindowManager *hd_mb_wm;
				HdCompMgrClient *client;
				HdCompMgr *cmgr = HD_COMP_MGR(hd_mb_wm->comp_mgr);
				client = hd_comp_mgr_get_current_client(cmgr);
				if (client)
					hd_comp_mgr_close_client(cmgr, &client->parent);
			}
			if (STATE_IS_TASK_NAV(hd_render_manager_get_state())
			    || STATE_IS_LAUNCHER(hd_render_manager_get_state())) {
				if (STATE_IS_PORTRAIT(hd_render_manager_get_state()))
					hd_render_manager_set_state(HDRM_STATE_HOME_PORTRAIT);
				else
					hd_render_manager_set_state(HDRM_STATE_HOME);
			}
		}
		break;
	case KEY_ACTION_VKB_BUTTON:
		hd_dbus_open_vkb();
		break;
	case KEY_ACTION_TOGGLE_NON_COMP_MODE:
		if (hd_render_manager_get_state() == HDRM_STATE_NON_COMPOSITED)
			hd_render_manager_set_state(HDRM_STATE_APP);
		else if (hd_render_manager_get_state() == HDRM_STATE_NON_COMP_PORT)
			hd_render_manager_set_state(HDRM_STATE_APP_PORTRAIT);
		else if (hd_render_manager_get_state() == HDRM_STATE_APP) {
			hd_render_manager_set_state(HDRM_STATE_NON_COMPOSITED);
			/* render manager does not unredirect non-fullscreen apps,
			 * so do it here */
			hd_comp_mgr_unredirect_topmost_client(hd_mb_wm, TRUE);
		} else if (hd_render_manager_get_state() == HDRM_STATE_APP_PORTRAIT) {
			hd_render_manager_set_state(HDRM_STATE_NON_COMP_PORT);
			hd_comp_mgr_unredirect_topmost_client(hd_mb_wm, TRUE);
		}
		break;
	case KEY_ACTION_TAKE_SCREENSHOT:
		take_screenshot();
		break;
	case KEY_ACTION_XTERMINAL:
		{
			GPid pid;
			if (hd_app_mgr_execute("/usr/bin/osso-xterm", &pid, TRUE))
				g_spawn_close_pid(pid);
			break;
		}

	case KEY_ACTION_TOGGLE_PORTRAITABLE:
		toggle_portraitable(wm);
		break;
	}
}

static void hd_shortcuts_add(MBWindowManager * wm, GKeyFile * file, gchar * key, unsigned int action)
{
	gsize len;
	gsize i;
	gchar **keystrlist = g_key_file_get_string_list(file, "Shortcuts", key, &len, NULL);
	
	if(!keystrlist)
		return;

	for (i = 0; i < len; ++i)
		mb_wm_keys_binding_add_with_spec(wm, keystrlist[i], key_binding_func, NULL, GUINT_TO_POINTER(action));

	g_strfreev(keystrlist);
}

void hd_shortcuts_setup(MBWindowManager * wm)
{
	GKeyFile *file = g_key_file_new();

	if (g_key_file_load_from_file(file, SHORTCUTS_INI, 0, NULL)) {
		hd_shortcuts_add(wm, file, "LeftButton", KEY_ACTION_LEFT_BUTTON);
		hd_shortcuts_add(wm, file, "MiddleButton", KEY_ACTION_CENTER_BUTTON);
		hd_shortcuts_add(wm, file, "RightButton", KEY_ACTION_RIGHT_BUTTON);
		hd_shortcuts_add(wm, file, "RaiseVkb", KEY_ACTION_VKB_BUTTON);
		hd_shortcuts_add(wm, file, "Tasknav", KEY_ACTION_TOGGLE_SWITCHER);
		hd_shortcuts_add(wm, file, "Xterm", KEY_ACTION_XTERMINAL);
		hd_shortcuts_add(wm, file, "Unredirect", KEY_ACTION_TOGGLE_NON_COMP_MODE);
		hd_shortcuts_add(wm, file, "Screenshot", KEY_ACTION_TAKE_SCREENSHOT);
		hd_shortcuts_add(wm, file, "Portraitable", KEY_ACTION_TOGGLE_PORTRAITABLE);
	}
	else {
		mb_wm_keys_binding_add_with_spec(wm, "<shift><ctrl>x", key_binding_func, NULL, GUINT_TO_POINTER(KEY_ACTION_XTERMINAL));
		mb_wm_keys_binding_add_with_spec(wm, "<shift><ctrl>n", key_binding_func, NULL, GUINT_TO_POINTER(KEY_ACTION_TOGGLE_NON_COMP_MODE));
		mb_wm_keys_binding_add_with_spec(wm, "<shift><ctrl>r", key_binding_func, NULL, GUINT_TO_POINTER(KEY_ACTION_TOGGLE_PORTRAITABLE));
		mb_wm_keys_binding_add_with_spec(wm, "<shift><ctrl>p", key_binding_func, NULL, GUINT_TO_POINTER(KEY_ACTION_TAKE_SCREENSHOT));
		mb_wm_keys_binding_add_with_spec(wm, "<ctrl>BackSpace", key_binding_func, NULL, GUINT_TO_POINTER(KEY_ACTION_TOGGLE_SWITCHER));
	}

	g_key_file_unref(file);
}
