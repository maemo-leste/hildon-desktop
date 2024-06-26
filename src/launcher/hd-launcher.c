/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Author:  Marc Ordinas i Llopis <marc.ordinasillopis@collabora.co.uk>
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
#include "config.h"
#endif

#include <math.h>
#include <unistd.h>

#include "hd-launcher.h"
#include "hd-launcher-app.h"

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include <gconf/gconf-client.h>

#include <clutter/clutter.h>
#include <tidy/tidy-finger-scroll.h>

#include "hildon-desktop.h"
#include "hd-launcher-grid.h"
#include "hd-launcher-page.h"
#include "hd-launcher-editor.h"
#include "hd-gtk-utils.h"
#include "hd-render-manager.h"
#include "hd-app-mgr.h"
#include "hd-gtk-style.h"
#include "hd-theme.h"
#include "hd-clutter-cache.h"
#include "hd-title-bar.h"
#include "hd-transition.h"
#include "hd-util.h"
#include "tidy/tidy-sub-texture.h"

#include <hildon/hildon-banner.h>

#define I_(str) (g_intern_static_string ((str)))

/* macros not defined in current hildon's glib, use the glib ones if possible
 */
#ifndef GBOOLEAN_TO_POINTER
#define GBOOLEAN_TO_POINTER(i) (GINT_TO_POINTER ((i) ? 2 : 1))
#endif
#ifndef GPOINTER_TO_BOOLEAN
#define GPOINTER_TO_BOOLEAN(i) ((gboolean) ((GPOINTER_TO_INT(i) == 2) ? TRUE : FALSE))
#endif

#define GCONF_KEY_DISABLE_MENU_EDIT "/apps/osso/hildon-desktop/menu_edit_disabled"

typedef struct
{
  GList *items;
  gboolean cancelled;
} HdLauncherTraverseData;

struct _HdLauncherPrivate
{
  GData *pages;
  ClutterActor *active_page;

  /* Actor and timeline required for zoom in on application screenshot
   * for app start. */
  gpointer launch_tile;
  ClutterActor *launch_image;
  guint launch_image_timeout; /* Timeout for removing launch image */
  ClutterTimeline *launch_transition;
  ClutterVertex launch_position; /* where were we clicked? */

  HdLauncherTree *tree;
  HdLauncherTraverseData *current_traversal;

  GtkWidget *editor;
  /* GConfClient to check whether menu editing is enabled or not */
  GConfClient *gconf_client;
  gboolean editor_done;

  gboolean portraited;
  gboolean is_editor_in_landscape;
};

#define HD_LAUNCHER_GET_PRIVATE(obj)  \
  (hd_launcher_get_instance_private (HD_LAUNCHER (obj)))

/* Signals */
enum
{
  APP_LAUNCHED,
  APP_RELAUNCHED,
  CAT_LAUNCHED,
  CAT_HIDDEN,
  HIDDEN,

  LAST_SIGNAL
};

static guint launcher_signals[LAST_SIGNAL] = {0, };

G_DEFINE_TYPE_WITH_CODE (HdLauncher,
                         hd_launcher,
                         CLUTTER_TYPE_GROUP,
                         G_ADD_PRIVATE (HdLauncher));

/* Forward declarations */
static void hd_launcher_constructed (GObject *gobject);
static void hd_launcher_dispose (GObject *gobject);
static void hd_launcher_category_tile_clicked (HdLauncherTile *tile,
                                               gpointer data);
static void hd_launcher_application_tile_clicked (HdLauncherTile *tile,
                                                  gpointer data);
static void hd_launcher_application_tile_long_clicked (HdLauncherTile *tile,
                                                       gpointer data);
static gboolean hd_launcher_captured_event_cb (HdLauncher *launcher,
                                               ClutterEvent *event,
                                               gpointer data);
static gboolean hd_launcher_background_clicked (HdLauncher *self,
                                                ClutterButtonEvent *event,
                                                gpointer *data);
static gboolean hd_launcher_key_pressed (HdLauncher *self,
                                                ClutterButtonEvent *event,
                                                gpointer *data);
static void hd_launcher_populate_tree_starting (HdLauncherTree *tree,
                                                gpointer data);
static void hd_launcher_populate_tree_finished (HdLauncherTree *tree,
                                                gpointer data);
static void hd_launcher_lazy_traverse_cleanup  (gpointer data);
static void hd_launcher_transition_new_frame(ClutterTimeline *timeline,
                                             gint frame_num, gpointer data);

/* We cannot #include "hd-transition.h" because it #include:s mb-wm.h,
 * which wants to #define _GNU_SOURCE unconditionally, but we already
 * have it in -D and they clash.  XXX */
extern void hd_transition_play_sound(const gchar *fname);

/* The HdLauncher singleton */
static HdLauncher *the_launcher = NULL;

HdLauncher *
hd_launcher_get (void)
{
  if (G_UNLIKELY (!the_launcher))
    the_launcher = g_object_new (HD_TYPE_LAUNCHER, NULL);
  return the_launcher;
}

static void
hd_launcher_class_init (HdLauncherClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose     = hd_launcher_dispose;
  gobject_class->constructed = hd_launcher_constructed;

  launcher_signals[APP_LAUNCHED] =
    g_signal_new (I_("application-launched"),
                  HD_TYPE_LAUNCHER,
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, HD_TYPE_LAUNCHER_APP);
  launcher_signals[APP_RELAUNCHED] =
    g_signal_new (I_("application-relaunched"),
                  HD_TYPE_LAUNCHER,
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, HD_TYPE_LAUNCHER_APP);
  launcher_signals[CAT_LAUNCHED] =
    g_signal_new (I_("category-launched"),
                  HD_TYPE_LAUNCHER,
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0, NULL);
  launcher_signals[CAT_HIDDEN] =
    g_signal_new (I_("category-hidden"),
                  HD_TYPE_LAUNCHER,
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0, NULL);
  launcher_signals[HIDDEN] =
    g_signal_new (I_("launcher-hidden"),
                  HD_TYPE_LAUNCHER,
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0, NULL);
}

