// SPDX-License-Identifier: GPL-3.0-or-later
// KHR query wrappers preserve the selected 1.0 profile. Guest pNext chains stay
// in the guest; unrecognized output structures are ignored, as Vulkan requires.
// The instance supports 1.1 queries; devices retain the selected 1.0 profile.
// No external import/export, sparse binding or additional device feature is
// claimed merely because an instance-level query exists.
static void query_output(const void *p,VkStructureType type) {
    if(!p || ((const VkBaseOutStructure *)p)->sType!=type)checked(VK_ERROR_FEATURE_NOT_PRESENT);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice p,VkPhysicalDeviceFeatures2 *out) {
    query_output(out,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    vkGetPhysicalDeviceFeatures(p,&out->features);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice p,VkPhysicalDeviceFeatures2 *out) {vkGetPhysicalDeviceFeatures2(p,out);}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice p,VkPhysicalDeviceProperties2 *out) {
    query_output(out,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    vkGetPhysicalDeviceProperties(p,&out->properties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice p,VkPhysicalDeviceProperties2 *out) {vkGetPhysicalDeviceProperties2(p,out);}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties2 *out) {
    query_output(out,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2);
    vkGetPhysicalDeviceMemoryProperties(p,&out->memoryProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties2 *out) {vkGetPhysicalDeviceMemoryProperties2(p,out);}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice p,VkFormat format,VkFormatProperties2 *out) {
    query_output(out,VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2);
    vkGetPhysicalDeviceFormatProperties(p,format,&out->formatProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice p,VkFormat format,VkFormatProperties2 *out) {vkGetPhysicalDeviceFormatProperties2(p,format,out);}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice p,const VkPhysicalDeviceImageFormatInfo2 *info,VkImageFormatProperties2 *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2))return VK_ERROR_FORMAT_NOT_SUPPORTED;
    query_output(out,VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);
    return vkGetPhysicalDeviceImageFormatProperties(p,info->format,info->type,info->tiling,info->usage,info->flags,&out->imageFormatProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice p,const VkPhysicalDeviceImageFormatInfo2 *info,VkImageFormatProperties2 *out) {return vkGetPhysicalDeviceImageFormatProperties2(p,info,out);}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice p,uint32_t *count,VkQueueFamilyProperties2 *out) {
    uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(p,&n,NULL);
    if(!out){*count=n;return;}
    if(*count<n)n=*count;
    if(!n){*count=0;return;}
    VkQueueFamilyProperties *properties=calloc(n,sizeof(*properties));
    if(!properties)checked(VK_ERROR_OUT_OF_HOST_MEMORY);
    vkGetPhysicalDeviceQueueFamilyProperties(p,&n,properties);
    for(uint32_t i=0;i<n;i++){
        query_output(out+i,VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2);
        out[i].queueFamilyProperties=properties[i];
    }
    free(properties);*count=n;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice p,uint32_t *count,VkQueueFamilyProperties2 *out) {vkGetPhysicalDeviceQueueFamilyProperties2(p,count,out);}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice p,const VkPhysicalDeviceSparseImageFormatInfo2 *info,uint32_t *count,VkSparseImageFormatProperties2 *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_INFO_2))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    (void)out;vkGetPhysicalDeviceSparseImageFormatProperties(p,info->format,info->type,info->samples,info->usage,info->tiling,count,NULL);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice p,const VkPhysicalDeviceSparseImageFormatInfo2 *info,uint32_t *count,VkSparseImageFormatProperties2 *out) {vkGetPhysicalDeviceSparseImageFormatProperties2(p,info,count,out);}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice d,const VkBufferMemoryRequirementsInfo2 *info,VkMemoryRequirements2 *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    query_output(out,VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2);
    vkGetBufferMemoryRequirements(d,info->buffer,&out->memoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(VkDevice d,const VkBufferMemoryRequirementsInfo2 *info,VkMemoryRequirements2 *out) {vkGetBufferMemoryRequirements2(d,info,out);}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice d,const VkImageMemoryRequirementsInfo2 *info,VkMemoryRequirements2 *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    query_output(out,VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2);
    vkGetImageMemoryRequirements(d,info->image,&out->memoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2KHR(VkDevice d,const VkImageMemoryRequirementsInfo2 *info,VkMemoryRequirements2 *out) {vkGetImageMemoryRequirements2(d,info,out);}
VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2(VkDevice d,const VkImageSparseMemoryRequirementsInfo2 *info,uint32_t *count,VkSparseImageMemoryRequirements2 *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    // Sparse image creation/binding is unavailable, so every valid profile image
    // is nonsparse and has no sparse memory requirements.
    (void)d;(void)out;*count=0;
}
VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2KHR(VkDevice d,const VkImageSparseMemoryRequirementsInfo2 *info,uint32_t *count,VkSparseImageMemoryRequirements2 *out) {vkGetImageSparseMemoryRequirements2(d,info,count,out);}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(VkInstance instance,uint32_t *count,VkPhysicalDeviceGroupProperties *out) {
    if(!out){uint32_t n=0;VkResult r=vkEnumeratePhysicalDevices(instance,&n,NULL);*count=n;return r;}
    if(!*count)return VK_INCOMPLETE;
    query_output(out,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES);
    uint32_t n=1;VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkResult r=vkEnumeratePhysicalDevices(instance,&n,&physical);if(r!=VK_SUCCESS)return r;
    out->physicalDeviceCount=1;out->physicalDevices[0]=physical;out->subsetAllocation=VK_FALSE;*count=1;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice p,const VkPhysicalDeviceExternalBufferInfo *info,VkExternalBufferProperties *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    query_output(out,VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES);
    (void)p;out->externalMemoryProperties=(VkExternalMemoryProperties){0};
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice p,const VkPhysicalDeviceExternalFenceInfo *info,VkExternalFenceProperties *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    query_output(out,VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES);
    (void)p;out->exportFromImportedHandleTypes=0;out->compatibleHandleTypes=0;out->externalFenceFeatures=0;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice p,const VkPhysicalDeviceExternalSemaphoreInfo *info,VkExternalSemaphoreProperties *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    query_output(out,VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES);
    (void)p;out->exportFromImportedHandleTypes=0;out->compatibleHandleTypes=0;out->externalSemaphoreFeatures=0;
}
