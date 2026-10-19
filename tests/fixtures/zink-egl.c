// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary EGL/GLES client: original signed Mesa chooses its Zink driver.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void check(int ok, const char *operation) {
    if (!ok) {
        fprintf(stderr, "zink-egl: %s failed, EGL error=0x%x\n", operation, eglGetError());
        exit(1);
    }
}

static GLuint shader(GLenum type,const char *source) {
    GLuint out=glCreateShader(type);glShaderSource(out,1,&source,NULL);glCompileShader(out);
    GLint ok=0;glGetShaderiv(out,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(out,sizeof(log),NULL,log);fprintf(stderr,"shader: %s\n",log);exit(1);}return out;
}
static GLuint program(const char *fragment) {
    GLuint vertex=shader(GL_VERTEX_SHADER,"attribute vec2 position; varying vec2 uv; void main(){uv=position*0.5+0.5;gl_Position=vec4(position,0.0,1.0);}");
    GLuint pixel=shader(GL_FRAGMENT_SHADER,fragment),out=glCreateProgram();glAttachShader(out,vertex);glAttachShader(out,pixel);glBindAttribLocation(out,0,"position");glLinkProgram(out);
    GLint ok=0;glGetProgramiv(out,GL_LINK_STATUS,&ok);if(!ok){char log[2048];glGetProgramInfoLog(out,sizeof(log),NULL,log);fprintf(stderr,"program: %s\n",log);exit(1);}glDeleteShader(vertex);glDeleteShader(pixel);return out;
}
static void draw_and_sample(void) {
    const GLfloat positions[]={-1,-1,3,-1,-1,3};GLuint vbo;glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(positions),positions,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);glViewport(0,0,8,8);
    GLuint color=program("precision mediump float; uniform vec4 tint; void main(){gl_FragColor=tint;}");glUseProgram(color);glUniform4f(glGetUniformLocation(color,"tint"),0.25f,0.5f,0.75f,1.0f);glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char pixels[8*8*4];glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(glGetError()==GL_NO_ERROR,"shader draw/readback");
    const int wanted[4]={64,128,191,255};for(unsigned i=0;i<sizeof(pixels);i++)check(abs((int)pixels[i]-wanted[i%4])<=1,"uniform shader pixel");puts("zink-egl-shader-ok: 64 uniform GLSL pixels correct");fflush(stdout);
    unsigned char image[4*4*4];for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){
        unsigned at=(y*4+x)*4;image[at]=x<2?255:0;image[at+1]=y<2?255:0;image[at+2]=x>=2?255:0;image[at+3]=255;
    }
    GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    GLuint sampled=program("precision mediump float; uniform sampler2D picture; varying vec2 uv; void main(){gl_FragColor=texture2D(picture,uv);}");glUseProgram(sampled);glUniform1i(glGetUniformLocation(sampled,"picture"),0);
    for(unsigned round=0;round<3;round++){
        glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(glGetError()==GL_NO_ERROR,"texture draw/readback");
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)for(unsigned c=0;c<4;c++)check(pixels[(y*8+x)*4+c]==image[((y/2)*4+x/2)*4+c],"sampled texture pixel");
    }
    puts("zink-egl-texture-ok: 3 rounds / 192 sampled GLSL pixels correct");fflush(stdout);
    glDeleteTextures(1,&texture);glDeleteProgram(sampled);glDeleteProgram(color);glDeleteBuffers(1,&vbo);
}

int main(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    check(get_display != NULL, "eglGetPlatformDisplayEXT");
    EGLDisplay display = get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    check(display != EGL_NO_DISPLAY, "surfaceless display");
    EGLint major, minor;
    check(eglInitialize(display, &major, &minor), "eglInitialize");
    printf("EGL initialized %d.%d\n",major,minor);fflush(stdout);
    check(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count;
    EGLBoolean selected=eglChooseConfig(display, attributes, &config, 1, &count);
    if(!selected || count!=1){
        EGLint total=0;eglGetConfigs(display,NULL,0,&total);fprintf(stderr,"zink-egl: %d available EGL configs\n",total);
        EGLConfig *all=calloc((size_t)total,sizeof(*all));if(all && eglGetConfigs(display,all,total,&total))for(int i=0;i<total && i<12;i++){
            EGLint red,green,blue,alpha,surface,render;eglGetConfigAttrib(display,all[i],EGL_RED_SIZE,&red);eglGetConfigAttrib(display,all[i],EGL_GREEN_SIZE,&green);eglGetConfigAttrib(display,all[i],EGL_BLUE_SIZE,&blue);eglGetConfigAttrib(display,all[i],EGL_ALPHA_SIZE,&alpha);eglGetConfigAttrib(display,all[i],EGL_SURFACE_TYPE,&surface);eglGetConfigAttrib(display,all[i],EGL_RENDERABLE_TYPE,&render);
            fprintf(stderr,"config %d: rgba=%d/%d/%d/%d surface=0x%x renderable=0x%x\n",i,red,green,blue,alpha,surface,render);
        }free(all);
    }
    check(selected && count==1,"eglChooseConfig");
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
    puts("zink-egl-clear-ok: 64 GPU clear/readback pixels correct");fflush(stdout);
    draw_and_sample();
    check(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "release context");
    check(eglDestroyContext(display, context), "eglDestroyContext");
    check(eglDestroySurface(display, surface), "eglDestroySurface");
    check(eglTerminate(display), "eglTerminate");
    puts("zink-egl-ok: clear, uniform shader, repeated texture sampling and teardown correct");
    return 0;
}
