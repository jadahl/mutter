/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#include "config.h"

#include "meta-cursor-private.h"

#include <meta/errors.h>

#include "display-private.h"
#include "screen-private.h"
#include "meta-backend-private.h"
#ifdef HAVE_WAYLAND
#include "frontends/wayland/meta-cursor-wayland.h"
#endif

#include <string.h>

#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xcursor/Xcursor.h>

struct _MetaCursorSpritePrivate
{
  int current_x;
  int current_y;

  MetaCursor cursor;
  MetaCursorImage image;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorSprite, meta_cursor_sprite, G_TYPE_OBJECT)

static void
meta_cursor_image_free (MetaCursorImage *image)
{
  if (image->texture)
    cogl_object_unref (image->texture);
}

static const char *
translate_meta_cursor (MetaCursor cursor)
{
  switch (cursor)
    {
    case META_CURSOR_DEFAULT:
      return "left_ptr";
    case META_CURSOR_NORTH_RESIZE:
      return "top_side";
    case META_CURSOR_SOUTH_RESIZE:
      return "bottom_side";
    case META_CURSOR_WEST_RESIZE:
      return "left_side";
    case META_CURSOR_EAST_RESIZE:
      return "right_side";
    case META_CURSOR_SE_RESIZE:
      return "bottom_right_corner";
    case META_CURSOR_SW_RESIZE:
      return "bottom_left_corner";
    case META_CURSOR_NE_RESIZE:
      return "top_right_corner";
    case META_CURSOR_NW_RESIZE:
      return "top_left_corner";
    case META_CURSOR_MOVE_OR_RESIZE_WINDOW:
      return "fleur";
    case META_CURSOR_BUSY:
      return "watch";
    case META_CURSOR_DND_IN_DRAG:
      return "dnd-none";
    case META_CURSOR_DND_MOVE:
      return "dnd-move";
    case META_CURSOR_DND_COPY:
      return "dnd-copy";
    case META_CURSOR_DND_UNSUPPORTED_TARGET:
      return "dnd-none";
    case META_CURSOR_POINTING_HAND:
      return "hand2";
    case META_CURSOR_CROSSHAIR:
      return "crosshair";
    case META_CURSOR_IBEAM:
      return "xterm";
    default:
      break;
    }

  g_assert_not_reached ();
}

Cursor
meta_cursor_create_x_cursor (Display    *xdisplay,
                             MetaCursor  cursor)
{
  return XcursorLibraryLoadCursor (xdisplay, translate_meta_cursor (cursor));
}

static XcursorImage *
load_cursor_on_client (MetaCursor cursor, int scale)
{
  return XcursorLibraryLoadImage (translate_meta_cursor (cursor),
                                  meta_prefs_get_cursor_theme (),
                                  meta_prefs_get_cursor_size () * scale);
}

static void
meta_cursor_image_load_from_xcursor_image (MetaCursorSprite *self,
                                           XcursorImage     *xc_image)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (self);
  MetaCursorImage *image = &priv->image;
  MetaBackend *meta_backend = meta_get_backend ();
  MetaCursorRenderer *renderer = meta_backend_get_cursor_renderer (meta_backend);
  uint width, height, rowstride;
  CoglPixelFormat cogl_format;
  ClutterBackend *clutter_backend;
  CoglContext *cogl_context;

  width           = xc_image->width;
  height          = xc_image->height;
  rowstride       = width * 4;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  cogl_format = COGL_PIXEL_FORMAT_BGRA_8888;
#else
  cogl_format = COGL_PIXEL_FORMAT_ARGB_8888;
#endif

  image->hot_x = xc_image->xhot;
  image->hot_y = xc_image->yhot;

  clutter_backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  image->texture = cogl_texture_2d_new_from_data (cogl_context,
                                                  width, height,
                                                  cogl_format,
                                                  rowstride,
                                                  (uint8_t *) xc_image->pixels,
                                                  NULL);

  meta_cursor_renderer_realize_cursor_from_xcursor (renderer, self, xc_image);
}

MetaCursorSprite *
meta_cursor_sprite_new (void)
{
#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    return g_object_new (META_TYPE_CURSOR_SPRITE_WAYLAND, NULL);
  else
#endif
    return g_object_new (META_TYPE_CURSOR_SPRITE, NULL);
}

void
meta_cursor_sprite_load_from_theme (MetaCursorSprite *self,
                                    int scale)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (self);
  XcursorImage *image;

  if (priv->image.texture)
    {
      cogl_object_unref (priv->image.texture);
      priv->image.texture = NULL;
    }

  image = load_cursor_on_client (priv->cursor, scale);
  if (!image)
    return;

  meta_cursor_image_load_from_xcursor_image (self, image);
  XcursorImageDestroy (image);
}

