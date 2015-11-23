.AUTOPARALLEL:

lib_LTLIBRARIES = libmutter-clutter.la

CLEANFILES =
DISTCLEANFILES =
EXTRA_DIST =
BUILT_SOURCES =
NULL =

AM_CPPFLAGS =								\
	-DCLUTTER_COMPILATION=1						\
	-DCOGL_ENABLE_EXPERIMENTAL_API					\
	-DCOGL_DISABLE_DEPRECATION_WARNINGS				\
	-DCOGL_ENABLE_EXPERIMENTAL_2_0_API				\
	-DG_LOG_DOMAIN=\"Clutter\"					\
	-DCLUTTER_LOCALEDIR=\""$(localedir)"\"				\
	-DCLUTTER_SYSCONFDIR=\""$(sysconfdir)"\"			\
	-I$(top_srcdir)/clutter/cally					\
	$(NULL)

CLUTTER_BASE_source_c =							\
	clutter-action.c						\
	clutter-actor-box.c						\
	clutter-actor.c							\
	clutter-actor-meta.c						\
	clutter-align-constraint.c					\
	clutter-animatable.c						\
	clutter-backend.c						\
	clutter-base-types.c						\
	clutter-bezier.c						\
	clutter-bind-constraint.c					\
	clutter-binding-pool.c						\
	clutter-bin-layout.c						\
	clutter-blur-effect.c						\
	clutter-box-layout.c						\
	clutter-brightness-contrast-effect.c				\
	clutter-cairo.c							\
	clutter-canvas.c						\
	clutter-child-meta.c						\
	clutter-click-action.c						\
	clutter-clone.c							\
	clutter-color.c							\
	clutter-colorize-effect.c					\
	clutter-constraint.c						\
	clutter-container.c						\
	clutter-content.c						\
	clutter-deform-effect.c						\
	clutter-desaturate-effect.c					\
	clutter-device-manager.c					\
	clutter-drag-action.c						\
	clutter-drop-action.c						\
	clutter-easing.c						\
	clutter-effect.c						\
	clutter-event.c							\
	clutter-event-translator.c					\
	clutter-feature.c						\
	clutter-fixed-layout.c						\
	clutter-flatten-effect.c					\
	clutter-flow-layout.c						\
	clutter-gesture-action.c					\
	clutter-grid-layout.c						\
	clutter-id-pool.c						\
	clutter-image.c							\
	clutter-input-device.c						\
	clutter-interval.c						\
	clutter-keyframe-transition.c					\
	clutter-keysyms-table.c						\
	clutter-layout-manager.c					\
	clutter-layout-meta.c						\
	clutter-main.c							\
	clutter-master-clock.c						\
	clutter-master-clock-default.c					\
	clutter-offscreen-effect.c					\
	clutter-page-turn-effect.c					\
	clutter-paint-node.c						\
	clutter-paint-nodes.c						\
	clutter-paint-volume.c						\
	clutter-pan-action.c						\
	clutter-path.c							\
	clutter-path-constraint.c					\
	clutter-property-transition.c					\
	clutter-rotate-action.c						\
	clutter-scriptable.c						\
	clutter-script.c						\
	clutter-script-parser.c						\
	clutter-scroll-actor.c						\
	clutter-settings.c						\
	clutter-shader-effect.c						\
	clutter-shader-types.c						\
	clutter-snap-constraint.c					\
	clutter-stage.c							\
	clutter-stage-manager.c						\
	clutter-stage-window.c						\
	clutter-swipe-action.c						\
	clutter-tap-action.c						\
	clutter-test-utils.c						\
	clutter-text-buffer.c						\
	clutter-text.c							\
	clutter-timeline.c						\
	clutter-transition.c						\
	clutter-transition-group.c					\
	clutter-units.c							\
	clutter-util.c							\
	clutter-zoom-action.c						\
	$(NULL)