static void
hd_launcher_init (HdLauncher *self)
{
  HdLauncherPrivate *priv;

  self->priv = priv = HD_LAUNCHER_GET_PRIVATE (self);
  priv->gconf_client = gconf_client_get_default ();
  g_datalist_init (&priv->pages);
}

static void hd_launcher_constructed (GObject *gobject)
{
  ClutterActor *self = CLUTTER_ACTOR (gobject);
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (gobject);

  clutter_actor_hide (self);
  clutter_actor_set_size (self,
                          HD_LAUNCHER_PAGE_WIDTH, HD_LAUNCHER_PAGE_HEIGHT);

  priv->tree = g_object_ref (hd_app_mgr_get_tree ());
  g_signal_connect (priv->tree, "starting",
                    G_CALLBACK (hd_launcher_populate_tree_starting),
                    gobject);
  g_signal_connect (priv->tree, "finished",
                    G_CALLBACK (hd_launcher_populate_tree_finished),
                    gobject);

  /* Add callback for clicked background */
  clutter_actor_set_reactive ( self, TRUE );
  g_signal_connect (self, "captured-event",
                    G_CALLBACK(hd_launcher_captured_event_cb), 0);
  g_signal_connect (self, "button-release-event",
                    G_CALLBACK(hd_launcher_background_clicked), 0);
  g_signal_connect (self, "key-press-event",
                    G_CALLBACK(hd_launcher_key_pressed), 0);

  /* App launch transition */
  priv->launch_image = 0;
  priv->launch_transition = g_object_ref (
                                clutter_timeline_new_for_duration (400));
  g_signal_connect (priv->launch_transition, "new-frame",
                    G_CALLBACK (hd_launcher_transition_new_frame), gobject);
  priv->launch_position.x = CLUTTER_INT_TO_FIXED(HD_LAUNCHER_PAGE_WIDTH) / 2;
  priv->launch_position.y = CLUTTER_INT_TO_FIXED(HD_LAUNCHER_PAGE_HEIGHT) / 2;
  priv->launch_position.z = 0;
}

static void
hd_launcher_dispose (GObject *gobject)
{
  HdLauncher *self = HD_LAUNCHER (gobject);
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (self);

  hd_launcher_stop_loading_transition();

  if (priv->tree)
    {
      g_object_unref (G_OBJECT (priv->tree));
      priv->tree = NULL;
    }

  if (priv->gconf_client)
    {
      g_object_unref (priv->gconf_client);
      priv->gconf_client = NULL;
    }

  g_datalist_clear (&priv->pages);

  G_OBJECT_CLASS (hd_launcher_parent_class)->dispose (gobject);
}

static void
_hd_launcher_hide_page (GQuark key_id, gpointer data, gpointer user_data)
{
  HdLauncherPage *page = HD_LAUNCHER_PAGE (data);
  clutter_actor_hide (CLUTTER_ACTOR(page));
}

static void
_hd_launcher_update_orientation_cb (GQuark key_id,
    gpointer data,
    gpointer user_data)
{
  HdLauncherPage *page = HD_LAUNCHER_PAGE (data);
  HdLauncherGrid *grid;
  ClutterActor *scroller;
  gboolean portraited = GPOINTER_TO_BOOLEAN (user_data);

  g_assert (HD_IS_LAUNCHER_PAGE (page));

  /* Scrolling animation delays screen rotation. */
  hd_launcher_page_stop_scrolling (page);

  grid = HD_LAUNCHER_GRID (hd_launcher_page_get_grid (page));
  /* actual layout update for the grid, reordering and resizing tiles */
  hd_launcher_grid_set_portrait (grid, portraited);
  hd_launcher_grid_layout (grid);

  /* resize the scroller as well */
  scroller = hd_launcher_page_get_scroller (page);
  if (portraited)
    clutter_actor_set_size (scroller, HD_LAUNCHER_PAGE_HEIGHT,
        HD_LAUNCHER_PAGE_WIDTH);
  else
    clutter_actor_set_size (scroller, HD_LAUNCHER_PAGE_WIDTH,
        HD_LAUNCHER_PAGE_HEIGHT);

  /* Update the 'empty label' position. */
  hd_launcher_page_update_emptylabel (page, portraited);
}

/* hd_launcher_update_orientation:
 *
 * update the the launcher's page orientation. It should be called when
 * passing from landscape to portrait orientation, or vice versa.
 *
 * Specifically, it will reorder the grid's tiles and the scroller according
 * to the orientation given by the HdAppMgr.
 */
void
hd_launcher_update_orientation (gboolean portraited)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  /* no changes, don't waste any time updating */
  if (priv->portraited == portraited)
    return;

  priv->portraited = portraited;

  g_datalist_foreach (&priv->pages,
      _hd_launcher_update_orientation_cb, GBOOLEAN_TO_POINTER (portraited));
}

/* hd_launcher_show:
 *
 * When the is_top_page is TRUE, the active_page private variable is set to top_page.
 * Otherwise it keeps the old value.
 *
 * Fixes BMO #12177: Autorotate disfunction using Catorise.
 */
