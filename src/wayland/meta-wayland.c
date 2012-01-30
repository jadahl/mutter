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

#define CLUTTER_ENABLE_EXPERIMENTAL_API
#define COGL_ENABLE_EXPERIMENTAL_2_0_API
#include <clutter/clutter.h>
#include <clutter/wayland/clutter-wayland-compositor.h>
#include <clutter/wayland/clutter-wayland-surface.h>
#include <clutter/evdev/clutter-evdev.h>

#include <glib.h>
#include <sys/time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <xf86drm.h>

#include <wayland-server.h>

#include "xserver-server-protocol.h"

#include "meta-wayland-private.h"
#include "meta-tty.h"
#include "meta-wayland-stage.h"
#include "meta-window-actor-private.h"
#include "display-private.h"
#include "window-private.h"
#include <meta/types.h>
#include <meta/main.h>
#include "frame.h"

static gboolean on_sigusr_channel_io (GIOChannel *channel,
                                      GIOCondition condition,
                                      void *user_data);

static MetaWaylandCompositor _meta_wayland_compositor;
static int sigusr_pipe_fds[2];

MetaWaylandCompositor *
meta_wayland_compositor_get_default (void)
{
  return &_meta_wayland_compositor;
}

static guint32
get_time (void)
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static gboolean
wayland_event_source_prepare (GSource *base, int *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
wayland_event_source_check (GSource *base)
{
  WaylandEventSource *source = (WaylandEventSource *)base;
  return source->pfd.revents;
}

static gboolean
wayland_event_source_dispatch (GSource *base,
                               GSourceFunc callback,
                               void *data)
{
  WaylandEventSource *source = (WaylandEventSource *)base;
  wl_event_loop_dispatch (source->loop, 0);
  return TRUE;
}

static GSourceFuncs wayland_event_source_funcs =
{
  wayland_event_source_prepare,
  wayland_event_source_check,
  wayland_event_source_dispatch,
  NULL
};

static GSource *
wayland_event_source_new (struct wl_event_loop *loop)
{
  WaylandEventSource *source;

  source = (WaylandEventSource *) g_source_new (&wayland_event_source_funcs,
                                                sizeof (WaylandEventSource));
  source->loop = loop;
  source->pfd.fd = wl_event_loop_get_fd (loop);
  source->pfd.events = G_IO_IN | G_IO_ERR;
  g_source_add_poll (&source->source, &source->pfd);

  return &source->source;
}

static void
buffer_destroy_callback (struct wl_listener *listener,
                         struct wl_resource *resource,
                         guint32 time)
{
  MetaWaylandBuffer *buffer =
    container_of (listener, MetaWaylandBuffer,
                  buffer_destroy_listener);
  GList *l;

  buffer->wayland_buffer = NULL;

  for (l = buffer->surfaces_attached_to; l; l = l->next)
    {
      MetaWaylandSurface *surface = l->data;
      surface->buffer = NULL;
    }

  g_list_free (buffer->surfaces_attached_to);
  buffer->surfaces_attached_to = NULL;
}

static MetaWaylandBuffer *
meta_wayland_buffer_new (struct wl_buffer *wayland_buffer)
{
  MetaWaylandBuffer *buffer = g_slice_new0 (MetaWaylandBuffer);

  buffer->wayland_buffer = wayland_buffer;
  buffer->surfaces_attached_to = NULL;

  buffer->buffer_destroy_listener.func = buffer_destroy_callback;
  wl_list_insert (wayland_buffer->resource.destroy_listener_list.prev,
                  &buffer->buffer_destroy_listener.link);

  return buffer;
}

static void
meta_wayland_buffer_free (MetaWaylandBuffer *buffer)
{
  GList *l;

  if (buffer->wayland_buffer)
    {
      buffer->wayland_buffer->user_data = NULL;

      wl_list_remove (&buffer->buffer_destroy_listener.link);
    }

  for (l = buffer->surfaces_attached_to; l; l = l->next)
    {
      MetaWaylandSurface *surface = l->data;
      surface->buffer = NULL;
    }

  g_list_free (buffer->surfaces_attached_to);
  g_slice_free (MetaWaylandBuffer, buffer);
}

static void
shm_buffer_created (struct wl_buffer *wayland_buffer)
{
  /* We ignore the buffer until it is attached to a surface */
  wayland_buffer->user_data = NULL;
}

static void
shm_buffer_damaged (struct wl_buffer *wayland_buffer,
		    gint32 x,
                    gint32 y,
                    gint32 width,
                    gint32 height)
{
  MetaWaylandBuffer *buffer = wayland_buffer->user_data;
  GList *l;

  /* We only have an associated MetaWaylandBuffer once the wayland buffer has
   * been attached to a surface. */
  if (!buffer)
    return;

  for (l = buffer->surfaces_attached_to; l; l = l->next)
    {
      MetaWaylandSurface *surface = l->data;
      ClutterWaylandSurface *surface_actor =
        CLUTTER_WAYLAND_SURFACE (surface->actor);
      clutter_wayland_surface_damage_buffer (surface_actor,
                                             wayland_buffer,
                                             x, y, width, height);
    }
}

static void
shm_buffer_destroyed (struct wl_buffer *wayland_buffer)
{
  /* We only have an associated MetaWaylandBuffer once the wayland buffer has
   * been attached to a surface. */
  if (wayland_buffer->user_data)
    meta_wayland_buffer_free ((MetaWaylandBuffer *)wayland_buffer->user_data);
}

const static struct wl_shm_callbacks shm_callbacks = {
  shm_buffer_created,
  shm_buffer_damaged,
  shm_buffer_destroyed
};

static void
meta_wayland_surface_destroy (struct wl_client *wayland_client,
                     struct wl_resource *wayland_resource)
{
  wl_resource_destroy (wayland_resource, get_time ());
}

static void
meta_wayland_surface_detach_buffer (MetaWaylandSurface *surface)
{
  MetaWaylandBuffer *buffer = surface->buffer;

  if (buffer)
    {
      wl_resource_queue_event(&buffer->wayland_buffer->resource,
                              WL_BUFFER_RELEASE);

      buffer->surfaces_attached_to =
        g_list_remove (buffer->surfaces_attached_to, surface);
      if (buffer->surfaces_attached_to == NULL)
        meta_wayland_buffer_free (buffer);
      surface->buffer = NULL;
    }
}

static void
meta_wayland_surface_attach_buffer (struct wl_client *wayland_client,
                                    struct wl_resource *wayland_surface_resource,
                                    struct wl_resource *wayland_buffer_resource,
                                    gint32 dx, gint32 dy)
{
  struct wl_buffer *wayland_buffer = wayland_buffer_resource->data;
  MetaWaylandBuffer *buffer = wayland_buffer->user_data;
  MetaWaylandSurface *surface = wayland_surface_resource->data;
  ClutterWaylandSurface *surface_actor;

  /* XXX: in the case where we are reattaching the same buffer we can
   * simply bail out. Note this is important because if we don't bail
   * out then the _detach_buffer will actually end up destroying the
   * buffer we're trying to attach. */
  if (buffer && surface->buffer == buffer)
    return;

  meta_wayland_surface_detach_buffer (surface);

  if (!buffer)
    {
      buffer = meta_wayland_buffer_new (wayland_buffer);
      wayland_buffer->user_data = buffer;
    }

  g_return_if_fail (g_list_find (buffer->surfaces_attached_to, surface) == NULL);

  buffer->surfaces_attached_to = g_list_prepend (buffer->surfaces_attached_to,
                                                 surface);

  /* XXX: Its a bit messy but even though xwayland surfaces are
   * handled separately we still set surface->actor to the
   * MetaShapedTexture actor thats created for a MetaWindowActor.
   * This means we can consistently deal with damage and attaching
   * buffers to surfaces.
   */
  if (surface->actor)
    {
      surface_actor = CLUTTER_WAYLAND_SURFACE (surface->actor);
      clutter_actor_set_reactive (surface->actor, TRUE);
      if (!clutter_wayland_surface_attach_buffer (surface_actor, wayland_buffer,
                                                  NULL))
        g_warning ("Failed to attach buffer to ClutterWaylandSurface");

      g_assert (surface->window);

      if (surface->window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
        {
          int x, y;
          meta_window_get_position (surface->window, &x, &y);
          meta_window_move_resize (surface->window,
                                   TRUE,
                                   x, y,
                                   wayland_buffer->width,
                                   wayland_buffer->height);
        }
    }

  surface->buffer = buffer;
}

static void
meta_wayland_surface_damage (struct wl_client *client,
                             struct wl_resource *surface_resource,
                             gint32 x,
                             gint32 y,
                             gint32 width,
                             gint32 height)
{
  MetaWaylandSurface *surface = surface_resource->data;
  if (surface->buffer && surface->actor)
    {
      clutter_wayland_surface_damage_buffer (CLUTTER_WAYLAND_SURFACE (surface->actor),
                                             surface->buffer->wayland_buffer,
                                             x, y, width, height);
    }
}

static void
destroy_frame_callback (struct wl_resource *callback_resource)
{
  MetaWaylandFrameCallback *callback = callback_resource->data;

  g_queue_unlink (&callback->compositor->frame_callbacks,
                  &callback->node);

  g_slice_free (MetaWaylandFrameCallback, callback);
}

static void
meta_wayland_surface_frame (struct wl_client *client,
                            struct wl_resource *surface_resource,
                            guint32 callback_id)
{
  MetaWaylandFrameCallback *callback;
  MetaWaylandSurface *surface = surface_resource->data;

  callback = g_slice_new0 (MetaWaylandFrameCallback);
  callback->compositor = surface->compositor;
  callback->node.data = callback;
  callback->resource.object.interface = &wl_callback_interface;
  callback->resource.object.id = callback_id;
  callback->resource.destroy = destroy_frame_callback;
  callback->resource.data = callback;

  wl_client_add_resource (client, &callback->resource);

  g_queue_push_tail_link (&surface->compositor->frame_callbacks,
                          &callback->node);
}

const struct wl_surface_interface meta_wayland_surface_interface = {
  meta_wayland_surface_destroy,
  meta_wayland_surface_attach_buffer,
  meta_wayland_surface_damage,
  meta_wayland_surface_frame
};

/* This should be called whenever the window stacking changes to
   update the current position on all of the input devices */
void
meta_wayland_compositor_repick (MetaWaylandCompositor *compositor)
{
  meta_wayland_input_device_repick (compositor->input_device,
                                    get_time (),
                                    NULL);
}

void
meta_wayland_compositor_set_input_focus (MetaWaylandCompositor *compositor,
                                         MetaWindow            *window)
{
  struct wl_surface *surface = NULL;

  if (window)
    {
      MetaWindowActor *window_actor =
        META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
      ClutterActor *shaped_texture =
        meta_window_actor_get_shaped_texture (window_actor);

      if (CLUTTER_WAYLAND_IS_SURFACE (shaped_texture))
        {
          ClutterWaylandSurface *surface_actor =
            CLUTTER_WAYLAND_SURFACE (shaped_texture);

          surface = clutter_wayland_surface_get_surface (surface_actor);
        }
    }

  wl_input_device_set_keyboard_focus ((struct wl_input_device *)
                                      compositor->input_device,
                                      (struct wl_surface *) surface,
                                      get_time ());
  wl_data_device_set_keyboard_focus ((struct wl_input_device *)
                                      compositor->input_device);
}

static void
surface_actor_destroyed_cb (void *user_data,
                            GObject *old_object)
{
  MetaWaylandSurface *surface = user_data;

  surface->actor = NULL;
  surface->window = NULL;
}

static void
meta_wayland_surface_free (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = surface->compositor;
  compositor->surfaces = g_list_remove (compositor->surfaces, surface);
  meta_wayland_surface_detach_buffer (surface);

  if (surface->actor)
    g_object_weak_unref (G_OBJECT (surface->actor),
                         surface_actor_destroyed_cb,
                         surface);

  /* NB: If the surface corresponds to an X window then we will be
   * sure to free the MetaWindow according to some X event. */
  if (surface->window &&
      surface->window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      MetaDisplay *display = meta_get_display ();
      guint32 timestamp = meta_display_get_current_time_roundtrip (display);
      meta_window_unmanage (surface->window, timestamp);
    }

  g_slice_free (MetaWaylandSurface, surface);

  meta_wayland_compositor_repick (compositor);

  if (compositor->implicit_grab_surface == (struct wl_surface *) surface)
    compositor->implicit_grab_surface =
      ((struct wl_input_device *) compositor->input_device)->current;
}

static void
meta_wayland_surface_resource_destroy_cb (struct wl_resource *wayland_surface_resource)
{
  MetaWaylandSurface *surface = wayland_surface_resource->data;
  meta_wayland_surface_free (surface);
}

static void
surface_destroy_callback (struct wl_listener *listener,
                          struct wl_resource *resource,
                          guint32 time)
{
  g_warning ("Surface destroy callback");
}

static void
meta_wayland_compositor_create_surface (struct wl_client *wayland_client,
                                        struct wl_resource *wayland_compositor_resource,
                                        guint32 id)
{
  MetaWaylandCompositor *compositor = wayland_compositor_resource->data;
  MetaWaylandSurface *surface = g_slice_new0 (MetaWaylandSurface);

  surface->compositor = compositor;

  surface->wayland_surface.resource.destroy =
    meta_wayland_surface_resource_destroy_cb;
  surface->wayland_surface.resource.object.id = id;
  surface->wayland_surface.resource.object.interface = &wl_surface_interface;
  surface->wayland_surface.resource.object.implementation =
          (void (**)(void)) &meta_wayland_surface_interface;
  surface->wayland_surface.resource.data = surface;

  wl_client_add_resource (wayland_client, &surface->wayland_surface.resource);

  surface->surface_destroy_listener.func = surface_destroy_callback;
  wl_list_insert (surface->wayland_surface.resource.destroy_listener_list.prev,
                  &surface->surface_destroy_listener.link);

  compositor->surfaces = g_list_prepend (compositor->surfaces, surface);
}

static void
bind_output (struct wl_client *client,
             void *data,
             guint32 version,
             guint32 id)
{
  MetaWaylandOutput *output = data;
  struct wl_resource *resource =
    wl_client_add_object (client, &wl_output_interface, NULL, id, data);
  GList *l;

  wl_resource_post_event (resource,
                          WL_OUTPUT_GEOMETRY,
                          output->x, output->y,
                          output->width_mm,
                          output->height_mm,
                          0, /* subpixel: unknown */
                          "unknown", /* make */
                          "unknown"); /* model */

  for (l = output->modes; l; l = l->next)
    {
      MetaWaylandMode *mode = l->data;
      wl_resource_post_event (resource,
                              WL_OUTPUT_MODE,
                              mode->flags,
                              mode->width,
                              mode->height,
                              mode->refresh);
    }
}

static void
meta_wayland_compositor_create_output (MetaWaylandCompositor *compositor,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       int width_mm,
                                       int height_mm)
{
  MetaWaylandOutput *output = g_slice_new0 (MetaWaylandOutput);
  MetaWaylandMode *mode;
  float final_width, final_height;

  /* XXX: eventually we will support sliced stages and an output should
   * correspond to a slice/CoglFramebuffer, but for now we only support
   * one output so we make sure it always matches the size of the stage
   */
  clutter_actor_set_size (compositor->stage, width, height);

  /* Read back the actual size we were given.
   * XXX: This really needs re-thinking later though so we know the
   * correct output geometry to use. */
  clutter_actor_get_size (compositor->stage, &final_width, &final_height);
  width = final_width;
  height = final_height;

  output->wayland_output.interface = &wl_output_interface;

  output->x = x;
  output->y = y;
  output->width_mm = width_mm;
  output->height_mm = height_mm;

  wl_display_add_global (compositor->wayland_display,
                         &wl_output_interface,
                         output,
                         bind_output);

  mode = g_slice_new0 (MetaWaylandMode);
  mode->flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
  mode->width = width;
  mode->height = height;
  mode->refresh = 60;

  output->modes = g_list_prepend (output->modes, mode);

  compositor->outputs = g_list_prepend (compositor->outputs, output);
}

const static struct wl_compositor_interface meta_wayland_compositor_interface = {
  meta_wayland_compositor_create_surface,
};

static void
paint_finished_cb (ClutterActor *self, void *user_data)
{
  MetaWaylandCompositor *compositor = user_data;

  while (!g_queue_is_empty (&compositor->frame_callbacks))
    {
      MetaWaylandFrameCallback *callback =
        g_queue_peek_head (&compositor->frame_callbacks);

      wl_resource_post_event (&callback->resource,
                              WL_CALLBACK_DONE, get_time ());
      wl_resource_destroy (&callback->resource, 0);
    }
}

static void
compositor_bind (struct wl_client *client,
		 void *data,
                 guint32 version,
                 guint32 id)
{
  MetaWaylandCompositor *compositor = data;

  wl_client_add_object (client, &wl_compositor_interface,
                        &meta_wayland_compositor_interface, id, compositor);
}

typedef struct
{
  struct wl_grab grab;
  int dx;
  int dy;
  MetaWaylandSurface *surface;
} MetaWaylandMoveState;

static void
noop_grab_focus (struct wl_grab *grab,
                 guint32 time,
                 struct wl_surface *surface,
                 gint32 x,
                 gint32 y)
{
  grab->focus = NULL;
}

static void
move_grab_motion (struct wl_grab *grab,
                  guint32 time,
                  gint32 x,
                  gint32 y)
{
  MetaWaylandMoveState *state = (MetaWaylandMoveState *)grab;
  struct wl_input_device *device = grab->input_device;
  MetaWaylandSurface *surface = state->surface;

  meta_window_move (surface->window,
                    TRUE,
                    device->x + state->dx,
                    device->y + state->dy);
}

static void
move_grab_button (struct wl_grab *grab,
                  guint32 time,
                  gint32 button,
                  gint32 state)
{
  struct wl_input_device *device = grab->input_device;

  if (device->button_count == 0 && state == 0)
    {
      wl_input_device_end_grab(device, time);
      g_slice_free (MetaWaylandMoveState, (MetaWaylandMoveState *)grab);
  }
}


static const struct wl_grab_interface move_grab_interface = {
    noop_grab_focus,
    move_grab_motion,
    move_grab_button,
};

static int
meta_wayland_surface_move (MetaWaylandSurface *surface,
                           MetaWaylandInputDevice *input_device,
                           guint32 time)
{
  MetaWaylandMoveState *state = g_slice_new (MetaWaylandMoveState);
  struct wl_input_device *wayland_device =
    (struct wl_input_device *)input_device;
  int x, y;

  meta_window_get_position (surface->window, &x, &y);

  state->grab.interface = &move_grab_interface;
  state->dx = x - wayland_device->grab_x;
  state->dy = y - wayland_device->grab_y;
  state->surface = surface;

  wl_input_device_start_grab (wayland_device, &state->grab, time);

  wl_input_device_set_pointer_focus (wayland_device,
                                     NULL, time, 0, 0, 0, 0);

  return 0;
}

static void
shell_surface_move (struct wl_client *client,
                    struct wl_resource *resource,
		    struct wl_resource *input_resource,
                    guint32 time)
{
  MetaWaylandInputDevice *input_device = input_resource->data;
  struct wl_input_device *wayland_device =
    (struct wl_input_device *)input_device;
  MetaWaylandShellSurface *shell_surface = resource->data;

  if (wayland_device->button_count == 0 ||
      wayland_device->grab_time != time ||
      wayland_device->pointer_focus !=
      &shell_surface->surface->wayland_surface)
    return;

  if (meta_wayland_surface_move (shell_surface->surface,
                                 input_device, time) < 0)
    wl_resource_post_no_memory (resource);
}

static void
shell_surface_resize (struct wl_client *client,
                      struct wl_resource *resource,
                      struct wl_resource *input_resource,
                      guint32 time,
                      guint32 edges)
{
}

static void
ensure_surface_window (MetaWaylandSurface *surface)
{
  MetaDisplay *display = meta_get_display ();

  if (!surface->window)
    {
      ClutterActor *window_actor;
      int width, height;

      if (surface->buffer && surface->buffer->wayland_buffer)
        {
          struct wl_buffer *buffer = surface->buffer->wayland_buffer;
          width = buffer->width;
          height = buffer->width;
        }
      else
        {
          width = 0;
          height = 0;
        }

      surface->window =
        meta_window_new_for_wayland (display, width, height, surface);

      /* The new MetaWindow should always result in us creating a corresponding
       * MetaWindowActor which will be immediately associated with the given
       * surface... */
      g_assert (surface->actor);

      /* If the MetaWindow becomes unmanaged (surface->actor will be freed in
       * this case) we need to make sure to clear our ->actor and ->window
       * pointers. */
      g_object_weak_ref (G_OBJECT (surface->actor),
                         surface_actor_destroyed_cb,
                         surface);
    }
}

static void
shell_surface_set_toplevel (struct wl_client *client,
                            struct wl_resource *resource)
{
  MetaWaylandCompositor *compositor = &_meta_wayland_compositor;
  MetaWaylandShellSurface *shell_surface = resource->data;
  MetaWaylandSurface *surface = shell_surface->surface;

  /* NB: Surfaces from xwayland become managed based on X events. */
  if (client == compositor->xwayland_client)
    return;

  ensure_surface_window (surface);

  meta_window_unmake_fullscreen (surface->window);
}

static void
shell_surface_set_transient (struct wl_client *client,
                             struct wl_resource *resource,
                             struct wl_resource *parent_resource,
                             int x,
                             int y,
                             guint32 flags)
{
  MetaWaylandCompositor *compositor = &_meta_wayland_compositor;
  MetaWaylandShellSurface *shell_surface = resource->data;
  MetaWaylandSurface *surface = shell_surface->surface;

  /* NB: Surfaces from xwayland become managed based on X events. */
  if (client == compositor->xwayland_client)
    return;

  ensure_surface_window (surface);
}

static void
shell_surface_set_fullscreen (struct wl_client *client,
                              struct wl_resource *resource)
{
  MetaWaylandCompositor *compositor = &_meta_wayland_compositor;
  MetaWaylandShellSurface *shell_surface = resource->data;
  MetaWaylandSurface *surface = shell_surface->surface;

  /* NB: Surfaces from xwayland become managed based on X events. */
  if (client == compositor->xwayland_client)
    return;

  ensure_surface_window (surface);

  meta_window_make_fullscreen (surface->window);
}

static const struct wl_shell_surface_interface meta_wayland_shell_surface_interface =
{
  shell_surface_move,
  shell_surface_resize,
  shell_surface_set_toplevel,
  shell_surface_set_transient,
  shell_surface_set_fullscreen
};

static void
shell_handle_surface_destroy (struct wl_listener *listener,
                              struct wl_resource *resource,
                              guint32 time)
{
  MetaWaylandShellSurface *shell_surface = container_of (listener,
                                                         MetaWaylandShellSurface,
                                                         surface_destroy_listener);

  shell_surface->surface->has_shell_surface = FALSE;
  shell_surface->surface = NULL;
  wl_resource_destroy (&shell_surface->resource, time);
}

static void
destroy_shell_surface (struct wl_resource *resource)
{
  MetaWaylandShellSurface *shell_surface = resource->data;

  /* In case cleaning up a dead client destroys shell_surface first */
  if (shell_surface->surface)
    {
      wl_list_remove (&shell_surface->surface_destroy_listener.link);
      shell_surface->surface->has_shell_surface = FALSE;
    }

  g_free (shell_surface);
}

static void
get_shell_surface (struct wl_client *client,
                   struct wl_resource *resource,
                   guint32 id,
                   struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = surface_resource->data;
  MetaWaylandShellSurface *shell_surface;

  if (surface->has_shell_surface)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "wl_shell::get_shell_surface already requested");
      return;
    }

  shell_surface = g_new0 (MetaWaylandShellSurface, 1);
  shell_surface->resource.destroy = destroy_shell_surface;
  shell_surface->resource.object.id = id;
  shell_surface->resource.object.interface = &wl_shell_surface_interface;
  shell_surface->resource.object.implementation =
    (void (**) (void)) &meta_wayland_shell_surface_interface;
  shell_surface->resource.data = shell_surface;

  shell_surface->surface = surface;
  shell_surface->surface_destroy_listener.func = shell_handle_surface_destroy;
  wl_list_insert (surface->wayland_surface.resource.destroy_listener_list.prev,
                  &shell_surface->surface_destroy_listener.link);

  surface->has_shell_surface = TRUE;

  wl_client_add_resource (client, &shell_surface->resource);
}

