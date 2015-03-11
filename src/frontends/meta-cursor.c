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

#include <string.h>

#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xcursor/Xcursor.h>

#ifdef HAVE_WAYLAND
#include <cogl/cogl-wayland-server.h>
#endif

typedef struct
{
  CoglTexture2D *texture;
  int hot_x, hot_y;
} MetaCursorImage;

struct _MetaCursorSpritePrivate
{
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
load_cursor_on_client (MetaCursor cursor)
{
  return XcursorLibraryLoadImage (translate_meta_cursor (cursor),
                                  meta_prefs_get_cursor_theme (),
                                  meta_prefs_get_cursor_size ());
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
meta_cursor_sprite_from_theme (MetaCursor cursor)
{
  MetaCursorSprite *self;
  MetaCursorSpritePrivate *priv;
  XcursorImage *image;

  self = g_object_new (META_TYPE_CURSOR_SPRITE, NULL);
  priv = meta_cursor_sprite_get_instance_private (self);

  priv->cursor = cursor;

  /* TODO: Only load this on-demand when running as X11 CM. */
  image = load_cursor_on_client (priv->cursor);
  if (!image)
    return self;

  meta_cursor_image_load_from_xcursor_image (self, image);
  XcursorImageDestroy (image);

  return self;
}

MetaCursorSprite *
meta_cursor_sprite_from_texture (CoglTexture2D *texture,
                                 int            hot_x,
                                 int            hot_y)
{
  MetaCursorSprite *self;
  MetaCursorSpritePrivate *priv;

  self = g_object_new (META_TYPE_CURSOR_SPRITE, NULL);
  priv = meta_cursor_sprite_get_instance_private (self);

  cogl_object_ref (texture);

  priv->image.texture = texture;
  priv->image.hot_x = hot_x;
  priv->image.hot_y = hot_y;

  return self;
}

#ifdef HAVE_WAYLAND
static void
meta_cursor_image_load_from_buffer (MetaCursorSprite   *self,
                                    struct wl_resource *buffer,
                                    int                 hot_x,
                                    int                 hot_y)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (self);
  MetaCursorImage *image = &priv->image;
  MetaBackend *meta_backend = meta_get_backend ();
  MetaCursorRenderer *renderer =
    meta_backend_get_cursor_renderer (meta_backend);
  ClutterBackend *backend;
  CoglContext *cogl_context;

  image->hot_x = hot_x;
  image->hot_y = hot_y;

  backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (backend);

  image->texture = cogl_wayland_texture_2d_new_from_buffer (cogl_context, buffer, NULL);

  meta_cursor_renderer_realize_cursor_from_wl_buffer (renderer, self, buffer);
}

MetaCursorSprite *
meta_cursor_sprite_from_buffer (struct wl_resource *buffer,
                                int                 hot_x,
                                int                 hot_y)
{
  MetaCursorSprite *self;

  self = g_object_new (META_TYPE_CURSOR_SPRITE, NULL);

  meta_cursor_image_load_from_buffer (self, buffer, hot_x, hot_y);

  return self;
}
#endif

CoglTexture *
meta_cursor_sprite_get_cogl_texture (MetaCursorSprite *self,
                                     int              *hot_x,
                                     int              *hot_y)
{
  MetaCursorSpritePrivate *priv = meta_cursor_sprite_get_instance_private (self);

  /* TODO: This should create the texture on-demand when running as X11 CM. */

  if (hot_x)
    *hot_x = priv->image.hot_x;
  if (hot_y)
    *hot_y = priv->image.hot_y;

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

guint
meta_cursor_sprite_get_width (MetaCursorSprite *self)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (self);

  return cogl_texture_get_width (COGL_TEXTURE (priv->image.texture));
}

guint
meta_cursor_sprite_get_height (MetaCursorSprite *self)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (self);

  return cogl_texture_get_height (COGL_TEXTURE (priv->image.texture));
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
