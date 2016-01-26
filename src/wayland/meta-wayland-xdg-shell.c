/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2012-2013 Intel Corporation
 * Copyright (C) 2013-2015 Red Hat Inc.
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
 */

#include "config.h"

#include "wayland/meta-wayland-xdg-shell.h"

#include "core/window-private.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-popup.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-window-wayland.h"
#include "xdg-shell-unstable-v6-server-protocol.h"

typedef struct _MetaWaylandXdgShellClient
{
  struct wl_resource *resource;
  GList *surfaces;
  GList *surface_constructors;
} MetaWaylandXdgShellClient;

typedef struct _MetaWaylandXdgSurfaceConstructor
{
  MetaWaylandSurface *surface;
  struct wl_resource *resource;
  MetaWaylandXdgShellClient *shell_client;
} MetaWaylandXdgSurfaceConstructor;

typedef struct _MetaWaylandXdgSurfacePrivate
{
  struct wl_resource *resource;
  MetaWaylandXdgShellClient *shell_client;
  MetaWaylandSerial acked_configure_serial;
  gboolean has_set_geometry;
  MetaRectangle geometry;
} MetaWaylandXdgSurfacePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaWaylandXdgSurface,
                            meta_wayland_xdg_surface,
                            META_TYPE_WAYLAND_SURFACE_ROLE_SHELL_SURFACE);

struct _MetaWaylandXdgToplevel
{
  MetaWaylandXdgSurface parent;

  struct wl_resource *resource;
};

G_DEFINE_TYPE (MetaWaylandXdgToplevel,
               meta_wayland_xdg_toplevel,
               META_TYPE_WAYLAND_XDG_SURFACE);

struct _MetaWaylandXdgPopup
{
  MetaWaylandXdgSurface parent;

  struct wl_resource *resource;

  MetaWaylandSurface *parent_surface;
  struct wl_listener parent_destroy_listener;

  MetaWaylandPopup *popup;
};

static void
popup_surface_iface_init (MetaWaylandPopupSurfaceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaWaylandXdgPopup,
                         meta_wayland_xdg_popup,
                         META_TYPE_WAYLAND_XDG_SURFACE,
                         G_IMPLEMENT_INTERFACE (META_TYPE_WAYLAND_POPUP_SURFACE,
                                                popup_surface_iface_init));

static struct wl_resource *
meta_wayland_xdg_surface_get_shell_resource (MetaWaylandXdgSurface *xdg_surface);

static MetaRectangle
meta_wayland_xdg_surface_get_window_geometry (MetaWaylandXdgSurface *xdg_surface);

static MetaWaylandSurface *
surface_from_xdg_surface_resource (struct wl_resource *resource)
{
  MetaWaylandSurfaceRole *surface_role = wl_resource_get_user_data (resource);
  return meta_wayland_surface_role_get_surface (surface_role);
}

static MetaWaylandSurface *
surface_from_xdg_toplevel_resource (struct wl_resource *resource)
{
  return surface_from_xdg_surface_resource (resource);
}

static void
xdg_toplevel_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgToplevel *xdg_toplevel = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  meta_wayland_surface_destroy_window (surface);
  xdg_toplevel->resource = NULL;
}

static void
xdg_toplevel_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_toplevel_set_parent (struct wl_client   *client,
                         struct wl_resource *resource,
                         struct wl_resource *parent_resource)
{
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);
  MetaWindow *transient_for = NULL;

  if (parent_resource)
    {
      MetaWaylandSurface *parent_surface =
        surface_from_xdg_surface_resource (parent_resource);

      transient_for = parent_surface->window;
    }

  meta_window_set_transient_for (surface->window, transient_for);
}

static void
xdg_toplevel_set_title (struct wl_client   *client,
                        struct wl_resource *resource,
                        const char         *title)
{
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  meta_window_set_title (surface->window, title);
}

static void
xdg_toplevel_set_app_id (struct wl_client   *client,
                         struct wl_resource *resource,
                         const char         *app_id)
{
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  meta_window_set_wm_class (surface->window, app_id, app_id);
}

static void
xdg_toplevel_show_window_menu (struct wl_client   *client,
                               struct wl_resource *resource,
                               struct wl_resource *seat_resource,
                               uint32_t            serial,
                               int32_t             x,
                               int32_t             y)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  if (!meta_wayland_seat_get_grab_info (seat, surface, serial, NULL, NULL))
    return;

  meta_window_show_menu (surface->window, META_WINDOW_MENU_WM,
                         surface->window->buffer_rect.x + x,
                         surface->window->buffer_rect.y + y);
}

static void
xdg_toplevel_move (struct wl_client   *client,
                   struct wl_resource *resource,
                   struct wl_resource *seat_resource,
                   uint32_t            serial)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);
  gfloat x, y;

  if (!meta_wayland_seat_get_grab_info (seat, surface, serial, &x, &y))
    return;

  meta_wayland_surface_begin_grab_op (surface, seat, META_GRAB_OP_MOVING, x, y);
}

