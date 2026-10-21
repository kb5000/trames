// SPDX-License-Identifier: GPL-3.0-or-later
// Standard Linux ICD frontend. Only fixed-size, little-endian wire records
// cross NativeCall; every returned function pointer is ordinary guest ELF code.
#define _GNU_SOURCE
#define VK_ENABLE_BETA_EXTENSIONS
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <time.h>
#include <pthread.h>

struct result { uint64_t value, fallback; };
extern struct result trames_vulkan_call(uint64_t, const uint64_t *, size_t);
typedef struct memory_record { uint64_t key,size; void *base,*mapped; size_t length; struct memory_record *next; } Memory;
typedef struct descriptor_template DescriptorTemplate;
static void free_templates(DescriptorTemplate *head);
typedef struct dispatch {
    uintptr_t magic; uint64_t key;
    struct dispatch *device, *physical, *queue, *commands, *next;
    uint64_t parent,map_alignment,capabilities[2],enabled; uint32_t family;
    pthread_mutex_t lock; Memory *memories; DescriptorTemplate *templates;
} Dispatch;
#define D(h) ((Dispatch *)(h))
#define KEY(h) (D(h)->key)
#define DEVICE(h) (D(h)->device)
#define DEVKEY(h) (DEVICE(h)->key)
static VkResult call(unsigned op, uint64_t a[16],const char *name) {
    if (getauxval(0x54520008UL)!=0x10006UL) return VK_ERROR_INCOMPATIBLE_DRIVER;
    VkResult r=(VkResult)(int32_t)trames_vulkan_call(op,a,128).value;
    if(r<0 && getenv("TRAMES_VULKAN_TRACE"))fprintf(stderr,"Trames Vulkan: %s op=%u result=%d args=[%llx,%llx,%llx,%llx,%llx,%llx]\n",name,op,r,(unsigned long long)a[0],(unsigned long long)a[1],(unsigned long long)a[2],(unsigned long long)a[3],(unsigned long long)a[4],(unsigned long long)a[5]);
    return r;
}
#define CALL(op, ...) call(op,(uint64_t[16]){__VA_ARGS__},__func__)
#define PTR(p) ((uint64_t)(uintptr_t)(p))
static int supported_next(const void *next) {
    const VkBaseInStructure *p=next;
    for(unsigned i=0;p && i<16;i++,p=p->pNext)
        if(p->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && p->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)return 0;
    return !p;
}
#define VALID(p, st) ((p) && (p)->sType==(st) && supported_next((p)->pNext))
#define CHECK(p, st) do { if (!VALID(p,st)) return VK_ERROR_FEATURE_NOT_PRESENT; } while (0)
#define ALLOCATOR(p) do { if (p) return VK_ERROR_FEATURE_NOT_PRESENT; } while (0)
static void check_result(VkResult r,const char *name) { if (r!=VK_SUCCESS) { fprintf(stderr,"Trames Vulkan: %s failed (%d)\n",name,r); abort(); } }
#define checked(r) check_result((r),__func__)
static Dispatch *dispatch(uint64_t key, Dispatch *device) { Dispatch *d=calloc(1,sizeof(*d)); if (d) { set_loader_magic_value(d); d->key=key;d->device=device;pthread_mutex_init(&d->lock,NULL); } return d; }
static void free_dispatch(Dispatch *d) {pthread_mutex_destroy(&d->lock);free(d);}
static Memory *memory(Dispatch *d,VkDeviceMemory key) {for (Memory *m=d->memories;m;m=m->next) if(m->key==(uint64_t)key)return m;return NULL;}

static uint64_t now_ns(void);
static int display_info(uint64_t key,uint64_t out[3]) { return CALL(key?80:79,key?key:PTR(out),key?PTR(out):0)==VK_SUCCESS && out[0]; }
static int wayland_available(void) {uint64_t available=0;return CALL(109,PTR(&available))==VK_SUCCESS && available;}
static VkResult display_extensions(const char *layer,uint32_t *count,VkExtensionProperties *out,int device,uint64_t caps) {
    if(layer)return VK_ERROR_LAYER_NOT_PRESENT;
    uint64_t info[3]={0};int fixed=display_info(0,info),wayland=wayland_available();
    const char *names[20];uint32_t n=0;
    if(device){
        if(fixed || wayland)names[n++]=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        names[n++]=VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
        names[n++]=VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME;
        if(caps&1)names[n++]=VK_KHR_MAINTENANCE_1_EXTENSION_NAME;
        if(caps&2)names[n++]=VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
        if(caps&4)names[n++]=VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME;
        if(caps&8)names[n++]=VK_KHR_MAINTENANCE_2_EXTENSION_NAME;
        if(caps&16)names[n++]=VK_KHR_MULTIVIEW_EXTENSION_NAME;
        if(caps&32)names[n++]=VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME;
        if(caps&64)names[n++]=VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME;
        if(caps&128)names[n++]=VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME;
        if(caps&256)names[n++]=VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME;
        if(caps&512)names[n++]=VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
        if(caps&1024)names[n++]=VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
        if(caps&2048)names[n++]=VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME;
    }
    else {
        if(fixed || wayland)names[n++]=VK_KHR_SURFACE_EXTENSION_NAME;
        if(fixed)names[n++]=VK_KHR_DISPLAY_EXTENSION_NAME;
        if(wayland)names[n++]=VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
        names[n++]=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    }
    if(!out){*count=n;return VK_SUCCESS;}uint32_t capacity=*count;*count=capacity<n?capacity:n;
    for(uint32_t i=0;i<*count;i++){memset(out+i,0,sizeof(*out));strcpy(out[i].extensionName,names[i]);out[i].specVersion=!strcmp(names[i],VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME)?3:(!strcmp(names[i],VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) || !strcmp(names[i],VK_KHR_MAINTENANCE_1_EXTENSION_NAME) || !strcmp(names[i],VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))?2:1;}
    return capacity<n?VK_INCOMPLETE:VK_SUCCESS;
}
static int extensions(uint32_t count,const char *const *names,int device) {
    unsigned bits=0;for(uint32_t i=0;i<count;i++){
        unsigned bit=0;if(!strcmp(names[i],device?VK_KHR_SWAPCHAIN_EXTENSION_NAME:VK_KHR_SURFACE_EXTENSION_NAME))bit=1;
        else if(!device && !strcmp(names[i],VK_KHR_DISPLAY_EXTENSION_NAME))bit=2;
        else if(!device && !strcmp(names[i],VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME))bit=4;
        else if(!device && !strcmp(names[i],VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))bit=8;
        else if(device && !strcmp(names[i],VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))bit=8;
        else if(device && !strcmp(names[i],VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME))bit=16;
        if(device && !strcmp(names[i],VK_KHR_MAINTENANCE_1_EXTENSION_NAME))bit=32;
        else if(device && !strcmp(names[i],VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))bit=64;
        else if(device && !strcmp(names[i],VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))bit=128;
        else if(device && !strcmp(names[i],VK_KHR_MAINTENANCE_2_EXTENSION_NAME))bit=256;
        else if(device && !strcmp(names[i],VK_KHR_MULTIVIEW_EXTENSION_NAME))bit=512;
        else if(device && !strcmp(names[i],VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME))bit=1024;
        else if(device && !strcmp(names[i],VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))bit=2048;
        else if(device && !strcmp(names[i],VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME))bit=4096;
        else if(device && !strcmp(names[i],VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME))bit=8192;
        else if(device && !strcmp(names[i],VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))bit=16384;
        else if(device && !strcmp(names[i],VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME))bit=32768;
        else if(device && !strcmp(names[i],VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME))bit=65536;
        // Original loaders may append an already enabled promoted extension.
        if(!bit)return -1;bits|=bit;
    }
    if(device)return (bits&1)!=0;
    // These selected native render routes have different final image layouts.
    // A single instance uses one route; separate instances may use either.
    if((bits&6)==6)return -1;
    if(bits&4)return 2;if(bits&2)return 1;
    return bits&1 ? (wayland_available()?2:1) : 0;
}

