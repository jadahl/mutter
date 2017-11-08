/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2010 Intel Corp.
 * Copyright (C) 2014 Jonas Ådahl
 * Copyright (C) 2016 Red Hat Inc.
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

#include "backends/native/meta-seat-native.h"

#include "backends/native/meta-input-device-native.h"
#include "backends/native/meta-input-device-tool-native.h"
#include "backends/native/meta-input-event-native.h"
#include "backends/native/meta-input-native.h"
#include "backends/native/meta-xkb.h"
#include "clutter/clutter-event-private.h"

/*
 * Try to keep the pointer inside the stage. Hopefully no one is using
 * this backend with stages smaller than this.
 */
#define INITIAL_POINTER_X 16
#define INITIAL_POINTER_Y 16

#define AUTOREPEAT_VALUE 2

#define DISCRETE_SCROLL_STEP 10.0

G_DEFINE_TYPE (MetaSeatNative, meta_seat_native, META_TYPE_SEAT)

void
meta_seat_native_set_libinput_seat (MetaSeatNative       *seat_native,
                                    struct libinput_seat *libinput_seat)
{
  g_assert (seat_native->libinput_seat == NULL);

  libinput_seat_ref (libinput_seat);
  libinput_seat_set_user_data (libinput_seat, seat_native);
  seat_native->libinput_seat = libinput_seat;
}

void
meta_seat_native_sync_leds (MetaSeatNative *seat_native)
{
  GSList *l;
  int caps_lock, num_lock, scroll_lock;
  enum libinput_led leds = 0;

  caps_lock = xkb_state_led_index_is_active (seat_native->xkb,
                                             seat_native->caps_lock_led);
  num_lock = xkb_state_led_index_is_active (seat_native->xkb,
                                            seat_native->num_lock_led);
  scroll_lock = xkb_state_led_index_is_active (seat_native->xkb,
                                               seat_native->scroll_lock_led);

  if (caps_lock)
    leds |= LIBINPUT_LED_CAPS_LOCK;
  if (num_lock)
    leds |= LIBINPUT_LED_NUM_LOCK;
  if (scroll_lock)
    leds |= LIBINPUT_LED_SCROLL_LOCK;

  for (l = seat_native->devices; l; l = l->next)
    {
      MetaInputDeviceNative *device_native = l->data;

      meta_input_device_native_update_leds (device_native, leds);
    }
}

static void
clutter_touch_state_free (MetaTouchState *touch_state)
{
  g_slice_free (MetaTouchState, touch_state);
}

MetaTouchState *
meta_seat_native_add_touch (MetaSeatNative *seat_native,
                            uint32_t        id)
{
  MetaTouchState *touch;

  touch = g_slice_new0 (MetaTouchState);
  touch->id = id;

  g_hash_table_insert (seat_native->touches, GUINT_TO_POINTER (id), touch);

  return touch;
}

void
meta_seat_native_remove_touch (MetaSeatNative *seat_native,
                               uint32_t        id)
{
  g_hash_table_remove (seat_native->touches, GUINT_TO_POINTER (id));
}

MetaTouchState *
meta_seat_native_get_touch (MetaSeatNative *seat_native,
                            uint32_t        id)
{
  return g_hash_table_lookup (seat_native->touches, GUINT_TO_POINTER (id));
}

