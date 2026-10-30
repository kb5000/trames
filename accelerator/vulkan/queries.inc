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
    VkBaseOutStructure *next=out->pNext;
    for(unsigned i=0;next && i<64;i++,next=next->pNext){
        if(compute_features_output(D(p),next))continue;
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT){
            VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *v=(void *)next;
            v->vertexAttributeInstanceRateDivisor=(D(p)->capabilities[0]&2048)!=0;
            v->vertexAttributeInstanceRateZeroDivisor=(D(p)->capabilities[0]&4096)!=0;
        }
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT){
            VkPhysicalDeviceTransformFeedbackFeaturesEXT *v=(void *)next;
            v->transformFeedback=(D(p)->capabilities[0]&1024)!=0;v->geometryStreams=VK_FALSE;
        }
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR && (D(p)->capabilities[0]&512)){
            uint32_t values[16];checked(CALL(133,KEY(p),PTR(values)));
            memcpy(&((VkPhysicalDevicePortabilitySubsetFeaturesKHR *)next)->constantAlphaColorBlendFactors,values,60);
        }
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES)
            ((VkPhysicalDeviceScalarBlockLayoutFeatures *)next)->scalarBlockLayout=(D(p)->capabilities[0]&256)!=0;
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES)
            ((VkPhysicalDeviceImagelessFramebufferFeatures *)next)->imagelessFramebuffer=(D(p)->capabilities[0]&128)!=0;

        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES)
            ((VkPhysicalDeviceTimelineSemaphoreFeatures *)next)->timelineSemaphore=(D(p)->capabilities[0]&2)!=0;
    }
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice p,VkPhysicalDeviceFeatures2 *out) {vkGetPhysicalDeviceFeatures2(p,out);}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice p,VkPhysicalDeviceProperties2 *out) {
    query_output(out,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    vkGetPhysicalDeviceProperties(p,&out->properties);
    VkBaseOutStructure *next=out->pNext;
    for(unsigned i=0;next && i<64;i++,next=next->pNext){
        compute_properties_output(p,next);
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT)
            checked(CALL(150,KEY(p),PTR(&((VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)next)->maxVertexAttribDivisor)));
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT){
            uint64_t values[12];checked(CALL(137,KEY(p),PTR(values)));
            VkPhysicalDeviceTransformFeedbackPropertiesEXT *v=(void *)next;
            v->maxTransformFeedbackStreams=values[2];v->maxTransformFeedbackBuffers=values[3];v->maxTransformFeedbackBufferSize=values[4];
            v->maxTransformFeedbackStreamDataSize=values[5];v->maxTransformFeedbackBufferDataSize=values[6];v->maxTransformFeedbackBufferDataStride=values[7];
            v->transformFeedbackQueries=values[8];v->transformFeedbackStreamsLinesTriangles=values[9];v->transformFeedbackRasterizationStreamSelect=values[10];v->transformFeedbackDraw=values[11];
        }
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_PROPERTIES_KHR && (D(p)->capabilities[0]&512)){
            uint32_t values[16];checked(CALL(133,KEY(p),PTR(values)));
            ((VkPhysicalDevicePortabilitySubsetPropertiesKHR *)next)->minVertexInputBindingStrideAlignment=values[15];
        }

        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES && (D(p)->capabilities[0]&8))
            checked(CALL(126,KEY(p),PTR(&((VkPhysicalDevicePointClippingProperties *)next)->pointClippingBehavior)));
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES && (D(p)->capabilities[0]&2))
            ((VkPhysicalDeviceTimelineSemaphoreProperties *)next)->maxTimelineSemaphoreValueDifference=D(p)->capabilities[1];
        if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES && (D(p)->capabilities[0]&4)){
            VkPhysicalDeviceDriverProperties *driver=(VkPhysicalDeviceDriverProperties *)next;
            unsigned char bytes[520];checked(CALL(121,KEY(p),PTR(bytes)));
            memcpy(&driver->driverID,bytes,520);
            // Identity/workarounds come from the native driver; the bridge has
            // not passed Vulkan conformance and cannot inherit its certificate.
            driver->conformanceVersion=(VkConformanceVersion){0};
        }
    }
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
    if(!info || info->sType!=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2)return VK_ERROR_FORMAT_NOT_SUPPORTED;
    const VkImageFormatListCreateInfo *list;const VkImageStencilUsageCreateInfo *stencil;if(!image_formats(info->pNext,&list,&stencil))return VK_ERROR_FORMAT_NOT_SUPPORTED;
    uint64_t extra[3]={list?PTR(list->pViewFormats):0,list?list->viewFormatCount:0,stencil?stencil->stencilUsage:0};
    query_output(out,VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);
    VkSamplerYcbcrConversionImageFormatProperties *conversion=NULL;
    for(VkBaseOutStructure *n=out->pNext;n;n=n->pNext)if(n->sType==VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES)conversion=(void *)n;
    return CALL(59,KEY(p),info->format,info->type,info->tiling,info->usage,info->flags,PTR(&out->imageFormatProperties),stencil?PTR(extra):extra[0],stencil?UINT64_MAX:extra[1],conversion?PTR(&conversion->combinedImageSamplerDescriptorCount):0);
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
    VkMemoryDedicatedRequirements *dedicated=NULL;
    for(VkBaseOutStructure *n=out->pNext;n;n=n->pNext)if(n->sType==VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS)dedicated=(void *)n;
    checked(CALL(239,KEY(d),1,(uint64_t)info->buffer,PTR(&out->memoryRequirements),dedicated?PTR(&dedicated->prefersDedicatedAllocation):0));
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(VkDevice d,const VkBufferMemoryRequirementsInfo2 *info,VkMemoryRequirements2 *out) {vkGetBufferMemoryRequirements2(d,info,out);}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice d,const VkImageMemoryRequirementsInfo2 *info,VkMemoryRequirements2 *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2))checked(VK_ERROR_FEATURE_NOT_PRESENT);
    query_output(out,VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2);
    VkMemoryDedicatedRequirements *dedicated=NULL;
    for(VkBaseOutStructure *n=out->pNext;n;n=n->pNext)if(n->sType==VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS)dedicated=(void *)n;
    checked(CALL(239,KEY(d),2,(uint64_t)info->image,PTR(&out->memoryRequirements),dedicated?PTR(&dedicated->prefersDedicatedAllocation):0));
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
