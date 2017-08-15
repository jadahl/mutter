/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Rob Bradford <rob@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Kristian Høgsberg (from eglkms.c)
 *   Benjamin Franzke (from eglkms.c)
 *   Robert Bragg <robert@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Neil Roberts <neil@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Jonas Ådahl <jadahl@redhat.com>
 *
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-egl.h"
#include "backends/meta-egl-ext.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-output.h"
#include "backends/meta-renderer-view.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-monitor-manager-kms.h"
#include "backends/native/meta-renderer-native.h"
#include "cogl/cogl.h"
#include "core/boxes-private.h"

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

enum
{
  PROP_0,

  PROP_MONITOR_MANAGER,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _MetaOnscreenNativeGpuData
{
  struct {
    struct gbm_device *device;
  } gbm;

#ifdef HAVE_EGL_DEVICE
  struct {
    EGLDeviceEXT device;

    gboolean no_egl_output_drm_flip_event;
  } egl;
#endif

  MetaRendererNativeMode mode;

  gboolean no_add_fb2;
} MetaRendererNativeGpuData;

typedef struct _MetaDumbBuffer
{
  uint32_t fb_id;
  uint32_t handle;
  void *map;
  uint64_t map_size;
} MetaDumbBuffer;

typedef struct _MetaOnscreenNative
{
  MetaGpuKms *gpu_kms;

  struct {
    struct gbm_surface *surface;
    uint32_t current_fb_id;
    uint32_t next_fb_id;
    struct gbm_bo *current_bo;
    struct gbm_bo *next_bo;
  } gbm;

#ifdef HAVE_EGL_DEVICE
  struct {
    EGLStreamKHR stream;

    MetaDumbBuffer dumb_fb;
  } egl;
#endif

  gboolean pending_queue_swap_notify;
  gboolean pending_swap_notify;

  gboolean pending_set_crtc;

  int64_t pending_queue_swap_notify_frame_count;
  int64_t pending_swap_notify_frame_count;

  MetaRendererView *view;
  int pending_flips;
} MetaOnscreenNative;

struct _MetaRendererNative
{
  MetaRenderer parent;

  MetaMonitorManagerKms *monitor_manager_kms;

  EGLDisplay egl_display;

  CoglClosure *swap_notify_idle;

  int64_t frame_counter;
};

static GQuark quark_onscreen_native_gpu_data = 0;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaRendererNative,
                         meta_renderer_native,
                         META_TYPE_RENDERER,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static const CoglWinsysEGLVtable _cogl_winsys_egl_vtable;
static const CoglWinsysVtable *parent_vtable;

static void
release_dumb_fb (MetaDumbBuffer *dumb_fb,
                 MetaGpuKms     *gpu_kms);

static gboolean
init_dumb_fb (MetaDumbBuffer *dumb_fb,
              MetaGpuKms     *gpu_kms,
              int             width,
              int             height,
              uint32_t        format,
              GError        **error);

static void
meta_renderer_native_gpu_data_free (MetaRendererNativeGpuData *renderer_gpu_data)
{
  g_free (renderer_gpu_data);
}

static MetaRendererNativeGpuData *
meta_renderer_native_gpu_data_from_gpu (MetaGpuKms *gpu_kms)
{
  return g_object_get_qdata (G_OBJECT (gpu_kms),
                             quark_onscreen_native_gpu_data);
}

struct gbm_device *
meta_gbm_device_from_gpu (MetaGpuKms *gpu_kms)
{
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = meta_renderer_native_gpu_data_from_gpu (gpu_kms);

  return renderer_gpu_data->gbm.device;
}

static MetaRendererNativeGpuData *
meta_create_renderer_native_gpu_data (MetaGpuKms *gpu_kms)
{
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = g_new0 (MetaRendererNativeGpuData, 1);
  g_object_set_qdata_full (G_OBJECT (gpu_kms),
                           quark_onscreen_native_gpu_data,
                           renderer_gpu_data,
                           (GDestroyNotify) meta_renderer_native_gpu_data_free);

  return renderer_gpu_data;
}

static MetaEgl *
meta_renderer_native_get_egl (MetaRendererNative *renderer_native)
{
  MetaMonitorManager *monitor_manager =
    META_MONITOR_MANAGER (renderer_native->monitor_manager_kms);
  MetaBackend *backend = meta_monitor_manager_get_backend (monitor_manager);

  return meta_backend_get_egl (backend);
}

static void
meta_renderer_native_disconnect (CoglRenderer *cogl_renderer)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

  if (cogl_renderer_egl->edpy != EGL_NO_DISPLAY)
    meta_egl_terminate (egl, cogl_renderer_egl->edpy, NULL);

  g_slice_free (CoglRendererEGL, cogl_renderer_egl);
}

static void
flush_pending_swap_notify (CoglFramebuffer *framebuffer)
{
  if (framebuffer->type == COGL_FRAMEBUFFER_TYPE_ONSCREEN)
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
      CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
      MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

      if (onscreen_native->pending_swap_notify)
        {
          CoglFrameInfo *info;

          while ((info = g_queue_peek_head (&onscreen->pending_frame_infos)) &&
                 info->global_frame_counter <= onscreen_native->pending_swap_notify_frame_count)
            {
              _cogl_onscreen_notify_frame_sync (onscreen, info);
              _cogl_onscreen_notify_complete (onscreen, info);
              cogl_object_unref (info);
              g_queue_pop_head (&onscreen->pending_frame_infos);
            }

          onscreen_native->pending_swap_notify = FALSE;
          cogl_object_unref (onscreen);
        }
    }
}

static void
flush_pending_swap_notify_idle (void *user_data)
{
  CoglContext *cogl_context = user_data;
  CoglRendererEGL *cogl_renderer_egl = cogl_context->display->renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  GList *l;

  /* This needs to be disconnected before invoking the callbacks in
   * case the callbacks cause it to be queued again */
  _cogl_closure_disconnect (renderer_native->swap_notify_idle);
  renderer_native->swap_notify_idle = NULL;

  l = cogl_context->framebuffers;
  while (l)
    {
      GList *next = l->next;
      CoglFramebuffer *framebuffer = l->data;

      flush_pending_swap_notify (framebuffer);

      l = next;
    }
}

static void
free_current_bo (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaGpuKms *gpu_kms = onscreen_native->gpu_kms;
  int kms_fd;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  if (onscreen_native->gbm.current_fb_id)
    {
      drmModeRmFB (kms_fd, onscreen_native->gbm.current_fb_id);
      onscreen_native->gbm.current_fb_id = 0;
    }
  if (onscreen_native->gbm.current_bo)
    {
      gbm_surface_release_buffer (onscreen_native->gbm.surface,
                                  onscreen_native->gbm.current_bo);
      onscreen_native->gbm.current_bo = NULL;
    }
}

static void
meta_onscreen_native_queue_swap_notify (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;

  onscreen_native->pending_swap_notify_frame_count =
    onscreen_native->pending_queue_swap_notify_frame_count;

  if (onscreen_native->pending_swap_notify)
    return;

  /* We only want to notify that the swap is complete when the
   * application calls cogl_context_dispatch so instead of
   * immediately notifying we queue an idle callback */
  if (!renderer_native->swap_notify_idle)
    {
      renderer_native->swap_notify_idle =
        _cogl_poll_renderer_add_idle (cogl_renderer,
                                      flush_pending_swap_notify_idle,
                                      cogl_context,
                                      NULL);
    }

  /*
   * The framebuffer will have its own referenc while the swap notify is
   * pending. Otherwise when destroying the view would drop the pending
   * notification with if the destruction happens before the idle callback
   * is invoked.
   */
  cogl_object_ref (onscreen);
  onscreen_native->pending_swap_notify = TRUE;
}

