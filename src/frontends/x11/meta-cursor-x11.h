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

#ifndef META_CURSOR_X11_H
#define META_CURSOR_X11_H

#include "frontends/meta-cursor-private.h"

#define META_TYPE_CURSOR_SPRITE_X11            (meta_cursor_sprite_x11_get_type ())
#define META_CURSOR_SPRITE_X11(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_CURSOR_SPRITE_X11, MetaCursorSpriteX11))
#define META_CURSOR_SPRITE_X11_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_CURSOR_SPRITE_X11, MetaCursorSpriteX11Class))
#define META_IS_CURSOR_SPRITE_X11(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_CURSOR_SPRITE_X11))
#define META_IS_CURSOR_SPRITE_X11_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_CURSOR_SPRITE_X11))
#define META_CURSOR_SPRITE_X11_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_CURSOR_SPRITE_X11, MetaCursorSpriteX11Class))

typedef struct _MetaCursorSpriteX11        MetaCursorSpriteX11;
typedef struct _MetaCursorSpriteX11Class   MetaCursorSpriteX11Class;

struct _MetaCursorSpriteX11
{
  MetaCursorSprite parent;
};

struct _MetaCursorSpriteX11Class
{
  MetaCursorSpriteClass parent_class;
};

GType meta_cursor_sprite_x11_get_type (void) G_GNUC_CONST;

MetaCursorSprite * meta_cursor_sprite_x11_from_texture (CoglTexture2D *texture,
                                                        int            hot_x,
                                                        int            hot_y);


#endif /* META_CURSOR_X11_H */
