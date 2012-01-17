/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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

#include <meta/meta-plugin.h>
#include <meta/window.h>

#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x

#include <clutter/clutter.h>
#include <gmodule.h>
#include <string.h>

#define DESTROY_TIMEOUT   250
#define MINIMIZE_TIMEOUT  250
#define MAXIMIZE_TIMEOUT  250
#define MAP_TIMEOUT       250
#define SWITCH_TIMEOUT    500

#define ORIG_PARENT_KEY "MCCP-original-parent"

#define META_TYPE_DEFAULT_PLUGIN            (meta_default_plugin_get_type ())
#define META_DEFAULT_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEFAULT_PLUGIN, MetaDefaultPlugin))
#define META_DEFAULT_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginClass))
#define META_IS_DEFAULT_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_DEFAULT_PLUGIN_TYPE))
#define META_IS_DEFAULT_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEFAULT_PLUGIN))
#define META_DEFAULT_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginClass))

#define META_DEFAULT_PLUGIN_GET_PRIVATE(obj) \
(G_TYPE_INSTANCE_GET_PRIVATE ((obj), META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginPrivate))

typedef struct _MetaDefaultPlugin        MetaDefaultPlugin;
typedef struct _MetaDefaultPluginClass   MetaDefaultPluginClass;
typedef struct _MetaDefaultPluginPrivate MetaDefaultPluginPrivate;

struct _MetaDefaultPlugin
{
  MetaPlugin parent;

  MetaDefaultPluginPrivate *priv;
};

struct _MetaDefaultPluginClass
{
  MetaPluginClass parent_class;
};

static void minimize   (MetaPlugin      *plugin,
                        MetaWindowActor *actor);
static void map        (MetaPlugin      *plugin,
                        MetaWindowActor *actor);
static void destroy    (MetaPlugin      *plugin,
                        MetaWindowActor *actor);
static void maximize   (MetaPlugin      *plugin,
                        MetaWindowActor *actor,
                        gint             x,
                        gint             y,
                        gint             width,
                        gint             height);
static void unmaximize (MetaPlugin      *plugin,
                        MetaWindowActor *actor,
                        gint             x,
                        gint             y,
                        gint             width,
                        gint             height);

static void switch_workspace (MetaPlugin          *plugin,
                              gint                 from,
                              gint                 to,
                              MetaMotionDirection  direction);

static void kill_window_effects   (MetaPlugin      *plugin,
                                   MetaWindowActor *actor);
static void kill_switch_workspace (MetaPlugin      *plugin);

static const MetaPluginInfo * plugin_info (MetaPlugin *plugin);

META_PLUGIN_DECLARE(MetaDefaultPlugin, meta_default_plugin);

/*
 * Plugin private data that we store in the .plugin_private member.
 */
struct _MetaDefaultPluginPrivate
{
  /* Valid only when switch_workspace effect is in progress */
  ClutterActor          *desktop1;
  ClutterActor          *desktop2;

  MetaPluginInfo         info;
};

static void
meta_default_plugin_dispose (GObject *object)
{
  /* MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (object)->priv;
  */
  G_OBJECT_CLASS (meta_default_plugin_parent_class)->dispose (object);
}

static void
meta_default_plugin_finalize (GObject *object)
{
  G_OBJECT_CLASS (meta_default_plugin_parent_class)->finalize (object);
}

static void
meta_default_plugin_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_default_plugin_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_default_plugin_class_init (MetaDefaultPluginClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  MetaPluginClass *plugin_class  = META_PLUGIN_CLASS (klass);

  gobject_class->finalize        = meta_default_plugin_finalize;
  gobject_class->dispose         = meta_default_plugin_dispose;
  gobject_class->set_property    = meta_default_plugin_set_property;
  gobject_class->get_property    = meta_default_plugin_get_property;

  plugin_class->map              = map;
  plugin_class->minimize         = minimize;
  plugin_class->maximize         = maximize;
  plugin_class->unmaximize       = unmaximize;
  plugin_class->destroy          = destroy;
  plugin_class->switch_workspace = switch_workspace;
  plugin_class->plugin_info      = plugin_info;
  plugin_class->kill_window_effects   = kill_window_effects;
  plugin_class->kill_switch_workspace = kill_switch_workspace;

  g_type_class_add_private (gobject_class, sizeof (MetaDefaultPluginPrivate));
}

