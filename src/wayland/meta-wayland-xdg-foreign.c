/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include "wayland/meta-wayland-xdg-foreign.h"

#include <wayland-server.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-xdg-shell.h"

#include "xdg-foreign-unstable-v1-server-protocol.h"

#define META_XDG_FOREIGN_HANDLE_LENGTH 32

typedef struct _MetaWaylandXdgExported MetaWaylandXdgExported;
typedef struct _MetaWaylandXdgImported MetaWaylandXdgImported;

typedef struct _MetaWaylandXdgForeign
{
  MetaWaylandCompositor *compositor;
  GRand *rand;

  GHashTable *exported_surfaces;
} MetaWaylandXdgForeign;

struct _MetaWaylandXdgExported
{
  MetaWaylandXdgForeign *foreign;

  MetaWaylandSurface *surface;
};

struct _MetaWaylandXdgImported
{
  MetaWaylandXdgForeign *foreign;

  MetaWaylandSurface *parent_of;
  MetaWaylandXdgExported *exported;
};

static void
xdg_exporter_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static gchar *
generate_handle (MetaWaylandXdgForeign *foreign)
{
  gchar *handle = g_new0 (gchar, META_XDG_FOREIGN_HANDLE_LENGTH + 1);
  int i;

  /* Generate a random string of printable ASCII characters.
   */
  for (i = 0; i < META_XDG_FOREIGN_HANDLE_LENGTH; i++)
    handle[i] = (gchar) g_rand_int_range (foreign->rand, 32, 127);

  return handle;
}

static void
xdg_exported_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zxdg_exported_v1_interface meta_xdg_exported_interface = {
  xdg_exported_destroy,
};

static void
xdg_exporter_export (struct wl_client   *client,
                     struct wl_resource *resource,
                     uint32_t            id,
                     struct wl_resource *wl_surface_resource)
{
  MetaWaylandXdgForeign *foreign = wl_resource_get_user_data (resource);
  struct wl_resource *xdg_exported_resource;
  MetaWaylandXdgExported *exported;
  gchar *handle;

  xdg_exported_resource =
    wl_resource_create (client,
                        &zxdg_exported_v1_interface,
                        wl_resource_get_version (resource),
                        id);
  if (!xdg_exported_resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  exported = g_new0 (MetaWaylandXdgExported, 1);
  exported->foreign = foreign;
  exported->surface = wl_resource_get_user_data (wl_surface_resource);

  wl_resource_set_implementation (xdg_exported_resource,
                                  &meta_xdg_exported_interface,
                                  exported,
                                  NULL);

  handle = generate_handle (foreign);
  g_hash_table_insert (foreign->exported_surfaces, handle, exported);

  zxdg_exported_v1_send_handle (xdg_exported_resource, handle);
}

static const struct zxdg_exporter_v1_interface meta_xdg_exporter_interface = {
  xdg_exporter_destroy,
  xdg_exporter_export,
};

static void
bind_xdg_exporter (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  MetaWaylandXdgForeign *foreign = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zxdg_exporter_v1_interface,
                                 META_ZXDG_EXPORTER_V1_VERSION,
                                 id);

  if (resource == NULL)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &meta_xdg_exporter_interface,
                                  foreign, NULL);
}

static void
xdg_imported_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_imported_set_parent_of (struct wl_client   *client,
                            struct wl_resource *resource,
                            struct wl_resource *surface_resource)
{
  MetaWaylandXdgImported *imported = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  if (!imported->exported)
    return;

  if (imported->parent_of)
    g_object_remove_weak_pointer (G_OBJECT (imported->parent_of),
                                  (gpointer *) &imported->parent_of);

  imported->parent_of = surface;

  if (surface)
    {
      g_object_add_weak_pointer (G_OBJECT (imported->parent_of),
                                 (gpointer *) &imported->parent_of);

      if (surface->window)
        {
          meta_window_set_transient_for (surface->window,
                                         imported->exported->surface->window);
        }
    }
}

static const struct zxdg_imported_v1_interface meta_xdg_imported_interface = {
  xdg_imported_destroy,
  xdg_imported_set_parent_of,
};

static void
xdg_importer_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_imported_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgImported *imported = wl_resource_get_user_data (resource);

  if (imported->parent_of)
    {
      MetaWindow *window;

      g_object_remove_weak_pointer (G_OBJECT (imported->parent_of),
                                    (gpointer *) &imported->parent_of);

      window = imported->parent_of->window;
      if (window)
        meta_window_set_transient_for (window, NULL);
    }
}

static void
xdg_importer_import (struct wl_client   *client,
                     struct wl_resource *resource,
                     uint32_t            id,
                     const char         *handle)
{
  MetaWaylandXdgForeign *foreign = wl_resource_get_user_data (resource);
  struct wl_resource *xdg_imported_resource;
  MetaWaylandXdgImported *imported;
  MetaWaylandXdgExported *exported;

  xdg_imported_resource =
    wl_resource_create (client,
                        &zxdg_imported_v1_interface,
                        wl_resource_get_version (resource),
                        id);
  if (!xdg_imported_resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  exported = g_hash_table_lookup (foreign->exported_surfaces, handle);
  if (!exported || !META_IS_WAYLAND_XDG_SURFACE (exported->surface->role))
    {
      zxdg_imported_v1_send_destroyed (resource);
      return;
    }

  imported = g_new0 (MetaWaylandXdgImported, 1);
  imported->foreign = foreign;
  imported->exported = exported;

  wl_resource_set_implementation (xdg_imported_resource,
                                  &meta_xdg_imported_interface,
                                  imported,
                                  xdg_imported_destructor);
}

static const struct zxdg_importer_v1_interface meta_xdg_importer_interface = {
  xdg_importer_destroy,
  xdg_importer_import,
};

static void
bind_xdg_importer (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  MetaWaylandXdgForeign *foreign = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zxdg_importer_v1_interface,
                                 META_ZXDG_IMPORTER_V1_VERSION,
                                 id);

  if (resource == NULL)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &meta_xdg_importer_interface,
                                  foreign,
                                  NULL);
}

gboolean
meta_wayland_xdg_foreign_init (MetaWaylandCompositor *compositor)
{
  MetaWaylandXdgForeign *foreign;

  foreign = g_new0 (MetaWaylandXdgForeign, 1);

  foreign->compositor = compositor;
  foreign->rand = g_rand_new ();
  foreign->exported_surfaces =
    g_hash_table_new_full ((GHashFunc) g_str_hash,
                           (GEqualFunc) g_str_equal,
                           (GDestroyNotify) g_free,
                           NULL);

  if (wl_global_create (compositor->wayland_display,
                        &zxdg_exporter_v1_interface, 1,
                        foreign,
                        bind_xdg_exporter) == NULL)
    return FALSE;

  if (wl_global_create (compositor->wayland_display,
                        &zxdg_importer_v1_interface, 1,
                        foreign,
                        bind_xdg_importer) == NULL)
    return FALSE;

  return TRUE;
}
