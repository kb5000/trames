/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Original glamor renders pixmaps; original Mesa/Zink renders and presents.
 * This adapter supplies an EGL Wayland transport without DRM/GBM/DMA-BUF.
 * X11 protocol, rasterization algorithms and shader compilers stay upstream.
 */
#include <xwayland-config.h>
#include <stdlib.h>
#include <string.h>
#define EGL_NO_X11
#define MESA_EGL_NO_X11_HEADERS
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <wayland-egl.h>
#include <glamor.h>
#include "xwayland-screen.h"
#include "xwayland-window.h"
#include "native-wsi.h"

struct xwl_native {
    EGLDisplay display;
    EGLConfig config;
    EGLContext draw, present;
    GLuint program, vertices;
    GLint opaque;
};
struct xwl_native_window {
    struct wl_egl_window *window;
    EGLSurface surface;
    int width, height;
};

Bool xwl_native_requested(void)
{
    const char *value = getenv("TRAMES_XWAYLAND_NATIVE_WSI");
    return value && !strcmp(value, "1");
}
static Bool current(struct xwl_native *n, EGLSurface surface, EGLContext context)
{
    return eglMakeCurrent(n->display, surface, surface, context);
}
static GLuint shader(GLenum type, const char *source)
{
    GLuint result = glCreateShader(type);
    GLint ok;
    glShaderSource(result, 1, &source, NULL);
    glCompileShader(result);
    glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char message[2048];
        glGetShaderInfoLog(result, sizeof(message), NULL, message);
        ErrorF("native WSI shader: %s\n", message);
        glDeleteShader(result);
        return 0;
    }
    return result;
}
Bool xwl_native_init(struct xwl_screen *screen)
{
    const EGLint config_attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    struct xwl_native *n = calloc(1, sizeof(*n));
    EGLint major, minor, count;
    GLuint vertex = 0, fragment = 0;
    GLint linked;
    const char *renderer;
    const GLfloat vertices[] = {-1,-1, 1,-1, -1,1, 1,1};
    if (!n)
        return FALSE;
    screen->native_glamor = n;
    n->display = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT, screen->display, NULL);
    if (n->display == EGL_NO_DISPLAY || !eglInitialize(n->display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(n->display, config_attributes, &n->config, 1, &count) || count != 1)
        goto fail;
    n->draw = eglCreateContext(n->display, n->config, EGL_NO_CONTEXT, context_attributes);
    if (n->draw == EGL_NO_CONTEXT || !current(n, EGL_NO_SURFACE, n->draw))
        goto fail;
    renderer = (const char *)glGetString(GL_RENDERER);
    if (!renderer || !strstr(renderer, "zink")) {
        ErrorF("native WSI requires an actual Zink renderer\n");
        goto fail;
    }
    if (!epoxy_has_egl_extension(n->display, "EGL_KHR_fence_sync") ||
        !epoxy_has_egl_extension(n->display, "EGL_KHR_wait_sync")) {
        ErrorF("native WSI requires cross-context EGL GPU fence/wait sync\n");
        goto fail;
    }
    screen->egl_display = n->display;
    screen->egl_context = n->draw;
    n->present = eglCreateContext(n->display, n->config, n->draw, context_attributes);
    if (n->present == EGL_NO_CONTEXT || !current(n, EGL_NO_SURFACE, n->present))
        goto fail;
    vertex = shader(GL_VERTEX_SHADER,
        "attribute vec2 position; varying vec2 uv; void main(){"
        /* Original glamor maps X's top row to texture v=0 (GL's bottom).
         * A Wayland default framebuffer is presented with a top-left origin. */
        "uv=vec2(position.x*0.5+0.5,0.5-position.y*0.5);"
        "gl_Position=vec4(position,0.0,1.0);}");
    fragment = shader(GL_FRAGMENT_SHADER,
        "precision mediump float; varying vec2 uv; uniform sampler2D picture;"
        "uniform float opaque; void main(){vec4 color=texture2D(picture,uv);"
        "gl_FragColor=vec4(color.rgb,mix(color.a,1.0,opaque));}");
    if (!vertex || !fragment)
        goto fail;
    n->program = glCreateProgram();
    glAttachShader(n->program, vertex);
    glAttachShader(n->program, fragment);
    glBindAttribLocation(n->program, 0, "position");
    glLinkProgram(n->program);
    glGetProgramiv(n->program, GL_LINK_STATUS, &linked);
    glDeleteShader(vertex); vertex = 0;
    glDeleteShader(fragment); fragment = 0;
    if (!linked)
        goto fail;
    glUseProgram(n->program);
    glUniform1i(glGetUniformLocation(n->program, "picture"), 0);
    n->opaque = glGetUniformLocation(n->program, "opaque");
    glGenBuffers(1, &n->vertices);
    glBindBuffer(GL_ARRAY_BUFFER, n->vertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    if (glGetError() != GL_NO_ERROR || !current(n, EGL_NO_SURFACE, n->draw))
        goto fail;
    ErrorF("Trames Xwayland native WSI: EGL %d.%d, %s; no DRM, no SHM presentation\n", major, minor, renderer);
    return TRUE;
fail:
    ErrorF("native WSI EGL initialization failed: 0x%x\n", eglGetError());
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    xwl_native_fini(screen);
    return FALSE;
}
Bool xwl_native_present(struct xwl_window *window)
{
    struct xwl_screen *screen = window->xwl_screen;
    struct xwl_native *n = screen->native_glamor;
    struct xwl_native_window *w = window->native_glamor;
    PixmapPtr pixmap = screen->screen->GetWindowPixmap(window->surface_window);
    GLuint texture = glamor_get_pixmap_texture(pixmap);
    EGLSyncKHR ready, consumed;
    const EGLint sync_attributes[] = {EGL_NONE};
    GLint parameters[4];
    const GLenum names[] = {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER,
                            GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T};
    int width = pixmap->drawable.width, height = pixmap->drawable.height;
    unsigned i;
    if (!texture || width < 1 || height < 1 || width > 4096 || height > 4096)
        FatalError("native WSI: unavailable/bounded pixmap texture (%dx%d)\n", width, height);
    if (!w) {
        w = calloc(1, sizeof(*w));
        if (!w) return FALSE;
        window->native_glamor = w;
        w->window = wl_egl_window_create(window->surface, width, height);
        if (!w->window) goto fail;
        w->surface = eglCreatePlatformWindowSurfaceEXT(n->display, n->config, w->window, NULL);
        if (w->surface == EGL_NO_SURFACE) goto fail;
        w->width = width; w->height = height;
    } else if (w->width != width || w->height != height) {
        wl_egl_window_resize(w->window, width, height, 0, 0);
        w->width = width; w->height = height;
    }
    /* Explicit GPU ordering between shared contexts, in both directions. No
     * pixel readback, CPU staging pixmap, global guest lock or glFinish. */
    ready = eglCreateSyncKHR(n->display, EGL_SYNC_FENCE_KHR, sync_attributes);
    if (ready == EGL_NO_SYNC_KHR) goto fail;
    glFlush();
    if (!current(n, w->surface, n->present)) {
        eglDestroySyncKHR(n->display, ready); goto fail;
    }
    if (!eglWaitSyncKHR(n->display, ready, 0)) {
        eglDestroySyncKHR(n->display, ready); goto fail;
    }
    eglDestroySyncKHR(n->display, ready);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    for (i = 0; i < 4; ++i) glGetTexParameteriv(GL_TEXTURE_2D, names[i], &parameters[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glViewport(0, 0, width, height);
    glUseProgram(n->program);
    glUniform1f(n->opaque, pixmap->drawable.depth == 24 ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    for (i = 0; i < 4; ++i) glTexParameteri(GL_TEXTURE_2D, names[i], parameters[i]);
    if (glGetError() != GL_NO_ERROR || !eglSwapBuffers(n->display, w->surface)) goto fail;
    consumed = eglCreateSyncKHR(n->display, EGL_SYNC_FENCE_KHR, sync_attributes);
    if (consumed == EGL_NO_SYNC_KHR) goto fail;
    glFlush();
    if (!current(n, EGL_NO_SURFACE, n->draw)) {
        eglDestroySyncKHR(n->display, consumed); goto fail;
    }
    if (!eglWaitSyncKHR(n->display, consumed, 0)) {
        eglDestroySyncKHR(n->display, consumed); goto fail;
    }
    eglDestroySyncKHR(n->display, consumed);
    return TRUE;
fail:
    FatalError("native WSI present failed (EGL 0x%x); refusing SHM fallback\n", eglGetError());
    return FALSE;
}
void xwl_native_window_destroy(struct xwl_window *window)
{
    struct xwl_native_window *w = window->native_glamor;
    struct xwl_native *n = window->xwl_screen->native_glamor;
    if (!w) return;
    current(n, EGL_NO_SURFACE, n->draw);
    if (w->surface != EGL_NO_SURFACE) eglDestroySurface(n->display, w->surface);
    if (w->window) wl_egl_window_destroy(w->window);
    free(w); window->native_glamor = NULL;
}
void xwl_native_fini(struct xwl_screen *screen)
{
    struct xwl_native *n = screen->native_glamor;
    if (!n) return;
    if (n->display != EGL_NO_DISPLAY) {
        if (n->present != EGL_NO_CONTEXT && current(n, EGL_NO_SURFACE, n->present)) {
            if (n->program) glDeleteProgram(n->program);
            if (n->vertices) glDeleteBuffers(1, &n->vertices);
        }
        current(n, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (n->present != EGL_NO_CONTEXT) eglDestroyContext(n->display, n->present);
        if (n->draw != EGL_NO_CONTEXT) eglDestroyContext(n->display, n->draw);
        eglTerminate(n->display);
    }
    screen->egl_context = NULL; screen->egl_display = NULL;
    free(n); screen->native_glamor = NULL;
}
