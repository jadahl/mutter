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

#ifndef META_INPUT_DEVICE_TOOL_NATIVE_H
#define META_INPUT_DEVICE_TOOL_NATIVE_H

#include <libinput.h>

#include "clutter/clutter.h"

struct _MetaInputDeviceToolNative
{
  ClutterInputDeviceTool parent;

  struct libinput_tablet_tool *tool;
  GHashTable *button_map;
  double pressure_curve[4];
};

#define META_TYPE_INPUT_DEVICE_TOOL_NATIVE (meta_input_device_tool_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaInputDeviceToolNative, meta_input_device_tool_native,
                      META, INPUT_DEVICE_TOOL_NATIVE,
                      ClutterInputDeviceTool)

MetaInputDeviceToolNative * meta_input_device_tool_native_new (struct libinput_tablet_tool *tool,
                                                               uint64_t                     serial,
                                                               ClutterInputDeviceToolType   type);

double meta_input_device_tool_native_translate_pressure (MetaInputDeviceToolNative *tool_native,
                                                         double                     pressure);

unsigned int meta_input_device_tool_native_get_button_code (MetaInputDeviceToolNative *tool_native,
                                                            unsigned int               button);

void meta_input_device_tool_native_set_button_code (MetaInputDeviceToolNative *tool_native,
                                                    unsigned int               button,
                                                    unsigned int               evcode);

void meta_input_device_tool_native_set_pressure_curve (MetaInputDeviceToolNative *tool_native,
                                                       double                     curve[4]);

#endif /* META_INPUT_DEVICE_TOOL_NATIVE_H */
