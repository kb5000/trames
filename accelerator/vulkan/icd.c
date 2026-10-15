// SPDX-License-Identifier: GPL-3.0-or-later
// Standard Linux ICD frontend. Only fixed-size, little-endian wire records
// cross NativeCall; every returned function pointer is ordinary guest ELF code.
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include <stdint.h>
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
typedef struct dispatch {
    uintptr_t magic; uint64_t key;
    struct dispatch *device, *physical, *queue, *commands, *next;
    uint64_t parent,map_alignment; uint32_t family;
    pthread_mutex_t lock; Memory *memories;
} Dispatch;
#define D(h) ((Dispatch *)(h))
#define KEY(h) (D(h)->key)
#define DEVICE(h) (D(h)->device)
#define DEVKEY(h) (DEVICE(h)->key)
static VkResult call(unsigned op, uint64_t a[16]) {
    if (getauxval(0x54520008UL)!=0x10006UL) return VK_ERROR_INCOMPATIBLE_DRIVER;
    return (VkResult)(int32_t)trames_vulkan_call(op,a,128).value;
}
#define CALL(op, ...) call(op,(uint64_t[16]){__VA_ARGS__})
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
static void checked(VkResult r) { if (r!=VK_SUCCESS) { fprintf(stderr,"Trames experimental Vulkan ICD: unsupported/invalid call (%d)\n",r); abort(); } }
static Dispatch *dispatch(uint64_t key, Dispatch *device) { Dispatch *d=calloc(1,sizeof(*d)); if (d) { set_loader_magic_value(d); d->key=key;d->device=device;pthread_mutex_init(&d->lock,NULL); } return d; }
static void free_dispatch(Dispatch *d) {pthread_mutex_destroy(&d->lock);free(d);}
static Memory *memory(Dispatch *d,VkDeviceMemory key) {for (Memory *m=d->memories;m;m=m->next) if(m->key==(uint64_t)key)return m;return NULL;}

