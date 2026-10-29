// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise Mesa's typed PBO upload helper on an ordinary GLES 3 context.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void integer_upload(int source_unsigned, int target_unsigned) {
    uint32_t source[4 * 4 * 4], actual[4 * 4 * 4], expected[4 * 4 * 4];
    const uint32_t cases[] = {0, 1, 17, 0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < sizeof source / sizeof *source; ++i) {
        source[i] = cases[i % (sizeof cases / sizeof *cases)];
        expected[i] = source[i];
        if (source_unsigned && !target_unsigned && source[i] > INT32_MAX)
            expected[i] = INT32_MAX;
        if (!source_unsigned && target_unsigned && (int32_t)source[i] < 0)
            expected[i] = 0;
    }
    GLuint texture, pbo, framebuffer;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, target_unsigned ? GL_RGBA32UI : GL_RGBA32I, 4, 4);
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof source, source, GL_STATIC_DRAW);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA_INTEGER,
                    source_unsigned ? GL_UNSIGNED_INT : GL_INT, NULL);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    GLenum error = glGetError();
    if (source_unsigned != target_unsigned) {
        // GLES 3 rejects these format/type combinations before a transfer.
        assert(error == GL_INVALID_OPERATION);
        glDeleteBuffers(1, &pbo);
        glDeleteTextures(1, &texture);
        puts("zink-pbo: cross-signedness upload rejected by GLES as required");
        return;
    }
    assert(error == GL_NO_ERROR);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    memset(actual, 0x55, sizeof actual);
    glReadPixels(0, 0, 4, 4, GL_RGBA_INTEGER,
                 target_unsigned ? GL_UNSIGNED_INT : GL_INT, actual);
    assert(glGetError() == GL_NO_ERROR);
    for (unsigned i = 0; i < sizeof source / sizeof *source; ++i) {
        if (actual[i] != expected[i])
            fprintf(stderr, "pbo %s->%s element %u: got %08x expected %08x\n",
                    source_unsigned ? "uint" : "sint", target_unsigned ? "uint" : "sint",
                    i, actual[i], expected[i]);
        assert(actual[i] == expected[i]);
    }
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteBuffers(1, &pbo);
    glDeleteTextures(1, &texture);
    printf("zink-pbo: %s->%s: 64 exact components\n",
           source_unsigned ? "uint" : "sint", target_unsigned ? "uint" : "sint");
    fflush(stdout);
}

int main(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    assert(platform);
    EGLDisplay display = platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    assert(display != EGL_NO_DISPLAY);
    EGLint major, minor, count;
    assert(eglInitialize(display, &major, &minor));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint config_attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    assert(eglChooseConfig(display, config_attributes, &config, 1, &count) && count == 1);
    const EGLint dimensions[] = {EGL_WIDTH, 4, EGL_HEIGHT, 4, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, dimensions);
    assert(surface != EGL_NO_SURFACE);
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
    assert(context != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display, surface, surface, context));
    printf("renderer: %s; version: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    fflush(stdout);
    for (int source_unsigned = 0; source_unsigned < 2; ++source_unsigned)
        for (int target_unsigned = 0; target_unsigned < 2; ++target_unsigned)
            integer_upload(source_unsigned, target_unsigned);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    assert(eglDestroyContext(display, context));
    assert(eglDestroySurface(display, surface));
    assert(eglTerminate(display));
    puts("zink-pbo-ok: 128 exact integer components and GLES type checks passed");
    return 0;
}
