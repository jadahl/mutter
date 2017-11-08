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

#include "backends/native/meta-input-device-native.h"

#include <cairo/cairo-gobject.h>

#include "backends/native/meta-input-device-tool-native.h"
#include "backends/native/meta-input-native.h"

G_DEFINE_TYPE (MetaInputDeviceNative, meta_input_device_native,
               META_TYPE_INPUT_DEVICE)

enum
{
  PROP_0,
  PROP_DEVICE_MATRIX,
  PROP_OUTPUT_ASPECT_RATIO,
  N_PROPS
};

static GParamSpec *obj_props[N_PROPS] = { 0 };

static gboolean
meta_input_device_native_keycode_to_evdev (ClutterInputDevice *device,
                                           guint               hardware_keycode,
                                           guint              *evdev_keycode)
{
  /*
   * The hardware keycodes from the evdev backend are almost evdev
   * keycodes: we use the evdev keycode file, but xkb rules have an
   * offset by 8. See the comment in _clutter_key_event_new_from_evdev()
   */
  *evdev_keycode = hardware_keycode - 8;
  return TRUE;
}

static void
meta_input_device_native_update_from_tool (ClutterInputDevice     *device,
                                           ClutterInputDeviceTool *tool)
{
  MetaInputDeviceToolNative *tool_native;

  tool_native = META_INPUT_DEVICE_TOOL_NATIVE (tool);

  g_object_freeze_notify (G_OBJECT (device));

  _clutter_input_device_reset_axes (device);

  _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_X, 0, 0, 0);
  _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_Y, 0, 0, 0);

  if (libinput_tablet_tool_has_distance (tool_native->tool))
    _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_DISTANCE, 0, 1, 0);

  if (libinput_tablet_tool_has_pressure (tool_native->tool))
    _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_PRESSURE, 0, 1, 0);

  if (libinput_tablet_tool_has_tilt (tool_native->tool))
    {
      _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_XTILT, -90, 90, 0);
      _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_YTILT, -90, 90, 0);
    }

  if (libinput_tablet_tool_has_rotation (tool_native->tool))
    _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_ROTATION, 0, 360, 0);

  if (libinput_tablet_tool_has_slider (tool_native->tool))
    _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_SLIDER, -1, 1, 0);

  if (libinput_tablet_tool_has_wheel (tool_native->tool))
    _clutter_input_device_add_axis (device, CLUTTER_INPUT_AXIS_WHEEL, -180, 180, 0);

  g_object_thaw_notify (G_OBJECT (device));
}

static gboolean
meta_input_device_native_is_mode_switch_button (ClutterInputDevice *device,
                                                guint               group,
                                                guint               button)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  struct libinput_device *libinput_device;
  struct libinput_tablet_pad_mode_group *mode_group;

  libinput_device =
    meta_input_device_native_get_libinput_device (device_native);
  mode_group =
    libinput_device_tablet_pad_get_mode_group (libinput_device, group);

  return libinput_tablet_pad_mode_group_button_is_toggle (mode_group, button) != 0;
}

static int
meta_input_device_native_get_group_n_modes (ClutterInputDevice *device,
                                            int                 group)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  struct libinput_device *libinput_device;
  struct libinput_tablet_pad_mode_group *mode_group;

  libinput_device =
    meta_input_device_native_get_libinput_device (device_native);
  mode_group =
    libinput_device_tablet_pad_get_mode_group (libinput_device, group);

  return libinput_tablet_pad_mode_group_get_num_modes (mode_group);
}

static gboolean
meta_input_device_native_is_grouped (ClutterInputDevice *device,
                                     ClutterInputDevice *other_device)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  MetaInputDeviceNative *other_device_native =
    META_INPUT_DEVICE_NATIVE (other_device);
  struct libinput_device *libinput_device, *other_libinput_device;

  libinput_device =
    meta_input_device_native_get_libinput_device (device_native);
  other_libinput_device =
    meta_input_device_native_get_libinput_device (other_device_native);

  return (libinput_device_get_device_group (libinput_device) ==
          libinput_device_get_device_group (other_libinput_device));
}

static ClutterInputDeviceType
determine_clutter_device_type (struct libinput_device *ldev)
{
  /*
   * This setting is specific to touchpads and alike, only in these
   * devices there is this additional layer of touch event interpretation.
   */
  if (libinput_device_config_tap_get_finger_count (ldev) > 0)
    return CLUTTER_TOUCHPAD_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TABLET_TOOL))
    return CLUTTER_TABLET_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TABLET_PAD))
    return CLUTTER_PAD_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_POINTER))
    return CLUTTER_POINTER_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TOUCH))
    return CLUTTER_TOUCHSCREEN_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_KEYBOARD))
    return CLUTTER_KEYBOARD_DEVICE;
  else
    return CLUTTER_EXTENSION_DEVICE;
}