static const struct wl_shell_interface meta_wayland_shell_interface =
{
  get_shell_surface
};

static void
bind_shell (struct wl_client *client,
            void *data,
            guint32 version,
            guint32 id)
{
  wl_client_add_object (client, &wl_shell_interface,
                        &meta_wayland_shell_interface, id, data);
}

static char *
create_lockfile (int display, int *display_out)
{
  char *filename;
  int size;
  char pid[11];
  int fd;

  do
    {
      char *end;
      pid_t other;

      filename = g_strdup_printf ("/tmp/.X%d-lock", display);
      fd = open (filename, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0444);

      if (fd < 0 && errno == EEXIST)
        {
          fd = open (filename, O_CLOEXEC, O_RDONLY);
          if (fd < 0 || read (fd, pid, 11) != 11)
            {
              const char *msg = strerror (errno);
              g_warning ("can't read lock file %s: %s", filename, msg);
              g_free (filename);

              /* ignore error and try the next display number */
              display++;
              continue;
          }
          close (fd);

          other = strtol (pid, &end, 0);
          if (end != pid + 10)
            {
              g_warning ("can't parse lock file %s", filename);
              g_free (filename);

              /* ignore error and try the next display number */
              display++;
              continue;
          }

          if (kill (other, 0) < 0 && errno == ESRCH)
            {
              g_warning ("unlinking stale lock file %s", filename);
              if (unlink (filename) < 0)
                {
                  const char *msg = strerror (errno);
                  g_warning ("failed to unlink stale lock file: %s", msg);
                  display++;
                }
              g_free (filename);
              continue;
          }

          g_free (filename);
          display++;
          continue;
        }
      else if (fd < 0)
        {
          const char *msg = strerror (errno);
          g_warning ("failed to create lock file %s: %s", filename , msg);
          g_free (filename);
          return NULL;
        }

      break;
    }
  while (1);

  /* Subtle detail: we use the pid of the wayland compositor, not the xserver
   * in the lock file. */
  size = snprintf (pid, 11, "%10d\n", getpid ());
  if (size != 11 || write (fd, pid, 11) != 11)
    {
      unlink (filename);
      close (fd);
      g_warning ("failed to write pid to lock file %s", filename);
      g_free (filename);
      return NULL;
    }

  close (fd);

  *display_out = display;
  return filename;
}

