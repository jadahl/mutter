/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2010 Intel Corp.
 * Copyright (C) 2014 Jonas Ådahl
 * Copyright (C) 2017 Red Hat
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
 */

#include "config.h"

#include "backends/native/meta-input-native.h"

#include <math.h>
#include <float.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <libinput.h>

#include "backends/native/meta-input-device-native.h"
#include "backends/native/meta-input-device-tool-native.h"
#include "backends/native/meta-input-event-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "backends/native/meta-xkb.h"
#include "clutter/clutter.h"
#include "clutter/clutter-device-manager-private.h"
#include "clutter/clutter-event-private.h"
#include "clutter/clutter-main-private.h"
#include "clutter/clutter-stage-private.h"
#include "meta/util.h"

/*
 * Clutter makes the assumption that two core devices have ID's 2 and 3 (core
 * pointer and core keyboard).
 *
 * Since the two first devices that will ever be created will be the virtual
 * pointer and virtual keyboard of the first seat, we fulfill the made
 * assumptions by having the first device having ID 2 and following 3.
 */
#define INITIAL_DEVICE_ID 2

typedef struct _MetaInputEventFilter
{
  MetaLibinputEventFilter func;
  gpointer data;
  GDestroyNotify destroy_notify;
} MetaInputEventFilter;

typedef struct _LibinputSource
{
  GSource source;

  MetaInputNative *input_native;
  GPollFD event_poll_fd;
} MetaLibinputSource;

struct _MetaInputNative
{
  MetaInput parent;

  struct libinput *libinput;

  ClutterStage *stage;
  gboolean released;

  MetaLibinputSource *libinput_source;

  GSList *devices;
  GSList *seats;

  MetaSeatNative *main_seat;
  struct xkb_keymap *keymap;

  MetaPointerConstrainCallback constrain_callback;
  gpointer constrain_data;
  GDestroyNotify constrain_data_notify;

  MetaRelativeMotionFilter relative_motion_filter;
  gpointer relative_motion_filter_user_data;

  ClutterStageManager *stage_manager;
  guint stage_added_handler;
  guint stage_removed_handler;

  GSList *event_filters;

  gint device_id_next;
  GList *free_device_ids;
};

static void
meta_input_native_event_extender_init (ClutterEventExtenderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaInputNative,
                         meta_input_native,
                         META_TYPE_INPUT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_EVENT_EXTENDER,
                                                meta_input_native_event_extender_init))

static MetaInputNativeOpenDeviceCallback  device_open_callback;
static MetaInputNativeCloseDeviceCallback device_close_callback;
static gpointer                           device_callback_data;
static char *                             evdev_seat_id;

#ifdef WITH_VERBOSE_MODE
static const char *device_type_str[] = {
  "pointer",            /* CLUTTER_POINTER_DEVICE */
  "keyboard",           /* CLUTTER_KEYBOARD_DEVICE */
  "extension",          /* CLUTTER_EXTENSION_DEVICE */
  "joystick",           /* CLUTTER_JOYSTICK_DEVICE */
  "tablet",             /* CLUTTER_TABLET_DEVICE */
  "touchpad",           /* CLUTTER_TOUCHPAD_DEVICE */
  "touchscreen",        /* CLUTTER_TOUCHSCREEN_DEVICE */
  "pen",                /* CLUTTER_PEN_DEVICE */
  "eraser",             /* CLUTTER_ERASER_DEVICE */
  "cursor",             /* CLUTTER_CURSOR_DEVICE */
  "pad",                /* CLUTTER_PAD_DEVICE */
};
#endif /* WITH_VERBOSE_MODE */

static const char *option_xkb_layout = "us";
static const char *option_xkb_variant = "";
static const char *option_xkb_options = "";

static void
meta_input_native_copy_event_data (ClutterEventExtender *event_extender,
                                   const ClutterEvent   *src,
                                   ClutterEvent         *dest)
{
  MetaInputEventNative *event_native;

  event_native = _clutter_event_get_platform_data (src);
  if (event_native)
    _clutter_event_set_platform_data (dest,
                                      meta_input_event_native_copy (event_native));
}

static void
meta_input_native_free_event_data (ClutterEventExtender *event_extender,
                                   ClutterEvent         *event)
{
  MetaInputEventNative *event_native;

  event_native = _clutter_event_get_platform_data (event);
  if (event_native != NULL)
    meta_input_event_native_free (event_native);
}

static void
meta_input_native_event_extender_init (ClutterEventExtenderInterface *iface)
{
  iface->copy_event_data = meta_input_native_copy_event_data;
  iface->free_event_data = meta_input_native_free_event_data;
}

static void
process_events (MetaInputNative *input_native);

static gboolean
libinput_source_prepare (GSource *source,
                         gint    *timeout)
{
  gboolean retval;

  _clutter_threads_acquire_lock ();

  *timeout = -1;
  retval = clutter_events_pending ();

  _clutter_threads_release_lock ();

  return retval;
}

static gboolean
libinput_source_check (GSource *source)
{
  MetaLibinputSource *libinput_source = (MetaLibinputSource *) source;
  gboolean retval;

  _clutter_threads_acquire_lock ();

  retval = ((libinput_source->event_poll_fd.revents & G_IO_IN) ||
            clutter_events_pending ());

  _clutter_threads_release_lock ();

  return retval;
}

static void
dispatch_libinput (MetaInputNative *input_native)
{
  libinput_dispatch (input_native->libinput);
  process_events (input_native);
}

static gboolean
libinput_source_dispatch (GSource     *source,
                          GSourceFunc  callback,
                          gpointer     user_data)
{
  MetaLibinputSource *libinput_source = (MetaLibinputSource *) source;
  MetaInputNative *input_native;
  ClutterEvent *event;

  _clutter_threads_acquire_lock ();

  input_native = libinput_source->input_native;

  /*
   * Don't queue more events if we haven't finished handling the previous batch
   */
  if (clutter_events_pending ())
    goto queue_event;

  dispatch_libinput (input_native);

queue_event:
  event = clutter_event_get ();

  if (event)
    {
      ClutterModifierType event_state;
      ClutterInputDevice *device =
        clutter_event_get_source_device (event);
      MetaSeat *seat = meta_input_device_get_seat (META_INPUT_DEVICE (device));
      MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

      /* Drop events if we don't have any stage to forward them to */
      if (!_clutter_input_device_get_stage (device))
        goto out;

      /* forward the event into clutter for emission etc. */
      _clutter_stage_queue_event (event->any.stage, event, FALSE);

      /* update the device states *after* the event */
      event_state = seat_native->button_state |
        xkb_state_serialize_mods (seat_native->xkb, XKB_STATE_MODS_EFFECTIVE);
      _clutter_input_device_set_state (seat_native->core_pointer, event_state);
      _clutter_input_device_set_state (seat_native->core_keyboard, event_state);
    }

out:
  _clutter_threads_release_lock ();

  return TRUE;
}
static GSourceFuncs libinput_source_funcs = {
  libinput_source_prepare,
  libinput_source_check,
  libinput_source_dispatch,
  NULL
};

static MetaLibinputSource *
libinput_source_new (MetaInputNative *input_native)
{
  GSource *source;
  MetaLibinputSource *libinput_source;
  int fd;

  source = g_source_new (&libinput_source_funcs, sizeof (MetaLibinputSource));
  libinput_source = (MetaLibinputSource *) source;

  libinput_source->input_native = input_native;

  fd = libinput_get_fd (input_native->libinput);
  libinput_source->event_poll_fd.fd = fd;
  libinput_source->event_poll_fd.events = G_IO_IN;

  g_source_set_priority (source, CLUTTER_PRIORITY_EVENTS);
  g_source_add_poll (source, &libinput_source->event_poll_fd);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return libinput_source;
}