MetaSeatNative *
meta_seat_native_new (MetaInputNative *input_native)
{
  ClutterDeviceManager *manager = CLUTTER_DEVICE_MANAGER (input_native);
  MetaSeatNative *seat_native;
  MetaInputDeviceNative *device_native;
  ClutterInputDevice *device;
  ClutterStage *stage;
  struct xkb_keymap *keymap;

  seat_native = g_object_new (META_TYPE_SEAT_NATIVE,
                              "input", input_native,
                              NULL);
  if (!seat_native)
    return NULL;

  device_native =
    meta_input_device_native_new_virtual (seat_native,
                                          CLUTTER_POINTER_DEVICE,
                                          CLUTTER_INPUT_MODE_MASTER);
  device = CLUTTER_INPUT_DEVICE (device_native);
  stage = meta_input_native_get_stage (input_native);
  _clutter_input_device_set_stage (device, stage);
  seat_native->pointer_x = INITIAL_POINTER_X;
  seat_native->pointer_y = INITIAL_POINTER_Y;
  _clutter_input_device_set_coords (device, NULL,
                                    seat_native->pointer_x,
                                    seat_native->pointer_y,
                                    NULL);
  _clutter_device_manager_add_device (manager, device);
  seat_native->core_pointer = device;

  device_native =
    meta_input_device_native_new_virtual (seat_native,
                                          CLUTTER_KEYBOARD_DEVICE,
                                          CLUTTER_INPUT_MODE_MASTER);
  device = CLUTTER_INPUT_DEVICE (device_native);
  _clutter_input_device_set_stage (device, stage);
  _clutter_device_manager_add_device (manager, device);
  seat_native->core_keyboard = device;

  seat_native->touches =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) clutter_touch_state_free);

  seat_native->repeat = TRUE;
  seat_native->repeat_delay = 250;     /* ms */
  seat_native->repeat_interval = 33;   /* ms */

  keymap = meta_input_native_get_keymap (input_native);
  if (keymap)
    {
      seat_native->xkb = xkb_state_new (keymap);

      seat_native->caps_lock_led =
        xkb_keymap_led_get_index (keymap, XKB_LED_NAME_CAPS);
      seat_native->num_lock_led =
        xkb_keymap_led_get_index (keymap, XKB_LED_NAME_NUM);
      seat_native->scroll_lock_led =
        xkb_keymap_led_get_index (keymap, XKB_LED_NAME_SCROLL);
    }

  return seat_native;
}

void
meta_seat_native_clear_repeat_timer (MetaSeatNative *seat_native)
{
  if (seat_native->repeat_timer)
    {
      g_source_remove (seat_native->repeat_timer);
      seat_native->repeat_timer = 0;
      g_clear_object (&seat_native->repeat_device);
    }
}

static gboolean
keyboard_repeat (gpointer data)
{
  MetaSeatNative *seat_native = data;
  MetaInput *input = meta_seat_get_input (META_SEAT (seat_native));
  GSource *source;

  /* There might be events queued in libinput that could cancel the
     repeat timer. */
  meta_input_native_dispatch (META_INPUT_NATIVE (input));
  if (!seat_native->repeat_timer)
    return G_SOURCE_REMOVE;

  g_return_val_if_fail (seat_native->repeat_device != NULL, G_SOURCE_REMOVE);
  source = g_main_context_find_source_by_id (NULL, seat_native->repeat_timer);

  meta_seat_native_notify_key (seat_native,
                               seat_native->repeat_device,
                               g_source_get_time (source),
                               seat_native->repeat_key,
                               AUTOREPEAT_VALUE,
                               FALSE);

  return G_SOURCE_CONTINUE;
}

static void
queue_event (ClutterEvent *event)
{
  _clutter_event_push (event, FALSE);
}

static int
update_button_count (MetaSeatNative *seat_native,
                     uint32_t        button,
                     uint32_t        state)
{
  if (state)
    {
      return ++seat_native->button_count[button];
    }
  else
    {
      /* Handle cases where we newer saw the initial pressed event. */
      if (seat_native->button_count[button] == 0)
        return 0;

      return --seat_native->button_count[button];
    }
}