static int
bind_to_abstract_socket (int display)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "%c/tmp/.X11-unix/X%d", 0, display);
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      g_warning ("failed to bind to @%s: %s\n",
                 addr.sun_path + 1, strerror (errno));
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      close (fd);
      return -1;
    }

  return fd;
}

static int
bind_to_unix_socket (int display)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "/tmp/.X11-unix/X%d", display) + 1;
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  unlink (addr.sun_path);
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      char *msg = strerror (errno);
      g_warning ("failed to bind to %s (%s)\n", addr.sun_path, msg);
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0) {
      unlink (addr.sun_path);
      close (fd);
      return -1;
  }

  return fd;
}

static gboolean
start_xwayland (MetaWaylandCompositor *compositor)
{
  int display = 0;
  char *lockfile = NULL;
  int sp[2];
  pid_t pid;

  do
    {
      lockfile = create_lockfile (display, &display);
      if (!lockfile)
        {
         g_warning ("Failed to create an X lock file");
         return FALSE;
        }

      compositor->xwayland_abstract_fd = bind_to_abstract_socket (display);
      if (compositor->xwayland_abstract_fd < 0 ||
          compositor->xwayland_abstract_fd == EADDRINUSE)
        {
          unlink (lockfile);
          display++;
          continue;
        }
      compositor->xwayland_unix_fd = bind_to_unix_socket (display);
      if (compositor->xwayland_abstract_fd < 0)
        {
          unlink (lockfile);
          return FALSE;
        }

      break;
    }
  while (1);

  compositor->xwayland_display_index = display;
  compositor->xwayland_lockfile = lockfile;

  /* We want xwayland to be a wayland client so we make a socketpair to setup a
   * wayland protocol connection. */
  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sp) < 0)
    {
      g_warning ("socketpair failed\n");
      unlink (lockfile);
      return 1;
    }

  switch ((pid = fork()))
    {
    case 0:
        {
          char *fd_string;
          char *display_name;
          /* Make sure the client end of the socket pair doesn't get closed
           * when we exec xwayland. */
          int flags = fcntl (sp[1], F_GETFD);
          if (flags != -1)
            fcntl (sp[1], F_SETFD, flags & ~FD_CLOEXEC);

          fd_string = g_strdup_printf ("%d", sp[1]);
          setenv ("WAYLAND_SOCKET", fd_string, 1);
          g_free (fd_string);

          display_name = g_strdup_printf (":%d",
                                          compositor->xwayland_display_index);

          if (execl (XWAYLAND_PATH,
                     XWAYLAND_PATH,
                     display_name,
                     "-wayland",
                     "-rootless",
                     "-retro",
                     /* FIXME: does it make sense to log to the filesystem by
                      * default? */
                     "-logfile", "/tmp/xwayland.log",
                     "-nolisten", "all",
                     "-terminate",
                     NULL) < 0)
            {
              char *msg = strerror (errno);
              g_warning ("xwayland exec failed: %s", msg);
            }
          exit (-1);
          return FALSE;
        }
    default:
      g_message ("forked X server, pid %d\n", pid);

      close (sp[1]);
      compositor->xwayland_client =
        wl_client_create (compositor->wayland_display, sp[0]);

      compositor->xwayland_pid = pid;
      break;

    case -1:
      g_error ("Failed to fork for xwayland server");
      return FALSE;
    }

  return TRUE;
}