MetaInputDeviceNative *
meta_input_device_native_new (MetaSeatNative         *seat_native,
                              struct libinput_device *libinput_device)
{
  MetaInput *input = meta_seat_get_input (META_SEAT (seat_native));
  MetaInputNative *input_native = META_INPUT_NATIVE (input);
  MetaInputDeviceNative *device_native;
  ClutterInputDeviceType type;
  char *vendor, *product;
  int device_id, n_rings = 0, n_strips = 0, n_groups = 1;
  char *node_path;
  double width, height;

  type = determine_clutter_device_type (libinput_device);
  vendor = g_strdup_printf ("%.4x",
                            libinput_device_get_id_vendor (libinput_device));
  product = g_strdup_printf ("%.4x",
                             libinput_device_get_id_product (libinput_device));
  device_id = meta_input_native_acquire_device_id (input_native);
  node_path = g_strdup_printf ("/dev/input/%s",
                               libinput_device_get_sysname (libinput_device));

  if (libinput_device_has_capability (libinput_device,
                                      LIBINPUT_DEVICE_CAP_TABLET_PAD))
    {
      n_rings = libinput_device_tablet_pad_get_num_rings (libinput_device);
      n_strips = libinput_device_tablet_pad_get_num_strips (libinput_device);
      n_groups = libinput_device_tablet_pad_get_num_mode_groups (libinput_device);
    }

  device_native = g_object_new (META_TYPE_INPUT_DEVICE_NATIVE,
                                "id", device_id,
                                "name", libinput_device_get_name (libinput_device),
                                "device-manager", input,
                                "device-type", type,
                                "device-mode", CLUTTER_INPUT_MODE_SLAVE,
                                "enabled", TRUE,
                                "vendor-id", vendor,
                                "product-id", product,
                                "n-rings", n_rings,
                                "n-strips", n_strips,
                                "n-mode-groups", n_groups,
                                "device-node", node_path,
                                NULL);

  device_native->seat_native = seat_native;
  device_native->libinput_device = libinput_device;

  libinput_device_set_user_data (libinput_device, device_native);
  libinput_device_ref (libinput_device);
  g_free (vendor);
  g_free (product);

  if (libinput_device_get_size (libinput_device, &width, &height) == 0)
    device_native->device_aspect_ratio = width / height;

  return device_native;
}

MetaInputDeviceNative *
meta_input_device_native_new_virtual (MetaSeatNative         *seat_native,
                                      ClutterInputDeviceType  type,
                                      ClutterInputMode        mode)
{
  MetaInput *input = meta_seat_get_input (META_SEAT (seat_native));
  MetaInputNative *input_native = META_INPUT_NATIVE (input);
  MetaInputDeviceNative *device_native;
  const char *name;
  int device_id;

  switch (type)
    {
    case CLUTTER_KEYBOARD_DEVICE:
      name = "Virtual keyboard device for seat";
      break;
    case CLUTTER_POINTER_DEVICE:
      name = "Virtual pointer device for seat";
      break;
    default:
      name = "Virtual device for seat";
      break;
    };

  device_id = meta_input_native_acquire_device_id (input_native);
  device_native = g_object_new (META_TYPE_INPUT_DEVICE_NATIVE,
                                "id", device_id,
                                "name", name,
                                "device-manager", input_native,
                                "device-type", type,
                                "device-mode", mode,
                                "enabled", TRUE,
                                NULL);

  device_native->seat_native = seat_native;

  return device_native;
}

MetaSeatNative *
meta_input_device_native_get_seat (MetaInputDeviceNative *device_native)
{
  return device_native->seat_native;
}

static ClutterInputDeviceToolType
translate_tool_type (struct libinput_tablet_tool *libinput_tool)
{
  enum libinput_tablet_tool_type tool;

  tool = libinput_tablet_tool_get_type (libinput_tool);

  switch (tool)
    {
    case LIBINPUT_TABLET_TOOL_TYPE_PEN:
      return CLUTTER_INPUT_DEVICE_TOOL_PEN;
    case LIBINPUT_TABLET_TOOL_TYPE_ERASER:
      return CLUTTER_INPUT_DEVICE_TOOL_ERASER;
    case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:
      return CLUTTER_INPUT_DEVICE_TOOL_BRUSH;
    case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:
      return CLUTTER_INPUT_DEVICE_TOOL_PENCIL;
    case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:
      return CLUTTER_INPUT_DEVICE_TOOL_AIRBRUSH;
    case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:
      return CLUTTER_INPUT_DEVICE_TOOL_MOUSE;
    case LIBINPUT_TABLET_TOOL_TYPE_LENS:
      return CLUTTER_INPUT_DEVICE_TOOL_LENS;
    default:
      return CLUTTER_INPUT_DEVICE_TOOL_NONE;
    }
}

void
meta_input_device_native_update_last_tool (MetaInputDeviceNative       *device_native,
                                           struct libinput_tablet_tool *libinput_tool)
{
  ClutterInputDevice *input_device = CLUTTER_INPUT_DEVICE (device_native);
  ClutterInputDeviceTool *tool = NULL;
  ClutterInputDeviceToolType tool_type;
  uint64_t tool_serial;