void
meta_seat_native_notify_key (MetaSeatNative     *seat_native,
                             ClutterInputDevice *device,
                             uint64_t            time_us,
                             uint32_t            key,
                             uint32_t            state,
                             gboolean            update_keys)
{
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  enum xkb_state_component changed_state;

  if (state != AUTOREPEAT_VALUE)
    {
      /* Drop any repeated button press (for example from virtual devices. */
      int count = update_button_count (seat_native, key, state);
      if (state && count > 1)
        return;
      if (!state && count != 0)
        return;
    }

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (device);
  if (stage == NULL)
    {
      meta_seat_native_clear_repeat_timer (seat_native);
      return;
    }

  event = meta_xkb_create_key_event_from_evdev (device,
                                                seat_native->core_keyboard,
                                                stage,
                                                seat_native->xkb,
                                                seat_native->button_state,
                                                us2ms (time_us), key, state);
  meta_input_event_native_set_event_code (event, key);

  /* We must be careful and not pass multiple releases to xkb, otherwise it gets
     confused and locks the modifiers */
  if (state != AUTOREPEAT_VALUE)
    {
      changed_state = xkb_state_update_key (seat_native->xkb,
                                            event->key.hardware_keycode,
                                            state ? XKB_KEY_DOWN : XKB_KEY_UP);
    }
  else
    {
      changed_state = 0;
      clutter_event_set_flags (event, CLUTTER_EVENT_FLAG_SYNTHETIC);
    }

  queue_event (event);

  if (update_keys && (changed_state & XKB_STATE_LEDS))
    meta_seat_native_sync_leds (seat_native);

  if (state == 0 ||             /* key release */
      !seat_native->repeat ||
      !xkb_keymap_key_repeats (xkb_state_get_keymap (seat_native->xkb),
                               event->key.hardware_keycode))
    {
      meta_seat_native_clear_repeat_timer (seat_native);
      return;
    }

  if (state == 1)               /* key press */
    seat_native->repeat_count = 0;

  seat_native->repeat_count += 1;
  seat_native->repeat_key = key;

  switch (seat_native->repeat_count)
    {
    case 1:
    case 2:
      {
        uint32_t interval;

        meta_seat_native_clear_repeat_timer (seat_native);
        seat_native->repeat_device = g_object_ref (device);

        if (seat_native->repeat_count == 1)
          interval = seat_native->repeat_delay;
        else
          interval = seat_native->repeat_interval;

        seat_native->repeat_timer =
          clutter_threads_add_timeout_full (CLUTTER_PRIORITY_EVENTS,
                                            interval,
                                            keyboard_repeat,
                                            seat_native,
                                            NULL);
        return;
      }
    default:
      return;
    }
}

static ClutterEvent *
new_absolute_motion_event (MetaSeatNative     *seat_native,
                           ClutterInputDevice *input_device,
                           uint64_t            time_us,
                           float               x,
                           float               y,
                           double             *axes)
{
  MetaInput *input = meta_seat_get_input (META_SEAT (seat_native));
  ClutterStage *stage = _clutter_input_device_get_stage (input_device);
  ClutterEvent *event;

  event = clutter_event_new (CLUTTER_MOTION);

  if (clutter_input_device_get_device_type (input_device) != CLUTTER_TABLET_DEVICE)
    meta_input_native_constrain_pointer (META_INPUT_NATIVE (input),
                                         seat_native->core_pointer,
                                         time_us,
                                         seat_native->pointer_x,
                                         seat_native->pointer_y,
                                         &x, &y);

  meta_input_event_native_set_time_usec (event, time_us);
  event->motion.time = us2ms (time_us);
  event->motion.stage = stage;
  event->motion.device = seat_native->core_pointer;
  meta_xkb_set_event_state (event,
                            seat_native->xkb,
                            seat_native->button_state);
  event->motion.x = x;
  event->motion.y = y;
  event->motion.axes = axes;
  clutter_event_set_device (event, seat_native->core_pointer);
  clutter_event_set_source_device (event, input_device);

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      MetaInputDeviceNative *device_native =
        META_INPUT_DEVICE_NATIVE (input_device);

      clutter_event_set_device_tool (event, device_native->last_tool);
      clutter_event_set_device (event, input_device);
    }
  else
    {
      clutter_event_set_device (event, seat_native->core_pointer);
    }

  _clutter_input_device_set_stage (seat_native->core_pointer, stage);

  if (clutter_input_device_get_device_type (input_device) != CLUTTER_TABLET_DEVICE)
    {
      seat_native->pointer_x = x;
      seat_native->pointer_y = y;
    }

  return event;
}

void
meta_seat_native_notify_relative_motion (MetaSeatNative     *seat_native,
                                         ClutterInputDevice *input_device,
                                         uint64_t            time_us,
                                         float               dx,
                                         float               dy,
                                         float               dx_unaccel,
                                         float               dy_unaccel)
{
  MetaInput *input = meta_seat_get_input (META_SEAT (seat_native));
  float new_x, new_y;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  if (!_clutter_input_device_get_stage (input_device))
    return;

  meta_input_native_filter_relative_motion (META_INPUT_NATIVE (input),
                                            input_device,
                                            seat_native->pointer_x,
                                            seat_native->pointer_y,
                                            &dx,
                                            &dy);

  new_x = seat_native->pointer_x + dx;
  new_y = seat_native->pointer_y + dy;
  event = new_absolute_motion_event (seat_native, input_device,
                                     time_us, new_x, new_y, NULL);

  meta_input_event_native_set_relative_motion (event,
                                            dx, dy,
                                            dx_unaccel, dy_unaccel);

  queue_event (event);
}