static void
libinput_source_free (MetaLibinputSource *libinput_source)
{
  GSource *source = (GSource *) libinput_source;

  meta_topic (META_DEBUG_INPUT, "Removing libinput source");

  close (libinput_source->event_poll_fd.fd);

  g_source_destroy (source);
  g_source_unref (source);
}

static void
queue_event (ClutterEvent *event)
{
  g_assert (clutter_event_get_device (event));
  _clutter_event_push (event, FALSE);
}

void
meta_input_native_constrain_pointer (MetaInputNative    *input_native,
                                     ClutterInputDevice *core_pointer,
                                     uint64_t            time_us,
                                     float               x,
                                     float               y,
                                     float              *new_x,
                                     float              *new_y)
{
  if (input_native->constrain_callback)
    {
      input_native->constrain_callback (core_pointer,
                                        us2ms (time_us),
                                        x, y,
                                        new_x, new_y,
                                        input_native->constrain_data);
    }
  else
    {
      ClutterActor *stage = CLUTTER_ACTOR (input_native->stage);
      float stage_width = clutter_actor_get_width (stage);
      float stage_height = clutter_actor_get_height (stage);

      x = CLAMP (x, 0.f, stage_width - 1);
      y = CLAMP (y, 0.f, stage_height - 1);
    }
}

void
meta_input_native_filter_relative_motion (MetaInputNative    *input_native,
                                          ClutterInputDevice *device,
                                          float               x,
                                          float               y,
                                          float              *dx,
                                          float              *dy)
{
  if (!input_native->relative_motion_filter)
    return;

  input_native->relative_motion_filter (device,
                                        x, y, dx, dy,
                                        input_native->relative_motion_filter_user_data);
}

static ClutterEvent *
new_absolute_motion_event (ClutterInputDevice *input_device,
                           uint64_t            time_us,
                           float               x,
                           float               y,
                           double             *axes)
{
  MetaInputDeviceNative *device_native =
    META_INPUT_DEVICE_NATIVE (input_device);
  MetaSeat *seat = meta_input_device_get_seat (META_INPUT_DEVICE (input_device));
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  MetaInput *input = meta_seat_get_input (META_SEAT (seat_native));
  MetaInputNative *input_native = META_INPUT_NATIVE (input);
  ClutterStage *stage = _clutter_input_device_get_stage (input_device);
  float stage_width, stage_height;
  ClutterEvent *event = NULL;


  stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
  stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

  event = clutter_event_new (CLUTTER_MOTION);

  if (input_native->constrain_callback &&
      clutter_input_device_get_device_type (input_device) != CLUTTER_TABLET_DEVICE)
    {
      input_native->constrain_callback (seat_native->core_pointer,
                                        us2ms (time_us),
                                        seat_native->pointer_x,
                                        seat_native->pointer_y,
                                        &x, &y,
                                        input_native->constrain_data);
    }
  else
    {
      x = CLAMP (x, 0.f, stage_width - 1);
      y = CLAMP (y, 0.f, stage_height - 1);
    }

  meta_input_event_native_set_time_usec (event, time_us);
  event->motion.time = us2ms (time_us);
  event->motion.stage = stage;
  event->motion.device = seat_native->core_pointer;
  meta_xkb_set_event_state (event,
                            seat_native->xkb,
                            seat_native->button_state);
  event->motion.x = x;
  event->motion.y = y;
  meta_input_device_native_translate_coordinates (device_native, stage,
                                                  &event->motion.x,
                                                  &event->motion.y);
  event->motion.axes = axes;
  clutter_event_set_source_device (event, input_device);

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      ClutterInputDeviceTool *last_tool;

      last_tool = meta_input_device_native_get_last_tool (device_native);
      clutter_event_set_device_tool (event, last_tool);
      clutter_event_set_device (event, input_device);
    }
  else
    clutter_event_set_device (event, seat_native->core_pointer);

  _clutter_input_device_set_stage (seat_native->core_pointer, stage);

  if (clutter_input_device_get_device_type (input_device) != CLUTTER_TABLET_DEVICE)
    {
      seat_native->pointer_x = x;
      seat_native->pointer_y = y;
    }

  return event;
}

static void
notify_absolute_motion (ClutterInputDevice *input_device,
                        uint64_t            time_us,
                        float               x,
                        float               y,
                        double             *axes)
{
  ClutterEvent *event;

  event = new_absolute_motion_event (input_device, time_us, x, y, axes);

  queue_event (event);
}

static MetaSeatNative *
seat_from_device (ClutterInputDevice *device)
{
  MetaSeat *seat = meta_input_device_get_seat (META_INPUT_DEVICE (device));

  return META_SEAT_NATIVE (seat);
}

static void
notify_relative_tool_motion (ClutterInputDevice *input_device,
                             uint64_t            time_us,
                             float               dx,
                             float               dy,
                             double             *axes)
{
  MetaSeatNative *seat_native = seat_from_device (input_device);
  MetaInput *input = meta_seat_get_input (META_SEAT (seat_native));
  ClutterEvent *event;
  float x, y;

  x = input_device->current_x + dx;
  y = input_device->current_y + dy;

  meta_input_native_filter_relative_motion (META_INPUT_NATIVE (input),
                                            input_device,
                                            seat_native->pointer_x,
                                            seat_native->pointer_y,
                                            &dx,
                                            &dy);

  event = new_absolute_motion_event (input_device, time_us, x, y, axes);
  meta_input_event_native_set_relative_motion (event, dx, dy, 0, 0);

  queue_event (event);
}

static void
notify_touch_event (ClutterInputDevice *input_device,
                    ClutterEventType    evtype,
                    uint64_t            time_us,
                    uint32_t            slot,
                    double              x,
                    double              y)
{
  MetaInputDeviceNative *device_native =
    META_INPUT_DEVICE_NATIVE (input_device);
  MetaSeatNative *seat_native = seat_from_device (input_device);
  ClutterStage *stage;
  ClutterEvent *event = NULL;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  event = clutter_event_new (evtype);

  meta_input_event_native_set_time_usec (event, time_us);
  event->touch.time = us2ms (time_us);
  event->touch.stage = CLUTTER_STAGE (stage);
  event->touch.device = seat_native->core_pointer;
  event->touch.x = x;
  event->touch.y = y;
  meta_input_device_native_translate_coordinates (device_native, stage,
                                                  &event->touch.x,
                                                  &event->touch.y);

  /* "NULL" sequences are special cased in clutter */
  event->touch.sequence = GINT_TO_POINTER (slot + 1);
  meta_xkb_set_event_state (event,
                            seat_native->xkb,
                            seat_native->button_state);

  if (evtype == CLUTTER_TOUCH_BEGIN ||
      evtype == CLUTTER_TOUCH_UPDATE)
    event->touch.modifier_state |= CLUTTER_BUTTON1_MASK;

  clutter_event_set_device (event, seat_native->core_pointer);
  clutter_event_set_source_device (event, input_device);

  queue_event (event);
}

static void
notify_pinch_gesture_event (ClutterInputDevice          *input_device,
                            ClutterTouchpadGesturePhase  phase,
                            uint64_t                     time_us,
                            double                       dx,
                            double                       dy,
                            double                       angle_delta,
                            double                       scale,
                            int                          n_fingers)
{
  MetaSeatNative *seat_native = seat_from_device (input_device);
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  ClutterPoint pos;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;


  event = clutter_event_new (CLUTTER_TOUCHPAD_PINCH);

  clutter_input_device_get_coords (seat_native->core_pointer, NULL, &pos);

  meta_input_event_native_set_time_usec (event, time_us);
  event->touchpad_pinch.phase = phase;
  event->touchpad_pinch.time = us2ms (time_us);
  event->touchpad_pinch.stage = CLUTTER_STAGE (stage);
  event->touchpad_pinch.x = pos.x;
  event->touchpad_pinch.y = pos.y;
  event->touchpad_pinch.dx = dx;
  event->touchpad_pinch.dy = dy;
  event->touchpad_pinch.angle_delta = angle_delta;
  event->touchpad_pinch.scale = scale;
  event->touchpad_pinch.n_fingers = n_fingers;

  meta_xkb_set_event_state (event,
                            seat_native->xkb,
                            seat_native->button_state);

  clutter_event_set_device (event, seat_native->core_pointer);
  clutter_event_set_source_device (event, input_device);

  queue_event (event);
}

