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

#include "backends/meta-input-device.h"
#include "backends/meta-seat.h"
#include "clutter/clutter.h"

enum
{
  PROP_0,

  PROP_SEAT,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _MetaInputDevicePrivate
{
  MetaSeat *seat;
} MetaInputDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaInputDevice, meta_input_device, CLUTTER_TYPE_INPUT_DEVICE)

MetaSeat *
meta_input_device_get_seat (MetaInputDevice *input_device)
{
  MetaInputDevicePrivate *priv =
    meta_input_device_get_instance_private (input_device);

  return priv->seat;
}

static void
meta_input_device_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  MetaInputDevice *input_device = META_INPUT_DEVICE (object);
  MetaInputDevicePrivate *priv =
    meta_input_device_get_instance_private (input_device);

  switch (prop_id)
    {
    case PROP_SEAT:
      g_value_set_object (value, priv->seat);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_input_device_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaInputDevice *input_device = META_INPUT_DEVICE (object);
  MetaInputDevicePrivate *priv =
    meta_input_device_get_instance_private (input_device);

  switch (prop_id)
    {
    case PROP_SEAT:
      priv->seat = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_input_device_init (MetaInputDevice *input_device)
{
}

static void
meta_input_device_class_init (MetaInputDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_input_device_get_property;
  object_class->set_property = meta_input_device_set_property;

  obj_props[PROP_SEAT] =
    g_param_spec_object ("seat",
                         "seat",
                         "MetaSeat",
                         META_TYPE_SEAT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
