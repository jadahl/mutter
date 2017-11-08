/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat Inc.
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

#include "config.h"

#include <glib-object.h>
#include <linux/input.h>

#include "backends/native/meta-input-device-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
//#include "clutter/clutter-main-private.h"
#include "clutter/clutter-virtual-input-device.h"

enum
{
  PROP_0,

  PROP_SEAT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaVirtualInputDeviceNative
{
  ClutterVirtualInputDevice parent;

  ClutterInputDevice *device;
  MetaSeatNative *seat_native;
  int button_count[KEY_CNT];
};

G_DEFINE_TYPE (MetaVirtualInputDeviceNative,
               meta_virtual_input_device_native,
               CLUTTER_TYPE_VIRTUAL_INPUT_DEVICE)

typedef enum _EvdevButtonType
{
  EVDEV_BUTTON_TYPE_NONE,
  EVDEV_BUTTON_TYPE_KEY,
  EVDEV_BUTTON_TYPE_BUTTON,
} EvdevButtonType;

static int
update_button_count (MetaVirtualInputDeviceNative *virtual_native,
                     uint32_t                      button,
                     uint32_t                      state)
{
  if (state)
    return ++virtual_native->button_count[button];
  else
    return --virtual_native->button_count[button];
}

static EvdevButtonType
get_button_type (uint16_t code)
{
  switch (code)
    {
    case BTN_TOOL_PEN:
    case BTN_TOOL_RUBBER:
    case BTN_TOOL_BRUSH:
    case BTN_TOOL_PENCIL:
    case BTN_TOOL_AIRBRUSH:
    case BTN_TOOL_MOUSE:
    case BTN_TOOL_LENS:
    case BTN_TOOL_QUINTTAP:
    case BTN_TOOL_DOUBLETAP:
    case BTN_TOOL_TRIPLETAP:
    case BTN_TOOL_QUADTAP:
    case BTN_TOOL_FINGER:
    case BTN_TOUCH:
      return EVDEV_BUTTON_TYPE_NONE;
    }

  if (code >= KEY_ESC && code <= KEY_MICMUTE)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_MISC && code <= BTN_GEAR_UP)
    return EVDEV_BUTTON_TYPE_BUTTON;
  if (code >= KEY_OK && code <= KEY_LIGHTS_TOGGLE)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT)
    return EVDEV_BUTTON_TYPE_BUTTON;
  if (code >= KEY_ALS_TOGGLE && code <= KEY_KBDINPUTASSIST_CANCEL)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_TRIGGER_HAPPY && code <= BTN_TRIGGER_HAPPY40)
    return EVDEV_BUTTON_TYPE_BUTTON;
  return EVDEV_BUTTON_TYPE_NONE;
}

static void
release_pressed_buttons (ClutterVirtualInputDevice *virtual_device)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  int code;
  uint64_t time_us;

  time_us = g_get_monotonic_time ();

  for (code = 0;
       code < (int) G_N_ELEMENTS (virtual_native->button_count);
       code++)
    {
      if (virtual_native->button_count[code] == 0)
        continue;

      switch (get_button_type (code))
        {
        case EVDEV_BUTTON_TYPE_KEY:
          clutter_virtual_input_device_notify_key (virtual_device,
                                                   time_us,
                                                   code,
                                                   CLUTTER_KEY_STATE_RELEASED);
          break;
        case EVDEV_BUTTON_TYPE_BUTTON:
          clutter_virtual_input_device_notify_button (virtual_device,
                                                      time_us,
                                                      code,
                                                      CLUTTER_BUTTON_STATE_RELEASED);
          break;
        case EVDEV_BUTTON_TYPE_NONE:
          g_assert_not_reached ();
        }
    }
}

static void
meta_virtual_input_device_native_notify_relative_motion (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         double                     dx,
                                                         double                     dy)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  meta_seat_native_notify_relative_motion (virtual_native->seat_native,
                                           virtual_native->device,
                                           time_us,
                                           dx, dy,
                                           dx, dy);
}

static void
meta_virtual_input_device_native_notify_absolute_motion (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         double                     x,
                                                         double                     y)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  meta_seat_native_notify_absolute_motion (virtual_native->seat_native,
                                           virtual_native->device,
                                           time_us,
                                           x, y,
                                           NULL);
}

static void
meta_virtual_input_device_native_notify_button (ClutterVirtualInputDevice *virtual_device,
                                                uint64_t                   time_us,
                                                uint32_t                   button,
                                                ClutterButtonState         button_state)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  int button_count;

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  if (get_button_type (button) != EVDEV_BUTTON_TYPE_BUTTON)
    {
      g_warning ("Unknown/invalid virtual device button 0x%x pressed",
                 button);
      return;
    }

  button_count = update_button_count (virtual_native, button, button_state);
  if (button_count < 0 || button_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x button %s (ignoring)", button,
                 button_state == CLUTTER_BUTTON_STATE_PRESSED ? "presses" : "releases");
      update_button_count (virtual_native, button, 1 - button_state);
      return;
    }

  meta_seat_native_notify_button (virtual_native->seat_native,
                                  virtual_native->device,
                                  time_us,
                                  button,
                                  button_state);
}

