/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "meta-cursor-renderer-x11.h"

#include <X11/extensions/Xfixes.h>

#include <meta/util.h>

#include "meta-backend-x11.h"
#include "meta-stage.h"

struct _MetaCursorRendererX11Private
{
  gboolean server_cursor_visible;
};
typedef struct _MetaCursorRendererX11Private MetaCursorRendererX11Private;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorRendererX11, meta_cursor_renderer_x11, META_TYPE_CURSOR_RENDERER);

static void
show_server_cursor (MetaBackendX11 *backend, MetaCursor cursor)
{
  Display *xdisplay = meta_backend_x11_get_xdisplay (backend);
  Window xwindow = meta_backend_x11_get_xwindow (backend);

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    {
      Cursor xcursor = meta_cursor_create_x_cursor (xdisplay, cursor);
      XDefineCursor (xdisplay, xwindow, xcursor);
      XFlush (xdisplay);
      XFreeCursor (xdisplay, xcursor);
    }
  else
#endif
    XFixesShowCursor (xdisplay, xwindow);
}

#ifdef HAVE_WAYLAND
static Cursor
create_empty_cursor (Display *xdisplay)
{
  XcursorImage *image;
  XcursorPixel *pixels;
  Cursor xcursor;

  image = XcursorImageCreate (1, 1);
  if (image == NULL)
    return None;

  image->xhot = 0;
  image->yhot = 0;

  pixels = image->pixels;
  pixels[0] = 0;

  xcursor = XcursorImageLoadCursor (xdisplay, image);
  XcursorImageDestroy (image);

  return xcursor;
}
#endif

static void
hide_server_cursor (MetaBackendX11 *backend)
{
  Display *xdisplay = meta_backend_x11_get_xdisplay (backend);
  Window xwindow = meta_backend_x11_get_xwindow (backend);

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    {
      Cursor empty_xcursor = create_empty_cursor (xdisplay);
      XDefineCursor (xdisplay, xwindow, empty_xcursor);
      XFlush (xdisplay);
      XFreeCursor (xdisplay, empty_xcursor);
    }
  else
#endif
    XFixesHideCursor (xdisplay, xwindow);
}

static gboolean
meta_cursor_renderer_x11_update_cursor (MetaCursorRenderer *renderer,
                                        MetaCursorSprite *cursor_sprite)
{
  MetaCursorRendererX11 *x11 = META_CURSOR_RENDERER_X11 (renderer);
  MetaCursorRendererX11Private *priv = meta_cursor_renderer_x11_get_instance_private (x11);

  MetaBackendX11 *backend = META_BACKEND_X11 (meta_get_backend ());
  Window xwindow = meta_backend_x11_get_xwindow (backend);

  if (xwindow == None)
    return FALSE;

  gboolean has_server_cursor = FALSE;

  if (cursor_sprite)
    {
      MetaCursor cursor = meta_cursor_sprite_get_meta_cursor (cursor_sprite);

      if (cursor != META_CURSOR_NONE &&
          meta_cursor_sprite_get_current_scale (cursor_sprite) == 1)
        {
          show_server_cursor (backend, cursor);
          has_server_cursor = TRUE;
        }
    }

  if (!has_server_cursor && has_server_cursor != priv->server_cursor_visible)
    hide_server_cursor (backend);
  priv->server_cursor_visible = has_server_cursor;

  return priv->server_cursor_visible;
}

#ifdef HAVE_WAYLAND
static void
meta_cursor_renderer_x11_realize_cursor_from_wl_buffer (MetaCursorRenderer *renderer,
                                                        MetaCursorSprite *cursor_sprite,
                                                        struct wl_resource *buffer)
{
}
#endif

static void
meta_cursor_renderer_x11_realize_cursor_from_xcursor (MetaCursorRenderer *renderer,
                                                      MetaCursorSprite *cursor_sprite,
                                                      XcursorImage *xc_image)
{
}

static void
meta_cursor_renderer_x11_class_init (MetaCursorRendererX11Class *klass)
{
  MetaCursorRendererClass *renderer_class = META_CURSOR_RENDERER_CLASS (klass);

  renderer_class->update_cursor = meta_cursor_renderer_x11_update_cursor;
#ifdef HAVE_WAYLAND
  renderer_class->realize_cursor_from_wl_buffer =
    meta_cursor_renderer_x11_realize_cursor_from_wl_buffer;
#endif
  renderer_class->realize_cursor_from_xcursor =
    meta_cursor_renderer_x11_realize_cursor_from_xcursor;
}

static void
meta_cursor_renderer_x11_init (MetaCursorRendererX11 *x11)
{
  MetaCursorRendererX11Private *priv = meta_cursor_renderer_x11_get_instance_private (x11);

  /* XFixes has no way to retrieve the current cursor visibility. */
  priv->server_cursor_visible = TRUE;
}
