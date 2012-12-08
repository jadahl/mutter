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

#include <wayland-server.h>
#include <clutter/clutter.h>
#include <clutter/wayland/clutter-wayland-compositor.h>
#include <clutter/wayland/clutter-wayland-surface.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include "meta-wayland-stage.h"
#include "meta-wayland-private.h"

static void
pointer_set_cursor (struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t serial,
                    struct wl_resource *surface_resource,
                    int32_t hotspot_x,
                    int32_t hotspot_y)
{
  MetaWaylandSurface *surface = surface_resource->data;
  MetaWaylandStage *stage = META_WAYLAND_STAGE (surface->compositor->stage);

  if (surface)
    meta_wayland_stage_set_cursor_from_buffer (stage,
                                               surface->buffer,
                                               hotspot_x, hotspot_y);
  else
    meta_wayland_stage_set_invisible_cursor (stage);
}

const static struct wl_pointer_interface
pointer_interface =
  {
    pointer_set_cursor
  };

static void
get_pointer (struct wl_client *client,
             struct wl_resource *resource,
             uint32_t id)
{
  MetaWaylandSeat *seat = resource->data;
  struct wl_resource *pointer_resource;

  if (!seat->seat.pointer)
    return;

  pointer_resource = wl_client_add_object (client,
                                           &wl_pointer_interface,
                                           &pointer_interface,
                                           id,
                                           seat);
  wl_list_insert (&seat->seat.pointer->resource_list, &pointer_resource->link);
}

static void
get_keyboard (struct wl_client *client,
              struct wl_resource *resource,
              uint32_t id)
{
  MetaWaylandSeat *seat = resource->data;
  struct wl_resource *keyboard_resource;

  if (!seat->seat.keyboard)
    return;

  keyboard_resource = wl_client_add_object (client,
                                            &wl_keyboard_interface,
                                            NULL,
                                            id,
                                            seat);
  wl_list_insert (&seat->seat.keyboard->resource_list,
                  &keyboard_resource->link);
}

const static struct wl_seat_interface
seat_interface =
  {
    get_pointer,
    get_keyboard,
    NULL /* TODO: get_touch */
  };

static void
unbind_seat (struct wl_resource *resource)
{
  wl_list_remove (&resource->link);
  free (resource);
}

static void
send_capabilities (MetaWaylandSeat *seat)
{
  struct wl_resource *resource;
  enum wl_seat_capability caps = 0;

  if (seat->seat.pointer)
    caps |= WL_SEAT_CAPABILITY_POINTER;
  if (seat->seat.keyboard)
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  if (seat->seat.touch)
    caps |= WL_SEAT_CAPABILITY_TOUCH;

  wl_list_for_each (resource, &seat->seat.base_resource_list, link)
    wl_seat_send_capabilities (resource, caps);
}

static void
bind_seat (struct wl_client *client,
           void *data,
           uint32_t version,
           uint32_t id)
{
  MetaWaylandSeat *seat = data;
  struct wl_resource *resource;

  resource = wl_client_add_object (client,
                                   &wl_seat_interface,
                                   &seat_interface,
                                   id,
                                   data);

  wl_list_insert (&seat->seat.base_resource_list, &resource->link);
  resource->destroy = unbind_seat;

  send_capabilities (seat);
}

MetaWaylandSeat *
meta_wayland_seat_new (MetaWaylandCompositor *compositor,
                       ClutterActor *stage)
{
  MetaWaylandSeat *seat = g_new (MetaWaylandSeat, 1);

  wl_seat_init (&seat->seat);
  seat->stage = stage;
  seat->compositor = compositor;

  wl_pointer_init (&seat->pointer);
  wl_seat_set_pointer (&seat->seat, &seat->pointer);
  wl_keyboard_init (&seat->keyboard);
  wl_seat_set_keyboard (&seat->seat, &seat->keyboard);

  wl_display_add_global (compositor->wayland_display,
                         &wl_seat_interface,
                         seat,
                         bind_seat);

  return seat;
}

static void
handle_motion_event (MetaWaylandSeat *seat,
                     const ClutterMotionEvent *event)
{
  seat->seat.pointer->x = event->x;
  seat->seat.pointer->y = event->y;

  meta_wayland_seat_repick (seat, event->source);

  seat->seat.pointer->grab->interface->motion (seat->seat.pointer->grab,
                                               event->time,
                                               seat->seat.pointer->grab->x,
                                               seat->seat.pointer->grab->y);
}