static int has_extension(uint32_t count,const char *const *names,const char *name) {
    for(uint32_t i=0;i<count;i++)if(!strcmp(names[i],name))return 1;return 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *info,const VkAllocationCallbacks *allocator,VkInstance *out) {
    CHECK(info,VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);ALLOCATOR(allocator);
    int wsi=extensions(info->enabledExtensionCount,info->ppEnabledExtensionNames,0);if(wsi<0 || info->enabledLayerCount)return VK_ERROR_EXTENSION_NOT_PRESENT;
    // Instance 1.1 implementations must accept applications targeting newer
    // versions. Physical-device queries still report the selected 1.0 profile.
    if(info->flags)return VK_ERROR_INCOMPATIBLE_DRIVER;
    if(info->pApplicationInfo){
        const VkApplicationInfo *app=info->pApplicationInfo;
        if(!VALID(app,VK_STRUCTURE_TYPE_APPLICATION_INFO) || (app->apiVersion &&
           (VK_API_VERSION_VARIANT(app->apiVersion) || VK_API_VERSION_MAJOR(app->apiVersion)!=1)))return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    Dispatch *d=dispatch(0,NULL);if(!d)return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkResult r=CALL(1,PTR(&d->key),wsi);if(r!=VK_SUCCESS){free_dispatch(d);return r;}*out=(VkInstance)d;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance,const VkAllocationCallbacks *allocator) { if (!instance)return;checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(2,KEY(instance)));if(D(instance)->physical)free_dispatch(D(instance)->physical);free_dispatch(D(instance)); }
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *out) {*out=VK_API_VERSION_1_1;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *layer,uint32_t *count,VkExtensionProperties *out) {return display_extensions(layer,count,out,0,0);}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *count,VkLayerProperties *out) {(void)out;*count=0;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physical,const char *layer,uint32_t *count,VkExtensionProperties *out) {return display_extensions(layer,count,out,1,D(physical)->capabilities[0]);}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physical,uint32_t *count,VkLayerProperties *out) {(void)physical;(void)out;*count=0;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance,uint32_t *count,VkPhysicalDevice *out) {
    uint32_t n=1;uint64_t key=0;VkResult r=CALL(3,KEY(instance),PTR(&n),PTR(&key));if(r!=VK_SUCCESS)return r;
    if (!out){*count=1;return VK_SUCCESS;}if (!*count)return VK_INCOMPLETE;
    Dispatch *i=D(instance);if(!i->physical)i->physical=dispatch(key,NULL);if(!i->physical)return VK_ERROR_OUT_OF_HOST_MEMORY;
    checked(CALL(115,key,PTR(i->physical->capabilities)));
    *count=1;out[0]=(VkPhysicalDevice)i->physical;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice p,VkPhysicalDeviceProperties *out) {checked(CALL(4,KEY(p),PTR(out)));}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *out) {checked(CALL(5,KEY(p),PTR(out)));}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice p,uint32_t *count,VkQueueFamilyProperties *out) {checked(CALL(6,KEY(p),PTR(count),PTR(out)));}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice p,VkPhysicalDeviceFeatures *out) {checked(CALL(7,KEY(p),PTR(out)));}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice p,VkFormat format,VkFormatProperties *out) {checked(CALL(58,KEY(p),format,PTR(out)));}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice p,VkFormat format,VkImageType type,VkImageTiling tiling,VkImageUsageFlags usage,VkImageCreateFlags flags,VkImageFormatProperties *out) {return CALL(59,KEY(p),format,type,tiling,usage,flags,PTR(out));}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice p,VkFormat format,VkImageType type,VkSampleCountFlagBits samples,VkImageUsageFlags usage,VkImageTiling tiling,uint32_t *count,VkSparseImageFormatProperties *out) {(void)p;(void)format;(void)type;(void)samples;(void)usage;(void)tiling;(void)out;*count=0;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice p,const VkDeviceCreateInfo *info,const VkAllocationCallbacks *allocator,VkDevice *out) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;ALLOCATOR(allocator);
    const VkPhysicalDeviceFeatures *features=info->pEnabledFeatures;
    const VkBaseInStructure *next=info->pNext;unsigned chain=0;int timeline=0,seen_timeline=0,imageless=0,seen_imageless=0,seen_multiview=0,scalar=0,seen_scalar=0,seen_portability=0,feedback=0,seen_feedback=0,divisor=0,zero_divisor=0,seen_divisor=0;
    while(next && chain++<16){
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2){
            if(features)return VK_ERROR_FEATURE_NOT_PRESENT;
            features=&((const VkPhysicalDeviceFeatures2 *)next)->features;
        }else if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES){
            if(seen_timeline++)return VK_ERROR_FEATURE_NOT_PRESENT;
            timeline=((const VkPhysicalDeviceTimelineSemaphoreFeatures *)next)->timelineSemaphore;
            if(timeline!=VK_FALSE && timeline!=VK_TRUE)return VK_ERROR_FEATURE_NOT_PRESENT;
        }else if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES){
            if(seen_imageless++)return VK_ERROR_FEATURE_NOT_PRESENT;
            imageless=((const VkPhysicalDeviceImagelessFramebufferFeatures *)next)->imagelessFramebuffer;
            if(imageless!=VK_FALSE && imageless!=VK_TRUE)return VK_ERROR_FEATURE_NOT_PRESENT;
        }else if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR){
            if(seen_portability++ || !(D(p)->capabilities[0]&512))return VK_ERROR_FEATURE_NOT_PRESENT;
            uint32_t supported[16];VkResult r=CALL(133,KEY(p),PTR(supported));if(r!=VK_SUCCESS)return r;
            const VkPhysicalDevicePortabilitySubsetFeaturesKHR *v=(const void *)next;
            const VkBool32 *values=&v->constantAlphaColorBlendFactors;
            for(unsigned i=0;i<15;i++)if(values[i]>VK_TRUE || (values[i] && !supported[i]))return VK_ERROR_FEATURE_NOT_PRESENT;
        }else if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES){
            if(seen_scalar++)return VK_ERROR_FEATURE_NOT_PRESENT;
            scalar=((const VkPhysicalDeviceScalarBlockLayoutFeatures *)next)->scalarBlockLayout;
            if(scalar!=VK_FALSE && scalar!=VK_TRUE)return VK_ERROR_FEATURE_NOT_PRESENT;
        }else if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT){
            const VkPhysicalDeviceTransformFeedbackFeaturesEXT *v=(const void *)next;
            if(seen_feedback++ || v->geometryStreams || v->transformFeedback>VK_TRUE)return VK_ERROR_FEATURE_NOT_PRESENT;
            feedback=v->transformFeedback;
        }else if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT){
            const VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *v=(const void *)next;
            if(seen_divisor++ || v->vertexAttributeInstanceRateDivisor>VK_TRUE || v->vertexAttributeInstanceRateZeroDivisor>VK_TRUE)return VK_ERROR_FEATURE_NOT_PRESENT;
            divisor=v->vertexAttributeInstanceRateDivisor;zero_divisor=v->vertexAttributeInstanceRateZeroDivisor;
        }else if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES){
            const VkPhysicalDeviceMultiviewFeatures *v=(const void *)next;
            if(seen_multiview++ || v->multiview || v->multiviewGeometryShader || v->multiviewTessellationShader)return VK_ERROR_FEATURE_NOT_PRESENT;
        }else if(next->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;
        next=next->pNext;
    }
    if(next)return VK_ERROR_FEATURE_NOT_PRESENT;
    int wsi=extensions(info->enabledExtensionCount,info->ppEnabledExtensionNames,1);if(info->flags || info->queueCreateInfoCount!=1 || wsi<0)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkDeviceQueueCreateInfo *q=info->pQueueCreateInfos;CHECK(q,VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    if(q->flags || q->queueCount!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t feature_bits=0;
    if(features){
        const VkBool32 *v=(const VkBool32 *)features;
        const size_t offsets[]={offsetof(VkPhysicalDeviceFeatures,samplerAnisotropy),offsetof(VkPhysicalDeviceFeatures,logicOp),offsetof(VkPhysicalDeviceFeatures,fillModeNonSolid),offsetof(VkPhysicalDeviceFeatures,shaderClipDistance),offsetof(VkPhysicalDeviceFeatures,independentBlend),offsetof(VkPhysicalDeviceFeatures,imageCubeArray),offsetof(VkPhysicalDeviceFeatures,sampleRateShading)};
        for(size_t j=0;j<sizeof(VkPhysicalDeviceFeatures)/sizeof(VkBool32);j++)if(v[j]){
            int found=0;if(v[j]!=VK_TRUE)return VK_ERROR_FEATURE_NOT_PRESENT;
            for(unsigned k=0;k<sizeof(offsets)/sizeof(offsets[0]);k++)if(j==offsets[k]/sizeof(VkBool32)){feature_bits|=1UL<<k;found=1;}
            if(!found)return VK_ERROR_FEATURE_NOT_PRESENT;
        }
    }
    Dispatch *d=dispatch(0,NULL);if(!d)return VK_ERROR_OUT_OF_HOST_MEMORY;d->device=d;d->family=q->queueFamilyIndex;
    uint64_t enabled=(has_extension(info->enabledExtensionCount,info->ppEnabledExtensionNames,VK_KHR_MAINTENANCE_1_EXTENSION_NAME)?1UL:0)
        |(has_extension(info->enabledExtensionCount,info->ppEnabledExtensionNames,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)?8UL:0)
        |(has_extension(info->enabledExtensionCount,info->ppEnabledExtensionNames,VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)?4UL:0)|(timeline?2UL:0);
    const char *extra[]={VK_KHR_MAINTENANCE_2_EXTENSION_NAME,VK_KHR_MULTIVIEW_EXTENSION_NAME,VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME};
    for(unsigned i=0;i<5;i++)if(has_extension(info->enabledExtensionCount,info->ppEnabledExtensionNames,extra[i]))enabled|=16UL<<i;
    if(imageless)enabled|=512;
    if(has_extension(info->enabledExtensionCount,info->ppEnabledExtensionNames,VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME))enabled|=1024;
    if(scalar)enabled|=2048;
    if(has_extension(info->enabledExtensionCount,info->ppEnabledExtensionNames,VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))enabled|=4096;
    if(has_extension(info->enabledExtensionCount,info->ppEnabledExtensionNames,VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME))enabled|=8192;
    if(feedback)enabled|=16384;
    if(has_extension(info->enabledExtensionCount,info->ppEnabledExtensionNames,VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME))enabled|=32768;
    if(divisor)enabled|=65536;if(zero_divisor)enabled|=131072;
    if((divisor || zero_divisor) && !(enabled&32768)){free_dispatch(d);return VK_ERROR_FEATURE_NOT_PRESENT;}
    if(feedback && !(enabled&8192)){free_dispatch(d);return VK_ERROR_FEATURE_NOT_PRESENT;}
    if(timeline && !(enabled&8)){free_dispatch(d);return VK_ERROR_FEATURE_NOT_PRESENT;}
    d->enabled=enabled;
    VkResult r=CALL(8,KEY(p),q->queueFamilyIndex,PTR(&d->key),wsi,feature_bits,enabled);if(r!=VK_SUCCESS){free_dispatch(d);return r;}
    r=CALL(57,d->key,PTR(&d->map_alignment));if(r!=VK_SUCCESS){checked(CALL(9,d->key));free_dispatch(d);return r;}*out=(VkDevice)d;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device,const VkAllocationCallbacks *allocator) {
    if(!device)return;Dispatch *d=D(device);checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(9,d->key));
    free_templates(d->templates);
    while(d->memories){Memory *m=d->memories;d->memories=m->next;if(m->mapped)munmap(m->base,m->length);free(m);}
    while(d->commands){Dispatch *c=d->commands;d->commands=c->next;free_dispatch(c);}if(d->queue)free_dispatch(d->queue);free_dispatch(d);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device,uint32_t family,uint32_t index,VkQueue *out) {
    Dispatch *d=D(device);pthread_mutex_lock(&d->lock);if(!d->queue){uint64_t key=0;checked(CALL(10,d->key,family,index,PTR(&key)));d->queue=dispatch(key,d);if(!d->queue)abort();}*out=(VkQueue)d->queue;pthread_mutex_unlock(&d->lock);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice d,const VkBufferCreateInfo *info,const VkAllocationCallbacks *allocator,VkBuffer *out) {CHECK(info,VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);ALLOCATOR(allocator);if(info->flags || info->sharingMode!=VK_SHARING_MODE_EXCLUSIVE)return VK_ERROR_FEATURE_NOT_PRESENT;return CALL(11,KEY(d),info->size,info->usage,PTR(out));}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks *allocator) {if(b)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(12,KEY(d),(uint64_t)b));}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice d,VkBuffer b,VkMemoryRequirements *out) {checked(CALL(13,KEY(d),(uint64_t)b,PTR(out)));}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *allocator,VkDeviceMemory *out) {
    CHECK(info,VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);ALLOCATOR(allocator);Memory *r=calloc(1,sizeof(*r));if(!r)return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkResult v=CALL(14,KEY(device),info->allocationSize,info->memoryTypeIndex,PTR(&r->key));if(v!=VK_SUCCESS){free(r);return v;}r->size=info->allocationSize;
    Dispatch *d=D(device);pthread_mutex_lock(&d->lock);r->next=d->memories;d->memories=r;pthread_mutex_unlock(&d->lock);*out=(VkDeviceMemory)r->key;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device,VkDeviceMemory mem,const VkAllocationCallbacks *allocator) {
    if(!mem)return;checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(15,KEY(device),(uint64_t)mem));Dispatch *d=D(device);pthread_mutex_lock(&d->lock);
    for(Memory **p=&d->memories;*p;p=&(*p)->next)if((*p)->key==(uint64_t)mem){Memory *r=*p;*p=r->next;if(r->mapped)munmap(r->base,r->length);free(r);break;}pthread_mutex_unlock(&d->lock);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice d,VkBuffer b,VkDeviceMemory mem,VkDeviceSize offset) {return CALL(16,KEY(d),(uint64_t)b,(uint64_t)mem,offset);}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device,VkDeviceMemory mem,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void **out) {
    if(flags)return VK_ERROR_FEATURE_NOT_PRESENT;Dispatch *d=D(device);pthread_mutex_lock(&d->lock);Memory *r=memory(d,mem);
    if(!r || r->mapped || offset>=r->size){pthread_mutex_unlock(&d->lock);return VK_ERROR_MEMORY_MAP_FAILED;}uint64_t n=size==VK_WHOLE_SIZE?r->size-offset:size;pthread_mutex_unlock(&d->lock);
    if(!n || n>64UL*1024*1024)return VK_ERROR_MEMORY_MAP_FAILED;
    uint64_t alignment=d->map_alignment;if(!alignment || alignment>1048576)return VK_ERROR_MEMORY_MAP_FAILED;
    // Align (pData-offset), as Vulkan requires, even for unaligned offsets.
    size_t allocation=(size_t)(n+alignment-1);void *base=mmap(NULL,allocation,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(base==MAP_FAILED)return VK_ERROR_OUT_OF_HOST_MEMORY;
    size_t padding=(size_t)((offset%alignment+alignment-(uintptr_t)base%alignment)%alignment);void *p=(char *)base+padding;
    VkResult v=CALL(17,KEY(d),(uint64_t)mem,offset,n,PTR(p));if(v!=VK_SUCCESS){munmap(base,allocation);return v;}
    pthread_mutex_lock(&d->lock);r->base=base;r->mapped=p;r->length=allocation;pthread_mutex_unlock(&d->lock);*out=p;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device,VkDeviceMemory mem) {checked(CALL(18,KEY(device),(uint64_t)mem));Dispatch *d=D(device);pthread_mutex_lock(&d->lock);Memory *r=memory(d,mem);if(r && r->mapped){munmap(r->base,r->length);r->base=NULL;r->mapped=NULL;r->length=0;}pthread_mutex_unlock(&d->lock);}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice d,uint32_t count,const VkMappedMemoryRange *ranges) {for(uint32_t i=0;i<count;i++){CHECK(&ranges[i],VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);VkResult r=CALL(19,KEY(d),(uint64_t)ranges[i].memory,ranges[i].offset,ranges[i].size);if(r!=VK_SUCCESS)return r;}return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice d,uint32_t count,const VkMappedMemoryRange *ranges) {for(uint32_t i=0;i<count;i++){CHECK(&ranges[i],VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);VkResult r=CALL(20,KEY(d),(uint64_t)ranges[i].memory,ranges[i].offset,ranges[i].size);if(r!=VK_SUCCESS)return r;}return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice d,const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *allocator,VkShaderModule *out) {CHECK(info,VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);ALLOCATOR(allocator);if(info->flags)return VK_ERROR_FEATURE_NOT_PRESENT;return CALL(21,KEY(d),PTR(info->pCode),info->codeSize,PTR(out));}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice d,VkShaderModule shader,const VkAllocationCallbacks *allocator) {if(shader)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(22,KEY(d),(uint64_t)shader));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice d,const VkDescriptorSetLayoutCreateInfo *info,const VkAllocationCallbacks *allocator,VkDescriptorSetLayout *out) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;ALLOCATOR(allocator);
    if(info->flags || info->bindingCount>64)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkDescriptorSetLayoutBindingFlagsCreateInfo *flags=info->pNext;
    if(flags){
        if(!VALID(flags,VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO) || (flags->bindingCount && flags->bindingCount!=info->bindingCount))return VK_ERROR_FEATURE_NOT_PRESENT;
        // Zero binding flags are valid without descriptor indexing features.
        for(uint32_t i=0;i<flags->bindingCount;i++)if(flags->pBindingFlags[i])return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    uint64_t data[256];for(uint32_t i=0;i<info->bindingCount;i++){const VkDescriptorSetLayoutBinding *b=&info->pBindings[i];if(b->pImmutableSamplers)return VK_ERROR_FEATURE_NOT_PRESENT;data[i*4]=b->binding;data[i*4+1]=b->descriptorType;data[i*4+2]=b->descriptorCount;data[i*4+3]=b->stageFlags;}
    return CALL(23,KEY(d),PTR(data),info->bindingCount,PTR(out));
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice d,VkDescriptorSetLayout layout,const VkAllocationCallbacks *allocator) {if(layout)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(24,KEY(d),(uint64_t)layout));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice d,const VkPipelineLayoutCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipelineLayout *out) {
    CHECK(info,VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);ALLOCATOR(allocator);if(info->flags || info->pushConstantRangeCount>8)return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t ranges[24];for(uint32_t i=0;i<info->pushConstantRangeCount;i++){ranges[i*3]=info->pPushConstantRanges[i].stageFlags;ranges[i*3+1]=info->pPushConstantRanges[i].offset;ranges[i*3+2]=info->pPushConstantRanges[i].size;}
    return CALL(25,KEY(d),PTR(info->pSetLayouts),info->setLayoutCount,PTR(ranges),info->pushConstantRangeCount,PTR(out));
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice d,VkPipelineLayout layout,const VkAllocationCallbacks *allocator) {if(layout)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(26,KEY(d),(uint64_t)layout));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out) {
    ALLOCATOR(allocator);if(count!=1)return VK_ERROR_FEATURE_NOT_PRESENT;CHECK(info,VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);CHECK(&info->stage,VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    if(info->flags || info->stage.flags || info->stage.stage!=VK_SHADER_STAGE_COMPUTE_BIT || info->basePipelineHandle)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkSpecializationInfo *s=info->stage.pSpecializationInfo;uint64_t entries[192];
    if(s){if(s->mapEntryCount>64 || s->dataSize>65536 || (s->mapEntryCount && !s->pMapEntries) || (s->dataSize && !s->pData))return VK_ERROR_FEATURE_NOT_PRESENT;
        for(uint32_t i=0;i<s->mapEntryCount;i++){entries[i*3]=s->pMapEntries[i].constantID;entries[i*3+1]=s->pMapEntries[i].offset;entries[i*3+2]=s->pMapEntries[i].size;}}
    return CALL(27,KEY(d),(uint64_t)info->layout,(uint64_t)info->stage.module,PTR(info->stage.pName),PTR(out),PTR(entries),s?s->mapEntryCount:0,s?PTR(s->pData):0,s?s->dataSize:0,(uint64_t)cache);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice d,VkPipeline pipeline,const VkAllocationCallbacks *allocator) {if(pipeline)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(28,KEY(d),(uint64_t)pipeline));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice d,const VkDescriptorPoolCreateInfo *info,const VkAllocationCallbacks *allocator,VkDescriptorPool *out) {
    CHECK(info,VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);ALLOCATOR(allocator);if(info->poolSizeCount>64)return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t sizes[128];for(uint32_t i=0;i<info->poolSizeCount;i++){sizes[i*2]=info->pPoolSizes[i].type;sizes[i*2+1]=info->pPoolSizes[i].descriptorCount;}return CALL(29,KEY(d),PTR(sizes),info->poolSizeCount,info->maxSets,info->flags,PTR(out));
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice d,VkDescriptorPool pool,const VkAllocationCallbacks *allocator) {if(pool)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(30,KEY(d),(uint64_t)pool));}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice d,const VkDescriptorSetAllocateInfo *info,VkDescriptorSet *out) {CHECK(info,VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);return CALL(31,KEY(d),(uint64_t)info->descriptorPool,PTR(info->pSetLayouts),info->descriptorSetCount,PTR(out));}
VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice d,VkDescriptorPool pool,uint32_t count,const VkDescriptorSet *sets) {return CALL(32,KEY(d),(uint64_t)pool,PTR(sets),count);}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice d,uint32_t count,const VkWriteDescriptorSet *writes,uint32_t copies,const VkCopyDescriptorSet *copy) {
    (void)copy;if(copies)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    for(uint32_t j=0;j<count;j++){const VkWriteDescriptorSet *w=&writes[j];if(!VALID(w,VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET) || !w->descriptorCount || w->descriptorCount>64)checked(VK_ERROR_FEATURE_NOT_PRESENT);
        uint64_t buffers[192];
        if(w->descriptorType>=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && w->descriptorType<=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC){
            if(!w->pBufferInfo)checked(VK_ERROR_FEATURE_NOT_PRESENT);
            for(uint32_t i=0;i<w->descriptorCount;i++){buffers[i*3]=(uint64_t)w->pBufferInfo[i].buffer;buffers[i*3+1]=w->pBufferInfo[i].offset;buffers[i*3+2]=w->pBufferInfo[i].range;}
        }else if(w->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || w->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){
            if(!w->pTexelBufferView)checked(VK_ERROR_FEATURE_NOT_PRESENT);
            for(uint32_t i=0;i<w->descriptorCount;i++){buffers[i*3]=(uint64_t)w->pTexelBufferView[i];buffers[i*3+1]=0;buffers[i*3+2]=0;}
        }else if(w->descriptorType<=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){
            if(!w->pImageInfo)checked(VK_ERROR_FEATURE_NOT_PRESENT);
            for(uint32_t i=0;i<w->descriptorCount;i++){buffers[i*3]=w->descriptorType>=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE?0:(uint64_t)w->pImageInfo[i].sampler;buffers[i*3+1]=w->descriptorType==VK_DESCRIPTOR_TYPE_SAMPLER?0:(uint64_t)w->pImageInfo[i].imageView;buffers[i*3+2]=w->descriptorType==VK_DESCRIPTOR_TYPE_SAMPLER?0:w->pImageInfo[i].imageLayout;}
        }else checked(VK_ERROR_FEATURE_NOT_PRESENT);
        checked(CALL(33,KEY(d),(uint64_t)w->dstSet,w->dstBinding,w->dstArrayElement,w->descriptorCount,w->descriptorType,PTR(buffers)));
    }
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice d,const VkCommandPoolCreateInfo *info,const VkAllocationCallbacks *allocator,VkCommandPool *out) {CHECK(info,VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);ALLOCATOR(allocator);return CALL(34,KEY(d),info->queueFamilyIndex,info->flags,PTR(out));}
static void unlink_commands(Dispatch *d,uint64_t pool,Dispatch *specific) {
    pthread_mutex_lock(&d->lock);for(Dispatch **p=&d->commands;*p;){Dispatch *c=*p;if((specific && c==specific) || (!specific && c->parent==pool)){*p=c->next;free_dispatch(c);}else p=&c->next;}pthread_mutex_unlock(&d->lock);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice d,VkCommandPool pool,const VkAllocationCallbacks *allocator) {if(pool){checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(35,KEY(d),(uint64_t)pool));unlink_commands(D(d),(uint64_t)pool,NULL);}}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device,const VkCommandBufferAllocateInfo *info,VkCommandBuffer *out) {
    CHECK(info,VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);if(info->level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY || info->commandBufferCount>32)return VK_ERROR_FEATURE_NOT_PRESENT;
    Dispatch *d=D(device);for(uint32_t i=0;i<info->commandBufferCount;i++){Dispatch *c=dispatch(0,d);VkResult r=c?CALL(36,d->key,(uint64_t)info->commandPool,PTR(&c->key)):VK_ERROR_OUT_OF_HOST_MEMORY;
        if(r!=VK_SUCCESS){if(c)free_dispatch(c);for(uint32_t j=0;j<i;j++){checked(CALL(37,d->key,(uint64_t)info->commandPool,KEY(out[j])));unlink_commands(d,0,D(out[j]));}return r;}
        c->parent=(uint64_t)info->commandPool;pthread_mutex_lock(&d->lock);c->next=d->commands;d->commands=c;pthread_mutex_unlock(&d->lock);out[i]=(VkCommandBuffer)c;
    }return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice d,VkCommandPool pool,uint32_t count,const VkCommandBuffer *commands) {for(uint32_t i=0;i<count;i++){checked(CALL(37,KEY(d),(uint64_t)pool,KEY(commands[i])));unlink_commands(D(d),0,D(commands[i]));}}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer c,const VkCommandBufferBeginInfo *info) {CHECK(info,VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);if(info->pInheritanceInfo)return VK_ERROR_FEATURE_NOT_PRESENT;return CALL(38,DEVKEY(c),KEY(c),info->flags);}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer c) {return CALL(39,DEVKEY(c),KEY(c));}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer c,VkCommandBufferResetFlags flags) {return CALL(40,DEVKEY(c),KEY(c),flags);}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice d,VkCommandPool pool,VkCommandPoolResetFlags flags) {return CALL(53,KEY(d),(uint64_t)pool,flags);}
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer c,VkPipelineBindPoint point,VkPipeline p) {if(point!=VK_PIPELINE_BIND_POINT_COMPUTE && point!=VK_PIPELINE_BIND_POINT_GRAPHICS)checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(41,DEVKEY(c),KEY(c),(uint64_t)p,point));}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer c,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,const VkDescriptorSet *sets,uint32_t offsets,const uint32_t *offset) {if((point!=VK_PIPELINE_BIND_POINT_COMPUTE && point!=VK_PIPELINE_BIND_POINT_GRAPHICS) || offsets>64)checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(42,DEVKEY(c),KEY(c),(uint64_t)layout,first,PTR(sets),count,point==VK_PIPELINE_BIND_POINT_GRAPHICS,PTR(offset),offsets));}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer c,uint32_t x,uint32_t y,uint32_t z) {checked(CALL(43,DEVKEY(c),KEY(c),x,y,z));}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t count,const VkMemoryBarrier *barriers,uint32_t buffers,const VkBufferMemoryBarrier *buffer,uint32_t images,const VkImageMemoryBarrier *image) {
    if(flags&~VK_DEPENDENCY_BY_REGION_BIT || images>64)checked(VK_ERROR_FEATURE_NOT_PRESENT);VkAccessFlags sa=0,da=0;uint64_t data[64][10];
    for(uint32_t i=0;i<images;i++){const VkImageMemoryBarrier *b=&image[i];if(!VALID(b,VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER) || b->srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED || b->dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED || (b->subresourceRange.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT && (!b->subresourceRange.aspectMask || (b->subresourceRange.aspectMask&~(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT)))))checked(VK_ERROR_FEATURE_NOT_PRESENT);data[i][0]=(uint64_t)b->image;data[i][1]=b->srcAccessMask;data[i][2]=b->dstAccessMask;data[i][3]=b->oldLayout;data[i][4]=b->newLayout;data[i][5]=b->subresourceRange.aspectMask;data[i][6]=b->subresourceRange.baseMipLevel;data[i][7]=b->subresourceRange.levelCount;data[i][8]=b->subresourceRange.baseArrayLayer;data[i][9]=b->subresourceRange.layerCount;}
    for(uint32_t i=0;i<count;i++){if(!VALID(&barriers[i],VK_STRUCTURE_TYPE_MEMORY_BARRIER))checked(VK_ERROR_FEATURE_NOT_PRESENT);sa|=barriers[i].srcAccessMask;da|=barriers[i].dstAccessMask;}
    for(uint32_t i=0;i<buffers;i++){if(!VALID(&buffer[i],VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER) || buffer[i].srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED || buffer[i].dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED)checked(VK_ERROR_FEATURE_NOT_PRESENT);sa|=buffer[i].srcAccessMask;da|=buffer[i].dstAccessMask;}
    checked(CALL(44,DEVKEY(c),KEY(c),src,dst,sa,da,PTR(data),images,10,flags));
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer c,VkBuffer src,VkBuffer dst,uint32_t count,const VkBufferCopy *regions) {if(count>64)checked(VK_ERROR_FEATURE_NOT_PRESENT);uint64_t data[192];for(uint32_t i=0;i<count;i++){data[i*3]=regions[i].srcOffset;data[i*3+1]=regions[i].dstOffset;data[i*3+2]=regions[i].size;}checked(CALL(45,DEVKEY(c),KEY(c),(uint64_t)src,(uint64_t)dst,PTR(data),count));}
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize size,uint32_t value) {checked(CALL(46,DEVKEY(c),KEY(c),(uint64_t)buffer,offset,size,value));}
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer c,VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *values) {if(!stages || (stages & ~(VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT)))checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(47,DEVKEY(c),KEY(c),(uint64_t)layout,offset,size,PTR(values),stages));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice d,const VkFenceCreateInfo *info,const VkAllocationCallbacks *allocator,VkFence *out) {CHECK(info,VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);ALLOCATOR(allocator);return CALL(48,KEY(d),info->flags,PTR(out));}
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice d,VkFence f,const VkAllocationCallbacks *allocator) {if(f)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(49,KEY(d),(uint64_t)f));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice d,const VkSemaphoreCreateInfo *info,const VkAllocationCallbacks *allocator,VkSemaphore *out) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO || info->flags)return VK_ERROR_FEATURE_NOT_PRESENT;ALLOCATOR(allocator);
    const VkSemaphoreTypeCreateInfo *type=info->pNext;
    if(type && (!VALID(type,VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) || type->semaphoreType>VK_SEMAPHORE_TYPE_TIMELINE ||
       (type->semaphoreType==VK_SEMAPHORE_TYPE_TIMELINE && !(D(d)->enabled&2)) || (type->semaphoreType==VK_SEMAPHORE_TYPE_BINARY && type->initialValue)))return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(54,KEY(d),PTR(out),type?type->semaphoreType:0,type?type->initialValue:0);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice d,VkSemaphore s,const VkAllocationCallbacks *allocator) {if(s)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(55,KEY(d),(uint64_t)s));}
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice d,uint32_t count,const VkFence *fences) {for(uint32_t i=0;i<count;i++){VkResult r=CALL(50,KEY(d),(uint64_t)fences[i]);if(r!=VK_SUCCESS)return r;}return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice d,VkFence f) {return CALL(51,KEY(d),(uint64_t)f);}
static uint64_t now_ns(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000UL+(uint64_t)t.tv_nsec;}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice d,uint32_t count,const VkFence *fences,VkBool32 all,uint64_t timeout) {
    if(!count || count>64)return VK_ERROR_FEATURE_NOT_PRESENT;uint64_t began=now_ns();
    for(;;){unsigned ready=0;for(uint32_t i=0;i<count;i++){VkResult r=vkGetFenceStatus(d,fences[i]);if(r==VK_SUCCESS)ready++;else if(r!=VK_NOT_READY)return r;}
        if(ready==(all?count:1) || (!all && ready))return VK_SUCCESS;
        uint64_t elapsed=now_ns()-began;if(timeout!=UINT64_MAX && elapsed>=timeout)return VK_TIMEOUT;
        struct timespec sleep={0,(long)(timeout==UINT64_MAX || timeout-elapsed>1000000?1000000:timeout-elapsed)};nanosleep(&sleep,NULL);
    }
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue,uint32_t count,const VkSubmitInfo *submits,VkFence fence) {
    if(count>64)return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t records[64][9],legacy[64][7],commands[64],waits[64],signals[64],wait_values[64]={0},signal_values[64]={0};uint32_t stages[64],nc=0,nw=0,ns=0;int timeline=0;
    for(uint32_t i=0;i<count;i++){const VkSubmitInfo *s=&submits[i];if(s->sType!=VK_STRUCTURE_TYPE_SUBMIT_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;
        const VkTimelineSemaphoreSubmitInfo *t=s->pNext;
        if(t){
            if(!VALID(t,VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) || !(DEVICE(queue)->enabled&2) ||
               (t->waitSemaphoreValueCount && t->waitSemaphoreValueCount!=s->waitSemaphoreCount) ||
               (t->signalSemaphoreValueCount && t->signalSemaphoreValueCount!=s->signalSemaphoreCount))return VK_ERROR_FEATURE_NOT_PRESENT;
            timeline=1;
        }
        if(s->waitSemaphoreCount>64-nw || s->signalSemaphoreCount>64-ns || s->commandBufferCount>64-nc)return VK_ERROR_FEATURE_NOT_PRESENT;
        records[i][0]=PTR(waits+nw);records[i][1]=s->waitSemaphoreCount;records[i][2]=PTR(stages+nw);records[i][3]=PTR(commands+nc);records[i][4]=s->commandBufferCount;records[i][5]=PTR(signals+ns);records[i][6]=s->signalSemaphoreCount;
        records[i][7]=PTR(wait_values+nw);records[i][8]=PTR(signal_values+ns);
        for(uint32_t j=0;j<s->waitSemaphoreCount;j++){waits[nw]=(uint64_t)s->pWaitSemaphores[j];if(t && t->waitSemaphoreValueCount)wait_values[nw]=t->pWaitSemaphoreValues[j];stages[nw++]=s->pWaitDstStageMask[j];}
        for(uint32_t j=0;j<s->commandBufferCount;j++)commands[nc++]=KEY(s->pCommandBuffers[j]);
        for(uint32_t j=0;j<s->signalSemaphoreCount;j++){if(t && t->signalSemaphoreValueCount)signal_values[ns]=t->pSignalSemaphoreValues[j];signals[ns++]=(uint64_t)s->pSignalSemaphores[j];}
        memcpy(legacy[i],records[i],sizeof(legacy[i]));
    }
    if(!timeline)return CALL(56,DEVKEY(queue),KEY(queue),PTR(legacy),count,(uint64_t)fence);
    for(;;){
        VkResult r=CALL(120,DEVKEY(queue),KEY(queue),PTR(records),count,(uint64_t)fence);
        if(r!=VK_NOT_READY)return r;
        struct timespec sleep={0,1000000};nanosleep(&sleep,NULL);
    }
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue q) {VkFence fence;VkFenceCreateInfo f={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkDevice d=(VkDevice)DEVICE(q);VkResult r=vkCreateFence(d,&f,NULL,&fence);if(r!=VK_SUCCESS)return r;r=vkQueueSubmit(q,0,NULL,fence);if(r==VK_SUCCESS)r=vkWaitForFences(d,1,&fence,VK_TRUE,UINT64_MAX);vkDestroyFence(d,fence,NULL);return r;}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice d) {VkQueue q;vkGetDeviceQueue(d,D(d)->family,0,&q);return vkQueueWaitIdle(q);}


VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice d,const VkPipelineCacheCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipelineCache *out) {
    CHECK(info,VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);ALLOCATOR(allocator);
    if(info->flags || info->initialDataSize>64UL*1024*1024)return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(112,KEY(d),PTR(out),PTR(info->pInitialData),info->initialDataSize);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice d,VkPipelineCache cache,const VkAllocationCallbacks *allocator) {
    if(cache)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(113,KEY(d),(uint64_t)cache));
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice d,VkDescriptorPool pool,VkDescriptorPoolResetFlags flags) {return CALL(129,KEY(d),(uint64_t)pool,flags);}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice d,VkPipelineCache cache,size_t *size,void *data) {return CALL(130,KEY(d),(uint64_t)cache,PTR(size),PTR(data));}
VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(VkDevice d,VkPipelineCache dest,uint32_t count,const VkPipelineCache *sources) {if(count>64)return VK_ERROR_FEATURE_NOT_PRESENT;return CALL(131,KEY(d),(uint64_t)dest,count,PTR(sources));}