static void
stop_xwayland (MetaWaylandCompositor *compositor)
{
  char path[256];

  snprintf (path, sizeof path, "/tmp/.X%d-lock",
            compositor->xwayland_display_index);
  unlink (path);
  snprintf (path, sizeof path, "/tmp/.X11-unix/X%d",
            compositor->xwayland_display_index);
  unlink (path);

  unlink (compositor->xwayland_lockfile);
}

static void
xserver_set_window_id (struct wl_client *client,
                       struct wl_resource *compositor_resource,
                       struct wl_resource *surface_resource,
                       guint32 xid)
{
  MetaWaylandCompositor *compositor = compositor_resource->data;
  MetaWaylandSurface *surface = surface_resource->data;
  MetaDisplay *display = meta_get_display ();
  MetaWindow *window;

  g_return_if_fail (surface->xid == None);

  surface->xid = xid;

  g_hash_table_insert (compositor->window_surfaces, &xid, surface);

  window  = meta_display_lookup_x_window (display, xid);
  if (window)
    {
      MetaWindowActor *window_actor =
        META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
      ClutterWaylandSurface *surface_actor = CLUTTER_WAYLAND_SURFACE (
        meta_window_actor_get_shaped_texture (window_actor));

      clutter_wayland_surface_set_surface (surface_actor,
                                           &surface->wayland_surface);
      if (surface->buffer)
        clutter_wayland_surface_attach_buffer (surface_actor,
                                               surface->buffer->wayland_buffer,
                                               NULL);

      surface->window = window;
      surface->actor = surface_actor;

      /* If the MetaWindow becomes unmanaged (surface->actor will be freed in
       * this case) we need to make sure to clear our ->actor and ->window
       * pointers in this case. */
      g_object_weak_ref (G_OBJECT (surface->actor),
                         surface_actor_destroyed_cb,
                         surface);

#if 0
      if (window->visible_to_compositor)
        meta_compositor_show_window (display->compositor, window,
                                     META_COMP_EFFECT_NONE);
#endif
    }

#warning "FIXME: Handle surface destroy and remove window_surfaces mapping"
}