CLUTTER_BASE_source_h =							\
	clutter-action.h						\
	clutter-actor.h							\
	clutter-actor-meta.h						\
	clutter-actor-meta-private.h					\
	clutter-actor-private.h						\
	clutter-align-constraint.h					\
	clutter-animatable.h						\
	clutter-autocleanups.h						\
	clutter-backend.h						\
	clutter-backend-private.h					\
	clutter-bezier.h						\
	clutter-bind-constraint.h					\
	clutter-binding-pool.h						\
	clutter-bin-layout.h						\
	clutter-blur-effect.h						\
	clutter-box-layout.h						\
	clutter-brightness-contrast-effect.h				\
	clutter-cairo.h							\
	clutter-canvas.h						\
	clutter-child-meta.h						\
	clutter-click-action.h						\
	clutter-clone.h							\
	clutter-cogl-compat.h						\
	clutter-color.h							\
	clutter-colorize-effect.h					\
	clutter-color-static.h						\
	clutter-config.h						\
	clutter-constraint.h						\
	clutter-constraint-private.h					\
	clutter-container.h						\
	clutter-content.h						\
	clutter-content-private.h					\
	clutter-debug.h							\
	clutter-deform-effect.h						\
	clutter-deprecated.h						\
	clutter-desaturate-effect.h					\
	clutter-device-manager.h					\
	clutter-device-manager-private.h				\
	clutter-drag-action.h						\
	clutter-drop-action.h						\
	clutter-easing.h						\
	clutter-effect.h						\
	clutter-effect-private.h					\
	clutter-enums.h							\
	clutter-event.h							\
	clutter-event-private.h						\
	clutter-event-translator.h					\
	clutter-feature.h						\
	clutter-fixed-layout.h						\
	clutter-flatten-effect.h					\
	clutter-flow-layout.h						\
	clutter-gesture-action.h					\
	clutter-gesture-action-private.h				\
	clutter-grid-layout.h						\
	clutter-group.h							\
	clutter.h							\
	clutter-id-pool.h						\
	clutter-image.h							\
	clutter-input-device.h						\
	clutter-interval.h						\
	clutter-keyframe-transition.h					\
	clutter-keysyms.h						\
	clutter-layout-manager.h					\
	clutter-layout-meta.h						\
	clutter-macros.h						\
	clutter-main.h							\
	clutter-master-clock-default.h					\
	clutter-master-clock.h						\
	clutter-offscreen-effect.h					\
	clutter-offscreen-effect-private.h				\
	clutter-page-turn-effect.h					\
	clutter-paint-node.h						\
	clutter-paint-node-private.h					\
	clutter-paint-nodes.h						\
	clutter-paint-volume-private.h					\
	clutter-pan-action.h						\
	clutter-path-constraint.h					\
	clutter-path.h							\
	clutter-private.h						\
	clutter-property-transition.h					\
	clutter-rotate-action.h						\
	clutter-scriptable.h						\
	clutter-script.h						\
	clutter-script-private.h					\
	clutter-scroll-actor.h						\
	clutter-settings.h						\
	clutter-settings-private.h					\
	clutter-shader-effect.h						\
	clutter-shader-types.h						\
	clutter-snap-constraint.h					\
	clutter-stage.h							\
	clutter-stage-manager.h						\
	clutter-stage-manager-private.h					\
	clutter-stage-private.h						\
	clutter-stage-window.h						\
	clutter-swipe-action.h						\
	clutter-tap-action.h						\
	clutter-test-utils.h						\
	clutter-text-buffer.h						\
	clutter-text.h							\
	clutter-texture.h						\
	clutter-timeline.h						\
	clutter-transition-group.h					\
	clutter-transition.h						\
	clutter-types.h							\
	clutter-units.h							\
	clutter-zoom-action.h						\
	$(NULL)

CLUTTER_BASE =								\
	$(CLUTTER_BASE_source_c)					\
	$(CLUTTER_BASE_source_h)					\
	$(NULL)

CLUTTER_CALLY_source_c =						\
	cally/cally-actor.c						\
	cally/cally.c							\
	cally/cally-clone.c						\
	cally/cally-group.c						\
	cally/cally-rectangle.c						\
	cally/cally-root.c						\
	cally/cally-stage.c						\
	cally/cally-text.c						\
	cally/cally-texture.c						\
	cally/cally-util.c						\
	$(NULL)

CLUTTER_CALLY_source_h =						\
	cally/cally-actor.h						\
	cally/cally-actor-private.h					\
	cally/cally-clone.h						\
	cally/cally-factory.h						\
	cally/cally-group.h						\
	cally/cally.h							\
	cally/cally-main.h						\
	cally/cally-rectangle.h						\
	cally/cally-root.h						\
	cally/cally-stage.h						\
	cally/cally-text.h						\
	cally/cally-texture.h						\
	cally/cally-util.h						\
	$(NULL)

CLUTTER_CALLY =								\
	$(CLUTTER_CALLY_source_c)					\
	$(CLUTTER_CALLY_source_h)					\
	$(NULL)

