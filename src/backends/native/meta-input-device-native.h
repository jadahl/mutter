/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2010 Intel Corp.
 * Copyright (C) 2014 Jonas Ådahl
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

#ifndef META_INPUT_DEVICE_NATIVE_H
#define META_INPUT_DEVICE_NATIVE_H

#include <glib-object.h>
#include <libinput.h>

#include "backends/native/meta-input-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/meta-input-device.h"

#define META_TYPE_INPUT_DEVICE_NATIVE (meta_input_device_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaInputDeviceNative, meta_input_device_native,
                      META, INPUT_DEVICE_NATIVE,
                      MetaInputDevice)

struct _MetaInputDeviceNative
{
  MetaInputDevice parent;

  struct libinput_device *libinput_device;
  MetaSeatNative *seat_native;
  ClutterInputDeviceTool *last_tool;

  cairo_matrix_t device_matrix;
  double device_aspect_ratio; /* w:h */
  double output_ratio;        /* w:h */
};

MetaInputDeviceNative * meta_input_device_native_new (MetaSeatNative         *seat_native,
                                                      struct libinput_device *libinput_device);

MetaInputDeviceNative * meta_input_device_native_new_virtual (MetaSeatNative         *seat,
                                                              ClutterInputDeviceType  type,
                                                              ClutterInputMode        mode);

MetaSeatNative * meta_input_device_native_get_seat (MetaInputDeviceNative *device_native);

struct libinput_device * meta_input_device_native_get_libinput_device (MetaInputDeviceNative *device_native);

void meta_input_device_native_update_last_tool (MetaInputDeviceNative       *device_native,
                                                struct libinput_tablet_tool *libinput_tool);

void meta_input_device_native_update_leds (MetaInputDeviceNative *device_native,
                                           enum libinput_led      leds);

void meta_input_device_native_translate_coordinates (MetaInputDeviceNative *device_native,
                                                     ClutterStage          *stage,
                                                     float                 *x,
                                                     float                 *y);

#endif /* META_INPUT_DEVICE_NATIVE_H */