static gboolean
meta_renderer_native_connect (CoglRenderer *cogl_renderer,
                              GError      **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  CoglRendererEGL *cogl_renderer_egl;

  cogl_renderer->winsys = g_slice_new0 (CoglRendererEGL);
  cogl_renderer_egl = cogl_renderer->winsys;

  cogl_renderer_egl->platform_vtable = &_cogl_winsys_egl_vtable;
  cogl_renderer_egl->platform = renderer_native;
  cogl_renderer_egl->edpy = renderer_native->egl_display;

  if (!_cogl_winsys_egl_renderer_connect_common (cogl_renderer, error))
    goto fail;

  return TRUE;

fail:
  meta_renderer_native_disconnect (cogl_renderer);

  return FALSE;
}

static int
meta_renderer_native_add_egl_config_attributes (CoglDisplay           *cogl_display,
                                                CoglFramebufferConfig *config,
                                                EGLint                *attributes)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_display->renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  MetaMonitorManagerKms *monitor_manager_kms =
    renderer_native->monitor_manager_kms;
  MetaGpuKms *primary_gpu =
    meta_monitor_manager_kms_get_primary_gpu (monitor_manager_kms);
  MetaRendererNativeGpuData *primary_renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (primary_gpu);
  int i = 0;

  switch (primary_renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_WINDOW_BIT;
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_STREAM_BIT_KHR;
      break;
#endif
    }

  return i;
}

static gboolean
meta_renderer_native_setup_egl_display (CoglDisplay *cogl_display,
                                        GError     **error)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRendererEGL *cogl_renderer_egl = cogl_display->renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;

  cogl_display_egl->platform = renderer_native;

  /* Force a full modeset / drmModeSetCrtc on
   * the first swap buffers call.
   */
  meta_renderer_native_queue_modes_reset (renderer_native);

  return TRUE;
}

static void
meta_renderer_native_destroy_egl_display (CoglDisplay *cogl_display)
{
}

static EGLSurface
create_dummy_pbuffer_surface (EGLDisplay egl_display,
                              GError   **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  EGLConfig pbuffer_config;
  static const EGLint pbuffer_config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 0,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };
  static const EGLint pbuffer_attribs[] = {
    EGL_WIDTH, 16,
    EGL_HEIGHT, 16,
    EGL_NONE
  };

  if (!meta_egl_choose_config (egl, egl_display, pbuffer_config_attribs,
                               &pbuffer_config, error))
    return EGL_NO_SURFACE;

  return meta_egl_create_pbuffer_surface (egl, egl_display,
                                          pbuffer_config, pbuffer_attribs,
                                          error);
}

static gboolean
meta_renderer_native_egl_context_created (CoglDisplay *cogl_display,
                                          GError     **error)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

  if ((cogl_renderer_egl->private_features &
       COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT) == 0)
    {
      cogl_display_egl->dummy_surface =
        create_dummy_pbuffer_surface (cogl_renderer_egl->edpy, error);
      if (cogl_display_egl->dummy_surface == EGL_NO_SURFACE)
        return FALSE;
    }

  if (!_cogl_winsys_egl_make_current (cogl_display,
                                      cogl_display_egl->dummy_surface,
                                      cogl_display_egl->dummy_surface,
                                      cogl_display_egl->egl_context))
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "Failed to make context current");
      return FALSE;
    }

  return TRUE;
}

static void
meta_renderer_native_egl_cleanup_context (CoglDisplay *cogl_display)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

  if (cogl_display_egl->dummy_surface != EGL_NO_SURFACE)
    {
      meta_egl_destroy_surface (egl,
                                cogl_renderer_egl->edpy,
                                cogl_display_egl->dummy_surface,
                                NULL);
      cogl_display_egl->dummy_surface = EGL_NO_SURFACE;
    }
}

static void
meta_onscreen_native_swap_drm_fb (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

  free_current_bo (onscreen);

  onscreen_native->gbm.current_fb_id = onscreen_native->gbm.next_fb_id;
  onscreen_native->gbm.next_fb_id = 0;

  onscreen_native->gbm.current_bo = onscreen_native->gbm.next_bo;
  onscreen_native->gbm.next_bo = NULL;
}

static void
on_crtc_flipped (GClosure         *closure,
                 MetaGpuKms       *gpu_kms,
                 MetaRendererView *view)
{
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (view);
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (stage_view);
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

  onscreen_native->pending_flips--;
  if (onscreen_native->pending_flips == 0)
    {
      MetaRendererNativeGpuData *renderer_gpu_data =
        meta_renderer_native_gpu_data_from_gpu (onscreen_native->gpu_kms);

      onscreen_native->pending_queue_swap_notify = FALSE;

      meta_onscreen_native_queue_swap_notify (onscreen);

      switch (renderer_gpu_data->mode)
        {
        case META_RENDERER_NATIVE_MODE_GBM:
          meta_onscreen_native_swap_drm_fb (onscreen);
          break;
#ifdef HAVE_EGL_DEVICE
        case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
          break;
#endif
        }
    }
}

static void
flip_closure_destroyed (MetaRendererView *view)
{
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (view);
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (stage_view);
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaGpuKms *gpu_kms = onscreen_native->gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (gpu_kms);

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (onscreen_native->gbm.next_fb_id)
        {
          int kms_fd;

          kms_fd = meta_gpu_kms_get_fd (gpu_kms);
          drmModeRmFB (kms_fd, onscreen_native->gbm.next_fb_id);
          gbm_surface_release_buffer (onscreen_native->gbm.surface,
                                      onscreen_native->gbm.next_bo);
          onscreen_native->gbm.next_bo = NULL;
          onscreen_native->gbm.next_fb_id = 0;
        }

      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      break;
#endif
    }

  if (onscreen_native->pending_queue_swap_notify)
    {
      meta_onscreen_native_queue_swap_notify (onscreen);
      onscreen_native->pending_queue_swap_notify = FALSE;
    }

  g_object_unref (view);
}

#ifdef HAVE_EGL_DEVICE
static gboolean
flip_egl_stream (MetaOnscreenNative *onscreen_native,
                 GClosure           *flip_closure)
{
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (onscreen_native->gpu_kms);
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglRendererEGL *cogl_renderer_egl = cogl_display->renderer->winsys;
  EGLAttrib *acquire_attribs;
  GError *error = NULL;

  if (renderer_gpu_data->egl.no_egl_output_drm_flip_event)
    return FALSE;

  acquire_attribs = (EGLAttrib[]) {
    EGL_DRM_FLIP_EVENT_DATA_NV,
    (EGLAttrib) flip_closure,
    EGL_NONE
  };

  if (!meta_egl_stream_consumer_acquire_attrib (egl,
                                                cogl_renderer_egl->edpy,
                                                onscreen_native->egl.stream,
                                                acquire_attribs,
                                                &error))
    {
      if (error->domain != META_EGL_ERROR ||
          error->code != EGL_RESOURCE_BUSY_EXT)
        {
          g_warning ("Failed to flip EGL stream (%s), relying on clock from "
                     "now on", error->message);
          renderer_gpu_data->egl.no_egl_output_drm_flip_event = TRUE;
        }
      g_error_free (error);
      return FALSE;
    }

  g_closure_ref (flip_closure);

  return TRUE;
}
#endif /* HAVE_EGL_DEVICE */