static void
meta_default_plugin_init (MetaDefaultPlugin *self)
{
  MetaDefaultPluginPrivate *priv;

  self->priv = priv = META_DEFAULT_PLUGIN_GET_PRIVATE (self);

  priv->info.name        = "Default Effects";
  priv->info.version     = "0.1";
  priv->info.author      = "Intel Corp.";
  priv->info.license     = "GPL";
  priv->info.description = "This is an example of a plugin implementation.";
}

static void
on_switch_workspace_effect_complete (ClutterActor *actor,
                                     gpointer      data)
{
  MetaPlugin *plugin = META_PLUGIN (data);
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;
  ClutterActorIter iter;
  ClutterActor *child;

  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      ClutterActor *orig_parent = g_object_get_data (G_OBJECT (actor), ORIG_PARENT_KEY);

      if (orig_parent != NULL)
        {
          clutter_actor_remove_child (actor, child);
          clutter_actor_add_child (child, orig_parent);
          g_object_set_data (G_OBJECT (actor), ORIG_PARENT_KEY, NULL);
        }
    }

  if (actor == priv->desktop1)
    priv->desktop1 = NULL;

  if (actor == priv->desktop2)
    priv->desktop2 = NULL;

  clutter_actor_destroy (actor);

  meta_plugin_switch_workspace_completed (plugin);
}

static void
switch_workspace (MetaPlugin *plugin,
                  gint from, gint to,
                  MetaMotionDirection direction)
{
  MetaScreen *screen;
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;
  GList        *l;
  ClutterActor *workspace0  = clutter_actor_new ();
  ClutterActor *workspace1  = clutter_actor_new ();
  ClutterActor *stage;
  int           screen_width, screen_height;

  screen = meta_plugin_get_screen (plugin);
  stage = meta_get_stage_for_screen (screen);

  meta_screen_get_size (screen,
                        &screen_width,
                        &screen_height);

  clutter_actor_set_anchor_point (workspace1,
                                  screen_width,
                                  screen_height);
  clutter_actor_set_position (workspace1,
                              screen_width,
                              screen_height);

  clutter_actor_set_scale (workspace1, 0.0, 0.0);

  clutter_actor_add_child (stage, workspace1);
  clutter_actor_add_child (stage, workspace0);

  if (from == to)
    {
      meta_plugin_switch_workspace_completed (plugin);
      return;
    }

  l = g_list_last (meta_get_window_actors (screen));

  while (l)
    {
      MetaWindowActor *window_actor = l->data;
      ClutterActor    *actor	    = CLUTTER_ACTOR (window_actor);
      gint             win_workspace;

      win_workspace = meta_window_actor_get_workspace (window_actor);

      if (win_workspace == to || win_workspace == from)
        {
          ClutterActor *old_parent, *new_parent;

          old_parent = clutter_actor_get_parent (actor);
          g_object_set_data_full (G_OBJECT (actor), ORIG_PARENT_KEY,
                                  g_object_ref (old_parent),
                                  (GDestroyNotify) g_object_unref);

          clutter_actor_remove_child (old_parent, actor);
          new_parent = win_workspace == to ? workspace1 : workspace0;
          clutter_actor_add_child (new_parent, actor);
          clutter_actor_show (actor);
          clutter_actor_set_child_above_sibling (new_parent, actor, NULL);
        }
      else
        {
          /* Window on some other desktop */
          clutter_actor_hide (actor);
        }

      l = l->prev;
    }

  priv->desktop1 = workspace0;
  priv->desktop2 = workspace1;

  clutter_actor_save_easing_state (workspace0);
  clutter_actor_set_easing_mode (workspace0, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration (workspace0, SWITCH_TIMEOUT);
  clutter_actor_set_scale (workspace0, 1.0, 1.0);
  g_signal_connect (workspace0, "transitions-completed",
                    G_CALLBACK (on_switch_workspace_effect_complete),
                    plugin);
  clutter_actor_restore_easing_state (workspace0);

  clutter_actor_save_easing_state (workspace1);
  clutter_actor_set_easing_mode (workspace1, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration (workspace1, SWITCH_TIMEOUT);
  clutter_actor_set_scale (workspace1, 0.0, 0.0);
  g_signal_connect (workspace1, "transitions-completed",
                    G_CALLBACK (on_switch_workspace_effect_complete),
                    plugin);
  clutter_actor_restore_easing_state (workspace1);
}


/*
 * Minimize effect completion callback; this function restores actor state, and
 * calls the manager callback function.
 */
static void
on_minimize_effect_complete (ClutterActor *actor,
                             MetaPlugin   *plugin)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (actor);

  clutter_actor_hide (actor);

  /* FIXME - we shouldn't assume the original scale, it should be saved
   * at the start of the effect */
  clutter_actor_set_scale (actor, 1.0, 1.0);
  clutter_actor_move_anchor_point_from_gravity (actor,
                                                CLUTTER_GRAVITY_NORTH_WEST);

  /* Now notify the manager that we are done with this effect */
  meta_plugin_minimize_completed (plugin, window_actor);
}

/*
 * Simple minimize handler: it applies a scale effect (which must be reversed on
 * completion).
 */
static void
minimize (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  MetaRectangle icon_geometry;
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);
  ClutterActor *actor  = CLUTTER_ACTOR (window_actor);


  type = meta_window_get_window_type (meta_window);

  if (!meta_window_get_icon_geometry(meta_window, &icon_geometry))
    {
      icon_geometry.x = 0;
      icon_geometry.y = 0;
    }

  if (type == META_WINDOW_NORMAL)
    {
      clutter_actor_move_anchor_point_from_gravity (actor,
                                                    CLUTTER_GRAVITY_CENTER);

      clutter_actor_save_easing_state (actor);
      clutter_actor_set_easing_mode (actor, CLUTTER_EASE_IN_SINE);
      clutter_actor_set_easing_duration (actor, MINIMIZE_TIMEOUT);
      clutter_actor_set_scale (actor, 0.0, 0.0);
      clutter_actor_set_position (actor, icon_geometry.x, icon_geometry.y);
      clutter_actor_restore_easing_state (actor);

      g_signal_connect (actor, "transitions-completed",
                        G_CALLBACK (on_minimize_effect_complete),
                        plugin);

    }
  else
    meta_plugin_minimize_completed (plugin, window_actor);
}

