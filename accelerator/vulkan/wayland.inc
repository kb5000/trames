// SPDX-License-Identifier: GPL-3.0-or-later
// Resolve the original guest libwayland by its own handle. Vulkan applications
// may dlopen it RTLD_LOCAL, so weak/RTLD_DEFAULT lookup misses valid displays.
// A compute-only installation has no libwayland dependency. These are ordinary
// guest function pointers; native WSI only receives an authenticated fd/object ID.
#include <dlfcn.h>
struct wl_proxy;
struct wl_interface;
static struct {
    void *library;
    int (*fd)(struct wl_display *);
    int (*flush)(struct wl_display *);
    uint32_t (*id)(struct wl_proxy *);
    uint32_t (*version)(struct wl_proxy *);
    struct wl_proxy *(*marshal)(struct wl_proxy *,uint32_t,const struct wl_interface *,uint32_t,uint32_t,...);
    int (*listen)(struct wl_proxy *,void (**)(void),void *);
    void (*destroy)(struct wl_proxy *);
    const struct wl_interface *callback;
} wayland_api;
static pthread_once_t wayland_once=PTHREAD_ONCE_INIT;
static void wayland_load(void) {
    wayland_api.library=dlopen("libwayland-client.so.0",RTLD_LAZY|RTLD_LOCAL);
    if(!wayland_api.library)return;
    wayland_api.fd=dlsym(wayland_api.library,"wl_display_get_fd");
    wayland_api.flush=dlsym(wayland_api.library,"wl_display_flush");
    wayland_api.id=dlsym(wayland_api.library,"wl_proxy_get_id");
    wayland_api.version=dlsym(wayland_api.library,"wl_proxy_get_version");
    wayland_api.marshal=dlsym(wayland_api.library,"wl_proxy_marshal_flags");
    wayland_api.listen=dlsym(wayland_api.library,"wl_proxy_add_listener");
    wayland_api.destroy=dlsym(wayland_api.library,"wl_proxy_destroy");
    wayland_api.callback=dlsym(wayland_api.library,"wl_callback_interface");
}
// Guest-only bookkeeping never crosses the native capability boundary. The
// lock covers bounded metadata lookup; it does not serialize rendering or wait
// for a native fence. A sync callback wakes clients that read the Wayland socket
// between presents (including original vkcube), without inventing frame-done
// events or dispatching the application's event queue from inside Vulkan.
static pthread_mutex_t wayland_records_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {VkSurfaceKHR key;struct wl_display *display;struct wl_surface *surface;} wayland_surfaces[256];
static struct {VkSwapchainKHR key;VkSurfaceKHR surface;} wayland_chains[256];
static int wayland_register_surface(VkSurfaceKHR key,struct wl_display *display,struct wl_surface *surface) {
    int found=0;pthread_mutex_lock(&wayland_records_lock);
    for(unsigned j=0;j<256;j++)if(!wayland_surfaces[j].key){wayland_surfaces[j].key=key;wayland_surfaces[j].display=display;wayland_surfaces[j].surface=surface;found=1;break;}
    pthread_mutex_unlock(&wayland_records_lock);return found;
}
static void wayland_forget_surface(VkSurfaceKHR key) {
    pthread_mutex_lock(&wayland_records_lock);for(unsigned j=0;j<256;j++)if(wayland_surfaces[j].key==key)wayland_surfaces[j].key=0;pthread_mutex_unlock(&wayland_records_lock);
}
static int wayland_register_chain(VkSwapchainKHR key,VkSurfaceKHR surface) {
    int is_wayland=0,found=0;pthread_mutex_lock(&wayland_records_lock);
    for(unsigned j=0;j<256;j++)if(wayland_surfaces[j].key==surface)is_wayland=1;
    if(is_wayland)for(unsigned j=0;j<256;j++)if(!wayland_chains[j].key){wayland_chains[j].key=key;wayland_chains[j].surface=surface;found=1;break;}
    pthread_mutex_unlock(&wayland_records_lock);return !is_wayland || found;
}
static void wayland_forget_chain(VkSwapchainKHR key) {
    pthread_mutex_lock(&wayland_records_lock);for(unsigned j=0;j<256;j++)if(wayland_chains[j].key==key)wayland_chains[j].key=0;pthread_mutex_unlock(&wayland_records_lock);
}
static void wayland_sync_done(void *data,struct wl_proxy *callback,uint32_t serial) {(void)data;(void)serial;wayland_api.destroy(callback);}
static void (*wayland_sync_listener[])(void)={(void (*)(void))wayland_sync_done};
static void wayland_present(VkSwapchainKHR key) {
    struct wl_display *display=NULL;struct wl_surface *surface=NULL;VkSurfaceKHR id=0;
    pthread_mutex_lock(&wayland_records_lock);
    for(unsigned j=0;j<256;j++)if(wayland_chains[j].key==key)id=wayland_chains[j].surface;
    if(id)for(unsigned j=0;j<256;j++)if(wayland_surfaces[j].key==id){display=wayland_surfaces[j].display;surface=wayland_surfaces[j].surface;break;}
    pthread_mutex_unlock(&wayland_records_lock);
    if(!display || !surface)return;
    struct wl_proxy *proxy=(struct wl_proxy *)surface;
    wayland_api.marshal(proxy,6,NULL,wayland_api.version(proxy),0);
    struct wl_proxy *sync=wayland_api.marshal((struct wl_proxy *)display,0,wayland_api.callback,1,0,NULL);
    if(sync)wayland_api.listen(sync,wayland_sync_listener,NULL);
    wayland_api.flush(display);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateWaylandSurfaceKHR(VkInstance i,const VkWaylandSurfaceCreateInfoKHR *info,const VkAllocationCallbacks *allocator,VkSurfaceKHR *out) {
    CHECK(info,VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR);ALLOCATOR(allocator);pthread_once(&wayland_once,wayland_load);
    if(info->flags || !info->display || !info->surface || !wayland_api.fd || !wayland_api.flush || !wayland_api.id || !wayland_api.version || !wayland_api.marshal || !wayland_api.listen || !wayland_api.destroy || !wayland_api.callback)return VK_ERROR_SURFACE_LOST_KHR;
    int fd=wayland_api.fd(info->display);uint32_t id=wayland_api.id((struct wl_proxy *)info->surface);
    if(fd<0 || !id)return VK_ERROR_SURFACE_LOST_KHR;
    uint64_t began=now_ns();
    for(;;){
        if(wayland_api.flush(info->display)<0 && errno!=EAGAIN && errno!=EINTR)return VK_ERROR_SURFACE_LOST_KHR;
        VkResult r=CALL(110,KEY(i),(uint64_t)fd,id,PTR(out));
        if(r==VK_SUCCESS && !wayland_register_surface(*out,info->display,info->surface)){CALL(82,KEY(i),(uint64_t)*out);return VK_ERROR_OUT_OF_HOST_MEMORY;}
        if(r!=VK_NOT_READY)return r;
        // The native server owns its own thread. Yield while it processes the
        // flushed wl_surface creation; do not dispatch the application's queue.
        if(now_ns()-began>5000000000ULL)return VK_ERROR_SURFACE_LOST_KHR;
        struct timespec pause={0,1000000};nanosleep(&pause,NULL);
    }
}
VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice p,uint32_t family,struct wl_display *display) {
    pthread_once(&wayland_once,wayland_load);if(!display || !wayland_api.fd)return VK_FALSE;
    int fd=wayland_api.fd(display);if(fd<0)return VK_FALSE;VkBool32 supported=VK_FALSE;
    return CALL(111,KEY(p),(uint64_t)fd,family,PTR(&supported))==VK_SUCCESS ? supported : VK_FALSE;
}