static void
notify_swipe_gesture_event (ClutterInputDevice          *input_device,
                            ClutterTouchpadGesturePhase  phase,
                            uint64_t                     time_us,
                            int                          n_fingers,
                            double                       dx,
                            double                       dy)
{
  MetaSeatNative *seat_native = seat_from_device (input_device);
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  ClutterPoint pos;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  event = clutter_event_new (CLUTTER_TOUCHPAD_SWIPE);

  meta_input_event_native_set_time_usec (event, time_us);
  event->touchpad_swipe.phase = phase;
  event->touchpad_swipe.time = us2ms (time_us);
  event->touchpad_swipe.stage = CLUTTER_STAGE (stage);

  clutter_input_device_get_coords (seat_native->core_pointer, NULL, &pos);
  event->touchpad_swipe.x = pos.x;
  event->touchpad_swipe.y = pos.y;
  event->touchpad_swipe.dx = dx;
  event->touchpad_swipe.dy = dy;
  event->touchpad_swipe.n_fingers = n_fingers;

  meta_xkb_set_event_state (event,
                            seat_native->xkb,
                            seat_native->button_state);

  clutter_event_set_device (event, seat_native->core_pointer);
  clutter_event_set_source_device (event, input_device);

  queue_event (event);
}

static void
notify_proximity (ClutterInputDevice *input_device,
                  uint64_t            time_us,
                  gboolean            in)
{
  MetaInputDeviceNative *device_native =
    META_INPUT_DEVICE_NATIVE (input_device);
  MetaSeatNative *seat_native = seat_from_device (input_device);
  ClutterStage *stage;
  ClutterInputDeviceTool *last_tool;
  ClutterEvent *event = NULL;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  last_tool = meta_input_device_native_get_last_tool (device_native);

  if (in)
    event = clutter_event_new (CLUTTER_PROXIMITY_IN);
  else
    event = clutter_event_new (CLUTTER_PROXIMITY_OUT);

  meta_input_event_native_set_time_usec (event, time_us);

  event->proximity.time = us2ms (time_us);
  event->proximity.stage = CLUTTER_STAGE (stage);
  event->proximity.device = seat_native->core_pointer;
  clutter_event_set_device_tool (event, last_tool);
  clutter_event_set_device (event, seat_native->core_pointer);
  clutter_event_set_source_device (event, input_device);

  _clutter_input_device_set_stage (seat_native->core_pointer, stage);

  queue_event (event);
}

static void
notify_pad_button (ClutterInputDevice *input_device,
                   uint64_t            time_us,
                   uint32_t            button,
                   uint32_t            mode_group,
                   uint32_t            mode,
                   uint32_t            pressed)
{
  MetaSeatNative *seat_native = seat_from_device (input_device);
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (pressed)
    event = clutter_event_new (CLUTTER_PAD_BUTTON_PRESS);
  else
    event = clutter_event_new (CLUTTER_PAD_BUTTON_RELEASE);

  meta_input_event_native_set_time_usec (event, time_us);
  event->pad_button.stage = stage;
  event->pad_button.button = button;
  event->pad_button.group = mode_group;
  event->pad_button.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat_native->core_pointer, stage);

  queue_event (event);
}

static void
notify_pad_strip (ClutterInputDevice *input_device,
                  uint64_t            time_us,
                  uint32_t            strip_number,
                  uint32_t            strip_source,
                  uint32_t            mode_group,
                  uint32_t            mode,
                  double              value)
{
  MetaSeatNative *seat_native = seat_from_device (input_device);
  ClutterInputDevicePadSource source;
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (strip_source == LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER)
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER;
  else
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;

  event = clutter_event_new (CLUTTER_PAD_STRIP);
  meta_input_event_native_set_time_usec (event, time_us);
  event->pad_strip.strip_source = source;
  event->pad_strip.stage = stage;
  event->pad_strip.strip_number = strip_number;
  event->pad_strip.value = value;
  event->pad_strip.group = mode_group;
  event->pad_strip.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat_native->core_pointer, stage);

  queue_event (event);
}

static void
notify_pad_ring (ClutterInputDevice *input_device,
                 uint64_t            time_us,
                 uint32_t            ring_number,
                 uint32_t            ring_source,
                 uint32_t            mode_group,
                 uint32_t            mode,
                 double              angle)
{
  MetaSeatNative *seat_native = seat_from_device (input_device);
  ClutterInputDevicePadSource source;
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (ring_source == LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER)
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER;
  else
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;

  event = clutter_event_new (CLUTTER_PAD_RING);
  meta_input_event_native_set_time_usec (event, time_us);
  event->pad_ring.ring_source = source;
  event->pad_ring.stage = stage;
  event->pad_ring.ring_number = ring_number;
  event->pad_ring.angle = angle;
  event->pad_ring.group = mode_group;
  event->pad_ring.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat_native->core_pointer, stage);

  queue_event (event);
}


static void
meta_input_native_device_added (MetaInputNative        *input_native,
                                struct libinput_device *libinput_device)
{
  struct libinput_seat *libinput_seat;
  MetaSeatNative *seat_native;
  ClutterInputDeviceType type;
  MetaInputDeviceNative *device_native;
  ClutterInputDevice *device;

  libinput_seat = libinput_device_get_seat (libinput_device);
  seat_native = libinput_seat_get_user_data (libinput_seat);
  if (!seat_native)
    {
      /*
       * Clutter has the notion of global "core" pointers and keyboard devices,
       * which are located on the main seat. Make whatever seat comes first the
       * main seat.
       */
      if (!input_native->main_seat->libinput_seat)
        seat_native = input_native->main_seat;
      else
        seat_native = meta_seat_native_new (input_native);

      meta_seat_native_set_libinput_seat (seat_native, libinput_seat);
      input_native->seats = g_slist_append (input_native->seats, seat_native);
    }

  device_native = meta_input_device_native_new (seat_native, libinput_device);
  device = CLUTTER_INPUT_DEVICE (device_native);
  _clutter_input_device_set_stage (device, input_native->stage);

  _clutter_device_manager_add_device (CLUTTER_DEVICE_MANAGER (input_native),
                                      device);

  /* Clutter assumes that device types are exclusive in the
   * ClutterInputDevice API */
  type = clutter_input_device_get_device_type (device);

  if (type == CLUTTER_KEYBOARD_DEVICE)
    {
      _clutter_input_device_set_associated_device (device,
                                                   seat_native->core_keyboard);
      _clutter_input_device_add_slave (seat_native->core_keyboard, device);
    }
  else if (type == CLUTTER_POINTER_DEVICE)
    {
      _clutter_input_device_set_associated_device (device,
                                                   seat_native->core_pointer);
      _clutter_input_device_add_slave (seat_native->core_pointer, device);
    }

  meta_topic (META_DEBUG_INPUT, "Added physical device '%s', type %s",
              clutter_input_device_get_device_name (device),
              device_type_str[type]);
}

static void
meta_input_native_device_removed (MetaInputNative       *input_native,
                                  MetaInputDeviceNative *device_native)
{
  _clutter_device_manager_remove_device (CLUTTER_DEVICE_MANAGER (input_native),
                                         CLUTTER_INPUT_DEVICE (device_native));
}

static void
meta_input_native_add_device (ClutterDeviceManager *manager,
                              ClutterInputDevice   *device)
{
  MetaSeatNative *seat_native = seat_from_device (device);
  MetaInputNative *input_native = META_INPUT_NATIVE (manager);

  seat_native->devices = g_slist_prepend (seat_native->devices, device);
  input_native->devices = g_slist_prepend (input_native->devices, device);
}

