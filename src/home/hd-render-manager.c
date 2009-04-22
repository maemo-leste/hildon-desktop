/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Author:  Gordon Williams <gordon.williams@collabora.co.uk>
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

#include "hd-render-manager.h"

#include <clutter/clutter.h>

#include "tidy/tidy-blur-group.h"

#include "hd-comp-mgr.h"
#include "hd-home.h"
#include "hd-switcher.h"
#include "hd-launcher.h"
#include "hd-task-navigator.h"
#include "hd-transition.h"
#include "hd-wm.h"
#include "hd-util.h"
#include "hd-title-bar.h"

#include <matchbox/core/mb-wm.h>
#include <matchbox/theme-engines/mb-wm-theme.h>

/* This is to dump debug information to the console to help see whether the
 * order of clutter actors matches that of matchbox. */
//#define STACKING_DEBUG 1

/* And this one is to help debugging visibility-related problems
 * ie. when stacking is all right but but you cannot see what you want. */
#if 0
# define VISIBILITY       g_debug
#else
# define VISIBILITY(...)  /* NOP */
#endif

/* ------------------------------------------------------------------------- */
#define I_(str) (g_intern_static_string ((str)))

GType
hd_render_manager_state_get_type (void)
{
  static GType gtype = 0;

  if (G_UNLIKELY (gtype == 0))
    {
      static GEnumValue values[] = {
        { HDRM_STATE_UNDEFINED,      "HDRM_STATE_UNDEFINED",      "Undefined" },
        { HDRM_STATE_HOME,           "HDRM_STATE_HOME",           "Home" },
        { HDRM_STATE_HOME_EDIT,      "HDRM_STATE_HOME_EDIT",      "Home edit" },
        { HDRM_STATE_HOME_EDIT_DLG,  "HDRM_STATE_HOME_EDIT_DLG",  "Home edit dialog" },
        { HDRM_STATE_HOME_PORTRAIT,  "HDRM_STATE_HOME_PORTRAIT",  "Home in portrait mode" },
        { HDRM_STATE_APP,            "HDRM_STATE_APP",            "Application" },
        { HDRM_STATE_APP_PORTRAIT,   "HDRM_STATE_APP_PORTRAIT",   "Application in portrait mode" },
        { HDRM_STATE_TASK_NAV,       "HDRM_STATE_TASK_NAV",       "Task switcher" },
        { HDRM_STATE_LAUNCHER,       "HDRM_STATE_LAUNCHER",       "Task launcher" },
        { 0, NULL, NULL }
      };

      gtype = g_enum_register_static (I_("HdRenderManagerStateType"), values);
    }

  return gtype;
}
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */

G_DEFINE_TYPE (HdRenderManager, hd_render_manager, CLUTTER_TYPE_GROUP);
#define HD_RENDER_MANAGER_GET_PRIVATE(obj) \
                (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                HD_TYPE_RENDER_MANAGER, HdRenderManagerPrivate))

/* The HdRenderManager singleton */
static HdRenderManager *the_render_manager = NULL;

/* HdRenderManager properties */
enum
{
  PROP_0,
  PROP_STATE
};
/* ------------------------------------------------------------------------- */

typedef enum
{
  HDRM_BLUR_NONE = 0,
  HDRM_BLUR_HOME = 1,
  HDRM_SHOW_TASK_NAV = 2,
  HDRM_BLUR_BACKGROUND = 4, /* like BLUR_HOME, but for dialogs, etc */
  HDRM_ZOOM_FOR_LAUNCHER = 16,
  HDRM_ZOOM_FOR_LAUNCHER_SUBMENU = 32,
  HDRM_ZOOM_FOR_TASK_NAV = 64,
} HDRMBlurEnum;

#define HDRM_WIDTH  HD_COMP_MGR_SCREEN_WIDTH
#define HDRM_HEIGHT HD_COMP_MGR_SCREEN_HEIGHT

enum
{
  TRANSITION_COMPLETE,
  LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0, };

/* ------------------------------------------------------------------------- */

/*
 *
 * HDRM ---> home_blur         ---> home
 *       |                                         --> home_get_front (!STATE_HOME_FRONT)
 *       |                      --> apps (not app_top)
 *       |                      --> blur_front (STATE_BLUR_BUTTONS)
 *       |                                        ---> home_get_front (STATE_HOME_FRONT)
 *       |                                         --> title_bar
 *       |                                               ---> title_bar::foreground (!HDTB_VIS_FOREGROUND)
 *       |                                                   --->status_area
 *       |
 *       --> task_nav_container --> task_nav
 *       |
 *       --> launcher
 *       |
 *       --> blur_front (!STATE_BLUR_BUTTONS)
 *       |
 *       --> app_top           ---> dialogs
 *       |
 *       --> front             ---> status_menu
 *                                                ---> title_bar::foreground (HDTB_VIS_FOREGROUND)
 *                                                           --->status_area
 *
 */

typedef struct _Range {
  float a, b, current;
} Range;

struct _HdRenderManagerPrivate {
  HDRMStateEnum state;
  HDRMStateEnum previous_state;

  TidyBlurGroup *home_blur;
  ClutterGroup  *app_top;
  ClutterGroup  *front;
  ClutterGroup  *blur_front;
  HdTitleBar    *title_bar;

  /* external */
  HdCompMgr            *comp_mgr;
  HdTaskNavigator      *task_nav;
  HdHome               *home;
  ClutterActor         *status_area;
  MBWindowManagerClient *status_area_client;
  ClutterActor         *status_menu;
  ClutterActor         *operator;
  ClutterActor         *button_task_nav;
  ClutterActor         *button_launcher;
  ClutterActor         *button_back;
  ClutterActor         *button_edit;

  /* these are current, from + to variables for doing the blurring animations */
  Range         home_radius;
  Range         home_zoom;
  Range         home_brightness;
  Range         home_saturation;
  Range         task_nav_opacity;
  Range         task_nav_zoom;

  HDRMBlurEnum  current_blur;

  ClutterTimeline    *timeline_blur;
  /* Timeline works by signals, so we get horrible flicker if we ask it if it
   * is playing right after saying _start() - so we have a boolean to figure
   * out for ourselves */
  gboolean            timeline_playing;

  gboolean            in_set_state;
  gboolean            queued_redraw;
};

/* ------------------------------------------------------------------------- */
static void
stage_allocation_changed(ClutterActor *actor, GParamSpec *unused,
                         ClutterActor *stage);
static void
hd_render_manager_paint_notify(void);
static void
on_timeline_blur_new_frame(ClutterTimeline *timeline,
                           gint frame_num, gpointer data);
static void
on_timeline_blur_completed(ClutterTimeline *timeline, gpointer data);

static void
hd_render_manager_sync_clutter_before(void);
static void
hd_render_manager_sync_clutter_after(void);

static const char *
hd_render_manager_state_str(HDRMStateEnum state);
static void
hd_render_manager_set_visibilities(void);

static void
hd_render_manager_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec);
static void
hd_render_manager_set_property (GObject      *gobject,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec);

/* ------------------------------------------------------------------------- */
/* -------------------------------------------------------------  RANGE      */
/* ------------------------------------------------------------------------- */
static inline void range_set(Range *range, float val)
{
  range->a = range->b = range->current = val;
}
static inline void range_interpolate(Range *range, float n)
{
  range->current = (range->a*(1-n)) + range->b*n;
}
static inline void range_next(Range *range, float x)
{
  range->a = range->current;
  range->b = x;
}
static inline gboolean range_equal(Range *range)
{
  return range->a == range->b;
}

/* ------------------------------------------------------------------------- */
/* -------------------------------------------------------------    INIT     */
/* ------------------------------------------------------------------------- */

HdRenderManager *hd_render_manager_create (HdCompMgr *hdcompmgr,
                                           HdLauncher *launcher,
                                           HdHome *home,
                                           HdTaskNavigator *task_nav
                                           )
{
  HdRenderManagerPrivate *priv;

  g_assert(the_render_manager == NULL);

  the_render_manager = HD_RENDER_MANAGER(g_object_ref (
        g_object_new (HD_TYPE_RENDER_MANAGER, NULL)));
  priv = the_render_manager->priv;

  priv->comp_mgr = hdcompmgr;

  /* Task switcher widget: anchor it at the centre so it is zoomed in
   * the middle when blurred. */
  priv->task_nav = task_nav;
  clutter_actor_set_visibility_detect(CLUTTER_ACTOR(priv->task_nav), FALSE);
  clutter_actor_set_position(CLUTTER_ACTOR(priv->task_nav), 0, 0);
  clutter_actor_set_size (CLUTTER_ACTOR(priv->task_nav),
                          HD_COMP_MGR_LANDSCAPE_WIDTH,
                          HD_COMP_MGR_LANDSCAPE_HEIGHT);
  clutter_container_add_actor(CLUTTER_CONTAINER(the_render_manager),
                              CLUTTER_ACTOR(priv->task_nav));
  clutter_actor_move_anchor_point_from_gravity(CLUTTER_ACTOR(priv->task_nav),
                                               CLUTTER_GRAVITY_CENTER);

  /* Add the launcher widget. */
  clutter_container_add_actor(CLUTTER_CONTAINER(the_render_manager),
                              CLUTTER_ACTOR(launcher));

  /* These must be below tasw and talu. */
  clutter_actor_lower_bottom (CLUTTER_ACTOR (priv->app_top));
  clutter_actor_lower_bottom (CLUTTER_ACTOR (priv->front));

  /* HdHome */
  priv->home = home;
  g_signal_connect_swapped(clutter_stage_get_default(), "notify::allocation",
                           G_CALLBACK(stage_allocation_changed), priv->home);
  clutter_container_add_actor(CLUTTER_CONTAINER(priv->home_blur),
                              CLUTTER_ACTOR(priv->home));

  /* Edit button */
  priv->button_edit = g_object_ref(hd_home_get_edit_button(priv->home));
  clutter_container_add_actor(CLUTTER_CONTAINER(priv->blur_front),
                              priv->button_edit);

  /* Operator */
  hd_render_manager_set_operator(hd_home_get_operator(priv->home));
  clutter_actor_reparent(priv->operator, CLUTTER_ACTOR(priv->blur_front));

  /* HdTitleBar */
  priv->title_bar = g_object_new(HD_TYPE_TITLE_BAR, NULL);
  g_signal_connect_swapped(clutter_stage_get_default(), "notify::allocation",
                           G_CALLBACK(stage_allocation_changed), priv->title_bar);
  clutter_container_add_actor(CLUTTER_CONTAINER(priv->blur_front),
                              CLUTTER_ACTOR(priv->title_bar));

  g_signal_connect (priv->button_back,
                    "button-release-event",
                    G_CALLBACK (hd_launcher_back_button_clicked),
                    launcher);
  g_signal_connect (priv->button_back,
                    "button-release-event",
                    G_CALLBACK (hd_home_back_button_clicked),
                    home);

  return the_render_manager;
}