static void
meta_onscreen_native_flip_crtc (MetaOnscreenNative *onscreen_native,
                                GClosure           *flip_closure,
                                MetaCrtc           *crtc,
                                int                 x,
                                int                 y,
                                gboolean           *fb_in_use)
{
  MetaGpuKms *gpu_kms = onscreen_native->gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;

  if (!meta_gpu_kms_is_crtc_active (gpu_kms, crtc))
    {
      *fb_in_use = FALSE;
      return;
    }

  renderer_gpu_data = meta_renderer_native_gpu_data_from_gpu (gpu_kms);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (meta_gpu_kms_flip_crtc (gpu_kms,
                                  crtc,
                                  x, y,
                                  onscreen_native->gbm.next_fb_id,
                                  flip_closure,
                                  fb_in_use))
        onscreen_native->pending_flips++;
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      if (flip_egl_stream (onscreen_native,
                           flip_closure))
        onscreen_native->pending_flips++;
      *fb_in_use = TRUE;
      break;
#endif
    }
}

typedef struct _SetCrtcFbData
{
  MetaGpuKms *gpu_kms;
  MetaLogicalMonitor *logical_monitor;
  uint32_t fb_id;
} SetCrtcFbData;

static void
set_crtc_fb (MetaLogicalMonitor *logical_monitor,
             MetaCrtc           *crtc,
             gpointer            user_data)
{
  SetCrtcFbData *data = user_data;
  MetaGpuKms *gpu_kms = data->gpu_kms;
  int x, y;

  x = crtc->rect.x - logical_monitor->rect.x;
  y = crtc->rect.y - logical_monitor->rect.y;

  meta_gpu_kms_apply_crtc_mode (gpu_kms, crtc, x, y, data->fb_id);
}

static void
meta_onscreen_native_set_crtc_modes (MetaOnscreenNative *onscreen_native)
{
  MetaGpuKms *gpu_kms = onscreen_native->gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (gpu_kms);
  MetaRendererView *view = onscreen_native->view;
  uint32_t fb_id = 0;
  MetaLogicalMonitor *logical_monitor;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      fb_id = onscreen_native->gbm.next_fb_id;
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      fb_id = onscreen_native->egl.dumb_fb.fb_id;
      break;
#endif
    }

  g_assert (fb_id != 0);

  logical_monitor = meta_renderer_view_get_logical_monitor (view);
  if (logical_monitor)
    {
      SetCrtcFbData data = {
        .gpu_kms = gpu_kms,
        .fb_id = fb_id
      };

      meta_logical_monitor_foreach_crtc (logical_monitor,
                                         set_crtc_fb,
                                         &data);
    }
  else
    {
      GList *l;

      for (l = meta_gpu_get_crtcs (META_GPU (gpu_kms)); l; l = l->next)
        {
          MetaCrtc *crtc = l->data;

          meta_gpu_kms_apply_crtc_mode (gpu_kms,
                                        crtc,
                                        crtc->rect.x, crtc->rect.y,
                                        fb_id);
        }
    }
}

typedef struct _FlipCrtcData
{
  MetaOnscreenNative *onscreen_native;
  GClosure *flip_closure;

  gboolean out_fb_in_use;
} FlipCrtcData;

static void
flip_crtc (MetaLogicalMonitor *logical_monitor,
           MetaCrtc           *crtc,
           gpointer            user_data)
{
  FlipCrtcData *data = user_data;
  int x, y;

  x = crtc->rect.x - logical_monitor->rect.x;
  y = crtc->rect.y - logical_monitor->rect.y;

  meta_onscreen_native_flip_crtc (data->onscreen_native,
                                  data->flip_closure,
                                  crtc, x, y,
                                  &data->out_fb_in_use);
}

static void
meta_onscreen_native_flip_crtcs (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaGpuKms *gpu_kms = onscreen_native->gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (gpu_kms);
  MetaRendererView *view = onscreen_native->view;
  GClosure *flip_closure;
  MetaLogicalMonitor *logical_monitor;
  gboolean fb_in_use = FALSE;

  /*
   * Create a closure that either will be invoked or destructed.
   * Invoking the closure represents a completed flip. If the closure
   * is destructed before being invoked, the framebuffer references will be
   * cleaned up accordingly.
   *
   * Each successful flip will each own one reference to the closure, thus keep
   * it alive until either invoked or destructed. If flipping failed, the
   * closure will be destructed before this function goes out of scope.
   */
  flip_closure = g_cclosure_new (G_CALLBACK (on_crtc_flipped),
                                 g_object_ref (view),
                                 (GClosureNotify) flip_closure_destroyed);
  g_closure_set_marshal (flip_closure, g_cclosure_marshal_VOID__OBJECT);

  /* Either flip the CRTC's of the monitor info, if we are drawing just part
   * of the stage, or all of the CRTC's if we are drawing the whole stage.
   */
  logical_monitor = meta_renderer_view_get_logical_monitor (view);
  if (logical_monitor)
    {
      FlipCrtcData data = {
        .onscreen_native = onscreen_native,
        .flip_closure = flip_closure,
      };

      meta_logical_monitor_foreach_crtc (logical_monitor,
                                         flip_crtc,
                                         &data);
      fb_in_use = data.out_fb_in_use;
    }
  else
    {
      GList *l;

      for (l = meta_gpu_get_crtcs (META_GPU (gpu_kms)); l; l = l->next)
        {
          MetaCrtc *crtc = l->data;

          meta_onscreen_native_flip_crtc (onscreen_native, flip_closure,
                                          crtc, crtc->rect.x, crtc->rect.y,
                                          &fb_in_use);
        }
    }

  /*
   * If the framebuffer is in use, but we don't have any pending flips it means
   * that flipping is not supported and we set the next framebuffer directly.
   * Since we won't receive a flip callback, lets just notify listeners
   * directly.
   */
  if (fb_in_use && onscreen_native->pending_flips == 0)
    {
      switch (renderer_gpu_data->mode)
        {
        case META_RENDERER_NATIVE_MODE_GBM:
          meta_onscreen_native_swap_drm_fb (onscreen);
          break;
#ifdef HAVE_EGL_DEVICE
        case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
          break;
#endif
        }
    }

  onscreen_native->pending_queue_swap_notify = TRUE;

  g_closure_unref (flip_closure);
}

