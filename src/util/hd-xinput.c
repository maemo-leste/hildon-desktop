#include "hd-xinput.h"

#include <string.h>
#include <stdio.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <clutter/clutter-main.h>
#include <clutter/x11/clutter-x11.h>

#define RR_Reflect_All	(RR_Reflect_X|RR_Reflect_Y)

GArray *xi_devices = NULL;
int xi_motion_ev_type = -1;
int xi_presence_ev_type = -1;

typedef struct Matrix {
	float m[9];
} Matrix;

void hd_close_input_devices(Display * dpy)
{
	for (int i = 0; i < xi_devices->len; i++) {
		hd_xi_device *xi_dev =
		    &g_array_index(xi_devices, hd_xi_device, i);
		if (xi_dev->dev) {
			XCloseDevice(dpy, xi_dev->dev);
			xi_dev->dev = NULL;
		}
	}
}

void hd_enumerate_input_devices(Display * dpy)
{
	XDeviceInfo *devinfo;
	int i, ndev;
	GArray *eclass;

	if (xi_devices) {
		hd_close_input_devices(dpy);
		g_array_free(xi_devices, TRUE);
		xi_devices = NULL;
	}

	/* get XInput DeviceMotion events */
	devinfo = XListInputDevices(dpy, &ndev);

	eclass = g_array_new(FALSE, FALSE, sizeof(XEventClass));
	xi_devices = g_array_new(FALSE, TRUE, sizeof(hd_xi_device));

	if (!devinfo)
		goto done;

	for (i = 0; i < ndev; i++) {
		XDeviceInfo info = devinfo[i];

		if (info.use != IsXPointer && info.use != IsXKeyboard) {
			XAnyClassPtr ci = info.inputclassinfo;
			int j;

			for (j = 0; j < info.num_classes; j++) {
				if (ci->class == ValuatorClass) {
					XEventClass ev_class;
					XDevice *dev =
					    XOpenDevice(dpy, info.id);
					XID id = info.id;
					XValuatorInfo *vi =
					    (XValuatorInfo *) ci;

					if (xi_devices->len <= id)
						g_array_set_size(xi_devices,
								 id + 1);

					hd_xi_device *xi_dev =
					    &g_array_index(xi_devices,
							   hd_xi_device, id);

					DeviceMotionNotify(dev,
							   xi_motion_ev_type,
							   ev_class);
					g_array_append_val(eclass, ev_class);

					xi_dev->is_ts =
					    (vi->mode & DeviceMode) == Absolute;
					xi_dev->dev = dev;
					break;
				}
				ci = (XAnyClassPtr) ((char *)ci + ci->length);
			}
		}
	}

	XFreeDeviceList(devinfo);

	if (eclass->len) {
		XSelectExtensionEvent(dpy,
				      RootWindow(dpy,
						 clutter_x11_get_default_screen
						 ()),
				      (XEventClass *) eclass->data,
				      eclass->len);
	}

 done:
	g_array_free(eclass, TRUE);
}

static int apply_matrix(Display * dpy, int deviceid, Matrix * m)
{
	Atom prop_float, prop_matrix;

	union {
		unsigned char *c;
		float *f;
	} data;
	int format_return;
	Atom type_return;
	unsigned long nitems;
	unsigned long bytes_after;

	int rc;

	prop_float = XInternAtom(dpy, "FLOAT", False);
	prop_matrix =
	    XInternAtom(dpy, "Coordinate Transformation Matrix", False);

	if (!prop_float) {
		fprintf(stderr,
			"Float atom not found. This server is too old.\n");
		return EXIT_FAILURE;
	}
	if (!prop_matrix) {
		fprintf(stderr,
			"Coordinate transformation matrix not found. This "
			"server is too old\n");
		return EXIT_FAILURE;
	}

	rc = XIGetProperty(dpy, deviceid, prop_matrix, 0, 9, False, prop_float,
			   &type_return, &format_return, &nitems, &bytes_after,
			   &data.c);
	if (rc != Success || prop_float != type_return || format_return != 32 ||
	    nitems != 9 || bytes_after != 0) {
		fprintf(stderr, "Failed to retrieve current property values\n");
		return EXIT_FAILURE;
	}

	memcpy(data.f, m->m, sizeof(m->m));

	XIChangeProperty(dpy, deviceid, prop_matrix, prop_float,
			 format_return, PropModeReplace, data.c, nitems);

	XFree(data.c);

	return EXIT_SUCCESS;
}

static void matrix_set(Matrix * m, int row, int col, float val)
{
	m->m[row * 3 + col] = val;
}

static void matrix_set_unity(Matrix * m)
{
	memset(m, 0, sizeof(m->m));
	matrix_set(m, 0, 0, 1);
	matrix_set(m, 1, 1, 1);
	matrix_set(m, 2, 2, 1);
}

