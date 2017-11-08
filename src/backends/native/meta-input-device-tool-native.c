/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright © 2009, 2010, 2011  Intel Corp.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/native/meta-input-device-tool-native.h"

G_DEFINE_TYPE (MetaInputDeviceToolNative, meta_input_device_tool_native,
               CLUTTER_TYPE_INPUT_DEVICE_TOOL)

MetaInputDeviceToolNative *
meta_input_device_tool_native_new (struct libinput_tablet_tool *tool,
                                   uint64_t                     serial,
                                   ClutterInputDeviceToolType   type)
{
  MetaInputDeviceToolNative *tool_native;

  tool_native = g_object_new (META_TYPE_INPUT_DEVICE_TOOL_NATIVE,
                              "type", type,
                              "serial", serial,
                              "id", libinput_tablet_tool_get_tool_id (tool),
                              NULL);

  tool_native->tool = libinput_tablet_tool_ref (tool);

  return tool_native;
}

void
meta_input_device_tool_native_set_pressure_curve (MetaInputDeviceToolNative *tool_native,
                                                  double                     curve[4])
{
  g_return_if_fail (curve[0] >= 0 && curve[0] <= 1 &&
                    curve[1] >= 0 && curve[1] <= 1 &&
                    curve[2] >= 0 && curve[2] <= 1 &&
                    curve[3] >= 0 && curve[3] <= 1);

  tool_native->pressure_curve[0] = curve[0];
  tool_native->pressure_curve[1] = curve[1];
  tool_native->pressure_curve[2] = curve[2];
  tool_native->pressure_curve[3] = curve[3];
}

void
meta_input_device_tool_native_set_button_code (MetaInputDeviceToolNative *tool_native,
                                               unsigned int               button,
                                               unsigned int               evcode)
{
  if (evcode == 0)
    {
      g_hash_table_remove (tool_native->button_map, GUINT_TO_POINTER (button));
    }
  else
    {
      g_hash_table_insert (tool_native->button_map, GUINT_TO_POINTER (button),
                           GUINT_TO_POINTER (evcode));
    }
}

static double
calculate_bezier_position (double pos,
                           double x1,
                           double y1,
                           double x2,
                           double y2)
{
  double int1_y, int2_y;

  pos = CLAMP (pos, 0, 1);

  /* Intersection between 0,0 and x1,y1 */
  int1_y = pos * y1;

  /* Intersection between x2,y2 and 1,1 */
  int2_y = (pos * (1 - y2)) + y2;

  /* Find the new position in the line traced by the previous points */
  return (pos * (int2_y - int1_y)) + int1_y;
}

double
meta_input_device_tool_native_translate_pressure (MetaInputDeviceToolNative *tool_native,
                                                  double                     pressure)
{
  return calculate_bezier_position (CLAMP (pressure, 0, 1),
                                    tool_native->pressure_curve[0],
                                    tool_native->pressure_curve[1],
                                    tool_native->pressure_curve[2],
                                    tool_native->pressure_curve[3]);
}

unsigned int
meta_input_device_tool_native_get_button_code (MetaInputDeviceToolNative *tool_native,
                                               unsigned int               button)
{
  return GPOINTER_TO_UINT (g_hash_table_lookup (tool_native->button_map,
                                                GUINT_TO_POINTER (button)));
}

static void
meta_input_device_tool_native_finalize (GObject *object)
{
  MetaInputDeviceToolNative *tool_native =
    META_INPUT_DEVICE_TOOL_NATIVE (object);

  g_hash_table_unref (tool_native->button_map);
  libinput_tablet_tool_unref (tool_native->tool);

  G_OBJECT_CLASS (meta_input_device_tool_native_parent_class)->finalize (object);
}

static void
meta_input_device_tool_native_class_init (MetaInputDeviceToolNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_input_device_tool_native_finalize;
}

static void
meta_input_device_tool_native_init (MetaInputDeviceToolNative *tool_native)
{
  tool_native->button_map = g_hash_table_new (NULL, NULL);
}
