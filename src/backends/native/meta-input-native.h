/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
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

#ifndef META_INPUT_NATIVE_H
#define META_INPUT_NATIVE_H

#include <glib-object.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>

#include "backends/meta-input.h"

typedef struct _MetaInputDeviceNative MetaInputDeviceNative;

#define META_TYPE_INPUT_NATIVE (meta_input_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaInputNative, meta_input_native,
                      META, INPUT_NATIVE,
                      MetaInput)

typedef int (* MetaInputNativeOpenDeviceCallback) (const char  *path,
                                                   int          flags,
                                                   gpointer     user_data,
                                                   GError     **error);

typedef void (* MetaInputNativeCloseDeviceCallback) (int          fd,
                                                     gpointer     user_data);

typedef void (* MetaPointerConstrainCallback) (ClutterInputDevice *device,
                                               uint32_t            time,
                                               float               prev_x,
                                               float               prev_y,
                                               float              *new_x,
                                               float              *new_y,
                                               gpointer            user_data);

typedef void (* MetaRelativeMotionFilter) (ClutterInputDevice *device,
                                           float               x,
                                           float               y,
                                           float              *dx,
                                           float              *dy,
                                           gpointer            user_data);

typedef gboolean (* MetaLibinputEventFilter) (struct libinput_event *event,
                                              gpointer               data);

void meta_input_native_constrain_pointer (MetaInputNative    *input_native,
                                          ClutterInputDevice *core_pointer,
                                          uint64_t            time_us,
                                          float               x,
                                          float               y,
                                          float              *new_x,
                                          float              *new_y);

void meta_input_native_filter_relative_motion (MetaInputNative    *input_native,
                                               ClutterInputDevice *device,
                                               float               x,
                                               float               y,
                                               float              *dx,
                                               float              *dy);

int meta_input_native_acquire_device_id (MetaInputNative *input_native);

void meta_input_native_release_device_id (MetaInputNative    *input_native,
                                          ClutterInputDevice *device);

struct xkb_keymap * meta_input_native_get_keymap (MetaInputNative *input_native);

ClutterStage * meta_input_native_get_stage (MetaInputNative *input_native);

void meta_input_native_release_devices (MetaInputNative *input_native);

void meta_input_native_reclaim_devices (MetaInputNative *input_native);

void meta_input_native_set_device_callbacks (MetaInputNativeOpenDeviceCallback  open_callback,
                                             MetaInputNativeCloseDeviceCallback close_callback,
                                             gpointer                           user_data);

void meta_input_native_set_keyboard_map (MetaInputNative   *input_native,
                                         struct xkb_keymap *keymap);

struct xkb_keymap * meta_input_native_get_keyboard_map (MetaInputNative *input_native);

void meta_input_native_set_keyboard_layout_index (MetaInputNative    *input_native,
                                                  xkb_layout_index_t  idx);

xkb_layout_index_t meta_input_native_get_keyboard_layout_index (MetaInputNative *input_native);

void meta_input_native_set_keyboard_numlock (MetaInputNative *input_native,
                                             gboolean         numlock_state);

void meta_input_native_set_pointer_constrain_callback (MetaInputNative              *input_native,
                                                       MetaPointerConstrainCallback  callback,
                                                       gpointer                      user_data,
                                                       GDestroyNotify                user_data_notify);

void meta_input_native_set_relative_motion_filter (MetaInputNative          *input_native,
                                                   MetaRelativeMotionFilter  filter,
                                                   gpointer                  user_data);

void meta_input_native_set_keyboard_repeat (MetaInputNative *input_native,
                                            gboolean         repeat,
                                            uint32_t         delay,
                                            uint32_t         interval);

void meta_input_native_add_filter (MetaInputNative         *input_native,
                                   MetaLibinputEventFilter  func,
                                   gpointer                 data,
                                   GDestroyNotify           destroy_notify);

void meta_input_native_remove_filter (MetaInputNative         *input_native,
                                      MetaLibinputEventFilter  func,
                                      gpointer                 data);;
void meta_input_native_warp_pointer (ClutterInputDevice *pointer_device,
                                     uint32_t            time_,
                                     int                 x,
                                     int                 y);

void meta_input_native_set_seat_id (const char *seat_id);

void meta_input_native_dispatch (MetaInputNative *input_native);

static inline uint64_t
us (uint64_t us)
{
  return us;
}

static inline uint64_t
ms2us (uint64_t ms)
{
  return us (ms * 1000);
}

static inline uint32_t
us2ms (uint64_t us)
{
  return (uint32_t) (us / 1000);
}

#endif /* META_INPUT_NATIVE_H */
