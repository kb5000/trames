// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary EGL/GLES client: original signed Mesa chooses its Zink driver.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, const char *operation) {
    if (!ok) {
        fprintf(stderr, "zink-egl: %s failed, EGL error=0x%x\n", operation, eglGetError());
        exit(1);
    }
}

int main(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    check(get_display != NULL, "eglGetPlatformDisplayEXT");
    EGLDisplay display = get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    check(display != EGL_NO_DISPLAY, "surfaceless display");
    EGLint major, minor;
    check(eglInitialize(display, &major, &minor), "eglInitialize");
    check(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count;
    check(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1, "eglChooseConfig");
    const EGLint size[] = {EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, size);
    check(surface != EGL_NO_SURFACE, "eglCreatePbufferSurface");
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
    check(context != EGL_NO_CONTEXT, "eglCreateContext");
    check(eglMakeCurrent(display, surface, surface, context), "eglMakeCurrent");
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    check(renderer && strstr(renderer, "zink"), "Zink renderer selection");
    printf("EGL %d.%d; GL renderer: %s\n", major, minor, renderer);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char pixels[8 * 8 * 4];
    glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(glGetError() == GL_NO_ERROR, "glReadPixels");
    for (unsigned i = 0; i < sizeof pixels; i += 4)
        check(pixels[i] == 255 && pixels[i + 1] == 0 &&
              pixels[i + 2] == 255 && pixels[i + 3] == 255, "clear pixel");
    check(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "release context");
    check(eglDestroyContext(display, context), "eglDestroyContext");
    check(eglDestroySurface(display, surface), "eglDestroySurface");
    check(eglTerminate(display), "eglTerminate");
    puts("zink-egl-ok: 64 GPU clear/readback pixels correct");
    return 0;
}