static gboolean
gbm_get_next_fb_id (CoglOnscreen   *onscreen,
                    struct gbm_bo **out_next_bo,
                    uint32_t       *out_next_fb_id)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaGpuKms *gpu_kms = onscreen_native->gpu_kms;
  uint32_t handle, stride;
  struct gbm_bo *next_bo;
  uint32_t next_fb_id;
  int kms_fd;

  /* Now we need to set the CRTC to whatever is the front buffer */
  next_bo = gbm_surface_lock_front_buffer (onscreen_native->gbm.surface);

  stride = gbm_bo_get_stride (next_bo);
  handle = gbm_bo_get_handle (next_bo).u32;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  if (drmModeAddFB (kms_fd,
                    cogl_framebuffer_get_width (COGL_FRAMEBUFFER (onscreen)),
                    cogl_framebuffer_get_height (COGL_FRAMEBUFFER (onscreen)),
                    24, /* depth */
                    32, /* bpp */
                    stride,
                    handle,
                    &next_fb_id))
    {
      g_warning ("Failed to create new back buffer handle: %m");
      gbm_surface_release_buffer (onscreen_native->gbm.surface, next_bo);
      return FALSE;
    }

  *out_next_bo = next_bo;
  *out_next_fb_id = next_fb_id;
  return TRUE;
}

static void
meta_onscreen_native_swap_buffers_with_damage (CoglOnscreen *onscreen,
                                               const int    *rectangles,
                                               int           n_rectangles)
{
  CoglContext *cogl_context = COGL_FRAMEBUFFER (onscreen)->context;
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaGpuKms *gpu_kms = onscreen_native->gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (gpu_kms);
  CoglFrameInfo *frame_info;

  frame_info = g_queue_peek_tail (&onscreen->pending_frame_infos);
  frame_info->global_frame_counter = renderer_native->frame_counter;

  /*
   * Wait for the flip callback before continuing, as we might have started the
   * animation earlier due to the animation being driven by some other monitor.
   */
  while (onscreen_native->pending_flips)
    meta_gpu_kms_wait_for_flip (gpu_kms, NULL);

  parent_vtable->onscreen_swap_buffers_with_damage (onscreen,
                                                    rectangles,
                                                    n_rectangles);

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      g_warn_if_fail (onscreen_native->gbm.next_bo == NULL &&
                      onscreen_native->gbm.next_fb_id == 0);

      if (!gbm_get_next_fb_id (onscreen,
                               &onscreen_native->gbm.next_bo,
                               &onscreen_native->gbm.next_fb_id))
        return;

      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      break;
#endif
    }

  /* If this is the first framebuffer to be presented then we now setup the
   * crtc modes, else we flip from the previous buffer */
  if (onscreen_native->pending_set_crtc)
    {
      meta_onscreen_native_set_crtc_modes (onscreen_native);
      onscreen_native->pending_set_crtc = FALSE;
    }

  onscreen_native->pending_queue_swap_notify_frame_count = renderer_native->frame_counter;
  meta_onscreen_native_flip_crtcs (onscreen);
}

static gboolean
meta_renderer_native_init_egl_context (CoglContext *cogl_context,
                                       GError     **error)
{
#ifdef HAVE_EGL_DEVICE
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  MetaMonitorManagerKms *monitor_manager_kms =
    renderer_native->monitor_manager_kms;
  MetaGpuKms *primary_gpu =
    meta_monitor_manager_kms_get_primary_gpu (monitor_manager_kms);
  MetaRendererNativeGpuData *primary_renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (primary_gpu);
#endif

  COGL_FLAGS_SET (cogl_context->features,
                  COGL_FEATURE_ID_SWAP_BUFFERS_EVENT, TRUE);
  /* TODO: remove this deprecated feature */
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_SWAP_BUFFERS_EVENT,
                  TRUE);
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT,
                  TRUE);
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_MULTIPLE_ONSCREEN,
                  TRUE);

#ifdef HAVE_EGL_DEVICE
  if (primary_renderer_gpu_data->mode == META_RENDERER_NATIVE_MODE_EGL_DEVICE)
    COGL_FLAGS_SET (cogl_context->features,
                    COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL, TRUE);
#endif

  return TRUE;
}

static gboolean
meta_renderer_native_create_surface_gbm (MetaOnscreenNative  *onscreen_native,
                                         int                  width,
                                         int                  height,
                                         struct gbm_surface **gbm_surface,
                                         EGLSurface          *egl_surface,
                                         GError             **error)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRendererEGL *cogl_renderer_egl = cogl_display->renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (onscreen_native->gpu_kms);
  struct gbm_surface *new_gbm_surface;
  EGLNativeWindowType egl_native_window;
  EGLSurface new_egl_surface;

  new_gbm_surface = gbm_surface_create (renderer_gpu_data->gbm.device,
                                        width, height,
                                        GBM_FORMAT_XRGB8888,
                                        GBM_BO_USE_SCANOUT |
                                        GBM_BO_USE_RENDERING);

  if (!new_gbm_surface)
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                   "Failed to allocate surface");
      return FALSE;
    }

  egl_native_window = (EGLNativeWindowType) new_gbm_surface;
  new_egl_surface =
    meta_egl_create_window_surface (egl,
                                    cogl_renderer_egl->edpy,
                                    cogl_display_egl->egl_config,
                                    egl_native_window,
                                    NULL,
                                    error);
  if (new_egl_surface == EGL_NO_SURFACE)
    {
      gbm_surface_destroy (new_gbm_surface);
      return FALSE;
    }

  *gbm_surface = new_gbm_surface;
  *egl_surface = new_egl_surface;

  return TRUE;
}

#ifdef HAVE_EGL_DEVICE
static gboolean
meta_renderer_native_create_surface_egl_device (MetaRendererNative *renderer_native,
                                                MetaLogicalMonitor *logical_monitor,
                                                int                 width,
                                                int                 height,
                                                EGLStreamKHR       *out_egl_stream,
                                                EGLSurface         *out_egl_surface,
                                                GError            **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  EGLDisplay egl_display = cogl_renderer_egl->edpy;
  MetaMonitor *monitor;
  MetaOutput *output;
  EGLConfig egl_config;
  EGLStreamKHR egl_stream;
  EGLSurface egl_surface;
  EGLint num_layers;
  EGLOutputLayerEXT output_layer;
  EGLAttrib output_attribs[3];
  EGLint stream_attribs[] = {
    EGL_STREAM_FIFO_LENGTH_KHR, 1,
    EGL_CONSUMER_AUTO_ACQUIRE_EXT, EGL_FALSE,
    EGL_NONE
  };
  EGLint stream_producer_attribs[] = {
    EGL_WIDTH, width,
    EGL_HEIGHT, height,
    EGL_NONE
  };

  egl_stream = meta_egl_create_stream (egl, egl_display, stream_attribs, error);
  if (egl_stream == EGL_NO_STREAM_KHR)
    return FALSE;

  monitor = meta_logical_monitor_get_monitors (logical_monitor)->data;
  output = meta_monitor_get_main_output (monitor);

  /*
   * An "logical_monitor" may have multiple outputs/crtcs in case its tiled,
   * but as far as I can tell, EGL only allows you to pass one crtc_id, so
   * lets pass the first one.
   */
  output_attribs[0] = EGL_DRM_CRTC_EXT;
  output_attribs[1] = output->crtc->crtc_id;
  output_attribs[2] = EGL_NONE;

  if (!meta_egl_get_output_layers (egl, egl_display,
                                   output_attribs,
                                   &output_layer, 1, &num_layers,
                                   error))
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  if (num_layers < 1)
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unable to find output layers.");
      return FALSE;
    }

  if (!meta_egl_stream_consumer_output (egl, egl_display,
                                        egl_stream, output_layer,
                                        error))
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  egl_config = cogl_display_egl->egl_config;
  egl_surface = meta_egl_create_stream_producer_surface (egl,
                                                         egl_display,
                                                         egl_config,
                                                         egl_stream,
                                                         stream_producer_attribs,
                                                         error);
  if (egl_surface == EGL_NO_SURFACE)
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  *out_egl_stream = egl_stream;
  *out_egl_surface = egl_surface;

  return TRUE;
}

