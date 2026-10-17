// SPDX-License-Identifier: GPL-3.0-or-later
// Independent original libwayland peer: Weston-style one text object per widget.
#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "text-input-v1-client-protocol.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
struct entry { struct zwp_text_input_v1 *text; int active,enters,leaves; };
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct xdg_wm_base *shell;
static struct zwp_text_input_manager_v1 *manager;
static struct wl_surface *surface;
static struct entry entries[2];
static void enter(void *data,struct zwp_text_input_v1 *t,struct wl_surface *s) {
 struct entry *e=data;assert(s==surface);e->active++;e->enters++;assert(e->active==1);
 zwp_text_input_v1_set_surrounding_text(t,"",0,0);zwp_text_input_v1_commit_state(t,(uint32_t)e->enters);
}
static void leave(void *data,struct zwp_text_input_v1 *t) { (void)t;struct entry *e=data;e->active--;e->leaves++;assert(e->active==0); }
static void modifiers(void *data,struct zwp_text_input_v1 *t,struct wl_array *map) { (void)data;(void)t;(void)map; }
static const struct zwp_text_input_v1_listener text_listener={.enter=enter,.leave=leave,.modifiers_map=modifiers};
static void configure(void *d,struct xdg_surface *s,uint32_t serial) {
 (void)d;xdg_surface_ack_configure(s,serial);
#ifdef __linux__
 int fd=memfd_create("text-v1",MFD_CLOEXEC);
#else
 char name[]="/tmp/trames-text-v1-XXXXXX";int fd=mkstemp(name);assert(fd>=0);unlink(name);fcntl(fd,F_SETFD,FD_CLOEXEC);
#endif
 assert(fd>=0&&ftruncate(fd,64*64*4)==0);struct wl_shm_pool *pool=wl_shm_create_pool(shm,fd,64*64*4);
 struct wl_buffer *b=wl_shm_pool_create_buffer(pool,0,64,64,64*4,WL_SHM_FORMAT_XRGB8888);
 wl_shm_pool_destroy(pool);wl_surface_attach(surface,b,0,0);wl_surface_commit(surface);wl_buffer_destroy(b);close(fd);
}
static const struct xdg_surface_listener surface_listener={configure};
static void global(void *d,struct wl_registry *r,uint32_t id,const char *name,uint32_t version) {
 (void)d;if(!strcmp(name,"wl_compositor"))compositor=wl_registry_bind(r,id,&wl_compositor_interface,version<4?version:4);
 if(!strcmp(name,"wl_shm"))shm=wl_registry_bind(r,id,&wl_shm_interface,1);
 if(!strcmp(name,"wl_seat"))seat=wl_registry_bind(r,id,&wl_seat_interface,7);
 if(!strcmp(name,"xdg_wm_base"))shell=wl_registry_bind(r,id,&xdg_wm_base_interface,3);
 if(!strcmp(name,"zwp_text_input_manager_v1"))manager=wl_registry_bind(r,id,&zwp_text_input_manager_v1_interface,1);
}
static void removed(void *d,struct wl_registry *r,uint32_t id) {(void)d;(void)r;(void)id;}
static const struct wl_registry_listener registry_listener={global,removed};
int main(void) {
 setvbuf(stdout,NULL,_IOLBF,0);display=wl_display_connect(NULL);assert(display);
 struct wl_registry *registry=wl_display_get_registry(display);wl_registry_add_listener(registry,&registry_listener,NULL);assert(wl_display_roundtrip(display)>=0);
 assert(compositor&&shm&&shell&&seat&&manager);surface=wl_compositor_create_surface(compositor);
 struct xdg_surface *xdg=xdg_wm_base_get_xdg_surface(shell,surface);xdg_surface_add_listener(xdg,&surface_listener,NULL);
 struct xdg_toplevel *top=xdg_surface_get_toplevel(xdg);wl_surface_commit(surface);
 for(int i=0;i<2;i++){entries[i].text=zwp_text_input_manager_v1_create_text_input(manager);zwp_text_input_v1_add_listener(entries[i].text,&text_listener,&entries[i]);}
 zwp_text_input_v1_activate(entries[0].text,seat,surface);
 while(!entries[0].active)assert(wl_display_dispatch(display)>=0);
 for(int phase=0;phase<12;phase++) {
  int target=phase%2;zwp_text_input_v1_activate(entries[target].text,seat,surface);assert(wl_display_roundtrip(display)>=0);
  assert(entries[target].active==1&&entries[1-target].active==0);
  int enters=entries[target].enters;
  zwp_text_input_v1_activate(entries[target].text,seat,surface);assert(wl_display_roundtrip(display)>=0);assert(entries[target].enters==enters);
 }
 for(int i=0;i<2;i++){zwp_text_input_v1_deactivate(entries[i].text,seat);zwp_text_input_v1_deactivate(entries[i].text,seat);}
 assert(wl_display_roundtrip(display)>=0);for(int i=0;i<2;i++)assert(entries[i].active==0&&entries[i].enters==entries[i].leaves);
 xdg_toplevel_destroy(top);xdg_surface_destroy(xdg);wl_surface_destroy(surface);wl_display_disconnect(display);
 puts("v1-multiwidget-ok balanced-enter-leave repeated-activation switching-back");return 0;
}
