/*
 * Wayland Support
 *
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

#include <config.h>

#define COGL_ENABLE_EXPERIMENTAL_2_0_API
#define CLUTTER_ENABLE_EXPERIMENTAL_API
#include <clutter/clutter.h>

#include <cogl/cogl-wayland-server.h>

#include "meta-wayland-stage.h"

#define META_WAYLAND_DEFAULT_CURSOR_HOTSPOT_X 7
#define META_WAYLAND_DEFAULT_CURSOR_HOTSPOT_Y 4

G_DEFINE_TYPE (MetaWaylandStage, meta_wayland_stage, CLUTTER_TYPE_STAGE);

static void
meta_wayland_stage_dispose (GObject *object)
{
  MetaWaylandStage *self = (MetaWaylandStage *) object;

  if (self->cursor_texture)
    {
      clutter_actor_destroy (self->cursor_texture);
      self->cursor_texture = NULL;
    }

  G_OBJECT_CLASS (meta_wayland_stage_parent_class)->dispose (object);
}

static void
meta_wayland_stage_finalize (GObject *object)
{
  MetaWaylandStage *self = (MetaWaylandStage *) object;

  if (self->default_cursor_image)
    cogl_object_unref (self->default_cursor_image);

  G_OBJECT_CLASS (meta_wayland_stage_parent_class)->finalize (object);
}

static void
meta_wayland_stage_paint (ClutterActor *actor)
{
  MetaWaylandStage *self = META_WAYLAND_STAGE (actor);

  CLUTTER_ACTOR_CLASS (meta_wayland_stage_parent_class)->paint (actor);

  /* Make sure the cursor is always painted on top of all of the other
     actors */
  clutter_actor_paint (self->cursor_texture);
}

static void
update_cursor_position (MetaWaylandStage *self)
{
  clutter_actor_set_position (self->cursor_texture,
                              self->cursor_x - self->cursor_hotspot_x,
                              self->cursor_y - self->cursor_hotspot_y);
}

static void
meta_wayland_stage_allocate (ClutterActor           *actor,
                             const ClutterActorBox  *allocation,
                             ClutterAllocationFlags  flags)
{
  MetaWaylandStage *self = META_WAYLAND_STAGE (actor);

  CLUTTER_ACTOR_CLASS (meta_wayland_stage_parent_class)->allocate (actor,
                                                                   allocation,
                                                                   flags);

  if (self->cursor_texture)
    clutter_actor_allocate_preferred_size (self->cursor_texture, flags);
}

static void
meta_wayland_stage_class_init (MetaWaylandStageClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  ClutterActorClass *actor_class = (ClutterActorClass *) klass;

  gobject_class->dispose = meta_wayland_stage_dispose;
  gobject_class->finalize = meta_wayland_stage_finalize;

  actor_class->paint = meta_wayland_stage_paint;
  actor_class->allocate = meta_wayland_stage_allocate;
}

static CoglTexture *
load_default_cursor_image (void)
{
  CoglTexture *ret;
  GError *error = NULL;
  char *filename;

  filename = g_build_filename (MUTTER_DATADIR,
                               "mutter/cursors/left_ptr.png",
                               NULL);

  ret = cogl_texture_new_from_file (filename,
                                    COGL_TEXTURE_NONE,
                                    COGL_PIXEL_FORMAT_ANY,
                                    &error);
  if (ret == NULL)
    {
      g_warning ("Failed to load default cursor: %s",
                 error->message);
      g_clear_error (&error);
    }

  g_free (filename);

  return ret;
}

static void
meta_wayland_stage_init (MetaWaylandStage *self)
{
  self->cursor_texture = clutter_texture_new ();

  /* We don't want to add this as a container child so that we can
     paint it manually above all of the other actors */
  clutter_actor_set_parent (self->cursor_texture, CLUTTER_ACTOR (self));

  self->default_cursor_image = load_default_cursor_image ();

  meta_wayland_stage_set_default_cursor (self);
}

ClutterActor *
meta_wayland_stage_new (void)
{
  return g_object_new (META_WAYLAND_TYPE_STAGE,
                       "cursor-visible", FALSE,
                       NULL);
}

void
meta_wayland_stage_set_cursor_position (MetaWaylandStage *self,
                                        int               x,
                                        int               y)
{
  self->cursor_x = x;
  self->cursor_y = y;
  update_cursor_position (self);
}

static void
meta_wayland_stage_set_cursor_from_texture (MetaWaylandStage *self,
                                            CoglTexture      *texture,
                                            int               hotspot_x,
                                            int               hotspot_y)
{
  ClutterTexture *cursor_texture =
    CLUTTER_TEXTURE (self->cursor_texture);

  self->cursor_hotspot_x = hotspot_x;
  self->cursor_hotspot_y = hotspot_y;

  clutter_texture_set_cogl_texture (cursor_texture, texture);

  clutter_actor_show (self->cursor_texture);

  update_cursor_position (self);
}

void
meta_wayland_stage_set_invisible_cursor (MetaWaylandStage *self)
{
  ClutterTexture *cursor_texture =
    CLUTTER_TEXTURE (self->cursor_texture);

  clutter_texture_set_cogl_texture (cursor_texture,
                                    self->default_cursor_image);
  clutter_actor_hide (self->cursor_texture);
}

void
meta_wayland_stage_set_default_cursor (MetaWaylandStage *self)
{
  int hotspot_x = META_WAYLAND_DEFAULT_CURSOR_HOTSPOT_X;
  int hotspot_y = META_WAYLAND_DEFAULT_CURSOR_HOTSPOT_Y;

  meta_wayland_stage_set_cursor_from_texture (self,
                                              self->default_cursor_image,
                                              hotspot_x,
                                              hotspot_y);
}

void
meta_wayland_stage_set_cursor_from_buffer (MetaWaylandStage *self,
                                           struct wl_buffer *buffer,
                                           int               hotspot_x,
                                           int               hotspot_y)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  CoglContext *context = clutter_backend_get_cogl_context (backend);
  CoglTexture *texture;
  GError *error = NULL;

  texture = COGL_TEXTURE (cogl_wayland_texture_2d_new_from_buffer (context,
                                                                   buffer,
                                                                   &error));

  if (texture == NULL)
    {
      g_warning ("%s", error->message);
      meta_wayland_stage_set_invisible_cursor (self);
    }
  else
    {
      meta_wayland_stage_set_cursor_from_texture (self,
                                                  texture,
                                                  hotspot_x,
                                                  hotspot_y);

      cogl_object_unref (texture);
    }
}