/*
 * Minimize effect completion callback; this function restores actor state, and
 * calls the manager callback function.
 */
static void
on_maximize_effect_complete (ClutterActor *actor,
                             MetaPlugin   *plugin)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (actor);

  /* FIXME - don't assume the original scale was 1.0 */
  clutter_actor_set_scale (actor, 1.0, 1.0);
  clutter_actor_move_anchor_point_from_gravity (actor,
                                                CLUTTER_GRAVITY_NORTH_WEST);

  /* Now notify the manager that we are done with this effect */
  meta_plugin_maximize_completed (plugin, window_actor);
}

/*
 * The Nature of Maximize operation is such that it is difficult to do a visual
 * effect that would work well. Scaling, the obvious effect, does not work that
 * well, because at the end of the effect we end up with window content bigger
 * and differently laid out than in the real window; this is a proof concept.
 *
 * (Something like a sound would be more appropriate.)
 */
static void
maximize (MetaPlugin *plugin,
          MetaWindowActor *window_actor,
          gint end_x, gint end_y, gint end_width, gint end_height)
{
  MetaWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);

  gdouble  scale_x    = 1.0;
  gdouble  scale_y    = 1.0;
  gfloat   anchor_x   = 0;
  gfloat   anchor_y   = 0;

  type = meta_window_get_window_type (meta_window);

  if (type == META_WINDOW_NORMAL)
    {
      gfloat width, height;
      gfloat x, y;

      clutter_actor_get_size (actor, &width, &height);
      clutter_actor_get_position (actor, &x, &y);

      /*
       * Work out the scale and anchor point so that the window is expanding
       * smoothly into the target size.
       */
      scale_x = (gdouble)end_width / (gdouble) width;
      scale_y = (gdouble)end_height / (gdouble) height;

      anchor_x = (gdouble)(x - end_x)*(gdouble)width /
        ((gdouble)(end_width - width));
      anchor_y = (gdouble)(y - end_y)*(gdouble)height /
        ((gdouble)(end_height - height));

      clutter_actor_move_anchor_point (actor, anchor_x, anchor_y);

      clutter_actor_save_easing_state (actor);
      clutter_actor_set_easing_mode (actor, CLUTTER_EASE_IN_SINE);
      clutter_actor_set_easing_duration (actor, MAXIMIZE_TIMEOUT);
      clutter_actor_set_scale (actor, scale_x, scale_y);
      clutter_actor_restore_easing_state (actor);

      g_signal_connect (actor, "transitions-completed",
                        G_CALLBACK (on_maximize_effect_complete),
                        plugin);
    }
  else
    meta_plugin_maximize_completed (plugin, window_actor);
}