MetaWaylandSurface *
meta_wayland_lookup_surface_for_xid (guint32 xid)
{
  return g_hash_table_lookup (_meta_wayland_compositor.window_surfaces, &xid);
}

static const struct xserver_interface xserver_implementation = {
    xserver_set_window_id
};

static void
bind_xserver (struct wl_client *client,
	      void *data,
              guint32 version,
              guint32 id)
{
  MetaWaylandCompositor *compositor = data;

  /* If it's a different client than the xserver we launched,
   * don't start the wm. */
  if (client != compositor->xwayland_client)
    return;

  compositor->xserver_resource =
    wl_client_add_object (client, &xserver_interface,
                          &xserver_implementation, id,
                          compositor);

  wl_resource_post_event (compositor->xserver_resource,
                          XSERVER_LISTEN_SOCKET,
                          compositor->xwayland_abstract_fd);

  wl_resource_post_event (compositor->xserver_resource,
                          XSERVER_LISTEN_SOCKET,
                          compositor->xwayland_unix_fd);
  g_warning ("bind_xserver");

  /* Make sure xwayland will recieve the above sockets in a finite
   * time before unblocking the initialization mainloop since we are
   * then going to immediately try and connect to those as the window
   * manager. */
  wl_client_flush (client);

  /* At this point xwayland is all setup to start accepting
   * connections so we can quit the transient initialization mainloop
   * and unblock meta_wayland_init() to continue initializing mutter.
   * */
  g_main_loop_quit (compositor->init_loop);
  compositor->init_loop = NULL;
}