static MetaGrabOp
grab_op_for_xdg_toplevel_resize_edge (int edge)
{
  MetaGrabOp op = META_GRAB_OP_WINDOW_BASE;

  if (edge & ZXDG_TOPLEVEL_V6_RESIZE_EDGE_TOP)
    op |= META_GRAB_OP_WINDOW_DIR_NORTH;
  if (edge & ZXDG_TOPLEVEL_V6_RESIZE_EDGE_BOTTOM)
    op |= META_GRAB_OP_WINDOW_DIR_SOUTH;
  if (edge & ZXDG_TOPLEVEL_V6_RESIZE_EDGE_LEFT)
    op |= META_GRAB_OP_WINDOW_DIR_WEST;
  if (edge & ZXDG_TOPLEVEL_V6_RESIZE_EDGE_RIGHT)
    op |= META_GRAB_OP_WINDOW_DIR_EAST;

  if (op == META_GRAB_OP_WINDOW_BASE)
    {
      g_warning ("invalid edge: %d", edge);
      return META_GRAB_OP_NONE;
    }

  return op;
}

static void
xdg_toplevel_resize (struct wl_client   *client,
                     struct wl_resource *resource,
                     struct wl_resource *seat_resource,
                     uint32_t            serial,
                     uint32_t            edges)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);
  gfloat x, y;
  MetaGrabOp grab_op;

  if (!meta_wayland_seat_get_grab_info (seat, surface, serial, &x, &y))
    return;

  grab_op = grab_op_for_xdg_toplevel_resize_edge (edges);
  meta_wayland_surface_begin_grab_op (surface, seat, grab_op, x, y);
}

static void
xdg_toplevel_set_maximized (struct wl_client   *client,
                            struct wl_resource *resource)
{
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  meta_window_maximize (surface->window, META_MAXIMIZE_BOTH);
}

static void
xdg_toplevel_unset_maximized (struct wl_client   *client,
                              struct wl_resource *resource)
{
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  meta_window_unmaximize (surface->window, META_MAXIMIZE_BOTH);
}

static void
xdg_toplevel_set_fullscreen (struct wl_client   *client,
                             struct wl_resource *resource,
                             struct wl_resource *output_resource)
{
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  meta_window_make_fullscreen (surface->window);
}

static void
xdg_toplevel_unset_fullscreen (struct wl_client   *client,
                               struct wl_resource *resource)
{
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  meta_window_unmake_fullscreen (surface->window);
}

static void
xdg_toplevel_set_minimized (struct wl_client   *client,
                            struct wl_resource *resource)
{
  MetaWaylandSurface *surface = surface_from_xdg_toplevel_resource (resource);

  meta_window_minimize (surface->window);
}

static const struct zxdg_toplevel_v6_interface meta_wayland_xdg_toplevel_interface = {
  xdg_toplevel_destroy,
  xdg_toplevel_set_parent,
  xdg_toplevel_set_title,
  xdg_toplevel_set_app_id,
  xdg_toplevel_show_window_menu,
  xdg_toplevel_move,
  xdg_toplevel_resize,
  xdg_toplevel_set_maximized,
  xdg_toplevel_unset_maximized,
  xdg_toplevel_set_fullscreen,
  xdg_toplevel_unset_fullscreen,
  xdg_toplevel_set_minimized,
};

static void
xdg_popup_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgPopup *xdg_popup =
    META_WAYLAND_XDG_POPUP (wl_resource_get_user_data (resource));

  if (xdg_popup->parent_surface)
    {
      wl_list_remove (&xdg_popup->parent_destroy_listener.link);
      xdg_popup->parent_surface = NULL;
    }

  if (xdg_popup->popup)
    meta_wayland_popup_dismiss (xdg_popup->popup);

  xdg_popup->resource = NULL;
}