CLUTTER_DEPRECATED_source_c =						\
	deprecated/clutter-actor-deprecated.c				\
	deprecated/clutter-alpha.c					\
	deprecated/clutter-animation.c					\
	deprecated/clutter-animator.c					\
	deprecated/clutter-behaviour.c					\
	deprecated/clutter-behaviour-depth.c				\
	deprecated/clutter-behaviour-ellipse.c				\
	deprecated/clutter-behaviour-opacity.c				\
	deprecated/clutter-behaviour-path.c				\
	deprecated/clutter-behaviour-rotate.c				\
	deprecated/clutter-behaviour-scale.c				\
	deprecated/clutter-box.c					\
	deprecated/clutter-cairo-texture.c				\
	deprecated/clutter-fixed.c					\
	deprecated/clutter-frame-source.c				\
	deprecated/clutter-group.c					\
	deprecated/clutter-input-device-deprecated.c			\
	deprecated/clutter-layout-manager-deprecated.c			\
	deprecated/clutter-list-model.c					\
	deprecated/clutter-media.c					\
	deprecated/clutter-model.c					\
	deprecated/clutter-rectangle.c					\
	deprecated/clutter-score.c					\
	deprecated/clutter-shader.c					\
	deprecated/clutter-state.c					\
	deprecated/clutter-table-layout.c				\
	deprecated/clutter-texture.c					\
	deprecated/clutter-timeout-interval.c				\
	deprecated/clutter-timeout-pool.c				\
	$(NULL)

CLUTTER_DEPRECATED_source_h =						\
	deprecated/clutter-actor.h					\
	deprecated/clutter-alpha.h					\
	deprecated/clutter-animatable.h					\
	deprecated/clutter-animation.h					\
	deprecated/clutter-animator.h					\
	deprecated/clutter-backend.h					\
	deprecated/clutter-behaviour-depth.h				\
	deprecated/clutter-behaviour-ellipse.h				\
	deprecated/clutter-behaviour.h					\
	deprecated/clutter-behaviour-opacity.h				\
	deprecated/clutter-behaviour-path.h				\
	deprecated/clutter-behaviour-rotate.h				\
	deprecated/clutter-behaviour-scale.h				\
	deprecated/clutter-bin-layout.h					\
	deprecated/clutter-box.h					\
	deprecated/clutter-cairo-texture.h				\
	deprecated/clutter-container.h					\
	deprecated/clutter-fixed.h					\
	deprecated/clutter-frame-source.h				\
	deprecated/clutter-group.h					\
	deprecated/clutter-input-device.h				\
	deprecated/clutter-keysyms.h					\
	deprecated/clutter-list-model.h					\
	deprecated/clutter-main.h					\
	deprecated/clutter-media.h					\
	deprecated/clutter-model.h					\
	deprecated/clutter-model-private.h				\
	deprecated/clutter-rectangle.h					\
	deprecated/clutter-score.h					\
	deprecated/clutter-shader.h					\
	deprecated/clutter-stage.h					\
	deprecated/clutter-stage-manager.h				\
	deprecated/clutter-state.h					\
	deprecated/clutter-table-layout.h				\
	deprecated/clutter-texture.h					\
	deprecated/clutter-timeline.h					\
	deprecated/clutter-timeout-interval.h				\
	deprecated/clutter-timeout-pool.h				\
	deprecated/clutter-util.h					\
	$(NULL)

CLUTTER_DEPRECATED =							\
	$(CLUTTER_DEPRECATED_source_c)					\
	$(CLUTTER_DEPRECATED_source_h)					\
	$(NULL)

CLUTTER_COGL =								\
	cogl/clutter-stage-cogl.c					\
	cogl/clutter-stage-cogl.h					\
	$(NULL)

CLUTTER_EGL =								\
	egl/clutter-backend-eglnative.c					\
	egl/clutter-backend-eglnative.h					\
	egl/clutter-egl.h						\
	egl/clutter-egl-headers.h					\
	egl/clutter-stage-eglnative.c					\
	egl/clutter-stage-eglnative.h					\
	$(NULL)

CLUTTER_EVDEV =								\
	evdev/clutter-device-manager-evdev.c				\
	evdev/clutter-device-manager-evdev.h				\
	evdev/clutter-evdev.h						\
	evdev/clutter-input-device-evdev.c				\
	evdev/clutter-input-device-evdev.h				\
	evdev/clutter-xkb-utils.c					\
	evdev/clutter-xkb-utils.h					\
	$(NULL)

CLUTTER_TSLIB =								\
	tslib/clutter-event-tslib.c					\
	tslib/clutter-event-tslib.h					\
	$(NULL)

