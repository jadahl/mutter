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

#ifndef META_INPUT_EVENT_NATIVE_H
#define META_INPUT_EVENT_NATIVE_H

#include "clutter/clutter.h"

typedef struct _MetaInputEventNative MetaInputEventNative;

MetaInputEventNative * meta_input_event_native_copy (MetaInputEventNative *event_native);

void meta_input_event_native_free (MetaInputEventNative *event_native);

void meta_input_event_native_set_event_code (ClutterEvent *event,
                                             uint32_t      evcode);

void meta_input_event_native_set_time_usec (ClutterEvent *event,
                                            uint64_t      time_usec);

void meta_input_event_native_set_relative_motion (ClutterEvent *event,
                                                  double        dx,
                                                  double        dy,
                                                  double        dx_unaccel,
                                                  double        dy_unaccel);
uint32_t meta_input_event_native_get_event_code (const ClutterEvent *event);

uint64_t meta_input_event_native_get_time_usec (const ClutterEvent *event);

gboolean meta_input_event_native_get_relative_motion (const ClutterEvent *event,
                                                      double             *dx,
                                                      double             *dy,
                                                      double             *dx_unaccel,
                                                      double             *dy_unaccel);

#endif /* META_INPUT_EVENT_NATIVE_H */
