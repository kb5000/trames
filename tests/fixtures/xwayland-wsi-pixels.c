// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only guest EGL interposer. Checks the final presentation framebuffer of
// the exact production Xwayland binary, before forwarding to original Mesa.
// Never installed/bundled into the App. Production has no pixel readback.
#define EGL_NO_X11
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EGLBoolean verify_swap(EGLDisplay display, EGLSurface surface) {
    PFNEGLSWAPBUFFERSPROC next=(PFNEGLSWAPBUFFERSPROC)dlsym(RTLD_NEXT,"eglSwapBuffers");
    if (!next) { fprintf(stderr,"WSI oracle: missing real swap: %s\n",dlerror());abort(); }
    EGLint width=0,height=0;
    eglQuerySurface(display,surface,EGL_WIDTH,&width);
    eglQuerySurface(display,surface,EGL_HEIGHT,&height);
    if ((width==128 && height==96) || (width==160 && height==100)) {
        unsigned char *pixels=malloc((size_t)width*height*4);
        if (!pixels) abort();
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        GLenum error=glGetError();
        if (error!=GL_NO_ERROR) { fprintf(stderr,"WSI oracle: glReadPixels error 0x%x\n",error);abort(); }
        int matches=1;
        for (int y=0; y<height && matches; ++y) for (int x=0; x<width; ++x) {
            // GL readback starts at the bottom. X11's upper half is green.
            const unsigned char expected[4]={x>=width/2?255:0,y>=height/2?255:0,0,255};
            if (memcmp(pixels+((size_t)y*width+x)*4,expected,4)) { matches=0;break; }
        }
        free(pixels);
        // Mapping/expose may produce an initial blank frame. The harness waits
        // for the complete pattern, then checks the real native GPU frame too.
        if (matches) {
            fprintf(stderr,"x11-wsi-pixels-ok: %dx%d %d pixels\n",width,height,width*height);
            fflush(stderr);
        }
    }
    return next(display,surface);
}
// Epoxy resolves EGL functions against its dlopen handle, bypassing ordinary
// eglSwapBuffers interposition. Override its exported dispatch slot instead.
EGLBoolean (*epoxy_eglSwapBuffers)(EGLDisplay,EGLSurface)=verify_swap;
