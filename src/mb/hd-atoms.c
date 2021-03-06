
#include "hd-atoms.h"
#include <X11/extensions/randr.h>

void
hd_atoms_init (Display * xdpy, Atom * atoms)
{
  /*
   *   The list below *MUST* be kept in the same order as the corresponding
   *   emun in hd-atoms.h or *everything* will break.
   *   Doing it like this avoids a mass of round trips on startup.
   */

  char *atom_names[] = {
    "_HILDON_APP_KILLABLE",	/* Hildon only props */
    "_HILDON_ABLE_TO_HIBERNATE",/* alias for the above */

    "_HILDON_HOME_VIEW",
    "_HILDON_STACKABLE_WINDOW",
    "_HILDON_NON_COMPOSITED_WINDOW",

    "_HILDON_WM_WINDOW_TYPE_HOME_APPLET",
    "_HILDON_WM_WINDOW_TYPE_APP_MENU",
    "_HILDON_WM_WINDOW_TYPE_STATUS_AREA",
    "_HILDON_WM_WINDOW_TYPE_STATUS_MENU",
    "_HILDON_WM_WINDOW_TYPE_ANIMATION_ACTOR",
    "_HILDON_WM_WINDOW_TYPE_REMOTE_TEXTURE",

    "_HILDON_NOTIFICATION_TYPE",
    "_HILDON_NOTIFICATION_TYPE_BANNER",
    "_HILDON_NOTIFICATION_TYPE_INFO",
    "_HILDON_NOTIFICATION_TYPE_CONFIRMATION",
    "_HILDON_NOTIFICATION_THREAD",

    "_HILDON_INCOMING_EVENT_NOTIFICATION_DESTINATION",
    "_HILDON_INCOMING_EVENT_NOTIFICATION_MESSAGE",
    "_HILDON_INCOMING_EVENT_NOTIFICATION_SUMMARY",
    "_HILDON_INCOMING_EVENT_NOTIFICATION_AMOUNT",
    "_HILDON_INCOMING_EVENT_NOTIFICATION_TIME",
    "_HILDON_INCOMING_EVENT_NOTIFICATION_ICON",

    "_HILDON_CLIENT_MESSAGE_PAN",
    "_HILDON_CLIENT_MESSAGE_SHOW_SETTINGS",

    "_HILDON_APPLET_ID",
    "_HILDON_APPLET_SETTINGS",
    "_HILDON_APPLET_SHOW_SETTINGS",
    "_HILDON_APPLET_ON_CURRENT_DESKTOP",

    "_HILDON_WM_WINDOW_PROGRESS_INDICATOR",
    "_HILDON_WM_WINDOW_MENU_INDICATOR",

    "WM_WINDOW_ROLE",

    "_HILDON_DO_NOT_DISTURB",
    "_HILDON_DO_NOT_DISTURB_OVERRIDE",

    "UTF8_STRING",

    "_HILDON_ANIMATION_CLIENT_MESSAGE_SHOW",
    "_HILDON_ANIMATION_CLIENT_MESSAGE_POSITION",
    "_HILDON_ANIMATION_CLIENT_MESSAGE_ROTATION",
    "_HILDON_ANIMATION_CLIENT_MESSAGE_SCALE",
    "_HILDON_ANIMATION_CLIENT_MESSAGE_ANCHOR",
    "_HILDON_ANIMATION_CLIENT_MESSAGE_PARENT",
    "_HILDON_ANIMATION_CLIENT_READY",

    "_HILDON_TEXTURE_CLIENT_MESSAGE_SHM",
    "_HILDON_TEXTURE_CLIENT_MESSAGE_DAMAGE",
    "_HILDON_TEXTURE_CLIENT_MESSAGE_SHOW",
    "_HILDON_TEXTURE_CLIENT_MESSAGE_POSITION",
    "_HILDON_TEXTURE_CLIENT_MESSAGE_OFFSET",
    "_HILDON_TEXTURE_CLIENT_MESSAGE_SCALE",
    "_HILDON_TEXTURE_CLIENT_MESSAGE_PARENT",
    "_HILDON_TEXTURE_CLIENT_READY",

    "_HILDON_LOADING_SCREENSHOT",

    RR_PROPERTY_CONNECTOR_TYPE,
    "Panel",

    /* Used to delete legacy menus */
    "_GTK_DELETE_TEMPORARIES",
    /* Used to see if window has a video overlay --> can't blur it */
    "_OMAP_VIDEO_OVERLAY",
    /* Signal that we are in a rotation transition */
    "_MAEMO_ROTATION_TRANSITION",
    "_MAEMO_ROTATION_PATIENCE",
    "_MAEMO_SCREEN_SIZE",
  };

  XInternAtoms (xdpy,
		atom_names,
		_HD_ATOM_LAST,
                False,
		atoms);
}