HdRenderManager *
hd_render_manager_get (void)
{
  return the_render_manager;
}

static void
hd_render_manager_finalize (GObject *gobject)
{
  HdRenderManagerPrivate *priv = HD_RENDER_MANAGER_GET_PRIVATE(gobject);
  g_object_unref(priv->home);
  g_object_unref(priv->task_nav);
  g_object_unref(priv->title_bar);
  G_OBJECT_CLASS (hd_render_manager_parent_class)->finalize (gobject);
}

static void
hd_render_manager_class_init (HdRenderManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (HdRenderManagerPrivate));

  gobject_class->get_property = hd_render_manager_get_property;
  gobject_class->set_property = hd_render_manager_set_property;
  gobject_class->finalize = hd_render_manager_finalize;

  signals[TRANSITION_COMPLETE] =
        g_signal_new ("transition-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);



  pspec = g_param_spec_enum ("state",
                             "State", "Render manager state",
                             HD_TYPE_RENDER_MANAGER_STATE,
                             HDRM_STATE_UNDEFINED,
                             G_PARAM_READABLE    |
                             G_PARAM_WRITABLE    |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_STATE, pspec);
}

static void
hd_render_manager_init (HdRenderManager *self)
{
  ClutterActor *stage;
  HdRenderManagerPrivate *priv;

  stage = clutter_stage_get_default();

  self->priv = priv = HD_RENDER_MANAGER_GET_PRIVATE (self);
  clutter_actor_set_name(CLUTTER_ACTOR(self), "HdRenderManager");
  g_signal_connect_swapped(stage, "notify::allocation",
                           G_CALLBACK(stage_allocation_changed), self);

  priv->state = HDRM_STATE_UNDEFINED;
  priv->previous_state = HDRM_STATE_UNDEFINED;
  priv->current_blur = HDRM_BLUR_NONE;

  priv->home_blur = TIDY_BLUR_GROUP(tidy_blur_group_new());
  clutter_actor_set_name(CLUTTER_ACTOR(priv->home_blur),
                         "HdRenderManager:home_blur");
  clutter_actor_set_visibility_detect(CLUTTER_ACTOR(priv->home_blur), FALSE);
  tidy_blur_group_set_use_alpha(CLUTTER_ACTOR(priv->home_blur), FALSE);
  tidy_blur_group_set_use_mirror(CLUTTER_ACTOR(priv->home_blur), TRUE);
  g_signal_connect_swapped(stage, "notify::allocation",
                           G_CALLBACK(stage_allocation_changed), priv->home_blur);
  clutter_container_add_actor(CLUTTER_CONTAINER(self),
                              CLUTTER_ACTOR(priv->home_blur));

  priv->app_top = CLUTTER_GROUP(clutter_group_new());
  clutter_actor_set_name(CLUTTER_ACTOR(priv->app_top),
                         "HdRenderManager:app_top");
  g_signal_connect_swapped(stage, "notify::allocation",
                           G_CALLBACK(stage_allocation_changed), priv->app_top);
  clutter_actor_set_visibility_detect(CLUTTER_ACTOR(priv->app_top), FALSE);
  clutter_container_add_actor(CLUTTER_CONTAINER(self),
                              CLUTTER_ACTOR(priv->app_top));

  priv->front = CLUTTER_GROUP(clutter_group_new());
  clutter_actor_set_name(CLUTTER_ACTOR(priv->front),
                         "HdRenderManager:front");
  g_signal_connect_swapped(stage, "notify::allocation",
                           G_CALLBACK(stage_allocation_changed), priv->front);
  clutter_actor_set_visibility_detect(CLUTTER_ACTOR(priv->front), FALSE);
  clutter_container_add_actor(CLUTTER_CONTAINER(self),
                              CLUTTER_ACTOR(priv->front));

  priv->blur_front = CLUTTER_GROUP(clutter_group_new());
  clutter_actor_set_name(CLUTTER_ACTOR(priv->blur_front),
                         "HdRenderManager:blur_front");
  g_signal_connect_swapped(stage, "notify::allocation",
                           G_CALLBACK(stage_allocation_changed), priv->blur_front);
  clutter_actor_set_visibility_detect(CLUTTER_ACTOR(priv->blur_front), FALSE);
  clutter_container_add_actor(CLUTTER_CONTAINER(priv->home_blur),
                              CLUTTER_ACTOR(priv->blur_front));

  /* Animation stuff */
  range_set(&priv->home_radius, 0);
  range_set(&priv->home_zoom, 1);
  range_set(&priv->home_saturation, 1);
  range_set(&priv->home_brightness, 1);
  range_set(&priv->task_nav_opacity, 0);
  range_set(&priv->task_nav_zoom, 1);

  priv->timeline_blur = clutter_timeline_new_for_duration(250);
  g_signal_connect (priv->timeline_blur, "new-frame",
                    G_CALLBACK (on_timeline_blur_new_frame), self);
  g_signal_connect (priv->timeline_blur, "completed",
                      G_CALLBACK (on_timeline_blur_completed), self);
  priv->timeline_playing = FALSE;

  g_signal_connect (self, "paint",
                      G_CALLBACK (hd_render_manager_paint_notify),
                      0);

  priv->in_set_state = FALSE;
  priv->queued_redraw = FALSE;
}

/* ------------------------------------------------------------------------- */
/* -------------------------------------------------------------  CALLBACK   */
/* ------------------------------------------------------------------------- */

/* Resize @actor to the current screen dimensions.
 * Also can be used to set @actor's initial size. */
static void
stage_allocation_changed(ClutterActor *actor, GParamSpec *unused,
                         ClutterActor *stage)
{
  clutter_actor_set_size(actor,
                         HD_COMP_MGR_SCREEN_WIDTH,
                         HD_COMP_MGR_SCREEN_HEIGHT);
}

static void
on_timeline_blur_new_frame(ClutterTimeline *timeline,
                           gint frame_num, gpointer data)
{
  HdRenderManagerPrivate *priv;
  float amt;
  gint task_opacity;

  priv = the_render_manager->priv;

  amt = frame_num / (float)clutter_timeline_get_n_frames(timeline);

  range_interpolate(&priv->home_radius, amt);
  range_interpolate(&priv->home_zoom, amt);
  range_interpolate(&priv->home_saturation, amt);
  range_interpolate(&priv->home_brightness, amt);
  range_interpolate(&priv->task_nav_opacity, amt);
  range_interpolate(&priv->task_nav_zoom, amt);

  tidy_blur_group_set_blur      (CLUTTER_ACTOR(priv->home_blur),
                                 priv->home_radius.current);
  tidy_blur_group_set_saturation(CLUTTER_ACTOR(priv->home_blur),
                                 priv->home_saturation.current);
  tidy_blur_group_set_brightness(CLUTTER_ACTOR(priv->home_blur),
                                 priv->home_brightness.current);
  tidy_blur_group_set_zoom(CLUTTER_ACTOR(priv->home_blur),
                                 priv->home_zoom.current);

  task_opacity = priv->task_nav_opacity.current*255;
  clutter_actor_set_opacity(CLUTTER_ACTOR(priv->task_nav), task_opacity);
  if (task_opacity==0)
    clutter_actor_hide(CLUTTER_ACTOR(priv->task_nav));
  else
    clutter_actor_show(CLUTTER_ACTOR(priv->task_nav));
  clutter_actor_set_scale(CLUTTER_ACTOR(priv->task_nav),
                          priv->task_nav_zoom.current,
                          priv->task_nav_zoom.current);
}

static void
on_timeline_blur_completed (ClutterTimeline *timeline, gpointer data)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  priv->timeline_playing = FALSE;
  hd_comp_mgr_set_effect_running(priv->comp_mgr, FALSE);

  g_signal_emit (the_render_manager, signals[TRANSITION_COMPLETE], 0);

  /* to trigger a change after the transition */
  hd_render_manager_sync_clutter_after();
}