void
meta_cursor_sprite_ensure_cogl_texture (MetaCursorSprite *self, int scale)
{
  MetaCursorSpritePrivate *priv = meta_cursor_sprite_get_instance_private (self);

  if (priv->image.texture)
    return;

  meta_cursor_sprite_load_from_theme (self, scale);
}

MetaCursorSprite *
meta_cursor_sprite_from_theme (MetaCursor cursor)
{
  MetaCursorSprite *self;
  MetaCursorSpritePrivate *priv;

  self = meta_cursor_sprite_new ();
  priv = meta_cursor_sprite_get_instance_private (self);

  priv->cursor = cursor;

  return self;
}

MetaCursorSprite *
meta_cursor_sprite_from_texture (CoglTexture2D *texture,
                                 int            hot_x,
                                 int            hot_y)
{
  MetaCursorSprite *self;
  MetaCursorSpritePrivate *priv;

  self = meta_cursor_sprite_new ();
  priv = meta_cursor_sprite_get_instance_private (self);

  cogl_object_ref (texture);

  priv->image.texture = texture;
  priv->image.hot_x = hot_x;
  priv->image.hot_y = hot_y;

  return self;
}

CoglTexture *
meta_cursor_sprite_get_cogl_texture (MetaCursorSprite *self)
{
  MetaCursorSpritePrivate *priv = meta_cursor_sprite_get_instance_private (self);
  guint scale = meta_cursor_sprite_get_current_scale (self);

  meta_cursor_sprite_ensure_cogl_texture (self, scale);

  return COGL_TEXTURE (priv->image.texture);
}

MetaCursor
meta_cursor_sprite_get_meta_cursor (MetaCursorSprite *self)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (self);

  return priv->cursor;
}

void
meta_cursor_sprite_get_hotspot (MetaCursorSprite *self,
                                int              *hot_x,
                                int              *hot_y)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (self);

  *hot_x = priv->image.hot_x;
  *hot_y = priv->image.hot_y;
}

MetaCursorImage *
meta_cursor_sprite_get_image (MetaCursorSprite *self)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (self);

  return &priv->image;
}

void
meta_cursor_sprite_update_position (MetaCursorSprite *self,
                                    int               x,
                                    int               y)
{
  if (META_CURSOR_SPRITE_GET_CLASS (self)->update_position)
    META_CURSOR_SPRITE_GET_CLASS (self)->update_position (self, x, y);
  else
    {
      MetaCursorSpritePrivate *priv =
        meta_cursor_sprite_get_instance_private (self);

      priv->current_x = x;
      priv->current_y = y;
    }
}

void
meta_cursor_sprite_get_current_rect (MetaCursorSprite *self,
                                     MetaRectangle    *rect)
{
  if (META_CURSOR_SPRITE_GET_CLASS (self)->get_current_rect)
    META_CURSOR_SPRITE_GET_CLASS (self)->get_current_rect (self, rect);
  else
    {
      MetaCursorSpritePrivate *priv =
        meta_cursor_sprite_get_instance_private (self);
      gint hot_x, hot_y;
      guint width, height;

      meta_cursor_sprite_ensure_cogl_texture (self, 1);

      meta_cursor_sprite_get_hotspot (self, &hot_x, &hot_y);
      width = cogl_texture_get_width (priv->image.texture);
      height = cogl_texture_get_height (priv->image.texture);

      rect->x = priv->current_x - hot_x;
      rect->y = priv->current_y - hot_y;
      rect->width = width;
      rect->height = height;
    }
}

guint
meta_cursor_sprite_get_current_scale (MetaCursorSprite *self)
{
  if (META_CURSOR_SPRITE_GET_CLASS (self)->get_current_scale)
    return META_CURSOR_SPRITE_GET_CLASS (self)->get_current_scale (self);
  else
    return 1;
}

static void
meta_cursor_sprite_init (MetaCursorSprite *self)
{
}

static void
meta_cursor_sprite_finalize (GObject *object)
{
  MetaCursorSprite *self = META_CURSOR_SPRITE (object);
  MetaCursorSpritePrivate *priv = meta_cursor_sprite_get_instance_private (self);

  meta_cursor_image_free (&priv->image);

  G_OBJECT_CLASS (meta_cursor_sprite_parent_class)->finalize (object);
}

static void
meta_cursor_sprite_class_init (MetaCursorSpriteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_cursor_sprite_finalize;
}
