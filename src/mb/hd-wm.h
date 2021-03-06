/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2008 Nokia Corporation.
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

#ifndef __HD_WM_H__
#define __HD_WM_H__

#include <glib.h>
#include <matchbox/core/mb-wm.h>

G_BEGIN_DECLS

typedef enum _HdWmClientType
{
  HdWmClientTypeHomeApplet  = MBWMClientTypeLast << 1,
  HdWmClientTypeAppMenu     = MBWMClientTypeLast << 2,
  HdWmClientTypeStatusArea  = MBWMClientTypeLast << 3,
  HdWmClientTypeStatusMenu  = MBWMClientTypeLast << 4,
  HdWmClientTypeAnimationActor = MBWMClientTypeLast << 5,
  HdWmClientTypeRemoteTexture = MBWMClientTypeLast << 6,
} HdWmClientType;

typedef struct HdWmClass   HdWmClass;
typedef struct HdWm        HdWm;
typedef struct HdWmPrivate HdWmPrivate;

#define HD_WM(c)       ((HdWm*)(c))
#define HD_WM_CLASS(c) ((HdWmClass*)(c))
#define HD_TYPE_WM     (hd_wm_class_type ())
#define HD_WM_CLIENT_TYPE(t) \
                       ((HdWmClientType)(t))
#define HD_WM_CLIENT_CLIENT_TYPE(c) \
                       HD_WM_CLIENT_TYPE ((MB_WM_CLIENT_CLIENT_TYPE (c)))

struct HdWm
{
    MBWindowManager             parent;

    HdWmPrivate                *priv;
};

struct HdWmClass
{
    MBWindowManagerClass parent;
};

int hd_wm_class_type (void);

Window                  hd_wm_current_app_is (MBWindowManager *wm,
                                              Window xid);
Bool                    hd_wm_activate_zoomed_client (MBWindowManager *wm,
                                                      MBWindowManagerClient *c);
gboolean                hd_wm_has_modal_blockers (const MBWindowManager *wm);
gboolean                hd_wm_close_modal_blockers (const MBWindowManager *wm);
void                    hd_wm_delete_temporaries (MBWindowManager *wm);
Window                  hd_wm_get_hung_client_dialog_xid (MBWindowManager *wm);

G_END_DECLS

#endif /* __HD_WM_H__ */
