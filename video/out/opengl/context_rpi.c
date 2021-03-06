/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stddef.h>
#include <assert.h>

#include "common/common.h"
#include "osdep/atomic.h"
#include "video/out/win_state.h"
#include "context.h"

#include "context_rpi.h"

static void *get_proc_address(const GLubyte *name)
{
    void *p = eglGetProcAddress(name);
    // EGL 1.4 (supported by the RPI firmware) does not necessarily return
    // function pointers for core functions.
    if (!p) {
        void *h = dlopen("/opt/vc/lib/libGLESv2.so", RTLD_LAZY);
        if (h) {
            p = dlsym(h, name);
            dlclose(h);
        }
    }
    return p;
}

static EGLConfig select_fb_config_egl(struct mp_egl_rpi *p)
{
    EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint config_count;
    EGLConfig config;

    eglChooseConfig(p->egl_display, attributes, &config, 1, &config_count);

    if (!config_count) {
        MP_FATAL(p, "Could find EGL configuration!\n");
        return NULL;
    }

    return config;
}

int mp_egl_rpi_init(struct mp_egl_rpi *p, DISPMANX_ELEMENT_HANDLE_T window,
                    int w, int h)
{
    p->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(p->egl_display, NULL, NULL)) {
        MP_FATAL(p, "EGL failed to initialize.\n");
        goto fail;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLConfig config = select_fb_config_egl(p);
    if (!config)
        goto fail;

    p->egl_window = (EGL_DISPMANX_WINDOW_T){
        .element = window,
        .width = w,
        .height = h,
    };
    p->egl_surface = eglCreateWindowSurface(p->egl_display, config,
                                            &p->egl_window, NULL);

    if (p->egl_surface == EGL_NO_SURFACE) {
        MP_FATAL(p, "Could not create EGL surface!\n");
        goto fail;
    }

    EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    p->egl_context = eglCreateContext(p->egl_display, config,
                                      EGL_NO_CONTEXT, context_attributes);

    if (p->egl_context == EGL_NO_CONTEXT) {
        MP_FATAL(p, "Could not create EGL context!\n");
        goto fail;
    }

    eglMakeCurrent(p->egl_display, p->egl_surface, p->egl_surface,
                   p->egl_context);

    p->gl = talloc_zero(NULL, struct GL);

    const char *exts = eglQueryString(p->egl_display, EGL_EXTENSIONS);
    mpgl_load_functions(p->gl, get_proc_address, exts, p->log);

    if (!p->gl->version && !p->gl->es)
        goto fail;

    return 0;

fail:
    mp_egl_rpi_destroy(p);
    return -1;
}