static void
meta_virtual_input_device_native_notify_key (ClutterVirtualInputDevice *virtual_device,
                                             uint64_t                   time_us,
                                             uint32_t                   key,
                                             ClutterKeyState            key_state)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  int key_count;

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  if (get_button_type (key) != EVDEV_BUTTON_TYPE_KEY)
    {
      g_warning ("Unknown/invalid virtual device key 0x%x pressed\n", key);
      return;
    }

  key_count = update_button_count (virtual_native, key, key_state);
  if (key_count < 0 || key_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x key %s (ignoring)", key,
                 key_state == CLUTTER_KEY_STATE_PRESSED ? "presses" : "releases");
      update_button_count (virtual_native, key, 1 - key_state);
      return;
    }

  meta_seat_native_notify_key (virtual_native->seat_native,
                               virtual_native->device,
                               time_us,
                               key,
                               key_state,
                               TRUE);
}

static gboolean
pick_keycode_for_keysym_in_current_group (ClutterVirtualInputDevice *virtual_device,
                                          xkb_keysym_t               keysym,
                                          xkb_keycode_t             *keycode_out,
                                          xkb_level_index_t         *level_out)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  ClutterDeviceManager *manager;
  struct xkb_keymap *xkb_keymap;
  struct xkb_state  *state;
  xkb_layout_index_t layout;
  xkb_keycode_t keycode;
  xkb_keycode_t min_keycode, max_keycode;

  manager = clutter_virtual_input_device_get_manager (virtual_device);
  xkb_keymap = meta_input_native_get_keymap (META_INPUT_NATIVE (manager));
  state = virtual_native->seat_native->xkb;

  layout = xkb_state_serialize_layout (state, XKB_STATE_LAYOUT_EFFECTIVE);
  min_keycode = xkb_keymap_min_keycode (xkb_keymap);
  max_keycode = xkb_keymap_max_keycode (xkb_keymap);
  for (keycode = min_keycode; keycode < max_keycode; keycode++)
    {
      xkb_level_index_t level, n_levels;

      n_levels = xkb_keymap_num_levels_for_key (xkb_keymap, keycode, layout);
      for (level = 0; level < n_levels; level++)
        {
          const xkb_keysym_t *syms;
          int n_syms, sym;

          n_syms = xkb_keymap_key_get_syms_by_level (xkb_keymap, keycode,
                                                     layout, level, &syms);
          for (sym = 0; sym < n_syms; sym++)
            {
              if (syms[sym] == keysym)
                {
                  *keycode_out = keycode;
                  if (level_out)
                    *level_out = level;
                  return TRUE;
                }
            }
        }
    }

  return FALSE;
}

static void
apply_level_modifiers (ClutterVirtualInputDevice *virtual_device,
                       uint64_t                   time_us,
                       uint32_t                   level,
                       uint32_t                   key_state)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  xkb_keysym_t keysym;
  xkb_keycode_t keycode;
  unsigned int evcode;

  if (level == 0)
    return;

  if (level == 1)
    {
      keysym = XKB_KEY_Shift_L;
    }
  else if (level == 2)
    {
      keysym = XKB_KEY_ISO_Level3_Shift;
    }
  else
    {
      g_warning ("Unhandled level: %d\n", level);
      return;
    }

  if (!pick_keycode_for_keysym_in_current_group (virtual_device, keysym,
                                                 &keycode, NULL))
    return;

  clutter_input_device_keycode_to_evdev (virtual_native->device,
                                         keycode, &evcode);
  meta_seat_native_notify_key (virtual_native->seat_native,
                               virtual_native->device,
                               time_us,
                               evcode,
                               key_state,
                               TRUE);
}

static void
meta_virtual_input_device_native_notify_keyval (ClutterVirtualInputDevice *virtual_device,
                                                uint64_t                   time_us,
                                                xkb_keysym_t               keysym,
                                                ClutterKeyState            key_state)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  int key_count;
  xkb_keycode_t keycode = 0;
  xkb_level_index_t level = 0;
  unsigned int evcode = 0;

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  if (!pick_keycode_for_keysym_in_current_group (virtual_device,
                                                 keysym, &keycode, &level))
    {
      g_warning ("No keycode found for keysym %x in current group", keysym);
      return;
    }

  clutter_input_device_keycode_to_evdev (virtual_native->device,
                                         keycode, &evcode);

  if (get_button_type (evcode) != EVDEV_BUTTON_TYPE_KEY)
    {
      g_warning ("Unknown/invalid virtual device key 0x%x pressed\n", evcode);
      return;
    }

  key_count = update_button_count (virtual_native, evcode, key_state);
  if (key_count < 0 || key_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x key %s (ignoring)", keycode,
                 key_state == CLUTTER_KEY_STATE_PRESSED ? "presses" : "releases");
      update_button_count (virtual_native, evcode, 1 - key_state);
      return;
    }

  if (key_state)
    apply_level_modifiers (virtual_device, time_us, level, key_state);

  meta_seat_native_notify_key (virtual_native->seat_native,
                               virtual_native->device,
                               time_us,
                               evcode,
                               key_state,
                               TRUE);

  if (!key_state)
    apply_level_modifiers (virtual_device, time_us, level, key_state);
}

