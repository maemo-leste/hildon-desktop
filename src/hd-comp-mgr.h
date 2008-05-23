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

#ifndef __HD_COMP_MGR_H__

#include <glib/gmacros.h>
#include <matchbox/core/mb-wm-object.h>
#include <matchbox/comp-mgr/mb-wm-comp-mgr.h>
#include <matchbox/comp-mgr/mb-wm-comp-mgr-clutter.h>

G_BEGIN_DECLS

typedef struct HdCompMgrClass   HdCompMgrClass;
typedef struct HdCompMgr        HdCompMgr;
typedef struct HdCompMgrPrivate HdCompMgrPrivate;

#define HD_COMP_MGR(c)       ((HdCompMgr*)(c))
#define HD_COMP_MGR_CLASS(c) ((HdCompMgrClass*)(c))
#define HD_TYPE_COMP_MGR     (hd_comp_mgr_class_type ())

struct HdCompMgr
{
  MBWMCompMgrClutter    parent;

  HdCompMgrPrivate     *priv;
};

struct HdCompMgrClass
{
    MBWMCompMgrClutterClass parent;
};

int hd_comp_mgr_class_type (void);

void hd_comp_mgr_sync_stacking       (HdCompMgr * hmgr);
void hd_comp_mgr_raise_home_actor    (HdCompMgr *hmgr);
void hd_comp_mgr_lower_home_actor    (HdCompMgr *hmgr);
void hd_comp_mgr_top_home            (HdCompMgr *hmgr);
void hd_comp_mgr_close_client        (HdCompMgr *hmgr,
				      MBWMCompMgrClutterClient *c);
void hd_comp_mgr_hibernate_client    (HdCompMgr *hmgr,
				      MBWMCompMgrClutterClient *c);

G_END_DECLS

#endif /* __HD_COMP_MGR_H__ */