static gboolean
init_dumb_fb (MetaDumbBuffer *dumb_fb,
              MetaGpuKms     *gpu_kms,
              int             width,
              int             height,
              uint32_t        format,
              GError        **error)
{
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (gpu_kms);
  struct drm_mode_create_dumb create_arg;
  struct drm_mode_destroy_dumb destroy_arg;
  struct drm_mode_map_dumb map_arg;
  uint32_t fb_id = 0;
  void *map;
  int kms_fd;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  create_arg = (struct drm_mode_create_dumb) {
    .bpp = 32, /* RGBX8888 */
    .width = width,
    .height = height
  };
  if (drmIoctl (kms_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) != 0)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create dumb drm buffer: %s",
                   g_strerror (errno));
      goto err_ioctl;
    }

  if (!renderer_gpu_data->no_add_fb2)
    {
      uint32_t handles[4] = { create_arg.handle, };
      uint32_t pitches[4] = { create_arg.pitch, };
      uint32_t offsets[4] = { 0 };

      if (drmModeAddFB2 (kms_fd, width, height, format,
                         handles, pitches, offsets,
                         &fb_id, 0) != 0)
        {
          g_warning ("drmModeAddFB2 failed (%s), falling back to drmModeAddFB",
                     g_strerror (errno));
          renderer_gpu_data->no_add_fb2 = TRUE;
        }
    }

  if (renderer_gpu_data->no_add_fb2)
    {
      if (drmModeAddFB (kms_fd, width, height,
                        24 /* depth of RGBX8888 */,
                        32 /* bpp of RGBX8888 */,
                        create_arg.pitch,
                        create_arg.handle,
                        &fb_id) != 0)
        {
          g_set_error (error, G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       "drmModeAddFB failed: %s",
                       g_strerror (errno));
          goto err_add_fb;
        }
    }

  map_arg = (struct drm_mode_map_dumb) {
    .handle = create_arg.handle
  };
  if (drmIoctl (kms_fd, DRM_IOCTL_MODE_MAP_DUMB,
                &map_arg) != 0)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to map dumb drm buffer: %s",
                   g_strerror (errno));
      goto err_map_dumb;
    }

  map = mmap (NULL, create_arg.size, PROT_WRITE, MAP_SHARED,
              kms_fd, map_arg.offset);
  if (map == MAP_FAILED)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to mmap dumb drm buffer memory: %s",
                   g_strerror (errno));
      goto err_mmap;
    }

  dumb_fb->fb_id = fb_id;
  dumb_fb->handle = create_arg.handle;
  dumb_fb->map = map;
  dumb_fb->map_size = create_arg.size;

  return TRUE;

err_mmap:
err_map_dumb:
  drmModeRmFB (kms_fd, fb_id);

err_add_fb:
  destroy_arg = (struct drm_mode_destroy_dumb) {
    .handle = create_arg.handle
  };
  drmIoctl (kms_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);

err_ioctl:
  return FALSE;
}

static void
release_dumb_fb (MetaDumbBuffer *dumb_fb,
                 MetaGpuKms     *gpu_kms)
{
  struct drm_mode_destroy_dumb destroy_arg;
  int kms_fd;

  if (!dumb_fb->map)
    return;

  munmap (dumb_fb->map, dumb_fb->map_size);
  dumb_fb->map = NULL;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  drmModeRmFB (kms_fd, dumb_fb->fb_id);

  destroy_arg = (struct drm_mode_destroy_dumb) {
    .handle = dumb_fb->handle
  };
  drmIoctl (kms_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
}
#endif /* HAVE_EGL_DEVICE */

static gboolean
meta_renderer_native_init_onscreen (CoglOnscreen *onscreen,
                                    GError      **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = framebuffer->context;
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglOnscreenEGL *onscreen_egl;
  MetaOnscreenNative *onscreen_native;

  _COGL_RETURN_VAL_IF_FAIL (cogl_display_egl->egl_context, FALSE);

  onscreen->winsys = g_slice_new0 (CoglOnscreenEGL);
  onscreen_egl = onscreen->winsys;

  onscreen_native = g_slice_new0 (MetaOnscreenNative);
  onscreen_egl->platform = onscreen_native;

  /*
   * Don't actually initialize anything here, since we may not have the
   * information available yet, and there is no way to pass it at this stage.
   * To properly allocate a MetaOnscreenNative, the caller must call
   * meta_onscreen_native_allocate() after cogl_framebuffer_allocate().
   *
   * TODO: Turn CoglFramebuffer/CoglOnscreen into GObjects, so it's possible
   * to add backend specific properties.
   */

  return TRUE;
}

static gboolean
meta_onscreen_native_allocate (CoglOnscreen *onscreen,
                               GError      **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = framebuffer->context;
  CoglDisplay *cogl_display = cogl_context->display;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (onscreen_native->gpu_kms);
  struct gbm_surface *gbm_surface;
  EGLSurface egl_surface;
  int width;
  int height;
#ifdef HAVE_EGL_DEVICE
  MetaRendererView *view;
  MetaLogicalMonitor *logical_monitor;
  EGLStreamKHR egl_stream;
#endif

  onscreen_native->pending_set_crtc = TRUE;

  /* If a kms_fd is set then the display width and height
   * won't be available until meta_renderer_native_set_layout
   * is called. In that case, defer creating the surface
   * until then.
   */
  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);
  if (width == 0 || height == 0)
    return TRUE;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (!meta_renderer_native_create_surface_gbm (onscreen_native,
                                                    width, height,
                                                    &gbm_surface,
                                                    &egl_surface,
                                                    error))
        return FALSE;

      onscreen_native->gbm.surface = gbm_surface;
      onscreen_egl->egl_surface = egl_surface;
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      if (!init_dumb_fb (&onscreen_native->egl.dumb_fb,
                         onscreen_native->gpu_kms,
                         width, height,
                         GBM_FORMAT_XRGB8888,
                         error))
        return FALSE;

      view = onscreen_native->view;
      logical_monitor = meta_renderer_view_get_logical_monitor (view);
      if (!meta_renderer_native_create_surface_egl_device (renderer_native,
                                                           logical_monitor,
                                                           width, height,
                                                           &egl_stream,
                                                           &egl_surface,
                                                           error))
        return FALSE;

      onscreen_native->egl.stream = egl_stream;
      onscreen_egl->egl_surface = egl_surface;
      break;
#endif /* HAVE_EGL_DEVICE */
    }

  return TRUE;
}

