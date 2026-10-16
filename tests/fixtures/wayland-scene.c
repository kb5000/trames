// SPDX-License-Identifier: GPL-3.0-or-later
// Independent original C ABI peer: subsurface atomicity, popup placement and grab.
#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_subcompositor *subcompositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct xdg_wm_base *shell;
static struct wl_surface *root,*child,*popup;
static struct wl_subsurface *sub;
static struct xdg_surface *root_xdg,*popup_xdg;
static struct xdg_popup *popup_role;
static struct wl_surface *pointer_surface;
static int quit;
static int shared_fd(void) {
#ifdef __linux__
 return memfd_create("wayland-scene",MFD_CLOEXEC);
#else
 char path[]="/tmp/trames-wayland-scene-XXXXXX";int fd=mkstemp(path);if(fd>=0){unlink(path);fcntl(fd,F_SETFD,FD_CLOEXEC);}return fd;
#endif
}
static void paint(struct wl_surface *s,int w,int h,uint32_t color) {
 size_t size=(size_t)w*h*4;int fd=shared_fd();assert(fd>=0&&ftruncate(fd,(off_t)size)==0);
 uint32_t *pixels=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);assert(pixels!=MAP_FAILED);
 for(size_t i=0;i<size/4;i++)pixels[i]=color;
 struct wl_shm_pool *pool=wl_shm_create_pool(shm,fd,(int)size);
 struct wl_buffer *buffer=wl_shm_pool_create_buffer(pool,0,w,h,w*4,WL_SHM_FORMAT_ARGB8888);
 wl_shm_pool_destroy(pool);wl_surface_attach(s,buffer,0,0);wl_surface_damage(s,0,0,w,h);wl_surface_commit(s);
 wl_buffer_destroy(buffer);munmap(pixels,size);close(fd);
}
static const char *name(struct wl_surface *s){return s==root?"root":s==child?"child":s==popup?"popup":"none";}
static void sync_done(void *data,struct wl_callback *cb,uint32_t serial){(void)serial;wl_callback_destroy(cb);printf("%s\n",(const char *)data);}
static const struct wl_callback_listener sync_listener={sync_done};
static void notify(const char *text){struct wl_callback *cb=wl_display_sync(display);wl_callback_add_listener(cb,&sync_listener,(void *)text);wl_display_flush(display);}
static void root_configured(void *data,struct xdg_surface *s,uint32_t serial){(void)data;xdg_surface_ack_configure(s,serial);xdg_surface_set_window_geometry(s,10,10,200,120);paint(root,240,160,0xff112233);}
static const struct xdg_surface_listener root_listener={root_configured};
static void closed(void *data,struct xdg_toplevel *top){(void)data;(void)top;quit=1;}
static void top_configured(void *data,struct xdg_toplevel *top,int32_t w,int32_t h,struct wl_array *states){(void)data;(void)top;(void)w;(void)h;(void)states;}
static const struct xdg_toplevel_listener top_listener={.configure=top_configured,.close=closed};
static int popup_w=80,popup_h=60;
static void popup_configured(void *data,struct xdg_popup *role,int32_t x,int32_t y,int32_t w,int32_t h){(void)data;(void)role;printf("popup-configure=%d,%d %dx%d\n",x,y,w,h);popup_w=w;popup_h=h;}
static void popup_done(void *data,struct xdg_popup *role){(void)data;(void)role;puts("popup-dismissed");}
static void repositioned(void *data,struct xdg_popup *role,uint32_t token){(void)data;(void)role;printf("repositioned=%u\n",token);}
static const struct xdg_popup_listener popup_listener={popup_configured,popup_done,repositioned};
static void popup_ack(void *data,struct xdg_surface *s,uint32_t serial){(void)data;xdg_surface_ack_configure(s,serial);paint(popup,popup_w,popup_h,0xffaabbcc);}
static const struct xdg_surface_listener popup_surface_listener={popup_ack};
static void keyboard_keymap(void *d,struct wl_keyboard *k,uint32_t format,int32_t fd,uint32_t size){(void)d;(void)k;(void)format;(void)size;close(fd);}
static void keyboard_enter(void *d,struct wl_keyboard *k,uint32_t serial,struct wl_surface *s,struct wl_array *keys){(void)d;(void)k;(void)serial;(void)s;(void)keys;}
static void keyboard_leave(void *d,struct wl_keyboard *k,uint32_t serial,struct wl_surface *s){(void)d;(void)k;(void)serial;(void)s;}
static void keyboard_modifiers(void *d,struct wl_keyboard *k,uint32_t serial,uint32_t a,uint32_t b,uint32_t c,uint32_t group){(void)d;(void)k;(void)serial;(void)a;(void)b;(void)c;(void)group;}
static void keyboard_repeat(void *d,struct wl_keyboard *k,int32_t rate,int32_t delay){(void)d;(void)k;(void)rate;(void)delay;}
static void keyboard_key(void *d,struct wl_keyboard *k,uint32_t serial,uint32_t time,uint32_t key,uint32_t state){
 (void)d;(void)k;(void)serial;(void)time;if(!state)return;
 if(key==30){wl_subsurface_set_position(sub,70,50);paint(child,32,24,0xff00cc00);notify("staged");}
 if(key==31){wl_surface_commit(root);notify("applied");}
 if(key==33){
   xdg_popup_destroy(popup_role);xdg_surface_destroy(popup_xdg);wl_surface_destroy(popup);
   popup=wl_compositor_create_surface(compositor);popup_xdg=xdg_wm_base_get_xdg_surface(shell,popup);xdg_surface_add_listener(popup_xdg,&popup_surface_listener,NULL);
   struct xdg_positioner *p=xdg_wm_base_create_positioner(shell);xdg_positioner_set_size(p,80,60);xdg_positioner_set_anchor_rect(p,100,80,10,10);xdg_positioner_set_anchor(p,XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);xdg_positioner_set_gravity(p,XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
   popup_role=xdg_surface_get_popup(popup_xdg,root_xdg,p);xdg_popup_add_listener(popup_role,&popup_listener,NULL);xdg_popup_grab(popup_role,seat,serial);xdg_positioner_destroy(p);wl_surface_commit(popup);notify("grabbed");
 }
 if(key==32){struct xdg_positioner *p=xdg_wm_base_create_positioner(shell);xdg_positioner_set_size(p,60,40);xdg_positioner_set_anchor_rect(p,5,5,10,10);xdg_positioner_set_anchor(p,XDG_POSITIONER_ANCHOR_TOP_LEFT);xdg_positioner_set_gravity(p,XDG_POSITIONER_GRAVITY_TOP_LEFT);xdg_positioner_set_constraint_adjustment(p,XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X|XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);xdg_popup_reposition(popup_role,p,17);xdg_positioner_destroy(p);notify("reposition-sent");}
}
static const struct wl_keyboard_listener keyboard_listener={keyboard_keymap,keyboard_enter,keyboard_leave,keyboard_key,keyboard_modifiers,keyboard_repeat};
static void pointer_enter(void *d,struct wl_pointer *p,uint32_t serial,struct wl_surface *s,wl_fixed_t x,wl_fixed_t y){(void)d;(void)p;(void)serial;(void)x;(void)y;pointer_surface=s;printf("pointer=%s\n",name(s));}
static void pointer_leave(void *d,struct wl_pointer *p,uint32_t serial,struct wl_surface *s){(void)d;(void)p;(void)serial;(void)s;pointer_surface=NULL;}
static void pointer_motion(void *d,struct wl_pointer *p,uint32_t time,wl_fixed_t x,wl_fixed_t y){(void)d;(void)p;(void)time;printf("motion=%s %.0f,%.0f\n",name(pointer_surface),wl_fixed_to_double(x),wl_fixed_to_double(y));}
static void pointer_button(void *d,struct wl_pointer *p,uint32_t serial,uint32_t time,uint32_t button,uint32_t state){(void)d;(void)p;(void)serial;(void)time;(void)button;printf("button=%s %u\n",name(pointer_surface),state);}
static void pointer_frame(void *d,struct wl_pointer *p){(void)d;(void)p;}
static const struct wl_pointer_listener pointer_listener={.enter=pointer_enter,.leave=pointer_leave,.motion=pointer_motion,.button=pointer_button,.frame=pointer_frame};
static void global(void *data,struct wl_registry *registry,uint32_t id,const char *interface,uint32_t version){(void)data;
 if(!strcmp(interface,"wl_compositor"))compositor=wl_registry_bind(registry,id,&wl_compositor_interface,version<4?version:4);
 if(!strcmp(interface,"wl_shm"))shm=wl_registry_bind(registry,id,&wl_shm_interface,1);
 if(!strcmp(interface,"wl_subcompositor"))subcompositor=wl_registry_bind(registry,id,&wl_subcompositor_interface,1);
 if(!strcmp(interface,"xdg_wm_base"))shell=wl_registry_bind(registry,id,&xdg_wm_base_interface,3);
 if(!strcmp(interface,"wl_seat")){seat=wl_registry_bind(registry,id,&wl_seat_interface,7);struct wl_keyboard *keyboard=wl_seat_get_keyboard(seat);wl_keyboard_add_listener(keyboard,&keyboard_listener,NULL);struct wl_pointer *pointer=wl_seat_get_pointer(seat);wl_pointer_add_listener(pointer,&pointer_listener,NULL);}
}
static void global_remove(void *d,struct wl_registry *r,uint32_t id){(void)d;(void)r;(void)id;}
static const struct wl_registry_listener registry_listener={global,global_remove};
int main(void){
 setvbuf(stdout,NULL,_IOLBF,0);display=wl_display_connect(NULL);assert(display);
 struct wl_registry *registry=wl_display_get_registry(display);wl_registry_add_listener(registry,&registry_listener,NULL);assert(wl_display_roundtrip(display)>=0);assert(compositor&&subcompositor&&shm&&shell);
 root=wl_compositor_create_surface(compositor);root_xdg=xdg_wm_base_get_xdg_surface(shell,root);xdg_surface_add_listener(root_xdg,&root_listener,NULL);struct xdg_toplevel *top=xdg_surface_get_toplevel(root_xdg);xdg_toplevel_add_listener(top,&top_listener,NULL);xdg_toplevel_set_title(top,"scene-peer");wl_surface_commit(root);assert(wl_display_roundtrip(display)>=0);
 child=wl_compositor_create_surface(compositor);sub=wl_subcompositor_get_subsurface(subcompositor,child,root);wl_subsurface_set_position(sub,30,40);
 struct wl_region *region=wl_compositor_create_region(compositor);wl_region_add(region,4,4,10,10);wl_surface_set_input_region(child,region);wl_region_destroy(region);paint(child,32,24,0xffcc0000);wl_surface_commit(root);
 popup=wl_compositor_create_surface(compositor);popup_xdg=xdg_wm_base_get_xdg_surface(shell,popup);xdg_surface_add_listener(popup_xdg,&popup_surface_listener,NULL);
 struct xdg_positioner *position=xdg_wm_base_create_positioner(shell);xdg_positioner_set_size(position,80,60);xdg_positioner_set_anchor_rect(position,190,100,10,10);xdg_positioner_set_anchor(position,XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);xdg_positioner_set_gravity(position,XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);xdg_positioner_set_offset(position,3,5);
 popup_role=xdg_surface_get_popup(popup_xdg,root_xdg,position);xdg_popup_add_listener(popup_role,&popup_listener,NULL);xdg_positioner_destroy(position);wl_surface_commit(popup);
 while(!quit)assert(wl_display_dispatch(display)>=0);
 xdg_popup_destroy(popup_role);xdg_surface_destroy(popup_xdg);wl_surface_destroy(popup);wl_subsurface_destroy(sub);wl_surface_destroy(child);xdg_toplevel_destroy(top);xdg_surface_destroy(root_xdg);wl_surface_destroy(root);wl_display_disconnect(display);puts("scene-client-ok");return 0;
}