void
meta_seat_native_notify_absolute_motion (MetaSeatNative     *seat_native,
                                         ClutterInputDevice *input_device,
                                         uint64_t            time_us,
                                         float               x,
                                         float               y,
                                         double             *axes)
{
  ClutterEvent *event;

  event = new_absolute_motion_event (seat_native, input_device,
                                     time_us, x, y, axes);

  queue_event (event);
}

void
meta_seat_native_notify_button (MetaSeatNative     *seat_native,
                                ClutterInputDevice *input_device,
                                uint64_t            time_us,
                                uint32_t            button,
                                uint32_t            state)
{
  MetaInputDeviceNative *device_native = (MetaInputDeviceNative *) input_device;
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  int button_nr;
  static int maskmap[8] =
    {
      CLUTTER_BUTTON1_MASK, CLUTTER_BUTTON3_MASK, CLUTTER_BUTTON2_MASK,
      CLUTTER_BUTTON4_MASK, CLUTTER_BUTTON5_MASK, 0, 0, 0
    };
  int button_count;

  /* Drop any repeated button press (for example from virtual devices. */
  button_count = update_button_count (seat_native, button, state);
  if (state && button_count > 1)
    return;
  if (!state && button_count != 0)
    return;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  /* The evdev button numbers don't map sequentially to clutter button
   * numbers (the right and middle mouse buttons are in the opposite
   * order) so we'll map them directly with a switch statement */
  switch (button)
    {
    case BTN_LEFT:
    case BTN_TOUCH:
      button_nr = CLUTTER_BUTTON_PRIMARY;
      break;

    case BTN_RIGHT:
    case BTN_STYLUS:
      button_nr = CLUTTER_BUTTON_SECONDARY;
      break;

    case BTN_MIDDLE:
    case BTN_STYLUS2:
      button_nr = CLUTTER_BUTTON_MIDDLE;
      break;

    default:
      /* For compatibility reasons, all additional buttons go after the old 4-7 scroll ones */
      if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
        button_nr = button - BTN_TOOL_PEN + 4;
      else
        button_nr = button - (BTN_LEFT - 1) + 4;
      break;
    }

  if (button_nr < 1 || button_nr > 12)
    {
      g_warning ("Unhandled button event 0x%x", button);
      return;
    }

  if (state)
    event = clutter_event_new (CLUTTER_BUTTON_PRESS);
  else
    event = clutter_event_new (CLUTTER_BUTTON_RELEASE);

  if (button_nr < (int) G_N_ELEMENTS (maskmap))
    {
      /* Update the modifiers */
      if (state)
        seat_native->button_state |= maskmap[button_nr - 1];
      else
        seat_native->button_state &= ~maskmap[button_nr - 1];
    }

  meta_input_event_native_set_time_usec (event, time_us);
  event->button.time = us2ms (time_us);
  event->button.stage = CLUTTER_STAGE (stage);
  meta_xkb_set_event_state (event,
                            seat_native->xkb,
                            seat_native->button_state);
  event->button.button = button_nr;

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      ClutterPoint point;

      clutter_input_device_get_coords (input_device, NULL, &point);
      event->button.x = point.x;
      event->button.y = point.y;
    }
  else
    {
      event->button.x = seat_native->pointer_x;
      event->button.y = seat_native->pointer_y;
    }

  clutter_event_set_device (event, seat_native->core_pointer);
  clutter_event_set_source_device (event, input_device);

  if (device_native->last_tool)
    {
      MetaInputDeviceToolNative *last_tool_native;
      unsigned int mapped_button;

      /* Apply the button event code as per the tool mapping */
      last_tool_native = META_INPUT_DEVICE_TOOL_NATIVE (device_native->last_tool);
      mapped_button =
        meta_input_device_tool_native_get_button_code (last_tool_native,
                                                       button_nr);
      if (mapped_button != 0)
        button = mapped_button;
    }

  meta_input_event_native_set_event_code (event, button);

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      clutter_event_set_device_tool (event, device_native->last_tool);
      clutter_event_set_device (event, input_device);
    }
  else
    {
      clutter_event_set_device (event, seat_native->core_pointer);
    }

  _clutter_input_device_set_stage (seat_native->core_pointer, stage);

  queue_event (event);
}