static void
meta_renderer_native_release_onscreen (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = framebuffer->context;
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNative *renderer_native = cogl_renderer_egl->platform;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native;
  MetaRendererNativeGpuData *renderer_gpu_data;

  /* If we never successfully allocated then there's nothing to do */
  if (onscreen_egl == NULL)
    return;

  onscreen_native = onscreen_egl->platform;

  if (onscreen_egl->egl_surface != EGL_NO_SURFACE)
    {
      meta_egl_destroy_surface (egl,
                                cogl_renderer_egl->edpy,
                                onscreen_egl->egl_surface,
                                NULL);
      onscreen_egl->egl_surface = EGL_NO_SURFACE;
    }

  renderer_gpu_data =
    meta_renderer_native_gpu_data_from_gpu (onscreen_native->gpu_kms);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      /* flip state takes a reference on the onscreen so there should
       * never be outstanding flips when we reach here. */
      g_return_if_fail (onscreen_native->gbm.next_fb_id == 0);

      free_current_bo (onscreen);

      if (onscreen_native->gbm.surface)
        {
          gbm_surface_destroy (onscreen_native->gbm.surface);
          onscreen_native->gbm.surface = NULL;
        }
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      release_dumb_fb (&onscreen_native->egl.dumb_fb,
                       onscreen_native->gpu_kms);
      if (onscreen_native->egl.stream != EGL_NO_STREAM_KHR)
        {
          MetaBackend *backend = meta_get_backend ();
          MetaEgl *egl = meta_backend_get_egl (backend);

          meta_egl_destroy_stream (egl,
                                   cogl_renderer_egl->edpy,
                                   onscreen_native->egl.stream,
                                   NULL);
          onscreen_native->egl.stream = EGL_NO_STREAM_KHR;
        }
      break;
#endif /* HAVE_EGL_DEVICE */
    }

  g_slice_free (MetaOnscreenNative, onscreen_native);
  g_slice_free (CoglOnscreenEGL, onscreen->winsys);
  onscreen->winsys = NULL;
}

static const CoglWinsysEGLVtable
_cogl_winsys_egl_vtable = {
  .add_config_attributes = meta_renderer_native_add_egl_config_attributes,
  .display_setup = meta_renderer_native_setup_egl_display,
  .display_destroy = meta_renderer_native_destroy_egl_display,
  .context_created = meta_renderer_native_egl_context_created,
  .cleanup_context = meta_renderer_native_egl_cleanup_context,
  .context_init = meta_renderer_native_init_egl_context
};

gboolean
meta_renderer_native_supports_mirroring (MetaRendererNative *renderer_native)
{
  MetaMonitorManager *monitor_manager =
    META_MONITOR_MANAGER (renderer_native->monitor_manager_kms);
  GList *l;

  for (l = monitor_manager->gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);
      MetaRendererNativeGpuData *renderer_gpu_data;

      renderer_gpu_data = meta_renderer_native_gpu_data_from_gpu (gpu_kms);
      switch (renderer_gpu_data->mode)
        {
        case META_RENDERER_NATIVE_MODE_GBM:
          break;
#ifdef HAVE_EGL_DEVICE
        case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
          return FALSE;
#endif
        }
    }

  return TRUE;
}

void
meta_renderer_native_queue_modes_reset (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  GList *l;

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      CoglFramebuffer *framebuffer =
        clutter_stage_view_get_onscreen (stage_view);
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
      CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
      MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

      onscreen_native->pending_set_crtc = TRUE;
    }
}

static CoglOnscreen *
meta_renderer_native_create_onscreen (MetaGpuKms           *gpu_kms,
                                      CoglContext          *context,
                                      MetaMonitorTransform  transform,
                                      gint                  view_width,
                                      gint                  view_height,
                                      GError              **error)
{
  CoglOnscreen *onscreen;
  CoglOnscreenEGL *onscreen_egl;
  MetaOnscreenNative *onscreen_native;
  gint width, height;

  if (meta_monitor_transform_is_rotated (transform))
    {
      width = view_height;
      height = view_width;
    }
  else
    {
      width = view_width;
      height = view_height;
    }

  onscreen = cogl_onscreen_new (context, width, height);
  cogl_onscreen_set_swap_throttled (onscreen,
                                    _clutter_get_sync_to_vblank ());

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (onscreen), error))
    {
      cogl_object_unref (onscreen);
      return NULL;
    }

  onscreen_egl = onscreen->winsys;
  onscreen_native = onscreen_egl->platform;
  onscreen_native->gpu_kms = gpu_kms;

  return onscreen;
}

static CoglOffscreen *
meta_renderer_native_create_offscreen (MetaRendererNative    *renderer,
                                       CoglContext           *context,
                                       MetaMonitorTransform   transform,
                                       gint                   view_width,
                                       gint                   view_height,
                                       GError               **error)
{
  CoglOffscreen *fb;
  CoglTexture2D *tex;

  tex = cogl_texture_2d_new_with_size (context, view_width, view_height);
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (tex), FALSE);

  if (!cogl_texture_allocate (COGL_TEXTURE (tex), error))
    {
      cogl_object_unref (tex);
      return FALSE;
    }

  fb = cogl_offscreen_new_with_texture (COGL_TEXTURE (tex));
  cogl_object_unref (tex);
  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (fb), error))
    {
      cogl_object_unref (fb);
      return FALSE;
    }

  return fb;
}

static const CoglWinsysVtable *
get_native_cogl_winsys_vtable (CoglRenderer *cogl_renderer)
{
  static gboolean vtable_inited = FALSE;
  static CoglWinsysVtable vtable;

  if (!vtable_inited)
    {
      /* The this winsys is a subclass of the EGL winsys so we
         start by copying its vtable */

      parent_vtable = _cogl_winsys_egl_get_vtable ();
      vtable = *parent_vtable;

      vtable.id = COGL_WINSYS_ID_CUSTOM;
      vtable.name = "EGL_KMS";

      vtable.renderer_connect = meta_renderer_native_connect;
      vtable.renderer_disconnect = meta_renderer_native_disconnect;

      vtable.onscreen_init = meta_renderer_native_init_onscreen;
      vtable.onscreen_deinit = meta_renderer_native_release_onscreen;

      /* The KMS winsys doesn't support swap region */
      vtable.onscreen_swap_region = NULL;
      vtable.onscreen_swap_buffers_with_damage =
        meta_onscreen_native_swap_buffers_with_damage;

      vtable_inited = TRUE;
    }

  return &vtable;
}

static CoglRenderer *
meta_renderer_native_create_cogl_renderer (MetaRenderer *renderer)
{
  CoglRenderer *cogl_renderer;

  cogl_renderer = cogl_renderer_new ();
  cogl_renderer_set_custom_winsys (cogl_renderer,
                                   get_native_cogl_winsys_vtable,
                                   NULL);

  return cogl_renderer;
}

static void
meta_onscreen_native_set_view (CoglOnscreen     *onscreen,
                               MetaRendererView *view)
{
  CoglOnscreenEGL *onscreen_egl;
  MetaOnscreenNative *onscreen_native;

  onscreen_egl = onscreen->winsys;
  onscreen_native = onscreen_egl->platform;
  onscreen_native->view = view;
}

static MetaMonitorTransform
calculate_view_transform (MetaMonitorManager *monitor_manager,
                          MetaLogicalMonitor *logical_monitor)
{
  MetaMonitor *main_monitor;
  MetaOutput *main_output;
  main_monitor = meta_logical_monitor_get_monitors (logical_monitor)->data;
  main_output = meta_monitor_get_main_output (main_monitor);

  /*
   * Pick any monitor and output and check; all CRTCs of a logical monitor will
   * always have the same transform assigned to them.
   */

  if (meta_monitor_manager_is_transform_handled (monitor_manager,
                                                 main_output->crtc,
                                                 logical_monitor->transform))
    return META_MONITOR_TRANSFORM_NORMAL;
  else
    return logical_monitor->transform;
}