static void
direction_to_discrete (ClutterScrollDirection  direction,
                       double                 *discrete_dx,
                       double                 *discrete_dy)
{
  switch (direction)
    {
    case CLUTTER_SCROLL_UP:
      *discrete_dx = 0.0;
      *discrete_dy = -1.0;
      break;
    case CLUTTER_SCROLL_DOWN:
      *discrete_dx = 0.0;
      *discrete_dy = 1.0;
      break;
    case CLUTTER_SCROLL_LEFT:
      *discrete_dx = -1.0;
      *discrete_dy = 0.0;
      break;
    case CLUTTER_SCROLL_RIGHT:
      *discrete_dx = 1.0;
      *discrete_dy = 0.0;
      break;
    case CLUTTER_SCROLL_SMOOTH:
      g_assert_not_reached ();
      break;
    }
}

static void
meta_virtual_input_device_native_notify_discrete_scroll (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         ClutterScrollDirection     direction,
                                                         ClutterScrollSource        scroll_source)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  double discrete_dx = 0.0, discrete_dy = 0.0;

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  direction_to_discrete (direction, &discrete_dx, &discrete_dy);

  meta_seat_native_notify_discrete_scroll (virtual_native->seat_native,
                                           virtual_native->device,
                                           time_us,
                                           discrete_dx, discrete_dy,
                                           scroll_source);
}

static void
meta_virtual_input_device_native_get_property (GObject    *object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      g_value_set_pointer (value, virtual_native->seat_native);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_virtual_input_device_native_set_property (GObject      *object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      virtual_native->seat_native = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_virtual_input_device_native_constructed (GObject *object)
{
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (object);
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);
  ClutterDeviceManager *manager;
  ClutterInputDeviceType device_type;
  MetaInputDeviceNative *device_native;
  ClutterStage *stage;

  manager = clutter_virtual_input_device_get_manager (virtual_device);
  device_type = clutter_virtual_input_device_get_device_type (virtual_device);

  device_native =
    meta_input_device_native_new_virtual (virtual_native->seat_native,
                                          device_type,
                                          CLUTTER_INPUT_MODE_SLAVE);
  virtual_native->device = CLUTTER_INPUT_DEVICE (device_native);

  stage = meta_input_native_get_stage (META_INPUT_NATIVE (manager));
  _clutter_input_device_set_stage (virtual_native->device, stage);
}

static void
meta_virtual_input_device_native_finalize (GObject *object)
{
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (object);
  MetaVirtualInputDeviceNative *virtual_native =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);
  GObjectClass *object_class;

  release_pressed_buttons (virtual_device);
  g_clear_object (&virtual_native->device);

  object_class =
    G_OBJECT_CLASS (meta_virtual_input_device_native_parent_class);
  object_class->finalize (object);
}

static void
meta_virtual_input_device_native_init (MetaVirtualInputDeviceNative *virtual_device_native)
{
}

static void
meta_virtual_input_device_native_class_init (MetaVirtualInputDeviceNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterVirtualInputDeviceClass *virtual_input_device_class =
    CLUTTER_VIRTUAL_INPUT_DEVICE_CLASS (klass);

  object_class->get_property = meta_virtual_input_device_native_get_property;
  object_class->set_property = meta_virtual_input_device_native_set_property;
  object_class->constructed = meta_virtual_input_device_native_constructed;
  object_class->finalize = meta_virtual_input_device_native_finalize;

  virtual_input_device_class->notify_relative_motion =
    meta_virtual_input_device_native_notify_relative_motion;
  virtual_input_device_class->notify_absolute_motion =
    meta_virtual_input_device_native_notify_absolute_motion;
  virtual_input_device_class->notify_button =
    meta_virtual_input_device_native_notify_button;
  virtual_input_device_class->notify_key =
    meta_virtual_input_device_native_notify_key;
  virtual_input_device_class->notify_keyval =
    meta_virtual_input_device_native_notify_keyval;
  virtual_input_device_class->notify_discrete_scroll =
    meta_virtual_input_device_native_notify_discrete_scroll;

  obj_props[PROP_SEAT] = g_param_spec_object ("seat",
                                              "MetaSeatNative",
                                              "MetaSeatNative",
                                              META_TYPE_SEAT_NATIVE,
                                              (G_PARAM_READABLE |
                                               G_PARAM_WRITABLE |
                                               G_PARAM_STATIC_STRINGS |
                                               G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