void
hd_launcher_show (gboolean is_top_page)
{
  ClutterActor *self = CLUTTER_ACTOR (hd_launcher_get ());
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (self);

  if (is_top_page)
    {
      /* Make sure we hide all pages when we start,
       * to ensure that we can't get into any bad state */
      if (priv->pages)
        g_datalist_foreach (&priv->pages, _hd_launcher_hide_page, NULL);

      ClutterActor *top_page = g_datalist_get_data (&priv->pages,
                                                    HD_LAUNCHER_ITEM_TOP_CATEGORY);
      if (!top_page)
        return;

      priv->active_page = top_page;
    }
  else
    {
      if (!priv->active_page)
        return;
    }

  if (priv->editor_done)
    {
      /* TODO?: Avoid the transition if we're coming from the editor. */
      priv->editor_done = FALSE;
    }

  hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page),
      HD_LAUNCHER_PAGE_TRANSITION_IN);
  /* We must show *after* starting the transition, because starting a new
   * transition when an old transition is in progress will cause the old
   * transition to be ended - which will in turn hide the launcher if
   * the transition was a LAUNCHER_OUT transition. */
  clutter_actor_show (self);
}

void
hd_launcher_hide (void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  if (priv->active_page)
    {
      ClutterActor *top_page = g_datalist_get_data (&priv->pages,
                                    HD_LAUNCHER_ITEM_TOP_CATEGORY);
      /* if we're not at the top page, we must transition that out too */
      if (priv->active_page != top_page)
        {
          hd_launcher_page_transition(HD_LAUNCHER_PAGE(top_page),
              HD_LAUNCHER_PAGE_TRANSITION_OUT_BACK);
        }

      hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page),
          HD_LAUNCHER_PAGE_TRANSITION_OUT);
      priv->active_page = NULL;
    }
  else
    {
      hd_launcher_hide_final ();
    }
}

/* hide the launcher fully. Called from hd-launcher-page
 * after a transition has finished */
void
hd_launcher_hide_final (void)
{
  clutter_actor_hide (CLUTTER_ACTOR (hd_launcher_get ()));
}


gboolean
hd_launcher_back_button_clicked ()
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  ClutterActor *top_page = g_datalist_get_data (&priv->pages,
                                                HD_LAUNCHER_ITEM_TOP_CATEGORY);

  if (!STATE_IS_LAUNCHER (hd_render_manager_get_state()))
    return FALSE;

  if (priv->active_page == top_page)
    g_signal_emit (hd_launcher_get (), launcher_signals[HIDDEN], 0);
  else
    {
      if (priv->active_page)
        hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page),
          HD_LAUNCHER_PAGE_TRANSITION_OUT_SUB);
      if (top_page)
        hd_launcher_page_transition(HD_LAUNCHER_PAGE(top_page),
          HD_LAUNCHER_PAGE_TRANSITION_FORWARD);
      priv->active_page = top_page;
      g_signal_emit (hd_launcher_get (), launcher_signals[CAT_HIDDEN],
                     0, NULL);
    }

  return FALSE;
}

static void
hd_launcher_category_tile_clicked (HdLauncherTile *tile, gpointer data)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  ClutterActor *page = CLUTTER_ACTOR (data);

  hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page),
        HD_LAUNCHER_PAGE_TRANSITION_BACK);
  hd_launcher_page_transition(HD_LAUNCHER_PAGE(page),
        HD_LAUNCHER_PAGE_TRANSITION_IN_SUB);
  priv->active_page = page;
  g_signal_emit (hd_launcher_get (), launcher_signals[CAT_LAUNCHED],
                 0, NULL);
}

static void
hd_launcher_application_tile_clicked (HdLauncherTile *tile,
                                      gpointer data)
{
  HdLauncher *launcher = hd_launcher_get();
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (launcher);
  HdLauncherApp *app = HD_LAUNCHER_APP (data);
  ClutterActor *top_page;

  /* We must do this before hd_app_mgr_launch, as it uses the tile
   * clicked in order to zoom the launch image from the correct place */
  if (tile)
    {
      priv->launch_tile = tile;
      g_object_add_weak_pointer (G_OBJECT (tile), &priv->launch_tile);
    }
  else
    priv->launch_tile = NULL;

  if (!hd_app_mgr_launch (app))
    return;

  hd_launcher_page_transition(HD_LAUNCHER_PAGE(priv->active_page),
        HD_LAUNCHER_PAGE_TRANSITION_LAUNCH);
  /* also do animation for the topmost pane if we had it... */
  top_page = g_datalist_get_data (&priv->pages,
                                   HD_LAUNCHER_ITEM_TOP_CATEGORY);
  /* if we're not at the top page, we must transition that out too.
   * @active_page is reset to %NULL when we exit by launcher_hide(). */
  if (priv->active_page && priv->active_page != top_page)
    {
      hd_launcher_page_transition(HD_LAUNCHER_PAGE(top_page),
                HD_LAUNCHER_PAGE_TRANSITION_OUT_BACK);
    }

  g_signal_emit (hd_launcher_get (), launcher_signals[APP_LAUNCHED],
                 0, data, NULL);
}

/* This is a little complex, h-d needs to:
 * - Go back to the launcher when the user closes the editor window.
 * - Hide and destroy the editor window when the user is taken away from it,
 * i.e., for an incoming call.
 */