/*
 * See comments on the maximize() function.
 *
 * (Just a skeleton code.)
 */
static void
unmaximize (MetaPlugin *plugin,
            MetaWindowActor *window_actor,
            gint end_x, gint end_y, gint end_width, gint end_height)
{
  meta_plugin_unmaximize_completed (plugin, window_actor);
}

static void
on_map_effect_complete (ClutterActor *actor,
                        MetaPlugin   *plugin)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (actor);

  clutter_actor_move_anchor_point_from_gravity (actor, CLUTTER_GRAVITY_NORTH_WEST);

  /* Now notify the manager that we are done with this effect */
  meta_plugin_map_completed (plugin, window_actor);
}

/*
 * Simple map handler: it applies a scale effect which must be reversed on
 * completion).
 */
static void
map (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);

  type = meta_window_get_window_type (meta_window);

  if (type == META_WINDOW_NORMAL)
    {
      clutter_actor_move_anchor_point_from_gravity (actor,
                                                    CLUTTER_GRAVITY_CENTER);

      clutter_actor_set_scale (actor, 0.0, 0.0);
      clutter_actor_show (actor);

      clutter_actor_save_easing_state (actor);
      clutter_actor_set_easing_mode (actor, CLUTTER_EASE_IN_SINE);
      clutter_actor_set_easing_duration (actor, MAP_TIMEOUT);
      clutter_actor_set_scale (actor, 1.0, 1.0);
      clutter_actor_restore_easing_state (actor);

      g_signal_connect (actor, "transitions-completed",
                        G_CALLBACK (on_map_effect_complete),
                        plugin);
    }
  else
    meta_plugin_map_completed (plugin, window_actor);
}

/*
 * Destroy effect completion callback; this is a simple effect that requires no
 * further action than notifying the manager that the effect is completed.
 */
static void
on_destroy_effect_complete (ClutterActor *actor,
                            MetaPlugin   *plugin)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (actor);

  meta_plugin_destroy_completed (plugin, window_actor);
}

/*
 * Simple TV-out like effect.
 */
static void
destroy (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
  MetaWindowType type;
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  MetaWindow *meta_window = meta_window_actor_get_meta_window (window_actor);

  type = meta_window_get_window_type (meta_window);

  if (type == META_WINDOW_NORMAL)
    {
      clutter_actor_move_anchor_point_from_gravity (actor,
                                                    CLUTTER_GRAVITY_CENTER);


      clutter_actor_save_easing_state (actor);
      clutter_actor_set_easing_mode (actor, CLUTTER_EASE_IN_SINE);
      clutter_actor_set_easing_duration (actor, DESTROY_TIMEOUT);
      clutter_actor_set_scale (actor, 1.0, 1.0);

      g_signal_connect (actor, "transitions-completed",
                        G_CALLBACK (on_destroy_effect_complete),
                        plugin);
    }
  else
    meta_plugin_destroy_completed (plugin, window_actor);
}

static void
kill_switch_workspace (MetaPlugin *plugin)
{
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;

  clutter_actor_remove_all_transitions (priv->desktop1);
  clutter_actor_remove_all_transitions (priv->desktop2);
}

static void
kill_window_effects (MetaPlugin      *plugin,
                     MetaWindowActor *window_actor)
{
  clutter_actor_remove_all_transitions (CLUTTER_ACTOR (window_actor));
}

static const MetaPluginInfo *
plugin_info (MetaPlugin *plugin)
{
  MetaDefaultPluginPrivate *priv = META_DEFAULT_PLUGIN (plugin)->priv;

  return &priv->info;
}