#include "graphics.inc"
#include "queries.inc"
#include "zink.inc"
#include "templates.inc"
#include "wayland.inc"
#include "wsi.inc"

static PFN_vkVoidFunction find(const char *name) {
    if(!name)return NULL;
#include "entrypoints.inc"
    return NULL;
}
static int global_name(const char *name) {return name && (!strcmp(name,"vkCreateInstance") || !strcmp(name,"vkGetInstanceProcAddr") || !strcmp(name,"vkEnumerateInstanceVersion") || !strcmp(name,"vkEnumerateInstanceExtensionProperties") || !strcmp(name,"vkEnumerateInstanceLayerProperties"));}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char *name) {if(!instance && !global_name(name))return NULL;return find(name);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char *name) {if(!device || !name || global_name(name) || !strcmp(name,"vkDestroyInstance") || !strcmp(name,"vkCreateDevice") || !strncmp(name,"vkEnumeratePhysicalDevice",25) || !strncmp(name,"vkGetPhysicalDevice",19) || !strcmp(name,"vkEnumerateDeviceExtensionProperties") || !strcmp(name,"vkEnumerateDeviceLayerProperties"))return NULL;return find(name);}
__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance,const char *name) {(void)instance;return find(name);}
__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance,const char *name) {(void)instance;return find(name);}
__attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *version) {if(*version>5)*version=5;return VK_SUCCESS;}
