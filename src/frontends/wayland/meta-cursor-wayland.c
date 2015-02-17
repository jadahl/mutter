/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2015 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include "frontends/wayland/meta-cursor-wayland.h"

#include <cogl/cogl-wayland-server.h>

#include <meta/meta-backend.h>
#include "core/screen-private.h"
#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-buffer.h"

#include "core/util-private.h"

typedef struct
{
  MetaRectangle current_rect;
  int monitor_scale;
  MetaWaylandSurface *surface;
  struct wl_listener surface_destroy_listener;
} MetaCursorSpriteWaylandPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorSpriteWayland,
                            meta_cursor_sprite_wayland,
                            META_TYPE_CURSOR_SPRITE)

static void
load_from_buffer (MetaCursorSpriteWayland *self,
                  struct wl_resource      *buffer,
                  int                      hot_x,
                  int                      hot_y)
{
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (self);
  MetaCursorImage *image = meta_cursor_sprite_get_image (cursor_sprite);
  MetaBackend *meta_backend = meta_get_backend ();
  MetaCursorRenderer *renderer =
    meta_backend_get_cursor_renderer (meta_backend);
  ClutterBackend *backend;
  CoglContext *cogl_context;

  image->hot_x = hot_x;
  image->hot_y = hot_y;

  backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (backend);

  image->texture = cogl_wayland_texture_2d_new_from_buffer (cogl_context,
                                                            buffer, NULL);

  meta_cursor_renderer_realize_cursor_from_wl_buffer (renderer,
                                                      cursor_sprite, buffer);
}

static void
on_surface_destroyed (struct wl_listener *listener, void *data)
{
  MetaCursorSpriteWaylandPrivate *priv =
    wl_container_of (listener, priv, surface_destroy_listener);

  priv->surface = NULL;
  wl_list_init (&priv->surface_destroy_listener.link);
}

MetaCursorSprite *
meta_cursor_sprite_wayland_from_surface (MetaWaylandSurface *surface,
                                         int                 hot_x,
                                         int                 hot_y)
{
  MetaCursorSpriteWayland *self;
  MetaCursorSpriteWaylandPrivate *priv;

  self = META_CURSOR_SPRITE_WAYLAND (meta_cursor_sprite_new ());
  priv = meta_cursor_sprite_wayland_get_instance_private (self);

  priv->surface = surface;
  priv->surface_destroy_listener.notify = on_surface_destroyed;
  wl_signal_add (&surface->destroy_signal, &priv->surface_destroy_listener);

  load_from_buffer (self, surface->buffer->resource, hot_x, hot_y);

  return META_CURSOR_SPRITE (self);
}

MetaWaylandSurface *
meta_cursor_sprite_wayland_get_surface (MetaCursorSpriteWayland *self)
{
  MetaCursorSpriteWaylandPrivate *priv =
    meta_cursor_sprite_wayland_get_instance_private (self);

  return priv->surface;
}

static void
meta_cursor_sprite_wayland_update_position (MetaCursorSprite *cursor_sprite,
                                            int               x,
                                            int               y)
{
  MetaCursorSpriteWayland *self = META_CURSOR_SPRITE_WAYLAND (cursor_sprite);
  MetaCursorSpriteWaylandPrivate *priv =
    meta_cursor_sprite_wayland_get_instance_private (self);
  MetaDisplay *display = meta_get_display ();
  MetaScreen *screen = display->screen;
  const MetaMonitorInfo *monitor;
  float image_scale;
  MetaCursorImage *image;
  guint image_width, image_height;
  int monitor_scale;

  /* During startup, we cannot retrieve the screen here since it has not been
   * set to MetaDisplay yet. Don't try to draw the cursor until we have started.
   */
  if (!screen)
    return;

  /* If surface was destroyed while it was a cursor there is not much
   * we can do any more. */
  if (!priv->surface &&
      meta_cursor_sprite_get_meta_cursor (cursor_sprite) == META_CURSOR_NONE)
    {
      priv->current_rect = (MetaRectangle) { 0 };
      return;
    }

  monitor = meta_screen_get_monitor_for_point (screen, x, y);
  monitor_scale = monitor ? monitor->scale : priv->monitor_scale;
  if (priv->surface)
    {
      if (meta_is_multi_dpi_clutter ())
        image_scale = 1.0 / priv->surface->scale;
      else
        image_scale = (float)monitor_scale / priv->surface->scale;
    }
  else
    {
      if (meta_is_multi_dpi_clutter ())
        image_scale = 1.0 / monitor_scale;
      else
        /* We always load the correct size, so we never scale in this case. */
        image_scale = 1;

      if (priv->monitor_scale != monitor_scale)
        meta_cursor_sprite_load_from_theme (cursor_sprite, monitor_scale);
    }
  priv->monitor_scale = monitor_scale;

  meta_cursor_sprite_ensure_cogl_texture (cursor_sprite, priv->monitor_scale);
  image = meta_cursor_sprite_get_image (cursor_sprite);
  image_width = cogl_texture_get_width (image->texture);
  image_height = cogl_texture_get_height (image->texture);

  priv->current_rect = (MetaRectangle) {
    .x = (int)(x - (image->hot_x * image_scale)),
    .y = (int)(y - (image->hot_y * image_scale)),
    .width = (int)(image_width * image_scale),
    .height = (int)(image_height * image_scale),
  };

  if (priv->surface)
    meta_wayland_surface_update_outputs (priv->surface);
}

static void
meta_cursor_sprite_wayland_get_current_rect (MetaCursorSprite *cursor_sprite,
                                             MetaRectangle    *rect)
{
  MetaCursorSpriteWayland *self = META_CURSOR_SPRITE_WAYLAND (cursor_sprite);
  MetaCursorSpriteWaylandPrivate *priv =
    meta_cursor_sprite_wayland_get_instance_private (self);

  *rect = priv->current_rect;
}

static guint
meta_cursor_sprite_wayland_get_current_scale (MetaCursorSprite *cursor_sprite)
{
  MetaCursorSpriteWayland *self = META_CURSOR_SPRITE_WAYLAND (cursor_sprite);
  MetaCursorSpriteWaylandPrivate *priv =
    meta_cursor_sprite_wayland_get_instance_private (self);

  return priv->monitor_scale;
}

static void
meta_cursor_sprite_wayland_init (MetaCursorSpriteWayland *self)
{
  MetaCursorSpriteWaylandPrivate *priv =
    meta_cursor_sprite_wayland_get_instance_private (self);

  priv->monitor_scale = 1;
  wl_list_init (&priv->surface_destroy_listener.link);
}

static void
meta_cursor_sprite_wayland_finalize (GObject *gobject)
{
  MetaCursorSpriteWayland *self = META_CURSOR_SPRITE_WAYLAND (gobject);
  MetaCursorSpriteWaylandPrivate *priv =
    meta_cursor_sprite_wayland_get_instance_private (self);

  wl_list_remove (&priv->surface_destroy_listener.link);

  G_OBJECT_CLASS (meta_cursor_sprite_wayland_parent_class)->finalize (gobject);
}

static void
meta_cursor_sprite_wayland_class_init (MetaCursorSpriteWaylandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaCursorSpriteClass *parent_class = META_CURSOR_SPRITE_CLASS (klass);

  object_class->finalize = meta_cursor_sprite_wayland_finalize;

  parent_class->update_position = meta_cursor_sprite_wayland_update_position;
  parent_class->get_current_rect = meta_cursor_sprite_wayland_get_current_rect;
  parent_class->get_current_scale = meta_cursor_sprite_wayland_get_current_scale;
}