static MetaRendererView *
meta_renderer_native_create_view (MetaRenderer       *renderer,
                                  MetaLogicalMonitor *logical_monitor)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  MetaMonitorManagerKms *monitor_manager_kms =
    renderer_native->monitor_manager_kms;
  MetaMonitorManager *monitor_manager =
    META_MONITOR_MANAGER (monitor_manager_kms);
  MetaGpuKms *primary_gpu =
    meta_monitor_manager_kms_get_primary_gpu (monitor_manager_kms);
  MetaBackend *backend = meta_monitor_manager_get_backend (monitor_manager);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglOnscreenEGL *onscreen_egl;
  MetaMonitorTransform view_transform;
  CoglOnscreen *onscreen = NULL;
  CoglOffscreen *offscreen = NULL;
  float scale;
  int width, height;
  MetaRendererView *view;
  GError *error = NULL;

  view_transform = calculate_view_transform (monitor_manager, logical_monitor);

  if (meta_is_stage_views_scaled ())
    scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    scale = 1.0;

  width = logical_monitor->rect.width * scale;
  height = logical_monitor->rect.height * scale;

  onscreen = meta_renderer_native_create_onscreen (primary_gpu,
                                                   cogl_context,
                                                   view_transform,
                                                   width,
                                                   height,
                                                   &error);
  if (!onscreen)
    g_error ("Failed to allocate onscreen framebuffer: %s", error->message);

  if (view_transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      offscreen = meta_renderer_native_create_offscreen (renderer_native,
                                                         cogl_context,
                                                         view_transform,
                                                         width,
                                                         height,
                                                         &error);
      if (!offscreen)
        g_error ("Failed to allocate back buffer texture: %s", error->message);
    }

  view = g_object_new (META_TYPE_RENDERER_VIEW,
                       "layout", &logical_monitor->rect,
                       "scale", scale,
                       "framebuffer", onscreen,
                       "offscreen", offscreen,
                       "logical-monitor", logical_monitor,
                       "transform", view_transform,
                       NULL);
  g_clear_pointer (&offscreen, cogl_object_unref);

  meta_onscreen_native_set_view (onscreen, view);

  if (!meta_onscreen_native_allocate (onscreen, &error))
    {
      g_warning ("Could not create onscreen: %s", error->message);
      cogl_object_unref (onscreen);
      g_object_unref (view);
      g_error_free (error);
      return NULL;
    }

  cogl_object_unref (onscreen);

  /* Ensure we don't point to stale surfaces when creating the offscreen */
  onscreen_egl = onscreen->winsys;
  _cogl_winsys_egl_make_current (cogl_display,
                                 onscreen_egl->egl_surface,
                                 onscreen_egl->egl_surface,
                                 cogl_display_egl->egl_context);

  return view;
}

void
meta_renderer_native_finish_frame (MetaRendererNative *renderer_native)
{
  renderer_native->frame_counter++;
}

int64_t
meta_renderer_native_get_frame_counter (MetaRendererNative *renderer_native)
{
  return renderer_native->frame_counter;
}

void
meta_renderer_native_pause (MetaRendererNative *renderer_native)
{
  GList *views;
  GList *l;

  views = meta_renderer_get_views (META_RENDERER (renderer_native));
  for (l = views; l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;

      g_object_set (G_OBJECT (stage_view),
                    "framebuffer", NULL,
                    NULL);
      g_object_set (G_OBJECT (stage_view),
                    "offscreen", NULL,
                    NULL);
    }
}

static void
meta_renderer_native_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (object);

  switch (prop_id)
    {
    case PROP_MONITOR_MANAGER:
      g_value_set_object (value, renderer_native->monitor_manager_kms);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_native_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (object);

  switch (prop_id)
    {
    case PROP_MONITOR_MANAGER:
      renderer_native->monitor_manager_kms = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
init_gbm (MetaRendererNative *renderer_native,
          MetaGpuKms         *gpu_kms,
          GError            **error)
{
  MetaMonitorManagerKms *monitor_manager_kms =
    renderer_native->monitor_manager_kms;
  MetaMonitorManager *monitor_manager =
    META_MONITOR_MANAGER (monitor_manager_kms);
  MetaBackend *backend = meta_monitor_manager_get_backend (monitor_manager);
  MetaEgl *egl = meta_backend_get_egl (backend);
  struct gbm_device *gbm_device;
  EGLDisplay egl_display;
  int kms_fd;
  MetaRendererNativeGpuData *renderer_gpu_data;

  if (!meta_egl_has_extensions (egl, EGL_NO_DISPLAY, NULL,
                                "EGL_MESA_platform_gbm",
                                NULL))
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing extension for GBM renderer: EGL_KHR_platform_gbm");
      return FALSE;
    }

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  gbm_device = gbm_create_device (kms_fd);
  if (!gbm_device)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create gbm device: %s", g_strerror (errno));
      return FALSE;
    }

  egl_display = meta_egl_get_platform_display (egl,
                                               EGL_PLATFORM_GBM_KHR,
                                               gbm_device, NULL, error);
  if (egl_display == EGL_NO_DISPLAY)
    {
      gbm_device_destroy (gbm_device);
      return FALSE;
    }

  renderer_gpu_data = meta_renderer_native_gpu_data_from_gpu (gpu_kms);
  if (!renderer_gpu_data)
    {
      renderer_gpu_data = meta_create_renderer_native_gpu_data (gpu_kms);
      renderer_gpu_data->gbm.device = gbm_device;
      renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_GBM;
    }

  renderer_native->egl_display = egl_display;

  return TRUE;
}

#ifdef HAVE_EGL_DEVICE
static const char *
get_drm_device_file (MetaEgl     *egl,
                     EGLDeviceEXT device,
                     GError     **error)
{
  if (!meta_egl_egl_device_has_extensions (egl, device,
                                           NULL,
                                           "EGL_EXT_device_drm",
                                           NULL))
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing required EGLDevice extension EGL_EXT_device_drm");
      return NULL;
    }

  return meta_egl_query_device_string (egl, device,
                                       EGL_DRM_DEVICE_FILE_EXT,
                                       error);
}

static EGLDeviceEXT
find_egl_device (MetaRendererNative *renderer_native,
                 MetaGpuKms         *gpu_kms,
                 GError            **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  char **missing_extensions;
  EGLint num_devices;
  EGLDeviceEXT *devices;
  const char *kms_file_path;
  EGLDeviceEXT device;
  EGLint i;

  if (!meta_egl_has_extensions (egl,
                                EGL_NO_DISPLAY,
                                &missing_extensions,
                                "EGL_EXT_device_base",
                                NULL))
    {
      char *missing_extensions_str;

      missing_extensions_str = g_strjoinv (", ", missing_extensions);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing EGL extensions required for EGLDevice renderer: %s",
                   missing_extensions_str);
      g_free (missing_extensions_str);
      g_free (missing_extensions);
      return EGL_NO_DEVICE_EXT;
    }

  if (!meta_egl_query_devices (egl, 0, NULL, &num_devices, error))
    return EGL_NO_DEVICE_EXT;

  devices = g_new0 (EGLDeviceEXT, num_devices);
  if (!meta_egl_query_devices (egl, num_devices, devices, &num_devices,
                               error))
    {
      g_free (devices);
      return EGL_NO_DEVICE_EXT;
    }

  kms_file_path = meta_gpu_kms_get_file_path (gpu_kms);

  device = EGL_NO_DEVICE_EXT;
  for (i = 0; i < num_devices; i++)
    {
      const char *egl_device_drm_path;

      g_clear_error (error);

      egl_device_drm_path = get_drm_device_file (egl, devices[i], error);
      if (!egl_device_drm_path)
        continue;

      if (g_str_equal (egl_device_drm_path, kms_file_path))
        {
          device = devices[i];
          break;
        }
    }
  g_free (devices);

  if (device == EGL_NO_DEVICE_EXT)
    {
      if (!*error)
        g_set_error (error, G_IO_ERROR,
                     G_IO_ERROR_FAILED,
                     "Failed to find matching EGLDeviceEXT");
      return EGL_NO_DEVICE_EXT;
    }

  return device;
}