/* ------------------------------------------------------------------------- */
/* -------------------------------------------------------------    PRIVATE  */
/* ------------------------------------------------------------------------- */

static
void hd_render_manager_paint_notify()
{
  /* It is not necessary to check for !@the_render_manager
   * because this signal handler is connected when it is created
   * and @the_render_manager is never taken down. */
  the_render_manager->priv->queued_redraw = FALSE;
}

static
void hd_render_manager_set_blur (HDRMBlurEnum blur)
{
  HdRenderManagerPrivate *priv;
  gboolean blur_home;
  gint zoom_task_nav = 0;
  gint zoom_home = 0;

  priv = the_render_manager->priv;

  if (priv->timeline_playing)
    {
      clutter_timeline_stop(priv->timeline_blur);
      hd_comp_mgr_set_effect_running(priv->comp_mgr, FALSE);
    }

  priv->current_blur = blur;

  range_next(&priv->home_radius, 0);
  range_next(&priv->home_saturation, 1);
  range_next(&priv->home_brightness, 1);
  range_next(&priv->home_zoom, 1);
  range_next(&priv->task_nav_opacity, 0);
  range_next(&priv->task_nav_zoom, 1);

  /* work out how much we need to zoom various things */
  zoom_task_nav += (blur & HDRM_ZOOM_FOR_LAUNCHER) ? 1 : 0;
  zoom_task_nav += (blur & HDRM_ZOOM_FOR_LAUNCHER_SUBMENU) ? 1 : 0;
  zoom_home += (blur & HDRM_ZOOM_FOR_LAUNCHER) ? 1 : 0;
  zoom_home += (blur & HDRM_ZOOM_FOR_LAUNCHER_SUBMENU) ? 1 : 0;
  zoom_home += (blur & HDRM_ZOOM_FOR_TASK_NAV) ? 1 : 0;

  blur_home = blur & (HDRM_BLUR_BACKGROUND | HDRM_BLUR_HOME);

  /* FIXME: cache the settings file */
  if (blur_home)
    {
      priv->home_saturation.b =
              hd_transition_get_double("home","saturation", 1);
      priv->home_brightness.b =
              hd_transition_get_double("home","brightness", 1);
      priv->home_radius.b =
              hd_transition_get_double("home",
                  (zoom_home)?"radius_more":"radius", 8);
    }

  if (zoom_home)
    {
      priv->home_zoom.b =
              hd_transition_get_double("home", "zoom", 1);
      priv->home_zoom.b = 1 - (1-priv->home_zoom.b)*zoom_home;
    }

  if (blur & HDRM_SHOW_TASK_NAV)
    {
      priv->task_nav_opacity.b = 1;
    }
  if (zoom_task_nav)
    {
      priv->task_nav_zoom.b =
              hd_transition_get_double("task_nav", "zoom", 1);
      priv->task_nav_zoom.b = 1 - (1-priv->task_nav_zoom.b)*zoom_task_nav;
    }

  /* no point animating if everything is already right */
  if (range_equal(&priv->home_radius) &&
      range_equal(&priv->home_saturation) &&
      range_equal(&priv->home_brightness) &&
      range_equal(&priv->home_zoom) &&
      range_equal(&priv->task_nav_opacity) &&
      range_equal(&priv->task_nav_zoom))
    {
      hd_render_manager_sync_clutter_after();
      return;
    }

  hd_comp_mgr_set_effect_running(priv->comp_mgr, TRUE);
  /* Set duration here so we reload from the file every time */
  clutter_timeline_set_duration(priv->timeline_blur,
      hd_transition_get_int("blur", "duration", 250));
  clutter_timeline_start(priv->timeline_blur);
  priv->timeline_playing = TRUE;
}

static void
hd_render_manager_set_input_viewport()
{
  ClutterGeometry geom[HDRM_BUTTON_COUNT + 1];
  int geom_count = 0;
  HdRenderManagerPrivate *priv = the_render_manager->priv;
  gboolean app_mode = STATE_IS_APP(priv->state);

  if (!STATE_NEED_GRAB(priv->state))
    {
      gint i;
      /* Now look at what buttons we have showing, and add each visible button X
       * to the X input viewport. We unfortunately have to ignore
       * HDRM_BUTTON_BACK in app mode, because matchbox wants to pick them up
       * from X */
      for (i = 1; i <= HDRM_BUTTON_COUNT; i++)
        {
          ClutterActor *button;
          button = hd_render_manager_get_button((HDRMButtonEnum)i);
          if (button &&
              CLUTTER_ACTOR_IS_VISIBLE(button) &&
              CLUTTER_ACTOR_IS_VISIBLE(clutter_actor_get_parent(button)) &&
              (CLUTTER_ACTOR_IS_REACTIVE(button)) &&
              (i!=HDRM_BUTTON_BACK || !app_mode))
            {
              clutter_actor_get_geometry (button, &geom[geom_count]);
              geom_count++;
            }
        }

      /* Block status area?  If so refer to the client geometry,
       * because we might be right after a place_titlebar_elements()
       * which could just have moved it. */
      if ((STATE_IS_PORTRAIT (priv->state) && priv->status_area
           && CLUTTER_ACTOR_IS_VISIBLE (priv->status_area))
	  /* also in the case of "dialog blur": */
	  || (priv->state == HDRM_STATE_APP
              && priv->status_area
              && CLUTTER_ACTOR_IS_VISIBLE (priv->status_area)
              /* FIXME: the following check does not work when there are
               * two levels of dialogs */
              && (priv->current_blur == HDRM_BLUR_BACKGROUND ||
                  priv->current_blur == HDRM_BLUR_HOME)))
        {
          g_assert(priv->status_area_client);
          const MBGeometry *src = &priv->status_area_client->frame_geometry;
          ClutterGeometry *dst = &geom[geom_count++];
          dst->x = src->x;
          dst->y = src->y;
          dst->width  = src->width;
          dst->height = src->height;
        }
    }
  else
    {
      /* get the whole screen! */
      geom[0].x = 0;
      geom[0].y = 0;
      geom[0].width = HDRM_WIDTH;
      geom[0].height = HDRM_HEIGHT;
      geom_count = 1;
    }

  hd_comp_mgr_setup_input_viewport(priv->comp_mgr, geom, geom_count);
}

/* The syncing with clutter that is done before a transition */
static
void hd_render_manager_sync_clutter_before ()
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  HDRMButtonEnum visible_top_left = HDRM_BUTTON_NONE;
  HDRMButtonEnum visible_top_right = HDRM_BUTTON_NONE;
  HdTitleBarVisEnum btn_state = hd_title_bar_get_state(priv->title_bar) &
    ~(HDTB_VIS_BTN_LEFT_MASK | HDTB_VIS_FULL_WIDTH | HDTB_VIS_BTN_RIGHT_MASK);

  switch (priv->state)
    {
      case HDRM_STATE_UNDEFINED:
        g_error("%s: NEVER supposed to be in HDRM_STATE_UNDEFINED", __func__);
	return;
      case HDRM_STATE_HOME:
        if (hd_task_navigator_is_empty())
          visible_top_left = HDRM_BUTTON_LAUNCHER;
        else
          visible_top_left = HDRM_BUTTON_TASK_NAV;
      case HDRM_STATE_HOME_PORTRAIT: /* Fallen truth */
        visible_top_right = HDRM_BUTTON_NONE;
        clutter_actor_show(CLUTTER_ACTOR(priv->home));
        hd_render_manager_set_blur(HDRM_BLUR_NONE);
        hd_home_update_layout (priv->home);
        break;
      case HDRM_STATE_HOME_EDIT:
      case HDRM_STATE_HOME_EDIT_DLG:
        visible_top_left = HDRM_BUTTON_NONE;
        visible_top_right = HDRM_BUTTON_NONE;
        clutter_actor_show(CLUTTER_ACTOR(priv->home));
        hd_render_manager_set_blur(HDRM_BLUR_HOME);
        hd_home_update_layout (priv->home);
        break;
      case HDRM_STATE_APP:
        visible_top_left = HDRM_BUTTON_TASK_NAV;
        visible_top_right = HDRM_BUTTON_NONE;
        /* Fall through */
      case HDRM_STATE_APP_PORTRAIT:
        clutter_actor_hide(CLUTTER_ACTOR(priv->home));
        hd_render_manager_set_blur(HDRM_BLUR_NONE);
        break;
      case HDRM_STATE_TASK_NAV:
        visible_top_left = HDRM_BUTTON_LAUNCHER;
        visible_top_right = HDRM_BUTTON_NONE;
        clutter_actor_show(CLUTTER_ACTOR(priv->home));
        hd_render_manager_set_blur(HDRM_BLUR_HOME |
                                   HDRM_ZOOM_FOR_TASK_NAV |
                                   HDRM_SHOW_TASK_NAV);
        break;
      case HDRM_STATE_LAUNCHER:
        visible_top_left = HDRM_BUTTON_NONE;
        visible_top_right = HDRM_BUTTON_BACK;
        clutter_actor_show(CLUTTER_ACTOR(priv->home));
        hd_render_manager_set_blur(
            HDRM_BLUR_HOME |
            HDRM_ZOOM_FOR_LAUNCHER |
            ((priv->previous_state==HDRM_STATE_TASK_NAV)?
                HDRM_ZOOM_FOR_TASK_NAV : 0) );
        break;
    }

  clutter_actor_show(CLUTTER_ACTOR(priv->home_blur));
  clutter_actor_show(CLUTTER_ACTOR(priv->app_top));
  clutter_actor_show(CLUTTER_ACTOR(priv->front));
  clutter_actor_raise_top(CLUTTER_ACTOR(priv->app_top));
  clutter_actor_raise_top(CLUTTER_ACTOR(priv->front));

  if (STATE_SHOW_OPERATOR(priv->state))
    clutter_actor_show(priv->operator);
  else
    clutter_actor_hide(priv->operator);

  if (priv->status_area)
    {
      if (STATE_SHOW_STATUS_AREA(priv->state))
        {
          clutter_actor_show(priv->status_area);
          clutter_actor_raise_top(priv->status_area);
        }
      else
        clutter_actor_hide(priv->status_area);
    }

  /* Set button state */
  switch (visible_top_left)
  {
    case HDRM_BUTTON_NONE:
      break;
    case HDRM_BUTTON_LAUNCHER:
      btn_state |= HDTB_VIS_BTN_LAUNCHER;
      break;
    case HDRM_BUTTON_TASK_NAV:
      btn_state |= HDTB_VIS_BTN_SWITCHER;
      break;
    default:
      g_warning("%s: Invalid button %d in top-left",
          __FUNCTION__, visible_top_left);
  }
  switch (visible_top_right)
  {
    case HDRM_BUTTON_NONE:
      break;
    case HDRM_BUTTON_BACK:
      btn_state |= HDTB_VIS_BTN_BACK;
      break;
    default:
      g_warning("%s: Invalid button %d in top-right",
          __FUNCTION__, visible_top_right);
  }

  if (priv->status_menu)
    clutter_actor_raise_top(CLUTTER_ACTOR(priv->status_menu));

  if (!STATE_BLUR_BUTTONS(priv->state) &&
      clutter_actor_get_parent(CLUTTER_ACTOR(priv->blur_front)) !=
              CLUTTER_ACTOR(the_render_manager))
    {
      /* raise the blur_front out of the blur group so we can still
       * see it unblurred */
      clutter_actor_reparent(CLUTTER_ACTOR(priv->blur_front),
                             CLUTTER_ACTOR(the_render_manager));
      /* lower this below app_top */
      clutter_actor_lower(CLUTTER_ACTOR(priv->blur_front),
                          CLUTTER_ACTOR(priv->app_top));
      hd_render_manager_blurred_changed();
    }

  hd_title_bar_set_state(priv->title_bar, btn_state);
  hd_render_manager_place_titlebar_elements();

  /* update our fixed title bar at the top of the screen */
  hd_title_bar_update(priv->title_bar, MB_WM_COMP_MGR(priv->comp_mgr));

  /* Make sure we hide the edit button if it's not required */
  if (priv->state != HDRM_STATE_HOME)
    hd_home_hide_edit_button(priv->home);

  /* Now look at what buttons we have showing, and add each visible button X
   * to the X input viewport */
  hd_render_manager_set_input_viewport();

  /* as soon as we start a transition, set out left-hand button to be
   * not pressed (used when home->switcher causes change of button style) */
  hd_title_bar_left_pressed(priv->title_bar, FALSE);
}

