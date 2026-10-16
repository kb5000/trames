// SPDX-License-Identifier: GPL-3.0-or-later
// Original libwayland C ABI and upstream v3 definitions; independently observes
// composition, UTF-8 offsets and done serials from the native host input bridge.
#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "text-input-v3-client-protocol.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct xdg_wm_base *shell;
static struct zwp_text_input_manager_v3 *manager;
static struct wl_surface *surface;
static struct zwp_text_input_v3 *text;
static int quit,has_commit;
static uint32_t commits;
static void state(const char *value,uint32_t cursor,uint32_t anchor){zwp_text_input_v3_set_surrounding_text(text,value,(int32_t)cursor,(int32_t)anchor);zwp_text_input_v3_set_cursor_rectangle(text,12,20,1,18);zwp_text_input_v3_commit(text);commits++;}
static void enter(void *d,struct zwp_text_input_v3 *t,struct wl_surface *s){(void)d;(void)t;assert(s==surface);zwp_text_input_v3_enable(text);state("a🙂中",8,1);puts("v3-enter");}
static void leave(void *d,struct zwp_text_input_v3 *t,struct wl_surface *s){(void)d;(void)t;(void)s;}
static void preedit(void *d,struct zwp_text_input_v3 *t,const char *value,int32_t begin,int32_t end){(void)d;(void)t;printf("v3-preedit:%s:%d:%d\n",value?value:"",begin,end);}
static void commit(void *d,struct zwp_text_input_v3 *t,const char *value){(void)d;(void)t;assert(!strcmp(value,"你好🙂"));has_commit=1;printf("v3-commit:%s\n",value);}
static void deletion(void *d,struct zwp_text_input_v3 *t,uint32_t before,uint32_t after){(void)d;(void)t;printf("v3-delete:%u:%u\n",before,after);}
static void done(void *d,struct zwp_text_input_v3 *t,uint32_t serial){(void)d;(void)t;assert(serial==commits);printf("v3-done:%u\n",serial);if(has_commit){state("a你好🙂",11,11);has_commit=0;}}
static const struct zwp_text_input_v3_listener text_listener={.enter=enter,.leave=leave,.preedit_string=preedit,.commit_string=commit,.delete_surrounding_text=deletion,.done=done};
static void configure(void *d,struct xdg_surface *s,uint32_t serial){(void)d;xdg_surface_ack_configure(s,serial);
#ifdef __linux__
 int fd=memfd_create("text-v3",MFD_CLOEXEC);
#else
 char name[]="/tmp/trames-text-v3-XXXXXX";int fd=mkstemp(name);assert(fd>=0);unlink(name);fcntl(fd,F_SETFD,FD_CLOEXEC);
#endif
 assert(fd>=0&&ftruncate(fd,64*64*4)==0);struct wl_shm_pool *pool=wl_shm_create_pool(shm,fd,64*64*4);struct wl_buffer *b=wl_shm_pool_create_buffer(pool,0,64,64,64*4,WL_SHM_FORMAT_XRGB8888);wl_shm_pool_destroy(pool);wl_surface_attach(surface,b,0,0);wl_surface_commit(surface);wl_buffer_destroy(b);close(fd);
}
static const struct xdg_surface_listener surface_listener={configure};
static void top_configure(void *d,struct xdg_toplevel *t,int32_t w,int32_t h,struct wl_array *s){(void)d;(void)t;(void)w;(void)h;(void)s;}
static void close_window(void *d,struct xdg_toplevel *t){(void)d;(void)t;quit=1;}
static const struct xdg_toplevel_listener top_listener={.configure=top_configure,.close=close_window};
static void global(void *d,struct wl_registry *r,uint32_t id,const char *name,uint32_t version){(void)d;
 if(!strcmp(name,"wl_compositor"))compositor=wl_registry_bind(r,id,&wl_compositor_interface,version<4?version:4);
 if(!strcmp(name,"wl_shm"))shm=wl_registry_bind(r,id,&wl_shm_interface,1);
 if(!strcmp(name,"wl_seat"))seat=wl_registry_bind(r,id,&wl_seat_interface,7);
 if(!strcmp(name,"xdg_wm_base"))shell=wl_registry_bind(r,id,&xdg_wm_base_interface,3);
 if(!strcmp(name,"zwp_text_input_manager_v3"))manager=wl_registry_bind(r,id,&zwp_text_input_manager_v3_interface,1);
}
static void removed(void *d,struct wl_registry *r,uint32_t id){(void)d;(void)r;(void)id;}
static const struct wl_registry_listener registry_listener={global,removed};
int main(void){setvbuf(stdout,NULL,_IOLBF,0);display=wl_display_connect(NULL);assert(display);struct wl_registry *r=wl_display_get_registry(display);wl_registry_add_listener(r,&registry_listener,NULL);assert(wl_display_roundtrip(display)>=0);assert(compositor&&shm&&shell&&manager&&seat);
 text=zwp_text_input_manager_v3_get_text_input(manager,seat);zwp_text_input_v3_add_listener(text,&text_listener,NULL);surface=wl_compositor_create_surface(compositor);struct xdg_surface *xdg=xdg_wm_base_get_xdg_surface(shell,surface);xdg_surface_add_listener(xdg,&surface_listener,NULL);struct xdg_toplevel *top=xdg_surface_get_toplevel(xdg);xdg_toplevel_add_listener(top,&top_listener,NULL);wl_surface_commit(surface);while(!quit)assert(wl_display_dispatch(display)>=0);zwp_text_input_v3_destroy(text);xdg_toplevel_destroy(top);xdg_surface_destroy(xdg);wl_surface_destroy(surface);wl_display_disconnect(display);puts("v3-client-ok");return 0;}
