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

#ifndef META_CURSOR_RENDERER_H
#define META_CURSOR_RENDERER_H

#include <glib-object.h>
#include <X11/Xcursor/Xcursor.h>

#include <meta/screen.h>
#include "frontends/meta-cursor.h"

#define META_TYPE_CURSOR_RENDERER            (meta_cursor_renderer_get_type ())
#define META_CURSOR_RENDERER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_CURSOR_RENDERER, MetaCursorRenderer))
#define META_CURSOR_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_CURSOR_RENDERER, MetaCursorRendererClass))
#define META_IS_CURSOR_RENDERER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_CURSOR_RENDERER))
#define META_IS_CURSOR_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_CURSOR_RENDERER))
#define META_CURSOR_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_CURSOR_RENDERER, MetaCursorRendererClass))

typedef struct _MetaCursorRenderer        MetaCursorRenderer;
typedef struct _MetaCursorRendererClass   MetaCursorRendererClass;

struct _MetaCursorRenderer
{
  GObject parent;
};

struct _MetaCursorRendererClass
{
  GObjectClass parent_class;

  gboolean (* update_cursor) (MetaCursorRenderer *renderer);
#ifdef HAVE_WAYLAND
  void (* realize_cursor_from_wl_buffer) (MetaCursorRenderer *renderer,
                                          MetaCursorSprite *cursor_sprite,
                                          struct wl_resource *buffer);
#endif
  void (* realize_cursor_from_xcursor) (MetaCursorRenderer *renderer,
                                        MetaCursorSprite *cursor_sprite,
                                        XcursorImage *xc_image);
};

GType meta_cursor_renderer_get_type (void) G_GNUC_CONST;

MetaCursorRenderer * meta_cursor_renderer_new (void);

void meta_cursor_renderer_set_cursor (MetaCursorRenderer *renderer,
                                      MetaCursorSprite   *cursor);

void meta_cursor_renderer_set_position (MetaCursorRenderer *renderer,
                                        int x, int y);

MetaCursorSprite * meta_cursor_renderer_get_cursor (MetaCursorRenderer *renderer);
const MetaRectangle * meta_cursor_renderer_get_rect (MetaCursorRenderer *renderer);

#ifdef HAVE_WAYLAND
void meta_cursor_renderer_realize_cursor_from_wl_buffer (MetaCursorRenderer *renderer,
                                                         MetaCursorSprite   *cursor_sprite,
                                                         struct wl_resource *buffer);
#endif

void meta_cursor_renderer_realize_cursor_from_xcursor (MetaCursorRenderer *renderer,
                                                       MetaCursorSprite   *cursor_sprite,
                                                       XcursorImage       *xc_image);

#endif /* META_CURSOR_RENDERER_H */