  if (libinput_tool)
    {
      tool_serial = libinput_tablet_tool_get_serial (libinput_tool);
      tool_type = translate_tool_type (libinput_tool);
      tool = clutter_input_device_lookup_tool (input_device,
                                               tool_serial, tool_type);

      if (!tool)
        {
          MetaInputDeviceToolNative *tool_native;

          tool_native = meta_input_device_tool_native_new (libinput_tool,
                                                           tool_serial,
                                                           tool_type);
          tool = CLUTTER_INPUT_DEVICE_TOOL (tool_native);
          clutter_input_device_add_tool (input_device, tool);
        }
    }

  if (device_native->last_tool != tool)
    {
      device_native->last_tool = tool;
      g_signal_emit_by_name (clutter_device_manager_get_default (),
                             "tool-changed", input_device, tool);
    }
}

void
meta_input_device_native_update_leds (MetaInputDeviceNative *device_native,
                                      enum libinput_led      leds)
{
  if (!device_native->libinput_device)
    return;

  libinput_device_led_update (device_native->libinput_device, leds);
}

struct libinput_device *
meta_input_device_native_get_libinput_device (MetaInputDeviceNative *device_native)
{
  return device_native->libinput_device;
}

void
meta_input_device_native_translate_coordinates (MetaInputDeviceNative *device_native,
                                                ClutterStage          *stage,
                                                float                 *x,
                                                float                 *y)
{
  double min_x = 0, min_y = 0, max_x = 1, max_y = 1;
  double stage_width, stage_height;
  double x_d, y_d;

  stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
  stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));
  x_d = *x / stage_width;
  y_d = *y / stage_height;

  /* Apply aspect ratio */
  if (device_native->output_ratio > 0 &&
      device_native->device_aspect_ratio > 0)
    {
      double ratio = (device_native->device_aspect_ratio /
                      device_native->output_ratio);

      if (ratio > 1)
        x_d *= ratio;
      else if (ratio < 1)
        y_d *= 1 / ratio;
    }

  cairo_matrix_transform_point (&device_native->device_matrix, &min_x, &min_y);
  cairo_matrix_transform_point (&device_native->device_matrix, &max_x, &max_y);
  cairo_matrix_transform_point (&device_native->device_matrix, &x_d, &y_d);

  *x = CLAMP (x_d, MIN (min_x, max_x), MAX (min_x, max_x)) * stage_width;
  *y = CLAMP (y_d, MIN (min_y, max_y), MAX (min_y, max_y)) * stage_height;
}

static void
meta_input_device_native_finalize (GObject *object)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (object);
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (object);
  MetaInputNative *input_native = META_INPUT_NATIVE (device->device_manager);

  g_clear_pointer (&device_native->libinput_device,
                   (GDestroyNotify) libinput_device_unref);

  meta_input_native_release_device_id (input_native, device);

  G_OBJECT_CLASS (meta_input_device_native_parent_class)->finalize (object);
}

static void
meta_input_device_native_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_DEVICE_MATRIX:
      {
        const cairo_matrix_t *matrix = g_value_get_boxed (value);
        cairo_matrix_init_identity (&device_native->device_matrix);
        cairo_matrix_multiply (&device_native->device_matrix,
                               &device_native->device_matrix, matrix);
        break;
      }
    case PROP_OUTPUT_ASPECT_RATIO:
      device_native->output_ratio = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_input_device_native_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_DEVICE_MATRIX:
      g_value_set_boxed (value, &device_native->device_matrix);
      break;
    case PROP_OUTPUT_ASPECT_RATIO:
      g_value_set_double (value, device_native->output_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_input_device_native_init (MetaInputDeviceNative *device_native)
{
  cairo_matrix_init_identity (&device_native->device_matrix);
  device_native->device_aspect_ratio = 0;
  device_native->output_ratio = 0;
}

static void
meta_input_device_native_class_init (MetaInputDeviceNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterInputDeviceClass *device_class = CLUTTER_INPUT_DEVICE_CLASS (klass);

  object_class->finalize = meta_input_device_native_finalize;
  object_class->set_property = meta_input_device_native_set_property;
  object_class->get_property = meta_input_device_native_get_property;

  device_class->keycode_to_evdev = meta_input_device_native_keycode_to_evdev;
  device_class->update_from_tool = meta_input_device_native_update_from_tool;
  device_class->is_mode_switch_button = meta_input_device_native_is_mode_switch_button;
  device_class->get_group_n_modes = meta_input_device_native_get_group_n_modes;
  device_class->is_grouped = meta_input_device_native_is_grouped;

  obj_props[PROP_DEVICE_MATRIX] =
    g_param_spec_boxed ("device-matrix",
                        "Device input matrix",
                        "Device input matrix",
                        CAIRO_GOBJECT_TYPE_MATRIX,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);
  obj_props[PROP_OUTPUT_ASPECT_RATIO] =
    g_param_spec_double ("output-aspect-ratio",
                         "Output aspect ratio",
                         "Output aspect ratio",
                         0, G_MAXDOUBLE, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
