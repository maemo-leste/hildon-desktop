/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Contributors:  Marc Ordinas i Llopis <marc.ordinasillopis@collabora.co.uk>
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

/*
 * An HdLauncherTree loads and keeps the applications tree.
 *
 */

#ifndef __HD_LAUNCHER_TREE_H__
#define __HD_LAUNCHER_TREE_H__

#include <glib-object.h>
#include "hd-launcher-item.h"
#include "hd-launcher-app.h"

G_BEGIN_DECLS

#define HD_TYPE_LAUNCHER_TREE                   (hd_launcher_tree_get_type ())
#define HD_LAUNCHER_TREE(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), HD_TYPE_LAUNCHER_TREE, HdLauncherTree))
#define HD_IS_LAUNCHER_TREE(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HD_TYPE_LAUNCHER_TREE))
#define HD_LAUNCHER_TREE_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST ((klass), HD_TYPE_LAUNCHER_TREE, HdLauncherTreeClass))
#define HD_IS_LAUNCHER_TREE_CLASS(klass)        (G_TYPE_CHECK_CLASS_TYPE ((klass), HD_TYPE_LAUNCHER_TREE))
#define HD_LAUNCHER_TREE_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), HD_TYPE_LAUNCHER_TREE, HdLauncherTreeClass))

typedef struct _HdLauncherTree          HdLauncherTree;
typedef struct _HdLauncherTreePrivate   HdLauncherTreePrivate;
typedef struct _HdLauncherTreeClass     HdLauncherTreeClass;

struct _HdLauncherTree
{
  GObject parent_instance;

  HdLauncherTreePrivate *priv;
};

struct _HdLauncherTreeClass
{
  GObjectClass parent_class;
};

GType           hd_launcher_tree_get_type (void) G_GNUC_CONST;

HdLauncherTree *hd_launcher_tree_new         (void);

void            hd_launcher_tree_populate    (HdLauncherTree *tree);

GList *         hd_launcher_tree_get_items   (HdLauncherTree *tree);
guint           hd_launcher_tree_get_size    (HdLauncherTree *tree);
HdLauncherItem *hd_launcher_tree_find_item   (HdLauncherTree *tree,
                                              const gchar *id);
HdLauncherApp  *hd_launcher_tree_find_app_by_service (
                                              HdLauncherTree *tree,
                                              const gchar *service);

/* Utility functions. */
void hd_launcher_tree_ensure_user_menu (void);

/* Launcher menu templates. */
#define HD_LAUNCHER_MENU_FILE  "hildon.menu"
#define HD_LAUNCHER_MENU_START \
  "<!DOCTYPE Menu PUBLIC \"-//freedesktop//DTD Menu 1.0//EN\"\n" \
  " \"http://www.freedesktop.org/standards/menu-spec/menu-1.0.dtd\">\n\n" \
  "<Menu>\n" \
  "\t<Name>Main</Name>\n" \
  "\t<MergeFile type=\"parent\">/etc/xdg/menus/hildon.menu</MergeFile>\n" \
  "\t<AppDir>%s/applications/hildon</AppDir>\n" \
  "\t<DirectoryDir>%s/applications/hildon</DirectoryDir>\n\n"
#define HD_LAUNCHER_MENU_END \
  "\t<Menu>\n" \
  "\t\t<Name>Debian</Name>\n" \
  "\t\t<MergeDir>%s/menus/hildon</MergeDir>\n\n" \
  "\t</Menu>\n" \
  "</Menu>\n"

#define HD_LAUNCHER_SUBMENU_START \
  "<!DOCTYPE Menu PUBLIC \"-//freedesktop//DTD Menu 1.0//EN\"\n" \
  " \"http://www.freedesktop.org/standards/menu-spec/menu-1.0.dtd\">\n\n" \
  "<Menu>\n" \
  "\t<Name>%s</Name>\n" \
  "\t<MergeFile type=\"type\">/etc/xdg/menus/%s.menu</MergeFile>\n" \
  "\t<AppDir>%s/applications/%s</AppDir>\n" \
  "\t<DirectoryDir>%s/applications/%s</DirectoryDir>\n\n"
#define HD_LAUNCHER_SUBMENU_END \
  "\t<MergeDir>%s/menus/%s</MergeDir>\n\n" \
  "</Menu>\n"

G_END_DECLS

#endif /* __HD_LAUNCHER_TREE_H__ */