static void
_hd_launcher_editor_is_topmost (HildonWindow *editor,
                                GParamSpec   *pspec,
                                HdLauncher   *launcher)
{
  HdLauncherPrivate *priv = launcher->priv;

  g_debug ("%s: topmost: %d", __FUNCTION__,
           hildon_window_get_is_topmost (editor));

  /* If something has replaced the editor, close it. */
  if (!hildon_window_get_is_topmost (editor))
    {
      gtk_widget_destroy (priv->editor);
    }
}

static void
_hd_launcher_editor_done (HdLauncherEditor *editor,
                          gboolean          modified,
                          HdLauncher       *launcher)
{
  HdLauncherPrivate *priv = launcher->priv;

  g_debug ("%s: modified: %d", __FUNCTION__,
           modified);

  priv->editor_done = TRUE;

  if (!modified)
    {
      /* Go back directly to the launcher. */
      hd_render_manager_set_state (HDRM_STATE_LAUNCHER);
    }
}

static gboolean
_hd_launcher_editor_destroyed (GtkWidget  *widget,
                               HdLauncher *launcher)
{
  HdLauncherPrivate *priv = launcher->priv;

  g_debug ("%s", __FUNCTION__);

  priv->editor_done = FALSE;
  priv->is_editor_in_landscape = FALSE;

  /* Reset the launcher's layout. */
  if (STATE_IS_PORTRAIT (hd_render_manager_get_state ()))
    {
      /* The launcher's grid is in landscape mode when coming
       * from the editor. Reset the portraited variable and update
       * the layout. */

      priv->portraited = FALSE;
      hd_launcher_update_orientation (TRUE);
    }

  if (priv->editor)
    {
      priv->editor = NULL;
    }

  return FALSE;
}

static void
hd_launcher_application_tile_long_clicked (HdLauncherTile *tile,
                                           gpointer data)
{
  HdLauncher *launcher = hd_launcher_get();
  HdLauncherPrivate *priv = launcher->priv;

  /* Don't show the editor if the key to disable is true. */
  if (gconf_client_get_bool (priv->gconf_client, GCONF_KEY_DISABLE_MENU_EDIT, NULL))
    return;

  /* Send a mouse released event, because when we've put the editor window up
   * the release event will go straight to that instead of to the scroller,
   * and the scroller will think that the mouse is pressed until after the
   * editor closes and we tap again. See NB#156831 */
  if (priv->active_page) {
    ClutterActor *scroll = hd_launcher_page_get_scroller(
        HD_LAUNCHER_PAGE(priv->active_page));
    ClutterButtonEvent event;
    memset(&event, 0, sizeof(ClutterButtonEvent));
    event.button = 1;
    event.flags = CLUTTER_EVENT_FLAG_SYNTHETIC;

    g_signal_emit_by_name (scroll, "button-release-event", &event, NULL);
  }

  const char *active_category = NULL;
  ClutterActor *top_page = g_datalist_get_data (&priv->pages, HD_LAUNCHER_ITEM_TOP_CATEGORY);
  if (priv->active_page == top_page) {
    active_category = "Main";
  } else {
    GList *entries;
    HdLauncherTree *tree;

    tree = hd_app_mgr_get_tree();
    entries = hd_launcher_tree_get_items(tree);
    while (entries) {
      HdLauncherItem *item = entries->data;
      if (hd_launcher_item_get_item_type (item) != HD_CATEGORY_LAUNCHER) {
          entries = entries->next;
          continue;
      }

      ClutterActor *page = g_datalist_get_data (&priv->pages, hd_launcher_item_get_id (item));

      if (priv->active_page == page) {
        active_category = hd_launcher_item_get_id(item);
        break;
      }

      entries = entries->next;
    }
  }

  priv->editor = g_object_new(HD_TYPE_LAUNCHER_EDITOR, "category", active_category, NULL);

  gint x, y;
  gfloat x_align, y_align;

  clutter_actor_get_transformed_position (CLUTTER_ACTOR (tile), &x, &y);

  if (STATE_IS_PORTRAIT (hd_render_manager_get_state ())) {
    x_align = (gfloat)(x + (HD_LAUNCHER_TILE_WIDTH / 2))
                      / HD_LAUNCHER_PAGE_HEIGHT;
    y_align = (gfloat)(y + (HD_LAUNCHER_TILE_WIDTH / 2))
                      / HD_LAUNCHER_PAGE_WIDTH;
  }
  else {
    x_align = (gfloat)(x + (HD_LAUNCHER_TILE_WIDTH / 2))
                      / HD_LAUNCHER_PAGE_WIDTH;
    y_align = (gfloat)(y + (HD_LAUNCHER_TILE_WIDTH / 2))
                      / HD_LAUNCHER_PAGE_HEIGHT;
  }

  g_signal_connect (priv->editor, "destroy",
                    G_CALLBACK (_hd_launcher_editor_destroyed),
                    launcher);
  g_signal_connect (priv->editor, "notify::is-topmost",
                    G_CALLBACK (_hd_launcher_editor_is_topmost),
                    launcher);
  g_signal_connect (priv->editor, "done",
                    G_CALLBACK (_hd_launcher_editor_done),
                    launcher);

  priv->is_editor_in_landscape = !STATE_IS_PORTRAIT (hd_render_manager_get_state ());

  hd_launcher_editor_show (priv->editor);
  hd_launcher_editor_select (HD_LAUNCHER_EDITOR (priv->editor),
                             hd_launcher_tile_get_text (tile),
                             x_align, y_align);
}

static void
_hd_launcher_clear_page (GQuark key_id, gpointer data, gpointer user_data)
{
  HdLauncherPage *page = HD_LAUNCHER_PAGE (data);
  hd_launcher_grid_clear (HD_LAUNCHER_GRID (hd_launcher_page_get_grid (page)));
}

