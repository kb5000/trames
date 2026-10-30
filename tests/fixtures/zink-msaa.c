// SPDX-License-Identifier: GPL-3.0-or-later
// Real GLES multisample renderbuffers, native resolve, sample masks and alpha
// coverage through the paired original Zink. No version/feature overrides.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static GLuint shader(GLenum stage, const char *source) {
    GLuint s = glCreateShader(stage); glShaderSource(s, 1, &source, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "%s\n", log); }
    assert(ok); return s;
}
static GLuint program(const char *vertex, const char *fragment) {
    GLuint p = glCreateProgram();
    GLuint a = shader(fragment ? GL_VERTEX_SHADER : GL_COMPUTE_SHADER, vertex); glAttachShader(p, a);
    GLuint b = 0; if (fragment) { b = shader(GL_FRAGMENT_SHADER, fragment); glAttachShader(p, b); }
    glLinkProgram(p); GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, NULL, log); fprintf(stderr, "%s\n", log); }
    assert(ok); glDeleteShader(a); if (b) glDeleteShader(b); return p;
}
static const char *vertex = "#version 310 es\nvoid main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);}";
static void multisample(void) {
    GLint maximum=0;glGetIntegerv(GL_MAX_SAMPLES,&maximum);assert(maximum>=2);GLsizei samples=maximum>=4?4:2;
    GLuint source,destination,color,depth,texture,vao;glGenFramebuffers(1,&source);glBindFramebuffer(GL_FRAMEBUFFER,source);
    glGenRenderbuffers(1,&color);glBindRenderbuffer(GL_RENDERBUFFER,color);glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,8,8);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH_COMPONENT32F,8,8);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glGenFramebuffers(1,&destination);glBindFramebuffer(GL_FRAMEBUFFER,destination);glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,8,8);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    GLuint p=program(vertex,"#version 310 es\nprecision highp float;uniform float alpha;layout(location=0)out vec4 c;void main(){c=vec4(1,0,0,alpha);}");glUseProgram(p);GLint alpha=glGetUniformLocation(p,"alpha");assert(alpha>=0);glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,8,8);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
    for(unsigned mode=0;mode<4;mode++){
        glBindFramebuffer(GL_FRAMEBUFFER,source);glClearColor(0,0,0,0);glClearDepthf(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(mode==1||mode==2){glEnable(GL_SAMPLE_MASK);glSampleMaski(0,mode==1?0x55555555u:0u);}else glDisable(GL_SAMPLE_MASK);
        if(mode==3)glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glUniform1f(alpha,mode==3?0:1);glDrawArrays(GL_TRIANGLES,0,3);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,source);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destination);glBlitFramebuffer(0,0,8,8,0,0,8,8,GL_COLOR_BUFFER_BIT,GL_NEAREST);glBindFramebuffer(GL_READ_FRAMEBUFFER,destination);
        uint8_t pixels[8*8*4];glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);GLenum error=glGetError();if(error)fprintf(stderr,"MSAA GLES mode=%u error=0x%x\n",mode,error);assert(error==GL_NO_ERROR);
        unsigned expected=mode==0?255:mode==1?128:0;for(unsigned i=0;i<64;i++)for(unsigned ch=0;ch<4;ch++){unsigned value=pixels[i*4+ch],wanted=ch==0||ch==3?expected:0;assert(value+1>=wanted && value<=wanted+1);}
        printf("zink-msaa-case-ok samples=%d mode=%u pixels=64\n",samples,mode);fflush(stdout);
    }
    glDeleteProgram(p);glDeleteVertexArrays(1,&vao);glDeleteTextures(1,&texture);glDeleteRenderbuffers(1,&color);glDeleteRenderbuffers(1,&depth);glDeleteFramebuffers(1,&source);glDeleteFramebuffers(1,&destination);
}
int main(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");assert(platform);
    EGLDisplay display=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);EGLint major,minor,count;assert(eglInitialize(display,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};EGLConfig config;assert(eglChooseConfig(display,attrs,&config,1,&count) && count==1);
    const EGLint dims[]={EGL_WIDTH,12,EGL_HEIGHT,12,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,dims);assert(surface!=EGL_NO_SURFACE);
    const EGLint version[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,1,EGL_NONE};EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);assert(context!=EGL_NO_CONTEXT);assert(eglMakeCurrent(display,surface,surface,context));
    printf("renderer: %s; version: %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));fflush(stdout);
    multisample();assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);puts("zink-msaa-ok");return 0;
}
