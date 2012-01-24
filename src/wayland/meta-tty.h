/*
 * Copyright (C) 2012 Intel Corporation
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

#ifndef META_TTY_H
#define META_TTY_H

#include "meta-wayland-private.h"

G_BEGIN_DECLS

struct tty *
meta_tty_create (MetaWaylandCompositor *compositor, int tty_nr);

void
meta_tty_destroy(struct tty *tty);

void
meta_tty_enter_vt (struct tty *tty);

void
meta_tty_leave_vt (struct tty *tty);

G_END_DECLS

#endif /* META_TTY_H */