/* The syncing with clutter that is done after a transition ends */
static
void hd_render_manager_sync_clutter_after ()
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  if (STATE_BLUR_BUTTONS(priv->state) &&
      clutter_actor_get_parent(CLUTTER_ACTOR(priv->blur_front)) !=
                                CLUTTER_ACTOR(priv->home_blur))
    {
      /* raise the blur_front to the top of the home_blur group so
       * we still see the apps */
      clutter_actor_reparent(CLUTTER_ACTOR(priv->blur_front),
                             CLUTTER_ACTOR(priv->home_blur));
      hd_render_manager_blurred_changed();
    }

  /* If we've gone back to the app state, make
   * sure we blur the right things (fix problem where going from
   * task launcher->app breaks blur)
   */
  if (STATE_IS_APP(priv->state))
    hd_render_manager_update_blur_state(0);

  /* The launcher transition should hide the launcher, so we shouldn't
   * need this.
  if (priv->state != HDRM_STATE_LAUNCHER)
    clutter_actor_hide(CLUTTER_ACTOR(priv->launcher));*/
}

/* ------------------------------------------------------------------------- */
/* -------------------------------------------------------------    PUBLIC   */
/* ------------------------------------------------------------------------- */

void hd_render_manager_stop_transition()
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  if (priv->timeline_playing)
    {
      guint frames;
      clutter_timeline_stop(priv->timeline_blur);
      frames = clutter_timeline_get_n_frames(priv->timeline_blur);
      on_timeline_blur_new_frame(priv->timeline_blur, frames, the_render_manager);
      on_timeline_blur_completed(priv->timeline_blur, the_render_manager);
    }

  hd_launcher_transition_stop();
}

void hd_render_manager_add_to_front_group (ClutterActor *item)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  clutter_actor_reparent(item, CLUTTER_ACTOR(priv->front));
}

void hd_render_manager_set_status_area (ClutterActor *item)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  if (priv->status_area)
    {
      g_object_unref(priv->status_area);
    }

  if (item)
    {
      MBWMCompMgrClient *cc;

      /*
       * Make the status area actor reactive.  Normally, this has no effect
       * (ie. when the status area region is not in our input viewport).
       * When it is in the viewport we put it there specifically to block
       * access to the status menu.  If we don't make it reactive the click
       * goes through to the background.  So prevent it.
       */
      cc = g_object_get_data(G_OBJECT(item), "HD-MBWMCompMgrClutterClient");
      priv->status_area_client = cc->wm_client;
      priv->status_area = g_object_ref(item);
      clutter_actor_reparent(priv->status_area,
          CLUTTER_ACTOR(hd_title_bar_get_foreground_group(priv->title_bar)));
      clutter_actor_set_reactive(priv->status_area, TRUE);
      g_signal_connect(item, "notify::allocation",
                       G_CALLBACK(hd_render_manager_place_titlebar_elements),
                       NULL);
    }
  else
    {
      priv->status_area = NULL;
      priv->status_area_client = NULL;
    }

  hd_render_manager_place_titlebar_elements();
}

void hd_render_manager_set_status_menu (ClutterActor *item)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  if (priv->status_menu)
    {
      g_object_unref(priv->status_menu);
    }

  if (item)
    {
      priv->status_menu = g_object_ref(item);
      clutter_actor_reparent(priv->status_menu, CLUTTER_ACTOR(priv->front));
      clutter_actor_raise_top(CLUTTER_ACTOR(priv->status_menu));
    }
  else
    priv->status_menu = NULL;
}

void hd_render_manager_set_operator (ClutterActor *item)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  if (priv->operator)
    g_object_unref(priv->operator);
  priv->operator = CLUTTER_ACTOR(g_object_ref(item));
}

void hd_render_manager_set_button (HDRMButtonEnum btn,
                                   ClutterActor *item)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  switch (btn)
    {
      case HDRM_BUTTON_TASK_NAV:
        g_assert(!priv->button_task_nav);
        priv->button_task_nav = CLUTTER_ACTOR(g_object_ref(item));
        return; /* Don't reparent, it's fine where it is. */
      case HDRM_BUTTON_LAUNCHER:
        g_assert(!priv->button_launcher);
        priv->button_launcher = CLUTTER_ACTOR(g_object_ref(item));
        return; /* Likewise */
      case HDRM_BUTTON_BACK:
        g_assert(!priv->button_back);
        priv->button_back = CLUTTER_ACTOR(g_object_ref(item));
        break;
      case HDRM_BUTTON_EDIT:
        g_warning("%s: edit button must be set at creation time!", __FUNCTION__);
	g_assert(FALSE);
        break;
      default:
        g_warning("%s: Invalid Enum %d", __FUNCTION__, btn);
	g_assert(FALSE);
    }
}

ClutterActor *hd_render_manager_get_button(HDRMButtonEnum button)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  switch (button)
    {
      case HDRM_BUTTON_TASK_NAV:
        return priv->button_task_nav;
      case HDRM_BUTTON_LAUNCHER:
        return priv->button_launcher;
      case HDRM_BUTTON_BACK:
        return priv->button_back;
      case HDRM_BUTTON_EDIT:
        return priv->button_edit;
      default:
        g_warning("%s: Invalid Enum %d", __FUNCTION__, button);
	g_assert(FALSE);
    }
  return 0;
}

ClutterActor *hd_render_manager_get_title_bar(void)
{
  return CLUTTER_ACTOR(the_render_manager->priv->title_bar);
}

ClutterActor *hd_render_manager_get_status_area(void)
{
  return CLUTTER_ACTOR(the_render_manager->priv->status_area);
}

void hd_render_manager_set_visible(HDRMButtonEnum button, gboolean visible)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  switch (button)
  {
    case HDRM_BUTTON_EDIT:
      if (visible == CLUTTER_ACTOR_IS_VISIBLE(priv->button_edit))
        return;
      if (visible)
        clutter_actor_show(priv->button_edit);
      else
        clutter_actor_hide(priv->button_edit);
      /* we need this so we can set up the X input area */
      hd_render_manager_set_input_viewport();
      break;
    default:
      g_warning("%s: Not supposed to set visibility for %d",
                __FUNCTION__, button);
  }
}

