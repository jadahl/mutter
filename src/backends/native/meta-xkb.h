/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2010  Intel Corporation.
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
 * Authors:
 *  Damien Lespiau <damien.lespiau@intel.com>
 */

#ifndef META_XKB_H
#define META_XKB_H

#include <xkbcommon/xkbcommon.h>

#include "clutter/clutter.h"

ClutterEvent * meta_xkb_create_key_event_from_evdev (ClutterInputDevice *device,
                                                     ClutterInputDevice *core_device,
                                                     ClutterStage       *stage,
                                                     struct xkb_state   *xkb_state,
                                                     uint32_t            button_state,
                                                     uint32_t            time_ms,
                                                     xkb_keycode_t       key,
                                                     uint32_t            state);

void meta_xkb_set_event_state (ClutterEvent     *event,
                               struct xkb_state *state,
                               uint32_t          button_state);

#endif /* META_XKB_H */