static void
stage_destroy_cb (void)
{
  meta_quit (META_EXIT_SUCCESS);
}

static gboolean
event_cb (ClutterActor *stage,
          const ClutterEvent *event,
          MetaWaylandCompositor *compositor)
{
  struct wl_input_device *device =
    (struct wl_input_device *) compositor->input_device;
  MetaWaylandSurface *surface;
  MetaDisplay *display;
  XMotionEvent xevent;

  meta_wayland_input_device_handle_event (compositor->input_device, event);

  meta_wayland_stage_set_cursor_position (META_WAYLAND_STAGE (stage),
                                          device->x,
                                          device->y);

  if (device->pointer_focus == NULL)
    meta_wayland_stage_set_default_cursor (META_WAYLAND_STAGE (stage));

  /* HACK: for now, the surfaces from Wayland clients aren't
     integrated into Mutter's stacking and Mutter won't give them
     focus on mouse clicks. As a hack to work around this we can just
     give them input focus on mouse clicks so we can at least test the
     keyboard support */
  if (event->type == CLUTTER_BUTTON_PRESS)
    {
      MetaWaylandSurface *surface = (MetaWaylandSurface *) device->current;

      /* Only focus surfaces that wouldn't be handled by the
         corresponding X events */
      if (surface && surface->xid == 0)
        {
          wl_input_device_set_keyboard_focus (device,
                                              (struct wl_surface *) surface,
                                              event->any.time);
          wl_data_device_set_keyboard_focus (device);

          /* Hack to test wayland surface stacking */
          meta_window_raise (surface->window);
        }
    }

  display = meta_get_display ();
  if (!display)
    return FALSE;

  /* We want to synthesize X events for mouse motion events so that we
     don't have to rely on the X server's window position being
     synched with the surface positoin. See the comment in
     event_callback() in display.c */

  switch (event->type)
    {
    case CLUTTER_BUTTON_PRESS:
      if (compositor->implicit_grab_surface == NULL)
        {
          compositor->implicit_grab_button = event->button.button;
          compositor->implicit_grab_surface = device->current;
        }
      return FALSE;

    case CLUTTER_BUTTON_RELEASE:
      if (event->type == CLUTTER_BUTTON_RELEASE &&
          compositor->implicit_grab_surface &&
          event->button.button == compositor->implicit_grab_button)
        compositor->implicit_grab_surface = NULL;
      return FALSE;

    case CLUTTER_MOTION:
      break;

    default:
      return FALSE;
    }

  xevent.type = MotionNotify;
  xevent.is_hint = NotifyNormal;
  xevent.same_screen = TRUE;
  xevent.serial = 0;
  xevent.send_event = False;
  xevent.display = display->xdisplay;
  xevent.root = DefaultRootWindow (display->xdisplay);

  if (compositor->implicit_grab_surface)
    surface = (MetaWaylandSurface *) compositor->implicit_grab_surface;
  else
    surface = (MetaWaylandSurface *) device->current;

  if (surface == (MetaWaylandSurface *) device->current)
    {
      xevent.x = device->current_x;
      xevent.y = device->current_y;
    }
  else if (surface)
    {
      float ax, ay;

      clutter_actor_transform_stage_point (surface->actor,
                                           device->x, device->y,
                                           &ax, &ay);
      xevent.x = ax;
      xevent.y = ay;
    }
  else
    {
      xevent.x = device->x;
      xevent.y = device->y;
    }

  if (surface && surface->xid != None)
    xevent.window = surface->xid;
  else
    xevent.window = xevent.root;

  /* Mutter doesn't really know about the sub-windows. This assumes it
     doesn't care either */
  xevent.subwindow = xevent.window;
  xevent.time = event->any.time;
  xevent.x_root = device->x;
  xevent.y_root = device->y;
  /* The Clutter state flags exactly match the X values */
  xevent.state = clutter_event_get_state (event);

  meta_display_handle_event (display, (XEvent *) &xevent);

  return FALSE;
}

