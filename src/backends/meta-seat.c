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

#include "config.h"

#include "backends/meta-seat.h"

enum
{
  PROP_0,

  PROP_INPUT,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _MetaSeatPrivate
{
  MetaInput *input;
} MetaSeatPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaSeat, meta_seat, G_TYPE_OBJECT)

uint32_t
meta_seat_get_sequence_slot (const ClutterEventSequence *sequence)
{
  /* TODO: This should be implemented by the backend */

  if (!sequence)
    return -1;

  return GPOINTER_TO_INT (sequence) - 1;
}

MetaInput *
meta_seat_get_input (MetaSeat *seat)
{
  MetaSeatPrivate *priv = meta_seat_get_instance_private (seat);

  return priv->input;
}

static void
meta_seat_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  MetaSeat *seat = META_SEAT (object);
  MetaSeatPrivate *priv = meta_seat_get_instance_private (seat);

  switch (prop_id)
    {
    case PROP_INPUT:
      g_value_set_object (value, priv->input);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_seat_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  MetaSeat *seat = META_SEAT (object);
  MetaSeatPrivate *priv = meta_seat_get_instance_private (seat);

  switch (prop_id)
    {
    case PROP_INPUT:
      priv->input = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_seat_init (MetaSeat *seat)
{
}

static void
meta_seat_class_init (MetaSeatClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_seat_get_property;
  object_class->set_property = meta_seat_set_property;

  obj_props[PROP_INPUT] =
    g_param_spec_object ("input",
                         "input",
                         "MetaInput",
                         META_TYPE_INPUT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
