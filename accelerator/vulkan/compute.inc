// SPDX-License-Identifier: GPL-3.0-or-later
// Capability metadata is paired with the host from one compute wire schema.
struct compute_extension { const char *name; uint64_t bit; };
struct compute_feature { VkStructureType type; size_t offset; unsigned count,first; uint64_t extension; uint32_t promoted; };
struct compute_aggregate { VkStructureType type; size_t offset; unsigned count; uint64_t seen; int mapping[12]; };
struct compute_property { VkStructureType type; size_t offset,bytes; uint64_t extension; };
#include "compute-schema.inc"
static uint64_t compute_extension(const char *name) {
    for(unsigned i=0;i<sizeof(compute_extensions)/sizeof(*compute_extensions);i++)if(!strcmp(name,compute_extensions[i].name))return compute_extensions[i].bit;
    return 0;
}
static int compute_feature(const VkBaseInStructure *p,uint64_t *seen,uint64_t *features) {
    for(unsigned i=0;i<sizeof(compute_aggregates)/sizeof(*compute_aggregates);i++){
        const struct compute_aggregate *a=compute_aggregates+i;if(p->sType!=a->type)continue;
        if(*seen&a->seen)return -1;*seen|=a->seen;
        for(unsigned j=0;j<a->count;j++){
            VkBool32 value;memcpy(&value,(const char *)p+a->offset+j*4,4);
            if(value>VK_TRUE || (value && a->mapping[j]<0))return -1;
            if(value)*features|=1UL<<a->mapping[j];
        }return 1;
    }
    for(unsigned i=0;i<sizeof(compute_features)/sizeof(*compute_features);i++){
        const struct compute_feature *f=compute_features+i;if(p->sType!=f->type)continue;
        if(*seen&(1UL<<i))return -1;*seen|=1UL<<i;
        for(unsigned j=0;j<f->count;j++){
            VkBool32 value;memcpy(&value,(const char *)p+f->offset+j*4,4);
            if(value>VK_TRUE)return -1;if(value)*features|=1UL<<(f->first+j);
        }
        return 1;
    }
    return 0;
}
static int compute_features_output(Dispatch *physical,VkBaseOutStructure *p) {
    for(unsigned i=0;i<sizeof(compute_aggregates)/sizeof(*compute_aggregates);i++){
        const struct compute_aggregate *a=compute_aggregates+i;if(p->sType!=a->type)continue;
        for(unsigned j=0;j<a->count;j++){
            VkBool32 value=a->mapping[j]<0?0:(physical->compute_caps[1]>>a->mapping[j])&1;
            memcpy((char *)p+a->offset+j*4,&value,4);
        }return 1;
    }
    for(unsigned i=0;i<sizeof(compute_features)/sizeof(*compute_features);i++){
        const struct compute_feature *f=compute_features+i;if(p->sType!=f->type)continue;
        for(unsigned j=0;j<f->count;j++){
            VkBool32 value=(physical->compute_caps[1]>>(f->first+j))&1;
            memcpy((char *)p+f->offset+j*4,&value,4);
        }
        return 1;
    }
    return 0;
}
static void compute_properties_output(VkPhysicalDevice physical,VkBaseOutStructure *p) {
    for(unsigned i=0;i<sizeof(compute_properties)/sizeof(*compute_properties);i++){
        const struct compute_property *q=compute_properties+i;if(p->sType!=q->type)continue;
        void *data=(char *)p+q->offset;memset(data,0,q->bytes);
        if((!q->extension && D(physical)->compute_caps[2]>=VK_API_VERSION_1_1) || (D(physical)->compute_caps[0]&q->extension))checked(CALL(207,KEY(physical),p->sType,PTR(data)));
        return;
    }
}
static VkResult compute_device(Dispatch *physical,const VkDeviceCreateInfo *info,uint64_t features,uint64_t *extensions) {
    *extensions=0;
    for(uint32_t i=0;i<info->enabledExtensionCount;i++)*extensions|=compute_extension(info->ppEnabledExtensionNames[i]);
    for(unsigned i=0;i<sizeof(compute_features)/sizeof(*compute_features);i++){
        const struct compute_feature *f=compute_features+i;
        if(!(features&(((1UL<<f->count)-1)<<f->first)))continue;
        if(!(*extensions&f->extension) && physical->compute_caps[2]<f->promoted)return VK_ERROR_FEATURE_NOT_PRESENT;
        *extensions|=f->extension;
    }
    if(*extensions&((1UL<<1)|(1UL<<2)))*extensions|=1;
    if((*extensions&~physical->compute_caps[0]) || (features&~physical->compute_caps[1]))return VK_ERROR_FEATURE_NOT_PRESENT;
    return VK_SUCCESS;
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(VkDevice d,const VkBufferDeviceAddressInfo *info) {
    if(!VALID(info,VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    VkDeviceAddress address=0;checked(CALL(208,KEY(d),(uint64_t)info->buffer,PTR(&address)));return address;
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressKHR(VkDevice d,const VkBufferDeviceAddressInfo *info) {return vkGetBufferDeviceAddress(d,info);}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice d,uint32_t count,const VkBindBufferMemoryInfo *info) {
    for(uint32_t i=0;i<count;i++){
        CHECK(info+i,VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO);VkResult r=vkBindBufferMemory(d,info[i].buffer,info[i].memory,info[i].memoryOffset);if(r!=VK_SUCCESS)return r;
    }return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(VkDevice d,uint32_t count,const VkBindBufferMemoryInfo *info) {return vkBindBufferMemory2(d,count,info);}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice d,uint32_t count,const VkBindImageMemoryInfo *info) {
    for(uint32_t i=0;i<count;i++){
        CHECK(info+i,VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO);VkResult r=vkBindImageMemory(d,info[i].image,info[i].memory,info[i].memoryOffset);if(r!=VK_SUCCESS)return r;
    }return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2KHR(VkDevice d,uint32_t count,const VkBindImageMemoryInfo *info) {return vkBindImageMemory2(d,count,info);}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice d,const VkDeviceQueueInfo2 *info,VkQueue *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2) || info->flags)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    vkGetDeviceQueue(d,info->queueFamilyIndex,info->queueIndex,out);
}