static gboolean
event_emission_hook_cb (GSignalInvocationHint *ihint,
                        guint n_param_values,
                        const GValue *param_values,
                        gpointer data)
{
  MetaWaylandCompositor *compositor = data;
  ClutterActor *actor;
  ClutterEvent *event;

  g_return_val_if_fail (n_param_values == 2, FALSE);

  actor = g_value_get_object (param_values + 0);
  event = g_value_get_boxed (param_values + 1);

  if (actor == NULL)
    return TRUE /* stay connected */;

  /* If this event belongs to the corresponding grab for this event
   * type then the captured-event signal won't be emitted so we have
   * to manually forward it on */

  switch (event->type)
    {
      /* Pointer events */
    case CLUTTER_MOTION:
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
      if (actor == clutter_get_pointer_grab ())
        event_cb (clutter_actor_get_stage (actor),
                  event,
                  compositor);
      break;

      /* Keyboard events */
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      if (actor == clutter_get_keyboard_grab ())
        event_cb (clutter_actor_get_stage (actor),
                  event,
                  compositor);

    default:
      break;
    }

  return TRUE /* stay connected */;
}

static void
on_vt_enter (MetaWaylandCompositor *compositor)
{
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);

  meta_tty_enter_vt (compositor->tty);

  if (drmSetMaster (compositor->drm_fd))
    g_critical ("failed to set master: %m\n");
  cogl_kms_display_queue_modes_reset (cogl_display);
  clutter_actor_queue_redraw (compositor->stage);
  clutter_evdev_reclaim_devices ();

  /* While we are switched away from mutter we run a special mainloop
   * that only responds to the sigusr signals we get when switching
   * vts so now that we have regained focus we can quit that loop...
   */

  if (compositor->sigusr_loop)
    g_main_loop_quit (compositor->sigusr_loop);
}

static GSource *
create_sigusr_source (MetaWaylandCompositor *compositor)
{
  GSource *source = g_io_create_watch (compositor->sigusr_channel, G_IO_IN);
  g_source_set_callback (source, (GSourceFunc)on_sigusr_channel_io,
                         compositor, NULL);
  return source;
}

static void
on_vt_leave (MetaWaylandCompositor *compositor)
{
  GMainContext *tmp_context = g_main_context_new ();
  GSource *source;

  clutter_evdev_release_devices ();
  if (drmDropMaster(compositor->drm_fd) < 0)
    g_warning ("failed to drop master: %m\n");

  /* Now we don't want to do any drawing or service clients
   * until we regain focus so we run a new mainloop that will
   * only respond to the SIGUSR signals we have at vt switch.
   */

  compositor->sigusr_loop = g_main_loop_new (tmp_context, TRUE);

  /* XXX: glib doesn't let you remove a source from a non default
   * context it only lets you destroy it so we have to create a
   * new source... */
  source = create_sigusr_source (compositor);

  g_source_attach (source, tmp_context);

  meta_tty_leave_vt (compositor->tty);

  g_main_loop_run (compositor->sigusr_loop);

  g_source_destroy (source);
  g_main_loop_unref (compositor->sigusr_loop);
  compositor->sigusr_loop = NULL;
  g_main_context_unref (tmp_context);
}

static gboolean
on_sigusr_channel_io (GIOChannel *channel,
                      GIOCondition condition,
                      void *user_data)
{
  MetaWaylandCompositor *compositor = user_data;
  char signal;
  int count;

  for (;;)
    {
      count = read (sigusr_pipe_fds[0], &signal, 1);
      if (count == EINTR)
        continue;
      if (count < 0)
        {
          const char *msg = strerror (errno);
          g_warning ("Error handling signal: %s", msg);
        }
      if (count != 1)
        {
          g_warning ("Unexpectedly failed to read byte from signal pipe\n");
          return TRUE;
        }
      break;
    }
  switch (signal)
    {
    case '1': /* SIGUSR1 */
      on_vt_leave (compositor);
      break;
    case '2': /* SIGUSR2 */
      on_vt_enter (compositor);
      break;
    default:
      g_warning ("Spurious character '%c' read from sigusr signal pipe",
                 signal);
    }

  return TRUE;
}

static void
sigusr_signal_handler (int signum)
{
  if (sigusr_pipe_fds[1] >= 0)
    {
      switch (signum)
        {
        case SIGUSR1:
          write (sigusr_pipe_fds[1], "1", 1);
          break;
        case SIGUSR2:
          write (sigusr_pipe_fds[1], "2", 1);
          break;
        default:
          break;
        }
    }
}

