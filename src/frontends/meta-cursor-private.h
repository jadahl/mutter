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

#ifndef META_CURSOR_PRIVATE_H
#define META_CURSOR_PRIVATE_H

#include "meta-cursor.h"

#define META_TYPE_CURSOR_SPRITE            (meta_cursor_sprite_get_type ())
#define META_CURSOR_SPRITE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_CURSOR_SPRITE, MetaCursorSprite))
#define META_CURSOR_SPRITE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_CURSOR_SPRITE, MetaCursorSpriteClass))
#define META_IS_CURSOR_SPRITE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_CURSOR_SPRITE))
#define META_IS_CURSOR_SPRITE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_CURSOR_SPRITE))
#define META_CURSOR_SPRITE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_CURSOR_SPRITE, MetaCursorSpriteClass))

typedef struct _MetaCursorSprite        MetaCursorSprite;
typedef struct _MetaCursorSpritePrivate MetaCursorSpritePrivate;
typedef struct _MetaCursorSpriteClass   MetaCursorSpriteClass;

struct _MetaCursorSprite
{
  GObject parent;
};

struct _MetaCursorSpriteClass
{
  GObjectClass parent_class;
};

GType meta_cursor_sprite_get_type (void) G_GNUC_CONST;

#endif /* META_CURSOR_PRIVATE_H */