static EGLDisplay
get_egl_device_display (MetaRendererNative *renderer_native,
                        MetaGpuKms         *gpu_kms,
                        EGLDeviceEXT        egl_device,
                        GError            **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  int kms_fd = meta_gpu_kms_get_fd (gpu_kms);
  EGLint platform_attribs[] = {
    EGL_DRM_MASTER_FD_EXT, kms_fd,
    EGL_NONE
  };

  return meta_egl_get_platform_display (egl, EGL_PLATFORM_DEVICE_EXT,
                                        (void *) egl_device,
                                        platform_attribs,
                                        error);
}

static gboolean
init_egl_device (MetaRendererNative *renderer_native,
                 MetaGpuKms         *gpu_kms,
                 GError            **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  char **missing_extensions;
  EGLDeviceEXT egl_device;
  EGLDisplay egl_display;
  MetaRendererNativeGpuData *renderer_gpu_data;

  if (!meta_is_stage_views_enabled())
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "EGLDevice requires stage views enabled");
      return FALSE;
    }

  egl_device = find_egl_device (renderer_native, gpu_kms, error);
  if (egl_device == EGL_NO_DEVICE_EXT)
    return FALSE;

  egl_display = get_egl_device_display (renderer_native, gpu_kms,
                                        egl_device, error);
  if (egl_display == EGL_NO_DISPLAY)
    return FALSE;

  if (!meta_egl_initialize (egl, egl_display, error))
    return FALSE;

  if (!meta_egl_has_extensions (egl,
                                egl_display,
                                &missing_extensions,
                                "EGL_NV_output_drm_flip_event",
                                "EGL_EXT_output_base",
                                "EGL_EXT_output_drm",
                                "EGL_KHR_stream",
                                "EGL_KHR_stream_producer_eglsurface",
                                "EGL_EXT_stream_consumer_egloutput",
                                "EGL_EXT_stream_acquire_mode",
                                NULL))
    {
      char *missing_extensions_str;

      missing_extensions_str = g_strjoinv (", ", missing_extensions);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing EGL extensions required for EGLDevice renderer: %s",
                   missing_extensions_str);
      g_free (missing_extensions_str);
      g_free (missing_extensions);
      return FALSE;
    }

  renderer_gpu_data = meta_renderer_native_gpu_data_from_gpu (gpu_kms);
  if (!renderer_gpu_data)
    {
      renderer_gpu_data = meta_create_renderer_native_gpu_data (gpu_kms);
      renderer_gpu_data->egl.device = egl_device;
      renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_EGL_DEVICE;
    }

  renderer_native->egl_display = egl_display;

  return TRUE;
}
#endif /* HAVE_EGL_DEVICE */

static gboolean
meta_renderer_native_init_gpu (MetaRendererNative *renderer_native,
                               MetaGpuKms         *gpu_kms,
                               GError            **error)
{
  GError *gbm_error = NULL;
#ifdef HAVE_EGL_DEVICE
  GError *egl_device_error = NULL;
#endif

#ifdef HAVE_EGL_DEVICE
  /* Try to initialize the EGLDevice backend first. Whenever we use a
   * non-NVIDIA GPU, the EGLDevice enumeration function won't find a match, and
   * we'll fall back to GBM (which will always succeed as it has a software
   * rendering fallback)
   */
  if (init_egl_device (renderer_native, gpu_kms, &egl_device_error))
    return TRUE;
#endif

  if (init_gbm (renderer_native, gpu_kms, &gbm_error))
    {
#ifdef HAVE_EGL_DEVICE
      g_error_free (egl_device_error);
#endif
      return TRUE;
    }

  g_set_error (error, G_IO_ERROR,
               G_IO_ERROR_FAILED,
               "Failed to initialize renderer: "
               "%s"
#ifdef HAVE_EGL_DEVICE
               ", %s"
#endif
               , gbm_error->message
#ifdef HAVE_EGL_DEVICE
               , egl_device_error->message
#endif
  );

  g_error_free (gbm_error);
#ifdef HAVE_EGL_DEVICE
  g_error_free (egl_device_error);
#endif

  return FALSE;
}

static gboolean
meta_renderer_native_initable_init (GInitable    *initable,
                                    GCancellable *cancellable,
                                    GError      **error)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (initable);
  MetaMonitorManagerKms *monitor_manager_kms =
    renderer_native->monitor_manager_kms;
  MetaMonitorManager *monitor_manager =
    META_MONITOR_MANAGER (monitor_manager_kms);
  GList *gpus;
  MetaGpuKms *gpu_kms;

  gpus = meta_monitor_manager_get_gpus (monitor_manager);
  if (g_list_length (gpus) != 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Renderer only supports single GPU configurations");
      return FALSE;
    }

  gpu_kms = META_GPU_KMS (gpus->data);

  if (!meta_renderer_native_init_gpu (renderer_native, gpu_kms, error))
    return FALSE;

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_renderer_native_initable_init;
}

static void
meta_renderer_native_init (MetaRendererNative *renderer_native)
{
}

static void
meta_renderer_native_class_init (MetaRendererNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaRendererClass *renderer_class = META_RENDERER_CLASS (klass);

  object_class->get_property = meta_renderer_native_get_property;
  object_class->set_property = meta_renderer_native_set_property;

  renderer_class->create_cogl_renderer = meta_renderer_native_create_cogl_renderer;
  renderer_class->create_view = meta_renderer_native_create_view;

  obj_props[PROP_MONITOR_MANAGER] =
    g_param_spec_object ("monitor-manager",
                         "monitor-manager",
                         "MetaMonitorManagerKms",
                         META_TYPE_MONITOR_MANAGER_KMS,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  quark_onscreen_native_gpu_data =
    g_quark_from_static_string ("-meta-onscreen-native-gpu-data");
}

MetaRendererNative *
meta_renderer_native_new (MetaMonitorManagerKms *monitor_manager_kms,
                          GError               **error)
{
  MetaRendererNative *renderer_native;

  renderer_native = g_object_new (META_TYPE_RENDERER_NATIVE,
                                  "monitor-manager", monitor_manager_kms,
                                  NULL);
  if (!g_initable_init (G_INITABLE (renderer_native), NULL, error))
    {
      g_object_unref (renderer_native);
      return NULL;
    }

  return renderer_native;
}
