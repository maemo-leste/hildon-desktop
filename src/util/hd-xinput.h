#ifndef _HD_XINPUT_H_
#define _HD_XINPUT_H_

#include <X11/extensions/XInput.h>
#include <glib.h>
#include <gmodule.h>

typedef struct {
	gboolean is_ts;
	XDevice *dev;
} hd_xi_device;

extern int xi_motion_ev_type;
extern int xi_presence_ev_type;

extern GArray *xi_devices;

void hd_close_input_devices(Display *dpy);

void hd_enumerate_input_devices(Display *dpy);

int hd_rotate_input_devices(Display *dpy);

#endif