CLUTTER_WAYLAND =							\
	wayland/clutter-backend-wayland.c				\
	wayland/clutter-backend-wayland.h				\
	wayland/clutter-backend-wayland-priv.h				\
	wayland/clutter-device-manager-wayland.c			\
	wayland/clutter-device-manager-wayland.h			\
	wayland/clutter-event-wayland.c					\
	wayland/clutter-event-wayland.h					\
	wayland/clutter-input-device-wayland.c				\
	wayland/clutter-input-device-wayland.h				\
	wayland/clutter-stage-wayland.c					\
	wayland/clutter-stage-wayland.h					\
	wayland/clutter-wayland-compositor.h				\
	wayland/clutter-wayland.h					\
	wayland/clutter-wayland-surface.c				\
	wayland/clutter-wayland-surface.h				\
	$(NULL)

CLUTTER_X11 =								\
	x11/clutter-backend-x11.c					\
	x11/clutter-backend-x11.h					\
	x11/clutter-device-manager-core-x11.c				\
	x11/clutter-device-manager-core-x11.h				\
	x11/clutter-device-manager-xi2.c				\
	x11/clutter-device-manager-xi2.h				\
	x11/clutter-event-x11.c						\
	x11/clutter-glx.h						\
	x11/clutter-glx-texture-pixmap.c				\
	x11/clutter-glx-texture-pixmap.h				\
	x11/clutter-input-device-core-x11.c				\
	x11/clutter-input-device-core-x11.h				\
	x11/clutter-input-device-xi2.c					\
	x11/clutter-input-device-xi2.h					\
	x11/clutter-keymap-x11.c					\
	x11/clutter-keymap-x11.h					\
	x11/clutter-settings-x11.h					\
	x11/clutter-stage-x11.c						\
	x11/clutter-stage-x11.h						\
	x11/clutter-x11.h						\
	x11/clutter-x11-texture-pixmap.c				\
	x11/clutter-x11-texture-pixmap.h				\
	x11/xsettings/xsettings-client.c				\
	x11/xsettings/xsettings-client.h				\
	x11/xsettings/xsettings-common.c				\
	x11/xsettings/xsettings-common.h				\
	$(NULL)

CLUTTER_source_c =							\
	$(CLUTTER_BASE_source_c)					\
	$(CLUTTER_CALLY_source_c)					\
	$(CLUTTER_DEPRECATED_source_c)					\
	$(NULL)

CLUTTER_source_h =							\
	$(CLUTTER_BASE_source_h)					\
	$(CLUTTER_CALLY_source_h)					\
	$(CLUTTER_DEPRECATED_source_h)					\
	$(NULL)

# built sources
CLUTTER_built_source_c =						\
	clutter-enum-types.c						\
	clutter-marshal.c						\
	$(NULL)

# built headers
CLUTTER_built_source_h =						\
	clutter-enum-types.h						\
	clutter-marshal.h						\
	$(NULL)

# glib-genmarshal rules
glib_marshal_list = clutter-marshal.list
glib_marshal_prefix = _clutter_marshal
include $(top_srcdir)/build/autotools/Makefile.am.marshal

# glib-mkenums rules
glib_enum_h = clutter-enum-types.h
glib_enum_c = clutter-enum-types.c
glib_enum_headers = $(CLUTTER_source_h)
include $(top_srcdir)/build/autotools/Makefile.am.enums


libmutter_clutter_la_SOURCES =						\
	$(CLUTTER_BASE)							\
	$(CLUTTER_DEPRECATED)						\
	$(CLUTTER_CALLY)						\
	$(CLUTTER_COGL)							\
	$(CLUTTER_X11)							\
	$(NULL)

nodist_libmutter_clutter_la_SOURCES =					\
	$(CLUTTER_built_source_c)					\
	$(CLUTTER_built_source_h)					\
	$(NULL)

if HAVE_NATIVE_BACKEND
libmutter_clutter_la_SOURCES +=						\
	$(CLUTTER_EVDEV)						\
	$(CLUTTER_EGL)							\
	$(NULL)
endif

if HAVE_WAYLAND
libmutter_clutter_la_SOURCES +=						\
	$(CLUTTER_WAYLAND)						\
	$(NULL)
endif

libmutter_clutter_la_CFLAGS =						\
	$(AM_CPPFLAGS)							\
	$(MUTTER_CLUTTER_CFLAGS)					\
	$(NULL)

libmutter_clutter_la_LIBADD =						\
	$(MUTTER_CLUTTER_LIBS)						\
	$(NULL)

libmutter_clutter_la_LDFLAGS =						\
	-no-undefined -export-symbols-regex "^clutter_.*"		\
	-export-dynamic							\
	$(NULL)