static void
_hd_launcher_layout_page (GQuark key_id, gpointer data, gpointer user_data)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  HdLauncherPage *page = HD_LAUNCHER_PAGE (data);
  HdLauncherGrid *grid = HD_LAUNCHER_GRID (hd_launcher_page_get_grid (page));

  /* actual layout update for the grid, reordering and resizing tiles */
  hd_launcher_grid_set_portrait (grid, priv->portraited);

  hd_launcher_grid_layout (HD_LAUNCHER_GRID (grid));
}

static void
hd_launcher_populate_tree_starting (HdLauncherTree *tree, gpointer data)
{
  HdLauncher *launcher = HD_LAUNCHER (data);
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (launcher);

  if (STATE_IS_LAUNCHER (hd_render_manager_get_state ()))
    {
      if (priv->portraited)
        hd_render_manager_set_state (HDRM_STATE_HOME_PORTRAIT);
      else
        hd_render_manager_set_state (HDRM_STATE_HOME);
    }

  priv->active_page = NULL;

  if (priv->current_traversal)
    {
      priv->current_traversal->cancelled = TRUE;
    }
  priv->current_traversal = NULL;

  if (priv->pages)
    {
      g_datalist_foreach (&priv->pages, _hd_launcher_clear_page, NULL);
      g_datalist_clear (&priv->pages);
      priv->pages = NULL;
    }
  g_datalist_init(&priv->pages);
}

/*
 * Creating the pages and tiles
 */

static void
hd_launcher_create_page (HdLauncherItem *item, gpointer data)
{
  ClutterActor *self = CLUTTER_ACTOR (hd_launcher_get ());
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (self);
  ClutterActor *newpage;

  if (hd_launcher_item_get_item_type (item) != HD_CATEGORY_LAUNCHER)
    return;

  newpage = hd_launcher_page_new ();

  clutter_actor_hide (newpage);
  clutter_container_add_actor (CLUTTER_CONTAINER (self), newpage);
  g_datalist_set_data_full (&priv->pages, hd_launcher_item_get_id (item), newpage, (GDestroyNotify) clutter_actor_destroy);
}

static gboolean
hd_launcher_lazy_traverse_tree (gpointer data)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  HdLauncherTraverseData *tdata = data;
  HdLauncherItem *item;
  HdLauncherTile *tile;
  HdLauncherPage *page = NULL;
  guint i;

  if (!tdata ||
      tdata->cancelled ||
      tdata != priv->current_traversal)
    /* This traversal is no longer current, go to cleanup. */
    return FALSE;

  /* We're called back with huge latency so let's batch the work
   * to cut the overall population time. */
  for (i = 0; i < 5; i++)
    {
      if (!tdata->items || !tdata->items->data)
        return FALSE;
      item = tdata->items->data;

      tile = hd_launcher_tile_new (
          hd_launcher_item_get_icon_name (item),
          hd_launcher_item_get_local_name (item));
      /* Signals can be handled during construction, so check this hasn't
       * been cancelled. */
      if (tdata->cancelled)
        {
          g_object_unref (tile);
          return FALSE;
        }

      /* Find in which page it goes */
      page = g_datalist_get_data (&priv->pages,
                                  hd_launcher_item_get_category (item));
      if (!page)
        /* Put it in the top level. */
        page = g_datalist_get_data (&priv->pages, HD_LAUNCHER_ITEM_TOP_CATEGORY);

      /* If we don't have a top level, we're in deep trouble, but we still
       * check just in case.
       */
      if (!page)
        {
          g_warning ("%s: Couldn't find any page to accept entry %s",
              __FUNCTION__, hd_launcher_item_get_id (item));
          g_object_unref (tile);
        }
      else
        {
          hd_launcher_page_add_tile (page, tile);

          if (hd_launcher_item_get_item_type(item) == HD_CATEGORY_LAUNCHER)
            {
              g_signal_connect (tile, "clicked",
                                G_CALLBACK (hd_launcher_category_tile_clicked),
                                g_datalist_get_data (&priv->pages,
                                  hd_launcher_item_get_id (item)));
            }
          else if (hd_launcher_item_get_item_type(item) == HD_APPLICATION_LAUNCHER)
            {
              g_signal_connect (tile, "clicked",
                                G_CALLBACK (hd_launcher_application_tile_clicked),
                                item);
            }

          g_signal_connect (tile, "long-clicked",
                        G_CALLBACK (hd_launcher_application_tile_long_clicked),
                        item);
        }

      g_object_unref (G_OBJECT (item));
      tdata->items = g_list_delete_link (tdata->items, tdata->items);
      if (!tdata->items)
        {
          g_datalist_foreach(&priv->pages, _hd_launcher_layout_page, NULL);

          /* This traversal has finished. */
          priv->current_traversal = NULL;

          /* If the changes came when an editor is present, switch back to
           * launcher
           */
          if (priv->editor && priv->editor_done)
            {
              hd_render_manager_set_state (HDRM_STATE_LAUNCHER);
            }
          return FALSE;
        }
    }

  g_datalist_foreach(&priv->pages, _hd_launcher_layout_page, NULL);

  return TRUE;
}

static void
hd_launcher_lazy_traverse_cleanup (gpointer data)
{
  HdLauncherTraverseData *tdata = data;

  /* It's possible that the traversal has been cut short, so clean up the list. */
  tdata->cancelled = TRUE;
  if (tdata->items)
    {
      g_list_foreach (tdata->items, (GFunc)g_object_unref, NULL);
      g_list_free (tdata->items);
      tdata->items = NULL;
    }

  g_free (data);
}