static void
xdg_popup_destroy (struct wl_client *client,
                   struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zxdg_popup_v6_interface meta_wayland_xdg_popup_interface = {
  xdg_popup_destroy,
};

static void
handle_popup_parent_destroyed (struct wl_listener *listener,
                               void               *data)
{
  MetaWaylandXdgPopup *xdg_popup =
    wl_container_of (listener, xdg_popup, parent_destroy_listener);
  MetaWaylandXdgSurface *xdg_surface = META_WAYLAND_XDG_SURFACE (xdg_popup);
  struct wl_resource *xdg_shell_resource =
    meta_wayland_xdg_surface_get_shell_resource (xdg_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (xdg_popup);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  wl_resource_post_error (xdg_shell_resource,
                          ZXDG_SHELL_V6_ERROR_NOT_THE_TOPMOST_POPUP,
                          "destroyed popup not top most popup");
  xdg_popup->parent_surface = NULL;

  meta_wayland_surface_destroy_window (surface);
}

static void
fill_states (struct wl_array *states, MetaWindow *window)
{
  uint32_t *s;

  if (META_WINDOW_MAXIMIZED (window))
    {
      s = wl_array_add (states, sizeof *s);
      *s = ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED;
    }
  if (meta_window_is_fullscreen (window))
    {
      s = wl_array_add (states, sizeof *s);
      *s = ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN;
    }
  if (meta_grab_op_is_resizing (window->display->grab_op))
    {
      s = wl_array_add (states, sizeof *s);
      *s = ZXDG_TOPLEVEL_V6_STATE_RESIZING;
    }
  if (meta_window_appears_focused (window))
    {
      s = wl_array_add (states, sizeof *s);
      *s = ZXDG_TOPLEVEL_V6_STATE_ACTIVATED;
    }
}

static void
xdg_toplevel_role_commit (MetaWaylandSurfaceRole  *surface_role,
                          MetaWaylandPendingState *pending)
{
  MetaWaylandXdgToplevel *xdg_toplevel = META_WAYLAND_XDG_TOPLEVEL (surface_role);
  MetaWaylandXdgSurface *xdg_surface = META_WAYLAND_XDG_SURFACE (xdg_toplevel);
  MetaWaylandXdgSurfacePrivate *xdg_surface_priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);
  MetaWaylandSurfaceRoleClass *surface_role_class;
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWindow *window = surface->window;
  MetaRectangle window_geometry;

  surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_xdg_toplevel_parent_class);
  surface_role_class->commit (surface_role, pending);

  /* If the window disappeared the surface is not coming back. */
  if (!window)
    return;

  if (!pending->has_new_geometry)
    {
      if (pending->dx != 0 || pending->dx != 0)
        {
          g_warning ("XXX: Attach-initiated move without a new geometry. This is unimplemented right now.");
        }
      return;
    }

  window_geometry = meta_wayland_xdg_surface_get_window_geometry (xdg_surface);
  meta_window_wayland_move_resize (window,
                                   &xdg_surface_priv->acked_configure_serial,
                                   window_geometry,
                                   pending->dx, pending->dy);
  xdg_surface_priv->acked_configure_serial.set = FALSE;
}

static MetaWaylandSurface *
xdg_toplevel_role_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  return meta_wayland_surface_role_get_surface (surface_role);
}

static void
xdg_toplevel_role_configure (MetaWaylandSurfaceRoleShellSurface *shell_surface_role,
                             int                                 new_width,
                             int                                 new_height,
                             MetaWaylandSerial                  *sent_serial)
{
  MetaWaylandXdgToplevel *xdg_toplevel =
    META_WAYLAND_XDG_TOPLEVEL (shell_surface_role);
  MetaWaylandXdgSurface *xdg_surface = META_WAYLAND_XDG_SURFACE (xdg_toplevel);
  MetaWaylandXdgSurfacePrivate *xdg_surface_priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface_role);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  struct wl_client *client = wl_resource_get_client (xdg_toplevel->resource);
  struct wl_display *display = wl_client_get_display (client);
  uint32_t serial = wl_display_next_serial (display);
  struct wl_array states;

  if (!xdg_surface_priv->resource)
    return;

  if (!xdg_toplevel->resource)
    return;

  wl_array_init (&states);
  fill_states (&states, surface->window);

  zxdg_toplevel_v6_send_configure (xdg_toplevel->resource,
                                   new_width, new_height,
                                   &states);
  zxdg_surface_v6_send_configure (xdg_surface_priv->resource, serial);

  wl_array_release (&states);

  if (sent_serial)
    {
      sent_serial->set = TRUE;
      sent_serial->value = serial;
    }
}

static void
xdg_toplevel_role_managed (MetaWaylandSurfaceRoleShellSurface *shell_surface_role,
                           MetaWindow                         *window)
{
}

static void
xdg_toplevel_role_close (MetaWaylandSurfaceRoleShellSurface *shell_surface_role)
{
  MetaWaylandXdgToplevel *xdg_toplevel =
    META_WAYLAND_XDG_TOPLEVEL (shell_surface_role);

  zxdg_toplevel_v6_send_close (xdg_toplevel->resource);
}

static void
xdg_toplevel_role_finalize (GObject *object)
{
  MetaWaylandXdgToplevel *xdg_toplevel = META_WAYLAND_XDG_TOPLEVEL (object);

  g_clear_pointer (&xdg_toplevel->resource, wl_resource_destroy);

  G_OBJECT_CLASS (meta_wayland_xdg_toplevel_parent_class)->finalize (object);
}

static void
meta_wayland_xdg_toplevel_init (MetaWaylandXdgToplevel *role)
{
}

