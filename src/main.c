/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2008-2009 Nokia Corporation.
 *
 * Author:  Johan Bilien <johan.bilien@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <time.h>
#include <signal.h>
#include <sys/time.h>

#include <matchbox/core/mb-wm.h>
#include <matchbox/mb-wm-config.h>
#include <matchbox/core/mb-wm-object.h>
#include <matchbox/comp-mgr/mb-wm-comp-mgr.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <hildon/hildon-main.h>
#include <clutter/clutter-main.h>
#include <clutter/clutter-stage.h>
#include <clutter/x11/clutter-x11.h>
#include <clutter/clutter-container.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>

#include "hildon-desktop.h"
#include "hd-wm.h"
#include "hd-theme.h"
#include "hd-util.h"
#include "hd-dbus.h"
#include "hd-volume-profile.h"
#include "launcher/hd-app-mgr.h"
#include "home/hd-render-manager.h"
#include "hd-transition.h"
#include "hd-orientation-lock.h"
#include "hd-home.h"
#include "hd-shortcuts.h"
#include "hd-xinput.h"

#ifndef DISABLE_A11Y
#include "hildon-desktop-a11y.h"
#endif

#ifdef MBWM_DEB_VERSION
asm(".section .rodata");
asm(".string \"built with libmatchbox2 "MBWM_DEB_VERSION"\"");
asm(".previous");
#endif

gboolean hd_debug_mode_set = FALSE;
static int hd_clutter_mutex_enabled = FALSE;
static int hd_clutter_mutex_do_unlock_after_disabling = FALSE;
static GMutex hd_clutter_mutex;

void hd_mutex_enable (int setting)
{
  /*g_printerr ("%s, setting %d\n", __func__, setting);*/
  if (hd_clutter_mutex_enabled && !setting)
    /* deactivating mutex, make sure the mutex is unlocked if
     * hd_mutex_unlock is called later */
    hd_clutter_mutex_do_unlock_after_disabling = TRUE;

  hd_clutter_mutex_enabled = setting;
}

static void
hd_mutex_lock (void)
{
  /*g_printerr ("%s, enabled %d\n", __func__, hd_clutter_mutex_enabled);*/
  if (hd_clutter_mutex_enabled)
    g_mutex_lock (&hd_clutter_mutex);
}

static void
hd_mutex_unlock (void)
{
  /*g_printerr ("%s, enabled %d\n", __func__, hd_clutter_mutex_enabled);*/
  if (hd_clutter_mutex_enabled)
    g_mutex_unlock (&hd_clutter_mutex);
  else if (hd_clutter_mutex_do_unlock_after_disabling)
    {
      g_mutex_unlock (&hd_clutter_mutex);
      hd_clutter_mutex_do_unlock_after_disabling = FALSE;
    }
}

static void
hd_mutex_init (void)
{
  /* g_printerr ("%s%d\n", __func__); */
  clutter_threads_set_lock_functions (hd_mutex_lock, hd_mutex_unlock);
}

static unsigned int
theme_type_func (const char *theme_name, void *data)
{
  if (theme_name && !strcmp (theme_name, "hildon"))
    return HD_TYPE_THEME;

  return MB_WM_TYPE_THEME;
}

static unsigned int
theme_client_type_func (const char *type_name, void *user_data)
{
  if (!type_name)
    return 0;

  if (!strcmp (type_name, "application-menu"))
    return HdWmClientTypeAppMenu;
  else if (!strcmp (type_name, "home-applet"))
    return HdWmClientTypeHomeApplet;
  else if (!strcmp (type_name, "status-menu"))
      return HdWmClientTypeStatusMenu;

  return 0;
}

static unsigned int
theme_button_type_func (const char *type_name,
			void       *user_data)
{
  if (type_name && !strcmp (type_name, "back"))
    return HdHomeThemeButtonBack;

  return 0;
}

/* Debugging aids */
static gboolean
dump_debug_info_when_idle (gpointer unused)
{
  extern void hd_comp_mgr_dump_debug_info (const gchar *tag);
  hd_comp_mgr_dump_debug_info ("SIGUSR1");
  return FALSE;
}

static void
dump_debug_info_sighand (int unused)
{
  if (hd_debug_mode_set)
    hd_debug_mode_set = FALSE;
  else
    hd_debug_mode_set = TRUE;

  g_idle_add (dump_debug_info_when_idle, GINT_TO_POINTER (0));
  signal(SIGUSR1, dump_debug_info_sighand);
}

