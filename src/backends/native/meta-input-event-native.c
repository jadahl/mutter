/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by:
 *      Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/native/meta-input-event-native.h"

#include "clutter/clutter.h"
#include "clutter/clutter-event-private.h"

typedef struct _MetaInputEventNative
{
  uint32_t evcode;

  uint64_t time_usec;

  gboolean has_relative_motion;
  double dx;
  double dy;
  double dx_unaccel;
  double dy_unaccel;
} MetaInputEventNative;

static MetaInputEventNative *
meta_input_event_native_new (void)
{
  return g_slice_new0 (MetaInputEventNative);
}

MetaInputEventNative *
meta_input_event_native_copy (MetaInputEventNative *event_native)
{
  if (event_native != NULL)
    return g_slice_dup (MetaInputEventNative, event_native);

  return NULL;
}

void
meta_input_event_native_free (MetaInputEventNative *event_native)
{
  if (event_native != NULL)
    g_slice_free (MetaInputEventNative, event_native);
}

static MetaInputEventNative *
meta_input_event_native_event_ensure_platform_data (ClutterEvent *event)
{
  MetaInputEventNative *event_native = _clutter_event_get_platform_data (event);

  if (!event_native)
    {
      event_native = meta_input_event_native_new ();
      _clutter_event_set_platform_data (event, event_native);
    }

  return event_native;
}

void
meta_input_event_native_set_event_code (ClutterEvent *event,
                                        uint32_t      evcode)
{
  MetaInputEventNative *event_native;

  event_native = meta_input_event_native_event_ensure_platform_data (event);
  event_native->evcode = evcode;
}

void
meta_input_event_native_set_time_usec (ClutterEvent *event,
                                       uint64_t      time_usec)
{
  MetaInputEventNative *event_native;

  event_native = meta_input_event_native_event_ensure_platform_data (event);
  event_native->time_usec = time_usec;
}

void
meta_input_event_native_set_relative_motion (ClutterEvent *event,
                                             double        dx,
                                             double        dy,
                                             double        dx_unaccel,
                                             double        dy_unaccel)
{
  MetaInputEventNative *event_native;

  event_native = meta_input_event_native_event_ensure_platform_data (event);
  event_native->dx = dx;
  event_native->dy = dy;
  event_native->dx_unaccel = dx_unaccel;
  event_native->dy_unaccel = dy_unaccel;
  event_native->has_relative_motion = TRUE;
}

/**
 * meta_input_event_native_get_event_code:
 * @event: a #ClutterEvent
 *
 * Returns the event code of the original event. See linux/input.h for more
 * information.
 *
 * Returns: The event code.
 **/
uint32_t
meta_input_event_native_get_event_code (const ClutterEvent *event)
{
  MetaInputEventNative *event_native = _clutter_event_get_platform_data (event);

  if (event_native)
    return event_native->evcode;

  return 0;
}

/**
 * meta_input_event_native_event_get_time_usec:
 * @event: a #ClutterEvent
 *
 * Returns the time in microsecond granularity, or 0 if unavailable.
 *
 * Returns: The time in microsecond granularity, or 0 if unavailable.
 */
uint64_t
meta_input_event_native_get_time_usec (const ClutterEvent *event)
{
  MetaInputEventNative *event_native = _clutter_event_get_platform_data (event);

  if (event_native)
    return event_native->time_usec;

  return 0;
}

/**
 * meta_input_event_native_event_get_pointer_motion
 * @event: a #ClutterEvent
 *
 * If available, the normal and unaccelerated motion deltas are written
 * to the dx, dy, dx_unaccel and dy_unaccel and TRUE is returned.
 *
 * If unavailable, FALSE is returned.
 *
 * Returns: TRUE on success, otherwise FALSE.
 **/
gboolean
meta_input_event_native_get_relative_motion (const ClutterEvent *event,
                                             double             *dx,
                                             double             *dy,
                                             double             *dx_unaccel,
                                             double             *dy_unaccel)
{
  MetaInputEventNative *event_native = _clutter_event_get_platform_data (event);

  if (event_native && event_native->has_relative_motion)
    {
      if (dx)
        *dx = event_native->dx;
      if (dy)
        *dy = event_native->dy;
      if (dx_unaccel)
        *dx_unaccel = event_native->dx_unaccel;
      if (dy_unaccel)
        *dy_unaccel = event_native->dy_unaccel;
      return TRUE;
    }
  else
    return FALSE;
}