static void
notify_scroll (ClutterInputDevice       *input_device,
               uint64_t                  time_us,
               double                    dx,
               double                    dy,
               ClutterScrollSource       scroll_source,
               ClutterScrollFinishFlags  flags,
               gboolean                  emulated)
{
  MetaSeat *seat;
  MetaSeatNative *seat_native;
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  double scroll_factor;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  seat = meta_input_device_get_seat (META_INPUT_DEVICE (input_device));
  seat_native = META_SEAT_NATIVE (seat);

  event = clutter_event_new (CLUTTER_SCROLL);

  meta_input_event_native_set_time_usec (event, time_us);
  event->scroll.time = us2ms (time_us);
  event->scroll.stage = CLUTTER_STAGE (stage);
  event->scroll.device = seat_native->core_pointer;
  meta_xkb_set_event_state (event,
                            seat_native->xkb,
                            seat_native->button_state);

  /* libinput pointer axis events are in pointer motion coordinate space.
   * To convert to Xi2 discrete step coordinate space, multiply the factor
   * 1/10. */
  event->scroll.direction = CLUTTER_SCROLL_SMOOTH;
  scroll_factor = 1.0 / DISCRETE_SCROLL_STEP;
  clutter_event_set_scroll_delta (event,
                                  scroll_factor * dx,
                                  scroll_factor * dy);

  event->scroll.x = seat_native->pointer_x;
  event->scroll.y = seat_native->pointer_y;
  clutter_event_set_device (event, seat_native->core_pointer);
  clutter_event_set_source_device (event, input_device);
  event->scroll.scroll_source = scroll_source;
  event->scroll.finish_flags = flags;

  _clutter_event_set_pointer_emulated (event, emulated);

  queue_event (event);
}

static void
notify_discrete_scroll (ClutterInputDevice     *input_device,
                        uint64_t                time_us,
                        ClutterScrollDirection  direction,
                        ClutterScrollSource     scroll_source,
                        gboolean                emulated)
{
  MetaSeat *seat;
  MetaSeatNative *seat_native;
  ClutterStage *stage;
  ClutterEvent *event = NULL;

  if (direction == CLUTTER_SCROLL_SMOOTH)
    return;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  seat = meta_input_device_get_seat (META_INPUT_DEVICE (input_device));
  seat_native = META_SEAT_NATIVE (seat);

  event = clutter_event_new (CLUTTER_SCROLL);

  meta_input_event_native_set_time_usec (event, time_us);
  event->scroll.time = us2ms (time_us);
  event->scroll.stage = CLUTTER_STAGE (stage);
  event->scroll.device = seat_native->core_pointer;
  meta_xkb_set_event_state (event,
                            seat_native->xkb,
                            seat_native->button_state);

  event->scroll.direction = direction;

  event->scroll.x = seat_native->pointer_x;
  event->scroll.y = seat_native->pointer_y;
  clutter_event_set_device (event, seat_native->core_pointer);
  clutter_event_set_source_device (event, input_device);
  event->scroll.scroll_source = scroll_source;

  _clutter_event_set_pointer_emulated (event, emulated);

  queue_event (event);
}