/* Returns the pid of the currently running hildon-desktop process or -1. */
static pid_t
hd_already_running (Display *dpy)
{
  Window root;
  char *wmname;
  pid_t *wmpid, pid;
  Window *wmwin, *wmwin2;
  Atom xpcheck, xpname, xppid;

  pid = -1;
  root = DefaultRootWindow(dpy);

  /* Read _NET_WM_PID of the _NET_SUPPORTING_WM_CHECK window. */
  xpcheck = XInternAtom (dpy, "_NET_SUPPORTING_WM_CHECK", False);
  wmwin = hd_util_get_win_prop_data_and_validate (dpy, root, xpcheck,
                                                  XA_WINDOW, 32, 1, NULL);
  if (!wmwin)
    goto out0;

  wmwin2 = hd_util_get_win_prop_data_and_validate (dpy, *wmwin, xpcheck,
                                                   XA_WINDOW, 32, 1, NULL);
  if (!wmwin2)
    {
      g_warning ("dangling _NET_SUPPORTING_WM_CHECK property on the root window");
      goto out1;
    }
  else if (*wmwin != *wmwin2)
    {
      g_warning ("inconsistent _NET_SUPPORTING_WM_CHECK values");
      goto out2;
    }

  xpname = XInternAtom (dpy, "_NET_WM_NAME", False);
  wmname = hd_util_get_win_prop_data_and_validate (dpy, *wmwin, xpname,
                                 XInternAtom (dpy, "UTF8_STRING", False),
                                 8, 0, NULL);
  if (!wmname)
    {
      g_warning ("_NET_SUPPORTING_WM_CHECK window has no _NET_WM_NAME");
      goto out2;
    }
  if (strcmp(wmname, PACKAGE))
    /* Not us. */
    goto out3;

  xppid = XInternAtom (dpy, "_NET_WM_PID", False);
  wmpid = hd_util_get_win_prop_data_and_validate (dpy, *wmwin, xppid,
                                                  XA_CARDINAL, 32, 1, NULL);
  if (!wmpid)
    {
      g_critical ("our previous instance didn't set _NET_WM_PID");
      goto out3;
    }

  pid = *wmpid;
  XFree (wmpid);
out3:
  XFree (wmname);
out2:
  XFree (wmwin2);
out1:
  XFree (wmwin);
out0:
  return pid;
}

static gboolean
get_program_file (const gchar *link, gchar *path, size_t spath)
{
  gint n;
  gchar *p;

  if ((n = readlink (link, path, spath-1)) < 0)
    {
      g_warning ("%s: %m", link);
      return FALSE;
    }

  path[n] = '\0';
  if ((p = strstr(path, " (deleted)")) != NULL)
    *p = '\0';

  return TRUE;
}

static gboolean
same_file (const gchar *link1, const gchar *link2)
{
  char path1[128], path2[128];

  if (!get_program_file (link1, path1, sizeof (path1)))
    return FALSE;
  if (!get_program_file (link2, path2, sizeof (path2)))
    return FALSE;

  return !strcmp(path1, path2);
}

static void
relaunch (int unused)
{
  char me[128];
  MBWMRootWindow *root;

  g_warning ("Relaunching myself...");
  if (!get_program_file ("/proc/self/exe", me, sizeof (me)))
    return;

  root = mb_wm_root_window_get (NULL);
  g_return_if_fail (root && root->wm);
  execv (me, root->wm->argv);
  g_warning ("%s: %m", me);
}

static void
try_to_relaunch (Display *dpy)
{
  pid_t mate;
  gchar *matepath;

  if ((mate = hd_already_running (dpy)) < 0)
    return;

  /* If our executable has overritten the other's ask it
   * to relaunch itself rather than killing it.  It has
   * the advantage of preserving file descriptor redirections. */
  matepath = g_strdup_printf ("/proc/%d/exe", mate);
  if (same_file (matepath, "/proc/self/exe"))
    {
      g_warning ("%d: reborn", mate);
      kill (mate, SIGHUP);
      exit (0);
    }
  else
    {
      g_warning("killing fellow hd %d", mate);
      kill (mate, SIGTERM);
      usleep (500000);
    }
  g_free (matepath);
}

static gboolean
get_debug_info (Display *dpy)
{
  pid_t mate;

  if ((mate = hd_already_running (dpy)) < 0)
    {
      g_warning ("Mate ain't not found");
      return FALSE;
    }
  else if (kill (mate, SIGUSR1) < 0)
    {
      g_warning ("kill(%d): %m", mate);
      return FALSE;
    }
  else
    return TRUE;
}

static void
terminating (int unused)
{
  gtk_main_quit ();
}

typedef enum
{
  OSSO_FPU_IEEE,    /* Usual processor mode, slow and accurate */
  OSSO_FPU_FAST     /* Fast but a bit non-accurate mode        */
} OSSO_FPU_MODE;

static void hd_fpu_set_mode(OSSO_FPU_MODE mode)
{
#ifdef __arm__
  if (OSSO_FPU_FAST == mode)
  {
    int tmp;
    __asm__ volatile(
        "fmrx       %[tmp], fpscr\n"
        "orr        %[tmp], %[tmp], #(1 << 24)\n" /* flush-to-zero */
        "orr        %[tmp], %[tmp], #(1 << 25)\n" /* default NaN */
        "bic        %[tmp], %[tmp], #((1 << 15) | (1 << 12) | (1 << 11) | (1 << 10) | (1 << 9) | (1 << 8))\n" /* clear exception bits */
        "fmxr       fpscr, %[tmp]\n"
        : [tmp] "=r" (tmp)
      );
  }
  else
  {
    int tmp;
    __asm__ volatile(
        "fmrx       %[tmp], fpscr\n"
        "bic        %[tmp], %[tmp], #(1 << 24)\n" /* flush-to-zero */
        "bic        %[tmp], %[tmp], #(1 << 25)\n" /* default NaN */
        "fmxr       fpscr, %[tmp]\n"
        : [tmp] "=r" (tmp)
      );
  }
#endif /* if __arm__ */
}