static void
meta_input_native_remove_device (ClutterDeviceManager *manager,
                                 ClutterInputDevice   *device)
{
  MetaSeatNative *seat_native = seat_from_device (device);
  MetaInputNative *input_native = META_INPUT_NATIVE (manager);

  seat_native->devices = g_slist_remove (seat_native->devices, device);
  input_native->devices = g_slist_remove (input_native->devices, device);

  if (seat_native->repeat_timer && seat_native->repeat_device == device)
    meta_seat_native_clear_repeat_timer (seat_native);

  g_object_unref (device);
}

static const GSList *
meta_input_native_get_devices (ClutterDeviceManager *manager)
{
  return META_INPUT_NATIVE (manager)->devices;
}

static ClutterInputDevice *
meta_input_native_get_core_device (ClutterDeviceManager   *manager,
                                   ClutterInputDeviceType  type)
{
  MetaInputNative *input_native = META_INPUT_NATIVE (manager);

  switch (type)
    {
    case CLUTTER_POINTER_DEVICE:
      return input_native->main_seat->core_pointer;

    case CLUTTER_KEYBOARD_DEVICE:
      return input_native->main_seat->core_keyboard;

    case CLUTTER_EXTENSION_DEVICE:
    default:
      return NULL;
    }

  return NULL;
}

static ClutterInputDevice *
meta_input_native_get_device (ClutterDeviceManager *manager,
                              gint                  id)
{
  MetaInputNative *input_native = META_INPUT_NATIVE (manager);
  GSList *l;

  for (l = input_native->seats; l; l = l->next)
    {
      MetaSeatNative *seat_native = l->data;
      GSList *k;

      for (k = seat_native->devices; k; k = k->next)
        {
          ClutterInputDevice *device = k->data;

          if (clutter_input_device_get_device_id (device) == id)
            return device;
        }
    }

  return NULL;
}

static void
flush_event_queue (void)
{
  ClutterEvent *event;

  while ((event = clutter_event_get ()) != NULL)
    {
      _clutter_process_event (event);
      clutter_event_free (event);
    }
}

static gboolean
process_base_event (MetaInputNative       *input_native,
                    struct libinput_event *event)
{
  ClutterInputDevice *device;
  struct libinput_device *libinput_device;
  gboolean handled = TRUE;

  switch (libinput_event_get_type (event))
    {
    case LIBINPUT_EVENT_DEVICE_ADDED:
      libinput_device = libinput_event_get_device (event);

      meta_input_native_device_added (input_native, libinput_device);
      break;

    case LIBINPUT_EVENT_DEVICE_REMOVED:
      /* Flush all queued events, there
       * might be some from this device.
       */
      flush_event_queue ();

      libinput_device = libinput_event_get_device (event);

      device = libinput_device_get_user_data (libinput_device);
      meta_input_native_device_removed (input_native,
                                        META_INPUT_DEVICE_NATIVE (device));
      break;

    default:
      handled = FALSE;
    }

  return handled;
}

static ClutterScrollSource
translate_scroll_source (enum libinput_pointer_axis_source source)
{
  switch (source)
    {
    case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
      return CLUTTER_SCROLL_SOURCE_WHEEL;
    case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
      return CLUTTER_SCROLL_SOURCE_FINGER;
    case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
      return CLUTTER_SCROLL_SOURCE_CONTINUOUS;
    default:
      return CLUTTER_SCROLL_SOURCE_UNKNOWN;
    }
}