gboolean hd_render_manager_get_visible(HDRMButtonEnum button)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  switch (button)
  {
    case HDRM_BUTTON_EDIT:
      return CLUTTER_ACTOR_IS_VISIBLE(priv->button_edit);
    case HDRM_BUTTON_TASK_NAV:
      return CLUTTER_ACTOR_IS_VISIBLE(priv->button_task_nav);
    case HDRM_BUTTON_LAUNCHER:
      return CLUTTER_ACTOR_IS_VISIBLE(priv->button_launcher);
    default:
      g_warning("%s: Not supposed to be asking for visibility of %d",
                __FUNCTION__, button);
  }
  return FALSE;
}

/* FIXME: this should not be exposed */
ClutterContainer *hd_render_manager_get_front_group(void)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;
  return CLUTTER_CONTAINER(priv->front);
}

/* #ClutterEffectCompleteFunc for hd_task_navigator_zoom_out(). */
static void zoom_out_completed(ClutterActor *actor,
                               MBWMCompMgrClutterClient *cmgrcc)
{
  mb_wm_object_unref(MB_WM_OBJECT(cmgrcc));
}

void hd_render_manager_set_state(HDRMStateEnum state)
{
  HdRenderManagerPrivate *priv;
  MBWMCompMgr          *cmgr;
  MBWindowManager      *wm;

  priv = the_render_manager->priv;
  cmgr = MB_WM_COMP_MGR (priv->comp_mgr);

  g_debug("%s: STATE %s -> STATE %s", __FUNCTION__,
      hd_render_manager_state_str(priv->state),
      hd_render_manager_state_str(state));

  if (!priv->comp_mgr)
  {
    g_warning("%s: HdCompMgr not defined", __FUNCTION__);
    return;
  }
  wm = cmgr->wm;

  if (priv->in_set_state)
      {
        g_warning("%s: State change ignored as already in "
                  "hd_render_manager_set_state", __FUNCTION__);
        return;
      }
  priv->in_set_state = TRUE;

  if (state != priv->state)
    {
      ClutterActor *home_front;
      HDRMStateEnum oldstate = priv->state;
      priv->previous_state = priv->state;
      priv->state = state;

      /* Goto HOME instead of an empty swither.  This way the caller
       * needn't care whether the switcher is empty, we'll do what
       * it meant. */
      if (state == HDRM_STATE_TASK_NAV && hd_task_navigator_is_empty())
        {
          state = priv->state = HDRM_STATE_HOME;
          g_debug("you must have meant STATE %s -> STATE %s",
                  hd_render_manager_state_str(oldstate),
                  hd_render_manager_state_str(state));
        }

      /* Enter or leave the task switcher. */
      if (STATE_NEED_TASK_NAV (state))
        {
          /* Zoom out if possible.  Otherwise if not coming from launcher
           * scroll it back to the top. */
          if (STATE_IS_APP(oldstate))
            {
              ClutterActor *actor;
              MBWindowManagerClient *mbwmc;
              MBWMCompMgrClutterClient *cmgrcc;

              /* This beautiful code seems to survive everything. */
              if ((mbwmc = mb_wm_get_visible_main_client(wm)) &&
                  (cmgrcc = MB_WM_COMP_MGR_CLUTTER_CLIENT(mbwmc->cm_client)) &&
                  (actor = mb_wm_comp_mgr_clutter_client_get_actor(cmgrcc)) &&
                  CLUTTER_ACTOR_IS_VISIBLE(actor) &&
                  hd_task_navigator_has_window(priv->task_nav, actor))
                {
                  /* Make the tasw fully opaque as it might have been made
                   * transparent while exiting it. */
                  clutter_actor_set_opacity(CLUTTER_ACTOR(priv->task_nav), 255);
                  range_set(&priv->task_nav_opacity, 1);

                  /* Make sure @cmgrcc stays around as long as needed. */
                  mb_wm_object_ref (MB_WM_OBJECT (cmgrcc));
                  hd_task_navigator_zoom_out(priv->task_nav, actor,
                          (ClutterEffectCompleteFunc)zoom_out_completed,
                          cmgrcc);
                }
            }
          else if (oldstate != HDRM_STATE_LAUNCHER)
            hd_task_navigator_scroll_back(priv->task_nav);
        }
      if (STATE_ONE_OF(state | oldstate, HDRM_STATE_TASK_NAV))
        /* Stop breathing the Tasks button when entering/leaving the switcher. */
        hd_title_bar_set_switcher_pulse(priv->title_bar, FALSE);

      /* Enter/leave the launcher. */
      if (state == HDRM_STATE_LAUNCHER)
        hd_launcher_show();
      if (oldstate == HDRM_STATE_LAUNCHER)
        hd_launcher_hide();

      /* Reset CURRENT_APP_WIN when entering tasw/talu. */
      if (STATE_ONE_OF(state, HDRM_STATE_TASK_NAV | HDRM_STATE_LAUNCHER)
          && !STATE_ONE_OF(oldstate, HDRM_STATE_TASK_NAV | HDRM_STATE_LAUNCHER))
        hd_wm_current_app_is (wm, 0);

      /* Move the applets out to the front. */
      home_front = hd_home_get_front (priv->home);
      if (STATE_HOME_FRONT (state))
        {
          if (clutter_actor_get_parent(home_front) !=
              CLUTTER_ACTOR (priv->blur_front))
            {
              clutter_actor_reparent(home_front, CLUTTER_ACTOR (priv->blur_front));
              hd_render_manager_blurred_changed();
            }
          clutter_actor_lower_bottom (home_front);
        }
      else if (clutter_actor_get_parent(home_front) !=
               CLUTTER_ACTOR (priv->home))
        {
          clutter_actor_reparent(home_front, CLUTTER_ACTOR (priv->home));
          hd_render_manager_blurred_changed();
        }

      /* Hide/show applets.  Must be be done after reparenting @home_front
       * because clutter_actor_reparent() shows the actor. */
      if (STATE_SHOW_APPLETS (state))
        clutter_actor_show (home_front);
      else
        clutter_actor_hide (home_front);

      if (STATE_NEED_DESKTOP(state) != STATE_NEED_DESKTOP(oldstate))
        mb_wm_handle_show_desktop(wm, STATE_NEED_DESKTOP(state));

      /* we always need to restack here */
      /*hd_comp_mgr_restack(MB_WM_COMP_MGR(priv->comp_mgr));*/
      /* then why is it commented out? */

      /* Divert state change if going to some portrait-capable mode.
       * Allow for APP_PORTRAIT <=> HOME_PORTRAIT too. */
      if ((   (oldstate != HDRM_STATE_APP_PORTRAIT  && state == HDRM_STATE_APP)
           || (oldstate != HDRM_STATE_HOME_PORTRAIT && state == HDRM_STATE_HOME))
          && hd_comp_mgr_should_be_portrait (priv->comp_mgr))
        {
          priv->in_set_state = FALSE;
          hd_render_manager_set_state (state == HDRM_STATE_APP
                                       ? HDRM_STATE_APP_PORTRAIT
                                       : HDRM_STATE_HOME_PORTRAIT);
          return;
        }

      hd_render_manager_sync_clutter_before();

      /* Switch between portrait <=> landscape modes. */
      if (!!STATE_IS_PORTRAIT (oldstate) == !!STATE_IS_PORTRAIT (state))
        /* Don't change orientation. */;
      else if (STATE_IS_PORTRAIT (state))
        hd_util_change_screen_orientation (wm, TRUE);
      else if (STATE_IS_PORTRAIT (oldstate))
        hd_util_change_screen_orientation (wm, FALSE);

      /* Signal the state has changed. */
      g_object_notify (G_OBJECT (the_render_manager), "state");
    }
  priv->in_set_state = FALSE;
}

/* Upgrade the current state to portrait. */
void hd_render_manager_set_state_portrait (void)
{
  g_assert (STATE_IS_PORTRAIT_CAPABLE (the_render_manager->priv->state));
  if (the_render_manager->priv->state == HDRM_STATE_APP)
    hd_render_manager_set_state (HDRM_STATE_APP_PORTRAIT);
  else
    hd_render_manager_set_state (HDRM_STATE_HOME_PORTRAIT);
}

/* ...and the opposit. */
void hd_render_manager_set_state_unportrait (void)
{
  g_assert (STATE_IS_PORTRAIT (the_render_manager->priv->state));
  if (the_render_manager->priv->state == HDRM_STATE_APP_PORTRAIT)
    hd_render_manager_set_state (HDRM_STATE_APP);
  else
    hd_render_manager_set_state (HDRM_STATE_HOME);
}

/* Returns whether set_state() is in progress. */
gboolean hd_render_manager_is_changing_state(void)
{
  return the_render_manager->priv->in_set_state;
}

HDRMStateEnum  hd_render_manager_get_state()
{
  if (!the_render_manager)
    return HDRM_STATE_UNDEFINED;
  return the_render_manager->priv->state;
}

static const char *hd_render_manager_state_str(HDRMStateEnum state)
{
  switch (state)
  {
    case HDRM_STATE_UNDEFINED : return "HDRM_STATE_UNDEFINED";
    case HDRM_STATE_HOME : return "HDRM_STATE_HOME";
    case HDRM_STATE_HOME_EDIT : return "HDRM_STATE_HOME_EDIT";
    case HDRM_STATE_HOME_EDIT_DLG : return "HDRM_STATE_HOME_EDIT_DLG";
    case HDRM_STATE_HOME_PORTRAIT : return "HDRM_STATE_HOME_PORTRAIT";
    case HDRM_STATE_APP : return "HDRM_STATE_APP";
    case HDRM_STATE_APP_PORTRAIT: return "HDRM_STATE_APP_PORTRAIT";
    case HDRM_STATE_TASK_NAV : return "HDRM_STATE_TASK_NAV";
    case HDRM_STATE_LAUNCHER : return "HDRM_STATE_LAUNCHER";
  }
  return "";
}

