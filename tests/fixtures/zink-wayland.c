// SPDX-License-Identifier: GPL-3.0-or-later
// Original libwayland/EGL/GLES2 client. No Mesa or Vulkan WSI replacement.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct xdg_wm_base *shell;
static EGLDisplay egl;
static EGLContext context;
static EGLConfig config;
static GLuint program, vbo;
struct window {
    struct wl_surface *surface;
    struct xdg_surface *xdg;
    struct xdg_toplevel *top;
    struct wl_egl_window *native;
    EGLSurface egl;
    int id, width, height, configured, closed, drawn_width, drawn_height;
    unsigned frames;
};
static void check(int ok, const char *operation) {
    if (!ok) {
        fprintf(stderr, "zink-wayland: %s failed: errno=%d EGL=0x%x GL=0x%x\n", operation, errno, eglGetError(), glGetError());
        exit(1);
    }
}
static void configured(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct window *w=data; xdg_surface_ack_configure(surface,serial); w->configured=1;
    if(w->native)wl_egl_window_resize(w->native,w->width,w->height,0,0);
}
static const struct xdg_surface_listener surface_listener={configured};
static void top_configure(void *data, struct xdg_toplevel *top, int32_t width, int32_t height, struct wl_array *states) {
    (void)top;(void)states;struct window *w=data;
    if(width>0 && height>0){w->width=width;w->height=height;}
}
static void top_close(void *data, struct xdg_toplevel *top){(void)top;((struct window *)data)->closed=1;}
static const struct xdg_toplevel_listener top_listener={.configure=top_configure,.close=top_close};
static void ping(void *data,struct xdg_wm_base *base,uint32_t serial){(void)data;xdg_wm_base_pong(base,serial);}
static const struct xdg_wm_base_listener shell_listener={ping};
static void global(void *data,struct wl_registry *registry,uint32_t id,const char *name,uint32_t version) {
    (void)data;
    if(!strcmp(name,"wl_compositor"))compositor=wl_registry_bind(registry,id,&wl_compositor_interface,version<4?version:4);
    if(!strcmp(name,"xdg_wm_base")){shell=wl_registry_bind(registry,id,&xdg_wm_base_interface,version<3?version:3);xdg_wm_base_add_listener(shell,&shell_listener,NULL);}
}
static void removed(void *data,struct wl_registry *registry,uint32_t id){(void)data;(void)registry;(void)id;}
static const struct wl_registry_listener registry_listener={global,removed};
static GLuint shader(GLenum type,const char *source) {
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"shader: %s\n",log);}
    check(ok,"compile GLSL");return s;
}
static void init_gl(void) {
    GLuint vert=shader(GL_VERTEX_SHADER,"attribute vec2 position; varying vec2 uv; void main(){uv=position*0.5+0.5;gl_Position=vec4(position,0.0,1.0);}");
    GLuint frag=shader(GL_FRAGMENT_SHADER,"precision mediump float; varying vec2 uv; uniform float blue; void main(){gl_FragColor=vec4(step(0.5,uv.x),step(0.5,uv.y),blue,1.0);}");
    program=glCreateProgram();glAttachShader(program,vert);glAttachShader(program,frag);glBindAttribLocation(program,0,"position");glLinkProgram(program);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);check(ok,"link GLSL");glDeleteShader(vert);glDeleteShader(frag);glUseProgram(program);
    const GLfloat vertices[]={-1,-1,3,-1,-1,3};glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
}
static void draw(struct window *w) {
    check(eglMakeCurrent(egl,w->egl,w->egl,context),"make window current");
    glViewport(0,0,w->width,w->height);glUniform1f(glGetUniformLocation(program,"blue"),(float)w->id);glDrawArrays(GL_TRIANGLES,0,3);
    GLenum error=glGetError();if(error!=GL_NO_ERROR)fprintf(stderr,"GL draw error 0x%x window=%d\n",error,w->id);check(error==GL_NO_ERROR,"draw window GLSL");
    int changed=w->width!=w->drawn_width || w->height!=w->drawn_height;
    if(changed){
        size_t size=(size_t)w->width*w->height*4;unsigned char *pixels=malloc(size);check(pixels!=NULL,"readback allocation");
        glReadPixels(0,0,w->width,w->height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);error=glGetError();if(error!=GL_NO_ERROR)fprintf(stderr,"GL read error 0x%x window=%d\n",error,w->id);check(error==GL_NO_ERROR,"readback window GLSL");
        for(int y=0;y<w->height;y++)for(int x=0;x<w->width;x++){
            const unsigned char expected[4]={x>=w->width/2?255:0,y>=w->height/2?255:0,w->id?255:0,255};
            check(!memcmp(pixels+((size_t)y*w->width+x)*4,expected,4),"window GLSL pixels");
        }
        free(pixels);
    }
    check(eglSwapBuffers(egl,w->egl),"eglSwapBuffers");w->frames++;
    if(changed){printf("zink-wayland-frame id=%d size=%dx%d pixels=correct\n",w->id,w->width,w->height);fflush(stdout);w->drawn_width=w->width;w->drawn_height=w->height;}
}
static void destroy(struct window *w) {
    check(eglMakeCurrent(egl,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT),"unbind closed window");
    check(eglDestroySurface(egl,w->egl),"destroy EGL window");wl_egl_window_destroy(w->native);
    xdg_toplevel_destroy(w->top);xdg_surface_destroy(w->xdg);wl_surface_destroy(w->surface);w->surface=NULL;
    printf("zink-wayland-closed id=%d frames=%u\n",w->id,w->frames);fflush(stdout);
}
int main(int argc,char **argv) {
    unsigned limit=argc>1?(unsigned)atoi(argv[1]):10000;check(limit>0 && limit<=10000,"frame limit");
    display=wl_display_connect(NULL);check(display!=NULL,"Wayland connect");
    struct wl_registry *registry=wl_display_get_registry(display);wl_registry_add_listener(registry,&registry_listener,NULL);
    check(wl_display_roundtrip(display)>=0 && compositor && shell,"Wayland globals");
    egl=eglGetDisplay((EGLNativeDisplayType)display);EGLint major,minor;check(eglInitialize(egl,&major,&minor),"Wayland eglInitialize");
    PFNEGLQUERYDISPLAYATTRIBEXTPROC query_display=(PFNEGLQUERYDISPLAYATTRIBEXTPROC)eglGetProcAddress("eglQueryDisplayAttribEXT");
    PFNEGLQUERYDEVICESTRINGEXTPROC query_device=(PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
    PFNEGLQUERYDEVICESEXTPROC query_devices=(PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    EGLAttrib device;EGLDeviceEXT devices[16];EGLint device_count;int found=0;
    check(query_display && query_device && query_devices,"EGL device query entrypoints");
    check(query_display(egl,EGL_DEVICE_EXT,&device) && device!=(EGLAttrib)EGL_NO_DEVICE_EXT,"display device identity");
    const char *extensions=query_device((EGLDeviceEXT)device,EGL_EXTENSIONS);check(extensions!=NULL,"device extensions");
    if(!getenv("LIBGL_ALWAYS_SOFTWARE"))check(!strstr(extensions,"EGL_MESA_device_software") && !strstr(extensions,"EGL_EXT_device_drm"),"native GPU must not claim software or DRM identity");
    check(query_devices(16,devices,&device_count) && device_count>0 && device_count<=16,"enumerate devices");
    for(int i=0;i<device_count;i++)if(devices[i]==(EGLDeviceEXT)device)found=1;
    check(found,"display device is enumerated");
    printf("zink-wayland-device extensions=[%s] identity=correct\n",extensions);fflush(stdout);
    printf("zink-wayland-EGL %d.%d\n",major,minor);fflush(stdout);check(eglBindAPI(EGL_OPENGL_ES_API),"bind GLES2");
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};EGLint count;
    check(eglChooseConfig(egl,attrs,&config,1,&count) && count==1,"Wayland config");
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};context=eglCreateContext(egl,config,EGL_NO_CONTEXT,ctx);check(context!=EGL_NO_CONTEXT,"GLES context");
    struct window windows[2]={{.id=0,.width=96,.height=64},{.id=1,.width=80,.height=48}};
    for(int i=0;i<2;i++){
        struct window *w=&windows[i];w->surface=wl_compositor_create_surface(compositor);w->xdg=xdg_wm_base_get_xdg_surface(shell,w->surface);xdg_surface_add_listener(w->xdg,&surface_listener,w);
        w->top=xdg_surface_get_toplevel(w->xdg);xdg_toplevel_add_listener(w->top,&top_listener,w);xdg_toplevel_set_title(w->top,i?"Zink EGL window 2":"Zink EGL window 1");xdg_toplevel_set_app_id(w->top,"trames-zink-wayland-test");wl_surface_commit(w->surface);
    }
    while(!windows[0].configured || !windows[1].configured)check(wl_display_dispatch(display)>=0,"initial configure");
    for(int i=0;i<2;i++){
        struct window *w=&windows[i];w->native=wl_egl_window_create(w->surface,w->width,w->height);check(w->native!=NULL,"wl_egl_window_create");
        w->egl=eglCreateWindowSurface(egl,config,(EGLNativeWindowType)w->native,NULL);check(w->egl!=EGL_NO_SURFACE,"eglCreateWindowSurface");
    }
    check(eglMakeCurrent(egl,windows[0].egl,windows[0].egl,context),"initial current");printf("zink-wayland-renderer %s\n",glGetString(GL_RENDERER));fflush(stdout);init_gl();
    while(windows[0].surface || windows[1].surface){
        check(wl_display_roundtrip(display)>=0,"Wayland dispatch");
        for(int i=0;i<2;i++){struct window *w=&windows[i];if(!w->surface)continue;if(w->closed || w->frames>=limit)destroy(w);else draw(w);}
        const struct timespec pause={0,10000000};nanosleep(&pause,NULL);
    }
    // Keep the context current without a drawable for final GL resource deletion.
    check(eglMakeCurrent(egl,EGL_NO_SURFACE,EGL_NO_SURFACE,context),"surfaceless cleanup");glDeleteProgram(program);glDeleteBuffers(1,&vbo);check(glGetError()==GL_NO_ERROR,"GL cleanup");
    check(eglMakeCurrent(egl,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT),"final unbind");check(eglDestroyContext(egl,context),"destroy context");check(eglTerminate(egl),"terminate EGL");
    xdg_wm_base_destroy(shell);wl_compositor_destroy(compositor);wl_registry_destroy(registry);check(wl_display_flush(display)>=0,"final flush");wl_display_disconnect(display);
    puts("zink-wayland-ok: two original Mesa EGL windows, GLSL pixels, swaps and teardown");return 0;
}