static void
hd_launcher_populate_tree_finished (HdLauncherTree *tree, gpointer data)
{
  HdLauncher *launcher = HD_LAUNCHER (data);
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (launcher);
  HdLauncherTraverseData *tdata = g_new0 (HdLauncherTraverseData, 1);

  /* As we'll be adding these in an idle loop, we need to ensure that they
   * won't disappear while we do this, so we copy the list and ref all the
   * items. */
  tdata->items = g_list_copy(hd_launcher_tree_get_items(tree));
  g_list_foreach (tdata->items, (GFunc)g_object_ref, NULL);

  if (priv->current_traversal)
    {
      priv->current_traversal->cancelled = TRUE;
    }
  priv->current_traversal = tdata;

  /* if after traversal starts, the user switches to LAUNCHER,
   * we can get empty launcher (the old page) that gets stuck on
   * the screen until the user opens the power menu */
  if (STATE_IS_LAUNCHER (hd_render_manager_get_state ()))
    {
      if (priv->portraited)
        hd_render_manager_set_state (HDRM_STATE_HOME_PORTRAIT);
      else
        hd_render_manager_set_state (HDRM_STATE_HOME);
    }

  /* First we traverse the list and create all the categories,
   * so that apps can be correctly put into them.
   */
  ClutterActor *top_page = hd_launcher_page_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (launcher),
                               top_page);
  clutter_actor_hide (top_page);
  priv->active_page = NULL;
  g_datalist_set_data_full (&priv->pages, HD_LAUNCHER_ITEM_TOP_CATEGORY, top_page, (GDestroyNotify) clutter_actor_destroy);

  g_list_foreach (tdata->items, (GFunc) hd_launcher_create_page, NULL);

  /* Then we add the tiles to them in a idle callback. */
  clutter_threads_add_idle_full (CLUTTER_PRIORITY_REDRAW + 20,
                                 hd_launcher_lazy_traverse_tree,
                                 tdata,
                                 hd_launcher_lazy_traverse_cleanup);
}

/* handle clicks to the fake launch image. If we've been up this long the
   app may have died and we just want to remove ourselves. */
static gboolean
_hd_launcher_transition_clicked(ClutterActor *actor,
                                ClutterEvent *event,
                                gpointer user_data)
{
  /* Just totally ignore clicks */
  return TRUE;
}

/* handle clicks to the fake launch image. If we've been up this long the
   app may have died and we just want to remove ourselves. */
static gboolean
hd_launcher_transition_loading_timeout()
{
	HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  hd_launcher_stop_loading_transition();
  hd_render_manager_set_loading (NULL);
  /* Change state back to switcher (if other apps exist) or home if the app
   * starting failed */
  if (hd_task_navigator_has_apps())
    if(priv->portraited)
      hd_render_manager_set_state(HDRM_STATE_TASK_NAV_PORTRAIT);
    else
      hd_render_manager_set_state(HDRM_STATE_TASK_NAV);
  else
    {
      if(priv->portraited)
	    hd_render_manager_set_state (HDRM_STATE_HOME_PORTRAIT);
      else
        hd_render_manager_set_state (HDRM_STATE_HOME);
	}
  return FALSE; // don't call again
}

/* TODO: Move the loading screen into its own class. */
void
hd_launcher_stop_loading_transition ()
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  if (priv->launch_image_timeout)
      {
        g_source_remove(priv->launch_image_timeout);
        priv->launch_image_timeout = 0;
      }
  if (priv->launch_image)
    {
      clutter_timeline_stop(priv->launch_transition);

      g_object_unref(priv->launch_image);
      priv->launch_image = 0;
    }
}

