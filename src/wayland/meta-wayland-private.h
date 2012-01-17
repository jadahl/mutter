/*
 * Copyright (C) 2012 Intel Corporation
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
 */

#ifndef META_WAYLAND_PRIVATE_H
#define META_WAYLAND_PRIVATE_H

#include <wayland-server.h>

#include <clutter/clutter.h>

#include <glib.h>

#include "window-private.h"
#include "meta-wayland-input-device.h"
#include "window-private.h"

typedef struct _MetaWaylandCompositor MetaWaylandCompositor;

typedef struct
{
  struct wl_buffer *wayland_buffer;
  GList *surfaces_attached_to;
  struct wl_listener buffer_destroy_listener;
} MetaWaylandBuffer;

struct _MetaWaylandSurface
{
  struct wl_surface wayland_surface;
  MetaWaylandCompositor *compositor;
  guint32 xid;
  int x;
  int y;
  MetaWaylandBuffer *buffer;
  MetaWindow *window;
  ClutterActor *actor;
  gboolean has_shell_surface;
  struct wl_listener surface_destroy_listener;
};

#ifndef HAVE_META_WAYLAND_SURFACE_TYPE
typedef struct _MetaWaylandSurface MetaWaylandSurface;
#endif

typedef struct
{
  MetaWaylandSurface *surface;
  struct wl_resource resource;
  struct wl_listener surface_destroy_listener;
} MetaWaylandShellSurface;

typedef struct
{
  guint32 flags;
  int width;
  int height;
  int refresh;
} MetaWaylandMode;

typedef struct
{
  struct wl_object wayland_output;
  int x;
  int y;
  int width_mm;
  int height_mm;
  /* XXX: with sliced stages we'd reference a CoglFramebuffer here. */

  GList *modes;
} MetaWaylandOutput;

typedef struct
{
  GSource source;
  GPollFD pfd;
  struct wl_event_loop *loop;
} WaylandEventSource;

typedef struct
{
  /* GList node used as an embedded list */
  GList node;

  /* Pointer back to the compositor */
  MetaWaylandCompositor *compositor;

  struct wl_resource resource;
} MetaWaylandFrameCallback;

struct _MetaWaylandCompositor
{
  struct wl_display *wayland_display;
  struct wl_shm *wayland_shm;
  struct wl_event_loop *wayland_loop;
  GMainLoop *init_loop;
  ClutterActor *stage;
  GList *outputs;
  GSource *wayland_event_source;
  GList *surfaces;
  GQueue frame_callbacks;

  int xwayland_display_index;
  char *xwayland_lockfile;
  int xwayland_abstract_fd;
  int xwayland_unix_fd;
  pid_t xwayland_pid;
  struct wl_client *xwayland_client;
  struct wl_resource *xserver_resource;
  GHashTable *window_surfaces;

  MetaWaylandInputDevice *input_device;

  /* This surface is only used to keep drag of the implicit grab when
     synthesizing XEvents for Mutter */
  struct wl_surface *implicit_grab_surface;
  /* Button that was pressed to initiate an implicit grab. The
     implicit grab will only be released when this button is
     released */
  guint32 implicit_grab_button;
};

void                    meta_wayland_init                       (void);
void                    meta_wayland_finalize                   (void);

/* We maintain a singleton MetaWaylandCompositor which can be got at via this
 * API after meta_wayland_init() has been called. */
MetaWaylandCompositor  *meta_wayland_compositor_get_default     (void);

void                    meta_wayland_handle_sig_child           (void);

MetaWaylandSurface     *meta_wayland_lookup_surface_for_xid     (guint32 xid);

void                    meta_wayland_compositor_repick          (MetaWaylandCompositor *compositor);

void                    meta_wayland_compositor_set_input_focus (MetaWaylandCompositor *compositor,
                                                                 MetaWindow            *window);

#endif /* META_WAYLAND_PRIVATE_H */
