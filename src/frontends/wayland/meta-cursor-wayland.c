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
#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"

G_DEFINE_TYPE (MetaCursorSpriteWayland, meta_cursor_sprite_wayland, G_TYPE_OBJECT)

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

MetaCursorSprite *
meta_cursor_sprite_wayland_from_buffer (struct wl_resource *buffer,
                                        int                 hot_x,
                                        int                 hot_y)
{
  MetaCursorSpriteWayland *self;

  self = META_CURSOR_SPRITE_WAYLAND (meta_cursor_sprite_new ());

  load_from_buffer (self, buffer, hot_x, hot_y);

  return META_CURSOR_SPRITE (self);
}

static void
meta_cursor_sprite_wayland_init (MetaCursorSpriteWayland *self)
{
}

static void
meta_cursor_sprite_wayland_class_init (MetaCursorSpriteWaylandClass *klass)
{
}