/* Does the transition for the application launch */
gboolean
hd_launcher_transition_app_start (HdLauncherApp *item)
{
  const gchar *loading_image = NULL;
  HdLauncher *launcher = hd_launcher_get();
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (launcher);
  gboolean launch_anim = FALSE;
  const gchar *service_name = 0;
  gchar *cached_image = NULL;
  ClutterActor *app_image = 0;
  gint cursor_x, cursor_y;

  /* Refuse to do a second loading screen if we are already showing one */
  if (STATE_IS_LOADING(hd_render_manager_get_state()))
      return FALSE;

  /* Is there a cached image? */
  if (item)
    service_name = hd_launcher_app_get_service (item);

  if (service_name &&
      index(service_name, '/')==NULL &&
      service_name[0]!='.')
    {
      if (STATE_IS_PORTRAIT(hd_render_manager_get_state()))
        cached_image = g_strdup_printf("%s/.cache/launch/%s_portrait.pvr",
				       getenv("HOME"),
				       service_name);
      else
        cached_image = g_strdup_printf("%s/.cache/launch/%s.pvr",
				       getenv("HOME"),
				       service_name);

      if (access (cached_image, R_OK)==0)
        loading_image = cached_image;
    }

  /* If not, does the .desktop file specify an image? */
  if (!loading_image && item)
    loading_image = hd_launcher_app_get_loading_image( item );

  if (loading_image && !g_strcmp0(loading_image, HD_LAUNCHER_NO_TRANSITION))
    {
      /* If the app specified no transition, just play the sound and return. */
      hd_transition_play_sound (HDCM_WINDOW_OPENED_SOUND);
      return FALSE;
    }

  hd_launcher_stop_loading_transition();

  /* App image - if we had one */
  if (loading_image)
    {
      app_image = clutter_texture_new_from_file(loading_image, 0);
      if (!app_image)
        g_warning("%s: Preload image file '%s' specified for '%s'"
                    " couldn't be loaded",
                  __FUNCTION__, loading_image, hd_launcher_app_get_exec(item));
      else
        {
          guint w,h;
          ClutterGeometry region = {0, 0, 0, 0};
          clutter_actor_get_size(app_image, &w, &h);

          region.width = hd_comp_mgr_get_current_screen_width ();
          region.height = hd_comp_mgr_get_current_screen_height () -
                          HD_COMP_MGR_TOP_MARGIN;

          if (w > region.width ||
              h > region.height)
            {
              /* It may be that we get a bigger texture than we need
               * (because PVR texture compression has to use 2^n width
               * and height). In this case we want to crop off the
               * bottom + right sides, which we can do more efficiently
               * with TidySubTexture than we can with set_clip.
               */
               TidySubTexture *sub;
               sub = tidy_sub_texture_new(CLUTTER_TEXTURE(app_image));
               tidy_sub_texture_set_region(sub, &region);
               clutter_actor_set_size(CLUTTER_ACTOR(sub),
                                      region.width, region.height);
               clutter_actor_hide(app_image);
               app_image = CLUTTER_ACTOR(sub);
            }
        }
    }
  /* if not, create a rectangle with the background colour from the theme */
  if (!app_image)
    {
      ClutterColor col;
      hd_gtk_style_get_bg_color(HD_GTK_BUTTON_SINGLETON,
                                GTK_STATE_NORMAL,
                                &col);
      app_image = clutter_rectangle_new_with_color(&col);
    }

  clutter_actor_set_size(app_image,
                         hd_comp_mgr_get_current_screen_width (),
                         hd_comp_mgr_get_current_screen_height ()
                         - HD_COMP_MGR_TOP_MARGIN);
  priv->launch_image = g_object_ref_sink(app_image);

  /* Try and get the current mouse cursor location - this should be the place
   * the user last pressed */
  if (hd_util_get_cursor_position(&cursor_x, &cursor_y))
    {
      priv->launch_position.x = CLUTTER_INT_TO_FIXED(cursor_x);
      priv->launch_position.y = CLUTTER_INT_TO_FIXED(cursor_y);
    }
  else
    {
      /* default pos to centre of the screen */
      priv->launch_position.x = CLUTTER_INT_TO_FIXED(HD_LAUNCHER_PAGE_WIDTH) / 2;
      priv->launch_position.y = CLUTTER_INT_TO_FIXED(HD_LAUNCHER_PAGE_HEIGHT) / 2;
    }

  /* If a launcher tile was clicked, expand the image from the centre of the
   * the tile instead.
   * Only do this if the user did really click on a tile. */
  if (priv->launch_tile && item)
    {
      ClutterActor *parent;
      HdLauncherTile *tile = HD_LAUNCHER_TILE (priv->launch_tile);
      ClutterActor *icon;
      clutter_actor_get_positionu(CLUTTER_ACTOR(tile),
                &priv->launch_position.x,
                &priv->launch_position.y);
      icon = hd_launcher_tile_get_icon(tile);
      if (icon)
        {
          ClutterVertex offs, size;
          clutter_actor_get_positionu(icon,
                &offs.x,
                &offs.y);
          clutter_actor_get_sizeu(icon, &size.x, &size.y);
          priv->launch_position.x += offs.x + size.x/2;
          priv->launch_position.y += offs.y + size.y/2;
        }
      /* add the X and Y offsets from all parents */
      parent = clutter_actor_get_parent(CLUTTER_ACTOR(tile));
      while (parent && !CLUTTER_IS_STAGE(parent)) {
        ClutterFixed x,y;
        clutter_actor_get_positionu(parent, &x, &y);
        priv->launch_position.x += x;
        priv->launch_position.y += y;
        parent = clutter_actor_get_parent(parent);
      }
    }
  /* Clean up. */
  if (priv->launch_tile)
    {
      g_object_remove_weak_pointer (G_OBJECT (priv->launch_tile),
                                    &priv->launch_tile);
      priv->launch_tile = NULL;
    }
  /* append scroller movement */
  if (priv->active_page)
    priv->launch_position.y -=
        hd_launcher_page_get_scroll_y(HD_LAUNCHER_PAGE(priv->active_page));
  /* all because the tidy- stuff breaks clutter's nice 'get absolute position'
   * code... */

  hd_render_manager_set_loading (priv->launch_image);
  clutter_actor_set_name(priv->launch_image,
                         "HdLauncher:launch_image");
  clutter_actor_set_position(priv->launch_image,
                             0, HD_COMP_MGR_TOP_MARGIN);
  clutter_actor_set_reactive ( priv->launch_image, TRUE );
  g_signal_connect (priv->launch_image, "button-release-event",
                    G_CALLBACK(_hd_launcher_transition_clicked), 0);

  clutter_timeline_set_duration(priv->launch_transition,
                                hd_transition_get_int("launcher_launch",
                                                      "duration", 200));
  /* Run the first step of the transition so we don't get flicker before
   * the timeline is called */
  hd_launcher_transition_new_frame(priv->launch_transition,
                                   0, launcher);
  if (STATE_IS_APP (hd_render_manager_get_state ()))
    {
      if (STATE_IS_PORTRAIT (hd_render_manager_get_state ()))
        hd_render_manager_set_state (HDRM_STATE_LOADING_SUBWIN_PORTRAIT);
      else
        hd_render_manager_set_state (HDRM_STATE_LOADING_SUBWIN);
    }
  else
    {
      if (STATE_IS_PORTRAIT (hd_render_manager_get_state ()))
        hd_render_manager_set_state (HDRM_STATE_LOADING_PORTRAIT);
      else
        hd_render_manager_set_state (HDRM_STATE_LOADING);
    }

  HdTitleBar *tbar = HD_TITLE_BAR (hd_render_manager_get_title_bar());
  const gchar *title = "";
  if (item)
    title =  hd_launcher_item_get_local_name(HD_LAUNCHER_ITEM (item));

  hd_title_bar_set_loading_title (tbar, title);

  clutter_timeline_rewind(priv->launch_transition);
  clutter_timeline_start(priv->launch_transition);

  launch_anim = TRUE;

  hd_transition_play_sound (HDCM_WINDOW_OPENED_SOUND);
  g_free (cached_image);

  /* Add callback for if application loading fails. We don't use the app
   * launcher signal here as if the icon starts an app that returns
   * immediately (eg. ls) we don't get a loading failed signal, as
   * nothing failed (but we don't get a window shown regardless). */
  priv->launch_image_timeout =
    g_timeout_add_seconds(10, hd_launcher_transition_loading_timeout, 0);

  return launch_anim;
}

