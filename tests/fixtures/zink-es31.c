// SPDX-License-Identifier: GPL-3.0-or-later
// Real GLES calls through Zink: GPU-produced indirect arguments, images/atomics,
// and native compressed mip-edge transfers. No version or feature overrides.
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
static void gpu_arguments(void) {
    uint32_t initial[32] = {0}; GLuint args, atomic, fragments, results, image, vao, indices;
    glGenBuffers(1, &args); glBindBuffer(GL_SHADER_STORAGE_BUFFER, args);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof initial, initial, GL_DYNAMIC_COPY); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, args);
    glGenBuffers(1, &atomic); glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomic);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, 4, initial, GL_DYNAMIC_COPY); glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 1, atomic);
    glGenBuffers(1, &fragments); glBindBuffer(GL_SHADER_STORAGE_BUFFER, fragments);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 4, initial, GL_DYNAMIC_COPY); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, fragments);
    glGenBuffers(1, &results); glBindBuffer(GL_SHADER_STORAGE_BUFFER, results);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 16, initial, GL_DYNAMIC_COPY); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, results);
    glGenTextures(1, &image); glBindTexture(GL_TEXTURE_2D, image); glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 1);
    glBindImageTexture(0, image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
    GLuint setup = program("#version 310 es\nlayout(local_size_x=1)in;layout(std430,binding=0)buffer B{uint v[];};void main(){v[0]=4u;v[1]=1u;v[2]=1u;v[4]=3u;v[5]=1u;v[6]=0u;v[7]=0u;v[8]=3u;v[9]=1u;v[10]=0u;v[11]=0u;v[12]=0u;}", NULL);
    glUseProgram(setup); glDispatchCompute(1,1,1); glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    GLuint compute = program("#version 310 es\nlayout(local_size_x=1)in;layout(std430,binding=3)buffer R{uint values[];};layout(binding=1,offset=0)uniform highp atomic_uint count;layout(binding=0,rgba8)writeonly uniform highp image2D pic;void main(){uint i=gl_GlobalInvocationID.x;values[i]=i*7u+11u;atomicCounterIncrement(count);imageStore(pic,ivec2(int(i),0),vec4(1,0.5,0,1));}", NULL);
    glUseProgram(compute); glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, args); glDispatchComputeIndirect(0);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    GLuint draw=program(vertex,"#version 310 es\nprecision highp float;layout(binding=0,rgba8)readonly uniform highp image2D pic;layout(std430,binding=2)buffer F{uint n;};layout(location=0)out vec4 c;void main(){atomicAdd(n,1u);c=imageLoad(pic,ivec2(0));}");
    glUseProgram(draw); glGenVertexArrays(1,&vao); glBindVertexArray(vao); glViewport(0,0,4,4); glBindBuffer(GL_DRAW_INDIRECT_BUFFER,args);
    glDrawArraysIndirect(GL_TRIANGLES,(void *)(uintptr_t)16);
    unsigned char pixels[4*4*4];
    const uint16_t index[]={0,1,2}; glGenBuffers(1,&indices); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indices); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof index,index,GL_STATIC_DRAW);
    glDrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_SHORT,(void *)(uintptr_t)32); glDeleteBuffers(1,&args);
    // All indirect commands have been recorded before any CPU readback. The
    // source argument buffer is deleted above while native commands retain it.
    glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(unsigned i=0;i<16;i++){assert(pixels[4*i]==255 && pixels[4*i+1]>=127 && pixels[4*i+1]<=128 && pixels[4*i+2]==0 && pixels[4*i+3]==255);}
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, results); uint32_t *values=glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, 16, GL_MAP_READ_BIT);
    assert(values); for(unsigned i=0;i<4;i++)assert(values[i]==i*7+11); assert(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomic); values=glMapBufferRange(GL_ATOMIC_COUNTER_BUFFER,0,4,GL_MAP_READ_BIT); assert(values && *values==4); assert(glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER));
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT); glBindBuffer(GL_SHADER_STORAGE_BUFFER,fragments);
    values=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,4,GL_MAP_READ_BIT); assert(values && *values>=32); assert(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    assert(glGetError()==GL_NO_ERROR);
    glDeleteBuffers(1,&indices);glDeleteBuffers(1,&atomic);glDeleteBuffers(1,&fragments);glDeleteBuffers(1,&results);glDeleteTextures(1,&image);glDeleteVertexArrays(1,&vao);glDeleteProgram(setup);glDeleteProgram(compute);glDeleteProgram(draw);
    puts("zink-es31-gpu-ok: GPU-produced dispatch/draw/indexed-draw arguments; compute and fragment image/SSBO atomics");fflush(stdout);
}
static void astc(void) {
    enum { FORMAT=0x93b6, WIDTH=9, HEIGHT=7 }; // ASTC 8x6 RGBA, four 16-byte blocks
    uint8_t blocks[64];const uint8_t colors[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}};
    for(unsigned b=0;b<4;b++) { uint8_t *p=blocks+b*16;memset(p,255,16);p[0]=0xfc;p[1]=0xfd;for(unsigned c=0;c<4;c++)p[8+c*2]=p[9+c*2]=colors[b][c]; }
    GLuint textures[2],vao;glGenTextures(2,textures);glBindTexture(GL_TEXTURE_2D,textures[0]);
    glCompressedTexImage2D(GL_TEXTURE_2D,0,FORMAT,WIDTH,HEIGHT,0,sizeof blocks,blocks);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    assert(glGetError()==GL_NO_ERROR);
    // A partial final block is legal at the logical edge; no texel-size math.
    glCompressedTexSubImage2D(GL_TEXTURE_2D,0,8,6,1,1,FORMAT,16,blocks);
    assert(glGetError()==GL_NO_ERROR);
    glBindTexture(GL_TEXTURE_2D,textures[1]);glTexStorage2D(GL_TEXTURE_2D,1,FORMAT,WIDTH,HEIGHT);
    assert(glGetError()==GL_NO_ERROR);
    typedef void (*copy_fn)(GLuint,GLenum,GLint,GLint,GLint,GLint,GLuint,GLenum,GLint,GLint,GLint,GLint,GLsizei,GLsizei,GLsizei);
    copy_fn copy=(copy_fn)eglGetProcAddress("glCopyImageSubDataOES");assert(copy);
    copy(textures[0],GL_TEXTURE_2D,0,0,0,0,textures[1],GL_TEXTURE_2D,0,0,0,0,WIDTH,HEIGHT,1);
    assert(glGetError()==GL_NO_ERROR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLuint p=program(vertex,"#version 310 es\nprecision highp float;uniform highp sampler2D tex;layout(location=0)out vec4 c;void main(){c=texelFetch(tex,ivec2(gl_FragCoord.xy),0);}");
    glUseProgram(p);glUniform1i(glGetUniformLocation(p,"tex"),0);glViewport(0,0,WIDTH,HEIGHT);glGenVertexArrays(1,&vao);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
    uint8_t pixels[WIDTH*HEIGHT*4];glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,pixels);assert(glGetError()==GL_NO_ERROR);
    for(unsigned y=0;y<HEIGHT;y++)for(unsigned x=0;x<WIDTH;x++) { unsigned b=(y/6)*2+x/8;if(b==3)b=0;assert(!memcmp(pixels+(y*WIDTH+x)*4,colors[b],4)); }
    glDeleteTextures(2,textures);glDeleteVertexArrays(1,&vao);glDeleteProgram(p);
    puts("zink-astc-ok: native 8x6 ASTC upload, partial edge block update, image copy, 63 exact pixels");fflush(stdout);
}
int main(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");assert(platform);
    EGLDisplay display=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);EGLint major,minor,count;assert(eglInitialize(display,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};EGLConfig config;assert(eglChooseConfig(display,attrs,&config,1,&count) && count==1);
    const EGLint dims[]={EGL_WIDTH,12,EGL_HEIGHT,12,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,dims);assert(surface!=EGL_NO_SURFACE);
    const EGLint version[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,1,EGL_NONE};EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);assert(context!=EGL_NO_CONTEXT);assert(eglMakeCurrent(display,surface,surface,context));
    printf("renderer: %s; version: %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));fflush(stdout);
    gpu_arguments();astc();assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);puts("zink-es31-ok");return 0;
}