const char *hd_render_manager_get_state_str()
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;
  return hd_render_manager_state_str(priv->state);
}

static void
hd_render_manager_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  switch (property_id)
    {
    case PROP_STATE:
      g_value_set_enum (value, hd_render_manager_get_state ());
      break;
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
hd_render_manager_set_property (GObject      *gobject,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_STATE:
      hd_render_manager_set_state (g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

gboolean hd_render_manager_in_transition(void)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;
  return clutter_timeline_is_playing(priv->timeline_blur);
}

/* Return @actor, an actor of a %HdApp to HDRM's care. */
void hd_render_manager_return_app(ClutterActor *actor)
{
  clutter_actor_reparent(actor,
                         CLUTTER_ACTOR(the_render_manager->priv->home_blur));
  clutter_actor_lower_bottom(actor);
  clutter_actor_hide(actor);
}

/* Same for dialogs. */
void hd_render_manager_return_dialog(ClutterActor *actor)
{
  clutter_actor_reparent(actor,
                         CLUTTER_ACTOR(the_render_manager->priv->app_top));
  clutter_actor_hide (actor);
}

/*
 * Clip the offscreen parts of a @geo, ensuring that it doesn't have negative
 * (x, y) coordinates.  Returns %FALSE if it's completely offscreen, meaning
 * you can ignore it.  NOTE that only the -x and the -y halfplanes are
 * considered offscreen in this context.
 */
static gboolean
hd_render_manager_clip_geo(ClutterGeometry *geo)
{
  if (geo->x < 0)
    {
      if (-geo->x >= geo->width)
        return FALSE;
      geo->width += geo->x;
      geo->x = 0;
    }

  if (geo->y < 0)
    {
      if (-geo->y >= geo->height)
        return FALSE;
      geo->height += geo->y;
      geo->y = 0;
    }

  return TRUE;
}

/* Called to restack the windows in the way we use for rendering... */
void hd_render_manager_restack()
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;
  MBWindowManager *wm;
  MBWindowManagerClient *c;
  gboolean past_desktop = FALSE;
  gboolean blur_changed = FALSE;
  gint i;
  GList *previous_home_blur = 0;


  wm = MB_WM_COMP_MGR(priv->comp_mgr)->wm;
  /* Add all actors currently in the home_blur group */

  for (i=0;i<clutter_group_get_n_children(CLUTTER_GROUP(priv->home_blur));i++)
    previous_home_blur = g_list_prepend(previous_home_blur,
        clutter_group_get_nth_child(CLUTTER_GROUP(priv->home_blur), i));

  /* Order and choose which window actors will be visible */
  for (c = wm->stack_bottom; c; c = c->stacked_above)
    {
      past_desktop |= (wm->desktop == c);
      /* If we're past the desktop then add us to the stuff that will be
       * visible */

      if (c->cm_client && c->desktop >= 0) /* FIXME: should check against
					      current desktop? */
        {
          ClutterActor *actor = 0;
          ClutterActor *desktop = mb_wm_comp_mgr_clutter_get_nth_desktop(
              MB_WM_COMP_MGR_CLUTTER(priv->comp_mgr), c->desktop);
          actor = mb_wm_comp_mgr_clutter_client_get_actor(
              MB_WM_COMP_MGR_CLUTTER_CLIENT(c->cm_client));
          if (actor)
            {
              ClutterActor *parent = clutter_actor_get_parent(actor);
              if (past_desktop)
                {
                  /* if we want to render this, add it. we need to be careful
                   * not to pull applets or other things out from where they
                   * were */
                  if (parent == CLUTTER_ACTOR(desktop) ||
                      parent == CLUTTER_ACTOR(priv->app_top))
                    {
                      clutter_actor_reparent(actor,
                                             CLUTTER_ACTOR(priv->home_blur));
                    }
#if STACKING_DEBUG
                  else
                    g_debug("%s NOT MOVED - OWNED BY %s",
                        clutter_actor_get_name(actor)?clutter_actor_get_name(actor):"?",
                        clutter_actor_get_name(parent)?clutter_actor_get_name(parent):"?");
#endif /*STACKING_DEBUG*/
                  if (parent)
		    clutter_actor_raise_top(actor);
#if STACKING_DEBUG
                  else
                    g_debug("%s DOES NOT HAVE A PARENT",
                        clutter_actor_get_name(actor)?clutter_actor_get_name(actor):"?");
#endif /*STACKING_DEBUG*/
                }
              else
                {
                  /* else we put it back into the arena */
                  if (parent == CLUTTER_ACTOR(priv->home_blur) ||
                      parent == CLUTTER_ACTOR(priv->app_top))
                    clutter_actor_reparent(actor, desktop);
                }
            }
        }
    }

  /* Now start at the top and put actors in the non-blurred group
   * until we find one that fills the screen. If we didn't find
   * any that filled the screen then add the window that does. */
  {
    gint i, n_elements;

    n_elements = clutter_group_get_n_children(CLUTTER_GROUP(priv->home_blur));
    for (i=n_elements-1;i>=0;i--)
      {
        ClutterActor *child =
          clutter_group_get_nth_child(CLUTTER_GROUP(priv->home_blur), i);


        if (child != CLUTTER_ACTOR(priv->home) &&
            child != CLUTTER_ACTOR(priv->blur_front))
          {
            ClutterGeometry geo;
            gboolean maximized;

            clutter_actor_get_geometry(child, &geo);
            if (!hd_render_manager_clip_geo(&geo))
              /* It's neiteher maximized nor @app_top, it doesn't exist. */
              continue;
            maximized = HD_COMP_MGR_CLIENT_IS_MAXIMIZED(geo);
            /* Maximized stuff should never be blurred (unless there
             * is nothing else) */
            if (!maximized)
              {
                clutter_actor_reparent(child, CLUTTER_ACTOR(priv->app_top));
                clutter_actor_lower_bottom(child);
                clutter_actor_show(child); /* because it is in app-top, vis
                                              check does not get applied */
              }
            /* If this is maximized, or in dialog's position, don't
             * blur anything after */
            if (maximized || (
                geo.width == HD_COMP_MGR_SCREEN_WIDTH &&
                geo.y + geo.height == HD_COMP_MGR_SCREEN_HEIGHT
                ))
              break;

          }
      }
  }

  if (clutter_actor_get_parent(CLUTTER_ACTOR(priv->blur_front)) ==
                               CLUTTER_ACTOR(priv->home_blur))
    clutter_actor_raise_top(CLUTTER_ACTOR(priv->blur_front));

  /* And for speed of rendering, work out what is visible and what
   * isn't, and hide anything that would be rendered over by another app */
  hd_render_manager_set_visibilities();

  /* now compare the contents of home_blur to see if the blur group has
   * actually changed... */
  if (g_list_length(previous_home_blur) ==
      clutter_group_get_n_children(CLUTTER_GROUP(priv->home_blur)))
    {
      GList *it;
      for (i = 0, it = g_list_last(previous_home_blur);
           (i<clutter_group_get_n_children(CLUTTER_GROUP(priv->home_blur))) && it;
           i++, it=it->prev)
        {
          ClutterActor *child =
              clutter_group_get_nth_child(CLUTTER_GROUP(priv->home_blur), i);
          if (CLUTTER_ACTOR(it->data) != child)
            {
              //g_debug("*** RE-BLURRING *** because contents changed at pos %d", i);
              blur_changed = TRUE;
              break;
            }
        }
    }
  else
    {
      /*g_debug("*** RE-BLURRING *** because contents changed size %d -> %d",
          g_list_length(previous_home_blur),
          clutter_group_get_n_children(CLUTTER_GROUP(priv->home_blur)));*/
      blur_changed = TRUE;
    }
  g_list_free(previous_home_blur);

  /* ----------------------------- DEBUG PRINTING */
#if STACKING_DEBUG
  for (i = 0;i<clutter_group_get_n_children(CLUTTER_GROUP(priv->home_blur));i++)
    {
      ClutterActor *child =
                clutter_group_get_nth_child(CLUTTER_GROUP(priv->home_blur), i);
      const char *name = clutter_actor_get_name(child);
      g_debug("STACK[%d] %s %s", i, name?name:"?",
          CLUTTER_ACTOR_IS_VISIBLE(child)?"":"(invisible)");
    }
  for (i = 0;i<clutter_group_get_n_children(CLUTTER_GROUP(priv->app_top));i++)
      {
        ClutterActor *child =
                  clutter_group_get_nth_child(CLUTTER_GROUP(priv->app_top), i);
        const char *name = clutter_actor_get_name(child);
        g_debug("TOP[%d] %s %s", i, name?name:"?",
            CLUTTER_ACTOR_IS_VISIBLE(child)?"":"(invisible)");
      }

  for (c = wm->stack_bottom,i=0; c; c = c->stacked_above,i++)
    {
      ClutterActor *a = 0;

      if (c->cm_client)
        a = mb_wm_comp_mgr_clutter_client_get_actor(
                MB_WM_COMP_MGR_CLUTTER_CLIENT(c->cm_client));
      g_debug("WM[%d] : %s %s %s", i,
          c->name?c->name:"?",
          (a && clutter_actor_get_name(a)) ?  clutter_actor_get_name(a) : "?",
          (wm->desktop==c) ? "DESKTOP" : "");
    }
#endif /*STACKING_DEBUG*/
    /* ----------------------------- */

  /* because swapping parents doesn't appear to fire a redraw */
  if (blur_changed)
    hd_render_manager_blurred_changed();

  /* update our fixed title bar at the top of the screen */
  hd_title_bar_update(priv->title_bar, MB_WM_COMP_MGR(priv->comp_mgr));
}

void hd_render_manager_update_blur_state(MBWindowManagerClient *ignore)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;
  HDRMBlurEnum blur_flags;
  HdTitleBarVisEnum title_flags;
  MBWindowManager *wm = MB_WM_COMP_MGR(priv->comp_mgr)->wm;
  MBWindowManagerClient *c;
  gboolean blur = FALSE;
  gboolean blur_buttons = FALSE;

  /* Now look through the MBWM stack and see if we need to blur or not.
   * This happens when we have a dialog/menu in front of the main app */
  for (c=wm->stack_top;c;c=c->stacked_below)
    {
      int c_type = MB_WM_CLIENT_CLIENT_TYPE(c);
      if (hd_comp_mgr_ignore_window(c) || c==ignore)
        continue;
      if (c_type == MBWMClientTypeApp)
        {
          /* If we have a fullscreen window then the top-left button and
           * status area will not be visible - so we don't want them
           * pulled out to the front. */
          if (c->window &&
              HD_COMP_MGR_CLIENT_IS_MAXIMIZED(c->window->geometry))
            blur_buttons = TRUE;
          break;
        }
      if (c_type == MBWMClientTypeDesktop)
        break;
      if (c_type == MBWMClientTypeDialog ||
          c_type == MBWMClientTypeMenu ||
          c_type == HdWmClientTypeAppMenu ||
          c_type == HdWmClientTypeStatusMenu ||
          HD_IS_CONFIRMATION_NOTE (c))
        {
          /* If this is a dialog that is maximised, it will be put in the
           * blur group - so do NOT blur the background for this alone.
           * Also this dialog is probably the VKB, which appears *over*
           * the top-left icon - so it acts like a system modal blocker
           * and we should not attempt to display unblurred top-left buttons */
          if (HD_COMP_MGR_CLIENT_IS_MAXIMIZED(c->window->geometry))
            {
              blur_buttons = TRUE;
              break;
            }

          /*g_debug("%s: Blurring caused by window type %d, geo=%d,%d,%d,%d name '%s'",
              __FUNCTION__, c_type,
              c->window->geometry.x, c->window->geometry.y,
              c->window->geometry.width, c->window->geometry.height,
              c->name?c->name:"(null)");*/
          blur=TRUE;
          if (hd_util_client_has_modal_blocker(c))
            blur_buttons = TRUE;
        }
    }

  blur_flags = priv->current_blur;
  title_flags = hd_title_bar_get_state(priv->title_bar);

  if (blur)
    blur_flags = blur_flags | HDRM_BLUR_BACKGROUND;
  else
    blur_flags = blur_flags & ~HDRM_BLUR_BACKGROUND;

  if (blur && !blur_buttons)
    title_flags |= HDTB_VIS_FOREGROUND;
  else
    title_flags &= ~HDTB_VIS_FOREGROUND;

  if (blur_flags !=  priv->current_blur)
    hd_render_manager_set_blur(blur_flags);

  hd_title_bar_set_state(priv->title_bar, title_flags);

  /* TODO Things break if we don't restack, but why? */
  hd_comp_mgr_sync_stacking (priv->comp_mgr);
}