/* Returns whether the application starting transition is moving,
 * ie. either the fake window or the applications stored screenshot
 * is growing. */
gboolean
hd_launcher_transition_is_playing(void)
{
  HdLauncher *launcher = hd_launcher_get();
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE(launcher);
  return clutter_timeline_is_playing(priv->launch_transition);
}

/* When a window has been created we want to be sure we've removed our
 * screenshot. Either that or we smoothly fade it out... maybe? :) */
void hd_launcher_window_created(void)
{
  hd_launcher_stop_loading_transition();
  hd_render_manager_set_loading (NULL);
}

static void
hd_launcher_transition_new_frame(ClutterTimeline *timeline,
                          gint frame_num, gpointer data)
{
  HdLauncher *page = HD_LAUNCHER(data);
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (page);
  gint frames;
  float amt, zoom;
  ClutterFixed mx,my;

  if (!HD_IS_LAUNCHER(data))
    return;

  frames = clutter_timeline_get_n_frames(timeline);
  amt = frame_num / (float)frames;

  zoom = 0.05 + hd_transition_ease_out(amt) * 0.95f;

  if (!priv->launch_image)
    return;

  /* mid-position of actor */
  mx = CLUTTER_FLOAT_TO_FIXED(//width*0.5f*(1-zoom) +
                CLUTTER_FIXED_TO_FLOAT(priv->launch_position.x)*(1-zoom)/zoom);
  my = CLUTTER_FLOAT_TO_FIXED(//height*0.5f*(1-zoom) +
                CLUTTER_FIXED_TO_FLOAT(priv->launch_position.y)*(1-zoom)/zoom);
  clutter_actor_set_anchor_pointu(priv->launch_image, -mx, -my);
  clutter_actor_set_scale(priv->launch_image, zoom, zoom);
}

HdLauncherTree *
hd_launcher_get_tree (void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());
  return priv->tree;
}

static void
_hd_launcher_transition_stop_foreach(GQuark         key_id,
                                     gpointer       data,
                                     gpointer       user_data)
{
  hd_launcher_page_transition_stop(HD_LAUNCHER_PAGE(data));
}

/* Stop any currently active transitions */
void
hd_launcher_transition_stop(void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  g_datalist_foreach(&priv->pages,
                     _hd_launcher_transition_stop_foreach,
                     (gpointer)0);
}

static gboolean
hd_launcher_captured_event_cb (HdLauncher *launcher,
                               ClutterEvent *event,
                               gpointer data)
{
  HdLauncherPrivate *priv;

  if (!HD_IS_LAUNCHER(launcher))
    return FALSE;
  priv = HD_LAUNCHER_GET_PRIVATE (launcher);

  if (event->type == CLUTTER_BUTTON_PRESS)
    {
      /* we need this for when the user clicks outside the page */
      if (priv->active_page)
        hd_launcher_page_set_drag_distance(
            HD_LAUNCHER_PAGE(priv->active_page), 0);
    }

  return FALSE;
}

static gboolean
hd_launcher_background_clicked (HdLauncher *self,
                                ClutterButtonEvent *event,
                                gpointer *data)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  /* We don't want to send a 'clicked' event if the user has dragged more
   * than the allowed distance - or if they released while inbetween icons.
   * If we have no page, just allow any click to take us back. */
  if (!priv->active_page ||
      (hd_launcher_page_get_drag_distance(HD_LAUNCHER_PAGE(priv->active_page)) <
                                          HD_LAUNCHER_TILE_MAX_DRAG))
    hd_launcher_back_button_clicked();

  return TRUE;
}

static gboolean
hd_launcher_key_pressed (HdLauncher *self,
                                ClutterButtonEvent *event,
                                gpointer *data)
{
//  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  hd_launcher_back_button_clicked();

  return TRUE;
}

void
hd_launcher_activate (int p)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  if (hd_render_manager_get_state () != HDRM_STATE_LAUNCHER)
    return;

  if (p == -1)
    {
      hd_launcher_back_button_clicked();
      return;
    }

  hd_launcher_page_activate(priv->active_page, p);
}

gboolean
hd_launcher_is_editor_in_landscape (void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  return priv->is_editor_in_landscape;
}

gboolean
hd_launcher_is_portrait (void)
{
  HdLauncherPrivate *priv = HD_LAUNCHER_GET_PRIVATE (hd_launcher_get ());

  return priv->portraited;
}
