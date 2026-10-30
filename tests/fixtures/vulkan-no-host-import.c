// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only native-loader filter: force existing shadow mappings by hiding one
// optional extension. Every allocation, shader, command and GPU result is native.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
static pthread_once_t once=PTHREAD_ONCE_INIT;
static PFN_vkGetInstanceProcAddr native_get;
static PFN_vkEnumerateDeviceExtensionProperties native_extensions;
static void initialize(void) {
    void *library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);
    if(!library)abort();
    native_get=(PFN_vkGetInstanceProcAddr)dlsym(library,"vkGetInstanceProcAddr");
    native_extensions=(PFN_vkEnumerateDeviceExtensionProperties)dlsym(library,"vkEnumerateDeviceExtensionProperties");
    if(!native_get || !native_extensions)abort();
}
static VKAPI_ATTR VkResult VKAPI_CALL extensions(VkPhysicalDevice p,const char *layer,uint32_t *count,VkExtensionProperties *out) {
    uint32_t n=0;VkResult r=native_extensions(p,layer,&n,NULL);if(r!=VK_SUCCESS)return r;
    VkExtensionProperties *all=calloc(n,sizeof(*all));if(!all && n)return VK_ERROR_OUT_OF_HOST_MEMORY;
    r=native_extensions(p,layer,&n,all);if(r!=VK_SUCCESS){free(all);return r;}
    uint32_t capacity=out?*count:0,kept=0;
    for(uint32_t i=0;i<n;i++)if(strcmp(all[i].extensionName,"VK_EXT_external_memory_host")){
        if(out && kept<capacity)out[kept]=all[i];kept++;
    }
    free(all);*count=out && capacity<kept?capacity:kept;
    return out && capacity<kept?VK_INCOMPLETE:VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char *name) {
    pthread_once(&once,initialize);
    if(name && !strcmp(name,"vkEnumerateDeviceExtensionProperties"))return (PFN_vkVoidFunction)extensions;
    return native_get(instance,name);
}