int
main (int argc, char **argv)
{
  Display * dpy = NULL;
  MBWindowManager *wm;
  HdAppMgr *app_mgr;

  signal (SIGUSR1, dump_debug_info_sighand);
  signal (SIGHUP,  relaunch);
  signal (SIGTERM, terminating);

  /* fast float calculations */
  hd_fpu_set_mode (OSSO_FPU_FAST);

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, "/usr/share/locale");
  textdomain (GETTEXT_PACKAGE);

  mb_wm_object_init();

  mb_wm_theme_set_custom_theme_type_func (theme_type_func, NULL);
  mb_wm_theme_set_custom_theme_alloc_func (hd_theme_alloc_func);
  mb_wm_theme_set_custom_client_type_func (theme_client_type_func, NULL);
  mb_wm_theme_set_custom_button_type_func (theme_button_type_func, NULL);

  hildon_gtk_init (&argc, &argv);
  /* Initialise the async error handler. Do it after gtk is inited, or gtk
   * will grab the handler for itself */
  mb_wm_util_async_x_error_init();

  hd_mutex_init ();

  /* NB: We _dont_ pass the X display from gtk into clutter because
   * it makes life far too complicated to have gtkish things like
   * creating dialogs going on using the same X display as the window
   * manager because it breaks all kinds of assumptions about
   * the relationship with client windows and the window manager.
   *
   * For example it breaks the SubStructureRedirect mechanism, and
   * assumptions about the number of benign UnmapNotifies the window
   * manager will see when reparenting "client" windows. */
  clutter_init (&argc, &argv);
  /* Disable mipmapping of text, as it is seldom scaled down and this
   * saves us memory/bandwidth/update speed */
  clutter_set_use_mipmapped_text(FALSE);
  /* Use software-based selection, which is much faster on SGX than rendering
   * with 'GL and reading back */
  clutter_set_software_selection(TRUE);

#ifndef DISABLE_A11Y
  hildon_desktop_a11y_init ();
#endif

  dpy = clutter_x11_get_default_display ();

  gdk_pixbuf_xlib_init (dpy, clutter_x11_get_default_screen ());

  /* Just before mb discovers there's a WM already and aborts
   * see if it's hildon-desktop and take it over. */
  if (argv[1] && !strcmp (argv[1], "-r"))
    try_to_relaunch (dpy);
  else if (argv[1] && !strcmp (argv[1], "-d"))
    return get_debug_info (dpy) != TRUE;

  wm = MB_WINDOW_MANAGER (mb_wm_object_new (HD_TYPE_WM,
					    MBWMObjectPropArgc, argc,
					    MBWMObjectPropArgv, argv,
					    MBWMObjectPropDpy,  dpy,
					    NULL));
  if (wm == NULL)
    mb_wm_util_fatal_error("OOM?");

  mb_wm_rename_window (wm, wm->root_win->hidden_window, PACKAGE);
  hd_util_display_portraitness_init(wm);
  mb_wm_init (wm);
  g_assert (mb_wm_comp_mgr_enabled (wm->comp_mgr));
  
  hd_shortcuts_setup(wm);

  hd_init_xinput (dpy);
  hd_enumerate_input_devices (dpy);
  hd_rotate_input_devices (dpy);

  clutter_x11_add_filter (hd_clutter_x11_event_filter, wm);

  app_mgr = hd_app_mgr_get ();

  hd_volume_profile_init ();

  /* Check if orientation is locked to portrait or the device is in vertical position. */
  if (hd_orientation_lock_is_locked_to_portrait () ||
      (!hd_orientation_lock_is_enabled () && hd_home_is_portrait_capable ()))
    {
      hd_render_manager_set_state (HDRM_STATE_HOME_PORTRAIT);
      if (hd_util_change_screen_orientation (wm, TRUE))
        hd_util_root_window_configured (wm);
    }
  else
    {
      /* Move to landscape for safety. */
      if (hd_util_change_screen_orientation (wm, FALSE))
        hd_util_root_window_configured (wm);
    }


  /* NB: we call gtk_main as opposed to clutter_main or mb_wm_main_loop
   * because it does the most extra magic, such as supporting quit functions
   * that the others don't. Except for adding the clutter_x11_add_filter
   * (manually done above) it appears be a super set of the other two
   * so everything *should* be covered this way. */
  gtk_main ();

  hd_close_input_devices (dpy);

  mb_wm_object_unref (MB_WM_OBJECT (wm));

  hd_app_mgr_stop ();
  g_object_unref (app_mgr);
  signal (SIGTERM, SIG_DFL);

#if MBWM_WANT_DEBUG
  mb_wm_object_dump ();
#endif

  return 0;
}