/* This is called when we are in the launcher subview so that we can blur and
 * darken the background even more */
void hd_render_manager_set_launcher_subview(gboolean subview)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;

  /*g_debug("%s: %s", __FUNCTION__, subview ? "SUBVIEW":"MAIN");*/
  if (subview)
    hd_render_manager_set_blur(priv->current_blur |
        HDRM_ZOOM_FOR_LAUNCHER_SUBMENU);
  else
    hd_render_manager_set_blur(priv->current_blur &
        ~HDRM_ZOOM_FOR_LAUNCHER_SUBMENU);
}

/* Sets whether any of the buttons will actually be set to do anything */
void hd_render_manager_set_reactive(gboolean reactive)
{
  gint i;

  for (i = 1; i <= HDRM_BUTTON_COUNT; ++i)
    {
      ClutterActor *button = hd_render_manager_get_button((HDRMButtonEnum)i);
      clutter_actor_set_reactive(button, reactive);
    }

  hd_home_set_reactive (the_render_manager->priv->home, reactive);
}

/* Work out if rect is visible after being clipped to avoid every
 * rect in blockers */
static gboolean
hd_render_manager_is_visible(GList *blockers,
                             ClutterGeometry rect)
{
  if (!hd_render_manager_clip_geo(&rect))
    return FALSE;

  /* clip for every block */
  for (; blockers; blockers = blockers->next)
    {
      ClutterGeometry blocker = *(ClutterGeometry*)blockers->data;
      gint rect_b, blocker_b;

      VISIBILITY ("RECT %dx%d%+d%+d BLOCKER %dx%d%+d%+d",
                  MBWM_GEOMETRY(&rect), MBWM_GEOMETRY(&blocker));

      /*
       * If rect does not fit inside blocker in the X axis...
       * Beware that ClutterGeometry.x and .y are signed, while .width
       * and .height are unsigned and the type propagation rules of C
       * makes sure we'll have trouble because the result is unsigned.
       * It's only significant, though when you compare signeds and
       * unsigneds.
       */
      if (!(blocker.x <= rect.x &&
            rect.x+(gint)rect.width <= blocker.x+(gint)blocker.width))
        continue;

      /* Because most windows will go edge->edge, just do a very simplistic
       * clipping in the Y direction */
      rect_b    = rect.y + rect.height;
      blocker_b = blocker.y + blocker.height;

      if (rect.y < blocker.y)
        { /* top of rect is above blocker */
          if (rect_b < blocker.y)
            /* rect is above blocker */
            continue;
          if (rect_b < blocker_b)
            /* rect is half above blocker, clip the rest */
            rect.height -= rect_b - blocker.y;
          else
            { /* rect is split into two pieces by blocker */
              rect.height = blocker.y - rect.y;
              if (hd_render_manager_is_visible(blockers, rect))
                /* upper half is visible */
                return TRUE;

              /* continue with the lower half */
              rect.y = blocker_b;
              rect.height = rect_b - blocker_b;
            }
        }
      else if (rect.y < blocker_b)
        { /* top of rect is inside blocker */
          if (rect_b < blocker_b)
            /* rect is confined in blocker */
            return FALSE;
          else
            { /* rect is half below blocker, clip the rest */
              rect.height -= blocker_b - rect.y;
              rect.y       = blocker_b;
            }
        }
      else
        /* rect is completely below blocker */;

      if (blocker.x <= rect.x &&
          rect.x+rect.width <= blocker.x+blocker.width)
        {
          if (blocker.y <= rect.y &&
              rect.y+rect.height <= blocker.y+blocker.height)
            {
              /* If rect fits inside blocker in the Y axis,
               * it is def. not visible */
              return FALSE;
            }
          else if (rect.y < blocker.y)
            {
              /* safety - if the blocker sits in the middle of us
               * it makes 2 rects, so don't use it */
              if (blocker.y+blocker.height >= rect.y+rect.height)
                /* rect out the bottom, clip to the blocker */
                rect.height = blocker.y - rect.y;
            }
          else
            { /* rect must be out the top, clip to the blocker */
              rect.height = (rect.y + rect.height) -
                            (blocker.y + blocker.height);
              rect.y = blocker.y + blocker.height;
            }
        }
    }

  return TRUE;
}

static
MBWindowManagerClient*
hd_render_manager_get_wm_client_from_actor(ClutterActor *actor)
{
  MBWindowManager *wm;
  MBWindowManagerClient *c;

  /* first off, try and get the client from the data set in the actor */
  MBWMCompMgrClient *cc = g_object_get_data (G_OBJECT (actor),
                                             "HD-MBWMCompMgrClutterClient");
  if (cc && cc->wm_client)
    return cc->wm_client;

  /*Or search... */
  wm = MB_WM_COMP_MGR(the_render_manager->priv->comp_mgr)->wm;
  /* Order and choose which window actors will be visible */
  for (c = wm->stack_bottom; c; c = c->stacked_above)
    if (c->cm_client) {
      ClutterActor *cactor = mb_wm_comp_mgr_clutter_client_get_actor(
                               MB_WM_COMP_MGR_CLUTTER_CLIENT(c->cm_client));
      if (actor == cactor)
        return c;
    }
  return 0;
}

static
gboolean hd_render_manager_actor_opaque(ClutterActor *actor)
{
  MBWindowManager *wm;
  MBWindowManagerClient *wm_client;

  /* this is ugly and slow, but is hopefully just a fallback... */
  if (!actor || !the_render_manager->priv->comp_mgr)
    /* this check is most probably unnecessary */
    return FALSE;
  wm = MB_WM_COMP_MGR(the_render_manager->priv->comp_mgr)->wm;
  wm_client = hd_render_manager_get_wm_client_from_actor(actor);
  return wm &&
         wm_client &&
         !wm_client->is_argb32 &&
         !mb_wm_theme_is_client_shaped(wm->theme, wm_client);
}