static void
meta_wayland_xdg_toplevel_class_init (MetaWaylandXdgToplevelClass *klass)
{
  GObjectClass *object_class;
  MetaWaylandSurfaceRoleClass *surface_role_class;
  MetaWaylandSurfaceRoleShellSurfaceClass *shell_surface_role_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = xdg_toplevel_role_finalize;

  surface_role_class = META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  surface_role_class->commit = xdg_toplevel_role_commit;
  surface_role_class->get_toplevel = xdg_toplevel_role_get_toplevel;

  shell_surface_role_class =
    META_WAYLAND_SURFACE_ROLE_SHELL_SURFACE_CLASS (klass);
  shell_surface_role_class->configure = xdg_toplevel_role_configure;
  shell_surface_role_class->managed = xdg_toplevel_role_managed;
  shell_surface_role_class->close = xdg_toplevel_role_close;
}

static void
xdg_popup_role_commit (MetaWaylandSurfaceRole  *surface_role,
                       MetaWaylandPendingState *pending)
{
  MetaWaylandXdgSurface *xdg_surface = META_WAYLAND_XDG_SURFACE (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class;
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWindow *window = surface->window;
  MetaRectangle window_geometry;

  surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_xdg_popup_parent_class);
  surface_role_class->commit (surface_role, pending);

  if (surface->buffer == NULL)
    {
      /* XDG surfaces can't commit NULL buffers */
      wl_resource_post_error (surface->resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "Cannot commit a NULL buffer to an xdg_popup");
      return;
    }

  /* If the window disappeared the surface is not coming back. */
  if (!window)
    return;

  meta_wayland_surface_apply_window_state (surface, pending);
  window_geometry = meta_wayland_xdg_surface_get_window_geometry (xdg_surface);
  meta_window_wayland_move_resize (window,
                                   NULL,
                                   window_geometry,
                                   pending->dx, pending->dy);
}

static MetaWaylandSurface *
xdg_popup_role_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandXdgPopup *xdg_popup = META_WAYLAND_XDG_POPUP (surface_role);

  return meta_wayland_surface_get_toplevel (xdg_popup->parent_surface);
}

static void
xdg_popup_role_configure (MetaWaylandSurfaceRoleShellSurface *shell_surface_role,
                          int                                 new_width,
                          int                                 new_height,
                          MetaWaylandSerial                  *sent_serial)
{
  /* This can happen if the popup window loses or receives focus.
   * Just ignore it. */
}

static void
xdg_popup_role_managed (MetaWaylandSurfaceRoleShellSurface *shell_surface_role,
                        MetaWindow                         *window)
{
  MetaWaylandXdgPopup *xdg_popup = META_WAYLAND_XDG_POPUP (shell_surface_role);
  MetaWaylandSurface *parent = xdg_popup->parent_surface;

  g_assert (parent);

  meta_window_set_transient_for (window, parent->window);
  meta_window_set_type (window, META_WINDOW_DROPDOWN_MENU);
}

static void
meta_wayland_xdg_popup_done (MetaWaylandPopupSurface *popup_surface)
{
  MetaWaylandXdgPopup *xdg_popup = META_WAYLAND_XDG_POPUP (popup_surface);

  zxdg_popup_v6_send_popup_done (xdg_popup->resource);
}

static void
meta_wayland_xdg_popup_dismiss (MetaWaylandPopupSurface *popup_surface)
{
  MetaWaylandXdgPopup *xdg_popup = META_WAYLAND_XDG_POPUP (popup_surface);
  MetaWaylandXdgSurface *xdg_surface = META_WAYLAND_XDG_SURFACE (xdg_popup);
  struct wl_resource *xdg_shell_resource =
    meta_wayland_xdg_surface_get_shell_resource (xdg_surface);
  MetaWaylandSurfaceRole *surface_role = META_WAYLAND_SURFACE_ROLE (xdg_popup);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *top_popup;

  top_popup = meta_wayland_popup_get_top_popup (xdg_popup->popup);
  if (surface != top_popup)
    {
      wl_resource_post_error (xdg_shell_resource,
                              ZXDG_SHELL_V6_ERROR_NOT_THE_TOPMOST_POPUP,
                              "destroyed popup not top most popup");
    }

  xdg_popup->popup = NULL;

  meta_wayland_surface_destroy_window (surface);
}

static MetaWaylandSurface *
meta_wayland_xdg_popup_get_surface (MetaWaylandPopupSurface *popup_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (popup_surface);

  return meta_wayland_surface_role_get_surface (surface_role);
}

static void
popup_surface_iface_init (MetaWaylandPopupSurfaceInterface *iface)
{
  iface->done = meta_wayland_xdg_popup_done;
  iface->dismiss = meta_wayland_xdg_popup_dismiss;
  iface->get_surface = meta_wayland_xdg_popup_get_surface;
}

static void
xdg_popup_role_finalize (GObject *object)
{
  MetaWaylandXdgPopup *xdg_popup = META_WAYLAND_XDG_POPUP (object);

  g_clear_pointer (&xdg_popup->resource, wl_resource_destroy);

  G_OBJECT_CLASS (meta_wayland_xdg_popup_parent_class)->finalize (object);
}