static void
check_notify_discrete_scroll (MetaSeatNative     *seat_native,
                              ClutterInputDevice *device,
                              uint64_t            time_us,
                              ClutterScrollSource scroll_source)
{
  int i, n_xscrolls, n_yscrolls;

  n_xscrolls = floor (fabs (seat_native->accum_scroll_dx) / DISCRETE_SCROLL_STEP);
  n_yscrolls = floor (fabs (seat_native->accum_scroll_dy) / DISCRETE_SCROLL_STEP);

  for (i = 0; i < n_xscrolls; i++)
    {
      notify_discrete_scroll (device, time_us,
                              seat_native->accum_scroll_dx > 0 ?
                              CLUTTER_SCROLL_RIGHT : CLUTTER_SCROLL_LEFT,
                              scroll_source, TRUE);
    }

  for (i = 0; i < n_yscrolls; i++)
    {
      notify_discrete_scroll (device, time_us,
                              seat_native->accum_scroll_dy > 0 ?
                              CLUTTER_SCROLL_DOWN : CLUTTER_SCROLL_UP,
                              scroll_source, TRUE);
    }

  seat_native->accum_scroll_dx = fmodf (seat_native->accum_scroll_dx,
                                        DISCRETE_SCROLL_STEP);
  seat_native->accum_scroll_dy = fmodf (seat_native->accum_scroll_dy,
                                        DISCRETE_SCROLL_STEP);
}

void
meta_seat_native_notify_scroll_continuous (MetaSeatNative           *seat_native,
                                           ClutterInputDevice       *input_device,
                                           uint64_t                  time_us,
                                           double                    dx,
                                           double                    dy,
                                           ClutterScrollSource       scroll_source,
                                           ClutterScrollFinishFlags  finish_flags)
{
  if (finish_flags & CLUTTER_SCROLL_FINISHED_HORIZONTAL)
    seat_native->accum_scroll_dx = 0;
  else
    seat_native->accum_scroll_dx += dx;

  if (finish_flags & CLUTTER_SCROLL_FINISHED_VERTICAL)
    seat_native->accum_scroll_dy = 0;
  else
    seat_native->accum_scroll_dy += dy;

  notify_scroll (input_device, time_us, dx, dy, scroll_source,
                 finish_flags, FALSE);
  check_notify_discrete_scroll (seat_native, input_device,
                                time_us, scroll_source);
}

static ClutterScrollDirection
discrete_to_direction (double discrete_dx,
                       double discrete_dy)
{
  if (discrete_dx > 0)
    return CLUTTER_SCROLL_RIGHT;
  else if (discrete_dx < 0)
    return CLUTTER_SCROLL_LEFT;
  else if (discrete_dy > 0)
    return CLUTTER_SCROLL_DOWN;
  else if (discrete_dy < 0)
    return CLUTTER_SCROLL_UP;
  else
    g_assert_not_reached ();
}

void
meta_seat_native_notify_discrete_scroll (MetaSeatNative      *seat_native,
                                         ClutterInputDevice  *input_device,
                                         uint64_t             time_us,
                                         double               discrete_dx,
                                         double               discrete_dy,
                                         ClutterScrollSource  scroll_source)
{
  notify_scroll (input_device, time_us,
                 discrete_dx * DISCRETE_SCROLL_STEP,
                 discrete_dy * DISCRETE_SCROLL_STEP,
                 scroll_source, CLUTTER_SCROLL_FINISHED_NONE,
                 TRUE);
  notify_discrete_scroll (input_device, time_us,
                          discrete_to_direction (discrete_dx, discrete_dy),
                          scroll_source, FALSE);

}

void
meta_seat_native_set_stage (MetaSeatNative *seat_native,
                            ClutterStage   *stage)
{
  GSList *l;

  _clutter_input_device_set_stage (seat_native->core_pointer, stage);
  _clutter_input_device_set_stage (seat_native->core_keyboard, stage);

  for (l = seat_native->devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      _clutter_input_device_set_stage (device, stage);
    }
}

static void
meta_seat_native_finalize (GObject *object)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (object);

  if (seat_native->devices)
    g_slist_free_full (seat_native->devices, g_object_unref);
  g_hash_table_unref (seat_native->touches);

  g_clear_pointer (&seat_native->xkb, (GDestroyNotify) xkb_state_unref);

  meta_seat_native_clear_repeat_timer (seat_native);

  g_clear_pointer (&seat_native->libinput_seat,
                   (GDestroyNotify) libinput_seat_unref);

  G_OBJECT_CLASS (meta_seat_native_parent_class)->finalize (object);
}

static void
meta_seat_native_init (MetaSeatNative *seat_native)
{
}

static void
meta_seat_native_class_init (MetaSeatNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_seat_native_finalize;
}