static double *
translate_tablet_axes (struct libinput_event_tablet_tool *tablet_event,
                       ClutterInputDeviceTool            *tool)
{
  GArray *axes = g_array_new (FALSE, FALSE, sizeof (double));
  struct libinput_tablet_tool *libinput_tool;
  double value;

  libinput_tool = libinput_event_tablet_tool_get_tool (tablet_event);

  value = libinput_event_tablet_tool_get_x (tablet_event);
  g_array_append_val (axes, value);
  value = libinput_event_tablet_tool_get_y (tablet_event);
  g_array_append_val (axes, value);

  if (libinput_tablet_tool_has_distance (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_distance (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_pressure (libinput_tool))
    {
      MetaInputDeviceToolNative *tool_native =
        META_INPUT_DEVICE_TOOL_NATIVE (tool);

      value = libinput_event_tablet_tool_get_pressure (tablet_event);
      value = meta_input_device_tool_native_translate_pressure (tool_native,
                                                                value);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_tilt (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_tilt_x (tablet_event);
      g_array_append_val (axes, value);
      value = libinput_event_tablet_tool_get_tilt_y (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_rotation (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_rotation (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_slider (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_slider_position (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_wheel (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_wheel_delta (tablet_event);
      g_array_append_val (axes, value);
    }

  if (axes->len == 0)
    {
      g_array_free (axes, TRUE);
      return NULL;
    }
  else
    return (double *) g_array_free (axes, FALSE);
}

static void
notify_continuous_axis (MetaSeatNative                *seat_native,
                        ClutterInputDevice            *device,
                        uint64_t                       time_us,
                        ClutterScrollSource            scroll_source,
                        struct libinput_event_pointer *axis_event)
{
  double dx = 0.0, dy = 0.0;
  ClutterScrollFinishFlags finish_flags = CLUTTER_SCROLL_FINISHED_NONE;

  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
    {
      dx = libinput_event_pointer_get_axis_value (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);

      if (fabs (dx) < DBL_EPSILON)
        finish_flags |= CLUTTER_SCROLL_FINISHED_HORIZONTAL;
    }
  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
    {
      dy = libinput_event_pointer_get_axis_value (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

      if (fabs (dy) < DBL_EPSILON)
        finish_flags |= CLUTTER_SCROLL_FINISHED_VERTICAL;
    }

  meta_seat_native_notify_scroll_continuous (seat_native, device, time_us,
                                             dx, dy,
                                             scroll_source, finish_flags);
}

static void
notify_discrete_axis (MetaSeatNative                *seat_native,
                      ClutterInputDevice            *device,
                      uint64_t                       time_us,
                      ClutterScrollSource            scroll_source,
                      struct libinput_event_pointer *axis_event)
{
  double discrete_dx = 0.0, discrete_dy = 0.0;

  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
    {
      discrete_dx = libinput_event_pointer_get_axis_value_discrete (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
    }
  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
    {
      discrete_dy = libinput_event_pointer_get_axis_value_discrete (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
    }

  meta_seat_native_notify_discrete_scroll (seat_native, device,
                                           time_us,
                                           discrete_dx, discrete_dy,
                                           scroll_source);
}

static gboolean
process_device_event (MetaInputNative       *input_native,
                      struct libinput_event *event)
{
  gboolean handled = TRUE;
  struct libinput_device *libinput_device = libinput_event_get_device(event);
  ClutterInputDevice *device;

  switch (libinput_event_get_type (event))
    {
    case LIBINPUT_EVENT_KEYBOARD_KEY:
      {
        uint32_t key, key_state, seat_key_count;
        uint64_t time_us;
        struct libinput_event_keyboard *key_event =
          libinput_event_get_keyboard_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_keyboard_get_time_usec (key_event);
        key = libinput_event_keyboard_get_key (key_event);
        key_state = libinput_event_keyboard_get_key_state (key_event) ==
                    LIBINPUT_KEY_STATE_PRESSED;
        seat_key_count =
          libinput_event_keyboard_get_seat_key_count (key_event);

        /* Ignore key events that are not seat wide state changes. */
        if ((key_state == LIBINPUT_KEY_STATE_PRESSED &&
             seat_key_count != 1) ||
            (key_state == LIBINPUT_KEY_STATE_RELEASED &&
             seat_key_count != 0))
          break;

        meta_seat_native_notify_key (seat_from_device (device),
                                     device,
                                     time_us, key, key_state, TRUE);

        break;
      }

    case LIBINPUT_EVENT_POINTER_MOTION:
      {
        struct libinput_event_pointer *pointer_event =
          libinput_event_get_pointer_event (event);
        uint64_t time_us;
        double dx;
        double dy;
        double dx_unaccel;
        double dy_unaccel;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_pointer_get_time_usec (pointer_event);
        dx = libinput_event_pointer_get_dx (pointer_event);
        dy = libinput_event_pointer_get_dy (pointer_event);
        dx_unaccel = libinput_event_pointer_get_dx_unaccelerated (pointer_event);
        dy_unaccel = libinput_event_pointer_get_dy_unaccelerated (pointer_event);

        meta_seat_native_notify_relative_motion (seat_from_device (device),
                                                 device,
                                                 time_us,
                                                 dx, dy,
                                                 dx_unaccel, dy_unaccel);

        break;
      }

    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
      {
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        ClutterStage *stage;
        struct libinput_event_pointer *motion_event =
          libinput_event_get_pointer_event (event);
        device = libinput_device_get_user_data (libinput_device);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        time_us = libinput_event_pointer_get_time_usec (motion_event);
        x = libinput_event_pointer_get_absolute_x_transformed (motion_event,
                                                               stage_width);
        y = libinput_event_pointer_get_absolute_y_transformed (motion_event,
                                                               stage_height);

        meta_seat_native_notify_absolute_motion (seat_from_device (device),
                                                 device,
                                                 time_us,
                                                 x, y,
                                                 NULL);

        break;
      }

    case LIBINPUT_EVENT_POINTER_BUTTON:
      {
        uint32_t button, button_state, seat_button_count;
        uint64_t time_us;
        struct libinput_event_pointer *button_event =
          libinput_event_get_pointer_event (event);
        device = libinput_device_get_user_data (libinput_device);

        time_us = libinput_event_pointer_get_time_usec (button_event);
        button = libinput_event_pointer_get_button (button_event);
        button_state = libinput_event_pointer_get_button_state (button_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;
        seat_button_count =
          libinput_event_pointer_get_seat_button_count (button_event);

        /* Ignore button events that are not seat wide state changes. */
        if ((button_state == LIBINPUT_BUTTON_STATE_PRESSED &&
             seat_button_count != 1) ||
            (button_state == LIBINPUT_BUTTON_STATE_RELEASED &&
             seat_button_count != 0))
          break;

        meta_seat_native_notify_button (seat_from_device (device), device,
                                        time_us, button, button_state);
        break;
      }

    case LIBINPUT_EVENT_POINTER_AXIS:
      {
        uint64_t time_us;
        enum libinput_pointer_axis_source source;
        struct libinput_event_pointer *axis_event =
          libinput_event_get_pointer_event (event);
        MetaSeatNative *seat_native;
        ClutterScrollSource scroll_source;

        device = libinput_device_get_user_data (libinput_device);
        seat_native = seat_from_device (device);

        time_us = libinput_event_pointer_get_time_usec (axis_event);
        source = libinput_event_pointer_get_axis_source (axis_event);
        scroll_source = translate_scroll_source (source);

        /* libinput < 0.8 sent wheel click events with value 10. Since 0.8
           the value is the angle of the click in degrees. To keep
           backwards-compat with existing clients, we just send multiples of
           the click count. */

        switch (scroll_source)
          {
          case CLUTTER_SCROLL_SOURCE_WHEEL:
            notify_discrete_axis (seat_native, device, time_us, scroll_source,
                                  axis_event);
            break;
          case CLUTTER_SCROLL_SOURCE_FINGER:
          case CLUTTER_SCROLL_SOURCE_CONTINUOUS:
          case CLUTTER_SCROLL_SOURCE_UNKNOWN:
            notify_continuous_axis (seat_native, device, time_us, scroll_source,
                                    axis_event);
            break;
          }
        break;
      }

    case LIBINPUT_EVENT_TOUCH_DOWN:
      {
        uint32_t slot;
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        MetaSeatNative *seat_native;
        ClutterStage *stage;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        seat_native = seat_from_device (device);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        x = libinput_event_touch_get_x_transformed (touch_event,
                                                    stage_width);
        y = libinput_event_touch_get_y_transformed (touch_event,
                                                    stage_height);

        touch_state = meta_seat_native_add_touch (seat_native, slot);
        touch_state->coords.x = x;
        touch_state->coords.y = y;

        notify_touch_event (device, CLUTTER_TOUCH_BEGIN, time_us, slot,
                             touch_state->coords.x, touch_state->coords.y);
        break;
      }

    case LIBINPUT_EVENT_TOUCH_UP:
      {
        uint32_t slot;
        uint64_t time_us;
        MetaSeatNative *seat_native;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        seat_native = seat_from_device (device);

        slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        touch_state = meta_seat_native_get_touch (seat_native, slot);

        notify_touch_event (device, CLUTTER_TOUCH_END, time_us, slot,
                            touch_state->coords.x, touch_state->coords.y);
        meta_seat_native_remove_touch (seat_native, slot);

        break;
      }

    case LIBINPUT_EVENT_TOUCH_MOTION:
      {
        uint32_t slot;
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        MetaSeatNative *seat_native;
        ClutterStage *stage;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        seat_native = seat_from_device (device);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        x = libinput_event_touch_get_x_transformed (touch_event,
                                                    stage_width);
        y = libinput_event_touch_get_y_transformed (touch_event,
                                                    stage_height);

        touch_state = meta_seat_native_get_touch (seat_native, slot);
        touch_state->coords.x = x;
        touch_state->coords.y = y;

        notify_touch_event (device, CLUTTER_TOUCH_UPDATE, time_us, slot,
                            touch_state->coords.x, touch_state->coords.y);
        break;
      }
    case LIBINPUT_EVENT_TOUCH_CANCEL:
      {
        MetaTouchState *touch_state;
        GHashTableIter iter;
        uint64_t time_us;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);
        MetaSeatNative *seat_native;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        seat_native = seat_from_device (device);
        g_hash_table_iter_init (&iter, seat_native->touches);

        while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &touch_state))
          {
            notify_touch_event (device, CLUTTER_TOUCH_CANCEL,
                                time_us, touch_state->id,
                                touch_state->coords.x, touch_state->coords.y);
            g_hash_table_iter_remove (&iter);
          }

        break;
      }
    case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
    case LIBINPUT_EVENT_GESTURE_PINCH_END:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        ClutterTouchpadGesturePhase phase;
        int n_fingers;
        uint64_t time_us;

        if (libinput_event_get_type (event) == LIBINPUT_EVENT_GESTURE_PINCH_BEGIN)
          phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN;
        else
          phase = libinput_event_gesture_get_cancelled (gesture_event) ?
            CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL : CLUTTER_TOUCHPAD_GESTURE_PHASE_END;

        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        notify_pinch_gesture_event (device, phase, time_us, 0, 0, 0, 0, n_fingers);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        double angle_delta, scale, dx, dy;
        int n_fingers;
        uint64_t time_us;

        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        angle_delta = libinput_event_gesture_get_angle_delta (gesture_event);
        scale = libinput_event_gesture_get_scale (gesture_event);
        dx = libinput_event_gesture_get_dx (gesture_event);
        dy = libinput_event_gesture_get_dx (gesture_event);

        notify_pinch_gesture_event (device,
                                    CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
                                    time_us, dx, dy, angle_delta, scale, n_fingers);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
    case LIBINPUT_EVENT_GESTURE_SWIPE_END:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        ClutterTouchpadGesturePhase phase;
        int n_fingers;
        uint64_t time_us;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);

        if (libinput_event_get_type (event) == LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN)
          phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN;
        else
          phase = libinput_event_gesture_get_cancelled (gesture_event) ?
            CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL : CLUTTER_TOUCHPAD_GESTURE_PHASE_END;

        notify_swipe_gesture_event (device, phase, time_us, n_fingers, 0, 0);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        int n_fingers;
        uint64_t time_us;
        double dx, dy;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        dx = libinput_event_gesture_get_dx (gesture_event);
        dy = libinput_event_gesture_get_dy (gesture_event);

        notify_swipe_gesture_event (device,
                                    CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
                                    time_us, n_fingers, dx, dy);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
      {
        uint64_t time;
        double x, y, dx, dy, *axes;
        float stage_width, stage_height;
        ClutterStage *stage;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        MetaInputDeviceNative *device_native;
        ClutterInputDeviceTool *last_tool;

        device = libinput_device_get_user_data (libinput_device);
        device_native = META_INPUT_DEVICE_NATIVE (device);

        stage = _clutter_input_device_get_stage (device);
        if (!stage)
          break;

        last_tool = meta_input_device_native_get_last_tool (device_native);
        axes = translate_tablet_axes (tablet_event, last_tool);
        if (!axes)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        time = libinput_event_tablet_tool_get_time_usec (tablet_event);

        if (clutter_input_device_get_mapping_mode (device) == CLUTTER_INPUT_DEVICE_MAPPING_RELATIVE ||
            clutter_input_device_tool_get_tool_type (last_tool) == CLUTTER_INPUT_DEVICE_TOOL_MOUSE ||
            clutter_input_device_tool_get_tool_type (last_tool) == CLUTTER_INPUT_DEVICE_TOOL_LENS)
          {
            dx = libinput_event_tablet_tool_get_dx (tablet_event);
            dy = libinput_event_tablet_tool_get_dy (tablet_event);
            notify_relative_tool_motion (device, time, dx, dy, axes);
          }
        else
          {
            x = libinput_event_tablet_tool_get_x_transformed (tablet_event, stage_width);
            y = libinput_event_tablet_tool_get_y_transformed (tablet_event, stage_height);
            notify_absolute_motion (device, time, x, y, axes);
          }

        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
      {
        uint64_t time;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        struct libinput_tablet_tool *libinput_tool = NULL;
        enum libinput_tablet_tool_proximity_state state;
        MetaInputDeviceNative *device_native;

        state = libinput_event_tablet_tool_get_proximity_state (tablet_event);
        time = libinput_event_tablet_tool_get_time_usec (tablet_event);
        device = libinput_device_get_user_data (libinput_device);
        device_native = META_INPUT_DEVICE_NATIVE (device);

        libinput_tool = libinput_event_tablet_tool_get_tool (tablet_event);

        if (state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN)
          meta_input_device_native_update_last_tool (device_native,
                                                     libinput_tool);
        notify_proximity (device, time, state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
        if (state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT)
          meta_input_device_native_update_last_tool (device_native, NULL);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
      {
        uint64_t time_us;
        uint32_t button_state;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        guint tablet_button;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_tablet_tool_get_time_usec (tablet_event);
        tablet_button = libinput_event_tablet_tool_get_button (tablet_event);

        button_state = libinput_event_tablet_tool_get_button_state (tablet_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;

        meta_seat_native_notify_button (seat_from_device (device), device,
                                        time_us, tablet_button, button_state);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_TIP:
      {
        uint64_t time_us;
        uint32_t button_state;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_tablet_tool_get_time_usec (tablet_event);

        button_state = libinput_event_tablet_tool_get_tip_state (tablet_event) ==
                       LIBINPUT_TABLET_TOOL_TIP_DOWN;

        meta_seat_native_notify_button (seat_from_device (device), device,
                                        time_us, BTN_TOUCH, button_state);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
      {
        uint64_t time;
        uint32_t button_state, button, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        button = libinput_event_tablet_pad_get_button_number (pad_event);
        button_state = libinput_event_tablet_pad_get_button_state (pad_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;
        notify_pad_button (device, time, button, group, mode, button_state);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_STRIP:
      {
        uint64_t time;
        uint32_t number, source, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);
        double value;

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);
        number = libinput_event_tablet_pad_get_strip_number (pad_event);
        value = libinput_event_tablet_pad_get_strip_position (pad_event);
        source = libinput_event_tablet_pad_get_strip_source (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        notify_pad_strip (device, time, number, source, group, mode, value);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_RING:
      {
        uint64_t time;
        uint32_t number, source, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);
        double angle;

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);
        number = libinput_event_tablet_pad_get_ring_number (pad_event);
        angle = libinput_event_tablet_pad_get_ring_position (pad_event);
        source = libinput_event_tablet_pad_get_ring_source (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        notify_pad_ring (device, time, number, source, group, mode, angle);
        break;
      }
    default:
      handled = FALSE;
    }

  return handled;
}

static gboolean
filter_event (MetaInputNative       *input_native,
              struct libinput_event *event)
{
  gboolean retval = CLUTTER_EVENT_PROPAGATE;
  GSList *tmp_list;

  tmp_list = input_native->event_filters;

  while (tmp_list)
    {
      MetaInputEventFilter *filter = tmp_list->data;

      retval = filter->func (event, filter->data);
      tmp_list = tmp_list->next;

      if (retval != CLUTTER_EVENT_PROPAGATE)
        break;
    }

  return retval;
}

static void
process_event (MetaInputNative       *input_native,
               struct libinput_event *event)
{
  gboolean retval;

  retval = filter_event (input_native, event);

  if (retval != CLUTTER_EVENT_PROPAGATE)
    return;

  if (process_base_event (input_native, event))
    return;
  if (process_device_event (input_native, event))
    return;
}

static void
process_events (MetaInputNative *input_native)
{
  struct libinput_event *event;

  while ((event = libinput_get_event (input_native->libinput)))
    {
      process_event (input_native, event);
      libinput_event_destroy (event);
    }
}

static int
open_restricted (const char *path,
                 int         flags,
                 void       *user_data)
{
  int fd;

  if (device_open_callback)
    {
      GError *error = NULL;

      fd = device_open_callback (path, flags, device_callback_data, &error);

      if (fd < 0)
        {
          g_warning ("Could not open device %s: %s", path, error->message);
          g_error_free (error);
        }
    }
  else
    {
      fd = open (path, O_RDWR | O_NONBLOCK);
      if (fd < 0)
        {
          g_warning ("Could not open device %s: %s", path, strerror (errno));
        }
    }

  return fd;
}

static void
close_restricted (int   fd,
                  void *user_data)
{
  if (device_close_callback)
    device_close_callback (fd, device_callback_data);
  else
    close (fd);
}

static const struct libinput_interface libinput_interface = {
  open_restricted,
  close_restricted
};

static ClutterVirtualInputDevice *
meta_input_native_create_virtual_device (ClutterDeviceManager  *manager,
                                         ClutterInputDeviceType device_type)
{
  MetaInputNative *input_native = META_INPUT_NATIVE (manager);

  return g_object_new (META_TYPE_VIRTUAL_INPUT_DEVICE_NATIVE,
                       "device-manager", manager,
                       "seat", input_native->main_seat,
                       "device-type", device_type,
                       NULL);
}

static void
meta_input_native_compress_motion (ClutterDeviceManager *device_manger,
                                   ClutterEvent         *event,
                                   const ClutterEvent   *to_discard)
{
  double dx, dy;
  double dx_unaccel, dy_unaccel;
  double dst_dx = 0.0, dst_dy = 0.0;
  double dst_dx_unaccel = 0.0, dst_dy_unaccel = 0.0;

  if (!meta_input_event_native_get_relative_motion (to_discard,
                                                    &dx, &dy,
                                                    &dx_unaccel, &dy_unaccel))
    return;

  meta_input_event_native_get_relative_motion (event,
                                               &dst_dx, &dst_dy,
                                               &dst_dx_unaccel, &dst_dy_unaccel);
  meta_input_event_native_set_relative_motion (event,
                                               dx + dst_dx,
                                               dy + dst_dy,
                                               dx_unaccel + dst_dx_unaccel,
                                               dy_unaccel + dst_dy_unaccel);
}

int
meta_input_native_acquire_device_id (MetaInputNative *input_native)
{
  GList *first;
  int next_id;

  if (input_native->free_device_ids == NULL)
    {
      int i;

      /* We ran out of free ID's, so append 10 new ones. */
      for (i = 0; i < 10; i++)
        {
          input_native->free_device_ids =
            g_list_append (input_native->free_device_ids,
                           GINT_TO_POINTER (input_native->device_id_next++));
        }
    }

  first = g_list_first (input_native->free_device_ids);
  next_id = GPOINTER_TO_INT (first->data);
  input_native->free_device_ids =
    g_list_remove_link (input_native->free_device_ids, first);

  return next_id;
}

void
meta_input_native_dispatch (MetaInputNative *input_native)
{
  dispatch_libinput (input_native);
}

static int
compare_ids (gconstpointer a,
             gconstpointer b)
{
  return GPOINTER_TO_INT (a) - GPOINTER_TO_INT (b);
}

void
meta_input_native_release_device_id (MetaInputNative    *input_native,
                                     ClutterInputDevice *device)
{
  gint device_id;

  device_id = clutter_input_device_get_device_id (device);
  input_native->free_device_ids =
    g_list_insert_sorted (input_native->free_device_ids,
                          GINT_TO_POINTER (device_id),
                          compare_ids);
}

struct xkb_keymap *
meta_input_native_get_keymap (MetaInputNative *input_native)
{
  return input_native->keymap;
}

ClutterStage *
meta_input_native_get_stage (MetaInputNative *input_native)
{
  return input_native->stage;
}

void
meta_input_native_release_devices (MetaInputNative *input_native)
{
  if (input_native->released)
    {
      g_warning ("meta_input_native_release_devices() shouldn't be called "
                 "multiple times without a corresponding call to "
                 "meta_input_native_reclaim_devices() first");
      return;
    }

  libinput_suspend (input_native->libinput);
  process_events (input_native);

  input_native->released = TRUE;
}

static void
meta_input_native_update_xkb_state (MetaInputNative *input_native)
{
  GSList *l;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;

  for (l = input_native->seats; l; l = l->next)
    {
      MetaSeatNative *seat_native = l->data;

      latched_mods = xkb_state_serialize_mods (seat_native->xkb,
                                               XKB_STATE_MODS_LATCHED);
      locked_mods = xkb_state_serialize_mods (seat_native->xkb,
                                              XKB_STATE_MODS_LOCKED);
      xkb_state_unref (seat_native->xkb);
      seat_native->xkb = xkb_state_new (input_native->keymap);

      xkb_state_update_mask (seat_native->xkb,
                             0, /* depressed */
                             latched_mods,
                             locked_mods,
                             0, 0, 0);

      seat_native->caps_lock_led =
        xkb_keymap_led_get_index (input_native->keymap, XKB_LED_NAME_CAPS);
      seat_native->num_lock_led =
        xkb_keymap_led_get_index (input_native->keymap, XKB_LED_NAME_NUM);
      seat_native->scroll_lock_led =
        xkb_keymap_led_get_index (input_native->keymap, XKB_LED_NAME_SCROLL);

      meta_seat_native_sync_leds (seat_native);
    }
}

void
meta_input_native_reclaim_devices (MetaInputNative *input_native)
{
  if (!input_native->released)
    {
      g_warning ("Spurious call to meta_input_native_reclaim_devices() without "
                 "previous call to meta_input_native_release_devices");
      return;
    }

  libinput_resume (input_native->libinput);
  meta_input_native_update_xkb_state (input_native);
  process_events (input_native);

  input_native->released = FALSE;
}

void
meta_input_native_set_device_callbacks (MetaInputNativeOpenDeviceCallback  open_callback,
                                        MetaInputNativeCloseDeviceCallback close_callback,
                                        gpointer                           user_data)
{
  device_open_callback = open_callback;
  device_close_callback = close_callback;
  device_callback_data = user_data;
}

void
meta_input_native_set_keyboard_map (MetaInputNative   *input_native,
                                    struct xkb_keymap *keymap)
{
  g_clear_pointer (&input_native, (GDestroyNotify) xkb_keymap_unref);

  input_native->keymap = xkb_keymap_ref (keymap);
  meta_input_native_update_xkb_state (input_native);
}

struct xkb_keymap *
meta_input_native_get_keyboard_map (MetaInputNative *input_native)
{
  return xkb_state_get_keymap (input_native->main_seat->xkb);
}

void
meta_input_native_set_keyboard_layout_index (MetaInputNative    *input_native,
                                             xkb_layout_index_t  idx)
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_state *xkb_state;

  xkb_state = input_native->main_seat->xkb;

  depressed_mods = xkb_state_serialize_mods (xkb_state,
                                             XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (xkb_state,
                                           XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (xkb_state,
                                          XKB_STATE_MODS_LOCKED);

  xkb_state_update_mask (xkb_state,
                         depressed_mods,
                         latched_mods,
                         locked_mods,
                         0, 0, idx);
}

xkb_layout_index_t
meta_input_native_get_keyboard_layout_index (MetaInputNative *input_native)
{
  struct xkb_state *xkb_state;

  xkb_state = input_native->main_seat->xkb;

  return xkb_state_serialize_layout (xkb_state, XKB_STATE_LAYOUT_LOCKED);
}

void
meta_input_native_set_keyboard_numlock (MetaInputNative *input_native,
                                        gboolean         numlock_state)
{
  GSList *l;
  xkb_mod_mask_t numlock;

  numlock = (1 << xkb_keymap_mod_get_index (input_native->keymap, "Mod2"));

  for (l = input_native->seats; l; l = l->next)
    {
      MetaSeatNative *seat_native = l->data;
      struct xkb_state *xkb_state = seat_native->xkb;
      xkb_mod_mask_t depressed_mods;
      xkb_mod_mask_t latched_mods;
      xkb_mod_mask_t locked_mods;
      xkb_mod_mask_t group_mods;

      depressed_mods = xkb_state_serialize_mods (xkb_state,
                                                 XKB_STATE_MODS_DEPRESSED);
      latched_mods = xkb_state_serialize_mods (xkb_state,
                                               XKB_STATE_MODS_LATCHED);
      locked_mods = xkb_state_serialize_mods (xkb_state,
                                              XKB_STATE_MODS_LOCKED);
      group_mods = xkb_state_serialize_layout (xkb_state,
                                               XKB_STATE_LAYOUT_EFFECTIVE);

      if (numlock_state)
        locked_mods |= numlock;
      else
        locked_mods &= ~numlock;

      xkb_state_update_mask (xkb_state,
                             depressed_mods,
                             latched_mods,
                             locked_mods,
                             0, 0,
                             group_mods);

      meta_seat_native_sync_leds (seat_native);
    }
}

void
meta_input_native_set_pointer_constrain_callback (MetaInputNative              *input_native,
                                                  MetaPointerConstrainCallback  callback,
                                                  gpointer                      user_data,
                                                  GDestroyNotify                user_data_notify)
{
  if (input_native->constrain_data_notify)
    input_native->constrain_data_notify (input_native->constrain_data);

  input_native->constrain_callback = callback;
  input_native->constrain_data = user_data;
  input_native->constrain_data_notify = user_data_notify;
}

void
meta_input_native_set_relative_motion_filter (MetaInputNative          *input_native,
                                              MetaRelativeMotionFilter  filter,
                                              gpointer                  user_data)
{
  input_native->relative_motion_filter = filter;
  input_native->relative_motion_filter_user_data = user_data;
}

void
meta_input_native_set_keyboard_repeat (MetaInputNative *input_native,
                                       gboolean         repeat,
                                       uint32_t         delay,
                                       uint32_t         interval)
{
  MetaSeatNative *seat_native;

  seat_native = input_native->main_seat;

  seat_native->repeat = repeat;
  seat_native->repeat_delay = delay;
  seat_native->repeat_interval = interval;
}

void
meta_input_native_add_filter (MetaInputNative         *input_native,
                              MetaLibinputEventFilter  func,
                              gpointer                 data,
                              GDestroyNotify           destroy_notify)
{
  MetaInputEventFilter *filter;

  filter = g_new0 (MetaInputEventFilter, 1);
  filter->func = func;
  filter->data = data;
  filter->destroy_notify = destroy_notify;

  input_native->event_filters = g_slist_append (input_native->event_filters,
                                                filter);
}

void
meta_input_native_remove_filter (MetaInputNative         *input_native,
                                 MetaLibinputEventFilter  func,
                                 gpointer                 data)
{
  MetaInputEventFilter *filter;
  GSList *tmp_list;

  g_return_if_fail (func != NULL);

  tmp_list = input_native->event_filters;

  while (tmp_list)
    {
      filter = tmp_list->data;

      if (filter->func == func && filter->data == data)
        {
          if (filter->destroy_notify)
            filter->destroy_notify (filter->data);
          g_free (filter);
          input_native->event_filters =
            g_slist_delete_link (input_native->event_filters, tmp_list);
          return;
        }

      tmp_list = tmp_list->next;
    }
}

void
meta_input_native_warp_pointer (ClutterInputDevice *pointer_device,
                                uint32_t            time_,
                                int                 x,
                                int                 y)
{
  notify_absolute_motion (pointer_device, ms2us(time_), x, y, NULL);
}

void
meta_input_native_set_seat_id (const char *seat_id)
{
  g_free (evdev_seat_id);
  evdev_seat_id = g_strdup (seat_id);
}

static void
meta_input_native_constructed (GObject *object)
{
  MetaInputNative *input_native = META_INPUT_NATIVE (object);
  MetaLibinputSource *libinput_source;
  struct udev *udev;
  struct xkb_context *xkb_context;
  struct xkb_rule_names names;

  udev = udev_new ();
  if (!udev)
    {
      g_critical ("Failed to create udev object");
      return;
    }

  input_native->libinput = libinput_udev_create_context (&libinput_interface,
                                                         input_native,
                                                         udev);
  if (!input_native->libinput)
    {
      g_critical ("Failed to create the libinput object.");
      return;
    }

  if (libinput_udev_assign_seat (input_native->libinput,
                                 evdev_seat_id ? evdev_seat_id : "seat0") == -1)
    {
      g_critical ("Failed to assign a seat to the libinput object.");
      libinput_unref (input_native->libinput);
      input_native->libinput = NULL;
      return;
    }

  udev_unref (udev);

  names.rules = "evdev";
  names.model = "pc105";
  names.layout = option_xkb_layout;
  names.variant = option_xkb_variant;
  names.options = option_xkb_options;

  xkb_context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  if (!xkb_context)
    {
      g_critical ("Failed to create XKB context");
      return;
    }

  input_native->keymap =
    xkb_keymap_new_from_names (xkb_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (xkb_context);

  input_native->main_seat = meta_seat_native_new (input_native);

  dispatch_libinput (input_native);

  libinput_source = libinput_source_new (input_native);
  input_native->libinput_source = libinput_source;
}

static void
meta_input_native_dispose (GObject *object)
{
  MetaInputNative *input_native = META_INPUT_NATIVE (object);

  if (input_native->stage_added_handler)
    {
      g_signal_handler_disconnect (input_native->stage_manager,
                                   input_native->stage_added_handler);
      input_native->stage_added_handler = 0;
    }

  if (input_native->stage_removed_handler)
    {
      g_signal_handler_disconnect (input_native->stage_manager,
                                   input_native->stage_removed_handler);
      input_native->stage_removed_handler = 0;
    }

  if (input_native->stage_manager)
    {
      g_object_unref (input_native->stage_manager);
      input_native->stage_manager = NULL;
    }

  G_OBJECT_CLASS (meta_input_native_parent_class)->dispose (object);
}

static void
meta_input_native_finalize (GObject *object)
{
  MetaInputNative *input_native = META_INPUT_NATIVE (object);

  if (input_native->seats)
    g_slist_free_full (input_native->seats, g_object_unref);
  g_clear_pointer (&input_native->devices, g_slist_free);
  g_clear_pointer (&input_native->keymap, (GDestroyNotify) &xkb_keymap_unref);
  g_clear_pointer (&input_native->libinput_source,
                   (GDestroyNotify) libinput_source_free);
  g_clear_pointer (&input_native->constrain_data,
                   (GDestroyNotify) input_native->constrain_data_notify);
  g_clear_pointer (&input_native->libinput, libinput_unref);
  g_clear_pointer (&input_native->free_device_ids, g_list_free);

  G_OBJECT_CLASS (meta_input_native_parent_class)->finalize (object);
}

static void
meta_input_native_class_init (MetaInputNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterDeviceManagerClass *manager_class =
    CLUTTER_DEVICE_MANAGER_CLASS (klass);

  object_class->constructed = meta_input_native_constructed;
  object_class->finalize = meta_input_native_finalize;
  object_class->dispose = meta_input_native_dispose;

  manager_class = CLUTTER_DEVICE_MANAGER_CLASS (klass);
  manager_class->add_device = meta_input_native_add_device;
  manager_class->remove_device = meta_input_native_remove_device;
  manager_class->get_devices = meta_input_native_get_devices;
  manager_class->get_core_device = meta_input_native_get_core_device;
  manager_class->get_device = meta_input_native_get_device;
  manager_class->create_virtual_device = meta_input_native_create_virtual_device;
  manager_class->compress_motion = meta_input_native_compress_motion;
}

static void
on_stage_added (ClutterStageManager *manager,
                ClutterStage        *stage,
                MetaInputNative     *input_native)
{
  GSList *l;

  /* NB: Currently we can only associate a single stage with all evdev
   * devices.
   *
   * We save a pointer to the stage so if we release/reclaim input
   * devices due to switching virtual terminals then we know what
   * stage to re associate the devices with.
   */
  input_native->stage = stage;

  /* Set the stage of any devices that don't already have a stage */
  for (l = input_native->seats; l; l = l->next)
    {
      MetaSeatNative *seat_native = l->data;

      meta_seat_native_set_stage (seat_native, stage);
    }

  /* We only want to do this once so we can catch the default
     stage. If the application has multiple stages then it will need
     to manage the stage of the input devices itself */
  g_signal_handler_disconnect (input_native->stage_manager,
                               input_native->stage_added_handler);
  input_native->stage_added_handler = 0;
}

static void
on_stage_removed (ClutterStageManager *manager,
                  ClutterStage        *stage,
                  MetaInputNative     *input_native)
{
  GSList *l;

  /* Remove the stage of any input devices that were pointing to this
     stage so we don't send events to invalid stages */
  for (l = input_native->seats; l; l = l->next)
    {
      MetaSeatNative *seat_native = l->data;

      meta_seat_native_set_stage (seat_native, NULL);
    }
}

static void
meta_input_native_init (MetaInputNative *input_native)
{
  input_native->stage_manager = clutter_stage_manager_get_default ();
  g_object_ref (input_native->stage_manager);

  /* evdev doesn't have any way to link an event to a particular stage
     so we'll have to leave it up to applications to set the
     corresponding stage for an input device. However to make it
     easier for applications that are only using one fullscreen stage
     (which is probably the most frequent use-case for the evdev
     backend) we'll associate any input devices that don't have a
     stage with the first stage created. */
  input_native->stage_added_handler =
    g_signal_connect (input_native->stage_manager,
                      "stage-added",
                      G_CALLBACK (on_stage_added),
                      input_native);
  input_native->stage_removed_handler =
    g_signal_connect (input_native->stage_manager,
                      "stage-removed",
                      G_CALLBACK (on_stage_removed),
                      input_native);

  input_native->device_id_next = INITIAL_DEVICE_ID;
}