static
void hd_render_manager_append_geo_cb(ClutterActor *actor, gpointer data)
{
  GList **list = (GList**)data;
  if (hd_render_manager_actor_opaque(actor))
    {
      ClutterGeometry *geo = g_malloc(sizeof(ClutterGeometry));
      clutter_actor_get_geometry(actor, geo);

      if (!hd_render_manager_clip_geo (geo))
        return;
      *list = g_list_prepend(*list, geo);
      VISIBILITY ("BLOCKER %dx%d%+d%+d", MBWM_GEOMETRY(geo));
    }
}

static
void hd_render_manager_set_visibilities()
{ VISIBILITY ("SET VISIBILITIES");
  HdRenderManagerPrivate *priv;
  GList *blockers = 0;
  GList *it;
  gint i, n_elements;
  ClutterGeometry fullscreen_geo = {0, 0, HDRM_WIDTH, HDRM_HEIGHT};
  MBWindowManager *wm;
  MBWindowManagerClient *c;
  gboolean has_fullscreen;

  priv = the_render_manager->priv;
  /* first append all the top elements... */
  clutter_container_foreach(CLUTTER_CONTAINER(priv->app_top),
                            hd_render_manager_append_geo_cb,
                            (gpointer)&blockers);
  /* Now check to see if the whole screen is covered, and if so
   * don't bother rendering blurring */
  if (hd_render_manager_is_visible(blockers, fullscreen_geo))
    {
      clutter_actor_show(CLUTTER_ACTOR(priv->home_blur));
    }
  else
    {
      clutter_actor_hide(CLUTTER_ACTOR(priv->home_blur));
    }
  /* Then work BACKWARDS through the other items, working out if they are
   * visible or not */
  n_elements = clutter_group_get_n_children(CLUTTER_GROUP(priv->home_blur));
  for (i=n_elements-1;i>=0;i--)
    {
      ClutterActor *child =
        clutter_group_get_nth_child(CLUTTER_GROUP(priv->home_blur), i);
      if (child != CLUTTER_ACTOR(priv->blur_front))
        {
          ClutterGeometry geo;
          clutter_actor_get_geometry(child, &geo);
          /*TEST clutter_actor_set_opacity(child, 63);*/
          VISIBILITY ("IS %p (%dx%d%+d%+d) VISIBLE?", child, MBWM_GEOMETRY(&geo));
          if (hd_render_manager_is_visible(blockers, geo))
            {
              VISIBILITY ("IS");
              clutter_actor_show(child);

              /* Add the geometry to our list of blockers and go to next... */
              if (hd_render_manager_actor_opaque(child))
                {
                  blockers = g_list_prepend(blockers, g_memdup(&geo, sizeof(geo)));
                  VISIBILITY ("MORE BLOCKER %dx%d%+d%+d", MBWM_GEOMETRY(&geo));
                }
            }
          else
            { /* Not visible, hide it unless... */
              VISIBILITY ("ISNT");
#ifdef __i386__
              /* On the device the flicker we can avoid with this check
               * upon subview->mainview transition is not visible. */
              if (!hd_transition_actor_will_go_away(child))
#endif
                clutter_actor_hide(child);
            }
        }
    }

  /* Sometimes we make a mistake because of the trasition effects.
   * One particular case is when a window stack with lots of windows
   * is dumped altogether. */
  if (STATE_NEED_DESKTOP (priv->state))
    if (!CLUTTER_ACTOR_IS_VISIBLE (CLUTTER_ACTOR (priv->home)))
      {
        g_critical ("i've got wrong the visibilities :(");
        clutter_actor_show (CLUTTER_ACTOR (priv->home));
      }

  /* now free blockers */
  it = g_list_first(blockers);
  while (it)
    {
      g_free(it->data);
      it = it->next;
    }
  g_list_free(blockers);
  blockers = 0;

  /* Do we have a fullscreen client visible? */
  has_fullscreen = FALSE;
  wm = MB_WM_COMP_MGR(priv->comp_mgr)->wm;
  for (c = wm->stack_top; c && !has_fullscreen; c = c->stacked_below)
    {
      if (c->cm_client && c->desktop >= 0) /* FIXME: should check against
                                              current desktop? */
        {
          ClutterActor *actor = mb_wm_comp_mgr_clutter_client_get_actor(
              MB_WM_COMP_MGR_CLUTTER_CLIENT(c->cm_client));
          if (actor && CLUTTER_ACTOR_IS_VISIBLE(actor))
            {
              if (c->window)
                has_fullscreen |= c->window->ewmh_state &
                  MBWMClientWindowEWMHStateFullscreen;
            }
        }
    }

  /* If we have a fullscreen something hide the blur_front
   * and move SA out of the way.  BTW blur_front is implcitly
   * shown by clutter when reparented. */
  c = priv->status_area_client;
  if (has_fullscreen)
    {
      clutter_actor_hide(CLUTTER_ACTOR(priv->blur_front));
      if (c && c->frame_geometry.y >= 0)
        { /* Move SA out of the way. */
          c->frame_geometry.y   = -c->frame_geometry.height;
          c->window->geometry.y = -c->window->geometry.height;
          mb_wm_client_geometry_mark_dirty(c);
        }
    }
  else
    {
      clutter_actor_show(CLUTTER_ACTOR(priv->blur_front));
      if (c && c->frame_geometry.y < 0)
        { /* Restore the position of SA. */
          c->frame_geometry.y = c->window->geometry.y = 0;
          mb_wm_client_geometry_mark_dirty(c);
        }
    }
  hd_render_manager_set_input_viewport();
}

void hd_render_manager_queue_delay_redraw()
{
  if (G_UNLIKELY(!the_render_manager))
    /* TODO Is it necessary to check? */
    return;
  if (!the_render_manager->priv->queued_redraw)
    { /* The flag is cleared when paint is complete. */
      the_render_manager->priv->queued_redraw = TRUE;

      /* TODO We can use clutter_stage_queue_redraw_damage()
       *       directlyif we never unset ->allow_redraw. */
      clutter_actor_queue_redraw_damage(clutter_stage_get_default());
    }
}

/* Called by hd-task-navigator when its state changes, as when notifications
 * arrive the button in the top-left may need to change */
void hd_render_manager_update()
{
  hd_render_manager_sync_clutter_before();
}

/* Returns whether @c's actor is visible in clutter sense.  If so, then
 * it most probably is visible to the user as well.  It is assumed that
 * set_visibilities() have been sorted out for the current stacking. */
gboolean hd_render_manager_is_client_visible(MBWindowManagerClient *c)
{
  ClutterActor *a;
  MBWMCompMgrClutterClient *cc;

  if (!(cc = MB_WM_COMP_MGR_CLUTTER_CLIENT(c->cm_client)))
    return FALSE;
  if (!(a  = mb_wm_comp_mgr_clutter_client_get_actor(cc)))
    return FALSE;

  /*
   * It is necessary to check the parents because sometimes
   * hd_render_manager_set_visibilities() hides the container
   * altogether.  Stage is never visible.  It is possible for
   * a client's actor to have a %NULL parent in case it was
   * never really mapped but deleted right away that time,
   * like in the case of unwanted notifications.
   */
  while (a && a != CLUTTER_ACTOR (the_render_manager))
    {
      if (!CLUTTER_ACTOR_IS_VISIBLE(a))
        return FALSE;
      a = clutter_actor_get_parent(a);
    }

  return TRUE;
}

/* Place the status area, the operator logo and the title bar,
 * depending on the visible visual elements. */
void hd_render_manager_place_titlebar_elements (void)
{
  HdRenderManagerPrivate *priv = the_render_manager->priv;
  guint x;

  x = 0;

  if (CLUTTER_ACTOR_IS_VISIBLE(priv->button_task_nav)
      || CLUTTER_ACTOR_IS_VISIBLE(priv->button_launcher))
    x += HD_COMP_MGR_TOP_LEFT_BTN_WIDTH;

  if (priv->status_area && CLUTTER_ACTOR_IS_VISIBLE(priv->status_area))
    {
      g_assert(priv->status_area_client && priv->status_area_client->window);
      if (priv->status_area_client->frame_geometry.x != x)
        {
          /* Reposition the status area. */
          MBWindowManagerClient *c = priv->status_area_client;
          c->frame_geometry.x = c->window->geometry.x = x;
          mb_wm_client_geometry_mark_dirty(c);
          x += c->window->geometry.width;
        }
      else
        x += priv->status_area_client->frame_geometry.width;
    }

  if (priv->operator && CLUTTER_ACTOR_IS_VISIBLE(priv->operator))
    /* Don't update @x since operator and app title are not shown at once. */
    clutter_actor_set_x(priv->operator, x + HD_COMP_MGR_OPERATOR_PADDING);

  if (STATE_ONE_OF(priv->state, HDRM_STATE_APP | HDRM_STATE_APP_PORTRAIT))
    /* Otherwise we don't show a title. */
    mb_adjust_dialog_title_position(MB_WM_COMP_MGR(priv->comp_mgr)->wm, x);

  hd_title_bar_update(priv->title_bar, MB_WM_COMP_MGR(priv->comp_mgr));
}

void hd_render_manager_blurred_changed()
{
  if (!the_render_manager) return;

  tidy_blur_group_set_source_changed(
      CLUTTER_ACTOR(the_render_manager->priv->home_blur));
}