static void
meta_wayland_xdg_popup_init (MetaWaylandXdgPopup *role)
{
}

static void
meta_wayland_xdg_popup_class_init (MetaWaylandXdgPopupClass *klass)
{
  GObjectClass *object_class;
  MetaWaylandSurfaceRoleClass *surface_role_class;
  MetaWaylandSurfaceRoleShellSurfaceClass *shell_surface_role_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = xdg_popup_role_finalize;

  surface_role_class = META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  surface_role_class->commit = xdg_popup_role_commit;
  surface_role_class->get_toplevel = xdg_popup_role_get_toplevel;

  shell_surface_role_class =
    META_WAYLAND_SURFACE_ROLE_SHELL_SURFACE_CLASS (klass);
  shell_surface_role_class->configure = xdg_popup_role_configure;
  shell_surface_role_class->managed = xdg_popup_role_managed;
}

static struct wl_resource *
meta_wayland_xdg_surface_get_shell_resource (MetaWaylandXdgSurface *xdg_surface)
{
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);

  return priv->shell_client->resource;
}

static MetaRectangle
meta_wayland_xdg_surface_get_window_geometry (MetaWaylandXdgSurface *xdg_surface)
{
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);

  return priv->geometry;
}

static gboolean
meta_wayland_xdg_surface_is_assigned (MetaWaylandXdgSurface *xdg_surface)
{
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);

  return priv->resource != NULL;
}

static void
xdg_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandSurface *surface = surface_from_xdg_surface_resource (resource);
  MetaWaylandXdgSurface *xdg_surface = wl_resource_get_user_data (resource);
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);

  meta_wayland_compositor_destroy_frame_callbacks (surface->compositor,
                                                   surface);

  priv->shell_client->surfaces = g_list_remove (priv->shell_client->surfaces,
                                                xdg_surface);

  priv->resource = NULL;
}

static void
xdg_surface_destroy (struct wl_client   *client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_surface_get_toplevel (struct wl_client   *client,
                          struct wl_resource *resource,
                          uint32_t            id)
{
  MetaWaylandXdgSurface *xdg_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = surface_from_xdg_surface_resource (resource);
  struct wl_resource *xdg_shell_resource =
    meta_wayland_xdg_surface_get_shell_resource (xdg_surface);

  wl_resource_post_error (xdg_shell_resource, ZXDG_SHELL_V6_ERROR_ROLE,
                          "wl_surface@%d already has a role assigned",
                          wl_resource_get_id (surface->resource));
}

static void
xdg_surface_get_popup (struct wl_client   *client,
                       struct wl_resource *resource,
                       uint32_t            id,
                       struct wl_resource *parent_resource,
                       struct wl_resource *seat_resource,
                       uint32_t            serial,
                       int32_t             x,
                       int32_t             y)
{
  MetaWaylandXdgSurface *xdg_surface = wl_resource_get_user_data (resource);
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);
  MetaWaylandSurface *surface = surface_from_xdg_surface_resource (resource);

  wl_resource_post_error (priv->shell_client->resource,
                          ZXDG_SHELL_V6_ERROR_ROLE,
                          "wl_surface@%d already has a role assigned",
                          wl_resource_get_id (surface->resource));
}

static void
xdg_surface_get_tooltip (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            id,
                         struct wl_resource *parent_resource,
                         int32_t             x,
                         int32_t             y)
{
  MetaWaylandXdgSurface *xdg_surface = wl_resource_get_user_data (resource);
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);
  MetaWaylandSurface *surface = surface_from_xdg_surface_resource (resource);

  wl_resource_post_error (priv->shell_client->resource,
                          ZXDG_SHELL_V6_ERROR_ROLE,
                          "wl_surface@%d already has a role assigned",
                          wl_resource_get_id (surface->resource));
}

static void
xdg_surface_set_window_geometry (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 int32_t             x,
                                 int32_t             y,
                                 int32_t             width,
                                 int32_t             height)
{
  MetaWaylandSurface *surface = surface_from_xdg_surface_resource (resource);

  surface->pending->has_new_geometry = TRUE;
  surface->pending->new_geometry.x = x;
  surface->pending->new_geometry.y = y;
  surface->pending->new_geometry.width = width;
  surface->pending->new_geometry.height = height;
}

static void
xdg_surface_ack_configure (struct wl_client   *client,
                           struct wl_resource *resource,
                           uint32_t            serial)
{
  MetaWaylandXdgSurface *xdg_surface = wl_resource_get_user_data (resource);
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);

  priv->acked_configure_serial.set = TRUE;
  priv->acked_configure_serial.value = serial;
}

static const struct zxdg_surface_v6_interface meta_wayland_xdg_surface_interface = {
  xdg_surface_destroy,
  xdg_surface_get_toplevel,
  xdg_surface_get_popup,
  xdg_surface_get_tooltip,
  xdg_surface_set_window_geometry,
  xdg_surface_ack_configure,
};