static void
handle_button (MetaWaylandSeat *seat,
               uint32_t time,
               uint32_t button,
               gboolean state)
{
  struct wl_pointer *pointer = seat->seat.pointer;

  if (state)
    {
      if (seat->seat.pointer->button_count == 0)
        {
          struct wl_surface *wayland_surface = pointer->current;
          MetaWaylandSurface *surface;

          pointer->grab_button = button;
          pointer->grab_time = time;
          pointer->grab_x = pointer->x;
          pointer->grab_y = pointer->y;

          if (wayland_surface)
            {
              surface = wayland_surface->resource.data;
              if (surface->window &&
                  surface->window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
                {
                  meta_window_raise (surface->window);
                }
            }
        }

      pointer->button_count++;
    }
  else
    pointer->button_count--;

  pointer->grab->interface->button (seat->seat.pointer->grab,
                                    time,
                                    button,
                                    state);
}

static void
handle_button_event (MetaWaylandSeat *seat,
                     const ClutterButtonEvent *event)
{
  gboolean state = event->type == CLUTTER_BUTTON_PRESS;
  uint32_t button;

  switch (event->button)
    {
      /* The evdev input right and middle button numbers are swapped
         relative to how Clutter numbers them */
    case 2:
      button = BTN_MIDDLE;
      break;

    case 3:
      button = BTN_RIGHT;
      break;

    default:
      button = event->button + BTN_LEFT - 1;
      break;
    }

  handle_button (seat, event->time, button, state);
}

static void
handle_key_event (MetaWaylandSeat *seat,
                  const ClutterKeyEvent *event)
{
  struct wl_keyboard *keyboard = seat->seat.keyboard;
  gboolean state = event->type == CLUTTER_KEY_PRESS;
  guint evdev_code;

  /* We can't do anything with the event if we can't get an evdev
     keycode for it */
  if (event->device == NULL ||
      !clutter_input_device_keycode_to_evdev (event->device,
                                              event->hardware_keycode,
                                              &evdev_code))
    return;

  /* We want to ignore events that are sent because of auto-repeat. In
     the Clutter event stream these appear as a single key press
     event. We can detect that because the key will already have been
     pressed */
  if (state)
    {
      uint32_t *end =
        (void *) ((char *) keyboard->keys.data + keyboard->keys.size);
      uint32_t *k;

      /* Ignore the event if the key is already down */
      for (k = keyboard->keys.data; k < end; k++)
        if (*k == evdev_code)
          return;

      /* Otherwise add the key to the list of pressed keys */
      k = wl_array_add (&keyboard->keys, sizeof (*k));
      *k = evdev_code;
    }
  else
    {
      uint32_t *end =
              (void *) ((char *) keyboard->keys.data + keyboard->keys.size);
      uint32_t *k;

      /* Remove the key from the array */
      for (k = keyboard->keys.data; k < end; k++)
        if (*k == evdev_code)
          {
            *k = *(end - 1);
            keyboard->keys.size -= sizeof (*k);

            goto found;
          }

      g_warning ("unexpected key release event for key 0x%x", evdev_code);

    found:
      (void) 0;
    }

  if (keyboard->focus_resource)
    {
      uint32_t serial =
        wl_display_next_serial (seat->compositor->wayland_display);
      wl_keyboard_send_key (keyboard->focus_resource,
                            serial,
                            event->time,
                            evdev_code,
                            state);
    }
}

static void
handle_scroll_event (MetaWaylandSeat *seat,
                     const ClutterScrollEvent *event)
{
  /* TODO: Handle smooth scrolling */
}

void
meta_wayland_seat_handle_event (MetaWaylandSeat *seat,
                                const ClutterEvent *event)
{
  switch (event->type)
    {
    case CLUTTER_MOTION:
      handle_motion_event (seat, (const ClutterMotionEvent *) event);
      break;

    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      handle_button_event (seat, (const ClutterButtonEvent *) event);
      break;

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      handle_key_event (seat, (const ClutterKeyEvent *) event);
      break;

    case CLUTTER_SCROLL:
      handle_scroll_event (seat, (const ClutterScrollEvent *) event);
      break;

    default:
      break;
    }
}

/* The actor argument can be NULL in which case a Clutter pick will be
   performed to determine the right actor. An actor should only be
   passed if the repick is being performed due to an event in which
   case Clutter will have already performed a pick so we can avoid
   redundantly doing another one */
void
meta_wayland_seat_repick (MetaWaylandSeat *seat,
                          ClutterActor    *actor)
{
  struct wl_pointer *pointer = seat->seat.pointer;
  struct wl_surface *surface;
  MetaWaylandSurface *focus;

  if (actor == NULL)
    {
      ClutterStage *stage = CLUTTER_STAGE (seat->stage);
      actor = clutter_stage_get_actor_at_pos (stage,
                                              CLUTTER_PICK_REACTIVE,
                                              pointer->x, pointer->y);
    }

  if (CLUTTER_WAYLAND_IS_SURFACE (actor))
    {
      ClutterWaylandSurface *wl_surface = CLUTTER_WAYLAND_SURFACE (actor);
      float ax, ay;

      clutter_actor_transform_stage_point (actor,
                                           pointer->x, pointer->y,
                                           &ax, &ay);
      pointer->current_x = ax;
      pointer->current_y = ay;

      surface = clutter_wayland_surface_get_surface (wl_surface);
    }
  else
    surface = NULL;

  if (surface != pointer->current)
    {
      const struct wl_pointer_grab_interface *interface =
        pointer->grab->interface;
      interface->focus (pointer->grab, surface,
                        pointer->current_x, pointer->current_y);
      pointer->current = surface;
    }

  focus = (MetaWaylandSurface *) pointer->grab->focus;
  if (focus)
    {
      float ax, ay;

      clutter_actor_transform_stage_point (focus->actor,
                                           pointer->x, pointer->y,
                                           &ax, &ay);
      pointer->grab->x = ax;
      pointer->grab->y = ay;
    }
}
