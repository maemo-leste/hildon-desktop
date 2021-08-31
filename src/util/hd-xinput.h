#ifndef _HD_XINPUT_H_
#define _HD_XINPUT_H_

#include <X11/extensions/XInput.h>
#include <glib.h>
#include <gmodule.h>
#include <clutter/clutter-main.h>
#include <clutter/x11/clutter-x11.h>

void hd_close_input_devices(Display *dpy);

void hd_enumerate_input_devices(Display *dpy);

int hd_rotate_input_devices(Display *dpy);

ClutterX11FilterReturn hd_clutter_x11_event_filter(XEvent *xev, ClutterEvent *cev, gpointer data);

#endif
