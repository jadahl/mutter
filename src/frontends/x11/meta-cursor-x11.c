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

#include "frontends/x11/meta-cursor-x11.h"

typedef struct
{
  int current_x;
  int current_y;
} MetaCursorSpriteX11Private;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorSpriteX11,
                            meta_cursor_sprite_x11,
                            META_TYPE_CURSOR_SPRITE)

static void
meta_cursor_sprite_x11_update_position (MetaCursorSprite *cursor_sprite,
                                        int               x,
                                        int               y)
{
  MetaCursorSpriteX11 *self = META_CURSOR_SPRITE_X11 (cursor_sprite);
  MetaCursorSpriteX11Private *priv =
    meta_cursor_sprite_x11_get_instance_private (self);

  priv->current_x = x;
  priv->current_y = y;
}

static void
meta_cursor_sprite_x11_get_current_rect (MetaCursorSprite *cursor_sprite,
                                         MetaRectangle    *rect)
{
  MetaCursorSpriteX11 *self = META_CURSOR_SPRITE_X11 (cursor_sprite);
  MetaCursorSpriteX11Private *priv =
    meta_cursor_sprite_x11_get_instance_private (self);
  MetaCursorImage *image;
  gint hot_x, hot_y;
  guint width, height;

  meta_cursor_sprite_ensure_cogl_texture (cursor_sprite, 1);

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);

  image = meta_cursor_sprite_get_image (cursor_sprite);
  width = cogl_texture_get_width (image->texture);
  height = cogl_texture_get_height (image->texture);

  rect->x = priv->current_x - hot_x;
  rect->y = priv->current_y - hot_y;
  rect->width = width;
  rect->height = height;
}

static guint
meta_cursor_sprite_x11_get_current_scale (MetaCursorSprite *cursor_sprite)
{
  return 1;
}

static void
meta_cursor_sprite_x11_init (MetaCursorSpriteX11 *self)
{
}

static void
meta_cursor_sprite_x11_class_init (MetaCursorSpriteX11Class *klass)
{
  MetaCursorSpriteClass *parent_class = META_CURSOR_SPRITE_CLASS (klass);

  parent_class->update_position = meta_cursor_sprite_x11_update_position;
  parent_class->get_current_rect = meta_cursor_sprite_x11_get_current_rect;
  parent_class->get_current_scale = meta_cursor_sprite_x11_get_current_scale;
}