static void
xdg_surface_role_finalize (GObject *object)
{
  MetaWaylandXdgSurface *xdg_surface = META_WAYLAND_XDG_SURFACE (object);
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);

  g_clear_pointer (&priv->resource, wl_resource_destroy);

  G_OBJECT_CLASS (meta_wayland_xdg_surface_parent_class)->finalize (object);
}

static void
xdg_surface_role_commit (MetaWaylandSurfaceRole  *surface_role,
                         MetaWaylandPendingState *pending)
{
  MetaWaylandXdgSurface *xdg_surface = META_WAYLAND_XDG_SURFACE (surface_role);
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWindow *window = surface->window;
  MetaWaylandSurfaceRoleClass *surface_role_class;

  surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_xdg_surface_parent_class);
  surface_role_class->commit (surface_role, pending);

  if (surface->buffer == NULL)
    {
      /* XDG surfaces can't commit NULL buffers */
      wl_resource_post_error (surface->resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "Cannot commit a NULL buffer to an xdg_toplevel");
      return;
    }

  if (!window)
    return;

  meta_wayland_surface_apply_window_state (surface, pending);

  if (pending->has_new_geometry)
    {
      /* If we have new geometry, use it. */
      priv->geometry = pending->new_geometry;
      priv->has_set_geometry = TRUE;
    }
  else if (!priv->has_set_geometry)
    {
      /* If the surface has never set any geometry, calculate
       * a default one unioning the surface and all subsurfaces together. */
      meta_wayland_surface_calculate_window_geometry (surface,
                                                      &priv->geometry,
                                                      0, 0);
    }
}

static void
xdg_surface_role_ping (MetaWaylandSurfaceRoleShellSurface *shell_surface_role,
                       uint32_t                            serial)
{
  MetaWaylandXdgSurface *xdg_surface =
    META_WAYLAND_XDG_SURFACE (shell_surface_role);
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);

  zxdg_shell_v6_send_ping (priv->shell_client->resource, serial);
}

static void
meta_wayland_xdg_surface_init (MetaWaylandXdgSurface *xdg_surface)
{
}

static void
meta_wayland_xdg_surface_class_init (MetaWaylandXdgSurfaceClass *klass)
{
  GObjectClass *object_class;
  MetaWaylandSurfaceRoleClass *surface_role_class;
  MetaWaylandSurfaceRoleShellSurfaceClass *shell_surface_role_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = xdg_surface_role_finalize;

  surface_role_class = META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  surface_role_class->commit = xdg_surface_role_commit;

  shell_surface_role_class =
    META_WAYLAND_SURFACE_ROLE_SHELL_SURFACE_CLASS (klass);
  shell_surface_role_class->ping = xdg_surface_role_ping;
}

static void
meta_wayland_xdg_surface_constructor_finalize (MetaWaylandXdgSurfaceConstructor *constructor,
                                               MetaWaylandXdgSurface            *xdg_surface)
{
  MetaWaylandXdgShellClient *shell_client = constructor->shell_client;
  MetaWaylandXdgSurfacePrivate *priv =
    meta_wayland_xdg_surface_get_instance_private (xdg_surface);

  shell_client->surface_constructors =
    g_list_remove (shell_client->surface_constructors, constructor);
  shell_client->surfaces = g_list_append (shell_client->surfaces, xdg_surface);

  priv->shell_client = shell_client;
  priv->resource = constructor->resource;

  wl_resource_set_implementation (constructor->resource,
                                  &meta_wayland_xdg_surface_interface,
                                  xdg_surface,
                                  xdg_surface_destructor);
  g_free (constructor);
}

static void
xdg_surface_constructor_destroy (struct wl_client   *client,
                                 struct wl_resource *resource)
{
  wl_resource_post_error (resource,
                          ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
                          "xdg_surface destroyed before constructed");
  wl_resource_destroy (resource);
}