void mp_egl_rpi_destroy(struct mp_egl_rpi *p)
{
    if (p->egl_display) {
        eglMakeCurrent(p->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
    }
    if (p->egl_surface)
        eglDestroySurface(p->egl_display, p->egl_surface);
    if (p->egl_context)
        eglDestroyContext(p->egl_display, p->egl_context);
    p->egl_context = EGL_NO_CONTEXT;
    eglReleaseThread();
    p->egl_display = EGL_NO_DISPLAY;
    talloc_free(p->gl);
    p->gl = NULL;
}

static int mp_egl_rpi_init_base(struct mp_egl_rpi *p)
{
    p->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(p->egl_display, NULL, NULL)) {
        MP_FATAL(p, "EGL failed to initialize.\n");
        goto fail;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    p->egl_config = select_fb_config_egl(p);
    if (!p->egl_config)
        goto fail;

    EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    p->egl_context = eglCreateContext(p->egl_display, p->egl_config,
                                      EGL_NO_CONTEXT, context_attributes);

    if (p->egl_context == EGL_NO_CONTEXT) {
        MP_FATAL(p, "Could not create EGL context!\n");
        goto fail;
    }

    return 0;

fail:
    mp_egl_rpi_destroy(p);
    return -1;
}

static void mp_egl_rpi_destroy_base(struct mp_egl_rpi *p)
{
    if (p->egl_context)
        eglDestroyContext(p->egl_display, p->egl_context);
    p->egl_context = EGL_NO_CONTEXT;
    eglReleaseThread();
    p->egl_display = EGL_NO_DISPLAY;
}

static int mp_egl_rpi_init_window(struct mp_egl_rpi *p,
                                  DISPMANX_ELEMENT_HANDLE_T window,
                                  int w, int h)
{
    p->egl_window = (EGL_DISPMANX_WINDOW_T){
        .element = window,
        .width = w,
        .height = h,
    };
    p->egl_surface = eglCreateWindowSurface(p->egl_display, p->egl_config,
                                            &p->egl_window, NULL);

    if (p->egl_surface == EGL_NO_SURFACE) {
        MP_FATAL(p, "Could not create EGL surface!\n");
        return -1;
    }

    if (!eglMakeCurrent(p->egl_display, p->egl_surface, p->egl_surface,
                        p->egl_context))
    {
        MP_FATAL(p, "Failed to set context!\n");
        return -1;
    }

    return 0;
}

static void mp_egl_rpi_destroy_window(struct mp_egl_rpi *p)
{
    if (p->egl_surface) {
        eglMakeCurrent(p->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        eglDestroySurface(p->egl_display, p->egl_surface);
        p->egl_surface = EGL_NO_SURFACE;
    }
}

struct priv {
    DISPMANX_DISPLAY_HANDLE_T display;
    DISPMANX_ELEMENT_HANDLE_T window;
    DISPMANX_UPDATE_HANDLE_T update;
    struct mp_egl_rpi egl;
    int x, y, w, h;
    double display_fps;
    atomic_bool reload_display;
    int win_params[4];
};

static void tv_callback(void *callback_data, uint32_t reason, uint32_t param1,
                        uint32_t param2)
{
    struct MPGLContext *ctx = callback_data;
    struct priv *p = ctx->priv;
    atomic_store(&p->reload_display, true);
    vo_wakeup(ctx->vo);
}

static void destroy_dispmanx(struct MPGLContext *ctx)
{
    struct priv *p = ctx->priv;

    mp_egl_rpi_destroy_window(&p->egl);
    if (p->window)
        vc_dispmanx_element_remove(p->update, p->window);
    p->window = 0;
    if (p->display)
        vc_dispmanx_display_close(p->display);
    p->display = 0;
    if (p->update)
        vc_dispmanx_update_submit_sync(p->update);
    p->update = 0;
}

static void rpi_uninit(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;

    vc_tv_unregister_callback_full(tv_callback, ctx);

    mp_egl_rpi_destroy_base(&p->egl);

    destroy_dispmanx(ctx);
}

static int recreate_dispmanx(struct MPGLContext *ctx)
{
    struct priv *p = ctx->priv;

    MP_VERBOSE(ctx->vo, "Recreating DISPMANX state...\n");

    destroy_dispmanx(ctx);

    p->display = vc_dispmanx_display_open(0);
    p->update = vc_dispmanx_update_start(0);
    if (!p->display || !p->update) {
        MP_FATAL(ctx->vo, "Could not get DISPMANX objects.\n");
        goto fail;
    }

    uint32_t dispw, disph;
    if (graphics_get_display_size(0, &dispw, &disph) < 0) {
        MP_FATAL(ctx->vo, "Could not get display size.\n");
        goto fail;
    }
    p->w = dispw;
    p->h = disph;

    if (ctx->vo->opts->fullscreen) {
        p->x = p->y = 0;
    } else {
        struct vo_win_geometry geo;
        struct mp_rect screenrc = {0, 0, p->w, p->h};

        vo_calc_window_geometry(ctx->vo, &screenrc, &geo);

        mp_rect_intersection(&geo.win, &screenrc);

        p->x = geo.win.x0;
        p->y = geo.win.y0;
        p->w = geo.win.x1 - geo.win.x0;
        p->h = geo.win.y1 - geo.win.y0;
    }

    // dispmanx is like a neanderthal version of Wayland - you can add an
    // overlay any place on the screen.
    VC_RECT_T dst = {.x = p->x, .y = p->y, .width = p->w, .height = p->h};
    VC_RECT_T src = {.width = p->w << 16, .height = p->h << 16};
    VC_DISPMANX_ALPHA_T alpha = {
        .flags = DISPMANX_FLAGS_ALPHA_FROM_SOURCE,
        .opacity = 0xFF,
    };
    p->window = vc_dispmanx_element_add(p->update, p->display, 1, &dst, 0,
                                        &src, DISPMANX_PROTECTION_NONE, &alpha, 0, 0);
    if (!p->window) {
        MP_FATAL(ctx->vo, "Could not add DISPMANX element.\n");
        goto fail;
    }

    vc_dispmanx_update_submit_sync(p->update);
    p->update = vc_dispmanx_update_start(0);

    if (mp_egl_rpi_init_window(&p->egl, p->window, p->w, p->h) < 0)
        goto fail;

    p->display_fps = 0;
    TV_GET_STATE_RESP_T tvstate;
    TV_DISPLAY_STATE_T tvstate_disp;
    if (!vc_tv_get_state(&tvstate) && !vc_tv_get_display_state(&tvstate_disp)) {
        if (tvstate_disp.state & (VC_HDMI_HDMI | VC_HDMI_DVI)) {
            p->display_fps = tvstate_disp.display.hdmi.frame_rate;

            HDMI_PROPERTY_PARAM_T param = {
                .property = HDMI_PROPERTY_PIXEL_CLOCK_TYPE,
            };
            if (!vc_tv_hdmi_get_property(&param) &&
                param.param1 == HDMI_PIXEL_CLOCK_TYPE_NTSC)
                p->display_fps = p->display_fps / 1.001;
        } else {
            p->display_fps = tvstate_disp.display.sdtv.frame_rate;
        }
    }

    p->win_params[2] = p->x;
    p->win_params[3] = p->y;

    ctx->vo->dwidth = p->w;
    ctx->vo->dheight = p->h;

    ctx->vo->want_redraw = true;

    vo_event(ctx->vo, VO_EVENT_WIN_STATE);
    return 0;

fail:
    destroy_dispmanx(ctx);
    return -1;
}

static int rpi_init(struct MPGLContext *ctx, int flags)
{
    struct priv *p = ctx->priv;

    bcm_host_init();

    vc_tv_register_callback(tv_callback, ctx);

    p->egl.log = ctx->vo->log;
    if (mp_egl_rpi_init_base(&p->egl) < 0) {
        rpi_uninit(ctx);
        return -1;
    }

    if (recreate_dispmanx(ctx) < 0) {
        rpi_uninit(ctx);
        return -1;
    }

    ctx->gl = talloc_zero(ctx, GL);

    const char *exts = eglQueryString(p->egl.egl_display, EGL_EXTENSIONS);
    mpgl_load_functions(ctx->gl, get_proc_address, exts, p->egl.log);

    ctx->native_display_type = "MPV_RPI_WINDOW";
    ctx->native_display = p->win_params;

    return 0;
}

static int rpi_reconfig(struct MPGLContext *ctx)
{
    return recreate_dispmanx(ctx);
}

static void rpi_swap_buffers(MPGLContext *ctx)
{
    struct priv *p = ctx->priv;
    eglSwapBuffers(p->egl.egl_display, p->egl.egl_surface);
}

static struct mp_image *take_screenshot(struct MPGLContext *ctx)
{
    struct priv *p = ctx->priv;

    if (!p->display)
        return NULL;

    struct mp_image *img = mp_image_alloc(IMGFMT_BGR0, p->w, p->h);
    if (!img)
        return NULL;

    DISPMANX_RESOURCE_HANDLE_T resource =
        vc_dispmanx_resource_create(VC_IMAGE_ARGB8888,
                                    img->w | ((img->w * 4) << 16), img->h,
                                    &(int32_t){0});
    if (!resource)
        goto fail;

    if (vc_dispmanx_snapshot(p->display, resource, 0))
        goto fail;

    VC_RECT_T rc = {.width = img->w, .height = img->h};
    if (vc_dispmanx_resource_read_data(resource, &rc, img->planes[0], img->stride[0]))
        goto fail;

    vc_dispmanx_resource_delete(resource);
    return img;

fail:
    vc_dispmanx_resource_delete(resource);
    talloc_free(img);
    return NULL;
}


static int rpi_control(MPGLContext *ctx, int *events, int request, void *arg)
{
    struct priv *p = ctx->priv;

    switch (request) {
    case VOCTRL_SCREENSHOT_WIN:
        *(struct mp_image **)arg = take_screenshot(ctx);
        return true;
    case VOCTRL_FULLSCREEN:
        recreate_dispmanx(ctx);
        return VO_TRUE;
    case VOCTRL_CHECK_EVENTS:
        if (atomic_fetch_and(&p->reload_display, 0)) {
            MP_WARN(ctx->vo, "Recovering from display mode switch...\n");
            recreate_dispmanx(ctx);
        }
        return VO_TRUE;
    case VOCTRL_GET_DISPLAY_FPS:
        *(double *)arg = p->display_fps;
        return VO_TRUE;
    }

    return VO_NOTIMPL;
}

const struct mpgl_driver mpgl_driver_rpi = {
    .name           = "rpi",
    .priv_size      = sizeof(struct priv),
    .init           = rpi_init,
    .reconfig       = rpi_reconfig,
    .swap_buffers   = rpi_swap_buffers,
    .control        = rpi_control,
    .uninit         = rpi_uninit,
};