static uint64_t now_ns(void);
static int display_info(uint64_t key,uint64_t out[3]) { return CALL(key?80:79,key?key:PTR(out),key?PTR(out):0)==VK_SUCCESS && out[0]; }
static VkResult display_extensions(const char *layer,uint32_t *count,VkExtensionProperties *out,int device) {
    if(layer)return VK_ERROR_LAYER_NOT_PRESENT;uint64_t info[3]={0};uint32_t n=display_info(0,info)?(device?1:2):0;
    const char *names[]={device?VK_KHR_SWAPCHAIN_EXTENSION_NAME:VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_DISPLAY_EXTENSION_NAME};
    if(!out){*count=n;return VK_SUCCESS;}uint32_t capacity=*count;*count=capacity<n?capacity:n;
    for(uint32_t i=0;i<*count;i++){memset(out+i,0,sizeof(*out));strcpy(out[i].extensionName,names[i]);out[i].specVersion=1;}
    return capacity<n?VK_INCOMPLETE:VK_SUCCESS;
}
static int extensions(uint32_t count,const char *const *names,int device) {
    unsigned bits=0;for(uint32_t i=0;i<count;i++){
        unsigned bit=0;if(!strcmp(names[i],device?VK_KHR_SWAPCHAIN_EXTENSION_NAME:VK_KHR_SURFACE_EXTENSION_NAME))bit=1;
        else if(!device && !strcmp(names[i],VK_KHR_DISPLAY_EXTENSION_NAME))bit=2;
        if(!bit || bits&bit)return -1;bits|=bit;
    }
    return bits!=0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *info,const VkAllocationCallbacks *allocator,VkInstance *out) {
    CHECK(info,VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);ALLOCATOR(allocator);
    int wsi=extensions(info->enabledExtensionCount,info->ppEnabledExtensionNames,0);if(wsi<0 || info->enabledLayerCount)return VK_ERROR_EXTENSION_NOT_PRESENT;
    if (info->flags || (info->pApplicationInfo && (!VALID(info->pApplicationInfo,VK_STRUCTURE_TYPE_APPLICATION_INFO) || info->pApplicationInfo->apiVersion>VK_API_VERSION_1_0))) return VK_ERROR_INCOMPATIBLE_DRIVER;
    Dispatch *d=dispatch(0,NULL);if(!d)return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkResult r=CALL(1,PTR(&d->key),wsi);if(r!=VK_SUCCESS){free_dispatch(d);return r;}*out=(VkInstance)d;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance,const VkAllocationCallbacks *allocator) { if (!instance)return;checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(2,KEY(instance)));if(D(instance)->physical)free_dispatch(D(instance)->physical);free_dispatch(D(instance)); }
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *out) {*out=VK_API_VERSION_1_0;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *layer,uint32_t *count,VkExtensionProperties *out) {return display_extensions(layer,count,out,0);}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *count,VkLayerProperties *out) {(void)out;*count=0;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physical,const char *layer,uint32_t *count,VkExtensionProperties *out) {(void)physical;return display_extensions(layer,count,out,1);}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physical,uint32_t *count,VkLayerProperties *out) {(void)physical;(void)out;*count=0;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance,uint32_t *count,VkPhysicalDevice *out) {
    uint32_t n=1;uint64_t key=0;VkResult r=CALL(3,KEY(instance),PTR(&n),PTR(&key));if(r!=VK_SUCCESS)return r;
    if (!out){*count=1;return VK_SUCCESS;}if (!*count)return VK_INCOMPLETE;
    Dispatch *i=D(instance);if(!i->physical)i->physical=dispatch(key,NULL);if(!i->physical)return VK_ERROR_OUT_OF_HOST_MEMORY;
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
    CHECK(info,VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);ALLOCATOR(allocator);
    int wsi=extensions(info->enabledExtensionCount,info->ppEnabledExtensionNames,1);if(info->flags || info->queueCreateInfoCount!=1 || wsi<0)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkDeviceQueueCreateInfo *q=info->pQueueCreateInfos;CHECK(q,VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    if(q->flags || q->queueCount!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
    if(info->pEnabledFeatures){const VkBool32 *v=(const VkBool32 *)info->pEnabledFeatures;for(size_t j=0;j<sizeof(VkPhysicalDeviceFeatures)/sizeof(VkBool32);j++)if(v[j])return VK_ERROR_FEATURE_NOT_PRESENT;}
    Dispatch *d=dispatch(0,NULL);if(!d)return VK_ERROR_OUT_OF_HOST_MEMORY;d->device=d;d->family=q->queueFamilyIndex;
    VkResult r=CALL(8,KEY(p),q->queueFamilyIndex,PTR(&d->key),wsi);if(r!=VK_SUCCESS){free_dispatch(d);return r;}
    r=CALL(57,d->key,PTR(&d->map_alignment));if(r!=VK_SUCCESS){checked(CALL(9,d->key));free_dispatch(d);return r;}*out=(VkDevice)d;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device,const VkAllocationCallbacks *allocator) {
    if(!device)return;Dispatch *d=D(device);checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(9,d->key));
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
    CHECK(info,VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);ALLOCATOR(allocator);if(info->flags || info->bindingCount>64)return VK_ERROR_FEATURE_NOT_PRESENT;
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
    ALLOCATOR(allocator);if(cache || count!=1)return VK_ERROR_FEATURE_NOT_PRESENT;CHECK(info,VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);CHECK(&info->stage,VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    if(info->flags || info->stage.flags || info->stage.stage!=VK_SHADER_STAGE_COMPUTE_BIT || info->basePipelineHandle)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkSpecializationInfo *s=info->stage.pSpecializationInfo;uint64_t entries[192];
    if(s){if(s->mapEntryCount>64 || s->dataSize>65536 || (s->mapEntryCount && !s->pMapEntries) || (s->dataSize && !s->pData))return VK_ERROR_FEATURE_NOT_PRESENT;
        for(uint32_t i=0;i<s->mapEntryCount;i++){entries[i*3]=s->pMapEntries[i].constantID;entries[i*3+1]=s->pMapEntries[i].offset;entries[i*3+2]=s->pMapEntries[i].size;}}
    return CALL(27,KEY(d),(uint64_t)info->layout,(uint64_t)info->stage.module,PTR(info->stage.pName),PTR(out),PTR(entries),s?s->mapEntryCount:0,s?PTR(s->pData):0,s?s->dataSize:0);
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
    for(uint32_t j=0;j<count;j++){const VkWriteDescriptorSet *w=&writes[j];if(!VALID(w,VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET) || w->descriptorCount>64 || !w->pBufferInfo)checked(VK_ERROR_FEATURE_NOT_PRESENT);
        uint64_t buffers[192];for(uint32_t i=0;i<w->descriptorCount;i++){buffers[i*3]=(uint64_t)w->pBufferInfo[i].buffer;buffers[i*3+1]=w->pBufferInfo[i].offset;buffers[i*3+2]=w->pBufferInfo[i].range;}
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
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer c,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,const VkDescriptorSet *sets,uint32_t offsets,const uint32_t *offset) {(void)offset;if(point!=VK_PIPELINE_BIND_POINT_COMPUTE || offsets)checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(42,DEVKEY(c),KEY(c),(uint64_t)layout,first,PTR(sets),count));}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer c,uint32_t x,uint32_t y,uint32_t z) {checked(CALL(43,DEVKEY(c),KEY(c),x,y,z));}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t count,const VkMemoryBarrier *barriers,uint32_t buffers,const VkBufferMemoryBarrier *buffer,uint32_t images,const VkImageMemoryBarrier *image) {
    if(flags || images>64)checked(VK_ERROR_FEATURE_NOT_PRESENT);VkAccessFlags sa=0,da=0;uint64_t data[64][5];
    for(uint32_t i=0;i<images;i++){const VkImageMemoryBarrier *b=&image[i];if(!VALID(b,VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER) || b->srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED || b->dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED || b->subresourceRange.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT || b->subresourceRange.baseMipLevel || b->subresourceRange.levelCount!=1 || b->subresourceRange.baseArrayLayer || b->subresourceRange.layerCount!=1)checked(VK_ERROR_FEATURE_NOT_PRESENT);data[i][0]=(uint64_t)b->image;data[i][1]=b->srcAccessMask;data[i][2]=b->dstAccessMask;data[i][3]=b->oldLayout;data[i][4]=b->newLayout;}
    for(uint32_t i=0;i<count;i++){if(!VALID(&barriers[i],VK_STRUCTURE_TYPE_MEMORY_BARRIER))checked(VK_ERROR_FEATURE_NOT_PRESENT);sa|=barriers[i].srcAccessMask;da|=barriers[i].dstAccessMask;}
    for(uint32_t i=0;i<buffers;i++){if(!VALID(&buffer[i],VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER) || buffer[i].srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED || buffer[i].dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED)checked(VK_ERROR_FEATURE_NOT_PRESENT);sa|=buffer[i].srcAccessMask;da|=buffer[i].dstAccessMask;}
    checked(CALL(44,DEVKEY(c),KEY(c),src,dst,sa,da,PTR(data),images));
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer c,VkBuffer src,VkBuffer dst,uint32_t count,const VkBufferCopy *regions) {if(count>64)checked(VK_ERROR_FEATURE_NOT_PRESENT);uint64_t data[192];for(uint32_t i=0;i<count;i++){data[i*3]=regions[i].srcOffset;data[i*3+1]=regions[i].dstOffset;data[i*3+2]=regions[i].size;}checked(CALL(45,DEVKEY(c),KEY(c),(uint64_t)src,(uint64_t)dst,PTR(data),count));}
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize size,uint32_t value) {checked(CALL(46,DEVKEY(c),KEY(c),(uint64_t)buffer,offset,size,value));}
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer c,VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *values) {if(stages!=VK_SHADER_STAGE_COMPUTE_BIT)checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(47,DEVKEY(c),KEY(c),(uint64_t)layout,offset,size,PTR(values)));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice d,const VkFenceCreateInfo *info,const VkAllocationCallbacks *allocator,VkFence *out) {CHECK(info,VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);ALLOCATOR(allocator);return CALL(48,KEY(d),info->flags,PTR(out));}
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice d,VkFence f,const VkAllocationCallbacks *allocator) {if(f)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(49,KEY(d),(uint64_t)f));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice d,const VkSemaphoreCreateInfo *info,const VkAllocationCallbacks *allocator,VkSemaphore *out) {CHECK(info,VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);ALLOCATOR(allocator);if(info->flags)return VK_ERROR_FEATURE_NOT_PRESENT;return CALL(54,KEY(d),PTR(out));}
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
    uint64_t records[64][7],commands[64],waits[64],signals[64];uint32_t stages[64],nc=0,nw=0,ns=0;
    for(uint32_t i=0;i<count;i++){const VkSubmitInfo *s=&submits[i];CHECK(s,VK_STRUCTURE_TYPE_SUBMIT_INFO);
        if(s->waitSemaphoreCount>64-nw || s->signalSemaphoreCount>64-ns || s->commandBufferCount>64-nc)return VK_ERROR_FEATURE_NOT_PRESENT;
        records[i][0]=PTR(waits+nw);records[i][1]=s->waitSemaphoreCount;records[i][2]=PTR(stages+nw);records[i][3]=PTR(commands+nc);records[i][4]=s->commandBufferCount;records[i][5]=PTR(signals+ns);records[i][6]=s->signalSemaphoreCount;
        for(uint32_t j=0;j<s->waitSemaphoreCount;j++){waits[nw]=(uint64_t)s->pWaitSemaphores[j];stages[nw++]=s->pWaitDstStageMask[j];}
        for(uint32_t j=0;j<s->commandBufferCount;j++)commands[nc++]=KEY(s->pCommandBuffers[j]);
        for(uint32_t j=0;j<s->signalSemaphoreCount;j++)signals[ns++]=(uint64_t)s->pSignalSemaphores[j];
    }
    return CALL(56,DEVKEY(queue),KEY(queue),PTR(records),count,(uint64_t)fence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue q) {VkFence fence;VkFenceCreateInfo f={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkDevice d=(VkDevice)DEVICE(q);VkResult r=vkCreateFence(d,&f,NULL,&fence);if(r!=VK_SUCCESS)return r;r=vkQueueSubmit(q,0,NULL,fence);if(r==VK_SUCCESS)r=vkWaitForFences(d,1,&fence,VK_TRUE,UINT64_MAX);vkDestroyFence(d,fence,NULL);return r;}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice d) {VkQueue q;vkGetDeviceQueue(d,D(d)->family,0,&q);return vkQueueWaitIdle(q);}

#include "graphics.inc"
#include "wsi.inc"

static PFN_vkVoidFunction find(const char *name) {
    if(!name)return NULL;
#include "entrypoints.inc"
    return NULL;
}
static int global_name(const char *name) {return name && (!strcmp(name,"vkCreateInstance") || !strcmp(name,"vkGetInstanceProcAddr") || !strcmp(name,"vkEnumerateInstanceVersion") || !strcmp(name,"vkEnumerateInstanceExtensionProperties") || !strcmp(name,"vkEnumerateInstanceLayerProperties"));}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char *name) {if(!instance && !global_name(name))return NULL;return find(name);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char *name) {if(!device || !name || global_name(name) || !strcmp(name,"vkDestroyInstance") || !strcmp(name,"vkCreateDevice") || !strcmp(name,"vkEnumeratePhysicalDevices") || !strncmp(name,"vkGetPhysicalDevice",19) || !strcmp(name,"vkEnumerateDeviceExtensionProperties") || !strcmp(name,"vkEnumerateDeviceLayerProperties"))return NULL;return find(name);}
__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance,const char *name) {(void)instance;return find(name);}
__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance,const char *name) {(void)instance;return find(name);}
__attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *version) {if(*version>5)*version=5;return VK_SUCCESS;}