static void
xdg_surface_constructor_get_toplevel (struct wl_client   *client,
                                      struct wl_resource *resource,
                                      uint32_t            id)
{
  MetaWaylandXdgSurfaceConstructor *constructor =
    wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = constructor->surface;
  MetaWaylandXdgToplevel *xdg_toplevel;
  MetaWaylandXdgSurface *xdg_surface;
  MetaWindow *window;

  if (!meta_wayland_surface_assign_role (surface, META_TYPE_WAYLAND_XDG_TOPLEVEL))
    {
      wl_resource_post_error (resource, ZXDG_SHELL_V6_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  xdg_toplevel = META_WAYLAND_XDG_TOPLEVEL (surface->role);
  xdg_toplevel->resource = wl_resource_create (client,
                                               &zxdg_toplevel_v6_interface,
                                               wl_resource_get_version (resource),
                                               id);
  wl_resource_set_implementation (xdg_toplevel->resource,
                                  &meta_wayland_xdg_toplevel_interface,
                                  xdg_toplevel,
                                  xdg_toplevel_destructor);

  xdg_surface = META_WAYLAND_XDG_SURFACE (xdg_toplevel);
  meta_wayland_xdg_surface_constructor_finalize (constructor, xdg_surface);

  window = meta_window_wayland_new (meta_get_display (), surface);
  meta_wayland_surface_set_window (surface, window);
}

static void
xdg_surface_constructor_get_popup (struct wl_client   *client,
                                   struct wl_resource *resource,
                                   uint32_t            id,
                                   struct wl_resource *parent_resource,
                                   struct wl_resource *seat_resource,
                                   uint32_t            serial,
                                   int32_t             x,
                                   int32_t             y)
{
  MetaWaylandXdgSurfaceConstructor *constructor =
    wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = constructor->surface;
  struct wl_resource *xdg_shell_resource = constructor->shell_client->resource;
  MetaWaylandPopupSurface *popup_surface;
  MetaWaylandSurface *parent_surf =
    surface_from_xdg_surface_resource (parent_resource);
  MetaWaylandSurface *top_popup;
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaWindow *window;
  MetaDisplay *display = meta_get_display ();
  MetaWaylandXdgPopup *xdg_popup;
  MetaWaylandXdgSurface *xdg_surface;
  MetaWaylandPopup *popup;

  if (!meta_wayland_surface_assign_role (surface, META_TYPE_WAYLAND_XDG_POPUP))
    {
      wl_resource_post_error (xdg_shell_resource, ZXDG_SHELL_V6_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  if (parent_surf == NULL ||
      parent_surf->window == NULL ||
      parent_surf->role == NULL ||
      (!META_IS_WAYLAND_XDG_POPUP (parent_surf->role) &&
       !META_IS_WAYLAND_XDG_SURFACE (parent_surf->role)))
    {
      wl_resource_post_error (xdg_shell_resource,
                              ZXDG_SHELL_V6_ERROR_INVALID_POPUP_PARENT,
                              "invalid parent surface");
      return;
    }

  top_popup = meta_wayland_pointer_get_top_popup (&seat->pointer);
  if ((top_popup == NULL && !META_IS_WAYLAND_XDG_SURFACE (parent_surf->role)) ||
      (top_popup != NULL && parent_surf != top_popup))
    {
      wl_resource_post_error (xdg_shell_resource,
                              ZXDG_SHELL_V6_ERROR_NOT_THE_TOPMOST_POPUP,
                              "parent not top most surface");
      return;
    }

  xdg_popup = META_WAYLAND_XDG_POPUP (surface->role);
  xdg_popup->resource = wl_resource_create (client,
                                            &zxdg_popup_v6_interface,
                                            wl_resource_get_version (resource),
                                            id);
  wl_resource_set_implementation (xdg_popup->resource,
                                  &meta_wayland_xdg_popup_interface,
                                  xdg_popup,
                                  xdg_popup_destructor);

  xdg_surface = META_WAYLAND_XDG_SURFACE (xdg_popup);
  meta_wayland_xdg_surface_constructor_finalize (constructor, xdg_surface);

  if (!meta_wayland_seat_can_popup (seat, serial))
    {
      zxdg_popup_v6_send_popup_done (xdg_popup->resource);
      return;
    }

  xdg_popup->parent_surface = parent_surf;
  xdg_popup->parent_destroy_listener.notify = handle_popup_parent_destroyed;
  wl_resource_add_destroy_listener (parent_surf->resource,
                                    &xdg_popup->parent_destroy_listener);

  window = meta_window_wayland_new (display, surface);
  meta_window_wayland_place_relative_to (window, parent_surf->window, x, y);
  window->showing_for_first_time = FALSE;

  meta_wayland_surface_set_window (surface, window);

  meta_window_focus (window, meta_display_get_current_time (display));
  popup_surface = META_WAYLAND_POPUP_SURFACE (surface->role);
  popup = meta_wayland_pointer_start_popup_grab (&seat->pointer,
                                                 popup_surface);
  if (popup == NULL)
    {
      zxdg_popup_v6_send_popup_done (xdg_popup->resource);
      meta_wayland_surface_destroy_window (surface);
      return;
    }

  xdg_popup->popup = popup;
}

static void
xdg_surface_constructor_get_tooltip (struct wl_client   *client,
                                     struct wl_resource *resource,
                                     uint32_t            id,
                                     struct wl_resource *parent_resource,
                                     int32_t             x,
                                     int32_t             y)
{
  meta_warning ("xdg_tooltip support not implemented yet\n");
}

static void
xdg_surface_constructor_set_window_geometry (struct wl_client   *client,
                                             struct wl_resource *resource,
                                             int32_t             x,
                                             int32_t             y,
                                             int32_t             width,
                                             int32_t             height)
{
  wl_resource_post_error (resource,
                          ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
                          "xdg_surface::set_window_geometry called before constructed");
}

static void
xdg_surface_constructor_ack_configure (struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            serial)
{
  wl_resource_post_error (resource,
                          ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
                          "xdg_surface::ack_configure called before constructed");
}

static const struct zxdg_surface_v6_interface meta_wayland_xdg_surface_constructor_interface = {
  xdg_surface_constructor_destroy,
  xdg_surface_constructor_get_toplevel,
  xdg_surface_constructor_get_popup,
  xdg_surface_constructor_get_tooltip,
  xdg_surface_constructor_set_window_geometry,
  xdg_surface_constructor_ack_configure,
};

static void
xdg_surface_constructor_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgSurfaceConstructor *constructor =
    wl_resource_get_user_data (resource);

  constructor->shell_client->surface_constructors =
    g_list_remove (constructor->shell_client->surface_constructors,
                   constructor);

  wl_resource_destroy (constructor->resource);

  g_free (constructor);
}

static void
xdg_shell_destroy (struct wl_client   *client,
                   struct wl_resource *resource)
{
  MetaWaylandXdgShellClient *shell_client = wl_resource_get_user_data (resource);

  if (shell_client->surfaces || shell_client->surface_constructors)
    wl_resource_post_error (resource, ZXDG_SHELL_V6_ERROR_DEFUNCT_SURFACES,
                            "xdg_shell destroyed before its surfaces");

  wl_resource_destroy (resource);
}

static void
xdg_shell_get_xdg_surface (struct wl_client   *client,
                           struct wl_resource *resource,
                           uint32_t            id,
                           struct wl_resource *surface_resource)
{
  MetaWaylandXdgShellClient *shell_client = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandXdgSurfaceConstructor *constructor;

  if (surface->role && !META_IS_WAYLAND_XDG_SURFACE (surface->role))
    {
      wl_resource_post_error (resource, ZXDG_SHELL_V6_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  if (surface->role && META_IS_WAYLAND_XDG_SURFACE (surface->role) &&
      meta_wayland_xdg_surface_is_assigned (META_WAYLAND_XDG_SURFACE (surface->role)))
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "xdg_shell::get_xdg_surface already requested");
      return;
    }

  constructor = g_new0 (MetaWaylandXdgSurfaceConstructor, 1);
  constructor->surface = surface;
  constructor->shell_client = shell_client;
  constructor->resource = wl_resource_create (client,
                                              &zxdg_surface_v6_interface,
                                              wl_resource_get_version (resource),
                                              id);
  wl_resource_set_implementation (constructor->resource,
                                  &meta_wayland_xdg_surface_constructor_interface,
                                  constructor,
                                  xdg_surface_constructor_destructor);

  shell_client->surface_constructors =
    g_list_append (shell_client->surface_constructors, constructor);
}

static void
xdg_shell_pong (struct wl_client   *client,
                struct wl_resource *resource,
                uint32_t            serial)
{
  MetaDisplay *display = meta_get_display ();

  meta_display_pong_for_serial (display, serial);
}

static const struct zxdg_shell_v6_interface meta_wayland_xdg_shell_interface = {
  xdg_shell_destroy,
  xdg_shell_get_xdg_surface,
  xdg_shell_pong,
};

static void
xdg_shell_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgShellClient *shell_client = wl_resource_get_user_data (resource);

  while (shell_client->surface_constructors)
    {
      MetaWaylandXdgSurfaceConstructor *constructor =
        g_list_first (shell_client->surface_constructors)->data;

      wl_resource_destroy (constructor->resource);
    }
  g_list_free (shell_client->surface_constructors);

  while (shell_client->surfaces)
    {
      MetaWaylandXdgSurface *xdg_surface =
        g_list_first (shell_client->surfaces)->data;
      MetaWaylandXdgSurfacePrivate *xdg_surface_priv =
        meta_wayland_xdg_surface_get_instance_private (xdg_surface);

      wl_resource_destroy (xdg_surface_priv->resource);
    }
  g_list_free (shell_client->surfaces);

  g_free (shell_client);
}

static void
bind_xdg_shell (struct wl_client *client,
                void             *data,
                guint32           version,
                guint32           id)
{
  MetaWaylandXdgShellClient *shell_client;

  shell_client = g_new0 (MetaWaylandXdgShellClient, 1);

  shell_client->resource = wl_resource_create (client,
                                               &zxdg_shell_v6_interface,
                                               version, id);
  wl_resource_set_implementation (shell_client->resource,
                                  &meta_wayland_xdg_shell_interface,
                                  shell_client, xdg_shell_destructor);
}

void
meta_wayland_xdg_shell_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &zxdg_shell_v6_interface,
                        META_XDG_SHELL_VERSION,
                        compositor, bind_xdg_shell) == NULL)
    g_error ("Failed to register a global xdg-shell object");
}