void
meta_wayland_init (void)
{
  MetaWaylandCompositor *compositor = &_meta_wayland_compositor;
  guint event_signal;

  memset (compositor, 0, sizeof (MetaWaylandCompositor));

  compositor->wayland_display = wl_display_create ();
  if (compositor->wayland_display == NULL)
    g_error ("failed to create wayland display");

  /* XXX: Come up with a more elegant approach... */
  if (strcmp (getenv ("CLUTTER_BACKEND"), "eglnative") == 0)
    {
      struct sigaction act;
      sigset_t empty_mask;
      GIOChannel *channel;
      GSource *source;

      pipe (sigusr_pipe_fds);

      channel = g_io_channel_unix_new (sigusr_pipe_fds[0]);
      g_io_channel_set_close_on_unref (channel, TRUE);
      g_io_channel_set_flags (channel, G_IO_FLAG_NONBLOCK, NULL);
      compositor->sigusr_channel = channel;

      source = create_sigusr_source (compositor);
      g_source_attach (source, NULL);
      g_source_unref (source);

      sigemptyset (&empty_mask);
      act.sa_handler = &sigusr_signal_handler;
      act.sa_mask    = empty_mask;
      act.sa_flags   = 0;

      if (sigaction (SIGUSR1,  &act, NULL) < 0)
        g_printerr ("Failed to register SIGUSR1 handler: %s\n",
                    g_strerror (errno));
      if (sigaction (SIGUSR2,  &act, NULL) < 0)
        g_printerr ("Failed to register SIGUSR1 handler: %s\n",
                    g_strerror (errno));

      compositor->tty = meta_tty_create (compositor, 0);
    }

  g_queue_init (&compositor->frame_callbacks);

  if (!wl_display_add_global (compositor->wayland_display,
                              &wl_compositor_interface,
			      compositor,
                              compositor_bind))
    {
      g_printerr ("Failed to register wayland compositor object");
      goto error;
    }

  compositor->wayland_shm = wl_shm_init (compositor->wayland_display,
                                         &shm_callbacks);
  if (!compositor->wayland_shm)
    {
      g_printerr ("Failed to allocate setup wayland shm callbacks");
      goto error;
    }

  compositor->wayland_loop =
    wl_display_get_event_loop (compositor->wayland_display);
  compositor->wayland_event_source =
    wayland_event_source_new (compositor->wayland_loop);

  /* XXX: Here we are setting the wayland event source to have a
   * slightly lower priority than the X event source, because we are
   * much more likely to get confused being told about surface changes
   * relating to X clients when we don't know what's happened to them
   * according to the X protocol.
   *
   * At some point we could perhaps try and get the X protocol proxied
   * over the wayland protocol so that we don't have to worry about
   * synchronizing the two command streams. */
  g_source_set_priority (compositor->wayland_event_source,
                         GDK_PRIORITY_EVENTS + 1);
  g_source_attach (compositor->wayland_event_source, NULL);

  clutter_wayland_set_compositor_display (compositor->wayland_display);

  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS)
    {
      g_printerr ("Failed to initialize Clutter");
      goto error;
    }

  compositor->stage = meta_wayland_stage_new ();
  clutter_stage_set_user_resizable (CLUTTER_STAGE (compositor->stage), FALSE);
  g_signal_connect_after (compositor->stage, "paint",
                          G_CALLBACK (paint_finished_cb), compositor);
  g_signal_connect (compositor->stage, "destroy",
                    G_CALLBACK (stage_destroy_cb), NULL);
  g_signal_connect (compositor->stage, "captured-event",
                    G_CALLBACK (event_cb), compositor);
  /* If something sets a grab on an actor then the captured event
   * signal won't get emitted but we still want to see these events so
   * we can update the cursor position. To make sure we see all events
   * we also install an emission hook on the event signal */
  event_signal = g_signal_lookup ("event", CLUTTER_TYPE_STAGE);
  g_signal_add_emission_hook (event_signal,
                              0 /* detail */,
                              event_emission_hook_cb,
                              compositor, /* hook_data */
                              NULL /* data_destroy */);

  if (compositor->tty)
    {
      ClutterBackend *clutter_backend = clutter_get_default_backend ();
      CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
      CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
      CoglRenderer *cogl_renderer = cogl_display_get_renderer (cogl_display);

      compositor->drm_fd = cogl_kms_renderer_get_kms_fd (cogl_renderer);
    }

  wl_data_device_manager_init (compositor->wayland_display);

  compositor->input_device =
    meta_wayland_input_device_new (compositor->wayland_display,
                                   compositor->stage);

  meta_wayland_compositor_create_output (compositor, 0, 0, 1024, 600, 222, 125);

  if (wl_display_add_global (compositor->wayland_display, &wl_shell_interface,
                             compositor, bind_shell) == NULL)
    {
      g_printerr ("Failed to register a global shell object");
      goto error;
    }

  clutter_actor_show (compositor->stage);

  if (wl_display_add_socket (compositor->wayland_display, "wayland-0"))
    {
      g_printerr ("Failed to create socket");
      goto error;
    }

  wl_display_add_global (compositor->wayland_display,
                         &xserver_interface,
                         compositor,
                         bind_xserver);

  /* We need a mapping from xids to wayland surfaces... */
  compositor->window_surfaces = g_hash_table_new (g_int_hash, g_int_equal);

  /* XXX: It's important that we only try and start xwayland after we
   * have initialized EGL because EGL implements the "wl_drm"
   * interface which xwayland requires to determine what drm device
   * name it should use.
   *
   * By waiting until we've shown the stage above we ensure that the
   * underlying GL resources for the surface have also been allocated
   * and so EGL must be initialized by this point.
   */

  if (!start_xwayland (compositor))
    {
      g_printerr ("Failed to start X Wayland");
      goto error;
    }

  putenv (g_strdup_printf ("DISPLAY=:%d", compositor->xwayland_display_index));

  /* We need to run a mainloop until we know xwayland has a binding
   * for our xserver interface at which point we can assume it's
   * ready to start accepting connections. */
  compositor->init_loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (compositor->init_loop);

  return;
error:
  meta_tty_destroy (compositor->tty);
}

void
meta_wayland_finalize (void)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  if (compositor->tty)
    meta_tty_destroy (compositor->tty);
  stop_xwayland (compositor);
}

void
meta_wayland_handle_sig_child (void)
{
  int status;
  pid_t pid = waitpid (-1, &status, WNOHANG);
  MetaWaylandCompositor *compositor = &_meta_wayland_compositor;

  /* The simplest measure to avoid infinitely re-spawning a crashing
   * X server */
  if (pid == compositor->xwayland_pid)
    {
      if (!WIFEXITED (status))
        g_critical ("X Wayland crashed; aborting");
      else
        {
          /* For now we simply abort if we see the server exit.
           *
           * In the future X will only be loaded lazily for legacy X support
           * but for now it's a hard requirement. */
          g_critical ("Spurious exit of X Wayland server");
        }
    }
}