static void matrix_s4(Matrix * m, float x02, float x12, float d1, float d2,
		      int main_diag)
{
	matrix_set(m, 0, 2, x02);
	matrix_set(m, 1, 2, x12);

	if (main_diag) {
		matrix_set(m, 0, 0, d1);
		matrix_set(m, 1, 1, d2);
	} else {
		matrix_set(m, 0, 0, 0);
		matrix_set(m, 1, 1, 0);
		matrix_set(m, 0, 1, d1);
		matrix_set(m, 1, 0, d2);
	}
}

static void set_transformation_matrix(Matrix * m, int offset_x, int offset_y,
				      int screen_width, int screen_height,
				      int rotation)
{
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL)
		dpy = XOpenDisplay(":0.0");

	if (dpy == NULL)
		return;

	/* total display size */
	int width = DisplayWidth(dpy, DefaultScreen(dpy));
	int height = DisplayHeight(dpy, DefaultScreen(dpy));

	g_debug("width %i height %i\n", width, height);

	/* offset */
	float x = 1.0 * offset_x / width;
	float y = 1.0 * offset_y / height;

	/* mapping */
	float w = 1.0 * screen_width / width;
	float h = 1.0 * screen_height / height;

	matrix_set_unity(m);

	/*
	 * There are 16 cases:
	 * Rotation X Reflection
	 * Rotation: 0 | 90 | 180 | 270
	 * Reflection: None | X | Y | XY
	 *
	 * They are spelled out instead of doing matrix multiplication to avoid
	 * any floating point errors.
	 */
	switch (rotation) {
	case RR_Rotate_0:
	case RR_Rotate_180 | RR_Reflect_All:
		matrix_s4(m, x, y, w, h, 1);
		break;
	case RR_Reflect_X | RR_Rotate_0:
	case RR_Reflect_Y | RR_Rotate_180:
		matrix_s4(m, x + w, y, -w, h, 1);
		break;
	case RR_Reflect_Y | RR_Rotate_0:
	case RR_Reflect_X | RR_Rotate_180:
		matrix_s4(m, x, y + h, w, -h, 1);
		break;
	case RR_Rotate_90:
	case RR_Rotate_270 | RR_Reflect_All:	/* left limited - correct in working zone. */
		matrix_s4(m, x + w, y, -w, h, 0);
		break;
	case RR_Rotate_270:
	case RR_Rotate_90 | RR_Reflect_All:	/* left limited - correct in working zone. */
		matrix_s4(m, x, y + h, w, -h, 0);
		break;
	case RR_Rotate_90 | RR_Reflect_X:	/* left limited - correct in working zone. */
	case RR_Rotate_270 | RR_Reflect_Y:	/* left limited - correct in working zone. */
		matrix_s4(m, x, y, w, h, 0);
		break;
	case RR_Rotate_90 | RR_Reflect_Y:	/* right limited - correct in working zone. */
	case RR_Rotate_270 | RR_Reflect_X:	/* right limited - correct in working zone. */
		matrix_s4(m, x + w, y + h, -w, -h, 0);
		break;
	case RR_Rotate_180:
	case RR_Reflect_All | RR_Rotate_0:
		matrix_s4(m, x + w, y + h, -w, -h, 1);
		break;
	}

	XCloseDisplay(dpy);
}

static XRROutputInfo *find_output_xrandr(Display * dpy)
{
	XRRScreenResources *res;
	XRROutputInfo *output_info = NULL;
	int i;
	int found = 0;

	res = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));

	for (i = 0; i < res->noutput && !found; i++) {
		output_info = XRRGetOutputInfo(dpy, res, res->outputs[i]);

		if (output_info->crtc
		    && output_info->connection == RR_Connected) {
			found = 1;
			break;
		}

		XRRFreeOutputInfo(output_info);
	}

	XRRFreeScreenResources(res);

	if (!found)
		output_info = NULL;

	return output_info;
}

static int map_output_xrandr(Display * dpy, int deviceid)
{
	int rc = EXIT_FAILURE;
	XRRScreenResources *res;
	XRROutputInfo *output_info;

	res = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));
	output_info = find_output_xrandr(dpy);

	/* crtc holds our screen info, need to compare to actual screen size */
	if (output_info) {
		XRRCrtcInfo *crtc_info;
		Matrix m;
		matrix_set_unity(&m);
		crtc_info = XRRGetCrtcInfo(dpy, res, output_info->crtc);
		set_transformation_matrix(&m, crtc_info->x, crtc_info->y,
					  crtc_info->width, crtc_info->height,
					  crtc_info->rotation);
		rc = apply_matrix(dpy, deviceid, &m);
		XRRFreeCrtcInfo(crtc_info);
		XRRFreeOutputInfo(output_info);
	}

	XRRFreeScreenResources(res);

	return rc;
}

int hd_rotate_input_devices(Display * dpy)
{
	int ret = 0;

	for (int i = 0; i < xi_devices->len; i++) {
		hd_xi_device *xi_dev =
		    &g_array_index(xi_devices, hd_xi_device, i);
		if (xi_dev->dev && xi_dev->is_ts) {
			ret =
			    ret & map_output_xrandr(dpy,
						    xi_dev->dev->device_id);
		}
	}

	return ret;
}
