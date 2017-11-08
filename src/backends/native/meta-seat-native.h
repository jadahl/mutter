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

#ifndef META_SEAT_NATIVE_H
#define META_SEAT_NATIVE_H

#include <glib.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>

#include "backends/native/meta-input-native.h"
#include "backends/meta-seat.h"
#include "clutter/clutter.h"

typedef struct _MetaTouchState
{
  uint32_t id;
  ClutterPoint coords;
} MetaTouchState;

struct _MetaSeatNative
{
  MetaSeat parent;

  struct libinput_seat *libinput_seat;

  GSList *devices;

  ClutterInputDevice *core_pointer;
  ClutterInputDevice *core_keyboard;

  GHashTable *touches;

  struct xkb_state *xkb;
  xkb_led_index_t caps_lock_led;
  xkb_led_index_t num_lock_led;
  xkb_led_index_t scroll_lock_led;
  uint32_t button_state;
  int button_count[KEY_CNT];

  /* keyboard repeat */
  gboolean repeat;
  uint32_t repeat_delay;
  uint32_t repeat_interval;
  uint32_t repeat_key;
  uint32_t repeat_count;
  uint32_t repeat_timer;
  ClutterInputDevice *repeat_device;

  float pointer_x;
  float pointer_y;

  /* Emulation of discrete scroll events out of smooth ones */
  float accum_scroll_dx;
  float accum_scroll_dy;
};

#define META_TYPE_SEAT_NATIVE (meta_seat_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaSeatNative, meta_seat_native,
                      META, SEAT_NATIVE, MetaSeat)

void meta_seat_native_set_libinput_seat (MetaSeatNative       *seat_native,
                                         struct libinput_seat *libinput_seat);

void meta_seat_native_sync_leds (MetaSeatNative *seat_native);

MetaTouchState * meta_seat_native_add_touch (MetaSeatNative *seat_native,
                                             uint32_t        id);

void meta_seat_native_remove_touch (MetaSeatNative *seat_native,
                                    uint32_t        id);

MetaTouchState * meta_seat_native_get_touch (MetaSeatNative *seat_native,
                                             uint32_t        id);

MetaSeatNative * meta_seat_native_new (MetaInputNative *input_native);

void meta_seat_native_clear_repeat_timer (MetaSeatNative *seat_native);

void meta_seat_native_notify_key (MetaSeatNative     *seat_native,
                                  ClutterInputDevice *device,
                                  uint64_t            time_us,
                                  uint32_t            key,
                                  uint32_t            state,
                                  gboolean            update_keys);

void meta_seat_native_notify_relative_motion (MetaSeatNative     *seat_native,
                                              ClutterInputDevice *input_device,
                                              uint64_t            time_us,
                                              float               dx,
                                              float               dy,
                                              float               dx_unaccel,
                                              float               dy_unaccel);

void meta_seat_native_notify_absolute_motion (MetaSeatNative     *seat_native,
                                              ClutterInputDevice *input_device,
                                              uint64_t            time_us,
                                              float               x,
                                              float               y,
                                              double             *axes);

void meta_seat_native_notify_button (MetaSeatNative     *seat_native,
                                     ClutterInputDevice *input_device,
                                     uint64_t            time_us,
                                     uint32_t            button,
                                     uint32_t            state);

void meta_seat_native_notify_scroll_continuous (MetaSeatNative           *seat_native,
                                                ClutterInputDevice       *input_device,
                                                uint64_t                  time_us,
                                                double                    dx,
                                                double                    dy,
                                                ClutterScrollSource       scroll_source,
                                                ClutterScrollFinishFlags  finish_flags);

void meta_seat_native_notify_discrete_scroll (MetaSeatNative      *seat_native,
                                              ClutterInputDevice  *input_device,
                                              uint64_t             time_us,
                                              double               discrete_dx,
                                              double               discrete_dy,
                                              ClutterScrollSource  scroll_source);

void meta_seat_native_set_stage (MetaSeatNative *seat_native,
                                 ClutterStage   *stage);

#endif /* META_SEAT_NATIVE_H */
